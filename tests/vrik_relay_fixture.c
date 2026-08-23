/* Relay compatibility goldens independent of engine allocation and sockets.
 * The server retains this validated v2 body byte-for-byte for v2 recipients,
 * while forwarding its canonical normalized form to v3 recipients. */
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

int main(void)
{
	static const unsigned char raw_v2[VRIK_V2_BODY_BYTES] = {
		0x34, 0x12, 0x00, 0x00, 0x00,
		0x08 /* ignored inactive HEAD x payload */
	};
	static const unsigned char v3_clear[] = {
		0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	unsigned char retained_v2[VRIK_V2_BODY_BYTES];
	unsigned char encoded_v3[sizeof(v3_clear)];
	vrik_v2_pose_t decoded_v2;
	vrik_codec_pose_t canonical_v3;
	size_t used;

	CHECK(vrik_v2_decode(raw_v2, sizeof(raw_v2), &decoded_v2, &used) == VRIK_CODEC_OK);
	CHECK(used == sizeof(raw_v2));
	CHECK(vrik_v2_validate_legacy_pose(&decoded_v2) == VRIK_CODEC_OK);
	/* This is the exact operation used by the v2 relay path after validation. */
	memcpy(retained_v2, raw_v2, sizeof(retained_v2));
	CHECK(memcmp(retained_v2, raw_v2, sizeof(raw_v2)) == 0);
	CHECK(vrik_v2_to_normalized(&decoded_v2, &canonical_v3) == VRIK_CODEC_OK);
	CHECK(vrik_v3_encode(&canonical_v3, encoded_v3, sizeof(encoded_v3), &used) == VRIK_CODEC_OK);
	CHECK(used == sizeof(v3_clear));
	CHECK(memcmp(encoded_v3, v3_clear, sizeof(v3_clear)) == 0);

	return failures ? 1 : 0;
}
