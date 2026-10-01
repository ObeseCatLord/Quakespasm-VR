# Full-scope final-checklist review brief

2026-09-30. User requested a full missing-feature enumeration through senior
review **before further implementation**, to become the final checklist. This
brief/worksheet are review inputs, not the final acceptance claim. Implementation
is paused for this audit; the overall migration goal remains active.

## Goal, scale and evidence rules

Keep vkQuake as the base, with all surviving inherited project behavior, the
requested QSS-M predictive VR/desktop networking, Ironwail/vkQuake performance
mechanisms and OpenXR. Reuse native owners and reference modules; smallest
adapters before replacement. Solo project: no new governance or rewrite to make
an audit easier. User-requested performance mechanisms are required; performance
measurement and user live headset/gaze/multiplayer trials are outside this goal.

Classify every requirement as: **M** confirmed missing/incorrect production;
**S** source-integrated/native-reused but final software qualification pending;
**Q** unresolved source/contract evidence (state the exact remaining question);
**X** excluded/deferred; or **R** retained reference/experiment only. Names,
declarations, counts, old plans and absence of TODO do not prove implementation.
Never turn an untested implementation into a missing feature merely because it
hasn't run. Never turn uncertain source correspondence into completion.

Final output needs a short numbered actionable checklist, a complete185-row
crosswalk and explicit disposition of interface/history obligations and optional
proposals. Preserve source-integrated features without reimplementing them.
Any uncovered inherited behavior gets a sub-item/ID and evidence, not a silent
scope shrink. Main makes final dispositions and integrates the checklist.

## Environment facts, verified cheaply by main

| Fact | Current authoritative evidence |
| --- | --- |
| Writable source | `/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0`, established2.0 checkout. Do not recheck/switch branches. Main/source siblings untouched. |
| Audited production snapshot | `2b420380` before this documentation-only audit. Working tree has only user-owned dirty `docs/migration-2.0.md`, excluded from reads as current approval/implementation evidence and from edits/staging. |
| vkQuake ancestry | `git merge-base --is-ancestor 4bc898f29073e8aa41069f0e79e3cb5a9eb73afa HEAD` succeeds. It is a real donor-based migration, not a rename. |
| Primary behavior | Read-only `../quakespasm-openvr` at pinned `51b452c018273647dcf94f4628a370267ff8fa91`; older inventory anchors1327f795 remain historical, later changes in migration-source-updates.md. |
| Other sources | XR3080841333fa94000df7e1fb9e549c7158685dd6; vkQuake4bc898f29073e8aa41069f0e79e3cb5a9eb73afa; QSS-M03a498aabc411e2e739adc815c5536b161b9626e; Ironwail08d578136ff43d7d1ef38e636dfbfd3e844be7cd. Use pinned objects where working references differ. |
| Full scope | migration-feature-map.csv has185 unique work rows across14 categories. It includes124 base/product/platform goals,29 network and32 renderer/loading/asset work items, with overlap. Counts aren't independent feature counts or completion percentages. |
| Interface/history coverage | migration-qc-interface-index.csv512 literal QC registry rows; migration-interface-index.csv1303 literal command/cvar declarations; history540 (MAIN497/XR43) and preservation906 ledgers route surviving inherited work. See final-scope-interface-history-2.0.md for the supplemental screening and limits. These are obligations/evidence inventories, not behavioral proofs. |
| Existing source receipts | implementation-plans.md and current source checkpoints for renderer/assets/input/menu/gaze/foveation/tracker/custom-avatar/co-op policy+saves/discovery/commands/QC strings/vectors/message reads plus native networking source receipts. Verify actual consumers, not prose alone. |
| Assets | Read-only game data in `/home/obesecatlord/Windows/Games/quakespasm_straight`, per inherited AGENTS guidance. No deployed assets/settings/saves/server changes. |
| Execution | User forbids tests/builds/compiler/lint/probes/fixtures/game/performance until implementation ends. This review is source-only; final consolidated Linux+isolated ssh Foundry ARM client/software qualification remains required. Windows builds deferred. |
| Delegation | Coding, when resumed, uses direct gpt-6-luna xhigh. Requested local Astra review uses explicit gpt-6-astra/xhigh. No ChatGPT-web models. Effective reviewer routing metadata is not currently exposed by multi_agent; requested-source-review provenance must stay explicit rather than claim certified settings. |

## Verified changes and known missing code

[Verified: actual committed source, main and bounded requested-Astra review.]
Recent completed slices include generic swimming486a5a49; FB/META/KHR foveation
source reconciliation5d8daf3b; existing AD selector1feb7d9f/83599382; native
pipeline cache07616cd1; BSP preparation3c440916; saved-edict lifecycle7485407a;
wheel raw opening grip/full-basis calibration/view follow6150cf39; inherited
find/nextent inactive-client filtering4e35dd69; inherited round-query predicates
38cdd7a4. None of their narrow receipts certifies the full goal or a live device.

[Verified: current actual native physics and primary source, main spot-check of
bounded requested-Astra MOD-011 findings.]

1. **M-PHYS-01, P2:** `SV_Physics_Toss` in sv_phys.c11745 returns on grounded
   state after Think without the inherited `SV_TossGroundIsValid` check. Primary
   sv_phys.c6767–6804 releases hidden/freed/invalid non-world supports. Existing
   robust pusher maintenance is not equivalent: it is record/mode-specific and
   precedes Toss Think. Smallest seam: copy the primary check at grounded return.
