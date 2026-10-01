# F06 preserve static depth replay with KHR foveation

2026-10-01. Actual Vulkan validation1.4.357 reports
VUID-vkCmdDrawIndexedIndirect-None-08608 on
world_hidden_area_depth_replay1 after vkCmdSetFragmentShadingRateKHR.
Private native diagnostic reached12loaded stereo frames with KHR active,
4xMSAA and7056rate-map bytes; output/normal exit is not a validation pass.

Verified production: gl_rmisc creates both depth replay families from the
static world base, with vertex-only stages. Opaque color variants explicitly
add dynamic shading-rate state. r_world/R_FlushBatch and r_brush indirect draw
paths invoke the shared R_SetWorldFragmentShadingRate after binding either
family; their opaque check does not exclude cbx->depth_only.

Smallest repair: make that existing helper return when cbx->depth_only.
Keep static full-rate depth pipelines, native binds, replay, hidden stencil,
SSAO and eligible/protected color combiner policy. One guard in gl_rmisc.c,
no new pipeline state, shader, renderer topology or duplicated caller policy.
Adding dynamic state to every depth pipeline is unnecessary and would require
new full-rate setup. Disabling foveation/SSAO or filtering validation is rejected.

After-code main source review and host rebuild, rerun the exact explicit fixed
GPU case with validation and natural quit. Preserve both failed script timeouts
and native validation finding. The native diagnostic's post-normal-exit bt caused
GDBexit1; correct only its post-run classification before rerun. Other F06
transitions and final affected Linux/ARM packages remain open.
Official validity owner: Vulkan vkCmdDrawIndexedIndirect reference,
https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDrawIndexedIndirect.html
