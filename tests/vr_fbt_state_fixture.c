#include "vr_fbt.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\\n", __FILE__, __LINE__, #expression); \
		++failures; \
	} \
} while (0)

static vr_fbt_candidate_t Candidate(unsigned int index, const char *serial,
	uint64_t ephemeral, int connected, int pose_valid, int tracking_result,
	uint64_t snapshot_id, double snapshot_time)
{
	vr_fbt_candidate_t candidate;

	memset(&candidate, 0, sizeof(candidate));
	candidate.device_index = index;
	if (serial) {
		strncpy(candidate.serial, serial, sizeof(candidate.serial) - 1);
		candidate.serial[sizeof(candidate.serial) - 1] = 0;
	}
	candidate.ephemeral_identity = ephemeral;
	candidate.connected = connected;
	candidate.pose_valid = pose_valid;
	candidate.tracking_result = tracking_result;
	candidate.snapshot_id = snapshot_id;
	candidate.snapshot_time = snapshot_time;
	candidate.device_to_absolute_tracking[0][3] = (float)index;
	return candidate;
}

static void SetSnapshot(vr_fbt_candidate_t *candidates, unsigned int count,
	uint64_t snapshot_id, double snapshot_time)
{
	unsigned int i;

	for (i = 0; i < count; ++i) {
		candidates[i].snapshot_id = snapshot_id;
		candidates[i].snapshot_time = snapshot_time;
	}
}

static vr_fbt_role_status_t Role(const vr_fbt_manager_t *manager,
	vr_fbt_role_t role)
{
	vr_fbt_role_status_t status;
	CHECK(VR_FBT_GetRoleStatus(manager, role, &status));
	return status;
}

static void TestCandidateCountsAndExplicitRoles(void)
{
	vr_fbt_manager_t manager;
	vr_fbt_candidate_t candidates[3];

	VR_FBT_Init(&manager);
	CHECK(VR_FBT_Reconcile(&manager, 1, 1.0, NULL, 0));
	CHECK(VR_FBT_GetCandidateCount(&manager) == 0);
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).state == VR_FBT_STATE_UNASSIGNED);
	candidates[0] = Candidate(3, "hip", 0, 1, 1, 200, 2, 2.0);
	CHECK(VR_FBT_Reconcile(&manager, 2, 2.0, candidates, 1));
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).state == VR_FBT_STATE_UNASSIGNED);
	CHECK(VR_FBT_AssignCandidate(&manager, VR_FBT_ROLE_HIP, 0));
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).state == VR_FBT_STATE_TRACKING);
	candidates[1] = Candidate(4, "left", 0, 1, 1, 200, 3, 3.0);
	SetSnapshot(candidates, 2, 3, 3.0);
	CHECK(VR_FBT_Reconcile(&manager, 3, 3.0, candidates, 2));
	CHECK(VR_FBT_AssignCandidate(&manager, VR_FBT_ROLE_LEFT_FOOT, 1));
	candidates[2] = Candidate(5, "right", 0, 1, 1, 200, 4, 4.0);
	SetSnapshot(candidates, 3, 4, 4.0);
	CHECK(VR_FBT_Reconcile(&manager, 4, 4.0, candidates, 3));
	CHECK(VR_FBT_AssignCandidate(&manager, VR_FBT_ROLE_RIGHT_FOOT, 2));
	CHECK(Role(&manager, VR_FBT_ROLE_LEFT_FOOT).state == VR_FBT_STATE_TRACKING);
	CHECK(Role(&manager, VR_FBT_ROLE_RIGHT_FOOT).state == VR_FBT_STATE_TRACKING);
}

