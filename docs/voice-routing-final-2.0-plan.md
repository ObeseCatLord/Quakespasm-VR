# F08 native microphone preference and route qualification plan

2026-10-01. Next bounded component slice after recovery; no new feature or
production rewrite. Existing VoiceSettings, Voice_LoadSettings/SaveSettings,
public menu actions, Voice_RefreshCapture and SDL own this behavior. Reuse them
inside the same initialized native fixture. No replacement settings parser,
device resolver, capture provider or tracking/session policy. Missing evidence
is not a reason to change routing. Main/reference/user assets untouched.

The existing fixture requires actual dummy recording and a private XDG path.
Run this optional phase only in a new private profile before its prepared PTT
preference; current native client is naturally negotiated desktop. Therefore
this slice can prove stored VR default/toggle persistence and desktop routing,
but not active XR profile switching, unavailable physical devices, duplicate
SDL names or physical default selection. Retain those distinctions in F08.

1. Require missing private settings file and initialized fresh native state:
   VR transmission on, desktop off, empty device preferences, no capture.
2. Actual Voice_SetVRTransmitEnabled(false), reload via native LoadSettings and
   require saved opt-out/atomic/menu accessor; restore on and reload. Desktop
   stays opted out and no dummy device opens from changing inactive VR settings.
3. Actual desktop menu Voice_SetTransmitEnabled(true) in negotiated session
   opens dummy default; SetMode1/reload preserves mode/desktop opt-in. No direct
   assignment of active settings or capture availability.
4. Native device cycle chooses a uniquely named enumerated dummy device;
   require the stored preference resolves uniquely and capture is ready. Native
   reverse cycle returns system default and capture remains ready.
5. Model a previously saved now-missing explicit device using a local copy of
   settings saved with VoiceSettings_Save to the private settings path, then
   actual LoadSettings/RefreshCapture. Require no capture and menu unavailable;
   preserve transmit preference, never silently route to default. Recover using
   actual device-cycle owners and then return to default, require ready capture.
6. Native voice_revoke closes capture/saves desktop opt-out. Reload verifies it;
   restore through menu transmit/mode actions for subsequent PCM phases. End
   with default device, desktop transmit1/PTT1, VR1, no pending action or PTT.

Luna gpt-6-luna/xhigh owns ONLY tests/voice_routing_native_fixture.h, <=130 lines,
one static Voice_RoutingChecks(void). No parent/production/docs edits, no
build/game/commits. Main owns optional -routing include/invocation and isolates
each run, reviews source, retains failed logs and reports exact limits. Main's
parent bound remains550 lines. No new builder/runtime framework. Run after
implementation finishes; senior acceptance review remains separate from F10.
If a requested native owner cannot establish the case, report it rather than
assign state/mocking outcomes or expanding the architecture.
