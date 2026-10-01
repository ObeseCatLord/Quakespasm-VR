# C19 slice2: installed-tree staging and static verification

2026-10-01. Refines the already reviewed
[portable artifact plan](portable-linux-2.0-plan.md), without changing its host
boundary or introducing an engine/build-system replacement. Implementation and
execution are separate: no builder/verifier execution until all implementation.

## Existing handoff

Slice1's shared native builder emits install/bin/vkquake, installed native/C18
notices, deps/{lib,include,bin}, sources/product.tar[.gz], pinned dependency
archives, recipes, exact Ubuntu source/deb archives and receipts. Native file
hashes, symlink targets, source archive hashes, architecture, revision and the
installed-package inventory travel with it. The requested apt roots are not all
transitive runtime or header/static contributors; staging completes those
receipts at the same clean Ubuntu24 builder boundary. It must not claim a direct
apt list proves complete source/notice coverage.

## One implementation, two commands

Exclusive writes Packaging/Linux/package.py and host-policy.json. CLI:

    python3 package.py stage BUILD_OUTPUT EMPTY_PACKAGE_DIR
    python3 package.py verify PACKAGE_DIR

Run staging in the same provisioned native image, mounting builder output and
an empty package destination. Verification is static and may inspect either
architecture with readelf; it must not execute vkquake or copied libraries.
The flat output contains bin, lib, share/licenses, sources, receipts and an
artifact manifest. Source access may be distributed as a companion archive;
the manifest binds its hashes to the runtime files, architecture and product
revision. Preserve native installed resources/notices instead of reconstructing
them. No game assets are included or modified.

Use one readelf parser for stage and verify: ELF64 little-endian, machine,
interpreter, SONAME/NEEDED/RUNPATH and version needs. Compare all shipped ELF
architecture against the builder target. Check numerical GLIBC requirements
<=2.39, refuse GLIBC_PRIVATE/unknown symbolic GLIBC requirements, recognize
GLIBC_ABI_DT_RELR's2.36 floor, and record actual GLIBCXX/CXXABI requirements.
Do not confuse exported version definitions with version needs or invent a
compiler-runtime floor. Exact host SONAME/interpreter policy is shared JSON,
matching the parent plan; no wildcard host libraries or host-private traversal.

Seed installed executable plus the pinned deps/lib OpenXR loader explicitly.
Resolve NEEDED only from the selected dependency prefix and native Ubuntu
library directories. Selected-prefix providers take precedence deliberately;
within a provider tier reject distinct conflicting files/SONAMEs. Resolve aliases
to the real provider, retain only relative aliases inside the package, and reject
absolute NEEDED or escaping staged links. Each executable-side ELF RUNPATH is
$ORIGIN/../lib; each lib ELF uses $ORIGIN. Patchelf is the sole mutation boundary.
Scan the complete resulting ELF tree, not just the main executable. Verify each
dependency resolves within its local RUNPATH search directory or is explicitly
host-owned. Do not rely on parent RUNPATH propagation or LD_LIBRARY_PATH.

## Ownership, notices and sources

Before mutations validate the existing source/native receipt hashes and symlink
targets. Prefix ELF providers must match the builder's native artifact receipts;
their source set is the selected pinned SDK archives already carried alongside
the product. Do not admit unreceipted prefix files. Copy every selected SDK's
actual notice paths from its exact source archive, including header/static tools.
Check selected commits/source hashes against sources.json/sources.tsv; missing
notices or mismatch fail. Do not silently fall back to arbitrary license text.

For actual distro ELF providers determine dpkg ownership from the original and
resolved paths, require one installed owner and the version in the builder
inventory. Capture its binary version, source package/version, installed
copyright, exact matching deb and .dsc/orig/debian source archives. Complete
installed development/header/static contributors as well as the requested apt
roots and runtime closure; reuse the existing inventory and download cache.
Overinclusive notices/source inputs are acceptable; a missing incorporated
component is not. The runtime provider remains the installed native library,
never a guessed library extracted from an unrelated release.

Keep the product source, complete source-access set, pins/patches and build
recipes. Record original and staged ELF hashes, closure/host dependencies,
source/binary ownership, versions and copied notices in a single manifest.
Static verify re-reads ELF facts and hashes, ensures aliases remain internal and
source/notice receipts exist, and fails on missing, conflicting or additional
unmanifested runtime files. Manifest integrity is an artifact consistency check,
not a signing scheme or proof of in-process driver/runtime ABI isolation.

## Bound and final qualification

Target350–450 lines for the shared Python implementation, host policy additional
declarative data; stop before550/new resolver or framework. Combined C19 reopening
bound1200 remains. Report exact interface/gaps rather than execute verification
or claim packaging works. The later Docker/Foundry wrapper copies these files
into the same image; slice1 remains a source-build owner, not a second packager.

Final qualification includes architecture/ABI/RUNPATH/aliases/missing dependency/
notice/source mismatch cases, required enabled build features, relocation in the
declared runtime-only host boundary and actual supported OpenXR/software output.
Host GPU ICDs, OpenXR runtimes/layers, CA trust and optional SDL service/plugin
configuration remain explicit host prerequisites. No deployment/performance test.
