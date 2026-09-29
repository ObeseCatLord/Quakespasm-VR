# Generic melee fallback

Status: Astra Max reviewed; chosen implementation contract below. User requested built-in melee for uncovered
mods, reducing per-mod adapters. Scope remains Linux/ARM first, 2.0 only.

## Facts and open boundary

| Environment fact | Evidence/status |
| --- | --- |
| Weapon schema already owns selected models/calibrated offsets | Verified: Quake/vr_weapon_schema.h and vr_weapon_calibration.c; no melee classification/edge fields currently. |
| Server contact pipeline owns sweep/stroke/rearm/native physical outcomes | Verified: Quake/sv_phys.c SV_VRContactProcessMelee and SV_VRStockAxeOutcome. Exact VM adapters currently required. |
| Native QC timing/animation may defer damage to later think callbacks | Verified: inherited vr_melee_copper.h W_AxeSwing/Think/Cycle pins. Universal immediate-leaf execution cannot assume these contracts. |
| Generic weapon-pose scopes already exist | Verified: sv_phys.c SV_VRWeaponPoseSetOrigin and borrowed pose owner. Whether an unknown deferred attack can use tracked targeting is unknown; must not promise it. |
| Client gesture to normal attack input could avoid wire changes | Unverified: assess cl_input/vr_input pending-command levels/replay and trigger merge before choosing. |
| Native attack will hit the exact swept surface in unknown QC | Unknown; explicitly outside fallback guarantee without an authored/verified adapter. |

Main lean: preserve exact adapters for contact fidelity and add a generic swing
gesture that invokes the mod's ordinary attack once. Preserve native damage,
effects, animation, cooldown and reach. Expose melee classification and gesture
geometry through existing weapon profiles, with standard axe convention as a
built-in default. Unknown weapons require explicit configuration rather than
guessing every hammer/sword is melee-only. Native trigger remains available.

Compare client-only gesture-to-button mapping with reusing the server's contact
stroke owner and a generic profile. Prefer the smallest existing owner that
can merge real trigger input and preserve command replay semantics. No new
melee protocol or arbitrary client-selected QC functions. Reject engine damage
replacement and universal trace hijacking: neither can preserve unknown QC
multi-hit/delayed attacks or custom side effects.

Open decisions: client/server gesture ownership; minimal schema additions;
default axe classification; native trigger merging; limits of hand targeting
for delayed QC; avoiding duplicate stroke state. Reuse and deletion are in
bounds. Exact adapters should remain exceptions rather than the default path
for every unrecognised mod. Assess whether generic fallback can replace future
family-port work where the user does not require exact contact fidelity.

Review budget: one Astra Max, read-only, <=850 words with prioritized findings,
effective model/effort verification and file/line evidence. No source edits,
builds/tests, nested agents or renewed Copper review. Main will spot-check and
record a disposition before generic implementation. Copper is already underway
in a disjoint server/client slice and remains a compatibility adapter.

End-of-goal proof: unknown native axe mod without VM hash registration, one
ordinary native attack per accepted physical gesture, native cooldown and
damage/effects retained, held-trigger and command replay behavior, death/menu/
tracking loss reset, desktop unchanged. Contact-fidelity guarantees remain
limited to verified adapters; headset feel testing is user-deferred.

## Astra Max disposition and chosen contract

