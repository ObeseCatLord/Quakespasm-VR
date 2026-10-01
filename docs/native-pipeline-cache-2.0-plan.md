# Reuse inherited pipeline caching at vkQuake's native owner

2026-09-30. Before-code design brief for XR-012. Actual current source baseline
`22ceceb6`; no pipeline-cache production changes yet. This restores surviving
XR work through a small adapter; it does not authorize another renderer.

## Verified current and reference behavior

- Current `gl_rmisc.c:R_CreateGraphicsPipeline` passes `VK_NULL_HANDLE` for the
  cache to both base and compatible-alternative creation. Compute creation does
  the same. Whole-source search finds no `VkPipelineCache` owner. The pinned
  vkQuake donor also has no cache owner. Do not label this donor reuse complete.
- `R_CreatePipelines` already eagerly creates known graphics/compute variants;
  `GL_CreateRenderResources` creates actual format/sample/pass topology first,
  then pipelines. Native restart rebuilds those resources on the same device.
  Existing eager creation is the reusable warmup owner, not a missing async job.
- Retained XR revision3080841333fa94000df7e1fb9e549c7158685dd6 has actual
  `vk_renderer.cpp:358–526` header/checksum/size/device validation, cache creation,
  atomic replacement and retirement. Its graphics/compute callers pass the
  cache. Reuse these algorithms by translating their small private helpers to C;
  do not transplant its renderer, C++ container state or pass cache.
- `gl_rmain.c:R_PrepareStereoView` already allocates stereo camera data from
  native `R_UniformAllocate`, reusing the scene descriptor unless protected
  projection scaling needs a second one. `glquake.h:R_BindPipeline` binds the
  existing shared dynamic descriptor/offset. No new camera descriptor cache.
- `vulkan_globals.device_properties` already supplies vendor/device/cache UUID.
  `COM_GetWriteRoot`, `Sys_fopen`, endian/CRC functions and existing platform
  atomic-file patterns are reusable. No per-mod cache or resource registry.
- `VID_Shutdown` joins native rendering before device-child retirement. General
  live incompatible-device replacement/device-loss recovery is explicitly
  deferred; cache lifetime must not introduce it.

Official [pipeline management guidance](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_cache/README.html)
describes early creation and driver cache persistence to reduce repeated
compilation. The [version-one header](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCacheHeaderVersionOne.html)
is32 little-endian bytes with vendor/device/UUID identity. The
[data query](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPipelineCacheData.html)
can return incomplete data; accept only a complete bounded successful snapshot.
These requirements support the adapter; no measured startup or frame-time
benefit is claimed for this project.

## Smallest adapter and alternatives

Lean: one private device-owned `VkPipelineCache` in `gl_rmisc.c`, initialized
before the first native pipeline creation, reused through same-device desktop/
stereo/resource restarts. Pass it to all three existing creation call sites.
The driver retains its own full creation-input identity; existing native pass/
sample/format/view-mask variants remain untouched. A second application-level
pipeline-key map or async compiler would duplicate native pipeline ownership.

Translate the retained XR64MiB bound,16-byte version/size/CRC envelope and32-byte
Vulkan payload validation at exact offsets. Load only an explicit file under
the native write root, never from mod/pak search paths. Reject short/oversized/
truncated/corrupt/wrong-device data and initialize empty. If non-device-loss
initial-data creation fails, retry empty once; if cache creation remains
unavailable, retain native null-cache creation. Device-loss handling follows
native fatal policy; it is not ordinary cache fallback or live reconstruction.

Reuse XR temporary-file/atomic-replacement algorithms and platform Unicode
paths; preserve an existing file on snapshot/write/close/replace failure.
No mandatory new config/UI/migration command or game/asset modification.
Optional allocation and file failures must not make gameplay unavailable.

Proposed save point: after each joined/eager `R_CreatePipelines` completion,
before rendering starts. That captures actual current variants and leaves no
first cache miss waiting for normal exit. It performs no steady-frame or map
work. Proposed retirement: one idempotent shutdown function called from joined
`VID_Shutdown`; ordinary `R_DestroyPipelines` keeps the same-device cache.
Do not share a cache across devices or retain it after its device is destroyed.
Review whether those exact owners cover all existing device-failure paths.

Expected production write set: `Quake/gl_rmisc.c`, `Quake/glquake.h`,
`Quake/gl_vidsdl.c`; roughly250–300 helper/hook lines, largely retained XR
translation. No new source module/build recipe, renderer abstraction, pipeline
registry, worker/task, shader, pass topology or broad filesystem rewrite.
Reopen if implementation exceeds this or requires another lifecycle owner.

## Senior design questions before implementation

Request local Astra xhigh/max on this verified brief, then main source-check and
record disposition before delegating coding. Challenge whether persistence is
required by retained XR-012, whether eager creation already covers warmup,
whether save-after-creation/explicit shutdown is the narrowest correct lifetime,
which existing atomic-file code can be reused without creating a service, and
whether optional allocation/failure/threading/driver identity safeguards suffice.
No effective reviewer-settings certification is available. Production is held
for that design disposition, not for user permission.

## Final software qualification

After all implementation, use actual native creation with disposable Linux and
ARM write roots: cold/valid/wrong-device/driver-UUID/corrupt/truncated/oversized
files; unavailable or read-only storage; failed cache creation/snapshot/atomic
replacement where meaningful facilities exist. Prove the driver receives the
cache in base/alternative/compute calls and actual output still renders.
Exercise desktop/stereo, native MSAA/AO/OIT/foveation variants, resource restart,
explicit XR attach/disable/recovery and shutdown. No draw-time creation or new
camera descriptor churn. An isolated header fixture is supporting evidence only.
Quantitative timing and live headset trials are user-deferred; Windows builds
remain later work. No builds/tests/probes/benchmarks before implementation ends.
