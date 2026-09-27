# Co-op silhouette adapter for the vkQuake scene

The inherited co-op scoreboard draws colored player outlines through walls in VR. Desktop and no-stencil rendering use a translucent filled silhouette. A selected co-op player in the weapon wheel has an additional highlight. The newly ported name tags are a separate, depth-tested feature; they do not become visible through opaque walls. Source references are `../quakespasm-openvr/Quake/gl_rmain.c:R_DrawPlayerOutlines`, `R_DrawWeaponMenuSelectedPlayerSilhouette`, and `Quake/r_alias.c:R_DrawAliasModelOutline`.

The target already orders sky, SSAO, transparency and particles in `Quake/r_passes.c`. `SUBPASS_FTE_PARTICLES` keeps depth/stencil read-only, so stencil writes there would be invalid under the [Vulkan image-layout rules](https://docs.vulkan.org/spec/latest/chapters/resources.html). Stereo already has a late writable `SUBPASS_MAIN` for the foreground wheel. The existing depth-only framebuffer view still exposes both depth and stencil when used as a framebuffer attachment under the same [resource rules](https://docs.vulkan.org/spec/latest/chapters/resources.html).

| Decision | Astra senior review | Disposition |
| --- | --- | --- |
| Add a standalone silhouette framebuffer/pass | Unnecessary duplication | Reject; use existing late scene subpasses |
| Draw VR rings in the late writable main subpass | Valid boundary, but it must record even with the wheel closed | Adopt; keep stencil operations out of read-only FTE |
| Clear stencil once and batch all player interiors | A union mask can erase another player's ring | Reject; clear once, then interleave each player's mask and ring with a unique reference |
| Clear stencil for every player | Preserves source overlap but can incur 16 full clears | Reject; unique references preserve the per-player comparison with one clear |
| Reuse model draw and VRIK preparation | Necessary for MDL, MD3, MD5 and tracked poses | Adopt; keep original entity identity and add a dependency on prepared palettes |
| Desktop fallback | Source uses filled silhouettes | Adopt in the late FTE subpass, before name tags |
| Wheel-selected player | Source draws it in addition to scoreboard outlines | Adapt; expose the already-validated hovered slot/name through a read-only wheel API and share model drawing without deduplicating the two effects |

For up to 16 players, use stencil references 1–16. Before each mask/ring pair, set the reference with `vkCmdSetStencilReference` on a pipeline created with `VK_DYNAMIC_STATE_STENCIL_REFERENCE`, as required by the [Vulkan command reference](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdSetStencilReference.html). Both front and back faces need matching state. A stencil-only `vkCmdClearAttachments` must leave depth intact; in multiview its clear rect uses `baseArrayLayer = 0` and `layerCount = 1` under the [Vulkan clear rules](https://docs.vulkan.org/spec/latest/chapters/clears.html). Sky and SSAO consume their stencil state earlier in the frame. The late wheel clear changes depth only.

The mask and ring need flat-color alias/MD5 pipeline modes with depth testing and depth writes disabled, plus bounded inflation matching the donor model formats. Reuse vkQuake's geometry buffers, interpolation, tracked VRIK palettes, stereo shader correction and command contexts. Merely selecting an unlit textured pipeline is insufficient. Avoid a copied player entity because palette lookup keys on the original entity pointer.

The first runtime proof is one animated remote MDL behind a wall with scoreboard held and released, both eyes, 1× MSAA and ordinary transparency. It must show an outline without changing scene depth. Then verify two overlapping players and distinct stencil references. Before calling the feature complete, cover MD3, MD5/MD5_8/VRIK, MSAA, OIT/MBOIT, SSAO/sky, desktop fallback, wheel selection and foreground layering.

## Implementation and senior-review disposition

The `2.0` implementation now reuses `GL_DrawAliasFrame` for all player geometry/poses and prepared VRIK palettes. A flat-color shader flag and dedicated pipeline modes render normal-scale stencil masks, inflated rings, and filled silhouettes without adding a framebuffer or renderer. VR records the mask/ring pairs in the existing late writable scene subpass after one stencil-only clear. Desktop records filled silhouettes in the late FTE subpass. The hovered co-op wheel action is read through the existing validated action identity; its filled highlight follows scoreboard rings. VR name tags and wheel meshes follow the highlights, with the held weapon last when the co-op scoreboard or VR wheel needs that ordering. Ordinary stereo frames keep vkQuake's original early weapon placement.

An Astra Max implementation review verified the current Vulkan/pass approach and found four correctness issues. The follow-up changes are:

| Review finding | Disposition |
| --- | --- |
| FTE filled highlights could inherit a zero projection when particles emitted no geometry | **Adopted:** each overlay model draw pushes the scene projection explicitly after binding its pipeline. |
| Ring pipelines disabled back-face culling, doubling alpha on closed meshes | **Adopted:** mask/ring retain vkQuake's normal back-face culling; fills remain two-sided as in the donor. |
| Early playspace wheel meshes could be covered by later through-wall highlights | **Adopted:** VR wheel meshes draw after highlights in the existing late MAIN subpass; playspace depth remains intact. |
| Moving every stereo weapon after transparency changed ordinary frames | **Adapted:** late weapon recording applies only to co-op scoreboard or open VR wheel frames; ordinary stereo and desktop retain the original draw order. |
| Separate framebuffer/pass or duplicate animation renderer | **Rejected:** existing pass and alias/MD5 draw path suffice. |

The Linux build succeeds after these changes. Static review and compilation do not prove images or performance. Physical headset testing is deferred to the user, and Windows/ARM verification remains later work. The renderer still needs runtime checks for both eyes, overlapping players, animation formats, OIT modes, MSAA, SSAO/sky, and wheel/depth ordering before visual parity is claimed.
