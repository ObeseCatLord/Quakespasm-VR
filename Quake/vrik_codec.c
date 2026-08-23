#include "vrik_codec.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#if defined(_MSC_VER)
#include <float.h>
#define VRIK_ISFINITE(value) (_finite(value) != 0)
#else
#define VRIK_ISFINITE(value) isfinite(value)
#endif

#define VRIK_ANGLE_SCALE (65536.0 / 360.0)
#define VRIK_ANGLE_UNSCALE (360.0f / 65536.0f)

static uint16_t vrik_read_u16le(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void vrik_write_u16le(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)(value & 0xffu);
	p[1] = (uint8_t)(value >> 8);
}

static int16_t vrik_u16_to_i16(uint16_t value)
{
	return value < 0x8000u ? (int16_t)value : (int16_t)((int)value - 0x10000);
}

/* Quake's Q_rint rule, called only after range checks keep the cast safe. */
static int vrik_q_rint(double value)
{
	return value > 0.0 ? (int)(value + 0.5) : (int)(value - 0.5);
}

static vrik_codec_status_t vrik_quantize_position(float value, uint16_t *out)
{
	double scaled;
	int rounded;

	if (!VRIK_ISFINITE(value))
		return VRIK_CODEC_RANGE;
	scaled = (double)value * 8.0;
	/* Strict inequalities are necessary: ties round away from zero. */
	if (!(scaled > -32768.5 && scaled < 32767.5))
		return VRIK_CODEC_RANGE;
	rounded = vrik_q_rint(scaled);
	*out = (uint16_t)(int16_t)rounded;
	return VRIK_CODEC_OK;
}

static vrik_codec_status_t vrik_quantize_angle(float value, uint16_t *out)
{
	double scaled;
	double reduced;
	int rounded;

	if (!VRIK_ISFINITE(value))
		return VRIK_CODEC_RANGE;
	/* Reducing before rounding is congruent modulo 65536 and avoids an unsafe
	 * integer conversion for otherwise valid, very large finite angles. */
	scaled = (double)value * VRIK_ANGLE_SCALE;
	reduced = fmod(scaled, 65536.0);
	rounded = vrik_q_rint(reduced);
	*out = (uint16_t)rounded;
	return VRIK_CODEC_OK;
}

static int vrik_transform_finite_and_in_range(const vrik_transform_t *target)
{
	int axis;
	uint16_t ignored;

	for (axis = 0; axis < 3; ++axis)
		if (vrik_quantize_position(target->position[axis], &ignored) != VRIK_CODEC_OK ||
			vrik_quantize_angle(target->orientation[axis], &ignored) != VRIK_CODEC_OK)
			return 0;
	return 1;
}

static int vrik_float_is_zero(float value)
{
	return value == 0.0f;
}

static int vrik_pose_has_zero_payload(const vrik_codec_pose_t *pose)
{
	int target;
	int axis;
	if (!vrik_float_is_zero(pose->body_yaw))
		return 0;
	for (target = 0; target < VRIK_TARGET_COUNT; ++target)
		for (axis = 0; axis < 3; ++axis)
			if (!vrik_float_is_zero(pose->targets[target].position[axis]) ||
				!vrik_float_is_zero(pose->targets[target].orientation[axis]))
				return 0;
	for (axis = 0; axis < 3; ++axis)
		if (!vrik_float_is_zero(pose->aim_orientation[axis]))
			return 0;
	return 1;
}

static int vrik_pose_payload_is_finite(const vrik_codec_pose_t *pose)
{
	int target;
	int axis;

	if (!VRIK_ISFINITE(pose->body_yaw))
		return 0;
	for (target = 0; target < VRIK_TARGET_COUNT; ++target)
		for (axis = 0; axis < 3; ++axis)
			if (!VRIK_ISFINITE(pose->targets[target].position[axis]) ||
				!VRIK_ISFINITE(pose->targets[target].orientation[axis]))
				return 0;
	for (axis = 0; axis < 3; ++axis)
		if (!VRIK_ISFINITE(pose->aim_orientation[axis]))
			return 0;
	return 1;
}

