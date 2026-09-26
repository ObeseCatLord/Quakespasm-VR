#!/usr/bin/env bash
set -euo pipefail

root=$(git -C "$(dirname "${BASH_SOURCE[0]}")/.." rev-parse --show-toplevel)
source=/home/obesecatlord/Windows/Games/quakespasm_straight
binary="$root/build-debug/vkquake"
game=$(mktemp -d "${TMPDIR:-/tmp}/qbj3-contact-runtime.XXXXXX")
trap 'rm -rf -- "$game"' EXIT

[[ -x "$binary" ]] || { echo "missing debug executable: $binary" >&2; exit 2; }
[[ -f "$source/id1/pak0.pak" && -f "$source/qbj3/progs.dat" &&
   -f "$source/qbj3/maps/start.bsp" &&
   -f "$source/qbj3/progs/v_wrench.mdl" &&
   -f "$source/qbj3/progs/v_berserk.mdl" ]] || {
  echo "installed QBJ3 runtime assets are incomplete" >&2
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

gdb -q -batch -x "$root/tests/vr_qbj3_contact_runtime.gdb" --args \
  "$binary" -basedir "$game" -game qbj3 -dedicated 2 -port 0 \
  +sv_coop_autosave 0 +coop 1 +map start
