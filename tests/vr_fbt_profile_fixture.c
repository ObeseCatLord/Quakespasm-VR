#include "vr_fbt_profile.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static vr_fbt_profile_transform_t Identity(void)
{
	vr_fbt_profile_transform_t value;
	memset(&value, 0, sizeof(value));
	value.orientation[0] = 1.0;
	return value;
}

static int Close(double a, double b)
{
	return fabs(a - b) < 0.00001;
}

static vr_fbt_profile_t Profile(void)
{
	vr_fbt_profile_t profile;
	memset(&profile, 0, sizeof(profile));
	strcpy(profile.name, "test_profile");
	profile.schema_version = VR_FBT_PROFILE_SCHEMA_VERSION;
	profile.calibration_algorithm = VR_FBT_PROFILE_CALIBRATION_ALGORITHM;
	strcpy(profile.avatar_fingerprint, VR_FBT_PROFILE_AVATAR_FINGERPRINT);
	strcpy(profile.hmd_serial, "hmd.1");
	profile.hmd_height_metres = 1.700001;
	profile.floor_height_metres = 0.0;
	profile.body_forward[0] = 0.6;
	profile.body_forward[2] = 0.8;
	profile.roles[VR_FBT_ROLE_HIP].present = 1;
	strcpy(profile.roles[VR_FBT_ROLE_HIP].serial, "hip.1");
	profile.roles[VR_FBT_ROLE_HIP].device_to_anatomical = Identity();
	profile.roles[VR_FBT_ROLE_HIP].device_to_anatomical.orientation[0] = 0.7071067811865475244;
	profile.roles[VR_FBT_ROLE_HIP].device_to_anatomical.orientation[2] = 0.7071067811865475244;
	return profile;
}

static void TestStrictText(void)
{
	vr_fbt_profile_t profile = Profile(), parsed, unchanged;
	vr_fbt_profile_error_t error;
	char text[2048], second[2048];
	size_t length, second_length;
	const char *bad[] = {
		"VRFBT-PROFILE 1\nname x\nalgorithm 1\navatar ranger_verified_rerelease\nhmd_serial -\nhmd_height_um 0\nfloor_um 0\nbody_forward_q30 0 0 1073741824\nrole hip s 0 0 0 1073741824 0 0 0\nrole hip s 0 0 0 1073741824 0 0 0\nend\n",
		"VRFBT-PROFILE 1\nname ../x\nalgorithm 1\navatar ranger_verified_rerelease\nhmd_serial -\nhmd_height_um 0\nfloor_um 0\nbody_forward_q30 0 0 1073741824\nend\n",
		"VRFBT-PROFILE 1\nname x\nalgorithm 1\navatar ranger_verified_rerelease\nhmd_serial -\nhmd_height_um 2000001\nfloor_um 0\nbody_forward_q30 0 0 1073741824\nend\n",
		"VRFBT-PROFILE 1\nname x\nalgorithm 1\navatar ranger_verified_rerelease\nhmd_serial -\nhmd_height_um 999999999999999999999999\nfloor_um 0\nbody_forward_q30 0 0 1073741824\nend\n",
		"VRFBT-PROFILE 1\nname x\nalgorithm 1\navatar ranger_verified_rerelease\nhmd_serial -\nhmd_height_um 0\nfloor_um 0\nbody_forward_q30 0 0 0\nend\n",
		"VRFBT-PROFILE 1\nname x\nalgorithm 1\navatar ranger_verified_rerelease\nhmd_serial -\nhmd_height_um 0\nfloor_um 0\nbody_forward_q30 0 0 1073741824\nwat nope\nend\n",
		"VRFBT-PROFILE 1\nname x\nalgorithm 1\navatar ranger_verified_rerelease\nhmd_serial -\nhmd_height_um 0\nfloor_um 0\nbody_forward_q30 0 0 1073741824\n"
	};
	unsigned int i;
	assert(VR_FBT_ProfileSerialize(&profile, text, sizeof(text), &length, &error));
	assert(VR_FBT_ProfileParse(text, length, &parsed, &error));
	assert(VR_FBT_ProfileSerialize(&parsed, second, sizeof(second), &second_length, &error));
	assert(length == second_length && !memcmp(text, second, length));
	unchanged = Profile();
	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
		vr_fbt_profile_t before = unchanged;
		assert(!VR_FBT_ProfileParse(bad[i], strlen(bad[i]), &unchanged, &error));
		assert(!memcmp(&before, &unchanged, sizeof(before)));
	}
	assert(!VR_FBT_ProfileParse(text, length - 2, &unchanged, &error));
	{
		char short_text[8] = "keep";
		char before_text[sizeof(short_text)];
		size_t unchanged_length = 77;
		vr_fbt_profile_t before = profile;
		memcpy(before_text, short_text, sizeof(short_text));
		assert(!VR_FBT_ProfileSerialize(&profile, short_text, sizeof(short_text),
			&unchanged_length, &error));
		assert(!memcmp(before_text, short_text, sizeof(short_text)));
		assert(unchanged_length == 77 && !memcmp(&before, &profile, sizeof(profile)));
	}
}