static void TestSerialRebindAndIndependentState(void)
{
	vr_fbt_manager_t manager;
	vr_fbt_candidate_t candidates[2];

	VR_FBT_Init(&manager);
	candidates[0] = Candidate(2, "hip", 0, 1, 1, 200, 1, 1.0);
	candidates[1] = Candidate(7, "left", 0, 1, 1, 200, 1, 1.0);
	CHECK(VR_FBT_Reconcile(&manager, 1, 1.0, candidates, 2));
	CHECK(VR_FBT_AssignCandidate(&manager, VR_FBT_ROLE_HIP, 0));
	CHECK(VR_FBT_AssignCandidate(&manager, VR_FBT_ROLE_LEFT_FOOT, 1));
	candidates[0] = Candidate(11, "left", 0, 1, 0, 200, 2, 2.0);
	candidates[1] = Candidate(4, "hip", 0, 1, 1, 200, 2, 2.0);
	SetSnapshot(candidates, 2, 2, 2.0);
	CHECK(VR_FBT_Reconcile(&manager, 2, 2.0, candidates, 2));
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).device_index == 4);
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).state == VR_FBT_STATE_TRACKING);
	CHECK(Role(&manager, VR_FBT_ROLE_LEFT_FOOT).state == VR_FBT_STATE_CONNECTED_INVALID);
	candidates[0] = Candidate(4, "hip", 0, 1, 1, 200, 3, 3.0);
	candidates[1] = Candidate(11, "left", 0, 0, 0, 1, 3, 3.0);
	SetSnapshot(candidates, 2, 3, 3.0);
	CHECK(VR_FBT_Reconcile(&manager, 3, 3.0, candidates, 2));
	CHECK(Role(&manager, VR_FBT_ROLE_LEFT_FOOT).state == VR_FBT_STATE_LOST);
	candidates[1] = Candidate(12, "left", 0, 1, 1, 200, 4, 4.0);
	SetSnapshot(candidates, 2, 4, 4.0);
	CHECK(VR_FBT_Reconcile(&manager, 4, 4.0, candidates, 2));
	CHECK(Role(&manager, VR_FBT_ROLE_LEFT_FOOT).state == VR_FBT_STATE_TRACKING);
	CHECK(Role(&manager, VR_FBT_ROLE_LEFT_FOOT).device_index == 12);
	candidates[1] = Candidate(12, "left", 0, 1, 0, 1, 5, 4.02);
	SetSnapshot(candidates, 2, 5, 4.02);
	CHECK(VR_FBT_Reconcile(&manager, 5, 4.02, candidates, 2));
	CHECK(Role(&manager, VR_FBT_ROLE_LEFT_FOOT).state == VR_FBT_STATE_PREDICTING);
	candidates[1] = Candidate(12, "left", 0, 0, 0, 1, 6, 4.03);
	SetSnapshot(candidates, 2, 6, 4.03);
	CHECK(VR_FBT_Reconcile(&manager, 6, 4.03, candidates, 2));
	CHECK(Role(&manager, VR_FBT_ROLE_LEFT_FOOT).device_index == (unsigned int)-1);
	CHECK(Role(&manager, VR_FBT_ROLE_LEFT_FOOT).tracking_result == 0);
}

