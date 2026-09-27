/* Semantic re-release avatar rigs and safe bind-relative retargeting. */
#include "quakedef.h"
#include "r_avatar.h"
#include "r_vrik.h"

#include <math.h>
#include <string.h>

#define A(name) { name, 0 }
#define V(name) { name, R_AVATAR_MAP_VIRTUAL }
#define N { NULL, 0 }

/* The donor view uses size_t; the retargeter uses bounded int indexes. */
static int R_AvatarJointCount (const md5_skeleton_view_t *view)
{
	return view->joint_count > R_AVATAR_MAX_JOINTS
		? R_AVATAR_MAX_JOINTS + 1 : (int)view->joint_count;
}

/* The paths are the fixed, allowlisted classic aliases.  They are not remote
 * input and must never be replaced by a server supplied string. */
static const r_avatar_profile_t r_avatar_profiles[PLAYER_AVATAR_COUNT] = {
	[PLAYER_AVATAR_RANGER] = {
		PLAYER_AVATAR_RANGER, "ranger", "progs/player.md5mesh",
		R_AVATAR_FAMILY_HUMANOID, R_AVATAR_CAP_HEAD | R_AVATAR_CAP_ARMS |
		R_AVATAR_CAP_LEGS | R_AVATAR_CAP_RETARGET | R_AVATAR_CAP_STANDARD_WEAPON,
		1.0f, R_AVATAR_EQUIPMENT_RANGER, { "Gun", "Axe", NULL, NULL }, {
			A("Hip"), A("Spine1"), A("Spine2"), A("Neck"), A("Head"),
			A("Shoulder_L"), A("UpperArm_L"), A("LowerArm_L"), A("Hand_L"),
			A("Shoulder_R"), A("UpperArm_R"), A("LowerArm_R"), A("Hand_R"),
			A("UpperLeg_L"), A("LowerLeg_L"), A("Foot_L"),
			A("UpperLeg_R"), A("LowerLeg_R"), A("Foot_R"), A("Gun"), A("Axe"),
			A("small_flame"), A("big_flame") },
		R_AVATAR_BASIS_HUMANOID, { NULL, NULL, NULL, NULL }, false, 1.0f, 0.35f, false
	},
	[PLAYER_AVATAR_SOLDIER] = {
		PLAYER_AVATAR_SOLDIER, "soldier", "progs/soldier.md5mesh",
		R_AVATAR_FAMILY_HUMANOID, R_AVATAR_CAP_HEAD | R_AVATAR_CAP_ARMS |
		R_AVATAR_CAP_LEGS | R_AVATAR_CAP_RETARGET | R_AVATAR_CAP_STANDARD_WEAPON,
		1.0f, R_AVATAR_EQUIPMENT_ATTACH_HAND, { "Gun", NULL, NULL, NULL }, {
			A("Hip"), A("Spine1"), A("Spine2"), A("Neck"), A("Head"),
			A("Shoulder_L"), A("UpperArm_L"), A("LowerArm_L"), A("Hand_L"),
			A("Shoulder_R"), A("UpperArm_R"), A("LowerArm_R"), A("Hand_R"),
			A("UpperLeg_L"), A("LowerLeg_L"), A("Foot_L"),
			A("UpperLeg_R"), A("LowerLeg_R"), A("Foot_R"), N, N, N, N },
		R_AVATAR_BASIS_HUMANOID, { NULL, NULL, NULL, NULL }, false, 1.0f, 0.35f, false
	},
	[PLAYER_AVATAR_ENFORCER] = {
		PLAYER_AVATAR_ENFORCER, "enforcer", "progs/enforcer.md5mesh",
		R_AVATAR_FAMILY_HUMANOID, R_AVATAR_CAP_HEAD | R_AVATAR_CAP_ARMS |
		R_AVATAR_CAP_LEGS | R_AVATAR_CAP_RETARGET | R_AVATAR_CAP_STANDARD_WEAPON,
		1.032662f, R_AVATAR_EQUIPMENT_ATTACH_HAND, { "gun", NULL, NULL, NULL }, {
			A("hip"), V("chest"), A("chest"), V("chest"), A("head"),
			V("upper_arm_L"), A("upper_arm_L"), A("lower_arm_L"), A("hand_L"),
			V("upper_arm_R"), A("upper_arm_R"), A("lower_arm_R"), A("hand_R"),
			A("upper_leg_L"), A("lower_leg_L"), A("foot_L"),
			A("upper_leg"), A("lower_leg"), A("foot"), N, N, N, N },
		R_AVATAR_BASIS_HUMANOID, { NULL, NULL, NULL, NULL }, false, 1.0f, 0.35f, false
	},
	[PLAYER_AVATAR_DOG] = {
		PLAYER_AVATAR_DOG, "dog", "progs/dog.md5mesh",
		R_AVATAR_FAMILY_QUADRUPED, R_AVATAR_CAP_HEAD | R_AVATAR_CAP_ARMS |
		R_AVATAR_CAP_LEGS | R_AVATAR_CAP_RETARGET | R_AVATAR_CAP_STANDARD_WEAPON,
		1.340388376f, R_AVATAR_EQUIPMENT_ATTACH_HAND, { NULL, NULL, NULL, NULL }, {
			A("Hip"), A("Spine1"), A("Spine2"), A("Neck"), A("Head"),
			V("FrontHigh_L"), A("FrontHigh_L"), A("FrontMid_L"), A("FrontFoot_L"),
			V("FrontHigh_R"), A("FrontHigh_R"), A("FrontMid_R"), A("FrontFoot_R"),
			A("RearHigh_L"), A("RearMid_L"), A("RearFoot_L"),
			A("RearHigh_R"), A("RearMid_R"), A("RearFoot_R"), N, N, N, N },
		R_AVATAR_BASIS_FEET_UP_HEAD_FORWARD,
		{ "RearFoot_L", "RearFoot_R", NULL, NULL }, true, 1.0f, 0.35f, false,
		R_AVATAR_POSTURE_UPRIGHT, 60.0f, { 0.0f, 1.0f, 0.0f }, true, 0.0f, true,
		true, 0.38f, 0.25f, { 0.0f, 0.0f, 0.0f }, MD5_VRIK_SPINE1
	},
	[PLAYER_AVATAR_OGRE] = {
		PLAYER_AVATAR_OGRE, "ogre", "progs/ogre.md5mesh",
		R_AVATAR_FAMILY_HUMANOID, R_AVATAR_CAP_HEAD | R_AVATAR_CAP_ARMS |
		R_AVATAR_CAP_LEGS | R_AVATAR_CAP_RETARGET | R_AVATAR_CAP_STANDARD_WEAPON,
		0.906394f, R_AVATAR_EQUIPMENT_ATTACH_HAND, { "Gun", "Chainsaw", NULL, NULL }, {
			A("Hips"), A("Spine1"), A("Spine2"), A("Neck"), A("Head"),
			A("Shoulder_L"), A("UpperArm_L"), A("Forearm_L"), A("Hand_L"),
			A("Shoulder_R"), A("UpperArm_R"), A("Forearm_R"), A("Hand_R"),
			A("Thigh_L"), A("Calf_L"), A("Foot_L"), A("Thigh_R"), A("Calf_R"),
			A("Foot_R"), N, N, N, N },
		R_AVATAR_BASIS_HUMANOID, { NULL, NULL, NULL, NULL }, false, 1.0f, 0.35f, false
		, .desktop_support_hand = true
	},
	[PLAYER_AVATAR_KNIGHT] = {
		PLAYER_AVATAR_KNIGHT, "knight", "progs/knight.md5mesh",
		R_AVATAR_FAMILY_HUMANOID, R_AVATAR_CAP_HEAD | R_AVATAR_CAP_ARMS |
		R_AVATAR_CAP_LEGS | R_AVATAR_CAP_RETARGET | R_AVATAR_CAP_STANDARD_WEAPON,
		1.108073f, R_AVATAR_EQUIPMENT_ATTACH_HAND, { "sword", NULL, NULL, NULL }, {
			A("hip"), V("chest"), A("chest"), V("chest"), A("head"),
			V("upper_arm_L"), A("upper_arm_L"), A("lower_arm_L"), A("hand_L"),
			V("upper_arm_R"), A("upper_arm_R"), A("lower_arm_R"), A("hand_R"),
			A("upper_leg_L"), A("lower_leg_L"), A("foot_L"), A("upper_leg_R"),
			A("lower_leg_R"), A("foot_R"), N, N, N, N },
		R_AVATAR_BASIS_HUMANOID, { NULL, NULL, NULL, NULL }, false, 1.0f, 0.35f, false
	},
	[PLAYER_AVATAR_DEATH_KNIGHT] = {
		PLAYER_AVATAR_DEATH_KNIGHT, "hknight", "progs/hknight.md5mesh",
		R_AVATAR_FAMILY_HUMANOID, R_AVATAR_CAP_HEAD | R_AVATAR_CAP_ARMS |
		R_AVATAR_CAP_LEGS | R_AVATAR_CAP_RETARGET | R_AVATAR_CAP_STANDARD_WEAPON,
		0.910811f, R_AVATAR_EQUIPMENT_ATTACH_HAND, { "sword", NULL, NULL, NULL }, {
			A("hip"), V("chest"), A("chest"), V("chest"), A("head"),
			V("upper_arm_L"), A("upper_arm_L"), A("lower_arm_L"), A("hand_L"),
			V("upper_arm_R"), A("upper_arm_R"), A("lower_arm_R"), A("hand_R"),
			A("lower_leg_L.001"), A("lower_leg_L"), A("foot_L"), A("upper_leg_R"),
			A("lower_leg_R"), A("foot_R"), N, N, N, N },
		R_AVATAR_BASIS_HUMANOID, { NULL, NULL, NULL, NULL }, false, 1.0f, 0.35f, false
	},
	[PLAYER_AVATAR_FIEND] = {
		PLAYER_AVATAR_FIEND, "fiend", "progs/demon.md5mesh",
		R_AVATAR_FAMILY_DIGITIGRADE, R_AVATAR_CAP_HEAD | R_AVATAR_CAP_ARMS |
		R_AVATAR_CAP_LEGS | R_AVATAR_CAP_RETARGET | R_AVATAR_CAP_STANDARD_WEAPON,
		1.168110716f, R_AVATAR_EQUIPMENT_ATTACH_HAND, { NULL, NULL, NULL, NULL }, {
			A("spine_1"), V("spine_1"), A("spine_2"), V("spine_2"), A("head"),
			A("shoulder_L"), A("upper_arm_L"), A("lower_arm_L"), A("hand_L"),
			A("shoulder_R"), A("upper_arm_R"), A("lower_arm_R"), A("hand_R"),
			A("upper_leg_L"), A("lower_leg_L"), A("hoof_L"), A("upper_leg_R"),
			A("lower_leg_R"), A("hoof_R"), N, N, N, N },
		R_AVATAR_BASIS_FEET_UP_HEAD_FORWARD,
		{ "hoof_L", "hoof_R", NULL, NULL }, true, 1.0f, 0.35f, false,
		R_AVATAR_POSTURE_UPRIGHT, 35.0f, { 0.0f, 1.0f, 0.0f }, true, 0.0f, true,
		true, 0.38f, 0.25f, { 0.0f, 0.0f, 0.0f }, MD5_VRIK_SPINE2
	},
	[PLAYER_AVATAR_SHAMBLER] = {
		PLAYER_AVATAR_SHAMBLER, "shambler", "progs/shambler.md5mesh",
		R_AVATAR_FAMILY_HUMANOID, R_AVATAR_CAP_HEAD | R_AVATAR_CAP_ARMS |
		R_AVATAR_CAP_LEGS | R_AVATAR_CAP_RETARGET | R_AVATAR_CAP_STANDARD_WEAPON,
		0.641860309f, R_AVATAR_EQUIPMENT_ATTACH_HAND, { NULL, NULL, NULL, NULL }, {
			A("hip"), V("chest"), A("chest"), V("chest"), A("head"),
			A("shoulder_L"), A("upper_arm_L"), A("lower_arm_L"), A("hand_L"),
			A("shoulder_R"), A("upper_arm_R"), A("lower_arm_R"), A("hand_R"),
			A("upper_leg_L"), A("lower_leg_L"), A("foot_L"), A("upper_leg_R"),
			A("lower_leg_R"), A("foot_R"), N, N, N, N },
		R_AVATAR_BASIS_HUMANOID, { NULL, NULL, NULL, NULL }, false, 0.136f, 0.223f, false,
		R_AVATAR_POSTURE_AUTHORED, 0.0f, { 0.0f, 0.0f, 0.0f }, false, 0.965f, false,
		false, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0, true
	},
	[PLAYER_AVATAR_ZOMBIE] = {
		PLAYER_AVATAR_ZOMBIE, "zombie", "progs/zombie.md5mesh",
		R_AVATAR_FAMILY_HUMANOID, R_AVATAR_CAP_HEAD | R_AVATAR_CAP_ARMS |
		R_AVATAR_CAP_LEGS | R_AVATAR_CAP_RETARGET | R_AVATAR_CAP_STANDARD_WEAPON,
		1.072589807f, R_AVATAR_EQUIPMENT_ATTACH_HAND, { NULL, NULL, NULL, NULL }, {
			A("hip"), V("chest"), A("chest"), V("chest"), A("face"),
			V("upper_arm_L"), A("upper_arm_L"), A("lower_arm_L"), A("hand_L"),
			V("upper_arm_R"), A("upper_arm_R"), A("lower_arm_R"), A("hand_R"),
			A("upper_leg_L"), A("lower_leg_L"), A("foot_L"), A("upper_leg_R"),
			A("lower_leg_R"), A("foot_R"), N, N, N, N },
		R_AVATAR_BASIS_HUMANOID, { NULL, NULL, NULL, NULL }, false, 1.0f, 0.35f, false
	},
	[PLAYER_AVATAR_VORE] = {
		PLAYER_AVATAR_VORE, "vore", "progs/shalrath.md5mesh",
		R_AVATAR_FAMILY_TRIPOD, R_AVATAR_CAP_HEAD | R_AVATAR_CAP_ARMS |
		R_AVATAR_CAP_LEGS | R_AVATAR_CAP_RETARGET | R_AVATAR_CAP_STANDARD_WEAPON,
		1.177174264f, R_AVATAR_EQUIPMENT_ATTACH_HAND, { NULL, NULL, NULL, NULL }, {
			A("hip"), V("chest"), A("chest"), V("chest"), A("head"),
			V("upper_arm_L"), A("upper_arm_L"), A("lower_arm_L"), A("hand_L"),
			V("upper_arm_R"), A("upper_arm_R"), A("lower_arm_R"), A("hand_R"),
			A("leg_1_L"), A("leg_2_L"), A("leg_3_L"),
			A("leg_1_R"), A("leg_2_R"), A("leg_3_R"), N, N, N, N },
		R_AVATAR_BASIS_HUMANOID, { NULL, NULL, NULL, NULL }, false, 1.0f, 0.35f, true,
		R_AVATAR_POSTURE_AUTHORED, 0.0f, { 0.0f, 0.0f, 0.0f }, false, 0.0f, false
	}
};

