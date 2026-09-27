# Tracked weapon collision on the vkQuake base

The inherited fork retracts a tracked held weapon when its grip or muzzle
would enter world geometry. The `2.0` client already has a read-only
`CL_TraceWeapon` scene query, a tracked `cl.viewent`, and a calibrated private
command muzzle. The first opt-in Classic Shotgun slice now calls the collision
query for both command and presentation poses.
This feature belongs at those existing boundaries; it does not need another
model manager, movement solver, or renderer.

## Minimal adapter versus replacement

| Choice | Reuse and state | Incompatibility/evidence | Smallest proof |
| --- | --- | --- | --- |
| Adapt the donor's stateless two-stage resolver | Keep vkQuake scene query, OpenXR hand sample, calibration slot, `cl.viewent`, command record and server muzzle clamp. Two evaluations use one resolver; no new persistent weapon state. | The source resolver needs several traces. `CL_TraceWeapon` is main-thread-only because it borrows PMove box-plane scratch; `V_SetupFrame` currently runs in a task. | Classic shotgun near a wall: the same completed presentation sample feeds both eyes and crosshair; command muzzle uses its own sample and reaches the pinned server. |
| Transplant donor `vr.c`/OpenGL weapon rendering | Duplicates model selection, world trace, animation and presentation ownership. | No demonstrated vkQuake incompatibility requires replacement. | Rejected. |

The render sample and command sample are taken at different times, with server
receive/relink between them. A single saved offset cannot be translated into
the later presentation frame: a wall may move, and stair rebase changes the
trace. Both evaluations must use the same stateless resolver, with raw and
render-rebased geometry respectively. The command subtracts accepted
room-scale displacement exactly once after muzzle correction. Tracking loss,
model change, map change and disabled collision cannot retain an old offset.
An unresolved trace returns the raw pose; it does not assert that geometry is
clear.

## Astra senior-review disposition

An Astra xhigh source review caught the task/thread boundary and the separate
sampling times before implementation. Its effective model setting could not
be inspected from inside the agent, so this records the requested invocation
settings and the source critique rather than claiming runtime qualification.

| Recommendation | Disposition |
| --- | --- |
| Move XR-only `V_SetupFrame` to main-thread preparation, then solve presentation collision before crosshair; leave draw/GPU tasks with vkQuake. | **Adopt for the first slice.** The current setup task calls `V_SetupFrame`, while the trace mutates PMove scratch. Keep desktop ordering and call view setup only once. |
| Re-solve for command and presentation rather than reusing a rebased command offset. | **Adopt.** Preserve distinct frame samples and the inherited stair rebase. |
| Import the entire contact/melee producer before wall retraction. | **Reject for this slice.** The pinned private muzzle path already reconstructs and clamps weapon use. Start with an explicit opt-in local collision control; do not claim donor server-admin policy parity until its narrow veto is negotiated. |
| Use grip and calibrated muzzle as the initial non-melee endpoints. | **Adopt.** Immersive melee edge geometry remains a separate behavior gate. |
| Treat equal translation of gun, crosshair and muzzle as full alignment proof. | **Reject.** The server separately clamps eye-to-muzzle, crosshair impact has another trace policy, and held/model calibration can differ. Observe post-clamp shot and actual impact before claiming parity. |
| Profile the repeated `CL_TraceWeapon` entity scan on `mj4m1`. | **Adopt.** Measure whole-solve p95 and candidate/hull counts before adding broadphase work. Never use PVS culling to exclude a nearby solid. |

The final feature must preserve the inherited server policy, mod/model edges,
both hands, and shot/barrel alignment. The first enabled path is deliberately
opt-in while those gates remain. Desktop rendering and ordinary movement must
keep their vkQuake owners.

## First implementation checkpoint

`vr_weapon_collision` defaults to `0`. When explicitly enabled with a tracked
Classic Shotgun and an eligible gameplay frame, `CL_ResolveWeaponCollision`
reuses the existing main-thread `CL_TraceWeapon` scene query. The command
producer corrects its muzzle before the existing room-scale subtraction;
the view owner solves its later render pose on the main thread and shares that
translation with the held model and crosshair. Other weapons and unresolved
geometry keep their raw pose. Linux normal and no-curl builds pass. This is
source/build evidence only: actual near-wall firing, all-model collision,
server policy, moving brush behavior and runtime cost remain unqualified.
The focused private-solid fixture also exercises a received box obstructing
the production resolver's muzzle path. A subsequent Astra xhigh read-only
review found no actionable code defect in the scoped integration, including
its task and audio call ownership. It did not qualify a live shot or headset.

