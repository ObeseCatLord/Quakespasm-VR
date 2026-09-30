# Inherited client/server QuakeC events

## Verified brief for the design review

Goal: restore inherited CSQC `sendevent` 359 and server `CSEv_*` callbacks on
the existing vkQuake message and VM owners. Solo-maintainer port, no new RPC,
transport, negotiation or scheduler. Linux and Linux ARM software qualification
is deferred until implementation is finished; Windows and live play are later.

| Environment fact | Evidence |
| --- | --- |
| Writable destination is `quakespasm-2.0` on `2.0`; primary and donors are read-only. | [verified: current worktree/status and existing migration scope] User's dirty `docs/migration-2.0.md` is excluded. |
| Primary has the whole vertical: CSQC 359, opcode `clcfte_qcrequest` 81, typed writer and `SV_ReadQCRequest` resolving `CSEv_<name>[_<types>]`. | [verified: primary `Quake/pr_cmds.c:6261`, `protocol.h:466`, `sv_user.c:1912`] |
| QSS-M has the same standard opcode/dispatch, with a terminator-checked seven-argument reader and additional numeric types. | [verified: QSS-M `Quake/sv_user.c:705`, `pr_ext.c:6093`, `protocol.h:411`] |
| Destination has neither opcode, reader nor builtin; it already has reliable `cls.message`, `MSG_*`, temporary QC strings and normal server client-message dispatch. | [verified: destination `protocol.h:385`, `sv_user.c:1908`, `common.c` and `pr_ext.c`] |
| Entity numbers use two bytes normally, three above 32767 with replacement deltas; sender and recipient use their negotiated pext2. | [verified: destination `MSG_WriteEntity`/`MSG_ReadEntity`] |
| Native `MSG_ReadFloat` is bounded; strings consume their full wire content and truncate to their destination buffer. QC temporary strings truncate at 1023 characters. | [verified: destination `common.c:1467`, `1512`; `pr_ext.c:107`; `progs.h:223`] |
| Direct `PR_ExecuteProgram` expects a QC body, not a negative builtin statement. | [verified: destination `pr_exec.c:310`; entry FIXME and `PR_EnterFunction`] |
| Primary/QSS-M reader can reach a callback without checking `msg_badread` immediately before execution. Their writer can serialize missing actual arguments from stale parameter globals. | [verified: actual loops/dispatch; argc is not bounded in either writer] |
| There is no verified standard peer-support advertisement for opcode81 in the inspected primary/QSS-M path. Native CSQC-lite loads local program files. | [verified: donor writer/reader and destination `host.c:956`; unknown: guarantees for arbitrary third-party servers] |

## Options and proposed decisions

1. **Reuse the paired inherited protocol.** Copy the writer's argument layout and
   QSS-M's terminated reader structure, adapting to native helpers. Reject text
   commands (lose typed arguments/callback contract) and a new RPC owner (duplicates
   working message, VM and lifetime policy). Existing desktop/VR clients use the
   same event wire; normal movement/voice/pose traffic is unchanged.
2. **Preserve explicit-call peer policy.** Connected CSQC calls send like primary;
   no events are automatically emitted. Do not infer support from an unrelated
   protocol bit or add a handshake solely for this standard inherited call.
   Mods using the call require a compatible server. Block disconnected/demo
   writing. Review must challenge whether demonstrated evidence requires more.
3. **Restore the complete primary payload set first:** string, float, integer,
   vector and entity (`s/f/i/v/e`), six sender arguments. Receive up to seven,
   matching QSS-M's bounded reader. Additional QSS-M 64-bit/double/unsigned tags
   are outside this inherited interface: destination's existing wide-number
   helpers have unchecked reads/unsafe shifts, so exposing them here would require
   a separate codec change. This slice must not silently accept unknown framing.
4. **Preflight before appending.** Keep primary's first-six-format-character limit
   and ignored unknown format characters. Require actual arguments for recognized
   characters; compute the complete message size against remaining reliable
   capacity before any write. Validate entity references before pointer conversion;
   use primary's `entnum` field or valid local-edict-number fallback, clamping an
   invalid/nonfinite/unrepresentable server number to world. No staging queue or
   retained packet buffer; reuse native `MSG_*` for serialization.
