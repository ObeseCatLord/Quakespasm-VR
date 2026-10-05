#!/usr/bin/env python3
"""Peril wheel qualification for the integrated debug binary. Never auto-run."""
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
    binary = output / "peril-wheel-policy"
    subprocess.run([str(x) for x in cc + flags + [ROOT / "tests/peril_weapon_wheel_fixture.c"] + native +
        ["-Wl,--gc-sections", "-Wl,--wrap=COM_LoadFile", "-Wl,--wrap=Sys_FileRead"] + libs +
        ["-lm", "-o", binary]], check=True)
    subprocess.run([str(binary), str(args.base)], check=True)
    env = dict(os.environ, QSVR_GAME_BASE=str(args.base), QSVR_BINARY=str(args.binary))
    log = output / "peril-wheel-native-runtime.log"
    with log.open("w") as stream:
        subprocess.run(["bash", str(ROOT / "tests/peril_weapon_wheel_runtime.sh")],
                       env=env, stdout=stream, stderr=subprocess.STDOUT, check=True)
    text = log.read_text()
    markers = ["PERIL_WHEEL_NATIVE_BASE_PASS", "PERIL_WHEEL_NATIVE_UPGRADES_PASS",
               "PERIL_WHEEL_NATIVE_GATES_PASS", "PERIL_WHEEL_NATIVE_OVERRIDES_PASS", "PERIL_WHEEL_NATIVE_PASS"]
    assert all(marker in text for marker in markers), log
    print("\n".join(line for line in text.splitlines() if line.startswith("PERIL_WHEEL_")))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, default=Path(os.environ.get("QSVR_GAME_BASE",
                        str(Path.home() / "Windows/Games/quakespasm_straight"))))
    parser.add_argument("--binary", type=Path, default=ROOT / "build-debug/vkquake")
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    args.base = args.base.resolve()
    args.binary = args.binary.resolve()
    if args.output_dir:
        check(args, args.output_dir.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="peril-wheel-") as directory:
            check(args, Path(directory))


if __name__ == "__main__":
    main()
