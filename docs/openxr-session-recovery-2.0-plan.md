# OpenXR session recovery on the existing Vulkan binding

Status: design accepted with adaptations before production edits; implementation
and final local review pending.
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