static qboolean R_AvatarFiniteMatrix (const float *m)
{
	int i;
	for (i = 0; i < 12; ++i)
		if (!isfinite(m[i]))
			return false;
	return true;
}

static void R_AvatarIdentity (float out[12])
{
	memset(out, 0, 12 * sizeof(*out));
	out[0] = out[5] = out[10] = 1.0f;
}

static void R_AvatarMultiply (const float a[12], const float b[12], float out[12])
{
	int r, c;
	float t[12];
	for (r = 0; r < 3; ++r) {
		for (c = 0; c < 3; ++c)
			t[r * 4 + c] = a[r * 4 + 0] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c];
		t[r * 4 + 3] = a[r * 4 + 0] * b[3] + a[r * 4 + 1] * b[7] + a[r * 4 + 2] * b[11] + a[r * 4 + 3];
	}
	memcpy(out, t, sizeof(t));
}

static void R_AvatarInverseRigid (const float in[12], float out[12])
{
	int r, c;
	for (r = 0; r < 3; ++r)
		for (c = 0; c < 3; ++c)
			out[r * 4 + c] = in[c * 4 + r];
	for (r = 0; r < 3; ++r)
		out[r * 4 + 3] = -(out[r * 4] * in[3] + out[r * 4 + 1] * in[7] + out[r * 4 + 2] * in[11]);
}

static qboolean R_AvatarNormalize3 (float v[3])
{
	float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (!isfinite(length) || length < 0.0001f) return false;
	v[0] /= length; v[1] /= length; v[2] /= length;
	return true;
}

static void R_AvatarBindOrigin (const r_avatar_rig_t *rig, int semantic, float out[3])
{
	const float *bind = rig->live->joints[rig->joint[semantic]].bind;
	out[0] = bind[3]; out[1] = bind[7]; out[2] = bind[11];
}

/* Columns are authored forward, left, up.  These are model-space bases,
 * not a guessed world/Quake axis convention. */
static qboolean R_AvatarBuildBindHumanoidBasis (const r_avatar_rig_t *rig, float out[12])
{
	float hip[3], head[3], left[3], right[3], forward[3], up[3], projection;
	int leftsemantic = MD5_VRIK_SHOULDER_L, rightsemantic = MD5_VRIK_SHOULDER_R;
	if (!rig || !rig->valid || !out || rig->joint[MD5_VRIK_HIP] < 0 ||
		rig->joint[MD5_VRIK_HEAD] < 0) return false;
	R_AvatarBindOrigin(rig, MD5_VRIK_HIP, hip);
	R_AvatarBindOrigin(rig, MD5_VRIK_HEAD, head);
	forward[0] = head[0] - hip[0]; forward[1] = head[1] - hip[1]; forward[2] = head[2] - hip[2];
	if (!R_AvatarNormalize3(forward)) return false;
	if (rig->joint[leftsemantic] < 0 || rig->joint[rightsemantic] < 0) return false;
	R_AvatarBindOrigin(rig, leftsemantic, left); R_AvatarBindOrigin(rig, rightsemantic, right);
	left[0] -= right[0]; left[1] -= right[1]; left[2] -= right[2];
	if (!R_AvatarNormalize3(left)) {
		leftsemantic = MD5_VRIK_UPPERARM_L; rightsemantic = MD5_VRIK_UPPERARM_R;
		if (rig->joint[leftsemantic] < 0 || rig->joint[rightsemantic] < 0) return false;
		R_AvatarBindOrigin(rig, leftsemantic, left); R_AvatarBindOrigin(rig, rightsemantic, right);
		left[0] -= right[0]; left[1] -= right[1]; left[2] -= right[2];
		if (!R_AvatarNormalize3(left)) return false;
	}
	projection = forward[0] * left[0] + forward[1] * left[1] + forward[2] * left[2];
	left[0] -= projection * forward[0]; left[1] -= projection * forward[1]; left[2] -= projection * forward[2];
	if (!R_AvatarNormalize3(left)) return false;
	up[0] = forward[1] * left[2] - forward[2] * left[1];
	up[1] = forward[2] * left[0] - forward[0] * left[2];
	up[2] = forward[0] * left[1] - forward[1] * left[0];
	if (!R_AvatarNormalize3(up)) return false;
	R_AvatarIdentity(out);
	out[0] = forward[0]; out[4] = forward[1]; out[8] = forward[2];
	out[1] = left[0]; out[5] = left[1]; out[9] = left[2];
	out[2] = up[0]; out[6] = up[1]; out[10] = up[2];
	return true;
}

/* Use the same [vertical, anatomical left, vertical x left] convention as
 * the humanoid source. The third column is backward, not gaze-forward.
 * Flipping it toward the animal's head also flips anatomical left and makes
 * the presentation face opposite Ranger while swapping its body sides. */
static qboolean R_AvatarBuildBindFeetUpHeadForwardBasis (const r_avatar_rig_t *rig,
	float out[12])
{
	float hip[3], footleft[3], footright[3], up[3], left[3], right[3];
	float forward[3], projection, midpoint[3];
	int leftsemantic = MD5_VRIK_SHOULDER_L, rightsemantic = MD5_VRIK_SHOULDER_R;
	if (!rig || !rig->valid || !out || rig->joint[MD5_VRIK_HIP] < 0 ||
		rig->joint[MD5_VRIK_HEAD] < 0 || rig->joint[MD5_VRIK_FOOT_L] < 0 ||
		rig->joint[MD5_VRIK_FOOT_R] < 0) return false;
	R_AvatarBindOrigin(rig, MD5_VRIK_HIP, hip);
	R_AvatarBindOrigin(rig, MD5_VRIK_FOOT_L, footleft);
	R_AvatarBindOrigin(rig, MD5_VRIK_FOOT_R, footright);
	midpoint[0] = (footleft[0] + footright[0]) * 0.5f;
	midpoint[1] = (footleft[1] + footright[1]) * 0.5f;
	midpoint[2] = (footleft[2] + footright[2]) * 0.5f;
	up[0] = hip[0] - midpoint[0]; up[1] = hip[1] - midpoint[1]; up[2] = hip[2] - midpoint[2];
	if (!R_AvatarNormalize3(up)) return false;
	if (rig->joint[leftsemantic] < 0 || rig->joint[rightsemantic] < 0) return false;
	R_AvatarBindOrigin(rig, leftsemantic, left); R_AvatarBindOrigin(rig, rightsemantic, right);
	left[0] -= right[0]; left[1] -= right[1]; left[2] -= right[2];
	projection = DotProduct(left, up);
	left[0] -= projection * up[0]; left[1] -= projection * up[1]; left[2] -= projection * up[2];
	if (!R_AvatarNormalize3(left)) {
		leftsemantic = MD5_VRIK_UPPERARM_L; rightsemantic = MD5_VRIK_UPPERARM_R;
		if (rig->joint[leftsemantic] < 0 || rig->joint[rightsemantic] < 0) return false;
		R_AvatarBindOrigin(rig, leftsemantic, left); R_AvatarBindOrigin(rig, rightsemantic, right);
		left[0] -= right[0]; left[1] -= right[1]; left[2] -= right[2];
		projection = DotProduct(left, up);
		left[0] -= projection * up[0]; left[1] -= projection * up[1]; left[2] -= projection * up[2];
		if (!R_AvatarNormalize3(left)) return false;
	}
	forward[0] = up[1] * left[2] - up[2] * left[1];
	forward[1] = up[2] * left[0] - up[0] * left[2];
	forward[2] = up[0] * left[1] - up[1] * left[0];
	if (!R_AvatarNormalize3(forward)) return false;
	R_AvatarIdentity(out);
	out[0] = up[0]; out[4] = up[1]; out[8] = up[2];
	out[1] = left[0]; out[5] = left[1]; out[9] = left[2];
	out[2] = forward[0]; out[6] = forward[1]; out[10] = forward[2];
	return true;
}

static qboolean R_AvatarBuildBindBodyBasis (const r_avatar_rig_t *rig, float out[12])
{
	if (rig && rig->profile && rig->profile->basis_policy ==
		R_AVATAR_BASIS_FEET_UP_HEAD_FORWARD)
		return R_AvatarBuildBindFeetUpHeadForwardBasis(rig, out);
	return R_AvatarBuildBindHumanoidBasis(rig, out);
}

static qboolean R_AvatarOrthonormal (const float m[12])
{
	float x[3] = {m[0], m[4], m[8]}, y[3] = {m[1], m[5], m[9]}, z[3] = {m[2], m[6], m[10]};
	float cross[3], xx, yy, zz, xy, xz, yz, determinant;
	if (!R_AvatarFiniteMatrix(m)) return false;
	xx = DotProduct(x, x); yy = DotProduct(y, y); zz = DotProduct(z, z);
	xy = DotProduct(x, y); xz = DotProduct(x, z); yz = DotProduct(y, z);
	cross[0] = x[1] * y[2] - x[2] * y[1];
	cross[1] = x[2] * y[0] - x[0] * y[2];
	cross[2] = x[0] * y[1] - x[1] * y[0];
	determinant = DotProduct(cross, z);
	return fabsf(xx - 1.0f) < 0.02f && fabsf(yy - 1.0f) < 0.02f &&
		fabsf(zz - 1.0f) < 0.02f && fabsf(xy) < 0.02f &&
		fabsf(xz) < 0.02f && fabsf(yz) < 0.02f &&
		fabsf(determinant - 1.0f) < 0.03f;
}

