/* VRIK CPU skin cache and lower-body rig helpers. */
#include "r_vrik.h"

static r_vrik_skincache_t r_vrik_skin_caches[MAX_SCOREBOARD + 1];
static r_vrik_lowerbody_targets_t r_vrik_lower_targets[MAX_SCOREBOARD + 1];

/* The rerelease player is small, but do not hard-code its joint order.  This
 * cache is rebuilt whenever the retained MD5 surface changes. */
typedef struct r_vrik_rigcache_s
{
	const aliashdr_t *surface;
	int numbones;
	unsigned char subtree[MD5_VRIK_JOINT_COUNT][MAX_MD5_JOINTS];
	qboolean leg_valid[2];
	vec3_t bind_knee_vector[2];
	vec3_t bind_foot_vector[2];
	float bind_upper_length[2];
	float bind_lower_length[2];
} r_vrik_rigcache_t;

static r_vrik_rigcache_t r_vrik_rigcache;

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
	memcpy (out, result, sizeof(result));
}

static qboolean R_VRIKFiniteVector (const vec3_t value)
{
	return isfinite (value[0]) && isfinite (value[1]) && isfinite (value[2]);
}

static qboolean R_VRIKIsDescendant (const md5liveinfo_t *live, int child,
	int ancestor)
{
	int parent;

	if (!live || child < 0 || child >= live->numbones || ancestor < 0 ||
		ancestor >= live->numbones)
		return false;
	for (parent = child; parent >= 0; parent = live->joints[parent].parent)
		if (parent == ancestor)
			return true;
	return false;
}

static qboolean R_VRIKBuildRigCache (const md5liveinfo_t *live)
{
	int semantic, joint, side;

	if (!live || !live->firstsurface || !live->joints || live->numbones < 1 ||
		live->numbones > MAX_MD5_JOINTS)
		return false;
	if (r_vrik_rigcache.surface == live->firstsurface &&
		r_vrik_rigcache.numbones == live->numbones)
		return true;
	memset (&r_vrik_rigcache, 0, sizeof(r_vrik_rigcache));
	r_vrik_rigcache.surface = live->firstsurface;
	r_vrik_rigcache.numbones = live->numbones;
	for (semantic = 0; semantic < MD5_VRIK_JOINT_COUNT; semantic++)
	{
		int root = live->jointindex[semantic];
		if (root < 0)
			continue;
		for (joint = 0; joint < live->numbones; joint++)
			r_vrik_rigcache.subtree[semantic][joint] =
				R_VRIKIsDescendant (live, joint, root);
	}
	for (side = 0; side < 2; side++)
	{
		int uppersemantic = side ? MD5_VRIK_UPPERLEG_R : MD5_VRIK_UPPERLEG_L;
		int lowersemantic = side ? MD5_VRIK_LOWERLEG_R : MD5_VRIK_LOWERLEG_L;
		int footsemantic = side ? MD5_VRIK_FOOT_R : MD5_VRIK_FOOT_L;
		int hip = live->jointindex[MD5_VRIK_HIP];
		int upper = live->jointindex[uppersemantic];
		int lower = live->jointindex[lowersemantic];
		int foot = live->jointindex[footsemantic];
		vec3_t upperorigin, lowerorigin, footorigin;

		if (hip < 0 || upper < 0 || lower < 0 || foot < 0 ||
			!R_VRIKIsDescendant (live, upper, hip) ||
			!R_VRIKIsDescendant (live, lower, upper) ||
			!R_VRIKIsDescendant (live, foot, lower))
			continue;
		R_VRIKMatrixOrigin (live->joints[upper].bind, upperorigin);
		R_VRIKMatrixOrigin (live->joints[lower].bind, lowerorigin);
		R_VRIKMatrixOrigin (live->joints[foot].bind, footorigin);
		VectorSubtract (lowerorigin, upperorigin,
			r_vrik_rigcache.bind_knee_vector[side]);
		VectorSubtract (footorigin, lowerorigin,
			r_vrik_rigcache.bind_foot_vector[side]);
		r_vrik_rigcache.bind_upper_length[side] =
			VectorLength (r_vrik_rigcache.bind_knee_vector[side]);
		r_vrik_rigcache.bind_lower_length[side] =
			VectorLength (r_vrik_rigcache.bind_foot_vector[side]);
		if (r_vrik_rigcache.bind_upper_length[side] > 0.01f &&
			r_vrik_rigcache.bind_lower_length[side] > 0.01f)
			r_vrik_rigcache.leg_valid[side] = true;
	}
	return true;
}

