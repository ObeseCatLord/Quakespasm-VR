# 2.0 migration status

## Baseline and preservation

The `2.0` branch starts directly at vkQuake commit
`4bc898f29073e8aa41069f0e79e3cb5a9eb73afa`, the upstream `master` tip checked
on 2026-09-20. Its initial tree was identical to upstream. Subsequent commits
are migration work. `vkquake-upstream` tracks the original repository at
<https://github.com/Novum/vkQuake>; `origin` remains the project fork.
The separate `quakespasm-2.0` worktree avoids modifying the user’s product
worktree (currently on `master`). No history was reset, rewritten, pushed, or deployed.

Behavioral references:

- Inherited base: `8c5a6007a60098b6a5b5c5b552def70e1238a852`.
- Original product snapshot: `7bc466b594e7a7e584dc47879eb6c00f971b01b1`.
- Current product authority: `1327f795cc2e3a8e4f7c9d68e31d64383930cc00`; includes four newer fixes/package/license commits missing from the OpenXR pin.
- Migration source: `3080841333fa94000df7e1fb9e549c7158685dd6`.
- The original worktree's 13 uncommitted files were copied with a binary patch
  and SHA-256 manifest into the shared Git directory's local-only
  `migration-references/2.0-30808413/` snapshot. The immutable snapshot remains in place; the product checkout is now clean on newer `master`.
  WIP is preserved evidence, not automatically accepted behavior.

The [complete-scope feature map](migration-feature-map.md) is the current migration checklist: 185 behavior/work items with source evidence, destination owners and acceptance criteria, plus [11 optional Ironwail/QSS-M additions](migration-useful-additions.md). The [senior review disposition](migration-feature-map-review.md) records the required early multiview proof and concrete donor differences. Detailed [network](migration-network-map.md) and [renderer](migration-renderer-map.md) maps distinguish donor equivalents from actual ports and goals.

[migration-preservation.csv](migration-preservation.csv) now covers 905 unique paths, extending the original 904-path snapshot with the current master delta. It retains old blob/WIP evidence and adds current-master blobs and feature routing. Exact source anchors and mechanical module routes are labeled separately. [History](migration-history-index.csv) and [public-interface](migration-interface-index.csv) indexes make omissions reviewable. These are scope/audit artifacts, **not proof of completed behavioral integration**. Historical deletions are reviewed rather than restored blindly; preserved WIP is not automatically accepted release behavior.

The sections below record successive checkpoints. The latest implemented slice
is **Initial stereo scene integration**; earlier limitations describe their
respective commits, not the current head. None closes the full P1 gameplay gate.

## First source checkpoint

The existing standalone OpenXR backend is adapted to the Vulkan-only donor.
It retains the original session/frame/action/tracker/gaze state machine and
Vulkan attachment API; the legacy OpenGL transport is omitted from this donor
module. This does not remove the requirement to migrate inherited OpenVR
behavior through a Vulkan compositor path.

Meson, the native Unix Makefile and the native Visual Studio project now
include this module as C++, while the upstream renderer remains C. The backend uses SDL2/SDL3 loader APIs and
loads OpenXR dynamically. Vendored headers, licensing and exact source pins
are in `Quake/thirdparty/openxr/PROVENANCE.md`.

**This checkpoint does not enable VR gameplay.** Engine initialization,
renderer/device attachment, menu settings, scene submission and shutdown are
not connected yet. Importing the runtime interfaces does not qualify gaze,
foveation, Vulkan stereo, or any headset. No new CLI switch claims otherwise.
Routine builds/runtime tests are deferred until the implementation slice is
complete, as requested. Source review is not build or device validation.

## Vulkan graphics bootstrap checkpoint

The renderer now honors explicit `-openxr` at Vulkan startup; `-novr` takes
precedence and ordinary desktop startup does not discover OpenXR. Discovery can
fall back to desktop before Vulkan handles are bound. Once the runtime has
selected the binding, creation failures are diagnosed and partial renderer-owned
handles are cleaned rather than mixed with an unrelated device.