## Server capability adapter disposition

A further Astra xhigh design review checked the donor contact offer against
the `2.0` signon and command paths. Its effective model setting was not
inspectable from the reviewer, so the review records the requested setting
and source findings rather than asserting runtime model provenance.

| Finding | Decision |
| --- | --- |
| Command retraction is private-only, but presentation currently accepts a local collision toggle on a public peer. | **Adopt.** Require one effective predicate: pinned private profile, local opt-in and server COLLISION bit. Keep the ordinary gameplay/tracking guards. A public peer cannot activate it with an offer. |
| `Send_Spawn_Info` clears the reliable client message; an offer queued during signon can be lost. | **Adopt.** Use one bounded post-`begin` per-client offer/update helper. Invalidate its sent mode on each serverinfo/map, retry when the reliable message has room, and avoid a second signon fallback. |
| The donor's COLLISION mode may produce contact payloads, even without a melee profile. | **Adapt.** Advertise only COLLISION or zero, with profile NONE. The current client emits stock-ranged contact samples and the server can use them for QuakeC touch callbacks; this behavior is already active when collision is authorized. A default-on change must account for those gameplay effects. |
| Donor's command handler trusts permissive integer parsing and state resets on map but not necessarily all stop paths. | **Adapt.** Reuse the server-command registry with exact bounded decimal parsing; malformed server offers clear authorization. Reset on map, disconnect and demo stop. |
| Revocation cannot rewrite commands already sampled or in redundant send history. | **Adopt.** Newly sampled commands use the new mode; the next synchronized view setup restores the raw gun/crosshair. Do not introduce a policy epoch or rewrite pending commands. |
| A geometry fixture alone does not prove corrected shots. | **Adopt as a later end-to-end gate.** Compare actual offer, corrected gun/crosshair, post-clamp shot and unchanged body at a stock-shotgun wall, then policy-off restoration and public/silent-server cases. |

The server's negative `sv_weapon_collision` policy should retain the donor's
single-player rule: non-dedicated with `maxclients == 1`, regardless of how
many players happen to be connected. The local `vr_weapon_collision` default
remains off. The contact capability must never select a wire dialect or imply
that melee gameplay has been implemented.
The implementation queues the offer in the ordinary reliable client message
after `begin`; `Send_Spawn_Info` invalidates the queued-mode marker when it
clears that message during fastload. The receiver checks the original argument
spelling as well as bounded numeric tokens, so incomplete quotes/comments
cannot authorize collision. A focused production-parser GDB smoke passes
public/private, mode, revocation, malformed syntax and local opt-out cases;
real network delivery and near-wall shot behavior remain open.
The subsequent Astra code review found two defects in the first patch: same-map
fastload could discard an unsent update, and permissive console tokenization
could accept a malformed quoted/commented offer. Both recommendations were
adopted in the reset and raw-syntax checks above. The reviewer CLI reported
`gpt-6-astra` at `xhigh`; backend routing was not independently observable.
Command retraction checks the selected `STAT_WEAPON` model used by the current
muzzle calibration; presentation also requires `cl.viewent.model` to match
that selection. This avoids applying the shotgun offset for a newly selected
weapon before the view entity refreshes.
The inherited client defaults `vr_weapon_collision` to on; the current `2.0`
default of off is a temporary gate while only the Classic Shotgun path exists.
Before declaring weapon parity, extend collision/contact handling to the
supported weapons and profiles, then restore the inherited default under the
server capability policy. Fixed foveation's separate opt-in default is not a
reason to keep weapon collision off.

The opt-in command and presentation path now accepts the seven stock ranged
viewmodels through one calibration-owned allowlist. Both paths still require a
valid muzzle calibration, and presentation still waits for the selected model
to match `cl.viewent.model`. Axe and custom models remain outside this slice:
the schema has no ranged/melee classification or general cutting-edge
geometry. This broadens the compiled path but does not prove shots near a wall
or restore the inherited default.

