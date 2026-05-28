# QuakeSpasm-OpenVR (Custom Build)

This is a customized fork of QuakeSpasm-OpenVR, optimized for VR gameplay with dual-binding support and classic engine defaults.

> [!IMPORTANT]
> **VR Multiplayer Fixes**: This build specifically addresses and fixes issues with VR clients in multiplayer sessions, ensuring stable connectivity and synchronized gameplay. This includes robust synchronization of **weapon positions** and **playspace offsets**, ensuring VR players are correctly represented for all participants.

There is however no big further development planned apart from minor features and bugfixes.

If your are looking for a much more feature rich VR experience, check out [Quake VR](https://github.com/vittorioromeo/quakevr) (see section below on differences between both projects).

Here are the changes to the last iterations of QuakeSpasm-OpenVR by [vittorioromero](https://github.com/vittorioromeo/Quakespasm-OpenVR/tree/wip) and [Fishbiter](https://github.com/Fishbiter/Quakespasm-OpenVR):

- Update to most current [QuakeSpasm](https://github.com/sezero/quakespasm) (v0.96.3)
- 64bit build
- Added head-based movment (in addition to controller-based movement)
- **Advanced Networking**: VR clients are now fully supported in multi-client sessions with accurate weapon and playspace synchronization.
- Improved controller binding with Dual-Bind support (Triggers/Buttons work alongside desktop keys)
- Support for [enhanced weapon models](#enhanced-models)
- Default setting: Linear texture filtering and blocky particles for classic look.
- Various other fixes and tweaks for cross-platform compatibility.

## Functional Changes From Main

This branch carries additional mod-compatibility, co-op, and multiplayer networking changes beyond the main/master branch.

- **CSQC compatibility:** the client can load a lightweight `csprogs.dat` and run supported CSQC entry points such as `CSQC_Init`, `CSQC_DrawHud`, and `CSQC_DrawScores`. It also supports CSQC drawing helpers, client stat reads, string helpers, and command checks needed by several modern mods. Full DarkPlaces/QuakeWorld CSQC is still not implemented.
- **Extended client stats:** the protocol-side stat table was expanded to 256 slots, with integer, float, and string stat support. Server-side QuakeC can register extra stats through `clientstat`, and the client exposes them through `getstati`, `getstatf`, and `getstats`.
- **Modern protocol defaults:** the default server protocol is RMQ protocol `999`, with higher model/entity/cache limits for large mods. Protocol `15` and FitzQuake protocol `666` remain available through `sv_protocol` or `-protocol`.
- **Large update handling:** server unreliable updates honor `sv_maxpacketsize`, split oversized updates without repeating snapshot timing data, and prioritize nearby/front-facing entities when packets are tight.
- **Networking hardening:** the datagram layer has a shared-socket path, NAT port-remap recovery, client port probing, stale-input clearing, send pacing, and lag diagnostics aimed at reducing VR multiplayer judder and delayed movement after packet gaps.
- **Co-op fixes:** players can be made non-solid to each other in co-op, weapon pickup targets can fire correctly for all players, optional pickup/ammo target fixes are available for mods, and co-op revive/ammo respawn helpers are available.
- **Gameplay compatibility fixes:** elevator/pusher behavior is more tolerant, QuakeC `random()` avoids returning exact 0/1 by default, and entity scaling is kept compatible with mods such as QBJ3 that break when static/baseline scale is encoded into signon data.
- **Weapon wheel compatibility:** the VR weapon wheel and weapon-offset presets include additional mod weapons, including Arcane Dimensions, Alkaline, Enyo, Tomb of Thunder, and QBJ3 model sets.
- **Config control:** `-postcfg <file.cfg>` executes a config after normal startup/game configs, which is useful for universal local bindings that should win over mod `quake.rc` or `autoexec.cfg` files.
- **Multiplayer save opt-in:** co-op multiplayer saves can be enabled for controlled server testing, but the current save format still supports only one active player in client slot 0.

## Quake VR vs QuakeSpasm-OpenVR?

Vittorio Romeo expanded `QuakeSpasm-OpenVR` considerably into the most excellent [Quake VR](https://github.com/vittorioromeo/quakevr), which is definitely a much more feature rich VR implementation - including teleportation, finger tracking, VR interactions, two-handed weapons, dual wielding, holsters and much more.

It is however a heavily modified version of QuakeSpasm, which is not compatible with enhanced mods (like Arcane Dimensions) out-of-the-box, and does not allow multiplayer-crossplay with non-VR players.

I would recommend you to enjoy [Quake VR](https://github.com/vittorioromeo/quakevr) primarily with vanilla Quake, it's official expansions as well as supported maps.

For a more bare-bone experience supporting all features of current QuakeSpasm (e.g. support for Arcane Dimensions and the Enhanced Edition), or cross-play multiplayer with non-VR-players QuakeSpasm-OpenVR would probably be the better option.

## History

This fork of [QuakeSpasm](https://github.com/sezero/quakespasm)...

- builds on the most current `QuakeSpasm-OpenVR` version from [vittorioromeo/Quakespasm-OpenVR](https://github.com/vittorioromeo/Quakespasm-OpenVR/tree/wip)
  - which was forked from [Fishbiter's improvement on Zackin5's version](https://github.com/Fishbiter/Quakespasm-OpenVR)
    - which was forked from [Zackin5's OpenVR port of Dominic Szablewski's (Phoboslab) Oculus modification of Quakespasm](https://github.com/Zackin5/Quakespasm-OpenVR)
      - which was forked from [Dominic Szablewski's (Phoboslab) Oculus modification of Quakespasm](https://github.com/phoboslab/Quakespasm-Rift) and utilizing the [OpenVR C wrapper by Ben Newhouse](https://github.com/newhouseb/openvr-c).

## Setup and Usage

Extract the most recent release into your `Quake` or `Quake\rerelease` folder (where the subfolder `Id1` resides).

Launch `quakespasm-openvr.exe`.

### HD Textures

There is a [HD textures](https://drive.google.com/file/d/1UAH4la2uOv3lwMkMk05yZuYmiPIyExU_/view?usp=sharing) package available. Simply extract the zip-file into the `Id1` subfolder (where `PAK0.PAK` is located).

You can also download a HD texture pack for __Arcane Dimensions__ [here](https://www.moddb.com/games/quake/addons/hires-texture-pack-for-arcane-dimensions). Simply extract the `textures` folder into your `ad` directory.

### Enhanced Models

There are also 3 mods available containing enhanced models for enemies and weapons. These can also be used with QuakeSpasm-OpenVR.

- [__Plague's Weapon Pack for VR__]:

  This pack contains the fully modelled weapons by [Plague](https://members.optusnet.com.au/%7eplaguespak/), adapted and animated for VR by Skizot, and expansion weapons added. This pack is perfect for VR.

  To use them with QuakeSpasm-OpenVR, extract `pakz.pak` into your `id1` subfolder and rename it by changing the `z` to a number higher then the highest existing `pak`-file inside your `id1` folder. If you are using `pak` files from vanilla Quake this will be `pak2.pak`, and if you're using the Re-Release, it will be `pak1.pak`.
  
  For the expansions, as as well as Arcane Dimensions, do exactly the same with the `hipnotic`, `rogue` and `ad` subfolders. There is also a special pack available for the Alkaline mod. Simply extract it into the `alk` subfolder.
  
  You will notice, that the weapon offsets and scaling will be off. To switch to the correct offsets, access the `VR Options` in Quake's main menu and switch `Gun Model Offsets` from `Vanilla` to `Plague`. (Note that you will have to do that for each expansion/add-on you load, since Quake writes separate configs per mod.)

- [__Enhanced Model Conversions Pack__](https://quakeone.com/forum/quake-mod-releases/finished-works/283295-osjc-s-enhanced-quake1-model-conversions-pack-v1):

  This pack is a conversion of the enhanced models from Quake's Re-Release. Models from the expansions are missing though. There are 2 ways to use it with QuakeSpasm-OpenVR:

  - Extract the `enhanced` folder of the downloaded archive into your `Quake` folder, and start the game with `-game enhanced`. This should automatically load vanilla Quake with the new models and correctly apply the correct weapon offsets.
  - If you want to use the new models globally with all expansions and add-ons, rename `pak0.pak` from the `enhanced` folder by changing the `0` to a number higher then the highest existing `pak`-file inside your `id1` folder. If you are using `pak` files from vanilla Quake this will be `pak2.pak`, and if you're using the Re-Release, it will be `pak1.pak`. You will notice, that the weapon offsets and scaling will be off. To switch to the correct offsets, access the `VR Options` in Quake's main menu and switch `Gun Model Offsets` from `Vanilla` to `Enhanced`. (Note that you will have to do that for each expansion/add-on you load, since Quake writes separate configs per mod.)

- [__Authentic Model Improvements__](https://github.com/NightFright2k19/quake_authmdl):

  This pack contains considerably more models as the one above - including converted ones from the Re-Release, but some weapons look worse than Plague's Weapon Pack and the Enhanced Model Conversion Pack linked above. To use them with QuakeSpasm-OpenVR, extract it into your `Quake` folder and rename the `pakz.pak` files by changing the `z` to a number higher then the highest existing `pak`-file inside your `id1`, `hipnotic`, and `rogue` folders. You will notice, that the weapon offsets and scaling will be off. To switch to the correct offsets, access the `VR Options` in Quake's main menu and switch `Gun Model Offsets` from `Vanilla` to `Authentic`. (Note that you will have to do that for each expansion/add-on you load, since Quake writes separate configs per mod.)

- [__Block-Quake__](https://kebby-quake.itch.io/block-quake):

  A total conversion mod for Quake  featuring familiar plastic blocks.

  To use them with QuakeSpasm-OpenVR, extract `blockquake_vanilla.pak` into your `id1` (or any mod) subfolder and rename it to a number higher then the highest existing `pak`-file inside your `id1` folder. If you are using `pak` files from vanilla Quake this will be `pak2.pak`, and if you're using the Re-Release, it will be `pak1.pak`.
  
  You will notice, that the weapon offsets and scaling will be off. To switch to the correct offsets, access the `VR Options` in Quake's main menu and switch `Gun Model Offsets` to `Block-Quake`. (Note that you will have to do that for each expansion/add-on you load, since Quake writes separate configs per mod.)

You can also use multiple MODs in conjunction. E.g. load the Authentic pack first as e.g. `pak1.pak` to get the wide arrange of models and then the Plague's Weapon Pack second e.g. as `pak2.pak` to get the better VR-optimized weapon models. Of course you have to set `Gun Model Offsets` to `Plague` in this case.

### Controls

Both head-based (default) and controller-based movement is supported. You can change it in the VR options.

Input from VR Controllers are mapped to various joystick-related input (except the left Application Menu button is bound to `ESCAPE`). The fork comes with the following reasonable default binding:

| Controller Button | Key Mapping | Default Action |
| ----------------- | ----------- | -------------- |
| Left Trigger | `LTRIGGER` | Jump |
| Right Trigger | `RTRIGGER` | Attack / Enter in Menu |
| Left Application Menu / B Button | `ESCAPE` | Toggle Menu / Escape |
| Right Application Menu / B Button | `BBUTTON` | Next Weapon |
| Left Pad/Stick Click | `LTHUMB` | Run |
| Right Pad/Stick Click | `RTHUMB` | Jump |
| Left Grip | `LSHOULDER` | Show Scores |
| Right Grip  | `RSHOULDER` |  Show Scores  |
| Left A Button | `ABUTTON` | Show Scores |
| Right A Button | `XBUTTON` | Previous Weapon |
| Right Axis 2 Press | `YBUTTON` | _none_ |
| Right Pad/Stick Up | `UPARROW` | _none_ |
| Right Pad/Stick Down | `DOWNARROW` | _none_ |
| Right Pad/Stick Left | `LEFTARROW` | _none_ |
| Right Pad/Stick Right | `RIGHTARROW` | _none_ |

#### Important infos

- On Windows SteamVR, run the included `Install SteamVR Controller Bindings.bat` after starting SteamVR to import the recommended Valve Index legacy bindings automatically. If the game has never appeared in SteamVR's controller binding UI, launch `quakespasm-openvr.exe` once first.
- In SteamVR's default Legacy bindings, controllers with a dedicated `A` button (e.g. Index Controllers) cannot use this button independently from the `Grip` button. The included binding installer fixes this by mapping `A Button` Click to `Left/Right A Button` instead of `Grip Button`.
- `Right Axis 2 Press` is not mapped at all in SteamVR's default Legacy bindings. The included Index binding maps it to `Right Touchpad Click` to expose an additional button.
- By default, the right pad/stick is configured for smooth/snap turning. If you use real roomscale-turning, you can set `Turn Speed` in the VR-Settings to the lowest setting (0) to turn this off. Then you can rebind the pad/stick like a D-Pad with 4 directions. You can use these 4 additional bindings e.g. for quick-loading/-saving or mapping of specific weapons.
- Check out the Community Binding `Index Controller Bindings` in SteamVR for a preset for Index Controllers, that makes the maximum buttons available for binding.

#### Tips on weapon selection

Quick weapon selection is not so easily achievable with the limited buttons available on VR controllers, and just relying on next/previous weapon buttons is a disadvantage. There is a trick to mitigate this:

In Quake the normal _Nailgun_ gets arguably obsolete, once the _Super Nailgun_ is obtained. It is the same with the _Shotguns_, as well as _Grenade- and Rocket-Launchers_.

__QuakeSpasm__ allows a button-binding e.g. like this: `impulse 4; wait; impulse 5`. This will select the _Nailgun_, if you have it, and then immediately try to select the _Super Nailgun_, if you have it. Effectively using this button as "___Gimme the best nailgun!___".

Now, if you use roomscale-turning, you have the right stick free for additional bindings. You could e.g. bind the directions like this in the `config.cfg`:

```txt
bind "UPARROW" "impulse 2; wait; impulse 3"
bind "DOWNARROW" "impulse 6; wait; impulse 7"
bind "LEFTARROW" "impulse 8"
bind "RIGHTARROW" "impulse 4; wait; impulse 5"
```

Throw in `bind "RTHUMB" "impulse 1"` to select the Axe on stick-press, and the perfect weapon for the current situation will always just a push or click away.

### Mission Packs, Add-Ons and Mods

All mission packs, add-ons and mods (supported by QuakeSpasm) should work out of the box. This includes:

- Scourge of Armagon
- Dissolution of Eternity
- Dimension of the Past
- Dimension of the Machine
- Arcane Dimensions (be sure to place it in a `ad` subfolder)
- [Alkaline](https://alkalinequake.wordpress.com/) (be sure to place it in a `alk` subfolder)
- [Slave Zero X: Episode Enyo](https://poppyworks.itch.io/episode-enyo) (be sure to place it in a `enyo` subfolder)
- [Tomb of Thunder](https://youtu.be/iA56E7Rvc6A) (be sure to place it in a `tombofthunder` subfolder)
- [Block-Quake](https://kebby-quake.itch.io/block-quake) (be sure to set `Gun Model Offsets` in the `VR Options` to `Block-Quake`)
- etc.

As usual, expansion packs and mods are placed inside subfolders and then launched by stating the subfolder via the `game` parameter (e.g. `quakespasm-openvr.exe -game hipnotic`).

Quake Enhanced Edtion (aka Re-Release) stores it's Add-Ons in `C:\Users\<your-user>\Saved Games\Nightdive Studios\Quake\`. You have to copy the subfolders (e.g. `honey` or `q64`) of this folder into the folder where `quakespasm-openvr.exe` is located and launch the Add-On like stated above.

#### Known Issues

- Arcane Dimensions, and Alkaline
  - When launching one of these mods, it will not display anything in VR at first. Press the __Enter__ key twice in order to get in game and play in VR.
    Alternatively you can also add `+map start` to your start script to circumvent this problem. (e.g. `quakespasm-openvr.exe -game ad +map start`)
- The Spiritworld
  - When launching this mod, it will not display anything in VR at first. Press the __Esc__ key and then the __Enter__ key twice in order to get in game and play in VR.
    Alternatively you can also add `+map start` to your start script to circumvent this problem. (e.g. `quakespasm-openvr.exe -game spiritworld +map start`)
- Underdark Overbright & Copper
  - Water is rendered differently per eye in Underdark Overbright & Copper. The problem can be alleviated a bit by setting `r_wateralpha "0"` in your `config.cfg`.

### Cvars

- `vr_enabled` – 0: disabled, 1: enabled
- `vr_crosshair` – 0: disabled, 1: point, 2: laser sight
- `vr_crosshair_size` - Sets the diameter of the crosshair dot/laser from 1-32 pixels wide. Default 3.
- `vr_crosshair_depth` – Projection depth for the crosshair. Use `0` to automatically project on nearest wall/entity. Default 0.
- `vr_crosshair_alpha` – Sets the opacity for the crosshair dot/laser. Default 0.25.
- `vr_aimmode` – 7: Head Aiming, 2: Head Aiming + mouse pitch, 3: Mouse aiming, 4: Mouse aiming + mouse pitch, 5: Mouse aims, with YAW decoupled for limited area, 6: Mouse aims, with YAW decoupled for limited area and pitch decoupled completely, 7: controller attached. Default 7. (Note I haven't been very careful about maintaining these other modes, since they're obsolete from my point of view).
- `vr_deadzone` – Deadzone in degrees for `vr_aimmode 5`. Default 30.
- `vr_viewkick`– 0: disables viewkick on player damage/gun fire, 1: enable
- `vr_world_scale` - 1: Size of the player compared to normal quake character.
- `vr_floor_offset` - -16: height (in Quake units) of the player's origin off the ground (probably not useful to change)
- `vr_snap_turn` - 0: If 0, smooth turning, otherwise the size in degrees of each snap turn.

---
__New cvars for analog stick (and touchpad?) tuning on VR controllers.__ Default values should behave the same as before, but note that this version has not been tested with snap turning enabled. These have only been tested with analog sticks (Oculus Touch and Index Controllers), no idea how they behave with Vive touchpads.

- `vr_joystick_yaw_multi` - 1.0: Adjusts turn speed when using VR controllers, suggested 2.0-3.0
- `vr_joystick_axis_deadzone` - 0.25: Deadzone value for joysticks, suggested 0.1-0.2
- `vr_joystick_axis_exponent` - 1.0: Exponent for axis input, suggested 2.0. Larger numbers increase the 'low speed' portion of the movement range, numbers under 1.0: decrease it, 1.0 is linear response. 2.0 makes it easier to make fine adjustments at low speed
- `vr_joystick_deadzone_trunc` - 1 If enabled (value 1) then minimum movement speed will be given by the deadzone value, so it will be impossible to move at speeds below the deadzone value. When disabled (value 0) movement speed will ramp up from complete standstill to maximum speed while above the deadzone, so any speed is possible. Suggest setting to 0 to disable

### Additional Branch Cvars

These cvars are custom additions or important changed defaults in this branch compared with main/master.

| Cvar | Default | Description |
| ---- | ------- | ----------- |
| `cl_nocsqc` | `0` | Set to `1` to disable loading client-side QC/CSQC. Useful when isolating a mod HUD or CSQC compatibility issue. |
| `cl_netfps` | `72` | Caps remote-client `CL_SendCmd` rate. Set to `0` to disable the cap. Helps VR clients avoid flooding a lower-rate server with redundant move packets. |
| `cfg_unbindall` | `1` | Controls whether generated `config.cfg` files start with `unbindall`. Set to `0` if you need persisted binds to merge instead of clearing first. |
| `cl_extrapolate` | `0.05` | Allows a small amount of client interpolation/extrapolation tolerance for remote snapshots. |
| `pr_checkextension` | `1` | Controls advertised QuakeC extension support. This branch advertises only implemented extensions such as `FTE_QC_CHECKCOMMAND`. |
| `sv_maxpacketsize` | `1400` | Maximum unreliable datagram size sent to remote clients. Keep near MTU to avoid UDP fragmentation; raise only for controlled networks. |
| `sv_netsort` | `1` | Uses an Ironwail/QSS-style entity priority sort so packet pressure drops distant or behind-camera entities before nearby/high-priority entities. |
| `sv_inputtimeout` | `0.25` | Clears stale movement/buttons after this many seconds without fresh input from a client. Set to `0` to disable. |
| `sv_freezenonclients` | `0` | When enabled, server physics runs clients/world only. This is mainly a diagnostic or special server control, not a normal gameplay setting. |
| `sv_gameplayfix_elevators` | `2` | Pusher/elevator fix. `0` off, `1` clients only, `2` all entities. Helps prevent entities from blocking lifts due to tiny contact errors. |
| `sv_gameplayfix_random` | `1` | Makes QuakeC `random()` return values strictly between 0 and 1, avoiding exact edge cases that can break some logic. |
| `sv_coop_noplayerclip` | `1` | In co-op, lets active players pass through each other for normal movement traces while preserving missile and point traces. |
| `sv_coop_weapon_targetfix` | `1` | In co-op, fires weapon pickup targets for later players when the mod's `weapon_touch` path would otherwise consume the trigger. `2` also attempts custom weapon touch handlers. |
| `sv_coop_pickup_targetfix` | `0` | Optional co-op target fix for non-weapon pickups. Requires class filtering through `sv_coop_pickup_targetfix_classes`. |
| `sv_coop_pickup_targetfix_classes` | empty | Comma/semicolon/space-separated classnames eligible for `sv_coop_pickup_targetfix`, for example `item_artifact_super_damage,item_health`. |
| `sv_coop_pickup_targetlog` | `0` | Logs pickup target state after touches to help audit mods for co-op trigger issues. |
| `sv_coop_ammo_respawn` | `0` | Respawns supported ammo pickups in co-op using the mod's `SUB_regen` path when available. |
| `sv_coop_ammo_respawn_time` | `30` | Respawn delay, in seconds, for `sv_coop_ammo_respawn`. |
| `sv_coop_revive` | `1` | Enables the co-op melee revive helper. |
| `sv_coop_revive_health` | `25` | Health restored by the co-op revive helper. |
| `sv_coop_revive_range` | `96` | Trace range for co-op revive attempts. |
| `sv_save_multiplayer` | `0` | Allows saving co-op multiplayer games only when set to `1`. Current save/load support is intentionally limited to one active client in slot 0. |
| `sv_cmdfile` | empty | Dedicated-server command file name relative to the current game directory. When present, the server executes and deletes the file each frame after reading it. |
| `net_lagdebug` | `0` | Enables verbose network/judder diagnostics for datagram gaps, dropped unreliable packets, frame spikes, stale input, and interpolation overruns. |
| `net_lagdebug_threshold` | `0.25` | Datagram-gap threshold, in seconds, for `net_lagdebug` messages. |
| `net_lagdebug_frame_threshold` | `0.05` | Frame/update-gap threshold, in seconds, for `net_lagdebug` messages. |
| `net_singlesocket` | `1` | Uses one UDP socket for accept/control and game traffic on the server, with queued dispatch to per-client logic. |
| `net_sameip_stale_timeout` | `3.0` | Time before stale same-IP connection state can be discarded during reconnect/NAT-remap handling. |
| `cl_netport` | `0` | Preferred local UDP port for the client. `0` lets the OS choose. |
| `cl_portpingprobe_enable` | `1` | Enables client connect-time server-info probes across candidate local ports. |
| `cl_portpingprobe_probes` | `6` | Number of local-port probes to try when `cl_portpingprobe_enable` is on. |
| `cl_portpingprobe_delay` | `0.20` | Seconds to wait for port-probe replies before falling back. |

### Additional Launch Flags And Commands

| Option | Description |
| ------ | ----------- |
| `-postcfg <file.cfg>` | Executes a relative config file after normal startup configs. With `+game`, it is queued after the new game's `quake.rc`/autoexec path. Multiple `-postcfg` entries are allowed and execute in command-line order. |
| `-protocol <15\|666\|999>` | Selects the server protocol before startup. RMQ `999` is the default in this branch. |
| `sv_protocol <15\|666\|999>` | Console command to view or change the protocol used on the next level load. |

### Note about weapons

Quake's weapons don't seem to be particularly consistently sized or offset. To work around this there are cvars to position/scale correct the weapons. Working default offsets are included for the following weapons:

- Vanilla _Quake_, _Scourge of Armagon_ and _Dissolution of Eternity_ weapons (including the VR versions of Plague's weapon pack and Enhanced and Authentic Model Packs - [see info above for details](#enhanced-models)!)
- _Arcane Dimensions_ weapons (be sure to use folder-name `ad` and start game with `-game ad` to have them applied, [see info above for use of VR weapons](#enhanced-models))
- _Alkaline_ weapons (be sure to use folder-name `alk` and start game with `-game alk` to have them applied, [see info above for use of VR weapons](#enhanced-models))
- Weapons for [_Block-Quake_](https://kebby-quake.itch.io/block-quake) (set `Gun Model Offsets` in the `VR Options` to `Block-Quake`).
- Weapons for _Slave Zero X: Episode Enyo_ (be sure to use folder-name `enyo` and start game with `-game enyo` to have them applied).
- Weapons for [_Tomb of Thunder_](https://youtu.be/iA56E7Rvc6A) (be sure to use folder-name `tombofthunder` and start game with `-game tombofthunder` to have them applied).
- Weapons for _QBJ3_ (be sure to use folder-name `qbj3` and start game with `-game qbj3` to have them applied).

Unsupported mods may require new offsets. You can modify offsets by using the following cvars:

There are 25 slots for weapon VR offsets. There are 5 cvars for each (nn can be 01 to 25):

- `vr_wofs_id_nn` : The model name to offset (this name will be shown when equipping a weapon that doesn't have a VR offset
- `vr_wofs_scale_nn` : The model's scale
- `vr_wofs_x_nn` : X offset
- `vr_wofs_y_nn` : Y offset
- `vr_wofs_z_nn` : Z offset

Here are the `nn` values for all vanilla and mission pack weapons:

| Weapon | nn |
| ----------------- | ----------- |
| Axe | 01 |
| Shotgun | 02 |
| Super Shotgun | 03 |
| Nailgun | 04 |
| Super Nailgun | 05 |
| Grenade Launcher | 06 |
| Rocket Launcher | 07 |
| ___Scourge of Armagon (hipnotic):___ |
| Thunderbold | 08 |
| Mjolnir Hammer | 09 |
| Laser Cannon | 10 |
| Proximity Launcher | 11 |
| ___Dissolution of Eternity (rogue):___ |
| Lava Nailgun | 12 |
| Lava Super Nailgun | 13 |
| Multi Grenade Launcher | 14 |
| Multi Rocket Launcher | 15 |
| Plasma Gun | 16 |

You can place any modified cvars in an `autoexec.cfg` in the mod's directory to apply them for a mod, or in `id1` to apply them globally.

If you have found working values for a mod, feel free to create an issue, and i will try to include support for them out-of-the-box!

## Development and Building

### Merging current QuakeSpasm

Here is how to merge the current version of QuakeSpasm:

```git
git remote add sezero https://github.com/sezero/quakespasm.git
git fetch sezero --tags
git merge quakespasm-0.95.1 // use tag of new version to merge
```

### Building on Windows

Here is how to build this fork on Windows:

1. Install current version of Visual Studio (17.5.3 at the time of writing) with C++ workloads.
2. Open the file `.\Windows\VisualStudio\quakespasm.sln` in Visual Studio
3. Build `quakespasm-sdl2`.
4. As usual you also need a `id1` folder with a `PAK0.PAK` to be able to launch the game.
