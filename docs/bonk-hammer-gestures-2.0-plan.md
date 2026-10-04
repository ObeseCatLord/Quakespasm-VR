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

## Prior mechanical stage, 2026-10-04 (historical status)

This section records the mechanical commit before full authorization. Its
dormant/unfinished descriptions apply only to that prior stage.
Baseline: `7cc8cdb20db55f550addf1b11b71ec4bbd93b8c6`, branch `2.0`.
The mechanical stage placed all 29 donor held recipes in the existing `gl_model.c` table, with
source topology from the existing splitter and unchanged generated size/CRC,
ready frame, endpoints, handedness, and controller-roll defaults.

The source models received 29 held-only calibration defaults. The donor initializer
and installed `vr_weapons.txt` have no authored entries for these hammer paths;
the defaults retain neutral offset `(0,0,0)` and scale `1`, matching the donor's
source-slot initialization. File/user schema fields retain precedence. These
entries did not yet enable melee gesture input. No new grip/endpoints were
invented. Rendering/contact identity was deferred to the final checks recorded
below, which now cover all 29 installed assets.

At that stage, dormant server helpers recognized the original 684974-byte SHA-pinned VM,
audited function ABI, `hammer_skin` selector, living player, weapon/items,
custom flags, exact selected model, and native `attack_finished_hammer` field
at offset 127. The ready helper accepts only the donor's harmless idle/recovery
thinks after its cooldown; it does not alter those thinks. Recognition was then
disconnected from negotiation and the direct-melee owner.

Checks completed: all 29 rows compared mechanically against donor
identities/size/CRC/frames/endpoints; matching calibration roster and endpoint
bounds; `git diff --check`; debug compilation of `sv_phys.o`, `gl_model.o`, and
`vr_weapon_calibration.o` with SDL2 and `-Werror`. The private original VM's
SHA, size, function signatures and field offsets were read directly.
At that prior stage, no behavioral tests had been added or run, no native
attacks were suppressed, and no Bonk contacts were offered/published. Full
adapter work followed the approved decision below; it is now implemented and
qualified as recorded in the current status section.

## Adapter implementation scope (approved)

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
   speed of .55 metres/second. Commit the tier at the first terminal outcome.

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

## Current status: approved adapter implemented and native qualification passed

Private source implementation is `997eb1d189514b2066170725add5d8146443395d`,
on upstream base `59c4df5c1fc0268c51ed7d56b34bc7f6e67bcbcf` plus mechanical
stage `e9907675` (equivalent to original `a47864a1`). Main integrated gameplay
as `643dc6a3` and owns the necessary outer contact-tail guard correction
`1f2c44ec7bb4a418cf78e1582d2cf1d9c8a0ddb5`. The accepted combined implementation
includes that correction; the original private gameplay commit requires it.
No new stream, dispatcher, movement conversion or stroke owner was needed.

Final acceptance ran against the production Clang assertion-enabled `-O0 -g
-Werror -D_DEBUG` graph `qsvr-upstream-bonk-final-debug-20261004`, rebuilt after
main's guard correction. The final full runner observed source head
`a5792be65c2bc2889279c7a3c5e0d7dc7ef63192` (fixture/documentation commits
following the graph's `e2b0f1c6` shader rebuild); executable SHA256 was
`7aa2c72137d5ed8a6f7788ff527008437f5c603ca2beacc9a1db1933dfe28e0a`.
Fixture objects use that graph's compiler settings and exact production sources
and headers, with no stale Make objects. The production graph/source are read
only; all fixture objects, symlinked installed PAKs, extracted model bytes and
logs remain under the private ignored `tests/Bonk/build/qualification` folder.

The complete runner is:

```sh
python3 tests/Bonk_verify.py --graph /path/to/qsvr-upstream-bonk-final-debug-20261004
```

Completed proof:

- `BONK_CODEC_RAW` and `BONK_CODEC_TRUSTED`: production codec, existing golden
  bytes, all flagged-prefix truncations and malformed head floats; three
  maximal commands use 924 and 852 bytes of the 1400-byte MTU respectively.
  Following Gorilla and command extensions retain exact framing. No estimator
  was introduced.
- `BONK_GEOMETRY_29_IDENTITIES_READY_EDGES_AND_MUTATION_PASSED`: original
  installed source bytes pass the production recipe/splitter identities and
  generated CRC/size checks; all 29 ready frames and cutting edges are valid.
  One-byte model mutations fail closed. Three unused historical archive aliases
  are excluded from the exact native/donor 29-model roster.
- `BONK_INPUT_HEAD_POSTTURN_THREE_MODES_GATES_TRIGGER_PRESERVATION_PASSED`:
  carried head mapping after turning, divergent selected movement angles,
  unchanged native movement values, negotiated/profile/geometry/pending-identity
  gates and trigger suppression helper; existing input/menu/movement regressions
  also pass. Global physical admission stays false.
- `BONK_RUNTIME_PASSED`: exact original installed VM and ABI; production
  encode/decode, `SV_QueuePrivateCommand`, queued contact owner and real server
  sweeps reach native QC. Three damage tiers, real world floor hops at all
  tiers, wall recoil without floor launch, grounded whiffs, head-facing airborne
  whiffs, `.4` native cadence, jitter rejection and immutable first-outcome tier
  across two victims pass. Native velocity, flags, aftermath and damage remain
  committed.
- `BONK_QUEUE_BASELINE_FIRST_HIT_AND_SETTLED_WHIFF_EXACTLY_ONCE_PASSED`:
  first-hit and grounded/airborne settled-whiff queue drains succeed, advance
  the accepted sample exactly once, and supply the next command's baseline.
  This catches the original outer Enyo/QBJ3-only guard omission.
- `BONK_EXPIRED_RECEIPT_AND_DUPLICATE_COMMAND_BASELINE_PASSED`: stale injection
  uses an actual queued receipt timestamp older than `.25` seconds, with no
  attack/velocity/cooldown outcome. Missing/malformed/revoked data fail closed;
  mutated accepted or queued head angles cannot borrow another command's head,
  and duplicate commands cannot overwrite the accepted baseline.
- `BONK_NATIVE_TRACE_BOUNDARY_AND_TEMPORARY_STATE_PASSED`: whiff roots observe
  exactly the five audited acquisition sites. Nested native floor/gameplay,
  neighboring statements, wrong-player and desktop-root traces are not masked.
  QC arguments/return, self/other/time, basis and trace globals are restored;
  live owned player angles are restored.
- `BONK_REAL_NATIVE_CALLBACK_OWNER_INVALIDATION_REJECTED_PASSED`: loss of owner
  at a real native leaf return rejects the command tail and preserves the
  previous baseline while retaining committed native damage. Cleanup restores
  VM temporaries without writing a retired/replaced client's edict pose.

## Proof limits and artifact boundaries

Server outcomes above execute original installed QC and real world sweeps.
Contact geometry is injected at the command boundary; input tests use typed XR
and presentation/calibration seams. This does not claim headset comfort, visual
alignment under live tracking, a live CSQC VM integration run, or a networked
multiplayer session. The production final post-CSQC filter was independently
spotchecked by main; its helper and movement preservation are covered here.
Hardware feel remains the user's final physical evaluation.

Desktop/root masking exclusions, unchanged legacy-profile codec bytes and the
existing input regressions pass. Other mod families are not newly physically
admitted, but this bounded fixture does not load every unrelated mod's QC.
Neutral Bonk calibration defaults match the donor initializer and preserve
file/user schema overrides.

No proprietary QC source, program image, PAK, source model or generated mesh is
committed. No production source or build graph was written by these fixtures.
