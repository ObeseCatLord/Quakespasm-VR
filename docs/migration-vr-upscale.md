# Reduced internal resolution in OpenXR multiview

The `2.0` renderer currently ignores `r_width`/`r_height` while stereo is
active. This leaves vkQuake's existing reduced-resolution scene path unavailable
in VR, even though the scene and native-resolution UI images already have two
layers. This is an optional performance control, not a change to the default
eye-target resolution or an asserted speedup.

## Narrow adapter decision

| Design | Reuse | Incompatibility and cost |
| --- | --- | --- |
| Extend the donor's scene upscale pass | Keep `VID_GetRenderSize`, scene/UI images, `PIPELINE_SCENE_UPSCALE`, `R_CreateUIPasses`, and one full-screen multiview draw. Select a shader variant that samples the matching layer of the scene array and transition both layers before the UI pass. | The existing shader declares `sampler2D` while stereo `color_buffers_view[0]` is `VK_IMAGE_VIEW_TYPE_2D_ARRAY`; the pre-UI barrier covers only layer 0. These are narrow shader/synchronization differences. |
| Add an OpenXR-specific upscaler or render a second eye pass | Would duplicate resolution policy, scene-to-UI composition, and resource ownership. | No demonstrated incompatibility requires another pass owner or eye-by-eye render. Reject. |

The smallest useful proof is a task-enabled OpenXR frame at a deliberately
reduced internal size: both eye layers show the correct scene, the UI stays at
the runtime target size, and resize/restart returns to full resolution without
stale layers. Check 1x and 4x MSAA, OIT modes, screen effects, foveation off/on,
and Vulkan validation. Compare GPU frame time and visual clarity before
recommending a setting. The `r_width`/`r_height` defaults remain `-1`; fixed
foveation remains explicit-only and eye-tracked foveation still returns to full
quality when gaze fails.

Vulkan's official [ViewIndex reference](https://docs.vulkan.org/refpages/latest/refpages/source/ViewIndex.html)
permits a fragment invocation to select the subpass view; the
[GL_EXT_multiview language extension](https://docs.vulkan.org/glslext/latest/glslext/ext/GL_EXT_multiview.html)
exposes it as `gl_ViewIndex`. The
[image barrier reference](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageMemoryBarrier.html)
limits the transition to its declared array-layer range. These are the
contracts for the shader variant and two-layer pre-UI transition.

## Implementation and senior review

The code now enables the existing size selection in stereo, compiles a stereo
variant of the donor upscale shader, samples the scene array layer selected by
`gl_ViewIndex`, and transitions both scene layers before the UI pass. The donor
scene/UI resources, pass order, and one-draw composition remain unchanged.
Fresh settings still render the native target size. Linux release and debug
Meson builds, Vulkan 1.1 validation of both shader variants, and the focused
render-acquire fixture pass. These checks do not establish visible stereo
output or a speedup.

An Astra read-only senior review verified the narrow adapter and found
one build integration defect: the Makefile object list lacked the new shader,
so Makefile linking would fail even for desktop. The finding was adopted by
adding the shader object and variant rule to `Quake/common.make`. The full
Makefile link then exposed three previously omitted port objects
(`addon_catalog`, `r_vrik`, `r_vrik_render`) and an undeclared `mkstemp` in
`voice_settings.c`; those narrow build fixes are included. A fresh affected
object rebuild and full Linux Makefile link now pass. The review
found no source-grounded need for another XR upscaler or per-eye pass; it did
not prove live image correctness. The review was requested at xhigh; its CLI
reported Astra/max, while backend routing was not independently verifiable.

One isolated simulated-Monado run on this Linux GPU reached a real multiview
upscale draw at 320x240 internal pixels against an 896x1007 per-eye runtime
target, then exited normally. A mirror capture after roughly 60 upscale draw
calls visibly contained the scene in both eyes. Earlier captures at about 12
draw calls had a black lower region in the right mirror half; whether that was
a compositor capture/resize transition or an application frame defect remains
unresolved. The runtime run did not use a Vulkan validation layer and is not a
headset-quality or performance result.

| Review recommendation | Disposition |
| --- | --- |
| Register the stereo shader in the Makefile path | Adopted; shader object and variant rule added, then the full link was checked. |
| Keep one donor upscale pass with a stereo shader variant | Adopted; scene/UI resource and task ownership remain with vkQuake. |
| Add a separate XR upscaler or per-eye render | Rejected; no source incompatibility requires duplicated composition or draw policy. |

Outstanding acceptance is a real task-enabled OpenXR render at reduced size
with both eyes visibly correct, the native HUD/menu legible, screen effects
and MSAA/OIT/foveation combinations, and reduced-to-native resize/restart
transitions. A repeatable workload must measure GPU frame time before claiming
this optional control improves performance.
