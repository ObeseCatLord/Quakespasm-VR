# Inherited QBJ3 respawn lifecycle adapter

2026-09-30. COOP-013 missing respawn cleanup, following the generic
[respawn-policy plan](coop-respawn-policy-2.0-plan.md). Co-op revival is excluded.
No physical-contact melee implementation or new mod ability is authorized here.
Status: source-integrated after the corrections/recheck below; final Linux/ARM
software qualification is pending. The retained brief preceded implementation.

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

## Verified integration brief for the source decision

Main read the actual primary implementations at2211–2597,2950–3020,
3120–3156 and3846–3904. Generic stage1 cleanup is currently being corrected
by a separate coder; its adopted callback contract is in the parent plan.
This brief plans stage2 only; implementation must wait for generic integration
and the source disposition. Requested Astra/max effective settings cannot be
verified by the current tool, so its result will be source advice only.

| Fact | Evidence/status |
| --- | --- |
| Mod APIs use named functions and typed fields, not a new ability registry | Verified primary2211–2315; existing2.0 typed-field/void-layer helpers are at sv_phys.c:566–609 before pending corrections. Native ED_FindField/Function remains the lookup owner. |
| Berserk active means items_qbj bit4 or berserk_finished greater than QC time | Verified current physical selection at sv_phys.c:3849–3875; physical selection additionally pins the program/model/selected weapon and therefore is not reusable as an expiration gate. Copy only the typed active predicate with finite/range admission. |
| Native context has self/other/time, argc and parameter/return scratch | Verified four repeated donor callback wrappers2353–2498. Current SV_SaveQCInputs/SV_RestoreQCInputs already own cooperative input borrowing; actual server destruction does not return into the stack (generic review). |
| Recovery/cleanup runs after ordinary PostThink | Verified primary3846–3856;2.0 already has one actual-PostThink completion and enclosing cancellation scope, including shared-QC windows. Do not invoke recovery at every accepted command head or invent another Think schedule. |
| Passive plunge entry must set pending cleanup, and timer/pending bits reset with current slot/map owners | Verified primary lifecycle arrays and Begin/End/Finish helpers; extend existing per-client metadata only. |
| Donor serialization helper calls ordinary RestoreInventory on a detached edict | Verified primary3120–3137 versus RestoreInventory3015 calling W_ChangeWeapon after stale-model rejection. This contradicts its no-transition-QC comment for that case; copying that callback path into snapshot preparation is rejected. |
| Fresh saved-player restoration is outside ordinary physics policy | Verified current host_cmd spawn/restore and sv_phys RestoreSavedInventory owner. Stage2 callbacks there need only the existing bounded cancellation binding; do not invoke full BeginPostThink/cooldown. |
| Runtime correctness/build/callback execution | Unverified; all software checks are deferred until complete implementation. |

Current lean: copy typed layer/API/limbo/expired-model logic, consolidate only
the repeated donor borrowed-context call bodies at a single private helper,
and reuse existing input borrowing/cancellation. Named function signatures and
the actual call's scalar/entity argument encodings remain explicit. Restore
borrowed call context/basis/trace/input, not a full saved gameplay-global struct.
Entity/destination writes and subsequent callbacks require the original live
owner; global unwind still occurs after client cancellation in a surviving VM.
Retain the destination while the native limbo callback runs. Do not add storage
generations, a dispatch framework, a second client identity or physical-melee
dependency.

Expired-fist rejection in inventory copying may be passive, but selector/stack
callbacks belong only to the fresh live player's restore/completion boundary.
Detached save projection and the live dead corpse during changelevel must never
run W_ChangeWeapon or other transition callbacks merely to encode inventory.
This is an explicit narrow safety correction to the actual donor reference.
Active berserk remains protected by the typed item/timer predicate.

Open decisions: exact callback-context reuse and minimum write boundary;
single actual-PostThink recovery timing; destination/client cancellation and
one semantic discontinuity after a native forced relocation; lifetime of pending
void cleanup; safe fresh-body selector versus serialization ownership. Rank
these by actual source risk and challenge whether any callback/helper is
unnecessary. Source depth budget: donor named helpers/inventory/completion and
current typed/binding/save/input owners only. No generic movement re-review,
revival, new mod abilities, physical melee, wheel, graphics or runtime tests.
Expected300–450 net lines, no new owner; reopen above450 or broader dispatch.

## Adopted source disposition and one reopened lifetime question

Main spot-checked the requested-Astra source advice against the donor inventory,
completion and callback bodies, current fresh saved-player caller and native
retention. Effective settings remain unobservable; this is source advice only.