The same allowlist now includes the seven official Hipnotic/Rogue ranged
viewmodels (`v_laserg`, `v_prox`, `v_lava`, `v_lava2`, `v_multi`, `v_multi2`, and
`v_plasma`). The existing calibrated-muzzle and server-capability gates still
apply. Axe and hammer models remain on the separate melee edge path, and
mod-defined ranged/melee classification remains unresolved. A Linux build
passed; mission-pack near-wall behavior and the inherited default are not yet
qualified.

## Remaining parity boundary

The donor defaults `vr_weapon_collision` to `1` and resolves the command muzzle
even for immersive melee, using its actual cutting edge for the collision
query. The `2.0` command path skips generic retraction for recognized melee;
its separately retracted held mesh/contact does not always move the private
firing origin. The current `sv_immersive_melee` default is also `0`, while the
donor's server default is automatic (`-1`). These gates need a coordinated
review before collision can safely default on.

The present stock-ranged allowlist does not classify mod viewmodels. Muzzle
calibration alone is insufficient because the supplied calibration data also
contains melee models. Uncalibrated alias viewmodels currently cannot produce
an ordinary private controller pose, although the donor uses the raw grip as
its muzzle fallback. Any general mod policy must preserve existing dedicated
held-melee and paired-weapon collision paths rather than applying generic
retraction a second time.

## Collision parity senior review (Astra Max)

An Astra `gpt-6-astra`/max read-only review compared the donor and `2.0`
command, contact, and presentation owners at `149e8745`. It found that
restoring the local default now would widen gameplay, not just presentation:
ordinary collision contacts can activate QuakeC buttons. Its recommendations
are staged here; the review did not run a headset or gameplay test.

| Finding | Disposition |
| --- | --- |
| Recognized melee corrects its visible edge but leaves the private command muzzle raw. | **Adopt.** Resolve the raw selected edge for the command sample and translate the calibrated muzzle before serialization. Keep the later render sample separate. |
| Held and paired melee contacts incorporate render collision displacement, whereas donor physical contact edges remain raw. | **Adopt.** Keep raw tracked geometry and velocity for physical contact. Presentation collision remains a separate, local result. |
| Generic collision eligibility is currently a stock-ranged list. Recipe names alone cannot exclude melee because an axe without a MELEE offer needs the generic fallback. | **Adapt.** Define active endpoint ownership using validated model and capability state. Preserve dedicated held/paired paths and one presentation offset. Expand calibrated mod retraction separately from contact publication. |
| Uncalibrated aliases cannot enter the ordinary private-pose path. | **Stage.** Provide a raw-grip muzzle fallback only for valid alias geometry and finite pose input, then qualify shots and crosshair alignment. |
| Collision defaults off in `2.0` while donor defaults on. | **Stage.** Restore `vr_weapon_collision 1` after endpoint ownership, contact gameplay, and mod fallback are covered. Preserve an explicit archived `0`. Leave the server melee default unchanged in this slice. |

The end-to-end proof should use loopback command serialization, view
preparation, server muzzle handling, and QuakeC. It must observe shot impact,
button activation, and unchanged player position with a calibrated mod gun,
an axe with and without MELEE authorization, an exact held/pair path,
obstruction, and contact-history reset. Resolver-only fixtures cannot establish
those outcomes. Runtime cost on a large map remains to be measured before a
broadphase or other trace optimization is added.

The first parity slice now shares a raw-edge calculation between recognized
melee command correction and physical contact. Stock axe geometry is rebuilt
from the current hand sample. Held-mesh geometry reuses the validated prepared
model but recomputes its edge from current hand angles; it does not reuse a
pre-turn render edge or its collision offset. Paired contact endpoints also
remain raw. The local Linux build passes. Collision remains opt-in, and this
does not qualify near-wall shots or button outcomes.

A follow-up Astra xhigh code review caught the held-edge yaw hazard before
commit: local turning can occur after the cached render edge is prepared, so
converting that edge with current yaw would mix two orientations. The patch
was changed to recompute the held edge with the existing model matrix helper.
The prepared held model is still required, so a failed or unavailable prepared
entity can suppress its physical contact and command-edge correction. This
remaining coupling requires end-to-end qualification before claiming full
donor parity.
