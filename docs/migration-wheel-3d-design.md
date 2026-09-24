# 3D VR weapon-wheel presentation: senior-review brief

## Goal and scale

Complete the inherited rotating 3D weapon wheel on the vkQuake-based `2.0`
branch while retaining its shared desktop/OpenXR catalog, release policy and
hitboxes. This is a solo-maintained engine port: use one narrow renderer adapter,
not a second wheel renderer or a new render graph. Physical headset testing is
deferred; the first proof must run on this Linux build with simulated OpenXR.

## Checked facts

| Claim | Evidence |
| --- | --- |
| The target already has catalog and selection owners for stock, mod `wwheel.txt`, schema and built-in profiles. | [verified: `Quake/vr_weapon_menu.c:896-947,1022-1116,1540-1575,1702-1836`] One `VR_WeaponMenu_BuildVisible` implementation feeds hit, release and 2D draw, but each currently rebuilds its own result. Runtime discovery remains open. |
| The target VR wheel is a flat UI panel with a frozen hand/view anchor. | [verified: `Quake/gl_screen.c:1895-2022,2368-2379`; `Quake/vr_weapon_menu.c:1458-1478,1702-1836`] `SCR_SetupFrame` prepares the anchor once; the GUI task draws the panel. |
| The source wheel draws temporary alias entities at center/multi-ring positions and spins them, with schema scale/offset and selected/equipped color; it directly loads model paths during draw. | [verified: pinned product `Quake/vr.c:12325-12585`] Missing models retain ring spacing. The OpenGL code and draw-time loading are reference behavior, not a safe transplant. |
| The target alias owner draws models in the scene pass with model textures, fullbrights and lighting. | [verified: `Quake/r_alias.c:764-869`; `Quake/gl_rmain.c:1191-1228,2237-2248`; `Quake/r_passes.c:227-258`] The UI is a later, separate render pass with no depth attachment (`r_passes.c:1250-1282`). |
| The target GUI may record in parallel with scene tasks. | [verified: `Quake/gl_screen.c:2517-2529`; `Quake/gl_rmain.c:2340-2388`] Model loading or mutating shared catalog/selection from a draw task needs an explicit owner and dependency. |
| The existing panel supplies precise source-pixel-to-world transforms but no model draw API. | [verified: `Quake/gl_draw.c:1331-1360`; `Quake/gl_screen.c:2005-2022`] UI-pipeline push constants and alias-pipeline layouts differ. |

## Current design lean and real alternatives

1. **Reuse the scene alias pipeline and one frame-owned immutable wheel model list.** Prepare model identities and panel-space positions before scene recording. Add a narrow draw call to the existing viewmodel scene command buffer after its normal draw. Keep catalog, selection and 2D action/hit policy in `vr_weapon_menu.c`. Use the same panel transform to place models so the controller ray and labels agree. The source playspace mode allows world depth occlusion; the scene pass can retain that rule. The later flat UI must stop painting opaque cards over the VR model positions. This is the lean, but scene effects/upscale and UI labels may differ in depth/quality.
2. **Adapt alias pipelines to the existing UI pass.** Models would remain composited with labels, but UI has no depth attachment and the current alias shader/pipeline uses scene matrices/lighting. This may require more pipeline variants than the wheel itself and risks duplicating renderer policy. Reject unless option 1 cannot meet visible parity.
3. **Render models to icon textures.** This could reuse `Draw_SubPic` but needs offscreen passes, per-model cache and invalidation, and may lose live rotation/stereo depth. Reject as the primary path.
4. **Import the source OpenGL wheel.** Rejected: it has immediate-mode GL state, direct `Mod_ForName` during draw, and its own geometry/selection owner, all incompatible with the target Vulkan task/render-pass lifetime.

The depth/occlusion decision and pass-placement decision may be one issue: changing world-depth policy changes whether the scene pass can preserve source behavior. Merge them in review if useful.

## Questions for Astra

- Verify the pass/task and alias assumptions above; challenge the scene-pass lean if an existing vkQuake UI/model path is better.
- Choose the smallest first vertical proof: one stock alias model in each multiview eye, correct wheel slot/hover alignment, no draw-time model load, and no desktop behavior change. Identify the exact data lifetime and task dependencies.
- Specify the minimum safe depth/visibility and UI-background policy for the first proof, then the extension to multi-ring mod models and missing-model fallback.
- Identify the highest-risk hidden coupling (model asset lifetime, interpolation/culling, transform, lighting, render-pass order, or QC mutex) and a deletion/simplification opportunity.

Do not re-review networking, foveation, headset providers, or the full 185-item
feature map. Do not implement code. Rank only decisions that affect this 3D
wheel seam; user preference is already clear that inherited 3D presentation is
required and vkQuake must remain the renderer owner.

## Astra senior-review disposition

The reviewer was requested as `gpt-6-astra` at `xhigh`; main verified those
effective settings in the local agent metadata. Main spot-checked the
load-bearing claims against the pinned source and target: `Mod_Extradata_CheckSkin`
can load a model, MD5 alias emission reads the VRIK palette lookup, the UI pass
has no depth attachment, and the target layout has only one ring.

| Finding | Disposition |
| --- | --- |
| Catalog, pointer, release and draw each rebuild visibility rather than sharing an immutable frame. | **Adopt.** Prepare one copied list of IDs, eligibility, labels, ammo and slot centers before parallel drawing. Release still revalidates against live state. |
| Use the existing scene alias path in `SCBX_VIEW_MODEL`, but place wheel draws outside the viewmodel's invisibility/death gates. | **Adopt.** Add a narrow prepared-alias adapter and factor the existing surface/material emission loop; keep vkQuake model pipelines and task graph. |
| MD5 wheel rendering could read a VRIK palette while that palette is being prepared concurrently. | **Adopt.** Wheel draw items use an explicit null tracked palette and resident geometry/material references. Do not call `Mod_Extradata_CheckSkin` from a draw worker. |
| Source uses unlit tint/fullbrights rather than ordinary world lighting. | **Adopt with verification.** Reuse the shader's color inputs and texture/fullbright handling, then compare source and target colors; avoid a separate lighting stack. |
| Playspace depth, labels and picking are one policy. | **Adopt.** Keep scene depth for model occlusion, remove flat VR fills/icons that would cover meshes, and reject world-occluded targets. Depth-tested 3D labels need a narrow existing scene-glyph pipeline variant; the current UI text cannot establish source parity. |
| Target has one ring; source has center and multiple rings. | **Adopt.** Extend the existing layout and hit owner after the first model proof. Missing model assets retain their slot and a readable fallback. |
| UI alias pipelines, offscreen icon textures or source OpenGL transplant. | **Reject.** They duplicate or bypass renderer policy without a demonstrated need. |

The first vertical proof is a real stock shotgun mesh in the existing slot in
both eyes under task-enabled simulated OpenXR, with frozen hover position,
rotation, release selection, depth occlusion, and no draw-time model load.
Then expand through model formats and multi-ring mods. Draw records must remain
immutable until `draw_done` joins; GPU resource retirement still uses the donor
fences. The precise scene/UI composition, transparent replacements and legacy
view-anchored mode remain integration risks until that proof is rendered.
