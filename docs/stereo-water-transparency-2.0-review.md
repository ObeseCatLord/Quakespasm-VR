# Q01 stereo water/transparency senior disposition

2026-10-01. Main synthesis of the local Astra review of
[the verified brief](stereo-water-transparency-2.0-brief.md). Effective reviewer
settings were verified as `gpt-6-astra` / `xhigh` from the matching turn-context
model/effort fields only. Read-only source assessment; no executable checks.

| Recommendation | Main disposition |
| --- | --- |
| Retain native sorted lists and alpha/water stage order; use eye-liquid category masks | Adopt. Current sorting deliberately remains shared, including in the primary reference. The demonstrated mismatch is category placement against each eye's liquid contents, not a need for another sorter. |
| Reuse unused eye_offset[].w rather than extending the uniform ABI | Adapt the brief. Current publication zeros w; shader consumers use xyz. Zero means participation, one exclusion. Preserve the existing 160-byte layout and copy the effective scene payload, including waterwarp. |
| Clip the excluded view through the shared vertex macro | Adopt as the planned narrow boundary. Use finite outside-x clip position vec4(2,0,0,1), keeping the macro one statement for basic.vert's unbraced caller. Rendered qualification remains required. |
| Select payload descriptor and dynamic offset at R_BindPipeline | Adopt. Main checked that set5 is bound even when pipeline identity is unchanged. Use existing uniform allocation/lifetime, flush before changing selection, restore ordinary state, preserve display/UI routing. |
| Serialize exceptional alpha recording in the existing indexed callback | Adopt. Main confirmed alias lighting writes the shared entity lightcache, and the existing indexed task has frame/preparation dependencies. On eye disagreement, invocation zero records both existing contexts sequentially; invocation one returns. Make the decision after dependencies, not during task construction. |
| Remove repeated local nonalias pitch mutation | Adopt at the narrow draw-transform boundary. Main confirmed current multiplication would be applied twice. Serialization alone does not preserve the first-draw transform; do not temporarily mutate/restore shared entities around concurrent consumers. |
| Reset persistent context selectors and frame fallback state | Adopt. Main checked primary/secondary reset in gl_vidsdl.c and stereo scene publication/fallback in gl_rmain.c. Include the small reset seam in the implementation write set. |
| Add new single-eye passes, force OIT or sort twice | Reject without a demonstrated failure of the smaller adapter. Existing topology, common water draw, conservative visibility and opaque single-pass stereo remain reusable. |

Let E hold wet-eye bits. Before world water, overwater entities participate in E
and underwater entities in 3^E; afterward, these masks reverse. Zero is skipped;
three uses the ordinary scene payload. Only E=1 or E=2 needs exceptional
allocations and serialized repeated recording. Both eyes agreeing against the
center still select the correct category. Sort disabled, OIT enabled and invalid
stereo-world frames retain their existing paths.

The clipping assessment is an inference from the official
[Vulkan multiview contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkRenderPassMultiviewCreateInfo.html)
and [primitive clipping contract](https://docs.vulkan.org/spec/latest/chapters/vertexpostproc.html#clipping),
not a rendered result or measured performance claim. No quad views are involved.

Before implementation, commit a bounded plan with exact transform/context owner
changes. The brief's 100–160-line estimate is unverified; reopen if the patch
requires new passes, policy owners or broad entity preparation. Final rendered
acceptance covers both eye-disagreement directions, agreement including center
mismatch, transitions/worldless fallback, tasks on/off, local pitch, brush liquid,
MDL/MD3/MD5/sprite variants, OIT/sort, MSAA/AO and foveation toggles. Gameplay and
pose preparation remain once per frame. Q01 is not closed by source inspection.
