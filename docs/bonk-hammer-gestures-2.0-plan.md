# Bonk hammer gestures 2.0 plan

## Scope and reference

This is a Bonk-only extension for the exact installed Bonk program
`b54e33e50ad06d5628132a26bb6b089534d091bd2b6756799a15b61e7af49811`.
It reuses the existing weapon-contact protocol, validated contact owner,
stroke accumulator, world sweep, model splitter, and calibration schema.
No QC, assets, dispatcher, client damage, or client velocity logic is added.
The binding decision below adds only a flagged contact suffix.

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

## Binding full implementation decision, 2026-10-04

The main dispositions adopt the reviewed contact suffix. Preserve command
angles and every movement value. Add capability bit 4 and contact head-present
flag 8; append three finite normalized float32 head angles (12 bytes) after
existing per-hand data. Decode the flagged shape independently of weapon
authorization, then fail closed unless the current exact Bonk offer and head
data authorize gameplay. Other profiles retain their original bytes.

Capture post-turn `V_TrackedMovementAngles(FOLLOW_HEAD)` beside accepted contact
geometry, with its pending identity, generation, complete-command queue and
freshness. The command committing the first outcome supplies its head sample.
No separate pose stream, movement conversion or state machine is introduced.

Extend the existing direct-melee owner for Bonk: deliberate activation requires
at least .06 metres arc and .55 metres/second accumulated peak; medium uses
.12/.85 and high .25/1.5. Commit the tier at the first outcome. Physical hits
call native five-argument `hithammer`; settled misses call `W_SwingHammer` with
only its five audited acquisition sites forced to miss. Native `saf(.4)` owns
cadence. Restore temporary QC state while retaining damage, velocity, flags and
cooldown. Keep global physical admission false and add only a negotiated exact
Bonk predicate, including final post-CSQC attack suppression.

Implementation estimate: one narrow codec extension, client recipe admission
and sampling, and a Bonk branch inside the existing outcome/stroke owner.
Stop and reopen the design if this needs another queue/dispatcher, duplicated
stroke state machine, movement changes, or broader physics policy.

The implementation-end proof uses an isolated debug graph and installed
original QC: encoded/decoded queued contact reaches the production sweep and
native outcomes; all tiers, floor/wall/air/whiff/cooldown/tier lock, movement
preservation, malformed/missing/revoked/stale data and 29 recipe geometries.
No proprietary program or meshes are committed.

## Full source implementation complete, qualification pending

Private integration base is upstream merge `59c4df5c1fc0268c51ed7d56b34bc7f6e67bcbcf`,
with the mechanical stage reapplied as `e9907675`. The bounded codec, exact
client admission, post-CSQC suppression, native outcome branch and tier lock
are implemented. Contact equality conditionally compares the carried head
angles, including the accepted-command and queued-command trigger paths.
No new stream, dispatcher, movement conversion or stroke owner was needed.

The isolated assertion-enabled SDL2 debug graph compiles and links with
`-Werror`; GCC 16's pre-existing mkpak format-truncation warning is disabled
only in the private graph. Native loaded-VM and codec/geometry/input fixtures
follow in a separate owned test commit. Compilation is not native qualification.

## Verification after implementation

Add focused `Bonk*` or `run_bonk*` coverage that loads the exact VM and proves
decoded contact reaches the existing swept-contact owner. Exercise admission
and rejection for program/model/skin, ready model selection, trigger
suppression, three tier selection and first-hit lock, floor versus wall
classification, grounded whiff, cooldown, stale/discontinuous reset, and no
publication for other physical families. The final user-facing proof must also
cover head-versus-weapon divergence in every movement mode once the adapter is
approved.
