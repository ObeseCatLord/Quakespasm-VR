/* Real stock BSP/QC crossing and batching through the admitted production
 * command/snapshot/live-replay path. Prepared starts/velocities/input and
 * captured delivery are explicit seams; no contents/collision stubs. */
#define STOCK_LIQUID_FIXTURE_ENTRY ImportedStockLiquidMain
#include "stock_liquid_native_fixture.c"

static void PrimeTransitStart (client_t *peer, client_state_t *state,
	const vec3_t position, float velocity_z)
{
	PrepareLiquidCase (peer, state, position, 0);
	realtime += host_frametime;
	LiquidSend (peer, state, 0, 0, 0, false, 0);
	GapWorldFrame ();
	/* Complete the real recovery command, then relocate through existing
 * owners without reopening a fence. Initial velocity is prepared. */
	host_client = peer; sv_player = peer->edict;
	char setpos[128];
	q_snprintf (setpos, sizeof (setpos), "setpos %.6f %.6f %.6f 0 0 0",
		position[0], position[1], position[2]);
	Cmd_ExecuteString (setpos, src_client);
	Cmd_ExecuteString ("noclip 0", src_client);
	peer->edict->v.flags = FL_CLIENT | FL_JUMPRELEASED;
	Cmd_ExecuteString ("notarget 1", src_client);
	VectorSet (peer->edict->v.velocity, 0, 0, velocity_z);
	SV_CheckWater (peer->edict);
	SV_LinkEdict (peer->edict, false);
	GapSnapshot (peer, state);
	assert (peer->private_input_phase == PRIVATE_INPUT_RUNNING &&
		state->move_ack_prediction_allowed == peer->private_pmove_walk_selected);
}

static void FindTransitSurface (edict_t *player, int contents, vec3_t dry, vec3_t wet)
{
	vec3_t shallow = {0};
	assert (FindLiquidPosition (player, contents, 1, shallow));
	entvars_t saved = player->v;
	qboolean found = false;
	for (int up = 0; up < 64 && !found; ++up)
	{
		VectorCopy (shallow, player->v.origin);
		player->v.origin[2] += up;
		SV_CheckWater (player);
		if (player->v.waterlevel || SV_TestEntityPosition (player)) continue;
		VectorCopy (player->v.origin, dry);
		player->v.origin[2] -= 2;
		SV_CheckWater (player);
		if (player->v.waterlevel != 1 || player->v.watertype != contents ||
			SV_TestEntityPosition (player)) continue;
		VectorCopy (player->v.origin, wet);
		trace_t clear = SV_Move (wet, player->v.mins, player->v.maxs, dry, MOVE_NORMAL, player);
		found = !clear.startsolid && !clear.allsolid && clear.fraction == 1;
	}
	if (found)
	{
		/* Locate the actual surface, then leave a 1/8-unit margin on either
 * side for the inherited PMove position nudge. No contents are invented. */
		for (int iteration = 0; iteration < 12; ++iteration)
		{
			VectorCopy (dry, player->v.origin);
			player->v.origin[2] = (dry[2] + wet[2]) * .5f;
			SV_CheckWater (player);
			if (player->v.waterlevel == 0) dry[2] = player->v.origin[2];
			else wet[2] = player->v.origin[2];
		}
		dry[2] += .125f;
		wet[2] -= .125f;
	}
	player->v = saved;
	assert (found);
}

static void RunCrossing (client_t *peer, client_state_t *state, const vec3_t start,
	float velocity_z, qboolean enter, int frames, qboolean vr)
{
	PrimeTransitStart (peer, state, start, velocity_z);
	const int baseline_ack = state->ackedmovemessages;
	for (int frame = 0; frame < frames; ++frame)
	{
		realtime += host_frametime;
		LiquidSend (peer, state, 0, 0, 0, vr, 0);
		vec3_t predicted, velocity;
		assert (LiquidPendingReplay (state, predicted, velocity));
		GapWorldFrame ();
		assert (peer->active && peer->edict->v.health > 0);
		for (int axis = 0; axis < 3; ++axis)
		{
			assert (fabsf (predicted[axis] - peer->edict->v.origin[axis]) <
				.125f * host_frametime * (frame + 1) + .001f);
			assert (fabsf (velocity[axis] - peer->edict->v.velocity[axis]) < .126f);
		}
		/* The prior complete snapshot stays the seed; history genuinely grows
 * across world frames, rather than being replaced with an empty replay. */
		assert (state->ackedmovemessages == baseline_ack &&
			state->movemessages - 1 > baseline_ack);
	}
	assert (enter ? peer->edict->v.waterlevel > 0 : peer->edict->v.waterlevel == 0);
	GapSnapshot (peer, state);
	assert (state->ackedmovemessages == peer->private_completed_move &&
		state->ackedmovemessages > baseline_ack && state->move_ack_prediction_allowed);
	LiquidDisposablePreview (state);
	printf ("STOCK_LIQUID_TRANSIT enter=%d frames=%d vr=%d water=%g ack=%d->%d\n",
		enter, frames, vr, peer->edict->v.waterlevel, baseline_ack, state->ackedmovemessages);
}

