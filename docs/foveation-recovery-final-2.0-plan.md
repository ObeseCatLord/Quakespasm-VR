# F06 renderer failure recovery: before-code plan and review brief

2026-10-01. Frozen F06 owner; two-eye VR, optional eyes, explicit fixed only.
Reuse the existing OpenXR attach/retirement and vkQuake resource owners. No new
runtime, device reconstruction, feature-family switch or quad views.

## Evidence

- [verified: direct source] `GL_PrepareRuntimeFoveation` clears per-frame flags,
  calls the backend setter, and latches `openxr_density_backend_failed` on -1.
  The current caller aborts and requests `VID_Restart`; subsequent preparation
  bypasses the setter entirely when the failure latch is set.
- [verified: direct source] `VID_Restart(false)` drains/recreates renderer
  resources but does not detach the XR session or replace its swapchains.
  Thus a failed off restoration can leave the last runtime profile active.
  Disabling density passes does not establish a successful full-quality restore.
- [verified: direct source] `GL_OpenXRAttach` already creates ordinary swapchains
  on the same device when runtime density support is unavailable; backend attach
  already retries ordinary swapchains after optional setup rejection. No KHR
  feature needs enabling after an FDM device was created.
- [verified: direct source] render-pass/framebuffer failure also latches failure;
  borrowed-density-view failure instead removes those views. These optional
  paths must not resume a previously coarse runtime profile without restoration.
- [unverified] physical FB/META borrowed images, producer readiness and runtime
  recovery. Hardware/provider testing remains user-deferred.

## Incremental proposal versus replacement

Prefer one failure latch at the existing renderer owner. After failed off
restoration, abort the acquired frame, release VR-owned input, retire/detach the
session through its existing callback, then allow one ordinary-swapchain attach
on the next native frame. Gate density requests with the same existing failure
latch, preserving device capabilities and never switching to KHR on this device.
No repeated retry if ordinary attachment fails. Preserve settings and ordinary
desktop fallback. Reuse `openxr_enable_fixture.c` for command/attach/retirement
and extend it for the actual preparation helper; dispatch/resources are spies.
Use the existing GPU probe for loaded-scene abort/re-attach proof separately.

Alternative: keep the session and retry off every frame. Rejected provisionally:
the backend has already failed restoration, and no valid render output can be
promised until off succeeds. Alternative: rebuild Vulkan device or replace the
runtime owner. Rejected: ordinary swapchains already work on the current device.
Estimated production scope: gl_vidsdl.c, under60 lines. Reopen the decision if
new state machines or wider renderer changes are needed.

## Local Astra questions

Verify the source facts first. Is detach/one ordinary reattach necessary and
sufficient at this boundary, including positive session/instance loss? Should
render-resource failures share the same recovery or safely restore off instead?
Challenge duplicated state and suggest the smallest end-to-end proof. Rank only
F06 failure/selection concerns; do not reopen ten-owner enumeration, graphics
algorithms, networking or user-deferred hardware tests. Read-only, <=700words,
file/line evidence and specific adopt/adapt/reject recommendations. Main owns
final integration; Luna owns only the separate profile fixture.

## Verified senior disposition before production edits

Main verified the reviewer's effective `gpt-6-astra/xhigh` turn context (one
context), independently of its generic self-identification. Review returned
read-only; advisory wording does not invalidate main's verified routing. This
is focused F06 senior review, not overall F10 signoff.

| Recommendation | Disposition |
| --- | --- |
| Keep healthy session when off succeeds | Adopted. Remove preparation bypasses; failure/no borrowed views requests off. Static off is already cached by the backend. |
| Latch actual borrowed-view creation failure | Adopted. Existing latch only, no parallel recovery state. |
| One ordinary attachment only after healthy detach | Adopted. Gate the density request with the latch; clear attempted only if wanted, stop reason NONE and existing binding retry eligibility. |
| Release input before abort | Adopted. Abort may synchronously retire the session; release first. |
| Build another renderer/device reconstruction owner | Rejected as unnecessary; native attach/retirement and same device remain. |

Final production scope stays one file, approximately25 lines. Resource creation
continues through existing full-rate render-pass/framebuffer fallback; before
any drawing the actual frame preparation must establish off or detach. Backend
fixtures distinguish session loss and teardown failures; renderer fixture
observes prepared call/ordering/attempt boundaries. Native GPU probe observes
loaded scene abort/ordinary reattach. Controlled faults do not certify FB/META
borrowed-image compatibility or physical runtime failure behavior.

Official mutable-state and result owners checked2026-10-01:
[xrUpdateSwapchainFB](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrUpdateSwapchainFB.html)
and [XrSwapchainStateFoveationFB](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSwapchainStateFoveationFB.html).
Changing application passes alone cannot establish the profile setter succeeded.
