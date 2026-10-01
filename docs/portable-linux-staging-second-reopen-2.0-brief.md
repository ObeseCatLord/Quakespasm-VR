# C19 staging: second source-derived reopening

2026-10-01. Main verified brief for local Astra xhigh. The first simplification
attempt after0eb04c2a grew the unaccepted draft from763 to973 Python lines;
host-policy.json remains41, with its known resolver spelling defect. Worker
reported incomplete and is closed. No tests/builds/probes/package/SSH execution.
The bound was not honored; do not integrate this draft or treat continued growth
as evidence that the architecture must be replaced.

## Goal, owners and evidence

Portable native Linux x86-64/ARM artifacts with required loaders, complete staged
closure, GLIBC<=2.39, matching notices/source access and static offline verification.
Retain native Meson/install, SDK build/resource owners and isolated transport.
The existing review/plan is in portable-linux-staging-reopen-2.0-review.md and
portable-linux-staging-2.0-plan.md. There is one package.py owner and policy, not
an engine/build-system rewrite. Combined delivery estimate1200 was main's estimate,
not a user-imposed code/time budget. User has not authorized dropping requirements.

[verified: main read all973 lines; scoped source reads only]

| Load-bearing finding | Evidence / consequence |
| --- | --- |
| Some first-review repairs are now present | Explicit headerless table contracts/empty links, correct builder-relative receipts, canonical facts/provenance record, every installed ELF seed, one native install copy and actual local RUNPATH resolution now exist. Reuse these rather than restart. Policy still contains ` libresolv.so.2`; current stage rejects it. |
| Installed source versions are lost | stage_ubuntu523 and verify910 call source_identity on separate source_package field with binary_version fallback, ignoring installed-packages.tsv's actual source_version column. A binary/source revision difference cannot match the builder's direct receipt/source archives. |
| Contributor verification omits runtime-only owners | Stage unions actual runtime provider owners into source_owners704/791/823; verify's expected_owners902 uses only apt roots/dev/static/header ownership. A codec runtime package distinct from its -dev package makes the set check fail. Required set must include actual owned staged runtime providers, with independently verified provenance membership. |
| Offline owner checks consult the current host | owner_map383 resolves an installed receipt path only if Path.exists on the verifier machine. Cross-architecture/offline verification must not derive builder ownership from the verifier's ambient libraries. Source-provider ownership must use retained exact lexical/resolved paths and receipts. |
| Canonical inventory is duplicated by provenance payload hashes | Distro records repeat deb/source/notice/staged hashes already in inventory, and add a second elf_members path/hash list after per-ELF provenance. Read facts and source identity from actual files; keep required dsc checksums/native original receipts, but do not maintain competing manifest hashes or duplicate ELF owner lists. |
| Repeated complete scans/source capture remain | stage does full scans before/after copying, before patchelf, after patchelf, and again for final_inventory. validate_build and pin_inputs repeat selected-source identity checks. stage_ubuntu reindexes every cached deb after each missing download. Remove duplicate work through the existing owners, not hidden modules. |
| Native installed notice completeness is not independent | verify checks actual inventory against whichever files the manifest lists, but does not require all retained install/non-ELF native-artifact receipt paths. Pin/distro notices are checked independently; native C18 notices must also be required against retained install receipts. |
| Source provider checks overreach and reject valid notices | deb_member_hash494–516 reopens an exact deb once per notice/runtime ELF, reconstructing payload membership already represented by installed ownership/source/version/original hash receipts. stage_ubuntu requires copyright to be a regular member of that same binary deb. Official Debian policy permits shared doc-directory symlinks between same-source dependent packages; this route needs explicit owner/source relationship rather than arbitrary text substitution. Actual builder examples remain unexecuted. |
| Compiler version policy is stricter than the contract | abi_needs rejects every nonnumeric GLIBCXX/CXXABI label, though the requirement is to record actual compiler needs, not invent supported symbolic-name limits. Keep numeric/special GLIBC ceiling checks; preserve other actual version names. |
| Unnecessary temporary code/interfaces remain | selected_contributors615–616 has an empty pass loop; provider_source receives unused build_tree; verify_closure receives unused arch; staged hashes duplicate inventory and four derived version lists duplicate version_needs. These are deletion opportunities, not architecture obligations. |

Main independently read [Debian's official documentation policy](https://www.debian.org/doc/debian-policy/ch-docs.html#copyright-information)
and source-based sharing rule: a doc-directory symlink may target a dependent
package from the same source. This is evidence that per-binary regular-member
assumption is too strong, not executed proof of any installed package failure.

## Main candidate and actual decisions

Prefer **consolidation at current functions**, retaining a single inventory and
the native installed adapter. Do not retry another wholesale rewrite with a
smaller number in its prompt. Target a realizable source-based bound after
deleting duplication, not minification or omitted safeguards.

1. One canonical artifact inventory owns staged hashes; per-ELF provenance owns
   original hash and original source path/owner, and measured readelf facts remain
   separate. Distro contributor records reference exact deb/dsc/notice paths and
   binary/source versions/repository, not another ELF list and source hash map.
   Validate source descriptors/notices against actual inventory/retained receipts.
2. One input/source validation owner shared by stage/verify; one post-mutation
   runtime readelf/closure validation; one final file inventory. Preserve all
   existing full native/source receipt guards and actual local dependency checks.
3. Use installed source_version verbatim. Derive required contributor closure from
   retained apt/dev/header/static plus actual original runtime-provider ownership.
   Shared Debian copyright follows exact owning package with matching source and
   installed dependency; carry that provider's matching notice/source records too.
   Never substitute a vaguely related package's notice or rely on verifier host.
4. Remove redundant deb-payload reconstruction **if** exact clean-builder installed
   ownership/version/original hash + exact deb control/source identity + matching
   notice owner/source relationship and complete immutable inventory suffice for
   the accepted consistency contract. This is not an authenticity/signing claim.
   Keep it if review establishes a missing load-bearing guarantee; seek a single
   bounded extraction instead of once per ELF. Do not weaken source access.
5. Revise the estimate only after determining necessary source work. Main lean is
   an800–850 total staging/policy target, reopen before950/new owner, with updated
   combined delivery estimate<=1403. This is a technical estimate, not permission
   to change user scope or hide code. Challenge this estimate and whether there is
   a concrete smaller reuse route preserving every accepted requirement.

Rejected: raising bounds merely to accept973 untouched; third parallel packager;
ldd/LinuxDeploy/AppImage as proof of dynamic OpenXR/source/notice closure; removal
of portable delivery or required ARM client; executing tests to choose architecture
before implementation is complete. Existing Nix/AppImage convenience routes stay.

## Delegation and review contract

Read-only senior review of this candidate and paused package.py/policy plus
builder receipts and prior staging disposition. Workspace only known2.0; no
branch checks, edits, tests/builds/compiler/lint/syntax probes/fixtures/production
scripts/SSH/game runs/telemetry/nested agents. A separate Luna worker owns only
Quake/{protocol.h,server.h,cmd.c,sv_main.c}; metadata is outside this review.

Return<=1400words: verify first; prioritized required corrections; what can be
deleted/consolidated without losing accepted guarantees; realistic revised bound
and smallest native installed-tree proof; technical versus human decisions; exact
unclosed seam if evidence insufficient. Challenge architecture necessity and
duplicated ownership, not merely local correctness. Main owns independent checks,
disposition and bounded before-code refinement. Actual two-architecture staging/
relocation/negative closure/notices/ABI/source qualification remains deferred.
