#!/usr/bin/env bash
set -euo pipefail

root=$(git -C "$(dirname "${BASH_SOURCE[0]}")/.." rev-parse --show-toplevel)
assets=${QSVR_TEST_ASSETS:-/home/obesecatlord/Windows/Games/quakespasm_straight}
binary="$root/build-debug/vkquake"
game=$(mktemp -d "${TMPDIR:-/tmp}/enyo-melee-outcome.XXXXXX")
trap 'rm -rf -- "$game"' EXIT

[[ -x "$binary" && -f "$assets/id1/pak0.pak" && -f "$assets/enyo/pak0.pak" ]] || {
  echo 'missing debug executable or installed Enyo/id1 PAKs' >&2
  exit 2
}
mkdir -p "$game/id1" "$game/enyo"
shopt -s nullglob
for mod in id1 enyo; do
  for file in "$assets/$mod"/pak*.pak; do
    ln -s -- "$file" "$game/$mod/${file##*/}"
  done
done
gdb -q -batch -x "$root/tests/vr_enyo_melee_outcome.gdb" --args \
  "$binary" -basedir "$game" -game enyo -dedicated 2 -port 0 \
  +sv_coop_autosave 0 +coop 1 +map start
