# Unused-attachment synchronization: local senior disposition

2026-10-01. Read-only local `gpt-6-astra`, explicit `xhigh`, reviewing the
[verified brief](unused-attachment-sync-2.0-brief.md) and live creation evidence.
Main independently checked the reviewer's effective turn-context model/effort;
both match the required settings. The reviewer's terminal caveat that it could
not verify its own routing is superseded by that main-side verification. No raw
session telemetry is reproduced here. No source edits or qualified repair.

## Main verification

The live creation capture in private desktop-hazard-live-pass.log confirms an
early one-subpass desktop MSAA/OIT pass with five attachment descriptions, color
reference2, depth reference1 and no resolve. Logical attachments0/3/4 are unused
but have declared layout transitions. Current and untouched baseline pass
compilers keep these descriptions. The actual validation report describes a
prior subpass1 store versus a later layout transition; callback objects do not
identify the image. Different acquired swapchain indices strengthen, but do not
prove, the shared-image hypothesis. Logical scene-image selection depends on
screen_effects and must still be correlated with the actual framebuffer.

Main spot-checked native UI attachment0's DONT_CARE store operation and its
shader-read final layout in r_passes.c:1443–1449. The renderer's physical mapping,
special rate attachment ordering and clear-value owners remain native.
Vulkan retains unused attachments' declared transitions; load/store omission
does not eliminate them. [Official attachment specification](https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentDescription.html).

## Dispositions

| Senior recommendation | Main disposition |
| --- | --- |
| Test one outside-render-pass global memory barrier at the existing frame boundary before a larger repair. | Adopted as a diagnostic candidate only: r_passes.c, approximately10–20 changed lines, reopen before25. An ALL_COMMANDS/MEMORY_WRITE to ALL_COMMANDS/MEMORY_READ\|MEMORY_WRITE barrier may order earlier queue submissions without guessing image layouts. Requires a committed before-code plan and actual validation/rendered comparison; a clean diagnostic would not establish exact-image identity or production suitability. |
| Use stable unused-attachment compaction if the smaller boundary cannot satisfy correctness/performance aims. | Adapted as the bounded fallback, not an approved rewrite: r_passes.c only,100–140 lines, reopen before160. Preserve logical policy calculations, distinguish retention from actual use, copy/remap references/preserve indices/views/clear values, retain special rate attachments and shader slot ordering. Existing pass/binding/resource/task owners remain authoritative. |
| Do not guess equal layouts or rely on a generic outgoing subpass edge for unused attachments. | Adopted. No demonstrated attachment-use scope supports those fixes. |
| Identify the conflicting resource before declaring a color0-specific fix proven. | Adopted. Need command-buffer generation/pass ordinal, begun framebuffer, scene slot and image/view/subresource correlation with validator state; another callback-object dump cannot supply that identity. |
| Require clean native desktop rendering, affected stereo matrix and lifecycle coverage. | Adopted. Existing 24-probe XR result remains bounded historical evidence; rerun affected current-tree coverage after a source repair. Desktop normal quit remains a separate obligation. |
| Require measured absence of a timing regression. | Adapted to the user's explicit exclusion of performance measurement. No timing test is a goal gate. Avoid a broad production serialization fix without a defensible ordering/cost argument; document any remaining performance risk for user measurement. No speedup or negligible-overhead claim is supported. |

No new missing feature or human decision was established. These candidates attach
to final checklist groups2/3/8. No production renderer change, final package
acceptance or goal signoff follows from this review.
