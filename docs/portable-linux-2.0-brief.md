# C19: portable Linux delivery — verified senior brief

2026-10-01. Solo-maintainer release route; implementation and all executable
checks remain deferred until a before-code plan is adopted. Only the 2.0
checkout is writable. Windows builds and user live-device trials are excluded.

## Goal and established owners

Deliver a relocatable native x86-64/ARM64 Linux client with OpenXR, codecs,
CURL and Steam Audio 4.8.1, GLIBC requirements no higher than2.39, matching
dependency notices and no Nix/build-tree resolution. ARM builds run natively
in an isolated Foundry directory; no deployment, remote checkout of master,
commercial assets or live-server changes. Keep native desktop operation.

| Fact | Evidence / confidence |
| --- | --- |
| Meson is the existing engine/install owner | [verified: meson.build] executable install plus engine/component notices; embedded engine pak already built into executable. Steam Audio explicit native include/library options; Vulkan dependency>=1.4.341; shader glslang/spirv-opt required. |
| Current Nix output is not a portable artifact | [verified: flake.nix, docs/linux-native-builds.md] executable-side loader symlink and dependencies reference Nix store. Both native systems and CPU Steam Audio recipe exist. |
| Existing AppImage route is x86-specific | [verified: Packaging/AppImage/docker/Dockerfile, run-in-docker.sh] Ubuntu22.04, x86 LunarG SDK, SDL3 build, linuxdeploy-x86_64; no explicit SteamAudio paths and CURL dev package absent. SDL2 auxiliary build exists. Do not execute its cleanup now. |
| Primary already has a useful bounded release pattern | [verified: readonly ../quakespasm-openvr/scripts/build_linux_glibc239.sh and build_linux_arm64_glibc239.sh] immutable git archive, unique temporary workspace, Ubuntu24.04 native Docker, flat dependency staging, notices, ELF version checks, separate clean runtime qualification. Old OpenVR/make/decoder policies cannot be copied literally. |
| Primary ARM orchestrator contains disallowed inherited deployment assumptions | [verified: readonly scripts/build_linux_arm64_foundry.sh] pushed-master check, clone/fetch/checkout and default installed game directory; replace only transport boundary with source archive and unique remote temp/output, no deployment. |
| SDL explicitly tries executable-side OpenXR first | [verified: Quake/vr_openxr.cpp around1215] libopenxr_loader.so.1/.so absolute path before normal system names. Preserve runtime manifest selection; no bundled compositor/driver. |
| Native SteamAudio CPU source recipe is reusable | [verified: nix/steamaudio.nix and unaligned patch]4.8.1, PFFFT/MySOFA/FlatBuffers/zlib, IPP/MKL/Embree/Radeon/TAN disabled; same unaligned-load fix. Existing package notices installed in C18. |
| Official SDK supports native Linux aarch64 introspection | [verified: official Valve4.8.1 core/CMakeLists.txt lines48–55] architecture selected from CMAKE_SYSTEM_PROCESSOR. PFFFT, MySOFA, FlatBuffers required; no claim that actual ARM build passes. |
| Current Vulkan headers/loader can be built natively | [verified: official Khronos v1.4.341 CMakeLists] native CMake install/config versions. Loader drivers remain system-selected. Tool versions/source builds need qualification. |
| Current OpenXR-SDK has native CMake loader owner | [verified: official release-1.1.60 top-level CMakeLists] pin matching header generation; build loader only, no layers/tests/examples. Exact options/dependency notices must be verified before code. |
| Target actual host tool/dependency availability and packaged ABI | [unknown] final Linux/Foundry qualification, not source/read status or a manifest claim. |

