# Use the system recording default

The user chose the operating system's default microphone on 2026-09-29.
This supersedes AUDIO-003's inherited headset endpoint/name matching goal.
Keep explicit named-device selection available, but do not discover or rank
headset microphones through OpenXR or product aliases.
The later instruction makes VR transmission **default on, opt out**. Preserve
saved opt-outs. Desktop transmission retains its existing opt-in default;
local wet-only monitoring remains a separate permission.

## Existing boundary and smallest adapter

`Quake/voice.c` already owns SDL2/SDL3 capture, conversion, retry, consent,
VAD/PTT and local wet-only monitoring. `voice_settings_profile_t.device` is
already empty in fresh profiles. Use that empty preference to request the
system default; retain a nonempty preference as an exact, unique SDL device
name. No new provider, capture loop, settings format or OpenXR API is needed.

Official SDL documentation specifies [NULL for SDL2 default capture](https://wiki.libsdl.org/SDL2/SDL_OpenAudioDevice)
and [SDL_AUDIO_DEVICE_DEFAULT_RECORDING for SDL3 streams](https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream).
SDL3's [default logical device](https://wiki.libsdl.org/SDL3/SDL_OpenAudioDevice)
can follow system default changes. For SDL2, leave physical routing to the
audio backend and reopen through the existing retry path after device loss;
do not promise that every SDL2 driver follows live default changes.

## Implementation contract

1. Empty device preference is a valid default-device request. Open SDL2 with
   NULL, never by passing a negative enumeration index to GetAudioDeviceName.
   Open SDL3 with DEFAULT_RECORDING. Failure stays inactive and retains retry.
2. Explicit names continue to require exactly one match. A missing or duplicate
   explicit device must never fall back to the default.
3. `voice_select_device default` restores the default preference through the
   existing local selection-confirmation owner. `voice_select_device none`
   must stop capture and revoke transmit and self-reverb permission, so an
   empty default preference cannot immediately reopen capture.
4. Fresh settings enable transmit for the VR profile. A valid loaded profile's
   transmit bit remains authoritative, including an explicit opt-out. VR
   capture requires an active negotiated multiplayer session; selecting a
   default device alone never enables desktop transmission or self-reverb.
   Desktop transmit and separate self-reverb confirmation accept default
   selection without requiring a named device. Retain exact-name checks for
   explicit selection. Unreadable saved settings stay off rather than losing
   an unparseable saved opt-out.
5. Status/help distinguish the system default from an unset/missing device.
   Keep the current settings and capture-route owners; avoid unrelated audio,
   networking changes in this slice.
6. Select the profile from the existing `V_TrackedSessionActive()` predicate,
   not the launch argument. On a mode transition cancel pending confirmation,
   release PTT, clear outgoing/capture backlog and close the old microphone
   before using the new profile. Keep this at the current voice owner, including
   local key/command paths that can arrive before Voice_Frame. No second
   session state machine or runtime-specific microphone API is needed.
7. Add one microphone toggle to the existing VR options menu. It displays the
   saved VR transmit preference through the voice owner's atomic UI snapshot,
   and writes that preference through a local menu action. Disabling stops
   current VR transmission/capture immediately and clears VR self-reverb
   permission; enabling persists only the VR transmit choice
   without requiring desktop consent. When opened from desktop, the row edits
   only the future VR preference and leaves desktop capture alone. The existing
   voice_revoke command remains the console opt-out. No separate voice menu or
   new cvar or wire layout is needed for this toggle.

## Astra review: saved empty-device semantics

The local Astra Max source review verified that the old `none` action saved an
empty name without clearing transmit/self-reverb permissions. The new empty
name default semantics would therefore reactivate those valid version-3
records. Reopen the persistence part of this plan before committing:

| Finding | Disposition |
| --- | --- |
| An old empty preference may be a deliberately disabled device. | Adopt: increment the semantic settings version to 4 with the same wire layout. Clear transmit and self-reverb only for pre-v4 empty profiles; retain old named-device permissions and explicit transmit-zero values. |
| Simply incrementing the version would activate the existing blanket permission reset for version-3 named profiles. | Adopt: retain the old pre-v3 reset boundary and accept version 3 explicitly. New version-4 empty default preferences retain their saved permissions. |
| Profile transition cleanup, default vs explicit routing, atomic menu snapshot and current opt-out use existing owners. | Accept the source review; keep these owners. No new capture/provider layer is needed. |

Fresh VR defaults remain on. Missing settings files use those defaults;
unreadable settings remain off. This is an observed saved-state semantic
conflict, not a general legacy-settings migration project.

## End-of-goal verification

Build both SDL paths after full implementation, then cover fresh VR default-on
and saved opt-out, desktop consent, revoke/none, default open
failure/retry, explicit missing/duplicate devices, exact selection, wet-only
monitoring, desktop/VR switching and shutdown. No builds, fixture
execution, recording or hardware tests during this implementation slice.

## Implementation checkpoint

`voice.c` now requests the SDL recording default for empty preferences, keeps
explicit names unique, selects profiles from attached VR state, and retires
capture/PTT/pending input on mode changes. The existing VR Options menu has a
Microphone row backed by the saved VR preference and an atomic UI snapshot.
The row's off action clears VR transmit and local reflections; its on action
enables only VR transmit. `voice_revoke` and `voice_select_device none` remain
immediate console opt-outs.

The local Astra Max review accepted the current routing/lifecycle/UI owners.
Its targeted follow-up verified the version-4 serializer, independent old-empty
profile clearing, version-3 named permission retention, load-error behavior and
atomic save boundary, with no remaining P1/P2 source blocker in this slice.
Only static source review and diff whitespace checks were performed. SDL
capture, settings round trips and mode-switch execution remain deferred to
end-of-goal verification.

## Manual capture retry slice

The command-surface audit found the primary's Voice_Restart_f
(voice.c:184–190): it closes capture and immediately retries through the
existing capture owner without changing device or permission. Native automatic
polling/stopped-device recovery remains in Voice_RefreshCapture; menu actions
already close/reopen, but an explicit retry should not require changing a saved
microphone toggle or device preference. This is part of AUDIO-003's retry
behavior, not a second capture loop or permission reset.

Add one local voice_restart command in Quake/voice.c. When initialized, use the
native profile selector, release any transmitting/captured backlog through
Voice_StopTransmit, close capture, clear voice_capture_wanted and force the
existing Voice_RefreshCapture. Keep pending consent, saved transmit/device/mode,
independent wet monitoring and all settings untouched. Reopening remains subject
to the current multiplayer/permission/exact-device routing predicate; opt-out
must not start capture. Do not invoke Voice_FinalizeMenuAction because that
clears pending confirmations and writes settings. Register alongside native
voice_status; preserve the existing VOICECHAT-off compile stubs. Estimate <=30
added lines, no new public API or state. Main source review is sufficient for
this narrow existing-owner action; there is no new architecture fork.

After full implementation, software checks cover forced retry after capture
failure, ready capture/backlog retirement, default/exact/missing device,
transmit opt-out, wet-only monitoring, pending confirmations and desktop/VR
profile transition. No builds/tests/probes now and no permission changes as a
side effect of retry.

The 12-line source adapter is now integrated at the native voice command owner.
Main verified stopped transmit/PTT/preroll/encoder cleanup, profile transition
selection, saved-preference preservation and force-refresh routing. A same-profile
retry leaves pending confirmations intact; an actual desktop/VR transition uses
the existing transition cleanup. Scoped whitespace review passed. No executable
checks or microphone capture trials have been run.
