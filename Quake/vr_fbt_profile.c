#include "vr_fbt_profile.h"

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define VR_FBT_PROFILE_METRES_LIMIT 2.0
#define VR_FBT_PROFILE_UNIT_TOLERANCE 0.02
#define VR_FBT_PROFILE_EPSILON 1e-12
#define VR_FBT_PROFILE_WINDOW_SECONDS 1.0
#define VR_FBT_PROFILE_LINEAR_VELOCITY_LIMIT 5.0
#define VR_FBT_PROFILE_ANGULAR_VELOCITY_LIMIT 20.0
#define VR_FBT_PROFILE_POSITION_INLIER_METRES 0.10
#define VR_FBT_PROFILE_ANGLE_INLIER_RADIANS 0.26179938779914943654 /* 15 degrees */

static void VR_FBT_ProfileSetError(vr_fbt_profile_error_t *error,
	vr_fbt_profile_error_t value)
{
	if (error)
		*error = value;
}

static int VR_FBT_ProfileFinite(double value)
{
	return isfinite(value) != 0;
}

static int VR_FBT_ProfileVectorFinite(const double *vector, unsigned int count)
{
	unsigned int i;
	for (i = 0; i < count; ++i)
		if (!VR_FBT_ProfileFinite(vector[i]))
			return 0;
	return 1;
}

static double VR_FBT_ProfileDot(const double *a, const double *b, unsigned int count)
{
	double result = 0.0;
	unsigned int i;
	for (i = 0; i < count; ++i)
		result += a[i] * b[i];
	return result;
}

static int VR_FBT_ProfileNormalize(double *vector, unsigned int count)
{
	double norm_squared;
	double inverse_norm;
	unsigned int i;
	if (!VR_FBT_ProfileVectorFinite(vector, count))
		return 0;
	norm_squared = VR_FBT_ProfileDot(vector, vector, count);
	if (!VR_FBT_ProfileFinite(norm_squared) || norm_squared <= VR_FBT_PROFILE_EPSILON)
		return 0;
	inverse_norm = 1.0 / sqrt(norm_squared);
	if (!VR_FBT_ProfileFinite(inverse_norm))
		return 0;
	for (i = 0; i < count; ++i)
		vector[i] *= inverse_norm;
	return VR_FBT_ProfileVectorFinite(vector, count);
}

static int VR_FBT_ProfileUnitWithinTolerance(const double *vector, unsigned int count)
{
	double norm_squared;
	double norm;
	if (!VR_FBT_ProfileVectorFinite(vector, count))
		return 0;
	norm_squared = VR_FBT_ProfileDot(vector, vector, count);
	if (!VR_FBT_ProfileFinite(norm_squared) || norm_squared <= VR_FBT_PROFILE_EPSILON)
		return 0;
	norm = sqrt(norm_squared);
	return norm >= 1.0 - VR_FBT_PROFILE_UNIT_TOLERANCE &&
		norm <= 1.0 + VR_FBT_PROFILE_UNIT_TOLERANCE;
}

static int VR_FBT_ProfileTransformSane(const vr_fbt_profile_transform_t *transform)
{
	unsigned int i;
	if (!transform || !VR_FBT_ProfileVectorFinite(transform->position, 3) ||
		!VR_FBT_ProfileVectorFinite(transform->orientation, 4))
		return 0;
	for (i = 0; i < 3; ++i)
		if (fabs(transform->position[i]) > VR_FBT_PROFILE_METRES_LIMIT)
			return 0;
	return VR_FBT_ProfileUnitWithinTolerance(transform->orientation, 4);
}

/* Runtime tracking poses may be far from a chosen calibration origin. */
static int VR_FBT_ProfileTrackingTransformSane(const vr_fbt_profile_transform_t *transform)
{
	double norm_squared;
	if (!transform || !VR_FBT_ProfileVectorFinite(transform->position, 3) ||
		!VR_FBT_ProfileVectorFinite(transform->orientation, 4))
		return 0;
	norm_squared = VR_FBT_ProfileDot(transform->orientation, transform->orientation, 4);
	if (!VR_FBT_ProfileFinite(norm_squared) || norm_squared <= VR_FBT_PROFILE_EPSILON)
		return 0;
	norm_squared = sqrt(norm_squared);
	return norm_squared >= 0.5 && norm_squared <= 1.5;
}

static int VR_FBT_ProfileTransformNormalize(const vr_fbt_profile_transform_t *input,
	vr_fbt_profile_transform_t *output)
{
	if (!input || !output || !VR_FBT_ProfileVectorFinite(input->position, 3) ||
		!VR_FBT_ProfileVectorFinite(input->orientation, 4))
		return 0;
	memcpy(output, input, sizeof(*output));
	return VR_FBT_ProfileNormalize(output->orientation, 4);
}

