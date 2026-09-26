# Stereo dynamic-lightmap facing check

vkQuake's GPU lightmap updater previously skipped dynamic lights for a texel whenever its surface faced away from the center view. With two OpenXR eyes sharing one lightmap, a surface can face one eye but fail that test. The Vulkan compute path now keeps the desktop center-view rule and, in stereo, runs dynamic-light work when either actual eye faces the surface. The existing lightmap image, submodel transform, task graph and render passes remain the owners; no per-eye lightmaps were added.

The C writer passes one stereo flag and two world-space eye origins through the existing compute push constants. The ordinary layout is 72 bytes and the ray-shadow layout is 76 bytes. All four 8/10-bit, RT/non-RT SPIR-V variants compile, pass `spirv-val`, and disassemble to the expected scalar offsets. The Linux debug executable links. This verifies the shader/host ABI; it does not establish an in-game image.

A requested Astra xhigh read-only review found no source defect in the patch, but the agent could not independently verify its effective model settings. Treat this as an evidence review rather than a certified senior-review run. Its findings and integration decisions were:

| Finding | Disposition |
| --- | --- |
| A shared lightmap needs dynamic light on any surface facing either eye; desktop keeps its original predicate. | **Adopt:** use the existing compute pass with the exact two-eye OR. |
| Host offsets, pipeline ranges and generated SPIR-V layouts agree in all four variants; setup precedes lightmap recording and its GPU barrier precedes scene sampling. | **Adopt:** retain the existing task and synchronization boundaries. |
| Per-eye images, extra eye-pose cache state, or a separate lighting pass are not justified by the source evidence. | **Reject:** keep one shared lightmap and its existing invalidation path. |
| Compilation cannot show grazing-angle appearance or light expiration behavior. | **Open:** compare desktop and both VR eyes across an eye-plane crossing with a stationary dynamic light and a moving/rotating brush, with ray shadows on and off. |

A local startup smoke attempt could not create an SDL video device in this sandbox. No visual or performance gain is claimed from that attempt. The user's later live headset testing remains separate from this source/build verification.
