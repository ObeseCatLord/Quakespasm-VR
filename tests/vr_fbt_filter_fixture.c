#include "vr_fbt_filter.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static int Close(float a, float b)
{
	return fabs(a - b) < 0.002f;
}

static float NaNValue(void)
{
	volatile float zero = 0.0f;
	return zero / zero;
}

static vr_fbt_filter_input_t Input(uint64_t id, double time,
	uint64_t identity, float x, float y, float z)
{
	vr_fbt_filter_input_t input;

	memset(&input, 0, sizeof(input));
	input.snapshot_id = id;
	input.snapshot_time = time;
	input.identity = identity;
	input.identity_valid = 1;
	input.connected = 1;
	input.tracking_valid = 1;
	input.position[0] = x;
	input.position[1] = y;
	input.position[2] = z;
	input.orientation[0] = 1.0f;
	input.root_yaw_valid = 1;
	return input;
}

static vr_fbt_filter_output_t Update(vr_fbt_filter_t *filter,
	vr_fbt_filter_role_t role, vr_fbt_filter_input_t *input)
{
	vr_fbt_filter_output_t output;
	assert(VR_FBT_FilterUpdate(filter, role, input, &output));
	return output;
}

static void TestOneFootDisconnectReconnectAndIdentity(void)
{
	vr_fbt_filter_t filter;
	vr_fbt_filter_input_t left;
	vr_fbt_filter_input_t hip;
	vr_fbt_filter_output_t output;

	VR_FBT_FilterInit(&filter);
	left = Input(1, 0.0, 11, 0.0f, 0.0f, 0.0f);
	hip = Input(1, 0.0, 22, 3.0f, 0.0f, 1.0f);
	output = Update(&filter, VR_FBT_FILTER_ROLE_LEFT_FOOT, &left);
	assert(output.tracked && Close(output.confidence, 0.0f));
	output = Update(&filter, VR_FBT_FILTER_ROLE_HIP, &hip);
	assert(output.tracked && !output.planted);
	left = Input(2, 0.15, 11, 0.0f, 0.0f, 0.0f);
	output = Update(&filter, VR_FBT_FILTER_ROLE_LEFT_FOOT, &left);
	assert(Close(output.confidence, 1.0f));
	hip = Input(2, 0.15, 22, 3.0f, 0.0f, 1.0f);
	output = Update(&filter, VR_FBT_FILTER_ROLE_HIP, &hip);
	assert(output.state == VR_FBT_FILTER_STATE_TRACKING);
	left = Input(3, 0.18, 11, 0.0f, 0.0f, 0.0f);
	left.connected = 0;
	left.tracking_valid = 0;
	output = Update(&filter, VR_FBT_FILTER_ROLE_LEFT_FOOT, &left);
	assert(output.state == VR_FBT_FILTER_STATE_PREDICTING && !output.tracked);
	assert(VR_FBT_FilterGetOutput(&filter, VR_FBT_FILTER_ROLE_HIP, &output));
	assert(output.state == VR_FBT_FILTER_STATE_TRACKING && Close(output.position[0], 3.0f));
	left = Input(4, 0.20, 11, 100.0f, 0.0f, 0.0f);
	output = Update(&filter, VR_FBT_FILTER_ROLE_LEFT_FOOT, &left);
	assert(Close(output.position[0], 100.0f) && Close(output.confidence, 0.0f));
	left = Input(5, 0.22, 99, -4.0f, 0.0f, 0.0f);
	output = Update(&filter, VR_FBT_FILTER_ROLE_LEFT_FOOT, &left);
	assert(Close(output.position[0], -4.0f) && Close(output.confidence, 0.0f));
}

