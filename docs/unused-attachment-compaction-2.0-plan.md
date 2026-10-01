# Stable unused-attachment compaction

2026-10-01. Existing final desktop synchronization finding, following the
[Astra review](unused-attachment-sync-2.0-review.md) and
[boundary diagnostic](unused-attachment-boundary-probe-2.0-plan.md).
The temporary nine-line global barrier produced signon4/twelve rendered desktop
frames with no validation error/hazard in desktop-boundary-probe.log; ordinary
quit still stopped on allocator SIGABRT (GDB exit255). This proves ordering can
resolve the tested reports, not exact conflicting-image identity or production
performance. Main removed the exact probe; no global barrier is accepted.

## Minimal adapter and explicit reuse

Keep the native pass compiler, stage definitions, stencil variants, frame targets,
pass bindings, resource/task/submission owners and logical attachment policies.
At each existing physical pass derive stable physical-to-logical attachment slots
and a temporary inverse map. Drop only attachments absent from all real subpass
references, preserve indices and the special density/rate-map reference. Keep
retention separate from used_here/used_before: preserve-only retention must not
change first-use load/layout decisions. Rate/density attachment remains last.
No image-layout tracker, new resource, alternate renderer or global serialization.

R_CreateGraphicsPasses first computes existing logical load/store/stencil/layout
policy unchanged. Copy and remap subpass color/input/resolve/depth references and
preserve indices into temporary local arrays; never mutate shared stage-definition
arrays or shader interface slot order. A nullable copy/remap helper may eliminate
repeated reference loops. VK_ATTACHMENT_UNUSED stays unchanged. Build compact
attachment descriptions separately for each stencil variant after its logical
depth policy; all retained formats/samples/layout/load/store remain identical.
Pass creation and existing RenderPass2 rate-map conversion consume compact counts.
Update native used_before only from original actual used_here in logical indices.

R_CreateFrameBuffers still builds and validates the original target view list,
then selects its compact physical order through the same derived slot map. Keep
UI/density scene-slot/swapchain selection and borrowed image ownership. R_RecordFrame
similarly selects clear values from the existing logical array for each physical
pass. This includes original MSAA color and WBOIT reveal clear slots. UI clear
count remains zero. Preserve existing pipeline/context bindings and compatibility.

## Ownership, size and proof

One Luna xhigh implementation worker owns only Quake/r_passes.c. Approximately
100–140 changed lines; stop/reopen BEFORE160, no new public API/test framework.
No other production writer. Main handles compiler diagnostics, teardown analysis
and docs; existing worker owns metadata fixture/runner/receipt. No branch/main/
assets changes, commits, SSH or subagents by the worker. Return complete scope,
files/diff size, diff-check, assumptions/open risks/follow-up.

Main reads the complete adapter, checks every remapping consumer/stencil policy,
commits and rebuilds. Minimal vertical proof is identical desktop MSAA/OIT scene,
correct captured output and clean validation; then current actual24-probe XR
matrix plus desktop MSAA1/4, OIT0/1/2, AO, screen effects/upscale and recreation.
Qualify full-rate/foveation compatibility separately where actual extensions are
available; simulation without those extensions cannot prove GPU foveation.
User performance measurements excluded; no measured speedup claim. Desktop
shutdown is an independent obligation, not part of this one-file change.
