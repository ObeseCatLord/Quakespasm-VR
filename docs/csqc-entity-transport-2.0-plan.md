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
never enable, full movement owner snapshots and voice/VRIK coexistence. Source
inspection/diff whitespace is not end-to-end proof. Do not mark NET-009 or the
full migration complete solely from plans, declarations, mocks or counters.
