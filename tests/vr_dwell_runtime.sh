#!/usr/bin/env bash
set -euo pipefail

root=$(git -C "$(dirname "${BASH_SOURCE[0]}")/.." rev-parse --show-toplevel)
source=/home/obesecatlord/Windows/Games/quakespasm_straight
binary="$root/build-debug/vkquake"
gdb_script="${1:-$root/tests/vr_dwell_policy_gate.gdb}"
game=$(mktemp -d "${TMPDIR:-/tmp}/dwell-runtime.XXXXXX")
trap 'rm -rf -- "$game"' EXIT

[[ -x "$binary" ]] || { echo "missing debug executable: $binary" >&2; exit 2; }
[[ -f "$gdb_script" ]] || { echo "missing GDB script: $gdb_script" >&2; exit 2; }
[[ -f "$source/id1/pak0.pak" && -f "$source/dwellv2p2/progs.dat" &&
   -f "$source/dwellv2p2/maps/start.bsp" ]] || {
  echo "installed Dwell runtime assets are incomplete" >&2
  exit 2
}

mkdir -p "$game/id1" "$game/dwellv2p2"
shopt -s nullglob
for file in "$source/id1"/pak*.pak; do
  ln -s -- "$file" "$game/id1/${file##*/}"
done
ln -s -- "$source/dwellv2p2/pak0.pak" "$game/dwellv2p2/pak0.pak"
ln -s -- "$source/dwellv2p2/progs.dat" "$game/dwellv2p2/progs.dat"
ln -s -- "$source/dwellv2p2/maps" "$game/dwellv2p2/maps"

gdb -q -batch -x "$gdb_script" --args \
  "$binary" -basedir "$game" -game dwellv2p2 -dedicated 2 -port 0 \
  +sv_coop_autosave 0 +coop 1 +map start
