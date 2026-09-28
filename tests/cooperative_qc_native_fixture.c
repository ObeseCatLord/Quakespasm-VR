/* Prepared cooperative QC loaded normally; actual public/private admission,
 * input sender/receipt, world physics and full datagram parser. Licensed stock
 * base/assets stay outside the repository. Transport and signon are captured. */
#define MIXED_NATIVE_FIXTURE_ENTRY ImportedCooperativeMixedMain
#include "mixed_native_fixture.c"
#include "../Quake/sv_phys.c"

static edict_t *fixture_trigger, *fixture_player, *fixture_nested;
static int trigger_calls, nested_calls;
void __real_PR_ExecuteProgram (func_t function);
void __wrap_PR_ExecuteProgram (func_t function)
{
	const qboolean touch = fixture_trigger && function == fixture_trigger->v.touch &&
		pr_global_struct->self == EDICT_TO_PROG (fixture_trigger) &&
		pr_global_struct->other == EDICT_TO_PROG (fixture_player);
	__real_PR_ExecuteProgram (function);
	if (!touch) return;
	++trigger_calls;
	if (nested_calls) return;
	++nested_calls;
	/* Prepared composition after actual SUB_Null dispatched by real links:
	 * another loaded QC hook invokes the real builtin inside this callback. */
	playermove_t saved = pmove;
	movevars_t saved_vars = movevars;
	int self = pr_global_struct->self, other = pr_global_struct->other;
	float time = pr_global_struct->time, frame = pr_global_struct->frametime;
	vec3_t origin;
	sv_qc_input_scope_t inputs;
	SV_SaveQCInputs (&inputs); // prepared nested caller borrows these globals
	VectorCopy (fixture_nested->v.origin, origin);
	pr_global_struct->self = EDICT_TO_PROG (fixture_nested);
	__real_PR_ExecuteProgram (qcvm->extfuncs.SV_RunClientCommand);
	SV_RestoreQCInputs (&inputs);
	assert (!memcmp (&saved, &pmove, sizeof (saved)) &&
		!memcmp (&saved_vars, &movevars, sizeof (saved_vars)) &&
		VectorCompare (fixture_nested->v.origin, origin));
	pr_global_struct->self = self; pr_global_struct->other = other;
	pr_global_struct->time = time; pr_global_struct->frametime = frame;
}

static float FixtureGlobal (const char *name)
{
	ddef_t *definition = ED_FindGlobal (name);
	assert (definition && (definition->type & ~DEF_SAVEGLOBAL) == ev_float);
	return G_FLOAT (definition->ofs);
}

static void FullSnapshot (client_t *peer, client_state_t *state)
{
	static byte bytes[NET_MAXMESSAGE];
	cl = *state; cls.netcon = peer->netconnection;
	host_client = peer; sv_player = peer->edict;
	SV_PresendClientDatagram (peer);
	captured_length = captured_sends = 0;
	assert (SV_SendClientDatagram (peer) && captured_sends == 1 && captured_length > 0);
	memcpy (bytes, captured, captured_length);
	net_message.data = bytes; net_message.maxsize = sizeof (bytes);
	net_message.cursize = captured_length;
	CL_ParseServerMessage ();
	assert (msg_readcount == net_message.cursize);
	*state = cl;
}

static void SendCooperative (client_t *peer, client_state_t *state, qboolean vr,
	float roomscale)
{
	usercmd_t command = {0};
	cl = *state; cls.netcon = peer->netconnection; cl.time = qcvm->time;
	command.servertime = cl.time;
	command.forwardmove = 100;
	command.vr_active = command.vr_handpos_relative = vr;
	command.vr_roomscalemove[1] = roomscale;
	command.vr_handpos[1] = 16; command.vr_handpos[2] = 22;
	captured_length = 0;
	CL_SendMove (&command);
	*state = cl;
	if (captured_length) GapDeliver (peer, captured, captured_length);
}

