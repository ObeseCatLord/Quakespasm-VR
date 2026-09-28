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
	SV_PresendClientDatagram (peer);
	net_message.data = bytes;
	net_message.maxsize = capacity;
	SZ_Clear (&net_message);
	if (SV_PrivateWalkTrialSelected (peer))
		assert (!SV_PrivateWalkTrialAdmissionFailure (peer));
	SVFTE_WriteStats (peer, &net_message);
	if (SV_PrivateWalkTrialSelected (peer))
		assert (SVFTE_WritePrivateMoveStats (peer, &net_message));
	assert (SVFTE_WriteEntitiesToClient (peer, &net_message, capacity, false));
	CL_ParseServerMessage (); // commits stats/ACK/owner at the real message end
	assert (msg_readcount == net_message.cursize);
}

int main (int argc, char **argv)
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
	selected = COM_CheckParm ("-selected") != 0;
	assert (svs.maxclients >= 2);
	/* Enable the existing profile explicitly for this component proof. The
	 * separate negotiation fixture owns verification of production defaults. */
	Cvar_SetQuick (&sv_qsvr_private, "1");
	assert (!sv_private_pmove_walk.value);
	if (selected) Cvar_SetQuick (&sv_private_pmove_walk, "1");
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
		states[slot] = Mem_Alloc (sizeof (*states[slot]));
		states[slot]->protocol = sv.protocol;
		states[slot]->protocolflags = sv.protocolflags;
		states[slot]->protocol_pext2 = peers[slot]->protocol_pext2;
		states[slot]->protocol_qsvr = peers[slot]->protocol_qsvr;
		states[slot]->viewentity = slot + 1;
		states[slot]->worldmodel = sv.qcvm.worldmodel;
		states[slot]->maxclients = svs.maxclients;
		states[slot]->scores = Mem_Alloc (svs.maxclients * sizeof (*cl.scores));
		states[slot]->gametype = GAME_COOP;
		states[slot]->ackedmovemessages = -1;
		states[slot]->max_edicts = qcvm->max_edicts;
		states[slot]->num_entities = 1;
		states[slot]->entities = Mem_Alloc (qcvm->max_edicts * sizeof (*cl.entities));
		for (int model = 0; model < MAX_MODELS; model++)
			states[slot]->model_precache[model] = sv.models[model];
		/* Reset deltas depend on signon baselines (notably player models).
		 * Consume the actual baseline codec rather than copying server state. */
		cl = *states[slot];
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
		*states[slot] = cl;
		VectorCopy (peers[slot]->edict->v.origin, start[slot]);
		VectorCopy (peers[slot]->edict->v.angles, states[slot]->viewangles);
		initial_shells[slot] = peers[slot]->edict->v.ammo_shells;
	}
	host_frametime = .025;
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
