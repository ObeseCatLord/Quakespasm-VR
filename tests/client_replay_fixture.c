/* Focused client replay checks.  The replay owner is production cl_main.c;
 * the PM entry points are probes here so replay admission/history semantics
 * can be tested independently from the existing real-solver fixture. */
#include "../Quake/cl_main.c"
#include <assert.h>
#include <stdio.h>

playermove_t pmove;
movevars_t movevars;
double realtime;
cvar_t r_lerpmove = {"r_lerpmove", "1", CVAR_NONE};
cvar_t r_lerpturn = {"r_lerpturn", "1", CVAR_NONE};

static qboolean selector_result = true;
static qboolean selector_mutates_movevars;
static int selector_calls, collector_calls, move_calls, preview_calls;
static int fluid_contact_call = -1;
static usercmd_t preview_cmd;
static usercmd_t observed_cmds[66];
static int observed_pm_types[66];
static float observed_waterjump_before[66];
static qboolean observed_gorilla_allowed[66];
static int observed_gorilla_touching[66];
static float observed_gorilla_anchor_x[66];

void CL_PreviewMove (usercmd_t *cmd)
{
	preview_calls++;
	*cmd = preview_cmd;
	cmd->seconds = 0;
	cmd->msec = 0;
}

qboolean PMCL_SetMoveVars (void)
{
	selector_calls++;
	if (selector_mutates_movevars)
		movevars.gravity = 1234;
	return selector_result;
}

void PMCL_AddEntities (vec3_t boxminmax[2])
{
	assert (boxminmax != NULL);
	collector_calls++;
}

void PM_PlayerMove (float gamespeed)
{
	assert (gamespeed == 1);
	assert (move_calls < (int)countof(observed_cmds));
	observed_cmds[move_calls] = pmove.cmd;
	observed_pm_types[move_calls] = pmove.pm_type;
	observed_waterjump_before[move_calls] = pmove.waterjumptime;
	observed_gorilla_allowed[move_calls] = pmove.gorilla_allowed;
	observed_gorilla_touching[move_calls] = pmove.gorilla.touching;
	observed_gorilla_anchor_x[move_calls] = pmove.gorilla.anchor[0][0];
	if (!pmove.cmd.vr_gorilla.flags)
		memset (&pmove.gorilla, 0, sizeof (pmove.gorilla));
	pmove.origin[0] += 1;
	/* The production solver latches a transient fluid crossing even when the
	 * command's final categorized position is dry. */
	pmove.fluid_contacted = move_calls == fluid_contact_call;
	pmove.waterlevel = 0;
	pmove.waterjumptime = 50 + pmove.cmd.sequence;
	pmove.onground = true;
	move_calls++;
}

static qmodel_t worldmodel;
static entity_t entities[5];

static void reset_probes (void)
{
	selector_result = true;
	selector_mutates_movevars = false;
	selector_calls = collector_calls = move_calls = preview_calls = 0;
	fluid_contact_call = -1;
	memset (&preview_cmd, 0, sizeof(preview_cmd));
	memset (observed_cmds, 0, sizeof(observed_cmds));
	memset (observed_pm_types, 0, sizeof(observed_pm_types));
	memset (observed_waterjump_before, 0, sizeof(observed_waterjump_before));
	memset (observed_gorilla_allowed, 0, sizeof(observed_gorilla_allowed));
	memset (observed_gorilla_touching, 0, sizeof(observed_gorilla_touching));
	memset (observed_gorilla_anchor_x, 0, sizeof(observed_gorilla_anchor_x));
}