static void R_VRIKTranslateSubtree (const md5liveinfo_t *live, float *palette,
	md5vrikjoint_t semantic, const vec3_t delta)
{
	int joint;

	if (!live || !palette || semantic < 0 || semantic >= MD5_VRIK_JOINT_COUNT ||
		live->jointindex[semantic] < 0)
		return;
	for (joint = 0; joint < live->numbones; joint++)
		if (r_vrik_rigcache.subtree[semantic][joint])
		{
			vec3_t origin;
			R_VRIKMatrixOrigin (palette + joint * 12, origin);
			VectorAdd (origin, delta, origin);
			R_VRIKSetMatrixOrigin (palette + joint * 12, origin);
		}
}

static void R_VRIKRotationDifference (const float desired[12],
	const float current[12], float delta[12])
{
	int row, column, k;

	memset (delta, 0, sizeof(float) * 12);
	for (row = 0; row < 3; row++)
		for (column = 0; column < 3; column++)
			for (k = 0; k < 3; k++)
				delta[row * 4 + column] += desired[row * 4 + k] *
					current[column * 4 + k];
}

static void R_VRIKRotateSubtree (const md5liveinfo_t *live, float *palette,
	md5vrikjoint_t semantic, const vec3_t pivot, const float delta[12])
{
	int joint;

	if (!live || !palette || semantic < 0 || semantic >= MD5_VRIK_JOINT_COUNT ||
		live->jointindex[semantic] < 0)
		return;
	for (joint = 0; joint < live->numbones; joint++)
		if (r_vrik_rigcache.subtree[semantic][joint])
		{
			float result[12];
			vec3_t oldorigin, relative, neworigin;
			R_VRIKMatrixOrigin (palette + joint * 12, oldorigin);
			VectorSubtract (oldorigin, pivot, relative);
			neworigin[0] = pivot[0] + delta[0] * relative[0] +
				delta[1] * relative[1] + delta[2] * relative[2];
			neworigin[1] = pivot[1] + delta[4] * relative[0] +
				delta[5] * relative[1] + delta[6] * relative[2];
			neworigin[2] = pivot[2] + delta[8] * relative[0] +
				delta[9] * relative[1] + delta[10] * relative[2];
			R_VRIKMatrixMultiply (delta, palette + joint * 12, result);
			R_VRIKSetMatrixOrigin (result, neworigin);
			memcpy (palette + joint * 12, result, sizeof(result));
		}
}

static void R_VRIKRotateTowardSubtree (const md5liveinfo_t *live,
	float *palette, md5vrikjoint_t semantic, const vec3_t pivot,
	const vec3_t from, const vec3_t to)
{
	vec3_t a, b, axis;
	float cosine, sine, one, delta[12];

	VectorCopy (from, a);
	VectorCopy (to, b);
	if (!VectorNormalize (a) || !VectorNormalize (b))
		return;
	cosine = CLAMP (-1.0f, DotProduct (a, b), 1.0f);
	CrossProduct (a, b, axis);
	sine = VectorNormalize (axis);
	if (sine < 0.0001f)
	{
		if (cosine > 0.0f)
			return;
		/* A 180 degree turn has no unique plane.  Preserve the avatar's
		 * authored knee direction by choosing an axis perpendicular to it. */
		axis[0] = 0; axis[1] = 0; axis[2] = 1;
		if (fabsf (DotProduct (axis, a)) > 0.9f)
			axis[0] = 0, axis[1] = 1, axis[2] = 0;
		CrossProduct (a, axis, axis);
		if (!VectorNormalize (axis))
			return;
		sine = 0.0f;
	}
	one = 1.0f - cosine;
	memset (delta, 0, sizeof(delta));
	delta[0] = cosine + axis[0] * axis[0] * one;
	delta[1] = axis[0] * axis[1] * one - axis[2] * sine;
	delta[2] = axis[0] * axis[2] * one + axis[1] * sine;
	delta[4] = axis[1] * axis[0] * one + axis[2] * sine;
	delta[5] = cosine + axis[1] * axis[1] * one;
	delta[6] = axis[1] * axis[2] * one - axis[0] * sine;
	delta[8] = axis[2] * axis[0] * one - axis[1] * sine;
	delta[9] = axis[2] * axis[1] * one + axis[0] * sine;
	delta[10] = cosine + axis[2] * axis[2] * one;
	R_VRIKRotateSubtree (live, palette, semantic, pivot, delta);
}

