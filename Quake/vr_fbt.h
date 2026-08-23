#ifndef VR_FBT_H
#define VR_FBT_H

/*
 * Hardware-independent full-body-tracker role bookkeeping.  This module owns
 * no OpenVR objects and intentionally performs no calibration, IK, rendering,
 * cvar persistence, or network work.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VR_FBT_MAX_CANDIDATES 64
#define VR_FBT_SERIAL_MAX 64
#define VR_FBT_PREDICT_WINDOW_SECONDS 0.050
#define VR_FBT_LOSS_WINDOW_SECONDS 0.250
#define VR_FBT_TRACKING_RESULT_RUNNING_OK 200

/*
 * Snapshot IDs are compared modulo uint64_t.  A nonzero forward distance less
 * than 2^63 is newer; a half-range distance is intentionally ambiguous and is
 * rejected.  A producer must not advance more than 2^63 snapshots between
 * reconciliations.
 */

typedef enum {
	VR_FBT_ROLE_HIP = 0,
	VR_FBT_ROLE_LEFT_FOOT,
	VR_FBT_ROLE_RIGHT_FOOT,
	VR_FBT_ROLE_COUNT
} vr_fbt_role_t;

typedef enum {
	VR_FBT_STATE_UNASSIGNED = 0,
	VR_FBT_STATE_CONNECTED_INVALID,
	VR_FBT_STATE_TRACKING,
	VR_FBT_STATE_PREDICTING,
	VR_FBT_STATE_LOST
} vr_fbt_state_t;

typedef enum {
	VR_FBT_IDENTITY_NONE = 0,
	VR_FBT_IDENTITY_SERIAL,
	VR_FBT_IDENTITY_EPHEMERAL
} vr_fbt_identity_kind_t;

/*
 * A copied pose candidate from one immutable pose snapshot.  serial must be
 * NUL-terminated within VR_FBT_SERIAL_MAX and use [A-Za-z0-9._:-]+ to be
 * considered stable.  ephemeral_identity
 * is intentionally opaque and has only session lifetime semantics.
 */
typedef struct {
	unsigned int device_index;
	char serial[VR_FBT_SERIAL_MAX];
	uint64_t ephemeral_identity;
	int connected;
	int pose_valid;
	int tracking_result;
	uint64_t snapshot_id;
	double snapshot_time;
	float device_to_absolute_tracking[3][4];
	float velocity[3];
	float angular_velocity[3];
} vr_fbt_candidate_t;

typedef struct {
	unsigned int device_index;
	int connected;
	int pose_valid;
	int tracking_result;
	uint64_t snapshot_id;
	double snapshot_time;
	int has_safe_serial;
	int serial_ambiguous;
	int ephemeral_ambiguous;
	uint64_t ephemeral_identity;
	char serial[VR_FBT_SERIAL_MAX];
} vr_fbt_candidate_status_t;

typedef struct {
	vr_fbt_state_t state;
	vr_fbt_identity_kind_t identity_kind;
	unsigned int device_index;
	char serial[VR_FBT_SERIAL_MAX];
	uint64_t ephemeral_identity;
	int connected;
	int pose_valid;
	int tracking_result;
	double last_reconciled_time;
	double last_seen_time;
	double last_connected_time;
	double last_pose_valid_time;
	double last_tracking_time;
	int has_tracking_history;
	float device_to_absolute_tracking[3][4];
	float velocity[3];
	float angular_velocity[3];
} vr_fbt_role_status_t;

typedef struct {
	double last_tracking_time;
	double tracking_age;
	int within_predict_window;
	int within_loss_window;
} vr_fbt_timing_t;

typedef struct {
	vr_fbt_candidate_t candidate;
	vr_fbt_candidate_status_t status;
} vr_fbt_cached_candidate_t;

typedef struct {
	vr_fbt_role_status_t roles[VR_FBT_ROLE_COUNT];
	vr_fbt_cached_candidate_t candidates[VR_FBT_MAX_CANDIDATES];
	unsigned int candidate_count;
	uint64_t last_snapshot_id;
	double last_snapshot_time;
	int has_snapshot;
} vr_fbt_manager_t;

void VR_FBT_Init(vr_fbt_manager_t *manager);
int VR_FBT_SerialIsSafe(const char *serial);

/*
 * Reconciles a whole snapshot.  Candidate IDs/times must agree with the
 * supplied snapshot metadata.  Snapshot times must be finite and nondecreasing
 * and device indexes must be unique.  Returns nonzero only when a newer
 * modular snapshot ID was accepted; rejected and stale snapshots leave manager
 * untouched.
 */
int VR_FBT_Reconcile(vr_fbt_manager_t *manager, uint64_t snapshot_id,
	double snapshot_time, const vr_fbt_candidate_t *candidates,
	unsigned int candidate_count);

unsigned int VR_FBT_GetCandidateCount(const vr_fbt_manager_t *manager);
int VR_FBT_GetCandidate(const vr_fbt_manager_t *manager, unsigned int index,
	vr_fbt_candidate_status_t *status);
int VR_FBT_GetRoleStatus(const vr_fbt_manager_t *manager, vr_fbt_role_t role,
	vr_fbt_role_status_t *status);
int VR_FBT_GetRoleTiming(const vr_fbt_manager_t *manager, vr_fbt_role_t role,
	double now, vr_fbt_timing_t *timing);

/* Assignment is explicit: tracker count and ordering never infer a role. */
int VR_FBT_AssignCandidate(vr_fbt_manager_t *manager, vr_fbt_role_t role,
	unsigned int candidate_index);
/* Binds a profile-saved safe serial without requiring a current device. */
int VR_FBT_BindSerial(vr_fbt_manager_t *manager, vr_fbt_role_t role,
	const char *serial);
int VR_FBT_UnassignRole(vr_fbt_manager_t *manager, vr_fbt_role_t role);

#ifdef __cplusplus
}
#endif

#endif /* VR_FBT_H */
