#!/usr/bin/env bash
set -euo pipefail

root=$(git -C "$(dirname "${BASH_SOURCE[0]}")/.." rev-parse --show-toplevel)
assets=${QSVR_TEST_ASSETS:-/home/obesecatlord/Windows/Games/quakespasm_straight}
scratch=$(mktemp -d "${TMPDIR:-/tmp}/enyo-melee-state.XXXXXX")
trap 'rm -rf -- "$scratch"' EXIT

# Read installed assets into a disposable test directory; never write the mod.
python3 - "$assets/enyo/pak0.pak" "$scratch/progs.dat" <<'PY'
import pathlib
import struct
import sys

pak = pathlib.Path(sys.argv[1]).read_bytes()
magic, directory, length = struct.unpack_from('<4sII', pak)
if magic != b'PACK' or length % 64 or directory + length > len(pak):
    raise SystemExit('invalid Enyo PAK directory')
for at in range(directory, directory + length, 64):
    name, offset, size = struct.unpack_from('<56sII', pak, at)
    if name.split(b'\0', 1)[0] == b'progs.dat':
        if offset + size > len(pak):
            raise SystemExit('invalid Enyo program extent')
        pathlib.Path(sys.argv[2]).write_bytes(pak[offset:offset + size])
        break
else:
    raise SystemExit('Enyo PAK has no progs.dat')
PY

cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-unused-function -Wno-sign-compare \
  -Wno-missing-field-initializers -ffunction-sections -fdata-sections \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -fno-omit-frame-pointer -I"$root/Quake" \
  "$root/tests/vr_enyo_melee_state_fixture.c" "$root/Quake/common.c" \
  -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lm \
  -o "$scratch/state-fixture"
ASAN_OPTIONS=detect_leaks=0 "$scratch/state-fixture" "$scratch/progs.dat"