static void R_VRIKOrientSubtree (const md5liveinfo_t *live, float *palette,
	md5vrikjoint_t semantic, const float desired[12])
{
	int index;
	vec3_t origin;
	float delta[12];

	if (!live || !palette || semantic < 0 || semantic >= MD5_VRIK_JOINT_COUNT)
		return;
	index = live->jointindex[semantic];
	if (index < 0)
		return;
	R_VRIKMatrixOrigin (palette + index * 12, origin);
	R_VRIKRotationDifference (desired, palette + index * 12, delta);
	R_VRIKRotateSubtree (live, palette, semantic, origin, delta);
}

static void R_VRIKSolveLeg (const md5liveinfo_t *live, float *palette,
	int side, const vec3_t supplied_target, float confidence)
{
	md5vrikjoint_t uppersemantic = side ? MD5_VRIK_UPPERLEG_R : MD5_VRIK_UPPERLEG_L;
	md5vrikjoint_t lowersemantic = side ? MD5_VRIK_LOWERLEG_R : MD5_VRIK_LOWERLEG_L;
	md5vrikjoint_t footsemantic = side ? MD5_VRIK_FOOT_R : MD5_VRIK_FOOT_L;
	int upperindex = live->jointindex[uppersemantic];
	int lowerindex = live->jointindex[lowersemantic];
	int footindex = live->jointindex[footsemantic];
	vec3_t hip, oldknee, oldfoot, target, toward, pole, knee, correction;
	vec3_t oldupperdir, oldlowerdir, newupperdir, newlowerdir;
	float upperlength, lowerlength, distance, cosine, anglecos, anglesin;

	if (!r_vrik_rigcache.leg_valid[side] || upperindex < 0 || lowerindex < 0 ||
		footindex < 0 || !R_VRIKFiniteVector (supplied_target))
		return;
	confidence = CLAMP (0.0f, confidence, 1.0f);
	if (confidence <= 0.0f)
		return;
	R_VRIKMatrixOrigin (palette + upperindex * 12, hip);
	R_VRIKMatrixOrigin (palette + lowerindex * 12, oldknee);
	R_VRIKMatrixOrigin (palette + footindex * 12, oldfoot);
	VectorSubtract (oldknee, hip, oldupperdir);
	VectorSubtract (oldfoot, oldknee, oldlowerdir);
	upperlength = VectorLength (oldupperdir);
	lowerlength = VectorLength (oldlowerdir);
	if (upperlength < 0.01f)
	{
		VectorCopy (r_vrik_rigcache.bind_knee_vector[side], oldupperdir);
		upperlength = r_vrik_rigcache.bind_upper_length[side];
	}
	if (lowerlength < 0.01f)
	{
		VectorCopy (r_vrik_rigcache.bind_foot_vector[side], oldlowerdir);
		lowerlength = r_vrik_rigcache.bind_lower_length[side];
	}
	if (upperlength < 0.01f || lowerlength < 0.01f)
		return;
	/* Confidence blends only from this frame's animated foot, never from a
	 * previously received tracker position.  A newly appearing role therefore
	 * cannot interpolate through zero or an unrelated stale role. */
	VectorSubtract (supplied_target, oldfoot, target);
	VectorMA (oldfoot, confidence, target, target);
	VectorSubtract (target, hip, toward);
	distance = VectorLength (toward);
	if (distance < 0.001f)
	{
		/* A target at the hip has no direction.  Use this frame's (or the
		 * bind fallback's) upper-leg direction so the singularity remains
		 * deterministic instead of leaving a collapsed animated segment. */
		VectorCopy (oldupperdir, toward);
		if (!VectorNormalize (toward))
			return;
		distance = 0.0f;
	}
	else
		VectorScale (toward, 1.0f / distance, toward);
	/* Keep the triangle just inside both reach singularities.  Extending the
	 * target past a+b makes the cosine clamp put the analytic knee beyond the
	 * upper-leg length. */
	distance = CLAMP (fabsf (upperlength - lowerlength) + 0.01f, distance,
		q_max (fabsf (upperlength - lowerlength) + 0.01f,
			upperlength + lowerlength - 0.01f));
	VectorMA (hip, distance, toward, target);

	/* Project the current animated knee onto the solve plane.  The bind vector
	 * is a stable fallback for straight animation frames. */
	VectorCopy (oldupperdir, pole);
	VectorMA (pole, -DotProduct (pole, toward), toward, pole);
	if (!VectorNormalize (pole))
	{
		VectorCopy (r_vrik_rigcache.bind_knee_vector[side], pole);
		VectorMA (pole, -DotProduct (pole, toward), toward, pole);
		if (!VectorNormalize (pole))
		{
			/* At the hip/straight-leg singularity no authored bend plane is
			 * available.  Choose one deterministically, independently per leg. */
			pole[0] = 0.0f; pole[1] = 0.0f; pole[2] = 1.0f;
			if (fabsf (DotProduct (pole, toward)) > 0.9f)
				pole[0] = 0.0f, pole[1] = 1.0f, pole[2] = 0.0f;
			VectorMA (pole, -DotProduct (pole, toward), toward, pole);
			if (!VectorNormalize (pole))
				return;
		}
	}
	cosine = CLAMP (-1.0f,
		(upperlength * upperlength + distance * distance - lowerlength * lowerlength) /
		(2.0f * upperlength * distance), 1.0f);
	anglecos = cosine * upperlength;
	anglesin = sqrtf (q_max (0.0f, 1.0f - cosine * cosine)) * upperlength;
	VectorMA (hip, anglecos, toward, knee);
	VectorMA (knee, anglesin, pole, knee);
	VectorSubtract (knee, hip, newupperdir);
	R_VRIKRotateTowardSubtree (live, palette, uppersemantic, hip,
		oldupperdir, newupperdir);
	R_VRIKMatrixOrigin (palette + lowerindex * 12, oldknee);
	/* Keep all knee descendants attached to the analytic elbow, including on
	 * a degenerate animation frame where an authored segment collapsed. */
	VectorSubtract (knee, oldknee, correction);
	R_VRIKTranslateSubtree (live, palette, lowersemantic, correction);
	/* The lower subtree has just moved.  Re-read its origin before using it as
	 * the pivot for the lower-leg rotation; otherwise a collapsed upper segment
	 * rotates around the stale pre-translation knee and pulls it off the
	 * analytic solution. */
	R_VRIKMatrixOrigin (palette + lowerindex * 12, oldknee);
	R_VRIKMatrixOrigin (palette + footindex * 12, oldfoot);
	VectorSubtract (oldfoot, oldknee, oldlowerdir);
	VectorSubtract (target, oldknee, newlowerdir);
	R_VRIKRotateTowardSubtree (live, palette, lowersemantic, oldknee,
		oldlowerdir, newlowerdir);
}

