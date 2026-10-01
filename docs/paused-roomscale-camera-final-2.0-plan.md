# F04/F05 paused body-owned positional camera

2026-10-01. Local verified Astra/xhigh camera review found a source-derived
counterexample: V_TrackedBodyOwnsRoomscale remains latched while cl.paused;
V_SetupFrame retains the prepared player view base; R_PrepareStereoFrame zeros
horizontal HMD displacement. A physically moving paused head then changes
orientation/height but not horizontal camera position. Main verified these
three source paths; a failing existing-source fixture is being added before code.

Preserve native collision-resolved body movement and its command/input gates.
Do not just disable body ownership while paused: existing initial XR reference
may include old already-consumed or wall-blocked displacement. Do not change
stereo_reference_position: public-peer origin, LOCAL height and downgrade paths
reuse it. No replacement movement solver, camera implementation or wire policy.

Candidate narrow adapter: the renderer retains a horizontal snapshot of its
last unpaused body-owned head sample, distinct from the initial playspace origin.
A shared helper serves existing camera preparation and canonical head/body
offset queries. Unpaused private body owner captures/updates that snapshot and
continues zero horizontal view displacement. While paused, use only fresh
horizontal movement relative to the snapshot; body and commands stay unchanged.
Retire snapshot on reference/session/owner invalidation. On resume, existing
collision-resolved body owner resumes and the paused presentation offset retires;
paused movement must not become accumulated gameplay commands.

Open senior decision: snapshot necessity/placement and resume semantics. Review
whether a retained native owner can supply the same known last rendered sample
without changing input gates. Exact private pause regression plus no historical
reapplication, resume, reference/session reset and canonical offset agreement.
One composed nonzero-game-yaw camera case supplements signed-axis tests. Native
paused/private GPU case follows if practical; actual headset remains user-deferred.

Main owns architecture, production integration and documents. Luna first owns
only camera fixture reproduction/composed case. No new product feature, general
paused-body movement policy or broader context framework is authorized by this
plan. F04/F05 and final F10 remain open at their other frozen boundaries.

Senior disposition: snapshot adopted, with render preparation the normal writer;
queries cannot advance it. Missing paused baseline seeds once; repeated queries
do not reset it. GL_BeginRendering already invalidates reference before consumers,
so do not add repeated cache invalidation in R_InitializeStereoReference.
Resume explicitly restores collision-resolved body view and retires paused visual
displacement; seamless positional continuity across resume is not introduced.
Verify actual input accumulation/rearm separately, not merely no renderer writes.

Actual source fixture reproduced horizontal delta0 versus expected fresh
(3.937008,-5.249344,0), exit134. New helper uses only two horizontal coordinates
and validity. Subsequent native private run exposed valid predicted HMD poses
without active tracking flags; match the existing valid-pose render/motion gate
for this snapshot, not stricter wire/avatar gates. Existing canonical offset
query's earlier tracking requirements remain. Add a narrow renderer cache reset
to V_ResetTrackedAim so client/map retirement works even if its transient
protocol0 state is never rendered. Keep initial playspace origin untouched.
