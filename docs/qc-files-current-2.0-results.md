# F03 loaded file/search results

2026-10-01. No production edit. Main integrated the two-file Luna/xhigh draft,
corrected duplicate read consumption and query operands, kept both writable
owners open for reciprocal foreign checks, and reopened unread streams before
retirement. Optional files mode reuses the existing assembler/bootstrap and
actual native SSQC/CSQC loader/interpreter/extension owners. No second compiler,
filesystem, registry, allocator, renderer or resource manager.
[Before-code plan](qc-files-final-2.0-plan.md).

Current assertion-enabled SDL3 Meson objects reused; native target has no pending
build work. The existing fixture replaces main_sdl/sv_main/cl_demo/cl_parse, with
its two existing Loop_Init/NET_CanSendMessage wrappers. Strict compile/link0;
dedicated/no-UDP/no-sound run0 with QC_BINDING_FILES_NATIVE_PASSED. Host_Init's
dedicated branch skips graphics/input/audio initialization; no window, Vulkan,
OpenXR, microphone or network socket. GPU tests remain stopped after the NVIDIA
fault. No system settings/main/reference/user migration document changed.

Both VMs execute real appended QC declarations through PR_ExecuteProgram:

- Data-prefixed read wins over the mounted fallback. Actual fgets returns
  data-first after CRLF, a nonzero empty string for the blank line, last for the
  unterminated tail, then raw0 EOF. Buffer loading appends to the owner seed,
  empty-file load succeeds without changing size4, and freeing index1 leaves a
  sparse hole. Actual default buf_writefile writes seed/blank/last; native close
  and independent C/Python byte reads agree on both exact17byte files.
- Distinct VM-owned live streams/buffers/searches refuse reciprocal foreign
  reads/closes/writes and buffer-load/write permutations. Both foreign write
  operands are checked separately while files are genuinely writable. Owner
  reads still begin at data-first, contents/snapshots survive, and post-close
  bytes contain no foreign append. Validated native append handles remain open;
  owner fputs appends owner-after-foreign after the foreign close attempts,
  producing exactly37bytes each. Rejected string reads are primed via native
  successful calls and require raw0, not merely empty text. Each checked zero
  float refusal follows an observed native strlen return12.
- Native mounted pack/loose search yields exactly four unique expected names:
  duplicate, packed-only, packed nested and loose-only. Nested loose entries and
  a directory are excluded. No-result search returns-1. Denied parent/absolute/
  colon/backslash paths have actual original and data-prefixed sentinel entries
  in the private pack; both fopen and search refuse them. These bytes are never
  extracted. Closed searches and each NaN/infinity/negative/huge search handle,
  index and file-read/close case are observed individually; owner state survives.
- The shared16search slots fill, a further request returns-1, and closing one
  permits exact slot reuse. CSQC clear/reload retires its old buffer/unread file/
  search before any replacement allocation while SSQC contents/search/unread
  tail continue. SSQC clear/reload likewise rejects its old buffer/unread file/
  search;15new snapshots coexist with the surviving CSQC snapshot and the next
  request refuses. CSQC program/search remain usable through SSQC retirement.
  Both retiring streams have valid handles and successful data-first reads
  immediately before clear, leaving blank/tail unread rather than an EOF oracle.

All six original licensed QC section prefixes remain byte-identical, version/CRC
and entity width unchanged; files mode appends54functions572statements. Original
licensed programs stay private. The core and resources generator outputs remain
byte-identical to their pre-change modes, rather than being unnecessarily rerun.
Both worker effective contexts verified gpt-6-luna/xhigh; worker did no execution.
Main verified actual generated sections and loaded result assertions.

Local Astra/xhigh found three assertion gaps; main verified and adopted all:
nonzero return priming, writable-handle/foreign-close owner sentinel and positive
read witnesses before retirement. [Final dispositions](qc-files-final-2.0-review-brief.md#final-disposition).
Final affected strict compile/link/run0 and explicit exit-status.json agree;
both independently read37byte files match. Initial/pre-review weaker passes
remain separate. No production fix or additional architecture was necessary.

Precise limits: same-VM stale handles after slot reuse have no new generation
guarantee; second-direction counterpart file/buffer survival is not inferred
from its surviving search. Case-fold variants, model-only mounts, all file-error
and optional range/permission/config cases are not certified here. Native
search flags/package filtering retain their documented unsupported semantics.
Callback/entity/surface/error-unwind/authored consumers remain distinct F03
work; no whole F03/F10, Windows or hardware/performance acceptance.

Private evidence: qc-files-current under the retained qualification root:
compile/link/run logs and argv JSON, section-preservation.json,
input-manifest.json, result-reviewed.json, exit-status.json and disposable
licensed profile. Initial/pre-review logs carry .initial/.pre-review. Expected invalid/foreign-handle warnings are
present; ordinary dedicated stock loading also warns about optional MD5 skins.
No assertion or native fatal error occurred in either executed proof.
