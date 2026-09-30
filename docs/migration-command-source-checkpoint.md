# Current inherited command-surface source checkpoint

2026-09-30. Bounded comparison of the primary literal command inventory against
current2.0 registrations and their actual owners. This is not full behavioral
or executable completion evidence. Old names alone do not prove missing
features; the user does not require legacy settings compatibility.

| Reference command / behavior | Current source evidence / disposition |
| --- | --- |
| sv_giveall / sv_givekeys / sv_god / sv_noclip | Confirmed absent before this turn, with existing inventory/key owners available. The [admin adapter plan](coop-admin-commands-2.0-plan.md) precedes the four-handler implementation and source review. |
| sv_reconnect_game / qs_reconnect_game | Confirmed missing timed sender/receiver behavior. Existing CL_StartAutoReconnect/CL_AutoReconnectFrame and datagram Start/Frame/Cancel are reusable; the [stage2 reconnect plan](coop-game-reconnect-2.0-plan.md) avoids a second connection state machine. |
| voice_list_devices / snd_spatial_status | Existing native `voice_devices` (`voice.c:1042`) and `spatial_status` (`snd_spatial.c:508`) expose the corresponding owners. No alias port is necessary just to preserve old spelling. Other voice/audio behavior needs its own evidence. |
| menu_weapons | Native keys menu includes stock and mission-pack impulse bindings plus weapon wheel (`menu.c:3940–3952`). Keep that native binding owner rather than copy another binding menu; command-name compatibility is not required. |
| menu_vr / menu_models | Native VROptions page (`menu.c:2638`) and player-model selection in multiplayer Setup (`:1354–1466`) reuse existing VR/avatar owners. Missing old menu command names do not require duplicating those pages. This does not prove every reference setting is covered. |
| menu_voice | Genuine remaining UI gap: primary voice controls (`menu.c:3710–3888`) include transmit/mode/device/gain/VAD/receive/radio/distance/HUD/local reflections. Current native SoundOptions (`menu.c:2388–2514`) exposes only sound/music/underwater controls; VR page exposes saved transmit preference (`:3344,3531`). The audio DSP/settings already exist. Plan a small menu adapter plus native settings actions, not another voice/capture implementation. |
| playgame and installed-mod launch controls | Primary COM_Game_f queues an active-mod `start` map for explicit playgame or VR game changes (`common.c:3226–3231`). Native installed-mod selection currently queues only game (`menu.c:4898–4901`). Native map/skill browsing remains valuable, but explicit inherited launch controls still need a bounded adapter plan and source acceptance. Avoid changing normal desktop game behavior merely to add that action. |

The unexamined literal differences remain unclassified, not assumed complete
or automatically required. Migration-default/probe/allocator/MP-offset command
names must be considered against current user scope and native owners before
porting. No builds, executable tests, engine probes or benchmarks were run.
Final Linux/ARM checks follow full implementation; user live checks are deferred.
