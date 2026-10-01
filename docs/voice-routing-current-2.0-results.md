# F08 stored microphone preferences and native desktop routing

2026-10-01. No production edits. Main reviewed the complete94-line Luna header
and integrated its optional -routing phase in the existing486-line native voice
fixture. Effective coding context verified gpt-6-luna/xhigh. Same preliminary
Linux DEBUG Meson/SDL3/Opus engine through aa828629; Steam Audio disabled.
[Before-code plan](voice-routing-final-2.0-plan.md).

Actual combined -routing -recovery build0/run0, with all three markers:
VOICE_ROUTING_NATIVE_PASSED, VOICE_RECOVERY_NATIVE_PASSED,
VOICE_PCM_NATIVE_PASSED. Normal native shutdown. No additional production build
or hardware acceptance is implied by these test-only changes.

| Boundary | Native observed result |
| --- | --- |
| Fresh private file/defaults | Native settings loader reports absent file. Initialized VR stored transmission1, desktop0, modes0 and both device names empty; desktop has no capture. Actual SDL driver asserted dummy. |
| Saved VR opt-out | Public Voice_SetVRTransmitEnabled(false) saves; actual Voice_LoadSettings reloads0 and accessor matches. Restoring true persists1. Inactive desktop remains off/no capture; menu remains desktop profile. |
| Desktop opt-in/PTT | Public menu transmit action opens native dummy system default; mode action saves PTT1. Actual loader preserves transmit/mode/default and capture remains ready. |
| Exact named device | Native device cycle chooses a uniquely enumerated dummy name; existing resolver confirms unique, native capture ready. Reverse cycle returns default with ready capture. No fake enumeration/open result. |
| Missing explicit preference | Local settings copy with deliberately absent name is written by native VoiceSettings_Save to the private path; native load/refresh preserves name/transmit preference, closes capture and reports device unavailable. It does not silently choose default. |
| Recovery and revoke | Native cycle recovers uniquely named capture and reverse returns default. Actual voice_revoke closes capture/saves desktop off; native reload preserves desktop0/VR1. Menu actions restore default desktop1/PTT1 for subsequent PCM checks, no PTT keys or pending action. |
| Integration | Existing controlled PCM, queue/gain/loss/reorder/source-generation/reset/shutdown cases all pass afterward in the same process. Prepared client resources remain alive through native shutdown. |

These are stored VR preferences and desktop dummy routes. Active XR profile
switching/default capture is not executed here. Explicit saved missing-device
input is prepared on disk; active native profile/capture state is derived by
the existing loaders/routes. Duplicate names, failed default open/hardware
disconnect, physical system device selection, independent client transport,
complete VAD/preroll/discontinuity and Steam Audio/spatial/music remain distinct
F08 obligations. Human microphone recording/listening stays deferred.

Fresh profile is mandatory for -routing: the first assertion requires absent
settings. Private root /tmp/qsvr-final-qualification-thchgzi8/voice-routing-current
has separate XDG directories and a read-only licensed pak0 link. Build executable
and compile/link argv are under build; actual logs are
logs/voice-routing-current-{build,run}.log, both exit0. Source and builder retain
assertion-enabled dummy-driver/environment guards. No failed run in this slice.
Main/reference/user migration doc, deployed servers/assets/preferences untouched.

Main reviewed public toggle/save/load, device-cycle unique-name/default logic,
native missing route/no fallback and revoke sources. Prior local Astra focused
[voice recovery review](voice-recovery-current-2.0-results.md) applies to that
unchanged recovery implementation; it is not a review of this new header.
No separate architecture decision or production change in this addition.
Whole F08 and final F10 signoff remain open.
