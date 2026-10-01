# C19 second reopening: senior disposition

2026-10-01. Local gpt-6-astra/xhigh reviewed
[the verified brief](portable-linux-staging-second-reopen-2.0-brief.md),9c3a1570.
Main verified effective routing, read the complete returned draft and independently
checked load-bearing claims. Worker/reviewer closed; draft unaccepted. No executable
checks, builders, packaging scripts, SSH or games. Known2.0 only; main untouched.

Retain current installed-tree adapter and existing native resource/build owners.
Correct and consolidate the draft; do not create another packager or retry a full
rewrite around an arbitrary smaller line target.

| Recommendation | Main disposition and independent evidence |
| --- | --- |
| Headered tables reject valid input before other gates | Adopted. Main read table83 and tuple-valued header callers: list!=tuple is unconditional. Normalize the explicit fields before comparison. Empty headerless links remain valid. Correct host resolver spelling. |
| Retained receipts must independently require artifact contents | Adopted. Main read verify886 and builder source/native/receipt hash owners. Validate retained original sources and receipts, every installed file/link and every selected prefix original hash. Non-ELF native notices must retain exact contents; installed ELFs need their original provenance even if an attacker removes both file and manifest row. This is consistency, not signing. |
| Fix source versions and complete contributor set | Adopted. Main read stage_ubuntu523/verify910: actual installed source_version is ignored. Use it, and derive contributors from apt/dev/header/static plus independently validated runtime and shared-notice provider ownership. Do not accept unverified manifest owner declarations as evidence. |
| Offline attribution must not probe verifier host | Adopted. Main checked owner_map383 and original provider path storage. Retain observed lexical/resolved paths at staging; match exact retained file ownership without consulting their existence on a verifier's different architecture/host. |
| Deb payload binding is not duplicated builder evidence | Adopted correction to main's lean. Main read builder273–281: inventory lists package versions/owners, but hashes cover install/deps, not distro ELF/copyright bytes. Retain actual exact-deb byte binding; group required members from canonical provenance/notices and stream each exact deb once. Delete persisted elf_members rather than its independent guarantee. |
| Support native shared copyright directories | Adopted. Main read [official Debian12.3/12.5](https://www.debian.org/doc/debian-policy/ch-docs.html#copyright-information): doc directories may be shared by same-source dependent packages. Identify the real notice owner and exact source/version/dependency relation; carry its matching source/deb/notice. No general apt resolver or arbitrary substitute. |
| Remove competing manifest data and repeated work | Adopted. Single inventory owns staged hashes; per-ELF provenance owns original hashes/paths/owner. Remove staged_sha256, duplicated source archive hash lists, per-owner architecture, elf_members and hand-maintained source_paths. Validate .dsc actual named files/size/checksum/source identity; preserve independent native/receipt original hashes. Consolidate pin identity validation, source/deb indexing, empty pass loop and unused parameters. |
| Reduce scans without losing pre-mutation proof | Adopted. Validated native install inventory seeds all installed ELFs. Keep original byte checks before patchelf, then one complete final inventory/fresh ELF discovery/closure. Main confirmed final_inventory864 repeats scan840 after only in-memory changes. No missing-ELF or source/notice completeness waiver. |
| Record compiler version requirements without invented symbolic restriction | Adopted. Main read abi_needs numeric-only GLIBCXX/CXXABI rejection. Preserve exact version_needs; retain numerical GLIBC<=2.39, GLIBC_PRIVATE/unknown GLIBC refusal and DT_RELR2.36 rule. Do not invent a compiler-runtime floor. |
| Host aliases must not shadow the host boundary | Adopted. Main read verify_closure358: it classifies host dependencies before checking local aliases. Reject package bin/lib entries named as host SONAMEs, including aliases to a differently named ELF. Apply during final discovery and shared closure validation. |
| OpenXR alias load context is unproven by canonical directory | Adopted. Main read SDL_LoadObject's executable-side pathname at vr_openxr.cpp1228 and package alias creation803–810. [GNU's official linker introspection](https://sourceware.org/glibc/manual/latest/html_node/Dynamic-Linker-Introspection.html) says shared-object load names do not necessarily resolve symlinks. Canonical-lib-only validation is insufficient. Give only the OpenXR loader RUNPATH $ORIGIN:$ORIGIN/../lib, and verify both canonical lib and executable-side bin load contexts resolve the same bundled providers. Preserve every other bin/lib policy and native engine loader. |
| Revised necessary estimate | Adopted. Verified draft1014 plus other453=1467. Main's800–850 staging estimate lacked a demonstrated deletion path; do not minify or remove guards to meet it. Target950–1000 combined package.py/policy, stop before1050/new owner; combined delivery1403–1453, reopening1503. This revises main's technical estimate without changing scope, guarantees or user-imposed budget. |

The loader exception is a bounded placement/search-path correction, not a general
dynamic-linker emulator. Canonical and bin load contexts are explicit roots only;
driver/runtime/layer private closures remain host-owned. Its runtime outcome is
still unqualified and must be demonstrated in the final relocated artifact.

Before code commit this disposition and updated bounded refinement plan. Only
package.py/host-policy.json may change; retain existing functions where useful,
one inventory/ELF reader/closure/source validator, native install and isolated
transport. Source review before integration; no early executable qualification.

Final proof: real native install staged/relocated/offline verified, runtime-only
owner, distinct binary/source versions, shared notices, executable-side OpenXR
and its actual descendants; negatives remove entries from both artifact and
manifest to exercise original receipts, plus host alias/ABI/source/notice gaps.
Qualify the same immutable completed source on the second native architecture.
