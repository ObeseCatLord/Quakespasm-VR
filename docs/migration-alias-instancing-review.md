# Opaque alias instancing within vkQuake's draw owner

Branch `2.0` adapts Ironwail's adjacent alias batching to vkQuake's existing
vertex streams, dynamic uniform allocator and command contexts. Compatible
opaque MDL/MD3 surfaces can share one indexed instanced draw in desktop and
OpenXR stereo. `r_aliasbatch` defaults to 1; 0 disables grouping. This is a
bounded first adapter, not Ironwail's arbitrary-pose geometry-fetch renderer or
a measured improvement on `mj4m1`.

## Reuse and scope

`R_DrawAliasModel` still owns reload, early visibility rejection, skin/geometry
selection, animation interpolation, transforms, lighting, player colors and
avatar presentation. `R_DrawAliasSurfaces` still resolves fullbrights and cheat
modes per surface. The batch receives the resulting values; it does not repeat
those policies or prepare another scene. Culled models remain rejected before
expensive alias preparation.

The retained vertex streams bind texture coordinates plus two poses. Instances
therefore share their actual geometry buffers, index range and pose offsets,
but keep distinct world matrices, shade vectors, light colors and interpolation
fractions. Resolved diffuse/fullbright descriptor sets, flags, alpha and pipeline
(including winding) must match. This preserves the fragment shader's existing
record-zero uniform prefix. Alpha-pixel textures are immediate even when their
entity entered the opaque list. MD5/MD5-8 palettes, transparent/OIT surfaces,
showtris, co-op overlays and unlit wheel models retain immediate handling;
viewmodel draws occur outside the scoped world-entity batch.

Each opaque `R_DrawEntitiesOnList` invocation owns one stack batch, referenced
only by its task's command context. No global queue or opaque sort is added.
Flush precedes brush/sprite calls and immediate alias draws, happens on a key
change or the 16-instance capacity limit, and runs before the invocation exits.
Enqueueing itself emits no GPU commands. The flush binds its saved state before
drawing. Context entry asserts that no previous stack pointer survives, and end
clears the pointer. Task-local order is retained; global entity order was already
subject to vkQuake's atomic work distribution among contexts.

The existing dynamic allocator uploads all instance records in one contiguous
allocation. Its mutex, buffer growth, 2048-byte descriptor-tail reservation and
GPU retirement remain authoritative. The shader/C record has the same first
100 bytes as before and a std140 array stride of 112; 16 records occupy 1792
bytes. The capacity check uses the allocator's shared `MAX_UNIFORM_ALLOC`, not
an independent buffer policy. Single draws use record zero and `firstInstance`
is always zero. The desktop and stereo vertex variants share the same source;
eye projection continues through `gl_ViewIndex` independently of instance index.

## Astra senior-review disposition

Astra reviewed the mostly-worked design against the actual renderer, allocator,
shaders, task contexts and Ironwail source. The main thread verified effective
`gpt-6-astra` / `max` settings from local metadata and spot-checked each
load-bearing boundary. The reviewer could not certify its own route; the main
thread's metadata check supplies that evidence without exporting session logs.

| Recommendation | Disposition |
| --- | --- |
| Reuse the draw preparation and dynamic UBO; avoid arbitrary-pose SSBO geometry fetching for this vertical proof. | Adopted. Existing vertex streams require matching bound poses; no descriptor, pipeline family, geometry format or lifetime owner is replaced. |
| Decide deferral before binding a pipeline or changing push constants. | Adopted. CPU records and keys are prepared first; the shared flush/single helper emits GPU state. MD5 and immediate aliases flush first. |
| Resolve material alpha, translated skins, cheats and winding before batching. | Adopted. Surface eligibility checks actual texture alpha pixels, and the key includes resolved descriptors, flags, alpha, pipeline and all geometry bindings. |
| Allocate the entire record array once. | Adopted. Per-instance dynamic allocations would have device-aligned gaps and cannot serve as a 112-byte-stride array. |
| Add a separate single-draw shader/pipeline family. | Rejected. The existing descriptor covers the array; single draws access record zero. No demonstrated interface incompatibility requires a parallel family. |
| Treat batching-off as independent shader validation. | Rejected. Off still uses the array shader, so it proves grouping policy only. A pre-change-shader framebuffer comparison is a separate proof. |
| Promise speedup or globally unchanged task order from command counts. | Rejected. Compatible calls merge, but grouping depends on visibility, materials, poses and scheduler distribution. The user will measure actual performance later. |

## Software evidence and limits

The native Linux SDL3 build and production desktop/stereo SPIR-V validation
pass. Disassembly confirms member offsets 0/64/76/80/92/96, array stride 112,
`InstanceIndex`, and the stereo variant's separate `ViewIndex`. The original
fragment prefix stays unchanged.

`tests/alias_batch_fixture.c` executes `GL_DrawAliasFrame` and its production
batch helpers with a captured Vulkan dispatch and test allocator. ASan/UBSan
passes. Two distinct ordinary draws become one two-instance draw, with bytewise
matching per-instance uniforms and geometry/material state. The fixture checks
16/17 capacity, pending work before any GPU emission, final partial batches,
material/pose/winding/index/buffer/texture-coordinate changes, fullbright and
cheat changes, alpha-pixel and translucent exclusions, MD5, showtris, co-op and
unlit fallbacks, explicit non-alias flushes, interleaved contexts, and MD3 stereo
descriptor binding. Its colored-skin case stages the resolved descriptor; it
does not execute the player-color texture generator. The non-alias boundary
check invokes flush directly; renderer glue is separately inspected. It is not
a rendered-image, driver-allocation-growth or actual worker-scheduling proof.

A headless real Vulkan framebuffer comparison is being implemented separately
using the exact pre-change shader at `62de7c26`, current batching-off and current
batching-on variants. Its result must be recorded before treating shader/image
equivalence as established. Headset/runtime qualification and performance
measurements remain with the user. Existing logical alias counters count
accepted entities, not compressed physical draws.

## Official and primary references

- [Khronos shader memory layout guide](https://docs.vulkan.org/guide/latest/shader_memory_layout.html): std140 extended alignment for uniform arrays.
- [Khronos indexed draw reference](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDrawIndexed.html): instance count and first-instance semantics.
- [Khronos descriptor binding reference](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBindDescriptorSets.html): dynamic offsets and descriptor-range bounds.
- Ironwail `Quake/r_alias.c`, `R_FlushAliasInstances` and `R_Alias_CanAddToBatch`: adjacent instance batching and the differing SSBO geometry path.