static void reset_client (void)
{
	memset (&cl, 0, sizeof(cl));
	memset (&cls, 0, sizeof(cls));
	memset (&pmove, 0, sizeof(pmove));
	memset (&movevars, 0, sizeof(movevars));
	memset (entities, 0, sizeof(entities));
	memset (&worldmodel, 0, sizeof(worldmodel));
	reset_probes ();

	cls.state = ca_connected;
	cls.signon = SIGNONS;
	cl.worldmodel = &worldmodel;
	cl.entities = entities;
	cl.num_entities = 2;
	cl.viewentity = 1;
	cl.protocol_pext2 = PEXT2_PREDINFO;
	cl.stats[STAT_HEALTH] = 100;
	cl.ackedmovemessages = 2;
	cl.movemessages = 4;
	cl.time = 10;
	cl.pendingcmd.servertime = 9.75f;
	cl.pendingcmd.seconds = .25f;
	realtime = 20;

	entities[1].update_type = true;
	entities[1].netstate.pmovetype = MOVETYPE_WALK | 0x80;
	entities[1].netstate.origin[0] = 100;
	entities[1].msg_origins[0][0] = 10;
	entities[1].netstate.velocity[0] = 80;

	cl.movecmds[3 & MOVECMDS_MASK].sequence = 3;
	cl.movecmds[3 & MOVECMDS_MASK].seconds = .1f;
	cl.movecmds[3 & MOVECMDS_MASK].msec = 100;
	cl.movecmds[3 & MOVECMDS_MASK].forwardmove = 320;
	cl.movecmds[3 & MOVECMDS_MASK].vr_active = true;
	cl.movecmds[3 & MOVECMDS_MASK].vr_handpos_relative = true;
	cl.movecmds[3 & MOVECMDS_MASK].vr_roomscalemove[0] = 8;
	cl.movecmds[3 & MOVECMDS_MASK].vr_gorilla.flags = VR_GORILLA_HANDS;
	cl_nopred.value = 0;
}

static void admit_private_snapshot (void)
{
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	cl.protocol_pext2 = QSVR_PEXT2_REQUIRED;
	cl.move_snapshot_valid = true;
	cl.move_snapshot_ack = cl.ackedmovemessages;
	cl.move_snapshot_owner = cl.viewentity;
	cl.move_ack_prediction_allowed = true;
	cl.move_ack_authority = MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT;
}

static void check_public_replay_strips_private_fields (void)
{
	vec3_t origin;

	reset_client ();
	preview_cmd.forwardmove = 96;
	preview_cmd.vr_active = true;
	preview_cmd.vr_roomscalemove[0] = 4;
	preview_cmd.vr_gorilla.flags = VR_GORILLA_HANDS;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (selector_calls == 1 && collector_calls == 1 && move_calls == 2);
	assert (preview_calls == 1);
	assert (observed_pm_types[0] == PM_NORMAL);
	assert (observed_cmds[0].sequence == 3);
	assert (observed_cmds[0].msec == 0);
	assert (!observed_cmds[0].vr_active);
	assert (observed_cmds[0].vr_roomscalemove[0] == 0);
	assert (!observed_cmds[0].vr_gorilla.flags);
	assert (!observed_cmds[0].vr_gorilla_motion.flags);
	assert (observed_cmds[1].forwardmove == 96);
	assert (observed_cmds[1].seconds == .25f);
	assert (observed_cmds[1].msec == 0);
	assert (!observed_cmds[1].vr_active);
	assert (observed_cmds[1].vr_roomscalemove[0] == 0);
	assert (!observed_cmds[1].vr_gorilla.flags);
	assert (origin[0] == 12); /* history plus disposable partial preview */
	assert (cl.velocity[0] == 10);
}

static void check_prediction_optout_and_public_protocol_gate (void)
{
	vec3_t origin;

	reset_client ();
	cl_nopred.value = 1;
	assert (!CL_ReplayPlayerMovement (&entities[1], origin));
	assert (!selector_calls && !collector_calls && !move_calls);

	cl_nopred.value = 0;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (selector_calls == 1 && collector_calls == 1 && move_calls == 2);

	reset_client ();
	cl.protocol_pext2 = 0;
	assert (!CL_ReplayPlayerMovement (&entities[1], origin));
	assert (!selector_calls && !collector_calls && !move_calls);
}