static void TestQuaternionWrapPredictionAndLoss(void)
{
	vr_fbt_filter_t filter;
	vr_fbt_filter_input_t input;
	vr_fbt_filter_output_t output;

	VR_FBT_FilterInit(&filter);
	input = Input(1, 1.0, 1, 0.0f, 0.0f, 0.0f);
	input.linear_velocity[0] = 1.0f;
	output = Update(&filter, VR_FBT_FILTER_ROLE_HIP, &input);
	input = Input(2, 1.15, 1, 0.0f, 0.0f, 0.0f);
	input.orientation[0] = -1.0f;
	input.linear_velocity[0] = 1.0f;
	output = Update(&filter, VR_FBT_FILTER_ROLE_HIP, &input);
	assert(output.orientation[0] > 0.99f && fabs(output.orientation[1]) < 0.001f);
	input = Input(3, 1.19, 1, 0.0f, 0.0f, 0.0f);
	input.connected = 0;
	input.tracking_valid = 0;
	output = Update(&filter, VR_FBT_FILTER_ROLE_HIP, &input);
	assert(output.state == VR_FBT_FILTER_STATE_PREDICTING);
	assert(!output.tracked);
	assert(Close(output.position[0], 0.04f));
	input = Input(4, 1.30, 1, 0.0f, 0.0f, 0.0f);
	input.connected = 0;
	input.tracking_valid = 0;
	output = Update(&filter, VR_FBT_FILTER_ROLE_HIP, &input);
	assert(output.state == VR_FBT_FILTER_STATE_HOLDING);
	assert(!output.tracked);
	assert(Close(output.position[0], 0.05f));
	input = Input(5, 1.41, 1, 0.0f, 0.0f, 0.0f);
	input.connected = 0;
	input.tracking_valid = 0;
	output = Update(&filter, VR_FBT_FILTER_ROLE_HIP, &input);
	assert(output.state == VR_FBT_FILTER_STATE_LOST && !output.tracked);
	assert(Close(output.confidence, 0.0f));
}

static void TestShortOcclusionKeepsFilterContinuity(void)
{
	vr_fbt_filter_t filter;
	vr_fbt_filter_input_t input;
	vr_fbt_filter_output_t output;

	VR_FBT_FilterInit(&filter);
	input = Input(1, 0.0, 123, 0.0f, 0.0f, 1.0f);
	output = Update(&filter, VR_FBT_FILTER_ROLE_HIP, &input);
	assert(Close(output.confidence, 0.0f));
	input = Input(2, 0.15, 123, 0.0f, 0.0f, 1.0f);
	output = Update(&filter, VR_FBT_FILTER_ROLE_HIP, &input);
	assert(Close(output.confidence, 1.0f));
	input = Input(3, 0.17, 123, 0.0f, 0.0f, 1.0f);
	input.tracking_valid = 0;
	output = Update(&filter, VR_FBT_FILTER_ROLE_HIP, &input);
	assert(output.state == VR_FBT_FILTER_STATE_PREDICTING);
	assert(!output.tracked);
	assert(Close(output.confidence, 1.0f));
	input = Input(4, 0.19, 123, 0.10f, 0.0f, 1.0f);
	output = Update(&filter, VR_FBT_FILTER_ROLE_HIP, &input);
	assert(output.state == VR_FBT_FILTER_STATE_TRACKING);
	assert(output.confidence > 0.99f);
	assert(output.position[0] > 0.0f && output.position[0] < 0.10f);
}

static void TestInvalidTimeAndWrappedSnapshot(void)
{
	vr_fbt_filter_t filter;
	vr_fbt_filter_input_t input;
	vr_fbt_filter_output_t before;
	vr_fbt_filter_output_t after;

	VR_FBT_FilterInit(&filter);
	input = Input(UINT64_MAX, 1.0, 7, 1.0f, 0.0f, 0.0f);
	before = Update(&filter, VR_FBT_FILTER_ROLE_RIGHT_FOOT, &input);
	input = Input(0, 1.01, 7, 2.0f, 0.0f, 0.0f);
	after = Update(&filter, VR_FBT_FILTER_ROLE_RIGHT_FOOT, &input);
	assert(after.tracked);
	input = Input(1, 0.99, 7, 900.0f, 0.0f, 0.0f);
	assert(!VR_FBT_FilterUpdate(&filter, VR_FBT_FILTER_ROLE_RIGHT_FOOT,
		&input, &after));
	assert(!memcmp(&after, &filter.roles[VR_FBT_FILTER_ROLE_RIGHT_FOOT].output,
		sizeof(after)));
	assert(before.tracked);
	input = Input(1, 1.02, 7, NaNValue(), 0.0f, 0.0f);
	assert(VR_FBT_FilterUpdate(&filter, VR_FBT_FILTER_ROLE_RIGHT_FOOT,
		&input, &after));
	assert(after.state == VR_FBT_FILTER_STATE_PREDICTING);
	assert(!after.tracked);
}

