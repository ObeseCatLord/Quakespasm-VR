/*
 * CPU-only port of the canonical Ranger upper-body VRIK palette math from
 * QuakeSpasm OpenVR's r_alias.c.  Matrices remain absolute, row-major 3x4
 * transforms throughout, matching MD5 animation poses and Vulkan skinning.
 */
#include "quakedef.h"
#include "r_vrik.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define R_VRIK_MAX_JOINTS 256
#define VRIK_ARM_MAX_STRETCH 1.10f
#define VRIK_GUN_MUZZLE_OFFSET 20.0f

typedef enum r_vrik_joint_e
{
	R_VRIK_HIP,
	R_VRIK_SPINE1,
	R_VRIK_SPINE2,
	R_VRIK_NECK,
	R_VRIK_HEAD,
	R_VRIK_SHOULDER_L,
	R_VRIK_UPPERARM_L,
	R_VRIK_LOWERARM_L,
	R_VRIK_HAND_L,
	R_VRIK_SHOULDER_R,
	R_VRIK_UPPERARM_R,
	R_VRIK_LOWERARM_R,
	R_VRIK_HAND_R,
	R_VRIK_GUN,
	R_VRIK_AXE,
	R_VRIK_SMALL_FLAME,
	R_VRIK_BIG_FLAME,
	R_VRIK_JOINT_COUNT
} r_vrik_joint_t;

static const char *const r_vrik_joint_names[R_VRIK_JOINT_COUNT] = {
	"Hip", "Spine1", "Spine2", "Neck", "Head",
	"Shoulder_L", "UpperArm_L", "LowerArm_L", "Hand_L",
	"Shoulder_R", "UpperArm_R", "LowerArm_R", "Hand_R",
	"Gun", "Axe", "small_flame", "big_flame"
};

static void R_VRIKMatrixOrigin (const float matrix[12], vec3_t origin)
{
	origin[0] = matrix[3];
	origin[1] = matrix[7];
	origin[2] = matrix[11];
}

static void R_VRIKSetMatrixOrigin (float matrix[12], const vec3_t origin)
{
	matrix[3] = origin[0];
	matrix[7] = origin[1];
	matrix[11] = origin[2];
}

static void R_VRIKMatrixMultiply (const float first[12], const float second[12],
	float out[12])
{
	float result[12];
	R_ConcatTransforms ((float (*)[4])first, (float (*)[4])second,
		(float (*)[4])result);
	memcpy (out, result, sizeof (result));
}

static void R_VRIKMatrixInverseRigid (const float matrix[12], float inverse[12])
{
	int row, column;

	for (row = 0; row < 3; row++)
		for (column = 0; column < 3; column++)
			inverse[row * 4 + column] = matrix[column * 4 + row];
	for (row = 0; row < 3; row++)
		inverse[row * 4 + 3] = -(inverse[row * 4 + 0] * matrix[3] +
			inverse[row * 4 + 1] * matrix[7] +
			inverse[row * 4 + 2] * matrix[11]);
}

static void R_VRIKOrthonormalize (float matrix[12])
{
	vec3_t x = { matrix[0], matrix[4], matrix[8] };
	vec3_t y = { matrix[1], matrix[5], matrix[9] };
	vec3_t z;
	float projection;

	if (!VectorNormalize (x))
		x[0] = 1, x[1] = 0, x[2] = 0;
	projection = DotProduct (x, y);
	VectorMA (y, -projection, x, y);
	if (!VectorNormalize (y))
	{
		y[0] = 0, y[1] = 1, y[2] = 0;
		projection = DotProduct (x, y);
		VectorMA (y, -projection, x, y);
		VectorNormalize (y);
	}
	CrossProduct (x, y, z);
	VectorNormalize (z);
	matrix[0] = x[0]; matrix[4] = x[1]; matrix[8] = x[2];
	matrix[1] = y[0]; matrix[5] = y[1]; matrix[9] = y[2];
	matrix[2] = z[0]; matrix[6] = z[1]; matrix[10] = z[2];
}

static void R_VRIKLerpPalette (const md5_skeleton_view_t *skeleton,
	const lerpdata_t *lerpdata, float (*palette)[12])
{
	const float (*first)[12] = skeleton->absolute_poses +
		(size_t)lerpdata->pose1 * skeleton->joint_count;
	const float (*second)[12] = skeleton->absolute_poses +
		(size_t)lerpdata->pose2 * skeleton->joint_count;
	size_t joint;
	int component;

	for (joint = 0; joint < skeleton->joint_count; joint++)
	{
		for (component = 0; component < 12; component++)
			palette[joint][component] = first[joint][component] +
				(second[joint][component] - first[joint][component]) *
				lerpdata->blend;
		R_VRIKOrthonormalize (palette[joint]);
	}
}

static int R_VRIKNameEqual (const char *a, const char *b)
{
	while (*a && *b)
	{
		unsigned char ca = (unsigned char)*a++;
		unsigned char cb = (unsigned char)*b++;
		if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
		if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
		if (ca != cb)
			return 0;
	}
	return *a == *b;
}

static qboolean R_VRIKResolveJoints (const md5_skeleton_view_t *skeleton,
	int indexes[R_VRIK_JOINT_COUNT])
{
	size_t joint;
	int semantic;

	for (semantic = 0; semantic < R_VRIK_JOINT_COUNT; semantic++)
		indexes[semantic] = -1;
	for (joint = 0; joint < skeleton->joint_count; joint++)
	{
		const md5_skeleton_joint_t *source = &skeleton->joints[joint];
		if (!memchr (source->name, '\0', sizeof (source->name)))
			return false;
		for (semantic = 0; semantic < R_VRIK_JOINT_COUNT; semantic++)
			if (R_VRIKNameEqual (source->name, r_vrik_joint_names[semantic]))
			{
				if (indexes[semantic] >= 0)
					return false;
				indexes[semantic] = (int)joint;
				break;
			}
	}
	/* These named links define the donor's Ranger upper-body solve.  Ancestor
	 * checks permit harmless intermediary authored joints while rejecting
	 * similarly named but unrelated bones. */
	{
		static const struct { int child, ancestor; } links[] = {
			{ R_VRIK_SPINE1, R_VRIK_HIP },
			{ R_VRIK_SPINE2, R_VRIK_SPINE1 },
			{ R_VRIK_NECK, R_VRIK_SPINE2 },
			{ R_VRIK_HEAD, R_VRIK_NECK },
			{ R_VRIK_SHOULDER_L, R_VRIK_SPINE2 },
			{ R_VRIK_SHOULDER_R, R_VRIK_SPINE2 },
			{ R_VRIK_UPPERARM_L, R_VRIK_SHOULDER_L },
			{ R_VRIK_LOWERARM_L, R_VRIK_UPPERARM_L },
			{ R_VRIK_HAND_L, R_VRIK_LOWERARM_L },
			{ R_VRIK_UPPERARM_R, R_VRIK_SHOULDER_R },
			{ R_VRIK_LOWERARM_R, R_VRIK_UPPERARM_R },
			{ R_VRIK_HAND_R, R_VRIK_LOWERARM_R }
		};
		size_t link;
		for (semantic = R_VRIK_HIP; semantic <= R_VRIK_HAND_R; semantic++)
			if (indexes[semantic] < 0)
				return false;
		for (link = 0; link < sizeof (links) / sizeof (links[0]); link++)
		{
			int child = indexes[links[link].child];
			int ancestor = indexes[links[link].ancestor];
			int parent = skeleton->joints[child].parent;
			int steps = 0;
			if (child == ancestor)
				return false;
			while (parent >= 0 && (size_t)parent < skeleton->joint_count &&
				steps++ < (int)skeleton->joint_count)
			{
				if (parent == ancestor)
					break;
				parent = skeleton->joints[parent].parent;
			}
			if (parent != ancestor)
				return false;
		}
	}
	return true;
}

static qboolean R_VRIKBuildBodyBasis (const int *jointindex,
	const float (*palette)[12], vec3_t lateral, vec3_t forward, vec3_t up)
{
	vec3_t leftshoulder, rightshoulder, hip, head;
	int left = jointindex[R_VRIK_SHOULDER_L];
	int right = jointindex[R_VRIK_SHOULDER_R];
	int leftupper = jointindex[R_VRIK_UPPERARM_L];
	int rightupper = jointindex[R_VRIK_UPPERARM_R];
	int hipjoint = jointindex[R_VRIK_HIP];
	int headjoint = jointindex[R_VRIK_HEAD];

	R_VRIKMatrixOrigin (palette[left], leftshoulder);
	R_VRIKMatrixOrigin (palette[right], rightshoulder);
	R_VRIKMatrixOrigin (palette[hipjoint], hip);
	R_VRIKMatrixOrigin (palette[headjoint], head);
	VectorSubtract (rightshoulder, leftshoulder, lateral);
	if (!VectorNormalize (lateral))
	{
		R_VRIKMatrixOrigin (palette[leftupper], leftshoulder);
		R_VRIKMatrixOrigin (palette[rightupper], rightshoulder);
		VectorSubtract (rightshoulder, leftshoulder, lateral);
		if (!VectorNormalize (lateral))
			return false;
	}
	VectorSubtract (head, hip, up);
	if (!VectorNormalize (up))
		return false;
	/* Network root-local coordinates are forward/left/up. */
	CrossProduct (up, lateral, forward);
	if (!VectorNormalize (forward))
		return false;
	CrossProduct (lateral, forward, up);
	return VectorNormalize (up) != 0.0f;
}

