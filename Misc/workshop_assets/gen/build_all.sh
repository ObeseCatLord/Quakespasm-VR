#!/bin/bash
# Regenerate the workshop assets: skins + meshes (Python/PIL), .blend + OBJ frames (headless Blender), .mdl (mdltool).
# usage: QUAKE_PALETTE=/path/to/palette.lmp ./build_all.sh [asset ...]     (or: ./build_all.sh --palette <file> [asset ...])
set -e
cd "$(dirname "$0")"
if [ "$1" = "--palette" ]; then QUAKE_PALETTE="$2"; shift 2; fi
[ -f "$QUAKE_PALETTE" ] || { echo "set QUAKE_PALETTE (or --palette) to gfx/palette.lmp extracted from your Quake id1/pak0.pak" >&2; exit 1; }
export QUAKE_PALETTE PYTHONDONTWRITEBYTECODE=1
export WORKSHOP_WORK="${WORKSHOP_WORK:-${TMPDIR:-/tmp}/workshop_gen}"
ASSETS="$(cd .. && pwd)"; OUT="$(cd ../.. && pwd)/vq_pak/progs/workshop"
ALL="${@:-v_platgun v_grapple starhook gingerblob gingershard bulb flare wristband}"
mkdir -p "$WORKSHOP_WORK" "$OUT"
python3 assets.py $(echo $ALL | sed 's/v_//g')
"${BLENDER:-blender}" -b --factory-startup --python bl_build.py -- $ALL 2>&1 | grep -E "BUILT|Error|Traceback" || true
python3 check.py $ALL
for n in $ALL; do
  python3 "$ASSETS/mdltool.py" build "$ASSETS/$n/spec.json" "$OUT/$n.mdl" --palette "$QUAKE_PALETTE"
  python3 "$ASSETS/mdltool.py" info "$OUT/$n.mdl"
done
