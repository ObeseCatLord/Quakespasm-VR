# CSQC entity transport reuse (NET-009)

Date: 2026-09-30. Branch `2.0` only; solo operator, bounded existing-owner
adapter. Quad views and Vulkan renderer changes are outside this slice.

## Goal and verified brief

Restore inherited custom entity creation/update/removal, `SendEntity/SendFlags`,
loss recovery and `enablecsqc` lifecycle from the primary OpenVR reference.
This is not a replacement full-game CSQC renderer. Default vkQuake geometry,
HUD fallback, native entity deltas, prediction, VR/voice and desktop cross-play
must remain usable. Existing QC particle and draw builtins consume callbacks.

| Environment fact | Evidence / status |
| --- | --- |
| Destination | `quakespasm-2.0/Quake`, writable2.0; main/reference and installed assets read-only. [verified: current worktree/commits] |
| Behavioral reference | Primary master `51b452c0`, `sv_main.c:SVFTE_WriteCSQCEntitiesToClient`, `cl_parse.c:CSQC_UpdateCsEdictForSSQC`, `Host_EnableCSQC_f`, `host.c:CL_LoadCSProgs`; QSS-M `03a498aa` same owners. [verified: direct source reads] |
| Required scope | `docs/migration-network-map.md:NET-009` requires transport, resend and re-enable, not declarations/events alone. [verified: row] |
| Existing native owners | `server.h` already has SENDFLAG constants and `deltaframe_s.ents[].csqcbits`; existing replacement ring, sequence/ACK, native pending entities, snapshot and packet continuation exist. [verified: server.h/sv_main.c] |
| Missing implementation | No destination custom pending array, csqcactive state, Ent_Update/Remove hooks, server writer or client parser/mapping. [verified: symbols/actual functions] |
| Existing QC channel | SendEntity/SendFlags fields exist; MSG_EXT_ENTITY already writes `sv.multicast`. [verified: progs.h/pr_cmds.c] |
| Existing model and spatial owners | Client GetModel now installed; client has its own native VM edicts, worldmodel and area nodes. Preserve those owners. [verified: host.c/world.c] |
| Public negotiation gap | Client offers FTE_PEXT1; `SV_Pext_f` ignores it, serverinfo does not emit it. [verified: cmd.c/sv_main.c] |
| Private admission | Marked and explicitly selected legacy private header requires PEXT1 zero; old2.0 clients advertise PEXT1 despite lacking transport. An offer alone cannot authorize a private-header change. [verified: cl_parse.c:CL_ReadServerProtocol and cmd.c] |
| Activation reference | Primary and QSS-M enable/disable client commands own one csqcactive flag; enable reflags present entries. Neither command requires a PEXT1 header. [verified: host_cmd.c] |
| Loader gap | Native loader admits only DrawHud; primary admits DrawHud/DrawScores/Ent_Update. Existing HUD fallback checks actual DrawHud, not merely VM presence. [verified: host.c/sbar.c/gl_screen.c] |
| Allocation lifetime | Native edicts have ED_Retain/Release; ED_Free queues slots only with retain_count zero. Raw reference mapping without retention can alias a freed/recycled slot. [verified: pr_edict.c] |
| Main-thread callback boundary | `_Host_Frame` reads server packets before SCR_UpdateScreen; task rendering joins draw_done (dependent on draw GUI/render consumers) before returning. Preserve this parser boundary and current-VM switching; no callbacks on renderer workers or new task graph. [verified: host.c:1234/gl_screen.c:2569-2588] |
| Final proof | Linux/ARM builds and actual mod/network loss/re-enable/software integration are deferred until full implementation. Live headset and performance measurement user-deferred. [unverified: none run in this slice] |

## Mostly-worked design and open decisions

1. Reuse the existing single recipient activation flag, native frame log/ACK and
   native client VM. Copy primary callback/mapping/wire encoding and QSS-M
   activation semantics, adapting allocation and buffer functions to native
   Mem owners. No second snapshot service, protocol engine, renderer, VM or
   reliability channel. Lean: adopt.
