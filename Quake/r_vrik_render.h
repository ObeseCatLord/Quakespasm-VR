/* Frame-owned Vulkan palette records for tracked player rendering. */
#ifndef R_VRIK_RENDER_H
#define R_VRIK_RENDER_H

#include "quakedef.h"

extern cvar_t r_avatar_humanoid;

/* CPU-only light socket, published with the complete prepared palette. */
typedef struct r_vrik_prepared_muzzle_s
{
	const qmodel_t *original_model;
	int avatar_id;
	unsigned int generation;
	unsigned char tracking_flags;
	double time;
	vec3_t origin;
	qboolean valid;
} r_vrik_prepared_muzzle_t;

#define R_VRIK_RENDER_MAX_ATTACHMENTS 2
typedef struct r_vrik_attachment_s
{
	const aliashdr_t *geometry;
	float to_canonical[12];
	double local_bound;
	qboolean valid;
} r_vrik_attachment_t;

typedef struct r_vrik_prepared_palette_s
{
	const entity_t *entity;
	const qmodel_t *model;
	/* Root geometry selected for this frame (admitted MD5 for alternates). */
	const aliashdr_t *geometry;
	/* Target model space to canonical Ranger model space, including static
	 * bind-floor correction; valid for alternates. */
	float target_to_canonical[12];
	qboolean alternate_avatar;
	/* Root-local static views: Ranger publishes one, QBJ publishes zero or two. */
	r_vrik_attachment_t attachments[R_VRIK_RENDER_MAX_ATTACHMENTS];
	uint32_t attachment_count;
	VkDescriptorSet descriptor_set;
	/* Joint index relative to descriptor_set's aggregate palette slice. */
	uint32_t joint_offset;
	uint32_t joint_count;
	/* Exact start of this palette in the same allocation; already includes joint_offset. */
	VkDeviceAddress palette_address;
	/* Yaw from the accepted tracked pose that solved this palette. */
	float tracked_root_yaw;
	qboolean tracked_root_valid;
	/* Origin-centered bound from the solved palette and selected surface chain;
	 * alternates include the target-to-canonical affine. */
	double tracked_cull_local_bound;
	vec3_t tracked_cull_origin;
	qboolean tracked_cull_valid;
	r_vrik_prepared_muzzle_t muzzle;
} r_vrik_prepared_palette_t;

/* Main thread, before this frame's render tasks. Reserve and stage supported
 * players and attributed QBJ3 corpses into one bounded frame list.
 * Call before SCR_UpdateScreen transfers prev_end_rendering_task to its new
 * begin task. On a needed first load, Stage joins prior CPU render tasks before
 * admission and its possible model/texture uploads; loaded cached IDs do not join.
 * This is the only render path that may load an avatar model. */
void R_VRIKRenderStageAvatars (void);

/* Shared raster and BLAS admission for supported original player models. */
qboolean R_VRIKRenderOriginalModelEligible (const entity_t *entity);

/* Main-thread content reset hook; permits retry of previously missing packs. */
void R_VRIKRenderResetAdmission (void);

/* Main-thread reset after prior render completion; retires active publication. */
void R_VRIKRenderInvalidatePublication (void);

/* Prepare after the matching frame-slot fence and dynamic-buffer swap. */
void R_VRIKRenderPrepareFrame (uint32_t frame_slot);

/* Read-only until this frame slot's fence is waited before reuse. */
const r_vrik_prepared_palette_t *R_VRIKRenderLookup (const entity_t *entity);

/* Same main-thread boundary as the getter; retires a socket even without a flash. */
void R_VRIKRenderInvalidateMuzzle (const entity_t *entity);

/* Main-thread read after prior draw completion, before the next render tasks.
 * Copies only the CPU point; the getter invalidates stale/changed/discontinuous
 * records until fresh preparation. Never borrows palette memory or geometry. */
qboolean R_VRIKRenderGetMuzzleOrigin (const entity_t *entity, qboolean discontinuity, vec3_t origin);

/* Called after device idle before renderer resources or the device are destroyed. */
void R_VRIKRenderShutdown (void);

#endif /* R_VRIK_RENDER_H */
