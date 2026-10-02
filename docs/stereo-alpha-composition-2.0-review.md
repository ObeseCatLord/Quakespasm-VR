# Stereo alpha output: local Astra dispositions

2026-10-02. Effective gpt-6-astra/xhigh verified. Read-only review of native
source, actual MDL inputs and all16captured images; main independently reproduced
8diagnostic color/category rows. [Verified brief](stereo-alpha-composition-2.0-review-brief.md).
This is a bounded F05 oracle review, not final integration signoff.

| Recommendation | Disposition / main spot-check |
| --- | --- |
| Upscaled witness counts do not prove independent coverage | Adopted. Native320x240 images become640x480 mirrors; smallest17/23witness groups occupy one mirror column. Main image geometry/diagnostic matches. Keep current composition inconclusive until projected source footprints, full coverage and material checks are certified. |
| Exclude missing water/entity/background as well as wrong ordering | Adopted. Main verifies P_E-P_W=.5(E-W), allowing large order separation with negligible water contribution. Candidate influence/distinguishability must be C-independent; all alternatives require exclusion. |
| No universal bit-depth-derived tolerance | Adopted. Official Vulkan blit-coordinate and blend-precision rules permit implementation differences. Current0..2byte residual is diagnostic, not an error bound. Freeze a scoped transfer/calibration budget before evaluating C; retain signed/unclamped math. |
| Observe native snapshot/presentation rather than count frames | Adopted. GL_EndXRFrame synchronizes submission only after its entry breakpoint; GL_PresentXRMirror may defer WSI acquisition. Reuse native snapshot completion and submit/return boundaries, bounded retries and exact phase/slot identity; record effective transfer/overlay state. |
| Native equations and mirror path are reusable | Adopted. Main verifies UNORM snapshot creation/raw image copy/UNORM blit, inverse-transfer postprocess, and alpha pipeline depth-write disable. No new renderer/readback layer justified yet. |
| SSAO graph invalidates equations | Closed as uncertainty. Main reads r_passes.c:233..252: SSAO composites before transparency, with transparent alias depth writes disabled in gl_rmisc.c:4423. |

Before-code presentation observer plan: retain two private normal parser commands
and the existing16-case recipe. No renderer/counter assignments. Observe the
actual successful snapshot tail at gl_vidsdl.c:5700 (after recorded copy/barriers).
In the current optimized native binary this resolves to GL_EndRenderingTask+2447;
GL_RecordXRMirrorSnapshot is inlined and has no standalone symbol. Record native
slot and immutable phase input identity only when actual opacity/water/mirror/wet
category matches the expected phase. No speculative helper-entry receipt.
Observe non-inlined GL_SubmitXRMirror entry and return: match its slot to that
snapshot, retain num_images_acquired at entry, and require exact decrement1,
ready/submitted state and no restart/surface-loss at return. Those native branches
exclude error/suboptimal/out-of-date completion without supplying any result.
Require at least3distinct accepted snapshots in the current phase plus the
existing8frame settle before capture; fail after120frames rather than accepting
missing provenance. Record receipts and actual transfer/overlay/extent state.
Observer variables remain external test bookkeeping, not production policy.
Snapshot source-line availability and optimized locals must be checked in actual
native execution; unobservable or ambiguous path is inconclusive, not a pass.
GDB finish observers must not call renderer/driver functions or join stopped
threads. Current captured images remain preserved and unaccepted for composition.

Future smallest coverage proof: first independently certify current source
sampling footprints and authored palette reconstruction using B/E/W alone. If
current narrow geometry cannot provide full coverage, use a slightly taller
still-entirely-wet/dry test plane and a1:1native mirror profile, keeping origins,
MDL/parser/renderer owners. Plan that exact input change before implementation.
No numerical checker is approved as a universal driver oracle, and no F05 group
closure follows. No human preference decision is needed.

Projection input recording before implementation: each accepted capture also
records actual native view_projection_matrix, stereo_clip_from_center for both
eyes and static origins/angles plus parsed MDL scale/scale_origin. Native shader
alias.vert transforms byte poses through that model matrix, center view/projection
and eye clip correction; no separately invented camera/projection model. Record
loaded world water surfaces from native water_surfs indices: plane/flags and
actual glpoly xyz, with conservative bounds and exact surface identity. These
are observations only; no copied BSP parser, injected list or geometry result.
The external checker can then independently project convex geometry and certify
source sampling footprints. Polygon/matrix inputs alone are not coverage proof.

Independent main synthesis check: main-material-diagnostic.json reproduces all8
C-independent signed material reconstructions from B/E/W. Max channel error is
2bytes for both authored palette colors in every cell. Same-side cells occupy
one mirror column, while opposite-side cells occupy15columns, confirming the
reviewer's coverage warning. These are diagnostics, not fitted acceptance bounds.

Bounded implementation disposition: the delegated worker was closed without a
patch after a narrowed retry. Main implements only the60-line native receipt
observer plus transfer/matrix observations; polygon extraction remains deferred.
Actual native execution passes16phases/112unique matching receipts, with stable
transfer/overlay inputs. [Exact results and limits](stereo-alpha-presentation-current-2.0-results.md).
Coverage/numerical/missing-layer acceptance remains inconclusive; no threshold
is weakened and no production renderer rewrite is introduced.
