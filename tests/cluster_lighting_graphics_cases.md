# Clustered-lighting graphics qualification

Run only after shader registration and a Vulkan build are present. Capture the
same deterministic scene in native and clustered mode with `r_dynamic 1`,
`r_gpulightmapupdate 1`, and `r_rtshadows 0`.

| Case | Required witness |
| --- | --- |
| Stereo asymmetric frusta | Both-eye, underwater scene clips reconstruct matching moving-brush lighting. |
| Geometry | Static, translating, rotating, and non-instanced brushes retain native light position and KEX/cone falloff at 0 and 64 lights. |
| Raster paths | Cutout derivatives, WBOIT, MBOIT, MSAA, fullbrights, and surface-dither intensities 0/low/medium/high preserve alpha and eye-stable UV noise. |
| Foveation | VRS and fragment-density tile edges retain every native light witness. |
| Mode changes | Native→clustered→native forces an atlas rebuild for hidden and moved brushes, with no old baked contribution or double lighting. |
| Compatibility | `r_rtshadows > 0`, `r_gpulightmapupdate 0`, and `r_dynamic 0` remain native; tasks-on and tasks-off submit the same frame-slot result. |

The CPU geometry fixture is an independent numerical reference. After registration,
validate the actual `cluster_lights.comp` dispatch/mapped masks with reversed-Z,
asymmetric/canted translated-eye matrices from that fixture. Include its central
far-plane interior light, off-axis equal-plane-depth witness, depth below nominal
near, padded edges, invalid padded ray denominator, and tail depths exceeding 1M
with 0/1/31/32/33/63/64 active lights. Inspect both eye masks, not just image output.
Reflect binding-2 offsets against the plan ABI table before uploading these cases.

For native/cluster comparisons, capture 8-bit and 10-bit atlas modes with one native
and one KEX light, including `SURF_PLANEBACK`, rotated brushes, and an eye on either
side of a plane. Confirm 2x dynamic units in both formats and outward KEX Lambert
orientation. Toggle modes while a hidden brush moves with zero atlas light counts;
check both forced static clearing and native re-entry after lights resume. Read
status during task evaluation and verify it uses the completed renderer decision;
no menu cvar changes should be required for fallback.

## Standalone production compute fixture

`tests/cluster_lighting_vulkan_fixture.c` reuses the standalone device, shader-module,
command submission and mapped-readback approach from `ssao_shared_mip_vulkan_fixture.c`.
It creates its own instance/device/queue/buffers/fence and touches no game resources,
GPU reset interfaces, or existing Vulkan devices. A Vulkan 1.1 software ICD is
acceptable. Execution is gated on finishing selected implementation; that gate has now been met:

```sh
glslc --target-env=vulkan1.1 -IShaders Shaders/cluster_lights.comp -o /tmp/qsvr-cluster-lights.spv
cc -std=gnu11 -O0 tests/cluster_lighting_vulkan_fixture.c -lvulkan -lm -o /tmp/qsvr-cluster-lights
/tmp/qsvr-cluster-lights /tmp/qsvr-cluster-lights.spv
```

To use a software ICD, set `VK_ICD_FILENAMES` to its installed manifest when running
the final command. Exit 77 means Vulkan 1.1 compute/set-4/storage-range support is
unavailable; exit 1 is a fixture failure; exit 0 is a pass. Expected evidence includes
`CLUSTER_VULKAN_SPIRV_ABI_PASSED`, six `CLUSTER_VULKAN_CASE_PASSED` records, and the final
`CLUSTER_LIGHTING_VULKAN_PASSED` record. The final selected-feature batch compiled and executed this fixture successfully on an RTX 4090.

The actual production SPIR-V is inspected for set 4/binding 2, all block/light-member
offsets, matrix stride and nested array strides. The fixture uploads the real 265456-byte
frame block at two separately aligned byte offsets (at least 256 bytes and the device's
`minStorageBufferOffsetAlignment`). Immutable descriptors reuse those offsets across
three submissions; sentinels in the alignment gaps and unchanged input headers catch
wrong slot offsets or shader writes outside the masks. Both slots dispatch the real
`cluster_lights.comp` and transfer the entire buffer into a separate readback allocation.
Noncoherent allocations are flushed/invalidated; each submission's fence completes
before the host checks or reuses its buffers.

Coverage includes mono/zero and stereo/64 lights in distinct slots, then mono/64 and
stereo/zero, plus isolated padding and stereo central-boundary witnesses. Known inverse
reverse-Z matrices produce translated, asymmetric, oppositely canted eyes. Full mask
readback is compared to independent double-precision finite-pyramid AABB calculations;
explicit tiny-light witnesses cover interior and extreme tile coordinates, the zero-depth
first slice, shared central tile/depth boundaries, and padding beyond an unpadded AABB.
The last slice must contain every active bit, including light 63 positioned beyond 1M
units. Every inactive-eye mask and zero-light mask must be empty.

GPU `pow`/FMA versus the double oracle can differ around AABB tangencies. The fixture
reports comparisons inside a `0.002 + far_slice_depth*1e-5` numerical margin and excludes
only those from exact hit/rejection comparison. Explicit witness bits are always required,
without that exemption. Passing verifies production compute/ABI/dispatch/readback for
these inputs, not world fragment reconstruction, lighting/UNORM clamp, rendered dither,
OpenXR runtime foveation, native atlas transitions, or game task/submission integration;
the earlier graphics matrix remains required for those behaviors.


## Final batch results (2026-10-04)

The independent CPU geometry check and production compute/ABI fixture both pass.
All six GPU cases pass at a 265472-byte aligned slot stride (device minimum 16),
including every required witness. All registered native/stereo world shader
variants compile in the strict debug graph.

`selected_lighting_native_smoke.gdb` also completed ten live renderer cases in
both desktop and simulated two-eye OpenXR, using ordinary and KEX/cone lights.
It checks effective frame data, native/cluster/native transitions, low/high
dither, Classic/Low/High transparency, tasks on/off, CPU-lightmap fallback,
dynamic-light disable and shadow fallback. Both eyes are published in stereo
with 4x MSAA. Native/cluster desktop captures were inspected; no performance
claim follows from these functional checks.

This bounded smoke does not complete the entire visual matrix above: physical
foveation, custom canted runtime views, moving hidden brush atlas pixels, all
8/10-bit formats and every MSAA combination remain additional qualification.
The separate GPU oracle does test synthetic asymmetric/canted matrices, 64
lights and conservative padding. It must not be represented as fragment pixel
or hardware gaze qualification.
