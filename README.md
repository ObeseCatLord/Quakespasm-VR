# QuakeSpasm-OpenVR

This repository is a custom QuakeSpasm-OpenVR fork focused on VR play,
modern single-player mods, and same-build co-op multiplayer. It keeps the
gameflorist's QuakeSpasm-OpenVR lineage and VR controls, then adds CSQC
compatibility, QSS-M-inspired networking, co-op quality-of-life behavior,
modern particle/weather support, and per-mod VR weapon calibration.

The pre-integration default branch is preserved as `legacymain`. The current
default branch is the active VR/co-op/mod-compatibility branch.

## Goals

- Keep QuakeSpasm-OpenVR usable for both VR and `-novr` desktop play.
- Support large modern mods such as Arcane Dimensions, Alkaline, Dwell,
  Mjolnir, QBJ3, Raven Keep, Peril, Enyo, and Quake VR-derived mods.
- Prefer smooth latest-client co-op over legacy client interoperability. Networked
  clients and servers are expected to run the same build.
- Keep behavior close to QuakeSpasm, Ironwail, QSS-M, vkQuake, and FTEQW where
  those ports have established compatibility behavior.

## Lineage And Credits

This fork descends from:

- [QuakeSpasm](https://github.com/sezero/quakespasm), currently based on
  QuakeSpasm 0.96.3.
- Dominic Szablewski's Phoboslab Oculus/Rift QuakeSpasm work.
- Ben Newhouse's OpenVR C wrapper.
- Zackin5's OpenVR port.
- Fishbiter's QuakeSpasm-OpenVR improvements.
- Vittorio Romeo's QuakeSpasm-OpenVR and Quake VR work.
- gameflorist's QuakeSpasm-OpenVR, which contributed the modern QuakeSpasm
  base, 64-bit build, head/controller movement, VR multiplayer weapon and
  playspace sync, dual desktop/VR bindings, enhanced model support, classic
  visual defaults, SteamVR binding work, and cross-platform fixes.

This branch also references behavior and code from QSS-M, Ironwail, vkQuake,
and FTEQW for networking, mod compatibility, particles, rendering behavior, and
extended protocol support.

## Major Changes Since gameflorist's QuakeSpasm-OpenVR

### VR

- VR and desktop modes are both first-class. Use `-vr` or `-novr`.
- VR multiplayer sends weapon pose, muzzle pose, playspace information, and
  controller-related state.
- The VR weapon wheel detects more mod weapons, supports multiple weapons in a
  shared slot, and avoids non-weapon inventory models.
- `vrweapons.txt` can define held-weapon offsets, muzzle offsets, scales, and
  multiplayer-only deltas without hardcoding a mod in C.
- In-game calibration commands can freeze the current weapon and save
  controller-aligned held or muzzle offsets back to `vrweapons.txt`.
- Default bindings expose secondary fire through `+button3`: right mouse in
  `-novr`, Index right touchpad in VR, and right grip on other controllers.
- Text, menus, controller input, and desktop fallback crosshairs were adjusted
  for VR stereo correctness and desktop compatibility.

### Networking

- Clients and servers use latest-client movement packets with numbered
  commands, QSS-M-style move ACKs, and replacement-delta snapshot ACKs.
- Co-op servers default to QSS-M-style pacing with latest-input,
  server-authoritative movement. Trusted/server PMove remains off unless
  explicitly enabled for testing.
- Replacement-delta entity updates reduce the need for unsafe split snapshots.
- Snapshot resend, ACK recovery, entity prioritization, and pacing help busy
  co-op maps stay playable under packet loss.
- The datagram layer has shared-socket support, client port probing, NAT
  remap recovery, same-IP stale socket handling, and reorder recovery.
- Network diagnostics can log gaps, reorder events, overflows, prediction
  corrections, and per-client send behavior without requiring on-screen spam.

### Co-op

- Co-op defaults are tuned for same-build play: no friendly fire, non-solid
  players, QSS-M-style networking, weapon-target fixes, optional ammo respawn,
  autosaves, and death-location respawn helpers.
- Co-op respawn can keep vanilla weapons/ammo plus known extra weapon, ammo,
  item, key, and string-key fields used by modern mods.
- Optional respawn delay gives the team time to recover; if all players are
  dead, respawn falls back to standard spawn points.
- Pickup target fixes let co-op-only players trigger weapon and progression
  pickup logic that some single-player maps attach to item touches.
- Multiplayer save/load can be enabled for controlled co-op testing.

### Mod Compatibility

- Lightweight CSQC support can load `csprogs.dat` and run supported entry
  points such as `CSQC_Init`, `CSQC_DrawHud`, and `CSQC_DrawScores`.
- Extended client stats support integer, float, vector, and string stat data
  for modern HUDs.
- Engine extension queries and Quake 2021/rerelease builtin aliases are
  recognized by QuakeC/CSQC where this fork implements them.
- FTE/Ironwail/QSS-style particles, decals, rain, weather, beam polygons,
  particle descriptors, and expanded particle limits are available.
- Skyboxes, alpha sorting, skywind, optional skyroom rendering, and skyroom PVS
  support were added for mods that rely on newer source-port behavior.
- Entity and protocol limits are raised for large maps. Protocol 999 is the
  default server protocol, while older protocols remain selectable.
- Menu/config startup supports `-postcfg` for a final local config that runs
  after mod startup files.

### Rendering And Performance

- Alias model batching is available for repeated model draws.
- Alpha surfaces are sorted more safely for water, glass, and other translucent
  content.
- Warp texture rendering was kept compatible with classic QuakeSpasm behavior.
- Portal culling, skyroom rendering, particle density, and performance logging
  have cvars so expensive paths can be isolated during testing.
- Model interpolation and shadow exclusion lists include additional modern mod
  models that should not lerp or cast shadows.

## Running

Place the executable beside your `id1` folder or Quake rerelease game data.

```sh
./quakespasm-openvr.bin -vr -game ad
./quakespasm-openvr.bin -novr -game ad
./quakespasm-openvr.bin -dedicated 16 -game vr +coop 1 +map start
```

For a final local config that should win after mod configs:

```sh
./quakespasm-openvr.bin -game ad -postcfg local.cfg
```

For co-op servers, the intended baseline is:

```txt
deathmatch 0
coop 1
sv_cheats 1
sv_nofriendlyfire 1
host_framerate 0
host_timescale 0
sys_ticrate 0.05
sv_netsort 1
sv_maxpacketsize 1400
sv_replacement_maxpackets 0
sv_nqplayerphysics 1
sv_trustedmovement 0
sv_inputtimeout 0
net_lagdebug 0
max_edicts 15000
sv_gravity 800
sv_maxspeed 320
sv_accelerate 10
sv_friction 4
sv_stopspeed 100
edgefriction 2
sv_maxvelocity 2000
```

The tracked deploy scripts install this baseline as
`id1/codex_coop_server.cfg` and generate per-mod `start_*_server.sh`
wrappers that execute `quakespasm-openvr.bin` with it before loading a map.

Use `-novr` for desktop clients and `-vr` for headset clients. The protocol uses
QSS-M-style move ACKs; server PMove/trusted movement is off by default,
including mods that supply `SV_RunClientCommand`; set
`sv_nqplayerphysics 0` only when intentionally testing server PMove.
Startup runs `host_migrate_network_defaults` after configs to repair stale
archived values such as `host_maxfps 72`, nonzero
`host_framerate`/`host_timescale` overrides, non-default
`sys_ticrate`/`sv_maxpacketsize`, stale PMove/trusted-movement settings,
nonzero `sv_inputtimeout`, `net_lagdebug`, non-vanilla gravity, and capped
replacement bursts. Lower stale `max_edicts` values are raised to QSS-M's
`15000` default, while higher mod-specific values are preserved.
Deploy also scrubs retired client-side smoothing/extrapolation cvars from
existing configs; explicit `+cvar value` launch arguments are preserved.

## Building

Linux:

```sh
make -C Quake -f Makefile.linux -j$(nproc)
```

Windows cross-builds are produced through the repository's Windows build
scripts and must not depend on MinGW/GCC DLLs at runtime. Do not commit built
executables, object files, local logs, or generated Visual Studio output.

## Controls

The engine supports head-based movement and controller-based movement. VR
controllers are exposed through Quake key/button names so mods and configs can
bind them normally.

| Controller input | Quake binding | Default action |
| --- | --- | --- |
| Left trigger | `LTRIGGER` | Jump |
| Right trigger | `RTRIGGER` | Attack / menu select |
| Left menu / left B | `ESCAPE` | Menu / escape |
| Right menu / right B | `BBUTTON` | Next weapon |
| Left stick/pad click | `LTHUMB` | Run |
| Right stick/pad click | `RTHUMB` | Jump |
| Index right touchpad / other right grip | `VR_ALTFIRE` / `+button3` | Alt fire |
| Left grip | `LSHOULDER` | Scores |
| Right grip on Index | `RSHOULDER` | Scores |
| Left A | `ABUTTON` | Scores |
| Right A | `XBUTTON` | Previous weapon |
| Right stick/pad directions | Arrow keys | Available for custom binds |

In desktop/no-VR mode, right mouse defaults to `+button3` for secondary fire.

Useful commands:

| Command | Purpose |
| --- | --- |
| `menu_vr` | Opens the VR options menu. |
| `+vr_weaponmenu` | Opens the VR weapon wheel while held. |
| `vr_turn180` | Performs a 180-degree snap turn. |
| `vradjustweapon` | Freeze the current weapon and save a controller-aligned held offset. |
| `vradjustmpweapon` | Save a multiplayer-only held offset delta. |
| `vradjustmuzzle` | Freeze the current muzzle/projectile origin and save a muzzle offset. |
| `vradjustmpmuzzle` | Save a multiplayer-only muzzle offset delta. |
| `vrweaponoffsetglobal` | Promote the current weapon offset to a global mod offset. |
| `vrglobalweaponoffset` | Alias for `vrweaponoffsetglobal`. |

## Cvars

This table includes gameflorist's QuakeSpasm-OpenVR VR cvars and the cvars
added or changed by this active branch. It does not list every vanilla
QuakeSpasm cvar.

| Cvar | Default | Area | Description |
| --- | --- | --- | --- |
| `vr_enabled` | `0` | VR | Enables VR rendering and input. Set by `-vr`; cleared by `-novr`. |
| `vr_aimmode` | `7` | VR | Aiming mode. `7` is controller-attached aiming. |
| `vr_crosshair` | `1` | VR | VR crosshair mode. |
| `vr_crosshair_depth` | `0` | VR | Crosshair projection depth; `0` traces to nearby surfaces. |
| `vr_crosshair_size` | `3.0` | VR | Crosshair point/laser size. |
| `vr_crosshair_alpha` | `0.25` | VR | Crosshair opacity. |
| `vr_crosshairy` | `0` | VR | Vertical crosshair offset. |
| `vr_deadzone` | `30` | VR | Angular deadzone for older aim modes. |
| `vr_floor_offset` | `-16` | VR | Player origin floor offset in Quake units. |
| `vr_gunangle` | `32` | VR | Legacy gun pitch angle adjustment. |
| `vr_gunmodeloffsets` | `0` | VR | Selects built-in weapon offset presets. |
| `vr_gunmodelpitch` | `0` | VR | Extra weapon pitch adjustment. |
| `vr_gunmodelscale` | `1.0` | VR | Global held-weapon scale. |
| `vr_gunmodely` | `0` | VR | Extra weapon vertical adjustment. |
| `vr_haptic` | `1` | VR | Enables VR haptics. |
| `vr_hud_scale` | `0.025` | VR | VR HUD scale. |
| `vr_joystick_axis_deadzone` | `0.25` | VR | Stick/touchpad deadzone. |
| `vr_joystick_axis_exponent` | `1.0` | VR | Stick response curve. |
| `vr_joystick_axis_menu_deadzone_extra` | `0.25` | VR | Extra menu deadzone. |
| `vr_joystick_deadzone_trunc` | `1` | VR | Truncates stick output at the deadzone edge. |
| `vr_joystick_yaw_multi` | `1.0` | VR | VR stick turn multiplier. |
| `vr_lefthanded` | `0` | VR | Left-handed weapon/control mode. |
| `vr_menu_scale` | `0.13` | VR | VR menu scale. |
| `vr_movement_instant_stop` | `0` | VR | Optional instant stop for VR locomotion when input stops. |
| `vr_movement_mode` | `0` | VR | Selects VR movement direction mode. |
| `vr_movement_speed` | `1.0` | VR | Optional VR locomotion multiplier. |
| `vr_msaa` | `4` | VR | VR render MSAA sample count. |
| `vr_projectilespawn_z_offset` | `24` | VR | Legacy projectile spawn vertical offset. |
| `vr_snap_turn` | `0` | VR | Snap-turn angle; `0` means smooth turning. |
| `vr_180_snap_turn` | `1` | VR | Enables 180-degree snap-turn command support. |
| `vr_turn_speed` | `2` | VR | Smooth turn speed. |
| `vr_viewkick` | `0` | VR | Enables/disables damage and weapon view kick in VR. |
| `vr_world_scale` | `1.0` | VR | World-to-player scale. |
| `vr_wofs_*` | generated | VR weapons | Per-weapon held offsets generated for VR weapon slots. |
| `vr_wmuzzle_*` | generated | VR weapons | Per-weapon muzzle offsets generated for VR weapon slots. |
| `cl_beams_polygons` | `0` | Rendering | Enables polygon beam rendering path for supported effects. |
| `cl_confirmquit` | `0` | UI | Adds an optional quit confirmation. |
| `cl_coop_nametags` | `1` | Co-op | Draws co-op player nametags. |
| `cl_desktop_vanilla_run` | `1` | Input | Keeps faster vanilla-style movement behavior for desktop and VR defaults. |
| `cl_extrapolate*` | `0` | Networking | Retired no-op compatibility cvars retained only so old configs do not warn. |
| `cl_iDrive` | `1` | Input | QSS-M last-pressed-wins handling for opposing movement keys. |
| `cl_lerpdebug` | `0` | Diagnostics | Logs model/entity interpolation reset causes. |
| `cl_lerpdebug_models` | `""` | Diagnostics | Comma-separated model filter for `cl_lerpdebug`. |
| `cl_mousemenu` | `1` | UI | Enables mouse menu interaction. |
| `cl_mwheelpitch` | `5` | Input | Mouse-wheel pitch tuning. |
| `cl_net_lerpbuffer*` | `0` | Networking | Retired no-op compatibility cvars retained only so old configs do not warn. |
| `cl_netfps` | `0` | Networking | Retired no-op retained for old configs; QSS-M-style pacing is controlled by `host_maxfps`. |
| `cl_netport` | `0` | Networking | Requested local UDP client port; `0` lets the OS choose. |
| `cl_nocsqc` | `0` | CSQC | Disables client-side QC loading when set. |
| `cl_nopred` | `0` | Networking | Runtime prediction disable/debug switch. |
| `cl_portpingprobe_delay` | `0.20` | Networking | Delay before port-probe connect fallback. |
| `cl_portpingprobe_enable` | `0` | Networking | Optional client UDP port probe selection; disabled by default for normal OS-selected source ports. |
| `cl_portpingprobe_probes` | `6` | Networking | Number of client port probes. |
| `cl_predict_error_log` | `1` | Diagnostics | Logs prediction mismatches. |
| `cl_predictmove` | `1` | Networking | Enables client-side movement prediction for remote play. |
| `cl_predict_smooth*` | `0` | Networking | Retired no-op compatibility cvars retained only so old configs and launch scripts do not warn. |
| `cfg_unbindall` | `1` | Config | Allows configs to execute `unbindall`; set `0` to ignore it. |
| `freelook` | `1` | Input | Default mouse look behavior. |
| `max_edicts` | `15000` | Networking | QSS-M default entity capacity for large maps; higher mod-specific values are preserved by deploy cleanup. |
| `net_lagdebug` | `0` | Diagnostics | Logs datagram gaps, delayed packets, and lag events. |
| `net_lagdebug_frame_threshold` | `0.05` | Diagnostics | Frame-gap threshold for lag logs. |
| `net_lagdebug_threshold` | `0.25` | Diagnostics | Network-gap threshold for lag logs. |
| `net_sameip_stale_timeout` | `3.0` | Networking | Stale timeout for same-IP virtual sockets. |
| `sys_ticrate` | `0.05` | Networking | Dedicated server tick interval; default matches QSS-M's 20 Hz dedicated cadence. |
| `pm_airstep` | `""` | PMove | PMove air-step compatibility setting. |
| `pm_autobunny` | `""` | PMove | PMove auto-bunny compatibility setting. |
| `pm_bunnyfriction` | `1` | PMove | PMove bunny friction behavior. |
| `pm_bunnyspeedcap` | `""` | PMove | PMove bunny speed cap compatibility setting. |
| `pm_edgefriction` | `2` | PMove | PMove edge friction. |
| `pm_flyfriction` | `""` | PMove | PMove fly friction compatibility setting. |
| `pm_ktjump` | `""` | PMove | PMove jump compatibility setting. |
| `pm_pground` | `""` | PMove | PMove ground detection compatibility setting. |
| `pm_slidefix` | `1` | PMove | Enables slide/corner compatibility fixes. |
| `pm_slidyslopes` | `""` | PMove | PMove slope compatibility setting. |
| `pm_stepdown` | `""` | PMove | PMove step-down compatibility setting. |
| `pm_stepheight` | `""` | PMove | PMove step height override. |
| `pm_walljump` | `""` | PMove | PMove wall-jump compatibility setting. |
| `pm_watersinkspeed` | `""` | PMove | PMove water sink speed compatibility setting. |
| `pr_checkextension` | `1` | QC | Enables `checkextension` responses for supported extensions. |
| `r_alias_batching` | `1` | Performance | Batches alias model drawing where possible. |
| `r_alphasort` | `1` | Rendering | Sorts alpha surfaces for correct translucent rendering. |
| `r_bloodstains` | `1` | Particles | Enables blood stain decals. |
| `r_bouncysparks` | `1` | Particles | Enables bouncy spark behavior. |
| `r_decal_noperpendicular` | `1` | Particles | Avoids perpendicular decal placement artifacts. |
| `r_lightflicker` | `1` | Rendering | Enables light flicker behavior used by some maps. |
| `r_part_beams` | `1` | Particles | Enables particle beam effects. |
| `r_part_contentswitch` | `1` | Particles | Enables particle behavior changes by contents. |
| `r_part_density` | `1` | Particles | Particle density multiplier. |
| `r_part_maxdecals` | `8192` | Particles | Maximum decal count. |
| `r_part_maxparticles` | `65536` | Particles | Maximum particle count. |
| `r_part_rain` | `1` | Particles | Enables rain/weather particles. |
| `r_part_rain_quantity` | `1` | Particles | Rain particle quantity multiplier. |
| `r_part_sparks` | `1` | Particles | Enables spark particles. |
| `r_part_sparks_textured` | `1` | Particles | Uses textured spark particles. |
| `r_part_sparks_trifan` | `1` | Particles | Uses trifan spark drawing. |
| `r_particle_tracelimit` | `0x7fffffff` | Particles | Particle trace limit. |
| `r_particledesc` | `classic` | Particles | Particle descriptor set. |
| `r_particles` | `2` | Particles | Changed default particle mode. |
| `r_perfdebug` | `0` | Diagnostics | Logs expensive render frames. |
| `r_perfdebug_min_ms` | `8` | Diagnostics | Minimum render time for performance logs. |
| `r_skyroom` | `0` | Rendering | Enables optional QSS-style skyroom rendering. |
| `r_skywind` | `0` | Rendering | Sky wind amount parsed from worldspawn or set manually. |
| `r_useportalculling` | `0` | Performance | Enables portal/PVS culling experiment. |
| `scr_crosshair_desktop_fallback` | `1` | UI | Draws a fallback desktop crosshair when mod HUDs omit one. |
| `sv_airaccelerate` | `-1` | PMove | PMove air acceleration; negative means compatibility default. |
| `sv_cmdfile` | `""` | Server | Server command file hook. |
| `sv_coop_ammo_respawn` | `0` | Co-op | Enables co-op ammo respawn. |
| `sv_coop_ammo_respawn_time` | `30` | Co-op | Ammo respawn delay in seconds. |
| `sv_coop_autosave` | `1` | Co-op | Enables co-op autosaves. |
| `sv_coop_autosave_kill_interval` | `10` | Co-op | Kill-count interval for autosaves. |
| `sv_coop_autosave_min_interval` | `30` | Co-op | Minimum seconds between autosaves. |
| `sv_coop_autosave_slots` | `4` | Co-op | Number of rotating autosave slots. |
| `sv_coop_noplayerclip` | `1` | Co-op | Makes co-op players non-solid to each other. |
| `sv_coop_pickup_targetfix` | `0` | Co-op | Enables generic pickup target firing for configured classes. |
| `sv_coop_pickup_targetfix_classes` | `""` | Co-op | Class list for generic pickup target fixes. |
| `sv_coop_pickup_targetlog` | `0` | Diagnostics | Logs pickup target decisions. |
| `sv_coop_predictmove` | `0` | Co-op | Optional co-op PMove prediction support; disabled by default. |
| `sv_coop_progression_item_respawn` | `1` | Co-op | Respawns configured progression items. |
| `sv_coop_progression_item_respawn_classes` | `item_jboots item_jboots_timed` | Co-op | Classes treated as progression respawn items. |
| `sv_coop_respawn_delay` | `10` | Co-op | Delay before co-op respawn; `0` disables the delay. |
| `sv_coop_respawn_keep_weapons_ammo` | `1` | Co-op | Preserves weapons, ammo, keycards, and supported mod extra fields on respawn. |
| `sv_coop_respawn_near_player` | `1` | Co-op | Respawns near the death spot or living teammates when safe. |
| `sv_coop_weapon_targetfix` | `1` | Co-op | Allows weapon pickup trigger targets to fire in co-op. |
| `sv_gameplayfix_elevators` | `2` | Gameplay | Elevator/pusher step recovery; `0` off, `1` clients, `2` all entities. |
| `sv_gameplayfix_random` | `1` | QC | Avoids exact `0`/`1` returns from QuakeC `random()`. |
| `sv_inputtimeout` | `0` | Networking | Optional stale-input recovery timeout in seconds; default `0` matches QSS-M by preserving the last live movement command until the connection times out. |
| `sv_maxpacketsize` | `1400` | Networking | Remote unreliable packet cap, clamped to QSS-M's `DATAGRAM_MTU`. |
| `sv_netdiag_interval` | `5` | Diagnostics | Periodic network diagnostic interval in seconds. |
| `sv_netsort` | `1` | Networking | Sorts entity updates by priority before packet clipping. |
| `sv_nofriendlyfire` | `1` | Co-op | Disables friendly fire in co-op. |
| `sv_nqplayerphysics` | `1` | PMove | Master default-off switch for server PMove/trusted movement, including mods with `SV_RunClientCommand`; set `0` only for testing PMove. |
| `sv_pmove_legacy_preserve_qc_velocity` | `1` | PMove | Preserves QC velocity pushes such as grapples through legacy PMove. |
| `sv_replacement_maxpackets` | `0` | Networking | QSS-M-style uncapped replacement-delta drain by default; positive values manually cap split packets sent to one client per server frame. |
| `sv_save_multiplayer` | `1` | Save/load | Allows multiplayer/co-op saves in controlled use. |
| `sv_skyroom_pvs` | `1` | Rendering/server | Adds skyroom PVS for skyroom entity visibility. |
| `sv_spectatormaxspeed` | `500` | PMove | Spectator max speed. |
| `sv_triggerdebug` | `0` | Diagnostics | Logs trigger/touch decisions. |
| `sv_vr_jump_velocity` | `297` | VR/co-op | Optional VR jump velocity override; default is 10% above vanilla's `270`. |
| `sv_wateraccelerate` | `-1` | PMove | PMove water acceleration; negative means compatibility default. |
| `sv_waterfriction` | `4` | PMove | PMove water friction. |

Important changed defaults from gameflorist's QuakeSpasm-OpenVR:

| Cvar | Current default | Legacy default | Reason |
| --- | --- | --- | --- |
| `gl_texture_anisotropy` | `16` | `1` | Sharper texture filtering by default. |
| `r_particles` | `2` | `1` | Uses the extended particle path by default. |
| `r_nolerp_list` | expanded | shorter | Prevents interpolation on additional flame/saw/fist/fire models. |
| `r_noshadow_list` | expanded | shorter | Prevents shadows on additional beam/bolt/laser effects. |

## Diagnostics

Use these together with `-condebug` when investigating networking or mod
compatibility:

```txt
net_lagdebug 1
sv_netdiag_interval 5
cl_predict_error_log 1
cl_lerpdebug 1
r_perfdebug 1
sv_triggerdebug 1
```

`net_lagdebug` and related diagnostics should be treated as log tools, not as
normal player-facing output.

## Compatibility Caveats

- Full DarkPlaces/FTE CSQC is not implemented. This fork implements the subset
  needed by tested Quake mods and keeps expanding it as real mods require more.
- Legacy clients and servers are not a compatibility target. Run matching builds
  on the server and all clients.
- `r_skyroom` defaults to `0` to avoid an extra VR render pass on maps that do
  not need it.
- Very large maps can still be CPU-heavy in VR because this remains an older
  OpenGL QuakeSpasm renderer, not Ironwail's or vkQuake's newer renderer.
