/* Real stock QC, negotiated public/private owners, production send/receive,
 * native physics and snapshots. Unreliable transport and player skin uploads
 * are captured (the dedicated bootstrap has no renderer textures);
 * commands are synthetic, and client signon/resource state is prepared here.
 * This is not connected signon, upstream-client or OpenXR input evidence. */
#define main NegotiationFixtureMain
#include "negotiation_native_fixture.c"
#undef main

static byte captured[NET_MAXMESSAGE];
static int captured_length;
static int skin_uploads;
extern qboolean SV_ReadClientMessage (void);

void __wrap_R_TranslateNewPlayerSkin (int player)
{
	assert (player >= 0 && player < cl.maxclients);
	skin_uploads++;
}

int __wrap_NET_SendUnreliableMessage (qsocket_t *socket, sizebuf_t *message)
{
	assert (socket && message->cursize <= sizeof (captured));
	memcpy (captured, message->data, message->cursize);
	captured_length = message->cursize;
	return 1;
}

static client_t *SpawnPeer (int slot, const char *offer, unsigned profile)
{
	client_t *client = Negotiate (slot, offer, profile);
	sv_player = client->edict;
	Cmd_ExecuteString (slot ? "name public" : "name private", src_client);
	Cmd_ExecuteString ("spawn", src_client);
	Cmd_ExecuteString ("begin", src_client);
	assert (client->active && client->spawned && client->knowntoqc);
	assert (client->edict->v.health > 0 && !client->edict->free);
	assert (client->private_pmove_walk_selected == (profile && sv_private_pmove_walk.value));
	return client;
}

static void ReadPeerSnapshot (client_t *peer, byte *bytes, size_t capacity)
{
	/* Production SV_SendClientMessages installs this writer context. */
	host_client = peer;
	sv_player = peer->edict;
	SV_PresendClientDatagram (peer);
	net_message.data = bytes;
	net_message.maxsize = capacity;
	SZ_Clear (&net_message);
	if (SV_PrivateWalkTrialSelected (peer))
	{
		const char *failure = SV_PrivateWalkTrialAdmissionFailure (peer);
		if (failure) fprintf (stderr, "MIXED_ADMISSION_FAILURE %s health=%g dead=%g movetype=%g solid=%g\n",
			failure, peer->edict->v.health, peer->edict->v.deadflag,
			peer->edict->v.movetype, peer->edict->v.solid);
		assert (!failure);
	}
	SVFTE_WriteStats (peer, &net_message);
	if (SV_PrivateWalkTrialSelected (peer))
		assert (SVFTE_WritePrivateMoveStats (peer, &net_message));
	assert (SVFTE_WriteEntitiesToClient (peer, &net_message, capacity, false));
	CL_ParseServerMessage (); // commits stats/ACK/owner at the real message end
	assert (msg_readcount == net_message.cursize);
}

static void GapDeliver (client_t *peer, byte *bytes, int length)
{
	host_client = peer;
	sv_player = peer->edict;
	net_message.data = bytes;
	net_message.cursize = length;
	assert (SV_ReadClientMessage ());
}

static void GapSend (client_t *peer, client_state_t *state, unsigned buttons,
	unsigned impulse, float roomscale)
{
	usercmd_t command = {0};
	cl = *state;
	cls.netcon = peer->netconnection;
	cl.time = qcvm->time;
	command.servertime = cl.time;
	VectorCopy (cl.viewangles, command.viewangles);
	command.forwardmove = 100;
	command.buttons = buttons;
	command.impulse = impulse;
	command.vr_active = command.vr_handpos_relative = true;
	command.vr_handpos[1] = 16;
	command.vr_handpos[2] = 22;
	VectorCopy (command.viewangles, command.vr_handrot);
	command.vr_roomscalemove[1] = roomscale;
	captured_length = 0;
	CL_SendMove (&command);
	assert (captured_length);
	*state = cl;
}

static void GapSnapshot (client_t *peer, client_state_t *state)
{
	static byte bytes[NET_MAXMESSAGE];
	cl = *state;
	cls.netcon = peer->netconnection;
	ReadPeerSnapshot (peer, bytes, sizeof (bytes));
	*state = cl;
}

