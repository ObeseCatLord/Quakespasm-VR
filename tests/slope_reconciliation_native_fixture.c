/* Targeted slope diagnostic. Actual QC/BSP, negotiated commands and
 * snapshot parsing; prepared relocation/input and captured delivery. This is
 * a same-completed-sequence solver comparison, not networking E2E evidence.
 * Build/run with run_slope_reconciliation_native.py; no production test API. */
/* Existing production shadow mode accepts bounded committed VR commands;
 * enabling that diagnostic does not alter movement or the protocol structs. */
#ifndef QSVR_SHADOW_TRACE
#define QSVR_SHADOW_TRACE 1
#endif
#define STOCK_LIQUID_FIXTURE_ENTRY ImportedSlopeLiquidMain
#include "stock_liquid_native_fixture.c"

typedef struct
{
	vec3_t origin, normal;
	float yaw;
} slope_start_t;

static qboolean slope_replaying, slope_replay_jump_held, slope_server_jump_owner;
static qboolean slope_vr_commands;
static int slope_pre_flags, slope_post_pre_flags, slope_qc_jump_accepted;
static float slope_pre_z, slope_post_pre_z;
void __real_PM_PlayerMove (float gamespeed);
void __wrap_PM_PlayerMove (float gamespeed)
{
	__real_PM_PlayerMove (gamespeed);
	if (slope_replaying) slope_replay_jump_held = pmove.jump_held;
	else if (shared_qc_observed_player && host_client &&
		host_client->edict == shared_qc_observed_player)
		slope_server_jump_owner = pmove.qc_jump_owner;
}

static void SlopeObserveQC (func_t function)
{
	if (!shared_qc_observed_player || function != pr_global_struct->PlayerPreThink ||
		pr_global_struct->self != EDICT_TO_PROG (shared_qc_observed_player)) return;
	slope_post_pre_flags = (int)shared_qc_observed_player->v.flags;
	slope_post_pre_z = shared_qc_observed_player->v.velocity[2];
	slope_qc_jump_accepted = (shared_qc_buttons & BUTTON_JUMP) &&
		(slope_pre_flags & FL_ONGROUND) && (slope_pre_flags & FL_JUMPRELEASED) &&
		!(slope_post_pre_flags & (FL_ONGROUND | FL_JUMPRELEASED)) &&
		slope_post_pre_z > slope_pre_z;
}

/* Require an actual dry world-hull corridor, rather than an authored plane
 * or a point trace which a standing player's hull cannot occupy. */
static qboolean SlopeCorridor (edict_t *player, const trace_t *floor,
	qboolean sloped, slope_start_t starts[2])
{
	vec3_t direction = {1, 0, 0};
	if (sloped)
	{
		VectorSet (direction, -floor->plane.normal[0], -floor->plane.normal[1], 0);
		VectorNormalize (direction);
	}
	for (int sample = -5; sample <= 5; ++sample)
	{
		vec3_t top, bottom;
		VectorMA (floor->endpos, sample * 16.0f, direction, top);
		top[2] -= sample * 16.0f * DotProduct (direction, floor->plane.normal) / floor->plane.normal[2];
		VectorCopy (top, bottom);
		top[2] += 40;
		bottom[2] -= 8;
		trace_t support = SV_Move (top, player->v.mins, player->v.maxs,
			bottom, MOVE_NORMAL, player);
		if (support.startsolid || support.allsolid || support.fraction == 1 ||
			support.ent != EDICT_NUM (0) ||
			DotProduct (support.plane.normal, floor->plane.normal) < .999f)
			return false;
		VectorCopy (support.endpos, player->v.origin);
		SV_CheckWater (player);
		if (player->v.waterlevel || SV_TestEntityPosition (player)) return false;
		if (sample == -5 || sample == 5)
		{
			int end = sample == -5 ? 0 : 1;
			VectorCopy (support.endpos, starts[end].origin);
			VectorCopy (support.plane.normal, starts[end].normal);
			starts[end].yaw = atan2f (direction[1], direction[0]) * 180 / M_PI + end * 180;
			/* Exact wire-representable yaw, including signed short decoding. */
			starts[end].yaw = (short)Q_rint (starts[end].yaw * 65536 / 360) * (360.0f / 65536);
		}
	}
	return true;
}

