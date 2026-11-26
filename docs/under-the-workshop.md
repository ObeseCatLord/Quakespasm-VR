# Under the Workshop: diggable cave prototype

Branch `drg-terrain`. A Deep Rock Galactic style cave inside the engine: a generated, fully
diggable ice cave that Quake's weapons, monsters and physics work in. Working title for the
total conversion: "Under the Workshop" (a tunnel is found under Santa's workshop).

This is a prototype. The cave is networked (seed plus op log, see below), but nothing has been
run in a headset or tested with a remote client yet.

## Run it

1. Compile `tests/maps/drgbox.map` with `qbsp` from ericw-tools and put `drgbox.bsp` in
   `<basedir>/id1/maps/`. The map is only a sealed box with a player start, weapons, ammo and
   25 vanilla monsters.
2. `vkquake -basedir <basedir> -novr +map drgbox +bind f terrain_flare`

Any map whose name starts with `drg` gets a cave: the server generates it over the whole map
before the entities spawn, then moves the player start, items and monsters onto the cave floor.

- Axe: digs.
- Rocket and grenade explosions (anyone's): dig.
- Hold jump in the air: jetpack (a stopgap for traversal).
- Hold jump against a ledge: vault onto it.
- `terrain_flare`: throws a bouncing light.
- Four fiends crawl on floors, walls and ceilings and hunt by a flow field.
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

### Spider crawlers

The crawler code drives any walking monster the loaded `progs.dat` has. With the same jam pack
as a mod directory (`<basedir>/tcj`):

    vkquake -basedir <basedir> -novr -game tcj +terrain_crawlers 10 \
      +terrain_crawler_types monster_spider:0.6:15:1.3,monster_spider:1:40,monster_spider:1.8:150:0.8,monster_spider:3:500:0.6 \
      +map drgbox +bind f terrain_flare

Each entry is `classname:scale:health:speed multiplier`; the list is cycled over the crawlers.
Put these arguments before `+map`: the crawlers are made while the map loads.

## The workshop tools

Engine-side, so they work with a stock `progs.dat`: a tool takes the weapon's place on screen
while it is out and holds the progs' own weapon back; ordinary weapon impulses put it away.

- **Platform gun** (`impulse 70`, key `0`), after the Engineer's in Deep Rock Galactic. Its numbers come
  from the game's own weapon and projectile assets (`WPN_PlatformGun`, `PRJ_PlatformMaker`),
  scaled to Quake: 4 a magazine, 16 carried, one shot a second, 2 second automatic reload, the
  shot flying at 4000 cm/s (1250 units/s) under half gravity, the platform forming a quarter
  second after it lands. Here the shot is a blob of gingerbread dough: on the cave or the map it
  spreads into a flat gingerbread disk (`TERRAIN_OP_DISK`, a networked terrain op, so it is
  ground like any other and the monsters' routes use it); on a monster or player it shatters
  and hurts them. Anyone a growing platform would bury is lifted onto it. Reserve comes back
  slowly (`workshop_platgun_regen`).
- **Festive grapple** (`impulse 71`, key `9`): hold fire to throw a gold star hook on a string of
  Christmas lights; once it bites, it pulls you in, letting go reels it back. Its behaviour
  follows the grapple in Dissolution of Eternity; the models are new.
- **Desktop keys**: `9` the grapple and `0` the platform gun (weapon slots after Quake's 1-8; press
  again or pick any weapon to put the tool away), `F` throws a flare (`workshop_flare`, from the
  same flares a VR wrist carries, four that come back over time). New configs get these from the
  engine's `default.cfg`; on the first workshop map the client also binds whichever of the three
  keys are free (or hold Quake's no-op `impulse 0`) and says so in the console;
  `cl_workshop_binds 0` leaves keys alone.
- **VR**: flares ride on the weapon hand's wrist and are taken with the other hand's grip and
  thrown by letting go, at the hand's own speed (as the VRG mod for Deep Rock does); the grapple
  sits in a holster on the right hip, drawn by gripping there with the weapon hand and put back
  by letting go. 

Assets: `Misc/workshop_assets/` holds the concept sheets, the Blender sources and
`mdltool.py`, which builds the Quake models into `Misc/vq_pak/progs/workshop/` (the engine's
own pak). Without them the tools fall back to stock models.

## Where things are

| File | What |
| --- | --- |
| `Quake/terrain.c`, `terrain.h` | Sparse signed-distance field (16-unit cells, 32^3 chunks), cave generator, surface-nets mesher, swept-box collision against triangles made on the fly, and the walking navigation grid (32-unit cells a body can stand in, joined where the climb is at most 45 degrees, plus drops of up to four cells off a ledge and, when allowed, jumps of a few cells up onto one; a weighted flood, so routes keep to the floor where they can). No engine dependencies; `tests/terrain_fixture.c` builds it alone. |
| `Quake/sv_terrain.c` | Server side: owns the region and the op log, entity placement, axe, jetpack, vault, explosion digging, flares, crawlers and their flow field, and walking routes for ordinary monsters. The grid follows the op log and marks cells inside the map's brushes closed. Each chased player has a near field (about 1900 units of walking around them, flooded again the same frame they move or the cave changes) and a whole-cave far field refreshed every second or two, one such flood a frame; the most chased player gets the larger near area and the faster refresh, so the pack keeps after whoever it is chasing. |
| `Quake/cl_terrain.c` | Client side: the client's own copy of the cave, generated from the seed the server sends and kept up to date by replaying its op stream. |
| `Quake/r_terrain.c` | Client side: meshes the client's copy on worker threads and draws it (CPU vertex lighting plus the client's dynamic lights, cached per chunk). |
| `Quake/r_terrain_tex.c`, `r_terrain_props.c`, `r_terrain_style.h`, `Shaders/terrain.*` | The looks: generated texture atlas, box-projected nearest-sampled shading, props and light strings placed from the seed. |
| `Quake/world.c`, `pmove.c`, `sv_move.c`, `sv_phys.c`, `pr_cmds.c` | Small hooks: traces, point contents, player movement, monster steps and `movetogoal` routes, explosion sniffing. |

## Design rules so far

- **The server owns the terrain.** State is seed + size + origin + an ordered list of sphere
  ops (`SV_Terrain_OpLog`). Every change goes through `SV_Terrain_Edit`. The generator and the
  ops are integer and deterministic: the seed-1 hash in the fixture is the same under gcc and
  clang at any optimisation level. A client that has the seed and the op log has the cave.
- **The client has its own copy.** A spawned client of this engine (the pinned QSVR protocol) is
  sent `//terrain_init` (seed, origin, size, ice, walk slope) and then every op in order as
  `//terrain_ops <first index> <bytes per op> <hex>`, hidden stuffed commands on the reliable
  channel. It generates the same cave and replays the ops (`cl_terrain.c`); drawing, props and
  the client's player prediction use that copy (`terrain_client`), the server's traces and player
  move use `terrain_active`. A listen server's own client takes the same path. `terrain_clhash`
  prints both hashes when one process runs both.
- **No QuakeC changes.** Everything works with a stock `progs.dat`: abilities read buttons and
  weapon fields, explosion digging watches the `TE_EXPLOSION` broadcasts, crawlers are spawned
  through the progs' own `monster_demon1` function.

## Cvars and commands

| Name | Default | |
| --- | --- | --- |
| `terrain_seed` | 1 | cave seed for drg* maps |
| `terrain_axedig`, `terrain_jetpack`, `terrain_vault` | 1 | abilities |
| `terrain_blast` | 64 | radius an explosion digs; 0 is off |
| `terrain_slope` | 0.55 | least walkable floor normal z on terrain |
| `terrain_monsterstep` | 1 | slope-tolerant monster steps |
| `terrain_pathfind` | 1 | walking monsters chasing a player follow a route through the cave; 0 is the stock chase |
| `terrain_monsterclimb` | 3 | 32-unit levels a walking monster may jump up onto a ledge on its route; 0 is none |
| `terrain_crawlers` | 4 | crawlers added at map load |
| `terrain_crawler_sense`, `terrain_crawler_speed` | 900, 210 | |
| `terrain_crawler_types` | `monster_demon1` | comma list of `classname[:scale[:health[:speed]]]`, cycled over the crawlers. With a mod that has one, e.g. `-game <mod> +terrain_crawler_types monster_spider:0.6:15:1.3,monster_spider:3:500:0.6` |
| `terrain_ice` | 1 | ice pillars, icicles and floor stubs in new caves |
| `r_terrain_style`, `r_terrain_props` | 1, 1 | look 0-3 and its props |
| `workshop_tools` | 1 | the platform gun and grapple on drg* maps |
| `workshop_platform_radius`, `workshop_platform_thickness` | 60, 20 | platform size |
| `workshop_platgun_speed`, `workshop_platgun_damage`, `workshop_platgun_regen` | 1250, 40, 10 | |
| `workshop_grapple_speed`, `workshop_grapple_pull`, `workshop_grapple_range` | 1100, 620, 1600 | |
| `workshop_flares`, `workshop_flare_regen` | 4, 15 | flares a VR wrist carries, seconds per flare back |
| `vr_workshop_holsters`, `vr_workshop_throw_scale`, `cl_workshop_lights`, `cl_workshop_binds` | 1, 2.5, 1, 1 | client side |
| `r_terrain_ambient`, `r_terrain_lamp`, `r_terrain_dlights`, `r_terrain_biome` | | look |

Debug commands: `terrain_probe`, `terrain_ents`, `terrain_wake`, `terrain_dig`, `terrain_fill`,
`terrain_spawn`, `terrain_bench`, `terrain_clhash`.

## Tests

- `gcc -O2 -std=c11 -IQuake tests/terrain_fixture.c Quake/terrain.c -lm && ./a.out` prints
  `TERRAIN_FIXTURE_PASSED`.
- `tests/run-headless.sh tests/terrain_<name>.gdb [basedir]` drives the engine with gdb inside a
  headless sway session (Linux, real GPU, no window) and leaves `id1/condump.txt`. Each script's
  header says what to expect. `terrain_pathfind.gdb` compares monsters' route costs to the player
  with routes on and off (`terrain_ents` prints `route N`: about 2 per 32 units walked).
  `terrain_netsync.gdb` checks that the client's copy matches the server's region after edits.
  `workshop_tools.gdb` fires the platform gun and the grapple.

## Known gaps

- No headset pass (the wrist flares and hip holster are untested in VR), no remote-client pass
  of the netsync, no VR-hand digging.
- The region covers the map up to 16 chunks (8192 units) a side; a larger map keeps a flat
  undiggable wall where the region ends.
- Walking routes are for ground monsters chasing a player. Flyers, swimmers, monsters fighting each
  other and `path_corner` patrols still use the stock chase. A monster drops off ledges up to 128
  units high and jumps up to `terrain_monsterclimb` levels, but cannot jump a gap or follow a
  player jetpacking high up a wall. A monster stuck for three seconds gets a second of the stock
  chase to shake loose; a wedged one is pushed out of the terrain.
- Crawlers stop at the surface point nearest the player when the player is not touching a
  surface they can reach (standing mid-floor under a wall they are on, or in the air). The flow
  field is through open cells, not along surfaces, and only the fiend has a leap to close the gap.
- Crawlers keep out of the map's brushes and do not walk into other monsters or players, but
  they test bodies as spheres, not boxes.
- Terrain lighting is still computed on the CPU (cached per chunk, redone only when its inputs
  change); each frame copies the visible chunks' lit vertices into the frame's vertex memory.
  Moving the lamp and dynamic lights into the shader would remove that copy.
