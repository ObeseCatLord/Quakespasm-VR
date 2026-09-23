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

/* Interpolate the two absolute animation poses using lerpdata, then apply the
 * donor Ranger head/arm and held-prop solve from pose.  Lower body, retargeting,
 * CPU vertex skinning, model state, and renderer state are deliberately outside
 * this adapter. */
r_vrik_palette_result_t R_VRIKBuildRangerPalette (
	const md5_skeleton_view_t *skeleton, const lerpdata_t *lerpdata,
	const vrik_pose_t *pose, qboolean muzzleflash,
	r_vrik_palette_output_t *out);

#endif /* R_VRIK_H */
