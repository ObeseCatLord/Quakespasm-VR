# F06 native device-selection qualification plan

2026-10-01. Frozen final owner F06; no feature expansion. GL_InitDevice already
owns discovery, candidate qualification, selection and the actual device-create
feature/extension chain. Reuse this whole native owner and existing renderer
fixture support. Do not extract/copy its policy, add a new selector, change the
renderer or reopen accepted two-view FB/META/KHR architecture.

Main source verification: gl_vidsdl.c GL_InitDevice queries extension/property/
feature/format/sample candidates; complete offsets plus runtime support prefer
FB/META. Explicit fixed requests permit FB; ordinary desktop prefers available
KHR; no-KHR desktop can prepare FDM for later attachment. Density guards reject
sample shading and explicit render size. Selected feature chain is terminated
before creation so unselected queried feature nodes cannot leak into VkDevice.
Native format helper checks RG8 two-layer single-sample and all scene color/depth
formats at the actual requested native MSAA. Invalid gaze/default mode does not
select fixed in the existing frame-policy owner; retain its prior results.

Minimal route versus rewrite: invoke actual GL_InitDevice under controlled
discovery dispatch, intercept its final VRXR_CreateVulkanDevice/vkCreateDevice
request and inspect extension names/feature nodes independently. Stop at that
boundary before a driver sees invented capability results. This is software
selection proof, not actual device creation, runtime borrowed images or headset
performance. A copied boolean policy would only mirror implementation; a new
renderer/device owner is unnecessary. Existing native GPU KHR evidence remains.

## Finite cases

Require selected extension and matching feature, never both FDM and KHR; queried
unselected features must not leak into the create chain. Each case resets fixture
and native globals/cache; no assigned selected feature flags.

1. Both complete candidates, startup XR with eye/offset/flags support: FB/META.
2. Same complete GPU, ordinary desktop: KHR.
3. Desktop with only FDM: prepare FDM.
4. Startup XR missing eye/offset/flags capability: KHR, no selected offset.
5. Startup XR explicit fixed mode with FB support but no eye offsets: FB.
6. Startup XR sample shading or explicit paired render size: KHR.
7. FDM non-subsampled, RG8/array, offset-format or required MSAA rejection: KHR.
8. Neither candidate or -novr: no foveation family.

Use compact table-driven test inputs/independent expected families. Controlled
queries may prepare extension/property/feature/format replies but must not
fabricate production candidate decisions. Observe native discovery and final
create request, bounded pNext walk; no fake successful device or GPU work.
Native allocations before the creation intercept are retired normally; no
parallel resource tracking framework. Query structures remain input-only.

Luna gpt-6-luna/xhigh coding ownership: tests/openxr_device_selection_fixture.c
ONLY new file (<=360 lines), plus <=20 lines in tests/openxr_enable_fixture.c
to guard only its existing command/runtime capability and image-view-destroy
spies for reuse. Rename
inherited main with a local macro; keep ordinary fixture behavior unchanged.
Do not copy bootstrap/spies already reusable. Supply only additional required
query/capture/support spies. A local setjmp/longjmp at the final create callback
is acceptable because every earlier native allocation is freed by then.
Main owns source review, docs, native compilation/execution and final claims.
No production edits, branch checks, builds/game/commits or physical devices by
worker. If bounds/dependencies require broader rewriting, report and stop.

Main runs existing renderer fixture after integration to protect its unchanged
behavior, then new selection component after implementation. Retain failed logs.
Actual GPU protected output, constructors and unavailable FB/META/gaze runtime
execution remain separate existing F06 boundaries; no full F06/F10 closure from
this component. Main/reference/user migration doc/assets remain untouched.

Main's disjoint constructor slice: new tests/openxr_image_view_fault_fixture.c,
<=170 lines, includes the same renderer fixture with only its destroy spy
guarded. Invoke actual GL_CreateXRImageViews/DestroyXRImageViews for valid
paired images, failed second density-view creation, absent later density image,
insufficient density extent and missing density maps. Track exact successful
native view handles and ordered one-time destruction; borrowed VkImages remain
unowned. Optional failure must retain all ordinary color views, retire earlier
density views and set the existing backend latch. Repeat creation cannot add
views; final native retirement clears them. Dispatch/metadata are controlled
inputs; no real borrowed map/device, no allocation-failure or render-pass/
framebuffer-constructor claim. Main tests actual-source creator before linking
its existing OFF/recovery evidence, rather than pretending a prepared latch is
constructor proof. No duplicated constructor/bootstrap/resource framework.

Official sources re-read2026-10-01: [Valve's custom-engine guide](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
still recommends OpenXR, Linux ARM64 or Android, the six FB/META foveation
extensions and EXT gaze. [VkDeviceCreateInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkDeviceCreateInfo.html)
VUID04481–04483 forbids enabling density maps together with any KHR shading-rate
feature. [Density-map feature description](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceFragmentDensityMapFeaturesEXT.html)
distinguishes queried support from enabled device features and requires
non-subsampled support for this existing ordinary-image renderer route.
[Borrowed XR map interface](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSwapchainImageFoveationVulkanFB.html)
provides paired image/extent; it does not expose format/layers/readiness in its
returned struct. Selection/constructor fixtures do not attest to those runtime
integration assumptions or target performance. No quad views are introduced.

## Estimate reopened before acceptance

Luna's first implementation is402 lines versus the360-line estimate. Main read
the complete file: extra code is explicit case data and the required native
query/create-boundary replies, not a copied GL_InitDevice selector, resource
manager or new production state. Keep that reuse design; permit up to450 lines
including main's independent-capability cases. Do not cosmetically compress code
to satisfy a count or move policy into a new production helper for testing.

Main found two format negatives lacked a complete positive FB candidate, so
their expected KHR result could pass without the intended format rejection.
Seed those from the working FB/offset/flags inputs and vary only RG8/layers.
Similarly split unavailable eye, flags and offsets into individual mutations
of the positive case. This changes assertions/evidence, not product policy.
Perform these main integration corrections after coding handoff, before native
execution. Reopen again if additional dependencies or wider rewrite appear.

Final integration remains inside the reopened bounds: selection441 lines,
constructor148 lines, reusable guards6 lines. Local Astra/xhigh found two
assertion gaps; main strengthened feature/dependency checks and per-view source
correspondence, then compiled/executed each affected component once successfully.
No production change or architecture expansion. See the
[final senior disposition](foveation-device-final-2.0-review-brief.md#final-disposition)
and linked result receipts. This finishes this finite software qualification
slice; it does not close the whole frozen F06/F10 owners.
