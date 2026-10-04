# VR audio focus boundary

The SDL2 and SDL3 event pumps currently mute the engine whenever the desktop
mirror loses keyboard focus. The Frame launcher runs the game under Gamescope
alongside SteamVR; mirror focus is not OpenXR session focus. This is a verified
mute path, although it does not establish the cause of every reported dropout.

Keep the existing audio driver, mixer, XR action ownership, and pause behavior.
Route SDL focus events through one input-owner helper. While stereo is attached,
mirror focus must not block audio. Reconcile the same condition every input
frame so attachment clears an earlier desktop mute and detachment restores
desktop background muting without requiring another window event. Use existing
idempotent S_BlockSound/S_UnblockSound; do not duplicate their mute state.

XR focus and tracking continue to govern controller acceptance. Do not make
unfocused runtime actions usable to conceal input interruptions. Remaining
input/regular-play crashes require independent evidence from the actual session.

At completion compile both SDL event paths where dependencies permit, exercise
the focus/attach/detach sequence in an isolated harness, and retain user-session
logs. Do not launch an automated VR session over the user's current session.