r_vrik_skincache_t *R_VRIKGetSkinCache (int entitynum, qmodel_t *model)
{
	r_vrik_skincache_t *cache;

	if (entitynum < 1 || entitynum > MAX_SCOREBOARD || !model)
		return NULL;
	cache = &r_vrik_skin_caches[entitynum];
	if (cache->model != model || cache->hostframe != host_framecount)
	{
		cache->model = model;
		cache->hostframe = host_framecount;
		cache->surface = NULL;
		cache->ready = false;
		cache->body_numindexes = 0;
		cache->muzzle_valid = false;
		cache->prop_surface = NULL;
		cache->prop_numverts = cache->prop_numindexes = 0;
		cache->prop_semantic = -1;
	}
	return cache;
}

qboolean R_VRIKSkinCacheReady (const r_vrik_skincache_t *cache,
	qmodel_t *model)
{
	return cache && cache->ready && cache->model == model &&
		cache->hostframe == host_framecount;
}

qboolean R_VRIKSkinCacheReserve (r_vrik_skincache_t *cache, int numverts)
{
	md5vertex_t *vertices;

	if (!cache || numverts < 1)
		return false;
	if (numverts <= cache->capacity)
		return true;
	if ((size_t)numverts > SIZE_MAX / sizeof(*vertices))
		return false;
	vertices = (md5vertex_t *)realloc (cache->vertices,
		(size_t)numverts * sizeof(*vertices));
	if (!vertices)
		return false;
	cache->vertices = vertices;
	cache->capacity = numverts;
	return true;
}