| Recommendation | Disposition / main spot-check |
| --- | --- |
| Guarantee an input pulse, not exactly one QC attack | Adopt. Unknown QC may reject a tap during cooldown or schedule multiple effects. sv_phys maintenance retains ordinary attack levels; no synthetic retry/damage queue. |
| Choose client input owner; omit GENERIC contact profile | Adopt. The server family pipeline would add classification and scheduling without proving native-contact fidelity. Server native QC remains authoritative over hits/damage/reach/cooldown; gesture recognition is ordinary client input. |
| Merge during command finalization before calibration suppression | Adopt. Verified cl_main.c CL_SendCmd applies pending pose before CL_FinishMove, whose Internal routine reconstructs cmd.buttons. Add one merge hook inside that routine; preview observes but does not consume. CSQC input filters remain afterward. |
| Exclusive small client gesture state | Adopt. Reuse accepted device/context, calibrated hand/model transforms and point velocity. Exact per-weapon adapters take priority; fallback only otherwise. Reset on model/weapon/hand/configuration/generation/context discontinuities. |
| Classification-only schema entries must survive parsing/storage | Adopt. Verified VR_SchemaHasHeldPresentation admission at vr_weapon_schema.c501 and VR_CalibrationEntryHasFields350 otherwise discard new-only entries. Extend these existing owners. |
| Avoid reach/ready-pose gameplay overrides | Adopt. Native animation and range remain; configurable endpoints affect gesture sensing only. No generic QC calls by name, delayed-think interception or damage replacement. |

Implementation stages and ownership:

1. Extend existing schema/parser/calibration storage with `melee` explicit
   boolean override, optional `melee_base`/`melee_tip` gesture endpoints in
   model-local MDL coordinates and bounded `melee_speed` sensitivity (metres/s).
   A lookup supplies standard v_axe/v_axe2 convention defaults unless explicitly
   disabled. Unknown/hybrid models require explicit opt-in. No substring or
   directory-name classification. Selected replacement geometry uses the same
   renderer transform; endpoints default to calibrated weapon/muzzle geometry
   if not authored, and are gesture geometry rather than damage reach.
2. In vr_input.c, retain only selected identity/reset generation, previous
   accepted sample/time/point, stroke/rearm and pending synthetic intent.
   Sample once per XR frame; reject nonfinite velocity, oversized sample gaps
   and discontinuities. Arc/rearm prevents constant high-speed auto-fire.
   Reuse existing context and calibration adjustment gates.
3. Hook CL_FinishMoveInternal after ordinary buttons are reconstructed and
   before calibration attack suppression. OR a current synthetic request;
   consume only on final creation, including while real attack is held.
   Existing command history/retransmission carries that finalized input;
   previews/catch-up commands do not replay the intent.
4. Main source review and local Astra final review. All builds/tests at the
   end of the whole migration implementation, including ordinary unknown-mod
   native dispatch and reset/held-trigger/replay cases.

Coding slice writes: vr_weapon_schema.c/h, vr_weapon_calibration.c/h,
vr_input.c/h and cl_input.c. Add only the completed frame's existing predicted
display time to vr_openxr.h/cpp for sample deltas; do not introduce another clock
or runtime sampling owner. Track configuration identity using the effective
profile, held/muzzle calibration and global weapon/world scaling values.
Server protocol/contact/outcome code stays unchanged
for generic behavior. Reopen if this becomes a second physical-hit solver,
server policy or more than a small gesture state. Exact inherited adapters
remain useful only for behavior that this generic input contract cannot supply.

The earlier stronger proof wording is superseded: promise one synthetic input
request per gesture with ordinary attack semantics. A cooldown-rejected request
is not queued for later, and unknown custom QC may generate its own multiple
effects. This limitation preserves mod behavior rather than inventing damage.

## Final source review corrections

Local Astra Max accepted the input/profile ownership and found four issues.
Main spot-checked the load-bearing declarations/transform consumers and adopted:

| Finding | Correction |
| --- | --- |
| Missing sixth muzzle-transform argument | Supply the existing physical-left-hand reflection argument. |
| Points compared across changing virtual yaw | Keep head-relative physical points and offsets in the existing yaw-zero tracking transform; sample velocities in that same basis. Explicit snap/180 turns discard continuity. No second yaw owner. |
| Geometry configuration identity incomplete | Snapshot effective model height/gun angle and decoded viewentity scale with existing calibration/scaling inputs. |
| Early rejected input retains partial stroke | Clear only generic gesture state in VR_InputMove's existing rejected context/angle-lock path; preserve ordinary held buttons. |

These are source corrections; no tests/builds ran. Local Astra Max follow-up
accepted all four corrections in 0a11b427 with no new source blocker in this
slice. End-of-goal native command/QC qualification remains separate evidence.