static vrik_codec_status_t vrik_validate_v2_wire(const vrik_v2_pose_t *pose)
{
	int target;
	int axis;
	uint16_t ignored;

	if (!pose)
		return VRIK_CODEC_INVALID_ARGUMENT;
	if (pose->flags & (uint8_t)~VRIK_V2_FLAG_KNOWN)
		return VRIK_CODEC_MALFORMED;
	if ((pose->flags & VRIK_V2_FLAG_ACTIVE) &&
		!(pose->flags & VRIK_V2_FLAG_HEAD_TRACKED))
		return VRIK_CODEC_MALFORMED;
	if (vrik_quantize_angle(pose->body_yaw, &ignored) != VRIK_CODEC_OK)
		return VRIK_CODEC_RANGE;
	for (target = 0; target < 3; ++target)
		if (!vrik_transform_finite_and_in_range(&pose->targets[target]))
			return VRIK_CODEC_RANGE;
	for (axis = 0; axis < 3; ++axis)
		if (vrik_quantize_angle(pose->aim_orientation[axis], &ignored) != VRIK_CODEC_OK)
			return VRIK_CODEC_RANGE;
	return VRIK_CODEC_OK;
}

vrik_codec_status_t vrik_v2_validate_legacy_pose(const vrik_v2_pose_t *pose)
{
	vrik_codec_status_t status;
	int target;
	double x;
	double y;
	double z;
	double max_distance_squared;

	status = vrik_validate_v2_wire(pose);
	if (status != VRIK_CODEC_OK)
		return status;
	max_distance_squared = (double)VRIK_V2_LEGACY_MAX_ROOT_LOCAL_OFFSET *
		(double)VRIK_V2_LEGACY_MAX_ROOT_LOCAL_OFFSET;
	for (target = 0; target < 3; ++target) {
		x = (double)pose->targets[target].position[0];
		y = (double)pose->targets[target].position[1];
		z = (double)pose->targets[target].position[2];
		if (x * x + y * y + z * z > max_distance_squared)
			return VRIK_CODEC_RANGE;
	}
	return VRIK_CODEC_OK;
}

static vrik_codec_status_t vrik_validate_v3(const vrik_codec_pose_t *pose)
{
	int target;
	int axis;
	uint16_t ignored;

	if (!pose)
		return VRIK_CODEC_INVALID_ARGUMENT;
	if ((pose->flags & (uint8_t)~VRIK_V3_FLAG_KNOWN) ||
		(pose->present_mask & (uint8_t)~VRIK_TARGET_MASK_ALL) ||
		(pose->tracked_mask & (uint8_t)~VRIK_TARGET_MASK_ALL) ||
		(pose->tracked_mask & (uint8_t)~pose->present_mask))
		return VRIK_CODEC_MALFORMED;
	if (!(pose->flags & VRIK_V3_FLAG_ACTIVE)) {
		if (!vrik_pose_payload_is_finite(pose))
			return VRIK_CODEC_RANGE;
		if (pose->flags != 0 || pose->present_mask != 0 || pose->tracked_mask != 0 ||
			!vrik_pose_has_zero_payload(pose))
			return VRIK_CODEC_MALFORMED;
		return VRIK_CODEC_OK;
	}
	if (!(pose->present_mask & VRIK_TARGET_BIT(VRIK_TARGET_HEAD)))
		return VRIK_CODEC_MALFORMED;
	if (vrik_quantize_angle(pose->body_yaw, &ignored) != VRIK_CODEC_OK)
		return VRIK_CODEC_RANGE;
	for (target = 0; target < VRIK_TARGET_COUNT; ++target)
		if ((pose->present_mask & VRIK_TARGET_BIT(target)) &&
			!vrik_transform_finite_and_in_range(&pose->targets[target]))
			return VRIK_CODEC_RANGE;
	for (axis = 0; axis < 3; ++axis)
		if (vrik_quantize_angle(pose->aim_orientation[axis], &ignored) != VRIK_CODEC_OK)
			return VRIK_CODEC_RANGE;
	return VRIK_CODEC_OK;
}

vrik_codec_status_t vrik_v3_body_size(uint8_t present_mask, size_t *body_size)
{
	size_t count = 0;
	uint8_t mask;

	if (!body_size)
		return VRIK_CODEC_INVALID_ARGUMENT;
	if (present_mask & (uint8_t)~VRIK_TARGET_MASK_ALL)
		return VRIK_CODEC_MALFORMED;
	for (mask = present_mask; mask != 0; mask = (uint8_t)(mask >> 1))
		count += (size_t)(mask & 1u);
	if (count > (SIZE_MAX - VRIK_V3_HEADER_BYTES - VRIK_V3_AIM_BYTES) / VRIK_V3_TARGET_BYTES)
		return VRIK_CODEC_RANGE;
	*body_size = VRIK_V3_HEADER_BYTES + count * VRIK_V3_TARGET_BYTES + VRIK_V3_AIM_BYTES;
	return VRIK_CODEC_OK;
}

