/* Actual paired loopback offer/spawn/begin, host scheduling and save/load.
 * Reuses the native bootstrap and prepared client model/baseline resources.
 * No window, graphical serverinfo loading, headset or gaze is exercised.
 * A second saved identity uses the existing synthetic negotiation endpoint;
 * the first player's commands and snapshots use real loopback throughout. */
#define MIXED_NATIVE_FIXTURE_ENTRY ImportedLocalMixedMain
#define NEGOTIATION_FIXTURE_CUSTOM_CAN_SEND
#define MIXED_FIXTURE_CUSTOM_UNRELIABLE
#include <sys/stat.h>
#include <unistd.h>
#include <setjmp.h>
#ifdef NDEBUG
#error "local native fixture requires assertions"
#endif
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

static client_t *LocalSignonNamed (qboolean already_connected, qboolean public,
	const char *identity)
{
	char name_command[128];
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
	assert (q_snprintf (name_command, sizeof (name_command), "name %s",
		identity && identity[0] ? identity : "private") > 0);
	const char *spawn[] = {name_command, "spawn", "begin", "notarget 1"};
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

static client_t *LocalSignon (qboolean already_connected, qboolean public)
{
	return LocalSignonNamed (already_connected, public, "private");
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

static client_t *LocalLoadAs (const char *command, const char *identity)
{
	hold_signon = true;
	/* Loading runs the real disconnect, server reconstruction and local
	 * connection producer. Only renderer-heavy signon is prepared by fixture. */
	PR_SwitchQCVM (NULL);
	Cmd_ExecuteString (command, src_command);
	assert (sv.active && cls.netcon && cls.signon == 0);
	PR_SwitchQCVM (&sv.qcvm);
	client_t *peer = LocalSignonNamed (true, false, identity);
	assert (!peer->private_cmd_queue_count && !peer->private_pmove_last_cmd_valid &&
		peer->private_completed_move == 0 && !peer->lastmovemessage &&
		cl.movemessages == 0 && cl.ackedmovemessages == -1 && !cl.move_snapshot_valid);
	return peer;
}

static client_t *LocalLoad (const char *command)
{
	return LocalLoadAs (command, "private");
}

static void LocalAutosaveFrameAt (double wall_time, double game_time)
{
	/* The trigger owner is native; only clocks/QC progress are fixture-controlled. */
	realtime = wall_time;
	qcvm->time = game_time;
	Host_CoopAutosaveFrame ();
}

static char *LocalReadAutosave (const char *name)
{
	char path[MAX_OSPATH];
	FILE *file;
	long length;
	char *contents;
	assert (q_snprintf (path, sizeof (path), "%s/%s", com_gamedir, name) > 0);
	file = fopen (path, "rb");
	assert (file && fseek (file, 0, SEEK_END) == 0);
	length = ftell (file);
	assert (length > 0 && fseek (file, 0, SEEK_SET) == 0);
	contents = malloc ((size_t)length + 1);
	assert (contents && fread (contents, 1, (size_t)length, file) == (size_t)length);
	assert (fclose (file) == 0);
	contents[length] = '\0';
	return contents;
}

static void LocalAssertCoopAutosave (const char *name, float armor)
{
	char *contents = LocalReadAutosave (name);
	char expected[96];
	char *version_end, *comment_end, *clients_end;
	long version = strtol (contents, &version_end, 10);
	long maxclients;
	assert (version == 7 && version_end > contents && *version_end == '\n');
	comment_end = strchr (version_end + 1, '\n');
	assert (comment_end);
	maxclients = strtol (comment_end + 1, &clients_end, 10);
	assert (maxclients == 2 && clients_end > comment_end + 1 && *clients_end == '\n');
	q_snprintf (expected, sizeof (expected), "\"armorvalue\" \"%f\"", (double)armor);
	assert (strstr (contents, expected));
	free (contents);
}

static void LocalCoopAutosave (client_t *peer)
{
	char temp_path[MAX_OSPATH];
	char *slot0_before, *slot0_after, *slot1_before, *slot1_after;
	const float first_armor = 41.25f;
	const float second_armor = 82.5f;
	const float recovered_armor = 123.25f;
	const double first_time = 10.0;
	const double second_time = 11.1;
	int prior_secrets, prior_kill_bucket, prior_serverflags, prior_next_slot;
	double prior_last_realtime, retry_deadline, retry_snapshot;

	assert (svs.maxclients == 2 && coop.value && !deathmatch.value &&
		sv_save_multiplayer.value && peer->active && peer->spawned &&
		peer->knowntoqc && peer->netconnection && peer->netconnection->driverdata == cls.netcon);
	Cvar_SetQuick (&sv_coop_autosave, "1");
	Cvar_SetQuick (&sv_coop_autosave_slots, "2");
	Cvar_SetQuick (&sv_coop_autosave_min_interval, "1");
	SaveFixture ("manual-anchor", 7);
	assert (!strcmp (sv.lastsave, "manual-anchor"));

	peer->edict->v.armorvalue = first_armor;
	LocalAutosaveFrameAt (first_time, 3.25);
	assert (sv.coop_autosave_mapstart_done && sv.coop_autosave_next_slot == 1 &&
		sv.coop_autosave_last_realtime == first_time && !sv.coop_autosave_retry_realtime &&
		!strcmp (sv.lastsave, "manual-anchor"));
	LocalAssertCoopAutosave ("coop_auto0.sav", first_armor);

	/* The fixture advances native realtime/QC time and the stock QC secret
	 * counter directly to create deterministic triggers; it never constructs
	 * save data or substitutes for the writer. A live progress change below
	 * proves the minimum interval defers the writer without consuming it. */
	peer->edict->v.armorvalue = second_armor;
	pr_global_struct->found_secrets = 1;
	LocalAutosaveFrameAt (first_time + 0.5, 3.30);
	assert (sv.coop_autosave_next_slot == 1 &&
		sv.coop_autosave_last_realtime == first_time &&
		sv.coop_autosave_last_secrets == 0 && !sv.coop_autosave_retry_realtime);
	LocalAutosaveFrameAt (second_time, 3.35);
	assert (sv.coop_autosave_next_slot == 0 &&
		sv.coop_autosave_last_realtime == second_time &&
		sv.coop_autosave_last_secrets == 1 && !sv.coop_autosave_retry_realtime &&
		!strcmp (sv.lastsave, "manual-anchor"));
	LocalAssertCoopAutosave ("coop_auto1.sav", second_armor);

	/* Preserve both native save files, then obstruct only the next slot's
	 * temporary-file open. This qualifies open failure, not rename failure. */
	slot0_before = LocalReadAutosave ("coop_auto0.sav");
	slot1_before = LocalReadAutosave ("coop_auto1.sav");
	LocalAssertCoopAutosave ("coop_auto0.sav", first_armor);
	peer->edict->v.armorvalue = recovered_armor;
	pr_global_struct->found_secrets = 2;
	assert (q_snprintf (temp_path, sizeof (temp_path), "%s/coop_auto0.sav.tmp", com_gamedir) > 0);
	assert (mkdir (temp_path, 0700) == 0);
	prior_secrets = sv.coop_autosave_last_secrets;
	prior_kill_bucket = sv.coop_autosave_last_kill_bucket;
	prior_serverflags = sv.coop_autosave_last_serverflags;
	prior_next_slot = sv.coop_autosave_next_slot;
	prior_last_realtime = sv.coop_autosave_last_realtime;
	LocalAutosaveFrameAt (second_time + 1.1, 3.40);
	assert (sv.coop_autosave_next_slot == prior_next_slot &&
		sv.coop_autosave_last_secrets == prior_secrets &&
		sv.coop_autosave_last_kill_bucket == prior_kill_bucket &&
		sv.coop_autosave_last_serverflags == prior_serverflags &&
		sv.coop_autosave_last_realtime == prior_last_realtime &&
		sv.coop_autosave_retry_realtime > realtime &&
		!strcmp (sv.lastsave, "manual-anchor"));
	retry_deadline = sv.coop_autosave_retry_realtime;
	slot0_after = LocalReadAutosave ("coop_auto0.sav");
	slot1_after = LocalReadAutosave ("coop_auto1.sav");
	assert (!strcmp (slot0_before, slot0_after) && !strcmp (slot1_before, slot1_after));
	free (slot0_after);
	free (slot1_after);

	/* While the real retry deadline is pending, another owner call must leave
	 * both progress and retry state untouched and the old slot byte-identical. */
	retry_snapshot = sv.coop_autosave_retry_realtime;
	LocalAutosaveFrameAt (retry_deadline - 0.01, 3.45);
	assert (sv.coop_autosave_next_slot == prior_next_slot &&
		sv.coop_autosave_last_secrets == prior_secrets &&
		sv.coop_autosave_last_realtime == prior_last_realtime &&
		sv.coop_autosave_retry_realtime == retry_snapshot);
	slot0_after = LocalReadAutosave ("coop_auto0.sav");
	assert (!strcmp (slot0_before, slot0_after));
	free (slot0_after);
	assert (rmdir (temp_path) == 0);

	LocalAutosaveFrameAt (retry_deadline + 0.01, 3.50);
	assert (sv.coop_autosave_next_slot == 1 &&
		sv.coop_autosave_last_realtime == retry_deadline + 0.01 &&
		sv.coop_autosave_last_secrets == 2 && !sv.coop_autosave_retry_realtime &&
		!strcmp (sv.lastsave, "manual-anchor"));
	LocalAssertCoopAutosave ("coop_auto0.sav", recovered_armor);
	slot1_after = LocalReadAutosave ("coop_auto1.sav");
	assert (!strcmp (slot1_before, slot1_after));
	free (slot1_after);
	LocalAutosaveFrameAt (retry_deadline + 0.02, 3.55);
	assert (sv.coop_autosave_next_slot == 1 &&
		sv.coop_autosave_last_realtime == retry_deadline + 0.01 &&
		!sv.coop_autosave_retry_realtime);
	free (slot0_before);
	free (slot1_before);

	Cvar_SetQuick (&sv_coop_autosave, "0");
	peer = LocalLoad ("load coop_auto0");
	assert (fabsf (peer->edict->v.armorvalue - recovered_armor) < 0.0001f);
	LocalMovement (peer, true, true);
	puts ("COOP_AUTOSAVE_NATIVE_PASSED stockQC slots=2 rotate=coop_auto0/coop_auto1 open-failure=tmp-directory backoff=recovery load=restored-player-and-movement");
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

static edict_t *LocalFindClass (const char *classname)
{
	for (int i = svs.maxclients + 1; i < qcvm->num_edicts; ++i)
	{
		edict_t *ent = EDICT_NUM (i);
		if (!ent->free && ent->v.classname &&
			!q_strcasecmp (PR_GetString (ent->v.classname), classname))
			return ent;
	}
	return NULL;
}

static edict_t *LocalFindUniqueCounter (void)
{
	for (int i = svs.maxclients + 1; i < qcvm->num_edicts; ++i)
	{
		edict_t *candidate = EDICT_NUM (i);
		const char *name;
		int matches = 0;
		if (candidate->free || !candidate->v.classname ||
			q_strcasecmp (PR_GetString (candidate->v.classname), "trigger_counter") ||
			!candidate->v.targetname)
			continue;
		name = PR_GetString (candidate->v.targetname);
		for (int j = svs.maxclients + 1; j < qcvm->num_edicts; ++j)
		{
			edict_t *ent = EDICT_NUM (j);
			if (!ent->free && ent->v.targetname &&
				!strcmp (PR_GetString (ent->v.targetname), name))
				++matches;
		}
		if (matches == 1)
			return candidate;
	}
	return NULL;
}

static eval_t *LocalFloatField (edict_t *ent, const char *name)
{
	ddef_t *field = ED_FindField (name);
	assert (ent && !ent->free && field &&
		(field->type & ~DEF_SAVEGLOBAL) == ev_float);
	return GetEdictFieldValue (ent, field->ofs);
}

static void LocalApplyDamage (edict_t *target, edict_t *inflictor,
	edict_t *attacker, float damage)
{
	dfunction_t *func = ED_FindFunction ("T_Damage");
	int old_self = pr_global_struct->self;
	int old_other = pr_global_struct->other;
	assert (func && target && !target->free && inflictor && !inflictor->free &&
		attacker && !attacker->free);
	pr_global_struct->time = qcvm->time;
	pr_global_struct->self = EDICT_TO_PROG (attacker);
	G_INT (OFS_PARM0) = EDICT_TO_PROG (target);
	G_INT (OFS_PARM1) = EDICT_TO_PROG (inflictor);
	G_INT (OFS_PARM2) = EDICT_TO_PROG (attacker);
	G_FLOAT (OFS_PARM3) = damage;
	PR_ExecuteProgram (func - qcvm->functions);
	pr_global_struct->self = old_self;
	pr_global_struct->other = old_other;
}

static client_t *LocalAttemptPeerAs (int slot, const char *identity, qboolean complete)
{
	char name_command[128];
	client_state_t saved_client_state = cl;
	sizebuf_t saved_client_message = cls.message;
	qsocket_t *saved_netcon = cls.netcon;
	int saved_signon = cls.signon;
	int saved_state = cls.state;
	int saved_net_driverlevel = net_driverlevel;
	client_t *peer;
	net_driverlevel = 0; // the prepared endpoint uses the existing loop driver
	peer = Negotiate (slot, modern_offer, QSVR_PROTOCOL_PINNED);
	host_client = peer;
	sv_player = peer->edict;
	assert (q_snprintf (name_command, sizeof (name_command), "name %s", identity) > 0);
	Cmd_ExecuteString (name_command, src_client);
	Cmd_ExecuteString ("spawn", src_client);
	if (complete)
		Cmd_ExecuteString ("begin", src_client);
	assert (peer->active && peer->knowntoqc && !strcmp (peer->name, identity));
	assert (complete ? peer->spawned && !peer->edict->free : !peer->spawned && peer->edict->free);
	/* The native negotiation helper prepares a synthetic client model. Restore
	 * the paired loopback client's real state before sending its next command. */
	cl = saved_client_state;
	cls.message = saved_client_message;
	cls.netcon = saved_netcon;
	cls.signon = saved_signon;
	cls.state = saved_state;
	net_driverlevel = saved_netcon ? saved_netcon->driver : saved_net_driverlevel;
	return peer;
}

static client_t *LocalSpawnPeerAs (int slot, const char *identity)
{
	return LocalAttemptPeerAs (slot, identity, true);
}

static void LocalDropPreparedPeer (client_t *peer)
{
	assert (peer->netconnection && !peer->netconnection->driverdata);
	/* This endpoint never opened a loop transport. Release its native socket
	 * allocation without Loop_Close retiring the unrelated real loop server. */
	NET_FreeQSocket (peer->netconnection);
	host_client = peer;
	SV_DropClient (true);
	assert (NET_QSocketIsLoopbackPeer (cls.netcon, svs.clients[0].netconnection));
}

static edict_t *LocalPendingClient (int slot)
{
	assert (sv.loadgame_client_saved[slot] && sv.loadgame_client_edicts);
	return (edict_t *)(sv.loadgame_client_edicts + slot * sv.loadgame_client_edict_size);
}

static int *LocalEntityGlobal (const char *name)
{
	ddef_t *def = ED_FindGlobal (name);
	assert (def && (def->type & ~DEF_SAVEGLOBAL) == ev_entity && (def->type & DEF_SAVEGLOBAL));
	return (int *)&qcvm->globals[def->ofs];
}

static qboolean LocalFreeListContains (int slot)
{
	for (size_t i = 0; i < qcvm->free_list.size; ++i)
		if (qcvm->free_list.circular_buffer[(qcvm->free_list.head_index + i) % MAX_EDICTS] == slot)
			return true;
	return false;
}

static void LocalAssertAnchor (int offset)
{
	edict_t *anchor = PROG_TO_EDICT (offset);
	assert (offset && anchor->free && anchor->retain_count == 1 && !anchor->area.prev &&
		anchor->v.solid == SOLID_NOT && !anchor->v.takedamage && !anchor->v.model &&
		!LocalFreeListContains (NUM_FOR_EDICT (anchor)) &&
		!memcmp (&anchor->baseline, &nullentitystate, sizeof nullentitystate));
}

static int local_staged_allocations;
static void LocalObserveFreshAllocation (edict_t *ent)
{
	assert (!ent->free);
	if (sv.loadgame_client_edicts)
		++local_staged_allocations;
}

static void LocalExerciseAnchorAllocator (int offset, int free_target)
{
	int *allocated;
	size_t count;
	ED_RebuildFreeList (true);
	LocalAssertAnchor (offset);
	count = qcvm->free_list.size;
	allocated = malloc ((count + 1) * sizeof *allocated);
	assert (allocated);
	for (size_t i = 0; i <= count; ++i)
	{
		edict_t *ent = ED_Alloc ();
		allocated[i] = NUM_FOR_EDICT (ent);
		assert (EDICT_TO_PROG (ent) != offset);
	}
	assert (LocalFreeListContains (NUM_FOR_EDICT (PROG_TO_EDICT (offset))) == false);
	assert (EDICT_NUM (0)->v.chain == free_target * qcvm->edict_size &&
		free_target < NUM_FOR_EDICT (PROG_TO_EDICT (offset)));
	for (size_t i = 0; i <= count; ++i)
		ED_Free (EDICT_NUM (allocated[i]));
	free (allocated);
	ED_RebuildFreeList (true);
	LocalAssertAnchor (offset);
	fprintf (stderr, "COOP_REFERENCE_ALLOCATOR_PASSED retained-anchor FIFO/rebuild fresh-tail referenced-free-target=%d\n", free_target);
}

static char *LocalNextLine (char **cursor)
{
	char *line, *end;
	if (!cursor || !*cursor)
		return NULL;
	line = *cursor;
	end = strchr (line, '\n');
	if (end)
	{
		*end = '\0';
		*cursor = end + 1;
	}
	else
		*cursor = NULL;
	return line;
}

static void LocalAssertTwoIdentitySave (const char *name, int reference_slot)
{
	char *contents = LocalReadAutosave (name);
	char *lines = strdup (contents), *cursor = lines;
	char *line;
	char expected_reference[64];
	assert (lines);
	assert ((line = LocalNextLine (&cursor)) && !strcmp (line, "7"));
	assert (LocalNextLine (&cursor)); // writer comment
	assert ((line = LocalNextLine (&cursor)) && !strcmp (line, "2"));
	for (int i = 0; i < 2; ++i)
	{
		assert ((line = LocalNextLine (&cursor)) && !strcmp (line, "1"));
		line = LocalNextLine (&cursor);
		assert (line && !strcmp (line, i ? "62657461" : "616c706861"));
		assert (LocalNextLine (&cursor)); // colors
		assert (LocalNextLine (&cursor)); // frags
		for (int parm = 0; parm < NUM_BASIC_SPAWN_PARMS; ++parm)
			assert (LocalNextLine (&cursor));
	}
	assert (q_snprintf (expected_reference, sizeof (expected_reference),
		"\"enemy\" \"%d\"", reference_slot) > 0);
	assert (strstr (contents, expected_reference));
	free (lines);
	free (contents);
}

static void LocalSetOrigin (edict_t *ent, const vec3_t origin)
{
	VectorCopy (origin, ent->v.origin);
	SV_LinkEdict (ent, false);
}

/* Derive a controlled forward/capacity boundary from a real v7 writer body;
 * the original two-identity witness stays untouched. This is not a donor save. */
static void LocalRewriteChainTarget (const char *source, const char *destination, int slot)
{
	char *contents = LocalReadAutosave (source);
	char *chain = strstr (contents, "\"chain\" \"");
	char *end;
	FILE *file = fopen (va ("%s/%s", com_gamedir, destination), "wb");
	assert (file && chain && (end = strchr (chain, '\n')));
	assert (fwrite (contents, 1, chain - contents, file) == (size_t)(chain - contents));
	assert (fprintf (file, "\"chain\" \"%d\"", slot) > 0 && fputs (end, file) >= 0);
	assert (fclose (file) == 0);
	free (contents);
}

static void LocalReferenceCapacityAndTeardown (void)
{
	extern jmp_buf host_abortserver;
	ED_AllocHook_func previous;
	LocalRewriteChainTarget ("reverse-coop-witness.sav", "reference-capacity.sav", qcvm->max_edicts - 1);
	local_staged_allocations = 0;
	previous = ED_AllocSetHook (LocalObserveFreshAllocation);
	/* The real error owner aborts this host frame. The local client is connected,
	 * so the dedicated bootstrap does not take the dedicated-console exit. */
	if (!setjmp (host_abortserver))
	{
		PR_SwitchQCVM (NULL);
		Cmd_ExecuteString ("load reference-capacity", src_command);
		assert (!"capacity load must fail before allocating/staging anchors");
	}
	assert (!sv.active && !local_staged_allocations);
	PR_SwitchQCVM (&sv.qcvm);
	assert (EDICT_NUM (0)->v.enemy == 2 * qcvm->edict_size &&
		EDICT_NUM (0)->v.owner == qcvm->edict_size &&
		LocalPendingClient (0)->v.owner == qcvm->edict_size &&
		LocalPendingClient (0)->v.enemy == 2 * qcvm->edict_size);
	for (int i = 0; i < MAX_SCOREBOARD; ++i)
		assert (!sv.loadgame_client_reference_anchors[i]);
	ED_AllocSetHook (previous);
	PR_SwitchQCVM (NULL);
	fprintf (stderr, "COOP_REFERENCE_CAPACITY_PASSED preflight no-anchor-allocation/no-reference-relocation\n");
	client_t *beta = LocalLoadAs ("load reverse-coop-witness", "beta");
	assert (beta && sv.loadgame_client_saved[0]);
	LocalAssertAnchor (sv.loadgame_client_reference_anchors[0]);
	PR_SwitchQCVM (NULL);
	Cmd_ExecuteString ("map e1m1", src_command);
	assert (sv.active && !sv.loadgame_client_edicts);
	PR_SwitchQCVM (&sv.qcvm);
	for (int i = 0; i < MAX_SCOREBOARD; ++i)
		assert (!sv.loadgame_client_saved[i] && !sv.loadgame_client_reference_anchors[i]);
	for (int i = 0; i < qcvm->num_edicts; ++i)
		assert (!EDICT_NUM (i)->retain_count);
	fprintf (stderr, "COOP_REFERENCE_TEARDOWN_PASSED pending-anchor destroyed by native map/VM teardown\n");
}

static void LocalCoopLifecycle (client_t *alpha)
{
	client_t *beta;
	edict_t *health, *key, *weapon, *counter, *world = EDICT_NUM (0);
	edict_t *teledeath, *start, *coop_spawn, *reference;
	vec3_t beta_origin, saved_beta_origin, delta;
	float before_health, before_count, beta_nails, alpha_nails;
	int reference_slot, free_target, pending_anchor, beta_anchor;
	edict_t *retained_reference;
	const char *counter_name;
	assert (svs.maxclients == 2 && coop.value && !deathmatch.value &&
		alpha && alpha->active && alpha->spawned && alpha->netconnection &&
		alpha->netconnection->driverdata == cls.netcon);
	LocalStrings ((const char *[]){"name alpha"}, 1);
	alpha = &svs.clients[0];
	beta = LocalSpawnPeerAs (1, "beta");
	assert (alpha->private_pmove_walk_selected && beta->private_pmove_walk_selected);

	/* Compare the real hull trace, team-damage guard and teledeath gate under
	 * their classic and default native policies. */
	VectorCopy (beta->edict->v.origin, beta_origin);
	VectorCopy (alpha->edict->v.origin, delta);
	delta[0] += 8.0f;
	LocalSetOrigin (beta->edict, delta);
	Cvar_Set ("sv_coop_classic", "1");
	Cvar_Set ("sv_coop_noplayerclip", "-1");
	assert (SV_TestEntityPosition (alpha->edict) == beta->edict);
	Cvar_Set ("sv_coop_classic", "0");
	assert (SV_TestEntityPosition (alpha->edict) == NULL);
	LocalSetOrigin (beta->edict, beta_origin);

	before_health = beta->edict->v.health;
	assert (Cvar_VariableValue ("sv_nofriendlyfire") == 0.0f);
	LocalApplyDamage (beta->edict, alpha->edict, alpha->edict, 5.0f);
	assert (beta->edict->v.health < before_health);
	beta->edict->v.health = before_health;
	Cvar_Set ("sv_nofriendlyfire", "1");
	assert (SV_CoopFriendlyFireBegin (alpha->edict));
	LocalApplyDamage (beta->edict, alpha->edict, alpha->edict, 25.0f);
	SV_CoopFriendlyFireEnd ();
	assert (beta->edict->v.health == before_health &&
		beta->edict->v.takedamage != DAMAGE_NO);
	Cvar_Set ("sv_nofriendlyfire", "0");

	teledeath = ED_Alloc ();
	assert (teledeath && teledeath->free == false);
	teledeath->v.classname = PR_SetEngineString ("teledeath");
	teledeath->v.owner = EDICT_TO_PROG (alpha->edict);
	Cvar_Set ("sv_coop_notelefrag", "-1");
	assert (SV_ShouldSuppressCoopTelefrag (teledeath, beta->edict));
	assert (!SV_ShouldSuppressCoopTelefrag (teledeath, alpha->edict));
	Cvar_Set ("sv_coop_notelefrag", "0");
	assert (!SV_ShouldSuppressCoopTelefrag (teledeath, beta->edict));
	Cvar_Set ("sv_coop_notelefrag", "-1");
	ED_Free (teledeath);
	fprintf (stderr, "COOP_COLLISION_FF_TELEFRAG_PASSED classic hull/default no-player-clip; QC damage/native protection; native teledeath gate\n");

	/* Actual loaded e1m3 QC pickups exercise native team-key/weapon ownership,
	 * distinct ammo, and the loaded SUB_UseTargets counter exactly once. */
	health = LocalFindClass ("item_health");
	key = LocalFindClass ("item_key2");
	weapon = LocalFindClass ("weapon_nailgun");
	counter = LocalFindUniqueCounter ();
	assert (health && key && weapon && counter && health->v.touch && key->v.touch &&
		weapon->v.touch && counter->v.use &&
		LocalFloatField (counter, "count")->_float > 0.0f);
	Cvar_Set ("sv_coop_shared_pickups", "1");
	Cvar_Set ("sv_coop_pickup_targetfix", "1");
	Cvar_Set ("sv_coop_pickup_targetfix_classes", "item_health");
	alpha->edict->v.health = 50.0f;
	alpha->edict->v.items = (int)alpha->edict->v.items & ~(IT_KEY2 | IT_NAILGUN);
	beta->edict->v.items = (int)beta->edict->v.items & ~(IT_KEY2 | IT_NAILGUN);
	alpha->edict->v.ammo_nails = 10.0f;
	beta->edict->v.ammo_nails = 6.0f;
	LocalFloatField (counter, "count")->_float = 5.0f;
	before_count = LocalFloatField (counter, "count")->_float;
	counter_name = PR_GetString (counter->v.targetname);
	assert (counter_name && counter_name[0]);
	health->v.target = PR_SetEngineString (counter_name);
	/* Signon can consume the spawn-adjacent map pickups. Re-arm those same
	 * loaded map edicts so their stock QC touch callbacks run under policy. */
	health->v.solid = SOLID_TRIGGER;
	key->v.solid = SOLID_TRIGGER;
	weapon->v.solid = SOLID_TRIGGER;
	LocalSetOrigin (health, alpha->edict->v.origin);
	SV_LinkEdict (alpha->edict, true);
	assert (!health->free && health->v.solid == SOLID_NOT &&
		alpha->edict->v.health > 50.0f &&
		LocalFloatField (counter, "count")->_float == before_count - 1.0f);

	LocalSetOrigin (key, alpha->edict->v.origin);
	SV_LinkEdict (alpha->edict, true);
	assert (((int)alpha->edict->v.items & IT_KEY2) &&
		((int)beta->edict->v.items & IT_KEY2));
	LocalSetOrigin (weapon, alpha->edict->v.origin);
	SV_LinkEdict (alpha->edict, true);
	assert (((int)alpha->edict->v.items & IT_NAILGUN) &&
		((int)beta->edict->v.items & IT_NAILGUN));
	assert (beta->edict->v.ammo_nails == 6.0f &&
		alpha->edict->v.ammo_nails > 10.0f);
	alpha_nails = alpha->edict->v.ammo_nails;
	beta_nails = beta->edict->v.ammo_nails;
	fprintf (stderr, "COOP_SHARED_PICKUPS_PASSED key2=shared nailgun=shared ammo-alpha=%.0f ammo-beta=%.0f target-counter=once\n",
		(double)alpha_nails, (double)beta_nails);

	/* Keep the actual producer's old command pending, then write the real v7
	 * two-identity save with a live entity reference to beta's edict. */
	Cvar_Set ("sv_coop_respawn_near_player", "1");
	Cvar_Set ("sv_coop_respawn_delay", "2");
	alpha->edict->v.armorvalue = 41.25f;
	beta->edict->v.armorvalue = 82.5f;
	VectorCopy (beta->edict->v.origin, saved_beta_origin);
	LocalFrame (); // refresh the native death-inventory cache before save
	LocalSend (true, 0, 0.0f); // private move sequences zero and one are warmup
	LocalSend (true, 0, 0.0f);
	LocalSend (true, 0, 100.0f);
	NetworkBuffer ();
	SV_RunClients ();
	assert (alpha->private_cmd_queue_count && !alpha->private_completed_move);
	world->v.enemy = EDICT_TO_PROG (beta->edict);
	LocalApplyDamage (alpha->edict, world, world, 500.0f);
	assert (alpha->edict->v.health <= 0.0f);
	world->v.owner = EDICT_TO_PROG (alpha->edict);
	alpha->edict->v.owner = EDICT_TO_PROG (alpha->edict);
	alpha->edict->v.enemy = EDICT_TO_PROG (beta->edict);
	beta->edict->v.owner = EDICT_TO_PROG (beta->edict);
	beta->edict->v.enemy = EDICT_TO_PROG (alpha->edict);
	*LocalEntityGlobal ("le1") = EDICT_TO_PROG (beta->edict);
	*LocalEntityGlobal ("le2") = EDICT_TO_PROG (alpha->edict);
	reference = ED_AllocFresh ();
	world->v.chain = EDICT_TO_PROG (reference);
	ED_Free (reference);
	world->v.frags = (float)EDICT_TO_PROG (beta->edict); // nonentity numeric control
	SaveFixture ("reverse-coop-witness", 7);
	free_target = qcvm->num_edicts + 7;
	assert (free_target + 2 < qcvm->max_edicts);
	LocalRewriteChainTarget ("reverse-coop-witness.sav", "reverse-coop-forward.sav", free_target);
	reference_slot = NUM_FOR_EDICT (beta->edict);
	LocalAssertTwoIdentitySave ("reverse-coop-witness.sav", reference_slot);
	fprintf (stderr, "COOP_V07_SAVE_PASSED identities=alpha,beta active=2 dead-alpha=1 reference-slot=%d\n",
		reference_slot);

	/* Beta is deliberately the first reconnect although it occupied saved
	 * slot two. Alpha's old local command/queue must not cross that identity. */
	local_staged_allocations = 0;
	ED_AllocHook_func previous_hook = ED_AllocSetHook (LocalObserveFreshAllocation);
	beta = LocalLoadAs ("load reverse-coop-forward", "beta");
	ED_AllocSetHook (previous_hook);
	world = EDICT_NUM (0); // Host_Loadgame rebuilt the QCVM/edict array
	pending_anchor = sv.loadgame_client_reference_anchors[0];
	beta_anchor = (free_target + 2) * qcvm->edict_size;
	assert (sv.loadgame && sv.loadgame_client_saved[0] &&
		!sv.loadgame_client_saved[1] && EDICT_NUM (reference_slot)->free &&
		PROG_TO_EDICT (world->v.enemy) == beta->edict && world->v.owner == pending_anchor);
	assert (pending_anchor == (free_target + 1) * qcvm->edict_size &&
		local_staged_allocations >= 2 && EDICT_NUM (free_target)->free &&
		!EDICT_NUM (free_target)->retain_count &&
		!PROG_TO_EDICT (beta_anchor)->retain_count && LocalFreeListContains (free_target + 2));
	LocalAssertAnchor (pending_anchor);
	char previous_save[sizeof sv.lastsave];
	memcpy (previous_save, sv.lastsave, sizeof previous_save);
	Cmd_ExecuteString ("save reference-pending-refused", src_command);
	assert (access (va ("%s/reference-pending-refused.sav", com_gamedir), F_OK) != 0 &&
		access (va ("%s/reference-pending-refused.sav.tmp", com_gamedir), F_OK) != 0 &&
		!memcmp (previous_save, sv.lastsave, sizeof previous_save) &&
		sv.loadgame_client_saved[0] && sv.loadgame_client_reference_anchors[0] == pending_anchor);
	LocalAssertAnchor (pending_anchor);
	fprintf (stderr, "COOP_REFERENCE_PENDING_SAVE_REFUSED_PASSED existing-refusal no-file/lastsave-change anchor-preserved\n");
	assert (beta->edict->v.owner == EDICT_TO_PROG (beta->edict) &&
		beta->edict->v.enemy == pending_anchor &&
		LocalPendingClient (0)->v.owner == pending_anchor &&
		LocalPendingClient (0)->v.enemy == EDICT_TO_PROG (beta->edict) &&
		*LocalEntityGlobal ("le1") == EDICT_TO_PROG (beta->edict) &&
		*LocalEntityGlobal ("le2") == pending_anchor &&
		world->v.frags == (float)(reference_slot * qcvm->edict_size));
	assert (!beta->private_cmd_queue_count && !beta->private_completed_move &&
		!memcmp (saved_beta_origin, beta->edict->v.origin, sizeof delta) &&
		beta->edict->v.armorvalue == 82.5f && beta->edict->v.ammo_nails == beta_nails);
	LocalExerciseAnchorAllocator (pending_anchor, free_target);
	/* Let real frames run while alpha is pending. Fresh references to beta1 must
	 * survive the later alpha1->live2 resolution. Keep beta clear of pickups. */
	start = LocalFindClass ("info_player_start");
	assert (start);
	VectorCopy (start->v.origin, delta);
	delta[2] += 128.0f;
	LocalSetOrigin (beta->edict, delta);
	for (int frame = 0; frame < 3; ++frame)
	{
		LocalSend (true, 0, 0.0f);
		LocalFrame ();
	}
	world->v.aiment = EDICT_TO_PROG (beta->edict);
	*LocalEntityGlobal ("le1") = EDICT_TO_PROG (beta->edict);
	/* Equal raw values in float/vector/function fields are deliberately untyped
	 * controls. No callbacks dereference these world controls. */
	memcpy (&world->v.frags, &pending_anchor, sizeof pending_anchor);
	memcpy (&world->v.movedir[0], &pending_anchor, sizeof pending_anchor);
	world->v.think = pending_anchor;
	/* Native retention permits callback readers to access a freed payload.
	 * Its typed pending identity must resolve before the anchor is released. */
	retained_reference = ED_AllocFresh ();
	retained_reference->v.owner = pending_anchor;
	retained_reference->v.enemy = EDICT_TO_PROG (beta->edict);
	ED_Retain (retained_reference);
	ED_Free (retained_reference);
	assert (retained_reference->free && retained_reference->retain_count == 1 &&
		retained_reference->v.owner == pending_anchor);
	client_t *newcomer = LocalSpawnPeerAs (1, "charlie");
	assert (sv.loadgame_client_saved[0] && world->v.owner == pending_anchor &&
		newcomer->edict == EDICT_NUM (2));
	newcomer->edict->v.enemy = EDICT_TO_PROG (beta->edict);
	LocalDropPreparedPeer (newcomer);
	assert (sv.loadgame_client_saved[0] && sv.loadgame_client_reference_anchors[0] == pending_anchor);
	alpha = LocalSpawnPeerAs (1, "alpha");
	assert (!sv.loadgame && !sv.loadgame_client_saved[0] &&
		!sv.loadgame_client_saved[1]);
	assert (beta->edict == EDICT_NUM (1) && alpha->edict == EDICT_NUM (2) &&
		beta->edict->v.armorvalue == 82.5f &&
		alpha->edict->v.ammo_nails == alpha_nails &&
		((int)alpha->edict->v.items & (IT_KEY2 | IT_NAILGUN)) ==
		(IT_KEY2 | IT_NAILGUN) &&
		((int)beta->edict->v.items & (IT_KEY2 | IT_NAILGUN)) ==
		(IT_KEY2 | IT_NAILGUN) && beta->edict->v.ammo_nails == beta_nails);
	assert (world->v.enemy == EDICT_TO_PROG (beta->edict) &&
		world->v.owner == EDICT_TO_PROG (alpha->edict) &&
		world->v.aiment == EDICT_TO_PROG (beta->edict) &&
		beta->edict->v.owner == EDICT_TO_PROG (beta->edict) &&
		beta->edict->v.enemy == EDICT_TO_PROG (alpha->edict) &&
		*LocalEntityGlobal ("le1") == EDICT_TO_PROG (beta->edict) &&
		*LocalEntityGlobal ("le2") == EDICT_TO_PROG (alpha->edict) &&
		!memcmp (&world->v.frags, &pending_anchor, sizeof pending_anchor) &&
		!memcmp (&world->v.movedir[0], &pending_anchor, sizeof pending_anchor) &&
		world->v.think == pending_anchor && !PROG_TO_EDICT (pending_anchor)->retain_count &&
		LocalFreeListContains (pending_anchor / qcvm->edict_size));
	assert (retained_reference->free && retained_reference->retain_count == 1 &&
		retained_reference->v.owner == EDICT_TO_PROG (alpha->edict) &&
		retained_reference->v.enemy == EDICT_TO_PROG (beta->edict));
	ED_Release (retained_reference);
	assert (!retained_reference->retain_count &&
		LocalFreeListContains (NUM_FOR_EDICT (retained_reference)));
	fprintf (stderr, "COOP_REFERENCE_RETAINED_FREE_PASSED typed pending container follows identity before anchor release\n");
	world->v.frags = world->v.movedir[0] = world->v.think = 0;
	for (int i = 0; i < MAX_SCOREBOARD; ++i)
		assert (!sv.loadgame_client_reference_anchors[i]);
	fprintf (stderr, "COOP_REFERENCE_TYPED_PENDING_PASSED world/global/self/cross pending-payload new-beta-refs nonentity-controls newcomer/drop released\n");
	fprintf (stderr, "COOP_V07_REVERSE_RECONNECT_PASSED beta-first own-state queues-reset beta-ammo=%.0f alpha-ammo=%.0f named-spawn alpha-dead-inventory-restored\n",
		(double)beta->edict->v.ammo_nails, (double)alpha->edict->v.ammo_nails);

	/* Exercise the native near-player placement owner and real local input
	 * cooldown/respawn after load; the second identity remains an independent
	 * survivor while beta dies and respawns. */
	start = LocalFindClass ("info_player_start");
	coop_spawn = LocalFindClass ("info_player_coop");
	assert (start && coop_spawn);
	LocalSetOrigin (alpha->edict, coop_spawn->v.origin);
	LocalSetOrigin (beta->edict, start->v.origin);
	const float survivor_health = alpha->edict->v.health;
	LocalApplyDamage (beta->edict, world, world, 500.0f);
	assert (beta->edict->v.health <= 0.0f);
	LocalFrame ();
	LocalSend (true, BUTTON_ATTACK, 0.0f);
	LocalFrame ();
	assert (beta->edict->v.health <= 0.0f); // native two-second respawn fence
	for (int frame = 0; frame < (int)ceil (2.1f / host_frametime); ++frame)
	{
		LocalSend (true, 0, 0.0f);
		LocalFrame ();
	}
	assert (beta->edict->v.health <= 0.0f);
	LocalSend (true, BUTTON_ATTACK, 0.0f);
	LocalFrame ();
	fprintf (stderr, "COOP_RESPAWN_OBSERVED beta-health=%g alpha-health=%g survivor-health=%g deadflag=%g buttons=%g time=%g\n",
		beta->edict->v.health, alpha->edict->v.health, survivor_health,
		beta->edict->v.deadflag, beta->edict->v.button0, qcvm->time);
	assert (beta->edict->v.health > 0.0f && alpha->edict->v.health == survivor_health);
	assert (SV_CoopRespawnPlaceNearPlayer (beta->edict));
	VectorSubtract (beta->edict->v.origin, alpha->edict->v.origin, delta);
	assert (VectorLength (delta) < 128.0f);
	fprintf (stderr, "COOP_RESPAWN_COOLDOWN_NEAR_PASSED native delay=2s near-helper-anchor=alpha survivor-preserved\n");

	/* A saved reference to beta's now-free slot must follow beta through the
	 * reverse-order restore, rather than aliasing alpha when slot two is reused. */
	reference = PROG_TO_EDICT (world->v.enemy);
	fprintf (stderr, "COOP_V07_REFERENCE_IDENTITY expected-slot=%d expected-beta=%d actual-slot=%d actual-free=%d\n",
		reference_slot, NUM_FOR_EDICT (beta->edict), NUM_FOR_EDICT (reference), reference->free);
	fflush (stderr);
	assert (reference == beta->edict);
	fprintf (stderr, "COOP_V07_REFERENCE_IDENTITY_PASSED saved-reference-follows-beta\n");
	const float alpha_health = alpha->edict->v.health;
	const float beta_health = beta->edict->v.health;
	LocalApplyDamage (reference, world, world, 10.0f);
	assert (beta->edict->v.health < beta_health && alpha->edict->v.health == alpha_health);
	fprintf (stderr, "COOP_REFERENCE_QC_CONSUMER_PASSED T_Damage through saved-reference affects only beta\n");
	/* Both players are now living: a second save/load covers actual full payload
	 * self/cross restoration, and proves released anchors do not serialize live. */
	alpha->edict->v.owner = EDICT_TO_PROG (alpha->edict);
	alpha->edict->v.enemy = EDICT_TO_PROG (beta->edict);
	beta->edict->v.owner = EDICT_TO_PROG (beta->edict);
	beta->edict->v.enemy = EDICT_TO_PROG (alpha->edict);
	SaveFixture ("references-resolved", 7);
	beta = LocalLoadAs ("load references-resolved", "beta");
	alpha = LocalSpawnPeerAs (1, "alpha");
	world = EDICT_NUM (0);
	assert (PROG_TO_EDICT (world->v.enemy) == beta->edict &&
		PROG_TO_EDICT (world->v.owner) == alpha->edict &&
		PROG_TO_EDICT (alpha->edict->v.owner) == alpha->edict &&
		PROG_TO_EDICT (alpha->edict->v.enemy) == beta->edict &&
		PROG_TO_EDICT (beta->edict->v.owner) == beta->edict &&
		PROG_TO_EDICT (beta->edict->v.enemy) == alpha->edict);
	for (int i = 0; i < MAX_SCOREBOARD; ++i)
		assert (!sv.loadgame_client_reference_anchors[i]);
	for (int i = 0; i < qcvm->num_edicts; ++i)
		assert (!EDICT_NUM (i)->retain_count);
	fprintf (stderr, "COOP_REFERENCE_RESAVE_PASSED living self/cross references no-retained-anchors\n");
	LocalReferenceCapacityAndTeardown ();
}

static void LocalReferenceCallbackCancellation (client_t *alpha)
{
	ddef_t *cancel, *calls;
	client_t *beta;
	edict_t *world = EDICT_NUM (0);
	int anchor, callbacks;
	LocalStrings ((const char *[]){"name alpha"}, 1);
	beta = LocalSpawnPeerAs (1, "beta");
	LocalApplyDamage (alpha->edict, world, world, 500.0f);
	assert (alpha->edict->v.health <= 0.0f);
	world->v.enemy = EDICT_TO_PROG (beta->edict);
	world->v.owner = EDICT_TO_PROG (alpha->edict);
	alpha->edict->v.enemy = EDICT_TO_PROG (beta->edict);
	SaveFixture ("reference-cancel", 7);
	beta = LocalLoadAs ("load reference-cancel", "beta");
	world = EDICT_NUM (0);
	anchor = sv.loadgame_client_reference_anchors[0];
	LocalAssertAnchor (anchor);
	assert (PROG_TO_EDICT (world->v.enemy) == beta->edict && world->v.owner == anchor);
	cancel = ED_FindGlobal ("fixture_cancel_spawn");
	calls = ED_FindGlobal ("fixture_spawn_callbacks");
	assert (cancel && calls);
	callbacks = (int)qcvm->globals[calls->ofs];
	qcvm->globals[cancel->ofs] = 1;
	alpha = LocalAttemptPeerAs (1, "alpha", false);
	assert (qcvm->globals[calls->ofs] == callbacks + 1 &&
		sv.loadgame_client_saved[0] && sv.loadgame_client_reference_anchors[0] == anchor &&
		LocalPendingClient (0)->v.enemy == EDICT_TO_PROG (beta->edict) &&
		world->v.owner == anchor && PROG_TO_EDICT (world->v.enemy) == beta->edict);
	LocalAssertAnchor (anchor);
	LocalDropPreparedPeer (alpha);
	qcvm->globals[cancel->ofs] = 0;
	alpha = LocalSpawnPeerAs (1, "alpha");
	assert (qcvm->globals[calls->ofs] == callbacks + 2 && !sv.loadgame_client_saved[0] &&
		!sv.loadgame_client_reference_anchors[0] && !PROG_TO_EDICT (anchor)->retain_count &&
		PROG_TO_EDICT (world->v.owner) == alpha->edict &&
		PROG_TO_EDICT (world->v.enemy) == beta->edict &&
		!alpha->private_cmd_queue_count && !beta->private_cmd_queue_count);
	fprintf (stderr, "COOP_REFERENCE_CANCEL_PASSED loaded-QC remove(self) cancels dead-spawn pending-payload/anchor preserved reconnect resolves/releases\n");
}

int main (int argc, char **argv)
{
	const char *scenario;
	int arg;
	scenario = NULL;
	for (int i = 1; i + 1 < argc; ++i)
		if (!strcmp (argv[i], "-localcase"))
			scenario = argv[i + 1];
	assert (scenario);
	Fixture_InitNativeEngine (argc, argv,
		!strcmp (scenario, "coop-lifecycle") ? "e1m3" : "e1m1", true);
	network_buffer = net_message;
	host_frametime = .025;
	arg = COM_CheckParm ("-localcase");
	assert (arg && arg + 1 < com_argc);
	scenario = com_argv[arg + 1];
	const qboolean public = !strncmp (scenario, "public", 6);
	const qboolean disabled = !strcmp (scenario, "disabled");
	if (disabled) Cvar_Set ("sv_private_pmove_walk", "0");
	client_t *peer = LocalSignon (false, public);
	const qboolean selected = !public && !disabled;
	if (!strcmp (scenario, "coop-ref-cancel"))
	{
		LocalReferenceCallbackCancellation (peer);
		fprintf (stderr, "LOCAL_LOAD_NATIVE_PASSED case=%s loaded-QC callback cancellation\n", scenario);
		return 0;
	}
	if (!strcmp (scenario, "coop-lifecycle"))
	{
		assert (peer->private_pmove_walk_selected == selected);
		LocalCoopLifecycle (peer);
		fprintf (stderr, "COOP_LIFECYCLE_NATIVE_PASSED loaded=e1m3 stock-QC loopback-first prepared-second\n");
		fprintf (stderr, "LOCAL_LOAD_NATIVE_PASSED case=%s typed-reference lifecycle\n", scenario);
		return 0;
	}
	assert (peer->private_pmove_walk_selected == selected);
	if (!strcmp (scenario, "autosave"))
	{
		LocalCoopAutosave (peer);
		printf ("LOCAL_LOAD_NATIVE_PASSED case=%s loopback commands/physics/full snapshots; prepared renderer signon\n", scenario);
		return 0;
	}
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
