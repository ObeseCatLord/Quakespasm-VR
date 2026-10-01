# C19: native portable artifacts at Meson/install boundaries

2026-10-01. Before-code plan using the
[local Astra disposition](portable-linux-2.0-review.md),
[final checklist refinement](final-checklist-refresh-2.0-review.md) and
[verified selected inputs](portable-linux-2.0-inputs.md). All executable builder/
verifier work is deferred until every required implementation item is finished.
No Windows build, deployment or modification of Foundry's existing game/server.

## Minimal adapter versus replacement

Reuse the root Meson dependency/feature/install owners, native Ubuntu container
and immutable git-archive pattern from primary scripts, and nix/steamaudio.nix's
CPU recipe/unaligned-accumulator patch/MySOFA finder adjustment. Keep native Nix/
AppImage convenience routes. New code only builds sources, stages the installed
tree and transfers isolated artifacts; no renderer/audio/build-system replacement.
One policy and one ELF reader serve native x86-64 and ARM64, including staging
and verification. No package manager, dependency plugin system or engine option.

The selected code is the same completed committed source archive for both
architectures; never build a mutable worktree or require pushed master. Explicit
source revision/archive hash travels with results. Docker runs natively on each
host, checks x86_64/aarch64, and writes only unique build/result directories.
The image is the pinned official Ubuntu24.04 multiarch index recorded in inputs.
Distro security revisions are intentionally resolved at container provisioning;
record exact installed binary/source versions and source archives. Distinguish
immutable product inputs from full bit-for-bit reproducibility, which is unclaimed.

## Native build selections

Common CMake Release/shared outputs under a dependency prefix, install lib rather
than guessed architecture subdirectories. No march=native: baseline x86-64 or
armv8-a, portable tuning, no host CFLAGS/CXXFLAGS/LDFLAGS leakage. Preserve native
SDK SIMD dispatch; check actual ISA/options in the final generated compile files.

| Owner | Explicit selection |
| --- | --- |
| Vulkan | Pinned Headers then Loader; native platform X11/Wayland headers; no tests. Installed Vulkan metadata satisfies Meson>=1.4.341. |
| Shader tools | Exact SPIRV-Headers/Tools pair, tests disabled; build installed SPIRV-Tools once. glslang uses exact matched installed tools with ALLOW_EXTERNAL_SPIRV_TOOLS=ON, optimizer/SPIR-V/binaries/install enabled, GLSLANG_TESTS=OFF. Release canonicalize-ids support is a final build gate. |
| OpenXR | Pinned SDK, BUILD_LOADER=ON, DYNAMIC_LOADER=ON, API layers/tests/conformance/SDK tests off, BUILD_WITH_SYSTEM_JSONCPP=ON, BUILD_WITH_STD_FILESYSTEM=ON, exception handling on. Logical CMAKE_INSTALL_SYSCONFDIR=/etc even under build staging, libdir=lib. No packaged runtime manifests/layers. |
| SDL3 | Selected source, shared on/static off, install/relocatable on, tests/test library/examples/CPack off, Vulkan/X11/Wayland/libdecor on. SDL_DEPS_SHARED and X11/Wayland/libdecor/ALSA/PulseAudio/PipeWire/JACK/sndio/KMSDRM/libusb shared-loading flags on; optional host service availability remains SDL's native policy. Final config confirms X11/Wayland support. |
| FlatBuffers | Pinned flatc plus matching headers, install on, tests/benchmarks/grpc tests and flat/shared libraries off. Not a shipped flatc executable. |
| PFFFT | Selected shared/install/SIMD float+double; TARGET_C_ARCH/TARGET_CXX_ARCH=none; tests/benchmarks/examples off. Explicit prefix/include/pffft finder path. |
| Steam Audio | Reuse4.8.1 recipe/patch; IPP/MKL/Embree/RadeonRays/TrueAudioNext off; tests/interactive tests/benchmarks/samples/docs off, shared phonon and STEAMAUDIO_STATIC_RUNTIME=OFF. Native SDK optional x86 AVX dispatch retained, no accelerator equivalence claim. Copy existing native phonon output and API/version headers. |
| Distro source inputs | Noble MySOFA/zlib/JsonCpp; MPG123 (selected MP3 backend), FLAC/Ogg/Vorbis/Opus and OpenSSL CURL development packages. Apt source indexes enabled to retrieve exact matching package sources. Capture dev/header/static ownership too. |
| Engine | Meson release, explicit SDL3/Steam Audio/WAVE/MP3/FLAC/Vorbis/Opus on, mp3_lib=mpg123, required phonon include/library paths, auto_features enabled. Require libcurl pkg-config before setup and verify USE_CURL/link selection at final qualification. Preserve native embedded pak/install notices and executable's dedicated mode. |

