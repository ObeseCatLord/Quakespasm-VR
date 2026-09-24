# Ironwail z-fighting treatment on the vkQuake 2.0 base

## Verified brief for senior design review

Goal: preserve vkQuake's world/material/render-task owner while bringing the
requested Ironwail-style original-level z-fighting treatment to desktop and
OpenXR multiview. This is a solo-maintained engine; add no parallel renderer.

| Fact | Evidence and limit |
| --- | --- |
| `[verified: source]` Ironwail defaults `gl_zfix` on and selects offset for non-world, non-decal brush draws. | Pinned Ironwail `Quake/gl_rmain.c:122`, `Quake/r_world.c:480` and `:311`; `map_checks` disables it. |
| `[verified: source]` Its world vertex shader adds signed `1/1024` directly to clip-space `gl_Position.z` for an offset draw; reversed-Z uses `-1/1024`. | Pinned Ironwail `Quake/gl_shaders.h:518-525`. Its depth convention and eligibility are relevant; a Vulkan raster depth-bias constant is not mathematically the same operation. |
| `[verified: source]` `2.0` has `gl_zfix=1`, separates indirect bmodel material groups, and excludes decals from bias. | `Quake/gl_rmain.c:151`, `Quake/r_brush.c:82,1025-1045`, `Quake/r_world.c:1402,1450`. |
| `[verified: source]` Its world pipelines enable dynamic depth bias and reversed-Z depth comparison. | `Quake/gl_rmisc.c:R_CreateWorldPipelines`, `R_InitDefaultStates` (`VK_COMPARE_OP_GREATER_OR_EQUAL`). Non-indirect and indirect brush draws set negative format-dependent constant and slope factors in `r_world.c:R_FlushBatch` and `r_brush.c:R_DrawIndirectBrushes`. |
| `[verified: source]` World vertex drawing uses one `Shaders/world.vert` source for desktop and generated stereo variants, and a shared 22-float graphics push range with instance transforms. | `Shaders/world.vert`, `Quake/gl_rmisc.c` world pipeline setup, `Quake/r_brush.c:R_DrawIndirectBrushes`, `Quake/r_world.c:R_DrawTextureChains_Multitexture`. A shader-level offset would require a per-draw flag and push-layout coordination. |
| `[unknown]` Existing Vulkan bias may already suppress the named original-level artifacts on this GPU, but no door/lift comparison has been recorded. | The feature map marks PERF-020 non-equivalent. Build and a stock-map image do not prove z-fighting parity. |
| `[unknown]` A clip-space constant from the OpenGL shader may need adjustment for Vulkan's clip/depth convention, asymmetric eye projections, and large/small depth formats. | Must preserve near-plane behavior and both eyes; source formula alone does not prove native Vulkan appearance. |

The narrowest option is to retain one world vertex shader and pass one offset
flag through its existing push constants, using the same eligibility already
computed by both brush paths. The flag would select a signed clip-space offset
after the stereo clip correction. The existing Vulkan depth bias would then be
disabled for that world treatment to avoid double offset. This reuses donor
draw groups, materials and tasks, but touches the shared graphics push layout.

Other options: retain the current dynamic raster bias and qualify it visually,
or tune its constants after measurement. Those avoid shader/plumbing edits but
cannot claim exact Ironwail behavior from source. A separate brush shader/pass
would duplicate pipeline/material policy and is not justified.

Open decisions for review:

1. Is source-equivalent clip-space bias actually the right target, or should
   Vulkan's depth-bias path be retained pending visible artifact evidence?
2. If adopting the shader offset, where should the offset flag live so per-draw
   state remains correct through indirect, texture-chain, showtris, depth and
   multiview variants without changing unrelated pushes?
3. Should `map_checks` suppress this treatment as in Ironwail, and are there
   texture/alpha/decal cases where the current `2.0` eligibility is too broad?

Suspected overlap: bias selection and material batching are one decision if a
new flag would force more draw groups. Review them together. Review depth:
challenge source equivalence, offset sign/space, and the narrowest safe owner;
do not re-review OpenXR session lifetime, network/gameplay, foveation, or the
whole migration catalogue. Smallest proof: stock door/lift at grazing angles
on desktop and both XR eyes, with `gl_zfix` on/off, decals unchanged, no new
z-fighting at near/far depth, and vkQuake task rendering retained. Headset live
testing and Windows/ARM verification are deferred by the user.

## Astra senior-review disposition

Astra at explicit `xhigh` read the target and pinned Ironwail source. Main
spot-checked the load-bearing findings: the world layout is already 88 bytes,
`R_BindPipeline` clears push constants on a layout-size change, Ironwail's
reversed-Z setup uses zero-to-one depth, and its water draw path marks non-world
brushes for the same bias. Khronos's [depth guide](https://docs.vulkan.org/guide/latest/depth.html)
confirms that changing clip-space Z changes post-divide depth by a value that
depends on W; the [depth-bias command](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdSetDepthBias.html)
instead sets raster slope/constant factors. Source equality needs a shader
offset, not retuned Vulkan raster constants.

| Recommendation | Disposition |
| --- | --- |
| Reuse the 88-byte graphics push layout; encode z-fix in a reserved high bit of `instance_base`, masking it before indexing. | **Adopt.** This avoids a new push field and the layout-change reset risk. Assert the ordinary buffer base does not use the reserved bit. Publish a defined flag or zero after binding the destination world pipeline for every affected draw. |
| Apply Ironwail's `-1/1024` reversed-Z clip offset after stereo correction; remove world raster bias. | **Adopt.** Keep the donor world shader, material groups and task owner. The offset remains opt-in through the existing `gl_zfix` cvar. |
| Align eligibility across indirect, texture-chain, and liquid brushes; suppress under `map_checks`; keep world and static-cutout decals unshifted. | **Adopt.** Ironwail's water path biases non-world brushes, so source parity answers this without a new user preference. |
| Treat source-equivalent math as proven visual parity. | **Reject.** Compare doors/lifts, near/far occlusion, decals and both eyes after implementation. Headset testing and Windows/ARM verification remain deferred as requested. |

The reviewed adapter is implemented in `Shaders/world.vert`,
`Quake/gl_rmisc.c`, `Quake/r_brush.c`, and `Quake/r_world.c`. Direct batches now
publish alpha and the encoded z-fix flag after their destination world pipeline
is bound; indirect batches do the same, while showtris publishes an unflagged
instance base. This avoids losing those fields if a pipeline-layout change
clears push constants. The existing 88-byte layout remains intact. Linux Meson
compiled both world shader variants and linked the executable. Visible
door/lift behavior, stereo occlusion, and measurable performance remain open;
the source/build check does not certify them.
