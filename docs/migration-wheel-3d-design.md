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

## First implementation checkpoint

The wheel now resolves visible alias assets on the main thread after a successful
render begin, including the inherited `g_` to `v_` fallback. Its setup frame
copies resident model and selected geometry references, catalog state and the
panel transform before scene and UI tasks record. The viewmodel scene task draws
temporary wheel entities through vkQuake's existing alias material loop; its MD5
branch uses the model's ordinary joint set rather than a concurrently prepared
avatar palette. The VR UI no longer paints the desktop annulus or cards over
available meshes, while missing models retain an icon/card slot. Desktop
presentation keeps its existing path.

The Linux debug and Meson debugoptimized builds passed. A disposable simulated
Monado/Xvfb session with GDB-injected tracked head and hand poses opened the
wheel on stock `start`, recorded 92 prepared alias draws across 46 frames and
captured rotating models in both compositor eyes. The simulated runtime supplied
eye views and focus but no tracked devices, so pose injection was needed to
exercise the wheel; this is not headset input qualification. The check did not
verify hover/release alignment, world-depth rejection of hidden slots,
source-equivalent multi-ring layout, depth-tested labels, all model formats or
runtime focus/reconnect behavior. Those remain open before 3D wheel parity can
be claimed.

## Post-implementation Astra review

Astra's read-only review found no concrete model-load, frame-lifetime, alias
matrix or MD5-palette regression in this slice. It identified four presentation
gaps. Selected mesh labels were nearly black after their contrast cards were
removed; they now use a bright green tint. Playspace pointer hits now trace the
world from controller to panel and reject a blocked ray before hover/haptic or
release selection. This closes the obvious fully occluded-panel case, but
per-model target visibility and model-offset-aware 3D hit geometry remain open.
The legacy view-anchored mode still needs its foreground depth rule; current
scene emission uses playspace world depth in both modes. These gaps remain
explicit parity work, not completed behavior.

## Playspace ring and picking checkpoint

The playspace wheel now places the first visible weapon at the center and
subsequent weapons on five-unit rings. A partially filled ring distributes its
weapons evenly, as in the pinned source. The frozen hand basis is retained while
the panel moves farther forward for additional rings. Quick and co-op actions
reserve at least the source's first-ring spacing, including an action-only
wheel. Scene meshes and pointer targets use the same slot positions and schema
offsets; the controller ray rejects world-blocked model targets. Per-slot flat
labels no longer cover available meshes. Desktop and legacy wheel layout keep
their existing path. The Linux debug build and whitespace check passed for this
slice; the new ring interaction has not yet been exercised on a headset.

At that checkpoint, remaining wheel parity included depth-tested 3D ammo
labels, the legacy view-anchored foreground depth treatment and source-scale
layout, representative mod model formats and missing-asset cases, and
hover/release interaction under
a live tracked controller. The current scene mesh adapter still skips model
frustum culling until conservative stereo bounds account for wheel mesh scale.

## World-space ammo-label checkpoint

The playspace mesh path now emits each available weapon's ammo above its model,
using the source's model-top, forward offset, selected size and empty-ammo red.
A narrow scene-only basic glyph pipeline reuses vkQuake's character atlas and
3D glyph builder, reads the depth buffer without writing it, and accepts the
wheel's explicit right/up basis. It is created only for stereo sessions. The
central UI ammo readout remains as an additional status aid.

The Linux Make and Meson debug builds pass. A disposable desktop stock-map
boot/quit passed after the stereo-only pipeline gate. Before that gate, the
Make debug build reproducibly aborted during ray-traced-shadow driver teardown
after an extra unused desktop pipeline was created; the older Make baseline,
current Meson build, and an AddressSanitizer build did not reproduce it.
This establishes desktop startup compatibility, not OpenXR glyph visibility or
occlusion. Verify glyph appearance, eye agreement, wall hiding, OIT/MSAA
variants and VR teardown in a later simulated-runtime integration pass.

## Conservative wheel mesh culling

The prepared wheel draw now skips an MDL mesh only when an origin-centered,
scale-aware bound is wholly outside vkQuake's existing stereo-union frustum.
The bound covers all MDL poses and includes padding for float cancellation in
the alias header transform. World-space ammo glyphs remain independently
submitted so a visible label is not hidden with an offscreen mesh.

The local Astra xhigh review rejected an initial yaw-corner bound because its
padding could miss a valid mesh when large MDL scale and origin terms cancel.
The revised sphere resolves that finding. It also identified that vkQuake's
MD3 loader currently computes shared bounds from frame zero only; a proposed
global all-frame fix would change the wheel's centering and hit targets while
the wheel still displays frame zero. That loader change was removed. Native
MD3, MD5 and enhanced replacement meshes therefore fail open until bounds
for the selected geometry and pose are available. No alternate culling owner
or foveation-based geometry rejection was added.

The Linux Meson build and whitespace check pass. Actual stereo-edge imagery
and net frame-time savings remain unmeasured; this does not establish a
large-map performance gain.
