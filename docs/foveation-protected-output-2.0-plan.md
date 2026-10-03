# Protected output under actual KHR foveation

2026-10-02. Existing frozen F06 only. No production renderer/policy change or
quad views. Actual FB/META/provider testing remains user-deferred; native Vulkan
KHR startup fallback is the available local GPU path. Fixed mode is an explicit
private test request only, never a default or unavailable-eye fallback.

Verified references: original vkQuake pipeline default/alias ownership retained
in 2.0 Quake/gl_rmisc.c:2990/4398. Ordinary pipelines have no fragment-rate dynamic
state or rate pNext, so the Vulkan default is1x1/KEEP. Only eligible opaque world
pipelines add dynamic state at4241; R_SetWorldFragmentShadingRate at4337 excludes
depth-only contexts and selects attachment REPLACE for eligible color draws.
Existing actual KHR live transitions/upload and corrected depth replay already
passed, separately qualified in foveation-current-qualification-2.0.md.
[Official shading-rate proposal/defaults](https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_fragment_shading_rate.html),
[official pipeline state](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineFragmentShadingRateStateCreateInfoKHR.html).

Compare minimal input/capture adaptation against a synthetic renderer/diagnostic
shader: reuse the parsed native alias fixture, asset packing, real scene, native
pipeline constructors/renderers, normal commands and matching snapshot/present
observations. A new scene graph, color oracle fitted to observed output, fake
runtime image producer or replacement renderer would duplicate working policy.
None is justified. The available full-rate reference is the actual same scene
with foveation OFF, compared at native projected interior footprints.

Input refinement: a separate small asset emitter reuses the existing308-byte
Quakev6 model packing and unit-scale geometry unchanged. Replace only8x8 skin
interior with alternating original fullbright category color/white254. Retain
255 top-left flood-fill guard and existing interior UVs. Manifest records actual
color counts/pattern/hash. Add opacity mode2 (ENTALPHA_DEFAULT opaque) to the
private native fixture command while preserving0/1 unchanged. Opaque detail
removes world-background blend dependence. Fullbright pattern and pixel
contrast witnesses prevent accepting a uniform skin that could look identical
under accidental coarsening.

Native host: build a fresh assertion-enabled debugoptimized graph from current
2.0 sources with existing Meson/dependencies/current SteamAudio SDK, then compile
only the existing parser fixture in place of the normal cl_parse translation
unit and relink using that graph. Record argv/status/source hashes. Immutable
previous artifacts remain unchanged. No Windows/ARM shipping rebuild needed for
test-only inputs. Do not silently treat the borrowed earlier graph as current.

Capture adapter: reuse published alpha-reference recipe at checked exact seams.
Both eyes retain native FOVs and runtime validity/tracking flags; controlled
source poses/head as existing scene. Eight phases per eye: OFF/FIXED by B/E,
each twice. B hides only authored aliases, E makes them opaque. OIT1,water0,
MSAA4/AO1, pause/time/transforms/exposure/transfer invariants retained. No list,
frustum, pipeline, returned rate map, capabilities or gaze results assigned.
Normal vr_foveation0/1 commands own transitions; no late feature-family switch.

At retired native end-frame observe enabled KHR, generated map/actual uploaded
byte equality, supported encodings and mixed full/coarse entries; OFF has no
active attachment. Observe real native alias acceptance and eligible world rate
setter calls. Inspect actual alias pipeline creation defaults if debugger seam
permits; no state forcing. Rate-map capture records extent/texel size/layer count
and bytes, with actual per-eye mapping. Successful snapshot/present receipts and
expired notification repeats remain required. New GDB observations must not call
GPU functions or stop mid-resource mutation.

Checker before acceptance: qualify the first eye, then second. Require exact
same-state decoded repeats, unchanged target/center clips and source geometry,
visible B/E influence within actual projected model interiors, and exact OFF/E
versus FIXED/E equality there. Select unique native2x2 source footprints wholly
inside a model and under a coarse attachment tile for every eye/model, require
at least16; additionally require actual local texture contrast under coarse tiles
so pattern sensitivity is evidenced, not inferred from asset names. World color
outside protected geometry must show a nonzero stable OFF/FIXED difference with
eligible world dispatch/map upload, otherwise coarsening output is inconclusive.
No benchmark, color tolerance fitting, universal shader/whole-map certification
or physical gaze proof. Surface/alias output and command-state evidence must
stay distinguished; actual coarse shader invocation measurement is not claimed.

Safety: isolated owned Monado simulated null compositor and actual small native
Vulkan/X11 mirror as the accepted recent runs. No real/XCB compositor, validation
layer, driver reload/reset, system setting/profile edits or unrelated app kills.
Natural client/runtime shutdown, retained first failures, bounded observer waits.
Stop investigating GPU work if a new fault appears; do not blindly retry it.

Review decisions before coding: Does the small patterned-opaque adaptation supply
sufficiently sensitive independent protected-output controls? Are native2x2 mirror
footprints/mapped coarse tiles and separate world differences sound? Any tighter
reuse/simplification or missing counterexample? No final whole-goal review.

## Read-only design review and work-order change

Local Astra/xhigh findings collected before the user requests complete remaining-
issue enumeration, implementation, then final testing. Main independently verifies
active-context model/effort scalars as gpt-6-astra/xhigh; the reviewer itself reports
its local lookup unknown. That lookup failure does not negate the main's direct
verification. No fixture or GPU work begins under this plan.

| Finding | Disposition for the consolidated final test phase |
| --- | --- |
| Local contrast within a coarse attachment tile can survive coarsening; bilinear enlargement can fabricate apparent detail. | Adopt. Select variation within the actual aligned2x2/4x4 fragment, within one primitive, from OFF/B–E input independently of FIXED/E equality. Existing bilinear footprint helper is insufficient alone. Pattern/geometry sensitivity remains unproved, not a pass. |
| Extent/tile/layer and orientation must be actual. | Adopt. Verify render extent as well as capture extent; every contributing source pixel must occupy a coarse tile, with actual one/two-layer selection and partial-edge handling. One-layer maps already choose the finer eye. World witnesses need filter-support clearance from protected geometry. |
| Opaque aliases enqueue instances; alpha-water consumer observations do not transfer. | Adopt. Reuse GL_DrawAliasInstances for actual opaque pipeline consumption. Preserve source eye poses instead of common-mode co-location. Native alias triangle deltas alone establish admission, not consumed pipeline state. |
| Native defaults provide full-rate alias protection. | Adopt existing-code reuse; no production rewrite is established. Keep actual protected output and pipeline consumption unverified until the final test phase. |

Fresh complete Linux graph setup/build succeeds before that work-order change,
using current production inputs and current enabled dependencies, no GPU work.
Private qsvr-protected-native-72o89z1i retains225objects, exact argv/logs/status and
source-input hashes. Parser-fixture compile/link arguments are prepared but not
executed. This build is not a protected-output test or final platform-signoff claim.
