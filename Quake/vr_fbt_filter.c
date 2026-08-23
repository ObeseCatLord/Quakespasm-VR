#include "vr_fbt_filter.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define VR_FBT_FILTER_RAMP_SECONDS 0.150
#define VR_FBT_FILTER_POSITION_TIME_CONSTANT 0.030
#define VR_FBT_FILTER_MAX_LINEAR_SPEED 4.0f
#define VR_FBT_FILTER_MAX_ANGULAR_SPEED 12.0f
#define VR_FBT_FILTER_PLANT_CONFIRM_SECONDS 0.120
#define VR_FBT_FILTER_PLANT_HEIGHT 0.035f
#define VR_FBT_FILTER_UNPLANT_HEIGHT 0.060f
#define VR_FBT_FILTER_PLANT_SPEED 0.20f
#define VR_FBT_FILTER_UNPLANT_SPEED 0.35f
#define VR_FBT_FILTER_DEGREES_TO_RADIANS 0.01745329251994329577f

static int VR_FBT_FilterRoleIsValid(vr_fbt_filter_role_t role)
{
	return role >= VR_FBT_FILTER_ROLE_HIP &&
		role < VR_FBT_FILTER_ROLE_COUNT;
}

static int VR_FBT_FilterFiniteDouble(double value)
{
	return value == value && value <= DBL_MAX && value >= -DBL_MAX;
}

static int VR_FBT_FilterFiniteFloat(float value)
{
	return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}

static int VR_FBT_FilterFiniteVector(const float *value, int count)
{
	int i;
	for (i = 0; i < count; ++i)
		if (!VR_FBT_FilterFiniteFloat(value[i]))
			return 0;
	return 1;
}

static int VR_FBT_FilterSnapshotIsNewer(uint64_t candidate, uint64_t previous)
{
	uint64_t distance = candidate - previous;
	return distance && distance < (UINT64_C(1) << 63);
}

static float VR_FBT_FilterClamp(float value, float minimum, float maximum)
{
	if (value < minimum)
		return minimum;
	if (value > maximum)
		return maximum;
	return value;
}

static float VR_FBT_FilterLength(const float *value)
{
	return (float)sqrt((double)value[0] * value[0] +
		(double)value[1] * value[1] + (double)value[2] * value[2]);
}

static void VR_FBT_FilterCopy3(float *destination, const float *source)
{
	destination[0] = source[0];
	destination[1] = source[1];
	destination[2] = source[2];
}

static void VR_FBT_FilterCopy4(float *destination, const float *source)
{
	destination[0] = source[0];
	destination[1] = source[1];
	destination[2] = source[2];
	destination[3] = source[3];
}

static int VR_FBT_FilterNormalizeQuaternion(float *quaternion)
{
	double length = sqrt((double)quaternion[0] * quaternion[0] +
		(double)quaternion[1] * quaternion[1] +
		(double)quaternion[2] * quaternion[2] +
		(double)quaternion[3] * quaternion[3]);
	if (length < 0.000001 || length > DBL_MAX)
		return 0;
	quaternion[0] = (float)(quaternion[0] / length);
	quaternion[1] = (float)(quaternion[1] / length);
	quaternion[2] = (float)(quaternion[2] / length);
	quaternion[3] = (float)(quaternion[3] / length);
	return 1;
}

static void VR_FBT_FilterSlerp(float *result, const float *a, const float *b,
	float fraction)
{
	float target[4];
	float dot;
	float theta;
	float sine;
	float a_weight;
	float b_weight;

	dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
	VR_FBT_FilterCopy4(target, b);
	if (dot < 0.0f) {
		dot = -dot;
		target[0] = -target[0];
		target[1] = -target[1];
		target[2] = -target[2];
		target[3] = -target[3];
	}
	dot = VR_FBT_FilterClamp(dot, -1.0f, 1.0f);
	if (dot > 0.9995f) {
		result[0] = a[0] + fraction * (target[0] - a[0]);
		result[1] = a[1] + fraction * (target[1] - a[1]);
		result[2] = a[2] + fraction * (target[2] - a[2]);
		result[3] = a[3] + fraction * (target[3] - a[3]);
		VR_FBT_FilterNormalizeQuaternion(result);
		return;
	}
	theta = (float)acos(dot);
	sine = (float)sin(theta);
	if (fabs(sine) < 0.000001f) {
		VR_FBT_FilterCopy4(result, a);
		return;
	}
	a_weight = (float)sin((1.0f - fraction) * theta) / sine;
	b_weight = (float)sin(fraction * theta) / sine;
	result[0] = a_weight * a[0] + b_weight * target[0];
	result[1] = a_weight * a[1] + b_weight * target[1];
	result[2] = a_weight * a[2] + b_weight * target[2];
	result[3] = a_weight * a[3] + b_weight * target[3];
	VR_FBT_FilterNormalizeQuaternion(result);
}