`Quake/gl_vidsdl.c` retains donor ownership of the instance, physical device,
logical device, queue and all ordinary renderer resources. It obtains the XR
requirements, uses the existing enable2 creation wrappers and runtime-selected
GPU, and integrates core multiview queries into the donor feature/property chains.
It enables the multiview bit only when supported for two views; geometry and
tessellation multiview bits stay off. This is capability negotiation, not a
multiview render pass. Desktop feature negotiation remains on the donor path.

Runtime requirements and the actual GPU are checked separately from loader
capability. The backend now compares major/minor versions (ignoring patches),
rejects versions below the runtime minimum, and warns above its highest tested
version. The startup selector prefers a known-tested version while honoring
minimums above Vulkan 1.1. Main-thread runtime cleanup also covers errors before device
creation completes, including the Windows fatal path. The failure helper retires
its debug messenger before destroying the instance. No frame-loop device-idle wait or second lifecycle owner was
introduced.

**No session or swapchain is attached by this checkpoint.** Startup logs say so.
Renderer/device bootstrap is the first part of P1; it does not close the early
multiview proof, implement gameplay/input, qualify an HMD, or enable foveation.
It currently retains donor WSI/graphics-queue requirements. Saved VR settings,
menu toggles, OpenVR compositor integration and healthy runtime reattachment
remain part of subsequent integration.

Consolidated software checks after this slice was implemented:

- Native Linux Meson `debugoptimized` build with SDL3 and Vulkan headers passed,
  including the donor shaders and C/C++ executable. This host build is diagnostic
  only; it is not a GLIBC-ceiling-qualified release artifact.
- The [reused Vulkan boundary fixture](../tests/README.md) passed creation-result,
  version/provenance, independent/array-image ownership, incomplete-frame and
  retirement-order checks. It uses simulated dispatch and is not graphics or
  headset proof. Porting it exposed a stale missing event callback in its reset
  setup, which was corrected.
- The build exposed an imported `const void*`/`void*` chain mismatch in runtime
  foveation setup. Linking the mutable-next FB node before the const-next META
  node fixes the C++ type error without changing flags or enabling foveation.
- Whitespace checks passed. Native Windows/ARM, runtime/headset tests, performance
  measurements and the P1 end-to-end gate remain pending.

The code/design review is recorded in
[the bootstrap review disposition](migration-bootstrap-review.md).

## Queue and frame ownership checkpoint

The OpenXR boundary now uses vkQuake's existing queue mutex through a small
callback registration. Attachment requires a complete lock/unlock pair;
registration cannot change while a session exists. The backend locks only the
four runtime calls permitted to access the Vulkan queue, releasing the mutex
before error logging or renderer retirement. Healthy detach preserves this
binding; full shutdown clears it. There is no new submission thread, mutex,
device, or session state machine.

The donor recorder also now consumes the actual WSI acquisition result. If an
image was not acquired, it skips the complete UI/presentation pass and screenshot
readback instead of accessing the previous swapchain index. Scene work remains
on the existing submission path, and pending screenshots wait for a successful
acquisition. Frame-slot bookkeeping now distinguishes retired work from a new
successful submission.

The native Linux SDL3 `debugoptimized` build passes after these changes.
The production frame-recorder fixture covers all OIT/SSAO variants and detects
the original invalid-image access in a temporary negative control. Review and
boundary-check details are in
[the frame ownership disposition](migration-frame-boundary-review.md).
Windows and ARM verification are explicitly deferred until the end.

**Session attachment and stereo scene output remain unimplemented.** This
checkpoint removes synchronization and invalid-image prerequisites; it does not
close the multiview proof or provide headset, gaze, gameplay, or performance
qualification. Image release must still follow actual application submission,
and teardown must retire both recording tasks and GPU users.

## Initial stereo scene integration

