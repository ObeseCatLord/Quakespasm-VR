#include "vr_fbt.h"

#include <math.h>
#include <string.h>

#define VR_FBT_NO_DEVICE_INDEX ((unsigned int)-1)

static int VR_FBT_RoleIsValid(vr_fbt_role_t role)
{
	return role >= VR_FBT_ROLE_HIP && role < VR_FBT_ROLE_COUNT;
}

static int VR_FBT_SnapshotIsNewer(uint64_t candidate, uint64_t reference)
{
	uint64_t distance = candidate - reference;

	return distance && distance < (UINT64_C(1) << 63);
}

static size_t VR_FBT_BoundedLength(const char *text, size_t maximum)
{
	size_t length = 0;

	if (!text)
		return 0;
	while (length < maximum && text[length])
		++length;
	return length;
}

int VR_FBT_SerialIsSafe(const char *serial)
{
	size_t i;
	size_t length = VR_FBT_BoundedLength(serial, VR_FBT_SERIAL_MAX);

	if (!length || length == VR_FBT_SERIAL_MAX)
		return 0;
	for (i = 0; i < length; ++i) {
		unsigned char c = (unsigned char)serial[i];
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') || c == '.' || c == '_' || c == ':' ||
			c == '-'))
			return 0;
	}
	return 1;
}

static void VR_FBT_CopySafeSerial(char *destination, const char *serial)
{
	if (VR_FBT_SerialIsSafe(serial)) {
		strncpy(destination, serial, VR_FBT_SERIAL_MAX - 1);
		destination[VR_FBT_SERIAL_MAX - 1] = 0;
	} else {
		destination[0] = 0;
	}
}

static void VR_FBT_ClearRole(vr_fbt_role_status_t *role)
{
	double last_reconciled_time = role->last_reconciled_time;

	memset(role, 0, sizeof(*role));
	role->state = VR_FBT_STATE_UNASSIGNED;
	role->identity_kind = VR_FBT_IDENTITY_NONE;
	role->device_index = VR_FBT_NO_DEVICE_INDEX;
	role->last_reconciled_time = last_reconciled_time;
}

void VR_FBT_Init(vr_fbt_manager_t *manager)
{
	int role;

	if (!manager)
		return;
	memset(manager, 0, sizeof(*manager));
	for (role = 0; role < VR_FBT_ROLE_COUNT; ++role) {
		manager->roles[role].state = VR_FBT_STATE_UNASSIGNED;
		manager->roles[role].device_index = VR_FBT_NO_DEVICE_INDEX;
	}
}

static int VR_FBT_CandidatesSameSerial(const vr_fbt_cached_candidate_t *a,
	const vr_fbt_cached_candidate_t *b)
{
	return a->status.has_safe_serial && b->status.has_safe_serial &&
		!strcmp(a->status.serial, b->status.serial);
}

static int VR_FBT_CandidatesSameEphemeral(const vr_fbt_cached_candidate_t *a,
	const vr_fbt_cached_candidate_t *b)
{
	return !a->status.has_safe_serial && !b->status.has_safe_serial &&
		a->status.ephemeral_identity &&
		a->status.ephemeral_identity == b->status.ephemeral_identity;
}

static void VR_FBT_RefreshCandidateAmbiguity(vr_fbt_manager_t *manager)
{
	unsigned int i, j;

	for (i = 0; i < manager->candidate_count; ++i) {
		for (j = i + 1; j < manager->candidate_count; ++j) {
			if (VR_FBT_CandidatesSameSerial(&manager->candidates[i],
				&manager->candidates[j])) {
				manager->candidates[i].status.serial_ambiguous = 1;
				manager->candidates[j].status.serial_ambiguous = 1;
			}
			if (VR_FBT_CandidatesSameEphemeral(&manager->candidates[i],
				&manager->candidates[j])) {
				manager->candidates[i].status.ephemeral_ambiguous = 1;
				manager->candidates[j].status.ephemeral_ambiguous = 1;
			}
		}
	}
}