static qboolean R_AvatarDescendant (const md5_skeleton_view_t *live, int child, int ancestor)
{
	int limit = 0;
	while (child >= 0 && child < R_AvatarJointCount(live) && limit++ < R_AvatarJointCount(live)) {
		if (child == ancestor) return true;
		child = live->joints[child].parent;
	}
	return false;
}

static int R_AvatarFindJoint (const md5_skeleton_view_t *live, const char *name)
{
	int i;
	if (!name) return -1;
	for (i = 0; i < R_AvatarJointCount(live); ++i)
		if (!strcmp(live->joints[i].name, name)) return i;
	return -1;
}

const r_avatar_profile_t *R_AvatarProfileForId (int id)
{
	return id >= 0 && id < PLAYER_AVATAR_COUNT ? r_avatar_profiles + id : NULL;
}

const r_avatar_profile_t *R_AvatarProfileForModelPath (const char *path)
{
	int i;
	if (!path) return NULL;
	for (i = 0; i < PLAYER_AVATAR_COUNT; ++i)
		if (!strcmp(r_avatar_profiles[i].model_path, path)) return r_avatar_profiles + i;
	return NULL;
}

static qboolean R_AvatarValidateRig (r_avatar_rig_t *rig)
{
	int i, j, hip = rig->joint[MD5_VRIK_HIP];
	const md5_skeleton_view_t *live = rig->live;
	if (hip < 0 || rig->joint[MD5_VRIK_HEAD] < 0 ||
		!R_AvatarDescendant(live, rig->joint[MD5_VRIK_HEAD], hip)) return false;
	for (i = MD5_VRIK_UPPERARM_L; i <= MD5_VRIK_UPPERARM_R; i += 3) {
		int lower = i + 1, hand = i + 2;
		if (rig->joint[i] < 0 || rig->joint[lower] < 0 || rig->joint[hand] < 0 ||
			!R_AvatarDescendant(live, rig->joint[i], hip) ||
			!R_AvatarDescendant(live, rig->joint[lower], rig->joint[i]) ||
			!R_AvatarDescendant(live, rig->joint[hand], rig->joint[lower])) return false;
	}
	for (i = MD5_VRIK_UPPERLEG_L; i <= MD5_VRIK_UPPERLEG_R; i += 3) {
		int lower = i + 1, foot = i + 2;
		if (rig->joint[i] < 0 || rig->joint[lower] < 0 || rig->joint[foot] < 0 ||
			!R_AvatarDescendant(live, rig->joint[i], hip) ||
			!R_AvatarDescendant(live, rig->joint[lower], rig->joint[i]) ||
			!R_AvatarDescendant(live, rig->joint[foot], rig->joint[lower])) return false;
	}
	for (i = 0; i < R_AvatarJointCount(live); ++i) {
		if (live->joints[i].parent < -1 || live->joints[i].parent >= i ||
			!R_AvatarOrthonormal(live->joints[i].bind)) return false;
	}
	for (i = 0; i < MD5_VRIK_JOINT_COUNT; ++i) for (j = i + 1; j < MD5_VRIK_JOINT_COUNT; ++j)
		if (rig->joint[i] >= 0 && rig->joint[i] == rig->joint[j] &&
			!(rig->virtual_mask & (1u << i)) && !(rig->virtual_mask & (1u << j))) return false;
	return true;
}

qboolean R_AvatarResolveRig (const r_avatar_profile_t *profile,
	const md5_skeleton_view_t *live, r_avatar_rig_t *out)
{
	int i;
	if (!out) return false;
	memset(out, 0, sizeof(*out));
	for (i = 0; i < MD5_VRIK_JOINT_COUNT; ++i) out->joint[i] = -1;
	if (!profile || !live || !live->joints || R_AvatarJointCount(live) < 1 || R_AvatarJointCount(live) > R_AVATAR_MAX_JOINTS) return false;
	out->profile = profile; out->live = live;
	for (i = 0; i < MD5_VRIK_JOINT_COUNT; ++i) {
		out->joint[i] = R_AvatarFindJoint(live, profile->joint[i].name);
		out->canonical_joint[i] = i;
		if (profile->joint[i].flags & R_AVATAR_MAP_VIRTUAL) out->virtual_mask |= 1u << i;
	}
	if (!R_AvatarValidateRig(out)) return false;
	out->valid = true;
	return true;
}

qboolean R_AvatarBuildPresentationContext (const r_avatar_rig_t *source,
	const r_avatar_rig_t *target, r_avatar_presentation_context_t *out)
{
	float sourcebasis[12], targetbasis[12], targetinverse[12], sourcehip[3], targethip[3];
	int r, c;
	if (!source || !target || !out || !source->valid || !target->valid ||
		!R_AvatarBuildBindBodyBasis(source, sourcebasis) ||
		!R_AvatarBuildBindBodyBasis(target, targetbasis)) return false;
	R_AvatarInverseRigid(targetbasis, targetinverse);
	R_AvatarMultiply(sourcebasis, targetinverse, out->rotation);
	out->scale = R_AvatarQuantizedDisplayScale(target->profile);
	if (out->scale <= 0.0f || !R_AvatarOrthonormal(out->rotation)) return false;
	/* Humanoid sourcebasis columns are vertical, left, facing.  Preserve the
	 * actual source axes for target-only posture work; Ranger's authored
	 * facing is not necessarily raw model +X. */
	out->source_semantic_vertical[0] = sourcebasis[0];
	out->source_semantic_vertical[1] = sourcebasis[4];
	out->source_semantic_vertical[2] = sourcebasis[8];
	out->source_semantic_facing[0] = sourcebasis[2];
	out->source_semantic_facing[1] = sourcebasis[6];
	out->source_semantic_facing[2] = sourcebasis[10];
	memcpy(out->forward, out->rotation, sizeof(out->forward));
	for (r = 0; r < 3; ++r) for (c = 0; c < 3; ++c)
		out->forward[r * 4 + c] *= out->scale;
	R_AvatarBindOrigin(source, MD5_VRIK_HIP, sourcehip);
	R_AvatarBindOrigin(target, MD5_VRIK_HIP, targethip);
	for (r = 0; r < 3; ++r)
		out->forward[r * 4 + 3] = sourcehip[r] -
			(out->forward[r * 4] * targethip[0] + out->forward[r * 4 + 1] * targethip[1] + out->forward[r * 4 + 2] * targethip[2]);
	for (r = 0; r < 3; ++r) for (c = 0; c < 3; ++c)
		out->inverse[r * 4 + c] = out->rotation[c * 4 + r] / out->scale;
	for (r = 0; r < 3; ++r)
		out->inverse[r * 4 + 3] = -(out->inverse[r * 4] * out->forward[3] + out->inverse[r * 4 + 1] * out->forward[7] + out->inverse[r * 4 + 2] * out->forward[11]);
	return R_AvatarFiniteMatrix(out->forward) && R_AvatarFiniteMatrix(out->inverse);
}

void R_AvatarPresentationAddCanonicalZ (r_avatar_presentation_context_t *context, float z)
{
	if (!context || !isfinite(z)) return;
	context->forward[11] += z;
	context->inverse[3] -= context->inverse[2] * z;
	context->inverse[7] -= context->inverse[6] * z;
	context->inverse[11] -= context->inverse[10] * z;
}

void R_AvatarPresentationPoint (const r_avatar_presentation_context_t *context,
	const float in[3], float out[3])
{
	float point[3] = {in[0], in[1], in[2]};
	int r; for (r = 0; r < 3; ++r) out[r] = context->forward[r * 4] * point[0] + context->forward[r * 4 + 1] * point[1] + context->forward[r * 4 + 2] * point[2] + context->forward[r * 4 + 3];
}

void R_AvatarPresentationInversePoint (const r_avatar_presentation_context_t *context,
	const float in[3], float out[3])
{
	float point[3] = {in[0], in[1], in[2]};
	int r; for (r = 0; r < 3; ++r) out[r] = context->inverse[r * 4] * point[0] + context->inverse[r * 4 + 1] * point[1] + context->inverse[r * 4 + 2] * point[2] + context->inverse[r * 4 + 3];
}

qboolean R_AvatarBuildAttachedPropTransform (
	const r_avatar_presentation_context_t *context,
	const float source_hand_pose[12], const float source_hand_bind[12],
	const float target_hand_pose[12], const float target_hand_bind[12],
	const float source_prop_pose[12], float out[12])
{
	float target_hand_canonical[12], target_bind_canonical[12];
	float source_bind_rotation[12], inverse[12], correction[12];
	float attach[12], result[12];
	float hand_origin[3], canonical_origin[3];

	if (!context || !source_hand_pose || !source_hand_bind ||
		!target_hand_pose || !target_hand_bind || !source_prop_pose || !out ||
		!R_AvatarOrthonormal(context->rotation) ||
		!R_AvatarFiniteMatrix(context->forward) ||
		!R_AvatarOrthonormal(source_hand_pose) ||
		!R_AvatarOrthonormal(source_hand_bind) ||
		!R_AvatarOrthonormal(target_hand_pose) ||
		!R_AvatarOrthonormal(target_hand_bind) ||
		!R_AvatarOrthonormal(source_prop_pose))
		return false;
	/* Match the inherited socket: presentation maps only the hand origin.
	 * The target/source bind rotations calibrate the two authored grips. */
	R_AvatarMultiply(context->rotation, target_hand_pose,
		target_hand_canonical);
	hand_origin[0] = target_hand_pose[3];
	hand_origin[1] = target_hand_pose[7];
	hand_origin[2] = target_hand_pose[11];
	R_AvatarPresentationPoint(context, hand_origin, canonical_origin);
	target_hand_canonical[3] = canonical_origin[0];
	target_hand_canonical[7] = canonical_origin[1];
	target_hand_canonical[11] = canonical_origin[2];
	R_AvatarMultiply(context->rotation, target_hand_bind,
		target_bind_canonical);
	target_bind_canonical[3] = target_bind_canonical[7] = target_bind_canonical[11] = 0;
	memcpy(source_bind_rotation, source_hand_bind, sizeof(source_bind_rotation));
	source_bind_rotation[3] = source_bind_rotation[7] = source_bind_rotation[11] = 0;
	R_AvatarInverseRigid(target_bind_canonical, inverse);
	R_AvatarMultiply(inverse, source_bind_rotation, correction);
	R_AvatarMultiply(target_hand_canonical, correction, target_hand_canonical);
	R_AvatarInverseRigid(source_hand_pose, inverse);
	R_AvatarMultiply(target_hand_canonical, inverse, attach);
	R_AvatarMultiply(attach, source_prop_pose, result);
	if (!R_AvatarOrthonormal(result))
		return false;
	memcpy(out, result, sizeof(result));
	return true;
}

