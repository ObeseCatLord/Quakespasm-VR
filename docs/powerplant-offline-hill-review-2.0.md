# Powerplant offline uphill: verified senior-review brief

Goal: fix sluggish uphill movement both offline desktop and networked without replacing native vkQuake walking, per-mod physics or hiding errors with smoothing. Solo operator; no enterprise ceremony. Earlier support patch d8a27162 is committed but NOT deployed. Main owns physics/protocol, a non-overlapping Sol6.1 agent owns Peril VR weapon calibration and akimbo. Review read-only.

## Environment facts

| Fact | Evidence / location |
|---|---|
| Checkout | the 2.0 engine checkout, branch2.0 |
| Assets | Straight/peril3.0/maps/powerplant.bsp, actual installed progs |
| Existing change | Quake/pmove.c PM_CategorizePosition only protects rising QC takeoff after QC clears support; Quake/sv_phys.c private WALK seed retains post-QC support only |
| Diagnostic root | the external hotfix diagnostic root |
| Actual geometry comparison | native-slope.c, imported-slope.c, build-native-slope.py, native-comparison.txt (old compiled PMove) and native-comparison-current.txt (current source rebuilt PMove) |
| Debug live local attempt | offline-peril/game.log, peril-errors.log; renderer initialized normally, no GPU reset |
| Linux portable + ARM | Two changed movement objects built successfully, deployment held pending this review |

## Verified evidence, separate from inference

- [verified: sv_main.c SV_PrivateWalkTrialSelectAtBegin / admission; host_cmd.c Host_Begin_f] Local offline sessions are NOT excluded from selection. My earlier hypothesis of an exclusively native SP path was incorrect. A local session still negotiates and selects private movement when eligible. [unknown until unblocked live signon] exact final selected state for this Powerplant isolated local launch.
- [verified: real BSP traces and actual native QC diagnostic] Powerplant has a dry walkable world-hull corridor; slope starts roughly (14683,9058,1389). Comparing eight identical 320-forward PMove commands at 4/8/16ms on real BSP: old compiled baseline reports uphill ground=0, horizontal speed29.02/28.04/26.33; same current-source PMove reports ground=1, speed91.58/179.26/320. Flat/downhill speeds remain comparable and grounded. These are prepared solver commands, NOT end-to-end live player proof. See diagnostic root above. No synthetic planes or fabricated BSP.
- [verified: diagnostic calls actual SV_ClientThink + SV_Physics_ClientNativeFrame, actual QC] Native no-prediction walking climbs the same slope and retains ground; at eight 8ms samples horizontal speed148.97 (analytic) vs178.71 flat, expected old gravity/clip behavior. No evidence supports rewriting native walking.
- [verified: Honey actual QC/snapshots existing fixture] d8a27162 corrected 8/8 uphill same-completed-sequence mismatches to0/8, retained flat/downhill, accepts moving uphill jump and same-slope landing/rejump, swim positive impulses. Generic jump trajectory still approximate and separately reported.
- [verified: private Powerplant fixture and local desktop debug launch fail] Actual snapshot writer hits MSG_WriteShort range error. GDB stack: MSGFTE_WriteEntityUpdate -> MSG_WriteSize16 -> MSG_WriteShort. Existing MSG_SolidSizeHasExtraBits tests only divisibility, not 16-bit compact dimensional range. Oversized map brush bounds can be multiples of8 yet exceed compact upper-height max472, producing bits beyond16. Release disables range check and truncates; not proven cause of sluggishness, but real malformed bounds and blocks debug signon/regression.
- [verified: format source] MSG_WriteSize16 stores x and lower extent in5bits at8unit granularity and upper extent+32 in6bits. Private format already has lossless32-bit solidsize variant and supports it in cl_parse.c. Legacy public remains compact16-bit and should saturate to representable extents, not wrap or overflow. No new protocol or client feature needed.

## Decisions / current lean

1. Treat d8a27162 as fundamental common uphill fix, pending actual local SP test: retain architecture; no new native SV_WalkMove physics changes. Alternative rewrite/native-to-PMove unification rejected because reusable shared solver already demonstrates support fix and native walking is not broken on this corridor.
2. Fix the narrow wire compatibility boundary to unblock complete real-BSP testing: private bounds use existing32-bit variant when compact dimensions out of range; compact writer saturates x/lower to0..31 and upper to0..63 before unsigned packing. Keep ordinary bytes identical. Public compact format inherently approximate; no widening offered to old clients. Reviewer should verify exact ranges and whether this belongs in fix vs diagnostic-only workaround (lean production fix: prevents corrupted collision state).
3. Repeat real offline desktop SP Powerplant and network Honey after implementation, plus existing q30/AD, Alkaline and QBJ3 command coverage with native mod bootstrap if kill/respawn fixture assumptions fail. Verify actual owner classification, grounded uphill, accepted jump/no instant floor snap, actual movement. No claim of physical VR based on prepared VR commands.

Possible overlap: the offline report may be exactly the shared support bug, not a second native bug. Delete unjustified native modifications. Do not expand into movement timing/smoothing/protocol redesign or unrelated Peril gameplay.