static void R_VRIKLocalVectorToModel (const vec3_t local,
	const vec3_t lateral, const vec3_t forward, const vec3_t up, vec3_t model)
{
	model[0] = forward[0] * local[0] - lateral[0] * local[1] + up[0] * local[2];
	model[1] = forward[1] * local[0] - lateral[1] * local[1] + up[1] * local[2];
	model[2] = forward[2] * local[0] - lateral[2] * local[1] + up[2] * local[2];
}

static void R_VRIKCanonicalMatrix (const vec3_t direction,
	const vec3_t preferred_up, const vec3_t origin, float matrix[12])
{
	vec3_t x, y, z;
	VectorCopy (direction, x);
	if (!VectorNormalize (x))
		x[0] = 1, x[1] = 0, x[2] = 0;
	CrossProduct (preferred_up, x, y);
	if (!VectorNormalize (y))
	{
		vec3_t fallback = { 0, 0, 1 };
		CrossProduct (fallback, x, y);
		if (!VectorNormalize (y))
			y[0] = 0, y[1] = 1, y[2] = 0;
	}
	CrossProduct (x, y, z);
	VectorNormalize (z);
	matrix[0] = x[0]; matrix[1] = y[0]; matrix[2] = z[0]; matrix[3] = origin[0];
	matrix[4] = x[1]; matrix[5] = y[1]; matrix[6] = z[1]; matrix[7] = origin[1];
	matrix[8] = x[2]; matrix[9] = y[2]; matrix[10] = z[2]; matrix[11] = origin[2];
}

static void R_VRIKAnglesToModelMatrix (const vec3_t angles,
	const vec3_t lateral, const vec3_t forward, const vec3_t up,
	const vec3_t origin, float matrix[12])
{
	vec3_t mutable_angles, quakeforward, quakeright, quakeup;
	vec3_t x, y, z, quakeleft;
	VectorCopy (angles, mutable_angles);
	AngleVectors (mutable_angles, quakeforward, quakeright, quakeup);
	quakeleft[0] = -quakeright[0];
	quakeleft[1] = -quakeright[1];
	quakeleft[2] = -quakeright[2];
	R_VRIKLocalVectorToModel (quakeforward, lateral, forward, up, x);
	R_VRIKLocalVectorToModel (quakeleft, lateral, forward, up, y);
	R_VRIKLocalVectorToModel (quakeup, lateral, forward, up, z);
	VectorNormalize (x); VectorNormalize (y); VectorNormalize (z);
	matrix[0] = x[0]; matrix[1] = y[0]; matrix[2] = z[0]; matrix[3] = origin[0];
	matrix[4] = x[1]; matrix[5] = y[1]; matrix[6] = z[1]; matrix[7] = origin[1];
	matrix[8] = x[2]; matrix[9] = y[2]; matrix[10] = z[2]; matrix[11] = origin[2];
}

static void R_VRIKTrackedItemMatrix (const float tracked[12],
	const vec3_t origin, float item[12])
{
	item[0] = -tracked[1]; item[1] = tracked[0]; item[2] = tracked[2];
	item[4] = -tracked[5]; item[5] = tracked[4]; item[6] = tracked[6];
	item[8] = -tracked[9]; item[9] = tracked[8]; item[10] = tracked[10];
	R_VRIKSetMatrixOrigin (item, origin);
}

static void R_VRIKTrackedAxeMatrix (const float tracked[12],
	const vec3_t origin, float item[12])
{
	item[0] = -tracked[1]; item[1] = tracked[2]; item[2] = -tracked[0];
	item[4] = -tracked[5]; item[5] = tracked[6]; item[6] = -tracked[4];
	item[8] = -tracked[9]; item[9] = tracked[10]; item[10] = -tracked[8];
	R_VRIKSetMatrixOrigin (item, origin);
}

static void R_VRIKRotateToward (float matrix[12], const vec3_t from,
	const vec3_t to)
{
	vec3_t a, b, axis, origin;
	float cosine, sine, one;
	float delta[12] = { 0 }, result[12];
	VectorCopy (from, a); VectorCopy (to, b);
	if (!VectorNormalize (a) || !VectorNormalize (b))
		return;
	cosine = CLAMP (-1.0f, DotProduct (a, b), 1.0f);
	CrossProduct (a, b, axis);
	sine = VectorNormalize (axis);
	if (sine < 0.0001f)
	{
		vec3_t fallbackaxis;
		if (cosine > 0.0f)
			return;
		axis[0] = 0, axis[1] = 0, axis[2] = 1;
		if (fabsf (DotProduct (axis, a)) > 0.9f)
			axis[0] = 0, axis[1] = 1, axis[2] = 0;
		CrossProduct (a, axis, fallbackaxis);
		VectorCopy (fallbackaxis, axis);
		if (!VectorNormalize (axis))
			return;
		sine = 0.0f;
	}
	one = 1.0f - cosine;
	delta[0] = cosine + axis[0] * axis[0] * one;
	delta[1] = axis[0] * axis[1] * one - axis[2] * sine;
	delta[2] = axis[0] * axis[2] * one + axis[1] * sine;
	delta[4] = axis[1] * axis[0] * one + axis[2] * sine;
	delta[5] = cosine + axis[1] * axis[1] * one;
	delta[6] = axis[1] * axis[2] * one - axis[0] * sine;
	delta[8] = axis[2] * axis[0] * one - axis[1] * sine;
	delta[9] = axis[2] * axis[1] * one + axis[0] * sine;
	delta[10] = cosine + axis[2] * axis[2] * one;
	R_VRIKMatrixOrigin (matrix, origin);
	R_VRIKMatrixMultiply (delta, matrix, result);
	memcpy (matrix, result, sizeof (result));
	R_VRIKSetMatrixOrigin (matrix, origin);
}

/* The Vulkan palette path uses the model-owned skeleton view directly.  Keep
 * the donor leg solver's hierarchy work local to this one palette build; the
 * legacy live-skinning rig cache would create a second mutable pose owner. */
typedef struct r_vrik_leg_rig_s
{
	int upper;
	int lower;
	int foot;
	vec3_t bind_knee_vector;
	vec3_t bind_foot_vector;
	float bind_upper_length;
	float bind_lower_length;
	qboolean valid;
} r_vrik_leg_rig_t;

static qboolean R_VRIKFinite3 (const vec3_t vector);

static qboolean R_VRIKIsDescendant (const md5_skeleton_view_t *skeleton,
	int child, int ancestor)
{
	int steps;

	if (!skeleton || !skeleton->joints || child < 0 || ancestor < 0 ||
		(size_t)child >= skeleton->joint_count ||
		(size_t)ancestor >= skeleton->joint_count)
		return false;
	for (steps = 0; steps < (int)skeleton->joint_count; steps++)
	{
		int parent;
		if (child == ancestor)
			return true;
		parent = skeleton->joints[child].parent;
		if (parent == -1)
			return false;
		if (parent < 0 || (size_t)parent >= skeleton->joint_count)
			return false;
		child = parent;
	}
	return false;
}

/* -1 means absent; -2 means ambiguous or malformed. */
static int R_VRIKFindNamedJoint (const md5_skeleton_view_t *skeleton,
	const char *name)
{
	int found = -1;
	size_t joint;

	for (joint = 0; joint < skeleton->joint_count; joint++)
	{
		const char *jointname = skeleton->joints[joint].name;
		if (!memchr (jointname, '\0', sizeof (skeleton->joints[joint].name)))
			return -2;
		if (!R_VRIKNameEqual (jointname, name))
			continue;
		if (found >= 0)
			return -2;
		found = (int)joint;
	}
	return found;
}

static qboolean R_VRIKInitLegRig (const md5_skeleton_view_t *skeleton,
	int hip, int upper, int lower, int foot, r_vrik_leg_rig_t *rig)
{
	vec3_t upperorigin, lowerorigin, footorigin;

	memset (rig, 0, sizeof (*rig));
	rig->upper = upper;
	rig->lower = lower;
	rig->foot = foot;
	if (hip < 0 || upper < 0 || lower < 0 || foot < 0 ||
		(size_t)hip >= skeleton->joint_count || (size_t)upper >= skeleton->joint_count ||
		(size_t)lower >= skeleton->joint_count || (size_t)foot >= skeleton->joint_count ||
		hip == upper || upper == lower || lower == foot || upper == foot ||
		!R_VRIKIsDescendant (skeleton, rig->upper, hip) ||
		!R_VRIKIsDescendant (skeleton, rig->lower, rig->upper) ||
		!R_VRIKIsDescendant (skeleton, rig->foot, rig->lower))
		return false;
	R_VRIKMatrixOrigin (skeleton->joints[rig->upper].bind, upperorigin);
	R_VRIKMatrixOrigin (skeleton->joints[rig->lower].bind, lowerorigin);
	R_VRIKMatrixOrigin (skeleton->joints[rig->foot].bind, footorigin);
	if (!R_VRIKFinite3 (upperorigin) || !R_VRIKFinite3 (lowerorigin) ||
		!R_VRIKFinite3 (footorigin))
		return false;
	VectorSubtract (lowerorigin, upperorigin, rig->bind_knee_vector);
	VectorSubtract (footorigin, lowerorigin, rig->bind_foot_vector);
	rig->bind_upper_length = VectorLength (rig->bind_knee_vector);
	rig->bind_lower_length = VectorLength (rig->bind_foot_vector);
	if (!isfinite (rig->bind_upper_length) || !isfinite (rig->bind_lower_length) ||
		rig->bind_upper_length <= 0.01f || rig->bind_lower_length <= 0.01f)
		return false;
	rig->valid = true;
	return true;
}