Official primary-source receipts:
- https://raw.githubusercontent.com/ValveSoftware/steam-audio/v4.8.1/core/CMakeLists.txt
- https://raw.githubusercontent.com/ValveSoftware/steam-audio/v4.8.1/core/build/FindPFFFT.cmake
- https://raw.githubusercontent.com/ValveSoftware/steam-audio/v4.8.1/core/build/FindMySOFA.cmake
- https://raw.githubusercontent.com/ValveSoftware/steam-audio/v4.8.1/core/build/FindFlatBuffers.cmake
- https://raw.githubusercontent.com/KhronosGroup/Vulkan-Headers/v1.4.341/CMakeLists.txt
- https://raw.githubusercontent.com/KhronosGroup/Vulkan-Loader/v1.4.341/CMakeLists.txt
- https://raw.githubusercontent.com/KhronosGroup/OpenXR-SDK/release-1.1.60/CMakeLists.txt

## Worked lean and alternatives

Lean: adapt the reference's immutable archive/native Ubuntu24.04 release pattern
under Packaging/Linux, preserving Meson setup/install and installed notices.
One shared native build/stage recipe, small local Docker entry and SSH archive
transport adapter. Produce a relocatable directory/tarball; desktop assets may
reuse Misc. Existing AppImage/Nix routes remain independent native conveniences,
not the portable acceptance proof. No engine/backend changes expected except
an optional Linux install_rpath at the existing Meson executable declaration.

Build native dependencies from pinned official sources where Ubuntu packages
cannot meet required versions (SDL3, recent Vulkan/shader tools, Steam Audio;
PFFFT availability unknown). Reuse Nix Steam Audio flags/patch; do not introduce
a second SDK policy. Prefer distro shared codecs/CURL and collect actual native
non-system dependency closure rather than maintain two architecture file lists.
Stage OpenXR loader beside executable; other bundled ELF dependencies in a
single relocatable library directory with $ORIGIN RUNPATH. System libc/loader,
GPU ICDs, OpenXR runtime, display/audio host services stay external. No global
LD_LIBRARY_PATH launcher that contaminates runtime/driver resolution.

Collect exact source/package notices alongside each additionally bundled
dependency, retain manifest of source commit, native architecture, dependencies
and hashes. Build scripts fail on missing required features/notices/architecture
or ABI ceiling, not silently ship a standard-audio/no-CURL build. Runtime-only
container checks and actual native client qualification happen only at the end;
do not run build recipes now. No claim of portability until those pass.

Alternatives considered:
- Repackage Nix closure: smaller staging code, rejected as sole GLIBC2.39 route
  because current pinned libc baseline is not established and store references
  would require another relocation policy.
- Extend existing AppImage Docker+linuxdeploy to ARM: possible, but x86 tool/SDK
  assumptions and ARM linuxdeploy acquisition are additional incompatibilities;
  a tarball satisfies current delivery requirement with less new machinery.
- Two separate ARM/x86 scripts: primary reference does this, but dependency and
  notice policy would duplicate; prefer one native recipe with strict uname guard.
- Bundle all host libraries/drivers: rejected; breaks runtime/driver ownership.
- Ship system-only optional dependencies: rejected as portable spatial-audio/
  OpenXR closure proof, although host glibc/driver/services remain requirements.

## Open decisions for local Astra

1. Is the native Ubuntu tarball adapter the smallest maintainable complete route,
   or can existing AppImage/Nix owners deliver the same contract with less code?
2. Define smallest precise dependency allow/exclusion and notice ownership rule
   that preserves system runtime/driver discovery. Avoid a generic package manager.
3. Is native source-built SteamAudio on both architectures preferable to SDK x64
   plus native ARM? Lean same recipe; surface any feature/performance tradeoff.
4. Propose bounded implementation slices/estimate and evidence that must be
   collected before coding. Close correctness forks locally; only taste/budget
   genuinely requires a human question.

Review scope: packaging source/dependency/install policy only. Verify load-bearing
facts before critique; do not re-review renderer, networking, XR API selection or
voice algorithms. No edits, tests, builds, SSH probes, cleanup or deployment.
Return prioritized critique <=1800 words with file/line evidence and concrete
disposition candidates; missing evidence should be named, not guessed.
