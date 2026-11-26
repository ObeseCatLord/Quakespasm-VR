#!/usr/bin/env bash
# Desktop (no headset) run for falling ice, with no monsters. P builds an icicle on the ceiling where
# you aim, C cuts its root (or shoot the root with rockets), - takes out the lava sprayer (hold fire
# to hose lava on the ice), F throws a flare, F7 cycles the terrain look, F8 the lava style.
cd "$(dirname "$0")/.." || exit 1
exec nix develop -c ./build-drg/vkquake -basedir "$PWD/run" -novr +terrain_crawlers 0 +nomonsters 1 +map drgbox \
  +bind p "terrain_ice_pillar 280" +bind c terrain_ice_cut +bind f terrain_flare +bind F7 "terrain_style next" \
  +bind F8 "terrain_lava_style next" "$@"
