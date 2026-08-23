/* Semantic re-release avatar rigs and Ranger-palette retargeting. */
#ifndef R_AVATAR_H
#define R_AVATAR_H

#include "quakedef.h"
#include "player_avatar.h"

typedef enum r_avatar_family_e {
	R_AVATAR_FAMILY_HUMANOID = 0,
	R_AVATAR_FAMILY_DIGITIGRADE,
	R_AVATAR_FAMILY_QUADRUPED,
	/* Vore's centre leg intentionally remains bind-following. */
	R_AVATAR_FAMILY_TRIPOD
} r_avatar_family_t;

typedef enum r_avatar_equipment_policy_e {
	/* Use the canonical Ranger prop bones. */
	R_AVATAR_EQUIPMENT_RANGER = 0,
	/* Ignore authored monster equipment and attach the actual player weapon. */
	R_AVATAR_EQUIPMENT_ATTACH_HAND
} r_avatar_equipment_policy_t;

#define R_AVATAR_CAP_HEAD             (1u << 0)
#define R_AVATAR_CAP_ARMS             (1u << 1)
#define R_AVATAR_CAP_LEGS             (1u << 2)
#define R_AVATAR_CAP_RETARGET         (1u << 3)
#define R_AVATAR_CAP_STANDARD_WEAPON  (1u << 4)

/* A semantic can be present solely for downstream IK; it does not receive a
 * second ambiguous animation delta when it aliases a real target joint. */
#define R_AVATAR_MAP_VIRTUAL          (1u << 0)

typedef struct r_avatar_joint_map_s {
	const char *name;
	unsigned char flags;
} r_avatar_joint_map_t;

typedef struct r_avatar_profile_s {
	player_avatar_id_t id;
	const char *key;
	const char *model_path;
	r_avatar_family_t family;
	unsigned int capabilities;
	float display_scale;
	r_avatar_equipment_policy_t equipment_policy;
	const char *native_equipment_joint[4];
	r_avatar_joint_map_t joint[MD5_VRIK_JOINT_COUNT];
} r_avatar_profile_t;

/* Runtime resolution has no ownership of live data.  joint[] are the
 * target's semantic indices; canonical_joint[] are the source Ranger
 * semantics consumed by the retargeter and deliberately stay stable. */
typedef struct r_avatar_rig_s {
	const r_avatar_profile_t *profile;
	const md5liveinfo_t *live;
	int joint[MD5_VRIK_JOINT_COUNT];
	int canonical_joint[MD5_VRIK_JOINT_COUNT];
	unsigned int virtual_mask;
	qboolean valid;
} r_avatar_rig_t;

const r_avatar_profile_t *R_AvatarProfileForId (int id);
const r_avatar_profile_t *R_AvatarProfileForModelPath (const char *path);
qboolean R_AvatarResolveRig (const r_avatar_profile_t *profile,
	const md5liveinfo_t *live, r_avatar_rig_t *out);

/* Translates a solved canonical Ranger global palette to target global
 * matrices.  Motions are copied as local bind-relative rotations only: every
 * target local translation remains its authored bind translation. */
qboolean R_AvatarRetargetPalette (const r_avatar_rig_t *source,
	const r_avatar_rig_t *target, const float *source_palette,
	float *target_palette);

/* Single-rig semantic body bases: canonical body forward/left/up to this
 * rig's model space and its inverse.  Cross-rig presentation should use the
 * full source+target affine functions below, which preserve Ranger's actual
 * authored model-space basis rather than assuming world axes. */
qboolean R_AvatarCanonicalToTargetBasis (const r_avatar_rig_t *rig,
	float out[12]);
qboolean R_AvatarTargetToCanonicalBasis (const r_avatar_rig_t *rig,
	float out[12]);

/* Full presentation affines.  They include the selected display scale and
 * translation that anchors target bind Hip to canonical Ranger bind Hip.
 * They are for skinned points/vectors and refinement targets only, never for
 * the rigid bone palette itself. */
qboolean R_AvatarTargetToCanonicalPresentation (const r_avatar_rig_t *source,
	const r_avatar_rig_t *target, float out[12]);
qboolean R_AvatarCanonicalToTargetPresentation (const r_avatar_rig_t *source,
	const r_avatar_rig_t *target, float out[12]);

/* Renderer scale is rounded to a stable 1/4096th to avoid tiny platform
 * differences changing cached presentation transforms. */
float R_AvatarQuantizedDisplayScale (const r_avatar_profile_t *profile);

#endif /* R_AVATAR_H */
