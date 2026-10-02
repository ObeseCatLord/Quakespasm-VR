# Native alpha snapshot and presentation receipts

2026-10-02. Existing F05 probe only; production Vulkan/OpenXR code unchanged.
[Senior dispositions and before-code plan](stereo-alpha-composition-2.0-review.md).
The delegated observer implementation did not return a patch; its scope was
narrowed, then the worker closed. Main implemented the bounded60-line observer
at the already verified native seams. World polygon extraction stays deferred.

The optimized snapshot helper is inlined. Its successful copy/barrier tail at
gl_vidsdl.c:5700 observes actual native slot plus opacity/water/mirror/wet-eye/time
inputs. No receipt is generated for a mismatched transitional phase. A native
GL_SubmitXRMirror entry/return observer associates that snapshot with its slot
and WSI image index; exact acquired-count decrement, ready/submitted flags and
no restart/surface-loss select the successful present branch. No native state,
Vulkan return value, visibility list or result is supplied. At least3distinct
accepted current-phase snapshots and8settle frames are required; missing
observable provenance fails after120frames. Finish observers make no inferior
renderer/driver calls or joins. Existing normal parser/command owners are reused.

Actual published recipe now passes all16B/E/W/C captures at both mirrored eyes
and reversed wet/dry arrangements, with7matching presentations per phase and
112distinct snapshot receipts. Native identities/skin/leaf/alpha inputs, frozen
time and MSAA4/SSAO1/OIT0 pass. Engine/GDB exit0 and exact completion marker.
Main independently verifies all16PNG/result entries and phase/eye/layer/alpha/
water/time associations, unique snapshot identities, transfer invariants and
recipe hash. Actual native center/eye clip matrices are recorded for later
independent projection; recording does not certify geometry coverage.

Transfer/overlay inputs are identical across all captures: gamma1,contrast1,
palette0,waterwarp0,polyblend1 with v_blend=(0,0,0,0),console0/not forced.
Native extent320x240, mirror640x480. This remains an upscaled mirror and does not
resolve the review's narrow source-footprint coverage warning. Successful native
presentation receipts strengthen phase provenance; no display-server present
feedback or exhaustive renderer/provider acceptance is claimed.

Private FastGames/qsvr-alpha-present-pee6mzzv retains entry/run/log/exit,
output/layers.json/16PNGs, main-verified-summary.json and native receipt recipe
hash7b2722adc8ec0d0643930edfae9b3995549c33753fc522340fa71c913fa1c113.
A first service-only startup fails because non-PTY stdin cannot be registered
with epoll; its log is retained. The exact isolated service succeeds with a PTY,
without driver/system changes. The native host binary and installed test assets
remain those documented in [visible-input results](stereo-alpha-colored-current-2.0-results.md).
The owned isolated service is normally stopped after the client exits. No human
input, physical microphone/headset/gaze, benchmark or validation-layer pass.

This closes the renewed native snapshot/presentation observation boundary.
Full source-footprint/material coverage, independently bounded numerical
composition and missing-layer alternatives remain open under F05. No production
renderer defect is established by these oracle gaps; no group/goal closure.
