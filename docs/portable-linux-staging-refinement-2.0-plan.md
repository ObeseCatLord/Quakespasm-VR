# C19 bounded refinement of the existing staging draft

2026-10-01. Before-code contract following
[the second local Astra disposition](portable-linux-staging-second-reopen-2.0-review.md).
Supersedes the600–625/650 estimate; current973+41 draft remains unaccepted.
Retain original staging guarantees, native build/install/resource/transport owners
and exact CLI: stage BUILD_OUTPUT EMPTY_PACKAGE_DIR; verify PACKAGE_DIR.

Exclusive write set: Packaging/Linux/package.py and host-policy.json. Reuse
current functions; do not wholesale rewrite, add modules/frameworks or hide code.
Target950–1000 total physical lines across BOTH files; stop BEFORE1050/new owner.
Other453 lines give target1403–1453, reopening1503. These are main estimates,
not permission to omit scope. No tests/builds/compiler/lint/syntax probes/fixtures/
production scripts/SSH/game runs/telemetry/commits/docs; source reads, wc and scoped
git diff --check only. Count prospective changes before crossing the bound.

## Exact corrections and consolidation

1. Fix header list/tuple comparison, resolver spelling and exact nonempty unique
   host names. Keep explicit headerless columns and valid empty symlink table.
2. Remove duplicate pin identity validation in validate_build; use pin_inputs
   as shared source-identity/notices owner. Preserve every original input hash,
   native file/link guard and exact selected commit/archive/notice relationship.
3. Make installed_row/source handling use its separate source_package and
   source_version columns (binary fallback only when genuinely absent); named
   deb Source fields still use native source_identity. Never substitute binary
   revision for an available source revision.
4. Remove ambient Path.exists/realpath from offline owner_map. During staging
   preserve actual lexical and resolved paths in canonical distro ELF provenance.
   Offline verification validates their recorded ownership against installed
   file-owner receipts without touching those absolute host paths. Use actual
   exact ownership and versions, never a manifest-only owner declaration.
5. Contributor closure includes apt roots/dev/static/header plus independently
   verified original runtime-provider owners and shared-notice-provider owners.
   Stage and verify derive the same set; unowned/ambiguous/missing contributors fail.
6. Canonical manifest: one file/link inventory, per-ELF measured facts plus
   provenance with original hash/source path(s)/owner, contributor records with
   binary/source versions/repository and exact deb/dsc/notice relationships.
   Remove staged_sha256, copied source archive hash lists, per-owner architecture,
   per-owner elf_members and ad-hoc per-ELF source_paths arrays. Inventory owns
   staged hashes; native receipts and .dsc own their independent original checks.
7. Retain exact deb payload binding for original distro ELF and copyright bytes.
   Group required regular members transiently from canonical ELF provenance and
   notice records, stream each exact deb once and compare their hashes. No second
   persisted member registry; fail absent/nonregular/mismatched members. Index
   cached deb/dsc once, adding only newly downloaded files rather than rescanning
   the whole cache. Exact binary/source/control identity and arch remain required.
8. Shared doc directories: staging resolves /usr/share/doc/BINARY/copyright to
   its actual regular file/owner, validates matching installed source/version and
   same-source dependency, and records the requested doc path, real notice path
   and exact provider package. Carry that provider's deb/source/notice too. Offline
   verification uses retained paths and exact deb control/dependency/regular notice
   member relationships, never the verifier's host. No general apt dependency
   solver; accept only this existing documented same-source doc-sharing boundary.
9. Verify independent receipt completeness: retained receipts.sha256 and
   sources.sha256 require all original entries and hashes; additions from closure
   are allowed but original entries may not disappear. Map every install/ native
   artifact entry into package paths: non-ELF bytes unchanged, installed links
   unchanged/internal, installed ELFs present with matching original provenance.
   Selected deps ELF original hashes match native receipts; do not require all
   SDK tools/headers to ship at runtime. Missing native installed notices/ELFs
   must fail even if the manifest was edited to omit them.
10. Reduce scans using validated installed inventory for initial ELF seeding and
    targeted original-byte checks before patchelf. After all mutations do one
    final inventory and fresh complete ELF/closure validation. Do not remove
    pre-mutation hash guards or final discovery. Remove direct final_inventory
    duplicate scan, derived ABI lists, empty contributor pass and unused params.
    Keep exact version_needs; validate GLIBC only under original numerical/special
    policy, recording actual GLIBCXX/CXXABI names without invented restrictions.
11. Host shadowing: reject local bin/lib entries or aliases with host-owned
    SONAME names regardless of the target ELF's own SONAME, even before classifying
    a NEEDED edge as host-owned. Preserve actual relative-path edge resolution,
    unmanifested file rejection, original/provider conflicts and architecture.
12. OpenXR alone uses RUNPATH $ORIGIN:$ORIGIN/../lib; keep its single physical
    loader in lib and relative executable-side alias in bin. All other bin ELFs
    retain $ORIGIN/../lib and other lib ELFs $ORIGIN. Policy owns this exception.
    Shared closure validation checks both canonical loader path and explicit
    bin/libopenxr_loader.so.1 load context, expands only the permitted paths in
    order, and requires identical matching bundled providers. Verify all aliases
    internal; no canonical-path assumption, second loader copy, engine change,
    global LD_LIBRARY_PATH or general loader emulator.

Keep product revision/archive hash bound to retained receipts, all pinned actual
notices/source archives, native/C18 installation, package-relative source paths,
exact .dsc source/version/checksums/size records (blank continuations allowed),
required bin/vkquake/executable and explicit OpenXR root, one readelf facts parser,
same-tier provider conflicts, numeric GLIBC ceiling and independent full closure.

Report complete/incomplete honestly; no accepted-source or executable claim.
If the bound or narrow shared-notice/closure contract cannot be satisfied, STOP
with exact missing seam and current counts; do not write beyond it or silently
drop guards. Main reviews complete source and integrates scoped commits. Execute
stage/verify/relocation/negative cases and native x86/ARM builds only after all
required implementation is finished.
