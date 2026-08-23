#ifndef VR_FBT_FILTER_H
#define VR_FBT_FILTER_H

/*
 * Small, deterministic FBT target filter.  It deliberately knows nothing
 * about OpenVR, rendering, calibration, or the wire protocol.  Callers map
 * their local or remote target role to vr_fbt_filter_role_t and give every
 * role the same monotonically-timed snapshots.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VR_FBT_FILTER_PREDICT_SECONDS 0.050
#define VR_FBT_FILTER_HOLD_SECONDS 0.250

typedef enum {
	VR_FBT_FILTER_ROLE_HIP = 0,
	VR_FBT_FILTER_ROLE_LEFT_FOOT,
	VR_FBT_FILTER_ROLE_RIGHT_FOOT,
	VR_FBT_FILTER_ROLE_COUNT
} vr_fbt_filter_role_t;

typedef enum {
	VR_FBT_FILTER_STATE_LOST = 0,
	VR_FBT_FILTER_STATE_TRACKING,
	VR_FBT_FILTER_STATE_PREDICTING,
	VR_FBT_FILTER_STATE_HOLDING
} vr_fbt_filter_state_t;

/* Quaternion order is w, x, y, z.  Positions, velocities and orientations
 * are in one root-local frame.  root_yaw_degrees names that frame's world
 * yaw so retained state can be rebased before smoothing the next input.
 * The floor plane is z = floor_height. */
typedef struct {
	uint64_t snapshot_id;
	double snapshot_time;
	uint64_t identity;
	int identity_valid;
	int connected;
	int tracking_valid;
	float position[3];
	float orientation[4];
	float linear_velocity[3];
	float angular_velocity[3];
	int floor_valid;
	float floor_height;
	float root_yaw_degrees;
	int root_yaw_valid;
} vr_fbt_filter_input_t;

typedef struct {
	float position[3];
	float orientation[4];
	float confidence;
	/* Set only for a fresh, valid hardware sample.  PREDICTING and HOLDING are
	 * usable finite payloads but deliberately report tracked = 0. */
	int tracked;
	int planted;
	vr_fbt_filter_state_t state;
} vr_fbt_filter_output_t;

typedef struct {
	uint64_t last_snapshot_id;
	double last_snapshot_time;
	uint64_t identity;
	double last_valid_time;
	double plant_candidate_time;
	float last_valid_position[3];
	float last_valid_orientation[4];
	float last_valid_linear_velocity[3];
	float last_valid_angular_velocity[3];
	float planted_position[3];
	vr_fbt_filter_output_t output;
	int has_snapshot;
	int has_identity;
	int has_valid;
	int reset_on_valid;
	int planted;
	float root_yaw_degrees;
	int has_root_yaw;
} vr_fbt_filter_role_state_t;

typedef struct {
	vr_fbt_filter_role_state_t roles[VR_FBT_FILTER_ROLE_COUNT];
} vr_fbt_filter_t;

void VR_FBT_FilterInit(vr_fbt_filter_t *filter);
void VR_FBT_FilterResetRole(vr_fbt_filter_t *filter,
	vr_fbt_filter_role_t role);

/*
 * Returns nonzero when this role accepted a newer modular snapshot ID.  A
 * rejected/nonfinite/regressing input never changes state; output, when
 * supplied, receives the preceding output.
 */
int VR_FBT_FilterUpdate(vr_fbt_filter_t *filter, vr_fbt_filter_role_t role,
	const vr_fbt_filter_input_t *input, vr_fbt_filter_output_t *output);

int VR_FBT_FilterGetOutput(const vr_fbt_filter_t *filter,
	vr_fbt_filter_role_t role, vr_fbt_filter_output_t *output);

/* Correct a calibrated anatomical point's tracking-space velocity according
 * to v_point = v_device + omega_device x device_to_point. */
int VR_FBT_FilterCorrectedPointVelocity(float output[3],
	const float linear_velocity[3], const float angular_velocity[3],
	const float device_to_point[3]);

#ifdef __cplusplus
}
#endif

#endif /* VR_FBT_FILTER_H */
