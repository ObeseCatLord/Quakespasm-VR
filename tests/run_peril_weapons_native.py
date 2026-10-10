#!/usr/bin/env python3
"""Isolated Peril calibration/split/native-QC regression; assets stay read-only."""
import argparse
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def run(args, **kwargs):
    subprocess.run([str(x) for x in args], check=True, **kwargs)


def member(base, name):
    """Match the contiguous numbered PAK override order used by the engine."""
    result = None
    i = 0
    while (base / f"pak{i}.pak").exists():
        data = (base / f"pak{i}.pak").read_bytes()
        magic, offset, length = struct.unpack_from("<4sII", data)
        assert magic == b"PACK" and length % 64 == 0
        for at in range(offset, offset + length, 64):
            path, start, size = struct.unpack_from("<56sII", data, at)
            if path.split(b"\0", 1)[0].decode() == name:
                result = data[start:start + size]
        i += 1
    assert result is not None, f"missing {name} in {base}"
    return result


def split_oracle(source, hand):
    """Independent direct-index byte-copy oracle, without production splitter."""
    skins, width, height, vertices, triangles, frames = struct.unpack_from("<6i", source, 48)
    assert (vertices, triangles, frames) == (292, 258, 9)
    uv = 84 + skins * (4 + width * height)
    faces = uv + vertices * 12
    poses = faces + triangles * 16
    frame_size = 28 + vertices * 4
    selected = list(range(136 * hand, 136 * (hand + 1))) + list(range(272 + hand * 10, 282 + hand * 10))
    mapping = {old: new for new, old in enumerate(selected)}
    kept = []
    for i in range(triangles):
        face = struct.unpack_from("<3i", source, faces + i * 16 + 4)
        admitted = [v in mapping for v in face]
        assert all(admitted) or not any(admitted), "triangle crosses hands"
        if all(admitted):
            kept.append((i, face))
    out = bytearray(source[:uv])
    struct.pack_into("<ii", out, 60, len(selected), len(kept))
    for v in selected:
        out.extend(source[uv + v * 12:uv + (v + 1) * 12])
    for i, face in kept:
        out.extend(source[faces + i * 16:faces + i * 16 + 4])
        out.extend(struct.pack("<3i", *(mapping[v] for v in face)))
    for i in range(frames):
        frame = source[poses + i * frame_size:poses + (i + 1) * frame_size]
        assert struct.unpack_from("<i", frame)[0] == 0
        header = bytearray(frame[:28])
        points = [frame[28 + v * 4:32 + v * 4] for v in selected]
        for axis in range(3):
            header[4 + axis] = min(v[axis] for v in points)
            header[8 + axis] = max(v[axis] for v in points)
        out.extend(header)
        for v in points:
            out.extend(v)
    assert len(out) == 74948
    assert zlib.crc32(out) == (0xBE17C930 if hand else 0x0CA1AA33)
    return out


def check(args, output):
    output.mkdir(parents=True, exist_ok=True)
    cc = shlex.split(os.environ.get("CC", "cc"))
    flags = ["-std=gnu11", "-DUSE_SDL3", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-sign-compare",
             "-Wno-missing-field-initializers", "-ffunction-sections", "-fdata-sections",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
             "-I" + str(ROOT / "Quake")]
    libs = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl3"], text=True))
    native = [ROOT / "Quake" / x for x in ("vr_weapon_calibration.c", "vr_weapon_schema.c",
              "vr_locomotion.c", "mathlib.c", "common.c", "sys_sdl.c", "strlcpy.c")]
    calibration = output / "peril-calibration"
    run(cc + flags + [ROOT / "tests/peril_weapon_calibration_fixture.c"] + native +
        ["-Wl,--gc-sections", "-Wl,--wrap=COM_LoadFile", "-Wl,--wrap=Sys_FileRead"] + libs +
        ["-lm", "-o", calibration])
    run([calibration, args.base])
    source = member(args.base / "peril3.0", "progs/v_nail.mdl")
    assert len(source) == 84020 and zlib.crc32(source) == 0x5EA01698
    paths = [output / n for n in ("v_nail.mdl", "left.mdl", "right.mdl")]
    for path, data in zip(paths, (source, split_oracle(source, 0), split_oracle(source, 1))):
        path.write_bytes(data)
    split = output / "peril-split"
    run(cc + flags + [ROOT / "tests/peril_mdl_split_fixture.c", "-o", split])
    run([split] + paths)
    env = dict(os.environ, QSVR_GAME_BASE=str(args.base), QSVR_BINARY=str(args.binary))
    with (output / "native-runtime.log").open("w") as log:
        run(["bash", ROOT / "tests/peril_akimbo_runtime.sh"], env=env, stdout=log, stderr=subprocess.STDOUT)
    text = (output / "native-runtime.log").read_text()
    assert all(marker in text for marker in ("PERIL_NATIVE_RUNTIME_PASS",
               "PERIL_NATIVE_BOUNDARIES_PASS", "PERIL_INPUT_LIFECYCLE_PASS"))
    print(text[text.index("PERIL_GATE_PASS"):].rstrip())
    print("PERIL_WEAPONS_NATIVE_PASS calibration, independent split oracle, actual installed QC shots")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, default=Path(os.environ.get("QSVR_GAME_BASE", "/home/obesecatlord/Windows/Games/quakespasm_straight")))
    parser.add_argument("--binary", type=Path, default=ROOT / "build-debug/vkquake")
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    args.base = args.base.resolve()
    args.binary = args.binary.resolve()
    if args.output_dir:
        check(args, args.output_dir.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="peril-weapons-") as temporary:
            check(args, Path(temporary))


if __name__ == "__main__":
    main()
