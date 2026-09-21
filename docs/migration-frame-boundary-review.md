# Frame ownership integration

This slice prepares the existing donor renderer for OpenXR queue and image
ownership. It does not attach a session or implement stereo rendering. The
early task-enabled multiview and gameplay gates remain open.

## Minimal adapter

Keep vkQuake's submission task, queue mutex, frame slots, and frame recorder.
The runtime boundary receives callbacks for the existing mutex. Only the four
OpenXR calls allowed to access the bound queue take that lock: `xrBeginFrame`,
`xrEndFrame`, `xrAcquireSwapchainImage`, and `xrReleaseSwapchainImage`.
Blocking frame/image waits, logging, and renderer retirement remain outside it.
These requirements come from Khronos's
[Vulkan concurrency specification](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/khr/khr_vulkan_enable.adoc#concurrency),
which the enable2 binding inherits.

Locking an entire public backend call would risk deadlock: error handling can
invoke renderer retirement, which must join a submission task that needs the
same queue mutex. Depending only on main-thread scheduling would also leave
staging submissions outside the synchronization argument. The callback adapter
avoids both without introducing another queue or lifecycle owner.

## Failed desktop acquisition

Inspection found an existing donor ownership problem: a failed WSI acquisition
suppressed the postprocess draw and presentation, but the frame recorder still
began the UI render pass using the previous image index. That render pass owns a
swapchain attachment even when its secondary draw buffer is empty. A pending
screenshot could also copy the unacquired image.

The recorder now takes the actual acquisition result. Failure skips the whole
UI/presentation pass and presentation-image readback, preserving scene work and
prepared command buffers. The next successful frame fulfills a pending
screenshot request. The frame-slot submission bit is cleared after retiring its
old work and set immediately after successful queue submission.

## Review disposition

Astra (`gpt-6-astra`, `xhigh`) reviewed the actual callback implementation and
donor recorder changes. Main independently verified the effective model/effort
and spot-checked the findings. The reviewer performed source inspection, not
execution or device testing.

| Recommendation / finding | Disposition |
| --- | --- |
| Keep the narrow queue-call adapter; avoid holding a mutex across public backend calls. | **Adopted.** All four calls have short guard scopes. Error handling, both waits, and retirement execute after unlocking. Donor submissions and staging retain their existing mutex. |
| The queue callback owner must outlive renderer image retirement. | **Retained.** Callback ownership is separate from image-retirement ownership. Healthy detach retains registration; full runtime reset clears it. No second session owner was added. |
| A failed acquire still formed a semaphore pointer from the output image index. A zero semaphore count does not make invalid C pointer arithmetic safe. | **Corrected.** Submission uses a null signal-semaphore pointer when no image was acquired. All actual presentation-image accesses remain guarded by acquisition. |
| Missing-callback validation must use an otherwise valid queue; restoring callbacks must preserve older invalid-queue tests. | **Adopted.** The fixture uses the created family/index for the missing-registration case, then restores registration before checking invalid queue provenance. |
| Exercise retirement followed by release/end during a begun-frame detach, not only shutdown after a completed frame. | **Adopted.** The boundary fixture checks unlocked retirement, locked subsequent queue calls, retention through healthy detach, and clearing on full shutdown. |
| Do not count command-spy checks as live-session or multiview proof. | **Retained.** Session attachment and stereo scene output remain pending; P1 is explicitly open. |

## Local verification

- Native Linux SDL3 Meson `debugoptimized` build passed, including the final
  semaphore correction. This is a development build, not release qualification.
- The reused OpenXR Vulkan fixture passed registration/queue-provenance,
  balanced error-path locking, array submission, and begun-frame detach checks.
  Main completed the final fixture corrections after the worker handoff and
  checked the review's required ordering. Astra closed the semaphore finding by
  source inspection; fixture execution and final coverage closure were by main.
- The production frame description/recorder fixture passes all three
  transparency modes with SSAO on/off, both acquired and unacquired cases.
- A negative control in temporary files, removing only the acquisition guard,
  fails at the invalid framebuffer index. The production files were unchanged.
- These command-spy checks do not prove actual GPU execution, async task
  scheduling, headset output, or performance.
- Whitespace checks passed; changes are confined to the migration worktree.

Windows and ARM execution checks remain deferred until the end, as requested.
