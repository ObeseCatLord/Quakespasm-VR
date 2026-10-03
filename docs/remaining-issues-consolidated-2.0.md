# Consolidated remaining issues — reviewed final work list

## Completion scope update — user instruction, 2026-10-02

The user now excludes things this agent cannot do or test from the goal.
This overrides unavailable-test requirements in the historical rows below.
Keep implemented features and the existing source audit; do not count an
unavailable check as a pass or invent a replacement runtime/provider.

Unavailable hardware-GPU rendering, simulated-XR execution, protected foveation output,
physical headset/controller/tracker/gaze/provider and listening/performance
checks no longer gate completion. Current Nvidia allocation fails with
NV_ERR_RESET_REQUIRED; CPU Monado also refuses compositor creation. No driver
reset or system graphics change is required. Retained failed and earlier
successful runs remain evidence at their stated limits.

CPU-native checks that can run here, available Windows/Linux/Linux ARM builds
and package/source reconciliation, demonstrated software-defect repairs and
the final local Astra review remain required. If an unavailable resource affects
only part of a row, exclude only that part. A failed runnable check is still a
failure to diagnose; it is not made unavailable by this instruction. Previously
successful CPU-rendered desktop execution remains eligible independently of
the unavailable OpenXR compositor.


2026-10-02, source snapshot414d58ec. User work order: identify every remaining
issue together, resolve the implementation issues, then run final verification.
No further per-boundary fixture/capture/test cycle. Only2.0 is writable; main,
reference checkouts, game assets and user-owned migration-2.0.md stay untouched.
The [frozen F01–F10 scope](final-goal-checklist-2.0.md) remains authoritative.
This reconciles progress into current actions, not new feature authorization.

## Current attainable work

The [final attainable-scope review and shared execution plan](attainable-final-integration-2.0-review.md)
governs remaining runnable checks. Historical rows below retain their original
evidence; unavailable parts no longer gate completion.

- Accepted current: D03/D04 repairs/regressions; native discovery/admission,
  connected public15/RCON/gameplay, real lost-ACK recovery, living v6 restore,
  RCON map/reconnect/quit and both expected error exits; CPU-rendered fork/public
  desktop gameplay/quit; id1/Hipnotic/Rogue transitions, built-in desktop
  record/play/pause/seek, postcfg/native saved configuration and inspected menus;
  metadata multi-packet/mid-signon/retire/reuse/reset and loaded AUTOCVAR admission;
  initialized native input lifecycle, neutral-rearm and command/QC gameplay;
  native calibration file save/reload/restart and authored q30 AD defaults;
  actual negotiated UDP voice/SDL callback/map-reconnect reset; native desktop
  cache valid/corrupt/repaired restart and >16384classic particle quad draws.
- V06 observed failure: reverse reconnect restores inventory correctly but a
  saved typed QC reference aliases the wrong player after slot reuse. Same local
  Astra approved the narrow native typed-reference/lifetime adapter; Luna is
  implementing it under the [reviewed plan](coop-save-reference-final-2.0-review-brief.md). This remains required.
- Accepted CSQC create/update/split-ACK recovery/re-enable/lost-removal/ID reuse
  at captured transport; actual typed cursor/weapon producer remains unproved.
- Pending native consumers: authored Shub lifecycle, remaining calibration/
  physics/paired-melee distinctions and actual music EOF tail.
- Pending attainable desktop residuals: paths/catalogue/cleanup, remaining CPU
  asset/worker consumers and desktop fallback/diagnostics.
  Listen-server RCON error recovery now passes through the available CPU route.
- Shipping079f4431: Linux x86-64, native Linux ARM and Windows Debug/Release
  build/package/source/inventory freshness accepted. The clean Windows Release
  build passed with /O2, /GL and LTCG; main verified all16 staged files including
  all13 x64 PEs. Earlier compiler/linker failures remain historical evidence.
  The pending V06 production fix will require affected engines to refresh.
  Final same-Astra integration signoff remains after runnable checks settle.