static void GapWorldFrame (void)
{
	pr_global_struct->frametime = host_frametime;
	SV_RunClients (); // real no-packet timeout boundary, with UDP disabled
	SV_Physics ();
	SV_FinishPrivateUsercmds ();
}

static void StartupPauseCommand (client_t *peer, client_state_t *state)
{
	host_client = peer;
	sv_player = peer->edict;
	SZ_Clear (&peer->message);
	Cmd_ExecuteString ("pause", src_client);
	cl = *state;
	net_message = peer->message;
	CL_ParseServerMessage (); // actual svc_setpause, no staged client pause bit
	assert (msg_readcount == net_message.cursize && cl.paused == sv.paused);
	*state = cl;
	SZ_Clear (&peer->message);
	SV_RunClients ();
	GapSnapshot (peer, state);
}

static void RunStartupPauseChecks (client_t *peer, client_state_t *state)
{
	assert (peer->private_pmove_walk_selected && state->movemessages == 0 &&
		peer->private_completed_move == 0);
	StartupPauseCommand (peer, state);
	assert (sv.paused && peer->private_input_phase == PRIVATE_INPUT_SUSPENDED &&
		state->movemessages == 0 && !state->move_ack_prediction_allowed);
	StartupPauseCommand (peer, state);
	assert (!sv.paused && peer->private_input_phase == PRIVATE_INPUT_AWAIT_MARKER &&
		state->move_ack_resume_pending && state->movemessages == 0);
	/* Bootstrap offers have already been consumed by real negotiation. */
	SZ_Clear (&cls.message);
	for (int sequence = 0; sequence <= 2; ++sequence)
	{
		usercmd_t command = {0};
		cl = *state;
		cls.netcon = peer->netconnection;
		cl.time = command.servertime = qcvm->time;
		VectorCopy (cl.viewangles, command.viewangles);
		command.forwardmove = 50;
		realtime += host_frametime;
		captured_length = 0;
		CL_SendMove (&command);
		*state = cl;
		assert (state->move_resume_marker_first_sequence == 2);
		if (sequence == 0)
		{
			assert (cls.message.cursize);
			GapDeliver (peer, cls.message.data, cls.message.cursize);
			SZ_Clear (&cls.message);
			assert (peer->private_input_phase == PRIVATE_INPUT_AWAIT_COMPLETION);
		}
		if (captured_length) GapDeliver (peer, captured, captured_length);
		if (sequence < 2)
			assert (!peer->private_cmd_queue_count && peer->private_completed_move == 0);
		GapWorldFrame ();
		GapSnapshot (peer, state);
		assert (peer->active && state->movemessages == sequence + 1 &&
			peer->private_completed_move == (sequence == 2 ? 2 : 0));
	}
	assert (peer->private_input_phase == PRIVATE_INPUT_RUNNING &&
		state->ackedmovemessages == 2 && state->move_ack_prediction_allowed);
	cl = *state;
	vec3_t replay;
	assert (CL_ReplayPlayerMovement (&cl.entities[1], replay));
	*state = cl;
	puts ("MIXED_STARTUP_PAUSE_PASSED actual admission/host pause/svc_setpause/send/marker/command completion/full snapshots/replay; captured delivery");
}

/* Extends the already admitted mixed-peer session; no injected selection or
 * test-only production timeout hook. Delivery is captured, not connected. */