static int VR_FBT_CandidateMatchesRole(const vr_fbt_cached_candidate_t *candidate,
	const vr_fbt_role_status_t *role)
{
	if (role->identity_kind == VR_FBT_IDENTITY_SERIAL)
		return candidate->status.has_safe_serial &&
			!candidate->status.serial_ambiguous &&
			!strcmp(candidate->status.serial, role->serial);
	if (role->identity_kind == VR_FBT_IDENTITY_EPHEMERAL)
		return !candidate->status.has_safe_serial &&
			!candidate->status.ephemeral_ambiguous &&
			candidate->status.ephemeral_identity &&
			candidate->status.ephemeral_identity == role->ephemeral_identity;
	return 0;
}

static int VR_FBT_FindRoleCandidate(const vr_fbt_manager_t *manager,
	const vr_fbt_role_status_t *role)
{
	unsigned int i;

	for (i = 0; i < manager->candidate_count; ++i)
		if (VR_FBT_CandidateMatchesRole(&manager->candidates[i], role))
			return (int)i;
	return -1;
}

static int VR_FBT_HasPredictHistory(const vr_fbt_role_status_t *role,
	double snapshot_time)
{
	return role->has_tracking_history && snapshot_time >= role->last_tracking_time &&
		snapshot_time - role->last_tracking_time <= VR_FBT_PREDICT_WINDOW_SECONDS;
}

static void VR_FBT_ClearObservedRoleMetadata(vr_fbt_role_status_t *role)
{
	role->device_index = VR_FBT_NO_DEVICE_INDEX;
	role->tracking_result = 0;
	memset(role->device_to_absolute_tracking, 0,
		sizeof(role->device_to_absolute_tracking));
	memset(role->velocity, 0, sizeof(role->velocity));
	memset(role->angular_velocity, 0, sizeof(role->angular_velocity));
}

static void VR_FBT_ApplyCandidate(vr_fbt_role_status_t *role,
	const vr_fbt_candidate_t *candidate, double snapshot_time)
{
	role->device_index = candidate->device_index;
	role->connected = candidate->connected != 0;
	role->pose_valid = candidate->pose_valid != 0;
	role->tracking_result = candidate->tracking_result;
	role->last_seen_time = snapshot_time;
	role->last_reconciled_time = snapshot_time;
	memcpy(role->device_to_absolute_tracking,
		candidate->device_to_absolute_tracking,
		sizeof(role->device_to_absolute_tracking));
	memcpy(role->velocity, candidate->velocity, sizeof(role->velocity));
	memcpy(role->angular_velocity, candidate->angular_velocity,
		sizeof(role->angular_velocity));

	if (!role->connected) {
		role->state = VR_FBT_HasPredictHistory(role, snapshot_time) ?
			VR_FBT_STATE_PREDICTING : VR_FBT_STATE_LOST;
		VR_FBT_ClearObservedRoleMetadata(role);
		return;
	}
	role->last_connected_time = snapshot_time;
	if (!role->pose_valid) {
		role->state = VR_FBT_HasPredictHistory(role, snapshot_time) ?
			VR_FBT_STATE_PREDICTING : VR_FBT_STATE_CONNECTED_INVALID;
		return;
	}
	role->last_pose_valid_time = snapshot_time;
	if (role->tracking_result == VR_FBT_TRACKING_RESULT_RUNNING_OK) {
		role->state = VR_FBT_STATE_TRACKING;
		role->last_tracking_time = snapshot_time;
		role->has_tracking_history = 1;
	} else {
		/* A valid but non-running pose is retained as a prediction candidate. */
		role->state = VR_FBT_STATE_PREDICTING;
	}
}

static void VR_FBT_MarkRoleLost(vr_fbt_role_status_t *role,
	double snapshot_time)
{
	role->state = VR_FBT_HasPredictHistory(role, snapshot_time) ?
		VR_FBT_STATE_PREDICTING : VR_FBT_STATE_LOST;
	role->connected = 0;
	role->pose_valid = 0;
	/* A serial binding survives an offline period, but its old OpenVR device
	 * index, pose and velocities must not be mistaken for live metadata. */
	VR_FBT_ClearObservedRoleMetadata(role);
	role->last_reconciled_time = snapshot_time;
}

