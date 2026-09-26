#!/usr/bin/env bash
set -euo pipefail
root=$(git -C "$(dirname "${BASH_SOURCE[0]}")/.." rev-parse --show-toplevel)
assets=${QSVR_TEST_ASSETS:-/home/obesecatlord/Windows/Games/quakespasm_straight}
scratch=$(mktemp -d "${TMPDIR:-/tmp}/alk-program.XXXXXX")
trap 'rm -rf -- "$scratch"' EXIT
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-unused-function -Wno-sign-compare \
  -Wno-missing-field-initializers -fsanitize=address,undefined \
  -fno-sanitize-recover=all -fno-omit-frame-pointer -I"$root/Quake" \
  "$root/tests/vr_alk_program_fixture.c" "$root/Quake/crc.c" \
  $(pkg-config --cflags --libs sdl3) -lm -o "$scratch/fixture"
for mod in alk limjam; do
  python3 - "$assets/$mod/pak0.pak" "$scratch/progs.dat" <<'PY'
import pathlib, struct, sys
pak = pathlib.Path(sys.argv[1]).read_bytes()
magic, directory, length = struct.unpack_from('<4sII', pak)
if magic != b'PACK' or length % 64 or directory + length > len(pak):
    raise SystemExit('invalid PAK directory')
for at in range(directory, directory + length, 64):
    name, offset, size = struct.unpack_from('<56sII', pak, at)
    if name.split(b'\0', 1)[0] == b'progs.dat':
        if offset + size > len(pak):
            raise SystemExit('invalid program extent')
        pathlib.Path(sys.argv[2]).write_bytes(pak[offset:offset + size])
        break
else:
    raise SystemExit('PAK has no progs.dat')
PY
  ASAN_OPTIONS=detect_leaks=0 "$scratch/fixture" "$scratch/progs.dat" "$mod"
done