2. **M-PHYS-02, P2:** legacy elevator mode1/2 recovery in `SV_PushMove` at
   sv_phys.c3279 changes final Z then continues without relinking. Primary
   sv_phys.c4414/QSS-M922–930 relink without firing triggers again. Robust native
   recovery already relinks; smallest seam is the inherited one-line relink.

Do not implement either until the requested full checklist is reviewed.

[Verified: main current-source inspection, not yet independent whole-feature
acceptance.] WPN-010 is not an established missing projectile subsystem. Current
input builds one shared-calibration body-relative muzzle; sv_user decodes finite
pose fields into accepted commands; `SV_BeginPrivateVRWeaponPose` uses that
originating pose, native world clamp and existing source-offset owner around
Think/PostThink; guarded restoration/setorigin/link/error cleanup exist. The
primary has the same generic QC source expression compensation. Stock nail and
mission-pack source corrections already exist. Full projectile outcome, nested
QC/cancellation and solo/listen/dedicated agreement remain qualification questions.

[Verified: current scope clarification and prior main/requested-Astra source
disposition.] Arbitrary cooperative QC replacement hooks intentionally allow
authoritative corrections; builtin347 does not establish that every custom hook
is replay-equivalent. No universally identifiable missing plain-replay subset
was demonstrated. Preserve real predictive native/compatible-mod behavior and
actual cooperative integration; don't invent a second client solver to close an
old “prediction missing” label. Review any concrete contrary source evidence.

## Scope decisions that override old gate text

Read migration-scope-decisions.md in full. Required: OpenXR+desktop, Windows as
eventual release target, Linux x86-64 and native Linux ARM64 client; Steam Frame
streaming/native and Beyond2e/Monado with optional eye tracking; two views only;
FB/META preference, KHR startup/device fallback allowed, no live switch onto FDM
device; eye toggle with unsupported/invalid/stale gaze full quality; fixed mode
explicit only, never default/fallback; conservative either-eye culling independent
of gaze; native vkQuake graphics and desktop behavior, shared AO setting/qualities
choosing native desktop versus VR AO; default-system mic and saved VR default-on
opt-out; one weapon/muzzle calibration solo/MP; gesture-only immersive melee with
trigger and visible attack animation suppressed while normal QC attack remains.

Excluded/deferred: quad views, skyrooms, Gorilla/instant stop/swim propulsion,
physical-contact melee/parry/hybrid adapters, Mjolnir dual-state weapons,
imagedump, co-op revival, VR demos/additional demo features, legacy-setting/MP
offset aliases, general incompatible-device/device-loss reconstruction. Keep
ordinary swimming/ladders/native momentum and desktop demos. Hardware/gaze/
multiplayer/performance trials and Windows builds aren't final-goal gates.

Optional proposals in migration-useful-additions.md are mapped research, not
automatically required: background saves, retry redesign, live previews, extra
downloads/dialects/CSQC prediction/ICE/255slots, dithering/clustered lighting.
Do not add them merely because user asked to map useful fork features. Required
native rendering/loading threading, precision/truecolor/WAD3/lightmapped liquids/
lightstyle interpolation/large-map mechanisms stay in scope.

## Main lean, open decisions and rejected alternatives

Lean: produce one full final checklist with **confirmed implementation defects
separate from source-evidence questions and consolidated software acceptance**.
Retain the185-row crosswalk, overlap grouping and optional/excluded appendix.
One missing owner/caller gets a narrowly specified adapter item, not a bulk port.

Decisions for reviewer: identify any additional actual missing behavior; demote
stale statuses with actual native consumers; expose unsupported source claims;
group final build/package/runtime/merge-maintainability obligations without
dropping any explicit target; settle whether archived experiments need only
provenance versus product integration based on actual user scope.

Rejected: treating every old pending label as absent production; counts/names
as parity; a percentage derived from185 overlapping rows; more renderer/network/
state owners to simplify auditing; silently adopting optional research; preserving
superseded old release gates (OpenVR compositor, legacy settings, Windows or user
hardware trials) against current explicit decisions.

Overlap is expected: XR010/011 and PERF013/021; VR/WPN/calibration UI; AV/FBT
network relay/shadows; NET013/028 and MOD011; audio transport/defaults/packaging.
Merging duplicated checklist steps is allowed, dropping concrete obligations isn't.

## Requested review contract and failure path

One lead local Astra xhigh reviewer, read-only, verified-main brief first then
actual consumers. Review the full surviving185 rows plus supplemental interface/
history/optional/user-policy obligations. Use existing bounded receipts only
with current source spot-checks and actual caller/registry/feature gates. Prefer
reuse/deletion, challenge scope, rank missing behavior by impact. Main will
spot-check every proposed missing item and synthesize disposition.

Output: all185 IDs classified with short owner/evidence and any unresolved exact
question; prioritized actual missing features with file/line+reference evidence
and smallest seam; separate final qualification/delivery checklist and exclusions.
Hard prose cap2500words excluding compact185-ID crosswalk; no noisy code/log
dumps. No edits/execution/branches/telemetry/nested agents. If the scope cannot
be verified in one pass, return exact coverage/remaining IDs rather than claim
full enumeration; reuse this same reviewer in bounded continuation batches.