static void TestAmbiguityAndEphemeralLifetime(void)
{
	vr_fbt_manager_t manager;
	vr_fbt_candidate_t candidates[2];
	vr_fbt_candidate_status_t status;

	VR_FBT_Init(&manager);
	candidates[0] = Candidate(1, "dup", 0, 1, 1, 200, 1, 1.0);
	candidates[1] = Candidate(2, "dup", 0, 1, 1, 200, 1, 1.0);
	CHECK(VR_FBT_Reconcile(&manager, 1, 1.0, candidates, 2));
	CHECK(VR_FBT_GetCandidate(&manager, 0, &status) && status.serial_ambiguous);
	CHECK(!VR_FBT_AssignCandidate(&manager, VR_FBT_ROLE_HIP, 0));
	candidates[0] = Candidate(9, "bad/name", 77, 1, 1, 200, 2, 2.0);
	CHECK(VR_FBT_Reconcile(&manager, 2, 2.0, candidates, 1));
	CHECK(VR_FBT_AssignCandidate(&manager, VR_FBT_ROLE_HIP, 0));
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).identity_kind == VR_FBT_IDENTITY_EPHEMERAL);
	candidates[0] = Candidate(9, "", 77, 1, 1, 201, 3, 3.0);
	CHECK(VR_FBT_Reconcile(&manager, 3, 3.0, candidates, 1));
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).state == VR_FBT_STATE_PREDICTING);
	candidates[0] = Candidate(9, "", 77, 0, 0, 1, 3, 3.0);
	candidates[0].snapshot_id = 4;
	candidates[0].snapshot_time = 4.0;
	CHECK(VR_FBT_Reconcile(&manager, 4, 4.0, candidates, 1));
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).state == VR_FBT_STATE_UNASSIGNED);
	candidates[0] = Candidate(9, "", 88, 1, 1, 200, 5, 5.0);
	CHECK(VR_FBT_Reconcile(&manager, 5, 5.0, candidates, 1));
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).state == VR_FBT_STATE_UNASSIGNED);
	CHECK(!VR_FBT_SerialIsSafe("bad/name"));
	CHECK(!VR_FBT_SerialIsSafe("bad\\name"));
	CHECK(!VR_FBT_SerialIsSafe("bad\"name"));
	CHECK(!VR_FBT_SerialIsSafe("bad\nname"));
	candidates[0] = Candidate(10, "", 0, 1, 1, 200, 6, 6.0);
	CHECK(VR_FBT_Reconcile(&manager, 6, 6.0, candidates, 1));
	CHECK(!VR_FBT_AssignCandidate(&manager, VR_FBT_ROLE_RIGHT_FOOT, 0));
}

static void TestIdentityProtectionSnapshotsAndTiming(void)
{
	vr_fbt_manager_t manager;
	vr_fbt_candidate_t candidate;
	vr_fbt_timing_t timing;

	VR_FBT_Init(&manager);
	candidate = Candidate(1, "saved", 0, 1, 1, 200, 10, 0.0);
	CHECK(VR_FBT_Reconcile(&manager, 10, 0.0, &candidate, 1));
	CHECK(VR_FBT_AssignCandidate(&manager, VR_FBT_ROLE_HIP, 0));
	CHECK(!VR_FBT_AssignCandidate(&manager, VR_FBT_ROLE_LEFT_FOOT, 0));
	candidate = Candidate(1, "replacement", 0, 1, 1, 200, 11, 0.02);
	CHECK(VR_FBT_Reconcile(&manager, 11, 0.02, &candidate, 1));
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).state == VR_FBT_STATE_PREDICTING);
	candidate.snapshot_id = 12;
	candidate.snapshot_time = 0.1;
	CHECK(VR_FBT_Reconcile(&manager, 12, 0.1, &candidate, 1));
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).state == VR_FBT_STATE_LOST);
	CHECK(!strcmp(Role(&manager, VR_FBT_ROLE_HIP).serial, "saved"));
	candidate.snapshot_id = 12;
	candidate.snapshot_time = 12.0;
	CHECK(!VR_FBT_Reconcile(&manager, 12, 12.0, &candidate, 1));
	candidate.snapshot_id = 11;
	CHECK(!VR_FBT_Reconcile(&manager, 9, 12.0, &candidate, 1));
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).last_reconciled_time == 0.1);
	CHECK(VR_FBT_GetRoleTiming(&manager, VR_FBT_ROLE_HIP,
		VR_FBT_PREDICT_WINDOW_SECONDS, &timing));
	CHECK(timing.within_predict_window);
	CHECK(!timing.within_loss_window);
	CHECK(VR_FBT_GetRoleTiming(&manager, VR_FBT_ROLE_HIP,
		VR_FBT_PREDICT_WINDOW_SECONDS + 0.001, &timing));
	CHECK(!timing.within_predict_window && timing.within_loss_window);
	CHECK(VR_FBT_GetRoleTiming(&manager, VR_FBT_ROLE_HIP,
		VR_FBT_LOSS_WINDOW_SECONDS, &timing));
	CHECK(timing.within_loss_window);
	CHECK(VR_FBT_GetRoleTiming(&manager, VR_FBT_ROLE_HIP,
		VR_FBT_LOSS_WINDOW_SECONDS + 0.001, &timing));
	CHECK(!timing.within_predict_window && !timing.within_loss_window);
}