static void TestMaximumTextAndDuplicates(void)
{
	vr_fbt_profile_t profile = Profile(), parsed;
	vr_fbt_profile_error_t error;
	char text[VR_FBT_PROFILE_TEXT_MAX + 1], crlf[VR_FBT_PROFILE_TEXT_MAX + 1];
	char serials[VR_FBT_ROLE_COUNT][VR_FBT_SERIAL_MAX];
	const char *expected[VR_FBT_ROLE_COUNT] = { serials[0], serials[1], serials[2] };
	size_t length, i, output = 0, crlf_length = 0;
	vr_fbt_profile_capture_t capture;
	for (i = 0; i < VR_FBT_PROFILE_NAME_MAX - 1; ++i) profile.name[i] = 'n';
	profile.name[VR_FBT_PROFILE_NAME_MAX - 1] = 0;
	for (i = 0; i < VR_FBT_ROLE_COUNT; ++i) {
		size_t j;
		for (j = 0; j < VR_FBT_SERIAL_MAX - 1; ++j) serials[i][j] = (char)('a' + i);
		serials[i][VR_FBT_SERIAL_MAX - 1] = 0;
		profile.roles[i].present = 1;
		strcpy(profile.roles[i].serial, serials[i]);
		profile.roles[i].device_to_anatomical = Identity();
	}
	assert(VR_FBT_ProfileSerialize(&profile, text, sizeof(text), &length, &error));
	for (i = 0; i < length; ++i) {
		if (text[i] == '\n') crlf[crlf_length++] = '\r';
		crlf[crlf_length++] = text[i];
	}
	assert(VR_FBT_ProfileParse(crlf, crlf_length, &parsed, &error));
	assert(VR_FBT_ProfileSerialize(&parsed, crlf, sizeof(crlf), &output, &error));
	assert(output == length && !memcmp(text, crlf, length));
	/* Direct profiles and text cannot bind one serial to two roles. */
	strcpy(profile.roles[VR_FBT_ROLE_LEFT_FOOT].serial, profile.roles[VR_FBT_ROLE_HIP].serial);
	assert(!VR_FBT_ProfileSerialize(&profile, crlf, sizeof(crlf), &output, &error));
	expected[VR_FBT_ROLE_LEFT_FOOT] = expected[VR_FBT_ROLE_HIP];
	assert(!VR_FBT_ProfileCaptureBegin(&capture,
		VR_FBT_PROFILE_ROLE_BIT(VR_FBT_ROLE_HIP) | VR_FBT_PROFILE_ROLE_BIT(VR_FBT_ROLE_LEFT_FOOT), expected));
	{
		vr_fbt_profile_t plain = Profile();
		const char duplicate[] = "role left_foot hip.1 0 0 0 1073741824 0 0 0\nend\n";
		assert(VR_FBT_ProfileSerialize(&plain, text, sizeof(text), &length, &error));
		assert(length >= 4);
		length -= 4; /* Replace canonical end with a duplicate role and end. */
		memcpy(text + length, duplicate, sizeof(duplicate) - 1);
		assert(!VR_FBT_ProfileParse(text, length + sizeof(duplicate) - 1, &parsed, &error));
	}
}

