# Final scope senior-review disposition

2026-10-01. Main synthesis of the local Astra xhigh lead and independent bounded
audio/avatar/QC reviews. Production snapshot `2b420380`; no code or executable
checks changed during the audit. Main verified the reviewers' effective
`gpt-6-astra`/`xhigh` routing. This establishes review provenance, not runtime
certification or completion of the migration.

The complete feature inventory has 185 unique rows. Final classification is
153 S (source-integrated/native-reused, qualification pending), 18 M (confirmed
missing production/delivery behavior), 2 Q (one shared unresolved rendering
contract), 11 X (excluded/deferred), and 1 R (reference experiment). These overlap
as product features: they are not percentages. The 18 M rows yield 22 concrete
checklist subitems after merging shared defects. The canonical work list is
[final-checklist-2.0.md](final-checklist-2.0.md); complete row evidence belongs in
[the crosswalk](final-scope-enumeration-2.0-worksheet.csv).

| Senior recommendation | Main disposition and rationale |
| --- | --- |
| NET-007 late-model tag and missing loading keepalive | Adopt C01/C04. Main checked actual QC binding, correct native helper, client tag consumer and current versus pinned-primary loading loops. Reuse native precache/network/parser owners; preserve queued reliable data. |
| NET-021 initial/retired metadata and generic SSQC lookup | Adopt C02/C03. Main checked current signon/spawn/drop stores and null fallback branches against QSS-M publication and Info_GetKey consumers. No second metadata protocol. |
| NET-020 unsupported challenge dialects | Adopt C05. Main checked offered tokens against actual accepted server protocol headers. Remove unsupported offers; implementing more dialects remains research. |
| NET-022/UI-004 missing-model reconnect failure | Adopt C06, one shared item. Main discovered and checked current Host_Error/CL_Disconnect/timed-retry chain; Astra independently confirmed. Adapt primary cleanup to current parser's true-to-abort return convention. |
| NET-014 optional smoothing | Adopt C07. Main checked inherited error producer/view consumer versus current reset-only remnants and direct replay publication. Keep existing ACK/epoch/movement authority and default-off bounded presentation behavior. |
| MOD-011 post-Think Toss support and elevator relink | Adopt C08/C09. Main checked current grounded return and successful legacy nudge against primary/QSS-M. Robust pusher bookkeeping does not replace either seam. |
| AV-002 root yaw and renderer eligibility | Adopt C10/C11. Main checked interpolated yaw has no current root consumer and current preparation lacks viewer gating; pinned primary applies both. Share corrected root across body/props/muzzle/shadow without mutable entity transforms. |
| AV-003/006 QBJ3 live/death/corpse/equipment | Adopt C12–C14. Main checked current scoreboard-only/player.mdl admission and Ranger prop path against primary exact model/frame, corpse owner, digest/socket and optional-prop behavior. These are inherited bounded exceptions, not a new mod framework. |
| Spatial loop pause and wet send | Adopt C15/C16. Main checked native mixer pause predicate versus unconditional spatial activity and primary 0.35 looping room-send factor. Keep callback cursor/generation ownership and independent voice/music policy. |
| AUDIO-010 local wet default decision | Adapt raw Q into C17/M: restore inherited 0.6 initializer; retain saved values/independent permission. Main compared actual 1 versus 0.6 defaults and consumer. No accepted functional reason for the accidental default change was established. |
| SteamAudio distribution notices | Adopt C18. Main confirmed current source closure/install carries native license only and lacks the inherited combined-build notice. Reuse reference notice and existing package owner; source SDK recipe already exists. No new license framework or legal determination. |
| Linux portable delivery versus Nix-store build | Adopt C19 at existing build/install/dependency seams. Main checked native recipe and its explicit nonportable status. Retain inherited GLIBC 2.39/clean loader contract and ARM dependency architecture; qualification follows implementation. Windows builds remain deferred. |
| MOD-014 VR field panel | Adopt C20. Main checked native collection and default-canvas drawing versus panel-flag stereo transform. Preserve desktop diagnostics, adapt physical VR presentation only. |
| PERF-F001 timing diagnostics | Adapt C21 to actual native frame/AO timestamps and shared-pass labels/availability. Main inspected masking/wrap rejection and zero-on-unavailable reporting. Do not create invented per-eye timings, a second profiler or performance measurement gate. |
| MOD-010 dense classic-particle indices | Adopt C22. Main independently checked uint16 staging/index binding, i*4 overflow above 16,384 and raw unbounded count parsing against primary 32,768 default/65,536 limit. Use bounded initialization and existing uint32 quad buffer; no new particle renderer/FTE pool. |
| XR-011/PERF-021 shared alpha sorting/water boundary | Retain Q01, one exact design task. Main checked center-origin sort and center-leaf category; primary deliberately shares sort origin too. Non-OIT remains allowed. Establish correct eye/water-boundary behavior before deciding whether a narrow exceptional pass is necessary; shared sorting alone is not an M. |
| NET-028 explicit private solo profile | Adapt to S. Ordinary solo remains native; explicit private-profile selection is an allowed override. Astra found no contrary default-play regression. Do not add an exclusion or another movement solver just to reproduce the old predicate. |
| NET-017 three-second same-IP established eviction | Reject restoration. Native timeout and guarded unambiguous rebinding follow the existing NAT review; restoring pruning can evict legitimate quiet peers. Reconcile stale inventory acceptance text. |
| cl_iDrive opposing-key arbitration and legacy aliases | Retain native desktop subtraction under selected vkQuake baseline. Unconsumed declarations, retired GL controls and waived aliases do not establish missing behavior. Preserve useful authored weapon calibration. |
| Separate two-hand gripping subsystem | Reject invented scope. Actual paired-hand/support geometry and cosmetic consumers are present; primary references did not establish another gripping solver. |
| AV-009 general VRM/manual clip rewrite | Reference-only R. Preserved experiment does not establish a general production importer requirement. Existing prepared palette/retarget owner and prior performance disposition remain; no unmeasured speedup claim. |
| PLAT-008 archived WIP | S for completed reconciliation/preservation ownership; underlying WIP remains reference-only. Native owners and explicit exclusions supersede stale experimental renderer/Gorilla code. Final substantive upstream merge rehearsal still required. |
| Additional fork research, Windows and live qualification | Keep mapped optional candidates separate. Honor user exclusions and defer Windows builds/live headset/gaze/listening/multiplayer/performance trials. Required Linux/ARM end-of-goal software qualification remains. |

