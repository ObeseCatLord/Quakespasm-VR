#!/usr/bin/env bash
# Isolated dedicated qualification; every installed asset is linked read-only.
set -euo pipefail
root=$(git -C "$(dirname "${BASH_SOURCE[0]}")/.." rev-parse --show-toplevel)
source=${QSVR_GAME_BASE:-${HOME}/Windows/Games/quakespasm_straight}
binary=${QSVR_BINARY:-$root/build-debug/vkquake}
game=$(mktemp -d "${TMPDIR:-/tmp}/peril-wheel-runtime.XXXXXX")
trap 'rm -rf -- "$game"' EXIT
[[ -x "$binary" && -f "$source/id1/pak0.pak" && -f "$source/peril3.0/pak2.pak" ]]
mkdir -p "$game/id1" "$game/peril3.0"
for file in "$source/id1"/pak*.pak; do ln -s -- "$file" "$game/id1/${file##*/}"; done
for file in "$source/peril3.0"/pak*.pak; do ln -s -- "$file" "$game/peril3.0/${file##*/}"; done
ln -s -- "$source/peril3.0/maps" "$game/peril3.0/maps"
ln -s -- "$source/peril3.0/vr_weapons.txt" "$game/peril3.0/vr_weapons.txt"
gdb -q -batch -x "$root/tests/peril_weapon_wheel_runtime.gdb" --args "$binary" \
  -basedir "$game" -game peril3.0 -dedicated 2 -port 0 \
  +sv_coop_autosave 0 +coop 1 +map start
