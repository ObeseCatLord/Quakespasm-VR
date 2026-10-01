# C02: resolve recipient limits at the existing capability owner

2026-10-01. Main verified design brief for local Astra xhigh. Reopen the
[metadata disposition](metadata-publication-2.0-review.md)'s pending capacity
decision, not its native-store/reliable/signon/public-projection design. The
optional stock-QSS-M question remains unanswered. Required product behavior is
this project's desktop/VR cross-play with QSS-M-style prediction; direct stock
interoperability was not explicitly requested. Seek a technical design that
preserves safe public interoperability without needing that optional choice.
Solo project, no new networking layer or wire metadata syntax. No production
edits for this reopening and no executable checks.

## Verified facts

| Fact | Main source verification |
| --- | --- |
| There is already a generic capability tuple owner, not only FTE bits | cmd.c:1124–1140 answers `cmd pext` with hexadecimal key/value pairs, including independent QSVR profile and movement-policy keys. SV_Pext_f in sv_main.c:3672–3722 reads them before pe xtknown/spawn and records offered attributes. |
| Unknown attributes are compatible at the reference server boundary | Pinned QSS-M03a498 sv_main.c:2404–2460 parses key/value pairs, recognizes its FTE keys and ignores unknown keys. It does not require a new command, ACK or selected gameplay profile for an additional tuple. |
| Capability records already survive map signon | client_t offered_* fields and SV_SendServerinfo reset selected protocol, retaining offered attributes when pext remains enabled. New connections memset client_t; disabling extensions forces fresh negotiation. |
| Project desktop and VR share this capability reply and parser | cmd.c, cl_parse.c and cl_main.c dispatch is not gated on headset mode. Private QSVR profile selection is separate and can be disabled. A reader-capacity declaration must therefore be independent of VR/private prediction selection. |
| PEXT2_PREDINFO does not prove large metadata tokens | Stock QSS-M com_token1024, MSG_ReadString2048 and receiving scoreboard8192. Existing server userinfo1024 does not imply arbitrary source metadata fits its receiver. |
| Current project full-reader exception is specific | cl_parse.c:3476–3495 reads fullserverinfo/fui into `sizeof(fullserverinfo_prefix)-1 + SERVER_INFO_STRING_SIZE-1 + sizeof("\"\n")`; every other stuffed command retains2047-byte limit. cmd.c uses8192-byte scratch for command execution; full/update handlers now validate complete quoted commands. |
| Reliable capacity is independent of token capacity | Native NQ8192 versus extended64000 reliable message; opcode/string framing reduces usable payload. Every full snapshot and accompanying native name/colors must fit atomically in an empty recipient message, even for a declared large reader. |
| No current metadata queue or publication registry | Native stores8192, MAX_SCOREBOARD16; the adopted candidate is one serverinfo-pending flag plus16 userinfo dirty bits, with serialization from current stores at the native reliable/signon owner. |
| Initial and spawn boundaries are distinct | PRESPAWN_SIGNONMSG appends native signon and2; Host_Spawn calls Send_Spawn_Info after QC, then immediately appends3. Send_Spawn_Info first clears reliable bytes. PRESPAWN_DONE idle guard suppresses non-signon messages. Fastload must not restart signon. |
| Committed local fixes do not fix framing | 44592006 sizes Info_Enumerate and SV_UpdateInfo scratch to existing stores. Native va still uses MAX_OSPATH or1024, and initial/incremental cvar/userinfo paths use it. A complete-command serializer must not use that bounded prefix as publication evidence. |
| Empty initial serverinfo affects outgoing userinfo | CL_SignonReply case2 currently calls Info_Enumerate only when *cl.serverinfo. The delivery plan must distinguish receipt/support from a nonempty metadata value so an empty legitimate snapshot does not suppress project userinfo initialization. |

Readonly references: primary51b452c018273647dcf94f4628a370267ff8fa91;
QSS-M03a498aabc411e2e739adc815c5536b161b9626e. Known2.0 writable checkout
quakespasm-2.0 only; no branch checks. User-owned migration-2.0.md untouched.
Luna owns Packaging/Linux/package.py and host-policy.json exclusively; all
engine/metadata files are read-only for this review. No tests/builds/compiler/
lint/syntax probes/scripts/SSH/game runs/telemetry/nested agents.

## Main candidate and alternatives

1. **One existing pext attribute.** Lean toward an independent project key
   (e.g. QSMI) with version1, whose documented receive contract is the existing
   full-metadata reader/token sizes. Include in both ordinary and explicit legacy
   pext replies; record at the existing offered-attribute owner and preserve/reset
   with native connection/map policy. No new offer command, ACK lifecycle, FTE bit,
   packet opcode, metadata encoding or private gameplay-profile dependency.
   Strictly validate this tuple before allowing the larger envelope. A peer's
   declaration is its reader contract, not a trusted source of allocation sizes.
2. **Conservative unknown peers.** Without that declaration, keep stock-safe
   token<=1023 and stuffed-command<=2047 limits, plus actual native reliable room.
   Existing fullserverinfo/fui/ui/svi remain the wire syntax. No guessing from
   PREDINFO, engine banner, VR pose negotiation or selected private profile.
   Exact same-build declaration allows the current larger full-reader exception;
   incremental ui/svi still obey the generic2047 limit, using a pending current
   full snapshot when a representable update requires the larger reader.
3. **Explicit permanent failure.** Main lean: a public snapshot that cannot fit
   even an empty declared recipient envelope must produce a bounded diagnostic
   and terminate that incompatible recipient through the native drop path. Never
   silently truncate, omit otherwise representable fields or retry forever.
   Temporary lack of queued reliable room leaves the native pending obligation.
   Quoted-unrepresentable/private fields use the already-adopted public projection
   policy; they are not capacity-driven omissions. Do not kill the server or
   corrupt local QC stores. Review whether a smaller compatible native outcome
   exists that preserves complete metadata, not a prefix falsely called parity.
4. **Scheduling stays native.** Retain serverinfo-before2 and complete current
   slot table-before3, rearm after spawn clear/QC and defer3 through the existing
   sendsignon owner when snapshots need multiple reliable messages. Late changes
   remain current-store dirty obligations; preserve PRESPAWN_DONE idle guard and
   fastload semantics. No copied string snapshots, per-message generations, extra
   queue, alternate signon driver or gameplay prediction rewrite.

Rejected lean: inferring capacity from PREDINFO/private profile; universal1KB
projection which discards project fields; expanding transport limits; inventing
chunked metadata syntax; a second capability ACK family. One pext tuple costs
less state and code than a new exchange and can safely represent both contracts.
Unknown: final all-path implementation size; this is a candidate, not a ready
patch. If requiring complete stock interoperability for a field above1023 cannot
be fulfilled by its parser, say so explicitly rather than proposing fake parity.

## Requested review

Verify these load-bearing facts first, especially stock handling of unknown pext
keys and actual project reader capacities. Challenge whether the existing tuple
solves the technical question without additional user input and whether this is
the smallest architecture. Separate genuinely human choices from native technical
policy. Reconcile the prior no-new-wire-dialect recommendation: the proposal adds
a declaration at an existing owner, not a data syntax or parallel protocol stack.

Return<=1300words: prioritized verdict; adopted/adapted/rejected choices; exact
native capacity, scheduling, empty-snapshot and committed-store/public-projection
seams; a realistic bounded implementation split; concrete unknowns and eventual
observable acceptance. Do not re-audit the185-feature inventory or implemented
receiver/C07/C14 code. Main owns independent checks, disposition and before-code
plans. All executable qualification remains after full implementation.
