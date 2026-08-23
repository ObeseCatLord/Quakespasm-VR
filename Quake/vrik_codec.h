/*
 * VRIK wire-body codecs.  These functions intentionally do not include a
 * client/server opcode: callers frame the returned bodies themselves.
 */
#ifndef VRIK_CODEC_H
#define VRIK_CODEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VRIK_V2_BODY_BYTES 47u
#define VRIK_V3_HEADER_BYTES 7u
#define VRIK_V3_AIM_BYTES 6u
#define VRIK_V3_TARGET_BYTES 12u
#define VRIK_TARGET_MASK_ALL 0x3fu

/* This is the current protocol-v2 server admission policy, kept separate
 * from the wider signed-13.3 wire range. */
#define VRIK_V2_LEGACY_MAX_ROOT_LOCAL_OFFSET 256.0f

/* v2 has tracking bits in its flags; v3 instead puts tracking in a mask. */
#define VRIK_V2_FLAG_ACTIVE 0x01u
#define VRIK_V2_FLAG_HEAD_TRACKED 0x02u
#define VRIK_V2_FLAG_LEFT_HAND_TRACKED 0x04u
#define VRIK_V2_FLAG_RIGHT_HAND_TRACKED 0x08u
#define VRIK_V2_FLAG_DOMINANT_LEFT 0x10u
#define VRIK_V2_FLAG_KNOWN (VRIK_V2_FLAG_ACTIVE | VRIK_V2_FLAG_HEAD_TRACKED | \
	VRIK_V2_FLAG_LEFT_HAND_TRACKED | VRIK_V2_FLAG_RIGHT_HAND_TRACKED | \
	VRIK_V2_FLAG_DOMINANT_LEFT)

#define VRIK_V3_FLAG_ACTIVE 0x01u
#define VRIK_V3_FLAG_DOMINANT_LEFT 0x02u
#define VRIK_V3_FLAG_KNOWN (VRIK_V3_FLAG_ACTIVE | VRIK_V3_FLAG_DOMINANT_LEFT)

typedef enum vrik_target_e {
	VRIK_TARGET_HEAD = 0,
	VRIK_TARGET_LEFT_HAND,
	VRIK_TARGET_RIGHT_HAND,
	VRIK_TARGET_HIP,
	VRIK_TARGET_LEFT_FOOT,
	VRIK_TARGET_RIGHT_FOOT,
	VRIK_TARGET_COUNT
} vrik_target_t;

#define VRIK_TARGET_BIT(target) ((uint8_t)(1u << (unsigned)(target)))

typedef enum vrik_codec_status_e {
	VRIK_CODEC_OK = 0,
	VRIK_CODEC_TRUNCATED,
	VRIK_CODEC_MALFORMED,
	VRIK_CODEC_RANGE,
	VRIK_CODEC_INVALID_ARGUMENT
} vrik_codec_status_t;

typedef struct vrik_transform_s {
	float position[3];
	float orientation[3];
} vrik_transform_t;

/* Canonical target order is HEAD, LEFT_HAND, RIGHT_HAND, HIP, LEFT_FOOT,
 * RIGHT_FOOT.  v3 flags use VRIK_V3_FLAG_* values. */
typedef struct vrik_pose_s {
	uint16_t sequence;
	uint8_t flags;
	uint8_t present_mask;
	uint8_t tracked_mask;
	float body_yaw;
	vrik_transform_t targets[VRIK_TARGET_COUNT];
	float aim_orientation[3];
} vrik_codec_pose_t;

typedef struct vrik_v2_pose_s {
	uint16_t sequence;
	uint8_t flags;
	float body_yaw;
	vrik_transform_t targets[3]; /* HEAD, LEFT_HAND, RIGHT_HAND */
	float aim_orientation[3];
} vrik_v2_pose_t;

/* Decode accepts a larger containing buffer and reports the exact body bytes
 * consumed.  A caller wanting one exact body must compare *consumed to size.
 * v2 decode is structural-wire validation: it deliberately accepts every
 * signed-13.3 position the established 47-byte body can carry. */
vrik_codec_status_t vrik_v2_decode(const uint8_t *buffer, size_t size,
	vrik_v2_pose_t *out, size_t *consumed);
vrik_codec_status_t vrik_v2_encode(const vrik_v2_pose_t *pose, uint8_t *buffer,
	size_t size, size_t *written);

/* Apply the current protocol-v2 gameplay admission policy after structural
 * decode/validation.  Every HEAD/LEFT_HAND/RIGHT_HAND root-local position,
 * including an untracked target's fixed payload, must have length <= 256.
 * This returns VRIK_CODEC_RANGE for a policy-distance failure. */
vrik_codec_status_t vrik_v2_validate_legacy_pose(const vrik_v2_pose_t *pose);

vrik_codec_status_t vrik_v3_body_size(uint8_t present_mask, size_t *body_size);
vrik_codec_status_t vrik_v3_decode(const uint8_t *buffer, size_t size,
	vrik_codec_pose_t *out, size_t *consumed);
vrik_codec_status_t vrik_v3_encode(const vrik_codec_pose_t *pose, uint8_t *buffer,
	size_t size, size_t *written);

/* Active v2 samples preserve their representable payload.  Inactive v2 has a
 * fixed legacy body whose ignored payload may be nonzero; conversion produces
 * the one canonical v3 clear (all payload and masks zero). */
vrik_codec_status_t vrik_v2_to_normalized(const vrik_v2_pose_t *v2,
	vrik_codec_pose_t *out);
/* This is a v2 downgrade.  HIP and feet intentionally have no v2
 * representation.  A present-but-untracked v3 HEAD/LEFT_HAND/RIGHT_HAND is
 * representable by neither v2 state, so conversion rejects it rather than
 * silently dropping its presence. */
vrik_codec_status_t vrik_normalized_to_v2(const vrik_codec_pose_t *pose,
	vrik_v2_pose_t *out);

/* Latch an offered wire version exactly once.  A repeated offer, including a
 * different valid version, leaves the established version unchanged. */
vrik_codec_status_t vrik_latch_protocol_version(uint8_t offered_version,
	int *latched, uint8_t *version);

/* Modular sequence ordering: a positive signed 16-bit delta is newer; the
 * half-range delta (0x8000) is deliberately not newer. */
int vrik_sequence_is_newer(uint16_t sequence, uint16_t previous);

#ifdef __cplusplus
}
#endif

#endif /* VRIK_CODEC_H */
