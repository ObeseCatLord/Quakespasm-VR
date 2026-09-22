/* Exercise the inherited VR locomotion and controller-angle arithmetic. */
#include "../Quake/vr_locomotion.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

static void near_value (float actual, float expected)
{
	assert (fabsf (actual - expected) < 0.002f);
}

static void near_vec3 (const float actual[3], const float expected[3])
{
	for (int component = 0; component < 3; ++component)
		near_value (actual[component], expected[component]);
}

static void expect_zero (const float actual[3])
{
	near_vec3 (actual, (float[3]){0.0f, 0.0f, 0.0f});
}

static void test_neutral_and_cardinal_modes (void)
{
	const float head_east[3] = {0.0f, 90.0f, 0.0f};
	const float hand_west[3] = {0.0f, -90.0f, 0.0f};
	float out[3];

	assert (VR_LocomotionMove (VR_MOVEMENT_MODE_FOLLOW_HEAD, head_east, hand_west,
		0.75f, -0.25f, 200.0f, 160.0f, out));
	near_vec3 (out, (float[3]){150.0f, -50.0f, 0.0f});

	assert (VR_LocomotionMove (VR_MOVEMENT_MODE_FOLLOW_HAND, head_east, hand_west,
		0.75f, -0.25f, 200.0f, 160.0f, out));
	near_vec3 (out, (float[3]){150.0f, -50.0f, 0.0f});
}

static void test_pitch_roll_and_near_vertical (void)
{
	const float steep_head[3] = {89.0f, 0.0f, 30.0f};
	const float neutral_hand[3] = {0.0f, 0.0f, 0.0f};
	float out[3];

	/* At near-vertical pitch the inherited gimbal path swaps forward/up.
	 * Roll then rotates horizontal intent while vertical motion still uses the
	 * original forward Z. */
	assert (VR_LocomotionMove (VR_MOVEMENT_MODE_FOLLOW_HEAD, steep_head,
		neutral_hand, 1.0f, 0.0f, 200.0f, 160.0f, out));
	near_vec3 (out, (float[3]){173.20508f, 100.01523f, -159.97563f});
}

static void test_raw_uses_offhand_for_vertical (void)
{
	const float unused_head[3] = {NAN, 0.0f, 0.0f};
	const float offhand[3] = {30.0f, 90.0f, 0.0f};
	float out[3];

	assert (VR_LocomotionMove (VR_MOVEMENT_MODE_RAW_INPUT, unused_head, offhand,
		0.5f, -0.25f, 200.0f, 160.0f, out));
	near_vec3 (out, (float[3]){100.0f, -50.0f, -40.0f});
}

static void test_zero_and_invalid_moves (void)
{
	const float neutral[3] = {0.0f, 0.0f, 0.0f};
	const float singular[3] = {0.0f, 0.0f, 90.0f};
	const float invalid[3] = {INFINITY, 0.0f, 0.0f};
	float out[3] = {7.0f, 8.0f, 9.0f};

	assert (VR_LocomotionMove (VR_MOVEMENT_MODE_FOLLOW_HEAD, neutral, NULL,
		0.0f, 0.0f, 200.0f, 160.0f, out));
	expect_zero (out);

	out[0] = out[1] = out[2] = 7.0f;
	assert (!VR_LocomotionMove (VR_MOVEMENT_MODE_FOLLOW_HEAD, invalid, neutral,
		1.0f, 0.0f, 200.0f, 160.0f, out));
	expect_zero (out);

	out[0] = out[1] = out[2] = 7.0f;
	assert (!VR_LocomotionMove (VR_MOVEMENT_MODE_FOLLOW_HEAD, singular, neutral,
		1.0f, 0.0f, 200.0f, 160.0f, out));
	expect_zero (out);

	out[0] = out[1] = out[2] = 7.0f;
	assert (!VR_LocomotionMove (VR_MOVEMENT_MODE_RAW_INPUT, neutral, invalid,
		1.0f, 0.0f, 200.0f, 160.0f, out));
	expect_zero (out);

	out[0] = out[1] = out[2] = 7.0f;
	assert (!VR_LocomotionMove (99, neutral, neutral, 1.0f, 0.0f, 200.0f,
		160.0f, out));
	expect_zero (out);
}

