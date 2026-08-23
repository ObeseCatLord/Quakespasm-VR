#include "quakedef.h"
#include "r_vrik.h"
#include "r_avatar.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

double realtime;

int q_strcasecmp (const char *first, const char *second)
{
	return strcasecmp (first, second);
}

qboolean R_VRIKSampleEntityLowerTargetsForTest (const entity_t *entity,
	r_vrik_lowerbody_targets_t *out);
qboolean R_VRIKBuildBodyIndexesForTest (const md5liveinfo_t *live,
	const r_avatar_profile_t *profile, const md5livesurface_t *surface,
	r_vrik_skincache_t *cache);
qboolean R_VRIKRefineArmOverreachForTest (void);
qboolean R_VRIKOrdinaryRangerLifecycleForTest (void);
qboolean R_VRIKShouldSubstituteAvatarForTest (int avatar, qboolean tracked,
	qboolean vrik_enabled);
qboolean R_VRIKShouldApplyPoseForTest (qboolean tracked, qboolean enabled);
qboolean R_VRIKAttachedPropForTest (
	const r_avatar_presentation_context_t *context, const float sourcehandpose[12],
	const float sourcehandbind[12], const float targethandpose[12],
	const float targethandbind[12], const float sourceprop[12],
	float socket[12], float out[12]);
qboolean R_VRIKApplyBindFloorCorrectionForTest (const md5liveinfo_t *canonical,
	const md5livesurface_t *canonicalsurface, const float *canonicalbind,
	int canonicalexclude1, int canonicalexclude2, const md5liveinfo_t *target,
	const md5livesurface_t *targetsurface, const float *targetbind,
	const unsigned short *targetindexes, int targetnumindexes,
	r_avatar_presentation_context_t *presentation, float *sourcefloor,
	float *targetfloor);

static void SetTarget (vrik_codec_pose_t *pose, int target, float x)
{
	pose->targets[target].position[0] = x;
	pose->targets[target].orientation[0] = x;
}

static void MultiplyMatrix (const float first[12], const float second[12],
	float out[12])
{
	int row, column;
	float result[12];

	for (row = 0; row < 3; row++)
	{
		for (column = 0; column < 3; column++)
			result[row * 4 + column] = first[row * 4] * second[column] +
				first[row * 4 + 1] * second[4 + column] +
				first[row * 4 + 2] * second[8 + column];
		result[row * 4 + 3] = first[row * 4] * second[3] +
			first[row * 4 + 1] * second[7] + first[row * 4 + 2] * second[11] +
			first[row * 4 + 3];
	}
	memcpy (out, result, sizeof(result));
}

