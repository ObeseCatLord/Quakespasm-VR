# Native either-eye culling continuation

2026-10-02. Existing F05, no production feature/renderer rewrite. Main reads
vkquake/Quake/gl_rmain.c:143/166 and inherited OpenVR counterpart:608/632;
2.0 retains donor R_CullBox/unrotated model bounds, augments native frame frustum
conservatively for either eye and adds prepared-avatar bounds separately. Native
loaded alias bounds come from gl_model.c Mod_CalcAliasBounds. No gaze-driven
geometry culling is added or tested. Avatar/equipment bounds remain separateF05.

Reuse current native parser-fixture host/loaded e1m1 and fresh12x1.5 authored
quads. Reuse published alpha-reference snapshot/presentation/notification/normal
command owners through a small GDB adapter; no copied renderer or state/result
assignment. Controlled head/eye90roll poses stay same. Set native per-eye FoV
upper versus lower half using located tangent inputs; each existing model's
native box is visible to exactly one view. Reverse halves between eyes. Also
both-upper/both-lower controls give a model outside both views. Native OIT1,
MSAA4/SSAO1, wateralpha0 and explicit foveationoff preserve the ordinary shared
alpha submission. The two fixture statics keep parsed alpha128/1, native efrags,
identity/origin/material; no object visibility/draw-list/frustum replacement.

Four input groups times both mirrors times B/E states times2repeats =32phases.
Native view tangents and submitted FoVs must agree. Record loaded model/world
bounds, actual center/eye clips, four native union frustum planes, native model
cull entries/returns and alias geometry accepted/rejected at actual consumers.
At retired end-frame, optional read-only native model-cull query can certify
negative bounds absent from lists; distinguish that from in-path observations.
Pure native CPU functions only, no debugger GPU/driver calls or joins.

Independent main clip-space box test, using native loaded bounds and recorded
column-major matrices, establishes each box's monocular classifications. Actual
native union rejection must agree with neither-eye visibility; either-eye boxes
survive and reach native alias consumer. B/E captures must be stable repeats,
show actual red/blue model influence only in the eligible view with >16unique
native sampling footprints, and reject missing expected output. Outside-both
controls must have no entity image influence. Overlays expire normally plus
8settled frames and3native matching successful presentations. Marker/process0
and clean owned teardown required. Qualify first split before remaining groups;
no driver/system changes/reset/benchmark/physical-headset claim.

Main owns the adapter/analysis/GPU/docs. Luna separately owns density constructor
fixture only, with no overlapping writes. Stop and investigate failed premise;
never assign a return/list/frustum or weaken output acceptance to obtain a pass.
