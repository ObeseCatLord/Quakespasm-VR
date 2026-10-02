# Native screenshot publication senior brief

2026-10-01. Solo project; finite F05/F07 source-derived capture race. Read-only
local Astra xhigh, <=600 words prioritized evidence/minimal recommendations.
No builds/edits/delegation/branch check/full renderer rewrite. Main owns decisions,
integration and final execution. Do not reopen excluded imagedump/VR demos.

| Fact | Verification / limit |
| --- | --- |
| Trigger | [verified: actual native debugger] Current pre-fix mj4m1 main→worker48frames: command1, take_screenshot true then worker false, WriteScreenshot0, no PNG, normal exit0. |
| Race source | [verified: main read] GL_EndRenderingTask samples take_screenshot at R_RecordFrame callback selection, later checks/clears global; command writes flag and shared format/quality/name with no prior join. Pinned native vkQuake4bc898f2 carries same sharing. Current acquired-image gating retains a failed-acquire request but not a command arriving mid-task. |
| Minimal repair | [verified: diff] Exactly3added lines in Quake/gl_vidsdl.c SCR_ScreenShot_f: comment+existing GL_SynchronizeEndRenderingTask before first metadata mutation, after initial format rejection. No other owner/state/policy change. |
| Reuse | [verified: source] Existing GL_SynchronizeEndRenderingTask joins/invalidates prev_end_rendering_task. Native SCR_UpdateScreen owns/submits end tasks; command queue runs on main before the next task. Existing restart/shutdown use this same synchronization. No per-frame queue/generation/mutex/capture framework. |
| Affected build/run | [verified: executed] Assertion-enabled current SDL3/Linux Meson rebuild0. Same native48frame diagnosis passes: command1/write1, PNG on disk, pending0, clean validation, natural exit0. Main inspected image: close wall/dark world geometry; no visible HUD/menu. The initial brief's HUD/weapon claim was corrected after actual viewing, and the reviewer was notified. Intended scene/UI output remains separate F05. Native4xMSAA/SSAO1 retained by config, not a new effect matrix. |
| Boundaries | [unverified] Deterministic post-readback-selection arrival and competing format/name requests may strengthen actual concurrency regression proof. New VR screenshot/mirror semantics, output-acquire failure and every screenshot-format combination are not certified by one run. Existing frame/render recovery owners stay unchanged. |

Decision lean: preserve native screenshot implementation with rare-command join;
no normal-frame performance work, no metadata snapshot/queue necessary when a
retired task cannot access those fields. Challenge deadlock/reentrancy, task
handle ownership across SCR_UpdateScreen/loading screens, different screenshot
requests while task writing, failed acquisition preservation and overbroad
claims. Recommend smallest focused proof/correction if a premise is weak; do not
replace a working renderer because instrumentation is easier elsewhere.

Read docs/large-map-output-final-2.0-plan.md; gl_vidsdl.c SCR_ScreenShot_f,
GL_SynchronizeEndRenderingTask/GL_EndRenderingTask and necessary gl_screen.c/
cmd.c command/task flow. Optional pinned reference via git show4bc898f2 only.
Private evidence /tmp/qsvr-final-qualification-thchgzi8/large-map-output-current:
probe.gdb, run.log, build-repaired.log, run-repaired.log; screenshot under sibling
large-map-extents-current/game/mjolnir. Raw sensitive telemetry not exported;
main verifies effective reviewer model/effort. User migration doc untouched.

## Final disposition

Main verified all three effective senior contexts as local gpt-6-astra/xhigh,
spot-checked main command flow, render handle transfer, native waiter return,
SDL2 boolean compatibility wrapper and both Linux fatal/shutdown guards.
Luna metadata worker had one verified gpt-6-luna/xhigh context; main read the
complete17added9removed-line delta in the two named functions.

| Senior recommendation | Main disposition / evidence |
| --- | --- |
| Existing end-task join before publication fixes the demonstrated race. | Adopted; native pre-fix command1/write0 becomes write1/PNG with normal exit. No new renderer/service or ordinary-frame work. |
| Pending metadata changes on invalid commands or filename exhaustion. | Adopted minimal repair: local format/quality/filename, validate first, existing join before collision probes, publish only after success/flag last. Final native refusal/acquire-recovery proof passes. |
| Infinite join result must precede handle invalidation/publication. | Adopted existing fatal boundary on false; no task scheduler/API rewrite. Native SDL2 wrapper converts success to boolean, retaining both SDL variants' contract. Actual failed-wait/prompt shutdown not claimed. |
| Fatal shutdown may retry synchronization. | Adapted limits: native isdown bounds recursive shutdown, but retry can still stall. No generic fatal-handler/device-loss reconstruction added to this finite goal. Source ordering accepted, failure robustness unproven. |
| Stronger forced overlap/producer failure evidence. | Partially adopted. Controlled unavailable acquisition retains real JPEG; invalid format/quality/all100names leave it unchanged, recovery writes once, subsequent PNG once. No deterministic worker hold just after callback selection or during encoding is claimed. |
| Intended scene/HUD/menu evidence. | Accepted only after actual separate named-map image inspection; mj4m1 close-wall capture remains narrow. Quoth/mfxsp17 PNGs show world, weapon and HUD; current refusal probe additionally has console notification text. No whole-map/UI/stereo matrix acceptance. |

Follow-up source review accepted the exact two-function delta without another
production recommendation. Main's final native build0/run0,5commands/2writes,
3refusals,7unacquired attempts/100existing names, correct JPEG/PNG file magic,
clean Vulkan validation and normal exit are in
[the current receipt](screenshot-order-current-2.0-results.md).
Both reviewers' executions remain read-only; native runs belong to main.
F05/F07/F10 retain their other existing boundaries; no new feature inventory.
