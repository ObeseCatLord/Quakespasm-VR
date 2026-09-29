# OpenXR Vulkan device reconstruction

Status: verified design brief; production reconstruction is not implemented.
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

For missing mesh upload arrays, compare two narrow options before coding:
reparse immutable asset sources through existing loaders into the same model
identity, versus retain minimal upload recipes/arrays at load time. Prefer
reparse when it preserves CPU pointers and avatar/held recipes without a second
model owner; retain bounded immutable input only where existing generation
cannot be replayed safely. Neither choice has been proven yet. A complete
asset-size RAM copy is not the default, especially for jumbo maps.

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
The immediate stage is the existing texture-retirement correction. Source
review and `git diff --check` are allowed now; builds/tests remain deferred by
the user's instruction until full implementation is finished.
