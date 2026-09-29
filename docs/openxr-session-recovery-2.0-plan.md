# OpenXR session recovery on the existing Vulkan binding

Status: implemented with passing bounded Linux checks and final local Astra Max
source acceptance. This bounded slice is complete; full migration remains open.
This advances VR-001/VR-002/XR-002 within the full migration. It does not remove
ordinary desktop hot-connect or instance-loss recovery from the parent scope.
Current scope exclusions and user-deferred live/device/platform/performance
qualification in [scope decisions](migration-scope-decisions.md) still apply.

## Outcome and verified evidence

After a session is lost or the runtime ends its XR experience, the game should
return to desktop, release held VR input and borrowed image users, and allow
the user to explicitly enable VR again when the original system and binding
are valid. User EXITING must not automatically restart XR. A missing system or
changed GPU/binding must leave desktop gameplay usable with a clear result.
The inherited `VR_Enabled_f` disables/reinitializes OpenVR on request; its quit
event dispatch queues the ordinary disable owner rather than exiting the game.

Verified current source at `a4f35d28`:

- `vr_openxr.cpp:destroy_stopped_runtime` already retains the OpenXR instance
  and Vulkan binding for EXITING, but destroys both backend ownership records
  for session loss. The renderer still owns the Vulkan handles themselves.
- `GL_OpenXREnable_f` refuses every non-NONE stop reason, including EXITING
  with its intact binding. Thus even the preserved binding cannot be retried.
- `destroy_session_resources` calls the renderer retirement callback before
  freeing runtime images, resets action/space/tracker/frame/mask/foveation
  resources and retains the queue callbacks. `GL_OpenXRRetireImages` restores
  desktop dimensions and invalidates the stereo reference.
- The backend already has GetSystem, VulkanRequirements, VulkanGraphicsDevice
  and the original system/physical-device/queue provenance. Its VkInstance
  creation wrapper validates API requirements but does not retain the selected
  API version. Record that version at its existing binding owner for retries.
- `VID_Restart` recreates render/WSI resources on the existing device. It is
  not an instance/device reconstruction owner. Borrowing an arbitrary desktop
  device or treating this restart as a complete graphics rebuild is invalid.

Official rules inspected on 2026-09-28:

- [XrSessionState](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSessionState.html):
  LOSS_PENDING requires destroying the old session and permits recreation.
  Recovery can require a newly selected system and related graphics resources;
  EXITING must not automatically restart the XR experience.
- [XrGraphicsBindingVulkan2KHR](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrGraphicsBindingVulkan2KHR.html):
  the Vulkan instance/device must be created through the OpenXR enable2
  wrappers, and the physical device must match the runtime selection.
- [xrCreateSession](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrCreateSession.html):
  graphics requirements must have been queried for that instance/system pair.

Unknown: device/headset behavior after actual disconnection, system changes,
instance loss, and full graphical task/queue recovery. Software dispatch checks
must state their limits. The user performs live testing later.

## Adapter versus replacement

| Choice | Reuse / demonstrated incompatibility | Cost and decision |
| --- | --- | --- |
| Retain an intact instance/binding after session-only loss; qualify explicit retry at existing attach owner | Reuses runtime state machine, stop reason, device provenance, retirement callback, session constructor and frame/input owners. Current destructive loss policy and frontend blanket rejection are the incompatibilities. | Chosen first slice: two production modules and existing fixtures; under100 new production lines. No extra scheduler, device, sampler or persistent retry machine. |
| Rebuild every Vulkan resource on every session loss | Would require a full device/asset recreation contract; current VID_Restart does not provide it. Duplicates adjacent healthy ownership for a session-only event. | Rejected for this slice. Required instance/device-change support remains a later shared-owner decision, not silently declared finished. |
| Blindly bind existing desktop/device handles to a new runtime/system | Ignores enable2 creation provenance and potentially changed runtime requirements. | Rejected. Unverified compatibility is not permission to attach. |

## Proposed stages, ownership and acceptance

1. Commit this verified design; Astra verifies then critiques it. Main prepares
   fixture changes independently, then records disposition before production.
