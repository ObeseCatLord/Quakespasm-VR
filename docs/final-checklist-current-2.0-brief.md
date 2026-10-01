# Current final-checklist senior review brief

2026-10-01. Main-owned verified brief for the user's request to enumerate every
remaining feature before further implementation. Solo-maintainer project; review
the existing architecture and final scope, not a proposal for new features.

## Environment and authority

| Fact | Evidence / status |
| --- | --- |
| Writable checkout | Established `/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0`, branch 2.0. Do not recheck/switch branches. |
| Current committed source | `0f2ed15b`, previous integration receipts in final-checklist-2.0.md. |
| Pending source | Seven-file C02 patch, 846 added/deleted lines from scoped git diff --numstat. Unaccepted, unstaged, stopped beyond the reviewed BEFORE800 boundary. No worker remains active. |
| User-owned change | docs/migration-2.0.md is dirty; do not edit or use as current acceptance evidence. |
| Reference checkouts | Sibling quakespasm-openvr and quakespasm-qssm references are read-only. Use the pinned revisions named in the committed plans. |
| Execution policy | Source/document reads only. No builds, tests, executable probes, lint, fixtures, SSH, game runs, deployment or performance measurements. |
| Previous exhaustive audit | final-scope-enumeration-2.0-worksheet.csv accounts for all185 IDs. Supplemental interfaces/history are in final-scope-interface-history-2.0.md. Frozen classifications are historical, not present progress percentages. |
| Final acceptance | final-linux-arm-qualification-2.0-plan.md defines the consolidated final software pass. Windows builds and user live headset/gaze/listening/multiplayer/performance trials remain outside it. |

## Verified evidence versus provisional interpretation

- [verified: current checklist and source receipts] The 22 original audit findings
  have 21 source-integrated entries. C02 metadata remains incomplete; C07 renderer,
  C14 equipment and C19 portable staging now have committed source receipts.
  No final-tree executable acceptance has been established.
- [verified: scoped status/diff] Only the seven allowed C02 production files plus
  the user-owned migration doc are dirty. C02 files: Quake/cl_main.c, client.h,
  cvar.c, host.c, host_cmd.c, server.h, sv_main.c. No source edits during this review.
- [verified: current full cl_main diff] The draft adds transient complete client
  signon serialization, pending native reply, larger backing/logical limits,
  empty-serverinfo receipt, and prospective live userinfo admission. These are
  source presence, not accepted correctness. Native cleanup clears cl; inspect
  lifecycle before alleging missing serverinfo_received reset.
- [verified: sv_main.c:5316-5569 and SV_ConnectClient] Draft recipient eligibility
  is PEXT2_PREDINFO; reader envelope separately uses offered_metadata. Connect
  does not explicitly dirty the new occupant for other existing recipients.
  Whether the current eligibility misses an intended profile needs checking
  against real negotiation, not assuming PREDINFO means large-reader support.
- [verified: sv_main.c:5393-5414,5490-5527] Binary name/color companions are appended
  without a preceding remaining-space check. Raw public userinfo is projected;
  actual ordered native name/topcolor/bottomcolor store overlay simulation is
  absent. This does not meet adopted plan steps3-4 on source inspection.
- [verified: current Cvar_SetQuick and SV_UpdateInfo diff] Live server mutations
  mark full obligations rather than using the plan's shared incremental publisher.
  Assess whether simpler current-store full publication is sufficient, or which
  exact surviving behavior requires an incremental seam. Do not invent a queue.
- [unverified] All remaining source-present renderer/input/QC/audio/co-op/asset
  behaviors still require the final meaningful native owner/output qualification.
  Source receipts and old tests cannot certify the current tree.
- [unknown until this review] Any independent missing feature outside C02 that
  the earlier exhaustive audit or recent integrations overlooked.

## Decisions and current lean

1. Is C02 the only unfinished implementation area? Lean yes, conditional on
   reviewing every185-row ID against prior audit, current receipts and scope.
   Use prior detailed source reviews for unchanged rows; independently challenge
   uncertain or contradicted closures at actual consumers. Repeating every
   unchanged file is unnecessary; unreviewed rows must be reported explicitly.
2. What exact C02 substeps remain? Lean retain native stores, reliable messages,
   dirty obligations and narrow signon phase; list source defects without accepting
   the oversized draft. Reopen estimate/design before any further C02 coding.
   Replacement protocol/networking, persistent cursors/snapshots/queues rejected.
3. What remains besides coding? Lean preserve all eight consolidated software/
   artifact/upstream/final-review groups with observable software behavior.
   Do not turn user live trials or excluded features into hidden completion gates.
4. Is anything genuinely a human decision? Lean none; user has repeatedly fixed
   scope. Technical capacity/privacy/lifetime choices should be resolved locally.

## Review contract and depth budget

One local Astra at explicit xhigh. Verify before critique. Read the canonical
checklist, all185 crosswalk IDs, prior original and refreshed dispositions,
supplemental interface/history decisions, current integration receipts and final
qualification plan. Read actual code where a source closure is challenged. Inspect
the seven-file C02 draft against its adopted plans. Rank by actual completion
leverage; merge overlapping findings under the existing feature owner. Pick the
highest-leverage issue for deeper specification rather than expanding every detail.

Return a terminal report only, maximum2500 words: coverage and any exact unreviewed
IDs; exhaustive remaining implementation subchecklist with file/line evidence;
pending qualification/delivery list; excluded/deferred boundaries; any scope or
architecture correction; prioritized findings and genuinely human decisions.
No writes, commits, tests, nested agents, telemetry reads or branch operations.
If evidence is missing, label it and report instead of broadening scope.

Not-list: no re-port of source-present features, no redesign of native vkQuake,
no quad views/skyrooms/Gorilla/instant stop/hand-swim propulsion/contact melee/
revival/imagedump/extra demos/Mjolnir dual-state weapons/legacy settings or MP
offset aliases/general incompatible-device-loss rebuilding. Eleven useful-addition
candidates remain optional research, not blanket-authorized features. Keep the
single solo/multiplayer weapon/muzzle calibration and desktop baseline.