2. Public negotiation: record existing offered PEXT1, intersect server support,
   emit the existing public PEXT1 prefix. Preserve private PEXT1-zero headers;
   require explicit enablecsqc plus replacement-delta capability for private
   custom transport. New clients enable only after an admitted Ent_Update exists;
   old private clients never issue this readiness command. Public custom clients
   need negotiated CSQC support plus replacement ACKs. Lean: this minimal
   split at the existing dialect boundary. Review must verify no public/private
   layout collision or unsupported-peer activation. Considered new private
   protocol/version/offer: reject unless existing command cannot express actual
   readiness. Do not relax private header admission blindly.
3. Loss and packet budget: append custom records to the SAME packet's existing
   frame after native entities; custom-only continuations still carry ordinary
   replacement time/ACK/mandatory selected owner/stat data. Copy removals and
   extended entity index framing, reserve terminator, log only emitted records.
   Temporary lack of packet space retains bits and continues. A payload larger
   than an otherwise empty valid packet must not be cleared as delivered or
   spin indefinitely. Lean: report the incompatible custom payload explicitly
   and retain unsent bits; bound attempts and let native world traffic progress.
   Review whether explicit client error is preferable to persistent skipped
   oversized updates. Primary's drop_oversized branch clears dirty/remove bits
   without emission: reject that demonstrated delivery loss. No fragmentation
   wire extension. Determine custom resume/progress at existing packet owner.
4. Visibility/lifecycle: integrate custom candidates at native snapshot PVS and
   recipient filtering, allowing no-model SendEntity entities. Keep ordinary
   snapshot state for mandatory local prediction owner; review whether such an
   entity can also have custom state without removing its native movement seed.
   Clear SendFlags once after all recipients accumulate, as primary does, not
   once per recipient. SendEntity rejection/hidden/free/transition back to native
   sends removal. Dropped stale updates cannot resurrect an entity after newer
   removal; world reset and re-enable force complete custom reconstruction.
5. Client mapping: copy primary bounded server-index mapping and callbacks,
   retain native edicts while mapped to prevent unrelated recycled-slot aliases;
   clear entry before remove callback and release afterward. If update callback
   frees its entity, next update must allocate a new entity and set isnew.
   Free mapping before VM teardown/reload/map reset; no pointer survives reset.
   Removal callback remains responsible for its reference behavior (no forced
   free if it deliberately keeps a local effect). Restore native default HUD
   when admitted transport program lacks DrawHud. Missing update callback cannot
   skip opaque unlength-delimited payload; fail through native Host_Error.
6. Activation buffer: destination has bounded capability writers, no general
   CL_QueueClientStringCommand helper. Keep enable pending until reliable buffer
   room exists (one native client flag/retry boundary), rather than importing
   QSS-M's unrelated command-queue subsystem or overflowing cls.message. Reset
   on new map/VM. Demo playback does not send client commands. Normal loader
   admission uses primary entry points but does not claim full3D scene support.

Decisions2/6 describe the same readiness lifetime; merging is in bounds.
Decisions3/4 describe packet delivery policy; do not add separate schedulers.
Expected production: roughly400-650 lines across existing server/client protocol,
VM hooks and loader owners. Reopen before exceeding this materially or duplicating
existing snapshot/reliability state. No new mod-specific policies.

## Reviewer contract

Local Astra requested Max: read-only verify-first architecture advisory, no
nested agents, no production edits, no builds/tests/probes. Audit load-bearing
facts, rank decisions by impact, propose deletions/minimal adapters, deep-spec
highest risk, distinguish human choices from technical conclusions. Limit
return to1500 words with file/function evidence and adoption recommendations.
Do not re-review renderer, prediction algorithms, avatars, foveation or unrelated
QC wrappers. Effective model/effort settings must be reported honestly; absent
metadata prevents certification of the senior-skill gate, not fabricated proof.
Main spot-checks, writes disposition and commits adopted plan BEFORE code.

## Smallest end-to-end proof and final acceptance