static void test_aim_offset_to_world (void)
{
	const float yaw_zero[3] = {0.0f, 0.0f, 0.0f};
	const float forward_offset[3] = {0.0f, 0.0f, 10.0f};
	const float right_offset[3] = {2.0f, 0.0f, 0.0f};
	const float invalid[3] = {NAN, 0.0f, 0.0f};
	float out[3] = {7.0f, 8.0f, 9.0f};

	assert (VR_LocomotionAimOffsetToWorld (forward_offset, yaw_zero, 1.0f, out));
	near_vec3 (out, (float[3]){10.0f, 0.0f, 0.0f});

	assert (VR_LocomotionAimOffsetToWorld (right_offset, yaw_zero, 1.0f, out));
	near_vec3 (out, (float[3]){0.0f, -2.0f, 0.0f});

	assert (!VR_LocomotionAimOffsetToWorld (invalid, yaw_zero, 1.0f, out));
	expect_zero (out);
}

static float vec3_length (const float value[3])
{
	return sqrtf (value[0] * value[0] + value[1] * value[1] +
		value[2] * value[2]);
}

static void test_muzzle_offset_to_world (void)
{
	const float yaw_zero[3] = {0.0f, 0.0f, 0.0f};
	const float right_offset[3] = {2.0f, 0.0f, 0.0f};
	const float forward_offset[3] = {0.0f, 0.0f, 10.0f};
	const float offset[3] = {2.0f, -3.0f, 5.0f};
	const float wrist_angles[3] = {31.0f, 47.0f, -29.0f};
	const float invalid[3] = {NAN, 0.0f, 0.0f};
	const float overflow_offset[3] = {0.0f, 0.0f, 2.0f};
	float right[3], left[3], naive_reflection[3];
	float out[3] = {7.0f, 8.0f, 9.0f};
	float aliased_local[3] = {2.0f, 0.0f, 0.0f};
	float aliased_angles[3] = {0.0f, 0.0f, 0.0f};

	assert (VR_LocomotionMuzzleOffsetToWorld (right_offset, yaw_zero, 1.0f,
		0.0f, false, out));
	near_vec3 (out, (float[3]){0.0f, -2.0f, 0.0f});
	assert (VR_LocomotionMuzzleOffsetToWorld (right_offset, yaw_zero, 1.0f,
		0.0f, true, out));
	near_vec3 (out, (float[3]){0.0f, 2.0f, 0.0f});

	assert (VR_LocomotionMuzzleOffsetToWorld (forward_offset, yaw_zero, 1.0f,
		0.0f, false, out));
	near_vec3 (out, (float[3]){10.0f, 0.0f, 0.0f});
	assert (VR_LocomotionMuzzleOffsetToWorld (forward_offset, yaw_zero, 1.0f,
		0.0f, true, out));
	near_vec3 (out, (float[3]){10.0f, 0.0f, 0.0f});

	assert (VR_LocomotionMuzzleOffsetToWorld (offset, wrist_angles, 1.5f,
		21.0f, false, right));
	assert (VR_LocomotionMuzzleOffsetToWorld (offset, wrist_angles, 1.5f,
		21.0f, true, left));
	near_value (vec3_length (left), vec3_length (right));
	naive_reflection[0] = right[0];
	naive_reflection[1] = -right[1];
	naive_reflection[2] = right[2];
	assert (vec3_length ((float[3]){left[0] - naive_reflection[0],
		left[1] - naive_reflection[1], left[2] - naive_reflection[2]}) > 0.2f);

	assert (VR_LocomotionMuzzleOffsetToWorld (aliased_local, yaw_zero, 1.0f,
		0.0f, false, aliased_local));
	near_vec3 (aliased_local, (float[3]){0.0f, -2.0f, 0.0f});
	assert (VR_LocomotionMuzzleOffsetToWorld (right_offset, aliased_angles, 1.0f,
		0.0f, false, aliased_angles));
	near_vec3 (aliased_angles, (float[3]){0.0f, -2.0f, 0.0f});

	assert (!VR_LocomotionMuzzleOffsetToWorld (invalid, yaw_zero, 1.0f,
		0.0f, false, out));
	expect_zero (out);
	out[0] = out[1] = out[2] = 7.0f;
	assert (!VR_LocomotionMuzzleOffsetToWorld (overflow_offset, yaw_zero,
		FLT_MAX, 0.0f, false, out));
	expect_zero (out);
}