static void RunArrivalGapChecks (client_t *peer, client_state_t *state)
{
	byte old_packet[NET_MAXMESSAGE];
	vec3_t origin, velocity;
	int completed, old_length, first_sequence, marker_length;
	unsigned short epoch;
	float jump_secs, waterjump_secs;

	assert (peer->private_input_phase == PRIVATE_INPUT_RUNNING &&
		peer->edict->v.movetype == MOVETYPE_WALK);
	completed = peer->private_completed_move;
	epoch = peer->private_move_discontinuity_epoch;
	realtime += host_frametime;
	GapSend (peer, state, BUTTON_ATTACK, 7, 3);
	GapDeliver (peer, captured, captured_length);
	assert (peer->private_cmd_queue_count && peer->private_completed_move == completed);
	VectorCopy (peer->edict->v.origin, origin);
	VectorCopy (peer->edict->v.velocity, velocity);
	jump_secs = peer->private_pmove_jump_secs;
	waterjump_secs = peer->private_pmove_waterjump_secs;
	realtime += 1.25;
	SV_RunClients ();
	assert (peer->active && peer->private_input_phase == PRIVATE_INPUT_AWAIT_MARKER &&
		peer->private_move_discontinuity_epoch == (unsigned short)(epoch + 1) &&
		peer->private_completed_move == completed && !peer->private_cmd_queue_count);
	assert (!peer->cmd.buttons && !peer->cmd.impulse && !peer->cmd.forwardmove &&
		!peer->cmd.vr_roomscalemove[1] && !peer->private_pmove_last_cmd_valid &&
		!peer->edict->v.button0 && !peer->edict->v.impulse);
	assert (VectorCompare (peer->edict->v.origin, origin) &&
		VectorCompare (peer->edict->v.velocity, velocity) &&
		peer->private_pmove_jump_secs == jump_secs &&
		peer->private_pmove_waterjump_secs == waterjump_secs);
	/* Produced before the client observes recovery, including a brief action
	 * and head motion. A fresh sequence alone is not proof of fresh input. */
	GapSend (peer, state, BUTTON_ATTACK, 7, 3);
	old_length = captured_length;
	memcpy (old_packet, captured, old_length);
	GapDeliver (peer, old_packet, old_length);
	GapWorldFrame ();
	assert (!peer->private_cmd_queue_count && peer->private_completed_move == completed);
	GapSnapshot (peer, state);
	assert (state->move_ack_resume_pending && !state->move_ack_prediction_allowed &&
		state->ackedmovemessages == completed);
	/* Deliver just the real inline marker, losing the first marked command. */
	realtime += host_frametime;
	GapSend (peer, state, 0, 0, 1);
	net_message.data = captured;
	net_message.cursize = captured_length;
	MSG_BeginReading ();
	assert (MSG_ReadByte () == clc_stringcmd);
	(void)MSG_ReadString ();
	marker_length = msg_readcount;
	first_sequence = state->move_resume_marker_first_sequence;
	GapDeliver (peer, captured, marker_length);
	assert (peer->private_input_phase == PRIVATE_INPUT_AWAIT_COMPLETION &&
		peer->private_resume_first_sequence == first_sequence &&
		peer->private_completed_move == completed);
	epoch = peer->private_move_discontinuity_epoch;
	realtime += 1.25;
	SV_RunClients ();
	assert (peer->active && peer->private_input_phase == PRIVATE_INPUT_AWAIT_MARKER &&
		peer->private_move_discontinuity_epoch == (unsigned short)(epoch + 1));
	GapSnapshot (peer, state);
	realtime += host_frametime;
	GapSend (peer, state, 0, 0, 1);
	first_sequence = state->move_resume_marker_first_sequence;
	GapDeliver (peer, captured, captured_length);
	assert (peer->private_input_phase == PRIVATE_INPUT_AWAIT_COMPLETION &&
		peer->private_cmd_queue_count == 1 &&
		peer->private_cmd_queue[peer->private_cmd_queue_head].sequence == first_sequence);
	GapWorldFrame ();
	assert (peer->private_input_phase == PRIVATE_INPUT_RUNNING &&
		peer->private_completed_move == first_sequence && !peer->private_cmd_queue_count);
	/* Lose the first completion reply: redundant marker/move delivery must
	 * neither rerun the command nor accumulate its roomscale a second time. */
	VectorCopy (peer->edict->v.origin, origin);
	GapDeliver (peer, captured, captured_length);
	assert (!peer->private_cmd_queue_count && peer->private_completed_move == first_sequence &&
		VectorCompare (peer->edict->v.origin, origin));
	GapSnapshot (peer, state);
	assert (!state->move_ack_resume_pending && state->ackedmovemessages == first_sequence &&
		state->move_ack_prediction_allowed && state->move_snapshot_valid);
	cl = *state;
	cl.time = qcvm->time;
	cl.pendingcmd.servertime = cl.time;
	assert (CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], origin));
	*state = cl;
	/* The native adapter owns both flight modes during quiet recovery. */
	for (int mode = 0; mode < 2; ++mode)
	{
		host_client = peer;
		sv_player = peer->edict;
		Cmd_ExecuteString (mode ? "fly 1" : "noclip 1", src_client);
		realtime += host_frametime;
		GapSend (peer, state, 0, 0, 0);
		GapDeliver (peer, captured, captured_length);
		GapWorldFrame ();
		completed = peer->private_completed_move;
		realtime += 1.25;
		GapWorldFrame ();
		assert (peer->active && peer->private_input_phase == PRIVATE_INPUT_AWAIT_MARKER &&
			peer->private_completed_move == completed);
		GapSnapshot (peer, state);
		assert (!state->move_ack_prediction_allowed && state->move_ack_selected_owner);
		realtime += host_frametime;
		GapSend (peer, state, 0, 0, 0);
		GapDeliver (peer, captured, captured_length);
		GapWorldFrame ();
		assert (peer->private_input_phase == PRIVATE_INPUT_RUNNING &&
			peer->private_completed_move > completed);
		GapSnapshot (peer, state);
		host_client = peer;
		sv_player = peer->edict;
		Cmd_ExecuteString (mode ? "fly 0" : "noclip 0", src_client);
	}
	/* Seed only the producer's full sequence (selection/admission remains
	 * real), then lose completion across a half-range and a full wrap. */
	for (int index = 0; index < 2; ++index)
	{
		completed = state->ackedmovemessages;
		/* Full wrap + two yields a misleading forward low16 alias. The
		 * second case also exceeds the half-range without that alias. */
		const int first = completed + (index ? 40000 : 65538);
		state->movemessages = first;
		realtime += 1.25;
		SV_RunClients ();
		GapSnapshot (peer, state);
		realtime += host_frametime;
		GapSend (peer, state, 0, 0, .1f);
		GapDeliver (peer, captured, captured_length);
		GapWorldFrame ();
		assert (peer->private_completed_move == first && state->ackedmovemessages == completed);
		/* No completion snapshot is delivered before the next gap. */
		realtime += 1.25;
		SV_RunClients ();
		GapSnapshot (peer, state);
		assert (state->move_ack_resume_pending && state->ackedmovemessages == completed &&
			state->move_ack_discontinuity_epoch == peer->private_move_discontinuity_epoch);
		GapSnapshot (peer, state); // identical pending metadata still cannot complete
		assert (state->ackedmovemessages == completed);
		realtime += host_frametime;
		GapSend (peer, state, 0, 0, .1f);
		GapDeliver (peer, captured, captured_length);
		GapSnapshot (peer, state); // marker accepted; queued command has not run
		assert (!state->move_ack_resume_pending && state->ackedmovemessages == completed &&
			peer->private_input_phase == PRIVATE_INPUT_AWAIT_COMPLETION);
		GapWorldFrame ();
		GapSnapshot (peer, state);
		assert (peer->private_input_phase == PRIVATE_INPUT_RUNNING &&
			state->ackedmovemessages == first + 1 && !state->move_ack_resume_pending);
	}
	for (int ordering = 0; ordering < 2; ++ordering)
	{
		char command[128];
		q_snprintf (command, sizeof (command), "setpos %.6f %.6f %.6f",
			peer->edict->v.origin[0], peer->edict->v.origin[1], peer->edict->v.origin[2]);
		host_client = peer;
		sv_player = peer->edict;
		if (!ordering) Cmd_ExecuteString (command, src_client);
		realtime += 1.25;
		SV_RunClients ();
		host_client = peer;
		sv_player = peer->edict;
		if (ordering) Cmd_ExecuteString (command, src_client);
		GapSnapshot (peer, state);
		assert (state->move_ack_resume_pending &&
			state->move_ack_discontinuity_reason == MOVEACK_DISCONTINUITY_RESET_TELEPORT &&
			state->move_teleport_epoch_valid &&
			state->move_teleport_epoch_consumed == peer->private_move_discontinuity_epoch);
		realtime += host_frametime;
		GapSend (peer, state, 0, 0, 0);
		GapDeliver (peer, captured, captured_length);
		GapWorldFrame ();
		GapSnapshot (peer, state);
		assert (peer->private_input_phase == PRIVATE_INPUT_RUNNING);
		host_client = peer;
		sv_player = peer->edict;
		Cmd_ExecuteString ("noclip 0", src_client);
	}
	/* Die while fenced using stock world damage, then let actual stock death
	 * callbacks settle before a marked attack requests respawn. */
	realtime += 1.25;
	SV_RunClients ();
	host_client = peer;
	sv_player = peer->edict;
	dfunction_t *damage = ED_FindFunction ("T_Damage");
	assert (damage);
	pr_global_struct->time = qcvm->time;
	pr_global_struct->self = EDICT_TO_PROG (peer->edict);
	G_INT (OFS_PARM0) = EDICT_TO_PROG (peer->edict);
	G_INT (OFS_PARM1) = G_INT (OFS_PARM2) = EDICT_TO_PROG (EDICT_NUM (0));
	G_FLOAT (OFS_PARM3) = 200;
	PR_ExecuteProgram (damage - qcvm->functions);
	assert (SV_PrivateWalkTrialTerminalState (peer));
	epoch = peer->private_move_discontinuity_epoch;
	for (int frame = 0; frame < 180; ++frame)
	{
		realtime += host_frametime;
		GapWorldFrame ();
	}
	assert (peer->active && SV_PrivateWalkTrialTerminalState (peer) &&
		peer->private_move_discontinuity_epoch == epoch);
	GapSnapshot (peer, state);
	realtime += host_frametime;
	GapSend (peer, state, BUTTON_ATTACK, 0, 0);
	GapDeliver (peer, captured, captured_length);
	GapWorldFrame ();
	assert (peer->active && !SV_PrivateWalkTrialTerminalState (peer) &&
		peer->private_input_phase == PRIVATE_INPUT_RUNNING);
	GapSnapshot (peer, state);
	completed = peer->private_completed_move;
	realtime += 1.25;
	SV_RunClients ();
	assert (peer->active && peer->private_input_phase == PRIVATE_INPUT_AWAIT_MARKER &&
		peer->private_completed_move == completed);
	GapSnapshot (peer, state);
	for (int frame = 0; frame < 20; ++frame)
	{
		realtime += host_frametime;
		GapSend (peer, state, BUTTON_ATTACK, 0, 0);
		GapDeliver (peer, captured, captured_length);
		GapWorldFrame ();
		GapSnapshot (peer, state);
	}
	assert (peer->private_input_phase == PRIVATE_INPUT_RUNNING &&
		peer->edict->v.health > 0 && peer->edict->v.ammo_shells < 25);
	puts ("MIXED_ARRIVAL_GAP_PASSED real admission/receipt/QC/physics/snapshots/replay; delayed input, lost commands/replies, native modes, half/full wrap, teleport orderings, death/respawn; captured delivery");
}