static qboolean FindSlopeStarts (edict_t *player, slope_start_t *flat,
	slope_start_t slopes[2])
{
	entvars_t saved = player->v;
	qboolean have_flat = false, have_slope = false;
	qmodel_t *world = qcvm->worldmodel;
	for (int leafnum = 1; leafnum <= world->numleafs && !(have_flat && have_slope); ++leafnum)
	{
		mleaf_t *leaf = &world->leafs[leafnum];
		if (leaf->contents != CONTENTS_EMPTY) continue;
		for (int x = 1; x < 4 && !(have_flat && have_slope); ++x)
			for (int y = 1; y < 4 && !(have_flat && have_slope); ++y)
				for (int z = 0; z < 3 && !(have_flat && have_slope); ++z)
				{
					vec3_t top = {
						leaf->minmaxs[0] + (leaf->minmaxs[3] - leaf->minmaxs[0]) * x / 4.0f,
						leaf->minmaxs[1] + (leaf->minmaxs[4] - leaf->minmaxs[1]) * y / 4.0f,
						leaf->minmaxs[5] - 32 - z * 32.0f};
					vec3_t bottom = {top[0], top[1], top[2] - 256};
					trace_t floor = SV_Move (top, player->v.mins, player->v.maxs,
						bottom, MOVE_NORMAL, player);
					if (floor.startsolid || floor.allsolid || floor.fraction == 1 ||
						floor.ent != EDICT_NUM (0)) continue;
					qboolean sloped = floor.plane.normal[2] > .72f && floor.plane.normal[2] < .98f;
					if ((!sloped && (have_flat || floor.plane.normal[2] != 1)) ||
						(sloped && have_slope)) continue;
					slope_start_t ends[2];
					if (!SlopeCorridor (player, &floor, sloped, ends)) continue;
					if (sloped)
					{
						memcpy (slopes, ends, sizeof (ends));
						have_slope = true;
					}
					else { *flat = ends[0]; have_flat = true; }
				}
	}
	player->v = saved;
	if (!have_flat || !have_slope)
		fprintf (stderr, "SLOPE_MISSING_GEOMETRY flat=%d slope=%d bounded dry world corridor; no synthetic replacement\n",
			have_flat, have_slope);
	return have_flat && have_slope;
}

static void SlopeBaseline (client_t *peer, client_state_t *state,
	const slope_start_t *start)
{
	PrepareLiquidCase (peer, state, start->origin, start->yaw);
	/* Finish the relocation recovery through the existing command owner. */
	host_frametime = .025;
	realtime += host_frametime;
	LiquidSend (peer, state, 0, 0, 0, slope_vr_commands, 0);
	GapWorldFrame ();
	host_client = peer; sv_player = peer->edict;
	char command[192];
	q_snprintf (command, sizeof (command), "setpos %.9g %.9g %.9g 0 %.9g 0",
		start->origin[0], start->origin[1], start->origin[2], start->yaw);
	Cmd_ExecuteString (command, src_client);
	Cmd_ExecuteString ("noclip 0", src_client);
	Cmd_ExecuteString ("notarget 1", src_client);
	/* Deliberately resting, released, known standing hull. No quantization
 * allowance needed for the baseline's zero velocity. */
	VectorClear (peer->edict->v.velocity);
	peer->edict->v.flags = (int)peer->edict->v.flags | FL_ONGROUND | FL_JUMPRELEASED;
	peer->edict->v.flags = (int)peer->edict->v.flags & ~FL_WATERJUMP;
	peer->edict->v.groundentity = 0;
	peer->edict->v.teleport_time = 0;
	SV_CheckWater (peer->edict);
	SV_LinkEdict (peer->edict, false);
	GapSnapshot (peer, state);
	VectorSet (state->viewangles, 0, start->yaw, 0);
	const entity_state_t *seed = &state->entities[state->viewentity].netstate;
	assert (peer->private_input_phase == PRIVATE_INPUT_RUNNING &&
		!peer->private_cmd_queue_count && state->move_ack_prediction_allowed &&
		state->move_ack_authority == MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT &&
		state->move_snapshot_valid && state->move_snapshot_ack == peer->private_completed_move &&
		state->ackedmovemessages == peer->private_completed_move &&
		state->move_snapshot_owner == state->viewentity &&
		seed->solidsize == (16u | (24u << 8) | ((32u + 32768u) << 16)) &&
		(seed->pmovetype & 0xc0) == 0x80 && peer->edict->v.waterlevel == 0);
	for (int axis = 0; axis < 3; ++axis)
	{
		assert (seed->origin[axis] == peer->edict->v.origin[axis] && seed->velocity[axis] == 0);
		assert (peer->edict->v.mins[axis] == (axis == 2 ? -24 : -16) &&
			peer->edict->v.maxs[axis] == (axis == 2 ? 32 : 16));
	}
	printf ("SLOPE_BASELINE ack=%d origin=%.9g,%.9g,%.9g velocity=0,0,0 hull=-16,-16,-24:16,16,32 normal=%.6f,%.6f,%.6f yaw=%.9g\n",
		state->ackedmovemessages, seed->origin[0], seed->origin[1], seed->origin[2],
		start->normal[0], start->normal[1], start->normal[2], start->yaw);
}

