# C02 final publication reopening and implementation contract

2026-10-01. Before-code reopening after the
[current Astra xhigh disposition](final-checklist-current-2.0-review.md),
`3249067a`. The seven-file846-changed-line draft exceeded BEFORE800 and remains
unaccepted. This plan supersedes that estimate and the separate live incremental
fast-path requirement, while retaining all surviving metadata behavior.

Known2.0 checkout only. Main/master/reference checkouts and user-owned dirty
migration-2.0.md stay untouched. No tests/builds/compiler/lint/syntax probes,
fixtures, production scripts, SSH, games or performance measurements until all
required implementation is finished. Source reads/counts/scoped diff-check only.

## Architecture decision and evidence

Retain native stores, Info_SetKey/Info_Enumerate, reliable transport and buffers,
one serverinfo dirty obligation plus bounded slot bits, and the native signon
phase. Use full current-store publication for live server/cvar changes; it clears
removed/newly unrepresentable fields without a second change queue. Compatible
empty-full plus incremental reconstruction remains for conservative full-token
limits. No cursor/snapshot owner, wire syntax or parallel networking layer.

The alternative incremental fast path adds policy without demonstrated surviving
behavior. A general transaction/rollback layer is unnecessary: callback-free
client control sequences can be admitted synchronously, and a private setter
result can protect the existing console persistence flags. Native source and
ordered output remain the behavioral reference.

Verified current readers: full userinfo invokes CL_UserinfoChanged; binary name
then colors invokes Info_SetKey for name, topcolor, bottomcolor. Info_SetKey removes
before insertion refusal. Project and pinned QSS-M client stores are8192; pinned
QSS-M server reverse userinfo is1024. Native CSQC loading clears/reloads its VM;
retrying CL_LoadCSProgs every pressure frame would be a regression. Public PREDINFO
and private0xe9 defaults already satisfy the old eligibility gate, but the explicit
directional declaration must also work without PREDINFO.

## Exact worker ownership and bound

One implementation worker owns only Quake/server.h, sv_main.c, client.h,
cl_main.c, cvar.c, host_cmd.c, host.c, including the existing unaccepted draft.
No concurrent source worker. Main owns docs/source review/integration. Preserve
all other work; do not revert others. No staging/commits/nested agents/branch work.
Target1000–1200 aggregate additions+deletions versus HEAD, stop/reopen BEFORE1300
or any new persistent queue/protocol/module/transaction owner. Do not minify or
move production work outside the bound. These limits include the prior846 lines.

## Required server refinement

1. Metadata eligibility accepts active recipients with explicit supported QSMI
   reader declaration OR legacy PREDINFO. Reader capacity still depends only on
   the directional declaration, never profile/prediction/reverse capacity.
   Ordinary fallback name/colors remain for other peers. Avoid redundant immediate
   native updates for explicitly declared recipients once dirty publication owns them.
2. Notify slot occupation at SV_ConnectClient after native initialization and
   SetNewParms effects, before/with SV_SendServerinfo. Retain retirement after
   disconnect QC and unchanged frags; current stores win on reuse.
3. Construct one ephemeral public projection, excluding private underscore and
   quote-unrepresentable fields, retaining stars/quoted LF and whole custom fields.
   For userinfo skip raw name/topcolor/bottomcolor and use authoritative current
   native name and decimal color nibbles instead. Inactive slots have empty native
   name/zero colors and no old custom data. Preserve all representable custom keys;
   do not discard fields to fit canonical replacements.
   Put those authoritative fields in the wire projection too; retaining them only
   in simulation makes every full snapshot temporarily zero client colors and
   perform needless skin translations before binary restoration. A quoted native
   name alone is omitted from quoted wire data and restored by the binary name
   companion. Decimal colors always remain in the wire projection. Build complete
   expected canonical data, derive representable wire data, then simulate ordered
   companions. Both full and incremental reconstruction enumerate that wire data.
4. Native Info_SetKey scratch plus exact Info_GetKey comparison can check each
   required insertion/removal; do not use growth as success proof for replacement.
   Build the expected canonical store, including quote-containing binary name;
   compare after exact ordered receiver name/topcolor/bottomcolor overlays.
   Simulate every intermediate store for full and reset-plus-incremental paths.
   Failure to retain a required value/custom field is permanent incompatibility,
   handled at the existing native recipient sender, not inside QC/cvar callbacks.
