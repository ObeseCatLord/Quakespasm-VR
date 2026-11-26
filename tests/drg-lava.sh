#!/usr/bin/env bash
# Desktop (no headset) run of the cave box with the particle lava. Extra arguments go to the engine.
# Pull out the lava sprayer with - (or scroll past the platform gun), hold fire to hose lava; 9 is the
# grapple, 0 the platform gun, F throws a flare, F7 cycles the terrain look, F8 the lava style.
# (The old +terrain_lava debug bind still exists: bind it from the console if you want a no-tank spray.)
cd "$(dirname "$0")/.." || exit 1
exec nix develop -c ./build-drg/vkquake -basedir "$PWD/run" -novr +map drgbox \
  +bind f terrain_flare +bind F7 "terrain_style next" +bind F8 "terrain_lava_style next" "$@"
