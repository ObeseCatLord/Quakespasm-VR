# Current platform artifact cohort

2026-10-02. Shipping source is 0bd4ddb1cf495b4d6f055ad37fcedf275485def9,
archive SHA256 5e8a396158a4f4dbcc94006651023e5b7935912dae25fba4bb385cf4e3f652cc.
Later commits so far change only documentation/tests. Existing native builders,
package ownership and dependency recipes are reused without platform rewrites.

Windows x64 Release and Debug both build/link successfully with strict warnings,
110 shader objects each and inspected/retrieved matching PE outputs;
[exact Windows results and limits](windows-current-2.0-results.md).

Linux x86-64 image/native build/stage/static verify each exit0. The actual package
has45 ELFs,651 inventory entries and118 Ubuntu contributors. Main independently
checks every inventoried file/link and compares all702 regular archived inputs
outside docs/tests against current bytes: zero differences. This includes root,
packaging and workflow inputs, rather than only C/C++/shader sources. Main engine
SHA256 1635eee05109b4e0abf48fc0a47d2a1ed20098b1892d5d605e975f032c7a1783.
Private package/receipts: FastGames/qsvr-linux-0bd4-6fadlwzp; actual coordinator
status.json and main-source-artifact-reconciliation.json retained. First input
mount failure is retained; source build only began after fixing that private mount.

Native ARM build/install and staging completed before the coordinator was
interrupted. Main confirms the existing remote source archive identity/hash,
no owned build/stage process remains, and reuses the staged package rather than
restarting compilation. A fresh read-only/network-disabled AArch64 verifier exits0:
45 ELFs,651 inventory entries and118 Ubuntu contributors. Remote transport archive
creation also exits0. Initial retrieval stalls at998178816 bytes and a new SSH
connection reports Network is unreachable. Only the positively identified owned
scp transfer is terminated; its failure and partial-size receipts remain.

A bounded IPv4-only connection succeeds without system/network configuration
changes. Existing rsync resumes the immutable owned archive with append-verify;
resume/checksum-receipt/transport-SHA256/extraction each0. Transport archive hash
97b0e4216e500ebbcf7d2436ecdbfcc7e8c57d4144d47576cccfdb0f192aca09,
1162956800 bytes. Main independently checks every651 inventoried file/link and
all702 archived regular shipping inputs outside docs/tests: zero differences.
ELF64 little-endian machine183 (AArch64); engine SHA256
d62f9dbf99671fc6bb4293bf2906e2eacf0ac4f8a29b365c558e04fc6cbf3ab2.
Private package/receipts: FastGames/qsvr-arm-0bd4-6yhf1pdy/arm-package,
recovery-verify.log, recovery-status.json, retrieval-interruption.json,
rsync-status.json and main-source-artifact-reconciliation.json. Remote owned
namespace n1cmIf retained; live server, unrelated containers and checkouts remain
untouched. This is current native ARM build/package/static/freshness acceptance,
not ARM gameplay/headset/gaze/audio-device execution.

Current packaged Linux desktop and two-eye native Vulkan/OpenXR graphics checks
resume after the user explicitly reports GPU availability. A bounded desktop run
loads stock e1m1, writes an actual native screenshot and exits0. Main inspects
world, held weapon and HUD. [Exact resumed GPU evidence and limits](gpu-resumed-current-2.0-results.md).
No driver reset/reload, system graphics settings or unrelated runtime changes.

These bounded results refresh platform build/freshness evidence; complete F01–F08
behavior, available graphics boundaries and final local Astra integration signoff
remain on the frozen checklist. Physical headset/gaze and performance measurement
remain user-deferred. This does not close F10 or the whole migration.
