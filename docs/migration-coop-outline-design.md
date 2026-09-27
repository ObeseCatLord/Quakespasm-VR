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

The first proof is one animated remote MDL behind a wall with scoreboard held and released, both eyes, 1× MSAA and ordinary transparency. It must show an outline without changing scene depth. Then verify two overlapping players and distinct stencil references. Before calling the feature complete, cover MD3, MD5/MD5_8/VRIK, MSAA, OIT/MBOIT, SSAO/sky, desktop fallback, wheel selection and foreground layering. The ordinary closed-wheel weapon and depth-tested name tags currently draw earlier than the proposed late VR outline; their layering needs explicit correction or a demonstrated acceptable visual result. No silhouette code or runtime proof is claimed by this document.
