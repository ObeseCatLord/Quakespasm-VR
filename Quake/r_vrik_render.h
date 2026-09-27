/* Frame-owned Vulkan palette records for tracked player rendering. */
#ifndef R_VRIK_RENDER_H
#define R_VRIK_RENDER_H

#include "quakedef.h"

typedef struct r_vrik_prepared_palette_s
{
	const entity_t *entity;
	const qmodel_t *model;
	/* Root geometry selected for this frame (admitted MD5 for alternates). */
	const aliashdr_t *geometry;
	/* Target model space to canonical Ranger model space; valid for alternates.
	 * The source renderer's bind-floor correction is not included here. */
	float target_to_canonical[12];
	qboolean alternate_avatar;
	VkDescriptorSet descriptor_set;
	/* Joint index relative to descriptor_set's aggregate palette slice. */
	uint32_t joint_offset;
	uint32_t joint_count;
	/* Exact start of this palette in the same allocation; already includes joint_offset. */
	VkDeviceAddress palette_address;
	/* Origin-centered bound from the solved palette and selected surface chain;
	 * alternates include the target-to-canonical affine. */
	double tracked_cull_local_bound;
	vec3_t tracked_cull_origin;
	qboolean tracked_cull_valid;
} r_vrik_prepared_palette_t;

/* Main thread, before this frame's render tasks. Stage one fixed built-in
 * selection for each original player entity each frame.
 * Call before SCR_UpdateScreen transfers prev_end_rendering_task to its new
 * begin task. On a needed first load, Stage joins prior CPU render tasks before
 * admission and its possible model/texture uploads; loaded cached IDs do not join.
 * Ranger clears a prior selection. False clears it on admission failure.
 * This is the only path that may load a built-in avatar. */
qboolean R_VRIKRenderStageBuiltinAvatar (const entity_t *entity, int id);

/* Main-thread content reset hook; permits retry of previously missing packs. */
void R_VRIKRenderResetAdmission (void);

/* Prepare after the matching frame-slot fence and dynamic-buffer swap. */
void R_VRIKRenderPrepareFrame (uint32_t frame_slot);

/* Read-only until this frame slot's fence is waited before reuse. */
const r_vrik_prepared_palette_t *R_VRIKRenderLookup (const entity_t *entity);

/* Called after device idle before renderer resources or the device are destroyed. */
void R_VRIKRenderShutdown (void);

#endif /* R_VRIK_RENDER_H */
