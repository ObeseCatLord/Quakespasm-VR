# Native either-eye static alias culling

2026-10-02. Existing F05, production renderer untouched. Main reuses the native
alpha fixture, current authored assets and published snapshot/presentation recipe
through an exact-seam GDB adapter. Native draw/frustum/list/cull result arrays are
not supplied. Original vkQuake unrotated model bounds/R_CullBox remain the owner;
2.0 supplies conservative combined eye frustum before native calls.
[Before-code reference/input plan](stereo-culling-current-2.0-plan.md).

First split eight-phase probe qualifies, then full four-group32phase run exits0
with exact marker, passed JSON and no Python observer errors. Both eyes and two
split arrangements place each parsed model inside only one native view. Controls
with both views upper/lower place the other model outside both. Runtime validity/
tracking flags stay provided; source eye/head pose/FoV inputs and matching
submitted FoV angles are controlled. OIT1/water0/foveationoff, MSAA4/SSAO1 retained.
The underlying quality pipeline is reused; SSAO cvar1 here is not an independent
compute-entry proof. Existing native AO matrix evidence remains accepted.

Main checker independently applies six homogeneous clip-plane equations to
actual loaded model bounds using recorded column-major center/eye clips. This
checks agreement conditional on native matrices, not their independent FOV
construction. The adapter does not sample GPU-bound uniform bytes; prior alpha
uniform observations do not silently transfer to this run. Native combined cull
false for either-eye models and true for outside-both controls agrees. Actual
in-path model-cull entries/returns and alias returns are observed: survivors
accept four triangles; excluded models accept zero geometry. Retired end-frame
native CPU model-cull queries are labeled separately and excluded from in-path
counts. All four outside-both enabled cases also have eight actual rejected alias
calls, so the negatives are not just prepared query results.

All32 native B/E images decode and have exact same-state repeats. Eight visible
category/eye cells each have1485unique native2x2sampling footprints with actual
B/E influence and intended red/blue color inside the independently projected
model. Eight ineligible view/model cells project to no visible polygon, including
four models rejected by both eyes. Empty projection is not direct pixel-absence
measurement. Main and Astra independently decode16same-target full-image B/E/
repeat comparisons across split versus excluded-both groups: zero differing RGB
pixels. Recorded target-eye FOV/clip and center transform remain invariant while
the other model changes CPU admission. This control plus zero accepted native
alias geometry supplies bounded exclusion evidence. Main visually inspects representative rendered
world/quad/HUD outputs. This qualifies the ordinary parsed static alias boundary;
avatar/equipment/submodel and exhaustive world-only visibility cases are not
claimed. Conservative visibility can retain objects between separated cones;
no requirement for exact per-eye geometry OR, benchmark or gaze geometry culling.

240successful matching native snapshot/presentation receipts and eight consecutive
expired-notification end frames qualify capture state. No per-image display-server
completion feedback is claimed. Source-recipe lookup initially uses quoted GDB
filename and exits255 before starting the inferior; that artifact remains. The
exact-seam adapter changes to controlled no-whitespace unquoted source path.
No renderer/driver change fixes that automation failure.

Private roots: qsvr-culling-native-ncf9b417 retains pre-inferior source failure and
owned isolated Monado-null log; qsvr-culling-source-z4c3p59_ retains successful
first split; qsvr-culling-all-n8x8d43p retains full run/expanded recipe/native
geometry/32PNGs/culling.json, checker.json and main-culling-receipt.json.
Actual game Vulkan/X11 mirror runs against the same native test host as the
alpha comparison, not a newly rebuilt full shipping package. Borrowed-graph
provenance retains its earlier limitations. Owned runtime stops normally after
client0; bounded kernel-log observation finds no new NVIDIA fault/reset event.
No validation-layer, physical headset/gaze/controller, performance or system/
driver/reset/global setting changes.

Local Astra/xhigh accepts this bounded boundary with the
[adopted corrections](stereo-resource-boundaries-2.0-review.md). Luna implements
the checker changes; main reviews and independently reruns the final CPU checker
on the unchanged32native captures: exit0,16full-image controls equal, all enabled
identities observed. Two disposable negative controls reject at their intended
checks: missing rejected-model observation and changed cross-control image.
Final private evidence: FastGames/qsvr-stereo-review-final-q38i0meu/checker.json,
main-receipt.json and individual negative receipts. The eight-phase probe is
explicitly labeled a bounded subset without full cross-controls. No GPU rerun
needed. This closes targeted ordinary static alias culling; other frozen F05/
F06/F10 owners remain open.
