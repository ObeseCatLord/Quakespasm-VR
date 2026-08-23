/*
 * Standalone VRIK protocol v2 wire fixture.
 *
 * This intentionally mirrors the fixed codec used by the engine without
 * linking production sources: CL_WriteVRIKPose and the corresponding parser
 * are private static functions.  Keep this fixture byte-oriented so v3 work
 * has an explicit, executable compatibility baseline for v2.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VRIK_TRACKER_COUNT 3
#define VRIK_POSE_WIRE_BYTES 47

#define VRIK_FLAG_ACTIVE 0x01
#define VRIK_FLAG_HEAD_TRACKED 0x02
#define VRIK_FLAG_LEFT_HAND_TRACKED 0x04
#define VRIK_FLAG_RIGHT_HAND_TRACKED 0x08
#define VRIK_FLAG_DOMINANT_LEFT 0x10

typedef struct {
	uint16_t sequence;
	uint8_t flags;
	float body_yaw;
	float position[VRIK_TRACKER_COUNT][3];
	float orientation[VRIK_TRACKER_COUNT][3];
	float aim_orientation[3];
} vrik_pose_v2_t;

static int failures;

static void fail(const char *message)
{
	fprintf(stderr, "vrik_v2_fixture: %s\n", message);
	failures++;
}

static void expect(int condition, const char *message)
{
	if (!condition)
		fail(message);
}

/* Matches Quake's Q_rint macro: ties are rounded away from zero. */
static int q_rint(double value)
{
	return value > 0.0 ? (int)(value + 0.5) : (int)(value - 0.5);
}

static int16_t sign_extend_u16(uint16_t value)
{
	return value < 0x8000U ? (int16_t)value :
		(int16_t)((int)value - 0x10000);
}

static uint16_t quantize_angle(float angle)
{
	return (uint16_t)(q_rint((double)angle * 65536.0 / 360.0) & 0xffff);
}

static uint16_t quantize_position(float position)
{
	return (uint16_t)(int16_t)q_rint((double)position * 8.0);
}

static void write_u16le(uint8_t **cursor, uint16_t value)
{
	(*cursor)[0] = (uint8_t)(value & 0xffU);
	(*cursor)[1] = (uint8_t)(value >> 8);
	*cursor += 2;
}

static uint16_t read_u16le(const uint8_t **cursor)
{
	uint16_t value = (uint16_t)(*cursor)[0] | ((uint16_t)(*cursor)[1] << 8);
	*cursor += 2;
	return value;
}

static void encode_pose(const vrik_pose_v2_t *pose, uint8_t wire[VRIK_POSE_WIRE_BYTES])
{
	uint8_t *cursor = wire;
	int tracker;
	int axis;

	write_u16le(&cursor, pose->sequence);
	*cursor++ = pose->flags;
	write_u16le(&cursor, quantize_angle(pose->body_yaw));
	for (tracker = 0; tracker < VRIK_TRACKER_COUNT; tracker++)
		for (axis = 0; axis < 3; axis++)
			write_u16le(&cursor, quantize_position(pose->position[tracker][axis]));
	for (tracker = 0; tracker < VRIK_TRACKER_COUNT; tracker++)
		for (axis = 0; axis < 3; axis++)
			write_u16le(&cursor, quantize_angle(pose->orientation[tracker][axis]));
	for (axis = 0; axis < 3; axis++)
		write_u16le(&cursor, quantize_angle(pose->aim_orientation[axis]));

	expect(cursor == wire + VRIK_POSE_WIRE_BYTES, "encoder length is not 47 bytes");
}

