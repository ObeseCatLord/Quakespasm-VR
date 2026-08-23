/* Exhaustive, standalone fixture for Quake/vrik_codec.c.  Wire sizes here,
 * like the production API, exclude any client/server opcode framing. */
#include "../Quake/vrik_codec.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void expect(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "vrik_codec_fixture: %s\n", message);
		++failures;
	}
}

static void expect_status(vrik_codec_status_t actual, vrik_codec_status_t expected,
	const char *message)
{
	if (actual != expected) {
		fprintf(stderr, "vrik_codec_fixture: %s (got %d, expected %d)\n",
			message, (int)actual, (int)expected);
		++failures;
	}
}

static void put_u16le(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static unsigned int bit_count(uint8_t value)
{
	unsigned int count = 0;
	while (value != 0) {
		count += (unsigned int)(value & 1u);
		value = (uint8_t)(value >> 1);
	}
	return count;
}

static vrik_codec_pose_t make_v3_pose(uint8_t present_mask)
{
	vrik_codec_pose_t pose;
	int target;
	int axis;

	memset(&pose, 0, sizeof(pose));
	pose.sequence = 0xbeefU;
	if (present_mask & VRIK_TARGET_BIT(VRIK_TARGET_HEAD)) {
		pose.flags = VRIK_V3_FLAG_ACTIVE | VRIK_V3_FLAG_DOMINANT_LEFT;
		pose.present_mask = present_mask;
		pose.tracked_mask = present_mask;
		pose.body_yaw = -180.0f;
		pose.aim_orientation[0] = -180.0f;
		pose.aim_orientation[1] = 90.0f;
		pose.aim_orientation[2] = -90.0f;
		for (target = 0; target < VRIK_TARGET_COUNT; ++target)
			if (present_mask & VRIK_TARGET_BIT(target))
				for (axis = 0; axis < 3; ++axis) {
					pose.targets[target].position[axis] = (float)(target * 32 + axis + 1);
					pose.targets[target].orientation[axis] = (float)(target * 30 + axis * 5);
				}
	} else if (present_mask != 0) {
		/* Deliberately malformed active pose used by the full mask matrix. */
		pose.flags = VRIK_V3_FLAG_ACTIVE;
		pose.present_mask = present_mask;
		pose.tracked_mask = present_mask;
	}
	return pose;
}

static void test_v2_golden_and_roundtrip(void)
{
	static const uint8_t golden[VRIK_V2_BODY_BYTES] = {
		0xef, 0xbe, 0x1f, 0x00, 0x80,
		0x00, 0x80, 0xff, 0x7f, 0x01, 0x00,
		0xff, 0xff, 0x0a, 0x00, 0xf6, 0xff,
		0x62, 0x00, 0x9e, 0xff, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x40, 0x00, 0x80,
		0x00, 0xc0, 0xff, 0xff, 0x00, 0xc0,
		0x00, 0x40, 0x00, 0x00, 0x00, 0xc0,
		0x00, 0x80, 0xff, 0x7f, 0x00, 0x00
	};
	vrik_v2_pose_t v2;
	vrik_v2_pose_t back;
	vrik_codec_pose_t normalized;
	uint8_t encoded[VRIK_V2_BODY_BYTES];
	size_t used;

	expect_status(vrik_v2_decode(golden, sizeof(golden), &v2, &used),
		VRIK_CODEC_OK, "v2 golden must decode");
	expect(used == VRIK_V2_BODY_BYTES, "v2 golden consumed size");
	expect_status(vrik_v2_encode(&v2, encoded, sizeof(encoded), &used),
		VRIK_CODEC_OK, "v2 golden must encode");
	expect(used == sizeof(encoded) && memcmp(encoded, golden, sizeof(golden)) == 0,
		"v2 golden bytes changed");
	expect_status(vrik_v2_to_normalized(&v2, &normalized), VRIK_CODEC_OK,
		"v2 to normalized");
	expect_status(vrik_normalized_to_v2(&normalized, &back), VRIK_CODEC_OK,
		"normalized to v2");
	expect_status(vrik_v2_encode(&back, encoded, sizeof(encoded), &used),
		VRIK_CODEC_OK, "v2 roundtrip encode");
	expect(memcmp(encoded, golden, sizeof(golden)) == 0,
		"v2-normalized-v2 must retain all 47 bytes");
}

static void test_v3_masks_order_and_lengths(void)
{
	static const uint8_t all_target_golden[VRIK_TARGET_COUNT][VRIK_V3_TARGET_BYTES] = {
		{0x08, 0x00, 0x10, 0x00, 0x18, 0x00, 0x00, 0x00, 0x8e, 0x03, 0x1c, 0x07},
		{0x08, 0x01, 0x10, 0x01, 0x18, 0x01, 0x55, 0x15, 0xe4, 0x18, 0x72, 0x1c},
		{0x08, 0x02, 0x10, 0x02, 0x18, 0x02, 0xab, 0x2a, 0x39, 0x2e, 0xc7, 0x31},
		{0x08, 0x03, 0x10, 0x03, 0x18, 0x03, 0x00, 0x40, 0x8e, 0x43, 0x1c, 0x47},
		{0x08, 0x04, 0x10, 0x04, 0x18, 0x04, 0x55, 0x55, 0xe4, 0x58, 0x72, 0x5c},
		{0x08, 0x05, 0x10, 0x05, 0x18, 0x05, 0xab, 0x6a, 0x39, 0x6e, 0xc7, 0x71}
	};
	uint8_t wire[85];
	vrik_codec_pose_t pose;
	vrik_codec_pose_t decoded;
	size_t used;
	size_t expected;
	unsigned int mask;
	int target;

	for (mask = 0; mask <= VRIK_TARGET_MASK_ALL; ++mask) {
		expect_status(vrik_v3_body_size((uint8_t)mask, &expected), VRIK_CODEC_OK,
			"every v3 mask has a size");
		expect(expected == VRIK_V3_HEADER_BYTES + VRIK_V3_AIM_BYTES +
			(size_t)bit_count((uint8_t)mask) * VRIK_V3_TARGET_BYTES,
			"v3 size follows mask population");
		pose = make_v3_pose((uint8_t)mask);
		if (mask & VRIK_TARGET_BIT(VRIK_TARGET_HEAD)) {
			expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), &used),
				VRIK_CODEC_OK, "head-present v3 combination encodes");
			expect(used == expected, "v3 encoded length matches exact mask size");
			expect_status(vrik_v3_decode(wire, used, &decoded, &expected),
				VRIK_CODEC_OK, "head-present v3 combination decodes");
			expect(expected == used && decoded.present_mask == mask &&
			decoded.tracked_mask == mask, "v3 mask roundtrip");
		} else if (mask == 0) {
			expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), &used),
				VRIK_CODEC_OK, "canonical inactive v3 encodes");
			expect(used == 13u, "inactive v3 is 13 bytes");
		} else {
			expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), &used),
				VRIK_CODEC_MALFORMED, "active v3 requires head");
		}
	}
	pose = make_v3_pose(VRIK_TARGET_MASK_ALL);
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), &used), VRIK_CODEC_OK,
		"full v3 encodes");
	expect(used == 85u, "six-target v3 body is exactly 85 bytes");
	for (target = 0; target < VRIK_TARGET_COUNT; ++target)
		expect(memcmp(wire + VRIK_V3_HEADER_BYTES +
			(size_t)target * VRIK_V3_TARGET_BYTES, all_target_golden[target],
			VRIK_V3_TARGET_BYTES) == 0,
			"v3 all-target payload has canonical golden positions");
	expect(wire[79] == 0x00u && wire[80] == 0x80u &&
		wire[81] == 0x00u && wire[82] == 0x40u &&
		wire[83] == 0x00u && wire[84] == 0xc0u,
		"v3 all-target golden aim starts after exactly six target payloads");
}

