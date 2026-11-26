#!/usr/bin/env bash
# Desktop (no headset) run of the cave box with one ice worm. Extra arguments go to the engine.
# G forces a hunt (the worm bursts out near you), F throws a flare, F7 cycles the look.
cd "$(dirname "$0")/.." && exec nix develop -c ./build-drg/vkquake -basedir "$PWD/run" -novr +terrain_worm 1 +map drgbox \
  +bind g terrain_worm_hunt +bind f terrain_flare +bind F7 "terrain_style next" "$@"