static qboolean R_VRIKResolveLegRig (const md5_skeleton_view_t *skeleton,
	int hip, int side, r_vrik_leg_rig_t *rig)
{
	return R_VRIKInitLegRig (skeleton, hip,
		R_VRIKFindNamedJoint (skeleton, side ? "UpperLeg_R" : "UpperLeg_L"),
		R_VRIKFindNamedJoint (skeleton, side ? "LowerLeg_R" : "LowerLeg_L"),
		R_VRIKFindNamedJoint (skeleton, side ? "Foot_R" : "Foot_L"), rig);
}

static void R_VRIKTranslateSubtree (const md5_skeleton_view_t *skeleton,
	float (*palette)[12], int root, const vec3_t delta)
{
	int joint;

	for (joint = 0; (size_t)joint < skeleton->joint_count; joint++)
		if (R_VRIKIsDescendant (skeleton, joint, root))
		{
			vec3_t origin;
			R_VRIKMatrixOrigin (palette[joint], origin);
			VectorAdd (origin, delta, origin);
			R_VRIKSetMatrixOrigin (palette[joint], origin);
		}
}

static void R_VRIKRotationDifference (const float desired[12],
	const float current[12], float delta[12])
{
	int row, column, k;

	memset (delta, 0, sizeof (float) * 12);
	for (row = 0; row < 3; row++)
		for (column = 0; column < 3; column++)
			for (k = 0; k < 3; k++)
				delta[row * 4 + column] += desired[row * 4 + k] *
					current[column * 4 + k];
}

static void R_VRIKRotateSubtree (const md5_skeleton_view_t *skeleton,
	float (*palette)[12], int root, const vec3_t pivot, const float delta[12])
{
	int joint;

	for (joint = 0; (size_t)joint < skeleton->joint_count; joint++)
		if (R_VRIKIsDescendant (skeleton, joint, root))
		{
			float result[12];
			vec3_t oldorigin, relative, neworigin;
			R_VRIKMatrixOrigin (palette[joint], oldorigin);
			VectorSubtract (oldorigin, pivot, relative);
			neworigin[0] = pivot[0] + delta[0] * relative[0] +
				delta[1] * relative[1] + delta[2] * relative[2];
			neworigin[1] = pivot[1] + delta[4] * relative[0] +
				delta[5] * relative[1] + delta[6] * relative[2];
			neworigin[2] = pivot[2] + delta[8] * relative[0] +
				delta[9] * relative[1] + delta[10] * relative[2];
			R_VRIKMatrixMultiply (delta, palette[joint], result);
			R_VRIKSetMatrixOrigin (result, neworigin);
			memcpy (palette[joint], result, sizeof (result));
		}
}

static void R_VRIKRotateTowardSubtree (const md5_skeleton_view_t *skeleton,
	float (*palette)[12], int root, const vec3_t pivot,
	const vec3_t from, const vec3_t to)
{
	vec3_t a, b, axis;
	float cosine, sine, one, delta[12] = { 0 };

	VectorCopy (from, a);
	VectorCopy (to, b);
	if (!VectorNormalize (a) || !VectorNormalize (b))
		return;
	cosine = CLAMP (-1.0f, DotProduct (a, b), 1.0f);
	CrossProduct (a, b, axis);
	sine = VectorNormalize (axis);
	if (sine < 0.0001f)
	{
		vec3_t fallbackaxis;
		if (cosine > 0.0f)
			return;
		axis[0] = 0.0f; axis[1] = 0.0f; axis[2] = 1.0f;
		if (fabsf (DotProduct (axis, a)) > 0.9f)
			axis[0] = 0.0f, axis[1] = 1.0f, axis[2] = 0.0f;
		CrossProduct (a, axis, fallbackaxis);
		VectorCopy (fallbackaxis, axis);
		if (!VectorNormalize (axis))
			return;
		sine = 0.0f;
	}
	one = 1.0f - cosine;
	delta[0] = cosine + axis[0] * axis[0] * one;
	delta[1] = axis[0] * axis[1] * one - axis[2] * sine;
	delta[2] = axis[0] * axis[2] * one + axis[1] * sine;
	delta[4] = axis[1] * axis[0] * one + axis[2] * sine;
	delta[5] = cosine + axis[1] * axis[1] * one;
	delta[6] = axis[1] * axis[2] * one - axis[0] * sine;
	delta[8] = axis[2] * axis[0] * one - axis[1] * sine;
	delta[9] = axis[2] * axis[1] * one + axis[0] * sine;
	delta[10] = cosine + axis[2] * axis[2] * one;
	R_VRIKRotateSubtree (skeleton, palette, root, pivot, delta);
}

static void R_VRIKOrientSubtree (const md5_skeleton_view_t *skeleton,
	float (*palette)[12], int root, const float desired[12])
{
	vec3_t origin;
	float delta[12];

	R_VRIKMatrixOrigin (palette[root], origin);
	R_VRIKRotationDifference (desired, palette[root], delta);
	R_VRIKRotateSubtree (skeleton, palette, root, origin, delta);
}

/* Copied from the inherited mirrored-leg policy. Constrain only after the
 * pole has been projected onto the final target plane, retaining sagittal bend. */
static void R_VRIKConstrainPoleOutward (vec3_t pole, const vec3_t toward,
	const vec3_t outward)
{
	vec3_t projected, corrected;
	float component;
	if (!R_VRIKFinite3 (outward))
		return;
	VectorMA (outward, -DotProduct (outward, toward), toward, projected);
	if (!VectorNormalize (projected))
		return;
	component = DotProduct (pole, projected);
	if (component < 0.0f)
	{
		VectorMA (pole, -2.0f * component, projected, corrected);
		VectorCopy (corrected, pole);
	}
	else if (component < 0.01f)
	{
		VectorMA (pole, 0.01f - component, projected, corrected);
		if (VectorNormalize (corrected))
			VectorCopy (corrected, pole);
	}
}

static qboolean R_VRIKBuildMirroredLegPoles (const float (*palette)[12],
	const r_vrik_leg_rig_t legs[2], const vec3_t lateral, vec3_t poles[2])
{
	vec3_t roots[2], knees[2], source[2], reflected, combined, corrected, sagittal;
	float component;
	for (int side = 0; side < 2; ++side)
	{
		if (!legs[side].valid)
			return false;
		R_VRIKMatrixOrigin (palette[legs[side].upper], roots[side]);
		R_VRIKMatrixOrigin (palette[legs[side].lower], knees[side]);
		if (!R_VRIKFinite3 (roots[side]) || !R_VRIKFinite3 (knees[side]))
			return false;
		VectorSubtract (knees[side], roots[side], source[side]);
		if (!VectorNormalize (source[side]))
		{
			VectorCopy (legs[side].bind_knee_vector, source[side]);
			if (!VectorNormalize (source[side]))
				return false;
		}
	}
	VectorMA (source[1], -2.0f * DotProduct (source[1], lateral), lateral, reflected);
	VectorAdd (source[0], reflected, combined);
	if (!VectorNormalize (combined))
	{
		VectorCopy (source[0], combined);
		if (!VectorNormalize (combined))
			return false;
	}
	component = DotProduct (combined, lateral);
	if (component > 0.0f)
	{
		VectorMA (combined, -2.0f * component, lateral, corrected);
		VectorCopy (corrected, combined);
	}
	component = DotProduct (combined, lateral);
	if (fabsf (component) < 0.01f)
	{
		VectorMA (combined, -component, lateral, sagittal);
		VectorMA (sagittal, -0.01f, lateral, combined);
		if (!VectorNormalize (combined))
			return false;
	}
	VectorCopy (combined, poles[0]);
	VectorMA (combined, -2.0f * DotProduct (combined, lateral), lateral, poles[1]);
	return R_VRIKFinite3 (poles[0]) && R_VRIKFinite3 (poles[1]);
}

/* Donor's bounded two-bone solve, applied to the animation palette for this
 * frame.  Canonical Ranger calls the donor's ANIMATED pole policy; its mirrored
 * paired policy is opt-in for avatar-profile leg repair and is not selected by
 * the Ranger path.  Bind vectors resolve collapsed frames without retained
 * rig state. */
