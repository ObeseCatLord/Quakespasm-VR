# Explicit mod play through native game switching and map discovery

2026-09-30. Remaining UI-002 launch behavior found in the
[command-source checkpoint](migration-command-source-checkpoint.md). Full
migration and ordinary mod-map support remain required; this does not add
mod-specific rules. Work stays on2.0, with no builds/tests/probes until full
implementation ends. Quad views remain excluded from the parent scope.

## Reference and native evidence

Read-only primary pin51b452c0: `menu.c:3497–3521` queues `playgame` for installed
catalogue entries and installed mods, closing the menu into gameplay.
`common.c:2958–2967` checks the supplying searchpath's path_id, so a mod without
its own maps/start.bsp cannot accidentally launch id1's start. COM_Game_f
`:3226–3231` starts that map for explicit playgame or VR game changes, excluding
automatic reconnect. Reference `host_cmd.c:311–357` also exposes maps_mod using
the existing map inventory.

Native2.0 installed selection (`menu.c:4898–4901`) currently queues game and
returns to main menu. Installed catalogue activation uses its existing entry
owner (`:4533`). COM_Game_f builds native multi-game paths and calls
COM_SwitchGame; its retirement, mount order, assets, localization, weapon
calibration and config queue must remain. Async reconnect calls COM_SwitchGame
directly, so a command-local play adapter can avoid starting maps during that
workflow without adding another reconnect policy flag.

Native searchpath path_id and COM_FileExists already provide the primary's
supplied-start test (`common.h:423,456`). Native map discovery categorizes source
ownership and asynchronously loads descriptions (`host_cmd.c:230–430`), and the
native map/skill menus already select appropriate maps (`menu.c:5390–5452`).
Do not copy another map scanner or attach primary filelist state to native
records. Actual GUI/launch/cancellation/Linux/ARM behavior remains unverified.

## Minimal adapter and intended behavior

Register playgame against the existing COM_Game_f. Copy only the primary
active-supplying-path start test. After the native game switch/config queue,
explicit playgame launches the supplied start map, including explicit play of
an already active game. If no supplied start exists, explicit play opens the
existing native map browser for selection; dedicated console use prints the
absence rather than opening a GUI. Never silently play an inherited id1 map
for another mod. No map-name heuristic, campaign whitelist or alternate loader.

Normal desktop game commands keep native behavior. A normal game command in
an actually tracked VR session can retain the inherited supplied-start behavior;
automatic server-game switch/catalogue reconnect continue calling their native
owner directly and do not auto-start a local map. Manual game/play requests
continue cancelling any pending reconnect through the existing COM_Game_f
boundary. Do not bypass configuration, native renderer/model retirement or
registered-game admission.

Use the explicit play command for installed browser/catalogue activation;
reuse existing approved-entry validation, native input/menu closing and pointer
handling. Catalogue installation remains the existing backend; launch does not
implicitly download or overwrite data. Keep current native search/filter and
map/skill browsing instead of replacing the browser with the primary layout.
Any remaining inherited VR keyboard or metadata controls require their own
bounded source acceptance, not a broad UI-002 completion claim here.

For active-only map listing, adapt maps_mod within native Host_Maps_f using
existing source categories (active mod versus inherited base), retaining native
description/filter output. This is a small optional command surface on the same
inventory; do not add another searchpath scanner or modify normal maps output.

## Scope and final acceptance

Write set after timed reconnect finishes: Quake/common.c, menu.c and the native
Host_Maps_f/registration region in host_cmd.c. Estimate <=100 added production
lines. Main reviews reference behavior, async reconnect independence, path-id
ownership and actual input routes; local Astra source review if the change
reveals a subtle transition boundary. Reopen for another launch/config/map owner.

End-of-implementation checks: Linux/ARM builds; installed loose/PAK mods and
catalogue entries; explicit same/different game play; mod-owned start versus
inherited id1 start; absent start -> native map browser; config/vid_unlock
ordering; ordinary desktop game unchanged; actual VR game change; no local map
launch during server-game/download reconnect; registered admission and invalid
names; dedicated no-GUI behavior; selected/all active map listing; native
mouse/controller/VR-pointer activation. User live headset/performance/Windows
checks remain deferred. This plan alone does not complete UI-002 or the goal.

## Source implementation checkpoint

The three-file adapter adds39 lines and removes9. playgame uses COM_Game_f;
the supplying-path start test is copied from the primary. Explicit same-game
play works through the unchanged native same-game switch return, then queues
the start map or native menu_maps. New map/menu commands follow the native
queued config/vid_unlock commands after an actual switch. Normal desktop game
does not auto-launch; an actually tracked session uses the inherited start
behavior. Direct reconnect calls never pass through this command-local adapter.

Installed browser/catalogue activation closes the menu/input into gameplay and
queues playgame through one local helper. maps_mod uses native source-category
ordering to omit inherited base maps without another scanner; normal maps
keeps its existing filtering and description output. Main inspected actual
registration, path-id ownership, category definitions and input routes against
the primary. Scoped whitespace checks passed. No executable qualification has
run; the full acceptance matrix and broader UI-002 audit remain open.