vrik_codec_status_t vrik_v2_decode(const uint8_t *buffer, size_t size,
	vrik_v2_pose_t *out, size_t *consumed)
{
	vrik_v2_pose_t decoded;
	const uint8_t *p;
	int target;
	int axis;
	vrik_codec_status_t status;

	if (consumed)
		*consumed = 0;
	if (!buffer || !out)
		return VRIK_CODEC_INVALID_ARGUMENT;
	if (size < VRIK_V2_BODY_BYTES)
		return VRIK_CODEC_TRUNCATED;
	p = buffer;
	memset(&decoded, 0, sizeof(decoded));
	decoded.sequence = vrik_read_u16le(p); p += 2;
	decoded.flags = *p++;
	decoded.body_yaw = (float)vrik_u16_to_i16(vrik_read_u16le(p)) * VRIK_ANGLE_UNSCALE; p += 2;
	for (target = 0; target < 3; ++target)
		for (axis = 0; axis < 3; ++axis) {
			decoded.targets[target].position[axis] = (float)vrik_u16_to_i16(vrik_read_u16le(p)) * (1.0f / 8.0f);
			p += 2;
		}
	for (target = 0; target < 3; ++target)
		for (axis = 0; axis < 3; ++axis) {
			decoded.targets[target].orientation[axis] = (float)vrik_u16_to_i16(vrik_read_u16le(p)) * VRIK_ANGLE_UNSCALE;
			p += 2;
		}
	for (axis = 0; axis < 3; ++axis) {
		decoded.aim_orientation[axis] = (float)vrik_u16_to_i16(vrik_read_u16le(p)) * VRIK_ANGLE_UNSCALE;
		p += 2;
	}
	status = vrik_validate_v2_wire(&decoded);
	if (status != VRIK_CODEC_OK)
		return status;
	*out = decoded;
	if (consumed)
		*consumed = VRIK_V2_BODY_BYTES;
	return VRIK_CODEC_OK;
}

vrik_codec_status_t vrik_v2_encode(const vrik_v2_pose_t *pose, uint8_t *buffer,
	size_t size, size_t *written)
{
	uint8_t encoded[VRIK_V2_BODY_BYTES];
	uint8_t *p = encoded;
	int target;
	int axis;
	uint16_t word = 0;
	vrik_codec_status_t status;

	if (written)
		*written = 0;
	if (!buffer || !pose)
		return VRIK_CODEC_INVALID_ARGUMENT;
	status = vrik_validate_v2_wire(pose);
	if (status != VRIK_CODEC_OK)
		return status;
	if (size < VRIK_V2_BODY_BYTES)
		return VRIK_CODEC_TRUNCATED;
	vrik_write_u16le(p, pose->sequence); p += 2;
	*p++ = pose->flags;
	(void)vrik_quantize_angle(pose->body_yaw, &word); vrik_write_u16le(p, word); p += 2;
	for (target = 0; target < 3; ++target)
		for (axis = 0; axis < 3; ++axis) {
			(void)vrik_quantize_position(pose->targets[target].position[axis], &word);
			vrik_write_u16le(p, word); p += 2;
		}
	for (target = 0; target < 3; ++target)
		for (axis = 0; axis < 3; ++axis) {
			(void)vrik_quantize_angle(pose->targets[target].orientation[axis], &word);
			vrik_write_u16le(p, word); p += 2;
		}
	for (axis = 0; axis < 3; ++axis) {
		(void)vrik_quantize_angle(pose->aim_orientation[axis], &word);
		vrik_write_u16le(p, word); p += 2;
	}
	memcpy(buffer, encoded, sizeof(encoded));
	if (written)
		*written = sizeof(encoded);
	return VRIK_CODEC_OK;
}