Actual admitted client program with Ent_Update but no DrawHud connects normally;
SSQC no-model SendEntity writes MSG_EXT_ENTITY payload, client gets a new mapped
entity and then an update, produces visible scripted effect through existing
owners, receives remove. Lose update and removal packets and observe recovery
using normal ACKs; disable/reload/re-enable rebuilds state. Check two recipients
get the same SendFlags generation, callback false/free, reused server/client
slots, extended indices, malformed/badread, demo playback/world reset, exact
packet budget and oversized refusal without ordinary-world starvation.

Run only at final implementation completion: Linux x86-64/ARM builds and actual
normal admission/packet parser/callback/effect checks, ordinary desktop HUD,
public QSS-M/native desktop peers, current private VR, old private clients that
never enable, full movement owner snapshots and voice/VRIK coexistence. No existing checked-in test presently exercises this custom entity transport;
final proof must add actual admission/loss/callback coverage rather than infer it
from private movement tests. Source
inspection/diff whitespace is not end-to-end proof. Do not mark NET-009 or the
full migration complete solely from plans, declarations, mocks or counters.

## Astra advisory disposition before production

Requested local Astra Max returned a verify-first architecture advisory; effective
settings were unavailable, so the formal senior-skill gate cannot be certified.
Main spot-checked primary BeginFrame, native stat/writer initialization, world
reset parsing, ED_Free/retain/release, debug live-reference conversion and private
header admission. No builds/tests/probes ran. The advisory caught real gaps.

| Recommendation | Main disposition |
| --- | --- |
| Begin/retire each packet frame before stats and both entity writers | Adopt primary BeginFrame at the existing native owner, including continuations and non-PREDINFO peers; delete writer-specific reinitialization. |
| Reset retransmission must reconstruct custom state | Adopt: every emitted native world reset clears client mapped state and reflags all current custom state, including already-sent continuations, for complete reconstruction. |
| Free/reuse must retain removal debt even while disabled | Adapt: notify the existing per-client pending flags from SSQC ED_Free. When a new custom entity reuses such a slot, encode remove then full create together in one bounded record candidate/packet; log both debts together, so loss cannot turn a new entity into a stale mapped lifetime. No generation/wire extension. Reconcile resend bits with current visibility/eligibility. |
| Mandatory player owner must not be replaced | Adopt native-only transport for that recipient's player owner. No inherited requirement for a second custom owner stream has been established. Other recipients' custom entities use ordinary filters. |
| Cursor/progress and impossible payload | Adopt one custom scan cursor in the existing packet loop. Retry a temporary shortage with unchanged bits in a fresh packet after mandatory native data. A record that cannot fit that valid otherwise-empty packet explicitly fails that recipient with size/entity diagnostics, preserving ordinary clients/server operation. Reject silent dirty-bit clearing and fragmentation. |
| Retained free mappings are not callable | Adopt liveness checks before callbacks, detach/release after an update frees itself, detach before live Remove callback, cleanup before every client VM teardown. Native ED_Retain/Release remains the sole lifetime adapter. |
| Private/public readiness split | Adopt negotiated public PEXT1 plus replacement ACKs, or unchanged private header plus explicit readiness. Server CSQC support must be enabled. Reset selected PEXT1 on serverinfo, retain original public offer, force private selected PEXT1 zero. |
| Loader, enable retry, reload | Adopt primary callback admission at both native checks and one pending reliable enable flag after Init; no initialization retries. Existing sign-on/map reload only; disable stops future sending but does not authorize unloading opaque in-flight parsers. Existing event predicate and HUD dispatch remain unchanged. |
| Additional renderer/command-queue/full-CSQC policy | Delete from scope; native parser ordering, tasks, VM, assets, GUI and fallback HUD suffice. |

Client decoder/hooks/mapping and reliable enable retry are independently writable
in `cl_parse.c`, `cl_main.c`, `client.h`, `progs.h`, `protocol.h`. One authorized
web GPT coding agent owns only those files. Main owns `sv_main.c`, `server.h`,
`host.c`, `host_cmd.c`, `pr_edict.c` for server integration, loader activation and
free/teardown hooks. No overlapping edits; main reviews and integrates every
agent change. Scope stays NET-009 until its source implementation is coherent;
no declarations-only completion claim.

### Visibility debt separation at the existing pending-word boundary

