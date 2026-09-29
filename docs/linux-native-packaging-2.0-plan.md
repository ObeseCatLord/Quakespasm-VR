# Linux native build packaging

Status: plan before implementation. Native x86-64 and ARM64 Linux, with Foundry
qualification after the entire implementation pass. No Windows builds now.

Behavior: build the same vkQuake/OpenXR/voice/spatial-audio engine on each host
architecture using native dependencies. Include matching libphonon rather than
silently dropping Steam Audio on ARM. Preserve runtime OpenXR discovery and
desktop fallback. Do not package proprietary game assets or runtime drivers.

Verified reuse: inherited nix/steamaudio.nix pins Valve Steam Audio4.8.1 and
its source hash, PFFFT/MySOFA/FlatBuffers/zlib dependencies and the inherited
unaligned accumulator fix. Its flake.lock pins nixpkgs and its flake already
lists x86_64-linux/aarch64-linux. Current2.0 Meson owns shader compilation,
SDL selection, codecs/voice, Vulkan and explicit native phonon paths, but does
not install its executable. Existing AppImage tooling is x86-only and predates
the XR/audio integration.

Official source confirms native Linux aarch64 detection in
[Valve's pinned CMakeLists](https://github.com/ValveSoftware/steam-audio/blob/v4.8.1/core/CMakeLists.txt)
and system dependency lookup in its build/FindPFFFT.cmake and
build/FindMySOFA.cmake. This is source support, not yet ARM build qualification.
The earlier missing-prebuilt-binary finding remains true; there is no evidence
requiring an SDK rewrite. The existing spatial-audio senior review requires
matching native libraries and remains the architecture decision.

Preferred adapter: copy the existing pinned SDK derivation/patch/lock unchanged,
and adapt only the flake game package to existing Meson, SDL3 and Vulkan/codecs.
Enable Meson executable installation. Expose SDK/game packages and a development
shell for both systems. Preserve upstream renderer/build ownership and ordinary
non-Nix builds. This gives a reproducible dependency/build path; portable
AppImage/tarball release artifacts remain a separate packaging qualification.
Reject copying the old OpenGL build and shipping x64 SDK binaries on ARM.

Main ownership: flake.nix/lock, nix/steamaudio.nix and its patch, meson.build
executable install flag, docs/Linux build instructions and this index. No engine
audio/renderer changes, deployment or Foundry server/game modifications.
Expected scope: copied SDK recipe plus a small game derivation; no new build
system. Reopen if runtime libraries need a second audio owner or a native port.

End-of-goal checks: native build and installed executable/dependency architecture
on each Linux target; no missing linked audio/codec/Vulkan libraries; optional
no-SDK build preserves desktop fallback; OpenXR loader discovery remains runtime
owned. Use isolated Foundry build directory via SSH. No builds/tests or package
evaluation during this implementation slice; software checks occur at the end.
Headset/audio listening and performance measurement remain user-deferred.
