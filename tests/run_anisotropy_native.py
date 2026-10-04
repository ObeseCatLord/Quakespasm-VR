#!/usr/bin/env python3
"""Compile production mip/sampler witnesses from a prepared Meson graph.

No Ninja build, regeneration, shared object writes, or driver selection/reset.
The graph supplies flags and libraries; this checkout supplies all source.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

from run_csqc_entity_native import native_link_recipe, remove_depfile_args

REPO = Path(__file__).resolve().parents[1]
DEFAULT_GRAPH = Path("/home/obesecatlord/FastGames/qsvr-selected-final-debug-clang-20261004")


def compile_recipe(graph, owner, source, output):
    entries = json.loads((graph / "compile_commands.json").read_text())
    entry = next(item for item in entries if item["file"].endswith("/Quake/" + owner))
    args = remove_depfile_args(entry.get("arguments") or shlex.split(entry["command"]))
    if "-D_DEBUG" not in args or any(arg.startswith("-DNDEBUG") for arg in args):
        raise RuntimeError("requires an assertion-enabled Debug graph")
    if "-Werror" not in args:
        raise RuntimeError("requires the strict production compile graph")
    # The shared PCH embeds another checkout's headers. Compile our own headers
    # with the same defines/diagnostics instead of importing that source state.
    if "-include-pch" in args:
        index = args.index("-include-pch")
        del args[index:index + 2]
    original_root = (Path(entry["directory"]) / entry["file"]).resolve().parent.parent
    for index, arg in enumerate(args):
        if arg.startswith("-I"):
            include = (Path(entry["directory"]) / arg[2:]).resolve()
            if include.is_relative_to(original_root):
                args[index] = "-I" + str(REPO / include.relative_to(original_root))
    args[args.index("-c") + 1] = str(source)
    args[args.index("-o") + 1] = str(output)
    args.extend(("-ffunction-sections", "-fdata-sections"))
    return args, entry["directory"]


def build(graph, root):
    environment = os.environ.copy()
    environment["TMPDIR"] = str(root)

    def compile_source(owner, source, name):
        output = root / (name + ".o")
        args, directory = compile_recipe(graph, owner, source, output)
        subprocess.run(args, cwd=directory, env=environment, check=True)
        return output, args[0]

    # Diagnostic compile of the complete changed production translation unit.
    compile_source("gl_texmgr.c", REPO / "Quake/gl_texmgr.c", "gl_texmgr-production")
    samplers, _ = compile_source("gl_rmisc.c", REPO / "Quake/gl_rmisc.c", "gl_rmisc-production")
    print("ANISO_NATIVE_PRODUCTION_COMPILE_PASSED strict_debug own_checkout_headers", flush=True)
    for name, objects, wrappers in (
        ("texmgr_mip_native_fixture", (), ()),
        ("anisotropy_vulkan_fixture", (samplers,), ("vkCreateSampler", "vkUpdateDescriptorSets")),
    ):
        fixture, compiler = compile_source("gl_texmgr.c", REPO / "tests" / (name + ".c"), name)
        binary = root / name
        # Reuse the native builder's graph parser and exact production libraries.
        # No prebuilt engine objects are linked: the fixture includes its actual
        # production owner, and the GPU witness links our isolated sampler owner.
        link = native_link_recipe(graph, binary)
        link[0] = compiler
        link = [arg for arg in link if not arg.endswith(".o")]
        link[1:1] = [str(fixture), *(str(obj) for obj in objects)]
        link.append("-Wl,--gc-sections")
        link.extend("-Wl,--wrap=" + symbol for symbol in wrappers)
        subprocess.run(link, cwd=graph, env=environment, check=True)
    shader = root / "anisotropy_fixture.comp.spv"
    if shutil.which("glslangValidator"):
        command = ["glslangValidator", "-V", "--target-env", "vulkan1.1", "--quiet"]
    elif shutil.which("glslc"):
        command = ["glslc", "--target-env=vulkan1.1"]
    else:
        raise RuntimeError("requires glslangValidator or glslc")
    subprocess.run(command + [str(REPO / "tests/anisotropy_fixture.comp"), "-o", str(shader)],
                   cwd=root, env=environment, check=True)


def run(graph, root, timeout):
    watched = (graph / "build.ninja", graph / "compile_commands.json")
    before = [hashlib.sha256(path.read_bytes()).digest() for path in watched]
    try:
        build(graph, root)
        subprocess.run([str(root / "texmgr_mip_native_fixture")], cwd=root,
                       check=True, timeout=timeout)
        gpu = subprocess.run([str(root / "anisotropy_vulkan_fixture"),
                              str(root / "anisotropy_fixture.comp.spv")],
                             cwd=root, timeout=timeout)
        if gpu.returncode == 77:
            print("ANISO_NATIVE_GPU_UNVERIFIED exit=77", flush=True)
            return 77
        gpu.check_returncode()
        print("ANISO_NATIVE_PASSED production_upload_and_real_vulkan_readback", flush=True)
        return 0
    finally:
        after = [hashlib.sha256(path.read_bytes()).digest() for path in watched]
        if before != after:
            raise RuntimeError("shared graph changed during fixture checks")
        print("ANISO_NATIVE_SHARED_GRAPH_UNCHANGED", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_GRAPH)
    parser.add_argument("--artifact-root", type=Path)
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()
    graph = args.build_dir.resolve(strict=True)
    if args.artifact_root:
        root = args.artifact_root.resolve()
        if not root.is_relative_to(REPO):
            raise SystemExit("artifacts must stay inside this checkout")
        root.mkdir(parents=True, exist_ok=False)
        raise SystemExit(run(graph, root, args.timeout))
    with tempfile.TemporaryDirectory(prefix=".anisotropy-native-", dir=REPO) as temporary:
        raise SystemExit(run(graph, Path(temporary), args.timeout))


if __name__ == "__main__":
    main()