5. Both wire paths enumerate that same canonical public projection. The binary
   companions require explicit combined remaining-space accounting before append.
   Include opcode/NUL/slot/name/colors in aggregate fit. Full-token/text limits,
   conservative per-field incremental limits, recipient message.maxsize,
   temporary backpressure and native drop headroom retain the reviewed guarantees.
   No scratch-overflow reproduction is assumed merely from the old missing guard.
6. Preserve all current dirty lifecycle obligations. Initial serverinfo precedes
   signon2; current slot table precedes3. Native spawn clears/QC rearm and fastload,
   mid-signon changes, map resets and retired/reused occupants remain correct.
   Keep native reliable sender exits and DONE/FLUSH ordering. Source-review that
   no phase can deadlock at an empty logical buffer or append partial units.

## Required client/control refinement

7. Extend the existing pending signon reply integer for bounded begin and prespawn
   commands; reuse CL_BuildSignonReply/CL_TryAppendSignonReply serialization and
   CL_SendCmd retry. No new queue or copied pending commands. Name remains before
   prespawn; color/current-userinfo/spawn remains complete. Begin must also fit or
   remain pending. Complete framing is opcode plus terminating NUL.
8. At the existing host prespawn owner, retire cl.sendprespawn before calling
   CL_LoadCSProgs once, as native code does. After loading, request pending prespawn
   through a narrow client helper, checking current connection/signon state in case
   initialization disconnected. Block enablecsqc while prespawn is pending; append
   it only after prespawn admission. Do not repeat VM clear/load/init on pressure.
   Existing bytes can drain via CL_SendCmd while the control obligation remains.
9. Retain single64000-byte backing allocation, initial/reset logical1024 and
   accepted-header NQ8192/Fitz32000/RMQ/FTE64000 selections. Preserve empty receipt
   or updates-only initialization and native cl cleanup; no duplicate receipt owner.
10. Reuse prospective live cvar/control/setinfo preflight and before-mutation
    rejection. Add a private Cvar_SetQuick implementation result used by Cvar_Set_f;
    public Cvar_SetQuick remains void and native callers unchanged. Return refusal
    only for failed new live admission, allowing existing successful/no-op/locked
    native wrapper behavior. Do not apply seta persistence flags after refusal.
    Keep assignment/default/autocvar/serverinfo/userinfo ordering and no effects on
    refusal. Current reachable set/seta wrappers have no setfl flag additions;
    setfl parsing remains unregistered and no new command is introduced.
11. Preserve whole synchronous name/color/legacy-color wrapper preflight, actual
    committed values including deletion, callback refusal, reverse underscore
    behavior/star rejection, individual tokens/text and local unchanged/retry
    diagnostics. No general rollback or eventual retry promise for live settings.
    Retain the earlier Astra live-admission decision: Info_SetKey's prospective
    resulting value is authoritative even when remove-first capacity/validation
    refusal makes it empty. Do not add requested-equals-result refusal at these
    client preparation helpers; a native cvar string may differ from its info-store
    result. Empty-result commands transmit deletion. Exact retention is required
    separately for server canonical projection and ordered receiver overlays.

12. Main source review found a native desktop-demo boundary: cl_demo.c:813 sets
    demoplayback/connected, and cl_parse.c:3693 still invokes CL_SignonReply.
    Live reply preflight must not disconnect playback because a local name or
    userinfo field is unrepresentable. At the existing TryAppend owner, retire
    pending reply without formatting/enqueue when demoplayback is true. Keep
    accepted-header logical selection, native once-only CSQC loading, signon
    progression and case4 loading completion. Existing CL_SendCmd demo clearing
    stays native. This is preservation of built-in desktop demos, not VR/extra
    demo functionality; include it in the unchanged aggregate bound.

## Handoff, main review and final acceptance

Report scope_done, exact changed files/aggregate counts, source/diff verification,
assumptions, open risks and followup; relinquish ownership. Report missing evidence
instead of inventing policy or broadening the write set. Main must read the whole
coupled patch and native consumers before integrating it and closing C02 source.
Any overrun reopens the estimate/design before additional production edits.

After all implementation, the consolidated Linux/ARM pass must exercise real
signon/reliable/parser/QC consumers without phase skips: pressure with name fitting
but prespawn not fitting, blocked begin, once-only CSQC load, refused seta flags,
declared metadata without PREDINFO, all slots/QC-intercepted occupation, empty/
updates-only metadata, near-full canonical ordered overlays, private/star/quote/LF/
removal cases, incompatible peers, both profiles, map/spawn/fastload/retire/reuse
and actual native scoreboard/skin/movement state. Native graphics/VR/co-op/audio/
assets/delivery qualification and final Astra evidence review remain required.
