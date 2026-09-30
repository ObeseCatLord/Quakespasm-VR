# Inherited voice controls through the native menu and capture owners

2026-09-30. Identified by the [command-source checkpoint](migration-command-source-checkpoint.md).
This is a remaining full-migration implementation feature, not old-setting
name preservation. Desktop and VR need usable voice controls while keeping
native vkQuake sound/graphics menus and the existing microphone defaults.
No voice protocol, mixer, codec, capture route or settings store is replaced.
Implementation follows this plan on2.0 after current co-op adapters; builds
and tests remain consolidated at the end of the complete implementation pass.

## Reference and actual source

Read-only primary `quakespasm-openvr` pin51b452c0:
`Quake/menu.c:3710–3888` supplies the voice row order, labels, ranges and
input behavior. Its menu controls receive, transmit, VAD/PTT, input device,
gain, sensitivity, voice/radio level, spatial distance, HUD and local wet
microphone reflections/level. Native2.0 has these DSP/capture owners, but
`menu.c:2388–2514` SoundOptions contains only sound/music/underwater controls;
the VR page has only its saved microphone toggle (`:3344,3531`).

`voice.c` already owns SDL default/exact-name capture, active desktop/VR
profiles, refresh, settings persistence, device-loss handling, transmit/VAD/PTT
and speaker output. `voice_settings.c:133–134` defaults desktop transmit off
and VR on; empty device means system default. Preserve saved opt-out and
negotiated-session/input gating. Local reflections remain independent of
network transmission. Native cvars already own gain/VAD/output/distance/HUD;
the reference's adjustable local wet level still needs a small cvar adapter
to existing Spatial_SelfGain rather than another DSP path.

Current public `voice.h` lacks several direct UI actions/getters. Existing
console actions use a physical-Y pending-confirmation flow; a menu action
should operate as an explicit local user action, without sending the player
to a console confirmation. Keep the native settings owner and update/refresh
it there. The user already authorized microphone control and default-on VR.

Menu drawing runs in SCR_DrawGUI. `gl_screen.c:2571–2587` joins draw_done,
which depends on GUI completion, before returning to the next main input
frame. Expose a pure bounded voice settings view for draw; never call
Voice_Profile/Voice_SyncProfile, SDL enumeration, device changes, persistence
or capture refresh from GUI recording. Actions run through native main input.
No new lock, renderer snapshot owner or frame-wide CPU stall is needed.
Actual desktop/VR/profile/device/GUI behavior remains unexecuted evidence.

## Chosen adapter and scope

Copy the reference row/adjustment behavior into one native menu page, using
existing cb_context drawing, pointer sliders, keys/controller navigation,
canvas scaling and VR hit testing. Add a Voice Chat entry to native SoundOptions
and a route from VR Options, plus the direct menu command. Retain the parent
menu for Back. A local m_voice enum/page is ordinary native menu state, not a
parallel interface framework. Reusing only console commands would leave the
inherited visible controls missing; replacing native SoundOptions or adding
another audio implementation is rejected.

Add small actions/getters in voice.c/voice.h for the active profile's saved
transmit preference, mode, chosen device, local reflections and availability.
Reuse Voice_SaveSettings, Voice_StopTransmit, Voice_CloseCapture and
Voice_RefreshCapture; clear stale pending actions when a local menu action
supersedes them. Default system microphone stays selected unless the user
explicitly chooses another. Device cycling is main-thread SDL enumeration,
including the default entry, exact-name length/uniqueness checks and graceful
loss. Do not open devices or enumerate them in the GUI draw path.

VR and desktop transmission actions mutate the existing profile, sharing the
native save/stop/close/refresh boundary with Voice_SetVRTransmitEnabled rather
than creating another saved switch. Preserve the existing VR Options microphone
opt-out, which also disables local monitoring. The voice page's explicitly named
transmission control, like primary Voice_SetTransmitEnabled at voice.c:365,
changes transmission only: local wet monitoring remains separately controlled.
Do not restore a revoked monitoring preference automatically. A small common
action-finalization helper is acceptable; no additional policy state is needed.
VAD/PTT is profile-owned; cvar controls retain reference ranges. A small
archived local-wet-level cvar feeds existing Spatial_SelfGain at Voice_Frame,
with reference0..2 range. Reflection enablement uses native availability and
device checks; failed activation leaves the preference/result readable.
No accidental dry sidetone or transmit activation follows from wet monitoring.

