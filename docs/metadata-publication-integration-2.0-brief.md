# C02 remaining publication: coupled native integration brief

2026-10-01. Mostly-worked main design after reader declaration68f8ec51 and
[the recipient-capacity disposition](metadata-capability-reopen-2.0-review.md).
No sender/lifecycle implementation authorized by this brief alone; commit a
resolved before-code plan first. No tests/builds/probes/game runs. Known2.0 only.

## Verified native boundaries

- SV_UpdateInfo in cl_main.c owns local mutation and native name/colors decoding;
  Cvar_SetQuick in cvar.c currently bypasses it for serverinfo publication. Actual
  Info_SetKey removes old values before rejecting replacements; use committed
  stores, not requested strings. Local scratch now8192, but va is still smaller.
- Each client_t owns reliable message/maxsize and current offered_metadata1/0.
  Adopted delivery state is one serverinfo-pending flag plus16 userinfo dirty bits.
  No copied persistent stores, publication generations, extra queue or net stack.
- SV_SendServerinfo initially queries pext, then sends native header/signon1.
  PRESPAWN_SIGNONMSG owns signon2; PRESPAWN_DONE idle guard suppresses non-signon
  traffic. PRESPAWN_FLUSH becomes DONE after one reliable send.
- Send_Spawn_Info clears native reliable bytes after QC spawn and writes native
  scoreboard/stats/etc; Host_Spawn immediately appends3. Fastload separately calls
  Send_Spawn_Info while already spawned, and must only rearm metadata.
- Native updatename/updatecolors mutate received userinfo after a full snapshot.
  Project a final resulting store too: public source fields plus authoritative
  binary native name/colors must fit without silent field loss. Info_SetKey's
  strict inequality permits8190 aggregate characters in its8192-byte store.
- Fullserverinfo costs L+22 wire bytes; full userinfo plus native name/colors
  costs L+18+d+N. Actual recipient message.maxsize governs atomic fit. Unknown
  token/text limits1023/2046; declared full8191/8211, incremental text2047.
- **New main source fact:** CL_Init allocates cls.message with SZ_Alloc1024
  (cl_main.c3554). CL_SignonReply2 adds color, all initial setinfo and spawn into
  that buffer. Reverse server string reader and src_client tokenizer remain
  ordinary; a receive declaration cannot enlarge this outgoing route.
- PMCL_ServerinfoUpdated recomputes current movement values without a new frame/
  protocol owner. Current full/update userinfo handlers call CL_UserinfoChanged
  (name/colors/skin), with native binary scoreboard companions afterward. Empty
  full info followed by all updates invokes intermediate native callbacks; final
  observations must be qualified, not assumed from helper counters.

## Main coupled design candidate

1. One shared projection at the existing serializer: enumerate native source
   losslessly, exclude underscore keys, retain star keys, omit fields containing
   literal quotes, keep native quoted LF. Exact representable fields remain whole.
   Check authoritative native field overlay separately, preserving binary name/
   color output even when a name cannot appear inside a quoted metadata command.
2. One ephemeral whole-unit serializer/preflight using current stores. Prefer a
   fitting full native command; for unknown peers whose aggregate exceeds their
   full-token limit, consider empty full + every existing ui/svi command as one
   reliable bundle, with no persistent field cursor or new encoding. Each token,
   command, resulting store and whole bundle must fit. This preserves arbitrary
   project fields through declared full readers and feasible stock aggregates;
   individually impossible fields remain a visible incompatibility.
3. Mutations publish incremental committed projected values where safe and no
   required full snapshot is pending; quote-invalid replacements remove old
   public values. Otherwise retain native dirty obligations. Incremental success
   never discharges an already-required full snapshot. Private mutations remain
   local. Cvar publication calls the same direct mutation/publication owner without
   recursing through SV_UpdateInfo's existing cvar redirect.
4. Flush from native sender, serializing latest stores only. Append a whole unit
   or retain dirty state under backpressure. Truly impossible unit/overlay emits
   a bounded diagnostic and native recipient drop at the actual host_client,
   with safe disconnect headroom; never drop other clients inline from a QC/cvar
   mutation callback. Dropped-slot QC sees old userinfo before clearing/marking;
   reuse sends latest occupant. Native frags stay unchanged.
5. Initial serverinfo drains before2 at its existing stage. After spawn clear/QC,
   rearm serverinfo/table and enter one added sendsignon metadata-drain phase;
   emit3 only after required units append and2 bytes remain. Successful sends
   leave that phase active while obligations remain; no replacement signon driver.
   Fastload rearms dirty flags without entering the signon phase. Idle DONE guard
   remains intact; post-spawn ordinary sender flushes latest dirty stores.
6. Client records valid fullserverinfo receipt independently of content, resetting
   through native cl state. Use receipt (while considering inherited nonempty-info
   compatibility) to admit initial userinfo after empty snapshots. Complete
   outgoing initial commands require their own native1024 reliable preflight;
   no va prefix or receive-capability assumption.

## Exact remaining decision before code

Resolve initial client publication under its actual1024 reliable owner. Main lean
is preserve its supported reverse-direction envelope with complete preflight and
an explicit outcome for permanently oversized fields/bundle, plus native retry
for queued-buffer backpressure. Arbitrary8KB reverse fields were not established
as inherited wire support. If lossless multi-packet initialization is necessary,
choose the smallest native signon/request owner; do not create another metadata
registry, mutation quarantine or copied snapshot queue merely to avoid the limit.
Do not silently omit configured fields or call a prefix complete initialization.

Resolve whether atomic empty+incremental reconstruction's native callbacks are
an acceptable compatible fallback using source/reference evidence and deferred
observable cases. It is not chunking and cannot make an impossible single field
fit. Required project desktop/VR contract remains intact; arbitrary stock support
beyond its actual parser is not newly required by the optional unanswered question.

Expected one coupled write region: server.h/sv_main.c, cl_main.c/client.h,
cvar.c, host_cmd.c and host.c. Reuse native methods and existing reliable owners;
one implementation worker, target375–500 changed lines/reopen before650/new owner.
Final bounds depend on resolving client initialization; this is not a promise or
authorization to cross them. Packaging worker has disjoint two-file scope.

Final deferred cases: initial/empty metadata,16 large slots across native sends,
both capability profiles and feasible unknown peers, current-buffer pressure,
mid-signon mutation, spawn/fastload clear, drop/reuse, actual native scoreboard/
movement consumers, near-full overlay, privacy/quoted-invalid transitions and
initial client metadata at its actual reliable/token boundaries. All qualification
waits until required implementation is finished.
