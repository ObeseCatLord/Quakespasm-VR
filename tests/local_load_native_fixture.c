/* Actual paired loopback offer/spawn/begin, host scheduling and save/load.
 * Reuses the native bootstrap and prepared client model/baseline resources.
 * No window, graphical serverinfo loading, headset or gaze is exercised.
 * A second saved identity uses the existing synthetic negotiation endpoint;
 * the first player's commands and snapshots use real loopback throughout. */
#define MIXED_NATIVE_FIXTURE_ENTRY ImportedLocalMixedMain
#define NEGOTIATION_FIXTURE_CUSTOM_CAN_SEND
#define MIXED_FIXTURE_CUSTOM_UNRELIABLE
#include "mixed_native_fixture.c"

static qboolean hold_signon = true;
static sizebuf_t network_buffer;
static int graphical_particle_clears;

void __wrap_R_ClearParticles (void)
{
	/* Dedicated bootstrap has no renderer particle pool. Record the existing
	 * fastload cleanup boundary; this fixture cannot qualify graphical cleanup. */
	assert (isDedicated);
	++graphical_particle_clears;
}

void __wrap_PScript_ClearParticles (qboolean load)
{
	assert (isDedicated && !load);
	++graphical_particle_clears;
}

qboolean __real_NET_CanSendMessage (qsocket_t *socket);
qboolean __wrap_NET_CanSendMessage (qsocket_t *socket)
{
	return !hold_signon && socket && socket->driverdata &&
		__real_NET_CanSendMessage (socket);
}
int __real_NET_SendUnreliableMessage (qsocket_t *socket, sizebuf_t *message);
int __wrap_NET_SendUnreliableMessage (qsocket_t *socket, sizebuf_t *message)
{
	if (socket && socket->driverdata)
		return __real_NET_SendUnreliableMessage (socket, message);
	/* Only the second saved player's prepared endpoint has no paired socket. */
	assert (socket);
	return 1;
}

static void NetworkBuffer (void)
{
	net_message = network_buffer;
	SZ_Clear (&net_message);
}

static void LocalStrings (const char **commands, int count)
{
	SZ_Clear (&cls.message);
	for (int i = 0; i < count; ++i)
	{
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, commands[i]);
	}
	assert (NET_SendMessage (cls.netcon, &cls.message) == 1);
	SZ_Clear (&cls.message);
	NetworkBuffer ();
	SV_RunClients ();
}

static client_t *LocalSignon (qboolean already_connected, qboolean public)
{
	hold_signon = true;
	if (!already_connected)
	{
		cls.netcon = NET_Connect ("local");
		assert (cls.netcon);
	}
	NetworkBuffer ();
	SV_CheckForNewClients ();
	client_t *peer = &svs.clients[0];
	assert (peer->active && peer->netconnection && peer->netconnection->driverdata == cls.netcon);
	SZ_Clear (&peer->message); // extension request; client offer owner runs below
	ClientOffer (public ? QSVR_PROTOCOL_PINNED : 0, false, modern_offer, sizeof modern_offer);
	const char *offer[] = {modern_offer};
	LocalStrings (offer, countof (offer));
	Header (peer, public ? 0 : QSVR_PROTOCOL_PINNED);
	SZ_Clear (&peer->message); // renderer resources are prepared after spawn
	const char *spawn[] = {"name private", "spawn", "begin", "notarget 1"};
	LocalStrings (spawn, countof (spawn));
	assert (peer->spawned && peer->knowntoqc && !peer->edict->free);
	SZ_Clear (&peer->message);
	peer->sendsignon = false;
	/* Full serverinfo loading is a renderer boundary. Execute its real reset
	 * owner before prepared resource setup, and inspect history before replacing
	 * resources, so preparation cannot conceal a client-reset regression. */
	CL_ClearState ();
	assert (!cl.movemessages && !cl.ackedmovemessages && !cl.move_snapshot_valid &&
		!cl.move_msec_sample_valid && !cl.move_replay_private_metadata_valid);
	for (int i = 0; i < MOVECMDS_MASK + 1; ++i)
		assert (!cl.movecmds[i].seconds && !cl.movecmds[i].forwardmove);
	CL_FreeState (); // release reset's entity allocation before prepared resources
	client_state_t *prepared = CreateMixedPeerState (peer, 0);
	cl = *prepared;
	Mem_Free (prepared); // arrays now owned by cl, not this temporary container
	cls.state = ca_connected;
	cls.signon = SIGNONS;
	SZ_Clear (&cls.message);
	key_dest = key_game;
	hold_signon = false;
	return peer;
}

static void LocalReceive (void)
{
	for (;;)
	{
		NetworkBuffer ();
		const int received = NET_GetMessage (cls.netcon);
		assert (received >= 0);
		if (!received) break;
		CL_ParseServerMessage ();
		assert (msg_readcount == net_message.cursize && cls.state == ca_connected);
	}
}

