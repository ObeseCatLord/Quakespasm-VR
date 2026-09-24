/* Pure CPU adapter for the inherited canonical Ranger MD5 VRIK palette. */
#ifndef R_VRIK_H
#define R_VRIK_H

#include "quakedef.h"

typedef enum r_vrik_palette_result_e
{
	R_VRIK_PALETTE_OK,
	R_VRIK_PALETTE_INVALID_ARGUMENT,
	R_VRIK_PALETTE_INVALID_SKELETON,
	R_VRIK_PALETTE_INVALID_POSE,
	R_VRIK_PALETTE_INSUFFICIENT_CAPACITY
} r_vrik_palette_result_t;

/* Caller owns matrices and provides capacity in joints.  Every successful
 * result contains absolute, row-major 3x4 MD5 matrices suitable for upload to
 * the existing Vulkan skinning palette.  The output is unchanged on failure. */
typedef struct r_vrik_palette_output_s
{
	float (*matrices)[12];
	size_t capacity;
	size_t joint_count;
	qboolean muzzle_valid;
	vec3_t muzzle_origin;
	vec3_t muzzle_forward;
} r_vrik_palette_output_t;

/* Protocol-v3 lower targets sampled from one entity frame.  present_mask
 * means the payload is currently usable; tracked_mask distinguishes fresh
 * hardware samples from sender-held predictions.  Predicted values are kept
 * only from the newest sample and are never interpolated with older poses. */
typedef enum r_vrik_lower_role_e
{
	R_VRIK_LOWER_HIP = 0,
	R_VRIK_LOWER_LEFT_FOOT,
	R_VRIK_LOWER_RIGHT_FOOT,
	R_VRIK_LOWER_ROLE_COUNT
} r_vrik_lower_role_t;

#define R_VRIK_LOWER_BIT(role) (1u << (unsigned int)(role))

typedef struct r_vrik_lowerbody_targets_s
{
	unsigned char present_mask;
	unsigned char tracked_mask;
	unsigned char predicted_mask;
	float confidence[R_VRIK_LOWER_ROLE_COUNT];
	vec3_t position[R_VRIK_LOWER_ROLE_COUNT];
	vec3_t orientation[R_VRIK_LOWER_ROLE_COUNT];
} r_vrik_lowerbody_targets_t;

/* Sample only v3 hip/foot roles.  The output is cleared on failure. */
qboolean R_VRIKSampleEntityLowerTargets (const entity_t *entity,
	r_vrik_lowerbody_targets_t *out);

/* Interpolate the two absolute animation poses using lerpdata, then apply the
 * donor Ranger head/arm and held-prop solve from pose.  Optional v3 hip/foot
 * roles are mapped through the same animated body basis and solved in this
 * same frame palette.  Retargeting, CPU vertex skinning, model state, and
 * renderer state remain outside this adapter. */
r_vrik_palette_result_t R_VRIKBuildRangerPalette (
	const md5_skeleton_view_t *skeleton, const lerpdata_t *lerpdata,
	const vrik_pose_t *pose, const r_vrik_lowerbody_targets_t *lower_targets,
	qboolean muzzleflash,
	r_vrik_palette_output_t *out);

#endif /* R_VRIK_H */
