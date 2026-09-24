# Legacy VR wheel foreground depth: Astra review disposition

The inherited view-anchored wheel clears scene depth after transparent world
and particle drawing, then draws its models with depth testing enabled, before
the held weapon. Playspace wheel meshes instead remain world-occluded. The
current Vulkan wheel uses the early viewmodel scene task for both modes, so the
legacy mode can be covered by world depth and later transparency.

Astra reviewed the verified brief at `/tmp/quake-wheel-depth-brief.md` with
`gpt-6-astra` at max effort. Main spot-checked the product draw order, target
pass order, repeated-subpass binding, and MSAA resolve placement. The
recommendations are:

| Recommendation | Disposition |
| --- | --- |
| Reuse the existing scene pass with a terminal MAIN subpass after FTE particles. | **Adopt.** Add one unique draw stage/context in the existing render graph. Record it with the current viewmodel task; keep playspace and desktop routing unchanged. |
| Clear depth once, then draw legacy wheel and held weapon in that order. | **Adopt.** Depth-test-off pipelines would lose mesh self-occlusion. Do not draw the held weapon early and again after the clear. |
| Preserve a final MSAA color resolve after the terminal work under standard, WBOIT and MBOIT. | **Adopt with verification.** The FTE subpass currently owns the last resolve in OIT variants; moving that responsibility is a required integration detail. |
| Create UI alias pipelines, icon textures, a second depth target or a new wheel resource owner. | **Reject.** The existing alias materials, multiview scene pass and frame-slot fences already provide the needed owners. |
| Apply one depth rule to legacy text and models. | **Reject.** Source view-mode text is foreground UI; playspace ammo glyphs use scene depth. Keep their policies distinct. |

For a multiview render pass, `vkCmdClearAttachments` broadcasts to active
views. Its `VkClearRect` must use `baseArrayLayer = 0` and `layerCount = 1`
([Vulkan clear commands](https://docs.vulkan.org/spec/latest/chapters/clears.html));
clear depth only, to the renderer's reversed-Z far value `0.0`.

The first proof is a stock rotating wheel model against a wall with an
overlapping held weapon, translucent water and FTE particles. Compare both
eyes in standard/WBOIT/MBOIT at 1x/4x MSAA, then check close/reopen,
playspace occlusion and desktop behavior. Alpha/holey, mirrored and MD5 assets
remain format acceptance cases. This decision is architecture guidance, not a
claim that the foreground implementation or runtime proof is complete.
