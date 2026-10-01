# Final goal audit: verified brief

2026-10-01. Input snapshot30395c31. The user requests one final exhaustive
remaining list, then execution of that list. Solo project; finite product scope,
no enterprise ceremony. This is scope reconciliation and risk prioritization,
not a claim that all software qualification has already passed.

## Product contract

Migrate all surviving inherited/user work onto vkQuake with desktop and OpenXR,
modern QSS-M-style predictive multiplayer and desktop/VR crossplay. Reuse native
renderer/tasks/loading/VM/network/asset/resource owners and reference code via
small adapters. Preserve native vkQuake graphics and desktop behavior except
requested shared features. Windows remains a release target, verification later.
Linux x86-64 and native Linux ARM/Steam Frame are current build targets.

Beyond2e/Linux/Monado and Steam Frame streamed/native: optional eye toggle,
FB/META preferred foveation, startup/device KHR fallback allowed; absent/disabled/
stale gaze means full quality. Fixed is explicit only, never default/fallback.
Two-eye single-pass eligible geometry, conservative either-eye culling independent
of gaze; no quad views. Shared AO setting/qualities chooses native desktop AO or
VR AO. Reuse native graphics/materials/effects and requested threading, precision,
true-color/WAD3/models/liquid-lightmap/style/large-map mechanisms.

Inherited HUD placement/wheel/controls/tracking/avatar/weapons/co-op/mod support
remain required. One solo/MP weapon/muzzle calibration; useful authored offsets
retained. Gesture-only melee: recognized immersive melee ignores trigger, swing
supplies ordinary QC attack, held attack animation suppressed; QC authority.
Default-system mic; fresh VR default-on with saved opt-out, desktop opt-in.
Generic mod behavior, AD presets and only established inherited/QBJ3 exceptions.

Excluded: skyrooms, quad views, Gorilla/instant stop/hand-swim propulsion,
physical-contact melee/parry/hybrids, revival, Mjolnir dual-state weapons,
imagedump, VR/additional demos, legacy settings/MP offset aliases and general
incompatible-device-loss reconstruction. AV-009 is reference research, not a
general VRM importer; eleven mapped useful additions are research, not blanket
implementation authorization. Live headset/gaze/provider/listening/multiplayer
and performance measurement belong to the user later. Do not reinstate these
as test gates. Do not silently exclude real current software defects.

## Verified evidence and environment

| Fact | Evidence/status |
| --- | --- |
| Only production workspace is this quakespasm-2.0 worktree; branch already checked once | [verified: prior session]. No further branch switch/check. Main/reference worktrees read-only. Dirty docs/migration-2.0.md is user-owned and outside every write set. |
| Complete feature map and audit worksheet each contain185 unique matching IDs | [verified: Python CSV read this turn]. Original worksheet153S/18M/2Q/11X/1R is historical classification, not current completion. Read all rows; reconcile changed scope and later integrations. |
| Supplemental interface/history coverage exists | [verified: CSV reads and final-scope-interface-history-2.0.md]. QC512rows, command/settings1303rows; history540 and preservation906 recorded by prior audit. Literal names/routes do not establish behavior. Read checkpoint dispositions and investigate contradictions, not every unchanged declaration anew. |
| Original18missing rows became22integrations C01–C22, plus shared Q01 | [verified: canonical checklist source receipts]. All integrated through f3727a10; current prior reviews establish no additional source gap. This is a claim to independently challenge at load-bearing seams, not executable certification. |
| Current builds/packages are production-equivalent | [verified: portable-current-qualification-2.0.md and308-file comparison]. Immutableff83e66a includes all current production changes through29129513; later commits tests/docs only. Full Linux/native ARM with SteamAudio4.8.1/OpenXR/shaders/codecs/CURL; both45ELF/651inventory/118contributors verified. Group1 complete. |
| Actual current packaged rendering works in bounded cases | [verified: portable receipt]. Desktop start signon4 and24actual simulated-Monado XR probes pass clean validation/normal exit; packaged native ARM dedicated start/map/exit passes. Simulated inputs, no real gaze/FB/META/device/audio/performance proof. |
| Current fixtures and maps | [verified: current receipts].19standalone;6native metadata incl permanent drop;4native mixed-network profiles pass. mj4m1/shib1_drake/tavistock/ad_tears initial render/exit without heapsize pass. Prepared/captured fixtures do not establish connected crossplay or every lifecycle. |
| Prior eight broad qualification groups remain open exceptbuilds | [verified: results ledger]. Need replace broad historical wording with a finite final actionable set, credit real current proof and deduplicate cases. |
| Upstream rehearsal is unresolved | [verified: upstream-merge-rehearsal-final-2.0.md]. Official0d812138,36new commits,14files/35blocks. Actual dispositions recorded, no merged build. Question: what finite requirement serves user maintainability without turning latest upstream features into new goal scope? |
| Assets/build locations | Read-only game assets /home/obesecatlord/Windows/Games/quakespasm_straight; only id1/pak0 confirmed. Existing isolated engine /tmp/qsvr-metadata-publication-native-isolated/Quake; host /tmp/qsvr-final-host-build; current artifacts/logs /tmp/qsvr-final-qualification-thchgzi8/retry5 and logs. /tmp16GiB tmpfs near full, avoid duplicate packages. Foundry builds isolated, no deployed server mutation. |

