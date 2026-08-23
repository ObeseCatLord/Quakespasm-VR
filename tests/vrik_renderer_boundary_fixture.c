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

static void SetTarget (vrik_codec_pose_t *pose, int target, float x)
{
	pose->targets[target].position[0] = x;
	pose->targets[target].orientation[0] = x;
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
	return 0;
}