static void VR_FBT_FilterMultiplyQuaternion(float *result, const float *a,
	const float *b)
{
	float value[4];
	value[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
	value[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
	value[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
	value[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
	VR_FBT_FilterCopy4(result, value);
}

static void VR_FBT_FilterBoundVelocity(float *result, const float *source,
	float maximum)
{
	float length;
	if (!VR_FBT_FilterFiniteVector(source, 3)) {
		result[0] = result[1] = result[2] = 0.0f;
		return;
	}
	length = VR_FBT_FilterLength(source);
	if (length <= 0.000001f) {
		result[0] = result[1] = result[2] = 0.0f;
		return;
	}
	if (length > maximum) {
		float scale = maximum / length;
		result[0] = source[0] * scale;
		result[1] = source[1] * scale;
		result[2] = source[2] * scale;
		return;
	}
	VR_FBT_FilterCopy3(result, source);
}

int VR_FBT_FilterCorrectedPointVelocity(float output[3],
	const float linear_velocity[3], const float angular_velocity[3],
	const float device_to_point[3])
{
	if (!output || !linear_velocity || !angular_velocity || !device_to_point ||
		!VR_FBT_FilterFiniteVector(linear_velocity, 3) ||
		!VR_FBT_FilterFiniteVector(angular_velocity, 3) ||
		!VR_FBT_FilterFiniteVector(device_to_point, 3))
		return 0;
	output[0] = linear_velocity[0] + angular_velocity[1] * device_to_point[2] -
		angular_velocity[2] * device_to_point[1];
	output[1] = linear_velocity[1] + angular_velocity[2] * device_to_point[0] -
		angular_velocity[0] * device_to_point[2];
	output[2] = linear_velocity[2] + angular_velocity[0] * device_to_point[1] -
		angular_velocity[1] * device_to_point[0];
	return VR_FBT_FilterFiniteVector(output, 3);
}

static void VR_FBT_FilterPredictOrientation(float *result, const float *base,
	const float *angular_velocity, double age)
{
	float bounded[3];
	float axis_length;
	float angle;
	float delta[4];

	VR_FBT_FilterBoundVelocity(bounded, angular_velocity,
		VR_FBT_FILTER_MAX_ANGULAR_SPEED);
	axis_length = VR_FBT_FilterLength(bounded);
	if (axis_length <= 0.000001f || age <= 0.0) {
		VR_FBT_FilterCopy4(result, base);
		return;
	}
	angle = (float)(axis_length * age);
	delta[0] = (float)cos(angle * 0.5f);
	delta[1] = bounded[0] / axis_length * (float)sin(angle * 0.5f);
	delta[2] = bounded[1] / axis_length * (float)sin(angle * 0.5f);
	delta[3] = bounded[2] / axis_length * (float)sin(angle * 0.5f);
	/* Root-local quaternions use the same row-basis convention as their input
	 * matrices.  Angular velocity is expressed in that local basis, hence
	 * q(t) = q(0) * exp(-omega * t / 2), not delta * q. */
	delta[1] = -delta[1];
	delta[2] = -delta[2];
	delta[3] = -delta[3];
	VR_FBT_FilterMultiplyQuaternion(result, base, delta);
	VR_FBT_FilterNormalizeQuaternion(result);
}

static void VR_FBT_FilterRebaseVector(float vector[3], float yaw_delta)
{
	const float radians = yaw_delta * VR_FBT_FILTER_DEGREES_TO_RADIANS;
	const float cosine = cosf(radians);
	const float sine = sinf(radians);
	const float x = vector[0];
	const float y = vector[1];

	vector[0] = cosine * x - sine * y;
	vector[1] = sine * x + cosine * y;
}

static void VR_FBT_FilterRebaseOrientation(float orientation[4],
	float yaw_delta)
{
	const float radians = yaw_delta * VR_FBT_FILTER_DEGREES_TO_RADIANS;
	float delta[4];

	delta[0] = cosf(radians * 0.5f);
	delta[1] = 0.0f;
	delta[2] = 0.0f;
	delta[3] = sinf(radians * 0.5f);
	VR_FBT_FilterMultiplyQuaternion(orientation, orientation, delta);
	VR_FBT_FilterNormalizeQuaternion(orientation);
}

static void VR_FBT_FilterRebaseRootFrame(vr_fbt_filter_role_state_t *state,
	const vr_fbt_filter_input_t *input)
{
	float yaw_delta;

	if (!input->root_yaw_valid ||
		!VR_FBT_FilterFiniteFloat(input->root_yaw_degrees))
		return;
	if (!state->has_root_yaw) {
		state->root_yaw_degrees = input->root_yaw_degrees;
		state->has_root_yaw = 1;
		return;
	}
	yaw_delta = state->root_yaw_degrees - input->root_yaw_degrees;
	if (yaw_delta != 0.0f) {
		VR_FBT_FilterRebaseVector(state->output.position, yaw_delta);
		VR_FBT_FilterRebaseVector(state->last_valid_position, yaw_delta);
		VR_FBT_FilterRebaseVector(state->last_valid_linear_velocity, yaw_delta);
		VR_FBT_FilterRebaseVector(state->last_valid_angular_velocity, yaw_delta);
		VR_FBT_FilterRebaseVector(state->planted_position, yaw_delta);
		VR_FBT_FilterRebaseOrientation(state->output.orientation, yaw_delta);
		VR_FBT_FilterRebaseOrientation(state->last_valid_orientation, yaw_delta);
	}
	state->root_yaw_degrees = input->root_yaw_degrees;
}

static void VR_FBT_FilterPredict(vr_fbt_filter_role_state_t *state,
	double age, vr_fbt_filter_output_t *output)
{
	float bounded[3];
	VR_FBT_FilterBoundVelocity(bounded, state->last_valid_linear_velocity,
		VR_FBT_FILTER_MAX_LINEAR_SPEED);
	output->position[0] = state->last_valid_position[0] +
		(float)(bounded[0] * age);
	output->position[1] = state->last_valid_position[1] +
		(float)(bounded[1] * age);
	output->position[2] = state->last_valid_position[2] +
		(float)(bounded[2] * age);
	VR_FBT_FilterPredictOrientation(output->orientation,
		state->last_valid_orientation, state->last_valid_angular_velocity, age);
	if (state->planted) {
		output->position[0] = state->planted_position[0];
		output->position[1] = state->planted_position[1];
	}
}

static int VR_FBT_FilterIsFoot(vr_fbt_filter_role_t role)
{
	return role == VR_FBT_FILTER_ROLE_LEFT_FOOT ||
		role == VR_FBT_FILTER_ROLE_RIGHT_FOOT;
}

static void VR_FBT_FilterUpdatePlant(vr_fbt_filter_role_state_t *state,
	vr_fbt_filter_role_t role, const vr_fbt_filter_input_t *input,
	double now)
{
	float speed;
	float height;
	int candidate;

	if (!VR_FBT_FilterIsFoot(role) || !input->floor_valid ||
		!VR_FBT_FilterFiniteFloat(input->floor_height) ||
		!VR_FBT_FilterFiniteVector(input->linear_velocity, 3)) {
		state->planted = 0;
		state->plant_candidate_time = -1.0;
		return;
	}
	speed = VR_FBT_FilterLength(input->linear_velocity);
	height = (float)fabs(input->position[2] - input->floor_height);
	if (state->planted && (speed > VR_FBT_FILTER_UNPLANT_SPEED ||
		height > VR_FBT_FILTER_UNPLANT_HEIGHT)) {
		state->planted = 0;
		state->plant_candidate_time = -1.0;
	}
	candidate = speed <= VR_FBT_FILTER_PLANT_SPEED &&
		height <= VR_FBT_FILTER_PLANT_HEIGHT;
	if (!candidate) {
		if (!state->planted)
			state->plant_candidate_time = -1.0;
		return;
	}
	if (state->plant_candidate_time < 0.0)
		state->plant_candidate_time = now;
	if (!state->planted && now - state->plant_candidate_time >=
		VR_FBT_FILTER_PLANT_CONFIRM_SECONDS) {
		state->planted = 1;
		VR_FBT_FilterCopy3(state->planted_position, state->output.position);
	}
}

void VR_FBT_FilterInit(vr_fbt_filter_t *filter)
{
	int role;
	if (!filter)
		return;
	memset(filter, 0, sizeof(*filter));
	for (role = 0; role < VR_FBT_FILTER_ROLE_COUNT; ++role) {
		filter->roles[role].output.state = VR_FBT_FILTER_STATE_LOST;
		filter->roles[role].plant_candidate_time = -1.0;
	}
}

void VR_FBT_FilterResetRole(vr_fbt_filter_t *filter,
	vr_fbt_filter_role_t role)
{
	if (!filter || !VR_FBT_FilterRoleIsValid(role))
		return;
	memset(&filter->roles[role], 0, sizeof(filter->roles[role]));
	filter->roles[role].output.state = VR_FBT_FILTER_STATE_LOST;
	filter->roles[role].plant_candidate_time = -1.0;
}

static void VR_FBT_FilterSetLost(vr_fbt_filter_role_state_t *state)
{
	state->output.tracked = 0;
	state->output.planted = 0;
	state->output.confidence = 0.0f;
	state->output.state = VR_FBT_FILTER_STATE_LOST;
	state->planted = 0;
	state->plant_candidate_time = -1.0;
}

int VR_FBT_FilterGetOutput(const vr_fbt_filter_t *filter,
	vr_fbt_filter_role_t role, vr_fbt_filter_output_t *output)
{
	if (!filter || !output || !VR_FBT_FilterRoleIsValid(role))
		return 0;
	*output = filter->roles[role].output;
	return 1;
}

int VR_FBT_FilterUpdate(vr_fbt_filter_t *filter, vr_fbt_filter_role_t role,
	const vr_fbt_filter_input_t *input, vr_fbt_filter_output_t *output)
{
	vr_fbt_filter_role_state_t *state;
	float normalized[4];
	float alpha;
	double age;
	double delta;
	int valid;

	if (!filter || !input || !VR_FBT_FilterRoleIsValid(role))
		return 0;
	state = &filter->roles[role];
	if (!VR_FBT_FilterFiniteDouble(input->snapshot_time) ||
		(state->has_snapshot && (!VR_FBT_FilterSnapshotIsNewer(
		input->snapshot_id, state->last_snapshot_id) ||
		input->snapshot_time < state->last_snapshot_time))) {
		if (output)
			*output = state->output;
		return 0;
	}
	state->last_snapshot_id = input->snapshot_id;
	state->last_snapshot_time = input->snapshot_time;
	state->has_snapshot = 1;
	if (input->identity_valid && state->has_identity &&
		input->identity != state->identity) {
		VR_FBT_FilterResetRole(filter, role);
		state = &filter->roles[role];
		state->last_snapshot_id = input->snapshot_id;
		state->last_snapshot_time = input->snapshot_time;
		state->has_snapshot = 1;
	}
	if (input->identity_valid) {
		state->identity = input->identity;
		state->has_identity = 1;
	}
	VR_FBT_FilterRebaseRootFrame(state, input);
	valid = input->identity_valid && input->connected && input->tracking_valid &&
		VR_FBT_FilterFiniteVector(input->position, 3) &&
		VR_FBT_FilterFiniteVector(input->orientation, 4);
	VR_FBT_FilterCopy4(normalized, input->orientation);
	if (valid && !VR_FBT_FilterNormalizeQuaternion(normalized))
		valid = 0;
	if (valid) {
		if (!state->has_valid || state->reset_on_valid) {
			VR_FBT_FilterCopy3(state->output.position, input->position);
			VR_FBT_FilterCopy4(state->output.orientation, normalized);
			state->output.confidence = 0.0f;
			state->reset_on_valid = 0;
			state->planted = 0;
			state->plant_candidate_time = -1.0;
		} else {
			delta = input->snapshot_time - state->last_valid_time;
			alpha = (float)(delta / (VR_FBT_FILTER_POSITION_TIME_CONSTANT + delta));
			alpha = VR_FBT_FilterClamp(alpha, 0.0f, 1.0f);
			state->output.position[0] += alpha *
				(input->position[0] - state->output.position[0]);
			state->output.position[1] += alpha *
				(input->position[1] - state->output.position[1]);
			state->output.position[2] += alpha *
				(input->position[2] - state->output.position[2]);
			VR_FBT_FilterSlerp(state->output.orientation,
				state->output.orientation, normalized, alpha);
			state->output.confidence = VR_FBT_FilterClamp(
				state->output.confidence + (float)(delta /
				VR_FBT_FILTER_RAMP_SECONDS), 0.0f, 1.0f);
		}
		state->has_valid = 1;
		state->last_valid_time = input->snapshot_time;
		VR_FBT_FilterCopy3(state->last_valid_position, state->output.position);
		VR_FBT_FilterCopy4(state->last_valid_orientation,
			state->output.orientation);
		VR_FBT_FilterBoundVelocity(state->last_valid_linear_velocity,
			input->linear_velocity, VR_FBT_FILTER_MAX_LINEAR_SPEED);
		VR_FBT_FilterBoundVelocity(state->last_valid_angular_velocity,
			input->angular_velocity, VR_FBT_FILTER_MAX_ANGULAR_SPEED);
		VR_FBT_FilterUpdatePlant(state, role, input, input->snapshot_time);
		if (state->planted) {
			state->output.position[0] = state->planted_position[0];
			state->output.position[1] = state->planted_position[1];
			VR_FBT_FilterCopy3(state->last_valid_position,
				state->output.position);
		}
		state->output.tracked = 1;
		state->output.planted = state->planted;
		state->output.state = VR_FBT_FILTER_STATE_TRACKING;
	} else if (!state->has_valid) {
		state->reset_on_valid = 1;
		VR_FBT_FilterSetLost(state);
	} else {
		age = input->snapshot_time - state->last_valid_time;
		/*
		 * A connected tracker with a one-frame invalid pose is an ordinary
		 * occlusion.  Its same-identity recovery must continue the filter,
		 * rather than visibly snap and restart confidence.  Disconnects and
		 * an exhausted hold window deliberately require a fresh ramp-in.
		 */
		if (!input->connected)
			state->reset_on_valid = 1;
		if (age <= VR_FBT_FILTER_PREDICT_SECONDS) {
			VR_FBT_FilterPredict(state, age, &state->output);
			state->output.tracked = 0;
			state->output.planted = state->planted;
			state->output.state = VR_FBT_FILTER_STATE_PREDICTING;
		} else if (age <= VR_FBT_FILTER_HOLD_SECONDS) {
			VR_FBT_FilterPredict(state, VR_FBT_FILTER_PREDICT_SECONDS,
				&state->output);
			state->output.tracked = 0;
			state->output.planted = state->planted;
			state->output.confidence = VR_FBT_FilterClamp(
				state->output.confidence * (float)((VR_FBT_FILTER_HOLD_SECONDS - age) /
				(VR_FBT_FILTER_HOLD_SECONDS - VR_FBT_FILTER_PREDICT_SECONDS)),
				0.0f, 1.0f);
			state->output.state = VR_FBT_FILTER_STATE_HOLDING;
		} else {
			state->reset_on_valid = 1;
			VR_FBT_FilterSetLost(state);
		}
	}
	if (output)
		*output = state->output;
	return 1;
}
