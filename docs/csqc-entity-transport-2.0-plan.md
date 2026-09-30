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
