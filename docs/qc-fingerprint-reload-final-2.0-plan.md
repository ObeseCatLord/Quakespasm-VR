# Changed-program fingerprint qualification

2026-10-02. Before-code extension of existing F03 loaded entity cases. Reuse
native PR_LoadProgs/SV_SpawnServer/CL_LoadCSProgs and existing generated entities
variants and FixtureEntityBody. No new production policy, registry, loader,
cache, generation service or copied traversal. Native PF_ProgsNeedsActiveClientEnumeration
caches server file pointer/whole-file CRC16; PR_LoadProgs computes CRC/hash/SHA256
on real file bytes and PR_ClearProgs frees/zeros the existing VM. Actual main
reference uses the same four function names and compatibility predicate.

Existing separate processes prove three fingerprints and same-program reload;
they do not prove switching fingerprints in one process. Minimal incremental
qualification: optional -entity-cycle-dir private-directory at the end of the
completed entity case. Copy prepared complete/missing/none/complete program
bytes to the private progs.dat using existing COM_WriteFile, actually spawn the
same stock map, assert real loaded function counts4/3/0/4 and native CRC changes,
and reuse exact loaded find/nextent/extension body oracles for each owner. Record
real pointer reuse if observed; do not force allocator addresses or cached state.
Load/clear actual CSQC after each changed server while preserving server program,
CRC and its query policy. No manually assigned callback, QC return or checksum.
Keep the initially generated CSQC independent; do not overwrite csprogs.dat.

One Luna/xhigh worker owns tests/qc_binding_native_fixture.c only, <=100 added
lines, no other edits/test/build/branch/runtime/delegation. Main owns private
profile/generation, plan/review/qualification. Runtime copy input names are
complete.dat, missing.dat, none.dat, complete.dat under the supplied directory;
only private COM_WriteFile progs.dat output. Enforce complete reads, bounded
length and successful loaded CRC/function evidence. Existing -entities behavior
unchanged without the new option. Report cap/missing evidence before broadening.

Qualification needs assertions, all actual compiler/link/runtime stage exits0,
existing entity marker plus distinct QC_BINDING_ENTITY_RELOAD_NATIVE_PASSED.
Original generated modes unchanged. CPU-only dedicated noudp/nosound/nosteamapi,
private readonly licensed assets, no GPU/device/server deployment. This verifies
normal changed-file fingerprint cache admission, not deliberate CRC collision,
authored Shub round restart/save load or full F03. No production change unless
actual observed results demonstrate an incompatibility; reopen its owner first.
