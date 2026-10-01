# C19 native source inputs — researched receipt

2026-10-01. Inputs for the pending portable builder plan, following
[Astra disposition](portable-linux-2.0-review.md). This is not an implemented
builder or artifact qualification. Official GitHub repository tag-to-commit
metadata and raw source manifests were read; no compiler/build/probe/test ran.

| Component | Official source reference | Immutable commit |
| --- | --- | --- |
| Vulkan-Headers | KhronosGroup/Vulkan-Headers v1.4.341 | b5c8f996196ba4aa6d8f97e52b5d3b6e70f7e4e2 |
| Vulkan-Loader | KhronosGroup/Vulkan-Loader v1.4.341 | 32fcb949e253cbeb40cda7ea76122b492db579ae |
| glslang | KhronosGroup/glslang vulkan-sdk-1.4.341.0 | f0bd0257c308b9a26562c1a30c4748a0219cc951 |
| SPIRV-Tools | KhronosGroup/SPIRV-Tools vulkan-sdk-1.4.341.0 / v2026.1 | fbe4f3ad913c44fe8700545f8ffe35d1382b7093 |
| SPIRV-Headers | Exact glslang known_good / SPIRV-Tools DEPS source | 04f10f650d514df88b76d25e83db360142c7b174 |
| OpenXR-SDK | KhronosGroup/OpenXR-SDK release-1.1.60 | 64f2b37c8c6da3d83c9b4d11865ba1fb752cb8ec |
| Steam Audio | ValveSoftware/steam-audio v4.8.1 | 0da18255cca520771f363ee01f100572b39a308e |
| SDL3 | libsdl-org/SDL release-3.4.12, matching existing AppImage source version | f87239e71e42da91ca317a12eefb82cfbf3393eb |
| FlatBuffers selection | google/flatbuffers v23.5.26 | 0100f6a5779831fa7a651e4b67ef389a8752bd9b |
| PFFFT selection | marton78/pffft v1.0.0 | d0db768f2898912cf7e84322124c5634aa961a41 |

The shader tool pair is source-matched, not a guessed version combination:
[glslang known_good.json](https://github.com/KhronosGroup/glslang/blob/f0bd0257c308b9a26562c1a30c4748a0219cc951/known_good.json)
names the SPIRV-Tools and SPIRV-Headers commits above, and
[SPIRV-Tools DEPS](https://github.com/KhronosGroup/SPIRV-Tools/blob/fbe4f3ad913c44fe8700545f8ffe35d1382b7093/DEPS)
agrees on the header commit. Tests/fuzzers are not needed for the tool build;
do not fetch their optional test dependency stack merely because DEPS lists it.
Release shader canonicalize-ids capability remains a final build requirement.

PFFFT and FlatBuffers are selected for the before-code plan, not build-qualified.
The chosen PFFFT v1.0.0 installs include/pffft, while Valve's finder asks for
pffft.h: set PFFFT_INCLUDE_DIR explicitly. Its TARGET_C_ARCH/TARGET_CXX_ARCH
default none avoids march=native; aarch64 selects its NEON implementation without
requiring a host-specific CPU flag. Preserve SIMD and float/double defaults,
disable tests/benchmarks/examples and install shared pffft. LICENSE.txt is the
complete selected-source notice. FlatBuffers installs matching flatc and headers;
disable tests/benchmarks/grpc tests and unnecessary flat library builds. LICENSE
covers the incorporated C++ headers. No successful compilation is implied.

Selected native base: official Ubuntu24.04 multiarchitecture image index
sha256:008173c23f95b170204355c12626cb5a965d779a7e1283b09e9cffbb1bf33ca3,
read from Docker's official registry on2026-10-01. Native amd64 manifest
sha256:496754492fb28b4d3049432f2ca787449331e23fb14f0dd3fffea86bf5a93eb4;
arm64/v8 manifest sha256:11dc1ccb427f0464a2369e645454c272bb0baece7357c892ba69d313b3a332cf.
The shared index fixes the image input for both architectures. Distro security
packages remain selected within noble/noble-updates/noble-security; record exact
resolved binary/source versions, repository inputs and matching source archives
in each artifact. This is not a promise of bit-identical outputs across builds.

Official noble package pages were read for Meson1.3.2-1ubuntu1,
MySOFA1.3.2+dfsg-2ubuntu2, libdecor0.2.2-1build2, JsonCpp1.9.5-6build1,
FLAC1.4.3+ds-2.1ubuntu2, Opus1.4-1build1 and codec/CURL development packages.
MPG123/CURL pages expose security revisions and architecture variation: capture
the actual resolved versions, never copy these observations into an unsupported
universal version pin. Meson is architecture-independent; a literal lack of amd64/
arm64 in that package page is not absence of native support.

Official source receipts:
- [PFFFT exact optimization owner](https://github.com/marton78/pffft/blob/d0db768f2898912cf7e84322124c5634aa961a41/cmake/target_optimizations.cmake)
- [PFFFT notice](https://github.com/marton78/pffft/blob/d0db768f2898912cf7e84322124c5634aa961a41/LICENSE.txt)
- [FlatBuffers selected build/install options](https://github.com/google/flatbuffers/blob/0100f6a5779831fa7a651e4b67ef389a8752bd9b/CMakeLists.txt)
- [Noble JsonCpp package/source notices](https://packages.ubuntu.com/noble/libjsoncpp-dev)

Other required before-code receipts: exact OpenXR options and system JsonCpp
choice/config prefixes; MySOFA/zlib and distro codec/CURL package policy; native
SDK/tool flags and source/header/static notices; strict host SONAME boundary;
supported ISA without march=native leakage; source archive/hash and dependency
source-access manifest. Native Ubuntu24.04 constrains GLIBC baseline but does
not substitute for checking every final ELF. C19 remains open.