qboolean R_AvatarRetargetPaletteWithContext (const r_avatar_rig_t *source,
	const r_avatar_rig_t *target, const r_avatar_presentation_context_t *context,
	const float *source_palette, float *target_palette)
{
	int i, semantic, owner[R_AVATAR_MAX_JOINTS];
	float local[12], inv[12], desired[12], delta[12], mapped[12];
	if (!source || !target || !context || !source->valid || !target->valid || !source_palette || !target_palette ||
		R_AvatarJointCount(source->live) > R_AVATAR_MAX_JOINTS || R_AvatarJointCount(target->live) > R_AVATAR_MAX_JOINTS) return false;
	for (i = 0; i < R_AvatarJointCount(source->live); ++i) if (!R_AvatarOrthonormal(source_palette + i * 12)) return false;
	for (i = 0; i < R_AVATAR_MAX_JOINTS; ++i) owner[i] = -1;
	for (semantic = 0; semantic < MD5_VRIK_JOINT_COUNT; ++semantic) {
		int joint = target->joint[semantic];
		if (joint >= 0 && !(target->virtual_mask & (1u << semantic))) owner[joint] = semantic;
	}
	for (i = 0; i < R_AvatarJointCount(target->live); ++i) {
		int parent = target->live->joints[i].parent;
		semantic = owner[i];
		if (parent < 0) memcpy(local, target->live->joints[i].bind, sizeof(local));
		else { R_AvatarInverseRigid(target->live->joints[parent].bind, inv); R_AvatarMultiply(inv, target->live->joints[i].bind, local); }
		if (semantic >= 0 && source->joint[semantic] >= 0 && !(source->virtual_mask & (1u << semantic))) {
			int sj = source->joint[semantic];
			/* Absolute global transport: rotate the canonical global animation
			 * delta through the presentation body's rigid basis, but map origins
			 * solely through L^-1 so non-unit display scale never contaminates a
			 * bone rotation. */
			R_AvatarInverseRigid(source->live->joints[sj].bind, inv);
			R_AvatarMultiply(source_palette + sj * 12, inv, delta);
			delta[3] = delta[7] = delta[11] = 0;
			R_AvatarMultiply(delta, context->rotation, mapped);
			R_AvatarInverseRigid(context->rotation, inv);
			R_AvatarMultiply(inv, mapped, delta);
			R_AvatarMultiply(delta, target->live->joints[i].bind, desired);
			/* Profiles with bind-relative unmapped children keep their authored
			 * Hip orientation so those children remain in their intended plane. */
			if (semantic == MD5_VRIK_HIP && target->profile->preserve_hip_rotation)
			{
				desired[0] = target->live->joints[i].bind[0];
				desired[1] = target->live->joints[i].bind[1];
				desired[2] = target->live->joints[i].bind[2];
				desired[4] = target->live->joints[i].bind[4];
				desired[5] = target->live->joints[i].bind[5];
				desired[6] = target->live->joints[i].bind[6];
				desired[8] = target->live->joints[i].bind[8];
				desired[9] = target->live->joints[i].bind[9];
				desired[10] = target->live->joints[i].bind[10];
			}
			desired[3] = target->live->joints[i].bind[3] +
				(context->inverse[0] * (source_palette[sj * 12 + 3] - source->live->joints[sj].bind[3]) + context->inverse[1] * (source_palette[sj * 12 + 7] - source->live->joints[sj].bind[7]) + context->inverse[2] * (source_palette[sj * 12 + 11] - source->live->joints[sj].bind[11]));
			desired[7] = target->live->joints[i].bind[7] +
				(context->inverse[4] * (source_palette[sj * 12 + 3] - source->live->joints[sj].bind[3]) + context->inverse[5] * (source_palette[sj * 12 + 7] - source->live->joints[sj].bind[7]) + context->inverse[6] * (source_palette[sj * 12 + 11] - source->live->joints[sj].bind[11]));
			desired[11] = target->live->joints[i].bind[11] +
				(context->inverse[8] * (source_palette[sj * 12 + 3] - source->live->joints[sj].bind[3]) + context->inverse[9] * (source_palette[sj * 12 + 7] - source->live->joints[sj].bind[7]) + context->inverse[10] * (source_palette[sj * 12 + 11] - source->live->joints[sj].bind[11]));
			if (parent < 0) memcpy(local, desired, sizeof(local));
			else { R_AvatarInverseRigid(target_palette + parent * 12, inv); R_AvatarMultiply(inv, desired, local); }
		}
		if (parent < 0) memcpy(target_palette + i * 12, local, sizeof(local));
		else R_AvatarMultiply(target_palette + parent * 12, local, target_palette + i * 12);
		if (!R_AvatarOrthonormal(target_palette + i * 12)) return false;
	}
	return true;
}

qboolean R_AvatarRetargetPalette (const r_avatar_rig_t *source,
	const r_avatar_rig_t *target, const float *source_palette, float *target_palette)
{
	r_avatar_presentation_context_t context;
	return R_AvatarBuildPresentationContext(source, target, &context) &&
		R_AvatarRetargetPaletteWithContext(source, target, &context,
			source_palette, target_palette);
}

qboolean R_AvatarRetargetRangerOutput (const r_avatar_rig_t *source,
	const r_avatar_rig_t *target, const r_vrik_palette_output_t *ranger,
	float (*target_palette)[12], size_t target_capacity)
{
	if (!source || !target || !source->live || !target->live || !ranger ||
		!ranger->matrices || !target_palette || !source->valid || !target->valid ||
		source->live->joint_count > R_AVATAR_MAX_JOINTS ||
		target->live->joint_count > R_AVATAR_MAX_JOINTS ||
		ranger->joint_count != source->live->joint_count ||
		ranger->capacity < ranger->joint_count ||
		target_capacity < target->live->joint_count)
		return false;
	return R_AvatarRetargetPalette(source, target,
		(const float *)ranger->matrices, (float *)target_palette);
}

static void R_AvatarRotationOnly(float m[12])
{
	m[3] = m[7] = m[11] = 0;
}

/* Derive roll from a common body frame, not exporter-specific bone axes.
 * +X follows the semantic segment. The body-facing guide defines +Y after
 * projection, with anatomical left as the deterministic singular fallback. */
static qboolean R_AvatarAnatomicalFrame(const r_avatar_rig_t *rig, int semantic,
	float out[12])
{
	static const int next[19] = {1,2,3,4,-1,6,7,8,-1,10,11,12,-1,14,15,-1,17,18,-1};
	static const int previous[19] = {-1,0,1,2,3,2,5,6,7,2,9,10,11,0,13,14,0,16,17};
	float body[12], x[3], y[3], z[3], projection;
	int a = semantic, b = next[semantic], r, attempt;
	if (!R_AvatarBuildBindBodyBasis(rig, body)) return false;
	if (b < 0) { a = previous[semantic]; b = semantic; }
	for (r = 0; r < 3; ++r)
		x[r] = rig->live->joints[rig->joint[b]].bind[r*4+3] -
			rig->live->joints[rig->joint[a]].bind[r*4+3];
	/* Feet have no required toe semantic; use anatomical facing, not shin. */
	if (semantic == MD5_VRIK_FOOT_L || semantic == MD5_VRIK_FOOT_R)
		for (r = 0; r < 3; ++r) x[r] = -body[r*4+2];
	if (!R_AvatarNormalize3(x)) return false;
	for (attempt = 0; attempt < 3; ++attempt) {
		int column = attempt == 0 ? 2 : attempt == 1 ? 1 : 0;
		for (r = 0; r < 3; ++r) y[r] = body[r*4+column];
		projection = x[0]*y[0]+x[1]*y[1]+x[2]*y[2];
		for (r = 0; r < 3; ++r) y[r] -= projection*x[r];
		if (R_AvatarNormalize3(y)) break;
	}
	if (attempt == 3) return false;
	z[0]=x[1]*y[2]-x[2]*y[1]; z[1]=x[2]*y[0]-x[0]*y[2]; z[2]=x[0]*y[1]-x[1]*y[0];
	R_AvatarIdentity(out);
	for (r=0;r<3;++r) {out[r*4]=x[r];out[r*4+1]=y[r];out[r*4+2]=z[r];}
	return R_AvatarOrthonormal(out);
}

static float R_AvatarJointDistance(const float *a, const float *b)
{
	float sum=0; int r;
	for(r=3;r<12;r+=4)sum+=(a[r]-b[r])*(a[r]-b[r]);
	return sqrtf(sum);
}

float R_AvatarHumanoidHeight(const r_avatar_rig_t *rig)
{
	float body[12],height=0;int r;
	if(!rig || !rig->valid || rig->joint[MD5_VRIK_HEAD]<0 ||
		rig->joint[MD5_VRIK_FOOT_L]<0 || rig->joint[MD5_VRIK_FOOT_R]<0 ||
		!R_AvatarBuildBindBodyBasis(rig,body))return 0;
	for(r=0;r<3;++r)height+=body[r*4]*(rig->live->joints[rig->joint[MD5_VRIK_HEAD]].bind[r*4+3] -
		.5f*(rig->live->joints[rig->joint[MD5_VRIK_FOOT_L]].bind[r*4+3]+rig->live->joints[rig->joint[MD5_VRIK_FOOT_R]].bind[r*4+3]));
	return isfinite(height) && height>.001f ? height : 0;
}

qboolean R_AvatarBuildHumanoid(const r_avatar_rig_t *source,
	const r_avatar_rig_t *target, const r_avatar_presentation_context_t *context,
	r_avatar_humanoid_t *out)
{
	r_avatar_humanoid_t result;
	float sf[12],tf[12],inv[12],tmp[12],sb[12],tb[12],rotation_inverse[12];
	float sourceleg=0,targetleg=0; int i,j;
	if(!source || !target || !source->valid || !target->valid || !context || !out ||
		source->profile->family != R_AVATAR_FAMILY_HUMANOID ||
		target->profile->family != R_AVATAR_FAMILY_HUMANOID) return false;
	/* This first policy requires unambiguous full humanoids. Legacy virtual
	 * monster mappings retain their existing family-specific implementation. */
	for(i=0;i<19;++i) {
		if(source->joint[i]<0 || target->joint[i]<0 ||
			(source->virtual_mask & (1u<<i)) || (target->virtual_mask & (1u<<i)))return false;
		for(j=0;j<i;++j)if(source->joint[i]==source->joint[j] || target->joint[i]==target->joint[j])return false;
	}
	R_AvatarInverseRigid(context->rotation,rotation_inverse);
	for(i=0;i<19;++i) {
		if(!R_AvatarAnatomicalFrame(source,i,sf) || !R_AvatarAnatomicalFrame(target,i,tf))return false;
		memcpy(sb,source->live->joints[source->joint[i]].bind,sizeof(sb));R_AvatarRotationOnly(sb);
		memcpy(tb,target->live->joints[target->joint[i]].bind,sizeof(tb));R_AvatarRotationOnly(tb);
		R_AvatarInverseRigid(sb,inv);R_AvatarMultiply(inv,sf,tmp);
		R_AvatarInverseRigid(tf,inv);R_AvatarMultiply(tmp,inv,result.offset[i]);
		R_AvatarMultiply(result.offset[i],tb,result.offset[i]);
		R_AvatarMultiply(sb,result.offset[i],tmp);
		R_AvatarMultiply(rotation_inverse,tmp,result.reference[i]);
		if(!R_AvatarOrthonormal(result.reference[i]))return false;
	}
	for(i=13;i<18;++i)if(i!=15) {
		sourceleg+=R_AvatarJointDistance(source->live->joints[source->joint[i]].bind,source->live->joints[source->joint[i+1]].bind);
		targetleg+=R_AvatarJointDistance(target->live->joints[target->joint[i]].bind,target->live->joints[target->joint[i+1]].bind);
	}
	if(sourceleg<.001f || targetleg<.001f)return false;
	result.motion_scale=targetleg/sourceleg;
	*out=result;
	return true;
}

qboolean R_AvatarRetargetHumanoid(const r_avatar_rig_t *source,
	const r_avatar_rig_t *target, const r_avatar_presentation_context_t *context,
	const r_avatar_humanoid_t *map, const float *source_palette, float *out)
{
	float result[R_AVATAR_MAX_JOINTS*12],inv[12],local[12],desired[12],rotation_inverse[12];
	int i,s,owner[R_AVATAR_MAX_JOINTS];
	if(!source || !target || !source->valid || !target->valid || !context || !map || !source_palette || !out ||
		R_AvatarJointCount(source->live)>R_AVATAR_MAX_JOINTS || R_AvatarJointCount(target->live)>R_AVATAR_MAX_JOINTS ||
		!isfinite(map->motion_scale) || map->motion_scale<=0)return false;
	for(i=0;i<R_AvatarJointCount(source->live);++i)if(!R_AvatarOrthonormal(source_palette+i*12))return false;
	for(i=0;i<R_AVATAR_MAX_JOINTS;++i)owner[i]=-1;
	for(s=0;s<19;++s) {
		if(target->joint[s]<0 || target->joint[s]>=R_AvatarJointCount(target->live) ||
			source->joint[s]<0 || source->joint[s]>=R_AvatarJointCount(source->live) ||
			((source->virtual_mask|target->virtual_mask)&(1u<<s)) ||
			!R_AvatarOrthonormal(map->offset[s]))return false;
		owner[target->joint[s]]=s;
	}
	R_AvatarInverseRigid(context->rotation,rotation_inverse);
	for(i=0;i<R_AvatarJointCount(target->live);++i) {
		int parent=target->live->joints[i].parent;
		if(parent<0)memcpy(local,target->live->joints[i].bind,sizeof(local));
		else {R_AvatarInverseRigid(target->live->joints[parent].bind,inv);R_AvatarMultiply(inv,target->live->joints[i].bind,local);}
		if(parent<0)memcpy(desired,local,sizeof(desired));
		else R_AvatarMultiply(result+parent*12,local,desired);
		s=owner[i];
		if(s>=0) {
			float pose[12],mapped[12];int r,c;
			memcpy(pose,source_palette+source->joint[s]*12,sizeof(pose));R_AvatarRotationOnly(pose);
			R_AvatarMultiply(pose,map->offset[s],mapped);R_AvatarMultiply(rotation_inverse,mapped,pose);
			for(r=0;r<3;++r)for(c=0;c<3;++c)desired[r*4+c]=pose[r*4+c];
			if(s==MD5_VRIK_HIP) {
				float d[3];
				for(r=0;r<3;++r)d[r]=source_palette[source->joint[s]*12+r*4+3]-source->live->joints[source->joint[s]].bind[r*4+3];
				for(r=0;r<3;++r)desired[r*4+3]=target->live->joints[i].bind[r*4+3]+map->motion_scale*(rotation_inverse[r*4]*d[0]+rotation_inverse[r*4+1]*d[1]+rotation_inverse[r*4+2]*d[2]);
			}
		}
		if(!R_AvatarOrthonormal(desired))return false;
		memcpy(result+i*12,desired,sizeof(desired));
	}
	memcpy(out,result,R_AvatarJointCount(target->live)*12*sizeof(float));return true;
}

