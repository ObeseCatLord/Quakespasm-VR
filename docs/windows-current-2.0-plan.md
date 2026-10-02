# Windows build qualification

2026-10-01. The user now requests Windows builds through running WinBoat,
overriding the earlier build deferral. Live graphical/headset tests remain
deferred, and the host NVIDIA state must not be changed.

Reuse the native Visual Studio solution and bundled SDL3/codec/Vulkan import
libraries. Build an immutable 2.0 archive in a fresh guest directory through
the private winboat-ssh channel; do not use the interactive SMB mapping or
modify another checkout. Record revision, archive hash, actual compiler,
configuration, command, exit status and binary hashes. Initially verify x64
Release, then Debug once demonstrated source/build incompatibilities are fixed.

The guest has VS2022/v143, whereas the upstream solution defaults to v145.
Use a command-line PlatformToolset=v143 override, preserving upstream defaults.
The guest lacks the shader SDK. Use pinned LunarG 1.4.341.1, matching Windows
CI, its published SHA256, and documented copy_only=1 with a private --root.
Set VULKAN_SDK only in the build process. No driver/runtime installer, registry
change, Vulkan device probing or game launch is part of qualification.

Enable existing Steam Audio SDK integration using a verified SDK layout.
Inspect all current production source and generated shader coverage against
the working Meson manifest: successful linking must include the actual stereo,
UI, AO, particles and migrated gameplay owners. Fix missing entries at the
existing project/custom-build boundary, reusing definitions and tools rather
than introducing another renderer or parallel build service. Keep strict
warnings; correct demonstrated portability errors narrowly at their owner.

The initial native build demonstrated skipped declarations before the PCH
include, missing PCH includes in independent helpers, UTF-8 comments read as
codepage 932, local-name shadow warnings and three signed function-index
comparisons. Disable PCH only for the demonstrated independent/prelude owners,
specify UTF-8 in the existing compiler metadata, rename only local identifiers,
and make the existing unsigned conversion of function-count bounds explicit
using func_t, preserving the native comparison semantics.
MSVC warns on the existing standard C flexible-array member; retain its exact
layout/allocation math with a declaration-local warning push/pop for C4200.
Do not weaken warnings globally or replace working runtime owners.

After a clean compile/link, inspect PE architecture and dependent DLLs and
retrieve artifacts/receipts. This is Windows compilation qualification, not
proof of headset behavior or a redistributable installer. Final F10 integration
still requires reconciling Linux/ARM/Windows shipping source and artifact hashes.

Official SDK copy-only/--root documentation:
https://vulkan.lunarg.com/doc/view/1.4.341.1/windows/getting_started.html