Explicit `-openxr` now attempts session attachment at the first renderer frame.
The renderer creates two-layer views of the runtime-owned images and adapts its
existing color, depth, MSAA and transparency resources to the same two-layer
layout. Scene and UI render passes use Vulkan multiview; existing tasks and draws
remain the owners of animation, world preparation, recording and submission.
Main-thread frame completion joins the actual submission task before releasing
both eye layers. Desktop startup remains the default; `-novr` takes precedence.

A dynamic uniform supplies relative per-eye clip transforms to the existing
vertex shaders, preserving donor per-model MVP calculations. Sky uses per-eye
origins. World visibility combines the center and both eye PVS sets, encloses
both full-eye frusta, and retains surfaces facing either eye. Existing SIMD and
indirect GPU culling stay in use; moving/scaled brush backface tests receive a
conservative eye-separation margin. This favors correctness at eye-only visibility
boundaries; its CPU/GPU cost still needs measurement.

Array-aware color/palette/underwater effects use the donor internal color format.
The final XR SRGB target receives the exact inverse transfer before hardware
encoding, preserving the donor gamma/contrast result. SSAO, ray-debug output and
reduced internal render dimensions are not yet array-qualified; their stored
settings are preserved and these paths are ineffective during XR rendering.
No eye tracking or foveation is activated by this slice.

The senior review caught exceptional-frame buffer-retirement and skipped-frame
reference-invalidation defects. Abort cleanup now drains existing donor GPU work
before another begin can rotate dynamic storage without a matching submission;
reference invalidation persists until a valid camera pose is consumed. Window
mode changes retain eye-target dimensions. A desktop screenshot-and-quit smoke
run exposed an additional shutdown race: the render task could still use Vulkan
while SDL retired its driver. Shutdown now joins that task and retires GPU work.

Local verification after implementation:

- Linux SDL3 `debugoptimized` build, including desktop and stereo shaders, passed.
- Production-boundary ownership and render-pass/recorder fixtures passed across
  desktop/stereo, all transparency modes, MSAA off/4x and SSAO requested on/off.
- Independent projection checks and production camera restoration/reference/
  abort-boundary checks passed. Wait/creation spies do not establish GPU validity.
- On this machine's RTX 4090, the stock `start` map rendered with task rendering
  and 4x MSAA; its screenshot was visually inspected. After the shutdown fix,
  the same screenshot-and-quit sequence completed with exit status 0. The test
  used an isolated temporary config and read-only links to existing game packs.
- Windows and ARM remain deferred until the end. No performance gain, multiview
  GPU validation, actual headset presentation or gameplay parity is established.

**Experimental renderer integration, not completed VR gameplay.** Current head
translation uses a temporary initial-pose anchor and the inherited default world
scale. Tracked locomotion, controller/weapon behavior, options toggles, physical
VR HUD/menu placement, the desktop mirror and OpenVR compositor support remain
migration work. Eye-only visibility, moving/scaled models, task-enabled multiview,
array effects, pause/recenter/restart and real pending-GPU abort recovery still
need an actual multiview scene test; P1 remains open. See the
[stereo senior-review disposition](migration-stereo-review.md).

## Initial checkpoint review

Astra (`gpt-6-astra`, xhigh), 2026-09-20, reviewed the introduced backend and
build changes against `30808413`. No concrete introduced defects were found
within that source-review scope. Main review confirmed that the dynamic loader
is reached only through explicit OpenXR preparation and that renderer retirement
still precedes runtime image destruction. The interrupted earlier audit was not
counted as a completed review.

| Review item | Disposition |
|---|---|
| SDL2/SDL3 loader types and base-path ownership | Retained after source review |
| Vulkan session/image lifecycle preservation | Retained; donor submission/mutex integration still required |
| No installed runtime needed for desktop startup | Preserved by dormant, explicitly invoked loader discovery; runtime qualification pending |
| Meson, Unix Makefile and native Visual Studio declarations | Source-reviewed; builds deferred |