/* Equality is checked on the real decoded queue before any world execution.
 * Bytewise struct comparison would compare padding and absent VR defaults. */
static void SlopeCheckCommands (client_t *peer, const client_state_t *state)
{
	for (unsigned offset = 0; offset < peer->private_cmd_queue_count; ++offset)
	{
		const usercmd_t *wire = &peer->private_cmd_queue[
			(peer->private_cmd_queue_head + offset) % SV_PRIVATE_CMD_QUEUE_SIZE];
		const usercmd_t *journal = &state->movecmds[wire->sequence & MOVECMDS_MASK];
		assert (wire->sequence == journal->sequence && wire->msec == journal->msec &&
			wire->seconds == journal->seconds && wire->buttons == journal->buttons &&
			wire->impulse == journal->impulse && wire->forwardmove == journal->forwardmove &&
			wire->sidemove == journal->sidemove && wire->upmove == journal->upmove &&
			VectorCompare (wire->viewangles, journal->viewangles) &&
			wire->vr_active == journal->vr_active && journal->vr_active == slope_vr_commands);
		if (slope_vr_commands)
			assert (wire->vr_handpos_relative == journal->vr_handpos_relative &&
				VectorCompare (wire->vr_handpos, journal->vr_handpos) &&
				VectorCompare (wire->vr_handrot, journal->vr_handrot) &&
				VectorCompare (wire->vr_roomscalemove, journal->vr_roomscalemove));
	}
}