Before production, main identifies one necessary detail of replay reconciliation:
a dropped-frame replay can occur in BeginFrame after the frame's recipient
visibility snapshot was collected. Old update bits must not make a currently
hidden entity eligible. Add one internal CURRENT marker to the existing pending
word, owned only by native snapshot visibility; never log/replay it or expose
it as a QC SendFlags bit. Frame replay restores delivery debt only (USABLE and
REMOVE), not visibility or PRESENT policy. This prevents a second PVS pass or
parallel eligibility table. Free clears CURRENT while preserving removal debt;
current visible reused slots receive atomic remove/full-create as above.

`pr_ext.c` is added to main ownership for the existing `csqcactive` client-key
query: report the actual recipient readiness flag instead of its hardcoded zero.
No new query/capability owner is introduced.

## Source implementation checkpoint (review pending)

Commit `ad2f0ea2` integrates both sides at existing owners, within the estimated
400-650-line scope (528 inserted production lines). One web GPT worker supplied
the client codec/mapping/hooks/reliable retry; main reviewed those source changes,
added allocation-hook liveness refusal, and integrated server, loader and lifetime
boundaries. The worker's final read-only spot-check was automatically rejected;
main's direct source/status/diff inspection succeeded without that command.

Primary BeginFrame, wire index encoding, SendEntity payload channel, callback
admission and enable/disable semantics are reused with native Mem/edict/VM/frame
owners. The internal CURRENT bit never enters logs or callback flags. Reused
slots encode removal/full-create in one candidate; only complete emitted
candidates enter ACK history. Unfit payloads explicitly fail the recipient,
without a new fragmentation service or silently marking delivery. Native PVS
and recipient filters remain; this slice does not import primary skyroom/PVS
policies or a full-game CSQC scene renderer.

SendFlags is cleared once after all recipients collect the generation, before
mutating SendEntity callbacks, so callback-written next-generation flags survive.
This deliberately adapts primary's after-all-sends clearing at the same snapshot
owner rather than clearing per recipient. Mandatory recipient players always
use native snapshots. Public PEXT1 offers are now selected at the existing
serverinfo owner; selected private PEXT1 remains zero. Event-service admission
is unchanged. Parser absence is an explicit native error for opaque payloads.
Mapping cleanup precedes all client VM edict storage destruction.

Only source reads and `git diff --check` ran. Local Astra advisory source review
of the coherent implementation is pending; no Linux/ARM build, compiler check,
fixture, runtime or headset execution has run. NET-009 remains unqualified
until final normal-session, scripted-effect and packet-loss lifecycle checks.

### Bound reset replay to the existing snapshot-start boundary

Main's integration source inspection identifies a delivery-loop hazard before
final review: after64 unacknowledged packets BeginFrame can retire the packet
that logged world removal. Selected native writing previously checks pending
world removal on every continuation, so a replay can repeatedly reset the custom
scan to1 during one large burst before the host can read ACKs. Comparing cursor
inequality alone does not bound this backward restart.

Adapt selected world-reset emission to the snapshot's first packet only;
continuations keep pending world-reset debt for the next snapshot. Ordinary
native entities already visit entity0 only through the initial snapshot cursor.
This aligns the two paths without another packet quota, scheduler or retry
channel. Every actual world-reset emission still reflags all current custom
entities, and every selected continuation still includes full native owner and
movement stats. Old update bits behind scan cursors likewise wait for next
snapshot, using existing behavior. Expected production correction: one condition.

### Reopen outstanding removal lifetime and prespawn capacity

The follow-up advisory on the implementation closes the ring/reset restart but
finds two introduced defects, verified by main's source trace before fixes.

* Remove-only emission currently clears PRESENT/REMOVE. If lost, a slot reused
  before ACK-gap recovery sends full update without remove, and the retained old
  client mapping receives isnew=false. Atomic encoding alone does not retain the
  outstanding lifetime barrier. Keep one internal REMOVEWAIT flag across emission
  and snapshot/free transitions; record the last emitted removal's existing
  packet sequence per slot. Every update while this boundary remains unconfirmed
  carries remove/full-create together. ACK of that exact emitted sequence clears
  only REMOVEWAIT; it never erases REMOVE delivery debt from a later free/drop.
