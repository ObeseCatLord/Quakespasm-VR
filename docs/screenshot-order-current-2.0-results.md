# Native screenshot ordering and pending request results

2026-10-01. Frozen F05/F07; native vkQuake screenshot owner retained. Actual
pre-fix mj4m148-frame run calls the command once, sets its request on main,
worker clears it without readback/write, no PNG, natural exit0. Source and pinned
vkQuake4bc898f2 expose the same global request/metadata sharing across the
unfinished end task. This was a real missed capture, not image-export scope.

Initial3-line native join repair rebuilds0 and the identical diagnosis now
produces one write/PNG, clears the pending flag and exits0 with clean validation.
Main inspected close/dark wall geometry in that early mj4m1 image. No HUD/menu
presence or intended viewpoint acceptance follows; the initial senior brief was
corrected after actual viewing. Broad scene/UI inspection remains separate.

Local Astra/xhigh verified the task/command ordering and found pending metadata
could still change on invalid command/refused filename, plus unchecked infinite
join result. Main checked both findings. Luna/xhigh implemented the same two
native functions: local format/quality/name preparation, existing join before
candidate-name probing, publication only after success, flag last. Infinite
join failure cannot invalidate the handle or continue publication. No queue,
new resource owner, scheduler rewrite, frame-graph or ordinary-frame work.
Final source delta17added9removed lines; main read all changes, inside40bound.

Final current native SDL3 assertion-enabled host rebuild0 and60-frame mfxsp17
run0. Actual native command/renderer/Vulkan/readback/encode/write retained.
Only acquisition and filename availability inputs are controlled by debugger:
seven unacquired attempts, all100candidate names unavailable for one request.
Actual initial JPEG90 metadata/request survives invalid format, quality101 and
filename exhaustion exactly. After normal acquisition resumes it writes once
with the original JPEG name/quality; subsequent PNG writes once. Five commands,
two write entries, three refusal observations, final pending0, native pass marker,
JSON passed, correct JPEG/PNG magic bytes, clean validation and normal exit0.
Main inspected final PNG: world, weapon and Hipnotic-layout HUD present.

The probe uses ordinary command lists, owned preallocated command strings and
brief scheduler locking only for nonblocking Cbuf_AddText calls. Normal command
execution/join uses engine scheduling. It does not invoke the blocking join
from GDB. Previous-frame dependency retirement establishes observation order;
capture metadata/results are not assigned. Failed probe attempts retained:
interrupted debugger-created string allocation; C boolean macro unavailable in
GDB; FS_ENT_FILE preprocessor macro unavailable. Explicit source values0/1 fix
the recipe, without changing production checks or weakening expected outcomes.

Senior final source follow-up accepted exact ordering/metadata delta. Its three
effective contexts were locally verified gpt-6-astra/xhigh; both Luna workers'
single effective contexts were verified xhigh. Raw operational telemetry not
exported. [Senior dispositions](screenshot-order-final-2.0-review-brief.md#final-disposition),
[before-code plan and reopened estimate](large-map-output-final-2.0-plan.md).

Limits: source-reviewed concurrent writer protection, not a forced post-callback
selection overlap; actual condition-wait failure and prompt fatal shutdown are
unproven. Fatal shutdown can retry the infinite wait, so no general failure/
device-loss robustness is claimed. Acquisition/name availability is software
input, not driver-loss testing. Same-slot valid requests may supersede an
unacquired earlier request as native policy allows. No new VR screenshot feature,
every codec/Steam-library gate, full F05/F07/F10 or performance acceptance.

Private evidence /tmp/qsvr-final-qualification-thchgzi8:
large-map-output-current/{probe.gdb,run.log,build-repaired.log,run-repaired.log};
screenshot-pending-current/{build.log,run-final.log,result-final.json,game/quoth};
earlier run.log/run-corrected.log/run-constant-corrected.log and retained probes.
Current Linux/ARM package refresh must include the gl_vidsdl source change.
Main/reference, licensed assets, user prefs/server/migration doc untouched.
