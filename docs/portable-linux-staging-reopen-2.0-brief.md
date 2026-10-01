# C19 staging draft: reopen before refinement

2026-10-01. Draft package.py763 lines +host-policy.json41 exceed slice2's550
stop boundary. Committed builder340 +transport/README/Docker113 +draft804 =1257,
above combined1200. Worker relinquished; draft uncommitted, incomplete and
unaccepted. No tests/builds/compiler/lint/syntax probes/SSH or scripts executed.
Main read all763 lines, current builder/transport and primary verifier reference.

## Verified facts and defects

[verified: main source] Established writable quakespasm-2.0 only. Readonly primary
../quakespasm-openvr51b452c0 contains scripts/verify_linux_runtime.sh; its readelf
metadata/architecture/RUNPATH idea remains reusable, but hard-coded OpenVR flat
library names, copied compiler floors and ldd do not satisfy accepted OpenXR/
full-closure/static-verification contract. Existing Meson/Nix owners unchanged.
User-owned docs/migration-2.0.md untouched. No other live agents own these files.

[verified: main direct schema comparison] package.py:116 passes source receipt
keys still prefixed sources/ to a tree-relative comparison. :121 similarly mixes
receipts/ keys with receipt-tree-relative entries. Builder:275/281/292 emits
headerless installed-packages/file-owners/native-symlinks tables; draft:124/331/
341 expects headers path/package/binary_package. Those interfaces disagree.

[verified: main source] verify's runtime comprehension at718 lacks a closing
brace before its for-clause; source cannot be accepted as syntax-ready. Stage
runtime_entries tuples at687 differ from verify's proposed per-file dictionaries.
Stage adds resolved_needed/original_sha256/staged_sha256 to ELF facts at674–677;
verify727 compares fresh bare inventory facts directly with those enriched facts.
It subsequently expects original/staged fields in that fresh bare result. One
consistent schema/shared inventory is needed, not more normalization layers.

[verified: main JSON/code] host-policy contains leading whitespace before
libresolv.so.2 and groups unsorted names; policy_and_arch467 requires lexically
sorted names and fails that exact file. Sorting is presentation, not dependency
policy. Prefer uniqueness/nonempty/exact names with one corrected declarative
table; no new ordering requirement.

[verified: main source] stage492/493 copies install twice via different helpers;
the second helper recreates existing links. Retain one internal-link-aware copy
owner. readelf165/elf_files271 read entire files merely to check four-byte magic,
including large source archives; use one bounded header read and reuse inventory.
ABI formatting223 uses rstrip('.0'), corrupting e.g.2.30 into2.3. Keep exact
required version names, validate numerical floor without changing presentation.

[verified: main source/path arithmetic] stage_ubuntu460 stores source paths
relative to output/sources; verify743 resolves them relative to package root.
Correct one package-relative convention. Repeated owner/source/deb selection and
cache scans offer reuse opportunities. deb_identity392 assumes multi-field
dpkg-deb output is unlabeled lines; verify native documented output before
choosing explicit control-field parsing. [unknown] dsc_records continuation
blank handling also needs scrutiny; do not claim execution proved it.

[verified: main source] provider_for considers prefix/distro tiers; stage seeds
OpenXR and records a dependency closure. Neither stage inventory nor verify
actually resolves every NEEDED through each local relative RUNPATH; manifest
edge records alone do not prove loader discovery. Preserve this accepted gate,
aliases, machine/GLIBC2.39, actual compiler floors, notices and exact sources.

## Mostly-worked decision

Lean retain ONE script and policy over existing install/source receipts, but
simplify to one file/link inventory schema, one ELF reader and shared closure/
verification traversal. Reuse hashlib/pathlib/readelf/patchelf, dpkg/apt and
native receipts; remove duplicate copies/schemas, unused parameters and repetitive
whole-tree scans. No another packager/resource abstraction/architecture-specific
resolver, no weakening source/license or actual runtime closure requirements.
Alternative export/split multiple services would hide growth, not reduce it.

Review readonly, verify then critique; recommend necessary fixes and deletions,
which existing primary/native components remain reusable, whether550 is credible
or a justified revised target<=650 can retain combined1200. Line count is not
permission to minify or remove safeguards. Challenge current design, not only
local bugs. Rank leverage and any genuinely human choices. No nested agents,
telemetry, edits, probes/tests/builds/SSH; output<=1500words exact file-line
evidence, disposition recommendations, bounded coding slice and qualification
limits. Main spot-checks and commits disposition/plan before further coding.
