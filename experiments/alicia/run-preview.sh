#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
spike_dir=${ALICIA_SPIKE_ROOT:-$HOME/.local/share/quakespasm-vr-spikes/alicia}
vrm_file=${ALICIA_VRM:-/mnt/s/code/mainspring/vrm_samples/AliciaSolid_vrm-0.51.vrm}
quake_data=${ALICIA_QUAKE_DATA:-$HOME/Downloads/quakespasm_straight}
case ${1:-vrm} in
  md5) mode=0 ;;
  vrm) mode=1 ;;
  unlit) mode=2 ;;
  *) echo 'Usage: run-preview.sh [md5|vrm|unlit]' >&2; exit 2 ;;
esac
if [[ ! -x "$spike_dir/bin/quakespasm-alicia" || ! -f "$spike_dir/base/player_models/alicia/avatar.cfg" ]]; then
  echo 'Build the isolated Alicia spike and generate its package first; see experiments/alicia/README.md.' >&2
  exit 1
fi
cp -- "$repo_dir/experiments/alicia/preview.cfg" "$spike_dir/base/id1/alicia-preview.cfg"
exec nix develop "$repo_dir" --command "$spike_dir/bin/quakespasm-alicia" \
  -basedir "$spike_dir/base" -rerelease "$quake_data" -aliciavrm "$vrm_file" \
  -window -width 1280 -height 960 -nosound -nomusic \
  +exec alicia-preview.cfg +r_alicia_direct "$mode"