| Recommendation | Disposition before implementation |
| --- | --- |
| Serialization/corpse encoding must never call transition QC | Adopt: keep incoming model evidence in passive serialization and dead-changelevel transfers. Only a fresh live-body restore rejects an expired incoming fist model and invokes W_ChangeWeapon(selected,1). Preserve fresh QC model until that repair. |
| Saved restore must propagate cancellation beyond the callee | Adopt qboolean owner-survival return and a narrow Host_Spawn_f early return before alpha/frags, saved-slot consumption, placement and signon. Missing optional APIs mean unchanged live-owner success. Existing admission permits active pre-begin clients; do not require spawned. |
| Four callback wrappers duplicate borrowing | Adopt one private invocation/unwind helper with explicit named signatures and encoded arguments at callers. Reuse current input snapshot. Borrow host_client/sv_player and context/parameter/return/basis/trace only; preserve gameplay globals and surviving VM unwind after cancellation. |
| Recovery must not run at every accepted head | Adopt existing actual-PostThink completion, before the mod-owned bypass, retaining the binding through callbacks. Copy native six-second fully frozen limbo retry with finite clocks/flags and reversal reset. |
| Forced native relocation can double-publish an epoch | Adopt captured existing epoch and one publication if still unpublished, coordinated with generic successful respawn. Preserve authored hold and accepted queues; color cleanup publishes none. |
| Destination history needs a new identity registry | Reject: use inherited current reference/alignment/range/class admission and native retention during the callback. No claim of historical identity across the six-second wait. |
| Pending cleanup belongs in the transient think window | Reject: extend existing slot metadata/reset owners with timer/pending bit; set pending on dead mod-owned plunge and keep it until the guarded layer disappears or lifecycle resets. |
| Invalid typed berserk state means expired | Reject: validity and active result are separate. Missing/mistyped/nonfinite state cannot authorize expiration cleanup; no selected-weapon/program-pin/physical-profile gate. |
| csf_clear_all selectively removes only priority70 | Reject that unproven guarantee. Preserve donor call and exact admission; csf_clear(self,100) remains the separate inactive-berserk cleanup. Do not claim stronger callee behavior without QC evidence. |

Revised stage2 ownership is sv_phys.c plus only the saved-restore return contract
in server.h and caller cancellation in host_cmd.c. Target350–450 net lines,
including required lifetime adaptation; no stage2 sv_main.c change is expected.

## Final lifetime disposition before coding

The follow-up requested-Astra source review caught a concrete error in the
previous brief: PF_clientcommand dispatches src_client, but spawn is registered
with Cmd_AddCommand (src_command), and the current source guard rejects it.
Main verified the complete Cmd_AddCommand2 body: no alternate/twin client
registration exists. The earlier claim of current reachability is withdrawn.
The [registration prerequisite](host-command-registration-2.0-plan.md) restores
the QSS-M source classes for required native player commands, without weakening
the source guard. After that separate prerequisite, the active pre-begin saved
spawn route is admitted and the planned cancellation borrowing must nest.
Runtime execution remains unverified.

| Follow-up recommendation | Main disposition |
| --- | --- |
| Current clientcommand(spawn) is rejected | Adopt verified correction above. Repair demonstrated ordinary multiplayer registration as its own zero-net-line prerequisite, not a dispatch rewrite. |
| Nested borrowed ownership needs traversable parents | Adopt previous, VM, client and edict pointers in the existing policy record and one transient head. Reuse native temporary-scope ownership; no copied second binding representation, registry, heap or generation. |
| Targeted cancellation must reach every matching scope | Adopt sticky cancellation across the bounded live chain, retaining links for normal unwind. Full abort/VM clear marks all and detaches head before nonlocal unwind. Pop only the current record; never reinitialize a linked record. |
| Completion survival was cached before new QC callbacks | Adopt recomputation after callback-capable EndPostThink, before shared-death/inventory/ACK writes. This is a stage2 integration requirement, not a demonstrated generic defect before callbacks exist. |
| Nesting requires a broad command/spawn rewrite | Reject: restore native player registration only; preserve native spawn/context limitations and borrow actual owner for adapter calls. |

Nesting/caller adaptation adds approximately30–50 net lines within sv_phys.c.
The stage2 target is now400–450 net lines, including the existing planned
server.h boolean return and Host_Spawn_f cancellation continuation. Reopen
above450 rather than omit required semantics. Actual generic2e7536b9 remains
the behavior base. No further source framework or generic movement rewrite is
authorized. Effective reviewer settings are unobservable: this disposition
uses requested-Astra/max source advice, not certified model/runtime acceptance.

## Integration review corrections before commit

The first413-net-line three-file implementation is under main/source review;
it is not yet source-integrated or runtime-qualified. Main read the full patch
and actual donor model rejection, and the local requested-Astra source review
verified these two corrections to the fresh model helper before integration:

| Finding | Adopted narrow correction |
| --- | --- |
| Expired model rejection was gated by W_ChangeWeapon availability/finite weapon | First identify exact incoming fist and valid inactive typed state, then passively restore the fresh QC model independently of the optional selector. Primary model rejection is likewise independent of later selector success. |
| Saved helper could invoke the selector for a corpse or frozen limbo body | Gate only the callback with health>0, DEAD_NO, solid!=SOLID_NOT, finite weapon and matching named signature. Do not require spawned, skip passive rejection or cancel a surviving owner merely because the optional callback is unavailable. |

This stays in the existing private repair helper. No deferred selector state,
new owner or serialization callback is added. Main also reopened the prediction
epoch publication timing for source disposition before commit; its earlier
completion claim remains unverified until that review is reconciled.

## Joint epoch and retry disposition before correction

The consolidated requested-Astra review verified two additional source-contract
gaps in the first patch: ordinary mod-owned dead-to-alive completion can take
the bypass without an epoch, and limbo recovery can publish before generic
completion captures its separate epoch. Main independently inspected End,
PF_setorigin and contact invalidation: setorigin does not itself publish the
private movement epoch. The native world wrapper covers identified teleport
touches, not direct scheduled Think/PostThink completion.

Adopt one captured existing client epoch in the existing borrowed policy at
Begin, before QC. Recovery and successful generic/mod-owned completion share
that value. For policy relocation, preserve contact invalidation and let End
publish once; standalone/admin relocation retains its direct publication.
Mod-owned completion before the bypass also invalidates old contacts and
publishes only if that captured epoch is still unchanged. Preserve authored
deadlines via SV_PrivatePlayerTeleported(ent,true), with no queue retirement,
new protocol state, registry or generation.

Also preserve the actual donor timer when a forced retry remains in limbo; do
not introduce another six-second delay. Failed/invalid destination admission
and clock reversal still reset through existing timer logic. The source review
found no additional sticky-chain/unwind/caller-survival blocker.

Adapt its deletion recommendation: remove the added saved VM/progs/edict/global
address captures and changed-storage early return. No returning VM destruction
route was established; native teardown is nonlocal. A simple native server-VM
assertion after the callback documents that established contract before normal
context/input/retention unwind. This is simplification, not certified general
VM recovery. No other native command/spawn/physics ownership is reopened.

The first model-helper correction is main-reviewed at418 net lines. These
remaining corrections stay in sv_phys.c and within the existing450-net target;
main reviews the exact patch before integration. Software checks remain deferred.

## Main correction checkpoint, source recheck pending

Main reviewed the complete original three-file diff, the five-net-line fresh
model correction and the final six changed regions. The current413-net patch
uses one Begin-captured epoch for limbo/generic/mod-owned completion; policy
relocation leaves publication to End, standalone relocation keeps native direct
publication. Forced limbo retry retains donor timing. Passive expired-model
rejection precedes live/pre-begin/finite/signature callback admission. Invoke
uses the established server-VM contract and always performs normal borrowed
context/input/retention unwind after client cancellation. Extra storage-address
captures/bailout were deleted. No persistent selector, protocol generation or
queue retirement was added.

The narrow Host_Spawn_f return precedes alpha/frags/saved-slot consumption/
placement/signon after the new callbacks. Existing slot/map reset owners retain
pending void/timer metadata, and full abort detaches the borrowed scope chain.
Scoped whitespace checks pass. This records main source review only: local
requested-Astra recheck and production integration are pending at this point,
and all executable/platform qualification is still deferred.

## Production integration and final scoped source acceptance

The final413-net-line patch is integrated in sv_phys.c, server.h and host_cmd.c.
The local requested-Astra recheck read the six corrected regions and found no
additional P1/P2 source blocker; its earlier bounded chain/unwind/caller review
likewise established no additional lifetime blocker. Main independently reviewed
the full patch and both correction slices before integration. Typed limbogoal/
API checks, borrowed context/input/retention, persistent void cleanup, valid
inactive berserk protection, passive fresh-model rejection and native selector
admission reuse actual primary/native owners. Save/corpse encoding stays passive.

One Begin-captured epoch coordinates native recovery and generic/mod-owned
respawn completion without accepted-command retirement. The native command
registration prerequisite is6f090b1f. No revival, physical-contact feature,
new mod abilities, second scheduler or general VM/device recovery was added.
Scoped git diff --check passes. No build/compiler/test/fixture/probe/benchmark
ran. This is source integration with requested-Astra advice and unobservable
effective settings, not certified review/runtime acceptance or full migration
completion. The final Linux/ARM and broader co-op qualification remain pending.