void R_VRIKResetSkinCaches (void)
{
	int entitynum;

	for (entitynum = 0; entitynum <= MAX_SCOREBOARD; entitynum++)
	{
		free (r_vrik_skin_caches[entitynum].vertices);
		free (r_vrik_skin_caches[entitynum].body_indexes);
		free (r_vrik_skin_caches[entitynum].prop_vertices);
		free (r_vrik_skin_caches[entitynum].prop_indexes);
		memset (&r_vrik_skin_caches[entitynum], 0,
			sizeof(r_vrik_skin_caches[entitynum]));
	}
	memset (r_vrik_lower_targets, 0, sizeof(r_vrik_lower_targets));
	memset (&r_vrik_rigcache, 0, sizeof(r_vrik_rigcache));
}

void R_VRIKSkinCacheCommit (r_vrik_skincache_t *cache,
	const aliashdr_t *surface)
{
	if (!cache || !surface)
		return;
	cache->surface = surface;
	cache->ready = true;
}

void R_VRIKSetLowerBodyTargets (int entitynum,
	const r_vrik_lowerbody_targets_t *targets)
{
	r_vrik_lowerbody_targets_t clean;
	int role;

	if (entitynum < 1 || entitynum > MAX_SCOREBOARD)
		return;
	memset (&clean, 0, sizeof(clean));
	if (!targets)
	{
		r_vrik_lower_targets[entitynum] = clean;
		return;
	}
	clean.present_mask = targets->present_mask &
		((1u << R_VRIK_LOWER_ROLE_COUNT) - 1u);
	clean.tracked_mask = targets->tracked_mask & clean.present_mask;
	clean.predicted_mask = targets->predicted_mask & clean.present_mask &
		~clean.tracked_mask;
	for (role = 0; role < R_VRIK_LOWER_ROLE_COUNT; role++)
		if (clean.present_mask & R_VRIK_LOWER_BIT (role))
		{
			if (!R_VRIKFiniteVector (targets->position[role]) ||
				!R_VRIKFiniteVector (targets->orientation[role]) ||
				!isfinite (targets->confidence[role]))
			{
				clean.present_mask &= ~R_VRIK_LOWER_BIT (role);
				clean.tracked_mask &= ~R_VRIK_LOWER_BIT (role);
				clean.predicted_mask &= ~R_VRIK_LOWER_BIT (role);
				continue;
			}
			clean.confidence[role] = CLAMP (0.0f, targets->confidence[role], 1.0f);
			VectorCopy (targets->position[role], clean.position[role]);
			VectorCopy (targets->orientation[role], clean.orientation[role]);
		}
	r_vrik_lower_targets[entitynum] = clean;
}