2. `vr_openxr.cpp`: preserve instance/binding for session-only loss and EXITING
   after normal retirement; preserve original stop reason. Escalation to
   instance loss/failure remains destructive. Positive SESSION_LOSS_PENDING
   from frame calls must retain its proper reason rather than become FAILURE.
   Source inspection confirmed that WaitFrame already invokes `sayf`, which
   classifies that positive result correctly; no new classification was needed.
3. At every new attachment, including healthy disable/re-enable, poll pending instance events,
   query the current HMD system, and require the same system, compatible API
   minimum and same runtime-selected physical device. Preserve original
   Vulkan creation/queue provenance. Reject a changed or unavailable system
   before creating resources; do not invent a new system/device owner here.
4. `vr_openxr_vulkan.h`, `gl_vidsdl.c`: one observational binding-availability
   query allows explicit enable after a retained stop. Defer creation to the
   same next-frame attach boundary. The renderer still joins/retire work and
   creates stereo resources; no automatic retry loop or Vulkan recreation.
5. Reuse production Vulkan fixture and input/camera fixtures. Check healthy
   detach/re-enable, EXITING with no automatic resurrection, event/error/positive
   session loss, unmatched old session events, unavailable/changed system,
   incompatible API/GPU, instance-loss escalation, repeated explicit retry,
   queue callback balance, retirement-before-image-destruction, zero/stale input
   and first successful new frame. Add only needed simulated dispatch support.
   Consolidated Linux build/checks occur after implementation, then final local
   Astra review. Dispatch spies cannot prove physical runtime/GPU behavior.

## Senior review brief

Solo maintainer, no enterprise ceremony. Verify source and official rules
before critique. Rank hidden lifetime/provenance and user-intent hazards; verify
that a same-system retry is sufficient without a graphics rebuild. Challenge
new state/duplication and narrow the change where appropriate. Decide whether
EXITING explicit retry and session-loss retry can share the existing attach
boundary. Depth budget: max700words, exact file/line evidence, adopt/adapt/reject
recommendations. Read-only; no edits, builds, tests or nested agents. Do not
re-review graphics algorithms, network prediction, excluded locomotion or the
entire185-feature inventory.

Environment: worktree `/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0`,
branch2.0 only. Main owns production/doc/test integration. User's dirty
`docs/migration-2.0.md` remains untouched. Linux SDL3 Make build and native Vulkan
dispatch fixtures work. Luna coding route is unavailable; main handles this
bounded implementation. Astra is review only. No installed assets are modified.

## Astra design disposition (2026-09-29 UTC)

Reviewer Cicero verified local execution as `gpt-6-astra`, effort `max`.
Read-only source and official-rule verification completed before critique.

| Finding | Disposition |
| --- | --- |
| A healthy detach can retain NONE while the hardware changes. | Adopt: qualify every new attachment, querying the runtime directly rather than the cached physical-device accessor. |
| DestroySession errors are ignored, so successful retirement alone cannot justify retained recovery. | Adopt: return destruction success from the existing cleanup owner; any failed session destruction abandons backend setup. Preserve the most specific stop reason. |
| EXITING must not automatically restart. | Adopt: preserve the renderer attempted latch; only a fresh explicit command schedules another attachment. |
| Backend-only dispatch does not cover frontend intent/input/camera integration. | Adapt: extend production command/attach boundaries and existing input/camera checks. State each substituted owner and avoid claiming full GPU/HMD integration. Add healthy detach changes and release/end-frame positive loss. |
| Full device reconstruction would duplicate adjacent healthy ownership. | Adopt: retain current owners; broader device/instance recovery remains parent scope. |

The P1 design gaps are resolved by these changes to the plan. Actual device
health, physical reconnect behavior and graphics completion remain live-test
unknowns, which the user has deferred; software evidence must not imply them.

## Implementation and bounded verification

- The existing `VulkanBinding` records the API version selected at its successful
  enable2 creation wrapper. Every attachment polls events, verifies its existing
  queue, then directly queries the current system, API minimum and runtime GPU.
  Temporary query outputs leave the original provenance unchanged on rejection.