* Loader currently tries enable before the frame appends prespawn. With12-21 bytes
  left enable fits but prespawn then overflows. Loader should only arm readiness;
  the existing CL_SendCmd bounded retry runs after prespawn. No Init rerun/queue.

Removal-sequence storage shares the existing pending-array capacity, growth and
teardown owner. It caches the existing ACK boundary, not a second generation or
protocol. Compared alternatives: scanning64 frame logs for every candidate update
or ACK adds repeated entity-history searches; forgetting the boundary reproduces
the demonstrated bug; a new wire generation is unnecessary. The smallest
adapter is one extra sequence integer per allocated custom slot and one internal
flag. No changes to ordinary native entities, renderer or client wire format.
Expected correction under60 lines. Request bounded local advisory recheck of
actual ACK/free/drop/visibility/update and sign-on paths after integration.

### Reopen stable lifetime boundary after retry liveness finding

Advisory on `c702d64f` closes prespawn capacity and traces the stale mapping fix,
but finds a new P1: writer advances the exact required removal ACK on every dirty
atomic retry. At normal RTT>one snapshot, ACKn always arrives after n+1 was sent;
WAIT never clears and client lifecycle resets continuously. Main verifies the
exact-equality predicate and unconditional emitted-sequence overwrite in source.
Pause production in this region and reopen the architecture decision, as the
workspace discipline requires after repeated interaction repairs.

Mostly-worked smallest correction: reuse the same pending word and sequence
array as a **stable first-emitted boundary**, not newest retry target. Add one
internal RETIRENEW bit only to distinguish a genuinely subsequent retirement
from a retry of the outstanding boundary. New ED_Free of an entity carrying
PRESENT marks RETIRENEW; live PRESENT becoming hidden/native similarly marks it.
A SendEntity rejection/free of a PRESENT entity that emits remove-only is a new
retirement too. Unsent RETIRENEW blocks an older ACK from clearing WAIT. The
first emitted remove for that retirement sets the boundary sequence and clears
RETIRENEW. Atomic retries retain the boundary. A matching entity removal log
in an ACK packet at-or-after that stable boundary clears WAIT, never delivery
REMOVE or a newer RETIRENEW. A later retirement replaces the boundary when first
emitted, so older ACKs cannot discharge it. Internal bits never reach QC or wire.

Alternatives: freeze sequence with exact equality still fails if first packet
was lost; clear WAIT from any older removal ACK loses a newer lifetime; scan the
whole history for every candidate adds repeated searches; a new wire generation,
reliable channel or another state machine is unnecessary. Main lean is the one-bit
stable-boundary adapter, but local Astra must challenge its necessity and seek
simplification before code. Compare update/visibility/free/replay interactions as
one lifetime owner, rather than adding independent fixes. No user taste decision.

Required source traces: ACKn after atomic retry n+1 while always dirty; lost first
boundary and ACK of later retry; new free before first new removal emission;
new free plus new removal emission before old ACK; hidden/native transition
while an older atomic boundary is pending; callback false/free; dropped-frame
replay of old removal after a boundary ACK. Existing native sequence-ordering
contracts remain the assumption; no new wrap/dialect protocol is introduced.
Expected correction under30 lines plus one pending flag. Scope must stay within
the original native pending/frame owners and under650-line production estimate.
All tests/probes remain deferred; this is a design reopening, not source closure.

## Current authoritative review state

The stable-boundary correction is committed as `39ea8209` and accepted in the
bounded local Astra source advisory below. It supersedes the continuously moving
ACK target in `c702d64f`. Reset-loop corrections `771e6f43`/`7e3c26a5` and loader
prespawn ordering were accepted in earlier bounded source advisories. This
qualifies the reviewed source correction only: there is no NET-009 end-to-end or
full migration completion claim. The user-owned migration document is untouched.

## Stable-boundary design advisory disposition

Local requested-Astra advisory verified unchanged `c702d64f` and adopted the
one-bit correction. Main spot-checks the repeated retirement masks and the
post-callback commit point against its recommendations. Effective settings
metadata remains unavailable; this is not certified source/runtime acceptance.