static unsigned RunSlopeCase (client_t *peer, client_state_t *state,
	const slope_start_t *start, const char *name, int msec, int jumping)
{
	SlopeBaseline (peer, state, start);
	const int baseline_ack = state->ackedmovemessages;
	const entity_state_t baseline = state->entities[state->viewentity].netstate;
	unsigned mismatches = 0, multihead = 0;
	int carry_msec = 0;
	shared_qc_observed_player = peer->edict;
	shared_qc_pre = shared_qc_post = 0;
	shared_qc_pre_seconds = shared_qc_post_seconds = 0;
	liquid_after_qc_composition = SlopeObserveQC; // observation only, no QC writes
	unsigned accepted_jumps = 0;
	unsigned jump_contract_errors = 0;
	float maximum_height = 0;
	int last_accepted_frame = -10;
	qboolean saw_air = false, landed_on_slope = false, rejump_air = false;
	int second_press = -1, frames_done = 0;
	/* A complete launch/landing/second launch at16ms fits the64-command ring.
 * Stop after the second release; no new snapshot, relocation, velocity reset,
 * or invented ground flag may manufacture the landing or release latch. */
	assert (jumping < 3 || msec == 16);
	for (int frame = 0; frame < (jumping >= 3 ? 40 : 8); ++frame)
	{
		if (jumping >= 3 && landed_on_slope && second_press < 0)
			second_press = frame;
		unsigned buttons;
		if (jumping >= 3)
			buttons = ((frame >= (jumping == 4 ? 5 : 0) && frame < (jumping == 4 ? 7 : 2)) ||
				(second_press >= 0 && frame < second_press + 2)) ? BUTTON_JUMP : 0;
		else
		{
			int first_press = jumping == 2 ? 0 : 1;
			buttons = jumping && (frame == first_press || frame == first_press + 1 || frame == 5 || frame == 6) ? BUTTON_JUMP : 0;
		}
		carry_msec += 25;
		while (carry_msec >= msec)
		{
			host_frametime = msec * .001;
			realtime += host_frametime;
			/* Initial release, press/hold, release, second press/hold/release.
 * Second press while airborne must not be counted as accepted QC jump. */
			LiquidSend (peer, state, buttons,
				(jumping == 4 || frame < 5) ? 160 : 0, 0, slope_vr_commands, 0);
			carry_msec -= msec;
		}
		SlopeCheckCommands (peer, state);
		int before = peer->private_completed_move;
		slope_pre_flags = (int)peer->edict->v.flags;
		slope_pre_z = peer->edict->v.velocity[2];
		slope_qc_jump_accepted = 0;
		host_frametime = .025;
		GapWorldFrame ();
		accepted_jumps += slope_qc_jump_accepted;
		int completed = peer->private_completed_move;
		frames_done = frame + 1;
		assert (peer->active && !peer->private_move_native_frame && completed > before &&
			shared_qc_pre == frame + 1 && shared_qc_post == frame + 1);
		if (completed - before > 1) ++multihead;
		/* Withheld baseline; replay only through the server's completed head.
 * A button-transition suffix can remain queued and MUST NOT be compared. */
		cl = *state;
		cl_replay_result_t replay;
		slope_replaying = true;
		assert (CL_ComputeReplayPlayerMovement (&cl.entities[cl.viewentity], &replay, true, completed));
		slope_replaying = false;
		assert (replay.target_sequence == completed && completed < state->movemessages &&
			state->ackedmovemessages == baseline_ack &&
			!memcmp (&baseline, &state->entities[state->viewentity].netstate, sizeof (baseline)));
		float origin_error = 0, velocity_error = 0;
		for (int axis = 0; axis < 3; ++axis)
		{
			origin_error = fmaxf (origin_error, fabsf (replay.origin[axis] - peer->edict->v.origin[axis]));
			velocity_error = fmaxf (velocity_error, fabsf (replay.velocity[axis] - peer->edict->v.velocity[axis]));
		}
		qboolean ground = ((int)peer->edict->v.flags & FL_ONGROUND) != 0;
		maximum_height = fmaxf (maximum_height, peer->edict->v.origin[2] - baseline.origin[2]);
		if (slope_qc_jump_accepted)
		{
			last_accepted_frame = frame;
			if (ground || peer->edict->v.velocity[2] <= 0) ++jump_contract_errors;
		}
		/* The next world frame must not resurrect support immediately after
 * a confirmed launch. Exact generic QC impulse timing is not negotiated. */
		if (jumping && frame == last_accepted_frame + 1 && ground) ++jump_contract_errors;
		if (jumping >= 3)
		{
			if (accepted_jumps == 1 && !ground) saw_air = true;
			if (accepted_jumps == 1 && saw_air && ground && !landed_on_slope)
			{
				vec3_t down;
				VectorCopy (peer->edict->v.origin, down);
				down[2] -= 2;
				trace_t support = SV_Move (peer->edict->v.origin, peer->edict->v.mins,
					peer->edict->v.maxs, down, MOVE_NORMAL, peer->edict);
				landed_on_slope = !support.startsolid && !support.allsolid && support.fraction < 1 &&
					support.ent == EDICT_NUM (0) && peer->edict->v.waterlevel == 0 &&
					DotProduct (support.plane.normal, start->normal) > .999f &&
					((int)peer->edict->v.flags & FL_JUMPRELEASED);
				printf ("SLOPE_LANDING case=%s frame=%d completed=%d same_dry_world_slope=%d released=%d normal=%.6f,%.6f,%.6f\n",
					name, frame, completed, landed_on_slope,
					((int)peer->edict->v.flags & FL_JUMPRELEASED) != 0,
					support.plane.normal[0], support.plane.normal[1], support.plane.normal[2]);
			}
			if (accepted_jumps == 2 && slope_qc_jump_accepted && !ground && peer->edict->v.velocity[2] > 0)
				rejump_air = true;
		}
		/* Match the existing same-sequence private shadow diagnostic bounds.
 * Baseline and command equality above are independently exact. */
		qboolean mismatch = origin_error > .05f || velocity_error > .25f || replay.onground != ground;
		mismatches += mismatch;
		printf ("SLOPE_JUMP case=%s jump=%d frame=%d completed=%d qc_buttons=%u accepted=%d pre_flags=%d post_pre_flags=%d pre_z=%.9g post_pre_z=%.9g native_released=%d replay_held=%d server_qc_owner=%d height=%.9g/%.9g ground=%d/%d\n",
			name, jumping, frame, completed, shared_qc_buttons, slope_qc_jump_accepted,
			slope_pre_flags, slope_post_pre_flags, slope_pre_z, slope_post_pre_z,
			((int)peer->edict->v.flags & FL_JUMPRELEASED) != 0, slope_replay_jump_held,
			slope_server_jump_owner, replay.origin[2] - baseline.origin[2],
			peer->edict->v.origin[2] - baseline.origin[2], replay.onground, ground);
		printf ("SLOPE_SAMPLE case=%s jump=%d client_ms=%d server_ms=25 frame=%d baseline=%d completed=%d heads=%d queued=%u axis=%g buttons=%u mismatch=%d origin_error=%.9g velocity_error=%.9g ground=%d/%d replay_origin=%.9g,%.9g,%.9g server_origin=%.9g,%.9g,%.9g replay_velocity=%.9g,%.9g,%.9g server_velocity=%.9g,%.9g,%.9g speed=%.9g/%.9g\n",
			name, jumping, msec, frame, baseline_ack, completed, completed - before,
			peer->private_cmd_queue_count, peer->private_pmove_last_cmd.forwardmove,
			peer->private_pmove_last_cmd.buttons, mismatch, origin_error, velocity_error,
			replay.onground, ground, replay.origin[0], replay.origin[1], replay.origin[2],
			peer->edict->v.origin[0], peer->edict->v.origin[1], peer->edict->v.origin[2],
			replay.velocity[0], replay.velocity[1], replay.velocity[2],
			peer->edict->v.velocity[0], peer->edict->v.velocity[1], peer->edict->v.velocity[2],
			hypotf (replay.velocity[0], replay.velocity[1]),
			hypotf (peer->edict->v.velocity[0], peer->edict->v.velocity[1]));
		if (jumping >= 3 && rejump_air && frame >= second_press + 2 &&
			!shared_qc_buttons && ((int)peer->edict->v.flags & FL_JUMPRELEASED)) break;
	}
	assert (multihead && fabs (shared_qc_pre_seconds - frames_done * .025) < .000001 &&
		fabs (shared_qc_post_seconds - frames_done * .025) < .000001);
	shared_qc_observed_player = NULL;
	liquid_after_qc_composition = NULL;
	if (jumping >= 3)
	{
		printf ("SLOPE_REJUMP case=%s client_ms=%d frames=%d accepted_jumps=%u airborne=%d landed_on_slope=%d second_airborne=%d second_press_frame=%d held=%d released=%d\n",
			name, msec, frames_done, accepted_jumps, saw_air, landed_on_slope, rejump_air,
			second_press, slope_replay_jump_held, ((int)peer->edict->v.flags & FL_JUMPRELEASED) != 0);
		if (!saw_air || !landed_on_slope || !rejump_air || accepted_jumps != 2 ||
			shared_qc_buttons || !((int)peer->edict->v.flags & FL_JUMPRELEASED))
		{
			fprintf (stderr, "SLOPE_MISSING_LANDING_REJUMP actual dry slope landing and second accepted QC launch not established\n");
			return UINT_MAX;
		}
	}
	if (jumping && !accepted_jumps)
	{
		/* The moving uphill press can expose the regression by being rejected:
 * server lost ground before QC saw the press, while replay predicts takeoff.
 * Do not label that an accepted jump followed by a ground snap. */
		printf ("SLOPE_QC_JUMP_REJECTED case=%s; no accepted QC launch, see pre/post flags\n", name);
		++jump_contract_errors;
	}
	if (jumping && maximum_height < 2) ++jump_contract_errors;
	GapSnapshot (peer, state);
	assert (state->ackedmovemessages == peer->private_completed_move &&
		state->move_snapshot_ack == peer->private_completed_move && state->move_ack_prediction_allowed);
	printf ("SLOPE_CASE case=%s jump=%d client_ms=%d parity_mismatches=%u trajectory_mismatches=%u jump_contract_errors=%u multihead_frames=%u accepted_jumps=%u maximum_height=%.9g exact_baseline_commands=1 vr_command=%d\n",
		name, jumping, msec, jumping ? 0 : mismatches, mismatches, jump_contract_errors,
		multihead, accepted_jumps, maximum_height, slope_vr_commands);
	return jumping ? jump_contract_errors : mismatches;
}