int main (void)
{
	entity_t entity;
	r_vrik_lowerbody_targets_t targets;
	unsigned char hip = VRIK_TARGET_BIT (VRIK_TARGET_HIP);
	unsigned char left = VRIK_TARGET_BIT (VRIK_TARGET_LEFT_FOOT);
	unsigned char right = VRIK_TARGET_BIT (VRIK_TARGET_RIGHT_FOOT);

	memset (&entity, 0, sizeof(entity));
	entity.vrik_pose_count = 2;
	entity.vrik_pose_times[0] = 10.0;
	entity.vrik_pose_times[1] = 9.9;
	entity.vrik_v3_poses[0].flags = VRIK_V3_FLAG_ACTIVE;
	entity.vrik_v3_poses[1].flags = VRIK_V3_FLAG_ACTIVE;
	entity.vrik_v3_poses[0].present_mask = hip | left;
	entity.vrik_v3_poses[0].tracked_mask = hip;
	entity.vrik_v3_poses[1].present_mask = hip | left | right;
	entity.vrik_v3_poses[1].tracked_mask = hip | left | right;
	SetTarget (&entity.vrik_v3_poses[1], VRIK_TARGET_HIP, 0.0f);
	SetTarget (&entity.vrik_v3_poses[0], VRIK_TARGET_HIP, 10.0f);
	SetTarget (&entity.vrik_v3_poses[1], VRIK_TARGET_LEFT_FOOT, 100.0f);
	SetTarget (&entity.vrik_v3_poses[0], VRIK_TARGET_LEFT_FOOT, 20.0f);
	SetTarget (&entity.vrik_v3_poses[1], VRIK_TARGET_RIGHT_FOOT, 30.0f);

	/* The interpolation clock is halfway between the two fresh hip samples. */
	realtime = 10.025;
	assert (R_VRIKSampleEntityLowerTargetsForTest (&entity, &targets));
	assert (targets.present_mask == (R_VRIK_LOWER_BIT (R_VRIK_LOWER_HIP) |
		R_VRIK_LOWER_BIT (R_VRIK_LOWER_LEFT_FOOT)));
	assert (targets.tracked_mask == R_VRIK_LOWER_BIT (R_VRIK_LOWER_HIP));
	assert (targets.predicted_mask == R_VRIK_LOWER_BIT (R_VRIK_LOWER_LEFT_FOOT));
	assert (fabsf (targets.position[R_VRIK_LOWER_HIP][0] - 5.0f) < 0.001f);
	/* Do not interpolate this held payload toward an older measurement. */
	assert (fabsf (targets.position[R_VRIK_LOWER_LEFT_FOOT][0] - 20.0f) < 0.001f);
	assert (targets.confidence[R_VRIK_LOWER_LEFT_FOOT] == 1.0f);

	/* A stale newest packet clears every independently present role. */
	realtime = 10.0 + VRIK_POSE_STALE_TIME + 0.001;
	assert (!R_VRIKSampleEntityLowerTargetsForTest (&entity, &targets));
	{
		md5liveinfo_t live;
		md5livejoint_t joints[2];
		md5livevertex_t vertices[3];
		md5liveweight_t weights[3];
		unsigned short indexes[] = {0, 1, 0, 0, 2, 2};
		md5livesurface_t surface;
		r_vrik_skincache_t cache;
		r_avatar_profile_t ranger, alternate;
		int semantic;

		memset (&live, 0, sizeof(live));
		memset (joints, 0, sizeof(joints));
		memset (vertices, 0, sizeof(vertices));
		memset (weights, 0, sizeof(weights));
		memset (&surface, 0, sizeof(surface));
		memset (&cache, 0, sizeof(cache));
		memset (&ranger, 0, sizeof(ranger));
		memset (&alternate, 0, sizeof(alternate));
		for (semantic = 0; semantic < MD5_VRIK_JOINT_COUNT; semantic++)
			live.jointindex[semantic] = -1;
		joints[0].parent = -1;
		joints[1].parent = 0;
		strcpy (joints[1].name, "Sword");
		live.joints = joints;
		live.numbones = 2;
		for (semantic = 0; semantic < 3; semantic++)
		{
			vertices[semantic].firstweight = semantic;
			vertices[semantic].numweights = 1;
			weights[semantic].joint = semantic < 2 ? 1 : 0;
			weights[semantic].position[3] = 1.0f;
		}
		surface.vertices = vertices;
		surface.weights = weights;
		surface.indexes = indexes;
		surface.numverts = 3;
		surface.numindexes = 6;
		ranger.equipment_policy = R_AVATAR_EQUIPMENT_RANGER;
		assert (R_VRIKBuildBodyIndexesForTest (&live, &ranger, &surface, &cache));
		assert (cache.body_numindexes == surface.numindexes);
		assert (!memcmp (cache.body_indexes, indexes, sizeof(indexes)));
		alternate.equipment_policy = R_AVATAR_EQUIPMENT_ATTACH_HAND;
		alternate.native_equipment_joint[0] = "Sword";
		assert (R_VRIKBuildBodyIndexesForTest (&live, &alternate, &surface, &cache));
		assert (cache.body_numindexes == 3);
		assert (cache.body_indexes[0] == 0 && cache.body_indexes[1] == 2 &&
			cache.body_indexes[2] == 2);
		free (cache.body_indexes);
	}
	assert (R_VRIKRefineArmOverreachForTest ());
	assert (R_VRIKOrdinaryRangerLifecycleForTest ());
	assert (R_VRIKShouldSubstituteAvatarForTest (PLAYER_AVATAR_RANGER, false, true));
	assert (!R_VRIKShouldSubstituteAvatarForTest (PLAYER_AVATAR_RANGER, true, false));
	assert (R_VRIKShouldSubstituteAvatarForTest (PLAYER_AVATAR_RANGER, true, true));
	assert (R_VRIKShouldSubstituteAvatarForTest (PLAYER_AVATAR_FIEND, false, false));
	assert (!R_VRIKShouldApplyPoseForTest (true, false));
	assert (!R_VRIKShouldApplyPoseForTest (false, true));
	assert (R_VRIKShouldApplyPoseForTest (true, true));
	{
		r_avatar_presentation_context_t context;
		float sourcehand[12] = {1, 0, 0, 4, 0, 1, 0, 5, 0, 0, 1, 6};
		float sourcebind[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
		float targethand[12] = {1, 0, 0, 3, 0, 0, -1, 4, 0, 1, 0, -4};
		float targetbind[12] = {1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0};
		float sourceprop[12] = {1, 0, 0, 5, 0, 1, 0, 5, 0, 0, 1, 6};
		float attach[12], attached[12], presentedhand[12], offset[3], determinant;

		memset (&context, 0, sizeof(context));
		context.scale = 2.0f;
		context.rotation[1] = -1.0f; context.rotation[4] = 1.0f;
		context.rotation[10] = 1.0f;
		context.forward[1] = -2.0f; context.forward[4] = 2.0f;
		context.forward[10] = 2.0f;
		context.forward[3] = 100.0f; context.forward[7] = -50.0f;
		context.forward[11] = 7.0f;
		assert (R_VRIKAttachedPropForTest (&context, sourcehand, sourcebind,
			targethand, targetbind, sourceprop, attach, attached));
		MultiplyMatrix (attach, sourcehand, presentedhand);
		/* P maps the hand origin, including both presentation scale and c. */
		assert (fabsf (presentedhand[3] - 92.0f) < 0.001f &&
			fabsf (presentedhand[7] + 44.0f) < 0.001f &&
			fabsf (presentedhand[11] + 1.0f) < 0.001f);
		determinant = presentedhand[0] * (presentedhand[5] * presentedhand[10] - presentedhand[6] * presentedhand[9]) -
			presentedhand[1] * (presentedhand[4] * presentedhand[10] - presentedhand[6] * presentedhand[8]) +
			presentedhand[2] * (presentedhand[4] * presentedhand[9] - presentedhand[5] * presentedhand[8]);
		assert (fabsf (presentedhand[0] * presentedhand[0] + presentedhand[4] * presentedhand[4] +
			presentedhand[8] * presentedhand[8] - 1.0f) < 0.001f &&
			fabsf (presentedhand[1] * presentedhand[1] + presentedhand[5] * presentedhand[5] +
			presentedhand[9] * presentedhand[9] - 1.0f) < 0.001f &&
			fabsf (presentedhand[0] * presentedhand[1] + presentedhand[4] * presentedhand[5] +
			presentedhand[8] * presentedhand[9]) < 0.001f && fabsf (determinant - 1.0f) < 0.001f);
		/* R-only and uncorrected R*Ht preserve neither the canonical grip nor
		 * the canonical prop basis for this deliberately rotated target hand. */
		assert (fabsf (presentedhand[0] - 1.0f) < 0.001f && fabsf (presentedhand[5] - 1.0f) < 0.001f &&
			fabsf (presentedhand[10] - 1.0f) < 0.001f && fabsf (presentedhand[1]) < 0.001f &&
			fabsf (presentedhand[4]) < 0.001f && fabsf (presentedhand[6]) < 0.001f &&
			fabsf (presentedhand[9]) < 0.001f);
		offset[0] = attached[3] - presentedhand[3];
		offset[1] = attached[7] - presentedhand[7];
		offset[2] = attached[11] - presentedhand[11];
		assert (fabsf (sqrtf (offset[0] * offset[0] + offset[1] * offset[1] +
			offset[2] * offset[2]) - 1.0f) < 0.001f);
	}
	{
		md5liveinfo_t canonical, target;
		md5livejoint_t canonicaljoints[2], targetjoints[2];
		md5livevertex_t canonicalvertices[6], targetvertices[6];
		md5liveweight_t canonicalweights[6], targetweights[6];
		unsigned short indexes[] = {0, 1, 2, 3, 4, 5};
		md5livesurface_t canonicalsurface, targetsurface;
		r_vrik_skincache_t cache;
		r_avatar_profile_t profile;
		r_avatar_presentation_context_t initial, corrected, repeated;
		float canonicalbind[24], targetbind[24], animation_a[24], animation_b[24];
		float sourcefloor, targetfloor, repeatedsource, repeatedtarget;
		int joint, vertex;

		memset (&canonical, 0, sizeof(canonical));
		memset (&target, 0, sizeof(target));
		memset (canonicaljoints, 0, sizeof(canonicaljoints));
		memset (targetjoints, 0, sizeof(targetjoints));
		memset (canonicalvertices, 0, sizeof(canonicalvertices));
		memset (targetvertices, 0, sizeof(targetvertices));
		memset (canonicalweights, 0, sizeof(canonicalweights));
		memset (targetweights, 0, sizeof(targetweights));
		memset (&canonicalsurface, 0, sizeof(canonicalsurface));
		memset (&targetsurface, 0, sizeof(targetsurface));
		memset (&cache, 0, sizeof(cache));
		memset (&profile, 0, sizeof(profile));
		memset (canonicalbind, 0, sizeof(canonicalbind));
		memset (targetbind, 0, sizeof(targetbind));
		memset (animation_a, 0, sizeof(animation_a));
		for (joint = 0; joint < 2; joint++)
		{
			canonicalbind[joint * 12] = canonicalbind[joint * 12 + 5] =
				canonicalbind[joint * 12 + 10] = 1.0f;
			targetbind[joint * 12] = targetbind[joint * 12 + 5] =
				targetbind[joint * 12 + 10] = 1.0f;
			canonicaljoints[joint].parent = targetjoints[joint].parent = joint ? 0 : -1;
		}
		strcpy (canonicaljoints[1].name, "Gun");
		strcpy (targetjoints[1].name, "Sword");
		for (vertex = 0; vertex < 6; vertex++)
		{
			canonicalvertices[vertex].firstweight = targetvertices[vertex].firstweight = vertex;
			canonicalvertices[vertex].numweights = targetvertices[vertex].numweights = 1;
			canonicalweights[vertex].joint = targetweights[vertex].joint = vertex < 3 ? 0 : 1;
			canonicalweights[vertex].position[3] = targetweights[vertex].position[3] = 1.0f;
			canonicalweights[vertex].position[2] = vertex < 3 ? (float)(vertex + 1) : -100.0f;
			targetweights[vertex].position[2] = vertex < 3 ? -4.0f : -100.0f;
		}
		canonical.joints = canonicaljoints; canonical.numbones = 2;
		target.joints = targetjoints; target.numbones = 2; target.numposes = 1;
		canonicalsurface.vertices = canonicalvertices;
		canonicalsurface.weights = canonicalweights;
		canonicalsurface.indexes = indexes;
		canonicalsurface.numverts = 6; canonicalsurface.numindexes = 6;
		targetsurface.vertices = targetvertices; targetsurface.weights = targetweights;
		targetsurface.indexes = indexes;
		targetsurface.numverts = 6; targetsurface.numindexes = 6;
		profile.equipment_policy = R_AVATAR_EQUIPMENT_ATTACH_HAND;
		profile.native_equipment_joint[0] = "Sword";
		assert (R_VRIKBuildBodyIndexesForTest (&target, &profile, &targetsurface, &cache));
		assert (cache.body_numindexes == 3 && cache.body_indexes[0] == 0 &&
			cache.body_indexes[1] == 1 && cache.body_indexes[2] == 2);
		memset (&initial, 0, sizeof(initial));
		initial.scale = 2.0f;
		initial.rotation[0] = initial.rotation[5] = initial.rotation[10] = 1.0f;
		initial.forward[0] = initial.forward[5] = initial.forward[10] = 2.0f;
		initial.forward[11] = 7.0f; /* c is deliberately nonzero. */
		initial.inverse[0] = initial.inverse[5] = initial.inverse[10] = 0.5f;
		corrected = initial;
		target.boneposes = animation_a;
		assert (R_VRIKApplyBindFloorCorrectionForTest (&canonical, &canonicalsurface,
			canonicalbind, 1, -1, &target, &targetsurface, targetbind,
			cache.body_indexes, cache.body_numindexes, &corrected, &sourcefloor,
			&targetfloor));
		/* The Sword triangle at z=-100 is absent from both the body index stream
		 * and the floor calculation; c makes the body floor -1 before correction. */
		assert (fabsf (sourcefloor - 1.0f) < 0.001f && fabsf (targetfloor + 1.0f) < 0.001f);
		assert (fabsf (corrected.forward[11] - 9.0f) < 0.001f);
		assert (fabsf (2.0f * -4.0f + corrected.forward[11] - sourcefloor) < 0.001f);
		repeated = initial;
		for (vertex = 0; vertex < 24; vertex++) animation_b[vertex] = (float)(vertex * 13 - 71);
		target.boneposes = animation_b;
		assert (R_VRIKApplyBindFloorCorrectionForTest (&canonical, &canonicalsurface,
			canonicalbind, 1, -1, &target, &targetsurface, targetbind,
			cache.body_indexes, cache.body_numindexes, &repeated, &repeatedsource,
			&repeatedtarget));
		assert (fabsf (repeatedsource - sourcefloor) < 0.001f &&
			fabsf (repeatedtarget - targetfloor) < 0.001f &&
			fabsf (repeated.forward[11] - corrected.forward[11]) < 0.001f);
		free (cache.body_indexes);
	}
	return 0;
}
