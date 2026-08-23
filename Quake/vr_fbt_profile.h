#ifndef VR_FBT_PROFILE_H
#define VR_FBT_PROFILE_H

/*
 * Pure full-body-tracker calibration profile support.  This module deliberately
 * has no OpenVR, filesystem, cvar, renderer, or game-state dependency.
 *
 * Canonical v1 text grammar (one ASCII record per line; no blank lines):
 *   VRFBT-PROFILE 1
 *   name <[A-Za-z0-9_-]{1,32}>
 *   algorithm 1
 *   avatar ranger_verified_rerelease
 *   hmd_serial <safe-serial|->
 *   hmd_height_um <signed integer>
 *   floor_um <signed integer>
 *   body_forward_q30 <x> <y> <z>
 *   role <hip|left_foot|right_foot> <safe-serial> <px> <py> <pz> <qw> <qx> <qy> <qz>
 *   end
 * Integers are decimal fixed-point: metres are micrometres and unit-vector or
 * quaternion components are Q30.  Required non-role records occur exactly
 * once, roles occur zero or one time, and `end` is the last line.
 */

#include <stddef.h>
#include <stdint.h>

#include "vr_fbt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VR_FBT_PROFILE_SCHEMA_VERSION 1
#define VR_FBT_PROFILE_CALIBRATION_ALGORITHM 1
#define VR_FBT_PROFILE_AVATAR_FINGERPRINT "ranger_verified_rerelease"
#define VR_FBT_PROFILE_NAME_MAX 33
#define VR_FBT_PROFILE_TEXT_MAX 16384
#define VR_FBT_PROFILE_MAX_LINES 128
/* Longest v1 role record (63-byte serial plus signed fixed-point fields) fits. */
#define VR_FBT_PROFILE_LINE_MAX 192
#define VR_FBT_PROFILE_MAX_SAMPLES 240
#define VR_FBT_PROFILE_ROLE_BIT(role) (1u << (unsigned int)(role))

typedef struct {
	double position[3];
	double orientation[4]; /* normalized quaternion, w x y z */
} vr_fbt_profile_transform_t;

typedef struct {
	int present;
	char serial[VR_FBT_SERIAL_MAX];
	vr_fbt_profile_transform_t device_to_anatomical;
} vr_fbt_profile_role_entry_t;

typedef struct {
	char name[VR_FBT_PROFILE_NAME_MAX];
	unsigned int schema_version;
	unsigned int calibration_algorithm;
	char avatar_fingerprint[sizeof(VR_FBT_PROFILE_AVATAR_FINGERPRINT)];
	char hmd_serial[VR_FBT_SERIAL_MAX]; /* empty means no diagnostic serial */
	double hmd_height_metres;
	double floor_height_metres;
	double body_forward[3]; /* normalized, horizontal-vector policy is caller-owned */
	vr_fbt_profile_role_entry_t roles[VR_FBT_ROLE_COUNT];
} vr_fbt_profile_t;

typedef enum {
	VR_FBT_PROFILE_OK = 0,
	VR_FBT_PROFILE_ERR_ARGUMENT,
	VR_FBT_PROFILE_ERR_FORMAT,
	VR_FBT_PROFILE_ERR_RANGE,
	VR_FBT_PROFILE_ERR_IDENTITY,
	VR_FBT_PROFILE_ERR_SNAPSHOT,
	VR_FBT_PROFILE_ERR_INSUFFICIENT_SAMPLES
} vr_fbt_profile_error_t;

/* Math helpers reject non-finite values and near-zero quaternion norms. */
int VR_FBT_ProfileTransformInverse(const vr_fbt_profile_transform_t *input,
	vr_fbt_profile_transform_t *output);
int VR_FBT_ProfileTransformCompose(const vr_fbt_profile_transform_t *left,
	const vr_fbt_profile_transform_t *right, vr_fbt_profile_transform_t *output);
/* Applies a saved T_DA to a runtime T_WD: T_WA = T_WD * T_DA. */
int VR_FBT_ProfileApplyCorrection(const vr_fbt_profile_transform_t *world_device,
	const vr_fbt_profile_transform_t *device_to_anatomical,
	vr_fbt_profile_transform_t *world_anatomical);

/* Strict parse/serialize.  Parse never modifies destination on failure. */
int VR_FBT_ProfileParse(const char *text, size_t text_length,
	vr_fbt_profile_t *destination, vr_fbt_profile_error_t *error);
int VR_FBT_ProfileSerialize(const vr_fbt_profile_t *profile, char *text,
	size_t text_capacity, size_t *text_length, vr_fbt_profile_error_t *error);

typedef struct {
	int present;
	int connected;
	int pose_valid;
	char serial[VR_FBT_SERIAL_MAX];
	vr_fbt_profile_transform_t raw_tracker_transform;
	vr_fbt_profile_transform_t reference_target_transform;
	double linear_velocity_metres_per_second[3];
	double angular_velocity_radians_per_second[3];
} vr_fbt_profile_capture_sample_t;

typedef struct {
	char name[VR_FBT_PROFILE_NAME_MAX];
	char hmd_serial[VR_FBT_SERIAL_MAX];
	double hmd_height_metres;
	double floor_height_metres;
	double body_forward[3];
} vr_fbt_profile_capture_metadata_t;

typedef struct {
	unsigned int required_role_mask;
	char expected_serials[VR_FBT_ROLE_COUNT][VR_FBT_SERIAL_MAX];
	int started;
	uint64_t first_snapshot_id;
	double first_snapshot_time;
	uint64_t last_snapshot_id;
	double last_snapshot_time;
	unsigned int accepted[VR_FBT_ROLE_COUNT];
	unsigned int rejected[VR_FBT_ROLE_COUNT];
	unsigned int snapshot_rejected;
	vr_fbt_profile_transform_t corrections[VR_FBT_ROLE_COUNT][VR_FBT_PROFILE_MAX_SAMPLES];
} vr_fbt_profile_capture_t;

typedef struct {
	unsigned int required_role_mask;
	unsigned int accepted[VR_FBT_ROLE_COUNT];
	unsigned int rejected[VR_FBT_ROLE_COUNT];
	unsigned int snapshot_rejected;
	double elapsed_seconds;
} vr_fbt_profile_capture_progress_t;

/* expected_serials must contain safe serials for every required role. */
int VR_FBT_ProfileCaptureBegin(vr_fbt_profile_capture_t *capture,
	unsigned int required_role_mask,
	const char *const expected_serials[VR_FBT_ROLE_COUNT]);
/* Each accepted snapshot ID/time is consumed once; rejected snapshot metadata
 * increments snapshot_rejected and leaves collected samples unchanged. */
int VR_FBT_ProfileCaptureAddSnapshot(vr_fbt_profile_capture_t *capture,
	uint64_t snapshot_id, double snapshot_time,
	const vr_fbt_profile_capture_sample_t samples[VR_FBT_ROLE_COUNT]);
void VR_FBT_ProfileCaptureGetProgress(const vr_fbt_profile_capture_t *capture,
	double now, vr_fbt_profile_capture_progress_t *progress);
/* Finalization builds a temporary profile and leaves destination unchanged on
 * every failure, including insufficient samples or invalid metadata. */
int VR_FBT_ProfileCaptureFinalize(const vr_fbt_profile_capture_t *capture,
	const vr_fbt_profile_capture_metadata_t *metadata,
	vr_fbt_profile_t *destination, vr_fbt_profile_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* VR_FBT_PROFILE_H */