## Prioritized proposal to critique

1. Source-feature inventory is closed unless code proves a concrete missed
   contract. Independent review of185rows, interface/history dispositions and
   current user contract must identify exact missing IDs if closure fails.
   Reject fresh wishlists and feature counts as proof.
2. Collapse remaining qualification into finite risk slices: desktop baseline;
   stereo/input/presentation/lifetimes/foveation software policy; real connected
   crossplay/metadata/prediction/voice; loaded QC/co-op/save lifetimes; asset/mod
   compatibility; package/maintainability; final integration signoff. Credit
   current passes. Reuse existing actual-owner fixtures before adding coverage.
   Do not demand exhaustive512builtin or everymod/setting/device matrix merely
   because a historical plan lists them. Retain meaningful changed-contract
   end-to-end evidence. Flag overlapping slices for merging.
3. Next practical work: qualify existing local-load/cooperative-QC native
   fixtures, then connected process cases and remaining rendered/asset policy
   cases. Fix actual failures at native owners with before-code bounded plans.
   Reject replacing working lifecycle/protocol machinery to ease tests.
4. Upstream merge: lean accept documented real conflict/adaptor rehearsal as
   maintainability evidence with unresolved risk clearly stated, unless actual
   user goal requires successfully resolving/building that update now. Earlier
   reviews required resolved rehearsal evidence before claiming it resolved,
   but that is not automatically production integration of36new commits.
   Senior review must make a concrete, technically justified finite disposition.

## Review contract

One local gpt-6-astra/xhigh, read-only; no edits/builds/tests/SSH/children/raw
telemetry. Verify before critique. Read complete185-row inventory/crosswalk,
current scope decisions, interface/history dispositions, original source reviews,
current integration and qualification receipts. Independently inspect code for
highest-risk challenged contracts and any apparent omitted requirement. Prior
unchanged source audits may be reused with provenance; do not call that a new
line-by-line review of everything.

Return final message <=2400words: exhaustive coverage/exact unreviewed IDs;
source gaps versus verification gaps; one finite prioritized remaining checklist
with IDs, owner/evidence and objective completion boundaries; disposition of
upstream rehearsal; overlooked contracts/defects, exclusions/deferred work and
any genuinely human decision. Challenge scope/gates and eliminate redundant
work. If evidence cannot support closure, name exact missing evidence rather
than claiming nothing was missed. This is the final scope-setting audit; later
findings attach to these owners, not new silently expanding features.