vrik_codec_status_t vrik_v3_decode(const uint8_t *buffer, size_t size,
	vrik_codec_pose_t *out, size_t *consumed)
{
	vrik_codec_pose_t decoded;
	const uint8_t *p;
	size_t required;
	int target;
	int axis;
	vrik_codec_status_t status;

	if (consumed)
		*consumed = 0;
	if (!buffer || !out)
		return VRIK_CODEC_INVALID_ARGUMENT;
	if (size < VRIK_V3_HEADER_BYTES)
		return VRIK_CODEC_TRUNCATED;
	status = vrik_v3_body_size(buffer[3], &required);
	if (status != VRIK_CODEC_OK)
		return status;
	if (size < required)
		return VRIK_CODEC_TRUNCATED;
	p = buffer;
	memset(&decoded, 0, sizeof(decoded));
	decoded.sequence = vrik_read_u16le(p); p += 2;
	decoded.flags = *p++;
	decoded.present_mask = *p++;
	decoded.tracked_mask = *p++;
	decoded.body_yaw = (float)vrik_u16_to_i16(vrik_read_u16le(p)) * VRIK_ANGLE_UNSCALE; p += 2;
	for (target = 0; target < VRIK_TARGET_COUNT; ++target)
		if (decoded.present_mask & VRIK_TARGET_BIT(target)) {
			for (axis = 0; axis < 3; ++axis) {
				decoded.targets[target].position[axis] = (float)vrik_u16_to_i16(vrik_read_u16le(p)) * (1.0f / 8.0f);
				p += 2;
			}
			for (axis = 0; axis < 3; ++axis) {
				decoded.targets[target].orientation[axis] = (float)vrik_u16_to_i16(vrik_read_u16le(p)) * VRIK_ANGLE_UNSCALE;
				p += 2;
			}
		}
	for (axis = 0; axis < 3; ++axis) {
		decoded.aim_orientation[axis] = (float)vrik_u16_to_i16(vrik_read_u16le(p)) * VRIK_ANGLE_UNSCALE;
		p += 2;
	}
	status = vrik_validate_v3(&decoded);
	if (status != VRIK_CODEC_OK)
		return status;
	*out = decoded;
	if (consumed)
		*consumed = required;
	return VRIK_CODEC_OK;
}

vrik_codec_status_t vrik_v3_encode(const vrik_codec_pose_t *pose, uint8_t *buffer,
	size_t size, size_t *written)
{
	uint8_t encoded[VRIK_V3_HEADER_BYTES + VRIK_TARGET_COUNT * VRIK_V3_TARGET_BYTES + VRIK_V3_AIM_BYTES];
	uint8_t *p = encoded;
	size_t required;
	int target;
	int axis;
	uint16_t word = 0;
	vrik_codec_status_t status;

	if (written)
		*written = 0;
	if (!buffer || !pose)
		return VRIK_CODEC_INVALID_ARGUMENT;
	status = vrik_validate_v3(pose);
	if (status != VRIK_CODEC_OK)
		return status;
	status = vrik_v3_body_size(pose->present_mask, &required);
	if (status != VRIK_CODEC_OK)
		return status;
	if (size < required)
		return VRIK_CODEC_TRUNCATED;
	vrik_write_u16le(p, pose->sequence); p += 2;
	*p++ = pose->flags;
	*p++ = pose->present_mask;
	*p++ = pose->tracked_mask;
	(void)vrik_quantize_angle(pose->body_yaw, &word); vrik_write_u16le(p, word); p += 2;
	for (target = 0; target < VRIK_TARGET_COUNT; ++target)
		if (pose->present_mask & VRIK_TARGET_BIT(target)) {
			for (axis = 0; axis < 3; ++axis) {
				(void)vrik_quantize_position(pose->targets[target].position[axis], &word);
				vrik_write_u16le(p, word); p += 2;
			}
			for (axis = 0; axis < 3; ++axis) {
				(void)vrik_quantize_angle(pose->targets[target].orientation[axis], &word);
				vrik_write_u16le(p, word); p += 2;
			}
		}
	for (axis = 0; axis < 3; ++axis) {
		(void)vrik_quantize_angle(pose->aim_orientation[axis], &word);
		vrik_write_u16le(p, word); p += 2;
	}
	memcpy(buffer, encoded, required);
	if (written)
		*written = required;
	return VRIK_CODEC_OK;
}

