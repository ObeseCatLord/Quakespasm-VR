# Under the Workshop: diggable cave prototype

Branch `drg-terrain`. A Deep Rock Galactic style cave inside the engine: a generated, fully
diggable ice cave that Quake's weapons, monsters and physics work in. Working title for the
total conversion: "Under the Workshop" (a tunnel is found under Santa's workshop).

This is a prototype. Nothing here is networked yet, and nothing has been run in a headset.

## Run it

1. Compile `tests/maps/drgbox.map` with `qbsp` from ericw-tools and put `drgbox.bsp` in
   `<basedir>/id1/maps/`. The map is only a sealed box with a player start, weapons, ammo and
   25 vanilla monsters.
2. `vkquake -basedir <basedir> -novr +map drgbox +bind f terrain_flare`

Any map whose name starts with `drg` gets a cave: the server generates it at the map centre
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
| `terrain_blast` | 64 | radius an explosion digs; 0 is off |
| `terrain_slope` | 0.55 | least walkable floor normal z on terrain |
| `terrain_monsterstep` | 1 | slope-tolerant monster steps |
| `terrain_pathfind` | 1 | walking monsters chasing a player follow a route through the cave; 0 is the stock chase |
| `terrain_crawlers` | 4 | crawlers added at map load |
| `terrain_crawler_sense`, `terrain_crawler_speed` | 900, 210 | |
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
- Crawlers stop at the surface point nearest the player when the player is not touching a
  surface they can reach (standing mid-floor under a wall they are on, or in the air). The flow
  field is through open cells, not along surfaces, and only the fiend has a leap to close the gap.
- Crawlers ignore each other's bodies beyond a simple spacing push. Their flow field avoids the map's
  brushes, but their movement along the surface does not.
- Meshing is synchronous on the main thread; vertices are re-uploaded every frame.
