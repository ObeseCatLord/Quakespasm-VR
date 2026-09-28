/* Initial nonstock admission through real offer/spawn/begin/QC/body/parser.
 * Reuses native owners; initial state and transport/signon are prepared.
 * Licensed programs/assets remain external. */
#define MIXED_NATIVE_FIXTURE_ENTRY ImportedInitialStateMixedMain
#include "mixed_native_fixture.c"
#include "../Quake/sv_phys.c"
#include "native_liquid_fixture.h"

static edict_t *initial_actor;
static int initial_hooks, initial_custom_calls;
void __real_PR_ExecuteProgram (func_t function);
void __wrap_PR_ExecuteProgram (func_t function)
{
	if (initial_actor && pr_global_struct->self == EDICT_TO_PROG (initial_actor))
	{
		if (function == qcvm->extfuncs.SV_RunClientCommand) ++initial_hooks;
		eval_t *custom = GetEdictFieldValue (initial_actor, qcvm->extfields.customphysics);
		if (custom && custom->function && function == custom->function) ++initial_custom_calls;
	}
	__real_PR_ExecuteProgram (function);
}

static void InitialSnapshot (client_t *peer, client_state_t *state)
{
	static byte bytes[NET_MAXMESSAGE];
	cl = *state; cls.netcon = peer->netconnection;
	host_client = peer; sv_player = peer->edict;
	SV_PresendClientDatagram (peer);
	captured_length = captured_sends = 0;
	assert (SV_SendClientDatagram (peer) && captured_sends == 1 && captured_length > 0);
	memcpy (bytes, captured, captured_length);
	net_message.data = bytes; net_message.maxsize = sizeof bytes;
	net_message.cursize = captured_length;
	CL_ParseServerMessage ();
	assert (msg_readcount == net_message.cursize);
	if (SV_PrivateWalkTrialSelected (peer))
		assert ((cl_move_stat_receipts & CL_MOVE_STAT_RECEIPTS_COMPLETE) ==
			CL_MOVE_STAT_RECEIPTS_COMPLETE && CL_ReceivedMoveStatsUsable ());
	*state = cl;
	for (int axis = 0; axis < 3; ++axis)
		assert (fabsf (state->entities[state->viewentity].netstate.origin[axis] -
			peer->edict->v.origin[axis]) <= .125f);
}

static void InitialSend (client_t *peer, client_state_t *state, qboolean vr)
{
	usercmd_t command = {0};
	cl = *state; cls.netcon = peer->netconnection; cl.time = qcvm->time;
	VectorClear (cl.viewangles); // prepared input axes after actual spawn setangle
	command.servertime = cl.time;
	command.forwardmove = 100;
	command.vr_active = command.vr_handpos_relative = vr;
	VectorCopy (cl.viewangles, command.viewangles);
	captured_length = 0;
	CL_SendMove (&command);
	*state = cl;
	if (captured_length) GapDeliver (peer, captured, captured_length);
}