static qboolean R_VRIKSolveLeg (const md5_skeleton_view_t *skeleton,
	float (*palette)[12], const r_vrik_leg_rig_t *rig,
	const vec3_t supplied_target, float confidence,
	const vec3_t supplied_pole, const vec3_t outward)
{
	vec3_t hip, oldknee, oldfoot, target, toward, pole, knee, correction;
	vec3_t oldupperdir, oldlowerdir, newupperdir, newlowerdir;
	float upperlength, lowerlength, distance, cosine, anglecos, anglesin;

	if (!rig || !rig->valid || !R_VRIKFinite3 (supplied_target) ||
		!isfinite (confidence))
		return false;
	confidence = CLAMP (0.0f, confidence, 1.0f);
	if (confidence <= 0.0f)
		return true;
	R_VRIKMatrixOrigin (palette[rig->upper], hip);
	R_VRIKMatrixOrigin (palette[rig->lower], oldknee);
	R_VRIKMatrixOrigin (palette[rig->foot], oldfoot);
	VectorSubtract (oldknee, hip, oldupperdir);
	VectorSubtract (oldfoot, oldknee, oldlowerdir);
	upperlength = VectorLength (oldupperdir);
	lowerlength = VectorLength (oldlowerdir);
	if (!isfinite (upperlength) || upperlength < 0.01f)
	{
		VectorCopy (rig->bind_knee_vector, oldupperdir);
		upperlength = rig->bind_upper_length;
	}
	if (!isfinite (lowerlength) || lowerlength < 0.01f)
	{
		VectorCopy (rig->bind_foot_vector, oldlowerdir);
		lowerlength = rig->bind_lower_length;
	}
	if (upperlength < 0.01f || lowerlength < 0.01f)
		return false;
	VectorSubtract (supplied_target, oldfoot, target);
	VectorMA (oldfoot, confidence, target, target);
	VectorSubtract (target, hip, toward);
	distance = VectorLength (toward);
	if (!isfinite (distance))
		return false;
	if (distance < 0.001f)
	{
		VectorCopy (oldupperdir, toward);
		if (!VectorNormalize (toward))
			return false;
		distance = 0.0f;
	}
	else
		VectorScale (toward, 1.0f / distance, toward);
	distance = CLAMP (fabsf (upperlength - lowerlength) + 0.01f, distance,
		q_max (fabsf (upperlength - lowerlength) + 0.01f,
			upperlength + lowerlength - 0.01f));
	VectorMA (hip, distance, toward, target);
	if (supplied_pole && R_VRIKFinite3 (supplied_pole))
		VectorCopy (supplied_pole, pole);
	else
		VectorCopy (oldupperdir, pole);
	VectorMA (pole, -DotProduct (pole, toward), toward, pole);
	if (!VectorNormalize (pole))
	{
		VectorCopy (rig->bind_knee_vector, pole);
		VectorMA (pole, -DotProduct (pole, toward), toward, pole);
		if (!VectorNormalize (pole))
		{
			pole[0] = 0.0f; pole[1] = 0.0f; pole[2] = 1.0f;
			if (fabsf (DotProduct (pole, toward)) > 0.9f)
				pole[0] = 0.0f, pole[1] = 1.0f, pole[2] = 0.0f;
			VectorMA (pole, -DotProduct (pole, toward), toward, pole);
			if (!VectorNormalize (pole))
				return false;
		}
	}
	if (outward)
		R_VRIKConstrainPoleOutward (pole, toward, outward);
	cosine = CLAMP (-1.0f,
		(upperlength * upperlength + distance * distance - lowerlength * lowerlength) /
		(2.0f * upperlength * distance), 1.0f);
	anglecos = cosine * upperlength;
	anglesin = sqrtf (q_max (0.0f, 1.0f - cosine * cosine)) * upperlength;
	VectorMA (hip, anglecos, toward, knee);
	VectorMA (knee, anglesin, pole, knee);
	VectorSubtract (knee, hip, newupperdir);
	R_VRIKRotateTowardSubtree (skeleton, palette, rig->upper, hip,
		oldupperdir, newupperdir);
	R_VRIKMatrixOrigin (palette[rig->lower], oldknee);
	VectorSubtract (knee, oldknee, correction);
	R_VRIKTranslateSubtree (skeleton, palette, rig->lower, correction);
	R_VRIKMatrixOrigin (palette[rig->lower], oldknee);
	R_VRIKMatrixOrigin (palette[rig->foot], oldfoot);
	VectorSubtract (oldfoot, oldknee, oldlowerdir);
	VectorSubtract (target, oldknee, newlowerdir);
	R_VRIKRotateTowardSubtree (skeleton, palette, rig->lower, oldknee,
		oldlowerdir, newlowerdir);
	return true;
}

static qboolean R_VRIKModelPaletteRigid (const float (*palette)[12], size_t count)
{
	for (size_t joint = 0; joint < count; ++joint)
	{
		const float *m = palette[joint];
		vec3_t x = {m[0], m[4], m[8]}, y = {m[1], m[5], m[9]}, z = {m[2], m[6], m[10]}, cross;
		for (int component = 0; component < 12; ++component)
			if (!isfinite (m[component]))
				return false;
		CrossProduct (x, y, cross);
		/* Match the existing avatar boundary's finite rigid-matrix tolerance. */
		if (fabsf (DotProduct (x, x) - 1.0f) >= 0.02f ||
			fabsf (DotProduct (y, y) - 1.0f) >= 0.02f ||
			fabsf (DotProduct (z, z) - 1.0f) >= 0.02f ||
			fabsf (DotProduct (x, y)) >= 0.02f || fabsf (DotProduct (x, z)) >= 0.02f ||
			fabsf (DotProduct (y, z)) >= 0.02f || fabsf (DotProduct (cross, z) - 1.0f) >= 0.03f)
			return false;
	}
	return true;
}

unsigned char R_VRIKRefineModelFeet (const md5_skeleton_view_t *skeleton,
	int hip, const int joints[2][3], unsigned char usable_mask,
	const vec3_t goals[2], const float confidence[2], qboolean mirrored_poles,
	float (*palette)[12], size_t capacity)
{
	r_vrik_leg_rig_t legs[2];
	float saved[R_VRIK_MAX_JOINTS][12], footbasis[2][12];
	vec3_t roots[2], lateral, outward[2], poles[2];
	qboolean have_axis = false, have_pair = false;
	unsigned char committed = 0;
	size_t bytes;
	usable_mask &= 3;
	if (!usable_mask || !skeleton || !skeleton->joints || !skeleton->joint_count ||
		skeleton->joint_count > R_VRIK_MAX_JOINTS || capacity < skeleton->joint_count ||
		!joints || !goals || !confidence || !palette ||
		!R_VRIKModelPaletteRigid ((const float (*)[12])palette, skeleton->joint_count))
		return 0;
	bytes = skeleton->joint_count * sizeof (*palette);
	for (int side = 0; side < 2; ++side)
	{
		R_VRIKInitLegRig (skeleton, hip, joints[side][0], joints[side][1],
			joints[side][2], &legs[side]);
		if (legs[side].valid)
			memcpy (footbasis[side], palette[legs[side].foot], sizeof (footbasis[side]));
	}
	/* A batch cannot promise independent rollback for overlapping branches. */
	if (legs[0].valid && legs[1].valid &&
		(R_VRIKIsDescendant (skeleton, legs[0].upper, legs[1].upper) ||
		 R_VRIKIsDescendant (skeleton, legs[1].upper, legs[0].upper)))
		return 0;
	if (mirrored_poles && joints[0][0] >= 0 && joints[1][0] >= 0 &&
		(size_t)joints[0][0] < skeleton->joint_count &&
		(size_t)joints[1][0] < skeleton->joint_count)
	{
		R_VRIKMatrixOrigin (palette[joints[0][0]], roots[0]);
		R_VRIKMatrixOrigin (palette[joints[1][0]], roots[1]);
		VectorSubtract (roots[1], roots[0], lateral);
		have_axis = R_VRIKFinite3 (lateral) && VectorNormalize (lateral) != 0.0f;
		if (!have_axis)
		{
			/* The inherited policy still has an outward hemisphere when the
			 * current roots coincide: use their authored bind separation. */
			R_VRIKMatrixOrigin (skeleton->joints[joints[0][0]].bind, roots[0]);
			R_VRIKMatrixOrigin (skeleton->joints[joints[1][0]].bind, roots[1]);
			VectorSubtract (roots[1], roots[0], lateral);
			have_axis = R_VRIKFinite3 (roots[0]) && R_VRIKFinite3 (roots[1]) &&
				R_VRIKFinite3 (lateral) && VectorNormalize (lateral) != 0.0f;
		}
		if (have_axis)
		{
			VectorScale (lateral, -1.0f, outward[0]);
			VectorCopy (lateral, outward[1]);
			have_pair = R_VRIKBuildMirroredLegPoles ((const float (*)[12])palette,
				legs, lateral, poles);
		}
	}
	for (int side = 0; side < 2; ++side)
	{
		if (!(usable_mask & (1u << side)) || !legs[side].valid)
			continue;
		memcpy (saved, palette, bytes);
		if (!R_VRIKSolveLeg (skeleton, palette, &legs[side], goals[side], confidence[side],
			have_pair ? poles[side] : NULL, have_axis ? outward[side] : NULL))
		{
			memcpy (palette, saved, bytes);
			continue;
		}
		if (confidence[side] <= 0.0f)
		{
			/* Accepted zero-confidence input is an exact positional/basis no-op. */
			committed |= 1u << side;
			continue;
		}
		/* Restore the authored/retargeted basis around the solved origin, carrying
		 * descendants. Copying the complete old matrix would undo foot placement. */
		R_VRIKOrientSubtree (skeleton, palette, legs[side].foot, footbasis[side]);
		{
			vec3_t origin;
			R_VRIKMatrixOrigin (palette[legs[side].foot], origin);
			memcpy (palette[legs[side].foot], footbasis[side], sizeof (footbasis[side]));
			R_VRIKSetMatrixOrigin (palette[legs[side].foot], origin);
		}
		if (!R_VRIKModelPaletteRigid ((const float (*)[12])palette, skeleton->joint_count))
			memcpy (palette, saved, bytes);
		else
			committed |= 1u << side;
	}
	return committed;
}