static void VR_FBT_ProfileRotate(const double q[4], const double in[3], double out[3])
{
	double uv[3];
	double uuv[3];
	double s = q[0];
	uv[0] = q[2] * in[2] - q[3] * in[1];
	uv[1] = q[3] * in[0] - q[1] * in[2];
	uv[2] = q[1] * in[1] - q[2] * in[0];
	uuv[0] = q[2] * uv[2] - q[3] * uv[1];
	uuv[1] = q[3] * uv[0] - q[1] * uv[2];
	uuv[2] = q[1] * uv[1] - q[2] * uv[0];
	out[0] = in[0] + 2.0 * (s * uv[0] + uuv[0]);
	out[1] = in[1] + 2.0 * (s * uv[1] + uuv[1]);
	out[2] = in[2] + 2.0 * (s * uv[2] + uuv[2]);
}

static void VR_FBT_ProfileQuaternionMultiply(const double a[4], const double b[4], double out[4])
{
	out[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
	out[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
	out[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
	out[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
}

int VR_FBT_ProfileTransformInverse(const vr_fbt_profile_transform_t *input,
	vr_fbt_profile_transform_t *output)
{
	vr_fbt_profile_transform_t normalized;
	double negative_position[3];
	if (!VR_FBT_ProfileTransformNormalize(input, &normalized) || !output)
		return 0;
	output->orientation[0] = normalized.orientation[0];
	output->orientation[1] = -normalized.orientation[1];
	output->orientation[2] = -normalized.orientation[2];
	output->orientation[3] = -normalized.orientation[3];
	negative_position[0] = -normalized.position[0];
	negative_position[1] = -normalized.position[1];
	negative_position[2] = -normalized.position[2];
	VR_FBT_ProfileRotate(output->orientation, negative_position, output->position);
	return VR_FBT_ProfileVectorFinite(output->position, 3);
}

int VR_FBT_ProfileTransformCompose(const vr_fbt_profile_transform_t *left,
	const vr_fbt_profile_transform_t *right, vr_fbt_profile_transform_t *output)
{
	vr_fbt_profile_transform_t a, b;
	double rotated[3];
	if (!VR_FBT_ProfileTransformNormalize(left, &a) ||
		!VR_FBT_ProfileTransformNormalize(right, &b) || !output)
		return 0;
	VR_FBT_ProfileRotate(a.orientation, b.position, rotated);
	output->position[0] = a.position[0] + rotated[0];
	output->position[1] = a.position[1] + rotated[1];
	output->position[2] = a.position[2] + rotated[2];
	VR_FBT_ProfileQuaternionMultiply(a.orientation, b.orientation, output->orientation);
	return VR_FBT_ProfileVectorFinite(output->position, 3) &&
		VR_FBT_ProfileNormalize(output->orientation, 4);
}

int VR_FBT_ProfileApplyCorrection(const vr_fbt_profile_transform_t *world_device,
	const vr_fbt_profile_transform_t *device_to_anatomical,
	vr_fbt_profile_transform_t *world_anatomical)
{
	return VR_FBT_ProfileTransformCompose(world_device, device_to_anatomical,
		world_anatomical);
}

static int VR_FBT_ProfileNameIsSafe(const char *name)
{
	size_t i;
	if (!name || !name[0])
		return 0;
	for (i = 0; i < VR_FBT_PROFILE_NAME_MAX; ++i) {
		unsigned char c = (unsigned char)name[i];
		if (!c)
			return i > 0;
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') || c == '_' || c == '-'))
			return 0;
	}
	return 0;
}

static int VR_FBT_ProfileBoundedString(const char *text, size_t maximum)
{
	size_t length = 0;
	if (!text)
		return 0;
	while (length < maximum && text[length])
		++length;
	return length < maximum;
}

static int VR_FBT_ProfileValid(const vr_fbt_profile_t *profile)
{
	unsigned int role;
	if (!profile || !VR_FBT_ProfileNameIsSafe(profile->name) ||
		profile->schema_version != VR_FBT_PROFILE_SCHEMA_VERSION ||
		profile->calibration_algorithm != VR_FBT_PROFILE_CALIBRATION_ALGORITHM ||
		!VR_FBT_ProfileBoundedString(profile->avatar_fingerprint,
			sizeof(profile->avatar_fingerprint)) ||
		strncmp(profile->avatar_fingerprint, VR_FBT_PROFILE_AVATAR_FINGERPRINT,
			sizeof(profile->avatar_fingerprint)) ||
		!VR_FBT_ProfileBoundedString(profile->hmd_serial, VR_FBT_SERIAL_MAX) ||
		(profile->hmd_serial[0] && !VR_FBT_SerialIsSafe(profile->hmd_serial)) ||
		!VR_FBT_ProfileFinite(profile->hmd_height_metres) ||
		!VR_FBT_ProfileFinite(profile->floor_height_metres) ||
		fabs(profile->hmd_height_metres) > VR_FBT_PROFILE_METRES_LIMIT ||
		fabs(profile->floor_height_metres) > VR_FBT_PROFILE_METRES_LIMIT ||
		!VR_FBT_ProfileUnitWithinTolerance(profile->body_forward, 3))
		return 0;
	for (role = 0; role < VR_FBT_ROLE_COUNT; ++role)
		if (profile->roles[role].present &&
			(!VR_FBT_SerialIsSafe(profile->roles[role].serial) ||
			!VR_FBT_ProfileTransformSane(&profile->roles[role].device_to_anatomical)))
			return 0;
	for (role = 0; role < VR_FBT_ROLE_COUNT; ++role) {
		unsigned int other;
		if (!profile->roles[role].present) continue;
		for (other = role + 1; other < VR_FBT_ROLE_COUNT; ++other)
			if (profile->roles[other].present &&
				!strcmp(profile->roles[role].serial, profile->roles[other].serial))
				return 0;
	}
	return 1;
}

static int VR_FBT_ProfileParseInteger(const char *text, int64_t *value)
{
	uint64_t magnitude = 0;
	int negative = 0;
	if (!text || !*text || !value)
		return 0;
	if (*text == '-') {
		negative = 1;
		++text;
		if (!*text)
			return 0;
	}
	for (; *text; ++text) {
		unsigned char c = (unsigned char)*text;
		if (c < '0' || c > '9')
			return 0;
		if (magnitude > (UINT64_MAX - (uint64_t)(c - '0')) / 10)
			return 0;
		magnitude = magnitude * 10 + (uint64_t)(c - '0');
	}
	if ((!negative && magnitude > INT64_MAX) ||
		(negative && magnitude > (uint64_t)INT64_MAX + 1u))
		return 0;
	if (negative)
		*value = magnitude == (uint64_t)INT64_MAX + 1u ? INT64_MIN : -(int64_t)magnitude;
	else
		*value = (int64_t)magnitude;
	return 1;
}

static int VR_FBT_ProfileFixedMetres(const char *text, double *value)
{
	int64_t integer;
	if (!VR_FBT_ProfileParseInteger(text, &integer) || integer < -2000000 || integer > 2000000)
		return 0;
	*value = (double)integer / 1000000.0;
	return 1;
}

static int VR_FBT_ProfileFixedUnit(const char *text, double *value)
{
	int64_t integer;
	if (!VR_FBT_ProfileParseInteger(text, &integer) ||
		integer < -1100000000LL || integer > 1100000000LL)
		return 0;
	*value = (double)integer / 1073741824.0;
	return 1;
}

static int VR_FBT_ProfileRoleFromText(const char *text, vr_fbt_role_t *role)
{
	if (!strcmp(text, "hip")) *role = VR_FBT_ROLE_HIP;
	else if (!strcmp(text, "left_foot")) *role = VR_FBT_ROLE_LEFT_FOOT;
	else if (!strcmp(text, "right_foot")) *role = VR_FBT_ROLE_RIGHT_FOOT;
	else return 0;
	return 1;
}

static const char *VR_FBT_ProfileRoleText(vr_fbt_role_t role)
{
	if (role == VR_FBT_ROLE_HIP) return "hip";
	if (role == VR_FBT_ROLE_LEFT_FOOT) return "left_foot";
	return "right_foot";
}

static int VR_FBT_ProfileSplitLine(char *line, char **words, unsigned int capacity,
	unsigned int *count)
{
	unsigned int result = 0;
	char *cursor = line;
	if (!line || !*line || line[0] == ' ' || line[strlen(line) - 1] == ' ')
		return 0;
	while (*cursor) {
		if (result == capacity)
			return 0;
		words[result++] = cursor;
		while (*cursor && *cursor != ' ')
			if ((unsigned char)*cursor < 0x20 || (unsigned char)*cursor > 0x7e)
				return 0;
			else ++cursor;
		if (*cursor) {
			*cursor++ = 0;
			if (*cursor == ' ')
				return 0;
		}
	}
	*count = result;
	return 1;
}

int VR_FBT_ProfileParse(const char *text, size_t text_length,
	vr_fbt_profile_t *destination, vr_fbt_profile_error_t *error)
{
	vr_fbt_profile_t temporary;
	char buffer[VR_FBT_PROFILE_TEXT_MAX + 1];
	char *line;
	unsigned int lines = 0, required = 0, ended = 0;
	if (!text || !destination || text_length == 0 || text_length > VR_FBT_PROFILE_TEXT_MAX) {
		VR_FBT_ProfileSetError(error, VR_FBT_PROFILE_ERR_ARGUMENT);
		return 0;
	}
	if (memchr(text, 0, text_length)) goto format_error;
	memcpy(buffer, text, text_length);
	buffer[text_length] = 0;
	memset(&temporary, 0, sizeof(temporary));
	line = buffer;
	while (line && *line) {
		char *newline = strchr(line, '\n');
		char *next = newline;
		char *words[12];
		unsigned int count;
		if (newline) {
			if (newline > line && newline[-1] == '\r')
				newline[-1] = 0;
			*newline = 0;
			next = newline + 1;
		}
		if (++lines > VR_FBT_PROFILE_MAX_LINES || strlen(line) >= VR_FBT_PROFILE_LINE_MAX ||
			!VR_FBT_ProfileSplitLine(line, words, 12, &count)) goto format_error;
		if (ended) goto format_error;
		if (lines == 1) {
			if (count != 2 || strcmp(words[0], "VRFBT-PROFILE") || strcmp(words[1], "1")) goto format_error;
		} else if (!strcmp(words[0], "name")) {
			if (count != 2 || (required & 1u) || !VR_FBT_ProfileNameIsSafe(words[1])) goto format_error;
			strcpy(temporary.name, words[1]); required |= 1u;
		} else if (!strcmp(words[0], "algorithm")) {
			if (count != 2 || (required & 2u) || strcmp(words[1], "1")) goto format_error;
			temporary.calibration_algorithm = 1; required |= 2u;
		} else if (!strcmp(words[0], "avatar")) {
			if (count != 2 || (required & 4u) || strcmp(words[1], VR_FBT_PROFILE_AVATAR_FINGERPRINT)) goto format_error;
			strcpy(temporary.avatar_fingerprint, words[1]); required |= 4u;
		} else if (!strcmp(words[0], "hmd_serial")) {
			if (count != 2 || (required & 8u) || (strcmp(words[1], "-") && !VR_FBT_SerialIsSafe(words[1]))) goto format_error;
			if (strcmp(words[1], "-"))
				strcpy(temporary.hmd_serial, words[1]);
			required |= 8u;
		} else if (!strcmp(words[0], "hmd_height_um")) {
			if (count != 2 || (required & 16u) || !VR_FBT_ProfileFixedMetres(words[1], &temporary.hmd_height_metres)) goto format_error;
			required |= 16u;
		} else if (!strcmp(words[0], "floor_um")) {
			if (count != 2 || (required & 32u) || !VR_FBT_ProfileFixedMetres(words[1], &temporary.floor_height_metres)) goto format_error;
			required |= 32u;
		} else if (!strcmp(words[0], "body_forward_q30")) {
			if (count != 4 || (required & 64u) ||
				!VR_FBT_ProfileFixedUnit(words[1], &temporary.body_forward[0]) ||
				!VR_FBT_ProfileFixedUnit(words[2], &temporary.body_forward[1]) ||
				!VR_FBT_ProfileFixedUnit(words[3], &temporary.body_forward[2]) ||
				!VR_FBT_ProfileUnitWithinTolerance(temporary.body_forward, 3)) goto format_error;
			VR_FBT_ProfileNormalize(temporary.body_forward, 3); required |= 64u;
		} else if (!strcmp(words[0], "role")) {
			vr_fbt_role_t role;
			vr_fbt_profile_role_entry_t *entry;
			if (count != 10 || !VR_FBT_ProfileRoleFromText(words[1], &role) ||
				!VR_FBT_SerialIsSafe(words[2]) || temporary.roles[role].present) goto format_error;
			entry = &temporary.roles[role];
			if (!VR_FBT_ProfileFixedMetres(words[3], &entry->device_to_anatomical.position[0]) ||
				!VR_FBT_ProfileFixedMetres(words[4], &entry->device_to_anatomical.position[1]) ||
				!VR_FBT_ProfileFixedMetres(words[5], &entry->device_to_anatomical.position[2]) ||
				!VR_FBT_ProfileFixedUnit(words[6], &entry->device_to_anatomical.orientation[0]) ||
				!VR_FBT_ProfileFixedUnit(words[7], &entry->device_to_anatomical.orientation[1]) ||
				!VR_FBT_ProfileFixedUnit(words[8], &entry->device_to_anatomical.orientation[2]) ||
				!VR_FBT_ProfileFixedUnit(words[9], &entry->device_to_anatomical.orientation[3]) ||
				!VR_FBT_ProfileTransformSane(&entry->device_to_anatomical)) goto format_error;
			VR_FBT_ProfileNormalize(entry->device_to_anatomical.orientation, 4);
			strcpy(entry->serial, words[2]); entry->present = 1;
		} else if (!strcmp(words[0], "end")) {
			if (count != 1 || required != 127u) goto format_error;
			ended = 1;
		} else goto format_error;
		line = next;
	}
	temporary.schema_version = VR_FBT_PROFILE_SCHEMA_VERSION;
	if (!ended || !VR_FBT_ProfileValid(&temporary)) goto format_error;
	*destination = temporary;
	VR_FBT_ProfileSetError(error, VR_FBT_PROFILE_OK);
	return 1;
format_error:
	VR_FBT_ProfileSetError(error, VR_FBT_PROFILE_ERR_FORMAT);
	return 0;
}

static int VR_FBT_ProfileToFixed(double value, double scale, int64_t limit, int64_t *integer)
{
	double scaled;
	if (!VR_FBT_ProfileFinite(value) || !integer)
		return 0;
	scaled = value * scale;
	if (!VR_FBT_ProfileFinite(scaled) || scaled > (double)limit + 0.5 || scaled < -(double)limit - 0.5)
		return 0;
	*integer = (int64_t)(scaled >= 0.0 ? floor(scaled + 0.5) : ceil(scaled - 0.5));
	return 1;
}

static int VR_FBT_ProfileAppend(char **cursor, size_t *remaining, const char *format, ...)
{
	va_list arguments;
	int written;
	va_start(arguments, format);
	written = vsnprintf(*cursor, *remaining, format, arguments);
	va_end(arguments);
	if (written < 0 || (size_t)written >= *remaining)
		return 0;
	*cursor += written;
	*remaining -= (size_t)written;
	return 1;
}

int VR_FBT_ProfileSerialize(const vr_fbt_profile_t *profile, char *text,
	size_t text_capacity, size_t *text_length, vr_fbt_profile_error_t *error)
{
	vr_fbt_profile_t canonical;
	char encoded[VR_FBT_PROFILE_TEXT_MAX + 1];
	char normalized_name[VR_FBT_PROFILE_NAME_MAX];
	char *cursor;
	size_t remaining;
	int64_t height, floor_value, forward[3];
	unsigned int role;
	if (!profile || !text || !text_length || !text_capacity || !VR_FBT_ProfileValid(profile)) {
		VR_FBT_ProfileSetError(error, VR_FBT_PROFILE_ERR_ARGUMENT);
		return 0;
	}
	canonical = *profile;
	VR_FBT_ProfileNormalize(canonical.body_forward, 3);
	for (role = 0; role < VR_FBT_ROLE_COUNT; ++role)
		if (canonical.roles[role].present)
			VR_FBT_ProfileNormalize(canonical.roles[role].device_to_anatomical.orientation, 4);
	profile = &canonical;
	/* Normalization was already validated; output integers must represent it. */
	strcpy(normalized_name, profile->name);
	if (!VR_FBT_ProfileToFixed(profile->hmd_height_metres, 1000000.0, 2000000, &height) ||
		!VR_FBT_ProfileToFixed(profile->floor_height_metres, 1000000.0, 2000000, &floor_value) ||
		!VR_FBT_ProfileToFixed(profile->body_forward[0], 1073741824.0, 1100000000, &forward[0]) ||
		!VR_FBT_ProfileToFixed(profile->body_forward[1], 1073741824.0, 1100000000, &forward[1]) ||
		!VR_FBT_ProfileToFixed(profile->body_forward[2], 1073741824.0, 1100000000, &forward[2])) {
		VR_FBT_ProfileSetError(error, VR_FBT_PROFILE_ERR_RANGE);
		return 0;
	}
	cursor = encoded; remaining = sizeof(encoded);
	if (!VR_FBT_ProfileAppend(&cursor, &remaining, "VRFBT-PROFILE 1\nname %s\nalgorithm 1\navatar %s\nhmd_serial %s\nhmd_height_um %lld\nfloor_um %lld\nbody_forward_q30 %lld %lld %lld\n",
		normalized_name, VR_FBT_PROFILE_AVATAR_FINGERPRINT,
		profile->hmd_serial[0] ? profile->hmd_serial : "-",
		(long long)height, (long long)floor_value, (long long)forward[0],
		(long long)forward[1], (long long)forward[2])) goto capacity_error;
	for (role = 0; role < VR_FBT_ROLE_COUNT; ++role) {
		const vr_fbt_profile_transform_t *transform;
		int64_t position[3], orientation[4];
		unsigned int i;
		if (!profile->roles[role].present) continue;
		transform = &profile->roles[role].device_to_anatomical;
		for (i = 0; i < 3; ++i)
			if (!VR_FBT_ProfileToFixed(transform->position[i], 1000000.0, 2000000, &position[i])) goto range_error;
		for (i = 0; i < 4; ++i)
			if (!VR_FBT_ProfileToFixed(transform->orientation[i], 1073741824.0, 1100000000, &orientation[i])) goto range_error;
		if (!VR_FBT_ProfileAppend(&cursor, &remaining, "role %s %s %lld %lld %lld %lld %lld %lld %lld\n",
			VR_FBT_ProfileRoleText((vr_fbt_role_t)role), profile->roles[role].serial,
			(long long)position[0], (long long)position[1], (long long)position[2],
			(long long)orientation[0], (long long)orientation[1],
			(long long)orientation[2], (long long)orientation[3])) goto capacity_error;
	}
	if (!VR_FBT_ProfileAppend(&cursor, &remaining, "end\n")) goto capacity_error;
	if ((size_t)(cursor - encoded) + 1 > text_capacity) goto capacity_error;
	memcpy(text, encoded, (size_t)(cursor - encoded) + 1);
	*text_length = (size_t)(cursor - encoded);
	VR_FBT_ProfileSetError(error, VR_FBT_PROFILE_OK);
	return 1;
range_error:
	VR_FBT_ProfileSetError(error, VR_FBT_PROFILE_ERR_RANGE);
	return 0;
capacity_error:
	VR_FBT_ProfileSetError(error, VR_FBT_PROFILE_ERR_ARGUMENT);
	return 0;
}

static int VR_FBT_ProfileSnapshotNewer(uint64_t candidate, uint64_t reference)
{
	uint64_t distance = candidate - reference;
	return distance != 0 && distance < (UINT64_C(1) << 63);
}

int VR_FBT_ProfileCaptureBegin(vr_fbt_profile_capture_t *capture,
	unsigned int required_role_mask,
	const char *const expected_serials[VR_FBT_ROLE_COUNT])
{
	unsigned int role;
	if (!capture || !expected_serials || !required_role_mask ||
		(required_role_mask & ~((1u << VR_FBT_ROLE_COUNT) - 1u))) return 0;
	for (role = 0; role < VR_FBT_ROLE_COUNT; ++role)
		if ((required_role_mask & VR_FBT_PROFILE_ROLE_BIT(role)) &&
			!VR_FBT_SerialIsSafe(expected_serials[role])) return 0;
	for (role = 0; role < VR_FBT_ROLE_COUNT; ++role) {
		unsigned int other;
		if (!(required_role_mask & VR_FBT_PROFILE_ROLE_BIT(role))) continue;
		for (other = role + 1; other < VR_FBT_ROLE_COUNT; ++other)
			if ((required_role_mask & VR_FBT_PROFILE_ROLE_BIT(other)) &&
				!strcmp(expected_serials[role], expected_serials[other])) return 0;
	}
	memset(capture, 0, sizeof(*capture));
	capture->required_role_mask = required_role_mask;
	for (role = 0; role < VR_FBT_ROLE_COUNT; ++role)
		if (required_role_mask & VR_FBT_PROFILE_ROLE_BIT(role))
			strcpy(capture->expected_serials[role], expected_serials[role]);
	return 1;
}

int VR_FBT_ProfileCaptureAddSnapshot(vr_fbt_profile_capture_t *capture,
	uint64_t snapshot_id, double snapshot_time,
	const vr_fbt_profile_capture_sample_t samples[VR_FBT_ROLE_COUNT])
{
	unsigned int role;
	if (!capture || !samples || !capture->required_role_mask || !VR_FBT_ProfileFinite(snapshot_time)) return 0;
	if (capture->started && (!VR_FBT_ProfileSnapshotNewer(snapshot_id, capture->last_snapshot_id) ||
		snapshot_time < capture->last_snapshot_time)) {
		++capture->snapshot_rejected;
		return 0;
	}
	if (!capture->started) {
		capture->started = 1;
		capture->first_snapshot_id = snapshot_id;
		capture->first_snapshot_time = snapshot_time;
	}
	capture->last_snapshot_id = snapshot_id;
	capture->last_snapshot_time = snapshot_time;
	for (role = 0; role < VR_FBT_ROLE_COUNT; ++role) {
		const vr_fbt_profile_capture_sample_t *sample = &samples[role];
		vr_fbt_profile_transform_t inverse;
		if (!(capture->required_role_mask & VR_FBT_PROFILE_ROLE_BIT(role)) || !sample->present) continue;
		if (snapshot_time - capture->first_snapshot_time > VR_FBT_PROFILE_WINDOW_SECONDS ||
			capture->accepted[role] == VR_FBT_PROFILE_MAX_SAMPLES || !sample->connected ||
			!sample->pose_valid || !VR_FBT_ProfileBoundedString(sample->serial, VR_FBT_SERIAL_MAX) ||
			!VR_FBT_SerialIsSafe(sample->serial) || strcmp(sample->serial, capture->expected_serials[role]) ||
			!VR_FBT_ProfileTrackingTransformSane(&sample->raw_tracker_transform) ||
			!VR_FBT_ProfileTrackingTransformSane(&sample->reference_target_transform) ||
			!VR_FBT_ProfileVectorFinite(sample->linear_velocity_metres_per_second, 3) ||
			!VR_FBT_ProfileVectorFinite(sample->angular_velocity_radians_per_second, 3) ||
			sqrt(VR_FBT_ProfileDot(sample->linear_velocity_metres_per_second,
				sample->linear_velocity_metres_per_second, 3)) > VR_FBT_PROFILE_LINEAR_VELOCITY_LIMIT ||
			sqrt(VR_FBT_ProfileDot(sample->angular_velocity_radians_per_second,
				sample->angular_velocity_radians_per_second, 3)) > VR_FBT_PROFILE_ANGULAR_VELOCITY_LIMIT ||
			!VR_FBT_ProfileTransformInverse(&sample->raw_tracker_transform, &inverse) ||
			!VR_FBT_ProfileTransformCompose(&inverse, &sample->reference_target_transform,
				&capture->corrections[role][capture->accepted[role]]) ||
			!VR_FBT_ProfileTransformSane(&capture->corrections[role][capture->accepted[role]])) {
			++capture->rejected[role];
			continue;
		}
		++capture->accepted[role];
	}
	return 1;
}

void VR_FBT_ProfileCaptureGetProgress(const vr_fbt_profile_capture_t *capture,
	double now, vr_fbt_profile_capture_progress_t *progress)
{
	if (!progress) return;
	memset(progress, 0, sizeof(*progress));
	if (!capture) return;
	progress->required_role_mask = capture->required_role_mask;
	memcpy(progress->accepted, capture->accepted, sizeof(progress->accepted));
	memcpy(progress->rejected, capture->rejected, sizeof(progress->rejected));
	progress->snapshot_rejected = capture->snapshot_rejected;
	if (capture->started && VR_FBT_ProfileFinite(now) && now >= capture->first_snapshot_time)
		progress->elapsed_seconds = now - capture->first_snapshot_time;
}

static void VR_FBT_ProfileSort(double *values, unsigned int count)
{
	unsigned int i;
	for (i = 1; i < count; ++i) {
		double value = values[i];
		unsigned int j = i;
		while (j && values[j - 1] > value) { values[j] = values[j - 1]; --j; }
		values[j] = value;
	}
}

static double VR_FBT_ProfileMedian(const vr_fbt_profile_transform_t *values, unsigned int count, unsigned int component)
{
	double sorted[VR_FBT_PROFILE_MAX_SAMPLES];
	unsigned int i;
	for (i = 0; i < count; ++i) sorted[i] = values[i].position[component];
	VR_FBT_ProfileSort(sorted, count);
	return count & 1u ? sorted[count / 2] : (sorted[count / 2 - 1] + sorted[count / 2]) * 0.5;
}

static int VR_FBT_ProfileAggregate(const vr_fbt_profile_transform_t *values,
	unsigned int count, vr_fbt_profile_transform_t *result)
{
	double median[3], reference[4], initial[4] = {0, 0, 0, 0}, refined[4] = {0, 0, 0, 0};
	unsigned int joint = 0, best_support = 0, i, component;
	if (count < 30 || !result) return 0;
	memset(result, 0, sizeof(*result));
	for (component = 0; component < 3; ++component) median[component] = VR_FBT_ProfileMedian(values, count, component);
	/* A deterministic quaternion medoid prevents a near-50% opposite rotation
	 * cluster from pulling the preliminary mean away from the strict majority. */
	for (i = 0; i < count; ++i) {
		unsigned int candidate_support = 0, other;
		for (other = 0; other < count; ++other) {
			double dot = fabs(VR_FBT_ProfileDot(values[i].orientation, values[other].orientation, 4));
			double angle = 2.0 * acos(dot > 1.0 ? 1.0 : dot);
			if (angle <= VR_FBT_PROFILE_ANGLE_INLIER_RADIANS)
				++candidate_support;
		}
		if (candidate_support > best_support) {
			best_support = candidate_support;
			memcpy(reference, values[i].orientation, sizeof(reference));
		}
	}
	if (best_support <= count / 2) return 0;
	for (i = 0; i < count; ++i) {
		double dot = fabs(VR_FBT_ProfileDot(reference, values[i].orientation, 4));
		double angle = 2.0 * acos(dot > 1.0 ? 1.0 : dot);
		if (angle <= VR_FBT_PROFILE_ANGLE_INLIER_RADIANS) {
			double sign = VR_FBT_ProfileDot(reference, values[i].orientation, 4) < 0.0 ? -1.0 : 1.0;
			for (component = 0; component < 4; ++component) initial[component] += sign * values[i].orientation[component];
		}
	}
	if (!VR_FBT_ProfileNormalize(initial, 4)) return 0;
	for (i = 0; i < count; ++i) {
		double dx = values[i].position[0] - median[0];
		double dy = values[i].position[1] - median[1];
		double dz = values[i].position[2] - median[2];
		double dot = fabs(VR_FBT_ProfileDot(initial, values[i].orientation, 4));
		double angle = 2.0 * acos(dot > 1.0 ? 1.0 : dot);
		if (sqrt(dx * dx + dy * dy + dz * dz) <= VR_FBT_PROFILE_POSITION_INLIER_METRES &&
			angle <= VR_FBT_PROFILE_ANGLE_INLIER_RADIANS) {
			double sign = VR_FBT_ProfileDot(initial, values[i].orientation, 4) < 0.0 ? -1.0 : 1.0;
			for (component = 0; component < 3; ++component) result->position[component] += values[i].position[component];
			for (component = 0; component < 4; ++component) refined[component] += sign * values[i].orientation[component];
			++joint;
		}
	}
	if (joint <= count / 2 || !VR_FBT_ProfileNormalize(refined, 4)) return 0;
	for (component = 0; component < 3; ++component) result->position[component] /= (double)joint;
	memcpy(result->orientation, refined, sizeof(refined));
	return 1;
}

int VR_FBT_ProfileCaptureFinalize(const vr_fbt_profile_capture_t *capture,
	const vr_fbt_profile_capture_metadata_t *metadata,
	vr_fbt_profile_t *destination, vr_fbt_profile_error_t *error)
{
	vr_fbt_profile_t temporary;
	unsigned int role;
	if (!capture || !metadata || !destination || !capture->started ||
		!VR_FBT_ProfileNameIsSafe(metadata->name) ||
		!VR_FBT_ProfileBoundedString(metadata->hmd_serial, VR_FBT_SERIAL_MAX) ||
		(metadata->hmd_serial[0] && !VR_FBT_SerialIsSafe(metadata->hmd_serial)) ||
		!VR_FBT_ProfileFinite(metadata->hmd_height_metres) || !VR_FBT_ProfileFinite(metadata->floor_height_metres) ||
		fabs(metadata->hmd_height_metres) > VR_FBT_PROFILE_METRES_LIMIT || fabs(metadata->floor_height_metres) > VR_FBT_PROFILE_METRES_LIMIT ||
		!VR_FBT_ProfileUnitWithinTolerance(metadata->body_forward, 3)) {
		VR_FBT_ProfileSetError(error, VR_FBT_PROFILE_ERR_ARGUMENT);
		return 0;
	}
	if (capture->last_snapshot_time - capture->first_snapshot_time < VR_FBT_PROFILE_WINDOW_SECONDS) {
		VR_FBT_ProfileSetError(error, VR_FBT_PROFILE_ERR_INSUFFICIENT_SAMPLES);
		return 0;
	}
	for (role = 0; role < VR_FBT_ROLE_COUNT; ++role)
		if ((capture->required_role_mask & VR_FBT_PROFILE_ROLE_BIT(role)) && capture->accepted[role] < 30) {
			VR_FBT_ProfileSetError(error, VR_FBT_PROFILE_ERR_INSUFFICIENT_SAMPLES);
			return 0;
		}
	memset(&temporary, 0, sizeof(temporary));
	strcpy(temporary.name, metadata->name);
	temporary.schema_version = VR_FBT_PROFILE_SCHEMA_VERSION;
	temporary.calibration_algorithm = VR_FBT_PROFILE_CALIBRATION_ALGORITHM;
	strcpy(temporary.avatar_fingerprint, VR_FBT_PROFILE_AVATAR_FINGERPRINT);
	strcpy(temporary.hmd_serial, metadata->hmd_serial);
	temporary.hmd_height_metres = metadata->hmd_height_metres;
	temporary.floor_height_metres = metadata->floor_height_metres;
	memcpy(temporary.body_forward, metadata->body_forward, sizeof(temporary.body_forward));
	VR_FBT_ProfileNormalize(temporary.body_forward, 3);
	for (role = 0; role < VR_FBT_ROLE_COUNT; ++role) if (capture->required_role_mask & VR_FBT_PROFILE_ROLE_BIT(role)) {
		vr_fbt_profile_role_entry_t *entry = &temporary.roles[role];
		if (!VR_FBT_ProfileAggregate(capture->corrections[role], capture->accepted[role], &entry->device_to_anatomical)) {
			VR_FBT_ProfileSetError(error, VR_FBT_PROFILE_ERR_INSUFFICIENT_SAMPLES);
			return 0;
		}
		strcpy(entry->serial, capture->expected_serials[role]);
		entry->present = 1;
	}
	if (!VR_FBT_ProfileValid(&temporary)) {
		VR_FBT_ProfileSetError(error, VR_FBT_PROFILE_ERR_RANGE);
		return 0;
	}
	*destination = temporary;
	VR_FBT_ProfileSetError(error, VR_FBT_PROFILE_OK);
	return 1;
}