## Supplemental coverage and limits

The audit considered the 512 QC declarations, 1303 command/cvar declarations,
540 history entries and 906 preservation paths through their source routes and
[explicit interface/history dispositions](final-scope-interface-history-2.0.md).
The two QC pins each contribute 256 rows. No empty preservation/history routes
remain. Literal names/counts alone are not signature, permission or behavior
proof; source-integrated families still need loaded-program and end-to-end
qualification. Legacy-name absence is not a defect under the settings waiver.

The reviews changed the work list: they found real precache, reconnect, pose-root,
physics, audio and index defects, while rejecting unnecessary projectile,
movement, avatar-importer and rendering replacements. Main spot-checked the
load-bearing M/Q claims, retained exact uncertainties and merged duplicated
requirements. No user decision is required to begin the listed small plans;
Q01 needs technical assessment before implementation, not guessed policy.

## Final synthesis check

Local Astra xhigh reviewed the completed checklist, crosswalk and disposition
document after main integration. It found no blocking consistency errors: all
185 IDs and 22 missing subitems, Q01, eight final qualification groups and user
policies agree. The enumeration review is complete; no source re-audit or
executable checks occurred.

Main also corrected the ASSET-009 filename receipt from r_light.c:41 to the
actual Quake/gl_rlight.c:41. This is an evidence-path correction only.