Visual Studio project XML is well formed and retains its original BOM/CRLF.
The ten unchanged dependency/license files match the source byte-for-byte.
Whitespace checks pass for adapted project files; pre-existing whitespace in
vendored `openxr.h` is intentionally preserved. The original thirteen-file WIP snapshot was verified at that initial checkpoint;
newer master now supplies the product checkout. None of these checks establishes
build success, working VR, desktop runtime parity, or a performance improvement.

## Next integration gates

Follow the [reviewed architecture plan](vkquake-base-migration-plan.md).
The next bounded end-to-end slice is:

1. The code-level runtime/instance/device bootstrap is now implemented and
   Linux-build checked. Qualify its runtime GPU/WSI combinations as session
   integration proceeds; retain the existing owners.
2. Attach OpenXR swapchains to donor rendering. Join actual queue submission,
   not just recording tasks, before releasing images; retire task and GPU users
   before teardown. Add no second render graph or image-lifetime authority.
3. Reuse existing VR view/input/gameplay algorithms for an actual map, weapon,
   movement and HUD. Update gameplay/particles once per logical frame. Preserve
   independent live avatar poses and their shadow poses within donor models.
4. Establish task-enabled opaque multiview with moving brush lighting, restart,
   focus loss and shutdown before expanding the bulk migration. Then qualify
   Linux (including ARM64) and native Windows; headset testing remains a
   separate user checkpoint.

The donor integration points are `GL_InitInstance` / `GL_InitDevice` and the
existing submission task in `Quake/gl_vidsdl.c`. Keep its queue mutex: OpenXR
begin/end-frame and swapchain acquire/release calls can also access the bound
queue and must be synchronized with donor submissions. Released color images
must have the required attachment layout and all application accesses submitted;
GPU completion before every release is not required. Use completion waits for
resource retirement, rather than a new per-frame device-idle stall. These rules
come from Khronos's official
[`XR_KHR_vulkan_enable2` specification](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/khr/khr_vulkan_enable2.adoc).

Save dialect validation must precede world/game changes: the fork's multiplayer
version 6 conflicts with donor KEX version 6. Prediction, command timing and
server changes are one coupled migration, with explicit peer revision fixtures.
QuakeC callback ownership must remain coherent when donor GUI work uses tasks.
These are prerequisites, not optional cleanups after porting files.

## Runtime and performance requirements

- Windows, Linux, and Linux ARM64 (Steam Frame standalone) each require both
  OpenXR VR and desktop support. Linux/Monado/Beyond 2e remains the primary
  headset configuration. Steam Frame PC streaming is also a release target.
  No eye provider is assumed; desktop operation must not require an installed
  OpenXR runtime or connected headset.
- Eye tracking is an optional VR-menu toggle, off by default. Missing/invalid
  gaze renders full quality. Fixed foveation is explicit opt-in only, never a
  default or automatic fallback. Runtime support must be detected; the presence
  of a headset name or an extension declaration is not proof of working gaze.
- Reuse donor multithreaded loading/rendering, resource ownership, GPU batching,
  precision and asset-format support. Add conservative two-eye visibility and
  single-pass stereo; foveation must not cull visible peripheral geometry.
- Preserve all inherited/project behavior, including OpenVR, weapon wheel,
  prediction/co-op, saves, QC/mod compatibility, avatars/FBT, physical melee,
  Gorilla movement, akimbo, audio/voice, catalogue and configuration/assets.
- Compare complete frame CPU/GPU time and loading on large maps including
  Mjolnir `mj4m1`. No speedup is claimed before comparable measurements.

## Upstream maintenance

Keep `vkquake-upstream/master` as the untouched donor tracking ref. Merge reviewed
upstream updates into `2.0` using ordinary ancestry; do not replay the old fork's
unrelated history or repeatedly cherry-pick the entire renderer. Keep product
policy at existing narrow boundaries and record donor conflicts by subsystem.
Avoid mass formatting, renaming donor files or replacing its working owners.
Retain source licenses and provenance for each transplanted subsystem. Review
conflicts against both the product behavior reference and the donor's change.
Branch ancestry makes merging possible; it does not promise conflict-free merges.