static void check_private_snapshot_and_metadata_gates (void)
{
	vec3_t origin;

	reset_client ();
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	assert (!CL_ReplayPlayerMovement (&entities[1], origin));
	assert (!selector_calls && !collector_calls && !move_calls);

	admit_private_snapshot ();
	cl.move_snapshot_ack--;
	assert (!CL_ReplayPlayerMovement (&entities[1], origin));
	cl.move_snapshot_ack = cl.ackedmovemessages;
	cl.move_ack_prediction_allowed = false;
	assert (!CL_ReplayPlayerMovement (&entities[1], origin));
	assert (!selector_calls && !collector_calls && !move_calls);

	cl.move_ack_prediction_allowed = true;
	cl.move_ack_authority = MOVE_AUTHORITY_LEGACY_FRAME;
	assert (!CL_ReplayPlayerMovement (&entities[1], origin));
	cl.move_ack_authority = MOVE_AUTHORITY_PMOVE_QC_COMMAND;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (origin[0] == 102); /* private baseline plus history and preview */
	assert (observed_cmds[0].msec == 100);
	assert (observed_cmds[0].vr_active);
	assert (observed_cmds[0].vr_roomscalemove[0] == 8);
}

static void check_private_walk_shadow (void)
{
	cl_replay_result_t result;
	playermove_t saved_pmove;
	movevars_t saved_movevars;
	int saved_propagate_sequence[countof(cl.move_replay_propagate_sequence)];
	float saved_propagate_waterjumptime[countof(cl.move_replay_propagate_waterjumptime)];
	vec3_t saved_velocity;
	qboolean saved_onground, saved_inwater;
	int i;

	reset_client ();
	admit_private_snapshot ();
	cl.move_ack_prediction_allowed = false;
	cl_nopred.value = 1;
	cl.statsf[STAT_PRIVATE_JUMP_SECS] = 1.25f;
	cl.statsf[STAT_PRIVATE_WATERJUMP_SECS] = 1.5f;
	cl.movecmds[3 & MOVECMDS_MASK].vr_active = false;
	cl.movemessages = 5;
	cl.movecmds[4 & MOVECMDS_MASK].sequence = 99; /* target 3 must not read past itself */
	cl.move_replay_private_metadata_valid = true;
	cl.move_replay_private_prediction_allowed = true;
	cl.move_replay_private_authority = MOVE_AUTHORITY_LEGACY_FRAME;
	cl.move_replay_private_mode_epoch = cl.move_ack_mode_epoch + 1;
	cl.move_replay_private_discontinuity_epoch = cl.move_ack_discontinuity_epoch + 1;
	for (i = 0; i < (int)countof(cl.move_replay_propagate_sequence); i++)
	{
		cl.move_replay_propagate_sequence[i] = -100 - i;
		cl.move_replay_propagate_waterjumptime[i] = (float)i + .25f;
	}
	cl.move_replay_propagate_sequence[3 & MOVECMDS_MASK] = 3;
	cl.move_replay_propagate_waterjumptime[3 & MOVECMDS_MASK] = 42;
	memcpy (saved_propagate_sequence, cl.move_replay_propagate_sequence,
		sizeof(saved_propagate_sequence));
	memcpy (saved_propagate_waterjumptime, cl.move_replay_propagate_waterjumptime,
		sizeof(saved_propagate_waterjumptime));
	VectorSet (cl.velocity, 7, 8, 9);
	VectorCopy (cl.velocity, saved_velocity);
	saved_onground = cl.onground = false;
	saved_inwater = cl.inwater = true;
	pmove.origin[0] = 77;
	pmove.cmd.sequence = 66;
	pmove.pm_type = PM_DEAD;
	movevars.gravity = 654;
	saved_pmove = pmove;
	saved_movevars = movevars;
	selector_mutates_movevars = true;

	assert (CL_ComputeReplayPlayerMovement (&entities[1], &result, true, 3));
	assert (!cl.move_ack_prediction_allowed);
	assert (selector_calls == 1 && collector_calls == 1 && move_calls == 1);
	assert (preview_calls == 0);
	assert (observed_cmds[0].sequence == 3 && observed_cmds[0].msec == 100);
	assert (!observed_cmds[0].vr_active);
	assert (observed_waterjump_before[0] == 1.5f); /* shadow uses authority, not stale propagation */
	assert (result.origin[0] == 101 && result.velocity[0] == 10);
	assert (result.onground && !result.inwater);
	assert (result.target_sequence == 3 && result.jump_secs == 1.25f);
	assert (memcmp (&pmove, &saved_pmove, sizeof(pmove)) == 0);
	assert (memcmp (&movevars, &saved_movevars, sizeof(movevars)) == 0);
	assert (memcmp (cl.move_replay_propagate_sequence, saved_propagate_sequence,
		sizeof(saved_propagate_sequence)) == 0);
	assert (memcmp (cl.move_replay_propagate_waterjumptime, saved_propagate_waterjumptime,
		sizeof(saved_propagate_waterjumptime)) == 0);
	assert (VectorCompare (cl.velocity, saved_velocity));
	assert (cl.onground == saved_onground && cl.inwater == saved_inwater);

	/* A selector failure can mutate movevars before rejecting; shadow restores
	 * both solver globals on that path too. */
	reset_probes ();
	selector_result = false;
	movevars.gravity = 321;
	saved_movevars = movevars;
	pmove.origin[0] = 88;
	saved_pmove = pmove;
	assert (!CL_ComputeReplayPlayerMovement (&entities[1], &result, true, 3));
	assert (memcmp (&pmove, &saved_pmove, sizeof(pmove)) == 0);
	assert (memcmp (&movevars, &saved_movevars, sizeof(movevars)) == 0);
	assert (!collector_calls && !move_calls && !preview_calls);

	/* A bad owner baseline fails after pmove has been cleared and must restore
	 * both globals along with the earlier selector-failure path. */
	reset_probes ();
	selector_mutates_movevars = true;
	movevars.gravity = 222;
	saved_movevars = movevars;
	pmove.origin[0] = 99;
	saved_pmove = pmove;
	entities[1].netstate.origin[1] = NAN;
	assert (!CL_ComputeReplayPlayerMovement (&entities[1], &result, true, 3));
	entities[1].netstate.origin[1] = 0;
	assert (memcmp (&pmove, &saved_pmove, sizeof(pmove)) == 0);
	assert (memcmp (&movevars, &saved_movevars, sizeof(movevars)) == 0);
	assert (selector_calls == 1 && !collector_calls && !move_calls && !preview_calls);

	selector_result = true;
	reset_probes ();
	cl.move_snapshot_ack--;
	assert (!CL_ComputeReplayPlayerMovement (&entities[1], &result, true, 3));
	cl.move_snapshot_ack = cl.ackedmovemessages;
	cl.move_snapshot_owner = 2;
	assert (!CL_ComputeReplayPlayerMovement (&entities[1], &result, true, 3));
	cl.move_snapshot_owner = cl.viewentity;
	cl.move_ack_authority = MOVE_AUTHORITY_PMOVE_QC_COMMAND;
	assert (!CL_ComputeReplayPlayerMovement (&entities[1], &result, true, 3));
	cl.move_ack_authority = MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT;
	entities[1].netstate.pmovetype = MOVETYPE_TOSS;
	assert (!CL_ComputeReplayPlayerMovement (&entities[1], &result, true, 3));
	entities[1].netstate.pmovetype = MOVETYPE_WALK | 0x80;
	cl.protocol_qsvr = 0;
	assert (!CL_ComputeReplayPlayerMovement (&entities[1], &result, true, 3));
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	cl.movecmds[3 & MOVECMDS_MASK].vr_active = true;
	assert (!CL_ComputeReplayPlayerMovement (&entities[1], &result, true, 3));
	cl.movecmds[3 & MOVECMDS_MASK].vr_active = false;
	cl.movecmds[3 & MOVECMDS_MASK].sequence = 99;
	assert (!CL_ComputeReplayPlayerMovement (&entities[1], &result, true, 3));
	cl.movecmds[3 & MOVECMDS_MASK].sequence = 3;
	assert (!CL_ComputeReplayPlayerMovement (&entities[1], &result, true, 2));
	assert (!CL_ComputeReplayPlayerMovement (&entities[1], &result, true, 4));
	assert (!CL_ComputeReplayPlayerMovement (&entities[1], &result, true, 5));
	assert (!selector_calls && !collector_calls && !move_calls && !preview_calls);
}

