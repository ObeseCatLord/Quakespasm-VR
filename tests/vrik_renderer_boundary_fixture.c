#include "quakedef.h"
#include "r_vrik.h"

#include <assert.h>
#include <math.h>
#include <string.h>

double realtime;

qboolean R_VRIKSampleEntityLowerTargetsForTest (const entity_t *entity,
	r_vrik_lowerbody_targets_t *out);

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
	return 0;
}
