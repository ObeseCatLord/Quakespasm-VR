#!/usr/bin/env python3
"""Run the focused presentation checks after implementation and native build.

Reuses strict Debug graph flags/libraries, with this checkout's headers and
sources. Never rebuilds or writes into the shared graph. Geometry/policy seams
in individual fixtures remain explicit; this is not a headset or QC outcome test.
"""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from run_anisotropy_native import compile_recipe, DEFAULT_GRAPH
from run_csqc_entity_native import native_link_recipe

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_GRAPH)
    parser.add_argument("--pak", type=Path, required=True,
                        help="External licensed official rerelease pak0.pak")
    args = parser.parse_args()
    graph = args.build_dir.resolve(strict=True)
    watched = (graph / "build.ninja", graph / "compile_commands.json")
    before = [hashlib.sha256(path.read_bytes()).digest() for path in watched]
    with tempfile.TemporaryDirectory(prefix="qsvr-presentation-native-") as directory:
        temporary = Path(directory)
        environment = os.environ.copy()
        environment["TMPDIR"] = directory
        compiled = {}

        def compile_source(owner, source, name):
            if name not in compiled:
                output = temporary / (name + ".o")
                recipe, cwd = compile_recipe(graph, owner, source, output)
                subprocess.run(recipe, cwd=cwd, env=environment, check=True)
                compiled[name] = output
            return compiled[name]

        for fixture, owner, dependencies in (
            ("vr_index_controller_pose_fixture", "vr_locomotion.c", ("vr_locomotion", "mathlib")),
            ("vr_coherent_gesture_fixture", "vr_input.c", ("vr_locomotion", "mathlib")),
            ("vr_held_offset_scale_fixture", "vr_weapon_calibration.c", ()),
            ("render_acquire_fixture", "r_passes.c", ()),
        ):
            objects = [compile_source(owner, ROOT / "tests" / (fixture + ".c"), fixture)]
            objects += [compile_source(name + ".c", ROOT / "Quake" / (name + ".c"), name)
                        for name in dependencies]
            binary = temporary / fixture
            recipe = [arg for arg in native_link_recipe(graph, binary) if not arg.endswith(".o")]
            recipe[1:1] = [str(path) for path in objects]
            recipe.append("-Wl,--gc-sections")
            subprocess.run(recipe, cwd=graph, env=environment, check=True)
            subprocess.run([str(binary)], env=environment, check=True, timeout=60)

        for fixture in ("wheel_presentation_slot_parity_fixture.py",
                        "vr_crosshair_surface_fixture.py",
                        "graphics_dynamic_off_frozen_styles_fixture.py",
                        "graphics_particle_reload_menu_state_fixture.py",
                        "enhanced_provenance_fixture.py",
                        "md5_weapon_geometry_fixture.py"):
            command = [sys.executable, "-B", str(ROOT / "tests" / fixture)]
            if fixture == "md5_weapon_geometry_fixture.py":
                command += ["--pak", str(args.pak.resolve(strict=True))]
            subprocess.run(command, env=environment, check=True, timeout=120)
    after = [hashlib.sha256(path.read_bytes()).digest() for path in watched]
    if before != after:
        raise RuntimeError("shared build graph changed during focused checks")
    print("VR_PRESENTATION_NATIVE_PASSED strict_debug own_sources shared_graph_unchanged", flush=True)


if __name__ == "__main__":
    main()