static void check_partial_preview_timing_and_empty_history (void)
{
	vec3_t origin;
	double sample_time, fractional_carry;

	reset_client ();
	cl.ackedmovemessages = 3;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (move_calls == 1 && preview_calls == 1);
	assert (observed_cmds[0].seconds == .25f);
	assert (observed_cmds[0].msec == 0);

	reset_client ();
	cl.time = 12;
	cl.pendingcmd.servertime = 10;
	cl.pendingcmd.seconds = 2;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (observed_cmds[1].seconds == .5f);

	reset_client ();
	admit_private_snapshot ();
	cl.move_msec_sample_valid = true;
	cl.move_msec_sample_time = 19.9604;
	cl.move_msec_fractional_carry = .375;
	sample_time = cl.move_msec_sample_time;
	fractional_carry = cl.move_msec_fractional_carry;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (move_calls == 2 && preview_calls == 1);
	assert (fabsf (observed_cmds[1].seconds - .0396f) < .0001f);
	assert (observed_cmds[1].msec == 40);
	assert (cl.move_msec_sample_time == sample_time);
	assert (cl.move_msec_fractional_carry == fractional_carry);
}

static void check_history_loss_and_selector_failure (void)
{
	vec3_t origin;

	reset_client ();
	cl.movecmds[3 & MOVECMDS_MASK].sequence = 99;
	assert (!CL_ReplayPlayerMovement (&entities[1], origin));
	assert (!selector_calls && !move_calls);

	reset_client ();
	cl.ackedmovemessages = 1;
	cl.movemessages = 70;
	for (int seq = 6; seq < cl.movemessages; seq++)
		cl.movecmds[seq & MOVECMDS_MASK].sequence = seq;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (selector_calls == 1 && move_calls == 65);
	assert (observed_cmds[0].sequence == 6 && observed_cmds[63].sequence == 69);

	reset_client ();
	cl.ackedmovemessages = 1;
	cl.movemessages = 70;
	for (int seq = 6; seq < cl.movemessages; seq++)
		cl.movecmds[seq & MOVECMDS_MASK].sequence = seq;
	admit_private_snapshot ();
	assert (!CL_ReplayPlayerMovement (&entities[1], origin));
	assert (!selector_calls && !collector_calls && !move_calls);

	reset_client ();
	selector_result = false;
	assert (!CL_ReplayPlayerMovement (&entities[1], origin));
	assert (selector_calls == 1 && !collector_calls && !move_calls);
}

