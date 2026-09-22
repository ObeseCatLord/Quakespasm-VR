/* Exercise the inherited VR locomotion and controller-angle arithmetic. */
#include "../Quake/vr_locomotion.h"

#include <assert.h>
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
	test_gun_angle_composition ();
	test_invalid_hand_angles ();
	puts ("Inherited VR locomotion and gun-angle arithmetic preserves source geometry");
	return 0;
}
