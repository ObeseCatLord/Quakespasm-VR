# OpenXR loader for Windows packages

`x64/openxr_loader.dll` is the unmodified Khronos OpenXR loader from the
official OpenXR SDK release `release-1.1.60`, the same SDK version as the
loader in the Linux packages:

- Archive: `openxr_loader_windows-1.1.60.zip` from
  https://github.com/KhronosGroup/OpenXR-SDK-Source/releases/tag/release-1.1.60
- Archive SHA-256: `f4a0fc334f73ead29501d5a2a53aa453d6bb79b3b2274120996838fd8c52050a`
  (detached signature `openxr_loader_windows-1.1.60.zip.asc` verified)
- Member `x64/bin/openxr_loader.dll`, SHA-256
  `7f07a9ae5b02f6dc9d667673fbc7a309fdabe136c3b24c0d4f54b5c1d544dd3b`
- License: Apache-2.0, `LICENSE.txt` (from `share/doc/openxr/LICENSE`).
  Source: https://github.com/KhronosGroup/OpenXR-SDK-Source/tree/release-1.1.60

The Windows release packaging copies it beside `quakespasm-openvr.exe`; the
engine loads it from there to reach the active OpenXR runtime. It is a runtime
file, not a build input, so it is outside the pinned MSBuild dependency tree.
