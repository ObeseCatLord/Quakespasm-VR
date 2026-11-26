#!/usr/bin/env bash
# Desktop (no headset) run of the cave box. Extra arguments go to the engine.
# The workshop tools are on 9 (grapple), 0 (platform gun) and - (lava sprayer), or scroll past your guns.
# F7 cycles the terrain look (r_terrain_style 0-3), F8 toggles the props, F9 cycles the lava style,
# F6 throws a flare.
cd "$(dirname "$0")/.." && exec nix develop -c ./build-drg/vkquake -basedir "$PWD/run" -novr +map drgbox \
  +bind F7 "terrain_style next" +bind F8 "toggle r_terrain_props" +bind F9 "terrain_lava_style next" +bind F6 terrain_flare "$@"