D01 retains bounded client-prediction evidence and the separate incompatible
short-jump-oracle failure; current server displacement does not replace client
prediction proof. D02's ordinary public desktop terminal check now passes0;
unavailable mixed-XR reproduction is excluded, historical attribution unknown.
Neither row establishes an unresolved production defect.

## Evidence and classification

[Verified: current CSV read] migration-feature-map.csv and original senior
worksheet each have185unique matching IDs. Original C01–C22/Q01 are source-
integrated; latest prior complete scope review establishes no additional source
omission. That is reused audited-source evidence, not a new full code proof.
Current local senior review must challenge it at load-bearing seams before this
list is frozen. That review is now complete with the two confirmed source paths
below and the explicit existing-scope coverage clarifications. An unverified behavior is not a confirmed code defect.

Types: **D** observed unresolved failure requiring diagnosis/disposition; **V**
remaining software acceptance, no demonstrated production defect; **A** final
integration/source-artifact reconciliation. Any new defect found in the audit or
final test attaches to its existing frozen owner and gets a bounded repair; no
silent product-scope expansion. Each row has one completion boundary rather than
an exhaustive mods/settings/builtin/device matrix.

## Retained historical failure evidence

| Item | Type / frozen owner | Issue and required resolution |
| --- | --- | --- |
| D01 | D / F01 | Existing connected prediction/movement probes intermittently see maximum between-send displacement0.119 below0.25, or original settled-distance failure, despite successful replay/ACK/fire and subsequent unchanged passes. Retain as an oracle-evidence gap: between-send observations lack elapsed-time/velocity/unobstructed-movement premises; settled endpoints do not measure travelled distance. Preserve historical thresholds/results. During final qualification record accepted input duration, timing, positions, velocity and obstruction with an ordinary collision-free movement opportunity; diagnose current failure if present without assuming broken prediction. Evidence: connected-crossplay-current-2.0-results.md and connected-lifecycle-current-2.0-results.md. |
| D02 | D / F01,F10 | Newest unchanged public-vkQuake peer exits with corrupted double-linked list/SIGABRT at quit after the crossplay/map/slot phases. Earlier public run exits normally. Root cause and whether current fork shares it are unestablished. Do not attribute to fork/driver/renderer or demand unconditional repair of the read-only baseline. Current fork clean exits remain valid bounded evidence. Record each process outcome during final mixed-peer qualification; a recurring abort requires a contemporaneous stack/ownership diagnosis. Historical raw logs are unavailable after targeted relocation lookup; do not require recreating them or claim the failed aggregate a pass. |


## Confirmed production issues — repaired and qualified

| Item | Type / frozen owner | Verified path and smallest repair |
| --- | --- | --- |
| D03 | D / F01,F03, NET-015 | SV_ReadQCRequest converts a bounds-checked numbered entity with Debug EDICT_TO_PROG, which Host_Errors on a legitimately freed slot before handler lookup. Main verifies sv_user.c:1984, pr_edict.c:2426, host.c:304 and plain-offset pinned MAIN/QSS-M references. Reuse the existing saved-reference byte-offset adapter; do not revive the slot or weaken general Debug checks. |
| D04 | D / F01,F03 | QC string-command self-drop causes reader false and a second SV_DropClient; immediate spawnclient may let trailing old message commands reach the replacement. Main verifies sv_user.c:2084/2183, pr_ext.c:2491 and host.c:593–647. Reuse typed-request requester/socket retirement guard for both QC hook and normal command dispatch. No new lifetime manager; inherited donor bug, still within callback/slot contract. Also reject unterminated EOF string prefixes immediately after MSG_ReadString before any capability/QC/native command effects. Main and the same Astra reviewer confirm this adjacent admission defect; terminated overlong-string policy remains unchanged. |

## Remaining software acceptance — source behavior exists

Already accepted subcases are excluded from the rows below. These are existing
completion obligations, not instructions to invent a second implementation or
fix code without evidence.