int main (int argc, char **argv)
{
	client_t *peers[2];
	client_state_t *states[2];
	char private_offer[1024], public_offer[1024];
	Fixture_InitNativeEngine (argc, argv, "e1m1", true);
	const qboolean selected = COM_CheckParm ("-defaultselection") != 0;
	ConfigurePrivateMovementFixture (selected, selected); // native hook or default selected-native control
	dfunction_t *standard = ED_FindFunction ("runstandardplayerphysics");
	printf ("COOPERATIVE_QC_LOADER hook=%d builtin=%d input=%d field=%d\n",
		qcvm->extfuncs.SV_RunClientCommand, standard ? standard->first_statement : 1,
		qcvm->extglobals.input_movevalues != NULL && qcvm->extglobals.input_timelength != NULL,
		ED_FindFieldOffset ("pmove_flags"));
	assert (qcvm->extfuncs.SV_RunClientCommand && standard && standard->first_statement == -347 &&
		qcvm->extglobals.input_movevalues && qcvm->extglobals.input_timelength &&
		ED_FindFieldOffset ("pmove_flags") >= 0);
	ClientOffer (0, false, private_offer, sizeof (private_offer));
	ClientOffer (QSVR_PROTOCOL_PINNED, false, public_offer, sizeof (public_offer));
	peers[0] = SpawnPeer (0, private_offer, QSVR_PROTOCOL_PINNED);
	peers[1] = SpawnPeer (1, public_offer, 0);
	cls.signon = SIGNONS; cls.state = ca_connected;
	cls.demoplayback = false; cls.legacy_qsvr = 0;
	host_frametime = .025;
	for (int slot = 0; slot < 2; ++slot)
	{
		states[slot] = CreateMixedPeerState (peers[slot], slot);
		host_client = peers[slot]; sv_player = peers[slot]->edict;
		Cmd_ExecuteString ("notarget 1", src_client);
	}
	for (int frame = 0; frame < 3; ++frame)
	{
		realtime += host_frametime;
		SendCooperative (peers[0], states[0], true, 0);
		SendCooperative (peers[1], states[1], false, 0);
		GapWorldFrame ();
		for (int slot = 0; slot < 2; ++slot) FullSnapshot (peers[slot], states[slot]);
	}
	/* Explicit resting ground starts use actual BSP hull traces. The first
	 * standard step's known50-unit QC wish speed detects extra native input
	 * acceleration or a second ordinary world move. */
	vec3_t start[2];
	for (int slot = 0; slot < 2; ++slot)
	{
		edict_t *player = peers[slot]->edict;
		trace_t floor = SV_Move (player->v.origin, player->v.mins, player->v.maxs,
			(vec3_t){player->v.origin[0], player->v.origin[1], player->v.origin[2] - 256},
			MOVE_NORMAL, player);
		assert (!floor.startsolid && !floor.allsolid && floor.fraction < 1 && floor.plane.normal[2] > .7);
		VectorCopy (floor.endpos, player->v.origin);
		VectorClear (player->v.velocity); VectorClear (player->v.v_angle); VectorClear (player->v.angles);
		player->v.flags = FL_CLIENT | FL_ONGROUND | FL_JUMPRELEASED;
		player->v.groundentity = floor.ent ? EDICT_TO_PROG (floor.ent) : 0;
		player->v.teleport_time = 0;
		SV_CheckWater (player); SV_LinkEdict (player, false);
		assert (!player->v.waterlevel);
		VectorCopy (player->v.origin, start[slot]);
	}
	const float calls = FixtureGlobal ("fixture_hook_calls");
	const float seconds = FixtureGlobal ("fixture_hook_seconds");
	const int expected_calls = FixtureGlobal ("fixture_expected_calls");
	const float standard_calls = FixtureGlobal ("fixture_standard_calls");
	fixture_player = peers[0]->edict;
	fixture_trigger = ED_Alloc ();
	fixture_trigger->v.solid = SOLID_TRIGGER;
	fixture_trigger->v.movetype = MOVETYPE_NONE;
	fixture_trigger->v.touch = ED_FindFunction ("SUB_Null") - qcvm->functions;
	fixture_trigger->v.classname = PR_SetEngineString ("trigger_multiple");
	VectorCopy (fixture_player->v.origin, fixture_trigger->v.origin);
	VectorSet (fixture_trigger->v.mins, -64, -64, -64);
	VectorSet (fixture_trigger->v.maxs, 64, 64, 64);
	SV_LinkEdict (fixture_trigger, false);
	fixture_nested = ED_Alloc ();
	fixture_nested->v.movetype = MOVETYPE_NONE;
	VectorCopy (fixture_player->v.origin, fixture_nested->v.origin);
	fixture_nested->v.origin[2] += 128;
	VectorCopy (fixture_nested->v.origin, fixture_nested->v.oldorigin);
	SV_LinkEdict (fixture_nested, false);
	float saved_cursor[] = {.125f, .25f, .375f};
	assert (qcvm->extglobals.input_cursor_screen);
	VectorCopy (saved_cursor, qcvm->extglobals.input_cursor_screen);
	realtime += host_frametime;
	SendCooperative (peers[0], states[0], true, .5);
	SendCooperative (peers[1], states[1], false, 0);
	if (captured_length) GapDeliver (peers[1], captured, captured_length); // duplicate public packet
	GapWorldFrame ();
	assert (FixtureGlobal ("fixture_hook_calls") == calls + 2 + nested_calls &&
		fabsf (FixtureGlobal ("fixture_hook_seconds") - seconds - .05f -
			(expected_calls ? .025f / expected_calls : 0)) < .000001f &&
		FixtureGlobal ("fixture_standard_calls") == standard_calls + (2 + nested_calls) * expected_calls &&
		trigger_calls == expected_calls && nested_calls == (expected_calls != 0) &&
		!memcmp (saved_cursor, qcvm->extglobals.input_cursor_screen, sizeof (saved_cursor)));
	/* Existing NQ friction order makes the deliberate two12.5ms calls differ
	 * from one25ms call; do not claim integrator equivalence. */
	const float expected_velocity = expected_calls == 0 ? 0 : expected_calls == 1 ? 12.5f : 7.5f;
	const float expected_distance = expected_calls == 0 ? 0 : expected_calls == 1 ? .3125f : .171875f;
	for (int slot = 0; slot < 2; ++slot)
	{
		edict_t *player = peers[slot]->edict;
		printf ("COOPERATIVE_QC_BODY slot=%d x=%g y=%g velocity=%g health=%g\n", slot,
			player->v.origin[0] - start[slot][0], player->v.origin[1] - start[slot][1],
			player->v.velocity[0], player->v.health);
		assert (peers[slot]->active && peers[slot]->private_pmove_walk_selected == (selected && slot == 0) &&
			fabsf (player->v.velocity[0] - expected_velocity) < .001f &&
			fabsf (player->v.origin[0] - start[slot][0] - expected_distance) < .001f &&
			fabsf (player->v.origin[1] - start[slot][1] - (slot ? 0 : .5f)) < .001f);
		FullSnapshot (peers[slot], states[slot]);
		assert (states[slot]->move_ack_selected_owner == (selected && slot == 0) &&
			!states[slot]->move_ack_prediction_allowed &&
			states[slot]->entities[1].netstate.modelindex > 0 &&
			states[slot]->entities[2].netstate.modelindex > 0);
	}
	/* Validation probes the same builtin backend before VM error reporting;
	 * invalid QC scalars and a zero duration must leave body/scratch intact. */
	const playermove_t saved_move = pmove;
	const movevars_t saved_vars = movevars;
	vec3_t body;
	VectorCopy (peers[0]->edict->v.origin, body);
	const float saved_duration = *qcvm->extglobals.input_timelength;
	const float saved_sequence = *qcvm->extglobals.input_sequence;
	*qcvm->extglobals.input_timelength = 0;
	assert (!SV_RunStandardPlayerPhysics (peers[0]->edict));
	*qcvm->extglobals.input_timelength = NAN;
	assert (SV_RunStandardPlayerPhysics (peers[0]->edict));
	*qcvm->extglobals.input_timelength = .05;
	assert (SV_RunStandardPlayerPhysics (peers[0]->edict));
	*qcvm->extglobals.input_timelength = .025;
	*qcvm->extglobals.input_sequence = INFINITY;
	assert (SV_RunStandardPlayerPhysics (peers[0]->edict));
	assert (VectorCompare (peers[0]->edict->v.origin, body) &&
		!memcmp (&saved_move, &pmove, sizeof (pmove)) &&
		!memcmp (&saved_vars, &movevars, sizeof (movevars)));
	*qcvm->extglobals.input_timelength = saved_duration;
	*qcvm->extglobals.input_sequence = saved_sequence;
	printf ("COOPERATIVE_QC_NATIVE_PASSED calls_per_hook=%d actual loader/builtin/input transform/public-private/world duration/owned body move/roomscale/full-send/raw vector restoration; prepared QC/resting starts/nested callback and captured transport\n", expected_calls);
	return 0;
}
