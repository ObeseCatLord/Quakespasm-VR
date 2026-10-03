# Consolidated remaining issues — audit draft

2026-10-02, source snapshot414d58ec. User work order: identify every remaining
issue together, resolve the implementation issues, then run final verification.
No further per-boundary fixture/capture/test cycle. Only2.0 is writable; main,
reference checkouts, game assets and user-owned migration-2.0.md stay untouched.
The [frozen F01–F10 scope](final-goal-checklist-2.0.md) remains authoritative.
This reconciles progress into current actions, not new feature authorization.

## Evidence and classification

[Verified: current CSV read] migration-feature-map.csv and original senior
worksheet each have185unique matching IDs. Original C01–C22/Q01 are source-
integrated; latest prior complete scope review establishes no additional source
omission. That is reused audited-source evidence, not a new full code proof.
Current local senior review must challenge it at load-bearing seams before this
list is frozen. An unverified behavior is not a confirmed code defect.

Types: **D** observed unresolved failure requiring diagnosis/disposition; **V**
remaining software acceptance, no demonstrated production defect; **A** final
integration/source-artifact reconciliation. Any new defect found in the audit or
final test attaches to its existing frozen owner and gets a bounded repair; no
silent product-scope expansion. Each row has one completion boundary rather than
an exhaustive mods/settings/builtin/device matrix.

## Current unresolved failures

| Item | Type / frozen owner | Issue and required resolution |
| --- | --- | --- |
| D01 | D / F01 | Existing connected prediction/movement probes intermittently see maximum between-send displacement0.119 below0.25, or original settled-distance failure, despite successful replay/ACK/fire and subsequent unchanged passes. Determine whether sampling/input/timing assumptions or actual movement/presentation are at fault; retain native command/QC/reference semantics and do not weaken an unexplained threshold. Evidence: connected-crossplay-current-2.0-results.md and connected-lifecycle-current-2.0-results.md. |
| D02 | D / F01,F10 | Newest unchanged public-vkQuake peer exits with corrupted double-linked list/SIGABRT at quit after the crossplay/map/slot phases. Earlier public run exits normally. Root cause and whether current fork shares it are unestablished. Diagnose from retained evidence/source; do not modify the read-only baseline or claim the failed aggregate a pass. The old documented /tmp qualification root is currently absent; trace relocated artifacts before drawing a memory-owner conclusion. |

## Remaining software acceptance — source behavior exists

Already accepted subcases are excluded from the rows below. These are existing
completion obligations, not instructions to invent a second implementation or
fix code without evidence.

