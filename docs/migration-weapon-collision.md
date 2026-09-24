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
| The donor's COLLISION mode may produce contact payloads, even without a melee profile. | **Adapt.** Advertise only COLLISION or zero, with profile NONE. Keep the existing decoder compatible, but do not emit or consume contact gameplay samples until that separate feature is ported. |
| Donor's command handler trusts permissive integer parsing and state resets on map but not necessarily all stop paths. | **Adapt.** Reuse the server-command registry with exact bounded decimal parsing; malformed server offers clear authorization. Reset on map, disconnect and demo stop. |
| Revocation cannot rewrite commands already sampled or in redundant send history. | **Adopt.** Newly sampled commands use the new mode; the next synchronized view setup restores the raw gun/crosshair. Do not introduce a policy epoch or rewrite pending commands. |
| A geometry fixture alone does not prove corrected shots. | **Adopt as a later end-to-end gate.** Compare actual offer, corrected gun/crosshair, post-clamp shot and unchanged body at a stock-shotgun wall, then policy-off restoration and public/silent-server cases. |

The server's negative `sv_weapon_collision` policy should retain the donor's
single-player rule: non-dedicated with `maxclients == 1`, regardless of how
many players happen to be connected. The local `vr_weapon_collision` default
remains off. The contact capability must never select a wire dialect or imply
that melee gameplay has been implemented.
The inherited client defaults `vr_weapon_collision` to on; the current `2.0`
default of off is a temporary gate while only the Classic Shotgun path exists.
Before declaring weapon parity, extend collision/contact handling to the
supported weapons and profiles, then restore the inherited default under the
server capability policy. Fixed foveation's separate opt-in default is not a
reason to keep weapon collision off.
