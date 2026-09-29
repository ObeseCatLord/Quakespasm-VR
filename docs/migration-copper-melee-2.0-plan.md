# Reuse the inherited Copper axe adapter

Status: client geometry/presentation implemented; server adapter in progress.
Only branch 2.0; the user's
dirty migration-2.0.md stays untouched. Builds/tests are deferred until the full
implementation is finished. This bounded slice does not close the whole melee,
movement, foveation, packaging or migration scope.

## Behavior and existing owners

Copper-family physical axe strikes should pass the accepted server sweep to
the original W_FireAxe. That routine retains native damage, knockback, leech,
sounds and target filtering. Unsupported programs/models retain native trigger
attacks. Existing immersive-melee policy and private peer negotiation remain.

Verified sources: inherited Quake/vr_melee_copper.h contains five exact mounted
VM descriptors, selection/readiness, native quad/whiff prelude, initial helper
trace substitution, retry misses and return-time fraction repair. The source
tools/immersive_melee/copper_family/function_metadata.tsv records program SHA256
identities. Its Quake/vr.c profile table pins the 70932-byte MDL, CRC32 f5d8df1b,
155 vertices/198 triangles/51 frames, ready frame0 and edge112/124, under either
v_axe.mdl or v_axe2.mdl.

Current2.0 sv_phys.c already owns accepted command/contact continuity, sweep,
stroke/whiff, trigger suppression, borrowed native-call context, trace scope and
PR_LeaveFunction fraction repair for Dwell. gl_model.c caches two source-pinned
edge points before discarding CPU poses; vr_input.c consumes that cache using
the same calibrated transforms as rendering. progs.h supplies stack/callsite
metadata; PR_LeaveFunction calls SV_VRAxeTraceLeaveFunction before local restore.

## Adapter comparison and chosen contract

Preferred: copy the inherited descriptor/selection/trace rules into a Copper
header, adapt progscrc and field/function lookup at their existing boundaries,
and extend the current axe owner. Reuse its trace scope with a Copper mode and
its completion/cleanup; no new queue, solver, attack scheduler or wire fields.
Extend the source-pinned edge cache and input authorization rather than port
the old OpenGL renderer or retain all CPU model vertices.

Copying the entire old melee dispatcher would duplicate current contact and
weapon-pose ownership. Pretending Copper is stock loses traceline2 retries,
native sounds/timing and helper fraction semantics. Both routes are rejected.
Expected scope is the copied header plus about200 integration lines; reopen
if integration needs another persistent combat state or changes native desktop
behavior beyond accepted private VR contact.

1. Preserve exact descriptor/ABI pins; supplement with recorded whole-program
   identities, not folder-name admission. Reject malformed optional numeric
   fields before integer casts.
2. Offer the existing Copper contact profile under current melee policy.
   Client authorization requires the exact cached selected MDL and the accepted
   profile. Unknown/replacement assets remain native.
3. Reuse current single-hand axe selection, stroke, sweep and whiff owners.
   Copper's inherited edge/reversal behavior must follow the reference, not
   silently inherit another family's tuning. Preserve native animation when
   not authorized and freeze the verified ready pose only for accepted geometry.
4. In the existing native outcome, execute original SuperDamageSound then
   W_AxeWhiffSound, apply show_hostile and .49 recovery, and call W_FireAxe
   only for a valid hit. No W_AxeSwing/Think/Cycle scheduling. Revalidate VM,
   owner and weapon after each callback before accessing retained QC pointers.
5. The existing trace owner supplies one hit to the exact root->traceline2
   acquisition. Filtered retries receive well-formed misses; only the exact
   helper return restores the temporal fraction after native target/endpoint
   filtering. Root return, errors, relocation, reset and callback invalidation
   retire/neutralize that same scope; no unrestricted fallback acquisition.
6. Keep desktop, Dwell, stock/mission packs, Alkaline and direct QBJ3/Enyo
   boundaries intact. No parallel multiplayer weapon offsets are introduced.

## Ownership and review

Main owns plan/scope/index and client integration in gl_model.c/h,
vr_input.c/h, r_alias.c and vr_weapon_calibration.c. The alias hook is read-only
and Copper-only; view.c already consumes the shared edge helper.
One coding worker may own only sv_phys.c, sv_main.c and a new
vr_melee_copper.h; server.h only for a required declaration. Those writes are
disjoint. No source-reference repo, tests, runtime installation or deployment
edits. Main reviews/integrates output; local Astra Max verifies then critiques
this mostly-worked boundary and later the final source.

## Astra Max design disposition

| Finding | Disposition and verified evidence |
| --- | --- |
| Stock/Copper v_axe pathname collision | Accepted. Add a pin discriminator to the existing two-point cache and require it in the family getter. Preserve the held-mesh cache owner. |
| Recursive traceline2 could escape the owned acquisition | Resolved by reading all five mounted bytecode programs. The recursive call is guarded by `(traceflags & 3) == 2`; W_FireAxe supplies zero and the helper never writes traceflags. Calls 8853/8863/9326/9081/9217 are therefore unreachable for this exact acquisition. Keep the immediate caller/callsite pins; no broader descendant interception. Loop retries at the pinned builtin still receive misses. |
| Prelude retains fields across callbacks | Accepted. Revalidate VM storage, edicts, owner, selection, idle think and contact cursor/origin after each native callback; reacquire fields before writing. Failed owned acquisition remains miss-only until root return. |
| Copper ready pose should not rewrite entity animation | Accepted. An observational input predicate gates frame0 in R_SetupAliasFrame, including exact selected geometry and tracked session/menu/calibration checks. gl_screen.c joins draw_done_task after all alias-draw tasks, so the render predicate reads stable frame input. |
| Reversal/recovery and whiff policy | Accepted. Reuse inherited edge-only sweep/overlap recovery and ALK reversal rearm; Copper whiff performs only the native prelude. Fraction repair restores trace.fraction, never sweep event_time. |

Copy the generic inherited v_axe2 calibration (-3.5,34,41.5,.33), retaining
the existing QBJ3 exclusion. Schema/user calibration remains authoritative.
Do not add folder-name admission or further mod-specific behavior.

## End-of-goal acceptance

Build Linux and Linux ARM after implementation. Reuse the inherited Copper
fixture patterns at actual selected command/QC owners: each recorded program,
hit/world/whiff, helper filtering/retry/return fraction, duplicate/gapped input,
reversal/cooldown, held trigger, missing/model replacements, left-handed input,
callback death/free/relocation and context cleanup. Require native damage and
once-only outcomes, not admission bits alone. Preserve ordinary desktop attacks
and stock/Dwell/Alkaline regression behavior. Headset feel and performance are
user-deferred. Until then, source implementation is not runtime certification.
