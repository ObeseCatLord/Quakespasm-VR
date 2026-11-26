#!/usr/bin/env bash
# Desktop (no headset) run for falling ice, with no monsters. P builds an icicle on the ceiling where
# you aim, C cuts its root (or shoot the root with rockets), G sprays lava, F throws a flare.
cd "$(dirname "$0")/.." && exec nix develop -c ./build-drg/vkquake -basedir "$PWD/run" -novr +terrain_crawlers 0 +nomonsters 1 +map drgbox \
  +bind p "terrain_ice_pillar 280" +bind c terrain_ice_cut +bind g +terrain_lava +bind f terrain_flare +bind F7 "terrain_style next" "$@"
