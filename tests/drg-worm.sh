#!/usr/bin/env bash
# Desktop (no headset) run of the cave box with one ice worm. Extra arguments go to the engine.
# No other monsters. G forces a hunt (the worm bursts out near you), X toggles boxes drawn through the
# rock around the worm's segments, F throws a flare, F7 cycles the look.
cd "$(dirname "$0")/.." && exec nix develop -c ./build-drg/vkquake -basedir "$PWD/run" -novr +terrain_worm 1 +terrain_crawlers 0 +nomonsters 1 +r_showbboxes_filter terrain_worm +map drgbox \
  +bind g terrain_worm_hunt +bind x "toggle r_showbboxes" +bind f terrain_flare +bind F7 "terrain_style next" "$@"
