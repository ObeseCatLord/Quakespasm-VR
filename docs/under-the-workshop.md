# Under the Workshop: diggable cave prototype

Branch `drg-terrain`. A Deep Rock Galactic style cave inside the engine: a generated, fully
diggable ice cave that Quake's weapons, monsters and physics work in. Working title for the
total conversion: "Under the Workshop" (a tunnel is found under Santa's workshop).

This is a prototype. The cave is networked (seed plus op log, see below), but nothing has been
run in a headset or tested with a remote client yet.

## New to try (all merged here, each first pass and untuned)

From a `nix develop` build in `build-drg` with `run/` as the basedir:

- `tests/drg-worm.sh`: one ice worm and no other monsters. G forces a hunt, X toggles an x-ray of
  the worm inside the rock. Fight it and follow its tunnels.
- `tests/drg-ice.sh`: falling ice. P builds an icicle on the ceiling where you aim, C cuts its root
  (rockets on the root work too); the piece falls as a rigid body, crushes monsters and freezes
  back into the cave. Any dig that cuts a piece of ice loose does the same.
- `tests/drg-lava.sh`: take out the lava sprayer (`-`, or scroll past the platform gun) and hold fire to
  hose particle lava that runs downhill, pools and melts the ice; F8 cycles the lava look.

Sections below: "Ice worm", "Falling ice", "Particle lava".

## Run it

1. Compile `tests/maps/drgbox.map` with `qbsp` from ericw-tools and put `drgbox.bsp` in
   `<basedir>/id1/maps/`. The map is only a sealed box with a player start, weapons, ammo and
   25 vanilla monsters.
2. `vkquake -basedir <basedir> -novr +map drgbox +bind f terrain_flare`

Any map whose name starts with `drg` gets a cave: the server generates it over the whole map
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

On by default in every cave (`terrain_worm 0` turns it off); `tests/drg-worm.sh` runs it alone with
the x-ray on. `terrain_worm_scale` (2) sets its thickness. A segmented worm (shambler head, greybox
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
quantized pose (a `stamp` op), so replaying the log reproduces the same field. Both ops go to
clients in the op stream like digs (a stamp's packed orientation rides in the four bytes a disk
uses for its normal and thickness), so the client's copy loses the piece and gets it back at the
same pose; the moving body itself is drawn from server memory (listen server only). A moving body hurts
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
`gcc -O2 -std=c11 -IQuake tests/terrain_lava_fixture.c Quake/terrain.c Quake/terrain_ice.c -lm && ./a.out` prints
`TERRAIN_LAVA_FIXTURE_PASSED`; in the engine, `tests/terrain_lava.gdb` and `tests/terrain_lava_melt.gdb`.

#### Look, lights, steam, sleeping (second pass)

Styles: `r_terrain_lava_style 0..3` or `terrain_lava_style next` (prints the name; `terrain_lava_style info`
prints quad counts and the frame GPU time): blobs (shaded spheres), goo (big merged bodies, dithered
edge), chunky (pixel-art discs, six palette steps, world-locked texels), crust (cooling plates with glowing
cracks). Hot glow sprites are additive, steam is a soft premultiplied puff; both skip depth writes.
Lights: at most 8, orange-red, scale with heat and particle count, smoothed over time; brightness is
`r_terrain_lava_light` (0 = off). Steam: `r_terrain_lava_steam` (0 = off, 1 = default) where lava melts or
digs ice; puffs are big and soft, rise 60 to 120 units and fade over about 2 s, capped at 900
(`r_terrain_lava_steam 2` doubles the amount). The default style is chunky (`r_terrain_lava_style 2`). Sleeping: `terrain_lava_sleep 1` lets settled
particles stop being simulated (they wake when an awake neighbour moves fast or the terrain changes
nearby); `terrain_lava_crust` sets surface cooling. Look test: `tests/terrain_lava_look.gdb`.
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

## The workshop tools

Engine-side, so they work with a stock `progs.dat`: a tool takes the weapon's place on screen
while it is out and holds the progs' own weapon back; ordinary weapon impulses put it away (and
the progs switch weapon in the same command). The mouse wheel (Quake's next and previous weapon,
impulses 10 and 12) runs on past Quake's last usable weapon to the grapple, then the platform
gun, then the lava sprayer, then back round to the axe, and the reverse going the other way. The tools and their order
are one table in `Quake/sv_workshop.c` (`ws_tools`); `workshop_info` prints each player's tool
and weapon.

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
- **Lava sprayer** ("molten candy", `impulse 72`, key `-`): a candy-striped hose gun with a glass tank
  of glowing lava. Hold fire to spray the particle lava (see "Particle lava") from the view, or from
  the weapon hand in VR (the hand pose in the usercmd; not tried in a headset). It uses the lava
  file's own emitter, one per player (`SV_Terrain_LavaSpray`), so any client's sprayer works on the
  server; drawing the lava is still listen-server only. The tank shows as the ammo count in percent: it
  drains at `workshop_lava_drain` (12) a second while the nozzle is open (about 8 s from full),
  starts refilling at `workshop_lava_refill` (5) a second after `workshop_lava_delay` (1 s) shut, and
  after running dry will not spray again until it is back to `workshop_lava_restart` (20) percent.
  The nozzle backs off when its eye is against rock. The stock `+terrain_lava` debug bind still
  works and needs no tool or tank. The HUD's ammo icon is whichever ammo type you hold, as for the other
  tools. Its view model (`v_lavagun.mdl`) is built by `Misc/workshop_assets/gen` like the others.
- **Desktop keys**: `9` the grapple, `0` the platform gun and `-` the lava sprayer (`-` was Quake's
  screen-size-down key; `sizedown` is still a console command) (weapon slots after Quake's 1-8; press
  again or pick any weapon to put the tool away), `F` throws a flare (`workshop_flare`, from the
  same flares a VR wrist carries, four that come back over time). New configs get these from the
  engine's `default.cfg`; on the first workshop map the client also binds whichever of the four
  keys are free (or hold Quake's no-op `impulse 0`, or for `-` the stock `sizedown`) and says so in the console;
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
| `Quake/sv_workshop.c`, `cl_workshop.c` | The workshop tools: server side (tool table, wheel and keys, platform gun, grapple, flares) and client side (key binds, light strings, VR wrist and holster). |
| `Quake/sv_terrain.c` | Server side: owns the region and the op log, entity placement, axe, jetpack, vault, explosion digging, flares, crawlers and their flow field, and walking routes for ordinary monsters. The grid follows the op log and marks cells inside the map's brushes closed. Each chased player has a near field (about 1900 units of walking around them, flooded again the same frame they move or the cave changes) and a whole-cave far field refreshed every second or two, one such flood a frame; the most chased player gets the larger near area and the faster refresh, so the pack keeps after whoever it is chasing. |
| `Quake/cl_terrain.c` | Client side: the client's own copy of the cave, generated from the seed the server sends and kept up to date by replaying its op stream. |
| `Quake/r_terrain.c` | Client side: meshes the client's copy on worker threads and draws it (CPU vertex lighting plus the client's dynamic lights, cached per chunk). |
| `Quake/r_terrain_tex.c`, `r_terrain_props.c`, `r_terrain_style.h`, `Shaders/terrain.*` | The looks: generated texture atlas, box-projected nearest-sampled shading, props and light strings placed from the seed. |
| `Quake/world.c`, `pmove.c`, `sv_move.c`, `sv_phys.c`, `pr_cmds.c` | Small hooks: traces, point contents, player movement, monster steps and `movetogoal` routes, explosion sniffing. |

## Design rules so far

- **The server owns the terrain.** State is seed + size + origin + an ordered list of ops
  (`SV_Terrain_OpLog`): dig and fill spheres, gingerbread disks, and the falling ice's detach and
  stamp. Every change goes through `SV_Terrain_PushOp` (`SV_Terrain_Edit` for spheres,
  `SV_Terrain_Disk` for disks). The generator and the
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
| `terrain_axefx` | 1 | axe bites play a hit sound and throw chips; 0 is off |
| `terrain_blast` | 64 | radius an explosion digs; 0 is off |
| `terrain_slope` | 0.55 | least walkable floor normal z on terrain |
| `terrain_monsterstep` | 1 | slope-tolerant monster steps |
| `terrain_pathfind` | 1 | walking monsters chasing a player follow a route through the cave; 0 is the stock chase |
| `terrain_monsterclimb` | 3 | 32-unit levels a walking monster may jump up onto a ledge on its route; 0 is none |
| `terrain_crawlers` | 4 | crawlers added at map load |
| `terrain_crawler_sense`, `terrain_crawler_speed` | 900, 210 | |
| `terrain_crawler_nav` | 2 | 0 straight at the enemy, 1 flow field through open air, 2 along surfaces |
| `terrain_crawler_pounce` | 360 | a stalled crawler jumps at a visible enemy within this range; 0 is off |
| `terrain_crawler_types` | `monster_demon1` | comma list of `classname[:scale[:health[:speed]]]`, cycled over the crawlers. With a mod that has one, e.g. `-game <mod> +terrain_crawler_types monster_spider:0.6:15:1.3,monster_spider:3:500:0.6` |
| `terrain_ice` | 1 | ice pillars, icicles and floor stubs in new caves |
| `r_terrain_style`, `r_terrain_props` | 1, 1 | look 0-3 and its props |
| `workshop_tools` | 1 | the platform gun, grapple and lava sprayer on drg* maps |
| `workshop_platform_radius`, `workshop_platform_thickness` | 60, 20 | platform size |
| `workshop_platgun_speed`, `workshop_platgun_damage`, `workshop_platgun_regen` | 1250, 40, 10 | |
| `workshop_grapple_speed`, `workshop_grapple_pull`, `workshop_grapple_range` | 1100, 620, 1600 | |
| `workshop_lava_drain`, `workshop_lava_refill`, `workshop_lava_delay`, `workshop_lava_restart` | 12, 5, 1, 20 | lava sprayer tank: percent per second used and refilled, seconds shut before refilling, percent needed to restart after running dry |
| `workshop_flares`, `workshop_flare_regen` | 4, 15 | flares a VR wrist carries, seconds per flare back |
| `vr_workshop_holsters`, `vr_workshop_throw_scale`, `cl_workshop_lights`, `cl_workshop_binds` | 1, 2.5, 1, 1 | client side |
| `r_terrain_ambient`, `r_terrain_lamp`, `r_terrain_dlights`, `r_terrain_biome` | | look |

Debug commands: `terrain_probe`, `terrain_ents`, `terrain_wake`, `terrain_dig`, `terrain_fill`,
`terrain_spawn`, `terrain_bench`, `terrain_clhash`.

## Tests

- `gcc -O2 -std=c11 -IQuake tests/terrain_fixture.c Quake/terrain.c Quake/terrain_ice.c -lm && ./a.out`
  prints `TERRAIN_FIXTURE_PASSED`; the same with `tests/ice_fixture.c` prints
  `ICE_FIXTURE_PASSED` (a cut, a detach and two stamps replay to the same hash, directly and
  through the netsync wire form).
- `tests/run-headless.sh tests/terrain_<name>.gdb [basedir]` drives the engine with gdb inside a
  headless sway session (Linux, real GPU, no window) and leaves `id1/condump.txt`. Each script's
  header says what to expect. `terrain_pathfind.gdb` compares monsters' route costs to the player
  with routes on and off (`terrain_ents` prints `route N`: about 2 per 32 units walked).
  `terrain_netsync.gdb` checks that the client's copy matches the server's region after edits.
  `workshop_tools.gdb` fires the platform gun and the grapple, then walks the mouse wheel through
  the weapons and tools; `workshop_lava_tool.gdb` runs the lava sprayer (tank drain and refill, running dry, melting
  a wall, the stock weapon held back). `terrain_combined.gdb` runs the worm, crawlers, a falling pillar, lava and
  a platform in one map and compares the client's copy with the server's at the end.

## Known gaps

- No headset pass (the wrist flares and hip holster are untested in VR), no remote-client pass
  of the netsync, no VR-hand digging.
- A remote client gets every terrain op, ice included, so its cave matches; but the lava
  particles, the falling ice bodies and the worm's debug view are drawn from server memory and
  are missing there (not tried with a remote client). The stamp op uses float maths to place the brick; the same binary
  replays it exactly, a client built by another compiler might differ by a sample.
- The region covers the map up to 12 chunks (6144 units) a side; a larger map keeps a flat
  undiggable wall where the region ends.
- Walking routes are for ground monsters chasing a player. Flyers, swimmers, monsters fighting each
  other and `path_corner` patrols still use the stock chase. A monster drops off ledges up to 128
  units high and jumps up to `terrain_monsterclimb` levels, but cannot jump a gap or follow a
  player jetpacking high up a wall. A monster stuck for three seconds gets a second of the stock
  chase to shake loose; a wedged one is pushed out of the terrain.
- Crawler surface pathing is new: the field (about 39k surface cells at 32 units) is rebuilt whole
  four times a second, about 3 ms each time on one core; it should be time-sliced. In seed 1 one
  spawn spot still traps a crawler that hops in place.
- Crawlers keep out of the map's brushes and do not walk into other monsters or players, but
  they test bodies as spheres, not boxes.
- Terrain lighting is still computed on the CPU (cached per chunk, redone only when its inputs
  change); each frame copies the visible chunks' lit vertices into the frame's vertex memory.
  Moving the lamp and dynamic lights into the shader would remove that copy. Falling ice bodies
  are relit every frame they move.