vrik_codec_status_t vrik_v2_to_normalized(const vrik_v2_pose_t *v2, vrik_codec_pose_t *out)
{
	vrik_codec_status_t status;

	if (!out)
		return VRIK_CODEC_INVALID_ARGUMENT;
	status = vrik_validate_v2_wire(v2);
	if (status != VRIK_CODEC_OK)
		return status;
	memset(out, 0, sizeof(*out));
	out->sequence = v2->sequence;
	/* Legacy v2 transmits a complete fixed-size body even when inactive.  That
	 * payload has no v3 meaning: v3 requires a zero clear body. */
	if (!(v2->flags & VRIK_V2_FLAG_ACTIVE))
		return VRIK_CODEC_OK;
	out->flags = (uint8_t)(((v2->flags & VRIK_V2_FLAG_ACTIVE) ? VRIK_V3_FLAG_ACTIVE : 0u) |
		((v2->flags & VRIK_V2_FLAG_DOMINANT_LEFT) ? VRIK_V3_FLAG_DOMINANT_LEFT : 0u));
	out->present_mask = 0;
	if (v2->flags & VRIK_V2_FLAG_HEAD_TRACKED)
		out->present_mask |= VRIK_TARGET_BIT(VRIK_TARGET_HEAD);
	if (v2->flags & VRIK_V2_FLAG_LEFT_HAND_TRACKED)
		out->present_mask |= VRIK_TARGET_BIT(VRIK_TARGET_LEFT_HAND);
	if (v2->flags & VRIK_V2_FLAG_RIGHT_HAND_TRACKED)
		out->present_mask |= VRIK_TARGET_BIT(VRIK_TARGET_RIGHT_HAND);
	out->tracked_mask = out->present_mask;
	out->body_yaw = v2->body_yaw;
	memcpy(out->targets, v2->targets, sizeof(v2->targets));
	memcpy(out->aim_orientation, v2->aim_orientation, sizeof(out->aim_orientation));
	return VRIK_CODEC_OK;
}

vrik_codec_status_t vrik_normalized_to_v2(const vrik_codec_pose_t *pose, vrik_v2_pose_t *out)
{
	vrik_v2_pose_t converted;
	int target;
	vrik_codec_status_t status;

	if (!pose || !out)
		return VRIK_CODEC_INVALID_ARGUMENT;
	/* Permit legacy v2 normalized inactive payloads, but still reject all other
	 * impossible normalized states before dropping body-only roles. */
	if ((pose->flags & (uint8_t)~VRIK_V3_FLAG_KNOWN) ||
		(pose->present_mask & (uint8_t)~VRIK_TARGET_MASK_ALL) ||
		(pose->tracked_mask & (uint8_t)~VRIK_TARGET_MASK_ALL) ||
		(pose->tracked_mask & (uint8_t)~pose->present_mask) ||
		((pose->flags & VRIK_V3_FLAG_ACTIVE) &&
		 !(pose->present_mask & VRIK_TARGET_BIT(VRIK_TARGET_HEAD))))
		return VRIK_CODEC_MALFORMED;
	if (pose->present_mask & (uint8_t)~pose->tracked_mask &
		(VRIK_TARGET_BIT(VRIK_TARGET_HEAD) |
		 VRIK_TARGET_BIT(VRIK_TARGET_LEFT_HAND) |
		 VRIK_TARGET_BIT(VRIK_TARGET_RIGHT_HAND)))
		return VRIK_CODEC_MALFORMED;
	memset(&converted, 0, sizeof(converted));
	converted.sequence = pose->sequence;
	converted.flags = (uint8_t)(((pose->flags & VRIK_V3_FLAG_ACTIVE) ? VRIK_V2_FLAG_ACTIVE : 0u) |
		((pose->flags & VRIK_V3_FLAG_DOMINANT_LEFT) ? VRIK_V2_FLAG_DOMINANT_LEFT : 0u));
	for (target = 0; target < 3; ++target)
		if (pose->tracked_mask & VRIK_TARGET_BIT(target))
			converted.flags |= (uint8_t)(VRIK_V2_FLAG_HEAD_TRACKED << target);
	converted.body_yaw = pose->body_yaw;
	memcpy(converted.targets, pose->targets, sizeof(converted.targets));
	memcpy(converted.aim_orientation, pose->aim_orientation, sizeof(converted.aim_orientation));
	status = vrik_validate_v2_wire(&converted);
	if (status != VRIK_CODEC_OK)
		return status;
	*out = converted;
	return VRIK_CODEC_OK;
}

vrik_codec_status_t vrik_latch_protocol_version(uint8_t offered_version,
	int *latched, uint8_t *version)
{
	if (!latched || !version)
		return VRIK_CODEC_INVALID_ARGUMENT;
	if (offered_version != 2u && offered_version != 3u)
		return VRIK_CODEC_MALFORMED;
	if (*latched)
		return VRIK_CODEC_OK;
	*version = offered_version;
	*latched = 1;
	return VRIK_CODEC_OK;
}

int vrik_sequence_is_newer(uint16_t sequence, uint16_t previous)
{
	return vrik_u16_to_i16((uint16_t)(sequence - previous)) > 0;
}
