#!/usr/bin/env bash
# Desktop (no headset) run for falling ice, with no monsters. P builds an icicle on the ceiling where
# you aim, C cuts its root (or shoot the root with rockets), hold L to spray lava, F throws a flare.
cd "$(dirname "$0")/.." || exit 1
# a bind to a +command cannot go on the command line (the + starts a new command), so it goes in a cfg
mkdir -p run/id1 && printf 'bind l "+terrain_lava"\n' > run/id1/drg-keys.cfg
exec nix develop -c ./build-drg/vkquake -basedir "$PWD/run" -novr +terrain_crawlers 0 +nomonsters 1 +map drgbox +exec drg-keys.cfg \
  +bind p "terrain_ice_pillar 280" +bind c terrain_ice_cut +bind f terrain_flare +bind F7 "terrain_style next" "$@"