static void RunQueuedCrossing (client_t *peer, client_state_t *state,
	const vec3_t start, float velocity_z, qboolean vr)
{
	PrimeTransitStart (peer, state, start, velocity_z);
	const double command_frame = host_frametime;
	const int commands = fmin (3, floor (.250 / command_frame));
	assert (commands >= 2);
	const int baseline_ack = state->ackedmovemessages;
	for (int command = 0; command < commands; ++command)
	{
		realtime += command_frame;
		LiquidSend (peer, state, 0, 0, 0, vr, 0);
		assert (peer->private_completed_move == baseline_ack &&
			peer->private_cmd_queue_count == (unsigned)(command + 1));
	}
	vec3_t predicted, velocity;
	assert (LiquidPendingReplay (state, predicted, velocity));
	/* A prepared long world frame supplies the existing credit owner;
	 * command durations and all per-command QC/solver work remain actual. */
	host_frametime = command_frame * commands;
	GapWorldFrame ();
	host_frametime = command_frame;
	assert (!peer->private_cmd_queue_count && peer->edict->v.waterlevel > 0 &&
		peer->private_completed_move == state->movemessages - 1);
	for (int axis = 0; axis < 3; ++axis)
	{
		assert (fabsf (predicted[axis] - peer->edict->v.origin[axis]) <
			.125f * command_frame * commands + .001f);
		assert (fabsf (velocity[axis] - peer->edict->v.velocity[axis]) < .126f);
	}
	GapSnapshot (peer, state);
	assert (state->ackedmovemessages == peer->private_completed_move &&
		state->move_ack_prediction_allowed);
	LiquidDisposablePreview (state);
	printf ("STOCK_LIQUID_QUEUED_TRANSIT commands=%d water=%g ack=%d->%d\n",
		commands, peer->edict->v.waterlevel, baseline_ack, state->ackedmovemessages);
}

static void CheckPermissionExclusions (client_t *peer, client_state_t *state)
{
	float deadline = peer->edict->v.teleport_time, flags = peer->edict->v.flags;
	peer->private_pmove_pusher_interaction = true;
	GapSnapshot (peer, state);
	assert (!state->move_ack_prediction_allowed);
	peer->private_pmove_pusher_interaction = false;
	peer->edict->v.teleport_time = NAN;
	GapSnapshot (peer, state);
	assert (!state->move_ack_prediction_allowed);
	peer->edict->v.teleport_time = qcvm->time + .7f;
	GapSnapshot (peer, state);
	assert (!state->move_ack_prediction_allowed);
	peer->edict->v.teleport_time = deadline;
	peer->edict->v.flags = (int)flags | FL_WATERJUMP;
	GapSnapshot (peer, state);
	assert (!state->move_ack_prediction_allowed); // no private owned timer
	peer->edict->v.flags = flags;
	peer->private_pmove_waterjump_secs = .5f;
	GapSnapshot (peer, state);
	assert (!state->move_ack_prediction_allowed); // owned timer requires flag
	peer->private_pmove_waterjump_secs = 0;
	GapSnapshot (peer, state);
	assert (state->move_ack_prediction_allowed);
	puts ("STOCK_LIQUID_PERMISSION_NEGATIVES_PASSED prepared predicate inputs; actual full snapshot metadata");
}

