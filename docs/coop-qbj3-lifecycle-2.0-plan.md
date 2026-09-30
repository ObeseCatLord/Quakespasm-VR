# Inherited QBJ3 respawn lifecycle adapter

2026-09-30. COOP-013 missing respawn cleanup, following the generic
[respawn-policy plan](coop-respawn-policy-2.0-plan.md). Co-op revival is excluded.
No physical-contact melee implementation or new mod ability is authorized here.
This is a before-code brief; generic policy integration precedes this slice.

## Actual behavior and reusable references

Primary `51b452c0` `sv_phys.c:2211–2597` implements typed compatibility for the
existing QBJ3 void/plunge/limbo callbacks and color-shift stack. It checks named
native mod APIs and exact field types before applying the inherited exception.
The generic respawn stage must already bypass the mod-owned plunge lifecycle;
this follow-up adds only demonstrated missing recovery/cleanup.

The reference recovers a fully frozen stuck limbo after six seconds by setting
the mod's existing dest.z force flag and running teleport_limbo_think once.
It validates the destination reference/class before invoking that callback.
It does not replace portal movement or call teleport_exit_limbo in isolation.
After mod-owned respawn completes, pending cleanup calls csf_clear_all only for
the orphaned exact priority70/density255 dark-red void layer, including its
deferred stack entry. Existing QC remains responsible for the stack operation.

The reference separately calls csf_clear(self,100) only for the expired exact
priority100/density32 red berserk layer; active item/timer state protects a real
powerup. It rejects retained progs/v_berserk.mdl after fresh QC removed berserk,
then uses W_ChangeWeapon(selected,1) to make the weapon/model agree. It does not
copy corpse callbacks or overwrite arbitrary color-shift fields.

Current2.0 source has native freeze/hold, teleport occupancy and typed inventory
owners, but no inherited named limbo/void cleanup helpers. sv_phys.c:3611–3640
already validates items_qbj/berserk_finished for physical-melee selection, using
program pins and selected model. That path is not a suitable spawning gate:
expired/unselected powerup cleanup must not depend on active melee or a physical
attack profile. Use the donor's typed API boundary instead, reusing the generic
policy's passive typed-field/function/color-layer helpers. Native ED_FindField/
ED_FindFunction use existing hash maps (pr_edict.c:390/426); no new registry or
per-mod discovery cache is needed.

## Minimal copied adapter, compared with replacement

Extend the same transient respawn state with the donor limbo timer and pending
void-cleanup bit, reset at existing slot/map/abort owners. Copy guarded reference
callback and stack-layer logic. Reuse native edict retention, context/input
borrowing, placement/discontinuity and typed inventory restore. Do not introduce
a second lifecycle manager, portal solver, animation runner or whitelist.

Borrow only QC call context: self/other/time/frametime, parameter/return scratch,
argc, basis/trace and cooperative input globals as needed by the enclosing
owner. Preserve intentional mod gameplay globals. Retain player/destination and
validate saved VM storage and same client identity before and after every QC
boundary. Retention alone cannot distinguish a reused reserved client slot;
share the generic policy's cancellation hooks, not a new generation protocol.
Abort/map teardown must release borrowed work before VM storage disappears.

Control admission must reject nonfinite/out-of-range flags before integer
conversion. Reject invalid named-function signatures, destination indices,
dead/free/reused targets and invalid timers. A failed optional recovery leaves
the mod's normal lifecycle active. Successful native relocation publishes the
existing movement discontinuity without discarding command queues. Do not turn
all mod freezes into a six-second engine timeout: only the copied typed QBJ3
teleport conditions qualify.

Estimated production write set: sv_phys.c and narrowly related existing reset/
save-spawn hooks if needed. Target300–450 net lines largely copied primary logic;
reopen above450, a new owner or broader mod dispatch. Integration points:
generic policy completion, alive saved-client restore, ordinary inventory
restoration and normal post-QC body refresh. Respect placement fallback and
native QC weapon timing; rendering/particles/network formats are unchanged.

## Decisions requiring source review before implementation

Verify the typed replacement for donor melee-descriptor guards, actual native
callback cancellation/context reuse after generic policy integration, when a
limbo retry may run without repeating native Think, orphan-stack cleanup lifetime,
and safe placement of the expired-fist correction in inventory/save callbacks.
Prioritize these over a broad QBJ3 gameplay rewrite. Main will spot-check and
record the local Astra disposition before this production slice starts.

Final Linux/ARM software checks use the existing supported QBJ3 program variants:
normal freeze/thaw, guarded stuck limbo, removed/reused destination, normal
mod-owned respawn, orphan void/deferred layer cleanup, active berserk preserved,
expired fist model corrected by native selector, save/load and client/map/abort
resets. Verify native movement/attack/color effects and desktop/VR prediction
fencing. Builds/tests are deferred until all migration implementation is finished;
user live trials and performance measurement remain separate.