static void LocalFrame (void)
{
	NetworkBuffer ();
	Host_ServerFrame ();
	LocalReceive ();
}

static void LocalSend (qboolean vr, unsigned buttons, float forward)
{
	usercmd_t command = {0};
	realtime += host_frametime;
	cl.time = command.servertime = qcvm->time;
	command.forwardmove = forward;
	command.buttons = buttons;
	command.vr_active = command.vr_handpos_relative = vr;
	command.vr_handpos[1] = 16;
	command.vr_handpos[2] = 22;
	VectorCopy (cl.viewangles, command.viewangles);
	const int received_ack = cl.ackedmovemessages;
	CL_SendMove (&command);
	if (cl.protocol_qsvr == QSVR_PROTOCOL_PINNED)
		assert (cl.ackedmovemessages == received_ack);
	if (cls.message.cursize)
	{
		assert (NET_SendMessage (cls.netcon, &cls.message) == 1);
		SZ_Clear (&cls.message);
	}
}

static void LocalMovement (client_t *peer, qboolean vr, qboolean selected)
{
	vec3_t start;
	VectorCopy (peer->edict->v.origin, start);
	const float shells = peer->edict->v.ammo_shells;
	qboolean replayed = false;
	for (int frame = 0; frame < 12; ++frame)
	{
		LocalSend (vr, frame == 5 ? BUTTON_ATTACK : 0, 100);
		LocalFrame ();
		assert (peer->active && peer->spawned && peer->private_pmove_walk_selected == selected);
		if (selected && frame > 2)
		{
			assert (cl.ackedmovemessages == peer->private_completed_move &&
				cl.move_ack_selected_owner && cl.move_snapshot_valid &&
				cl.move_snapshot_owner == cl.viewentity && CL_ReceivedMoveStatsUsable ());
			vec3_t replay;
			replayed |= CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], replay);
		}
	}
	vec3_t displacement;
	VectorSubtract (peer->edict->v.origin, start, displacement);
	assert (VectorLength (displacement) > 1);
	if (selected && SV_PrivateWalkTrialStockProgram ()) assert (replayed);
	assert (peer->edict->v.ammo_shells < shells); // actual QC weapon discharge
}

static void LocalOutstandingReplay (client_t *peer)
{
	const int ack = cl.ackedmovemessages;
	vec3_t predicted, authoritative, delta;
	VectorCopy (peer->edict->v.origin, authoritative);
	LocalSend (true, 0, 100); // remains outstanding on the real socket
	assert (cl.movemessages - 1 > ack && peer->private_completed_move == ack &&
		cl.ackedmovemessages == ack && cl.move_snapshot_ack == ack);
	assert (CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], predicted));
	VectorSubtract (predicted, authoritative, delta);
	assert (VectorLength (delta) > .05f);
	LocalFrame ();
	assert (cl.ackedmovemessages == cl.movemessages - 1);
	for (int axis = 0; axis < 3; ++axis)
		assert (fabsf (predicted[axis] - peer->edict->v.origin[axis]) <= .125f);
}

static void LocalSuspend (client_t *peer, qboolean menu)
{
	if (menu) key_dest = key_menu;
	else
	{
		const char *pause[] = {"pause"};
		LocalStrings (pause, countof (pause));
	}
	vec3_t origin;
	VectorCopy (peer->edict->v.origin, origin);
	const int completed = peer->private_completed_move;
	for (int i = 0; i < 3; ++i)
	{
		LocalSend (true, BUTTON_ATTACK | BUTTON_JUMP, 100);
		LocalFrame ();
		assert (!memcmp (origin, peer->edict->v.origin, sizeof origin) &&
			peer->private_completed_move == completed && cl.ackedmovemessages == completed);
	}
	assert (peer->private_input_phase == PRIVATE_INPUT_SUSPENDED);
	if (menu) key_dest = key_game;
	else
	{
		const char *pause[] = {"pause"};
		LocalStrings (pause, countof (pause));
	}
	LocalFrame (); // publish the existing recovery epoch/marker requirement
	for (int i = 0; i < 5; ++i)
	{
		LocalSend (true, 0, 100);
		LocalFrame ();
	}
	assert (peer->private_input_phase == PRIVATE_INPUT_RUNNING &&
		peer->private_completed_move > completed);
	if (SV_PrivateWalkTrialStockProgram ()) assert (cl.move_ack_prediction_allowed);
}

static void SaveFixture (const char *name, int version)
{
	Cmd_ExecuteString (va ("save %s", name), src_command);
	FILE *file = fopen (va ("%s/%s.sav", com_gamedir, name), "r");
	int actual = 0;
	assert (file && fscanf (file, "%d", &actual) == 1 && actual == version);
	fclose (file);
}