int VR_FBT_Reconcile(vr_fbt_manager_t *manager, uint64_t snapshot_id,
	double snapshot_time, const vr_fbt_candidate_t *candidates,
	unsigned int candidate_count)
{
	unsigned int i;
	unsigned int j;
	int role;

	if (!manager || !isfinite(snapshot_time) ||
		candidate_count > VR_FBT_MAX_CANDIDATES ||
		(candidate_count && !candidates))
		return 0;
	if (manager->has_snapshot &&
		(!VR_FBT_SnapshotIsNewer(snapshot_id, manager->last_snapshot_id) ||
			snapshot_time < manager->last_snapshot_time))
		return 0;
	for (i = 0; i < candidate_count; ++i) {
		if (candidates[i].snapshot_id != snapshot_id ||
			candidates[i].snapshot_time != snapshot_time)
			return 0;
		for (j = 0; j < i; ++j)
			if (candidates[i].device_index == candidates[j].device_index)
				return 0;
	}

	manager->candidate_count = candidate_count;
	for (i = 0; i < candidate_count; ++i) {
		vr_fbt_cached_candidate_t *cached = &manager->candidates[i];
		memset(cached, 0, sizeof(*cached));
		memcpy(&cached->candidate, &candidates[i], sizeof(cached->candidate));
		cached->status.device_index = candidates[i].device_index;
		cached->status.connected = candidates[i].connected != 0;
		cached->status.pose_valid = candidates[i].pose_valid != 0;
		cached->status.tracking_result = candidates[i].tracking_result;
		cached->status.snapshot_id = snapshot_id;
		cached->status.snapshot_time = snapshot_time;
		cached->status.has_safe_serial = VR_FBT_SerialIsSafe(candidates[i].serial);
		cached->status.ephemeral_identity = candidates[i].ephemeral_identity;
		VR_FBT_CopySafeSerial(cached->status.serial, candidates[i].serial);
	}
	VR_FBT_RefreshCandidateAmbiguity(manager);
	manager->last_snapshot_id = snapshot_id;
	manager->last_snapshot_time = snapshot_time;
	manager->has_snapshot = 1;

	for (role = 0; role < VR_FBT_ROLE_COUNT; ++role) {
		vr_fbt_role_status_t *assigned = &manager->roles[role];
		int candidate_index;

		if (assigned->identity_kind == VR_FBT_IDENTITY_NONE) {
			assigned->last_reconciled_time = snapshot_time;
			continue;
		}
		candidate_index = VR_FBT_FindRoleCandidate(manager, assigned);
		if (candidate_index < 0) {
			if (assigned->identity_kind == VR_FBT_IDENTITY_EPHEMERAL)
				VR_FBT_ClearRole(assigned);
			else
				VR_FBT_MarkRoleLost(assigned, snapshot_time);
			continue;
		}
		VR_FBT_ApplyCandidate(assigned,
			&manager->candidates[candidate_index].candidate, snapshot_time);
		if (assigned->identity_kind == VR_FBT_IDENTITY_EPHEMERAL &&
			!assigned->connected)
			VR_FBT_ClearRole(assigned);
	}
	return 1;
}

unsigned int VR_FBT_GetCandidateCount(const vr_fbt_manager_t *manager)
{
	return manager ? manager->candidate_count : 0;
}

int VR_FBT_GetCandidate(const vr_fbt_manager_t *manager, unsigned int index,
	vr_fbt_candidate_status_t *status)
{
	if (!manager || !status || index >= manager->candidate_count)
		return 0;
	memcpy(status, &manager->candidates[index].status, sizeof(*status));
	return 1;
}

int VR_FBT_GetRoleStatus(const vr_fbt_manager_t *manager, vr_fbt_role_t role,
	vr_fbt_role_status_t *status)
{
	if (!manager || !status || !VR_FBT_RoleIsValid(role))
		return 0;
	memcpy(status, &manager->roles[role], sizeof(*status));
	return 1;
}

int VR_FBT_GetRoleTiming(const vr_fbt_manager_t *manager, vr_fbt_role_t role,
	double now, vr_fbt_timing_t *timing)
{
	const vr_fbt_role_status_t *status;

	if (!manager || !timing || !isfinite(now) || !VR_FBT_RoleIsValid(role))
		return 0;
	status = &manager->roles[role];
	memset(timing, 0, sizeof(*timing));
	timing->last_tracking_time = status->last_tracking_time;
	if (!status->has_tracking_history || now < status->last_tracking_time)
		return 1;
	timing->tracking_age = now - status->last_tracking_time;
	timing->within_predict_window =
		timing->tracking_age <= VR_FBT_PREDICT_WINDOW_SECONDS;
	timing->within_loss_window =
		timing->tracking_age > VR_FBT_PREDICT_WINDOW_SECONDS &&
		timing->tracking_age <= VR_FBT_LOSS_WINDOW_SECONDS;
	return 1;
}