Use Ubuntu native build/development packages for compiler, CMake, Meson, Ninja,
Python, pkg-config, binutils, patchelf, patch, git/certificates, audio/display
headers and named distro codecs/CURL/MySOFA/JsonCpp/zlib. Record the exact image,
tool versions and repository/package receipts. Do not download optional test SDKs.

## Host boundary and bundled closure

Fixed layout: bin/vkquake and bin/libopenxr_loader.so.1; bundled libs in lib;
native/component notices in share/licenses. Each executable-side ELF RUNPATH is
$ORIGIN/../lib; each lib ELF's is $ORIGIN. Loader aliases remain inside the tree.
No global LD_LIBRARY_PATH, absolute DT_NEEDED, escaping symlinks or build/Nix
paths. Do not assume parent RUNPATH propagates to descendants.

Explicit host ELF SONAMEs (exact names, no wildcard policy):

- glibc/compiler: libc.so.6, libm.so.6, libdl.so.2, libpthread.so.0, librt.so.1,
  libresolv.so.2, libutil.so.1, libgcc_s.so.1, libstdc++.so.6; native interpreter
  ld-linux-x86-64.so.2 or ld-linux-aarch64.so.1. Record required GLIBCXX/CXXABI
  floors rather than inventing a baseline or copying another release's numbers.
- Display/input: libX11.so.6, libX11-xcb.so.1, libxcb.so.1, libXext.so.6,
  libXcursor.so.1, libXrandr.so.2, libXfixes.so.3, libXi.so.6, libXss.so.1,
  libXrender.so.1, libXinerama.so.1, libwayland-client.so.0, libwayland-server.so.0,
  libwayland-cursor.so.0, libwayland-egl.so.1, libxkbcommon.so.0, libdecor-0.so.0.
- Graphics/desktop services: libEGL.so.1, libGL.so.1, libGLX.so.0,
  libOpenGL.so.0, libGLESv2.so.2, libdrm.so.2, libgbm.so.1, libudev.so.1,
  libdbus-1.so.3, libsystemd.so.0, libusb-1.0.so.0.
- Audio services: libasound.so.2, libpulse.so.0, libpulse-simple.so.0,
  libpipewire-0.3.so.0, libjack.so.0, libsndio.so.7.0.

Runtime/driver ICDs, OpenXR runtime/layer binaries and their private dependencies
remain host-owned, not traversed/copied into the application. Record host service/
runtime prerequisites, user/system runtime discovery, CA trust, audio configuration
and applicable dlopen plugin data independently from the DT_NEEDED closure.
Required SDL service libraries are available from host installation; unavailable
optional native backends retain SDL's existing selection/failure behavior.

Bundle SDL3, Vulkan/OpenXR loaders, phonon and actual native codec/CURL/JsonCpp/
MySOFA/PFFFT descendants unless an exact host SONAME above applies. A bundled
provider must resolve to a selected pinned dependency build or an installed noble
package with exact binary/source ownership and a matching notice/source receipt.
Unowned/unclassified providers fail; do not copy an arbitrary host library.
Resolve only dependency-prefix and native distro search directories in the clean
builder, detect multiple conflicting providers/SONAMEs and check architecture
and GLIBC<=2.39 over the complete shipped ELF closure. Seed OpenXR explicitly;
engine DT_NEEDED alone misses this dynamically loaded root.

This layout does not prove in-process runtime/driver isolation: host code may
share SONAMEs with bundled codec/CURL descendants. Final relocation/runtime
qualification must expose unresolved version requirements rather than promise
universal runtime compatibility based on absence of LD_LIBRARY_PATH.

