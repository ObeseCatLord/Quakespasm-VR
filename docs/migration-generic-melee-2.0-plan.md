# Generic melee fallback

Status: architecture brief for Astra Max; implementation not yet authorized by
this plan's decision record. User has requested built-in melee for uncovered
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