static void RunDrowning (client_t *peer, client_state_t *state, const vec3_t deep)
{
	PrimeTransitStart (peer, state, deep, 0);
	/* Expired air/pain clocks are prepared; actual pinned WaterMove owns
 * damage cadence, death and its native continuation. No health is forced. */
	eval_t *air = GetEdictFieldValue (peer->edict, ED_FindFieldOffset ("air_finished"));
	eval_t *pain = GetEdictFieldValue (peer->edict, ED_FindFieldOffset ("pain_finished"));
	assert (air && pain);
	air->_float = pain->_float = qcvm->time - 1;
	int damage_events = 0;
	for (int frame = 0; frame < 1000; ++frame)
	{
		float health = peer->edict->v.health;
		realtime += host_frametime;
		LiquidSend (peer, state, 0, 0, 0, false, 0);
		GapWorldFrame ();
		GapSnapshot (peer, state);
		if (peer->edict->v.health < health)
		{
			damage_events++;
			printf ("STOCK_DROWN_DAMAGE frame=%d time=%g health=%g->%g depth=%g\n",
				frame, qcvm->time, health, peer->edict->v.health, peer->edict->v.waterlevel);
		}
		if (peer->edict->v.health <= 0 || peer->edict->v.deadflag != DEAD_NO)
		{
			cl = *state;
			vec3_t origin;
			assert (damage_events > 0 && !state->move_ack_prediction_allowed &&
				!peer->private_pmove_waterjump_secs &&
				!CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], origin));
			*state = cl;
			puts ("STOCK_DROWN_TERMINAL_PASSED actual environmental QC/native continuation/full snapshot replay denial");
			return;
		}
		assert (state->move_ack_prediction_allowed == peer->private_pmove_walk_selected);
		LiquidDisposablePreview (state);
	}
	assert (!"prepared expired-air drowning did not reach terminal state");
}

/* Deliver the real reliable pause service but withhold all snapshots. */
static void LiquidPauseService (client_t *peer, client_state_t *state)
{
	host_client = peer; sv_player = peer->edict;
	SZ_Clear (&peer->message);
	Cmd_ExecuteString ("pause", src_client);
	cl = *state;
	net_message = peer->message;
	CL_ParseServerMessage ();
	assert (msg_readcount == net_message.cursize && cl.paused == sv.paused);
	*state = cl;
	SZ_Clear (&peer->message);
	SV_RunClients ();
}

static int LiquidCaptureSnapshot (client_t *peer, byte *bytes, size_t capacity)
{
	host_client = peer; sv_player = peer->edict;
	SV_PresendClientDatagram (peer);
	net_message.data = bytes; net_message.maxsize = capacity;
	SZ_Clear (&net_message);
	SVFTE_WriteStats (peer, &net_message);
	assert (SVFTE_WritePrivateMoveStats (peer, &net_message));
	assert (SVFTE_WriteEntitiesToClient (peer, &net_message, capacity, false));
	return net_message.cursize;
}

static void RunLostPauseSnapshots (client_t *peer, client_state_t *state,
	const vec3_t position, float yaw, qboolean ledge)
{
	PrepareLiquidCase (peer, state, position, yaw);
	realtime += host_frametime;
	LiquidSend (peer, state, 0, 0, 0, false, 0);
	GapWorldFrame ();
	GapSnapshot (peer, state);
	const int completed = peer->private_completed_move;
	const float timer = peer->private_pmove_waterjump_secs;
	assert (peer->edict->v.waterlevel > 0 && state->move_ack_prediction_allowed);
	if (ledge) assert (timer > 0);
	byte stale[NET_MAXMESSAGE];
	/* A real semantic relocation can publish a newer pre-pause generation
	 * without the client receiving it. A completed flag alone cannot make
	 * that delayed packet the resume fence. Keep the active-ledge case intact. */
	if (!ledge)
	{
		host_client = peer; sv_player = peer->edict;
		char setpos[128];
		q_snprintf (setpos, sizeof (setpos), "setpos %.6f %.6f %.6f 0 %.6f 0",
			position[0], position[1], position[2], yaw);
		Cmd_ExecuteString (setpos, src_client);
		Cmd_ExecuteString ("noclip 0", src_client);
		assert (peer->private_move_discontinuity_epoch != state->move_ack_discontinuity_epoch);
	}
	const int stale_length = LiquidCaptureSnapshot (peer, stale, sizeof (stale));
	realtime += host_frametime;
	LiquidSend (peer, state, 0, 0, 0, false, 0);
	vec3_t replay, velocity;
	assert (LiquidPendingReplay (state, replay, velocity) && peer->private_cmd_queue_count);
	LiquidPauseService (peer, state);
	assert (sv.paused && !peer->private_cmd_queue_count &&
		peer->private_completed_move == completed && peer->private_pmove_waterjump_secs == timer);
	cl = *state;
	assert (!CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], replay));
	*state = cl;
	LiquidPauseService (peer, state);
	assert (!sv.paused && peer->private_input_phase == PRIVATE_INPUT_AWAIT_MARKER);
	cl = *state;
	qboolean reopened = CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], replay);
	*state = cl;
	net_message.data = stale; net_message.cursize = stale_length;
	CL_ParseServerMessage ();
	qboolean stale_reopened = CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], replay);
	*state = cl;
	printf ("STOCK_LOST_PAUSE_SNAPSHOTS ledge=%d reopened=%d stale_reopened=%d\n",
		ledge, reopened, stale_reopened);
	assert (!reopened && !stale_reopened && state->move_ack_resume_pending);
	/* The reliable unpause pair now supplies the actual new pending fence,
	 * even though no owner snapshot has arrived. Its legal marker can resume
	 * input, but live replay still needs a completed authoritative snapshot. */
	realtime += host_frametime;
	LiquidSend (peer, state, 0, 0, 0, false, 0);
	assert (peer->private_input_phase == PRIVATE_INPUT_AWAIT_COMPLETION &&
		peer->private_cmd_queue_count == 1 && peer->private_completed_move == completed);
	assert (state->move_resume_marker_epoch_valid && state->move_resume_marker_first_sequence > completed &&
		state->move_resume_marker_epoch_sent == peer->private_move_discontinuity_epoch);
	GapSnapshot (peer, state); // awaiting completion, not permission to replay
	assert (!state->move_ack_prediction_allowed);
	GapWorldFrame ();
	GapSnapshot (peer, state);
	cl = *state;
	assert (peer->private_input_phase == PRIVATE_INPUT_RUNNING &&
		state->ackedmovemessages == peer->private_completed_move &&
		state->ackedmovemessages > completed && state->move_ack_prediction_allowed &&
		CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], replay));
	if (ledge) assert (peer->private_pmove_waterjump_secs > 0);
	*state = cl;
}