Depth budget: verify these claims, rank recommendations; max1000 words. Review real source and compact format; critique whether additional native changes necessary and whether >180 ground cutoff needs a demonstrated steep-slope case. Return prioritized recommendations and minimal verification, no writes/nested agents, no external communication. Human preference already clear; no approval questions unless genuine missing user choice.

## Astra disposition

Effective Astra6 / xhigh confirmed from bounded turn metadata. Reviewed source-backed claims; main checked compact decoder, selection-at-begin, actual Powerplant program classification and test ownership.

| Recommendation | Disposition |
|---|---|
| Keep shared support correction; avoid native walk rewrite | Adopted; no additional native walking edits |
| Private packed hull overflow uses existing32-bit form | Adopted; aligned but oversized upper extents now choose tag32 |
| Public compact bounds saturate upper extent | Adopted; lateral/lower extracted bytes already fit their five-bit values |
| Distinguish selection from current q30/native owner | Adopted; local traversal checks record both |
| Running uphill landing/rejump may expose positive-Z support gap | Adopted as meaningful regression; patch only if reproduced |
| Retain inherited180 cutoff without a failed steep traversal | Adopted; current real-BSP steeper corridor still reaches320 horizontal without failure |

Correction: Powerplant overflow comes from BBOX/SLIDEBOX bounds, not true BSP sentinel. Lossless32-bit transport preserves the existing packed solidsize; it does not invent higher precision than that representation.

## Running landing reproduction and second Astra disposition

The required running case reproduced the predicted gap: real Honey QC/BSP, continuousforward160, frame30 floor collision creates positive Z20.469 but server ground0; successive commands stay airborne and slow toward30, rejecting a second jump. This is separate from actual takeoff. Fresh explicitly selected Astra6/xhigh verified the minimal correction: PM_AirMove authorizes the existing final support probe for QC owners after BLOCKED_FLOOR, preserving pground=0 and final trace authority. No extra floor-hit state is required for the start/allsolid return3 because that path clears velocity and the final trace rejects solid-start support. No native walking or protocol rewrite.

| Follow-up recommendation | Disposition |
|---|---|
| QC owners use existing collision-to-support handoff | Adopted, one condition plus explanation; no new policy/cvar/state |
| Final trace owns ground entity and edge/solid rejection | Adopted; solver regression covers world and nonworld slope support and removed support |
| Retain180, actual takeoff, shallow-water safety | Adopted; existing low-takeoff/release/water checks retained |
| Keep generic jump timing separate from no-jump parity | Adopted; unchanged honest fixture reporting |

After implementation, Honey real-QC/BSP matrix passes at4/8/16ms, including both rest and continuously moving uphill landing/rejump: two accepted QC jumps, true same-slope landing, second airborne launch and release. Exact generic QC jump trajectories remain separately reported; these captured transport tests are not physical headset evidence.

The actual isolated local desktop Powerplant run reaches signon, records selection=true/currentnative=true (weapon2 invokes the existing conservative q30 native fallback), reaches~315units/sec grounded uphill and then rises on jump. This proves native preservation, not q30 shared solver dispatch. The identifier q30 is misleading here: its existing exactSHA also matches installed Perilpak2. Asset priority was independently checked; no assets/configs changed.

## Final integrated qualification

The later actual desktop local Powerplant run switches through native impulse1
to weapon4096, then records selection=true/currentnative=false throughout the
uphill run, jump and landing. Horizontal speed reaches320; the player climbs
from Z1389 to1492 and finishes grounded with positive uphill Z velocity88.34.
This proves the selected shared solver in an initialized offline client/server
session, separately from the earlier weapon2 native fallback. Evidence:
`offline-peril/hill-result7.json` and `hill-game7.log` in the diagnostic root.
No installed game assets/configuration or GPU services were modified.

The combined Debug engine passes Peril native paired firing and the actual
solid-bound writer/decoder regression, including compact limits and private
extended dimensions. Movement fixes are engine-wide support-state corrections;
there are no new mod-specific slope rules.

Additional requested-mod qualification uses the stable combined movement
graph: q30a1024 and AD pass the existing actual-QC real-floor/jump/landing/
release/rejump fixture, with maximum selected/native displacement0.659554;
Alkaline alk_caustic and QBJ3 start pass dry flat/uphill/downhill parity and
both standing/running uphill landing/rejump, each with two authored launches
and zero jump-contract errors. QBJ3's scratch fixture retains a living spawned
client instead of assuming an immediate respawn after kill; production is
unchanged. The generic AD bootstrap is unsupported, so AD uses its exact
existing q30 fixture. Evidence is under diagnostic `mod-final/report.txt`.

These are captured-delivery/component tests, not physical input or UDP timing.
AD/q30 stairs and uphill geometry are not independently covered by those
fixtures; Powerplant covers the same selected q30 policy on actual slopes.
Generic-QC airborne trajectory differences remain separately reported; the
patch corrects support and jump acceptance rather than claiming a new exact
predictor. Actual final integrated Honey UDP also passes client movement,
firing, between-send replay and zero settled owner error. Prepared VR command
uphill landing/rejump and actual-water positive-QC swim impulses pass.