static void MakeMetadata(vr_fbt_profile_capture_metadata_t *metadata)
{
	memset(metadata, 0, sizeof(*metadata));
	strcpy(metadata->name, "captured");
	strcpy(metadata->hmd_serial, "hmd.1");
	metadata->hmd_height_metres = 1.7;
	metadata->body_forward[2] = 1.0;
}

static void MakeSamples(vr_fbt_profile_capture_sample_t samples[VR_FBT_ROLE_COUNT],
	unsigned int mask, const char *serials,
	unsigned int index)
{
	unsigned int role;
	memset(samples, 0, sizeof(vr_fbt_profile_capture_sample_t) * VR_FBT_ROLE_COUNT);
	for (role = 0; role < VR_FBT_ROLE_COUNT; ++role) if (mask & VR_FBT_PROFILE_ROLE_BIT(role)) {
		vr_fbt_profile_capture_sample_t *sample = &samples[role];
		sample->present = sample->connected = sample->pose_valid = 1;
		strcpy(sample->serial, serials + role * VR_FBT_SERIAL_MAX);
		sample->raw_tracker_transform = Identity();
		sample->reference_target_transform = Identity();
		sample->reference_target_transform.position[0] = 0.1;
		if (index == 34) {
			sample->reference_target_transform.position[0] = 1.5;
			sample->reference_target_transform.orientation[0] = 0.0;
			sample->reference_target_transform.orientation[1] = 1.0;
		}
	}
}

static void TestCaptureMask(unsigned int mask)
{
	char serials[VR_FBT_ROLE_COUNT][VR_FBT_SERIAL_MAX] = { "hip.1", "left.1", "right.1" };
	const char *expected[VR_FBT_ROLE_COUNT] = { serials[0], serials[1], serials[2] };
	vr_fbt_profile_capture_t capture;
	vr_fbt_profile_capture_metadata_t metadata;
	vr_fbt_profile_capture_sample_t samples[VR_FBT_ROLE_COUNT];
	vr_fbt_profile_t result;
	vr_fbt_profile_error_t error;
	unsigned int i, role;
	assert(VR_FBT_ProfileCaptureBegin(&capture, mask, expected));
	for (i = 0; i < 35; ++i) {
		MakeSamples(samples, mask, &serials[0][0], i);
		assert(VR_FBT_ProfileCaptureAddSnapshot(&capture, i + 1, (double)i / 34.0, samples));
	}
	MakeMetadata(&metadata);
	assert(VR_FBT_ProfileCaptureFinalize(&capture, &metadata, &result, &error));
	for (role = 0; role < VR_FBT_ROLE_COUNT; ++role) {
		assert(result.roles[role].present == !!(mask & VR_FBT_PROFILE_ROLE_BIT(role)));
		if (result.roles[role].present) {
			assert(Close(result.roles[role].device_to_anatomical.position[0], 0.1));
			assert(Close(result.roles[role].device_to_anatomical.orientation[0], 1.0));
		}
	}
}