/* Wet risk probe: real QC, water and admitted commands. This
 * validates authoritative swim impulse/airborne support, not exact arbitrary
 * QC client replay or a shoreline/waterjump traversal. */
static unsigned RunSlopeSwim (client_t *peer, client_state_t *state)
{
	vec3_t wet;
	qboolean found = FindLiquidPosition (peer->edict, CONTENTS_WATER, 3, wet) ||
		FindLiquidPosition (peer->edict, CONTENTS_WATER, 2, wet);
	if (!found)
	{
		puts ("SLOPE_MISSING_SWIM_GEOMETRY existing depth2/3 hull finder found no sample");
		return UINT_MAX;
	}
	PrepareLiquidCase (peer, state, wet, 0);
	host_frametime = .025;
	realtime += host_frametime;
	LiquidSend (peer, state, 0, 0, 0, slope_vr_commands, 0);
	GapWorldFrame ();
	GapSnapshot (peer, state);
	float baseline_z = peer->edict->v.origin[2];
	unsigned errors = 0, impulses = 0;
	shared_qc_observed_player = peer->edict;
	shared_qc_pre = shared_qc_post = 0;
	shared_qc_pre_seconds = shared_qc_post_seconds = 0;
	liquid_after_qc_composition = SlopeObserveQC;
	for (int frame = 0; frame < 4; ++frame)
	{
		for (int command = 0; command < 3; ++command)
		{
			host_frametime = .008;
			realtime += host_frametime;
			LiquidSend (peer, state, BUTTON_JUMP, 0, 0, slope_vr_commands, 0);
		}
		SlopeCheckCommands (peer, state);
		slope_pre_flags = (int)peer->edict->v.flags;
		slope_pre_z = peer->edict->v.velocity[2];
		host_frametime = .025;
		GapWorldFrame ();
		qboolean ground = ((int)peer->edict->v.flags & FL_ONGROUND) != 0;
		if (shared_qc_buttons == BUTTON_JUMP && slope_post_pre_z > 0) ++impulses;
		if (!peer->active || peer->edict->v.waterlevel < 2 || ground || peer->edict->v.velocity[2] <= 0) ++errors;
		printf ("SLOPE_SWIM frame=%d completed=%d water=%g ground=%d buttons=%u qc_z=%.9g server_z=%.9g height=%.9g released=%d\n",
			frame, peer->private_completed_move, peer->edict->v.waterlevel, ground, shared_qc_buttons,
			slope_post_pre_z, peer->edict->v.velocity[2], peer->edict->v.origin[2] - baseline_z,
			((int)peer->edict->v.flags & FL_JUMPRELEASED) != 0);
		GapSnapshot (peer, state);
		assert (state->ackedmovemessages == peer->private_completed_move && state->move_snapshot_ack == peer->private_completed_move);
	}
	shared_qc_observed_player = NULL;
	liquid_after_qc_composition = NULL;
	assert (shared_qc_pre == 4 && shared_qc_post == 4 &&
		fabs (shared_qc_pre_seconds - .100) < .000001 && fabs (shared_qc_post_seconds - .100) < .000001);
	if (!impulses || peer->edict->v.origin[2] <= baseline_z) ++errors;
	printf ("SLOPE_SWIM_CONTRACT errors=%u qc_positive_impulses=%u real_water=1 exact_client_replay=unverified\n", errors, impulses);
	return errors;
}

