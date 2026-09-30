# Reuse unchanged static OpenXR foveation state

Plan before implementation, 2026-09-30. This is a bounded optimization of the
existing FB/META adapter, not a change to backend selection. Quad views remain
excluded. Fixed foveation remains explicit-only; unavailable gaze restores off.

## Current evidence and reference

- `Quake/vr_openxr.cpp:update_foveation_profile` calls `xrUpdateSwapchainFB`
  for every owned swapchain on every rendered frame, including unchanged off
  and static fixed profiles. It is the only application caller of that setter.
- The existing `Chain` owns each swapchain lifetime and is reset by
  `destroy_session_resources` before its profiles are destroyed. Off and fixed
  profiles are immutable; fixed has `XR_FOVEATION_DYNAMIC_DISABLED_FB`.
- [FB swapchain state](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrUpdateSwapchainFB.html)
  is mutable state associated with the swapchain. Retaining a successful static
  setting is an application-state inference from this API, not a measurement
  of saved CPU/GPU time or a borrowed-image metadata guarantee.
- The [META eye-state contract](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/meta/meta_foveation_eye_tracked.adoc#L173)
  recommends updating immediately before each state query to request a pattern
  update and potentially optimize eye-camera latency. Both dynamic and static
  strength eye profiles must continue doing this; static strength is still
  gaze-tracked. Eye validity/stability and the off restoration remain unchanged.

## Minimal adapter and ownership

Store only the last successfully applied profile in the existing `Chain`.
Skip repeated off/fixed setters for that same chain. Always update eye profiles.
No new policy, profile owner, frame state machine, runtime query, image cache,
Vulkan resource or renderer work is needed. A session-wide last-profile value
is insufficient for the existing separate-eye swapchain fallback: one chain
can succeed while another fails. Do not replace the current per-chain owner.

Production write set: `Quake/vr_openxr.cpp` only, fewer than 30 added lines.
The worker must not alter profile creation/strength, frame acquire/wait/release,
gaze timing, failure retry, native Vulkan behavior or the development gate.
Record a profile only after `ok` accepts its setter. Invalidate that chain's
record on a failed setter, since its state is uncertain; existing off recovery
then retries it while already-restored chains can keep their known off state.
The normal `Chain()` reset must clear the record across session recreation.

## Acceptance and limits

Main source review and a bounded local Astra source advisory cover repeated
off/fixed requests, forced repeated eye updates, both eye-profile variants,
eye-to-off transitions, partial separate-eye failure/off recovery, loss-pending
results and swapchain recreation. Main owns this plan and the plan index; one
web worker owns the production file and must not revert concurrent edits.
Missing evidence must be reported rather than expanding into another backend.

No builds/tests/probes until full implementation is complete. At consolidated
Linux/ARM verification, count setter calls through the real adapter: unchanged
static state skips calls; eye updates still precede each query; failed updates
are retried and recreated chains start unknown. Runtime image-contract
qualification, automatic FB/META preference, and the rest of the migration
remain unfinished. No performance gain is claimed without the user's later
measurements.
