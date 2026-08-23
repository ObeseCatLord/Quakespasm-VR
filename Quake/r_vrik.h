/* VRIK renderer support shared by the alias draw paths. */
#ifndef R_VRIK_H
#define R_VRIK_H

#include "quakedef.h"

/* CPU skinning is immutable for a remote player over one host frame. */
typedef struct r_vrik_skincache_s
{
	qmodel_t *model;
	int hostframe;
	unsigned int pose_generation;
	unsigned char avatar_id;
	const aliashdr_t *surface;
	md5vertex_t *vertices;
	int capacity;
	unsigned short *body_indexes;
	int body_index_capacity;
	int body_numindexes;
	const aliashdr_t *prop_surface;
	md5vertex_t *prop_vertices;
	unsigned short *prop_indexes;
	int prop_vertex_capacity;
	int prop_index_capacity;
	int prop_numverts;
	int prop_numindexes;
	int prop_semantic;
	/* Canonical Ranger pose is kept separately so target retargeting may never
	 * overwrite a source joint before one of its descendants consumes it. */
	float canonical_palette[MAX_MD5_JOINTS * 12];
	float palette[MAX_MD5_JOINTS * 12];
	qboolean ready;
	qboolean muzzle_valid;
	vec3_t muzzle_origin;
	vec3_t muzzle_forward;
} r_vrik_skincache_t;

/* These are deliberately renderer-local rather than protocol flags.  The
 * protocol-v3 owner will translate its canonical target mask into this small
 * input at the receive boundary.  Every Set replaces all roles, so a vanished
 * tracker cannot leave a stale target behind. */
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
	unsigned char	present_mask;
	unsigned char	tracked_mask;
	unsigned char	predicted_mask;
	float		confidence[R_VRIK_LOWER_ROLE_COUNT];
	vec3_t		position[R_VRIK_LOWER_ROLE_COUNT];
	vec3_t		orientation[R_VRIK_LOWER_ROLE_COUNT];
} r_vrik_lowerbody_targets_t;

/* Model-space form used only after r_alias has converted root-local Quake
 * axes to the validated avatar basis.  Matrix origins are ignored. */
typedef struct r_vrik_lowerbody_model_targets_s
{
	unsigned char	usable_mask;
	unsigned char	orientation_mask;
	float		confidence[R_VRIK_LOWER_ROLE_COUNT];
	vec3_t		position[R_VRIK_LOWER_ROLE_COUNT];
	float		orientation[R_VRIK_LOWER_ROLE_COUNT][12];
} r_vrik_lowerbody_model_targets_t;

/* The default preserves each animated leg's authored bend plane.  The paired
 * policy derives one outer-leg pair from those animated/bind poles and mirrors
 * it across the sagittal plane midway between the two upper-leg roots. */
typedef enum r_vrik_lowerbody_pole_policy_e
{
	R_VRIK_LOWERBODY_POLES_ANIMATED = 0,
	R_VRIK_LOWERBODY_POLES_MIRRORED_PAIR
} r_vrik_lowerbody_pole_policy_t;

/* Native Ranger bind-space references.  They intentionally expose the
 * validated authored transforms, not guessed human dimensions; a calibration
 * caller maps them through its existing avatar/tracking basis. */
typedef struct r_vrik_calibration_reference_s
{
	float		transform[R_VRIK_LOWER_ROLE_COUNT][12];
	float		head_transform[12];
	vec3_t		lateral;
	vec3_t		forward;
	vec3_t		up;
} r_vrik_calibration_reference_t;

/* Tracking-space input/output for calibration.  `floor_height` is the scalar
 * floor coordinate along `up` (dot(point, up)); all vectors use metres.  The
 * three basis vectors must form a normalized horizontal forward/right/up
 * frame, with right = forward x up.  Quaternion results are w,x,y,z. */
typedef struct r_vrik_calibration_projection_input_s
{
	float		hmd_position[3];
	float		floor_height;
	float		forward[3];
	float		right[3];
	float		up[3];
} r_vrik_calibration_projection_input_t;

typedef struct r_vrik_calibration_projection_s
{
	float		position[R_VRIK_LOWER_ROLE_COUNT][3];
	float		orientation_wxyz[R_VRIK_LOWER_ROLE_COUNT][4];
	float		metres_per_bind_unit;
} r_vrik_calibration_projection_t;

r_vrik_skincache_t *R_VRIKGetSkinCache (int entitynum, qmodel_t *model);
void R_VRIKResetSkinCaches (void);
qboolean R_VRIKSkinCacheReady (const r_vrik_skincache_t *cache,
	qmodel_t *model);
qboolean R_VRIKSkinCacheReserve (r_vrik_skincache_t *cache, int numverts);
void R_VRIKSkinCacheCommit (r_vrik_skincache_t *cache,
	const aliashdr_t *surface);

/* Wire/network code owns target lifetime and must call Set for every accepted
 * pose.  A predicted role is usable only for that supplied pose and is never
 * retained or blended with an older role sample. */
void R_VRIKSetLowerBodyTargets (int entitynum,
	const r_vrik_lowerbody_targets_t *targets);
void R_VRIKClearLowerBodyTargets (int entitynum);
qboolean R_VRIKGetLowerBodyTargets (int entitynum,
	r_vrik_lowerbody_targets_t *out);

/* Applies validated optional hips/legs to an animation-first model-space
 * palette.  False means the live model did not provide the verified Ranger
 * hierarchy, so the caller must retain its upper-body-only palette. */
qboolean R_VRIKApplyLowerBody (const md5liveinfo_t *live, float *palette,
	const r_vrik_lowerbody_model_targets_t *targets);
/* Explicit lower-body entry point for avatars that need paired outer-leg
 * bend planes.  Target positions, confidence, and requested orientations are
 * interpreted exactly as by R_VRIKApplyLowerBody; only the two-leg knee pole
 * selection differs when both feet are supplied. */
qboolean R_VRIKApplyLowerBodyWithPolePolicy (const md5liveinfo_t *live,
	float *palette, const r_vrik_lowerbody_model_targets_t *targets,
	r_vrik_lowerbody_pole_policy_t pole_policy);
qboolean R_VRIKGetCalibrationReference (qmodel_t *model,
	r_vrik_calibration_reference_t *out);
qboolean R_VRIKProjectCalibrationReference (qmodel_t *model,
	const r_vrik_calibration_projection_input_t *input,
	r_vrik_calibration_projection_t *out);

#endif /* R_VRIK_H */