## Notices and matching source access

Retain Meson's native/C18 installed notices. Add selected-source notices at exact
paths: Vulkan Header/Loader LICENSE.txt, SPIRV-Tools LICENSE, SPIRV-Headers LICENSE,
glslang LICENSE.txt, OpenXR LICENSE, SDL LICENSE.txt, Valve LICENSE.md and
core/THIRDPARTY.md, PFFFT LICENSE.txt, FlatBuffers LICENSE. Confirm actual source
paths when implementing; missing notices fail rather than synthesize license text.
Include native dev/header/static contributors as well as shipped ELF providers.
For Ubuntu packages copy installed copyright notices and record binary package,
source package/version, repository, checksums and exact matching .dsc/orig/debian
source archives. Include fetched pinned dependency source archives, product source,
build scripts/pins and reused patches in the accompanying source-access artifact.
Hash staged files, archives, selected options and source/tool/package receipts;
never infer incorporated notices from ldd alone.

## Bounded coding slices and failure paths

### Slice1 handoff refinement before source integration

Main source review of the returned318-line builder found that gzip piped to
git get-tar-commit-id may receive SIGPIPE because the latter reads only the
archive's initial header. Under pipefail this can reject valid compressed input.
Decompress fully into the builder's unique temporary workspace before checking
the archive commit; retain original compressed source/hash in output. Keep the
single native build owner and existing cleanup trap.

Install SDKs directly under the actual deps prefix, preserving their metadata
and build-time loader paths, rather than configuring prefix=/ then DESTDIR
relocating them. The pinned official
[Vulkan pkg-config template](https://github.com/KhronosGroup/Vulkan-Loader/blob/32fcb949e253cbeb40cda7ea76122b492db579ae/loader/vulkan.pc.in)
uses the configured prefix for include/library paths. Do not depend on implicit
pkg-config relocation to find the selected SDK. Engine Meson installation still
uses its native logical prefix=/ plus install DESTDIR. Set SDK install RUNPATH
to the deps/lib build prefix; final staging replaces every shipped RUNPATH with
the reviewed relative policy. Logical OpenXR sysconfdir remains /etc.

Version/path-query pipelines must consume complete producer output; replace
early-exit head/awk consumers with first-result selection without early exit.
These are source-derived corrections within slice1's500-line bound, not an
executed build or a new packaging architecture. Final installed metadata, native
tools/features and ISA qualification remain required.

1. Packaging/Linux sources.json, Dockerfile and build-native.sh only: explicit
   pins/options and shared native SDK/engine build/install. Target300–400 lines,
   pause before500 or engine edits. Emit installed-tree/dependency/source receipts
   for later staging; no fake success placeholder or host-code fallback.
2. Packaging/Linux/package.py and host-policy.json only: shared static ELF reader,
   closure/staging/aliases/RUNPATH, notices/source manifests and verification
   subcommand. Target350–450 lines, pause before550/new resolver/framework. Read
   native metadata, never execute a copied library as dependency analysis.
3. Packaging/Linux/build-foundry.sh plus concise README only: immutable archive
   transport into a unique native ARM directory, invoke same builder and retrieve
   artifacts/checksums. Target80–120 lines, pause before160/new deployment logic.

Combined rough800–1000 lines, reopen before1200, duplicated policy or unexpected
runtime/engine changes. Delegate disjoint slices only after dependent interfaces
are source-reviewed. Each source patch gets main integration review and regular
scoped commits. Execute none of these scripts until all implementation is done.

Final qualification: build the same immutable source on Linux/isolated Foundry,
qualify shaders/features/native ISA, actual installed ELF closure/RUNPATH/ABI/
notices/source-access receipts and relocation in a runtime-only environment whose
packages match the declared host boundary, not bundled-codec/CURL omissions.
Supplementary missing-data startup is not loaded-scene/OpenXR acceptance. Use
actual native client/software owners, gaze/foveation lifecycle fixtures and
meaningful content/network checks from the consolidated plan, without deploying
or timing performance. Close observed failures at the existing narrow owners.