static void test_hand_body_offset (void)
{
	const float head[3] = {1.0f, 1.6f, -2.0f};
	const float hand_forward[3] = {1.0f, 1.6f, -2.5f};
	const float hand_right[3] = {1.2f, 1.6f, -2.0f};
	const float hand_lower[3] = {1.0f, 1.4f, -2.0f};
	const float overflow_head[3] = {FLT_MAX, 1.6f, -2.0f};
	const float overflow_hand[3] = {-FLT_MAX, 1.6f, -2.0f};
	float out[3] = {7.0f, 8.0f, 9.0f};
	float aliased_head[3] = {1.0f, 1.6f, -2.0f};
	float aliased_hand[3] = {1.0f, 1.6f, -2.5f};

	assert (VR_LocomotionHandBodyOffset (head, hand_forward, 0.0f, 40.0f,
		64.0f, out));
	near_vec3 (out, (float[3]){20.0f, 0.0f, 64.0f});

	assert (VR_LocomotionHandBodyOffset (head, hand_right, 0.0f, 40.0f,
		64.0f, out));
	near_vec3 (out, (float[3]){0.0f, -8.0f, 64.0f});

	assert (VR_LocomotionHandBodyOffset (head, hand_forward, 90.0f, 40.0f,
		64.0f, out));
	near_vec3 (out, (float[3]){0.0f, 20.0f, 64.0f});

	assert (VR_LocomotionHandBodyOffset (head, hand_lower, 0.0f, 40.0f,
		64.0f, out));
	near_vec3 (out, (float[3]){0.0f, 0.0f, 56.0f});

	assert (VR_LocomotionHandBodyOffset (aliased_head, hand_forward, 0.0f, 40.0f,
		64.0f, aliased_head));
	near_vec3 (aliased_head, (float[3]){20.0f, 0.0f, 64.0f});
	assert (VR_LocomotionHandBodyOffset (head, aliased_hand, 0.0f, 40.0f,
		64.0f, aliased_hand));
	near_vec3 (aliased_hand, (float[3]){20.0f, 0.0f, 64.0f});

	assert (!VR_LocomotionHandBodyOffset (head, hand_forward, NAN, 40.0f,
		64.0f, out));
	expect_zero (out);
	out[0] = out[1] = out[2] = 7.0f;
	assert (!VR_LocomotionHandBodyOffset (head, hand_forward, 0.0f, 0.0f,
		64.0f, out));
	expect_zero (out);
	out[0] = out[1] = out[2] = 7.0f;
	assert (!VR_LocomotionHandBodyOffset (overflow_head, overflow_hand, 0.0f,
		1.0f, 64.0f, out));
	expect_zero (out);
}

static void test_gun_angle_composition (void)
{
	const float identity[3][4] = {
		{1.0f, 0.0f, 0.0f, 0.0f},
		{0.0f, 1.0f, 0.0f, 0.0f},
		{0.0f, 0.0f, 1.0f, 0.0f}
	};
	const float roll_right[3][4] = {
		{0.7071068f, -0.7071068f, 0.0f, 0.0f},
		{0.7071068f, 0.7071068f, 0.0f, 0.0f},
		{0.0f, 0.0f, 1.0f, 0.0f}
	};
	float out[3];

	assert (VR_LocomotionHandAngles (roll_right, 0.0f, 0.0f, out));
	near_vec3 (out, (float[3]){0.0f, 0.0f, -45.0f});

	assert (VR_LocomotionHandAngles (identity, 15.0f, 32.0f, out));
	near_vec3 (out, (float[3]){32.0f, 15.0f, 0.0f});

	/* Pitching a rolled controller is a true matrix composition: all three
	 * resulting Quake angles change, rather than simply adding gun pitch. */
	assert (VR_LocomotionHandAngles (roll_right, 0.0f, 32.0f, out));
	near_vec3 (out, (float[3]){22.00636f, -23.83821f, -49.70045f});
}

static void test_invalid_hand_angles (void)
{
	float invalid[3][4] = {
		{1.0f, 0.0f, 0.0f, 0.0f},
		{0.0f, 1.0f, 0.0f, 0.0f},
		{0.0f, NAN, 1.0f, 0.0f}
	};
	float out[3] = {7.0f, 8.0f, 9.0f};

	assert (!VR_LocomotionHandAngles (invalid, 0.0f, 32.0f, out));
	expect_zero (out);
	out[0] = out[1] = out[2] = 7.0f;
	assert (!VR_LocomotionHandAngles (invalid, 0.0f, NAN, out));
	expect_zero (out);
}

int main (void)
{
	test_neutral_and_cardinal_modes ();
	test_pitch_roll_and_near_vertical ();
	test_raw_uses_offhand_for_vertical ();
	test_zero_and_invalid_moves ();
	test_aim_offset_to_world ();
	test_muzzle_offset_to_world ();
	test_hand_body_offset ();
	test_gun_angle_composition ();
	test_invalid_hand_angles ();
	puts ("Inherited VR locomotion and gun-angle arithmetic preserves source geometry");
	return 0;
}
