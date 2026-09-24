# vkQuake graphics effects in OpenXR stereo

Goal: keep vkQuake's renderer as the visual baseline in VR. Availability in
source is not proof of correct appearance or performance in a headset. The
Linux desktop and OpenXR renderer share vkQuake's frame graph; the latter uses
two-view Vulkan multiview. Both-eye image inspection and comparable frame-time
measurements remain acceptance gates.

| Effect/path | Current 2.0 source state | Next evidence needed |
| --- | --- | --- |
| MSAA and higher precision color/depth | Stereo attachments are two-layer images; sample count and format flow through the existing Vulkan render passes (`gl_vidsdl.c`, `r_passes.c`). | Inspect resolves, dark ramps, transparent edges and both eyes at supported sample counts. |
| Opaque lighting, interpolated lightstyles, optional ray-query shadows | Existing world/brush pipelines and lightmap update path are retained; there is no stereo gate on `r_rtshadows` (`r_brush.c`). | Check moving brush receivers and VR avatar poses; measure shadow cost. |
| OIT/transparency and FTE particles | The scene frame builder retains standard, WBOIT and MBOIT variants plus FTE particle subpasses in stereo (`r_passes.c`). | Check per-eye ordering, water boundaries, dense particles and MSAA resolve. |
| Water warp, palette/polyblend and screen effects | `FRAME_SCREEN_EFFECTS` remains in the stereo frame graph, with 2D-array shader output for stereo (`r_passes.c`, `screen_effects.comp`). | Verify each effect's input and per-eye output with underwater, palette and damage scenes. |
| Scene upscale | Stereo shader samples the matching `gl_ViewIndex` layer (`scene_upscale.frag`). | Compare scaling modes in both eyes and desktop. |
| Entity SSAO/contact occlusion | Full-resolution per-eye compute and one multiview composite are implemented. The shared Graphics Options `r_ssao` quality setting selects the existing desktop path or the VR path by rendering mode. | Inspect both-eye AO, asymmetric frusta, MSAA/OIT/foveation combinations, and measure frame cost. Benchmark a VR-only lower-resolution path before adopting it. |
| Ray-debug visualization | Explicitly disabled in stereo by `gl_vidsdl.c`; this is a debug view, separate from optional ray-query shadows. | Decide whether the diagnostic itself is useful in VR; it is not a production lighting gate. |

The [SSAO design and implementation review](migration-vr-ssao-review.md)
selected a narrow per-eye resource/compute adapter over a shader rewrite.
It preserves the existing desktop algorithm and one multiview composite draw.
Vulkan multiview maps each view to an attachment layer and exposes its index to fragment shaders, as specified by
[Khronos's render-pass chapter](https://docs.vulkan.org/spec/latest/chapters/renderpass.html)
and [ViewIndex reference](https://docs.vulkan.org/refpages/latest/refpages/source/ViewIndex.html).
This audit records known gates; it does not certify every visual effect yet.