| Recommendation | Disposition before code |
| --- | --- |
| RETIRENEW distinguishes fresh retirement from replayed REMOVE | Adopt. Shared local retirement-mask helper marks it for PRESENT-bearing free/hidden/native transitions and preserves existing delivery/confirmation flags. |
| Only RETIRENEW installs a boundary, not WAIT-clear or ordinary retry | Adopt. Remove unconditional sequence/WAIT replacement; replayed removal after confirmation cannot reopen WAIT. |
| Callback rejection establishes retirement only after actual emission | Adopt. Live post-callback pending marker or an emitted remove-only result with prior PRESENT classifies fresh retirement at the commit point. No new boundary for a candidate that merely did not fit. |
| Qualifying ACK can be at-or-after stable boundary | Adopt within existing valid-frame/entity-log guards, requiring WAIT and no un-emitted RETIRENEW. Clear only WAIT; preserve REMOVE delivery debt. |
| Sentinel / native reset / new reliability owner alternatives | Reject: sentinel relocates necessary state, native baseline resets do not express opaque callback lifetimes, and no new channel/history scan is needed. |

One bounded coding worker may change only `sv_main.c` and `server.h` with this
specified transition contract; main reviews/integrates and requests targeted
local source recheck. Main works on non-overlapping visibility documentation and
client/reference reads. Source correctness, final actual scripted effects,
normal-session loss recovery, Linux/ARM builds remain unqualified until their
respective gates. Exactly-once lifecycle callbacks or reliable creation are not
promised by this unchanged unreliable reference wire contract.

## Stable-boundary source integration checkpoint

Production `39ea8209` changes only `server.h` and `sv_main.c` (30 insertions,
27 deletions). The main agent inspected the actual helper, ACK guards,
post-callback emission commit, array allocation/growth/teardown and deferred
candidate paths; scoped `git diff --check` passed. One coding worker implemented
the specified transitions and was closed after integration.

The follow-up local requested-Astra source advisory independently inspected the
exact commit and found no actionable P1/P2 in this bounded correction:

| Load-bearing recommendation | Main disposition and actual source evidence |
| --- | --- |
| Fresh free/hidden/native retirement shares one mask helper | Accept: `sv_main.c:1604`, called by free and snapshot transitions, retains RETIRENEW until emission. |
| ACK discharges only the stable confirmed lifetime | Accept: `sv_main.c:1660`, matching retained removal log, WAIT with no RETIRENEW and sequence at-or-after boundary; clears only WAIT. |
| Retry and replay must not continually reopen confirmation | Accept: `sv_main.c:2434`, reads live post-callback bits after candidate copy, installs boundary only for a fresh retirement, preserves it for ordinary retries. |
| Exercise seven adopted transition cases in source | Accept bounded analysis: continuous dirty retry; lost first removal; fresh free before emission; fresh emitted removal before old ACK; hidden/native transition; callback rejection/free; old replay after confirmation. No runtime result is claimed. |

Effective model/effort metadata is unavailable, so this remains an advisory,
not a certified senior-skill pass. Native sequence ordering and valid retained
frame ACK contracts remain assumptions. No builds, tests, compiler checks,
runtime probes, fixtures or benchmarks ran. End-to-end scripted effects and
loss/lifecycle qualification plus final Linux/ARM checks remain deferred until
full implementation. The shared snapshot region is now available for the
separately planned NET-003 design; this checkpoint adds no visibility policy,
renderer work, protocol owner or quad-view rendering.

## Shared callback connection-lifetime reopening

The [NET-003 reopened brief/disposition](snapshot-visibility-2.0-plan.md#reopened-callback-connection-lifetime-decision)
also applies to this stream's SendEntity callback and its two failure callers.
Disconnect can retain a live player body while destroying connection-owned frame
arrays, and QC can replace that client slot with a bot. The stable removal ACK
boundary accepted above remains; edict-only callback source acceptance is not
sufficient for these connection-retirement paths. The narrow planned correction
reuses the existing request dispatch's active/socket guard, captured retained
pointers and native cleanup. No new lifetime/wire/ACK state, frame owner or quad
views. Final software qualification stays after full implementation.
