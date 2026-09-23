/* Frame-owned Vulkan palette records for tracked player rendering. */
#ifndef R_VRIK_RENDER_H
#define R_VRIK_RENDER_H

#include "quakedef.h"

typedef struct r_vrik_prepared_palette_s
{
	const entity_t *entity;
	const qmodel_t *model;
	/* Root geometry selected by Mod_Extradata_CheckSkin for this frame. */
	const aliashdr_t *geometry;
	VkDescriptorSet descriptor_set;
	/* Joint index relative to descriptor_set's aggregate palette slice. */
	uint32_t joint_offset;
	uint32_t joint_count;
	/* Exact start of this palette in the same allocation; already includes joint_offset. */
	VkDeviceAddress palette_address;
	/* Prepared from this solved palette and every surface in geometry's selected chain. */
	double tracked_cull_local_bound;
	vec3_t tracked_cull_origin;
	qboolean tracked_cull_valid;
} r_vrik_prepared_palette_t;

/* Prepare after the matching frame-slot fence and dynamic-buffer swap. */
void R_VRIKRenderPrepareFrame (uint32_t frame_slot);

/* Read-only until this frame slot's fence is waited before reuse. */
const r_vrik_prepared_palette_t *R_VRIKRenderLookup (const entity_t *entity);

/* Called after device idle before renderer resources or the device are destroyed. */
void R_VRIKRenderShutdown (void);

#endif /* R_VRIK_RENDER_H */