qboolean R_AvatarHumanoidDesktopSupportEndpoint (
	const r_avatar_rig_t *source, const r_avatar_rig_t *target,
	const r_avatar_presentation_context_t *context,
	const r_avatar_humanoid_t *map, const float *source_palette,
	const float *target_palette, float endpoint[12])
{
	float identity[12], attach[12], mapped[12], calibrated[12], inverse[12];
	float canonical_origin[3], target_origin[3];
	int source_right, source_left, target_right;
	if (!source || !target || !context || !map || !source_palette ||
		!target_palette || !endpoint || !source->valid || !target->valid)
		return false;
	source_right = source->joint[MD5_VRIK_HAND_R];
	source_left = source->joint[MD5_VRIK_HAND_L];
	target_right = target->joint[MD5_VRIK_HAND_R];
	if (source_right < 0 || source_left < 0 || target_right < 0 ||
		!R_AvatarOrthonormal(map->offset[MD5_VRIK_HAND_L]) ||
		!R_AvatarOrthonormal(map->reference[MD5_VRIK_HAND_R]))
		return false;
	R_AvatarIdentity (identity);
	if (!R_AvatarBuildAttachedPropTransform (context,
		source_palette + source_right * 12,
		source->live->joints[source_right].bind,
		target_palette + target_right * 12,
		map->reference[MD5_VRIK_HAND_R], identity, attach))
		return false;
	R_AvatarMultiply (attach, source_palette + source_left * 12, mapped);
	canonical_origin[0] = mapped[3];
	canonical_origin[1] = mapped[7];
	canonical_origin[2] = mapped[11];
	R_AvatarPresentationInversePoint (context, canonical_origin, target_origin);
	mapped[3] = mapped[7] = mapped[11] = 0.0f;
	R_AvatarMultiply (mapped, map->offset[MD5_VRIK_HAND_L], calibrated);
	R_AvatarInverseRigid (context->rotation, inverse);
	R_AvatarMultiply (inverse, calibrated, endpoint);
	endpoint[3] = target_origin[0];
	endpoint[7] = target_origin[1];
	endpoint[11] = target_origin[2];
	return R_AvatarOrthonormal (endpoint);
}

/* Rotate a whole physical subtree about a joint, never translate an endpoint
 * independently. The opposite-vector case gets a deterministic rotation axis. */
static qboolean R_AvatarAimBranch(const r_avatar_rig_t *rig,float *palette,int root,
	const float from[3],const float to[3])
{
	float a[3],b[3],axis[3],m[12],dot,sine;int i,r,c;
	memcpy(a,from,sizeof(a));memcpy(b,to,sizeof(b));
	if(!R_AvatarNormalize3(a)||!R_AvatarNormalize3(b))return false;
	dot=fmaxf(-1,fminf(1,a[0]*b[0]+a[1]*b[1]+a[2]*b[2]));
	axis[0]=a[1]*b[2]-a[2]*b[1];axis[1]=a[2]*b[0]-a[0]*b[2];axis[2]=a[0]*b[1]-a[1]*b[0];
	sine=sqrtf(fmaxf(0,1-dot*dot));
	if(!R_AvatarNormalize3(axis)) {
		if(dot>0)return true;
		i=fabsf(a[0])<fabsf(a[1])?0:1;if(fabsf(a[2])<fabsf(a[i]))i=2;
		for(r=0;r<3;++r)axis[r]=(r==i)-a[r]*a[i];
		if(!R_AvatarNormalize3(axis))return false;
	}
	R_AvatarIdentity(m);
	for(r=0;r<3;++r)for(c=0;c<3;++c)m[r*4+c]=(r==c?dot:0)+(1-dot)*axis[r]*axis[c];
	m[1]-=sine*axis[2];m[2]+=sine*axis[1];m[4]+=sine*axis[2];m[6]-=sine*axis[0];m[8]-=sine*axis[1];m[9]+=sine*axis[0];
	for(r=0;r<3;++r)m[r*4+3]=palette[root*12+r*4+3]-(m[r*4]*palette[root*12+3]+m[r*4+1]*palette[root*12+7]+m[r*4+2]*palette[root*12+11]);
	for(i=root;i<R_AvatarJointCount(rig->live);++i) {
		int ancestor=i;while(ancestor>=0 && ancestor!=root)ancestor=rig->live->joints[ancestor].parent;
		if(ancestor==root)R_AvatarMultiply(m,palette+i*12,palette+i*12);
	}
	return true;
}

float R_AvatarSolveHumanoidLimb(const r_avatar_rig_t *rig,float *palette,
	int upper_semantic,const float endpoint[12],const float pole[3])
{
	float saved[R_AVATAR_MAX_JOINTS*12],a[3],b[3],tip[3],direction[3],bend[3],from[3],to[3];
	float l1,l2,d,raw,projection,along,height;int upper,lower,end,r,i;
	if(!rig || !rig->valid || !palette || !endpoint || !pole || !R_AvatarOrthonormal(endpoint))return -1;
	if(R_AvatarJointCount(rig->live)>R_AVATAR_MAX_JOINTS)return -1;
	for(i=0;i<R_AvatarJointCount(rig->live);++i)if(!R_AvatarOrthonormal(palette+i*12))return -1;
	if(upper_semantic!=6 && upper_semantic!=10 && upper_semantic!=13 && upper_semantic!=16)return -1;
	upper=rig->joint[upper_semantic];lower=rig->joint[upper_semantic+1];end=rig->joint[upper_semantic+2];
	if(upper<0 || lower<0 || end<0)return -1;
	l1=R_AvatarJointDistance(palette+upper*12,palette+lower*12);
	l2=R_AvatarJointDistance(palette+lower*12,palette+end*12);
	if(l1<.001f || l2<.001f)return -1;
	for(r=0;r<3;++r){a[r]=palette[upper*12+r*4+3];b[r]=palette[lower*12+r*4+3];direction[r]=endpoint[r*4+3]-a[r];bend[r]=pole[r]-a[r];if(!isfinite(bend[r]))return -1;}
	raw=sqrtf(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);
	if(!R_AvatarNormalize3(direction))return -1;
	d=fmaxf(fabsf(l1-l2)+.0001f,fminf(l1+l2-.0001f,raw));
	projection=bend[0]*direction[0]+bend[1]*direction[1]+bend[2]*direction[2];
	for(r=0;r<3;++r)bend[r]-=projection*direction[r];
	if(!R_AvatarNormalize3(bend)) {
		int axis=fabsf(direction[0])<fabsf(direction[1])?0:1;if(fabsf(direction[2])<fabsf(direction[axis]))axis=2;
		for(r=0;r<3;++r)bend[r]=(r==axis)-direction[r]*direction[axis];
		if(!R_AvatarNormalize3(bend))return -1;
	}
	along=(l1*l1-l2*l2+d*d)/(2*d);height=sqrtf(fmaxf(0,l1*l1-along*along));
	for(r=0;r<3;++r){from[r]=b[r]-a[r];to[r]=direction[r]*along+bend[r]*height;tip[r]=a[r]+direction[r]*d;}
	memcpy(saved,palette,R_AvatarJointCount(rig->live)*12*sizeof(float));
	if(!R_AvatarAimBranch(rig,palette,upper,from,to))goto fail;
	for(r=0;r<3;++r){from[r]=palette[end*12+r*4+3]-palette[lower*12+r*4+3];to[r]=tip[r]-palette[lower*12+r*4+3];}
	if(!R_AvatarAimBranch(rig,palette,lower,from,to))goto fail;
	/* Apply endpoint orientation to its descendants as well (fingers/toes). */
	{float inv[12],desired[12],delta[12];int i;
	 memcpy(desired,endpoint,sizeof(desired));for(r=3;r<12;r+=4)desired[r]=palette[end*12+r];
	 R_AvatarInverseRigid(palette+end*12,inv);R_AvatarMultiply(desired,inv,delta);
	 for(i=end;i<R_AvatarJointCount(rig->live);++i){int p=i;while(p>=0 && p!=end)p=rig->live->joints[p].parent;if(p==end)R_AvatarMultiply(delta,palette+i*12,palette+i*12);}
	}
	return R_AvatarJointDistance(palette+end*12,endpoint);
fail:
	memcpy(palette,saved,R_AvatarJointCount(rig->live)*12*sizeof(float));return -1;
}

/* The desktop animal torso is bind-local under the animated Hip. Ranger's
 * upper-spine twists are not a useful animation for Dog or Fiend anatomy. */
static qboolean R_AvatarStabilizeDesktopUpperBody(const r_avatar_rig_t *rig,
	float *palette)
{
	int hip=rig->joint[MD5_VRIK_HIP], semantic=rig->profile->desktop_upperbody_bind_root;
	int root, joint;
	float inverse[12], local[12];
	if(semantic<=0)return true;
	if(semantic>=MD5_VRIK_JOINT_COUNT || hip<0)return false;
	root=rig->joint[semantic];
	if(root<0)return true;
	if(!R_AvatarDescendant(rig->live,root,hip))return false;
	R_AvatarInverseRigid(rig->live->joints[hip].bind,inverse);
	for(joint=0;joint<R_AvatarJointCount(rig->live);++joint)
		if(R_AvatarDescendant(rig->live,joint,root)) {
			R_AvatarMultiply(inverse,rig->live->joints[joint].bind,local);
			R_AvatarMultiply(palette+hip*12,local,palette+joint*12);
		}
	return true;
}

/* Master caps the Hip-descendant posture turn for the named animal profiles.
 * Refuse the ambiguous partial-antipode instead of choosing an arbitrary axis. */
static qboolean R_AvatarLimitedTurn(const float from[3],const float to[3],
	float degrees,float turn[12])
{
	float a[3],b[3],axis[3],cosine,sine,angle,limit,t;
	int r,c;
	memcpy(a,from,sizeof(a));memcpy(b,to,sizeof(b));
	if(!R_AvatarNormalize3(a)||!R_AvatarNormalize3(b)||
		!isfinite(degrees)||degrees<=0)return false;
	cosine=fmaxf(-1.0f,fminf(1.0f,DotProduct(a,b)));
	axis[0]=a[1]*b[2]-a[2]*b[1];
	axis[1]=a[2]*b[0]-a[0]*b[2];
	axis[2]=a[0]*b[1]-a[1]*b[0];
	sine=sqrtf(DotProduct(axis,axis));
	angle=atan2f(sine,cosine);
	limit=degrees*(float)M_PI/180.0f;
	if(angle>limit && sine<0.0001f)return false;
	if(sine<0.000001f) {
		if(cosine<0)return false;
		R_AvatarIdentity(turn);return true;
	}
	for(r=0;r<3;++r)axis[r]/=sine;
	t=fminf(angle,limit);cosine=cosf(t);sine=sinf(t);
	R_AvatarIdentity(turn);
	for(r=0;r<3;++r)for(c=0;c<3;++c)
		turn[r*4+c]=(r==c?cosine:0)+(1-cosine)*axis[r]*axis[c];
	turn[1]-=sine*axis[2];turn[2]+=sine*axis[1];
	turn[4]+=sine*axis[2];turn[6]-=sine*axis[0];
	turn[8]-=sine*axis[1];turn[9]+=sine*axis[0];
	return R_AvatarOrthonormal(turn);
}

