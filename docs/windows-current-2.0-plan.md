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

2026-10-02 resumed with about1.9GB host root available. Main committed the narrow
room filter reset as f814a05d; old3204 archive is historical. New immutable archive
5b6aee34d3f75c13fbb5a46701212b852190062449fc47ed33333a18b96e2013
includes current production. Build Release then Debug separately in fresh guest
source-f814a05d. Existing initial source/build footprint is only83.6MB; native SDK
reused. Before each configuration require1.5GiB root available; monitor host free
space every2s and stop only our recorded MSBuild process tree if below750MiB.
Validate process name, immutable source argument and creation ticks before stop;
never stop another build or VM. Keep receipts/artifacts. This permits bounded
qualification with recovered space rather than assuming guest sparse free space
is host capacity. Process-level CL_MPCount=2 plus MSBuild/m:2 avoids default-wide
compiler fanout; no project warning/graphics/driver changes. Full compile/link/
shader/PE evidence still required, no device runtime execution.
Microsoft official /MP and build parallelism guidance:
https://learn.microsoft.com/en-us/cpp/build/reference/mp-build-with-multiple-processes
https://devblogs.microsoft.com/cppblog/improved-parallelism-in-msbuild/

Actual f814 native Release attempt reaches strict compiler/shader pipeline and
reports C4244 in three existing bounded(int→float) room arguments and the
position-valid boolean→float assignment; C4310 in eight constant VRIK complement
mask casts. Narrow fixes: explicit float conversion at original bounded boundary,
ternary1.0f/0.0f, omit uint8 cast only on constant-complement bitwise tests with
uint8 left operand (same low8 mask; variable casts unchanged). No warning weakening
or codec/mixer/protocol rewrite. Luna/xhigh owns precisely these three files; main
handles wrapper/docs/shipping/MSVC. Keep accumulated filter reset intact. Existing
codec checks plus affected native graph/audio qualification after implementation.
Wrapper's PS Start-Process exit property was null after native job completed; keep
the process handle alive before waiting, record actual native exit, never infer
success from outer SSH status. First launcher denied .ps1 under default policy;
per-process child PowerShell ExecutionPolicy only, no persistent setting changed.
Official Microsoft process scope documentation:
https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_execution_policies?view=powershell-5.1