static void test_failures_and_bounds(void)
{
	uint8_t wire[86];
	uint8_t guarded[100];
	uint8_t v2guarded[VRIK_V2_BODY_BYTES + 1u];
	uint8_t v2zero[VRIK_V2_BODY_BYTES];
	vrik_codec_pose_t pose = make_v3_pose(VRIK_TARGET_MASK_ALL);
	vrik_codec_pose_t decoded;
	vrik_v2_pose_t v2decoded;
	size_t used;
	size_t index;

	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), &used), VRIK_CODEC_OK,
		"setup full v3 wire");
	for (index = 0; index < used; ++index)
		expect_status(vrik_v3_decode(wire, index, &decoded, NULL), VRIK_CODEC_TRUNCATED,
			"every v3 prefix is truncated");
	memset(v2zero, 0, sizeof(v2zero));
	for (index = 0; index < VRIK_V2_BODY_BYTES; ++index)
		expect_status(vrik_v2_decode(v2zero, index, &v2decoded, NULL), VRIK_CODEC_TRUNCATED,
			"every v2 prefix is truncated");
	expect_status(vrik_v2_decode(v2zero, sizeof(v2zero), &v2decoded, NULL), VRIK_CODEC_OK,
		"setup canonical v2 body for short-output tests");
	for (index = 0; index < used; ++index) {
		memset(guarded, 0xa5, sizeof(guarded));
		expect_status(vrik_v3_encode(&pose, guarded, index, NULL), VRIK_CODEC_TRUNCATED,
			"every short v3 output is rejected");
		for (size_t i = 0; i < sizeof(guarded); ++i)
			expect(guarded[i] == 0xa5, "short v3 encode must not overwrite sentinels");
	}
	for (index = 0; index < VRIK_V2_BODY_BYTES; ++index) {
		memset(v2guarded, 0xa5, sizeof(v2guarded));
		expect_status(vrik_v2_encode(&v2decoded, v2guarded, index, NULL), VRIK_CODEC_TRUNCATED,
			"every short v2 output is rejected");
		for (size_t i = 0; i < sizeof(v2guarded); ++i)
			expect(v2guarded[i] == 0xa5, "short v2 encode must not overwrite sentinels");
	}
	wire[used] = 0xee;
	expect_status(vrik_v3_decode(wire, used + 1, &decoded, &index), VRIK_CODEC_OK,
		"v3 decode accepts containing buffer");
	expect(index == used, "v3 decode reports exact consumed body, not trailing byte");
	v2zero[VRIK_V2_BODY_BYTES - 1u] = 0xee;
	expect_status(vrik_v2_decode(v2zero, sizeof(v2zero), &v2decoded, &index),
		VRIK_CODEC_OK, "v2 decode accepts one exact body after setup mutation");
	v2guarded[VRIK_V2_BODY_BYTES] = 0xee;
	memcpy(v2guarded, v2zero, sizeof(v2zero));
	expect_status(vrik_v2_decode(v2guarded, sizeof(v2guarded), &v2decoded, &index),
		VRIK_CODEC_OK, "v2 decode accepts containing buffer");
	expect(index == VRIK_V2_BODY_BYTES,
		"v2 decode reports exact consumed body, not trailing byte");

	pose.flags = 0x80u;
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), NULL), VRIK_CODEC_MALFORMED,
		"v3 reserved flags rejected");
	pose = make_v3_pose(VRIK_TARGET_BIT(VRIK_TARGET_HEAD));
	pose.present_mask = 0x80u;
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), NULL), VRIK_CODEC_MALFORMED,
		"v3 high present bit rejected");
	pose = make_v3_pose(VRIK_TARGET_BIT(VRIK_TARGET_HEAD));
	pose.tracked_mask = VRIK_TARGET_BIT(VRIK_TARGET_HIP);
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), NULL), VRIK_CODEC_MALFORMED,
		"tracked-not-present rejected");
	pose = make_v3_pose(VRIK_TARGET_BIT(VRIK_TARGET_LEFT_HAND));
	pose.flags = VRIK_V3_FLAG_ACTIVE;
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), NULL), VRIK_CODEC_MALFORMED,
		"active-without-head rejected");
	pose = make_v3_pose(0);
	pose.body_yaw = 1.0f;
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), NULL), VRIK_CODEC_MALFORMED,
		"inactive nonzero payload rejected");
	pose = make_v3_pose(VRIK_TARGET_BIT(VRIK_TARGET_HEAD));
	pose.targets[0].position[0] = 4096.0f;
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), NULL), VRIK_CODEC_RANGE,
		"out-of-int16 position rejected rather than wrapped");
	pose.targets[0].position[0] = -4096.0f;
	pose.targets[0].position[1] = 4095.875f;
	pose.targets[0].position[2] = 0.0625f;
	pose.targets[0].orientation[0] = -180.0f;
	pose.targets[0].orientation[1] = 180.0f;
	pose.targets[0].orientation[2] = 360.0f;
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), NULL), VRIK_CODEC_OK,
		"position and angle boundary values encode");
	pose.targets[0].position[0] = NAN;
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), NULL), VRIK_CODEC_RANGE,
		"NaN rejected");
	pose.targets[0].position[0] = INFINITY;
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), NULL), VRIK_CODEC_RANGE,
		"infinite position rejected");
	pose.targets[0].position[0] = 0.0f;
	pose.body_yaw = INFINITY;
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), NULL), VRIK_CODEC_RANGE,
		"infinite angle rejected");
	pose.body_yaw = 0.0f;
	pose.targets[0].orientation[0] = NAN;
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), NULL), VRIK_CODEC_RANGE,
		"nonfinite orientation rejected");

	pose = make_v3_pose(VRIK_TARGET_BIT(VRIK_TARGET_HEAD));
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), &used), VRIK_CODEC_OK,
		"setup malformed decode body");
	wire[2] = 0x80u;
	expect_status(vrik_v3_decode(wire, used, &decoded, NULL), VRIK_CODEC_MALFORMED,
		"decoder rejects unknown v3 flag");
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), &used), VRIK_CODEC_OK,
		"reset malformed decode body");
	wire[3] = 0x80u;
	expect_status(vrik_v3_decode(wire, used, &decoded, NULL), VRIK_CODEC_MALFORMED,
		"decoder rejects high present mask bit");
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), &used), VRIK_CODEC_OK,
		"reset tracked mask decode body");
	wire[4] = 0x80u;
	expect_status(vrik_v3_decode(wire, used, &decoded, NULL), VRIK_CODEC_MALFORMED,
		"decoder rejects high tracked mask bit");
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), &used), VRIK_CODEC_OK,
		"reset subset decode body");
	wire[4] = VRIK_TARGET_BIT(VRIK_TARGET_HIP);
	expect_status(vrik_v3_decode(wire, used, &decoded, NULL), VRIK_CODEC_MALFORMED,
		"decoder rejects tracked-not-present");
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), &used), VRIK_CODEC_OK,
		"reset head requirement decode body");
	wire[3] = VRIK_TARGET_BIT(VRIK_TARGET_LEFT_HAND);
	wire[4] = VRIK_TARGET_BIT(VRIK_TARGET_LEFT_HAND);
	expect_status(vrik_v3_decode(wire, used, &decoded, NULL), VRIK_CODEC_MALFORMED,
		"decoder rejects active-without-head");
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), &used), VRIK_CODEC_OK,
		"reset inactive decode body");
	wire[2] = 0;
	expect_status(vrik_v3_decode(wire, used, &decoded, NULL), VRIK_CODEC_MALFORMED,
		"decoder rejects inactive nonzero masks");
	pose = make_v3_pose(0);
	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), &used), VRIK_CODEC_OK,
		"setup canonical inactive decode body");
	wire[5] = 1u;
	expect_status(vrik_v3_decode(wire, used, &decoded, NULL), VRIK_CODEC_MALFORMED,
		"decoder rejects inactive nonzero payload");
}