static void decode_pose(const uint8_t wire[VRIK_POSE_WIRE_BYTES], vrik_pose_v2_t *pose)
{
	const uint8_t *cursor = wire;
	int tracker;
	int axis;

	memset(pose, 0, sizeof(*pose));
	pose->sequence = read_u16le(&cursor);
	pose->flags = *cursor++;
	pose->body_yaw = sign_extend_u16(read_u16le(&cursor)) * (360.0f / 65536.0f);
	for (tracker = 0; tracker < VRIK_TRACKER_COUNT; tracker++)
		for (axis = 0; axis < 3; axis++)
			pose->position[tracker][axis] = sign_extend_u16(read_u16le(&cursor)) * (1.0f / 8.0f);
	for (tracker = 0; tracker < VRIK_TRACKER_COUNT; tracker++)
		for (axis = 0; axis < 3; axis++)
			pose->orientation[tracker][axis] = sign_extend_u16(read_u16le(&cursor)) * (360.0f / 65536.0f);
	for (axis = 0; axis < 3; axis++)
		pose->aim_orientation[axis] = sign_extend_u16(read_u16le(&cursor)) * (360.0f / 65536.0f);

	expect(cursor == wire + VRIK_POSE_WIRE_BYTES, "decoder length is not 47 bytes");
}

static int sequence_is_newer(uint16_t sequence, uint16_t previous)
{
	return sign_extend_u16((uint16_t)(sequence - previous)) > 0;
}

static void expect_words(const vrik_pose_v2_t *pose, const uint16_t expected[24])
{
	int index = 0;
	int tracker;
	int axis;

	expect(pose->sequence == expected[index++], "decoded sequence differs");
	expect(pose->flags == expected[index++], "decoded flags differ");
	expect(quantize_angle(pose->body_yaw) == expected[index++], "decoded body yaw differs");
	for (tracker = 0; tracker < VRIK_TRACKER_COUNT; tracker++)
		for (axis = 0; axis < 3; axis++)
			expect(quantize_position(pose->position[tracker][axis]) == expected[index++],
				"decoded position ordering differs");
	for (tracker = 0; tracker < VRIK_TRACKER_COUNT; tracker++)
		for (axis = 0; axis < 3; axis++)
			expect(quantize_angle(pose->orientation[tracker][axis]) == expected[index++],
				"decoded orientation ordering differs");
	for (axis = 0; axis < 3; axis++)
		expect(quantize_angle(pose->aim_orientation[axis]) == expected[index++],
			"decoded aim ordering differs");
	expect(index == 24, "fixture word count differs");
}

static void test_quantization_boundaries(void)
{
	expect(quantize_position(-4096.0f) == 0x8000U, "position minimum must encode as 0x8000");
	expect(quantize_position(4095.875f) == 0x7fffU, "position maximum must encode as 0x7fff");
	expect(quantize_position(0.0625f) == 0x0001U, "positive position half step must round away from zero");
	expect(quantize_position(-0.0625f) == 0xffffU, "negative position half step must round away from zero");
	expect(quantize_angle(-180.0f) == 0x8000U, "-180 degrees must encode as 0x8000");
	expect(quantize_angle(180.0f) == 0x8000U, "180 degrees must encode as 0x8000");
	expect(quantize_angle(360.0f) == 0x0000U, "360 degrees must wrap to zero");
	expect(quantize_angle(-360.0f) == 0x0000U, "-360 degrees must wrap to zero");
	expect(quantize_angle(-360.0f / 131072.0f) == 0xffffU,
		"negative angle half step must round away from zero");
}

static void test_sequence_wrap_and_reorder(void)
{
	uint16_t accepted = 0xfffeU;

	expect(sequence_is_newer(0xffffU, 0xfffeU), "0xffff must follow 0xfffe");
	accepted = 0xffffU;
	expect(sequence_is_newer(0x0000U, accepted), "zero must follow 0xffff");
	accepted = 0x0000U;
	expect(!sequence_is_newer(0xffffU, accepted), "reordered 0xffff must be rejected after wrap");
	expect(sequence_is_newer(0x0001U, accepted), "one must follow zero");
	expect(!sequence_is_newer(0x0000U, 0x0000U), "duplicate sequence must be rejected");
	expect(sequence_is_newer(0x7fffU, 0x0000U), "largest positive delta must be newer");
	expect(!sequence_is_newer(0x8000U, 0x0000U), "half-range delta must not be newer");
}

