# Client QuakeC line drawing on the native GUI path

## Verified reference and boundary

Primary `quakespasm-openvr/Quake/pr_cmds.c` `PF_cl_drawline` registers CSQC 315.
It draws a straight blended RGB/alpha line between the supplied XY endpoints,
with default width/alpha one and physical width `max(1, width * PR_GetVMScale())`.
The optional draw flag is ignored. Z does not affect this 2D primitive.
`PR_GetVMScale` is the primary status-bar scale, clamped like the destination's
absolute CSQC display scale.

Destination `Quake/pr_ext.c` lacks 315 but already draws filled colored quads
using `R_VertexAllocate`, `basicvertex_t`, the GUI context and
`PIPELINE_BASIC_NOTEX_BLEND`. `SCR_GetCSQCDisplay` owns absolute/relative desktop
scales and the VR panel override. `GL_SetCanvas(CANVAS_CSQC)` consumes its
`pixel_scale` axes; existing `R_BindGraphicsPipeline` restores VR panel transforms
and clipping. Reuse these paths. No Vulkan feature, pipeline or shader change is
needed, and no second HUD or command-buffer owner is introduced.

## Smallest adapter

Extract only the existing drawfill quad submission into a private
`DrawQC_SolidQuad` helper accepting four XY corners and RGB/alpha. Keep drawfill's
corner order, colors, vertex order, descriptor, pipeline and draw command exactly
as before. This removes duplication of the existing submit path rather than
replacing the GUI system.

For drawline, convert endpoint differences into source canvas pixels using the
existing display's two pixel scales. Compute the normalized perpendicular there,
multiply by half `max(1, width * display.scale)`, then convert the offsets back to
virtual canvas coordinates. Submit a butt-ended quad through the same helper.
Absolute desktop scale matches primary's requested line thickness; relative
desktop and VR use the current native display/canvas contract. Geometry then
receives the same clipping and panel projection as other CSQC primitives.

This triangle representation avoids depending on Vulkan wide-line support or
adding a dedicated line pipeline. It preserves the straight stroke, supplied
color, blending, endpoints and thickness contract, but does not claim identical
OpenGL line-rasterization edge coverage. A zero-length line draws nothing.
Reject nonfinite inputs and invalid display scales, clamp RGB/alpha to 0..1
before byte conversion, and skip fully transparent calls. Use double precision
for endpoint differences/normalization to avoid finite-input float overflow;
reject corners outside finite float range before handing them to native vertices.
Do not alter existing drawfill's input policy.

Production scope is `Quake/pr_ext.c`: one extracted submit helper, one wrapper,
one CSQC-only 315 registry entry. No SSQC handler; no capability added. Reopen
the design if this requires a render-pass, shader, graphics-feature or lifecycle
change. Legacy OpenGL calls cannot be copied into this Vulkan engine; their
argument/shape contract remains the behavioral reference.

## Acceptance

Personal local Astra source review must check the reference contract, unchanged
drawfill submission, number/VM permissions, quad winding, scale/width math,
finite conversion boundaries and native GUI/VR transform reuse. Final Linux/ARM
software qualification must cover named/numeric 315, rejection in SSQC,
horizontal/vertical/diagonal/reversed and zero-length endpoints, fractional and
negative widths, alpha/color endpoints, clipping, absolute/relative scales and
VR panel source extents. Builds and tests remain deferred until implementation
is finished; headset edge coverage is a later user-owned visual check.

## Local Astra source disposition

Personal local Astra Max found no introduced P1/P2. It verified the unchanged
drawfill allocation, corners, colors, triangle order, descriptor/pipeline and draw
command; the new stroke's winding, anisotropic scaling, defaults and finite
conversion boundaries; slot 315 and native SSQC rejection; and reuse of existing
canvas/panel transforms and clipping. Main spotchecked the shared submission and
source-pixel perpendicular math against the actual diff and display contract.

This accepts the bounded source change only. Source-pixel thickness is applied
before VR panel projection; actual final eye-pixel coverage and OpenGL edge
equivalence are not proven. No builds, tests, compiler probes or performance
measurements were performed. The planned final software qualification remains.