static void test_v2_legacy_semantic_policy(void)
{
	vrik_v2_pose_t pose;
	vrik_v2_pose_t decoded;
	uint8_t wire[VRIK_V2_BODY_BYTES];
	size_t used;

	memset(&pose, 0, sizeof(pose));
	pose.flags = VRIK_V2_FLAG_ACTIVE | VRIK_V2_FLAG_HEAD_TRACKED;
	pose.targets[VRIK_TARGET_HEAD].position[0] =
		VRIK_V2_LEGACY_MAX_ROOT_LOCAL_OFFSET;
	expect_status(vrik_v2_encode(&pose, wire, sizeof(wire), &used), VRIK_CODEC_OK,
		"v2 structural encoder accepts exact legacy root-local boundary");
	expect_status(vrik_v2_decode(wire, used, &decoded, NULL), VRIK_CODEC_OK,
		"v2 structural decoder accepts exact legacy root-local boundary");
	expect_status(vrik_v2_validate_legacy_pose(&decoded), VRIK_CODEC_OK,
		"v2 legacy policy accepts exactly 256 units");
	pose.targets[VRIK_TARGET_RIGHT_HAND].position[0] =
		VRIK_V2_LEGACY_MAX_ROOT_LOCAL_OFFSET + 0.125f;
	expect_status(vrik_v2_encode(&pose, wire, sizeof(wire), &used), VRIK_CODEC_OK,
		"v2 structural encoder retains wider signed-13.3 wire range");
	expect_status(vrik_v2_decode(wire, used, &decoded, NULL), VRIK_CODEC_OK,
		"v2 structural decoder does not apply server distance policy");
	expect_status(vrik_v2_validate_legacy_pose(&decoded), VRIK_CODEC_RANGE,
		"v2 legacy policy rejects an untracked target beyond 256 units");
}

