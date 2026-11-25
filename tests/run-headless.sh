#!/usr/bin/env bash
# Runs a gdb-driven engine script with no visible window, on the machine's real GPU, inside a
# headless sway (wlroots) session. Used for the scripted terrain checks.
#   tests/run-headless.sh tests/terrain_play.gdb [basedir]     (basedir defaults to ./run)
# Output: <basedir>/id1/condump.txt, screenshots beside it, engine log in $TMPDIR/drg-headless.log
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
script="$(realpath "$1")"
base="$(realpath "${2:-$repo/run}")"
work="$(mktemp -d)"
log="${TMPDIR:-/tmp}/drg-headless.log"
cat > "$work/job.sh" <<J
#!/usr/bin/env bash
cd "$repo"
export SDL_VIDEODRIVER=wayland
unset DISPLAY
timeout 300 nix develop -c nix shell nixpkgs#gdb -c gdb -q -batch -x "$script" --args ./build-drg/vkquake \\
  -basedir "$base" -novr -nosound -window -width 1280 -height 720 +skill 1 +map drgbox > "$log" 2>&1
echo "exit \$?" >> "$log"
swaymsg exit
J
chmod +x "$work/job.sh"
printf 'output HEADLESS-1 resolution 1280x720\nexec %s\n' "$work/job.sh" > "$work/sway.conf"
env -u WAYLAND_DISPLAY -u DISPLAY WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 \
  timeout 400 sway --unsupported-gpu -c "$work/sway.conf" > "$work/sway.log" 2>&1 || true
tail -1 "$log"
grep -a -E "^terrain" "$base/id1/condump.txt" | head -5
