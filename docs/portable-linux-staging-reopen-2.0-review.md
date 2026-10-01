# C19 staging reopening: senior disposition

2026-10-01. Local gpt-6-astra/xhigh, effective routing independently verified by
main. [Verified brief](portable-linux-staging-reopen-2.0-brief.md)5760a99e;
763-line Python +41-line policy draft remains uncommitted/unaccepted. Main read
the full draft and checked its load-bearing contracts against producer/source.
No tests/builds/compiler/lint/syntax probes/SSH or production scripts executed.

Retain one installed-tree adapter and static verifier; preserve Meson, native
SteamAudio recipe, SDK/resource owners and isolated transport. The draft needs
schema consolidation and actual closure verification, not another packager.

| Recommendation | Main disposition / independent evidence |
| --- | --- |
| Reuse existing install/readelf/patchelf boundaries | Adopted. Main read primary verifier's architecture/SONAME/RUNPATH checks; adapt their substance. Its OpenVR flat names, ldd and fixed compiler floors cannot establish the accepted OpenXR/dynamic-root/full-closure contract. No parallel verification implementation or engine/build-system change. |
| Resolve each NEEDED through its actual staged RUNPATH | Adopted. Main checked inventory310 and manifest edge construction668: current code validates the string but never tests local edge discovery. Share one post-stage/offline-verify closure traversal: expand the allowed ORIGIN path, resolve filename/alias internally to matching ELF, classify only explicit host SONAMEs, reject missing/path-like/conflicting dependencies. No general loader emulator. |
| Require executable and executable-side OpenXR alias independently | Adopted. Main read vr_openxr.cpp:1210–1236: SDL's loader tries the executable directory first. Seed every installed ELF plus that explicit root; verifier must require bin/vkquake and bin/libopenxr_loader.so.1 rather than accept an arbitrary manifest-selected subset. |
| Match receipt paths and headerless table contracts | Adopted. Main read builder273–294 and draft115/124/331/341. Use builder-output-relative hash keys consistently and explicit columns for headerless inventory/owners/links. Empty symlink table is valid. Do not add legacy normalization around this unaccepted format. |
| One JSON-stable file/link inventory and canonical ELF record | Adopted. Main checked malformed runtime comprehension718, tuple/list/dictionary disagreement and enriched-vs-bare comparison727. Keep measured facts separate from provenance within each record, shared by stage/verify. Drop redundant original/staged top-level maps; capture every original hash before mutation, no staged-hash fallback masquerading as original evidence. |
| Parse Debian control fields by name | Adopted. Main independently read [the official dpkg-deb interface](https://manpages.debian.org/bookworm/dpkg/dpkg-deb.1.en.html#COMMANDS): multiple requested fields are labeled and follow control-file order, absent fields are omitted. Current positional assumptions fail. Use named control fields with absent-Source fallback to binary name/version. |
| Handle checksum continuations without a runtime-version assumption | Adopted as robustness, not an executed defect. Reviewer compared CPython3.12 implementations with different initial-newline handling; effective image patch remains unknown. Skip blank continuation lines while retaining strict hash/size/file records and exact source identity. |
| Verify source/notice relationships, not only listed files | Adopted. Main checked source path460 versus verifier743 mismatch. Use package-relative internal paths only; index exact deb/dsc once, reuse native receipts and collect contributors once. Cross-check required selected SDK notices and actual distro contributors with source/deb identities, hashes/notices; bind product revision/hash to retained receipts. No external source references. |
| Delete duplicated copying/inventory and formatting policy | Adopted. Main checked install copies492/493 and whole-file four-byte probes165/271. One internal-link-aware copy, bounded ELF magic reads and common inventory, refreshed after patchelf. Remove repeated contributor writes/unused parameters. Preserve exact version names (rstrip corrupts2.30). Correct resolver spelling; validate exact nonempty unique names, not decorative sorting. |
| Reopen size while keeping total delivery bound | Adopted. Target600–625 lines for package.py+host-policy.json together; stop before650 or another state/ownership model. Other files total453 (builder295, pins28, Docker19, transport63, README48), giving max1103, within the retained1200 combined boundary. Do not minify, hide code elsewhere or drop requirements. |
| Human budget choice | Adapted. These line limits are main's implementation estimates, not a user-imposed budget. Revised size remains inside the existing combined bound and changes no requested behavior or release scope, so main may make this technical choice. Reprioritizing delivery if simplification fails would require user input; do not silently drop C19. |

Before further edits commit this disposition and revised staging plan. Bounded
Luna refinement owns only package.py/host-policy.json: unify schema, consolidate
copy/inventory, share staged closure validation, reconnect source provenance and
verification. Main owns source review/integration; source presence is not release
qualification. Final vertical proof remains actual stage/relocate/offline verify
with transitive libraries/aliases/notices/sources on both native architectures,
negative omissions/ABI cases and meaningful installed software/OpenXR behavior.