| Item | Owner | Remaining action / completion boundary |
| --- | --- | --- |
| V01 | F01 | Bounded actual transport loss/reorder/split snapshot and ACK/owner recovery; advancing native socket sequences and focused IPv6 cleanup/admission for native/public/private peers; NET-020 LAN/master/status/RCON/challenge/discovery control and truthful dialect advertising with controlled peers. Preserve accepted connected signon/gameplay/map/slot proofs. |
| V02 | F01,C02 | Metadata slots spanning reliable sends, mid-signon mutation, empty-full/update-only init, downgrade/fastload, QC-intercepted slot occupation, retirement/reuse and complete logical envelopes. Complete publication or explicit refusal; preserve six accepted native metadata profiles. |
| V03 | F01,C02 | Live cvar/name/color/setinfo/seta refusal preserves store/default/VM/output/archive flags; admitted controls retry without partial commands or stale state. Source publication/admission integration is complete; final native producer/parser/lifecycle evidence remains. |
| V04 | F02 | Distinct stock-liquid, pusher/customphysics handoffs and command-versus-world-QC authority; pause/load/teleport/frozen reset; removed Toss support and successful elevator relink. Reuse current movement/physics owners and donor behaviors. |
| V05 | F02 | Classic/default co-op collision/friendly-fire/telefrag policies, shared keys/weapons without copied ammo, exact-once targets, respawn-near/cooldown and established QBJ3 lifecycle revisions. Existing9stock/5cooperative load cases and7loaded QC profiles stay credited. |
| V06 | F02 | Save v5/inheritedv6/v7/KEX6 lifecycle beyond accepted malformed headers: living/dead/pending clients, reverse-order restore, referenced free-edict reuse, hubs and autosave rotation/failed replacement. Native identities/callbacks retire without duplicate ownership. |
| V07 | F03 | Remaining loaded-QC error unwind/simultaneous VM retirement and graphical authored HUD/localization consumers. Numeric/name/core permissions, buffers/files/search/calls/message/token/reflection/entity/surface/commands/changed-program ordinary reload already have bounded current acceptance; no512builtin framework. |
| V08 | F03,F02 | Authored inherited entity/round behavior including Shub same-map restart/save-load latches and actual CSQC create/update/remove, lost-removal/update recovery and re-enable resend (NET-009), plus typed requests/cursor/weapon arguments/following-command alignment (NET-015), still unproved by dedicated generated programs. Document intentional native/unsupported differences rather than adding mod-specific machinery. |
| V09 | F04 | Initialized input→native command→QC profile/handedness/focus/context/release/neutral-rearm checks; aim/recenter/turn/roomscale and pointer/wheel draw-hit-release; explicit hand/toggle haptics (VR-010), calibrated crosshair depth (VR-011), obstruction handling and mod bindings/suppression. Existing eight actual6DoF phases stay accepted. |
| V10 | F04 | Tracker identity/staleness, calibration accept/cancel/save/restart/reconnect, high ownership/ammo/schema consumers and AD preset reload, including AD-based mods. One shared solo/MP calibration and actual projectile placement. Existing offsets stay useful; no separate MP offsets. |
| V11 | F04 | Paired ranged identities and recognized immersive-melee trigger suppression, swing ordinary-QC attack and held ready pose without gesture attack animation. Existing inherited/reference behavior remains the authority; contact melee and Mjolnir dual-state behavior are excluded. |
| V12 | F05 | Targeted HUD/menu/console/wheel/intermission/field-panel output at inherited placement, names/outlines, mirror/mask/precision/lightmaps. World/PVS/moving-brush visibility (XR-010/PERF-013), once-per-frame effects, supported shadow/caster/receiver consistency (PERF-F003) and truthful available/unavailable/shared diagnostics (PERF-F001) remain explicit. Actual packaged desktop/two-eye/AO/MSAA/6DoF and native opposite-water composition/either-eye static alias boundaries remain accepted. |
| V13 | F05 | Independent tracked-player render and ordinary desktop/dead/corpse fallback; QBJ3 optional equipment present/missing with body/prop/muzzle/shadow agreement. Reuse published immutable poses/model-owned resources. General VRM importer/CPU retargeting rewrite is not required. |
| V14 | F05 | Loaded desktop↔XR transition, focus/reference reset/map retirement and valid/corrupt/restart pipeline-cache behavior with clean output/lifetimes. Current session/backend component evidence is bounded; do not substitute mocks for whole loaded rendering. |
| V15 | F06 | Actual available KHR protected output while eligible world shading coarsens. Existing backend policy/capability/selection/setter/off-recovery/view/pass/framebuffer components and actual fixed/menu/unavailable/off transitions are accepted. Patterned opaque capture/checker is prepared, source-reviewed and compiles/links; actual GPU output still requires aligned fragment sensitivity, actual tile/layer mapping and consumed opaque pipeline observation. Never implicit fixed fallback. |
| V16 | F07 | Bounded native id1/Hipnotic/Rogue desktop play/transitions, built-in desktop demo record/play/pause/seek and config/postcfg/controls. Preserve vkQuake desktop behavior; no full-campaign or VR-demo gate. |
| V17 | F07 | Unicode/path precedence, installed-mod/filter/catalogue install/cancel/failure and missing-model reconnect cleanup. Source code/pinned donor adaptation exists; actual complete cleanup/refusal at final software boundaries remains. |
| V18 | F07 | Actual image/model/fullbright/WAD3/lightmapped-liquid/style CPU-GPU and late-precache/whitespace consumers, malformed BSP bounds; texture/skin worker-versus-serial equivalence (PERF-003/004); retained worker/serial mfxsp17 and large-map extents acceptance stays credited. |
| V19 | F07,F05 | Repeated jumbo-map replacement and targeted stereo output including mj4m1, without heapsize workaround; >16384classic quads use their own vertices. Existing initial named-map rendering and exact worker/serial extents are accepted. No performance measurement gate. |
| V20 | F08,F01 | Remaining real negotiated voice delivery/receiver/map reset, private movement framing/fresh reconnect and active-XR default-system dummy mic profile with saved opt-out/desktop opt-in. Existing13native audio components and Opus/relay/jitter/loss/budget/generation/PTT/VAD/retry cases stay accepted. No physical mic recording or human listening. |
| V21 | F08 | Full host/device callback integration and retained music EOF-tail/timing limitations at the native audio owner. Current HRTF/native fallback, loop pause/cursor/reduced room send, wet-only monitor/room-worker/music formats/reset and device-fault component evidence remains credited. User physical audio/performance tests are excluded. |
| A01 | F10 | After implementation fixes settle, reconcile all final shipping inputs against Windows Release/Debug, Linux x86-64 and native Linux ARM artifacts, refreshing only affected production/platform inputs. Production0f277 shipping cohort builds/packages/source hashes match; the58fb discovery repair requires the affected engine refresh now underway. |
| A02 | F10 | Run combined final software verification for this list after fixes, credit prior unaffected acceptance; final local Astra integration signoff and exact outcomes/limits. Do not claim overall completion from builds/helpers or downgrade unavailable software evidence to a pass. |