static qboolean R_AvatarApplyDesktopUprightPosture(const r_avatar_rig_t *rig,
	const r_avatar_presentation_context_t *context,float *palette)
{
	const r_avatar_profile_t *profile=rig->profile;
	float from[3],to[3],hiporigin[3],turn[12],old[12],rotated[12];
	int hip=rig->joint[MD5_VRIK_HIP],head=rig->joint[MD5_VRIK_HEAD],joint,r;
	if(profile->posture_policy!=R_AVATAR_POSTURE_UPRIGHT)return true;
	if(hip<0||head<0||!R_AvatarDescendant(rig->live,head,hip))return false;
	for(r=0;r<3;++r) {
		hiporigin[r]=palette[hip*12+r*4+3];
		from[r]=palette[head*12+r*4+3]-hiporigin[r];
		to[r]=context->inverse[r*4]*context->source_semantic_vertical[0]+
			context->inverse[r*4+1]*context->source_semantic_vertical[1]+
			context->inverse[r*4+2]*context->source_semantic_vertical[2];
	}
	if(!R_AvatarLimitedTurn(from,to,profile->posture_degrees,turn))return false;
	for(joint=0;joint<R_AvatarJointCount(rig->live);++joint)
		if(joint!=hip && R_AvatarDescendant(rig->live,joint,hip)) {
			memcpy(old,palette+joint*12,sizeof(old));
			R_AvatarMultiply(turn,old,rotated);
			for(r=0;r<3;++r)
				rotated[r*4+3]=hiporigin[r]+
				turn[r*4]*(old[3]-hiporigin[0])+
				turn[r*4+1]*(old[7]-hiporigin[1])+
				turn[r*4+2]*(old[11]-hiporigin[2]);
			memcpy(palette+joint*12,rotated,sizeof(rotated));
		}
	return true;
}

/* Reattach the original arm-local rotation and authored link translation to
 * the corrected torso. This includes Dog's unmapped foreleg link. */
static qboolean R_AvatarRebuildDesktopAnimalArms(const r_avatar_rig_t *rig,
	const float *animated,float *palette)
{
	int roots[2],hands[2],side,joint,parent;
	float inverse[12],local[12],bindlocal[12];
	for(side=0;side<2;++side) {
		int shoulder=side?MD5_VRIK_SHOULDER_R:MD5_VRIK_SHOULDER_L;
		int upper=side?MD5_VRIK_UPPERARM_R:MD5_VRIK_UPPERARM_L;
		roots[side]=rig->joint[(rig->virtual_mask&(1u<<shoulder))?upper:shoulder];
		hands[side]=rig->joint[side?MD5_VRIK_HAND_R:MD5_VRIK_HAND_L];
		if(roots[side]<0||hands[side]<0||
			!R_AvatarDescendant(rig->live,hands[side],roots[side]))return false;
	}
	if(R_AvatarDescendant(rig->live,roots[0],roots[1])||
		R_AvatarDescendant(rig->live,roots[1],roots[0]))return false;
	for(joint=0;joint<R_AvatarJointCount(rig->live);++joint)
		if(R_AvatarDescendant(rig->live,joint,roots[0])||
			R_AvatarDescendant(rig->live,joint,roots[1])) {
			parent=rig->live->joints[joint].parent;
			if(parent<0||parent>=joint)return false;
			R_AvatarInverseRigid(animated+parent*12,inverse);
			R_AvatarMultiply(inverse,animated+joint*12,local);
			R_AvatarInverseRigid(rig->live->joints[parent].bind,inverse);
			R_AvatarMultiply(inverse,rig->live->joints[joint].bind,bindlocal);
			local[3]=bindlocal[3];local[7]=bindlocal[7];local[11]=bindlocal[11];
			R_AvatarMultiply(palette+parent*12,local,palette+joint*12);
			if(joint==hands[0]||joint==hands[1]) {
				int r;for(r=0;r<3;++r)
					memcpy(palette+joint*12+r*4,animated+joint*12+r*4,3*sizeof(float));
			}
		}
	return true;
}

/* Vore's paired outer legs can inherit an inward knee despite unchanged foot
 * contacts. Reuse the existing bounded limb solve only for direct chains. */
static qboolean R_AvatarRepairInwardOuterLegs(const r_avatar_rig_t *rig,float *palette)
{
	float lateral[3],roots[2][3],pole[3],toward[3],bend[3],endpoint[12];
	int side,r;
	for(side=0;side<2;++side) {
		int upper=rig->joint[side?MD5_VRIK_UPPERLEG_R:MD5_VRIK_UPPERLEG_L];
		for(r=0;r<3;++r)roots[side][r]=palette[upper*12+r*4+3];
	}
	for(r=0;r<3;++r)lateral[r]=roots[1][r]-roots[0][r];
	if(!R_AvatarNormalize3(lateral))return true;
	for(side=0;side<2;++side) {
		int semantic=side?MD5_VRIK_UPPERLEG_R:MD5_VRIK_UPPERLEG_L;
		int upper=rig->joint[semantic],lower=rig->joint[semantic+1],foot=rig->joint[semantic+2];
		float outward,residual,scale;
		if(rig->live->joints[lower].parent!=upper||
			rig->live->joints[foot].parent!=lower)continue;
		for(r=0;r<3;++r) {
			toward[r]=palette[foot*12+r*4+3]-roots[side][r];
			bend[r]=palette[lower*12+r*4+3]-roots[side][r];
		}
		if(!R_AvatarNormalize3(toward))continue;
		scale=DotProduct(bend,toward);
		for(r=0;r<3;++r)bend[r]-=scale*toward[r];
		outward=DotProduct(bend,lateral)*(side?1.0f:-1.0f);
		if(!isfinite(outward))return false;
		if(outward>=-0.0001f)continue;
		scale=R_AvatarJointDistance(palette+upper*12,palette+lower*12);
		for(r=0;r<3;++r)pole[r]=roots[side][r]+(side?1.0f:-1.0f)*lateral[r]*scale;
		memcpy(endpoint,palette+foot*12,sizeof(endpoint));
		residual=R_AvatarSolveHumanoidLimb(rig,palette,semantic,endpoint,pole);
		if(!isfinite(residual)||residual<0||residual>0.01f)return false;
	}
	return true;
}

static void R_AvatarOrigin(const float matrix[12],float out[3])
{
	out[0]=matrix[3];out[1]=matrix[7];out[2]=matrix[11];
}

static float R_AvatarLength3(const float v[3])
{
	return sqrtf(DotProduct(v,v));
}

static qboolean R_AvatarBuildRotationToward(const float from[3],const float to[3],float out[12])
{
	float a[3],b[3],axis[3],fallback[3],cosine,sine,one;
	memcpy(a,from,sizeof(a));memcpy(b,to,sizeof(b));
	if(!R_AvatarNormalize3(a)||!R_AvatarNormalize3(b))return false;
	cosine=fmaxf(-1.0f,fminf(1.0f,DotProduct(a,b)));
	axis[0]=a[1]*b[2]-a[2]*b[1];axis[1]=a[2]*b[0]-a[0]*b[2];axis[2]=a[0]*b[1]-a[1]*b[0];
	sine=R_AvatarLength3(axis);
	if(sine<0.000001f) {
		if(cosine>0.0f){R_AvatarIdentity(out);return true;}
		fallback[0]=0;fallback[1]=1;fallback[2]=0;
		if(fabsf(DotProduct(fallback,a))>.9f){fallback[0]=0;fallback[1]=0;fallback[2]=1;}
		axis[0]=a[1]*fallback[2]-a[2]*fallback[1];axis[1]=a[2]*fallback[0]-a[0]*fallback[2];axis[2]=a[0]*fallback[1]-a[1]*fallback[0];
		if(!R_AvatarNormalize3(axis))return false;
		sine=0;
	} else for(int i=0;i<3;++i)axis[i]/=sine;
	one=1.0f-cosine;R_AvatarIdentity(out);
	out[0]=cosine+axis[0]*axis[0]*one;out[1]=axis[0]*axis[1]*one-axis[2]*sine;out[2]=axis[0]*axis[2]*one+axis[1]*sine;
	out[4]=axis[1]*axis[0]*one+axis[2]*sine;out[5]=cosine+axis[1]*axis[1]*one;out[6]=axis[1]*axis[2]*one-axis[0]*sine;
	out[8]=axis[2]*axis[0]*one-axis[1]*sine;out[9]=axis[2]*axis[1]*one+axis[0]*sine;out[10]=cosine+axis[2]*axis[2]*one;
	return R_AvatarOrthonormal(out);
}

static void R_AvatarTransformSubtree(const r_avatar_rig_t *rig,float *palette,
	int root,const float delta[12])
{
	int joint;
	for(joint=0;joint<R_AvatarJointCount(rig->live);++joint)if(joint!=root&&R_AvatarDescendant(rig->live,joint,root))
		R_AvatarMultiply(delta,palette+joint*12,palette+joint*12);
}

static qboolean R_AvatarRotateSubtreeToward(const r_avatar_rig_t *rig,float *palette,
	int root,const float from[3],const float to[3])
{
	float before[12],desired[12],inverse[12],delta[12];
	if(root<0||root>=R_AvatarJointCount(rig->live)||!R_AvatarBuildRotationToward(from,to,delta))return false;
	memcpy(before,palette+root*12,sizeof(before));R_AvatarMultiply(delta,before,desired);
	/* This is a pivoted branch rotation: preserve the root origin before
	 * deriving the global delta applied to each child. */
	desired[3]=before[3];desired[7]=before[7];desired[11]=before[11];
	memcpy(palette+root*12,desired,sizeof(desired));
	R_AvatarInverseRigid(before,inverse);R_AvatarMultiply(desired,inverse,delta);
	R_AvatarTransformSubtree(rig,palette,root,delta);
	return true;
}

static qboolean R_AvatarSetSubtreeTransform(const r_avatar_rig_t *rig,float *palette,
	int root,const float desired[12])
{
	float inverse[12],delta[12];int joint;
	if(root<0||root>=R_AvatarJointCount(rig->live)||!R_AvatarOrthonormal(desired))return false;
	R_AvatarInverseRigid(palette+root*12,inverse);R_AvatarMultiply(desired,inverse,delta);
	for(joint=0;joint<R_AvatarJointCount(rig->live);++joint)if(R_AvatarDescendant(rig->live,joint,root)) {
		if(joint==root)memcpy(palette+joint*12,desired,sizeof(float)*12);
		else R_AvatarMultiply(delta,palette+joint*12,palette+joint*12);
	}
	return true;
}

static qboolean R_AvatarBuildPaletteBodyBasis(const r_avatar_rig_t *rig,const float *palette,
	float lateral[3],float forward[3],float up[3])
{
	float left[3],right[3],hip[3],head[3];int l=rig->joint[MD5_VRIK_SHOULDER_L],r=rig->joint[MD5_VRIK_SHOULDER_R];
	if(l<0||r<0||rig->joint[MD5_VRIK_HIP]<0||rig->joint[MD5_VRIK_HEAD]<0)return false;
	R_AvatarOrigin(palette+l*12,left);R_AvatarOrigin(palette+r*12,right);
	for(int i=0;i<3;++i)lateral[i]=right[i]-left[i];
	if(!R_AvatarNormalize3(lateral)) {
		l=rig->joint[MD5_VRIK_UPPERARM_L];r=rig->joint[MD5_VRIK_UPPERARM_R];if(l<0||r<0)return false;
		R_AvatarOrigin(palette+l*12,left);R_AvatarOrigin(palette+r*12,right);for(int i=0;i<3;++i)lateral[i]=right[i]-left[i];
		if(!R_AvatarNormalize3(lateral))return false;
	}
	R_AvatarOrigin(palette+rig->joint[MD5_VRIK_HIP]*12,hip);R_AvatarOrigin(palette+rig->joint[MD5_VRIK_HEAD]*12,head);
	for(int i=0;i<3;++i)up[i]=head[i]-hip[i];
	if(!R_AvatarNormalize3(up))return false;
	forward[0]=up[1]*lateral[2]-up[2]*lateral[1];forward[1]=up[2]*lateral[0]-up[0]*lateral[2];forward[2]=up[0]*lateral[1]-up[1]*lateral[0];
	if(!R_AvatarNormalize3(forward))return false;
	up[0]=lateral[1]*forward[2]-lateral[2]*forward[1];up[1]=lateral[2]*forward[0]-lateral[0]*forward[2];up[2]=lateral[0]*forward[1]-lateral[1]*forward[0];
	return R_AvatarNormalize3(up);
}