static void RunAheadPauseSnapshots (client_t *peer, client_state_t *state,
	const vec3_t position, float yaw, qboolean ledge)
{
	PrepareLiquidCase (peer, state, position, yaw);
	realtime += host_frametime;
	LiquidSend (peer, state, 0, 0, 0, false, 0);
	GapWorldFrame (); GapSnapshot (peer, state);
	if (ledge) assert (peer->private_pmove_waterjump_secs > 0);
	byte paused[NET_MAXMESSAGE], resumed[NET_MAXMESSAGE];
	unsigned short epoch = 0;
	for (int delivery = 0; delivery < 2; ++delivery)
	{
		host_client = peer; sv_player = peer->edict;
		SZ_Clear (&peer->message);
		Cmd_ExecuteString ("pause", src_client);
		const int paused_length = peer->message.cursize;
		memcpy (paused, peer->message.data, paused_length);
		assert (peer->private_input_phase == PRIVATE_INPUT_SUSPENDED);
		SZ_Clear (&peer->message);
		/* No intermediate tick: capture each actual toggle's generation. */
		Cmd_ExecuteString ("pause", src_client);
		const int resumed_length = peer->message.cursize;
		memcpy (resumed, peer->message.data, resumed_length);
		SZ_Clear (&peer->message);
		assert (peer->private_input_phase == PRIVATE_INPUT_AWAIT_MARKER);
		GapSnapshot (peer, state); // pending precedes this distinct reliable pair
		epoch = state->move_ack_discontinuity_epoch;
		if (delivery)
		{
			/* Complete before delivering either service, rather than reuse
			 * an already delivered pair as a duplicate-only proof. */
			realtime += host_frametime;
			LiquidSend (peer, state, 0, 0, 0, false, 0);
			GapWorldFrame (); GapSnapshot (peer, state);
		}
		cl = *state;
		net_message.data = paused; net_message.cursize = paused_length;
		CL_ParseServerMessage ();
		assert (cl.paused && cl.move_ack_discontinuity_epoch == epoch);
		net_message.data = resumed; net_message.cursize = resumed_length;
		CL_ParseServerMessage ();
		assert (!cl.paused && cl.move_ack_discontinuity_epoch == epoch);
		*state = cl;
		if (!delivery)
		{
			assert (state->move_ack_resume_pending && !state->move_resume_marker_epoch_valid);
			realtime += host_frametime;
			LiquidSend (peer, state, 0, 0, 0, false, 0);
			GapWorldFrame (); GapSnapshot (peer, state);
		}
		cl = *state;
		vec3_t origin;
		assert (peer->private_input_phase == PRIVATE_INPUT_RUNNING &&
			state->ackedmovemessages == peer->private_completed_move &&
			state->move_ack_prediction_allowed && CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], origin));
		*state = cl;
		printf ("STOCK_AHEAD_PAUSE_SNAPSHOTS ledge=%d delivery=%d epoch=%u ack=%d\n",
			ledge, delivery, epoch, state->ackedmovemessages);
	}
	/* This genuinely new pause shares the completed epoch; it must fence,
	 * unlike the delayed old pause above. */
	LiquidPauseService (peer, state);
	assert (sv.paused && !state->move_ack_prediction_allowed && state->move_ack_resume_pending);
	LiquidPauseService (peer, state);
	assert (!sv.paused && state->move_ack_discontinuity_epoch == (unsigned short)(epoch + 1));
	realtime += host_frametime;
	LiquidSend (peer, state, 0, 0, 0, false, 0);
	GapWorldFrame (); GapSnapshot (peer, state);
	cl = *state;
	vec3_t origin;
	assert (state->move_ack_prediction_allowed && CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], origin));
	*state = cl;
}

