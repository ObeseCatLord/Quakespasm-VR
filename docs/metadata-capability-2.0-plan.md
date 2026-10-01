# C02 slice1: directional reader declaration in native pext

2026-10-01. Before-code plan after the committed
[local Astra disposition](metadata-capability-reopen-2.0-review.md),c42cc32d.
No new command, ACK, FTE bit, metadata syntax or selected gameplay profile.
Writable2.0 only; no branch checks, tests/builds/probes/SSH/game runs.

Exact write set: Quake/protocol.h, server.h, cmd.c and sv_main.c. Packaging is
paused/unaccepted and outside this worker's scope. No other engine writer.

- Add independent pext key PROTOCOL_QSVR_METADATA, four-character QSMI, and
  QSVR_METADATA_VERSION=1. Document its server-to-client contract: full command
  text<=8211, full token<=8191; ordinary ui/svi<=2047. These are capacities,
  not an allocation request or inference from PREDINFO/private/VR mode. Do not
  alter the stuffed-command readers or public extension bits.
- Add unsigned offered_metadata to client_t next to the other offered attributes.
  Include the key/version pair in BOTH native ordinary and explicit legacy pext
  replies. Keep existing cls.offered_qsvr behavior and profile selection unchanged.
- At SV_Pext_f, compute this offered attribute from the original complete numeric
  reply at the existing owner before recording it. The new attribute requires
  an even key/value count, every relevant token fully consumed as an unsigned
  base0 integer within UINT_MAX, exactly one QSMI pair and exact version1.
  Reject signs, overflow, dangling pairs, duplicated/conflicting QSMI, trailing
  extra text or unterminated/quoted spellings for this declaration. Ordinary
  native replies already use unquoted hex numbers. Use strtoul/end/range/errno
  and compare consumed spelling/length against existing Cmd_Argv with Cmd_Args;
  no second general tokenizer or stored argument array. A small private admission
  helper returns supported version or0; it does not modify the FTE/profile loop.
- Record only after complete validation, default0 otherwise. Preserve existing
  legacy attribute interpretation; a malformed declaration must never activate
  larger readers by a narrowing alias or valid-looking prefix.
- Reset offered_metadata when native extension negotiation is disabled/forgotten,
  and on new connection initialization alongside offered_* (memset already resets).
  Preserve it across ordinary map signon when pextknown remains valid. Do not
  gate it on sv_qsvr_private, selected private layout, desktop or headset mode.

This slice declares the receiver contract only. Sender projection/serialization,
native reliable limits, userinfo eligibility, permanent-oversize outcomes, dirty
delivery/sign-on/drop lifecycle and empty-receipt handling are separate coupled
implementation slices; C02 remains open. Unknown recipients retain conservative
token1023/text2046 limits when the sender is implemented. Reverse-direction
setinfo remains its existing ordinary reader/token envelope.

Target60–90 changed lines total; stop before120/new file/second negotiation owner.
Main source review and scoped diff checking only. No tests/builds/compiler/lint/
syntax probes/fixtures/script or game execution. Final qualification covers both
replies, native public/private/desktop/VR peers, unknown stock keys, malformed and
duplicate declarations, map/connection reset and sender use of actual limits.