static client_t *LocalLoad (const char *command)
{
	hold_signon = true;
	/* Loading runs the real disconnect, server reconstruction and local
	 * connection producer. Only renderer-heavy signon is prepared by fixture. */
	PR_SwitchQCVM (NULL);
	Cmd_ExecuteString (command, src_command);
	assert (sv.active && cls.netcon && cls.signon == 0);
	PR_SwitchQCVM (&sv.qcvm);
	client_t *peer = LocalSignon (true, false);
	assert (!peer->private_cmd_queue_count && !peer->private_pmove_last_cmd_valid &&
		peer->private_completed_move == 0 && !peer->lastmovemessage &&
		cl.movemessages == 0 && cl.ackedmovemessages == -1 && !cl.move_snapshot_valid);
	return peer;
}

static void LocalRewriteSaveHeader (const char *name, qboolean kex,
	int field, const char *bad)
{
	FILE *in = fopen (va ("%s/valid-header-fixture.sav", com_gamedir), "r");
	FILE *out = fopen (va ("%s/%s.sav", com_gamedir, name), "w");
	assert (in && out);
	char line[4096];
	int index = 0;
	while (fgets (line, sizeof (line), in))
	{
		if (!index && kex)
			assert (fprintf (out, "6\nid1\n") > 0);
		else if (bad && index == NUM_BASIC_SPAWN_PARMS + 2 + field * 2)
			assert (fprintf (out, "%s\n", bad) > 0);
		else
			assert (fputs (line, out) >= 0);
		++index;
	}
	assert (!ferror (in) && fclose (in) == 0 && fclose (out) == 0);
	assert (index > NUM_BASIC_SPAWN_PARMS + 4);
}

/* Mutate only owned native save headers; retain the real writer's body. */
static void LocalInvalidSave (client_t *peer)
{
	assert (svs.maxclients == 1 && SV_PrivateWalkTrialStockProgram ());
	SaveFixture ("valid-header-fixture", 5);
	const char *bad[] = {"not-a-number", "nan", "1e30"};
	qsocket_t *connection = cls.netcon;
	edict_t *player = peer->edict;
	dprograms_t *program = sv.qcvm.progs;
	const double world_time = sv.qcvm.time;
	const int skill = current_skill;

	for (int kex = 0; kex < 2; ++kex)
		for (int field = 0; field < 2; ++field)
			for (int variant = 0; variant < (field ? 2 : 3); ++variant)
			{
				LocalRewriteSaveHeader ("invalid-header-fixture", kex, field, bad[variant]);
				PR_SwitchQCVM (NULL);
				Cmd_ExecuteString ("load invalid-header-fixture", src_command);
				assert (qcvm == NULL && sv.active && sv.qcvm.progs == program &&
					sv.qcvm.time == world_time && current_skill == skill &&
					cls.netcon == connection && cls.state == ca_connected &&
					cls.signon == SIGNONS && peer->active && peer->spawned &&
					peer->edict == player && !player->free);
				PR_SwitchQCVM (&sv.qcvm);
			}
	/* Refusals must not poison either supported dialect's next valid load. */
	LocalRewriteSaveHeader ("valid-kex-fixture", true, 0, NULL);
	peer = LocalLoad ("load valid-kex-fixture");
	LocalMovement (peer, true, true);
	peer = LocalLoad ("load valid-header-fixture");
	LocalMovement (peer, true, true);
	puts ("INVALID_SAVE_NATIVE_PASSED legacy/KEX skill/time refusals and valid recovery");
}