static void RunPauseRecipientChecks (client_t *private_peer, client_t *public_peer)
{
	client_t *third = SpawnPeer (2, modern_offer, QSVR_PROTOCOL_PINNED);
	assert (third->private_pmove_walk_selected && third->private_move_discontinuity_epoch !=
		private_peer->private_move_discontinuity_epoch);
	for (int toggle = 0; toggle < 2; ++toggle)
	{
		for (int slot = 0; slot < svs.maxclients; ++slot) SZ_Clear (&svs.clients[slot].message);
		host_client = private_peer; sv_player = private_peer->edict;
		Cmd_ExecuteString ("pause", src_client);
		client_t *selected[] = {private_peer, third};
		for (int slot = 0; slot < 2; ++slot)
		{
			client_t *peer = selected[slot];
			assert (peer->message.cursize >= 12);
			byte *pair = peer->message.data + peer->message.cursize - 12;
			assert (pair[0] == QSVR_SVC_MOVEACK && pair[10] == svc_setpause &&
				pair[11] == sv.paused && (pair[7] | (pair[8] << 8)) == peer->private_move_discontinuity_epoch);
		}
		assert (public_peer->message.cursize >= 2);
		byte *native = public_peer->message.data + public_peer->message.cursize - 2;
		assert (native[0] == svc_setpause && native[1] == sv.paused);
	}
	assert (!sv.paused);
	puts ("STOCK_PAUSE_RECIPIENTS_PASSED actual third-peer admission/distinct epochs/native public pause bytes/rapid toggles");
}

static void RunWetRecovery (client_t *peer, client_state_t *state,
	const vec3_t position, float yaw, qboolean ledge)
{
	PrepareLiquidCase (peer, state, position, yaw);
	realtime += host_frametime;
	LiquidSend (peer, state, 0, 0, 0, false, 0);
	GapWorldFrame ();
	GapSnapshot (peer, state);
	assert (peer->edict->v.waterlevel > 0 && state->move_ack_prediction_allowed);
	if (ledge) assert (peer->private_pmove_waterjump_secs > 0);
	const int completed = peer->private_completed_move;
	const float timer = peer->private_pmove_waterjump_secs;
	StartupPauseCommand (peer, state);
	/* The pause service fences the completed generation immediately, even
	 * when paused/recovery snapshots arrive late. Resume publishes a fresh
	 * server fence below; a zero first sequence cannot produce an old marker. */
	cl = *state;
	vec3_t paused_replay;
	assert (sv.paused && cl.paused && !cl.move_ack_prediction_allowed &&
		peer->private_input_phase == PRIVATE_INPUT_SUSPENDED &&
		!CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], paused_replay) &&
		peer->private_completed_move == completed && peer->private_pmove_waterjump_secs == timer);
	*state = cl;
	StartupPauseCommand (peer, state);
	assert (!sv.paused && state->move_ack_resume_pending &&
		!state->move_ack_prediction_allowed && peer->private_completed_move == completed);
	for (int stage = 0; stage < 2; ++stage)
	{
		if (stage == 1)
		{
			realtime += 1.25;
			SV_RunClients ();
			GapSnapshot (peer, state);
			assert (peer->private_input_phase == PRIVATE_INPUT_AWAIT_MARKER &&
				!state->move_ack_prediction_allowed);
		}
		realtime += host_frametime;
		LiquidSend (peer, state, 0, 0, 0, false, 0);
		GapWorldFrame ();
		GapSnapshot (peer, state);
		assert (peer->active && peer->private_input_phase == PRIVATE_INPUT_RUNNING &&
			state->move_ack_prediction_allowed && state->ackedmovemessages == peer->private_completed_move);
		if (ledge) assert (peer->private_pmove_waterjump_secs > 0 &&
			state->statsf[STAT_PRIVATE_WATERJUMP_SECS] == peer->private_pmove_waterjump_secs);
		cl = *state;
		vec3_t replay;
		assert (CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], replay));
		*state = cl;
		printf ("STOCK_WET_RECOVERY ledge=%d stage=%d water=%g timer=%g ack=%d\n",
			ledge, stage, peer->edict->v.waterlevel, peer->private_pmove_waterjump_secs,
			peer->private_completed_move);
	}
}

