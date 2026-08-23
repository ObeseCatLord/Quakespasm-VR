/* Hardware-free v3 framing checks.  The engine readers use the declared
 * envelope length before decoding so malformed bodies cannot consume the
 * following command in a datagram. */
#include "../Quake/vrik_codec.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

static size_t decode_framed_v3(const unsigned char *packet, size_t packet_size,
	vrik_codec_pose_t *out, vrik_codec_status_t *status)
{
	size_t consumed = 0;
	unsigned int length;

	if (!packet || !out || !status || packet_size < 1)
		return 0;
	length = packet[0];
	if (length > VRIK_V3_HEADER_BYTES + VRIK_TARGET_COUNT *
		VRIK_V3_TARGET_BYTES + VRIK_V3_AIM_BYTES || packet_size - 1 < length)
		return 0;
	*status = vrik_v3_decode(packet + 1, length, out, &consumed);
	/* Exactly the declared body is consumed even when the codec rejects it. */
	return 1 + length;
}

int main(void)
{
	vrik_codec_pose_t source;
	vrik_codec_pose_t decoded;
	unsigned char packet[1 + VRIK_V3_HEADER_BYTES +
		VRIK_TARGET_COUNT * VRIK_V3_TARGET_BYTES + VRIK_V3_AIM_BYTES];
	size_t body_size = 0;
	size_t consumed;
	vrik_codec_status_t status;
	uint8_t version = 0;
	int latched = 0;

	CHECK(vrik_latch_protocol_version(3, &latched, &version) == VRIK_CODEC_OK);
	CHECK(latched && version == 3);
	CHECK(vrik_latch_protocol_version(2, &latched, &version) == VRIK_CODEC_OK);
	CHECK(version == 3); /* 3 -> 2 cannot change a sent capability. */
	latched = 0;
	version = 0;
	CHECK(vrik_latch_protocol_version(2, &latched, &version) == VRIK_CODEC_OK);
	CHECK(vrik_latch_protocol_version(3, &latched, &version) == VRIK_CODEC_OK);
	CHECK(version == 2); /* 2 -> 3 cannot change a sent capability. */
	CHECK(vrik_latch_protocol_version(2, &latched, &version) == VRIK_CODEC_OK);
	CHECK(version == 2); /* Duplicate is a no-op. */
	CHECK(vrik_latch_protocol_version(1, &latched, &version) == VRIK_CODEC_MALFORMED);
	CHECK(version == 2);

	memset(&source, 0, sizeof(source));
	source.sequence = 0xfffeu;
	source.flags = VRIK_V3_FLAG_ACTIVE;
	source.present_mask = VRIK_TARGET_BIT(VRIK_TARGET_HEAD) |
		VRIK_TARGET_BIT(VRIK_TARGET_HIP) |
		VRIK_TARGET_BIT(VRIK_TARGET_LEFT_FOOT);
	source.tracked_mask = VRIK_TARGET_BIT(VRIK_TARGET_HEAD) |
		VRIK_TARGET_BIT(VRIK_TARGET_LEFT_FOOT);
	source.targets[VRIK_TARGET_HEAD].position[2] = 48.0f;
	source.targets[VRIK_TARGET_HIP].position[2] = 24.0f;
	source.targets[VRIK_TARGET_LEFT_FOOT].position[0] = -8.0f;
	source.targets[VRIK_TARGET_LEFT_FOOT].position[2] = -32.0f;
	packet[0] = 0;
	CHECK(vrik_v3_encode(&source, packet + 1, sizeof(packet) - 1,
		&body_size) == VRIK_CODEC_OK);
	CHECK(body_size <= 255u);
	packet[0] = (unsigned char)body_size;
	status = VRIK_CODEC_INVALID_ARGUMENT;
	consumed = decode_framed_v3(packet, body_size + 1, &decoded, &status);
	CHECK(consumed == body_size + 1);
	CHECK(status == VRIK_CODEC_OK);
	CHECK(decoded.present_mask == source.present_mask);
	CHECK(decoded.tracked_mask == source.tracked_mask);

	/* An unknown mask bit is rejected, but the following byte remains aligned. */
	packet[4] |= 0x80u; /* body offset 3 is present_mask; +1 envelope */
	packet[body_size + 1] = 0xa5u;
	status = VRIK_CODEC_OK;
	consumed = decode_framed_v3(packet, body_size + 2, &decoded, &status);
	CHECK(consumed == body_size + 1);
	CHECK(status == VRIK_CODEC_MALFORMED);
	CHECK(packet[consumed] == 0xa5u);

	/* A declared size must exactly match the structural codec body. */
	packet[4] &= 0x7fu;
	packet[0] = (unsigned char)(body_size - 1);
	status = VRIK_CODEC_OK;
	consumed = decode_framed_v3(packet, body_size + 1, &decoded, &status);
	CHECK(consumed == body_size);
	CHECK(status != VRIK_CODEC_OK);

	return failures ? 1 : 0;
}