int main (int argc, char **argv)
{
	Fixture_InitNativeEngine (argc, argv, "e1m1", true);
	network_buffer = net_message;
	host_frametime = .025;
	const int arg = COM_CheckParm ("-localcase");
	assert (arg && arg + 1 < com_argc);
	const char *scenario = com_argv[arg + 1];
	const qboolean public = !strncmp (scenario, "public", 6);
	const qboolean disabled = !strcmp (scenario, "disabled");
	if (disabled) Cvar_Set ("sv_private_pmove_walk", "0");
	client_t *peer = LocalSignon (false, public);
	const qboolean selected = !public && !disabled;
	assert (peer->private_pmove_walk_selected == selected);
	LocalMovement (peer, !public, selected);
	if (!strcmp (scenario, "invalid-save"))
		LocalInvalidSave (peer);
	else if (!strcmp (scenario, "local"))
	{
		assert (svs.maxclients == 1);
		if (SV_PrivateWalkTrialStockProgram ()) LocalOutstandingReplay (peer);
		LocalSuspend (peer, true);
		LocalSuspend (peer, false);
	}
	else if (strstr (scenario, "fastload"))
	{
		assert (svs.maxclients == 1);
		vec3_t origin;
		VectorCopy (peer->edict->v.origin, origin);
		const float shells = peer->edict->v.ammo_shells;
		SaveFixture ("local-load-fixture", 5);
		LocalSend (!public, BUTTON_ATTACK, -100);
		NetworkBuffer ();
		SV_RunClients (); // old queued commands deliberately not completed
		if (!public) assert (peer->private_cmd_queue_count && peer->private_pmove_last_cmd_valid);
		if (strstr (scenario, "autofastload")) Cvar_Set ("autofastload", "1");
		const char *command = strstr (scenario, "autofastload") ?
			"load local-load-fixture" : "fastload local-load-fixture";
		if (public)
		{
			qsocket_t *connection = cls.netcon;
			const int sequence = cl.movemessages;
			PR_SwitchQCVM (NULL);
			Cmd_ExecuteString (command, src_command);
			PR_SwitchQCVM (&sv.qcvm);
			assert (cls.netcon == connection && cls.signon == SIGNONS &&
				cl.movemessages == sequence && !peer->private_pmove_walk_selected &&
				graphical_particle_clears == 2);
			/* Send_Spawn_Info belongs to fastload; no reconstruction is required. */
			SZ_Clear (&peer->message);
		}
		else peer = LocalLoad (!strcmp (scenario, "fastload") ?
			"fastload local-load-fixture" : "load local-load-fixture");
		for (int axis = 0; axis < 3; ++axis)
			assert (fabsf (origin[axis] - peer->edict->v.origin[axis]) < .001f);
		assert (peer->edict->v.ammo_shells == shells && peer->private_pmove_walk_selected == !public);
		if (!public)
		{
			assert (!graphical_particle_clears);
			LocalFrame (); // no pre-load command may fire after reconnect
			assert (!peer->private_completed_move && peer->edict->v.ammo_shells == shells);
			LocalMovement (peer, true, true);
		}
	}
	else if (!strcmp (scenario, "pending") || !strcmp (scenario, "pending-ground"))
	{
		assert (svs.maxclients == 2);
		net_driverlevel = 0; // prepared endpoint uses the valid loop driver
		client_t *second = SpawnPeer (1, modern_offer, QSVR_PROTOCOL_PINNED);
		host_client = second; sv_player = second->edict;
		Cmd_ExecuteString ("name second", src_client);
		Cmd_ExecuteString ("notarget 1", src_client);
		vec3_t second_origin;
		VectorCopy (second->edict->v.origin, second_origin);
		const float second_shells = second->edict->v.ammo_shells;
		const qboolean stale_ground = !strcmp (scenario, "pending-ground");
		if (stale_ground) peer->edict->v.groundentity = EDICT_TO_PROG (second->edict);
		SaveFixture ("local-load-fixture", 7);
		SZ_Clear (&second->message); // no renderer signon on the prepared endpoint
		peer = LocalLoad ("load local-load-fixture");
		assert (sv.loadgame && sv.loadgame_multiplayer && !sv.paused &&
			peer->private_pmove_walk_selected == !stale_ground);
		assert (EDICT_NUM (2)->free);
		Cmd_ExecuteString ("save pending-denied-fixture", src_command);
		FILE *denied = fopen (va ("%s/pending-denied-fixture.sav", com_gamedir), "r");
		assert (!denied); // existing pending identity save restriction still applies
		if (stale_ground)
		{
			/* Native fallback must survive the stale saved ground until normal
			 * physics refreshes it; never commit then drop selected movement. */
			LocalMovement (peer, true, false);
		}
		else LocalMovement (peer, true, true);
		client_state_t first = cl;
		qsocket_t *first_socket = cls.netcon;
		net_driverlevel = 0;
		second = Negotiate (1, modern_offer, QSVR_PROTOCOL_PINNED);
		host_client = second; sv_player = second->edict;
		Cmd_ExecuteString ("name second", src_client);
		Cmd_ExecuteString ("spawn", src_client);
		Cmd_ExecuteString ("begin", src_client);
		assert (second->spawned && second->private_pmove_walk_selected && !sv.loadgame);
		for (int axis = 0; axis < 3; ++axis)
			assert (fabsf (second_origin[axis] - second->edict->v.origin[axis]) < .001f);
		assert (second->edict->v.ammo_shells == second_shells);
		cl = first; cls.netcon = first_socket;
		SZ_Clear (&cls.message);
		LocalMovement (peer, true, !stale_ground);
	}
	else assert (public || disabled);
	printf ("LOCAL_LOAD_NATIVE_PASSED case=%s loopback commands/physics/full snapshots; prepared renderer signon\n", scenario);
	return 0;
}