static void check_pause_death_and_move_modes (void)
{
	vec3_t origin;

	reset_client ();
	cl.paused = true;
	assert (!CL_ReplayPlayerMovement (&entities[1], origin));
	cl.paused = false;
	cl.stats[STAT_HEALTH] = 0;
	assert (!CL_ReplayPlayerMovement (&entities[1], origin));
	assert (!selector_calls && !move_calls);

	reset_client ();
	entities[1].netstate.pmovetype = MOVETYPE_STEP;
	assert (!CL_ReplayPlayerMovement (&entities[1], origin));
	assert (!selector_calls && !move_calls);

	reset_client ();
	entities[1].netstate.pmovetype = MOVETYPE_TOSS;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (observed_pm_types[0] == PM_DEAD);
}

static void check_private_epoch_resets_propagation (void)
{
	vec3_t origin;

	reset_client ();
	admit_private_snapshot ();
	cl.statsf[STAT_PRIVATE_WATERJUMP_SECS] = 1.25f;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (observed_waterjump_before[0] == 1.25f);
	assert (cl.move_replay_propagate_sequence[4 & MOVECMDS_MASK] == 4);
	assert (cl.move_replay_propagate_waterjumptime[4 & MOVECMDS_MASK] == 53);

	reset_probes ();
	cl.ackedmovemessages = 3;
	cl.move_snapshot_ack = 3;
	cl.movemessages = 5;
	cl.movecmds[4 & MOVECMDS_MASK] = cl.movecmds[3 & MOVECMDS_MASK];
	cl.movecmds[4 & MOVECMDS_MASK].sequence = 4;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (observed_waterjump_before[0] == 53);
	assert (cl.move_replay_propagate_sequence[5 & MOVECMDS_MASK] == 5);

	reset_probes ();
	cl.ackedmovemessages = 4;
	cl.move_snapshot_ack = 4;
	cl.movemessages = 6;
	cl.movecmds[5 & MOVECMDS_MASK] = cl.movecmds[4 & MOVECMDS_MASK];
	cl.movecmds[5 & MOVECMDS_MASK].sequence = 5;
	cl.move_ack_mode_epoch++;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (observed_waterjump_before[0] == 1.25f);
}