## Completed or outside the goal

F09: five semantic package negatives complete. Current Windows Release/Debug,
Linux x86-64 and native ARM builds/packages/freshness accepted at0bd4 shipping
source; the refreshed0f277 cohort also matches inventories. The58fb production repair requires affected engines to refresh. Storage remains available. Current native Vulkan allocation is blocked by
NV_ERR_RESET_REQUIRED; CPU Monado also refuses compositor device creation. Upstream vkQuake merge rehearsal/conflict-owner
maintainability work accepted as attempted rehearsal; integrating36new upstream
commits is outside this migration goal. Native architecture and graphics remain.

Excluded: skyrooms, quad views, Gorilla/instant stop/hand-swim propulsion, contact
melee/parry/hybrids, revival, Mjolnir dual-state additions, imagedump, VR/extra
demos, legacy settings/MP aliases and incompatible-device-loss reconstruction.
AV-009/general VRM importer and eleven useful-addition candidates remain research.
Physical Beyond2e/Monado and Steam Frame headset/gaze/FB-META/provider/listening/
multiplayer/performance outcomes remain user-side work. Both native and streamed
Frame release targets and optional eye tracking remain the implemented contract.

## Execution status after the frozen enumeration

D03/D04 production repairs are reviewed and committed at0f277d1f. All12loaded
native QC/parser regression cases pass with Debug assertions; the same12pass
with an optimized sv_user object and Debug support graph (not full Release).
The [grouped results](final-grouped-software-current-2.0-results.md) record22native
physics/metadata/save cases, six UBSan components and13native audio markers.