int main(void)
{
	static const uint8_t golden[VRIK_POSE_WIRE_BYTES] = {
		0xef, 0xbe, 0x1f, 0x00, 0x80,
		0x00, 0x80, 0xff, 0x7f, 0x01, 0x00,
		0xff, 0xff, 0x0a, 0x00, 0xf6, 0xff,
		0x62, 0x00, 0x9e, 0xff, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x40, 0x00, 0x80,
		0x00, 0xc0, 0xff, 0xff, 0x00, 0xc0,
		0x00, 0x40, 0x00, 0x00, 0x00, 0xc0,
		0x00, 0x80, 0xff, 0x7f, 0x00, 0x00
	};
	static const uint16_t expected_words[24] = {
		0xbeef, 0x001f, 0x8000,
		0x8000, 0x7fff, 0x0001, 0xffff, 0x000a, 0xfff6, 0x0062, 0xff9e, 0x0000,
		0x0000, 0x4000, 0x8000, 0xc000, 0xffff, 0xc000, 0x4000, 0x0000, 0xc000,
		0x8000, 0x7fff, 0x0000
	};
	vrik_pose_v2_t source;
	vrik_pose_v2_t decoded;
	uint8_t wire[VRIK_POSE_WIRE_BYTES];
	int index;

	memset(&source, 0, sizeof(source));
	source.sequence = 0xbeefU;
	source.flags = VRIK_FLAG_ACTIVE | VRIK_FLAG_HEAD_TRACKED |
		VRIK_FLAG_LEFT_HAND_TRACKED | VRIK_FLAG_RIGHT_HAND_TRACKED | VRIK_FLAG_DOMINANT_LEFT;
	source.body_yaw = -180.0f;
	source.position[0][0] = -4096.0f;
	source.position[0][1] = 4095.875f;
	source.position[0][2] = 0.0625f;
	source.position[1][0] = -0.0625f;
	source.position[1][1] = 1.1875f;
	source.position[1][2] = -1.1875f;
	source.position[2][0] = 12.25f;
	source.position[2][1] = -12.25f;
	source.orientation[0][0] = 0.0f;
	source.orientation[0][1] = 90.0f;
	source.orientation[0][2] = 180.0f;
	source.orientation[1][0] = -90.0f;
	source.orientation[1][1] = -360.0f / 131072.0f;
	source.orientation[1][2] = -90.0f;
	source.orientation[2][0] = 90.0f;
	source.orientation[2][2] = -90.0f;
	source.aim_orientation[0] = -180.0f;
	source.aim_orientation[1] = 180.0f - 360.0f / 65536.0f;
	source.aim_orientation[2] = 360.0f;

	encode_pose(&source, wire);
	expect(memcmp(wire, golden, sizeof(golden)) == 0, "encoded pose differs from v2 golden bytes");
	decode_pose(golden, &decoded);
	expect_words(&decoded, expected_words);

	memset(&source, 0, sizeof(source));
	source.sequence = 0x1234U;
	encode_pose(&source, wire);
	expect(wire[0] == 0x34 && wire[1] == 0x12 && wire[2] == 0,
		"inactive pose must preserve sequence and zero active flags");
	for (index = 3; index < VRIK_POSE_WIRE_BYTES; index++)
		expect(wire[index] == 0, "inactive pose must have a deterministic zero payload");

	test_quantization_boundaries();
	test_sequence_wrap_and_reorder();
	if (failures)
		return EXIT_FAILURE;
	for (index = 0; index < VRIK_POSE_WIRE_BYTES; index++)
		printf("%02x", golden[index]);
	putchar('\n');
	return EXIT_SUCCESS;
}