static void check_private_replay_stops_at_fluid_crossing (void)
{
	vec3_t origin;

	reset_client ();
	admit_private_snapshot ();
	fluid_contact_call = 0;
	assert (!CL_ReplayPlayerMovement (&entities[1], origin));
	assert (move_calls == 1 && preview_calls == 0);
	assert (cl.move_replay_propagate_sequence[4 & MOVECMDS_MASK] == 0);

	reset_client ();
	admit_private_snapshot ();
	fluid_contact_call = 1;
	assert (!CL_ReplayPlayerMovement (&entities[1], origin));
	assert (move_calls == 2 && preview_calls == 1);
	assert (cl.move_replay_propagate_sequence[4 & MOVECMDS_MASK] == 0);

	reset_client ();
	fluid_contact_call = 0;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));

	reset_client ();
	admit_private_snapshot ();
	cl.move_ack_authority = MOVE_AUTHORITY_PMOVE_QC_COMMAND;
	fluid_contact_call = 0;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
}

static void check_trusted_gorilla_generation (void)
{
	vec3_t origin;

	reset_client ();
	admit_private_snapshot ();
	cl.vr_gorilla_allowed = true;
	cl.vr_gorilla_trusted_cap_sent = true;
	cl.vr_gorilla_motion_generation_valid = true;
	cl.vr_gorilla_motion_generation = 8;
	cl.movecmds[3 & MOVECMDS_MASK].vr_gorilla_motion.flags = VR_GORILLA_MOTION_ACTIVE;
	cl.movecmds[3 & MOVECMDS_MASK].vr_gorilla_motion.generation = 7;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (!observed_cmds[0].vr_gorilla_motion.flags);
	assert (!observed_cmds[0].vr_gorilla.flags);

	reset_probes ();
	cl.movecmds[3 & MOVECMDS_MASK].vr_gorilla.flags = VR_GORILLA_HANDS;
	cl.movecmds[3 & MOVECMDS_MASK].vr_gorilla_motion.flags = VR_GORILLA_MOTION_ACTIVE;
	cl.movecmds[3 & MOVECMDS_MASK].vr_gorilla_motion.generation = 8;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (observed_cmds[0].vr_gorilla_motion.flags == VR_GORILLA_MOTION_ACTIVE);
}