- Session-only LOSS_PENDING and EXITING retire through the existing owner and
  preserve binding eligibility. Failed DestroySession abandons the entire
  backend setup. Instance loss still destroys that setup. No graphics device is
  reconstructed and renderer-owned handles are never transferred.
- `VRXR_VulkanRetryAvailable` is observational. The command permits explicit
  retries only when backend eligibility exists. The existing pending transition
  block was factored as `GL_OpenXRApplySessionChange`, retaining the same order
  at the beginning of GL_BeginRendering. Attachment remains latched until a
  fresh command reaches that boundary. No retry scheduler was added.

Consolidated software checks (2026-09-29 UTC):

| Check | Result and scope |
| --- | --- |
| Linux SDL3 Make engine | Pass, warnings treated as errors. |
| `openxr_session_recovery_fixture.cpp` | Pass: actual backend creation wrappers/session/frame owners with simulated runtime/Vulkan dispatch. Loss event/error/positive WaitFrame, ReleaseSwapchainImage and EndFrame results; explicit successful new frames; no automatic recreation; stale old events; healthy detach with changed system; missing/changed system, API/GPU rejection; failed session destruction and instance escalation; retirement order and balanced queue callbacks. |
| `openxr_enable_fixture.c` | Pass: actual command/transition/attach/retirement owners, repeated desktop iterations after EXITING, explicit retry and ordinary disable. Actual empty render-resource cleanup runs against a prepared idle device; runtime availability/attachment, donor destructor and input/camera calls are spies. No actual GPU work is submitted. |
| Existing Vulkan boundary fixture | Pass: version/provenance, borrowed-image access, queue locking and failure unwind. Its main explicitly returns0 so the recovery fixture can reuse its helpers. |
| Existing stereo camera fixture | Pass: reference invalidation, fresh camera preparation, paused/reference/LOCAL/body-height paths. Four disabled optional FBT/weapon-adjustment stubs repair standalone linking after newer ports. |
| Focused input continuity fixture | ASan/UBSan pass with leak scanning disabled for sandbox: actual motion reset, GateAndReleaseAll and Neutral helpers release held triggers, preserve appropriate latches and reject held/nonfinite input. |

The older broad `vr_input_fixture.c` does not currently link standalone because
later FBT, akimbo and UI dependencies lack fixture seams. The focused checks do
not substitute for full `VR_InputCommands` dispatch, combined renderer/input/
runtime integration, live controller reconnect or GPU completion. This is a
recorded verification limitation, not a newly implemented input owner or a
claim that the entire migration is finished. Ordinary desktop hot-connect and
full instance/device recovery still require their own shared-owner decisions.

## Final source review adaptation

Cicero's final Astra Max review accepted the provenance, destruction result,
renderer intent/latch and reuse boundaries, but found one concrete polling
classification defect: a new fatal PollEvent error during explicit retry could
inherit the previous EXITING/SESSION_LOST reason. The existing attach owner now
starts polling with a fresh outcome and restores the previous reason only after
a healthy poll, preserving it for ordinary qualification refusals. Added both
historical-stop → explicit retry → runtime polling failure cases; they require
full backend teardown and unavailable retry.

The reviewer also distinguished a DestroySession instance-loss injection from
its permitted registry results. That probe is replaced with permitted runtime
failure after EXITING; actual instance-loss events still test escalation. Input
helper/renderer spy limits remain accurately stated.

Final disposition: Cicero verified `gpt-6-astra`, effort `max`, and accepted the
targeted correction with no remaining blocker. The reviewer checked the fresh
poll outcome and restoration ordering, discriminating EXITING/session-loss
failure assertions and the corrected destruction probe. No new owner or
persistent recovery policy was introduced. Both backend recovery and original
Vulkan boundary regressions pass after the correction; the Linux SDL3 Make
build passes again. The reviewer inspected source/assertions and did not rerun
these main-reported checks. Full dispatcher integration, combined runtime/
renderer/input behavior and GPU/HMD health remain the stated evidence limits.
