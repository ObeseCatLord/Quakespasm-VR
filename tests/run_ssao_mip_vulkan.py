#!/usr/bin/env python3
"""Qualify the stable upstream SSAO mip shaders with isolated GPU readbacks.

Requires --shader-root and a new private --output-dir. Compiles all production
mip wrappers; dispatches shared and renderer-supported subgroup/FP16 variants.
MSAA compilation is recorded separately from its unverified runtime slice.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess

REPO = Path(__file__).resolve().parents[1]
VARIANTS = {
    "shared-fp32": "ssao_mip_shared.comp",
    "shared-fp16": "ssao_mip_shared_fp16.comp",
    "subgroup-fp32": "ssao_mip.comp",
    "subgroup-fp16": "ssao_mip_fp16.comp",
}
MSAA = ("ssao_mip_shared_msaa.comp", "ssao_mip_shared_msaa_fp16.comp",
        "ssao_mip_msaa.comp", "ssao_mip_msaa_fp16.comp")


def git(root, *args):
    return subprocess.run(["git", "-C", str(root), *args], check=True,
                          text=True, stdout=subprocess.PIPE).stdout.strip()


def sources(root):
    """Hash the actual include closure, not a guessed shader mirror."""
    result = {}

    def visit(path):
        path = path.resolve(strict=True)
        if not path.is_relative_to(root):
            raise RuntimeError("shader include escaped the explicit root")
        key = str(path.relative_to(root))
        if key in result:
            return
        data = path.read_bytes()
        if re.search(rb"^(?:<<<<<<< |=======\s*$|>>>>>>> )", data, re.MULTILINE):
            raise RuntimeError("unresolved shader conflict: " + key)
        result[key] = hashlib.sha256(data).hexdigest()
        for include in re.findall(rb'^\s*#include\s+"([^"]+)"', data, re.MULTILINE):
            visit(path.parent / include.decode())

    for name in (*VARIANTS.values(), *MSAA):
        visit(root / name)
    return dict(sorted(result.items()))


def stable_reference(root):
    checkout = Path(git(root, "rev-parse", "--show-toplevel"))
    merge = Path(git(checkout, "rev-parse", "--git-path", "MERGE_HEAD"))
    if not merge.is_absolute():
        merge = checkout / merge
    if merge.exists() or git(checkout, "diff", "--name-only", "--diff-filter=U"):
        raise RuntimeError("reference merge is still in progress; qualification must wait")
    watched = [str(root.relative_to(checkout)), "Quake/r_ssao.c", "Quake/gl_rmisc.c", "Quake/gl_vidsdl.c"]
    if git(checkout, "status", "--porcelain", "--", *watched):
        raise RuntimeError("reference shaders/renderer are not committed and stable")
    return checkout, git(checkout, "rev-parse", "HEAD")


def execute(command, root, log, timeout):
    environment = os.environ.copy()
    environment["TMPDIR"] = str(root)
    result = subprocess.run(command, cwd=root, env=environment, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    log.write_text(result.stdout)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shader-root", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()
    root = args.shader_root.resolve(strict=True)
    if not (root / "ssao_mip.inc").is_file() and (root / "Shaders").is_dir():
        root = root / "Shaders"
    checkout, reference = stable_reference(root)
    before = sources(root)
    output = args.output_dir.resolve()
    if not output.is_relative_to(REPO):
        raise SystemExit("output directory must stay in this private checkout")
    output.mkdir(parents=True, exist_ok=False)
    compiler = shutil.which("glslc")
    if not compiler:
        raise SystemExit("glslc is required for production shader qualification")
    validator = shutil.which("spirv-val")
    manifest = {"reference_sha": reference, "shader_root": str(root), "source_sha256": before,
                "spirv_validation": bool(validator), "variants": {},
                "msaa_runtime": "unverified: reuse the renderer smoke in final integration"}
    binary = output / "ssao-mip-vulkan"
    try:
        cc = shlex.split(os.environ.get("CC", "clang"))
        compile_result = execute(cc + ["-std=gnu11", "-O0", "-g", "-Wall", "-Werror",
                                "-Wno-missing-field-initializers", "-Wno-misleading-indentation",
                                str(REPO / "tests/ssao_shared_mip_vulkan_fixture.c"),
                                "-lvulkan", "-lm", "-o", str(binary)],
                                 output, output / "fixture-build.log", args.timeout)
        compile_result.check_returncode()
        for name in (*VARIANTS.values(), *MSAA):
            spv = output / (name + ".spv")
            result = execute([compiler, "--target-env=vulkan1.1", "-I" + str(root),
                              str(root / name), "-o", str(spv)], output,
                             output / (name + ".compile.log"), args.timeout)
            result.check_returncode()
            if validator:
                validation = execute([validator, "--target-env", "vulkan1.1", str(spv)],
                                     output, output / (name + ".validation.log"), args.timeout)
                validation.check_returncode()
        print("SSAO_MIP_SHADER_BUILD_PASSED wrappers=8 spirv_validation=" + str(bool(validator)), flush=True)
        failures = []
        for variant, name in VARIANTS.items():
            result = execute([str(binary), str(output / (name + ".spv")), variant],
                             output, output / (variant + ".gpu.log"), args.timeout)
            manifest["variants"][variant] = {"exit": result.returncode,
                                            "status": "unsupported" if result.returncode == 77 else "passed" if result.returncode == 0 else "failed"}
            lines = result.stdout.splitlines()
            for line in lines:
                if line.startswith(("SSAO_MIP_DEVICE", "SSAO_MIP_VULKAN_PASSED", "SSAO_MIP_VULKAN_FAILED",
                                    "SSAO_MIP_MISMATCH", "SSAO_MIP_TAG_MISMATCH", "SSAO_MIP_EYE_ISOLATION_MISMATCH",
                                    "SSAO_SHARED_MIP_VULKAN_SKIPPED")):
                    print(line, flush=True)
            if result.returncode not in (0, 77):
                failures.append(variant)
            if result.returncode == 0 and not any(line == "SSAO_MIP_VULKAN_PASSED variant=" + variant + " cases=16 dispatches=64" for line in lines):
                raise RuntimeError("GPU process lacked its complete matrix witness: " + variant)
        if manifest["variants"]["shared-fp32"]["exit"] == 77:
            raise SystemExit(77)
        if failures:
            print("SSAO_MIP_QUALIFICATION_FAILED variants=" + ",".join(failures) + " reference=" + reference, flush=True)
            raise SystemExit(1)
        print("SSAO_MIP_QUALIFICATION_PASSED reference=" + reference, flush=True)
        print("SSAO_MIP_MSAA_RUNTIME_UNVERIFIED renderer_smoke_remains", flush=True)
    finally:
        manifest["sources_unchanged"] = sources(root) == before and git(checkout, "rev-parse", "HEAD") == reference
        (output / "qualification.json").write_text(json.dumps(manifest, indent=2) + "\n")
        if not manifest["sources_unchanged"]:
            raise RuntimeError("reference changed during qualification; results are not final")


if __name__ == "__main__":
    main()
