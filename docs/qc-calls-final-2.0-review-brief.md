# F03 loaded callback/readers review brief

2026-10-01. One bounded acceptance decision, solo operator; no new architecture.
Before-code [plan](qc-calls-final-2.0-plan.md). No production changes proposed.

| Fact | Evidence / status |
| --- | --- |
| Workspace | quakespasm-2.0, only writable 2.0 checkout; main/reference and user migration document untouched |
| Runtime | [verified: dedicated Host_Init branch] skips window/Vulkan/OpenXR/input/audio; no UDP/Steam; disposable writable private profile |
| Owners | [verified: source] actual pr_ext.c named dispatcher and MSG_Read*, pr_exec.c locals/executor, PR_LoadProgs/PR_ClearProgs; existing native fixture bootstrap |
| Inputs | [verified: generated section comparison] all six licensed prefixes/version/CRC/entity width preserved;47 functions/164 statements appended; core/resources/files outputs unchanged byte-for-byte |
| Positive run | [verified: exit-status.json/run.log] strict assertion-enabled current SDL3 host objects compile/link/run0, QC_BINDING_CALLS_NATIVE_PASSED |
| Negative runs | [verified: negative-results.json and separate logs] actual SSQC named readbyte and INT_MIN declaration terminate native dedicated processes exit1, target-specific unimplemented-builtin and Program error diagnostics |
| GPU | Tests stopped after NVIDIA reset-required incident; these processes never create graphics/audio/devices |

The facts above describe the pre-review execution; final strengthened counts
and results are recorded in the disposition below.
Private evidence root: /tmp/qsvr-final-qualification-thchgzi8/qc-calls-current.
Source: tests/qc_binding_program.py optional --calls; native fixture -calls,
-calls-forbidden/-calls-badbuiltin. Existing helpers retain default behavior.
Original programs licensed/private, no source payload exported.

[verified: loaded native assertions] Both VMs invoke named core fabs(-2.5),
lazy min(5,3), string strcat(qc-name), vector normalize(0,3,4), ordinary
two-parameter QC body that itself calls named min, and seven-parameter sum28.
Native parameter metadata, true local sentinels and post-call argc3/8 checked.
Missing/zero/no-argument no-ops each follow actual strlen(return-prime)12,
with separately saved positive witness. isfunction queries body, zero,
missing and unsupported-in-SSQC readbyte declarations as existence semantics.

[verified: loaded native assertions] CSQC reads byte250,char-12,short-1234,
long-1234567, legacy coord-12.25/angle45, modern coord123.125/angle-90,
string fixture-calls-read,float-3.25 from actual MSG_Write* output. Both legacy
flags0 and FLOATCOORD|SHORTANGLE execute. Native remaining byte0x5a is preserved,
subsequent native MSG_ReadByte consumes it; further QC byte read yields-1/badread without
advancing. Message/cursor/error/protocol state is saved/restored. CSQC clear/
reload twice retains server program pointer; SSQC named cases execute again.

Main integrated source-only draft and corrected a fixture collision: an entry
named fixture_calls_missing accidentally was its own supposedly absent target,
causing stack overflow. It now has _entry suffix and zero declaration likewise
stays distinct. Main replaced synthetic return priming, addition-only nesting,
and small out-of-range target with native priming/named nesting/INT_MIN. First
failed positive log retained .initial; corrected positive and both negatives pass.

Decisions: accept this exact subset or tighten observations where weak. Current
lean accepts only stated values/normal argc-local restoration/parser cursor and
native error rejection; whole F03, GUI error recovery, all resource cleanup,
renderer consumers, real socket transport and ARM remain unverified. Replacing
the executor/message parser or making a custom error trap is rejected: existing
owners implement behavior, native dedicated rejection already observable.
Potential overlap: nested callback/argument/local restoration are one boundary;
do not create independent frameworks or duplicate test VM policy.

Review budget: one read-only Astra/xhigh, <=650 words. Verify artifacts before
critiquing; report prioritized actionable oracle/claim gaps with file/line
evidence and minimal changes. Challenge unnecessary architecture/scope. Do not
re-review other nine owners, invent new feature inventory, run builds/tests/GPU,
delegate, edit files, manipulate devices or inspect/export session telemetry.
No human product decision needed unless source exposes one. Return terminal note.

## Final disposition

Main verified both reviewer effective contexts as gpt-6-astra/xhigh. Reviewer
initially stopped because it could not self-verify settings; main supplied its
verified scalar routing and the same reviewer then completed actual read-only
review. No nested review/delegation or reviewer execution. Main spot-checked
the actual trailing-byte call, cl_parse.c3871 event-dispatch boundary, nested
opcodes/saved argc, commutative sum and production unsigned target conversion.

| Recommendation | Disposition |
| --- | --- |
| Narrow parser claim | Adopt: subsequent native MSG_ReadByte consumes sentinel. CL_ParseServerMessage/CSQC_Parse_Event integration is not certified here. |
| Distinguish nested counts | Adopt: after named min(argc3), nested QC invokes named fabs(argc2); outer restoration to3 and native locals still pass. No production change. |
| Observe argument positions | Adopt: QC copies each received parameter into independent witnesses; native C observes both exact two-argument order3,5 and seven positions1..7, alongside sum28. |
| Valid zero-forwarded target is different from no-name no-op | Adopt optional tiny case: real no-parameter QC body returns3 through named call, outer count1. Existing no-name/absent/zero checks remain separate. |
| Establish reused-object provenance | Adopt: retained native ninja -n vkquake argv/log reports no work to do; same current native objects strict-linked. This is native target freshness, not portable/ARM artifact acceptance. |
| Preserve owners/delete overstatement | Adopt: no replacement executor/parser/error trap or source production edit. |

Final affected compile/link/positive run0 and both matching native negative
exit1 checks pass.49functions181statements appended; all original six prefixes,
version/CRC/entity width and core/resources/files modes still byte-identical.
Pre-review logs/receipts retained with .pre-review, initial naming-collision
failure with .initial. Whole F03 and GUI error/resource cleanup remain open.