static void test_destination_immutability(void)
{
	uint8_t wire[85];
	vrik_codec_pose_t pose = make_v3_pose(VRIK_TARGET_BIT(VRIK_TARGET_HEAD));
	vrik_codec_pose_t decoded;
	vrik_codec_pose_t decoded_before;
	vrik_v2_pose_t v2decoded;
	vrik_v2_pose_t v2decoded_before;
	vrik_v2_pose_t converted;
	vrik_v2_pose_t converted_before;
	size_t used;

	expect_status(vrik_v3_encode(&pose, wire, sizeof(wire), &used), VRIK_CODEC_OK,
		"setup v3 destination immutability input");
	memset(&decoded, 0xa5, sizeof(decoded));
	memcpy(&decoded_before, &decoded, sizeof(decoded));
	expect_status(vrik_v3_decode(wire, used - 1u, &decoded, NULL), VRIK_CODEC_TRUNCATED,
		"short v3 decode is rejected");
	expect(memcmp(&decoded, &decoded_before, sizeof(decoded)) == 0,
		"failed v3 decode leaves destination unchanged");
	wire[2] = 0x80u;
	expect_status(vrik_v3_decode(wire, used, &decoded, NULL), VRIK_CODEC_MALFORMED,
		"malformed v3 decode is rejected");
	expect(memcmp(&decoded, &decoded_before, sizeof(decoded)) == 0,
		"malformed v3 decode leaves destination unchanged");
	memset(&v2decoded, 0xa5, sizeof(v2decoded));
	memcpy(&v2decoded_before, &v2decoded, sizeof(v2decoded));
	expect_status(vrik_v2_decode(wire, VRIK_V2_BODY_BYTES - 1u, &v2decoded, NULL),
		VRIK_CODEC_TRUNCATED, "short v2 decode is rejected");
	expect(memcmp(&v2decoded, &v2decoded_before, sizeof(v2decoded)) == 0,
		"failed v2 decode leaves destination unchanged");
	expect_status(vrik_v2_decode(wire, VRIK_V2_BODY_BYTES, &v2decoded, NULL),
		VRIK_CODEC_MALFORMED, "malformed v2 decode is rejected");
	expect(memcmp(&v2decoded, &v2decoded_before, sizeof(v2decoded)) == 0,
		"malformed v2 decode leaves destination unchanged");
	pose.tracked_mask &= (uint8_t)~VRIK_TARGET_BIT(VRIK_TARGET_LEFT_HAND);
	pose.present_mask |= VRIK_TARGET_BIT(VRIK_TARGET_LEFT_HAND);
	memset(&converted, 0xa5, sizeof(converted));
	memcpy(&converted_before, &converted, sizeof(converted));
	expect_status(vrik_normalized_to_v2(&pose, &converted), VRIK_CODEC_MALFORMED,
		"v3-to-v2 rejects present untracked v2 role");
	expect(memcmp(&converted, &converted_before, sizeof(converted)) == 0,
		"failed v3-to-v2 conversion leaves destination unchanged");
}

