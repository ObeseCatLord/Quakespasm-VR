#!/usr/bin/env bash
# Desktop (no headset) run of the cave box with the particle lava. Extra arguments go to the engine.
# Hold G to spray lava along the view, F throws a flare, F7 cycles the look.
cd "$(dirname "$0")/.." && exec nix develop -c ./build-drg/vkquake -basedir "$PWD/run" -novr +map drgbox \
  +bind g +terrain_lava +bind f terrain_flare +bind F7 "terrain_style next" "$@"