void R_VRIKClearLowerBodyTargets (int entitynum)
{
	if (entitynum < 1 || entitynum > MAX_SCOREBOARD)
		return;
	memset (&r_vrik_lower_targets[entitynum], 0,
		sizeof(r_vrik_lower_targets[entitynum]));
}

qboolean R_VRIKGetLowerBodyTargets (int entitynum,
	r_vrik_lowerbody_targets_t *out)
{
	if (!out || entitynum < 1 || entitynum > MAX_SCOREBOARD)
		return false;
	*out = r_vrik_lower_targets[entitynum];
	return out->present_mask != 0;
}

qboolean R_VRIKApplyLowerBody (const md5liveinfo_t *live, float *palette,
	const r_vrik_lowerbody_model_targets_t *targets)
{
	int hipindex, role;
	vec3_t hip, delta;

	if (!live || !palette || !targets || !R_VRIKBuildRigCache (live))
		return false;
	/* Do not apply a partly named or unrelated MD5 skeleton.  The upper-body
	 * compatibility check remains authoritative; legs add their own ancestry
	 * checks below so an optional bad chain just falls back to animation. */
	if (!live->compatible || !live->from_rerelease)
		return false;
	hipindex = live->jointindex[MD5_VRIK_HIP];
	if (hipindex < 0)
		return false;
	if ((targets->usable_mask & R_VRIK_LOWER_BIT (R_VRIK_LOWER_HIP)) &&
		R_VRIKFiniteVector (targets->position[R_VRIK_LOWER_HIP]))
	{
		float confidence = CLAMP (0.0f,
			targets->confidence[R_VRIK_LOWER_HIP], 1.0f);
		R_VRIKMatrixOrigin (palette + hipindex * 12, hip);
		VectorSubtract (targets->position[R_VRIK_LOWER_HIP], hip, delta);
		/* This is a renderer safety limit, not a calibration policy. */
		if (VectorLength (delta) > 96.0f)
		{
			VectorNormalize (delta);
			VectorScale (delta, 96.0f, delta);
		}
		VectorScale (delta, confidence, delta);
		R_VRIKTranslateSubtree (live, palette, MD5_VRIK_HIP, delta);
		if (targets->orientation_mask & R_VRIK_LOWER_BIT (R_VRIK_LOWER_HIP))
			R_VRIKOrientSubtree (live, palette, MD5_VRIK_HIP,
				targets->orientation[R_VRIK_LOWER_HIP]);
	}
	if (targets->usable_mask & R_VRIK_LOWER_BIT (R_VRIK_LOWER_LEFT_FOOT))
		R_VRIKSolveLeg (live, palette, 0,
			targets->position[R_VRIK_LOWER_LEFT_FOOT],
			targets->confidence[R_VRIK_LOWER_LEFT_FOOT]);
	if (targets->usable_mask & R_VRIK_LOWER_BIT (R_VRIK_LOWER_RIGHT_FOOT))
		R_VRIKSolveLeg (live, palette, 1,
			targets->position[R_VRIK_LOWER_RIGHT_FOOT],
			targets->confidence[R_VRIK_LOWER_RIGHT_FOOT]);
	for (role = R_VRIK_LOWER_LEFT_FOOT;
		role <= R_VRIK_LOWER_RIGHT_FOOT; role++)
		if ((targets->usable_mask & R_VRIK_LOWER_BIT (role)) &&
			(targets->orientation_mask & R_VRIK_LOWER_BIT (role)))
			R_VRIKOrientSubtree (live, palette,
				role == R_VRIK_LOWER_LEFT_FOOT ? MD5_VRIK_FOOT_L : MD5_VRIK_FOOT_R,
				targets->orientation[role]);
	return true;
}

