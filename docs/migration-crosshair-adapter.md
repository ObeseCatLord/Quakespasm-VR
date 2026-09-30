# VR crosshair migration boundary

The inherited renderer draws its VR crosshair in world space from the aimed
weapon or view, while vkQuake currently reaches `SCR_DrawCrosshair` in the GUI
pass even during stereo (`Quake/gl_screen.c:1298-1309, 2098`). A flat stereo
reticle does not preserve the inherited pointer depth or alignment. Desktop
`crosshair` and `crosshair_size` remain separate from the inherited VR
`vr_crosshair`, `vr_crosshair_depth`, `vr_crosshair_size`, `vr_crosshair_alpha`
and vertical offset (`../quakespasm-openvr/Quake/vr.c:1913-1916, 1925,
10407-10516`). The Vulkan adapter is implemented below; headset alignment
and full parity remain unverified.

The donor resolves up to two rays before changing render state. Controller aim
starts at the calibrated muzzle and follows hand rotation, including akimbo;
other aim modes start at the viewmodel origin and follow aim angles. Point mode
traces to the first world wall when depth is nonpositive or uses a configured
physical depth. Line mode traces to the impact. Its red translucent primitive
is drawn from the viewmodel scene path with depth testing disabled
(`../quakespasm-openvr/Quake/vr.c:10407-10516`,
`../quakespasm-openvr/Quake/gl_rmain.c:1296-1310`).

The minimal Vulkan adapter is to prepare immutable ray/impact/size data once
per logical XR frame using the existing tracked aim and muzzle owners, then
record a small billboard or ribbon in the existing scene/viewmodel command
context. `TraceLine` already provides the donor's world-only collision
behavior (`Quake/chase.c:51-60`); `R_VertexAllocate` and the basic untextured
blend pipeline already draw simple vertex geometry (`Quake/gl_draw.c:1062-1110`,
`Quake/gl_rmisc.c:3286-3303`). Preparation must precede worker draw tasks and
model/skin loading; a draw worker must not re-resolve the weapon model or
mutate QC state. The draw should use the same prepared target for both eye
layers, with the eye-specific projection supplied by the renderer. Make the
flat GUI crosshair stereo-only suppressed **after** this world reticle exists;
desktop code stays unchanged.

A direct copy of the donor's OpenGL `glBegin`, point-size, line-width and state
transitions would create a second rendering path and cannot run in the Vulkan
command task. A GUI-only reticle is smaller to code but contradicts the donor's
world hit/depth behavior. The adapter reuses existing ray, trace and basic
vertex facilities, while the currently missing akimbo aim source remains an
explicit dependency rather than being silently simulated.

The first end-to-end proof is one controller-calibrated point target on a wall
in both eyes, with weapon fire hitting the same surface and the desktop
crosshair unchanged. Then cover fixed depth, line mode, noncontroller aim,
tracking loss, wrong/absent weapon model, handedness, akimbo, occlusion,
multiview and public-versus-private server behavior. Neither shader
compilation nor a point drawn at arbitrary depth establishes alignment.

## Implemented slice

`R_PrepareVRCrosshair` now resolves one controller-calibrated or ordinary aim
ray and performs the inherited world trace on the main frame owner before
render tasks. The viewmodel scene context draws the resulting red point quad
or line ribbon through vkQuake's existing untextured blended pipeline. VR
crosshair mode, depth, size, alpha and vertical offset have their inherited
cvars; `vr_crosshair 0` disables the VR reticle. The flat GUI reticle stays
desktop-only even when VR tracking is unavailable. No remote avatar or akimbo
ray is implied by this slice.

The Vulkan primitive uses the current center field of view to estimate pixel
size. OpenXR's asymmetric per-eye projections can change its apparent size,
and the viewmodel's stair-smoothing offset is applied later in scene setup.
Both-eye wall alignment, barrel alignment, controller loss, and stairs need
headset verification before claiming visual parity. Linux compilation alone
does not close that gate.

Current-source checkpoint (2026-09-30): the historical single-ray limitation
above is superseded by `R_PrepareVRCrosshair` calling the shared
`VR_InputCrosshairAimRays` producer for controller mode. It admits one or two
calibrated rays before recording the immutable frame snapshot. Noncontroller
mode retains the gameplay-aim ray through `cl.viewangles`; scene tasks do not
reload weapon models or mutate the command/QC owner. This source wiring does
not qualify wall/barrel alignment, stairs or physical headset appearance.