static void R_VRIKApplyLowerTargets (const md5_skeleton_view_t *skeleton,
	int hipindex, const r_vrik_lowerbody_targets_t *targets,
	const vec3_t lateral, const vec3_t forward, const vec3_t up,
	float (*palette)[12])
{
	r_vrik_leg_rig_t legs[2];
	unsigned char usable;
	int role, side;

	if (!targets)
		return;
	usable = targets->present_mask &
		(targets->tracked_mask | targets->predicted_mask) &
		((1u << R_VRIK_LOWER_ROLE_COUNT) - 1u);
	if (usable & R_VRIK_LOWER_BIT (R_VRIK_LOWER_HIP))
	{
		vec3_t target, current, delta;
		float distance, confidence;
		role = R_VRIK_LOWER_HIP;
		if (R_VRIKFinite3 (targets->position[role]) &&
			R_VRIKFinite3 (targets->orientation[role]) &&
			isfinite (targets->confidence[role]))
		{
			confidence = CLAMP (0.0f, targets->confidence[role], 1.0f);
			R_VRIKLocalVectorToModel (targets->position[role], lateral, forward,
				up, target);
			R_VRIKMatrixOrigin (palette[hipindex], current);
			VectorSubtract (target, current, delta);
			distance = VectorLength (delta);
			if (isfinite (distance))
			{
				if (distance > 96.0f)
				{
					VectorScale (delta, 96.0f / distance, delta);
				}
				VectorScale (delta, confidence, delta);
				R_VRIKTranslateSubtree (skeleton, palette, hipindex, delta);
				{
					float desired[12];
					R_VRIKAnglesToModelMatrix (targets->orientation[role],
						lateral, forward, up, target, desired);
					R_VRIKOrientSubtree (skeleton, palette, hipindex, desired);
				}
			}
		}
	}
	if (!(usable & (R_VRIK_LOWER_BIT (R_VRIK_LOWER_LEFT_FOOT) |
		R_VRIK_LOWER_BIT (R_VRIK_LOWER_RIGHT_FOOT))))
		return;
	R_VRIKResolveLegRig (skeleton, hipindex, 0, &legs[0]);
	R_VRIKResolveLegRig (skeleton, hipindex, 1, &legs[1]);
	for (side = 0; side < 2; side++)
	{
		role = side ? R_VRIK_LOWER_RIGHT_FOOT : R_VRIK_LOWER_LEFT_FOOT;
		if (legs[side].valid && (usable & R_VRIK_LOWER_BIT (role)) &&
			R_VRIKFinite3 (targets->position[role]) &&
			R_VRIKFinite3 (targets->orientation[role]) &&
			isfinite (targets->confidence[role]))
		{
			vec3_t target;
			float desired[12];
			R_VRIKLocalVectorToModel (targets->position[role], lateral,
				forward, up, target);
			R_VRIKSolveLeg (skeleton, palette, &legs[side], target,
				targets->confidence[role], NULL, NULL);
			R_VRIKAnglesToModelMatrix (targets->orientation[role], lateral,
				forward, up, target, desired);
			R_VRIKOrientSubtree (skeleton, palette, legs[side].foot, desired);
		}
	}
}

static void R_VRIKSolveArm (const int *jointindex, float (*palette)[12],
	qboolean rightside, const vec3_t target, const vec3_t targetangles,
	const vec3_t lateral, const vec3_t forward, const vec3_t up)
{
	int upperindex = jointindex[rightside ? R_VRIK_UPPERARM_R : R_VRIK_UPPERARM_L];
	int lowerindex = jointindex[rightside ? R_VRIK_LOWERARM_R : R_VRIK_LOWERARM_L];
	int handindex = jointindex[rightside ? R_VRIK_HAND_R : R_VRIK_HAND_L];
	float *upper = palette[upperindex];
	float *lower = palette[lowerindex];
	float *hand = palette[handindex];
	float tracked[12], desiredhand[12];
	vec3_t wrist, shoulder, oldelbow, oldhand, toward, pole, bendnormal, bendaxis;
	vec3_t elbow, oldupperdir, oldlowerdir, newupperdir, newlowerdir;
	float upperlength, lowerlength, distance, restreach, stretch;
	float solveupper, solvelower, cosine, anglecos, anglesin;

	VectorCopy (target, wrist);
	R_VRIKMatrixOrigin (upper, shoulder);
	R_VRIKMatrixOrigin (lower, oldelbow);
	R_VRIKMatrixOrigin (hand, oldhand);
	VectorSubtract (oldelbow, shoulder, oldupperdir);
	VectorSubtract (oldhand, oldelbow, oldlowerdir);
	upperlength = VectorLength (oldupperdir);
	lowerlength = VectorLength (oldlowerdir);
	if (upperlength < 0.01f || lowerlength < 0.01f)
		return;
	VectorSubtract (wrist, shoulder, toward);
	distance = VectorLength (toward);
	if (distance < 0.001f)
		return;
	VectorScale (toward, 1.0f / distance, toward);
	restreach = upperlength + lowerlength;
	distance = CLAMP (fabsf (upperlength - lowerlength) + 0.01f, distance,
		restreach * VRIK_ARM_MAX_STRETCH);
	VectorMA (shoulder, distance, toward, wrist);
	stretch = distance > restreach ? distance / restreach : 1.0f;
	solveupper = upperlength * stretch;
	solvelower = lowerlength * stretch;
	VectorScale (lateral, rightside ? 1.0f : -1.0f, pole);
	VectorMA (pole, -0.35f, forward, pole);
	CrossProduct (toward, pole, bendnormal);
	if (!VectorNormalize (bendnormal))
		VectorCopy (up, bendnormal);
	CrossProduct (bendnormal, toward, bendaxis);
	VectorNormalize (bendaxis);
	cosine = CLAMP (-1.0f,
		(solveupper * solveupper + distance * distance - solvelower * solvelower) /
		(2.0f * solveupper * distance), 1.0f);
	anglecos = cosine * solveupper;
	anglesin = sqrtf (q_max (0.0f, 1.0f - cosine * cosine)) * solveupper;
	VectorMA (shoulder, anglecos, toward, elbow);
	VectorMA (elbow, anglesin, bendaxis, elbow);
	VectorSubtract (elbow, shoulder, newupperdir);
	VectorSubtract (wrist, elbow, newlowerdir);
	R_VRIKRotateToward (upper, oldupperdir, newupperdir);
	R_VRIKRotateToward (lower, oldlowerdir, newlowerdir);
	R_VRIKSetMatrixOrigin (lower, elbow);
	R_VRIKAnglesToModelMatrix (targetangles, lateral, forward, up, wrist, tracked);
	R_VRIKTrackedItemMatrix (tracked, wrist, desiredhand);
	memcpy (hand, desiredhand, sizeof (desiredhand));
}

static void R_VRIKMoveJoint (const int *jointindex, float (*palette)[12],
	int semantic, const vec3_t delta, float scale)
{
	int index = jointindex[semantic];
	vec3_t origin;
	R_VRIKMatrixOrigin (palette[index], origin);
	VectorMA (origin, scale, delta, origin);
	R_VRIKSetMatrixOrigin (palette[index], origin);
}

