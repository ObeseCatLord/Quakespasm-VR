#!/usr/bin/env bash
set -euo pipefail

root=$(git -C "$(dirname "${BASH_SOURCE[0]}")/.." rev-parse --show-toplevel)
source=/home/obesecatlord/Windows/Games/quakespasm_straight
binary="$root/build-debug/vkquake"
script="$root/tests/vr_stock_axe_runtime.gdb"
game=$(mktemp -d "${TMPDIR:-/tmp}/stock-axe-runtime.XXXXXX")
trap 'rm -rf -- "$game"' EXIT

[[ -x "$binary" && -f "$script" ]] || {
  echo "stock axe debug build or GDB script is missing" >&2
  exit 2
}
[[ -f "$source/id1/pak0.pak" ]] || {
  echo "installed id1 pak0.pak is missing" >&2
  exit 2
}

mkdir -p "$game/id1"
shopt -s nullglob
for file in "$source/id1"/pak*.pak; do
  ln -s -- "$file" "$game/id1/${file##*/}"
done

gdb -q -batch -x "$script" --args \
  "$binary" -basedir "$game" -dedicated 2 -port 0 \
  +sv_coop_autosave 0 +coop 1 +map start