| Item | Owner | Remaining action / completion boundary |
| --- | --- | --- |
| V01 | F01 | Bounded actual transport loss/reorder/split snapshot and ACK/owner recovery; advancing native socket sequences and focused IPv6 cleanup/admission for native/public/private peers. Preserve accepted connected signon/gameplay/map/slot proofs. |
| V02 | F01,C02 | Metadata slots spanning reliable sends, mid-signon mutation, empty-full/update-only init, downgrade/fastload, QC-intercepted slot occupation, retirement/reuse and complete logical envelopes. Complete publication or explicit refusal; preserve six accepted native metadata profiles. |
| V03 | F01,C02 | Live cvar/name/color/setinfo/seta refusal preserves store/default/VM/output/archive flags; admitted controls retry without partial commands or stale state. Source publication/admission integration is complete; final native producer/parser/lifecycle evidence remains. |
| V04 | F02 | Distinct stock-liquid, pusher/customphysics handoffs and command-versus-world-QC authority; pause/load/teleport/frozen reset; removed Toss support and successful elevator relink. Reuse current movement/physics owners and donor behaviors. |
| V05 | F02 | Classic/default co-op policies, shared keys/weapons without copied ammo, exact-once targets, respawn-near/cooldown and established QBJ3 lifecycle revisions. Existing9stock/5cooperative load cases and7loaded QC profiles stay credited. |
| V06 | F02 | Save v5/inheritedv6/v7/KEX6 lifecycle beyond accepted malformed headers: living/dead/pending clients, reverse-order restore, referenced free-edict reuse, hubs and autosave rotation/failed replacement. Native identities/callbacks retire without duplicate ownership. |
| V07 | F03 | Remaining loaded-QC error unwind/simultaneous VM retirement and graphical authored HUD/localization consumers. Numeric/name/core permissions, buffers/files/search/calls/message/token/reflection/entity/surface/commands/changed-program ordinary reload already have bounded current acceptance; no512builtin framework. |
| V08 | F03,F02 | Authored inherited entity/round behavior including Shub same-map restart/save-load latches and actual CSQC entity lifecycle/parser/application boundaries still unproved by dedicated generated programs. Document intentional native/unsupported differences rather than adding mod-specific machinery. |
| V09 | F04 | Initialized input→native command→QC profile/handedness/focus/context/release/neutral-rearm checks; aim/recenter/turn/roomscale and pointer/wheel draw-hit-release. Existing eight actual6DoF phases stay accepted. |
| V10 | F04 | Tracker identity/staleness, calibration accept/cancel/save/restart/reconnect, high ownership/ammo/schema consumers and AD preset reload, including AD-based mods. One shared solo/MP calibration and actual projectile placement. Existing offsets stay useful; no separate MP offsets. |
| V11 | F04 | Paired ranged identities and recognized immersive-melee trigger suppression, swing ordinary-QC attack and held ready pose without gesture attack animation. Existing inherited/reference behavior remains the authority; contact melee and Mjolnir dual-state behavior are excluded. |
| V12 | F05 | Targeted HUD/menu/console/wheel/intermission/field-panel output at inherited placement, names/outlines, mirror/mask/precision/lightmaps. Actual packaged desktop/two-eye/AO/MSAA/6DoF and native opposite-water composition/either-eye static alias boundaries remain accepted. |
| V13 | F05 | Independent tracked-player render and ordinary desktop/dead/corpse fallback; QBJ3 optional equipment present/missing with body/prop/muzzle/shadow agreement. Reuse published immutable poses/model-owned resources. General VRM importer/CPU retargeting rewrite is not required. |
| V14 | F05 | Loaded desktop↔XR transition, focus/reference reset/map retirement and valid/corrupt/restart pipeline-cache behavior with clean output/lifetimes. Current session/backend component evidence is bounded; do not substitute mocks for whole loaded rendering. |
| V15 | F06 | Actual available KHR protected output while eligible world shading coarsens. Existing backend policy/capability/selection/setter/off-recovery/view/pass/framebuffer components and actual fixed/menu/unavailable/off transitions are accepted. Pattern/opaque image-oracle design is unimplemented; local review requires aligned fragment sensitivity, actual tile/layer mapping and consumed opaque pipeline observation. Never implicit fixed fallback. |
| V16 | F07 | Bounded native id1/Hipnotic/Rogue desktop play/transitions, built-in desktop demo record/play/pause/seek and config/postcfg/controls. Preserve vkQuake desktop behavior; no full-campaign or VR-demo gate. |
| V17 | F07 | Unicode/path precedence, installed-mod/filter/catalogue install/cancel/failure and missing-model reconnect cleanup. Source code/pinned donor adaptation exists; actual complete cleanup/refusal at final software boundaries remains. |
| V18 | F07 | Actual image/model/fullbright/WAD3/lightmapped-liquid/style CPU-GPU and late-precache/whitespace consumers, malformed BSP bounds; retained worker/serial mfxsp17 and large-map extents acceptance stays credited. |
| V19 | F07,F05 | Repeated jumbo-map replacement and targeted stereo output including mj4m1, without heapsize workaround; >16384classic quads use their own vertices. Existing initial named-map rendering and exact worker/serial extents are accepted. No performance measurement gate. |
| V20 | F08,F01 | Remaining real negotiated voice delivery/receiver/map reset, private movement framing/fresh reconnect and active-XR default-system dummy mic profile with saved opt-out/desktop opt-in. Existing13native audio components and Opus/relay/jitter/loss/budget/generation/PTT/VAD/retry cases stay accepted. No physical mic recording or human listening. |
| V21 | F08 | Full host/device callback integration and retained music EOF-tail/timing limitations at the native audio owner. Current HRTF/native fallback, loop pause/cursor/reduced room send, wet-only monitor/room-worker/music formats/reset and device-fault component evidence remains credited. User physical audio/performance tests are excluded. |
| A01 | F10 | After implementation fixes settle, reconcile all final shipping inputs against Windows Release/Debug, Linux x86-64 and native Linux ARM artifacts, refreshing only affected production/platform inputs. Current0bd4 shipping cohort builds/packages/source hashes already match; later committed changes are tests/docs only. |
| A02 | F10 | Run combined final software verification for this list after fixes, credit prior unaffected acceptance; final local Astra integration signoff and exact outcomes/limits. Do not claim overall completion from builds/helpers or downgrade unavailable software evidence to a pass. |

## Completed or outside the goal

F09: five semantic package negatives complete. Current Windows Release/Debug,
Linux x86-64 and native ARM builds/packages/freshness accepted at0bd4 shipping
source; current docs/tests commits need no platform rebuild. No current storage
or GPU-availability blocker. Upstream vkQuake merge rehearsal/conflict-owner
maintainability work accepted as attempted rehearsal; integrating36new upstream
commits is outside this migration goal. Native architecture and graphics remain.

Excluded: skyrooms, quad views, Gorilla/instant stop/hand-swim propulsion, contact
melee/parry/hybrids, revival, Mjolnir dual-state additions, imagedump, VR/extra
demos, legacy settings/MP aliases and incompatible-device-loss reconstruction.
AV-009/general VRM importer and eleven useful-addition candidates remain research.
Physical Beyond2e/Monado and Steam Frame headset/gaze/FB-META/provider/listening/
multiplayer/performance outcomes remain user-side work. Both native and streamed
Frame release targets and optional eye tracking remain the implemented contract.

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

Draft status: senior reconciliation pending. This document is not final signoff.
