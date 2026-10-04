# Bonk hammer gestures 2.0 plan

## Scope and reference

This is a Bonk-only extension for the exact installed Bonk program
`b54e33e50ad06d5628132a26bb6b089534d091bd2b6756799a15b61e7af49811`.
It reuses the existing weapon-contact protocol, validated contact owner,
stroke accumulator, world sweep, model splitter, and calibration schema.
No QC, assets, protocol fields, dispatcher, client damage, or client velocity
logic is added.

The behavioral reference is the original `w_hammer.qc`: charge multipliers
`.2`, `2`, and `4`, a `.4` hammer cooldown, floor-dependent hop, and airborne
whiff dash. `vr.c` rows 7139--7225 supply the 29 audited held-model recipes.

## Completed mechanical stage, 2026-10-04

Baseline: `7cc8cdb20db55f550addf1b11b71ec4bbd93b8c6`, branch `2.0`.
All 29 donor held recipes now live in the existing `gl_model.c` table, with
source topology from the existing splitter and unchanged generated size/CRC,
ready frame, endpoints, handedness, and controller-roll defaults.

The source models have 29 held-only calibration defaults. The donor initializer
and installed `vr_weapons.txt` have no authored entries for these hammer paths;
the defaults retain neutral offset `(0,0,0)` and scale `1`, matching the donor's
source-slot initialization. File/user schema fields retain precedence. These
entries do not enable melee gesture input. No new grip/endpoints are invented.
Rendering/contact identity against all 29 installed assets still requires the
final implementation's geometry checks.

Dormant server helpers recognize the original 684974-byte SHA-pinned VM,
audited function ABI, `hammer_skin` selector, living player, weapon/items,
custom flags, exact selected model, and native `attack_finished_hammer` field
at offset 127. The ready helper accepts only the donor's harmless idle/recovery
thinks after its cooldown; it does not alter those thinks. Recognition remains
disconnected from negotiation and the direct-melee owner.

Checks completed: all 29 rows compared mechanically against donor
identities/size/CRC/frames/endpoints; matching calibration roster and endpoint
bounds; `git diff --check`; debug compilation of `sv_phys.o`, `gl_model.o`, and
`vr_weapon_calibration.o` with SDL2 and `-Werror`. The private original VM's
SHA, size, function signatures and field offsets were read directly.
No behavioral tests were added or run. No native attacks are suppressed and
no Bonk contacts are offered/published. The full adapter is unfinished.

## Mechanical implementation in this change

1. Pin the exact Bonk VM identity and the `W_SwingHammer`, `hithammer`, and
   `saf` ABI entry points before recognizing Bonk on the server.
2. Add all 29 audited Bonk held recipes to the existing model selector,
   retaining each source model, skin selector, generated-mesh size/CRC, ready
   frame, and endpoints.
3. Admit only that selected, calibrated Bonk recipe on the client. Keep
   `VR_InputPhysicalMeleeAllowed()` globally false; add a narrower Bonk
   publication predicate that requires negotiated `BONK` plus the exact
   selected recipe. Other physical families remain disabled.
4. Suppress synthetic `BUTTON_ATTACK` only while an actual authorized Bonk
   gesture owns the immersive trigger. Hold the selected Bonk viewmodel at its
   declared ready frame. Desktop input and unrecognized models retain native
   behavior.
5. Publish the existing contact record and offer the Bonk profile from the
   server only when the exact VM, weapon/model/skin selection, and living
   player gates succeed. Retain the existing server stroke owner and its
   reset, stale, discontinuity, freeze, death, and map-transition guards.
6. Use the existing Bonk tier thresholds (`.12/.85` for 2 and `.25/1.5` for
   4), with a deliberate Bonk activation floor of `.06` arc and calibrated
   speed. Commit the tier at the first terminal outcome.

## Pending Astra decision; do not silently choose here

The Bonk airborne-whiff direction must use head facing. `cmd.viewangles`
currently carries selected movement orientation in some locomotion modes, so
this pass must not reinterpret it as head orientation or alter movement
conversion. The final adapter will be selected after review from an existing,
timestamp-compatible, validated source or the feature will remain gated.

The final QC-call branch also remains pending that decision. It must call the
native five-argument Bonk leaf and native cooldown path while saving/restoring
only temporary QC globals and temporary trace basis. Damage and velocity from
the accepted QC call must remain committed. It must not add fan-range damage,
intercept the QC floor trace, or add new physical-melee families.

## Verification after implementation

Add focused `Bonk*` or `run_bonk*` coverage that loads the exact VM and proves
decoded contact reaches the existing swept-contact owner. Exercise admission
and rejection for program/model/skin, ready model selection, trigger
suppression, three tier selection and first-hit lock, floor versus wall
classification, grounded whiff, cooldown, stale/discontinuous reset, and no
publication for other physical families. The final user-facing proof must also
cover head-versus-weapon divergence in every movement mode once the adapter is
approved.