static void check_raw_gorilla_state_provenance (void)
{
	vec3_t origin;

	reset_client ();
	admit_private_snapshot ();
	cl.vr_gorilla_supported = cl.vr_gorilla_allowed = true;
	cl.vr_gorilla_state_valid = true;
	cl.vr_gorilla_state_sequence = cl.ackedmovemessages;
	cl.vr_gorilla_state.initialized = true;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (observed_gorilla_allowed[0]);

	reset_probes ();
	cl.movecmds[3 & MOVECMDS_MASK].vr_gorilla.flags = 0;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (!observed_gorilla_allowed[0]);

	reset_probes ();
	cl.movecmds[3 & MOVECMDS_MASK].vr_gorilla.flags = VR_GORILLA_HANDS;
	cl.vr_gorilla_state_sequence--;
	assert (!CL_ReplayPlayerMovement (&entities[1], origin));
	assert (!move_calls);

	reset_client ();
	admit_private_snapshot ();
	cl.vr_gorilla_supported = cl.vr_gorilla_allowed = true;
	cl.vr_gorilla_state_valid = true;
	cl.vr_gorilla_state_sequence = cl.ackedmovemessages;
	cl.vr_gorilla_state.initialized = true;
	cl.movecmds[3 & MOVECMDS_MASK].vr_gorilla.flags = 0;
	cl.movemessages = 5;
	cl.movecmds[4 & MOVECMDS_MASK] = cl.movecmds[3 & MOVECMDS_MASK];
	cl.movecmds[4 & MOVECMDS_MASK].sequence = 4;
	cl.movecmds[4 & MOVECMDS_MASK].vr_active = true;
	cl.movecmds[4 & MOVECMDS_MASK].vr_handpos_relative = true;
	cl.movecmds[4 & MOVECMDS_MASK].vr_gorilla.flags = VR_GORILLA_HANDS;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (observed_cmds[0].sequence == 3 && observed_cmds[1].sequence == 4);
	assert (observed_gorilla_allowed[1]);
}

static void check_raw_gorilla_preview_after_ack (void)
{
	vec3_t origin;

	reset_client ();
	admit_private_snapshot ();
	cl.vr_gorilla_supported = cl.vr_gorilla_allowed = true;
	cl.movecmds[3 & MOVECMDS_MASK].vr_gorilla.flags = 0;
	cl.vr_gorilla_state_valid = true;
	cl.vr_gorilla_state_sequence = cl.ackedmovemessages;
	cl.vr_gorilla_state.initialized = true;
	cl.vr_gorilla_state.touching = VR_GORILLA_HANDS;
	cl.vr_gorilla_state.anchor[0][0] = 42;
	preview_cmd.vr_gorilla.flags = VR_GORILLA_HANDS;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (preview_calls == 1 && move_calls == 2);
	assert (observed_gorilla_allowed[1]);
	assert (observed_cmds[1].vr_gorilla.flags == VR_GORILLA_HANDS);
	assert (!observed_gorilla_touching[1] && !observed_gorilla_anchor_x[1]);

	reset_probes ();
	cl.vr_gorilla_state_valid = false;
	preview_cmd.vr_gorilla.flags = VR_GORILLA_HANDS;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (observed_gorilla_allowed[1] && !observed_gorilla_touching[1]);

	reset_probes ();
	preview_cmd.vr_gorilla.flags = VR_GORILLA_HANDS | VR_GORILLA_RESET;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (preview_calls == 1 && observed_gorilla_allowed[1]);

	/* With every sent command acknowledged, the preview is the only raw
	 * sample. It must still seed the existing PMove replay owner. */
	reset_client ();
	admit_private_snapshot ();
	cl.ackedmovemessages = cl.move_snapshot_ack = 3;
	cl.vr_gorilla_supported = cl.vr_gorilla_allowed = true;
	cl.vr_gorilla_state_valid = true;
	cl.vr_gorilla_state_sequence = 3;
	cl.vr_gorilla_state.initialized = true;
	cl.vr_gorilla_state.touching = VR_GORILLA_HANDS;
	cl.vr_gorilla_state.anchor[0][0] = 42;
	preview_cmd.vr_gorilla.flags = VR_GORILLA_HANDS;
	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (preview_calls == 1 && move_calls == 1);
	assert (observed_gorilla_allowed[0]);
	assert (observed_gorilla_touching[0] == VR_GORILLA_HANDS);
	assert (observed_gorilla_anchor_x[0] == 42);

	/* An earlier raw command without a matching baseline cannot be replayed.
	 * A later preview RESET cannot recover the missed body displacement. */
	reset_client ();
	admit_private_snapshot ();
	cl.vr_gorilla_supported = cl.vr_gorilla_allowed = true;
	preview_cmd.vr_gorilla.flags = VR_GORILLA_HANDS | VR_GORILLA_RESET;
	assert (!CL_ReplayPlayerMovement (&entities[1], origin));
	assert (!preview_calls && !move_calls);
}

