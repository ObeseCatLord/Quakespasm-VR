# Pause generation correction: final causal adapter review

Solo fork, bounded follow-up to your prior live-liquid review. Read the committed
[pause ordering plan](predictive-pause-ordering-2.0-plan.md), current production
diff in `Quake/cl_parse.c` and the relevant captured driver/ACK fixture. Main
will run final checks/docs while production remains frozen. Read-only, no edits,
tests, agents, broad migration analysis or operational telemetry export.

The tuple-only proposal below was superseded by the committed architecture
amendment in `59bf0c6c`. Review the implemented causal adapter now: actual
`Host_Pause_f` calls `SV_SendPauseNotifications`; each active selected pinned
peer synchronizes at the actual toggle through the existing pause owner, then
receives an atomic reliable `QSVR_SVC_MOVEACK`/`svc_setpause` pair. Public/native
recipients receive the original pause bytes. The reused ACK body carries actual
completed ACK/epochs/reason and selected/pending flags, UNKNOWN authority, no
prediction/authoritative flag. No new opcode/body/capability/state field.

Validated raw event epoch survives ordinary stale ACK rejection and is held
only in message-local context for the adjacent pause service. Older events
preserve newer accepted pending/completed metadata. Current/source-ahead events
latch their source epoch and invalidate replay; a zero first sequence prevents
any old/unobserved marker. Actual newer pending metadata or the reliable
unpause control body resumes the ordinary legal marker producer; permission
still needs actual full completion. Pair context resets at other services and
does not manufacture an owner snapshot. Demo ACK parsing provides no live context.

[verified normal software] New `RunAheadPauseSnapshots` passes actual pending
snapshot before delayed services, completed snapshot before either service of
a distinct delayed pair, and a genuinely new pause afterward, in plain wet
and active ledge cases.
Rapid toggles have no intervening run-client tick. Actual third-peer admission
checks distinct selected clients' event epochs and public native pause bytes.
Original withheld-owner-snapshot/stale pre-pause/newer relocation tests pass;
the reliable unpause pair now supplies the real pending fence before an owner
snapshot, so a legal marker may queue while replay remains denied.

[verified software] Linux -Werror, complete water/slime/lava/generated-VR/
10/25/100-ms transit matrix and mixed earlypause/arrivalgap/native/loss/wrap/
death checks pass. Focused ACK ASan/UBSan passes, including validated raw epochs
from rejected metadata, malformed-body exclusion and source wrap. Main finishes
partial combined sanitizers/docs while final production stays frozen. Review
the actual revised pairing plus source-relative tuple; ignore superseded claims
in the historical table as current architecture. Return <=500 words, no reruns.

| Fact | Verification |
| --- | --- |
| Prior issue actually reproduced | [verified actual-code software] Withholding paused/pending snapshots and delivering host pause/unpause services reopened live pending replay and the delayed old snapshot in ordinary wet movement and positive e1m2 ledge (`reopened=1`, `stale_reopened=1`). |
| Minimal correction reuses the existing tuple | [verified source] Pause service for a known selected private live peer invalidates snapshot/permission and latches current epoch as valid with first sequence zero, resume pending. No new field, epoch increment, timer reset, protocol or producer is added. Existing producer emits no marker with that tuple. |
| Parser requires actual newer pending fence before leaving pause latch | [verified source] Equal/older generations stay rejected; a merely newer pre-pause completed/relocation snapshot cannot reopen it. Fresh pending metadata resets through existing observation path; normal legal marker/completion follows. Existing completed-generation stale guard retained. |
| Captured live proof now denies old replay and completes fresh generation | [verified] Transit driver real host services without snapshots, received queued command discarded without completed ACK/timer advancement, delayed same-epoch ledge snapshot and newer pre-pause snapshot produced by actual setpos+nclip-off in ordinary wet case, actual new pending snapshot/marker/QC/world/full completion. Both reopen only after actual completion. |
| Focused payload parser tests pass ASan/UBSan | [verified] Equal suspended/completed and newer completed metadata remain unchanged/rejected; actual newer pending body accepted, including epoch wrap. This is a raw-codec fixture; pause service is separately proved in admitted actual-code driver. |
| Ledge oracle strengthened without widening movement tolerance | [verified normal software runs] e1m2 25/100-ms desktop and generated-VR live origins/tangential velocities stay within original wire-derived bounds. Desktop history-only diagnostic velocity uses the same bounds. Exceptional live z must be exactly zero from grounded dry/no-waterjump upward clipping; no broad threshold. Zero preview also ends a falling disposable T before the next authoritative command; require T=0 vs server T>0 and both downward velocity. Authoritative stats stay unchanged. Existing shadow intentionally excludes VR, so VR uses actual live movement and explicit solver-rule assertions, not a fabricated shadow. |
| Limits remain explicit | [verified plan] Default selection off. Synthetic one-command dry→wet→dry rather than real BSP; captured delivery/prepared client/resources/input/start seams. No full-domain/mod/connected-XR/Windows/ARM/performance claim. |

Review the pause tuple's meaning and all current uses in parser/producer: does
reuse avoid another owner without letting stale state reopen prediction or
stalling valid completion? Also challenge the ledge oracle exceptions against
actual PMove zero-duration ground and falling-timer rules. Prior ownership and
full graphics/migration scope are not to be reviewed again. Verify effective
local Astra/max only from turn_context model/effort fields. Return <=500 words,
scope_done/model-effort, ranked blockers or why addressed, file/line evidence,
minimum remaining qualification and assumptions. Main owns integration.