static qboolean R_VRIKSolvePalette (const md5_skeleton_view_t *skeleton,
	const int *jointindex, const vrik_pose_t *pose,
	const r_vrik_lowerbody_targets_t *lower_targets, qboolean muzzleflash,
	float (*palette)[12], qboolean *muzzle_valid,
	vec3_t muzzle_origin, vec3_t muzzle_forward, vec3_t body_basis[3])
{
	vec3_t lateral, forward, up, hip, oldhead, targethead, headdelta, torso;
	vec3_t lefttarget, righttarget, weaponhandlocal = { 0, 0, 0 };
	float weaponhand[12], inverse[12], attached[12];
	int headindex = jointindex[R_VRIK_HEAD];
	int hipindex = jointindex[R_VRIK_HIP];
	int handright = jointindex[R_VRIK_HAND_R];
	int handleft = jointindex[R_VRIK_HAND_L];
	int gun = jointindex[R_VRIK_GUN];
	int axe = jointindex[R_VRIK_AXE];
	int flames[2] = { jointindex[R_VRIK_SMALL_FLAME],
		jointindex[R_VRIK_BIG_FLAME] };
	int weapon = -1;
	qboolean dominantleft = (pose->flags & VRIK_FLAG_DOMINANT_LEFT) != 0;

	*muzzle_valid = false;
	muzzle_origin[0] = muzzle_origin[1] = muzzle_origin[2] = 0.0f;
	muzzle_forward[0] = muzzle_forward[1] = muzzle_forward[2] = 0.0f;
	if (!R_VRIKBuildBodyBasis (jointindex, (const float (*)[12])palette,
		lateral, forward, up))
		return false;
	VectorCopy (lateral, body_basis[0]);
	VectorCopy (forward, body_basis[1]);
	VectorCopy (up, body_basis[2]);
	/* Lower-body network targets are restricted to the donor's byte-verified
	 * rerelease Ranger rig.  The inherited head/arm solve below still runs for
	 * ordinary compatible MD5 models. */
	if (skeleton->from_rerelease)
		R_VRIKApplyLowerTargets (skeleton, hipindex, lower_targets,
			lateral, forward, up, palette);
	if (handright >= 0 && (gun >= 0 || axe >= 0))
	{
		vec3_t handorigin, gunorigin, axeorigin;
		float gundistance = FLT_MAX, axedistance = FLT_MAX;
		R_VRIKMatrixOrigin (palette[handright], handorigin);
		if (gun >= 0)
		{
			R_VRIKMatrixOrigin (palette[gun], gunorigin);
			VectorSubtract (gunorigin, handorigin, gunorigin);
			gundistance = VectorLength (gunorigin);
		}
		if (axe >= 0)
		{
			R_VRIKMatrixOrigin (palette[axe], axeorigin);
			VectorSubtract (axeorigin, handorigin, axeorigin);
			axedistance = VectorLength (axeorigin);
		}
		weapon = gundistance <= axedistance ? gun : axe;
		if (q_min (gundistance, axedistance) > 64.0f)
			weapon = -1;
		if (weapon >= 0)
		{
			R_VRIKMatrixInverseRigid (palette[weapon], inverse);
			R_VRIKMatrixMultiply (inverse, palette[handright], weaponhand);
			R_VRIKMatrixOrigin (weaponhand, weaponhandlocal);
		}
	}

	R_VRIKMatrixOrigin (palette[hipindex], hip);
	R_VRIKMatrixOrigin (palette[headindex], oldhead);
	R_VRIKLocalVectorToModel (pose->position[VRIK_TRACKER_HEAD], lateral,
		forward, up, targethead);
	VectorSubtract (oldhead, hip, torso);
	{
		float restreach = VectorLength (torso);
		float targetreach;
		VectorSubtract (targethead, hip, torso);
		targetreach = VectorLength (torso);
		if (restreach > 0.01f && targetreach > restreach * VRIK_ARM_MAX_STRETCH)
		{
			VectorScale (torso, 1.0f / targetreach, torso);
			VectorMA (hip, restreach * VRIK_ARM_MAX_STRETCH, torso, targethead);
		}
	}
	VectorSubtract (targethead, oldhead, headdelta);
	if (VectorLength (headdelta) > 24.0f)
	{
		VectorNormalize (headdelta);
		VectorScale (headdelta, 24.0f, headdelta);
		VectorAdd (oldhead, headdelta, targethead);
	}
	R_VRIKMoveJoint (jointindex, palette, R_VRIK_SPINE1, headdelta, 0.12f);
	R_VRIKMoveJoint (jointindex, palette, R_VRIK_SPINE2, headdelta, 0.32f);
	R_VRIKMoveJoint (jointindex, palette, R_VRIK_NECK, headdelta, 0.68f);
	R_VRIKMoveJoint (jointindex, palette, R_VRIK_HEAD, headdelta, 1.0f);
	R_VRIKMoveJoint (jointindex, palette, R_VRIK_SHOULDER_L, headdelta, 0.32f);
	R_VRIKMoveJoint (jointindex, palette, R_VRIK_SHOULDER_R, headdelta, 0.32f);
	R_VRIKMoveJoint (jointindex, palette, R_VRIK_UPPERARM_L, headdelta, 0.32f);
	R_VRIKMoveJoint (jointindex, palette, R_VRIK_UPPERARM_R, headdelta, 0.32f);
	{
		float body[12], target[12], correction[12], result[12];
		vec3_t bodyforward, headorigin;
		VectorCopy (forward, bodyforward);
		R_VRIKCanonicalMatrix (bodyforward, up, oldhead, body);
		R_VRIKMatrixInverseRigid (body, inverse);
		R_VRIKMatrixMultiply (inverse, palette[headindex], correction);
		R_VRIKMatrixOrigin (palette[headindex], headorigin);
		R_VRIKAnglesToModelMatrix (pose->orientation[VRIK_TRACKER_HEAD],
			lateral, forward, up, headorigin, target);
		R_VRIKMatrixMultiply (target, correction, result);
		R_VRIKSetMatrixOrigin (result, headorigin);
		memcpy (palette[headindex], result, sizeof (result));
	}
	if (pose->flags & VRIK_FLAG_LEFT_HAND_TRACKED)
	{
		R_VRIKLocalVectorToModel (pose->position[VRIK_TRACKER_LEFT_HAND], lateral,
			forward, up, lefttarget);
		R_VRIKSolveArm (jointindex, palette, false, lefttarget,
			pose->orientation[VRIK_TRACKER_LEFT_HAND], lateral, forward, up);
	}
	if (pose->flags & VRIK_FLAG_RIGHT_HAND_TRACKED)
	{
		R_VRIKLocalVectorToModel (pose->position[VRIK_TRACKER_RIGHT_HAND], lateral,
			forward, up, righttarget);
		R_VRIKSolveArm (jointindex, palette, true, righttarget,
			pose->orientation[VRIK_TRACKER_RIGHT_HAND], lateral, forward, up);
	}
	if (weapon >= 0)
	{
		int destination = dominantleft ? handleft : handright;
		if (destination >= 0 &&
			((dominantleft && (pose->flags & VRIK_FLAG_LEFT_HAND_TRACKED)) ||
			 (!dominantleft && (pose->flags & VRIK_FLAG_RIGHT_HAND_TRACKED))))
		{
			float tracked[12];
			vec3_t handorigin, weaponorigin;
			if (weapon == axe)
			{
				int tracker = dominantleft ? VRIK_TRACKER_LEFT_HAND :
					VRIK_TRACKER_RIGHT_HAND;
				R_VRIKAnglesToModelMatrix (pose->orientation[tracker], lateral,
					forward, up, vec3_origin, tracked);
				R_VRIKTrackedAxeMatrix (tracked, vec3_origin, attached);
			}
			else
			{
				R_VRIKAnglesToModelMatrix (pose->aim_orientation, lateral, forward,
					up, vec3_origin, tracked);
				R_VRIKTrackedItemMatrix (tracked, vec3_origin, attached);
			}
			R_VRIKMatrixOrigin (palette[destination], handorigin);
			weaponorigin[0] = handorigin[0] -
				(attached[0] * weaponhandlocal[0] + attached[1] * weaponhandlocal[1] +
				 attached[2] * weaponhandlocal[2]);
			weaponorigin[1] = handorigin[1] -
				(attached[4] * weaponhandlocal[0] + attached[5] * weaponhandlocal[1] +
				 attached[6] * weaponhandlocal[2]);
			weaponorigin[2] = handorigin[2] -
				(attached[8] * weaponhandlocal[0] + attached[9] * weaponhandlocal[1] +
				 attached[10] * weaponhandlocal[2]);
			R_VRIKSetMatrixOrigin (attached, weaponorigin);
			if (!dominantleft)
			{
				float correctedhand[12];
				R_VRIKMatrixMultiply (attached, weaponhand, correctedhand);
				R_VRIKSetMatrixOrigin (correctedhand, handorigin);
				memcpy (palette[destination], correctedhand, sizeof (correctedhand));
			}
			memcpy (palette[weapon], attached, sizeof (attached));
			if (weapon == gun)
			{
				vec3_t gunorigin;
				int flame;
				R_VRIKMatrixOrigin (attached, gunorigin);
				muzzle_forward[0] = attached[1];
				muzzle_forward[1] = attached[5];
				muzzle_forward[2] = attached[9];
				*muzzle_valid = VectorNormalize (muzzle_forward) != 0.0f;
				VectorMA (gunorigin, VRIK_GUN_MUZZLE_OFFSET,
					muzzle_forward, muzzle_origin);
				if (muzzleflash)
					for (flame = 0; flame < 2; flame++)
						if (flames[flame] >= 0)
						{
							memcpy (palette[flames[flame]], attached, sizeof (attached));
							R_VRIKSetMatrixOrigin (palette[flames[flame]], muzzle_origin);
						}
			}
		}
	}
	return true;
}