static int VR_FBT_IdentityAssignedElsewhere(const vr_fbt_manager_t *manager,
	vr_fbt_role_t requested_role, const vr_fbt_cached_candidate_t *candidate)
{
	int role;

	for (role = 0; role < VR_FBT_ROLE_COUNT; ++role) {
		const vr_fbt_role_status_t *assigned = &manager->roles[role];
		if ((vr_fbt_role_t)role == requested_role)
			continue;
		if (assigned->identity_kind == VR_FBT_IDENTITY_SERIAL &&
			candidate->status.has_safe_serial &&
			!strcmp(assigned->serial, candidate->status.serial))
			return 1;
		if (assigned->identity_kind == VR_FBT_IDENTITY_EPHEMERAL &&
			!candidate->status.has_safe_serial &&
			assigned->ephemeral_identity == candidate->status.ephemeral_identity)
			return 1;
	}
	return 0;
}

static int VR_FBT_SerialAssignedElsewhere(const vr_fbt_manager_t *manager,
	vr_fbt_role_t requested_role, const char *serial)
{
	int role;

	for (role = 0; role < VR_FBT_ROLE_COUNT; ++role) {
		const vr_fbt_role_status_t *assigned = &manager->roles[role];
		if ((vr_fbt_role_t)role != requested_role &&
			assigned->identity_kind == VR_FBT_IDENTITY_SERIAL &&
			!strcmp(assigned->serial, serial))
			return 1;
	}
	return 0;
}

int VR_FBT_AssignCandidate(vr_fbt_manager_t *manager, vr_fbt_role_t role,
	unsigned int candidate_index)
{
	vr_fbt_role_status_t *assigned;
	const vr_fbt_cached_candidate_t *candidate;

	if (!manager || !VR_FBT_RoleIsValid(role) ||
		candidate_index >= manager->candidate_count)
		return 0;
	candidate = &manager->candidates[candidate_index];
	if ((candidate->status.has_safe_serial && candidate->status.serial_ambiguous) ||
		(!candidate->status.has_safe_serial &&
			(!candidate->status.ephemeral_identity ||
			candidate->status.ephemeral_ambiguous)))
		return 0;
	if (VR_FBT_IdentityAssignedElsewhere(manager, role, candidate))
		return 0;

	assigned = &manager->roles[role];
	VR_FBT_ClearRole(assigned);
	if (candidate->status.has_safe_serial) {
		assigned->identity_kind = VR_FBT_IDENTITY_SERIAL;
		VR_FBT_CopySafeSerial(assigned->serial, candidate->status.serial);
	} else {
		assigned->identity_kind = VR_FBT_IDENTITY_EPHEMERAL;
		assigned->ephemeral_identity = candidate->status.ephemeral_identity;
	}
	VR_FBT_ApplyCandidate(assigned, &candidate->candidate,
		manager->last_snapshot_time);
	if (assigned->identity_kind == VR_FBT_IDENTITY_EPHEMERAL &&
		!assigned->connected)
		VR_FBT_ClearRole(assigned);
	return 1;
}

int VR_FBT_BindSerial(vr_fbt_manager_t *manager, vr_fbt_role_t role,
	const char *serial)
{
	vr_fbt_role_status_t *assigned;

	if (!manager || !VR_FBT_RoleIsValid(role) || !VR_FBT_SerialIsSafe(serial) ||
		VR_FBT_SerialAssignedElsewhere(manager, role, serial))
		return 0;
	assigned = &manager->roles[role];
	VR_FBT_ClearRole(assigned);
	assigned->identity_kind = VR_FBT_IDENTITY_SERIAL;
	assigned->state = VR_FBT_STATE_LOST;
	VR_FBT_CopySafeSerial(assigned->serial, serial);
	assigned->last_reconciled_time = manager->has_snapshot ?
		manager->last_snapshot_time : 0.0;
	return 1;
}

int VR_FBT_UnassignRole(vr_fbt_manager_t *manager, vr_fbt_role_t role)
{
	if (!manager || !VR_FBT_RoleIsValid(role))
		return 0;
	VR_FBT_ClearRole(&manager->roles[role]);
	return 1;
}
