# Tracked weapon collision on the vkQuake base

The inherited fork retracts a tracked held weapon when its grip or muzzle
would enter world geometry. The `2.0` client already has a read-only
`CL_TraceWeapon` scene query, a tracked `cl.viewent`, and a calibrated private
command muzzle. It does not yet call the collision query for either pose.
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