/* The physical Dog/Fiend paths include joints semantic retargeting may skip.
 * FABRIK selects positions; subtree rotations and translations then preserve
 * every target-model parent link and the retargeted endpoint basis. */
static qboolean R_AvatarSolvePhysicalPath(const r_avatar_rig_t *rig,float *palette,
	int root,int endpoint,const float target[3],const float endpointbasis[12])
{
	int chain[8],count=0,i,iteration,jointcount=R_AvatarJointCount(rig->live);float position[8][3],solved[8][3],length[7],direction[3],endpointtarget[3],total=0,rootdistance,extension=1;float intended[12];
	if(root<0||endpoint<0||root>=jointcount||endpoint>=jointcount||!R_AvatarOrthonormal(endpointbasis))return false;
	memcpy(intended,endpointbasis,sizeof(intended));
	for(i=endpoint;i>=0&&count<8;i=rig->live->joints[i].parent){chain[count++]=i;if(i==root)break;}
	if(count<3||chain[count-1]!=root)return false;
	for(i=0;i<count/2;++i){int swap=chain[i];chain[i]=chain[count-1-i];chain[count-1-i]=swap;}
	for(i=0;i<count;++i)R_AvatarOrigin(palette+chain[i]*12,position[i]);
	for(i=0;i+1<count;++i){for(int a=0;a<3;++a)direction[a]=position[i+1][a]-position[i][a];length[i]=R_AvatarLength3(direction);if(!isfinite(length[i])||length[i]<.001f)return false;total+=length[i];}
	memcpy(endpointtarget,target,sizeof(endpointtarget));for(i=0;i<3;++i)direction[i]=endpointtarget[i]-position[0][i];rootdistance=R_AvatarLength3(direction);if(!isfinite(rootdistance)||rootdistance<.0001f)return false;
	if(rootdistance>total){extension=fminf(rootdistance/total,1.10f);for(i=0;i+1<count;++i)length[i]*=extension;total*=extension;if(rootdistance>total){for(i=0;i<3;++i)endpointtarget[i]=position[0][i]+direction[i]*total/rootdistance;}}
	memcpy(solved,position,(size_t)count*sizeof(position[0]));
	for(i=0;i<3;++i)direction[i]=endpointtarget[i]-position[0][i];
	rootdistance=R_AvatarLength3(direction);
	if(rootdistance>=total-.001f){if(!R_AvatarNormalize3(direction))return false;for(i=1;i<count;++i)for(int a=0;a<3;++a)solved[i][a]=solved[i-1][a]+length[i-1]*direction[a];}
	else {
		float original[3], side[3] = {0}, perpendicular[3], transport[12];
		float strongest = 0, deficit;
		for(i=0;i<3;++i)original[i]=position[count-1][i]-position[0][i];
		if(!R_AvatarNormalize3(original)||!R_AvatarNormalize3(direction))return false;
		/* A reachable point on a straight chain has no FABRIK bend side.
		 * Select one in the authored frame and transport it toward the goal. */
		for(i=1;i+1<count;++i){
			float projection,magnitude;
			for(int a=0;a<3;++a)perpendicular[a]=position[i][a]-position[0][a];
			projection=DotProduct(perpendicular,original);
			for(int a=0;a<3;++a)perpendicular[a]-=projection*original[a];
			magnitude=R_AvatarLength3(perpendicular);
			if(magnitude>strongest){strongest=magnitude;for(int a=0;a<3;++a)side[a]=perpendicular[a]/magnitude;}
		}
		if(strongest==0){
			for(i=0;i<3;++i){
				float projection;
				for(int a=0;a<3;++a)perpendicular[a]=intended[a*4+i];
				projection=DotProduct(perpendicular,original);
				for(int a=0;a<3;++a)perpendicular[a]-=projection*original[a];
				if(R_AvatarNormalize3(perpendicular)){memcpy(side,perpendicular,sizeof(side));break;}
			}
			if(i==3)return false;
		}
		if(!R_AvatarBuildRotationToward(original,direction,transport))return false;
		for(i=0;i<3;++i)
			perpendicular[i]=transport[i*4]*side[0]+transport[i*4+1]*side[1]+transport[i*4+2]*side[2];
		{
			float projection=DotProduct(perpendicular,direction);
			for(i=0;i<3;++i)perpendicular[i]-=projection*direction[i];
		}
		if(!R_AvatarNormalize3(perpendicular))return false;
		deficit=sqrtf(fmaxf(0,total*total-rootdistance*rootdistance));
		for(i=1;i+1<count;++i){
			float fraction=(float)i/(float)(count-1);
			for(int a=0;a<3;++a)solved[i][a]=position[0][a]+fraction*rootdistance*direction[a]+
				.5f*deficit*sinf((float)M_PI*fraction)*perpendicular[a];
		}
		memcpy(solved[0],position[0],sizeof(position[0]));
		memcpy(solved[count-1],endpointtarget,sizeof(endpointtarget));
		for(iteration=0;iteration<256;++iteration){
			memcpy(solved[count-1],endpointtarget,sizeof(endpointtarget));
			for(i=count-2;i>=0;--i){
				for(int a=0;a<3;++a)direction[a]=solved[i][a]-solved[i+1][a];
				if(!R_AvatarNormalize3(direction))return false;
				for(int a=0;a<3;++a)solved[i][a]=solved[i+1][a]+length[i]*direction[a];
			}
			memcpy(solved[0],position[0],sizeof(position[0]));
			for(i=1;i<count;++i){
				for(int a=0;a<3;++a)direction[a]=solved[i][a]-solved[i-1][a];
				if(!R_AvatarNormalize3(direction))return false;
				for(int a=0;a<3;++a)solved[i][a]=solved[i-1][a]+length[i-1]*direction[a];
			}
			for(i=0;i<3;++i)direction[i]=endpointtarget[i]-solved[count-1][i];
			if(R_AvatarLength3(direction)<.001f)break;
		}
	}
	for(i=0;i<3;++i)direction[i]=endpointtarget[i]-solved[count-1][i];
	if(R_AvatarLength3(direction)>=.01f)return false;
	for(i=0;i+1<count;++i){float oldnext[3],oldroot[3];R_AvatarOrigin(palette+chain[i]*12,oldroot);R_AvatarOrigin(palette+chain[i+1]*12,oldnext);for(int a=0;a<3;++a){oldnext[a]-=oldroot[a];direction[a]=solved[i+1][a]-solved[i][a];}if(!R_AvatarRotateSubtreeToward(rig,palette,chain[i],oldnext,direction))return false;}
	for(i=1;i<count;++i){float current[3],delta[3];R_AvatarOrigin(palette+chain[i]*12,current);for(int a=0;a<3;++a)delta[a]=solved[i][a]-current[a];if(R_AvatarLength3(delta)>.0001f)for(int j=0;j<jointcount;++j)if(R_AvatarDescendant(rig->live,j,chain[i]))for(int a=0;a<3;++a)palette[j*12+a*4+3]+=delta[a];}
	for(i=0;i<3;++i)intended[i*4+3]=endpointtarget[i];
	return R_AvatarSetSubtreeTransform(rig,palette,endpoint,intended);
}

static qboolean R_AvatarRepairTrackedAnimalUpperBody(const r_avatar_rig_t *source,
	const r_avatar_rig_t *target,const r_avatar_presentation_context_t *context,
	const float *sourcepalette,float *palette)
{
	const r_avatar_profile_t *profile=target->profile;int semantics[3]={MD5_VRIK_HEAD,MD5_VRIK_HAND_L,MD5_VRIK_HAND_R},endpoint[3],hip=target->joint[MD5_VRIK_HIP],root=target->joint[profile->desktop_upperbody_bind_root],i,joint;float saved[3][12],targetbasis[12],inverse[12],solved[12],turn[12],desired[12],pitch[12],sourceleft[3],sourceforward[3],sourceup[3],tr[3],tf[3],tu[3],hiporigin[3],bindhip[3],current[3];
	if(hip<0||root<0||!R_AvatarDescendant(target->live,root,hip)||!R_AvatarBuildPaletteBodyBasis(source,sourcepalette,sourceleft,sourceforward,sourceup)||!R_AvatarCanonicalToTargetBasis(target,targetbasis))return false;
	for(i=0;i<3;++i){endpoint[i]=target->joint[semantics[i]];if(endpoint[i]<0||!R_AvatarDescendant(target->live,endpoint[i],root))return false;memcpy(saved[i],palette+endpoint[i]*12,sizeof(saved[i]));}
	for(i=0;i<3;++i){tr[i]=context->inverse[i*4]*sourceleft[0]+context->inverse[i*4+1]*sourceleft[1]+context->inverse[i*4+2]*sourceleft[2];tf[i]=context->inverse[i*4]*sourceforward[0]+context->inverse[i*4+1]*sourceforward[1]+context->inverse[i*4+2]*sourceforward[2];tu[i]=context->inverse[i*4]*sourceup[0]+context->inverse[i*4+1]*sourceup[1]+context->inverse[i*4+2]*sourceup[2];}
	if(!R_AvatarNormalize3(tr)||!R_AvatarNormalize3(tf)||!R_AvatarNormalize3(tu))return false;
	R_AvatarIdentity(solved);for(i=0;i<3;++i){solved[i*4]=tu[i];solved[i*4+1]=-tr[i];solved[i*4+2]=-tf[i];}
	R_AvatarInverseRigid(targetbasis,inverse);R_AvatarMultiply(solved,inverse,turn);R_AvatarOrigin(palette+hip*12,hiporigin);R_AvatarOrigin(target->live->joints[hip].bind,bindhip);
	for(i=0;i<3;++i)turn[i*4+3]=hiporigin[i]-(turn[i*4]*bindhip[0]+turn[i*4+1]*bindhip[1]+turn[i*4+2]*bindhip[2]);
	R_AvatarMultiply(turn,target->live->joints[root].bind,desired);R_AvatarOrigin(desired,current);for(i=0;i<3;++i)current[i]-=hiporigin[i];
	if(!R_AvatarLimitedTurn(current,tu,profile->posture_degrees,pitch))return false;
	for(joint=0;joint<R_AvatarJointCount(target->live);++joint)if(joint==root||R_AvatarDescendant(target->live,joint,root))R_AvatarMultiply(turn,target->live->joints[joint].bind,palette+joint*12);
	for(joint=0;joint<R_AvatarJointCount(target->live);++joint)if(R_AvatarDescendant(target->live,joint,root)){float old[3],moved[12];R_AvatarOrigin(palette+joint*12,old);for(i=0;i<3;++i)old[i]-=hiporigin[i];R_AvatarMultiply(pitch,palette+joint*12,moved);for(i=0;i<3;++i)moved[i*4+3]=hiporigin[i]+pitch[i*4]*old[0]+pitch[i*4+1]*old[1]+pitch[i*4+2]*old[2];memcpy(palette+joint*12,moved,sizeof(moved));}
	for(i=0;i<3;++i){R_AvatarOrigin(palette+endpoint[i]*12,current);saved[i][3]=current[0];saved[i][7]=current[1];saved[i][11]=current[2];if(!R_AvatarSetSubtreeTransform(target,palette,endpoint[i],saved[i]))return false;}
	return true;
}

static qboolean R_AvatarPaletteValid(const r_avatar_rig_t *rig,const float *palette)
{
	for(int joint=0;joint<R_AvatarJointCount(rig->live);++joint)
		if(!R_AvatarOrthonormal(palette+joint*12))return false;
	return true;
}

/* Retargeting preserves an animal's authored Hip basis for its unmapped
 * branches. A supplied tracker restores the canonical Hip turn to just those
 * branches; semantic branches already carry the retargeted source pose. */