static void test_conversion_and_v2_legacy_inactive(void)
{
	vrik_codec_pose_t pose = make_v3_pose(VRIK_TARGET_MASK_ALL);
	vrik_codec_pose_t normalized;
	vrik_v2_pose_t v2;
	vrik_v2_pose_t legacy;
	uint8_t v2wire[VRIK_V2_BODY_BYTES];
	uint8_t v3clear[VRIK_V3_HEADER_BYTES + VRIK_V3_AIM_BYTES];
	size_t used;
	int target;
	int axis;

	expect_status(vrik_normalized_to_v2(&pose, &v2), VRIK_CODEC_OK,
		"v3 normalized to v2 preserves representable tracked roles");
	expect((v2.flags & (VRIK_V2_FLAG_HEAD_TRACKED | VRIK_V2_FLAG_LEFT_HAND_TRACKED |
		VRIK_V2_FLAG_RIGHT_HAND_TRACKED)) == 0x0eu, "v3 hands map to v2 flags");
	expect_status(vrik_v2_to_normalized(&v2, &normalized), VRIK_CODEC_OK,
		"v2 to normalized conversion");
	expect(normalized.present_mask == 0x07u && normalized.tracked_mask == 0x07u,
		"v2 conversion drops hip and feet");
	for (target = VRIK_TARGET_HEAD; target <= VRIK_TARGET_RIGHT_HAND; ++target)
		for (axis = 0; axis < 3; ++axis)
			expect(normalized.targets[target].position[axis] == pose.targets[target].position[axis] &&
				normalized.targets[target].orientation[axis] == pose.targets[target].orientation[axis] &&
				normalized.aim_orientation[axis] == pose.aim_orientation[axis],
				"v2/v3 equivalent head-hand-aim values retained");

	memset(v2wire, 0, sizeof(v2wire));
	put_u16le(v2wire, 0x1234u);
	v2wire[2] = 0; /* v2 inactive is still the complete 47-byte body. */
	put_u16le(v2wire + 5, 0x0008u); /* nonzero legacy payload is legal v2 */
	expect_status(vrik_v2_decode(v2wire, sizeof(v2wire), &legacy, &used),
		VRIK_CODEC_OK, "v2 inactive nonzero fixed payload decodes");
	expect_status(vrik_v2_to_normalized(&legacy, &normalized), VRIK_CODEC_OK,
		"legacy inactive v2 normalizes to a canonical clear");
	expect(normalized.sequence == 0x1234u && normalized.flags == 0 &&
		normalized.present_mask == 0 && normalized.tracked_mask == 0 &&
		normalized.body_yaw == 0.0f && normalized.targets[0].position[0] == 0.0f,
		"inactive v2 ignored payload is absent from canonical v3 clear");
	expect_status(vrik_v3_encode(&normalized, v3clear, sizeof(v3clear), &used),
		VRIK_CODEC_OK, "canonical inactive v3 clear encodes");
	expect(used == sizeof(v3clear) && v3clear[0] == 0x34u &&
		v3clear[1] == 0x12u && memcmp(v3clear + 2, "\0\0\0\0\0\0\0\0\0\0\0", 11) == 0,
		"canonical inactive v3 clear has zero flags, masks, and payload");
}

static void test_sequence(void)
{
	expect(vrik_sequence_is_newer(0xffffu, 0xfffeu), "sequence ffff follows fffe");
	expect(vrik_sequence_is_newer(0u, 0xffffu), "sequence wraps to zero");
	expect(!vrik_sequence_is_newer(0xffffu, 0u), "old packet rejected after wrap");
	expect(!vrik_sequence_is_newer(0x8000u, 0u), "half range is not newer");
	expect(vrik_sequence_is_newer(0x7fffu, 0u), "largest positive delta is newer");
}

int main(void)
{
	test_v2_golden_and_roundtrip();
	test_v3_masks_order_and_lengths();
	test_failures_and_bounds();
	test_v2_legacy_semantic_policy();
	test_destination_immutability();
	test_conversion_and_v2_legacy_inactive();
	test_sequence();
	if (failures)
		return EXIT_FAILURE;
	puts("vrik_codec_fixture: PASS");
	return EXIT_SUCCESS;
}
