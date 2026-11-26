# Under the Workshop: diggable cave prototype

Branch `drg-terrain`. A Deep Rock Galactic style cave inside the engine: a generated, fully
diggable ice cave that Quake's weapons, monsters and physics work in. Working title for the
total conversion: "Under the Workshop" (a tunnel is found under Santa's workshop).

This is a prototype. Nothing here is networked yet, and nothing has been run in a headset.

## New to try (all merged here, each first pass and untuned)

From a `nix develop` build in `build-drg` with `run/` as the basedir:

- `tests/drg-worm.sh`: one ice worm and no other monsters. G forces a hunt, X toggles an x-ray of
  the worm inside the rock. Fight it and follow its tunnels.
- `tests/drg-ice.sh`: falling ice. P builds an icicle on the ceiling where you aim, C cuts its root
  (rockets on the root work too); the piece falls as a rigid body, crushes monsters and freezes
  back into the cave. Any dig that cuts a piece of ice loose does the same.
- `tests/drg-lava.sh`: hold L to spray particle lava that runs downhill, pools and melts the ice.

Sections below: "Ice worm", "Falling ice", "Particle lava".

## Run it

1. Compile `tests/maps/drgbox.map` with `qbsp` from ericw-tools and put `drgbox.bsp` in
   `<basedir>/id1/maps/`. The map is only a sealed box with a player start, weapons, ammo and
   25 vanilla monsters.
2. `vkquake -basedir <basedir> -novr +map drgbox +bind f terrain_flare`

Any map whose name starts with `drg` gets a cave: the server generates it at the map centre
before the entities spawn, then moves the player start, items and monsters onto the cave floor.

- Axe: digs, with a hit sound and a puff of chips.
- Rocket and grenade explosions (anyone's): dig.
- Hold jump in the air: jetpack (a stopgap for traversal).
- Hold jump against a ledge: vault onto it.
- `terrain_flare`: throws a bouncing light.
- Four fiends crawl on floors, walls and ceilings, hunt by a flow field along the surfaces, and jump the last gap.
- Vanilla monsters walk a route through the cave to the player they are chasing.

`tests/drg-desktop.sh` does step 2 from a `nix develop` build in `build-drg` with `run/` as the
basedir, and binds the keys below.

### Looks

Four looks for the cave, switched live. They are variants to choose between, not a final design.

- F7 (`terrain_style next`, or `r_terrain_style 0`-`3`): 0 the original untextured ice,
  1 workshop (snow, trees, presents, strings of lights), 2 glacier (veined blue ice, fog),
  3 Agartha (dark masonry, glowing glyphs, plinths and statues).
- F8 (`toggle r_terrain_props`): props and their lights on or off.
- F6: flare.
- `terrain_ice 0` then a map restart: the cave without ice pillars, icicles and floor stubs.

The textures are generated in code (`Quake/r_terrain_tex.c`). The prop models are not in the
repository: they come from the Twisted Christmas Jam 2022 pack (which includes Arcane
Dimensions). With your own copy of it,
`uv run --no-project python -I tests/link-tcj-props.py <tcj dir> <basedir>/id1` puts the models
the styles use where the engine finds them. Without them the looks still work, with no props.

### Ice worm (first pass)

`tests/drg-worm.sh`, or `+terrain_worm 1` before `+map`. A segmented worm (shambler head, greybox
body) burrows through the rock, digging a tunnel, and every few seconds hunts: it bursts out near a
player, arcs through the cave and dives back in. `terrain_worm_hunt` forces a hunt,
`r_terrain_worm_xray 1` draws its chain as lines through the rock (X in the runner, which also
starts with no other monsters), `terrain_worm_info` prints its state, `terrain_worm_hurt <segment> <damage> [qc]` damages a segment.
Tuning cvars start with `terrain_worm_` (`aim_height`, `hunt_speed`, `launch`, `telegraph`,
`dig_radius`, `dig_step`). Code: `Quake/sv_terrain_worm.c`. Unproven so far: hits from real
weapons, whether a player fits through its tunnels everywhere, its warning light and sound, and it
rarely touches the player at the default aim height.

### Falling ice

Cutting an icicle or pillar off its root (any dig, including a rocket) floods the solid samples
around the cut. A piece that is not joined to the rest of the cave within a bounded search becomes
a rigid body: it is removed from the field (a `detach` op in the op log), meshed once, and simulated
at a fixed 144 Hz as a 64-point shell against `Terrain_SampleWorld` with restitution, friction,
sleep and a cap of 16 bodies. When it sleeps it is stamped back into the field as solid at a
quantized pose (a `stamp` op), so replaying the log reproduces the same field. A moving body hurts
monsters through `T_Damage`. Commands: `terrain_ice_pillar [length]` (build an icicle where you
aim), `terrain_ice_cut`, `terrain_ice_dummy`, `terrain_ice_ramp`, `terrain_ice_toss`,
`terrain_ice_cam`, `terrain_ice_info`, `terrain_ice_verify`, `terrain_ice_bench`. Cvars
`terrain_ice_*` (`bodies`, `restitution`, `friction`, `damage`, `max`, `shell`, `sleep`).
Test: `tests/terrain_ice.gdb`. Code: `Quake/sv_terrain_ice.c`, `Quake/terrain_ice.c`.

### Particle lava (first pass)

`tests/drg-lava.sh`: hold L to spray. A pool of up to 4096 particles (`terrain_lava_cap`) simulated
on the server, one fixed 1/72 s step per frame at most: pairwise viscosity, double-density
relaxation (Clavet et al. 2005) on a hash grid, collision with the terrain field, friction and a
short cling so lava hangs on walls and then drips. Each particle has heat: it cools (faster on
terrain), thickens as it cools and freezes when cold. Heat spent on contact fills a counter per
terrain cell; a full counter digs ONE sphere through `SV_Terrain_Edit`, at most
`terrain_lava_ops_rate` (4) per second per emitter stream. Frozen particles piling up in a cell
leave a fill (dark rock). Hot particles are drawn as fullbright blobs coloured by heat, and up to 8
dynamic lights follow the hottest clusters.

Commands: `+terrain_lava` / `-terrain_lava`, `terrain_lava_emit x y z vx vy vz rate seconds [spread]`,
`terrain_lava_clear`, `terrain_lava_stop`, `terrain_lava_info` (counts, max speed, mean neighbours,
step cost split into neighbour search, relaxation and terrain), and test helpers
`terrain_lava_block n x y z` and `terrain_lava_trench x y z dirx diry length radius`. Cvars start
with `terrain_lava_` (`cap`, `density`, `stiffness`, `near_stiffness`, `cohesion`, `viscosity`,
`friction`, `stick`, `cooling`, `contact_cooling`, `melt`, `ops_rate`, `damage`). Code:
`Quake/sv_terrain_lava.c` (simulation, compiles alone with `-DLAVA_STANDALONE`),
`Quake/r_terrain_lava.c` (drawing and lights). Offline check:
`gcc -O2 -std=c11 -IQuake tests/terrain_lava_fixture.c Quake/terrain.c -lm && ./a.out` prints
`TERRAIN_LAVA_FIXTURE_PASSED`; in the engine, `tests/terrain_lava.gdb` and `tests/terrain_lava_melt.gdb`.
Not done: collision with map brushes and entities, a fluid surface, netsync (draws from server
memory, listen server only).

### Spider crawlers

The crawler code drives any walking monster the loaded `progs.dat` has. With the same jam pack
as a mod directory (`<basedir>/tcj`):

    vkquake -basedir <basedir> -novr -game tcj +terrain_crawlers 10 \
      +terrain_crawler_types monster_spider:0.6:15:1.3,monster_spider:1:40,monster_spider:1.8:150:0.8,monster_spider:3:500:0.6 \
      +map drgbox +bind f terrain_flare

Each entry is `classname:scale:health:speed multiplier`; the list is cycled over the crawlers.
Put these arguments before `+map`: the crawlers are made while the map loads.

## Where things are

| File | What |
| --- | --- |
| `Quake/terrain.c`, `terrain.h` | Sparse signed-distance field (16-unit cells, 32^3 chunks), cave generator, surface-nets mesher, swept-box collision against triangles made on the fly, and the walking navigation grid (32-unit cells a body can stand in, joined where the climb is at most 45 degrees, plus one-way drops of up to four cells off a ledge). No engine dependencies; `tests/terrain_fixture.c` builds it alone. |
| `Quake/sv_terrain.c` | Server side: owns the region and the op log, entity placement, axe, jetpack, vault, explosion digging, flares, crawlers and their flow field, and walking routes for ordinary monsters (the grid follows the op log and marks cells inside the map's brushes closed; one distance field per chased player and body size). |
| `Quake/r_terrain.c` | Client side: meshes and draws the region (CPU vertex lighting plus the client's dynamic lights). |
| `Quake/r_terrain_tex.c`, `r_terrain_props.c`, `r_terrain_style.h`, `Shaders/terrain.*` | The looks: generated texture atlas, box-projected nearest-sampled shading, props and light strings placed from the seed. |
| `Quake/world.c`, `pmove.c`, `sv_move.c`, `sv_phys.c`, `pr_cmds.c` | Small hooks: traces, point contents, player movement, monster steps and `movetogoal` routes, explosion sniffing. |

## Design rules so far

- **The server owns the terrain.** State is seed + size + origin + an ordered list of sphere
  ops (`SV_Terrain_OpLog`). Every change goes through `SV_Terrain_Edit`. The generator and the
  ops are integer and deterministic: the seed-1 hash in the fixture is the same under gcc and
  clang at any optimisation level. A client that has the seed and the op log has the cave.
- **The client only draws.** Today it reads the server's region through `terrain_active`, which
  works for a listen server only. Sending seed and ops over the wire is the missing piece.
- **No QuakeC changes.** Everything works with a stock `progs.dat`: abilities read buttons and
  weapon fields, explosion digging watches the `TE_EXPLOSION` broadcasts, crawlers are spawned
  through the progs' own `monster_demon1` function.

## Cvars and commands

| Name | Default | |
| --- | --- | --- |
| `terrain_seed` | 1 | cave seed for drg* maps |
| `terrain_axedig`, `terrain_jetpack`, `terrain_vault` | 1 | abilities |
| `terrain_axefx` | 1 | axe bites play a hit sound and throw chips; 0 is off |
| `terrain_blast` | 64 | radius an explosion digs; 0 is off |
| `terrain_slope` | 0.55 | least walkable floor normal z on terrain |
| `terrain_monsterstep` | 1 | slope-tolerant monster steps |
| `terrain_pathfind` | 1 | walking monsters chasing a player follow a route through the cave; 0 is the stock chase |
| `terrain_crawlers` | 4 | crawlers added at map load |
| `terrain_crawler_sense`, `terrain_crawler_speed` | 900, 210 | |
| `terrain_crawler_nav` | 2 | 0 straight at the enemy, 1 flow field through open air, 2 along surfaces |
| `terrain_crawler_pounce` | 360 | a stalled crawler jumps at a visible enemy within this range; 0 is off |
| `terrain_crawler_types` | `monster_demon1` | comma list of `classname[:scale[:health[:speed]]]`, cycled over the crawlers. With a mod that has one, e.g. `-game <mod> +terrain_crawler_types monster_spider:0.6:15:1.3,monster_spider:3:500:0.6` |
| `terrain_ice` | 1 | ice pillars, icicles and floor stubs in new caves |
| `r_terrain_style`, `r_terrain_props` | 1, 1 | look 0-3 and its props |
| `r_terrain_ambient`, `r_terrain_lamp`, `r_terrain_dlights`, `r_terrain_biome` | | look |

Debug commands: `terrain_probe`, `terrain_ents`, `terrain_wake`, `terrain_dig`, `terrain_fill`,
`terrain_spawn`, `terrain_bench`.

## Tests

- `gcc -O2 -std=c11 -IQuake tests/terrain_fixture.c Quake/terrain.c -lm && ./a.out` prints
  `TERRAIN_FIXTURE_PASSED`.
- `tests/run-headless.sh tests/terrain_<name>.gdb [basedir]` drives the engine with gdb inside a
  headless sway session (Linux, real GPU, no window) and leaves `id1/condump.txt`. Each script's
  header says what to expect. `terrain_pathfind.gdb` compares monsters' route lengths to the player with
  routes on and off (`terrain_ents` prints `route N`, in 32-unit cells).

## Known gaps

- No netsync, no headset pass, no VR-hand digging.
- The region edge is a flat wall that cannot be dug.
- Walking routes are for ground monsters chasing a player. Flyers, swimmers, monsters fighting each
  other and `path_corner` patrols still use the stock chase. A monster drops off ledges up to 128
  units high but cannot climb anything steeper than 45 degrees or jump a gap, so a player who
  jetpacks onto a high ledge is out of reach on foot. Some monsters still stall on steps the slope
  rules refuse, or wedge into a wall.
- Crawler surface pathing is new: the field (about 39k surface cells at 32 units) is rebuilt whole
  four times a second, about 3 ms each time on one core; it should be time-sliced. In seed 1 one
  spawn spot still traps a crawler that hops in place.
- Crawlers ignore each other's bodies beyond a simple spacing push. Their flow field avoids the map's
  brushes, but their movement along the surface does not.
- Meshing is synchronous on the main thread; vertices are re-uploaded every frame.