static qboolean R_AvatarApplyTrackedAnimalHip(const r_avatar_rig_t *source,
	const r_avatar_rig_t *target,const r_avatar_presentation_context_t *context,
	const float *sourcepalette,float *palette,const float *before)
{
	int hip=target->joint[MD5_VRIK_HIP],sourcehip=source->joint[MD5_VRIK_HIP];
	float inverse[12],delta[12],mapped[12],desired[12];
	if(hip<0||sourcehip<0)return false;
	R_AvatarInverseRigid(source->live->joints[sourcehip].bind,inverse);
	R_AvatarMultiply(sourcepalette+sourcehip*12,inverse,delta);
	delta[3]=delta[7]=delta[11]=0;
	R_AvatarMultiply(delta,context->rotation,mapped);
	R_AvatarInverseRigid(context->rotation,inverse);
	R_AvatarMultiply(inverse,mapped,delta);
	R_AvatarMultiply(delta,palette+hip*12,desired);
	desired[3]=palette[hip*12+3];
	desired[7]=palette[hip*12+7];
	desired[11]=palette[hip*12+11];
	if(!R_AvatarSetSubtreeTransform(target,palette,hip,desired))return false;
	for(int joint=0;joint<R_AvatarJointCount(target->live);++joint){
		int ancestor=joint,semantic;
		if(joint==hip||!R_AvatarDescendant(target->live,joint,hip))continue;
		while(ancestor>=0&&ancestor!=hip){
			for(semantic=0;semantic<MD5_VRIK_JOINT_COUNT;++semantic)
				if(target->joint[semantic]==ancestor&&!(target->virtual_mask&(1u<<semantic)))break;
			if(semantic<MD5_VRIK_JOINT_COUNT){
				memcpy(palette+joint*12,before+joint*12,sizeof(float)*12);
				break;
			}
			ancestor=target->live->joints[ancestor].parent;
		}
	}
	return true;
}

static qboolean R_AvatarRefineTrackedAnimalPalette(const r_avatar_rig_t *source,
	const r_avatar_rig_t *target,const float *sourcepalette,
	float floor_correction_z,unsigned char tracked_lower_mask,float *palette)
{
	r_avatar_presentation_context_t context;
	float saved[R_AVATAR_MAX_JOINTS*12],footbasis[2][12],targetpoint[3];
	size_t bytes=(size_t)R_AvatarJointCount(target->live)*12*sizeof(float);
	qboolean complete=true,uppervalid;
	int side,head=target->joint[MD5_VRIK_HEAD];
	if(!isfinite(floor_correction_z)||
		!R_AvatarBuildPresentationContext(source,target,&context))return false;
	for(side=0;side<2;++side){
		int foot=target->joint[side?MD5_VRIK_FOOT_R:MD5_VRIK_FOOT_L];
		if(foot<0)return false;
		memcpy(footbasis[side],palette+foot*12,sizeof(footbasis[side]));
	}
	memcpy(saved,palette,bytes);
	if((tracked_lower_mask&R_AVATAR_TRACKED_HIP)&&target->profile->preserve_hip_rotation){
		if(!R_AvatarApplyTrackedAnimalHip(source,target,&context,sourcepalette,palette,saved)||
			!R_AvatarPaletteValid(target,palette)){
			memcpy(palette,saved,bytes);
			complete=false;
		}
	}
	R_AvatarPresentationAddCanonicalZ(&context,floor_correction_z);
	memcpy(saved,palette,bytes);
	uppervalid=R_AvatarRepairTrackedAnimalUpperBody(source,target,&context,sourcepalette,palette);
	for(side=0;uppervalid&&side<2;++side){
		int arm=side?MD5_VRIK_HAND_R:MD5_VRIK_HAND_L;
		int root=target->joint[side?MD5_VRIK_UPPERARM_R:MD5_VRIK_UPPERARM_L];
		if(target->profile->id==PLAYER_AVATAR_FIEND)
			root=target->joint[side?MD5_VRIK_SHOULDER_R:MD5_VRIK_SHOULDER_L];
		R_AvatarOrigin(sourcepalette+source->joint[arm]*12,targetpoint);
		R_AvatarPresentationInversePoint(&context,targetpoint,targetpoint);
		uppervalid=R_AvatarSolvePhysicalPath(target,palette,root,target->joint[arm],
			targetpoint,palette+target->joint[arm]*12);
	}
	if(uppervalid){
		float desired[12];
		R_AvatarOrigin(sourcepalette+source->joint[MD5_VRIK_HEAD]*12,targetpoint);
		R_AvatarPresentationInversePoint(&context,targetpoint,targetpoint);
		memcpy(desired,palette+head*12,sizeof(desired));
		for(int axis=0;axis<3;++axis)desired[axis*4+3]=targetpoint[axis];
		uppervalid=R_AvatarSetSubtreeTransform(target,palette,head,desired)&&
			R_AvatarPaletteValid(target,palette);
	}
	if(!uppervalid){memcpy(palette,saved,bytes);complete=false;}
	for(side=0;side<2;++side){
		int leg=side?MD5_VRIK_FOOT_R:MD5_VRIK_FOOT_L;
		int upperleg=side?MD5_VRIK_UPPERLEG_R:MD5_VRIK_UPPERLEG_L;
		if(!(tracked_lower_mask&(1u<<side)))continue;
		memcpy(saved,palette,bytes);
		R_AvatarOrigin(sourcepalette+source->joint[leg]*12,targetpoint);
		R_AvatarPresentationInversePoint(&context,targetpoint,targetpoint);
		if(!R_AvatarSolvePhysicalPath(target,palette,target->joint[upperleg],
			target->joint[leg],targetpoint,footbasis[side])||
			!R_AvatarPaletteValid(target,palette)){
			memcpy(palette,saved,bytes);
			complete=false;
		}
	}
	return complete;
}

qboolean R_AvatarRefineBuiltinPalette(const r_avatar_rig_t *source,
	const r_avatar_rig_t *target,qboolean tracked,
	const float (*source_palette)[12],float floor_correction_z,
	unsigned char tracked_lower_mask,
	float (*target_palette)[12],size_t target_capacity)
{
	r_avatar_presentation_context_t context;
	float saved[R_AVATAR_MAX_JOINTS*12];
	const r_avatar_profile_t *profile;
	size_t bytes;
	int joint;
	if(!source||!target||!source->valid||!target->valid||!target->profile||
		!target_palette||R_AvatarJointCount(target->live)>R_AVATAR_MAX_JOINTS||
		target_capacity<target->live->joint_count)return false;
	if(tracked&&(!source_palette||R_AvatarJointCount(source->live)>R_AVATAR_MAX_JOINTS))return false;
	if(tracked)for(joint=0;joint<R_AvatarJointCount(source->live);++joint)
		if(!R_AvatarOrthonormal(source_palette[joint]))return false;
	profile=target->profile;
	if(tracked&&(profile->id==PLAYER_AVATAR_DOG||profile->id==PLAYER_AVATAR_FIEND))
		return R_AvatarRefineTrackedAnimalPalette(source,target,(const float *)source_palette,
			floor_correction_z,tracked_lower_mask,(float *)target_palette);
	if(!tracked && (profile->id!=PLAYER_AVATAR_DOG &&
		profile->id!=PLAYER_AVATAR_FIEND && !profile->mirror_outer_leg_poles))
		return true;
	bytes=target->live->joint_count*12*sizeof(float);
	memcpy(saved,target_palette,bytes);
	if((!tracked && (profile->id==PLAYER_AVATAR_DOG||profile->id==PLAYER_AVATAR_FIEND) &&
		(!R_AvatarBuildPresentationContext(source,target,&context)||
		 !R_AvatarStabilizeDesktopUpperBody(target,(float *)target_palette)||
		 !R_AvatarApplyDesktopUprightPosture(target,&context,(float *)target_palette)||
		 !R_AvatarRebuildDesktopAnimalArms(target,saved,(float *)target_palette)))||
		(!tracked && profile->mirror_outer_leg_poles &&
		 !R_AvatarRepairInwardOuterLegs(target,(float *)target_palette)))
		goto rollback;
	for(joint=0;joint<R_AvatarJointCount(target->live);++joint)
		if(!R_AvatarOrthonormal(target_palette[joint]))goto rollback;
	return true;
rollback:
	memcpy(target_palette,saved,bytes);
	return false;
}

qboolean R_AvatarCanonicalToTargetBasis (const r_avatar_rig_t *rig, float out[12])
{
	return R_AvatarBuildBindBodyBasis(rig, out);
}

qboolean R_AvatarTargetToCanonicalBasis (const r_avatar_rig_t *rig, float out[12])
{
	float forward[12];
	if (!out || !R_AvatarBuildBindBodyBasis(rig, forward)) return false;
	R_AvatarInverseRigid(forward, out);
	return true;
}

qboolean R_AvatarTargetToCanonicalPresentation (const r_avatar_rig_t *source,
	const r_avatar_rig_t *target, float out[12])
{
	float scale, sourcehip[3], targethip[3], sourcebasis[12], targetbasis[12], targetinverse[12];
	if (!source || !target || !source->valid || !target->valid || !out ||
		source->joint[MD5_VRIK_HIP] < 0 || target->joint[MD5_VRIK_HIP] < 0 ||
		!R_AvatarBuildBindBodyBasis(source, sourcebasis) ||
		!R_AvatarBuildBindBodyBasis(target, targetbasis)) return false;
	R_AvatarInverseRigid(targetbasis, targetinverse);
	R_AvatarMultiply(sourcebasis, targetinverse, out);
	scale = R_AvatarQuantizedDisplayScale(target->profile);
	if (scale <= 0.0f) return false;
	sourcehip[0] = source->live->joints[source->joint[MD5_VRIK_HIP]].bind[3];
	sourcehip[1] = source->live->joints[source->joint[MD5_VRIK_HIP]].bind[7];
	sourcehip[2] = source->live->joints[source->joint[MD5_VRIK_HIP]].bind[11];
	targethip[0] = target->live->joints[target->joint[MD5_VRIK_HIP]].bind[3];
	targethip[1] = target->live->joints[target->joint[MD5_VRIK_HIP]].bind[7];
	targethip[2] = target->live->joints[target->joint[MD5_VRIK_HIP]].bind[11];
	out[0] *= scale; out[1] *= scale; out[2] *= scale;
	out[4] *= scale; out[5] *= scale; out[6] *= scale;
	out[8] *= scale; out[9] *= scale; out[10] *= scale;
	out[3] = sourcehip[0] - (out[0] * targethip[0] + out[1] * targethip[1] + out[2] * targethip[2]);
	out[7] = sourcehip[1] - (out[4] * targethip[0] + out[5] * targethip[1] + out[6] * targethip[2]);
	out[11] = sourcehip[2] - (out[8] * targethip[0] + out[9] * targethip[1] + out[10] * targethip[2]);
	return R_AvatarFiniteMatrix(out);
}

qboolean R_AvatarCanonicalToTargetPresentation (const r_avatar_rig_t *source,
	const r_avatar_rig_t *target, float out[12])
{
	float forward[12], scale;
	int r, c;
	if (!out || !R_AvatarTargetToCanonicalPresentation(source, target, forward)) return false;
	scale = R_AvatarQuantizedDisplayScale(target->profile);
	if (scale <= 0.0f) return false;
	/* The linear section is scale*rotation, rather than a rigid matrix. */
	for (r = 0; r < 3; ++r) for (c = 0; c < 3; ++c) out[r * 4 + c] = forward[c * 4 + r] / (scale * scale);
	for (r = 0; r < 3; ++r)
		out[r * 4 + 3] = -(out[r * 4] * forward[3] + out[r * 4 + 1] * forward[7] + out[r * 4 + 2] * forward[11]);
	return R_AvatarFiniteMatrix(out);
}

float R_AvatarQuantizedDisplayScale (const r_avatar_profile_t *profile)
{
	float value;
	if (!profile || !isfinite(profile->display_scale) || profile->display_scale <= 0.0f) return 1.0f;
	value = floorf(profile->display_scale * 4096.0f + 0.5f) / 4096.0f;
	return value > 0.0f && isfinite(value) ? value : 1.0f;
}
