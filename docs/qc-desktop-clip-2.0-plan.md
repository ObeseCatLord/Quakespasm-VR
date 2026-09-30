# Desktop QC clipping at the Vulkan boundary

Date: 2026-09-30. Scope is only the non-panel branch of
`Quake/pr_ext.c:PF_cl_drawsetclip` on `2.0`.

## Evidence and behavior

Native vkQuake and the current destination multiply the four QC arguments by
the CSQC display's pixel scales, then assign them directly to `VkRect2D`.
Primary `Quake/pr_cmds.c:PF_cl_drawsetclip` submits an OpenGL scissor instead.
A negative origin is legal for that [OpenGL clipping boundary](https://wikis.khronos.org/opengl/GLAPI/glScissor), but Vulkan
[requires nonnegative offsets and no signed offset-plus-extent overflow](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdSetScissor.html)
(VUIDs 00595–00597). For example, a HUD clipping rectangle partially left of
the screen currently supplies a negative Vulkan offset.

The existing VR panel branch calls `GL_SetUIPanelSourceClip`, whose source-space
clip validation and shader intersection are separate, already implemented
owners. Keep that branch exactly as it is. `SCR_GetCSQCDisplay` continues to
own absolute/relative scaling; native canvas projection and framebuffer
coordinate conventions remain unchanged.

## Minimal adapter versus replacement

Retain the existing float scaling and integer truncation for ordinary desktop
rectangles. Before constructing the Vulkan rectangle, reject nonfinite scaled
inputs, nonpositive width/height or invalid display scales to an empty scissor.
For valid inputs, truncate origin and extent separately as native conversion
does, calculate endpoints in double precision, and intersect each axis with
`[0, vid.width]` or `[0, vid.height]`. Only then convert the bounded coordinates
and differences to the existing integer Vulkan fields. Empty intersections
record a zero-extent scissor, replacing any previous clip. Keep reset unchanged.

This preserves valid in-frame native geometry and clips partial off-screen
geometry without invalid Vulkan offsets. Nonpositive/nonfinite requests become
empty, consistent with the existing VR panel validation policy; behavior from
an invalid legacy graphics call is not copied. No new persistent clip state,
cache, generic service, canvas transform, shader or rendering path is needed.
A replacement clipping system would duplicate existing desktop/panel ownership
without a demonstrated need. Expected scope: approximately 30 added/replaced
lines in one existing wrapper. Do not change adjacent production functions.

## Sequence and acceptance

Commit this plan before production. Main implements the bounded wrapper while
the web worker owns separate stat/precache functions in the same file; neither
may overwrite the other's edits. Request a bounded local Astra source review
of the actual diff, field conversion, native quantization and unchanged panel
branch/reset. This is a routine API-boundary repair, not a new renderer design.

No builds, tests, compiler/runtime probes, fixtures or measurements before the
full implementation finishes. `git diff --check` is allowed. Deferred Linux/ARM
qualification must cover interior/fractional, partly negative, fully outside,
oversized, zero/negative/nonfinite rectangles, both pixel scales, clip-reset and
desktop/VR panel transition. Actual Vulkan validation and HUD output remain
required final evidence; source reasoning alone does not certify those results.

## Implementation and source disposition

`0b54a828` implements only the planned non-panel wrapper conversion. A local
requested-Astra/Max read-only review found no actionable introduced defect.
Main checked the retained float products, separate origin/extent truncation,
double-precision `CLAMP` dispatch, integer framebuffer fields, bounded casts,
existing command-buffer identity and unchanged panel/reset branches.

| Review contract | Disposition |
| --- | --- |
| Valid in-frame quantization remains native | Accepted: origin and extent still truncate independently after the same float scaling. |
| Vulkan offsets and signed endpoint sums remain valid | Accepted: endpoints are intersected with positive integer framebuffer dimensions before casts; no negative offsets or out-of-range sums are submitted. |
| Empty or invalid request replaces prior clipping | Accepted: zero-extent rectangle is always recorded when no valid nonempty intersection exists. |
| VR panel/native canvas ownership remains | Confirmed: panel source clip and reset branches unchanged; no persistent state or shader change. |

`git diff --check` passed. Effective reviewer model metadata was not exposed;
the requested-Astra/Max result is bounded advisory source acceptance, not a
certified senior-skill pass. Linux/ARM execution, Vulkan validation, HUD output
and desktop/panel transitions remain deferred. No builds/tests/compiler/runtime
probes/fixtures or measurements were performed.
