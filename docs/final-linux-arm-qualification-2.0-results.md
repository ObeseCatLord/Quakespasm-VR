# Final Linux/ARM qualification ledger

2026-10-01. In progress; goal remains incomplete. Source entry snapshot
`8e48f41c9932a5475c1228b9143e80c01bccbb07`, immutable git archive. Final checklist
and all185 feature dispositions remain authoritative; narrow results below do
not close the full scope. No deployment or changes to main/master/assets.

## Local full compilation: first finding and bounded correction

Meson debugoptimized, required SDL3/WAVE/MP3/FLAC/Vorbis/Opus and CURL, host
GCC16.2.1/Vulkan1.4.357. Steam Audio disabled for this preliminary compile;
full Steam Audio4.8.1 portable native builds are separately required.

After the first correction, compilation found the copied entity-enumeration
fingerprint cache uses the donor VM field qcvm->crc, absent in vkQuake. Native
progs.h owns unsigned-short progscrc, initialized from the entire loaded file
in PR_LoadProgs before byte swapping. Smallest correction: use that existing
native field at both cache comparison/assignment; retain pointer/CRC cache,
fingerprint, SSQC guard and traversal. Two expression substitutions, no new
VM identity owner. Actual loaded-QC reload behavior still requires qualification.

First build stopped at gl_model.c:4828/4829 under native warnings-as-errors:
the inherited Copper axe cache passes const input to ReadLongUnaligned(byte*).
The helper only memcpy-reads into a local int. Smallest correction: accept
const byte* at that existing helper, retaining all callers and byte-order logic.
One-line signature change, no casts, replacement parser or warning suppression.
Rebuild the full configuration after the correction; portable results cannot
qualify a newer tree until that tree is built too.

## Active native portable builds

Local amd64 and Foundry native arm64 container routes launched from the same
immutable entry archive and committed production packaging recipes. Both use
pinned Ubuntu24.04/dependency recipes; builds, staging, relocation and negative
artifact checks are pending. Foundry source/output is isolated under /tmp,
without replacing its server or assets.

## Native metadata qualification

One verified gpt-6-luna/xhigh worker owns the new native publication fixture,
runner and bounded receipt only. It must exercise actual sender/parser owners
and report phase/resource boundaries; older prepared-signon fixtures are not
accepted as complete C02 lifecycle evidence. Result pending.