5. **Decode fully before callback.** Copy known values into existing parameter
   globals and temporary strings. Unknown tags, excess arguments, truncation,
   absent terminator or overlong event name fail the current client message without
   entering QC. Preserve native temporary argument-string truncation; don't turn
   that inherited contract into an unbounded string service. Out-of-range entity
   numbers become world as in both references. Build the callback name without
   truncation, find it using native `ED_FindFunction`, and invoke ordinary QC bodies
   only, with server time, requesting player's self and actual argc. Keep native
   host-client restoration around the call; no new callback context owner or
   declaration/signature policy. Unknown handlers retain the inherited client
   diagnostic. Do not require spawned state: reference events can run during
   signon. Review whether this requires a narrower lifecycle qualification.

## Adopted local Astra Max design disposition

Main spotchecked the review's load-bearing claims in `PR_EnterFunction`,
`PF_dropclient`/`PF_spawnclient`, `SV_DropClient`, `SV_ConnectClient` and the native
entity/string codecs. No human decision is required. The adapter remains three
files, with no new owner or handshake.

| Recommendation | Disposition |
| --- | --- |
| Native paired adapter, explicit compatible-peer responsibility, primary five types and signon callbacks. | Adopted. No automatic request traffic; connected/non-demo writer only. |
| Clear all parameter slots before decode. | Adopted: zero `MAX_PARMS * 3` words. `PR_EnterFunction` copies declared parameters regardless of actual argc; a zero-argument name can otherwise select a typed-name handler and read stale parameter bits. No new signature policy. |
| Detect callback retirement and slot reuse. | Adopted: enclosing reader snapshots requester/socket, restores `host_client`, and returns success immediately if the slot becomes inactive or its socket changes. Avoid double drop/accounting and subsequent commands applied to a reused slot. |
| Detect event-name truncation before lookup; preflight missing physical arguments and every serialized byte. | Adopted: compare consumed name bytes against stored length plus NUL, check actual argument positions despite ignored format characters, and preserve queued bytes on writer rejection with a diagnostic. |
| Preserve primary entity-field/fallback behavior with validated references and codec ceilings. | Adopted: world for invalid references/numbers; ordinary unsigned-short ceiling 65535, replacement-delta ceiling 8388607; use actual sanitized encoding length. |
| New RPC/queue, declaration-validation layer or wide-number codec changes. | Rejected as unnecessary for the complete inherited five-type vertical. |

Add stale-parameter/zero-argument-name aliases, self-drop, drop-and-slot-reuse,
signon and subsequent-command cases to final acceptance. Review was source-only;
no builds, tests, probes or performance measurements were performed.

The framing and callback decisions overlap and should be reviewed together.
Current lean is the minimal paired adapter plus demonstrated bounds checks,
not a new negotiation/VM architecture. [unknown] Runtime interoperability and
callback lifecycle behavior remain untested; unverified behavior is not evidence
that replacement is required.

## Files and implementation stages

- `Quake/protocol.h`: standard opcode81 only, no new protocol bit.
- `Quake/pr_ext.c`: CSQC-only 359 wrapper on the existing registry/message owner.
- `Quake/sv_user.c`: one private reader called from existing client-message switch.
- Record the personal local Astra design disposition before production edits.
  After implementation, obtain a bounded source review of the actual paired diff.

Expected production scope: one writer, one reader, one registry entry and one
switch case, roughly 180-240 lines. Reopen if the change requires a new transport,
state machine, VM execution layer, persistent packet cache or broad codec rewrite.
Do not re-review predictive movement, renderer, private protocol, other builtin
gaps or unrelated wide-number codecs in this design review. Prioritize concrete
behavior/framing/lifetime findings over generic network advice. Ask human questions
only for decisions evidence and established user scope cannot resolve.

## Final acceptance

After implementation is finished, Linux/ARM software verification must exercise
actual sender-to-parser-to-`CSEv_*` effects, not merely packet bytes: zero arguments,
each primary type, mixed/six-argument calls, server `self`/time/argc, string lifetimes,
entity field/fallback/world behavior, absent handlers and ordinary desktop/VR peers.
Negative cases include forbidden SSQC359, disconnected/demo calls, missing actual
arguments, full reliable buffer without partial append, truncated each payload,
missing terminator/name, unknown tags, excess args, long names and a builtin or
zero-statement callback. Consecutive requests and subsequent ordinary messages
must preserve framing. Native temp-string truncation must remain explicit.

Source review and planned checks do not establish full mod/netcode parity. Live
multiplayer/hardware play remains user-owned; software qualification is required
at the end of the implementation goal.
