# VR video and filtering fixes

## Reference behavior

Desktop vkQuake keeps its existing output/render-resolution model: `vid.width`
and `vid.height` describe the output, and an explicit `r_width`/`r_height`
pair may reduce the scene render target before the existing upscale pass.
`vid_anisotropic 1` means the Vulkan device maximum; the obsolete
`gl_texture_anisotropy` cvar is not part of this branch's configuration
contract.

With an attached OpenXR session, the runtime supplies the per-eye extent.
`GL_OpenXRAttach` assigns that extent to `vid.width`/`vid.height`, so eye image
allocation and projection already follow the runtime recommendation.  Applying
desktop render scaling afterwards changes the scene target below that extent,
then upscales it, which violates the VR resolution control expectation.

## Smallest change

Keep the runtime-owned eye extent authoritative in `VID_GetRenderSize` when
`vulkan_globals.stereo_active` is true.  Disable and label the Video Options
render-resolution row while the session is attached so it cannot advertise or
persist a VR effect.  Saved desktop render-resolution values remain intact and
resume when OpenXR detaches.

The pre-attachment FB/META device-selection gate also ignores saved desktop
render-resolution values when an OpenXR Vulkan binding is being created. Such
values must not silently disable runtime foveation even though they no longer
affect VR render dimensions.

For filtering, retain the existing vkQuake sampler objects and descriptor
layout.  The descriptor assignment is confirmed to choose the
nearest-anisotropic sampler for normal textures when `vid_filter` is
"classic"; the existing linear-anisotropic sampler is selected only when the
global preference is "smooth".  The Vulkan specification permits anisotropy
with nearest filtering, so that configuration alone does not establish a Steam
Frame driver visual defect.  It is a plausible explanation for the reported
ineffectiveness and must be validated on the device.

Limit the linear-anisotropic descriptor choice to an active OpenXR session for
eligible, non-forced textures.  Refresh descriptors after OpenXR attaches and
again after its render resources are idle and retired, restoring the unchanged
desktop point-filter behavior.  Forced nearest, forced linear, ordinary
no-picmip, and lightmap exclusions preserve their current behavior; the
existing warp-image exception remains.  This is a descriptor selection
experiment, not a new renderer or sampler family.

## Proof target

After attachment, changing `r_width`/`r_height` through Video Options or the
console leaves `vid.render_width`/`vid.render_height` equal to the runtime eye
extent.  On desktop, the existing render-resolution menu, upscale path, and
classic point-filter selection are unchanged.  During an active OpenXR session,
eligible world textures bind `linear_aniso_sampler_lod_bias` when
`vid_anisotropic` resolves above 1; lightmaps and explicitly point-filtered
assets do not.  A Steam Frame visual comparison is required to establish
whether this resolves the reported anisotropy issue.
