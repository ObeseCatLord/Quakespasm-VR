/* Exercise the physical-path branch rotation at a nonzero model origin. */
#include "../Quake/r_avatar.c"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void identity_at(float matrix[12], float x, float y, float z)
{
	memset(matrix, 0, 12 * sizeof(*matrix));
	matrix[0] = matrix[5] = matrix[10] = 1.0f;
	matrix[3] = x;
	matrix[7] = y;
	matrix[11] = z;
}

static void near(float actual, float expected)
{
	assert(fabsf(actual - expected) < 0.0001f);
}

int main(void)
{
	md5_skeleton_joint_t joints[3] = {0};
	md5_skeleton_view_t skeleton = {0};
	r_avatar_rig_t rig = {0};
	float palette[3][12];
	const float from[3] = {1, 0, 0};
	const float to[3] = {0, 1, 0};

	joints[0].parent = -1;
	joints[1].parent = 0;
	joints[2].parent = 1;
	skeleton.joints = joints;
	skeleton.joint_count = 3;
	rig.live = &skeleton;
	identity_at(palette[0], 10, -4, 5);
	identity_at(palette[1], 11, -4, 5);
	identity_at(palette[2], 12, -4, 5);

	assert(R_AvatarRotateSubtreeToward(&rig, (float *)palette, 0, from, to));
	near(palette[0][3], 10);
	near(palette[0][7], -4);
	near(palette[0][11], 5);
	near(palette[1][3], 10);
	near(palette[1][7], -3);
	near(palette[1][11], 5);
	near(palette[2][3], 10);
	near(palette[2][7], -2);
	near(palette[2][11], 5);

	/* A reachable target on the original line needs a seeded bend. */
	identity_at(palette[0], 10, -4, 5);
	identity_at(palette[1], 11, -4, 5);
	identity_at(palette[2], 12, -4, 5);
	{
		const float target[3] = {11.5f, -4, 5};
		float bend;
		assert(R_AvatarSolvePhysicalPath(&rig, (float *)palette, 0, 2,
			target, palette[2]));
		near(palette[2][3], target[0]);
		near(palette[2][7], target[1]);
		bend = fabsf(palette[1][7] + 4) + fabsf(palette[1][11] - 5);
		assert(bend > 0.1f);
	}
	puts("avatar path pivot fixture: ok");
	return 0;
}
