#!/usr/bin/env bash
set -euo pipefail
qsvr_mode=${1:-vr}
if [[ $# -gt 0 ]]; then shift; fi
case "$qsvr_mode" in
  vr) qsvr_mode_arg=-vr ;;
  desktop) qsvr_mode_arg=-novr ;;
  *) echo "usage: $0 [vr|desktop] [engine arguments...]" >&2; exit 2 ;;
esac
qsvr_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
qsvr_data=${QUAKE_DATA_DIR:-"$HOME/Downloads/quakespasm_straight"}
qsvr_profile=${QSVR_SPATIAL_PROFILE:-/tmp/qsvr-spatial-headset}
# OpenVR otherwise searches the isolated XDG_CONFIG_HOME below, losing the
# user's selected runtime. Preserve explicit overrides and the normal registry.
qsvr_vr_registry=${VR_PATHREG_OVERRIDE:-"${XDG_CONFIG_HOME:-$HOME/.config}/openvr/openvrpaths.vrpath"}
if [[ ! -f "$qsvr_data/id1/pak0.pak" ]]; then
  echo "Missing $qsvr_data/id1/pak0.pak; set QUAKE_DATA_DIR to the Quake installation." >&2
  exit 1
fi
mkdir -p "$qsvr_profile/id1" "$qsvr_profile/config"
for qsvr_pak in "$qsvr_data"/id1/*.pak; do
  qsvr_link="$qsvr_profile/id1/$(basename -- "$qsvr_pak")"
  if [[ ! -e "$qsvr_link" && ! -L "$qsvr_link" ]]; then ln -s -- "$qsvr_pak" "$qsvr_link"; fi
done
if [[ ! -e "$qsvr_profile/quakespasm.pak" ]]; then
  ln -s -- "$qsvr_root/Windows/static-release-files/quakespasm.pak" "$qsvr_profile/quakespasm.pak"
fi
# The profile is writable; only game-data archives are linked to the real install.
if [[ $# -eq 0 ]]; then
  set -- +map e1m1
fi
cd -- "$qsvr_root"
nix develop --command make -C Quake -f Makefile.linux USE_STEAMAUDIO=1 -j"$(nproc)"
echo "Isolated spatial-audio profile: $qsvr_profile"
echo "After loading: snd_spatial_probe; snd_hrtf 0/1; snd_spatial_status; stopsound"
exec nix develop --command env XDG_CONFIG_HOME="$qsvr_profile/config" \
  VR_PATHREG_OVERRIDE="$qsvr_vr_registry" \
  "$qsvr_root/Quake/quakespasm-openvr.bin" "$qsvr_mode_arg" -nosteam \
  -basedir "$qsvr_profile" "$@"
