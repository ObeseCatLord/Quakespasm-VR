# OpenXR Vulkan device reconstruction

Status: scope reopened after the user's Vulkan-reuse question. Further device
reconstruction implementation is paused pending the focused Astra review below.
Earlier local Astra Max design disposition recorded; full production reconstruction
is not implemented. Texture-retirement, alias replay and brush vertex regeneration
prerequisites pass bounded final source review.
Compatible-device late attachment already uses the separate
[late-binding plan](openxr-late-binding-2.0-plan.md). This plan covers the
remaining incompatible-device case without replacing vkQuake's renderer.

## Behavior and reference

After desktop play, an explicit `vr_enable 1` should be able to select the
runtime's Vulkan GPU/API/extensions, rebuild graphical resources, and resume
the same game and connection. A compatible binding keeps the existing fast
attachment path. Switching VR off returns to desktop rendering on the current
device; it does not gratuitously switch GPUs again. Ordinary `vid_restart`
continues to mean a render-resource/window restart.

Actual Vulkan device loss is a different failure class. It cannot use a healthy
device's staging-submit/wait contract. Recovery must stop further submissions,
retire CPU tasks, tear down device-owned objects and rehydrate them through the
same owners. Do not claim device-loss recovery from session recovery or from
an ignored `vkDeviceWaitIdle` result.

Read-only source references inspected for this design:

- The product's current primary branch is `master`, not a local `main` ref.
  Reference revision `51b452c018273647dcf94f4628a370267ff8fa91`:
  `Quake/gl_vidsdl.c:886` deletes old graphics objects before creating new ones,
  then calls `TexMgr_ReloadImages`, `GL_BuildBModelVertexBuffer` and
  `GLMesh_LoadVertexBuffers`. `Quake/gl_texmgr.c:1435` walks existing texture
  records. Reuse this separation of GPU objects and game state, not OpenGL
  context/global-state code. The existing primary checkout remains read-only.
- Local vkQuake donor `Quake/gl_vidsdl.c:4114` retains the device during
  `VID_Restart`. Its texture uploads, heap allocation, staging, descriptors,
  pipelines and model loaders remain the implementation owners in `2.0`.
- Ironwail `Quake/gl_texmgr.c:1749` and `Quake/gl_mesh.c:315` have the same
  record/re-upload separation. They do not supply a Vulkan recreation contract.