static void TestSnapshotValidationAndWrap(void)
{
	vr_fbt_manager_t manager;
	vr_fbt_manager_t before;
	vr_fbt_candidate_t candidate;
	vr_fbt_candidate_t duplicates[2];
	vr_fbt_timing_t timing;
	vr_fbt_timing_t timing_before;

	VR_FBT_Init(&manager);
	candidate = Candidate(3, "valid", 0, 1, 1, 200, 1, 1.0);
	CHECK(VR_FBT_Reconcile(&manager, 1, 1.0, &candidate, 1));
	before = manager;
	candidate.snapshot_id = 2;
	candidate.snapshot_time = NAN;
	CHECK(!VR_FBT_Reconcile(&manager, 2, NAN, &candidate, 1));
	CHECK(!memcmp(&manager, &before, sizeof(manager)));
	candidate.snapshot_time = INFINITY;
	CHECK(!VR_FBT_Reconcile(&manager, 2, INFINITY, &candidate, 1));
	CHECK(!memcmp(&manager, &before, sizeof(manager)));
	candidate.snapshot_time = 0.5;
	CHECK(!VR_FBT_Reconcile(&manager, 2, 0.5, &candidate, 1));
	CHECK(!memcmp(&manager, &before, sizeof(manager)));
	duplicates[0] = Candidate(4, "one", 0, 1, 1, 200, 2, 2.0);
	duplicates[1] = Candidate(4, "two", 0, 1, 1, 200, 2, 2.0);
	CHECK(!VR_FBT_Reconcile(&manager, 2, 2.0, duplicates, 2));
	CHECK(!memcmp(&manager, &before, sizeof(manager)));
	memset(&timing, 0x5a, sizeof(timing));
	timing_before = timing;
	CHECK(!VR_FBT_GetRoleTiming(&manager, VR_FBT_ROLE_HIP, NAN, &timing));
	CHECK(!memcmp(&timing, &timing_before, sizeof(timing)));
	CHECK(!VR_FBT_GetRoleTiming(&manager, VR_FBT_ROLE_HIP, INFINITY, &timing));
	CHECK(!memcmp(&timing, &timing_before, sizeof(timing)));

	VR_FBT_Init(&manager);
	candidate = Candidate(5, "wrap", 0, 1, 1, 200, UINT64_MAX, 1.0);
	CHECK(VR_FBT_Reconcile(&manager, UINT64_MAX, 1.0, &candidate, 1));
	CHECK(VR_FBT_AssignCandidate(&manager, VR_FBT_ROLE_HIP, 0));
	SetSnapshot(&candidate, 1, 0, 2.0);
	CHECK(VR_FBT_Reconcile(&manager, 0, 2.0, &candidate, 1));
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).state == VR_FBT_STATE_TRACKING);
	SetSnapshot(&candidate, 1, UINT64_MAX, 3.0);
	CHECK(!VR_FBT_Reconcile(&manager, UINT64_MAX, 3.0, &candidate, 1));
}

static void TestSafeSerialRebindAndOfflineBind(void)
{
	vr_fbt_manager_t manager;
	vr_fbt_candidate_t candidate;

	VR_FBT_Init(&manager);
	candidate = Candidate(8, "saved-safe", 0, 1, 1, 200, 1, 1.0);
	CHECK(VR_FBT_Reconcile(&manager, 1, 1.0, &candidate, 1));
	CHECK(VR_FBT_AssignCandidate(&manager, VR_FBT_ROLE_HIP, 0));
	candidate = Candidate(8, "replacement", 0, 1, 1, 200, 2, 2.0);
	CHECK(VR_FBT_Reconcile(&manager, 2, 2.0, &candidate, 1));
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).state == VR_FBT_STATE_LOST);
	CHECK(!strcmp(Role(&manager, VR_FBT_ROLE_HIP).serial, "saved-safe"));
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).device_index == (unsigned int)-1);
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).tracking_result == 0);
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).device_to_absolute_tracking[0][3] == 0.0f);
	candidate = Candidate(9, "saved-safe", 0, 1, 1, 200, 3, 3.0);
	CHECK(VR_FBT_Reconcile(&manager, 3, 3.0, &candidate, 1));
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).state == VR_FBT_STATE_TRACKING);
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).device_index == 9);

	VR_FBT_Init(&manager);
	CHECK(VR_FBT_BindSerial(&manager, VR_FBT_ROLE_LEFT_FOOT, "offline-left"));
	CHECK(Role(&manager, VR_FBT_ROLE_LEFT_FOOT).state == VR_FBT_STATE_LOST);
	CHECK(!VR_FBT_BindSerial(&manager, VR_FBT_ROLE_RIGHT_FOOT, "offline-left"));
	CHECK(!VR_FBT_BindSerial(&manager, VR_FBT_ROLE_RIGHT_FOOT, "bad/serial"));
	candidate = Candidate(13, "offline-left", 0, 1, 1, 200, 1, 1.0);
	CHECK(VR_FBT_Reconcile(&manager, 1, 1.0, &candidate, 1));
	CHECK(Role(&manager, VR_FBT_ROLE_LEFT_FOOT).state == VR_FBT_STATE_TRACKING);
}

