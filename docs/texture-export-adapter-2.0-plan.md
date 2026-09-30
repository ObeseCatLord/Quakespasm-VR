# Inherited texture export: native readback boundary

2026-09-30. Preliminary verified brief, no implementation authorized by this
document until the bounded source/design disposition closes the questions
below. Reuse vkQuake Vulkan/task/staging/texture/writer owners. This classifies
the remaining inherited command utility; it does not certify rendering parity.

## Actual contract and facts

Primary `51b452c0` `Quake/gl_texmgr.c:219–252` registers imagedump to export
current managed GPU textures at mip0 into active-game `imagedump/*.tga`. It
sanitizes colon/slash/asterisk, emits24/32-bit pixels and uses `Image_WriteTGA`.
Inherited BASE8c5a6007 already has this utility; it is not new project-authored
work. Native imagelist and source reload are not GPU-content export. Preserve
the useful operation through a native adapter, not an OpenGL texture API.

Verified native owners:

- `gl_texmgr.c:1034–1165` creates managed images: RGBA8, packed10-bit color
  lightmaps, integer surface indices, cubemaps and warp images. Ordinary/color
  lightmap images lack TRANSFER_SRC usage; warp images already have it.
  `gl_texmgr.h:65–92` has the managed image/source metadata, not VkFormat storage.
- `gl_vidsdl.c:5915` GL_WaitForDeviceIdle joins the end-render CPU task, submits
  staging work when needed and waits under the existing queue mutex. Call it
  outside texmgr_mutex; do not join workers while holding their texture lock.
- `gl_rmisc.c:763–839` R_StagingAllocate returns a command buffer while holding
  staging_mutex and increments in-flight accounting. BeginCopy/EndCopy pairing
  is mandatory even for a tiny token used solely to obtain a command buffer;
  otherwise submission can deadlock. R_SubmitStagingBuffers owns submission.
- `gl_vidsdl.c:5363–5461` native screenshot copy/map/invalidate provides reusable
  readback patterns. `gl_rmisc.c:5575/5631` owns host-visible buffer allocation/
  free. `image.c:246–279` TGA writer mutates the private RGB buffer to BGR.
- Native upload and CPU lightmap update restore SHADER_READ_ONLY_OPTIMAL
  (`gl_texmgr.c:1325`, `r_brush.c:3715`); warp compute also restores it at
  `gl_warp.c:310`. This is bounded source evidence, not all-layout proof.

## Minimal design to assess

One explicit synchronous main-thread diagnostic command, no periodic/hot-frame
work or second asynchronous readback service. Reuse existing managed list and
mutex, main-thread frame synchronization, readback allocation, submission and
image writer. Add necessary managed-image transfer-source usage at creation;
retain exact per-image format in the existing texture record if inference from
global format could be stale. Do not retain CPU copies of every texture.

Process one image/face at a time with bounded allocation; copy mip0 to private
host-visible buffer, restore the original image layout, wait for actual GPU
completion before mapping/invalidation/writing/free. RGBA8 exports directly;
packed10-bit color needs explicit normalization to8-bit output; export six
cubemap faces under distinct names. Integer surface-index resources are native
internal data, not inherited color-texture content: report them separately
instead of silently interpreting integers as RGBA. Preserve alpha when present.
Check writer dimension/integer size limits, path truncation/collisions and
success counts. Do not claim all textures dumped after failed writes/skips.

Open choices: reuse a minimal existing staging transaction for command recording
versus a short-lived diagnostic command pool; prove lock order/list lifetime,
all eligible image layouts/initialization, managed heap compatibility after
usage change and device-idle tracking. Rejected source-only redecoding because
it misses palette/colormap/downsampling/lightmap/warp GPU results. Rejected a
second general renderer/readback manager because an explicit command does not
need persistent state or a frame policy. A new render-owner design or broader
rewriting requires reopening this decision.

Expected write set `gl_texmgr.c/.h` plus at most a narrow existing staging helper
if necessary; target at most250 net production lines. Need requested-local-Astra
source disposition and verified official Vulkan usage/readback rules before
implementation. Effective reviewer settings must not be falsely certified.

## Official Vulkan evidence checked before implementation

[vkCmdCopyImageToBuffer](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyImageToBuffer.html)
requires transfer-source image usage/format support, transfer-destination buffer
usage, single-sample source and a supported actual copy layout. It also requires
a recording command buffer outside a render pass and externally synchronized
command-buffer/pool access. These rules justify a narrow usage/readback adapter,
not source redecoding or copying borrowed XR render targets.

[vkInvalidateMappedMemoryRanges](https://docs.vulkan.org/refpages/latest/refpages/source/vkInvalidateMappedMemoryRanges.html)
does not itself synchronize GPU work. Its documented chain requires a device
write, a memory dependency to host reads, device signal/host wait, then cache
invalidation. Map the whole private allocation when invalidating WHOLE_SIZE;
honor noncoherent alignment and prevent concurrent host writes. Native screenshot
allocation/copy/writer code is reusable evidence, not permission to omit the
readback buffer's transfer-write→host-read dependency. The final record must
show that dependency and completed GPU work explicitly.

The [Vulkan synchronization chapter](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)
distinguishes execution completion from memory availability/visibility. Existing
CPU task join alone does not make GPU-written bytes available to a host reader.
No new feature extension is needed merely for this core Vulkan diagnostic.

## Final qualification

No builds/tests/probes now. After all implementation, Linux/ARM checks cover
current uploaded color/alpha, colormapped/downsampled textures, updated color
lightmaps/warp results and cubemap faces; distinct safe names, writer failures,
resource retirement, repeated export and map/session teardown. Confirm continued
desktop/OpenXR rendering and native upload/task owners, including no per-frame
readback or idle wait added. User performance/headset trials remain separate.