static qboolean R_VRIKFinite3 (const vec3_t vector)
{
	return isfinite (vector[0]) && isfinite (vector[1]) && isfinite (vector[2]);
}

static qboolean R_VRIKPoseValid (const vrik_pose_t *pose)
{
	int tracker;
	if (!(pose->flags & VRIK_FLAG_ACTIVE) ||
		!(pose->flags & VRIK_FLAG_HEAD_TRACKED) ||
		(pose->flags & (unsigned char)~VRIK_FLAG_KNOWN))
		return false;
	if (!R_VRIKFinite3 (pose->position[VRIK_TRACKER_HEAD]) ||
		!R_VRIKFinite3 (pose->orientation[VRIK_TRACKER_HEAD]))
		return false;
	for (tracker = VRIK_TRACKER_LEFT_HAND; tracker <= VRIK_TRACKER_RIGHT_HAND;
		tracker++)
	{
		unsigned char flag = tracker == VRIK_TRACKER_LEFT_HAND ?
			VRIK_FLAG_LEFT_HAND_TRACKED : VRIK_FLAG_RIGHT_HAND_TRACKED;
		if ((pose->flags & flag) &&
			(!R_VRIKFinite3 (pose->position[tracker]) ||
			 !R_VRIKFinite3 (pose->orientation[tracker])))
			return false;
	}
	if (!R_VRIKFinite3 (pose->aim_orientation))
		return false;
	return true;
}

static r_vrik_palette_result_t R_VRIKValidateAnimationInput (
	const md5_skeleton_view_t *skeleton, const lerpdata_t *lerpdata,
	const r_vrik_palette_output_t *out)
{
	size_t joint, component;

	if (!skeleton || !lerpdata || !out || !out->matrices)
		return R_VRIK_PALETTE_INVALID_ARGUMENT;
	if (!skeleton->joints || !skeleton->absolute_poses ||
		!skeleton->joint_count || !skeleton->pose_count ||
		skeleton->joint_count > R_VRIK_MAX_JOINTS ||
		skeleton->joint_count > SIZE_MAX / skeleton->pose_count ||
		lerpdata->pose1 < 0 || lerpdata->pose2 < 0 ||
		(size_t)lerpdata->pose1 >= skeleton->pose_count ||
		(size_t)lerpdata->pose2 >= skeleton->pose_count ||
		!isfinite (lerpdata->blend) || lerpdata->blend < 0.0f ||
		lerpdata->blend > 1.0f)
		return R_VRIK_PALETTE_INVALID_SKELETON;
	if (out->capacity < skeleton->joint_count)
		return R_VRIK_PALETTE_INSUFFICIENT_CAPACITY;
	for (joint = 0; joint < skeleton->joint_count; joint++)
	{
		const md5_skeleton_joint_t *info = &skeleton->joints[joint];
		if (info->parent < -1 || info->poseparent < -1 ||
			(info->parent >= 0 && (size_t)info->parent >= skeleton->joint_count) ||
			(info->poseparent >= 0 && (size_t)info->poseparent >= joint))
			return R_VRIK_PALETTE_INVALID_SKELETON;
		for (component = 0; component < 12; component++)
			if (!isfinite (skeleton->absolute_poses[(size_t)lerpdata->pose1 *
				skeleton->joint_count + joint][component]) ||
				!isfinite (skeleton->absolute_poses[(size_t)lerpdata->pose2 *
				skeleton->joint_count + joint][component]))
				return R_VRIK_PALETTE_INVALID_SKELETON;
	}
	return R_VRIK_PALETTE_OK;
}

r_vrik_palette_result_t R_VRIKBuildRangerPalette (
	const md5_skeleton_view_t *skeleton, const lerpdata_t *lerpdata,
	const vrik_pose_t *pose, const r_vrik_lowerbody_targets_t *lower_targets,
	qboolean muzzleflash,
	r_vrik_palette_output_t *out)
{
	int jointindex[R_VRIK_JOINT_COUNT];
	float palette[R_VRIK_MAX_JOINTS][12];
	size_t joint, component;
	qboolean muzzle_valid;
	vec3_t muzzle_origin, muzzle_forward;
	vec3_t body_basis[3];
	r_vrik_palette_result_t validation;

	if (!pose)
		return R_VRIK_PALETTE_INVALID_ARGUMENT;
	validation = R_VRIKValidateAnimationInput (skeleton, lerpdata, out);
	if (validation != R_VRIK_PALETTE_OK)
		return validation;
	if (!R_VRIKPoseValid (pose))
		return R_VRIK_PALETTE_INVALID_POSE;
	if (!R_VRIKResolveJoints (skeleton, jointindex))
		return R_VRIK_PALETTE_INVALID_SKELETON;
	R_VRIKLerpPalette (skeleton, lerpdata, palette);
	if (!R_VRIKSolvePalette (skeleton, jointindex, pose, lower_targets,
		muzzleflash, palette,
		&muzzle_valid, muzzle_origin, muzzle_forward, body_basis))
		return R_VRIK_PALETTE_INVALID_SKELETON;
	for (joint = 0; joint < skeleton->joint_count; joint++)
		for (component = 0; component < 12; component++)
			if (!isfinite (palette[joint][component]))
				return R_VRIK_PALETTE_INVALID_POSE;
	memcpy (out->matrices, palette, skeleton->joint_count * sizeof (*palette));
	out->joint_count = skeleton->joint_count;
	out->muzzle_valid = muzzle_valid;
	VectorCopy (muzzle_origin, out->muzzle_origin);
	VectorCopy (muzzle_forward, out->muzzle_forward);
	out->body_basis_valid = true;
	for (int axis = 0; axis < 3; ++axis)
		VectorCopy (body_basis[axis], out->body_basis[axis]);
	return R_VRIK_PALETTE_OK;
}

r_vrik_palette_result_t R_VRIKBuildRangerAnimationPalette (
	const md5_skeleton_view_t *skeleton, const lerpdata_t *lerpdata,
	r_vrik_palette_output_t *out)
{
	int jointindex[R_VRIK_JOINT_COUNT];
	float palette[R_VRIK_MAX_JOINTS][12];
	r_vrik_palette_result_t validation =
		R_VRIKValidateAnimationInput (skeleton, lerpdata, out);
	if (validation != R_VRIK_PALETTE_OK)
		return validation;
	if (!R_VRIKResolveJoints (skeleton, jointindex))
		return R_VRIK_PALETTE_INVALID_SKELETON;
	R_VRIKLerpPalette (skeleton, lerpdata, palette);
	for (size_t joint = 0; joint < skeleton->joint_count; ++joint)
		for (int component = 0; component < 12; ++component)
			if (!isfinite (palette[joint][component]))
				return R_VRIK_PALETTE_INVALID_SKELETON;
	memcpy (out->matrices, palette, skeleton->joint_count * sizeof (*palette));
	out->joint_count = skeleton->joint_count;
	out->muzzle_valid = false;
	memset (out->muzzle_origin, 0, sizeof (out->muzzle_origin));
	memset (out->muzzle_forward, 0, sizeof (out->muzzle_forward));
	out->body_basis_valid = false;
	memset (out->body_basis, 0, sizeof (out->body_basis));
	return R_VRIK_PALETTE_OK;
}

typedef struct r_vrik_calibration_reference_s
{
	float transform[R_VRIK_LOWER_ROLE_COUNT][12];
	float head_transform[12];
	vec3_t lateral;
	vec3_t forward;
	vec3_t up;
} r_vrik_calibration_reference_t;