Native Voice_PTTKeyEvent already requires a physical gameplay key with the
exact +voicerecord binding, and console command text cannot press PTT
(voice.c:737,810). The native Controls list currently lacks that binding entry;
add it to the existing keybinding list and a route to Controls from the voice
page. Do not copy the primary's obsolete second ptt_allowed permission policy:
native physical-input gating already owns this behavior. This entry and route
complete usable VAD/PTT selection rather than add another input owner.

Write set: Quake/menu.c, menu.h, voice.c, voice.h. Estimate <=600 added lines,
revised before implementation to include native slider/pointer dispatch, device
cycling and the verified Controls entry/route, which the original420-line
estimate did not account for fully;
no new module or persistent store. Reopen if a separate state/capture owner,
new renderer synchronization or protocol change is proposed. Source-review
the callback/main-thread boundaries locally with Astra after implementation.

## Implementation and final acceptance

1. Add native voice UI actions and pure getters, including USE_VOICECHAT-off
   stubs. Retain saved desktop/VR defaults, default microphone, device-loss
   and native transport/capture policy. Add only the missing wet-level adapter.
2. Port row/adjustment behavior to native menu layout/drawing and input; connect
   Sound/VR/direct routes and Back, pointer sliders, keyboard/controller and
   actual VR pointer dispatch. No renamed phantom cvars for profile controls.
3. At the end of complete implementation: Linux/ARM voice-on/off builds;
   desktop/VR saved switches and profile transitions; default/exact/missing/
   duplicate devices; VAD/PTT and gameplay versus menu gating; sliders/ranges;
   receive/HUD/radio/spatial controls; local wet-only level and independent
   transmit; global VR microphone opt-out stops monitoring as well; physical
   PTT binding/release/menu gating; unavailable audio/Steam Audio; return navigation and both-eye
   pointer/menu coordinates. User live microphone/headset/performance checks
   remain deferred. This plan alone does not complete the voice/UI feature.

## Requested-Astra source disposition

The first bounded source advisory identified three P2 defects. Main verified
the device-failure path, native mouse drag entry/return and capture publication
against the current code. Adopt the corrections within the existing owners:

| Finding | Disposition |
| --- | --- |
| Failed SDL enumeration becomes an empty list and replaces an explicit device | Return with feedback on SDL3 NULL or SDL2 negative count before modifying preferences. A successful zero-device list remains distinct. No implicit device replacement follows enumeration failure. |
| Sound-to-Voice or Controls return can inherit a slider grab | Retire native slider/scrollbar drag state on Voice entry/return; initiate Sound grabs only on its actual sliders. No new input-capture state is needed. |
| Saved enablement hides a failed capture open/start | Add a bounded pure GUI result derived from existing capture-wanted/device state after the attempt and display requested-but-unavailable capture separately from saved preferences. No new failure-policy cache, capture owner or GUI-time SDL call. |

Main also corrected the worker's label/value/cursor overlap and shortened the
device-loss hint within the intended page layout. These are source findings;
no graphical or executable validation has run. Effective reviewer settings
are unexposed, so this is a requested-Astra advisory, not a certified
senior-skill pass. Final software acceptance remains pending.

## Source implementation checkpoint

The four-file adapter adds528 lines and removes9, within the revised bound.
Sound and VR Options route to one native Voice Chat page; direct menu_voice
registration, ordinary Back/Controls return, default PTT binding entry, row
hit testing and native slider dispatch are source-integrated. The page exposes
the primary controls through native profile/cvar/capture owners, including the
missing adjustable local wet level. It does not introduce another voice route,
settings store, codec, mixer or rendering synchronization owner.

The saved system-default microphone and VR default-on/opt-out remain. The
global VR microphone opt-out disables local monitoring too; the voice page's
transmission action preserves independently enabled local wet monitoring.
The UI reads a bounded view and a result derived from existing capture state;
all profile/device/persistence/capture actions remain on native main input.
Disabled-build stubs cover the same interface without device operations.

Main inspected the source corrections and bounded geometry against the primary.
The final requested-Astra recheck confirmed all three P2 findings resolved with
no new consequential P1/P2 finding in this slice. Scoped whitespace checks
passed. No builds, tests, probes or rendered UI validation have run; the final
Linux/ARM software matrix above remains unproven. This source checkpoint does
not certify the wider audio/UI feature set or complete the migration goal.
