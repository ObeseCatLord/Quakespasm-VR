# C19 portable Linux senior disposition

2026-10-01. Local Astra/xhigh review of the
[verified brief](portable-linux-2.0-brief.md). Main independently verified
effective gpt-6-astra/xhigh routing from matching turn-context metadata. The
reviewer could not verify its own routing. Source-only review: no executable
checks/builds/SSH or package qualification occurred.

Main spot-checked executable-side SDL loader discovery, Meson install/codec/CURL
selection, existing native SDK flags/patch and FlatBuffers omission from Valve's
checked-in notice collection. Official OpenXR source confirms JsonCpp choices,
dynamic loader options and compiled configuration directories; official Valve
helper/target confirms native linux-arm64 paths. These are inputs, not a proof
that either native artifact builds or relocates.

| Recommendation | Disposition |
| --- | --- |
| Shared native Ubuntu24.04 tarball adapter over AppImage/Nix relocation rewrite | Adopted. Reuse Meson/install and primary immutable archive/isolated native build pattern; retain existing convenience routes. No engine changes or new package manager. |
| Set every shipped ELF's own RUNPATH | Adopted. bin/vkquake and bin/libopenxr_loader.so.1 use $ORIGIN/../lib; bundled lib/* use $ORIGIN. Stage-only normalization avoids adding a Meson option. Preserve SONAMEs and relative internal aliases; reject absolute dependencies/escaping links/conflicting providers/unresolved edges. |
| Seed dynamically loaded OpenXR explicitly | Adopted. Engine DT_NEEDED cannot discover the SDL_LoadObject root. Keep loader beside executable and system runtime manifests/config discovery unchanged. |
| Concrete host versus bundled boundary | Adopted. SDL3, Vulkan/OpenXR loaders, phonon, codecs and CURL roots plus their actual native dependencies are bundled, except reviewed host SONAMEs. Host glibc/interpreter/compiler runtimes, drivers/ICDs/runtime/layers and display/audio services remain external. Name desktop client libraries individually; unknown classifications fail. Record CA trust/dlopen modules/config prerequisites separately. |
| Runtime-library isolation needs more than no LD_LIBRARY_PATH | Adopted. Qualify in-process runtime/driver compatibility; record actual host GLIBCXX/CXXABI floors without copying old values or bundling a compiler runtime silently. |
| Notices follow header/static dependencies too | Adopted. Preserve native installed notices and exact added dependency notices, including FlatBuffers and the selected JsonCpp source, plus versions/hashes/patches/source access. ELF closure alone cannot enumerate incorporated code. Matching source-access artifact includes scripts/patches. |
| Explicit required features and clean runtime boundary | Adopted. SDL3/SteamAudio/codecs enabled explicitly, CURL packaging precondition plus compiled/link confirmation. Runtime-only environment must not install the same libraries whose omission it should detect. Missing-data startup/ldd are supplementary, not full client acceptance. |
| Same native CPU SteamAudio recipe on both architectures | Adopted. Reuse4.8.1/patch/flags/MySOFA adjustment; no SDK abstraction/refactor. Accelerator/performance equivalence is unclaimed. Supported ISA and actual ARM build remain qualification inputs. |
| Close pins/options before builder implementation | Adopted. Exact PFFFT/FlatBuffers/shader dependency revisions, OpenXR loader options/JsonCpp/config prefix and matching notices are required before the builder slice. Use logical system config prefixes with staging, never temporary build directories in runtime lookup. |
| One committed immutable input for both architectures | Adopted. Archive completed integrated snapshot with packaging recipes/pins; hash source and artifacts. ARM transport only transfers into a unique workspace and retrieves results. No pushed-master requirement, deployment or live installation staging. |
| Estimate3–5 focused maintainer days | Recorded as uncertainty, not a calendar promise. Before-code slices/line bounds and final executable evidence remain required; reopen on engine changes, architecture-specific duplicated policy or generic framework growth. |

## Remaining before-code work

Separate bounded slices: source/tool inputs and native builder; installed-tree
staging/closure/notices/source manifest; artifact verifier; SSH transport. They
share one native architecture/dependency policy. Exact builder inputs and the
host SONAME table must be documented before their code, rather than discovered
through repeated build failures. All builds/verifiers execute only after every
required feature is implemented. C19 remains open.

Official evidence inspected by both reviewer/main:
- [OpenXR loader configuration/JsonCpp/install owner](https://github.com/KhronosGroup/OpenXR-SDK/blob/release-1.1.60/src/loader/CMakeLists.txt)
- [OpenXR build options](https://github.com/KhronosGroup/OpenXR-SDK/blob/release-1.1.60/src/CMakeLists.txt)
- [Valve native architecture helper](https://github.com/ValveSoftware/steam-audio/blob/v4.8.1/core/build/SteamAudioHelpers.cmake)
- [Valve native phonon target](https://github.com/ValveSoftware/steam-audio/blob/v4.8.1/core/src/core/CMakeLists.txt)
- [FlatBuffers interface dependency](https://github.com/ValveSoftware/steam-audio/blob/v4.8.1/core/build/FindFlatBuffers.cmake)

The review changed the plan: it caught non-transitive search-path assumptions,
missing dynamic roots, header/static notice omissions, and runtime validation
that could hide an incomplete bundle. No further human decision is required
within the existing portable-delivery contract.
