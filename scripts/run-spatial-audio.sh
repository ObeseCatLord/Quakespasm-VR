#!/usr/bin/env bash
# Isolated writable configs/saves, with shared read-only game assets.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-vr}
case "$mode" in vr|desktop) shift "$(( $# > 0 ? 1 : 0 ))";; *) echo "Usage: $0 [vr|desktop] [Quake arguments...]" >&2; exit 2;; esac
export QSVR_DATA=${QSVR_DATA:-$HOME/Downloads/quakespasm_straight}
export QSVR_PROFILE=${QSVR_PROFILE:-/tmp/qsvr-environment-$USER}
export QSVR_ROOT=$root
python3 - <<'PY'
import os
from pathlib import Path
source=Path(os.environ['QSVR_DATA']).resolve()
profile=Path(os.environ['QSVR_PROFILE']).resolve()
if source == profile or source in profile.parents:
    raise SystemExit('Choose an isolated profile outside the existing installation')
profile.mkdir(parents=True, exist_ok=True)
for mod in ('id1', 'qbj3'):
    src=source/mod
    if not src.is_dir(): continue
    dst=profile/mod
    if dst.is_symlink(): raise SystemExit(f'Refusing writable symlink: {dst}')
    dst.mkdir(exist_ok=True)
    for item in src.iterdir():
        # Engine writes configuration and saves only into the private mod root.
        if item.suffix.lower() in ('.pak', '.dat', '.wad') or item.name in (
            'maps','progs','sound','gfx','textures','music','particles'):
            target=dst/item.name
            if not target.exists(): target.symlink_to(item)
pak=profile/'quakespasm.pak'
if not pak.exists(): pak.symlink_to(Path(os.environ['QSVR_ROOT'])/'Windows/static-release-files/quakespasm.pak')
PY
# Preserve the real runtime registry before isolating SDL preferences.
export VR_PATHREG_OVERRIDE=${VR_PATHREG_OVERRIDE:-${XDG_CONFIG_HOME:-$HOME/.config}/openvr/openvrpaths.vrpath}
export XDG_CONFIG_HOME="$QSVR_PROFILE/config"
export XDG_DATA_HOME="$QSVR_PROFILE/data" # SDL_GetPrefPath stores microphone preferences here.
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
if [[ ${QSVR_SKIP_BUILD:-0} != 1 ]]; then
  nix develop "$root" --command make -C "$root/Quake" -f Makefile.linux USE_STEAMAUDIO=1 USE_VOICE=1 -j8
fi
if [[ $# == 0 ]]; then set -- +map e1m1; fi
flag=-vr; [[ $mode == desktop ]] && flag=-novr
exec nix develop "$root" --command "$root/Quake/quakespasm-openvr.bin" -basedir "$QSVR_PROFILE" "$flag" "$@"