static void TestCaptureFailures(void)
{
	char serials[VR_FBT_ROLE_COUNT][VR_FBT_SERIAL_MAX] = { "hip.1", "left.1", "right.1" };
	const char *expected[VR_FBT_ROLE_COUNT] = { serials[0], serials[1], serials[2] };
	vr_fbt_profile_capture_t capture;
	vr_fbt_profile_capture_metadata_t metadata;
	vr_fbt_profile_capture_sample_t samples[VR_FBT_ROLE_COUNT];
	vr_fbt_profile_t destination = Profile(), before;
	vr_fbt_profile_error_t error;
	unsigned int i;
	assert(VR_FBT_ProfileCaptureBegin(&capture, VR_FBT_PROFILE_ROLE_BIT(VR_FBT_ROLE_LEFT_FOOT), expected));
	MakeSamples(samples, VR_FBT_PROFILE_ROLE_BIT(VR_FBT_ROLE_LEFT_FOOT), &serials[0][0], 0);
	assert(VR_FBT_ProfileCaptureAddSnapshot(&capture, 10, 1.0, samples));
	assert(!VR_FBT_ProfileCaptureAddSnapshot(&capture, 10, 1.01, samples));
	assert(!VR_FBT_ProfileCaptureAddSnapshot(&capture, 9, 1.02, samples));
	assert(!VR_FBT_ProfileCaptureAddSnapshot(&capture, 11, 0.99, samples));
	assert(capture.snapshot_rejected == 3);
	for (i = 0; i < 29; ++i) {
		MakeSamples(samples, VR_FBT_PROFILE_ROLE_BIT(VR_FBT_ROLE_LEFT_FOOT), &serials[0][0], i);
		strcpy(samples[VR_FBT_ROLE_LEFT_FOOT].serial, "wrong.1");
		assert(VR_FBT_ProfileCaptureAddSnapshot(&capture, 12 + i, 1.03 + i * .01, samples));
	}
	MakeMetadata(&metadata);
	before = destination;
	assert(!VR_FBT_ProfileCaptureFinalize(&capture, &metadata, &destination, &error));
	assert(error == VR_FBT_PROFILE_ERR_INSUFFICIENT_SAMPLES);
	assert(!memcmp(&before, &destination, sizeof(before)));
}

static void TestCaptureBoundaries(void)
{
	char serials[VR_FBT_ROLE_COUNT][VR_FBT_SERIAL_MAX] = { "hip.1", "left.1", "right.1" };
	const char *expected[VR_FBT_ROLE_COUNT] = { serials[0], serials[1], serials[2] };
	vr_fbt_profile_capture_t capture;
	vr_fbt_profile_capture_metadata_t metadata;
	vr_fbt_profile_capture_sample_t samples[VR_FBT_ROLE_COUNT];
	vr_fbt_profile_t result;
	vr_fbt_profile_error_t error;
	unsigned int i, mask = VR_FBT_PROFILE_ROLE_BIT(VR_FBT_ROLE_LEFT_FOOT);
	assert(VR_FBT_ProfileCaptureBegin(&capture, mask, expected));
	for (i = 0; i < 240; ++i) {
		MakeSamples(samples, mask, &serials[0][0], i);
		samples[VR_FBT_ROLE_LEFT_FOOT].linear_velocity_metres_per_second[0] = 5.0;
		samples[VR_FBT_ROLE_LEFT_FOOT].angular_velocity_radians_per_second[2] = 20.0;
		assert(VR_FBT_ProfileCaptureAddSnapshot(&capture, i + 1, (double)i / 239.0, samples));
	}
	assert(capture.accepted[VR_FBT_ROLE_LEFT_FOOT] == 240);
	MakeSamples(samples, mask, &serials[0][0], 0);
	assert(VR_FBT_ProfileCaptureAddSnapshot(&capture, 241, 1.01, samples));
	assert(capture.accepted[VR_FBT_ROLE_LEFT_FOOT] == 240);
	MakeMetadata(&metadata);
	assert(VR_FBT_ProfileCaptureFinalize(&capture, &metadata, &result, &error));
	assert(VR_FBT_ProfileCaptureBegin(&capture, mask, expected));
	MakeSamples(samples, mask, &serials[0][0], 0);
	assert(VR_FBT_ProfileCaptureAddSnapshot(&capture, UINT64_MAX - 1u, 0.0, samples));
	assert(VR_FBT_ProfileCaptureAddSnapshot(&capture, UINT64_MAX, 0.1, samples));
	assert(VR_FBT_ProfileCaptureAddSnapshot(&capture, 0, 0.2, samples));
	assert(!VR_FBT_ProfileCaptureAddSnapshot(&capture, UINT64_C(1) << 63, 0.3, samples));
	assert(VR_FBT_ProfileCaptureBegin(&capture, mask, expected));
	MakeSamples(samples, mask, &serials[0][0], 0);
	samples[VR_FBT_ROLE_LEFT_FOOT].linear_velocity_metres_per_second[0] = NAN;
	assert(VR_FBT_ProfileCaptureAddSnapshot(&capture, 1, 0.0, samples));
	assert(capture.accepted[VR_FBT_ROLE_LEFT_FOOT] == 0 && capture.rejected[VR_FBT_ROLE_LEFT_FOOT] == 1);
}

