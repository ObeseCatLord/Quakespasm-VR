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
| FlatBuffers candidate | google/flatbuffers v23.5.26 | 0100f6a5779831fa7a651e4b67ef389a8752bd9b |
| PFFFT candidate | marton78/pffft v1.0.0 | d0db768f2898912cf7e84322124c5634aa961a41 |

The shader tool pair is source-matched, not a guessed version combination:
[glslang known_good.json](https://github.com/KhronosGroup/glslang/blob/f0bd0257c308b9a26562c1a30c4748a0219cc951/known_good.json)
names the SPIRV-Tools and SPIRV-Headers commits above, and
[SPIRV-Tools DEPS](https://github.com/KhronosGroup/SPIRV-Tools/blob/fbe4f3ad913c44fe8700545f8ffe35d1382b7093/DEPS)
agrees on the header commit. Tests/fuzzers are not needed for the tool build;
do not fetch their optional test dependency stack merely because DEPS lists it.
Release shader canonicalize-ids capability remains a final build requirement.

PFFFT and FlatBuffers are researched candidates, not certified selections.
The inspected newer PFFFT v1.1.0 installs headers under include/pffft; Valve's
finder asks for pffft.h, so an explicit include path may be needed. Before coding,
inspect the chosen v1.0.0 exact API/install/optimization options and license;
keep matching FlatBuffers compiler and headers. Do not assume native CPU/ISA
compatibility or successful SDK compilation from the tag metadata.

Other required before-code receipts: exact OpenXR options and system JsonCpp
choice/config prefixes; MySOFA/zlib and distro codec/CURL package policy; native
SDK/tool flags and source/header/static notices; strict host SONAME boundary;
supported ISA without march=native leakage; source archive/hash and dependency
source-access manifest. Native Ubuntu24.04 constrains GLIBC baseline but does
not substitute for checking every final ELF. C19 remains open.