int main (int argc, char **argv)
{
	setvbuf (stdout, NULL, _IONBF, 0);
	const char *map = "e1m1", *liquid = "water";
	int command_msec = 25;
	for (int arg = 1; arg + 1 < argc; ++arg)
	{
		if (!strcmp (argv[arg], "-fixturemap")) map = argv[arg + 1];
		if (!strcmp (argv[arg], "-fixtureliquid")) liquid = argv[arg + 1];
		if (!strcmp (argv[arg], "-fixturemsec")) command_msec = atoi (argv[arg + 1]);
	}
	assert (command_msec >= 1 && command_msec <= 125 &&
		(!strcmp (liquid, "water") || !strcmp (liquid, "slime") || !strcmp (liquid, "lava")));
	int contents = !strcmp (liquid, "water") ? CONTENTS_WATER :
		!strcmp (liquid, "slime") ? CONTENTS_SLIME : CONTENTS_LAVA;
	client_t *peers[2]; client_state_t *states[2];
	StartLiquidPeers (argc, argv, map, command_msec, peers, states);
	if (COM_CheckParm ("-drown-only"))
	{
		vec3_t deep = {0};
		assert (FindLiquidPosition (peers[0]->edict, contents, 3, deep));
		RunDrowning (peers[0], states[0], deep);
		goto finish;
	}
	assert (peers[0]->private_pmove_walk_selected);
	vec3_t dry, wet, deep;
	FindTransitSurface (peers[0]->edict, contents, dry, wet);
	assert (FindLiquidPosition (peers[0]->edict, contents, 3, deep));
	printf ("STOCK_TRANSIT_GEOMETRY liquid=%s dry_z=%g wet_z=%g\n", liquid, dry[2], wet[2]);
	qboolean vr = COM_CheckParm ("-vr") != 0;
	float impulse = fminf (1800, fmaxf (200, 4.0f / host_frametime + 40));
	RunCrossing (peers[0], states[0], dry, -impulse, true, 1, vr);
	RunCrossing (peers[0], states[0], wet, impulse, false, 1, vr);
	RunCrossing (peers[0], states[0], dry, -impulse, true, 3, vr);
	RunCrossing (peers[0], states[0], wet, impulse, false, 3, vr);
	RunQueuedCrossing (peers[0], states[0], dry, -impulse, vr);
	CheckPermissionExclusions (peers[0], states[0]);
	RunLostPauseSnapshots (peers[0], states[0], deep, 0, false);
	RunAheadPauseSnapshots (peers[0], states[0], deep, 0, false);
	RunWetRecovery (peers[0], states[0], deep, 0, false);
	if (COM_CheckParm ("-ledge-recovery"))
	{
		vec3_t ledge = {0}; float yaw = 0;
		assert (FindLiquidJumpPosition (peers[0]->edict, contents, ledge, &yaw));
		RunLostPauseSnapshots (peers[0], states[0], ledge, yaw, true);
		RunAheadPauseSnapshots (peers[0], states[0], ledge, yaw, true);
		RunWetRecovery (peers[0], states[0], ledge, yaw, true);
	}
	RunPauseRecipientChecks (peers[0], peers[1]);
	if (COM_CheckParm ("-drown")) RunDrowning (peers[0], states[0], deep);
	puts ("STOCK_LIQUID_TRANSIT_COMPONENT_PASSED real BSP entry/exit/delayed snapshot/live history/preview; prepared starts and captured delivery");
finish:
	for (int slot = 0; slot < 2; ++slot)
	{
		Mem_Free (states[slot]->entities); Mem_Free (states[slot]->scores); Mem_Free (states[slot]);
	}
	return 0;
}