static void TestCollectionWindow(void)
{
	char serials[VR_FBT_ROLE_COUNT][VR_FBT_SERIAL_MAX] = { "hip.1", "left.1", "right.1" };
	const char *expected[VR_FBT_ROLE_COUNT] = { serials[0], serials[1], serials[2] };
	vr_fbt_profile_capture_t capture;
	vr_fbt_profile_capture_metadata_t metadata;
	vr_fbt_profile_capture_sample_t samples[VR_FBT_ROLE_COUNT];
	vr_fbt_profile_t result, before = Profile();
	vr_fbt_profile_error_t error;
	unsigned int i, mask = VR_FBT_PROFILE_ROLE_BIT(VR_FBT_ROLE_LEFT_FOOT);
	assert(VR_FBT_ProfileCaptureBegin(&capture, mask, expected));
	for (i = 0; i < 30; ++i) {
		MakeSamples(samples, mask, &serials[0][0], i);
		assert(VR_FBT_ProfileCaptureAddSnapshot(&capture, i + 1, .99 * (double)i / 29.0, samples));
	}
	MakeMetadata(&metadata);
	result = before;
	assert(!VR_FBT_ProfileCaptureFinalize(&capture, &metadata, &result, &error));
	assert(error == VR_FBT_PROFILE_ERR_INSUFFICIENT_SAMPLES);
	assert(!memcmp(&before, &result, sizeof(before)));
	memset(samples, 0, sizeof(samples));
	assert(VR_FBT_ProfileCaptureAddSnapshot(&capture, 31, 1.0, samples));
	assert(VR_FBT_ProfileCaptureFinalize(&capture, &metadata, &result, &error));
	MakeSamples(samples, mask, &serials[0][0], 0);
	assert(VR_FBT_ProfileCaptureAddSnapshot(&capture, 32, 1.01, samples));
	assert(capture.accepted[VR_FBT_ROLE_LEFT_FOOT] == 30);
}

