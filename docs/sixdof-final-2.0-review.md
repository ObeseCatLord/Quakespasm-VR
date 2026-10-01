# Final six-axis camera review dispositions

2026-10-01. Bounded local Astra/xhigh review from the
[verified brief](sixdof-final-2.0-review-brief.md), with three focused follow-ups
on pause snapshot, valid predicted poses and client lifetime. Main verified all
five effective review contexts remain gpt-6-astra/xhigh. Read-only reviewer;
main made decisions, checked source and integrated production/fixtures.
This is F04/F05 camera integration review, not final overall F10 signoff.

| Senior recommendation | Disposition / independently checked result |
| --- | --- |
| Retain native stereo renderer and smallest startup guard | Adopted. Main read native model/world MVP, per-eye correction and projection submission.07f83e6e adds one condition to native startup menu branch. Actual default XR menu/no-demo and fresh desktop demo1/12frames/normal0 pass. |
| Reproduce paused body-owned horizontal freeze | Adopted. Main read latched owner, paused retained base and zeroed horizontal coordinates; actual-source fixture fails134 with expected fresh movement absent.8071a46b narrow snapshot/shared helper restores it. |
| Native mirror geometry proof; distinguish compositor mapping | Adopted. Main inspected native baseline/lateral/pitch/roll mirror: differential near/far shift and world geometry rotation; clean native run0. Compositor sees unchanged simulated physical head and remaps injected submission; mask tilt alone is not proof. |
| One combined full camera case at nonzero game yaw | Adopted. Actual-source XYZ/21°yaw/-13°pitch/9°roll at game yaw37 passes independent Hamilton-product oracle, center/both eyes and restore. |
| Snapshot normal writer must be camera preparation | Adopted. Main checked R_TrackedHeadBodyOffset query does not advance an unpaused sample; fixture verifies later queries do not change next paused baseline. |
| Missing baseline seeds once; reference reset idempotent | Adopted. One first valid paused sample seeds. GL_BeginRendering invalidates before consumers; helper does not repeatedly clear snapshot on reference_changed. Final main fixture keeps that flag through repeated queries and passes0. |
| Temporary pose/base unavailability differs from retirement | Adopted. Main checked early query rejection and latched-owner false paths preserve baseline; reference/session/hard owner/client resets retire. Fixture recovery cases pass. |
| Renderer non-writing does not prove input discards paused motion | Adopted. Main read native accumulator/context gates; ASAN/UBSAN actual-source pause/resume fixture passes; actual private GPU sampled pending/last payloads0 and unchanged server body. |
| Resume restores collision body and can move camera back | Adopted with explicit limit. Paused presentation offset retires, normal collision/command policy resumes. Seamless resume continuity would require additional placement policy and is outside this repair. |
| No parallel renderer, tracking protocol or saved-setting policy | Adopted. Two coordinates/validity at existing renderer owner, shared consumer helper, narrow lifecycle reset called by existing V_ResetTrackedAim. Initial/public/LOCAL origin and input movement solver retained. |

Main also identified a lifetime gap where transient protocol0 need not render
during client/map reset. Direct V_ResetTrackedAim clears body and camera snapshot
together; Astra checked CL_ClearState/disconnect ordering and found no P1 lifetime
or ordering issue. Final camera fixture executes that reset while still private,
paused and attached, then fresh-seeds/moves successfully. Current native private
four-phase GPU run repeats after this wiring with normal0 and clean validation.

Actual simulated head is valid without an active-tracking bit. Snapshot follows
native valid render/motion eligibility; wire/avatar stricter guards remain.
The senior source follow-ups cleared the focused helper/reset; main is responsible
for executed test/input/image claims. No physical headset/provider or broad
camera latency/collision/multiplayer certification is implied. No human decision
was required. [Exact current proof and failures](sixdof-current-qualification-2.0.md).