int main (int argc, char **argv)
{
	Fixture_InitNativeEngine (argc, argv, "e1m1", true);
	ConfigurePrivateMovementFixture (true, true);
	const int arg = COM_CheckParm ("-initialstate");
	assert (arg && arg + 1 < com_argc);
	const char *scenario = com_argv[arg + 1];
	const qboolean cooperative = qcvm->extfuncs.SV_RunClientCommand != 0;
	assert (!SV_PrivateWalkTrialStockProgram ()); // actually loaded foreign QC
	ClientOffer (0, false, modern_offer, sizeof modern_offer);
	client_t *peer = Negotiate (0, modern_offer, QSVR_PROTOCOL_PINNED);
	sv_player = peer->edict;
	Cmd_ExecuteString ("name private", src_client);
	Cmd_ExecuteString ("spawn", src_client);
	Cmd_ExecuteString ("notarget 1", src_client);
	assert (peer->knowntoqc && !peer->spawned && !peer->private_pmove_walk_selected);
	initial_actor = peer->edict;
	vec3_t dry;
	VectorCopy (initial_actor->v.origin, dry);
	const float original_type = initial_actor->v.movetype;
	const int original_ground = initial_actor->v.groundentity;
	const float original_nextthink = initial_actor->v.nextthink;
	const int original_think = initial_actor->v.think;
	eval_t *custom = GetEdictFieldValue (initial_actor, qcvm->extfields.customphysics);
	eval_t *gravity = GetEdictFieldValue (initial_actor, qcvm->extfields.gravity);
	const float original_gravity = gravity ? gravity->_float : 0;
	qboolean expected_selected = true;
	if (!strcmp (scenario, "wet"))
	{
		vec3_t wet;
		assert (FindLiquidPosition (initial_actor, CONTENTS_WATER, 3, wet));
		char relocate[128];
		q_snprintf (relocate, sizeof relocate, "setpos %.6f %.6f %.6f 0 0 0", wet[0], wet[1], wet[2]);
		Cmd_ExecuteString (relocate, src_client);
		Cmd_ExecuteString ("noclip 0", src_client); // setpos deliberately enters noclip
		SV_CheckWater (initial_actor);
		assert (initial_actor->v.waterlevel == 3 && initial_actor->v.movetype == MOVETYPE_WALK);
	}
	else if (!strcmp (scenario, "fly")) initial_actor->v.movetype = MOVETYPE_FLY;
	else if (!strcmp (scenario, "noclip"))
	{
		Cmd_ExecuteString ("noclip 1", src_client);
		assert (initial_actor->v.movetype == MOVETYPE_NOCLIP);
	}
	else if (!strcmp (scenario, "custom-hull"))
	{
		VectorSet (initial_actor->v.mins, -12, -12, -20);
		VectorSet (initial_actor->v.maxs, 12, 12, 28);
		SV_LinkEdict (initial_actor, false);
	}
	else if (!strcmp (scenario, "customphysics"))
	{
		assert (cooperative && custom);
		custom->function = ED_FindFunction ("SUB_Null") - qcvm->functions;
	}
	else
	{
		expected_selected = false;
		if (!strcmp (scenario, "invalid-ground")) initial_actor->v.groundentity = 1;
		else if (!strcmp (scenario, "stale-ground"))
		{
			edict_t *ground = ED_Alloc ();
			initial_actor->v.groundentity = EDICT_TO_PROG (ground);
			ED_Free (ground);
		}
		else if (!strcmp (scenario, "invalid-think"))
		{
			initial_actor->v.nextthink = qcvm->time + .01;
			initial_actor->v.think = qcvm->progs->numfunctions;
		}
		else if (!strcmp (scenario, "invalid-custom"))
		{
			assert (custom);
			custom->function = qcvm->progs->numfunctions;
		}
		else if (!strcmp (scenario, "invalid-gravity"))
		{
			assert (gravity);
			gravity->_float = NAN;
		}
		else
		{
			assert (!strcmp (scenario, "unsupported-type"));
			initial_actor->v.movetype = MOVETYPE_PUSH;
		}
	}
	/* Validation is observational. Compare all QC fields and native client
	 * storage, including queue/selection/capability state, before actual begin. */
	const size_t vars_size = qcvm->progs->entityfields * sizeof (float);
	void *saved_vars = Mem_Alloc (vars_size);
	client_t *saved_client = Mem_Alloc (sizeof *peer);
	memcpy (saved_vars, &initial_actor->v, vars_size);
	memcpy (saved_client, peer, sizeof *peer);
	const char *failure = SV_PrivateWalkTrialAdmissionFailure (peer);
	printf ("INITIAL_MOD_ADMISSION case=%s cooperative=%d reason=%s\n",
		scenario, cooperative, failure ? failure : "eligible");
	assert ((failure == NULL) == expected_selected &&
		!memcmp (saved_vars, &initial_actor->v, vars_size) &&
		!memcmp (saved_client, peer, sizeof *peer));
	Mem_Free (saved_vars); Mem_Free (saved_client);
	Cmd_ExecuteString ("begin", src_client);
	assert (peer->active && peer->spawned && peer->private_pmove_walk_selected == expected_selected);
	/* Invalid prepared boundary values are repaired for native gameplay.
	 * Begin must not retrospectively select that already begun owner. */
	if (!expected_selected)
	{
		initial_actor->v.movetype = original_type;
		initial_actor->v.groundentity = original_ground;
		initial_actor->v.nextthink = original_nextthink;
		initial_actor->v.think = original_think;
		if (custom) custom->function = 0;
		if (gravity) gravity->_float = original_gravity;
		Cmd_ExecuteString ("begin", src_client);
		assert (!peer->private_pmove_walk_selected);
	}
	char public_offer[1024];
	ClientOffer (QSVR_PROTOCOL_PINNED, false, public_offer, sizeof public_offer);
	client_t *public_peer = SpawnPeer (1, public_offer, 0);
	host_client = public_peer; sv_player = public_peer->edict;
	Cmd_ExecuteString ("notarget 1", src_client);
	client_state_t *state = CreateMixedPeerState (peer, 0);
	client_state_t *public_state = CreateMixedPeerState (public_peer, 1);
	cls.signon = SIGNONS; cls.state = ca_connected; cls.demoplayback = false; cls.legacy_qsvr = 0;
	VectorClear (state->viewangles); VectorClear (public_state->viewangles);
	/* First publication precedes all world callbacks and command receipt.
	 * A NATIVE owner must export/parse its stats even with an empty queue. */
	assert (!peer->private_cmd_queue_count);
	if (expected_selected)
		assert (SV_PrivateWalkTrialClassifyState (peer) == SV_PRIVATE_MOVE_NATIVE);
	InitialSnapshot (peer, state); InitialSnapshot (public_peer, public_state);
	assert (peer->active && public_peer->active &&
		!state->move_ack_selected_owner && !state->move_ack_prediction_allowed &&
		state->ackedmovemessages == -1);
	if (expected_selected)
		assert ((state->stats[STAT_MOVEFLAGS] & MOVEFLAG_VALID) && state->net_move_stale_acks > 0);
	printf ("INITIAL_MOD_FIRST_SNAPSHOT selected=%d class=%d stats=%d stale_ack=%d accepted_ack=%d\n",
		peer->private_pmove_walk_selected, SV_PrivateWalkTrialClassifyState (peer),
		(state->stats[STAT_MOVEFLAGS] & MOVEFLAG_VALID) != 0,
		state->net_move_stale_acks, state->ackedmovemessages);
	VectorClear (initial_actor->v.velocity);
	vec3_t start, public_start;
	VectorCopy (initial_actor->v.origin, start);
	VectorCopy (public_peer->edict->v.origin, public_start);
	host_frametime = .025;
	for (int frame = 0; frame < 7; ++frame)
	{
		realtime += host_frametime;
		InitialSend (peer, state, true);
		if (frame == 4 && expected_selected)
		{
			/* Prepared sender clock: two actual accepted heads in one world
			 * pass, so native and cooperative owners retire the real batch. */
			realtime += .010;
			InitialSend (peer, state, true);
			assert (peer->private_cmd_queue_count >= 2);
		}
		if (frame == 3 && expected_selected)
		{
			const unsigned count = peer->private_cmd_queue_count;
			const int completed = peer->private_completed_move;
			assert (count > 0);
			host_client = peer; sv_player = peer->edict;
			Cmd_ExecuteString ("begin", src_client);
			assert (peer->private_pmove_walk_selected &&
				peer->private_cmd_queue_count == count && peer->private_completed_move == completed);
		}
		InitialSend (public_peer, public_state, false);
		GapWorldFrame ();
		InitialSnapshot (peer, state); InitialSnapshot (public_peer, public_state);
		assert (peer->active && public_peer->active &&
			peer->private_pmove_walk_selected == expected_selected &&
			state->move_ack_selected_owner == expected_selected &&
			!peer->private_cmd_queue_count);
		if (frame > 2)
			assert (state->ackedmovemessages == peer->lastmovemessage &&
				(!expected_selected || peer->private_completed_move == peer->lastmovemessage) &&
				(!expected_selected || peer->private_retired_move == peer->private_completed_move));
		if (cooperative || !expected_selected || !strcmp (scenario, "wet") ||
			!strcmp (scenario, "fly") || !strcmp (scenario, "noclip") || !strcmp (scenario, "custom-hull"))
			assert (!state->move_ack_prediction_allowed);
	}
	vec3_t travel, public_travel;
	VectorSubtract (initial_actor->v.origin, start, travel);
	VectorSubtract (public_peer->edict->v.origin, public_start, public_travel);
	assert (public_travel[0] > .1f);
	if (!strcmp (scenario, "customphysics"))
	{
		assert (initial_custom_calls > 0 && !initial_hooks && VectorLength (travel) < .001f);
		custom->function = 0;
		realtime += host_frametime;
		InitialSend (peer, state, true); InitialSend (public_peer, public_state, false);
		GapWorldFrame (); InitialSnapshot (peer, state);
		assert (initial_hooks > 0 && peer->private_pmove_walk_selected &&
			peer->private_completed_move == peer->lastmovemessage && !state->move_ack_prediction_allowed);
		VectorSubtract (initial_actor->v.origin, start, travel);
	}
	assert (travel[0] > .01f);
	if (!cooperative && expected_selected && !strcmp (scenario, "wet"))
	{
		/* Ordinary native-to-shared return, same admitted session and queue.
		 * Relocation is an actual engine command; no re-begin or injected mode. */
		host_client = peer; sv_player = peer->edict;
		char relocate[128];
		q_snprintf (relocate, sizeof relocate, "setpos %.6f %.6f %.6f 0 0 0", dry[0], dry[1], dry[2]);
		Cmd_ExecuteString (relocate, src_client);
		Cmd_ExecuteString ("noclip 0", src_client);
		SV_CheckWater (initial_actor);
		assert (!initial_actor->v.waterlevel);
		vec3_t return_start;
		VectorCopy (initial_actor->v.origin, return_start);
		const int return_completed = peer->private_completed_move;
		for (int frame = 0; frame < 5; ++frame)
		{
			realtime += host_frametime;
			InitialSend (peer, state, true); InitialSend (public_peer, public_state, false);
			GapWorldFrame (); InitialSnapshot (peer, state);
		}
		/* The earlier prepared35ms batch can leave a legal25ms head waiting
		 * on native-to-command credit. One ordinary no-send world pass drains
		 * that retained head; do not require execution ahead of its budget. */
		realtime += host_frametime;
		GapWorldFrame (); InitialSnapshot (peer, state);
		printf ("INITIAL_MOD_DRY_RETURN active=%d selected=%d class=%d allowed=%d native=%d phase=%d queue=%u type=%g solid=%g water=%g flags=%g teleport=%g time=%g held=%d\n",
			peer->active, peer->private_pmove_walk_selected, SV_PrivateWalkTrialClassifyState (peer),
			state->move_ack_prediction_allowed, peer->private_move_native_frame, peer->private_input_phase,
			peer->private_cmd_queue_count, initial_actor->v.movetype, initial_actor->v.solid,
			initial_actor->v.waterlevel, initial_actor->v.flags, initial_actor->v.teleport_time,
			qcvm->time, SV_PrivateWalkTrialMotionHeld (peer));
		assert (peer->active && peer->private_pmove_walk_selected &&
			SV_PrivateWalkTrialClassifyState (peer) == SV_PRIVATE_MOVE_WALK &&
			state->move_ack_prediction_allowed && !peer->private_cmd_queue_count &&
			peer->private_completed_move == peer->lastmovemessage &&
			peer->private_retired_move == peer->private_completed_move &&
			peer->private_completed_move > return_completed &&
			initial_actor->v.origin[0] > return_start[0] + .01f);
	}
	printf ("INITIAL_MOD_STATE_PASSED case=%s cooperative=%d selected=%d travel=%g public=%g hooks=%d custom=%d actual begin/owner/command completion/full parser; prepared state and captured transport\n",
		scenario, cooperative, expected_selected, VectorLength (travel), VectorLength (public_travel), initial_hooks, initial_custom_calls);
	return 0;
}