static client_state_t *CreateMixedPeerState (client_t *peer, int slot)
{
	static byte snapshots[NET_MAXMESSAGE];
	client_state_t *state = Mem_Alloc (sizeof (*state));
	state->protocol = sv.protocol;
	state->protocolflags = sv.protocolflags;
	state->protocol_pext2 = peer->protocol_pext2;
	state->protocol_qsvr = peer->protocol_qsvr;
	state->viewentity = slot + 1;
	state->worldmodel = sv.qcvm.worldmodel;
	state->maxclients = svs.maxclients;
	state->scores = Mem_Alloc (svs.maxclients * sizeof (*cl.scores));
	state->gametype = GAME_COOP;
	state->ackedmovemessages = -1;
	state->max_edicts = qcvm->max_edicts;
	state->num_entities = 1;
	state->entities = Mem_Alloc (qcvm->max_edicts * sizeof (*cl.entities));
	for (int model = 0; model < MAX_MODELS; model++)
		state->model_precache[model] = sv.models[model];
	/* Reset deltas depend on signon baselines (notably player models).
	 * Consume the actual baseline codec rather than copying server state. */
	cl = *state;
	net_message.data = snapshots;
	net_message.maxsize = sizeof (snapshots);
	for (int entity = 1; entity < qcvm->num_edicts; entity++)
	{
		SZ_Clear (&net_message);
		MSG_WriteStaticOrBaseLine (&net_message, entity, &EDICT_NUM (entity)->baseline,
			cl.protocol_pext2, cl.protocol, cl.protocolflags);
		MSG_BeginReading ();
		assert (MSG_ReadByte () == svcfte_spawnbaseline2);
		assert (MSG_ReadShort () == entity);
		CL_ParseBaseline (CL_EntityNum (entity), 6);
		assert (!msg_badread && msg_readcount == net_message.cursize);
	}
	*state = cl;
	VectorCopy (peer->edict->v.angles, state->viewangles);
	return state;
}