D01 now has bounded CPU-rendered between-send prediction evidence at original
thresholds; that aggregate still fails a command-rate-incompatible short jump
oracle. D02 remains open: the public desktop gameplay probe passes but Monado
cannot start XR, so mixed-peer lifecycle and normal terminal evidence do not
complete. Controlled NVIDIA retries still fail device allocation; owned test
runtimes are stopped without GPU resets, driver reloads or system changes.

A01 refreshed Linux and Windows Release/Debug builds succeed from production
0f277d1f; native ARM refresh and final source/package reconciliation complete with matching inventories.
Protected-output inputs/capture/checker are reviewed and compile/link against
current native renderer; no GPU execution pass. Additional V03 producer refusal/retry and V06 autosave rotation/open-failure/retry
boundaries pass; their broader stated obligations remain. A new verified V01/NET-020
startup registration gap and adjacent RCON/query admission defects are repaired
at58fb8864; ten actual native UDP cases pass under the
[reviewed narrow repair plan](discovery-startup-final-2.0-plan.md). All V rows and A02
retain their stated completion boundaries. Tests/docs-only commits need no engine rebuild. Shipping engines are refreshing
for production079f4431, including the reviewed borrowed-SSQC/driver/error cleanup
[RCON repair](rcon-qc-context-final-2.0-plan.md). Current actual native connected
public15 gameplay/RCON/discovery, deliberately lost-ACK recovery and authored v6
living-player restore/movement pass with ordinary quit0; exact limits are in the
[grouped results](final-grouped-software-current-2.0-results.md). Successful RCON
map/reconnect and expected failed-map/changelevel exits remain in progress.
The latest controlled Nvidia allocation reports reset required; owned GPU test
peers/runtime are stopped and unavailable execution no longer gates completion
under the user scope update above.

## Execution order

1. Complete one read-only senior reconciliation of this draft and actual risky
   code/reference seams. Freeze the complete list with defects distinguished from
   acceptance gaps and exact evidence. No new fixture/GPU tests during enumeration.
2. Draft bounded adapter/reference-reuse plans for confirmed production issues,
   then resolve all approved implementation issues on2.0. Luna/xhigh writes
   disjoint coding slices; main reviews/integrates. No speculative rewrite.
3. Once those implementations are finished, prepare/reuse the combined final
   checks and execute them. Findings remain under the existing owners; any actual
   new defect is repaired before the affected final rerun. User live tests stay
   excluded. Commit regular coherent changes and reconcile shipping artifacts.

Reviewed status: complete enumeration on2026-10-02. Four observed/confirmed
issue rows D01–D04,21verification rows V01–V21 and two integration rows A01/A02.
D01/D02 are diagnosis/qualification gaps; D03/D04 (including adjacent EOF-prefix
admission) are the confirmed implementation queue. Same local Astra/xhigh
review, all185IDs reconciled; [main dispositions](consolidated-issues-senior-2.0-review.md).
Before-code repair plan: [native request boundary](server-request-retirement-final-2.0-plan.md).
No production patch or final tests are counted as completed by enumeration.
This is not final F10 integration signoff.
