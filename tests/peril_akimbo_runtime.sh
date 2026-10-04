#!/usr/bin/env bash
# Read-only installed Peril assets; isolated native dedicated process, no XR.
set -euo pipefail
root=$(git -C "$(dirname "${BASH_SOURCE[0]}")/.." rev-parse --show-toplevel)
source=${QSVR_GAME_BASE:-/home/obesecatlord/Windows/Games/quakespasm_straight}
binary=${QSVR_BINARY:-$root/build-debug/vkquake}
script=${1:-$root/tests/peril_akimbo_runtime.gdb}
game=$(mktemp -d "${TMPDIR:-/tmp}/peril-native-runtime.XXXXXX")
trap 'rm -rf -- "$game"' EXIT
[[ -x "$binary" && -f "$script" && -f "$source/id1/pak0.pak" && -f "$source/peril3.0/pak2.pak" && -f "$source/peril3.0/maps/start.bsp" ]]
mkdir -p "$game/id1" "$game/peril3.0"
for file in "$source/id1"/pak*.pak; do ln -s -- "$file" "$game/id1/${file##*/}"; done
for file in "$source/peril3.0"/pak*.pak; do ln -s -- "$file" "$game/peril3.0/${file##*/}"; done
ln -s -- "$source/peril3.0/maps" "$game/peril3.0/maps"
gdb -q -batch -x "$script" --args "$binary" -basedir "$game" -game peril3.0 -dedicated 2 -port 0 +sv_coop_autosave 0 +coop 1 +map start
