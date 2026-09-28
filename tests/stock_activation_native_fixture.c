/* Untouched production defaults at actual spawn/begin, including native
 * admission exclusions. Reuse real QC/BSP/send/parser/replay owners; starts,
 * metadata exclusions and captured delivery remain explicit component seams. */
#define STOCK_LIQUID_FIXTURE_ENTRY ImportedStockActivationLiquidMain
#include "stock_liquid_native_fixture.c"

int main (int argc, char **argv)
{
	setvbuf (stdout, NULL, _IONBF, 0);
	Fixture_InitNativeEngine (argc, argv, "e1m2", true);
	ConfigurePrivateMovementFixture (true, true);
	int arg = COM_CheckParm ("-admissioncase");
	assert (arg && arg + 1 < com_argc);
	const char *scenario = com_argv[arg + 1], *reason = NULL;
	ClientOffer (0, false, modern_offer, sizeof (modern_offer));
	client_t *peer = Negotiate (0, modern_offer, QSVR_PROTOCOL_PINNED);
	sv_player = peer->edict;
	Cmd_ExecuteString ("name private", src_client);
	Cmd_ExecuteString ("spawn", src_client);
	Cmd_ExecuteString ("notarget 1", src_client);
	assert (peer->knowntoqc && !peer->spawned && !peer->private_pmove_walk_selected);
	unsigned saved_hash = qcvm->progshash;
	int saved_slots = svs.maxclients;
	if (!strcmp (scenario, "wet"))
	{
		vec3_t position;
		assert (FindLiquidPosition (peer->edict, CONTENTS_WATER, 2, position));
		char relocate[128];
		q_snprintf (relocate, sizeof relocate, "setpos %.6f %.6f %.6f 0 0 0",
			position[0], position[1], position[2]);
		Cmd_ExecuteString (relocate, src_client);
		SV_CheckWater (peer->edict);
		assert (peer->edict->v.waterlevel == 2);
		reason = "requires a stock WALK/SOLID_SLIDEBOX owner, dry at selection";
	}
	else if (!strcmp (scenario, "noclip"))
	{
		Cmd_ExecuteString ("noclip 1", src_client);
		assert (peer->edict->v.movetype == MOVETYPE_NOCLIP);
		reason = "requires a stock WALK/SOLID_SLIDEBOX owner, dry at selection";
	}
	else if (!strcmp (scenario, "load"))
	{
		sv.loadgame = true; // prepared admission boundary, not an actual save/load
		reason = "loadgame state is outside the trial";
	}
	else if (!strcmp (scenario, "program"))
	{
		qcvm->progshash ^= 1; // prepared identity mismatch, not a mod QC proof
		reason = "requires the pinned stock progs identity";
	}
	else if (!strcmp (scenario, "slots"))
	{
		svs.maxclients = 1; // prepared eligibility boundary, not local-SP transport
		reason = "requires a remote client on a server with multiple client slots";
	}
	else if (!strcmp (scenario, "elevators"))
	{
		Cvar_SetQuick (&sv_gameplayfix_elevators, "2");
		assert (!peer->edict->v.groundentity); // static spawn, not riding a brush
		reason = "requires robust elevator physics";
	}
	else
	{
		assert (!strcmp (scenario, "disabled"));
		Cvar_SetQuick (&sv_private_pmove_walk, "0");
	}
	if (reason)
		assert (!strcmp (SV_PrivateWalkTrialAdmissionFailure (peer), reason));
	Cmd_ExecuteString ("begin", src_client);
	assert (peer->active && peer->spawned && !peer->private_pmove_walk_selected);
	qcvm->progshash = saved_hash;
	svs.maxclients = saved_slots;
	sv.loadgame = false;
	/* Repeated begin never switches a living native owner retroactively. */
	Cmd_ExecuteString ("begin", src_client);
	assert (!peer->private_pmove_walk_selected);
	char public_offer[1024];
	ClientOffer (QSVR_PROTOCOL_PINNED, false, public_offer, sizeof public_offer);
	client_t *public_peer = SpawnPeer (1, public_offer, 0);
	host_client = public_peer; sv_player = public_peer->edict;
	Cmd_ExecuteString ("notarget 1", src_client);
	client_state_t *state = CreateMixedPeerState (peer, 0);
	cls.signon = SIGNONS; cls.state = ca_connected; cls.demoplayback = false;
	cls.legacy_qsvr = 0;
	vec3_t initial;
	VectorCopy (peer->edict->v.origin, initial);
	float shells = peer->edict->v.ammo_shells;
	host_frametime = .025;
	for (int frame = 0; frame < 50; ++frame)
	{
		realtime += host_frametime;
		LiquidSend (peer, state, BUTTON_ATTACK, 75,
			!strcmp (scenario, "wet") ? 50 : 0, false, 0);
		GapWorldFrame (); GapSnapshot (peer, state);
		assert (peer->active && !peer->private_pmove_walk_selected &&
			!state->move_ack_selected_owner && !state->move_ack_prediction_allowed &&
			!peer->private_cmd_queue_count);
		if (frame > 2)
			assert (state->ackedmovemessages == peer->lastmovemessage);
		cl = *state;
		vec3_t replay;
		assert (!CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], replay));
		*state = cl;
	}
	vec3_t travel;
	VectorSubtract (peer->edict->v.origin, initial, travel);
	assert (VectorLength (travel) > .1f && peer->edict->v.ammo_shells < shells);
	printf ("STOCK_DEFAULT_NATIVE_ADMISSION_PASSED case=%s movement=%g shells=%g->%g actual begin/QC/commands/snapshot; prepared boundary/captured delivery\n",
		scenario, VectorLength (travel), shells, peer->edict->v.ammo_shells);
	return 0;
}