qboolean R_VRIKGetCalibrationReference (qmodel_t *model,
	r_vrik_calibration_reference_t *out)
{
	md5liveinfo_t live;
	int hip, head, leftfoot, rightfoot, leftshoulder, rightshoulder;
	vec3_t hiporigin, headorigin, leftorigin, rightorigin;

	if (!out || !Mod_GetMD5LiveData (model, &live) || !live.compatible ||
		!live.from_rerelease || !R_VRIKBuildRigCache (&live) ||
		!r_vrik_rigcache.leg_valid[0] || !r_vrik_rigcache.leg_valid[1])
		return false;
	hip = live.jointindex[MD5_VRIK_HIP];
	head = live.jointindex[MD5_VRIK_HEAD];
	leftfoot = live.jointindex[MD5_VRIK_FOOT_L];
	rightfoot = live.jointindex[MD5_VRIK_FOOT_R];
	leftshoulder = live.jointindex[MD5_VRIK_SHOULDER_L];
	rightshoulder = live.jointindex[MD5_VRIK_SHOULDER_R];
	if (hip < 0 || head < 0 || leftfoot < 0 || rightfoot < 0 ||
		leftshoulder < 0 || rightshoulder < 0)
		return false;
	memset (out, 0, sizeof(*out));
	memcpy (out->transform[R_VRIK_LOWER_HIP], live.joints[hip].bind, 12 * sizeof(float));
	memcpy (out->transform[R_VRIK_LOWER_LEFT_FOOT], live.joints[leftfoot].bind, 12 * sizeof(float));
	memcpy (out->transform[R_VRIK_LOWER_RIGHT_FOOT], live.joints[rightfoot].bind, 12 * sizeof(float));
	memcpy (out->head_transform, live.joints[head].bind, 12 * sizeof(float));
	R_VRIKMatrixOrigin (live.joints[hip].bind, hiporigin);
	R_VRIKMatrixOrigin (live.joints[head].bind, headorigin);
	R_VRIKMatrixOrigin (live.joints[leftshoulder].bind, leftorigin);
	R_VRIKMatrixOrigin (live.joints[rightshoulder].bind, rightorigin);
	VectorSubtract (rightorigin, leftorigin, out->lateral);
	VectorSubtract (headorigin, hiporigin, out->up);
	if (!VectorNormalize (out->lateral) || !VectorNormalize (out->up))
		return false;
	CrossProduct (out->up, out->lateral, out->forward);
	if (!VectorNormalize (out->forward))
		return false;
	CrossProduct (out->lateral, out->forward, out->up);
	VectorNormalize (out->up);
	return true;
}

static qboolean R_VRIKMatrixQuaternionWXYZ (const float matrix[12],
	float quaternion[4])
{
	float trace, scale;

	trace = matrix[0] + matrix[5] + matrix[10];
	if (trace > 0.0f)
	{
		scale = sqrtf (trace + 1.0f) * 2.0f;
		quaternion[0] = 0.25f * scale;
		quaternion[1] = (matrix[9] - matrix[6]) / scale;
		quaternion[2] = (matrix[2] - matrix[8]) / scale;
		quaternion[3] = (matrix[4] - matrix[1]) / scale;
	}
	else if (matrix[0] > matrix[5] && matrix[0] > matrix[10])
	{
		scale = sqrtf (1.0f + matrix[0] - matrix[5] - matrix[10]) * 2.0f;
		quaternion[0] = (matrix[9] - matrix[6]) / scale;
		quaternion[1] = 0.25f * scale;
		quaternion[2] = (matrix[1] + matrix[4]) / scale;
		quaternion[3] = (matrix[2] + matrix[8]) / scale;
	}
	else if (matrix[5] > matrix[10])
	{
		scale = sqrtf (1.0f + matrix[5] - matrix[0] - matrix[10]) * 2.0f;
		quaternion[0] = (matrix[2] - matrix[8]) / scale;
		quaternion[1] = (matrix[1] + matrix[4]) / scale;
		quaternion[2] = 0.25f * scale;
		quaternion[3] = (matrix[6] + matrix[9]) / scale;
	}
	else
	{
		scale = sqrtf (1.0f + matrix[10] - matrix[0] - matrix[5]) * 2.0f;
		quaternion[0] = (matrix[4] - matrix[1]) / scale;
		quaternion[1] = (matrix[2] + matrix[8]) / scale;
		quaternion[2] = (matrix[6] + matrix[9]) / scale;
		quaternion[3] = 0.25f * scale;
	}
	if (!isfinite (scale) || scale <= 0.0001f || !isfinite (quaternion[0]) ||
		!isfinite (quaternion[1]) || !isfinite (quaternion[2]) ||
		!isfinite (quaternion[3]))
		return false;
	scale = sqrtf (quaternion[0] * quaternion[0] + quaternion[1] * quaternion[1] +
		quaternion[2] * quaternion[2] + quaternion[3] * quaternion[3]);
	if (scale <= 0.0001f || !isfinite (scale))
		return false;
	quaternion[0] /= scale;
	quaternion[1] /= scale;
	quaternion[2] /= scale;
	quaternion[3] /= scale;
	return true;
}

