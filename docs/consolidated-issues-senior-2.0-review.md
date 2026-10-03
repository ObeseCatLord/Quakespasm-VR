# Consolidated remaining-issues senior review

2026-10-02. Local gpt-6-astra/xhigh, effective active-context settings independently
verified by main. Main reconciles185matching inventory IDs and supplies the
[consolidated draft](remaining-issues-consolidated-2.0.md); the reviewer reads
chronological source/acceptance evidence and independently checks risky current
code seams. Read-only, no children/build/test/runtime/GPU/system changes.
Historical unchanged donor reviews are reused with provenance, not presented as
a new every-line audit. Main spotchecks each load-bearing new source finding.

| Senior recommendation | Main disposition / evidence |
| --- | --- |
| Add stale typed-event entity conversion defect D03. | Adopt. Main reads sv_user.c SV_ReadQCRequest, Debug pr_edict.c EDICT_TO_PROG, native Host_Error and saved-reference offset adapter. In-range free target causes Debug server error before handler lookup. Pinned MAIN and QSS-M use plain offset conversion. Use that existing bounded-reference representation locally; leave free identity/general Debug validation intact. |
| Add string-command retirement/replacement defect D04. | Adopt. Main reads both QC/native command dispatch, typed-request socket guard, PF_dropclient/PF_spawnclient and SV_DropClient exact accounting. Old callback can retire requester; next parser false double-drops, or replacement becomes active and inherits trailing commands. Snapshot requester/socket and finish retired message successfully using existing typed-request pattern. No new lifetime policy. |
| D01 displacement thresholds do not prove broken prediction. | Adapt into evidence gap, not speculative code fix. Missing elapsed-time/velocity/obstruction premise; endpoint displacement is not distance travelled. Preserve existing thresholds/history; final grouped tests collect necessary premises from accepted commands and ordinary collision-free opportunity. |
| D02 is unknown old public-peer allocator failure. | Adopt scope correction. Neither new defect identifies its cause. Do not require modifying the read-only baseline or recreating missing old logs. Current fork clean exits stay credited; final mixed-peer process outcomes record contemporaneous stack/ownership evidence if the failure recurs. |
| Explicit missing coverage clarifications. | Adopt into existing V rows: NET-020 discovery/control, co-op collision/friendly-fire/telefrag, NET-009 CSQC lifecycle/loss/re-enable, NET-015 typed request cursor/weapon/alignment, VR-010 haptic routing/toggle, VR-011 calibrated crosshair/obstruction/mod bindings, world/PVS/moving brushes/once-per-frame effects, PERF-F003 shadows, PERF-F001 diagnostics and PERF-003/004 texture/skin worker equivalence. No new feature or gate hierarchy. |
| Preserve native architecture and current acceptance. | Adopt. Current metadata/cvar/replay/save/stereo-PVS/avatar/foveation/audio owners support narrow repairs. Current0bd4 Windows/Linux/ARM cohort, bounded GPU/alpha/static-culling, local/QC/map/metadata/foveation/audio accepted subcases remain credited at their exact limits. |
| Delete obsolete and duplicate work. | Adopt. No storage/GPU/missing-map platform blocker, no repeated C01–C22/Q01 implementation, no exhaustive builtin/mod/device/full-campaign matrix or every graphics quality combination. Inventory anticipated vr.c/glob/excluded paths are historical routes, not source omissions. Preserve provenance, clarify current evidence. |
| Four final grouped verification passes after all fixes. | Adopt. Network/QC/co-op/save; input/render/assets/foveation; native audio integration; affected shipping/integration. Reuse actual native owners/fixtures, record outcomes and limitations; tests/docs alone do not demand platform rebuild. Production D03/D04 repairs do. |

Coverage: all185IDs reconciled at inventory/source-disposition level; no exact
unreviewed ID. Outstanding executable evidence remains explicit in V01–V21 and
A01–A02. No whole-goal completion or final F10 integration signoff. Existing
physical headset/gaze/provider/listening/live multiplayer/performance and other
explicit feature exclusions remain; no human contract choice required.

Main final spotcheck identifies one adjacent malformed string-prefix dispatch
question before freezing implementation: MSG_ReadStringBuffer EOF produces a
terminated prefix with msg_badread; string case can dispatch before its next
loop check. Same reviewer verifies this bounded concern separately, not another
full audit. Main and the same reviewer adopt immediate msg_badread refusal after string
read, before any capability/QC/native dispatch. Only unterminated EOF prefixes;
terminated overlong-string and generic MSG helper policy remain unchanged. This
is D04's adjacent admission subcase. Consolidated enumeration is now complete;
implementation follows the before-code native-boundary plan, then final tests.
