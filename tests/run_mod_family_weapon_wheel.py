#!/usr/bin/env python3
"""Progs-family weapon wheel fixture against installed mods (read-only)."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def check(args, output):
    output.mkdir(parents=True, exist_ok=True)
    cc = shlex.split(os.environ.get("CC", "cc"))
    flags = ["-std=gnu11", "-DUSE_SDL3", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-sign-compare",
             "-Wno-missing-field-initializers", "-ffunction-sections", "-fdata-sections",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
             "-I" + str(ROOT / "Quake")]
    libs = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl3"], text=True))
    native = [ROOT / "Quake" / name for name in ("vr_weapon_calibration.c", "vr_weapon_schema.c",
              "vr_locomotion.c", "mathlib.c", "common.c", "sys_sdl.c", "strlcpy.c")]
    binary = output / "mod-family-wheel"
    subprocess.run([str(x) for x in cc + flags + [ROOT / "tests/mod_family_weapon_wheel_fixture.c"] + native +
        ["-Wl,--gc-sections", "-Wl,--wrap=COM_LoadFile", "-Wl,--wrap=Sys_FileRead"] + libs +
        ["-lm", "-o", binary]], check=True)
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
    subprocess.run([str(binary), str(args.base)], check=True, env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, default=Path(os.environ.get("QSVR_GAME_BASE",
                        str(Path.home() / "Windows/Games/quakespasm_straight"))))
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    args.base = args.base.resolve()
    if args.output_dir:
        check(args, args.output_dir.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="mod-family-wheel-") as directory:
            check(args, Path(directory))


if __name__ == "__main__":
    main()