static qboolean R_VRIKGetCalibrationReference (qmodel_t *model,
	r_vrik_calibration_reference_t *out)
{
	md5_skeleton_view_t skeleton;
	r_vrik_leg_rig_t legs[2];
	r_vrik_calibration_reference_t reference;
	int jointindex[R_VRIK_JOINT_COUNT];
	int hip, head, leftshoulder, rightshoulder;
	vec3_t hiporigin, headorigin, leftorigin, rightorigin;

	if (!out || !Mod_GetMD5Skeleton (model, &skeleton) ||
		!skeleton.from_rerelease || !R_VRIKResolveJoints (&skeleton, jointindex))
		return false;
	hip = jointindex[R_VRIK_HIP];
	head = jointindex[R_VRIK_HEAD];
	leftshoulder = jointindex[R_VRIK_SHOULDER_L];
	rightshoulder = jointindex[R_VRIK_SHOULDER_R];
	if (!R_VRIKResolveLegRig (&skeleton, hip, 0, &legs[0]) ||
		!R_VRIKResolveLegRig (&skeleton, hip, 1, &legs[1]))
		return false;
	memset (&reference, 0, sizeof (reference));
	memcpy (reference.transform[R_VRIK_LOWER_HIP],
		skeleton.joints[hip].bind, sizeof (skeleton.joints[hip].bind));
	memcpy (reference.transform[R_VRIK_LOWER_LEFT_FOOT],
		skeleton.joints[legs[0].foot].bind, sizeof (skeleton.joints[legs[0].foot].bind));
	memcpy (reference.transform[R_VRIK_LOWER_RIGHT_FOOT],
		skeleton.joints[legs[1].foot].bind, sizeof (skeleton.joints[legs[1].foot].bind));
	memcpy (reference.head_transform, skeleton.joints[head].bind,
		sizeof (skeleton.joints[head].bind));
	R_VRIKMatrixOrigin (skeleton.joints[hip].bind, hiporigin);
	R_VRIKMatrixOrigin (skeleton.joints[head].bind, headorigin);
	R_VRIKMatrixOrigin (skeleton.joints[leftshoulder].bind, leftorigin);
	R_VRIKMatrixOrigin (skeleton.joints[rightshoulder].bind, rightorigin);
	if (!R_VRIKFinite3 (hiporigin) || !R_VRIKFinite3 (headorigin) ||
		!R_VRIKFinite3 (leftorigin) || !R_VRIKFinite3 (rightorigin))
		return false;
	VectorSubtract (rightorigin, leftorigin, reference.lateral);
	VectorSubtract (headorigin, hiporigin, reference.up);
	if (!VectorNormalize (reference.lateral) || !VectorNormalize (reference.up))
		return false;
	CrossProduct (reference.up, reference.lateral, reference.forward);
	if (!VectorNormalize (reference.forward))
		return false;
	CrossProduct (reference.lateral, reference.forward, reference.up);
	if (!VectorNormalize (reference.up))
		return false;
	*out = reference;
	return true;
}

qboolean R_VRIKCalibrationReferenceAvailable (qmodel_t *model)
{
	r_vrik_calibration_reference_t reference;
	return R_VRIKGetCalibrationReference (model, &reference);
}

static qboolean R_VRIKCalibrationMatrixQuaternion (const float matrix[12],
	float quaternion[4])
{
	float trace = matrix[0] + matrix[5] + matrix[10];
	float scale;

	if (trace > 0.0f)
	{
		scale = sqrtf (trace + 1.0f) * 2.0f;
		if (!isfinite (scale) || scale <= 0.0001f) return false;
		quaternion[0] = 0.25f * scale;
		quaternion[1] = (matrix[9] - matrix[6]) / scale;
		quaternion[2] = (matrix[2] - matrix[8]) / scale;
		quaternion[3] = (matrix[4] - matrix[1]) / scale;
	}
	else if (matrix[0] > matrix[5] && matrix[0] > matrix[10])
	{
		scale = sqrtf (1.0f + matrix[0] - matrix[5] - matrix[10]) * 2.0f;
		if (!isfinite (scale) || scale <= 0.0001f) return false;
		quaternion[0] = (matrix[9] - matrix[6]) / scale;
		quaternion[1] = 0.25f * scale;
		quaternion[2] = (matrix[1] + matrix[4]) / scale;
		quaternion[3] = (matrix[2] + matrix[8]) / scale;
	}
	else if (matrix[5] > matrix[10])
	{
		scale = sqrtf (1.0f + matrix[5] - matrix[0] - matrix[10]) * 2.0f;
		if (!isfinite (scale) || scale <= 0.0001f) return false;
		quaternion[0] = (matrix[2] - matrix[8]) / scale;
		quaternion[1] = (matrix[1] + matrix[4]) / scale;
		quaternion[2] = 0.25f * scale;
		quaternion[3] = (matrix[6] + matrix[9]) / scale;
	}
	else
	{
		scale = sqrtf (1.0f + matrix[10] - matrix[0] - matrix[5]) * 2.0f;
		if (!isfinite (scale) || scale <= 0.0001f) return false;
		quaternion[0] = (matrix[4] - matrix[1]) / scale;
		quaternion[1] = (matrix[2] + matrix[8]) / scale;
		quaternion[2] = (matrix[6] + matrix[9]) / scale;
		quaternion[3] = 0.25f * scale;
	}
	if (!isfinite (quaternion[0]) || !isfinite (quaternion[1]) ||
		!isfinite (quaternion[2]) || !isfinite (quaternion[3]))
		return false;
	scale = sqrtf (quaternion[0] * quaternion[0] + quaternion[1] * quaternion[1] +
		quaternion[2] * quaternion[2] + quaternion[3] * quaternion[3]);
	if (!isfinite (scale) || scale <= 0.0001f)
		return false;
	for (int component = 0; component < 4; ++component)
		quaternion[component] /= scale;
	return true;
}

qboolean R_VRIKProjectCalibrationReference (qmodel_t *model,
	const r_vrik_calibration_projection_input_t *input,
	r_vrik_calibration_projection_t *out)
{
	r_vrik_calibration_reference_t reference;
	r_vrik_calibration_projection_t projection;
	vec3_t forward, right, up, cross, head, leftfoot, rightfoot;
	float bindfloor, bindheight, hmdheight, scale;

	if (!input || !out || !R_VRIKGetCalibrationReference (model, &reference) ||
		!R_VRIKFinite3 (input->hmd_position) || !isfinite (input->floor_height) ||
		!R_VRIKFinite3 (input->forward) || !R_VRIKFinite3 (input->right) ||
		!R_VRIKFinite3 (input->up))
		return false;
	VectorCopy (input->forward, forward);
	VectorCopy (input->right, right);
	VectorCopy (input->up, up);
	if (fabsf (VectorNormalize (forward) - 1.0f) > 0.02f ||
		fabsf (VectorNormalize (right) - 1.0f) > 0.02f ||
		fabsf (VectorNormalize (up) - 1.0f) > 0.02f ||
		fabsf (DotProduct (forward, right)) > 0.02f ||
		fabsf (DotProduct (forward, up)) > 0.02f ||
		fabsf (DotProduct (right, up)) > 0.02f)
		return false;
	CrossProduct (forward, up, cross);
	if (!VectorNormalize (cross) || DotProduct (cross, right) < 0.98f)
		return false;
	R_VRIKMatrixOrigin (reference.head_transform, head);
	R_VRIKMatrixOrigin (reference.transform[R_VRIK_LOWER_LEFT_FOOT], leftfoot);
	R_VRIKMatrixOrigin (reference.transform[R_VRIK_LOWER_RIGHT_FOOT], rightfoot);
	bindfloor = q_min (DotProduct (leftfoot, reference.up),
		DotProduct (rightfoot, reference.up));
	bindheight = DotProduct (head, reference.up) - bindfloor;
	hmdheight = DotProduct (input->hmd_position, up) - input->floor_height;
	if (!isfinite (bindheight) || !isfinite (hmdheight) || bindheight < 8.0f ||
		bindheight > 256.0f || hmdheight < 0.4f || hmdheight > 3.0f)
		return false;
	scale = hmdheight / bindheight;
	if (!isfinite (scale) || scale < 0.002f || scale > 1.0f)
		return false;
	memset (&projection, 0, sizeof (projection));
	projection.metres_per_bind_unit = scale;
	for (int role = 0; role < R_VRIK_LOWER_ROLE_COUNT; ++role)
	{
		float basis[12] = {0}, nativeinverse[12] = {0};
		float canonical[12], oriented[12];
		vec3_t roleorigin, offset;
		float component_forward, component_right, component_up;

		R_VRIKMatrixOrigin (reference.transform[role], roleorigin);
		VectorSubtract (roleorigin, head, offset);
		component_forward = DotProduct (offset, reference.forward) * scale;
		component_right = DotProduct (offset, reference.lateral) * scale;
		component_up = DotProduct (offset, reference.up) * scale;
		projection.position[role][0] = input->hmd_position[0] +
			forward[0] * component_forward + right[0] * component_right + up[0] * component_up;
		projection.position[role][1] = input->hmd_position[1] +
			forward[1] * component_forward + right[1] * component_right + up[1] * component_up;
		projection.position[role][2] = input->hmd_position[2] +
			forward[2] * component_forward + right[2] * component_right + up[2] * component_up;
		basis[0] = forward[0]; basis[1] = -right[0]; basis[2] = up[0];
		basis[4] = forward[1]; basis[5] = -right[1]; basis[6] = up[1];
		basis[8] = forward[2]; basis[9] = -right[2]; basis[10] = up[2];
		nativeinverse[0] = reference.forward[0];
		nativeinverse[1] = reference.forward[1];
		nativeinverse[2] = reference.forward[2];
		nativeinverse[4] = -reference.lateral[0];
		nativeinverse[5] = -reference.lateral[1];
		nativeinverse[6] = -reference.lateral[2];
		nativeinverse[8] = reference.up[0];
		nativeinverse[9] = reference.up[1];
		nativeinverse[10] = reference.up[2];
		R_VRIKMatrixMultiply (basis, nativeinverse, canonical);
		R_VRIKMatrixMultiply (canonical, reference.transform[role], oriented);
		if (!R_VRIKFinite3 (projection.position[role]) ||
			!R_VRIKCalibrationMatrixQuaternion (oriented,
				projection.orientation_wxyz[role]))
			return false;
	}
	*out = projection;
	return true;
}
