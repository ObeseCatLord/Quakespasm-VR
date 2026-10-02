# F07 named map and native output follow-up

2026-10-01. Frozen F07/F05, no new feature. Prior repeated mj4m1 native extent
proof is accepted separately; queued screenshots produced no PNG and do not
prove inspected output. Diagnose actual screenshot command→readback→write
ownership through bounded native debugger observations before any code change.
Do not replace the renderer/capture owner or add a new framework just to make
the probe pass. If a production defect appears, record its trigger and minimal
native/reference repair before implementation, then local Astra review.

Reuse normal current native client and existing disposable writable Mjolnir
profile. No focus/key injection, desktop capture or user mic. Observe native
command entry, requested capture flag, readback/write entry and output path;
normal native command queue exit. No screenshot artifact or output-acquired
premise means no output pass, even with earlier frame counters. Retain failures.

Separately obtain the exact named mfxsp17 map and required Quoth resources from
the producer-authorized public release links. [Quoth official page](https://tomeofpreach.wordpress.com/quoth/)
links quoth2pt2full.zip; [official map packs](https://tomeofpreach.wordpress.com/quoth/map-packs/)
links mfxsp17-pak.zip. Readme/assets only; no launch scripts/installers execute.
Download/extract only into a fresh private qualification directory, with archive
path/size bounds, hashes and source URLs recorded. Do not install into the real
game directory or commit asset bytes. Reuse licensed read-only id1 pak0.

Luna xhigh asset-preparation ownership: only fresh private
/tmp/qsvr-final-qualification-thchgzi8/mfxsp17-assets-current, no checkout edits.
Download the two exact URLs, bounded safe extraction, print concise archive/
PAK/readme names and hashes. No source analysis, builds/game/branch/delegation/
commits. Main reads packaging instructions and creates native test profile;
do not invent asset providers or broaden network search if downloads fail.

Use the existing native loader/extent helper for mfxsp17 after preparation,
with a bounded map-specific probe derived from the accepted command-list recipe.
Retain its source equality/worker observations, explicit actual output and
normal exit distinctions. No benchmark/hardware gate or broad loader rewrite.

## Confirmed native capture race and minimal repair

Actual pre-fix mj4m1 diagnosis:48 loaded frames, native command1, request flag
true on main then cleared on a worker, native writes0, no PNG; normal exit0.
Current GL_EndRenderingTask samples take_screenshot when choosing readback, then
checks/clears that mutable global later. A command arriving between those points
is lost. Snapshot filename/format/quality are also shared. Pinned vkQuake
4bc898f2 has the same unsynchronized command fields/flag and unconditional worker
clear. Native R_RecordFrame retains the normal readback callback boundary.

Reuse GL_SynchronizeEndRenderingTask in SCR_ScreenShot_f after the initial format
check, before writing any capture metadata/request. It already joins and retires
the previous end-task handle and is used by native restart/shutdown owners.
Commands execute on main before the next draw task, so no worker can consume or
clear a new request before the selected frame. Keep pending-on-failed-acquire
behavior; do not introduce capture queues/generations/per-frame locks or replace
vkQuake's screenshot implementation. Scope one call/comment in gl_vidsdl.c,
no additional renderer or ordinary-frame work. Native desktop semantics retained;
new VR screenshot features are outside this repair.

Main implements the immediate bounded repair, affected native host build and
repeats the same diagnosis once. Require actual readback/write, PNG inspection,
clean validation and normal exit before acceptance. Local Astra/xhigh reviews
the narrow lifetime/ordering/source plan before final disposition. If failure
persists, inspect the native readback boundary; never weaken the image premise.

## Senior follow-up: preserve pending metadata on refusal

Local Astra/xhigh accepted the join placement/main-thread ownership and narrow
native capture observation. Main checked two inherited caveats: a pending JPEG
followed by an invalid format resets its encoder to PNG without changing the
old filename; failed infinite Task_Join currently invalidates the handle anyway.
Keep both in this existing F05/F07 family, not new feature IDs or services.

Reopen3-line estimate to at most40changed production lines in the same owner.
Prepare requested format/quality/name in stack locals using existing native
parsing, naming and path rules. Reject unsupported format/quality before global
mutation; join the previous writer before candidate filename probing, publish
all metadata/request only after successful validation and unused-name selection.
Filename exhaustion must also retain the pending request. Retain native
single-slot/latest-valid-request behavior; no queue, generations or frame-state
duplication. Check infinite join completion before invalidating its handle;
use existing fatal error boundary if synchronization itself fails. Do not change
Task_Join/scheduler or add generalized device reconstruction.

Luna xhigh coding ownership only Quake/gl_vidsdl.c, exact two native functions
SCR_ScreenShot_f and GL_SynchronizeEndRenderingTask; no other changes/tests/build/
branch/commit. Main keeps bounded native probe/docs/integration disjoint. Require
final scope/files/checks/assumptions/risks/follow-up. Main exercises actual
command refusal while acquisition is temporarily software-unavailable, retains
old JPEG metadata/flag, then allows actual write and a subsequent PNG. Preserve
earlier successful capture evidence. Same senior reviewer checks final source
delta; no repeat full architecture audit or physical device gate.