static void TestAbsolutePoseAndMajority(void)
{
	char serials[VR_FBT_ROLE_COUNT][VR_FBT_SERIAL_MAX] = { "hip.1", "left.1", "right.1" };
	const char *expected[VR_FBT_ROLE_COUNT] = { serials[0], serials[1], serials[2] };
	vr_fbt_profile_capture_t capture;
	vr_fbt_profile_capture_metadata_t metadata;
	vr_fbt_profile_capture_sample_t samples[VR_FBT_ROLE_COUNT];
	vr_fbt_profile_t result, unchanged = Profile();
	vr_fbt_profile_error_t error;
	unsigned int i, mask = VR_FBT_PROFILE_ROLE_BIT(VR_FBT_ROLE_LEFT_FOOT);
	assert(VR_FBT_ProfileCaptureBegin(&capture, mask, expected));
	for (i = 0; i < 30; ++i) {
		MakeSamples(samples, mask, &serials[0][0], i);
		samples[VR_FBT_ROLE_LEFT_FOOT].raw_tracker_transform.position[0] = 100.0;
		samples[VR_FBT_ROLE_LEFT_FOOT].reference_target_transform.position[0] = 100.1;
		assert(VR_FBT_ProfileCaptureAddSnapshot(&capture, i + 1, (double)i / 29.0, samples));
	}
	MakeMetadata(&metadata);
	assert(VR_FBT_ProfileCaptureFinalize(&capture, &metadata, &result, &error));
	assert(Close(result.roles[VR_FBT_ROLE_LEFT_FOOT].device_to_anatomical.position[0], 0.1));
	assert(VR_FBT_ProfileCaptureBegin(&capture, mask, expected));
	for (i = 0; i < 30; ++i) {
		MakeSamples(samples, mask, &serials[0][0], i);
		if (i >= 16) {
			samples[VR_FBT_ROLE_LEFT_FOOT].reference_target_transform.position[0] = 1.5;
			samples[VR_FBT_ROLE_LEFT_FOOT].reference_target_transform.orientation[0] = 0.0;
			samples[VR_FBT_ROLE_LEFT_FOOT].reference_target_transform.orientation[1] = 1.0;
		}
		assert(VR_FBT_ProfileCaptureAddSnapshot(&capture, i + 1, (double)i / 29.0, samples));
	}
	assert(VR_FBT_ProfileCaptureFinalize(&capture, &metadata, &result, &error));
	assert(Close(result.roles[VR_FBT_ROLE_LEFT_FOOT].device_to_anatomical.position[0], 0.1));
	assert(VR_FBT_ProfileCaptureBegin(&capture, mask, expected));
	for (i = 0; i < 30; ++i) {
		MakeSamples(samples, mask, &serials[0][0], i);
		if (i >= 15) {
			samples[VR_FBT_ROLE_LEFT_FOOT].reference_target_transform.position[0] = 1.5;
			samples[VR_FBT_ROLE_LEFT_FOOT].reference_target_transform.orientation[0] = 0.0;
			samples[VR_FBT_ROLE_LEFT_FOOT].reference_target_transform.orientation[1] = 1.0;
		}
		assert(VR_FBT_ProfileCaptureAddSnapshot(&capture, i + 1, (double)i / 29.0, samples));
	}
	assert(!VR_FBT_ProfileCaptureFinalize(&capture, &metadata, &unchanged, &error));
	assert(error == VR_FBT_PROFILE_ERR_INSUFFICIENT_SAMPLES);
}

static void TestMath(void)
{
	vr_fbt_profile_transform_t raw = Identity(), reference = Identity(), inverse, correction, applied;
	const double root_half = 0.7071067811865475244;
	reference.orientation[0] = root_half;
	reference.orientation[2] = root_half;
	assert(VR_FBT_ProfileTransformInverse(&raw, &inverse));
	assert(VR_FBT_ProfileTransformCompose(&inverse, &reference, &correction));
	assert(VR_FBT_ProfileApplyCorrection(&raw, &correction, &applied));
	assert(Close(applied.orientation[0], root_half) && Close(applied.orientation[2], root_half));
	/* Composition is intentionally independent of global yaw or world scale. */
	raw = reference;
	correction = reference;
	assert(VR_FBT_ProfileApplyCorrection(&raw, &correction, &applied));
	assert(Close(applied.orientation[0], 0.0) && Close(applied.orientation[2], 1.0));
	/* q and -q represent one rotation, and no world-scale/yaw inputs exist here. */
	reference.orientation[0] = -root_half;
	reference.orientation[2] = -root_half;
	assert(VR_FBT_ProfileTransformCompose(&inverse, &reference, &correction));
	assert(Close(fabs(correction.orientation[0]), root_half));
}

int main(void)
{
	TestStrictText();
	TestMaximumTextAndDuplicates();
	TestCaptureMask(VR_FBT_PROFILE_ROLE_BIT(VR_FBT_ROLE_LEFT_FOOT));
	TestCaptureMask(VR_FBT_PROFILE_ROLE_BIT(VR_FBT_ROLE_HIP));
	TestCaptureMask(VR_FBT_PROFILE_ROLE_BIT(VR_FBT_ROLE_HIP) |
		VR_FBT_PROFILE_ROLE_BIT(VR_FBT_ROLE_LEFT_FOOT) |
		VR_FBT_PROFILE_ROLE_BIT(VR_FBT_ROLE_RIGHT_FOOT));
	TestCaptureFailures();
	TestCaptureBoundaries();
	TestCollectionWindow();
	TestAbsolutePoseAndMajority();
	TestMath();
	puts("vr_fbt_profile_fixture: ok");
	return 0;
}