#ifndef MIXED_NATIVE_FIXTURE_ENTRY
#define MIXED_NATIVE_FIXTURE_ENTRY main
#endif
int MIXED_NATIVE_FIXTURE_ENTRY (int argc, char **argv)
{
	char public_offer[1024];
	client_state_t *states[2];
	client_t *peers[2];
	vec3_t start[2];
	float initial_shells[2];
	qboolean saw_peer[2] = {false, false};
	qboolean selected, saw_replay = false, saw_return_replay[2] = {false, false};
	unsigned last_mode_epoch = 0;
	static byte snapshots[NET_MAXMESSAGE];
	Fixture_InitNativeEngine (argc, argv, "e1m1", true);
	const qboolean defaults = COM_CheckParm ("-defaultselection") != 0;
	selected = defaults || COM_CheckParm ("-selected") != 0;
	assert (svs.maxclients >= 2);
	ConfigurePrivateMovementFixture (selected, defaults);
	ClientOffer (0, false, modern_offer, sizeof (modern_offer));
	ClientOffer (QSVR_PROTOCOL_PINNED, false, public_offer, sizeof (public_offer));
	peers[0] = SpawnPeer (0, modern_offer, QSVR_PROTOCOL_PINNED);
	peers[1] = SpawnPeer (1, public_offer, 0);
	cls.signon = SIGNONS;
	cls.state = ca_connected;
	cls.demoplayback = false;
	cls.legacy_qsvr = 0;
	for (int slot = 0; slot < 2; slot++)
	{
		states[slot] = CreateMixedPeerState (peers[slot], slot);
		VectorCopy (peers[slot]->edict->v.origin, start[slot]);
		VectorCopy (peers[slot]->edict->v.angles, states[slot]->viewangles);
		initial_shells[slot] = peers[slot]->edict->v.ammo_shells;
	}
	host_frametime = .025;
	if (COM_CheckParm ("-earlypause")) RunStartupPauseChecks (peers[0], states[0]);
	for (int frame = 0; frame < 120; frame++)
	{
		pr_global_struct->frametime = host_frametime; /* Host_ServerFrame's QC clock. */
		realtime += host_frametime;
		if (selected && (frame == 20 || frame == 40 || frame == 60 || frame == 80))
		{
			host_client = peers[0];
			sv_player = host_client->edict;
			Cmd_ExecuteString (frame == 20 ? "fly 1" : frame == 40 ? "fly 0" :
				frame == 60 ? "noclip 1" : "noclip 0", src_client);
			assert (sv_player->v.movetype == (frame == 20 ? MOVETYPE_FLY :
				frame == 60 ? MOVETYPE_NOCLIP : MOVETYPE_WALK));
			cl = *states[0];
			if (frame == 20 || frame == 60)
			{
				cl.move_replay_propagate_sequence[0] = 123456;
				cl.move_replay_propagate_waterjumptime[0] = 1;
			}
			cls.netcon = peers[0]->netconnection;
			ReadPeerSnapshot (peers[0], snapshots, sizeof (snapshots));
			/* Entering native is observable before physics. Leaving it cannot
			 * grant replay until the selected frame has actually run. */
			assert (cl.move_ack_authority == MOVE_AUTHORITY_LEGACY_FRAME &&
				!cl.move_ack_prediction_allowed && cl.move_ack_selected_owner);
			*states[0] = cl;
		}
		for (int slot = 0; slot < 2; slot++)
		{
			usercmd_t command = {0};
			cl = *states[slot];
			cl.time = qcvm->time;
			cls.netcon = peers[slot]->netconnection;
			VectorCopy (cl.viewangles, command.viewangles);
			command.servertime = cl.time;
			command.forwardmove = slot ? -200 : 200;
			command.buttons = slot || frame % 30 == 2 ? BUTTON_ATTACK : 0;
			if (!slot)
			{
				command.vr_active = command.vr_handpos_relative = true;
				command.vr_handpos[1] = 16;
				command.vr_handpos[2] = 22;
				VectorCopy (command.viewangles, command.vr_handrot);
				command.vr_roomscalemove[1] = .05f;
				if (frame % 30 == 2) command.impulse = 2;
			}
			captured_length = 0;
			CL_SendMove (&command);
			*states[slot] = cl;
			host_client = peers[slot];
			sv_player = host_client->edict;
			if (captured_length)
			{
				net_message.data = captured;
				net_message.cursize = captured_length;
				assert (SV_ReadClientMessage ());
				/* The message owner's normal EOF read sets badread; acceptance
				 * above, rather than that terminal flag, is its public contract. */
				if (!SV_PrivateWalkTrialSelected (host_client))
					assert (host_client->cmd.vr_active == !slot);
			}
			if (!slot && frame % 30 == 2)
			{
				/* A brief tap and impulse, released in a second generated
				 * packet before the world frame. Redundant older commands in
				 * that packet must not accumulate their roomscale twice. */
				float pending_roomscale = host_client->cmd.vr_roomscalemove[1];
				float queued_roomscale = 0;
				unsigned queued_count = host_client->private_cmd_queue_count;
				int accepted = host_client->lastmovemessage;
				for (unsigned i = 0; i < queued_count; ++i)
					queued_roomscale += host_client->private_cmd_queue[
						(host_client->private_cmd_queue_head + i) % SV_PRIVATE_CMD_QUEUE_SIZE].vr_roomscalemove[1];
				command.buttons = command.impulse = 0;
				VectorClear (command.vr_roomscalemove);
				captured_length = 0;
				CL_SendMove (&command);
				*states[slot] = cl;
				assert (captured_length);
				net_message.data = captured;
				net_message.cursize = captured_length;
				assert (SV_ReadClientMessage ());
				if (!selected)
				{
					assert ((host_client->cmd.buttons & BUTTON_ATTACK) &&
						!host_client->private_latest_buttons && host_client->cmd.impulse == 2);
					assert (host_client->cmd.vr_roomscalemove[1] == pending_roomscale);
				}
				else
				{
					float after = 0;
					assert (host_client->lastmovemessage == accepted + 1 &&
						host_client->private_cmd_queue_count == queued_count + 1);
					for (unsigned i = 0; i < host_client->private_cmd_queue_count; ++i)
						after += host_client->private_cmd_queue[
							(host_client->private_cmd_queue_head + i) % SV_PRIVATE_CMD_QUEUE_SIZE].vr_roomscalemove[1];
					assert (after == queued_roomscale);
				}
			}
			if (!SV_PrivateWalkTrialSelected (host_client)) SV_ClientThink ();
		}
		SV_Physics ();
		SV_FinishPrivateUsercmds ();
		assert (!peers[0]->cmd.impulse && !peers[0]->edict->v.impulse);
		if (!selected || peers[0]->private_move_native_frame)
			assert (!(peers[0]->cmd.buttons & BUTTON_ATTACK) && !peers[0]->private_cmd_queue_count);
		for (int slot = 0; slot < 2; slot++)
		{
			cl = *states[slot];
			cls.netcon = peers[slot]->netconnection;
			ReadPeerSnapshot (peers[slot], snapshots, sizeof (snapshots));
			if (selected && !slot)
			{
				vec3_t replay_origin;
				qboolean native = peers[slot]->edict->v.movetype != MOVETYPE_WALK;
				assert (cl.move_ack_selected_owner && cl.move_snapshot_valid);
				assert (cl.move_ack_authority == (native ? MOVE_AUTHORITY_LEGACY_FRAME :
					MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT));
				if (frame == 20 || frame == 40 || frame == 60 || frame == 80)
					assert (cl.move_ack_mode_epoch > last_mode_epoch);
				last_mode_epoch = cl.move_ack_mode_epoch;
				cl.time = qcvm->time;
				cl.pendingcmd.servertime = cl.time;
				qboolean replayed = CL_ReplayPlayerMovement (&cl.entities[1], replay_origin);
				if (native)
					assert (!cl.move_ack_prediction_allowed && !replayed);
				else if (cl.move_ack_prediction_allowed && frame > 2)
				{
					assert (replayed && cl.move_replay_private_metadata_valid &&
						cl.move_replay_private_mode_epoch == cl.move_ack_mode_epoch);
					saw_replay = true;
					if (frame >= 40 && frame < 60) saw_return_replay[0] = true;
					if (frame >= 80) saw_return_replay[1] = true;
					if (frame == 40 || frame == 80)
						assert (!cl.move_replay_propagate_sequence[0] &&
							!cl.move_replay_propagate_waterjumptime[0]);
				}
			}
			else assert (!cl.move_ack_prediction_allowed && !peers[slot]->private_pmove_walk_selected);
			saw_peer[slot] |= cl.entities[2 - slot].netstate.modelindex > 0;
			if (frame >= 2)
			{
				assert (cl.ackedmovemessages == (selected && !slot ?
					peers[slot]->private_completed_move : peers[slot]->lastmovemessage));
				assert (cl.ackedmovemessages < cl.movemessages);
			}
			*states[slot] = cl;
		}
	}
	if (selected && COM_CheckParm ("-arrivalgap"))
	{
		RunArrivalGapChecks (peers[0], states[0]);
		GapSnapshot (peers[1], states[1]);
		assert (!states[1]->move_ack_selected_owner && !states[1]->move_ack_prediction_allowed);
	}
	for (int slot = 0; slot < 2; slot++)
	{
		vec3_t displacement;
		VectorSubtract (peers[slot]->edict->v.origin, start[slot], displacement);
		assert (peers[slot]->active && peers[slot]->spawned);
		assert (VectorLength (displacement) > 1);
		assert (peers[slot]->edict->v.ammo_shells < initial_shells[slot]);
		if (!saw_peer[slot])
			fprintf (stderr, "MIXED_NATIVE_MISSING_PEER observer=%d own_model=%.0f peer_model=%.0f peer_leaves=%d limits=%u/%u snapshots=%u\n",
				slot, peers[slot]->edict->v.modelindex, peers[1-slot]->edict->v.modelindex,
				peers[1-slot]->edict->num_leafs, peers[slot]->limit_entities, peers[slot]->limit_models,
				(unsigned)states[slot]->num_entities);
		assert (saw_peer[slot]); /* Later legitimate PVS removals are allowed. */
		for (int axis = 0; axis < 3; axis++)
			assert (states[slot]->entities[slot + 1].netstate.origin[axis] == peers[slot]->edict->v.origin[axis]);
		assert (states[slot]->ackedmovemessages > 100);
		printf ("MIXED_NATIVE_OWNER slot=%d private=%u movement=%.3f shells=%.0f->%.0f ack=%d\n",
			slot, peers[slot]->protocol_qsvr, VectorLength (displacement),
			initial_shells[slot], peers[slot]->edict->v.ammo_shells,
			states[slot]->ackedmovemessages);
		Mem_Free (states[slot]->entities);
		Mem_Free (states[slot]->scores);
		Mem_Free (states[slot]);
	}
	assert (skin_uploads > 0); // the graphics boundary actually ran
	if (selected)
	{
		assert (saw_replay && saw_return_replay[0] && saw_return_replay[1] && last_mode_epoch >= 4);
		puts ("MIXED_SELECTED_NATIVE_PASSED real admission/fly/noclip/return; full stats/snapshot/replay; captured transport");
	}
	else puts ("MIXED_NATIVE_PASSED synthetic commands/captured transport/actual QC physics/snapshots; no connected XR claim");
	return 0;
}
