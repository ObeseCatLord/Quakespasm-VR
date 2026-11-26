#!/usr/bin/env bash
# Desktop (no headset) run of the cave box with the particle lava. Extra arguments go to the engine.
# Hold L to spray lava along the view, F throws a flare, F7 cycles the look.
cd "$(dirname "$0")/.." || exit 1
# a bind to a +command cannot go on the command line (the + starts a new command), so it goes in a cfg
mkdir -p run/id1 && printf 'bind l "+terrain_lava"\n' > run/id1/drg-keys.cfg
exec nix develop -c ./build-drg/vkquake -basedir "$PWD/run" -novr +map drgbox +exec drg-keys.cfg \
  +bind f terrain_flare +bind F7 "terrain_style next" "$@"
