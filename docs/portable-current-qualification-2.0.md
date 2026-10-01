# Current Linux and ARM candidate qualification

**Subsequent freshness update:** production482cd9f5 changes cl_main.c and
4ef67790 changes gl_rmisc.c,07f83e6e changes host_cmd.c,8071a46b changes
gl_rmain.c/glquake.h/view.c;aa828629 changes gl_vidsdl.c for foveation recovery.
The ff83e66a packages below remain qualified for
that exact earlier input, not these repairs. Final affected artifact refresh belongs to F10 after
source fixes settle. The308-file equality below was verified at its original
qualification snapshot and no longer describes the latest production tree.

2026-10-01. Both native configurations use immutable source
ff83e66a59fb8d15113fca41a5ec35eeb350c3f6, archive SHA256
72dc43196180279e56ad3ee0081818f22bcb14c8707abed522dbe7dc945c39f1.
Main compared all308 tracked production C/C++/header/shader/make files against
the worktree at that qualification snapshot: zero differences. This comparison
predates the subsequent production repairs listed above.
Full pinned native dependencies, Steam Audio4.8.1, OpenXR, generated shaders,
codecs, voice and CURL are enabled; no warning suppression or fallback build.

Linux native build, stage and final verify exited0:45 ELF files,651 inventory
entries and118 Ubuntu contributors. Native ARM build/install, stage and verify
completed in the strict remote pipeline, which then created its archive/checksum.
ARM has the same45/651/118 counts. The original wrapper ended1 during retrieval
while /tmp was full; exact transport stderr is truncated. An owned copy operation
independently reported ENOSPC. Main recovered the existing remote archive without
rebuilding, checked its transport SHA256, extracted it and independently reran
local ARM static verification (exit0). Do not claim the original wrapper succeeded.
The two manifest product identities match exactly.

| Architecture | bin/vkquake SHA256 |
| --- | --- |
| x86_64 | 8c0bff0e6ff451c7d2221a1ab3f0a00bb2cd3594e230aa0d7dd664960413b5ac |
| aarch64 | b72c2e31fcfb07ebed0bfdba9ccd8864c9646ba63180570ca97c33fae5a13ca2 |

Current outputs: private qualification retry5/linux-package and arm-package;
remote /tmp/vkquake-2.0-arm64.MCk95i. Old retry4 evidence moved intact to an owned
disk cache with its original path retained as a symlink, freeing tmpfs. All source
access, notices and receipts remain. No deployed game/server, main branch, user
config/assets or global runtime settings changed.

The relocated current Linux package (path containing spaces) rendered stock start,
signon4, native screenshot and normal exit0 with clean Vulkan validation. Main
inspected actual world/held weapon/HUD. Canonical-lib and executable-side OpenXR
loader contexts loaded public API symbols through RTLD_NOW without LD_LIBRARY_PATH
inside the network-disabled builder. That loader check does not create a session.

The same actual packaged client passed24 simulated-Monado XR probes, each0–23 once:
tasks/GPU lightmaps and stereo SSAO effective, OIT/MSAA/indirect/palette/resize/
scale/floor/aim/pause cases, zero validation errors/hazards/failed markers and
normal command/inferior exit0. Main inspected initial two-eye world/HUD output.
The first attempt timed out because its new profile lacked the recipe smoke cfg;
the corrected run copied that existing config. Actual packaged GPU/runtime work,
simulated input, audio output disabled; no gaze/provider/FB/META or headset proof.

Four current owned negative copies were refused: missing loader, escaping alias,
removed notice and changed loader bytes. Each fails the inventory check, not an
independently exercised resealed semantic-validator path. Root-owned hardlinks
share unchanged data; changed bytes replace a separate leaf. Original untouched.

The packaged ARM client ran dedicated mode on native aarch64 in an isolated
network-disabled Docker namespace, loaded stock start, printed its native map
checklist and exited0. Licensed pak0 only in private test assets. No deployment,
live server, GPU/headset or audio capture/listening proof. The old Linux candidate's
-noudp dedicated trial failed native Network not available; private loopback UDP
succeeded. That invalid harness configuration is not a current product defect.

Private logs (all .log): linux-native-retry5, linux-stage-retry5,
linux-verify-retry5-rerun, arm-build-retry5, arm-local-verify-retry5,
arm-retrieve-retry5, arm-packaged-dedicated-retry5, package-desktop-retry5,
package-xr-retry5-final, package-loader-contexts-retry5, package-negatives-retry5.
Premature/empty failed verify trials are retained; named terminal0 results prove
only their stated boundaries.

Full native build/configuration obligation (group1) is complete for this
production-equivalent archive. Broader desktop/VR/network/QC/content, installed-
artifact/upstream maintainability and final review remain open; whole-project
release acceptance is not claimed.
