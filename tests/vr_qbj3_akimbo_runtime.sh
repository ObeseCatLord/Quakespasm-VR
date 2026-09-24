#!/usr/bin/env bash
set -euo pipefail

root=$(git -C "$(dirname "${BASH_SOURCE[0]}")/.." rev-parse --show-toplevel)
source=/home/obesecatlord/Windows/Games/quakespasm_straight
binary="$root/Quake/vkquake"
game=$(mktemp -d "${TMPDIR:-/tmp}/qbj3-akimbo-runtime.XXXXXX")
trap 'rm -rf -- "$game"' EXIT

[[ -x "$binary" ]] || { echo "missing target executable: $binary" >&2; exit 2; }
[[ -f "$source/id1/pak0.pak" ]] || { echo "missing installed id1 pak0.pak: $source" >&2; exit 2; }
[[ -f "$source/qbj3/progs.dat" && -f "$source/qbj3/maps/start.bsp" &&
   -f "$source/qbj3/progs/v_tnailgun.mdl" ]] || {
  echo "installed QBJ3 runtime assets are incomplete: $source/qbj3" >&2
  exit 2
}

mkdir -p "$game/id1" "$game/qbj3/progs"
shopt -s nullglob
for file in "$source/id1"/pak*.pak; do
  ln -s -- "$file" "$game/id1/${file##*/}"
done
for name in progs.dat maps sound gfx textures; do
  target="$source/qbj3/$name"
  [[ ! -e "$target" ]] || ln -s -- "$target" "$game/qbj3/$name"
done
for file in "$source/qbj3/progs"/*; do
  ln -s -- "$file" "$game/qbj3/progs/${file##*/}"
done

gdb -q -batch -x "$root/tests/vr_qbj3_akimbo_runtime.gdb" --args \
  "$binary" -basedir "$game" -game qbj3 -dedicated 2 -port 0 \
  +sv_coop_autosave 0 +coop 1 +map start