static void check_relink_prediction_and_attachment_pose (void)
{
	cl_relink_frame_t frame;
	vec3_t expected = {105, 0, 0};

	reset_client ();
	worldmodel.type = mod_brush;
	entities[1].model = &worldmodel;
	cl.mtime[0] = entities[1].msgtime = 1;
	entities[1].msg_origins[1][0] = 0;
	CL_PrepareRelinkViewPose (&frame, .5f);
	assert (frame.viewpose_valid);
	assert (!frame.viewpose_teleported);

	memset (entities, 0, sizeof(entities));
	cl.entities = entities;
	cl.num_entities = 4;
	cl.viewentity = 2;
	worldmodel.type = mod_brush;
	for (int i = 1; i < 4; i++)
		entities[i].model = &worldmodel;
	memset (&frame, 0, sizeof(frame));
	frame.viewpose_valid = true;
	VectorSet (frame.vieworigin, 100, 0, 0);

	entities[1].netstate.tagentity = 2;
	VectorSet (entities[1].origin, 5, 0, 0);
	assert (CL_AttachEntity (&entities[1], .5f, &frame));
	assert (VectorCompare (entities[1].origin, expected));

	entities[3].netstate.tagentity = 2;
	VectorSet (entities[3].origin, 5, 0, 0);
	assert (CL_AttachEntity (&entities[3], .5f, &frame));
	assert (VectorCompare (entities[3].origin, expected));

	memset (&frame, 0, sizeof(frame));
	cl.viewentity = 1;
	entities[2].msg_origins[0][0] = 20;
	entities[2].msg_origins[1][0] = 0;
	VectorSet (entities[2].origin, 999, 0, 0);
	entities[2].netstate.tagentity = 0;
	entities[3].netstate.tagentity = 2;
	VectorSet (entities[3].origin, 5, 0, 0);
	VectorSet (expected, 15, 0, 0);
	assert (CL_AttachEntity (&entities[3], .5f, &frame));
	assert (VectorCompare (entities[3].origin, expected));
}

int main (void)
{
	check_public_replay_strips_private_fields ();
	check_prediction_optout_and_public_protocol_gate ();
	check_private_snapshot_and_metadata_gates ();
	check_private_walk_shadow ();
	check_partial_preview_timing_and_empty_history ();
	check_history_loss_and_selector_failure ();
	check_pause_death_and_move_modes ();
	check_private_epoch_resets_propagation ();
	check_private_replay_stops_at_fluid_crossing ();
	check_trusted_gorilla_generation ();
	check_raw_gorilla_state_provenance ();
	check_raw_gorilla_preview_after_ack ();
	check_relink_prediction_and_attachment_pose ();
	puts ("QSS-M client replay: shadow WALK, partial preview timing, gating/history/epochs, Gorilla provenance and shared attachment pose passed");
	return 0;
}