static void TestConservativeFootPlant(void)
{
	vr_fbt_filter_t filter;
	vr_fbt_filter_input_t input;
	vr_fbt_filter_output_t output;

	VR_FBT_FilterInit(&filter);
	input = Input(1, 0.0, 44, 1.0f, 2.0f, 0.01f);
	input.floor_valid = 1;
	input.floor_height = 0.0f;
	output = Update(&filter, VR_FBT_FILTER_ROLE_LEFT_FOOT, &input);
	assert(!output.planted);
	input = Input(2, 0.13, 44, 1.0f, 2.0f, 0.01f);
	input.floor_valid = 1;
	input.floor_height = 0.0f;
	output = Update(&filter, VR_FBT_FILTER_ROLE_LEFT_FOOT, &input);
	assert(output.planted);
	input = Input(3, 0.16, 44, 2.0f, 2.0f, 0.01f);
	input.floor_valid = 1;
	input.floor_height = 0.0f;
	output = Update(&filter, VR_FBT_FILTER_ROLE_LEFT_FOOT, &input);
	assert(output.planted && Close(output.position[0], 1.0f));
	input = Input(4, 0.20, 44, 2.0f, 2.0f, 0.01f);
	input.floor_valid = 1;
	input.floor_height = 0.0f;
	input.linear_velocity[0] = 1.0f;
	output = Update(&filter, VR_FBT_FILTER_ROLE_LEFT_FOOT, &input);
	assert(!output.planted && output.position[0] > 1.0f);
	input = Input(1, 0.0, 55, 1.0f, 0.0f, 0.01f);
	input.floor_valid = 1;
	input.floor_height = 0.0f;
	output = Update(&filter, VR_FBT_FILTER_ROLE_HIP, &input);
	assert(!output.planted);
}

static void TestRootYawRebaseAndAngularPrediction(void)
{
	vr_fbt_filter_t filter;
	vr_fbt_filter_input_t input;
	vr_fbt_filter_output_t output;
	const float root_half_turn = 0.70710678f;

	VR_FBT_FilterInit(&filter);
	input = Input(1, 0.0, 88, 1.0f, 0.0f, 0.0f);
	output = Update(&filter, VR_FBT_FILTER_ROLE_HIP, &input);
	input = Input(2, 0.15, 88, 0.0f, -1.0f, 0.0f);
	input.root_yaw_degrees = 90.0f;
	input.orientation[0] = root_half_turn;
	input.orientation[3] = -root_half_turn;
	input.angular_velocity[2] = 1.0f;
	output = Update(&filter, VR_FBT_FILTER_ROLE_HIP, &input);
	assert(Close(output.position[0], 0.0f));
	assert(Close(output.position[1], -1.0f));
	assert(Close(output.orientation[0], root_half_turn));
	assert(Close(output.orientation[3], -root_half_turn));

	input = Input(3, 0.19, 88, 0.0f, -1.0f, 0.0f);
	input.root_yaw_degrees = 90.0f;
	input.tracking_valid = 0;
	output = Update(&filter, VR_FBT_FILTER_ROLE_HIP, &input);
	assert(output.state == VR_FBT_FILTER_STATE_PREDICTING && !output.tracked);
	/* q * delta(-omega): starting at -90 root yaw, +Z motion decreases z. */
	assert(output.orientation[3] < -root_half_turn);
}

static void TestCorrectedPointVelocity(void)
{
	const float linear[3] = {0.0f, 0.0f, 0.0f};
	const float angular[3] = {0.0f, 0.0f, 2.0f};
	const float offset[3] = {1.0f, 0.0f, 0.0f};
	float output[3];

	assert(VR_FBT_FilterCorrectedPointVelocity(output, linear, angular, offset));
	assert(Close(output[0], 0.0f));
	assert(Close(output[1], 2.0f));
	assert(Close(output[2], 0.0f));
}

int main(void)
{
	TestOneFootDisconnectReconnectAndIdentity();
	TestQuaternionWrapPredictionAndLoss();
	TestShortOcclusionKeepsFilterContinuity();
	TestInvalidTimeAndWrappedSnapshot();
	TestConservativeFootPlant();
	TestRootYawRebaseAndAngularPrediction();
	TestCorrectedPointVelocity();
	return 0;
}