Official requirements: destroy all explicitly destroyable/freeable device
children before destroying the device, with external device/queue
synchronization ([Vulkan device destruction](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyDevice.html)).
Waiting may report device loss ([device idle](https://docs.vulkan.org/refpages/latest/refpages/source/vkDeviceWaitIdle.html)).
The runtime's physical-device query is system/instance specific
([OpenXR Vulkan GPU query](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrGetVulkanGraphicsDevice2KHR.html)).
These requirements justify resource reconstruction; they do not certify our
recovery implementation or require a new renderer abstraction.

## Verified incompatibilities and reusable owners

Current `2.0` evidence, before implementation:

| Owner | Existing boundary | Demonstrated gap |
| --- | --- | --- |
| Device/instance/window | `gl_vidsdl.c:1192`, `:1573`, `:6340` | Init creates a device; restart rebuilds same-device render resources; final shutdown is not a complete live device-child teardown. |
| Runtime and borrowed images | `GL_OpenXRRetireImages`, `GL_OpenXRAttach`, `VRXR_AdoptVulkan` | Compatible adoption rejects incompatible actual creation metadata. Existing callback retires framebuffers/views before runtime image destruction. |
| Textures | `TexMgr_DeleteTextureObjects`, `TexMgr_ReloadImage`, `TexMgr_InitHeap` | GPU deletion preserves records, but no Vulkan reload-all/device-heap teardown contract. Deferred collection frees the sampled descriptor again when a storage descriptor exists. Deleted records retain descriptor handles. |
| Alias models and avatar props | `GLMesh_DeleteAllMeshBuffers`, `GLMesh_UploadBuffers` | Delete also frees CPU skin texels. MD3 and MD5 loaders free upload arrays after upload; an OpenGL-style re-upload loop cannot assume those arrays survive. |
| Brush/lightmap/indirect data | `GL_BuildLightmaps`, `GL_SetupLightmapCompute`, `GL_BuildBModelVertexBuffer` | These rebuild by map-load policy, not pure device ownership. Lightstyle upload sources and surface-index arrays are freed; some retained texture source offsets cannot be used as general replay data. |
| Staging/dynamic allocations | `R_InitStagingBuffers`, `R_InitGPUBuffers`, garbage collectors | Staging destroy helper currently only frees buffers/memory. Fences, command pool, dynamic buffers and garbage need one owner-local retirement contract. Mutex/condition initialization must not leak on repeated recreation. |
| Descriptor and pipeline layouts | `R_CreateDescriptorSetLayouts`, `R_CreateDescriptorPool`, `R_CreatePipelineLayouts` | Recreate only after old sets/layouts/pools are retired; reset allocation accounting and all cached device handles. Existing stereo-only layout teardown is insufficient. |
| Effects and tracked presentation | `R_DestroySSAO`, `R_VRIKRenderShutdown`, FTE particle GPU buffers | Keep algorithm/settings and particle/game state; add missing GPU-only retirement where needed. CPU palette/admission and model identities must remain valid or be explicitly refreshed. |
| Draw submission and command contexts | `GL_InitCommandBuffers`, end-render task, queue mutex | Join producers before retirement, clear command/fence/query state, rebuild contexts for the new device. No old handle may escape through queued screenshots or worker arguments. |

The texture findings are a bounded prerequisite correction, not evidence that
the entire renderer must be replaced. GPU model data cannot be recreated by
adding empty hooks or by calling `Mod_ResetAll`: that destroys model identities
used by client/server state. `R_NewMap` also resets particles, lightstyles and
other gameplay presentation, so it is not a transparent device restore.

## Adapter versus replacement

Choose owner-local GPU shutdown/recreate adapters and one main-thread transaction
at the existing joined frame boundary. Preserve vkQuake rendering, texture
records, mesh loaders, CPU BSP data, entity/model identities, netcode, sound and
SDL window/input. Keep creation metadata at its existing actual Vulkan creation
owner. Do not add a second renderer, resource registry, shadow model database,
connection protocol or independent session state machine.

For missing alias upload arrays, retain final transformed upload payloads in
the existing model/surface owners and share the GPU creation implementation.
Astra verified that reparsing replaces alias headers and breaks admitted
immutable avatar snapshots even if the qmodel_t address survives. Reuse existing
retained MD5 joint poses wherever lifetime permits; avoid another complete
model source copy. Aggregate added alias RAM is not yet qualified. Regenerate
brush inputs from retained BSP data, rather than retain whole jumbo-map GPU
upload copies; that distinct path still needs its bounded contract and proof.

Do not swap global `vulkan_globals` between two live devices. Capability,
descriptor/layout, heap, queue and cached-handle owners are single-device.
That would require extensive duplicated state and an unproven rollback scheme.
Preflight runtime availability, GPU presentation support and device requirements
before destructive retirement. Keep the old desktop output if preflight fails.
After retirement, reuse real startup creation/fallback logic with explicit
failure outcomes; no success message until a restored frame is usable.

## Stages and ownership

1. **Texture retirement prerequisite.** Bounded edits to `gl_texmgr.c` only:
   free a deferred storage descriptor using its own handle, clear both retired
   descriptor fields, retain records and source metadata. No new reload-all
   call, device destruction or switch command. Source review first; end-goal
   checks use sampled-only, sampled+storage and both garbage slots.
2. **Owner completeness.** Audit and add GPU-only retirement/recreation in
   `gl_rmisc.c`, `gl_mesh.c`, `r_brush.c`, `r_part_fte.c`, `gl_texmgr.c` and
   `gl_vidsdl.c`, with existing headers. Preserve CPU assets and state. Define
   healthy versus lost-device teardown policy at the queue owner, not an
   arbitrary global bypass of every wait. Commit narrower owner plans before
   changing lifetime contracts, especially mesh and lightmap data.
3. **Asset reconstruction.** Prove stock MDL plus MD3/MD5, generated held models,
   custom avatars/props, WAD3 palettes, cubemap sky and computed lightmaps with
   original resource owners. Do not call ordinary texture reload indiscriminately:
   six-face cubemap reload and freed lightmap computation inputs are distinct
   demonstrated boundaries. Skyboxes remain required; skyrooms remain excluded.
4. **Smallest transaction.** Loaded desktop stock scene -> explicit VR request
   requiring a new binding -> retire/create using real OpenXR enable2 wrappers
   -> restore scene -> attach -> submit. Preserve server/client connection,
   model/entity references, settings and running audio. First implement healthy
   explicit switching; do not entangle device-loss detection with this proof.
5. **Failure/recovery.** Requalify rediscovered runtime/API/GPU, unsupported
   presentation queue, extension refusal, asset-source absence, partial creation
   failure and actual device loss. Refuse preflight cleanly; teardown must be
   idempotent after partial creation. Runtime absence is not a reason to destroy
   a healthy desktop device. Use existing explicit attempt latch; no retry loop.
6. **Qualification at the end of the full implementation.** Linux and native
   Linux ARM builds, software end-to-end loaded-scene graphics/connection proof,
   validation/resource-lifetime checks and final local Astra review. Windows
   builds, actual headset/gaze trials and performance measurement stay deferred.

## Open decisions and review

Senior review must verify the above sources and challenge whether this broad
work is necessary, whether a smaller startup policy achieves the same behavior,
and whether reparsing can preserve model identities and game state. Estimate:
multiple coupled owner slices, not a one-file `vid_restart` change. Stop and
reopen if it introduces a parallel asset/resource state machine or repeatedly
fixes interactions between newly duplicated owners.

No device reconstruction or device-loss success is claimed at this checkpoint.
The [alias replay prerequisite](openxr-alias-replay-2.0-plan.md) is now source-
implemented in32ca78f7 with final local Astra acceptance, including private-prop
joint lifetime; its new APIs are not yet wired to a live switch transaction.
The [brush vertex regeneration prerequisite](openxr-brush-vertex-replay-2.0-plan.md)
is also source implemented with final Astra acceptance, without another retained
geometry copy. Next is separate lightmap/derived-input reconstruction. Source
review and `git diff --check` are allowed now; builds/tests remain deferred by
the user's instruction until full implementation is finished.

## Local Astra Max disposition

| Verified finding | Main disposition |
| --- | --- |
| Primary enables VR on demand; startup-only requirements lose that behavior. | Reject startup-only completion. Keep compatible adoption and implement incompatible reconstruction at existing owners. |
| Alias loader reparsing replaces referenced headers and custom avatar admission snapshots. | Adopt retained final alias payloads/shared upload; preserve qmodel/header/held/prop identity. |
| Brush polygons, lightmap upload arrays and compacted membership data cannot simply be replayed by calling map-load functions. | Adopt separate BSP-derived regeneration, preserving particles/lightstyles and avoiding permanent jumbo-map GPU copies. |
| Scrolling sky sources are temporary; cubemap filename is synthesized; warp targets have no source payload. WAD3 mipmaps/palettes already survive. | Use narrow sky/warp restoration. Delete the proposed need for another WAD3 cache. No skyrooms. |
| Joined frame boundary still has in_update_screen=true; deletion queues garbage. | Explicitly drain both slots before retiring old heaps/pools, including entity/static/private-prop and brush acceleration resources. |
| Adoption failure conflates unavailable runtime and incompatible binding; current multiview rejection precedes rediscovery. | Add typed qualification outcomes before any destructive switch; preserve old creation metadata until actual retirement. |
| Startup Sys_Error paths and -openxr gating are not a live fallback transaction. | Define preflight and post-retirement failure handling before activation; healthy runtime absence retains desktop. |
| Partial resource guards, stale handles and heap CPU allocations leave unwind gaps. | Move owner-local partial cleanup before the first transaction; include FTE mappings, SSAO private layouts, command/fence/query state. |
| Healthy wait submits staging, ignores result and marks idle; it is not lost-device proof. | Keep loss handling separate, with no new submissions after confirmed loss. |
| Texture storage free/cleared handles patch is correct at the existing boundary. | Accept bounded three-line patch35c5cf91. No broader lifetime or reconstruction qualification implied. |

Review evidence: gl_model.c:4910/5035/7446/7746/8334, gl_mesh.c:517/538,
gl_sky.c:126/499, r_brush.c:2498/2514/2570/2664, gl_screen.c:2497,
gl_heap.c:624, vr_openxr.cpp:1629 and gl_vidsdl.c:4686/5837. Main inspected
the upload and deletion/retention boundaries before choosing the next write set.
No builds, tests or live device work were performed for this review.

### Brush/lightmap source checkpoint before a bounded implementation brief

Direct current2.0 source inspection confirms the parent disposition still
applies. `r_brush.c:2629` uploads every brush surface from `s->polys->verts`,
then releases ordinary polygons at2700; tiled polygons remain. Calling that
upload again on a loaded map therefore cannot replay all surfaces unchanged.
The existing `BuildSurfaceDisplayList:1619` reconstructs those ordinary
vertices from retained BSP edges, vertexes, texture vectors and current
lightmap coordinates. Reuse its vertex calculation rather than create a
second brush source/payload cache. Its current currentmodel/vertex-base globals
and polygon allocation require a bounded owner-local contract before edits.

`GL_BuildLightmaps:2053` reallocates atlas assignments, surface data and draw
membership; `GL_SetupLightmapCompute:2460` frees lightstyle/surface-index/
workgroup upload inputs and compacts used-lightstyle membership in place.
They are not GPU-only replay functions. `R_NewMap` also clears particles and
resets live lightstyles, leaves and frame counters, so using it as reconstruction
would alter the running game. Distinguish regenerated derived inputs from live
scene state in the next brief. The bounded vertex adapter is now implemented;
full brush/lightmap reconstruction is not claimed.

The lightmap follow-up must preserve existing texture-record ownership too.
Current TexMgr_LoadImage atgl_texmgr.c1473 allocates a new record unless
TEXPREF_OVERWRITE is supplied; that flag alone is not a reconstruction path,
because an equal CRC returns the existing record before GPU upload. Ordinary
lightstyle and surface-index sources are freed after compacting/upload. These
source facts require a bounded regenerate-and-restore contract; neither new
duplicate records nor a cache-hit return proves new-device image creation.

## Heap destruction prerequisite, verified source plan

The existing GL_HeapCreate and GL_CreateHeapSegment allocate their opaque CPU
owner structs in gl_heap.c, but GL_HeapDestroy currently releases only nested
arrays and Vulkan segment memory. Its only current caller is the debug heap
exercise, which frees every allocation before destruction and never uses the
heap afterward; no caller already frees the root/segment structs separately.
Narrow correction: destroy those existing owned structs at the same teardown
boundary, permit null input, and assert that all allocation records (including
dedicated allocations) were retired first. Document that the existing API
consumes its opaque owner and callers must clear their own pointer. Expected
under20 source/header lines; no allocator rewrite, registry or new GPU policy.
This is the parent plan's already-dispositioned CPU heap leak prerequisite,
independent of the pending brush GPU design. Source review/git diff --check now;
existing heap exercise and full reconstruction checks remain end-of-goal only.


Heap prerequisite source integrated: null guard, live-allocation assertion and
frees for the existing segment/root structs, with consuming-owner declaration.
Main compared allocation/free/call sites and the eight-line source/header change; git diff --check
passes. No new heap retirement callers or device-switch claim. Source-only
verification; no builds/tests/fixtures. Final full-goal senior review/qualification
still includes actual heap destruction under the parent GPU retirement order.

## Fan-index allocation ownership prerequisite

Verified before implementation: R_InitFanIndexBuffer in gl_rmisc.c keeps its
persistent buffer in vulkan_globals, but stores the allocation in a local
VkDeviceMemory that is lost on return. No existing destroy path can release
that allocation explicitly. Keep the allocation at the existing dynamic-buffer
owner as a native vulkan_memory_t and use R_AllocateVulkanMemory instead of
duplicating allocation/accounting. Preserve buffer flags, memory selection,
binding offset, fan index generation and staging. Expected under20 changed
source lines; no new resource registry, retirement caller or submission policy.
The forthcoming owner retirement will release the buffer and this allocation
before destroying the device. Source review now; builds/tests only at the end.

Source integrated: native vulkan_memory_t retention and the existing allocation
helper replace the local allocation/manual counters. Main reviewed the ten
changed source lines against R_AllocateVulkanMemory/R_FreeVulkanMemory; index
generation and staging are unchanged, git diff --check passes. No retirement
caller or device reconstruction is claimed; no builds/tests were run.

## Primary reference check

The actual read-only primary master51b452c0 was reread for this continuation:
Quake/vr.c:7346 suppresses physical trigger by immersive-melee profile identity;
Quake/cl_input.c:1086 clears BUTTON_ATTACK before command history/send;
Quake/vr.c:7730 uses a scoped held-model ready pose and restores cl.viewent and
currententity without rewriting QC animation state. The bounded gesture-only
adapter follows those ownership/presentation principles; deferred hybrid and
physical-contact modes are not imported as new policy.
Quake/gl_vidsdl.c:920..936 retires old graphics before recreation and reuploads
existing texture/model owners. Vulkan reconstruction follows that behavioral
reference through vkQuake's Vulkan owners, preserving the loaded game.

## Scope reassessment brief: preserve the donor renderer

Trigger: the user asked why Vulkan is being changed extensively when vkQuake
already provides a good Vulkan renderer. This is a solo-maintainer migration,
not a request for a general live multi-device recovery framework. The migration
must preserve desktop, native graphics and performance goals, OpenXR on the
named targets and the actual primary VR behaviors. Avoid silently converting a
convenient extension of those behaviors into mandatory architecture.

Verified by direct source reads:

- GL_OpenXRPrepareVulkan (gl_vidsdl.c:924) already performs explicit -openxr
  startup negotiation through enable2 before GPU assets are loaded. Desktop
  without that argument retains donor GPU selection and does not require XR.
- GL_OpenXRAttach (:4689) and VRXR_AdoptVulkan (vr_openxr.cpp:1629) already
  support compatible desktop-device attachment, actual creation metadata,
  legacy Vulkan qualification and explicit session disable/re-enable. Refusal
  preserves desktop and recommends restarting with -openxr. They do not perform
  different-device replacement. No need to implement that path merely to
  obtain basic OpenXR VR or preserve vkQuake's Vulkan rendering.
- Primary master51b452c0 VR_Enabled_f/VR_Enable/VR_UpdateScreenContent enables
  OpenVR on demand on the existing OpenGL context. This demonstrates a mode
  toggle; it does not demonstrate live GPU migration or device-loss recovery.
  The primary restart at gl_vidsdl.c:920 recreates GL objects after a video-mode
  change; Vulkan VID_Restart retains its device. Different API lifecycle alone
  is not a regression requiring a general replacement transaction.
- Recent alias and lightmap reconstruction APIs have no production transaction
  callers. Their existence is not functionality or a completion gate by itself.
  Alias upload retention does add CPU memory at normal loading; assess that
  cost explicitly rather than describe all prerequisites as inactive.
- The original feature rows VR-001/VR-002/XR-001/XR-002 require explicit VR,
  runtime/device compatibility and reconnect safety. Later documents added
  full incompatible-device and Vulkan-device-loss reconstruction as parent
  scope. That addition was agent interpretation, not a separate user request.

Official source rechecked: [enable2 specification](https://raw.githubusercontent.com/KhronosGroup/OpenXR-Docs/main/specification/sources/chapters/extensions/khr/khr_vulkan_enable2.adoc)
requires compatible creation/runtime GPU and supplies startup wrappers.
[Original Vulkan binding](https://raw.githubusercontent.com/KhronosGroup/OpenXR-Docs/main/specification/sources/chapters/extensions/khr/khr_vulkan_enable.adoc)
permits qualified existing bindings. Neither requires transparent live asset
reconstruction on arbitrary runtime/GPU changes.

Proposed minimal route: keep explicit VR startup, same-device mode toggles and
compatible desktop late attachment. Incompatible API/GPU/extensions refuse
cleanly and explain the restart route; desktop stays available. Ordinary
desktop must not unconditionally discover XR/select its GPU. Keep donor
rendering plus necessary multiview/pass/foveation adaptations. Defer general
incompatible-device reconstruction and transparent device-loss recovery unless
a concrete target/use case establishes the need. Do not claim every arbitrary
hot-connect case supported by this route. User hardware/performance testing
and Windows builds remain excluded; Linux/ARM qualification comes at the end.

Astra contract: personally verify then challenge the framing and minimal route.
Read-only primary/donor/current creation and toggle owners, associated scope
rows and recent prerequisite call sites. No edits, builds/tests, nested agents
or unrelated feature audit. Return <=850 words: necessary rendering adaptations
versus speculative lifecycle work; precise observable limitations; recommended
scope disposition; smallest safe dormant-helper/payload cleanup follow-up.
Do not demand new layers to make an unused fallback complete. Main owns final
scope judgment and writes. The separate Stage3 brush design is conditional
acceptance with source/order/ownership issues, not authorization to continue
through this reopened architecture decision.
