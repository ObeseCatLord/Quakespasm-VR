# Direct native reference for exceptional per-eye alpha: senior brief

2026-10-02. Solo engine migration, existing F05 only. Decide a practical native
GPU behavioral oracle, not a renderer rewrite or universal Vulkan certification.
A prior Astra pass rejected fitted tolerance/weak water influence/uncertified
upscaled counts and requested presentation receipts. Those receipts now pass.
This brief proposes replacing numerical-budget complexity with direct reference
comparisons, while retaining native geometry and missing-layer controls.

| Environment / evidence | Status |
| --- | --- |
| Workspace | /home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0, writable2.0 only; main read-only; no writes requested |
| Actual native captures | /home/obesecatlord/FastGames/qsvr-alpha-present-pee6mzzv/output/layers.json and16PNGs, main-verified-summary.json |
| Native test host | /home/obesecatlord/FastGames/qsvr-alpha-host-9x2fw7sl/vkquake-alpha, actual assert-enabled common graph with cl_parse owner/test commands; not shipping package |
| Runtime | Isolated Monado25.1 simulated null compositor; actual game Vulkan/native X11mirror; no hardware/gaze/performance/validation-layer pass |
| Assets | /home/obesecatlord/FastGames/qsvr-alpha-colored-_ef3cp5j/assets.json, native parsed static aliases wet/dry, origins828/844,850,-297/-295;308byte MDL,12x1XZ planes,fullbright colors and .5alpha |
| Quality / stable transfer | Native MSAA4,SSAO1,OIT0,foveationoff, gamma/contrast1,palette0,waterwarp0,v_blend all0,no console; time frozen and native extent320x240 /mirror640x480 |
| Source reference | gl_rmain.c:2558..2633 common/exceptional alpha dispatch, r_passes.c:233..252 SSAO before transparency; gl_vidsdl.c native successful snapshot/present receipts |
| Test metadata writer | Luna added90lines test-only normal fixture_alpha_geometry; native world polys/static aliases export, no copied BSP/parser/prod command; main reviewing/building separately, not acceptance yet |

Facts to audit:
- [verified native] all16phases pass112distinct matching snapshot/presentation
  receipts; native alpha128/.5 or1/zero, wet-eye masks1/2, both actual alpha lists
  nonempty with entitieson. Transfer/overlay/time invariant. Source dispatcher
  intentionally records both pre/post-water contexts serially in exceptional mode.
- [verified source] R_DrawAlphaEntitiesWithEyeMask only supplies eye-specific
  descriptor/offset when mask!=3; mask3 uses the native common scene descriptor.
  For bothwet(mask3), stage0 draws overwater(alpha1), stage1 underwater(alpha2).
  Bothdry(mask0) reverses: alpha2 before water, alpha1 after. This is the legacy
  viewer-category sequence in R_DrawAlphaEntitiesTask's ordinary fallback, without
  exceptional exclusion/duplicated per-eye draws. Native stages/materials/graphics
  pipeline are reused; targeteye shader gl_ViewIndex/clip can remain identical.
- [verified source] normal stereo mask0/3 still enters the stereo categories
  branch, NOT the legacy fallback itself. Therefore common-path equivalence is
  specifically an exceptional-selector/execution oracle, not independent proof
  of every shared classification/material/world policy.
- [verified source] native VR AO uses fixed spatial noise, no temporal random
  seed found in r_ssao.c; source graph composites before alpha.
- [verified diagnostic] actual native center+eye matrices project authored planes
  into observed model strips. Conservative model-only 2x2native source footprints
  fit all same-side selected columns; old counts alone did not prove this.
  Water/full material/depth coverage remains unproven pending native export.

Proposed direct reference experiment (not implemented yet):
For each old group/targeteye/layer B/E/W/C, keep head matrix, targeteye matrix,
target submitted pose and all game/settings/time inputs identical. Co-locate
only the OTHER view/pose with targeteye, yielding native bothwet3 or bothdry0.
No category/mask/visibility/result assignment. Native runtime/head validity flags
stay supplied; only controlled located matrices/poses change as existing recipe.
Assert actual eye leaves/category/common nonexceptional state, no per-eye
scene descriptor overrides at native alpha consumers; verify observed target
center+clip matrices match original experiment. Existing successful snapshot/
present receipts associate target PNG with phase/eye. Preserve quality/overlays.

Main lean: compare all four target images directly, initially exact decoded
RGB equality (no tolerance). Shared GPU/transfer/MSAA path removes unknown
quantization/filter errors from a derived-color equation. Enforce actual visible
entity/water effects in separately projected wet/dry regions; combined reference
must differ from entity-only, water-only and background, not just wrong-order
algebra. Inspect output and native pre/post pass/alpha list provenance. Both
arrangements/both eyes/each entity category required. Missing/equality-invariant
ROI influence is inconclusive; do not lower thresholds from observed outcomes.

Options:
1. Direct common-pass equivalence plus native geometry/influence guards (lean).
   Small32capture controlled comparison, no product renderer/readback adapters.
2. Original four-layer color equations with calibrated scoped uncertainty sets.
   Rejected as primary for now: universal arithmetic/filter bounds are not required
   by user, opaque GPU precision permits uncertainty and weak-water separation;
   current0..2byte residual cannot become its own acceptance bound.
3. Separate native desktop single-eye camera/projection reference.
   Considered stronger independent legacy dispatch proof, but VR/desktop AO paths
   intentionally differ and changing head/reference factorization may change
   projection rounding. Use only if option1 cannot establish the needed boundary.
4. Call/list counters and visual screenshots alone.
   Rejected as full output acceptance: counts do not certify visible composition.

Open decisions: Is the proposed reference sufficiently independent for this
specific changed exceptional selector/dispatch boundary? What minimal independent
ordering/classification/influence checks are still necessary? Are exact target
images/matrices realistic with the changed other eye (union visibility/SSAO/UI/
resource ordering), and should any mismatch be treated as inconclusive or a
product defect? No native render state should be forced merely to obtain equality.
These decisions overlap; merge/delete unnecessary numerical/coverage machinery.

Verify before critique using actual sources/receipts. Read-only, no GPU/driver/
system changes/subagents. Rank by leverage; choose your own top deep spec.
Return<=1000words prioritized adopt/adapt/reject recommendations with exact
file/line evidence and unknowns. Challenge whether the proposed architecture/test
is necessary; avoid turning F05 into exhaustive Vulkan certification. No other
F owners/new feature list/whole-goal signoff. Human decisions only if necessary.