int main (int argc, char **argv)
{
	client_t *peers[2];
	client_state_t *states[2];
	int msec = 8;
	const char *map = "start";
	for (int i = 1; i + 1 < argc; ++i)
	{
		if (!strcmp (argv[i], "-fixturemsec")) msec = atoi (argv[i + 1]);
		if (!strcmp (argv[i], "-fixturemap")) map = argv[i + 1];
	}
	assert (msec == 4 || msec == 8 || msec == 16);
	setvbuf (stdout, NULL, _IOLBF, 0);
	StartLiquidPeers (argc, argv, map, 25, peers, states);
	slope_vr_commands = COM_CheckParm ("-vrcommand") != 0;
	printf ("SLOPE_PROGRAM map=%s bytes=%d stock=%d q30=%d runclientcommand=%d vr_command=%d sha256=",
		map, qcvm->progssize, SV_PrivateWalkTrialStockProgram (), SV_PrivateWalkTrialQ30Program (),
		!!qcvm->extfuncs.SV_RunClientCommand, slope_vr_commands);
	for (unsigned i = 0; i < sizeof (qcvm->progssha256); ++i) printf ("%02x", qcvm->progssha256[i]);
	putchar ('\n');
	if (SV_PrivateWalkTrialStockProgram () || SV_PrivateWalkTrialQ30Program () || qcvm->extfuncs.SV_RunClientCommand)
	{
		puts ("SLOPE_MISSING_GENERIC_OWNER loaded program has a different movement contract; use its existing native fixture");
		return 77;
	}
	assert (!SV_PrivateWalkTrialStockProgram () && !SV_PrivateWalkTrialQ30Program () &&
		!qcvm->extfuncs.SV_RunClientCommand && peers[0]->private_pmove_walk_selected);
	if (COM_CheckParm ("-swimonly"))
	{
		unsigned errors = RunSlopeSwim (peers[0], states[0]);
		if (errors == UINT_MAX) return 77;
		return errors && !COM_CheckParm ("-diagnostic") ? 1 : 0;
	}
	slope_start_t starts[3];
	if (!FindSlopeStarts (peers[0]->edict, &starts[0], &starts[1])) return 77;
	const char *names[] = {"dry-flat", "uphill", "downhill"};
	unsigned mismatches = 0;
	if (!COM_CheckParm ("-landingonly"))
	{
		for (unsigned scenario = 0; scenario < countof (starts); ++scenario)
			for (int jumping = 0; jumping < 2; ++jumping)
			{
				unsigned errors = RunSlopeCase (peers[0], states[0], &starts[scenario], names[scenario], msec, jumping);
				if (errors == UINT_MAX) return 77;
				mismatches += errors;
			}
		unsigned rest_errors = RunSlopeCase (peers[0], states[0], &starts[1], "uphill-rest-press", msec, 2);
		if (rest_errors == UINT_MAX) return 77;
		mismatches += rest_errors;
	}
	unsigned landing_errors = RunSlopeCase (peers[0], states[0], &starts[1], "uphill-landing-rejump", 16, 3);
	if (landing_errors == UINT_MAX) return 77;
	mismatches += landing_errors;
	unsigned running_errors = RunSlopeCase (peers[0], states[0], &starts[1], "uphill-running-landing-rejump", 16, 4);
	if (running_errors == UINT_MAX) return 77;
	mismatches += running_errors;
	printf ("SLOPE_RECONCILIATION_%s client_ms=%d violations=%u actual generic QC/BSP; captured delivery, prepared commands, committed replay; jump trajectory approximation reported separately\n",
		mismatches ? "DIVERGED" : "PASSED", msec, mismatches);
	return mismatches && !COM_CheckParm ("-diagnostic") ? 1 : 0;
}