static void TestExplicitRoleSubsets(void)
{
	vr_fbt_manager_t manager;
	vr_fbt_candidate_t candidates[2];

	VR_FBT_Init(&manager);
	candidates[0] = Candidate(1, "left-only", 0, 1, 1, 200, 1, 1.0);
	CHECK(VR_FBT_Reconcile(&manager, 1, 1.0, candidates, 1));
	CHECK(VR_FBT_AssignCandidate(&manager, VR_FBT_ROLE_LEFT_FOOT, 0));
	CHECK(Role(&manager, VR_FBT_ROLE_LEFT_FOOT).state == VR_FBT_STATE_TRACKING);
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).state == VR_FBT_STATE_UNASSIGNED);

	VR_FBT_Init(&manager);
	candidates[0] = Candidate(2, "right-only", 0, 1, 1, 200, 1, 1.0);
	CHECK(VR_FBT_Reconcile(&manager, 1, 1.0, candidates, 1));
	CHECK(VR_FBT_AssignCandidate(&manager, VR_FBT_ROLE_RIGHT_FOOT, 0));
	CHECK(Role(&manager, VR_FBT_ROLE_RIGHT_FOOT).state == VR_FBT_STATE_TRACKING);
	CHECK(Role(&manager, VR_FBT_ROLE_LEFT_FOOT).state == VR_FBT_STATE_UNASSIGNED);

	VR_FBT_Init(&manager);
	candidates[0] = Candidate(3, "hip-right", 0, 1, 1, 200, 1, 1.0);
	candidates[1] = Candidate(4, "right-pair", 0, 1, 1, 200, 1, 1.0);
	CHECK(VR_FBT_Reconcile(&manager, 1, 1.0, candidates, 2));
	CHECK(VR_FBT_AssignCandidate(&manager, VR_FBT_ROLE_HIP, 0));
	CHECK(VR_FBT_AssignCandidate(&manager, VR_FBT_ROLE_RIGHT_FOOT, 1));
	CHECK(Role(&manager, VR_FBT_ROLE_HIP).state == VR_FBT_STATE_TRACKING);
	CHECK(Role(&manager, VR_FBT_ROLE_RIGHT_FOOT).state == VR_FBT_STATE_TRACKING);
	CHECK(Role(&manager, VR_FBT_ROLE_LEFT_FOOT).state == VR_FBT_STATE_UNASSIGNED);
}

int main(void)
{
	TestCandidateCountsAndExplicitRoles();
	TestSerialRebindAndIndependentState();
	TestAmbiguityAndEphemeralLifetime();
	TestIdentityProtectionSnapshotsAndTiming();
	TestSnapshotValidationAndWrap();
	TestSafeSerialRebindAndOfflineBind();
	TestExplicitRoleSubsets();
	if (failures) {
		fprintf(stderr, "vr_fbt fixture: %d failure(s)\\n", failures);
		return 1;
	}
	puts("vr_fbt fixture: all checks passed");
	return 0;
}