qboolean R_VRIKProjectCalibrationReference (qmodel_t *model,
	const r_vrik_calibration_projection_input_t *input,
	r_vrik_calibration_projection_t *out)
{
	r_vrik_calibration_reference_t reference;
	vec3_t head, leftfoot, rightfoot;
	vec3_t forward, right, up, cross;
	float bindfloor, bindheight, hmdheight, scale;
	int role;

	if (!input || !out || !R_VRIKGetCalibrationReference (model, &reference) ||
		!R_VRIKFiniteVector (input->hmd_position) || !isfinite (input->floor_height) ||
		!R_VRIKFiniteVector (input->forward) || !R_VRIKFiniteVector (input->right) ||
		!R_VRIKFiniteVector (input->up))
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
	/* These broad limits catch unit/frame mistakes while accepting the authored
	 * rerelease Ranger (roughly Quake-unit height) and normal seated/standing
	 * calibration.  Never invent a transform for a degenerate rig. */
	if (!isfinite (bindheight) || !isfinite (hmdheight) || bindheight < 8.0f ||
		bindheight > 256.0f || hmdheight < 0.4f || hmdheight > 3.0f)
		return false;
	scale = hmdheight / bindheight;
	if (scale < 0.002f || scale > 1.0f)
		return false;
	memset (out, 0, sizeof(*out));
	out->metres_per_bind_unit = scale;
	for (role = 0; role < R_VRIK_LOWER_ROLE_COUNT; role++)
	{
		float basis[12], nativeinverse[12], canonical[12], oriented[12];
		vec3_t roleorigin, offset;
		float component_forward, component_right, component_up;

		R_VRIKMatrixOrigin (reference.transform[role], roleorigin);
		VectorSubtract (roleorigin, head, offset);
		component_forward = DotProduct (offset, reference.forward) * scale;
		component_right = DotProduct (offset, reference.lateral) * scale;
		component_up = DotProduct (offset, reference.up) * scale;
		out->position[role][0] = input->hmd_position[0] +
			forward[0] * component_forward + right[0] * component_right + up[0] * component_up;
		out->position[role][1] = input->hmd_position[1] +
			forward[1] * component_forward + right[1] * component_right + up[1] * component_up;
		out->position[role][2] = input->hmd_position[2] +
			forward[2] * component_forward + right[2] * component_right + up[2] * component_up;
		/* Both bases use forward/left/up; keeping -right in the middle column
		 * makes each a proper (not mirrored) rotation.  Bind joints are native
		 * model-space, so pass through native-basis inverse before world basis. */
		memset (basis, 0, sizeof(basis));
		basis[0] = forward[0]; basis[1] = -right[0]; basis[2] = up[0];
		basis[4] = forward[1]; basis[5] = -right[1]; basis[6] = up[1];
		basis[8] = forward[2]; basis[9] = -right[2]; basis[10] = up[2];
		memset (nativeinverse, 0, sizeof(nativeinverse));
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
		if (!R_VRIKMatrixQuaternionWXYZ (oriented, out->orientation_wxyz[role]))
			return false;
	}
	return true;
}
