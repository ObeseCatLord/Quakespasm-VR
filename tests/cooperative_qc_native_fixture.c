/* Prepared cooperative QC loaded normally; actual public/private admission,
 * input sender/receipt, world physics and full datagram parser. Licensed stock
 * base/assets stay outside the repository. Transport and signon are captured. */
#define MIXED_NATIVE_FIXTURE_ENTRY ImportedCooperativeMixedMain
#include "mixed_native_fixture.c"
#include "../Quake/sv_phys.c"

static edict_t *fixture_trigger, *fixture_player, *fixture_nested;
static int trigger_calls, nested_calls;
static qboolean observe_commands;
static int observed_pre, observed_post, observed_think, observed_hooks, observed_custom;
static qboolean schedule_after_post, custom_handoff, invalid_post;
static int later_due_think_waited;
typedef struct
{
	float seconds, frame, sequence, buttons, impulse;
} fixture_observed_input_t;
static fixture_observed_input_t observed_input[8], observed_pre_input[8];
static fixture_observed_input_t ObserveInput (void)
{
	return (fixture_observed_input_t){*qcvm->extglobals.input_timelength,
		pr_global_struct->frametime, *qcvm->extglobals.input_sequence,
		*qcvm->extglobals.input_buttons, *qcvm->extglobals.input_impulse};
}
static void ResetCommandObservation (void)
{
	observed_pre = observed_post = observed_think = observed_hooks = observed_custom = 0;
	memset (observed_input, 0, sizeof (observed_input));
	memset (observed_pre_input, 0, sizeof (observed_pre_input));
}
void __real_PR_ExecuteProgram (func_t function);
void __wrap_PR_ExecuteProgram (func_t function)
{
	if (observe_commands && pr_global_struct->self == EDICT_TO_PROG (fixture_player))
	{
		if (function == pr_global_struct->PlayerPreThink || function == pr_global_struct->PlayerPostThink)
			assert (pr_global_struct->frametime == *qcvm->extglobals.input_timelength);
		if (function == pr_global_struct->PlayerPreThink)
		{
			assert (observed_pre < countof (observed_pre_input));
			observed_pre_input[observed_pre++] = ObserveInput ();
		}
		if (function == pr_global_struct->PlayerPostThink) ++observed_post;
		if (function == qcvm->extfuncs.SV_RunClientCommand)
		{
			assert (observed_hooks < countof (observed_input));
			observed_input[observed_hooks] = ObserveInput ();
			if (schedule_after_post && observed_hooks == 1)
			{
				assert (fixture_player->v.nextthink > 0 &&
					fixture_player->v.nextthink <= qcvm->time + .025 && observed_think == 1);
				++later_due_think_waited;
			}
			++observed_hooks;
		}
		eval_t *custom = GetEdictFieldValue (fixture_player, qcvm->extfields.customphysics);
		if (custom && custom->function && function == custom->function)
			++observed_custom;
		else if (function == fixture_player->v.think)
		{
			++observed_think;
			assert (fabsf (pr_global_struct->frametime - .025f) < .000001f &&
				fabs (host_frametime - .025) < .000001);
		}
	}
	const qboolean actor = observe_commands && pr_global_struct->self == EDICT_TO_PROG (fixture_player);
	const qboolean touch = fixture_trigger && function == fixture_trigger->v.touch &&
		pr_global_struct->self == EDICT_TO_PROG (fixture_trigger) &&
		pr_global_struct->other == EDICT_TO_PROG (fixture_player);
	__real_PR_ExecuteProgram (function);
	if (actor && invalid_post && function == pr_global_struct->PlayerPostThink)
		fixture_player->v.nextthink = NAN;
	if (actor && schedule_after_post && function == pr_global_struct->PlayerPostThink && observed_post == 1)
	{
		fixture_player->v.think = ED_FindFunction ("SUB_Null") - qcvm->functions;
		fixture_player->v.nextthink = qcvm->time + .001;
	}
	if (actor && custom_handoff)
	{
		eval_t *custom = GetEdictFieldValue (fixture_player, qcvm->extfields.customphysics);
		assert (custom);
		if (function == pr_global_struct->PlayerPreThink)
			custom->function = ED_FindFunction ("SUB_Null") - qcvm->functions;
		if (function == pr_global_struct->PlayerPostThink)
			custom->function = 0; // restore eligibility; the engine must retain the earlier fence
	}
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

static void RunCooperativeCommandChecks (client_t *peer, client_state_t *state)
{
	edict_t *player = peer->edict;
	assert (peer->private_pmove_walk_selected && FixtureGlobal ("fixture_expected_calls") == 1 &&
		!peer->private_cmd_queue_count && ((int)player->v.flags & FL_ONGROUND));
	/* Prepared clock carry and zero bank isolate accepted10/15ms timing from
	 * bootstrap time. Commands still use the actual sampler/writer/receiver. */
	peer->private_pmove_credit_msec = 0;
	state->move_msec_sample_time = realtime;
	state->move_msec_fractional_carry = .25;
	VectorClear (state->viewangles); // prepared axes, matching resting body assertions
	VectorClear (player->v.velocity);
	vec3_t start;
	VectorCopy (player->v.origin, start);
	player->v.think = ED_FindFunction ("SUB_Null") - qcvm->functions;
	player->v.nextthink = qcvm->time + .01;
	observe_commands = true;
	schedule_after_post = true;
	later_due_think_waited = 0;
	ResetCommandObservation ();
	realtime += .010;
	GapSend (peer, state, 0, 0, .2);
	GapDeliver (peer, captured, captured_length);
	const int first = state->cmd.sequence;
	assert (state->cmd.msec == 10);
	realtime += .015;
	GapSend (peer, state, 8, 1, .3);
	GapDeliver (peer, captured, captured_length);
	GapDeliver (peer, captured, captured_length); // redundant delivery must not repeat QC
	const int second = state->cmd.sequence;
	assert (second == first + 1 && state->cmd.msec == 15 && peer->private_cmd_queue_count == 2);
	GapWorldFrame ();
	schedule_after_post = false;
	printf ("COOPERATIVE_COMMAND_BATCH pre=%d hook=%d post=%d think=%d x=%g y=%g credit=%g\n",
		observed_pre, observed_hooks, observed_post, observed_think,
		player->v.origin[0] - start[0], player->v.origin[1] - start[1], peer->private_pmove_credit_msec);
	assert (observed_pre == 2 && observed_hooks == 2 && observed_post == 2 && observed_think == 1 &&
		later_due_think_waited == 1 &&
		fabsf (observed_input[0].seconds - .010f) < .000001f &&
		fabsf (observed_input[1].seconds - .015f) < .000001f &&
		observed_input[0].frame == observed_input[0].seconds &&
		observed_input[1].frame == observed_input[1].seconds &&
		observed_input[0].sequence == first && observed_input[1].sequence == second &&
		observed_input[0].buttons == 0 && observed_input[1].buttons == 8 &&
		observed_input[0].impulse == 0 && observed_input[1].impulse == 1 &&
		peer->private_completed_move == second && peer->private_retired_move == second &&
		!peer->private_cmd_queue_count && fabs (peer->private_pmove_credit_msec) < .000001 &&
		fabsf (player->v.velocity[0] - 7.5f) < .001f &&
		fabsf (player->v.origin[0] - start[0] - .1625f) < .001f &&
		fabsf (player->v.origin[1] - start[1] - .5f) < .001f);
	FullSnapshot (peer, state);
	assert (state->ackedmovemessages == second && !state->move_ack_prediction_allowed);

	ResetCommandObservation ();
	VectorCopy (player->v.origin, start);
	player->v.think = ED_FindFunction ("SUB_Null") - qcvm->functions;
	player->v.nextthink = qcvm->time + .01;
	realtime += .050;
	GapSend (peer, state, 0, 2, 1);
	GapDeliver (peer, captured, captured_length);
	const int pending = state->cmd.sequence;
	assert (pending == second + 1 && state->cmd.msec == 50);
	float hook_calls = FixtureGlobal ("fixture_hook_calls");
	GapWorldFrame (); // only25ms credit: positive head must stay invisible
	assert (observed_pre == 1 && observed_hooks == 0 && observed_post == 1 && observed_think == 1 &&
		FixtureGlobal ("fixture_hook_calls") == hook_calls + 1 && // public hook alone; no repeated private side effect
		observed_pre_input[0].seconds == 0 && observed_pre_input[0].frame == 0 &&
		observed_pre_input[0].sequence == second && observed_pre_input[0].buttons == 8 &&
		observed_pre_input[0].impulse == 0 && VectorCompare (player->v.origin, start) &&
		peer->private_completed_move == second && peer->private_retired_move == second &&
		peer->private_cmd_queue_count == 1 && fabs (peer->private_pmove_credit_msec - 25) < .000001);
	FullSnapshot (peer, state);
	assert (state->ackedmovemessages == second && !state->move_ack_prediction_allowed);

	ResetCommandObservation ();
	GapWorldFrame (); // no new input; accumulated50ms now permits this head
	assert (observed_pre == 1 && observed_hooks == 1 && observed_post == 1 && observed_think == 0 &&
		fabsf (observed_input[0].seconds - .050f) < .000001f &&
		observed_input[0].frame == observed_input[0].seconds &&
		observed_input[0].sequence == pending && observed_input[0].buttons == 0 &&
		observed_input[0].impulse == 2 && peer->private_completed_move == pending &&
		peer->private_retired_move == pending && !peer->private_cmd_queue_count &&
		fabs (peer->private_pmove_credit_msec) < .000001 &&
		fabsf (player->v.velocity[0] - 25) < .001f &&
		fabsf (player->v.origin[0] - start[0] - 1.25f) < .001f &&
		fabsf (player->v.origin[1] - start[1] - 1) < .001f);
	FullSnapshot (peer, state);
	assert (state->ackedmovemessages == pending && !state->move_ack_prediction_allowed);
	ResetCommandObservation ();
	VectorCopy (player->v.origin, start);
	const int touches = trigger_calls;
	hook_calls = FixtureGlobal ("fixture_hook_calls");
	GapWorldFrame (); // genuinely empty queue: no positive movement or completion
	assert (observed_pre == 1 && observed_hooks == 0 && observed_post == 1 &&
		FixtureGlobal ("fixture_hook_calls") == hook_calls + 1 &&
		observed_pre_input[0].seconds == 0 && observed_pre_input[0].frame == 0 &&
		observed_pre_input[0].sequence == pending && observed_pre_input[0].impulse == 0 &&
		VectorCompare (player->v.origin, start) && trigger_calls == touches &&
		peer->private_completed_move == pending && peer->private_retired_move == pending &&
		!peer->private_cmd_queue_count && fabs (peer->private_pmove_credit_msec - 25) < .000001);
	FullSnapshot (peer, state);
	assert (state->ackedmovemessages == pending && !state->move_ack_prediction_allowed);

	/* Prepared native customphysics during PreThink, cleared by PostThink.
	 * Rechecking only final eligibility would wrongly execute the second head. */
	peer->private_pmove_credit_msec = 0;
	state->move_msec_sample_time = realtime;
	state->move_msec_fractional_carry = .25;
	VectorCopy (player->v.origin, start);
	ResetCommandObservation ();
	custom_handoff = true;
	realtime += .010;
	GapSend (peer, state, 0, 0, .2);
	GapDeliver (peer, captured, captured_length);
	const int handoff = state->cmd.sequence;
	realtime += .015;
	GapSend (peer, state, 8, 0, .3);
	GapDeliver (peer, captured, captured_length);
	const int suffix = state->cmd.sequence;
	assert (state->cmd.msec == 15 && peer->private_cmd_queue_count == 2);
	GapWorldFrame ();
	custom_handoff = false;
	assert (observed_pre == 1 && observed_post == 1 && observed_custom == 1 && observed_hooks == 0 &&
		SV_CooperativeCommandOwner (peer) && peer->private_move_native_frame &&
		peer->private_completed_move == handoff && peer->private_retired_move == handoff &&
		peer->private_cmd_queue_count == 1 && peer->private_cmd_queue_msec == 15 &&
		peer->private_cmd_queue[peer->private_cmd_queue_head].sequence == (unsigned)suffix &&
		fabs (peer->private_pmove_credit_msec) < .000001 &&
		fabsf (player->v.origin[0] - start[0]) < .001f &&
		fabsf (player->v.origin[1] - start[1] - .2f) < .001f);
	FullSnapshot (peer, state);
	assert (state->ackedmovemessages == handoff && !state->move_ack_prediction_allowed);
	ResetCommandObservation ();
	GapWorldFrame ();
	assert (observed_pre == 1 && observed_hooks == 1 && observed_post == 1 && !observed_custom &&
		peer->private_completed_move == suffix && peer->private_retired_move == suffix &&
		!peer->private_cmd_queue_count && fabs (peer->private_pmove_credit_msec - 10) < .000001);
	FullSnapshot (peer, state);
	assert (state->ackedmovemessages == suffix && !state->move_ack_prediction_allowed);
	observe_commands = false;
	printf ("COOPERATIVE_COMMANDS_PASSED actual accepted10/15/50ms/batch transition/duplicate/zero-time maintenance/world Think/retained credit/full-parser; prepared bank/carry/Think and captured delivery\n");
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
	if (COM_CheckParm ("-commandchecks"))
		RunCooperativeCommandChecks (peers[0], states[0]);
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
	if (COM_CheckParm ("-invalidpost"))
	{
		assert (selected && !peers[0]->private_cmd_queue_count);
		/* NewQSocket was called outside a driver by the captured bootstrap.
		 * Give this prepared endpoint real loopback close ownership for Drop. */
		peers[0]->netconnection->driver = 0;
		const int completed = peers[0]->private_completed_move;
		observe_commands = invalid_post = true;
		ResetCommandObservation ();
		realtime += host_frametime;
		GapWorldFrame (); // actual quiet lifecycle, with a prepared invalid PostThink write
		assert (!peers[0]->active && peers[1]->active && observed_pre == 1 &&
			observed_post == 1 && !observed_hooks && peers[0]->private_completed_move == completed &&
			!peers[0]->edict->retain_count && !memcmp (&saved_move, &pmove, sizeof (pmove)) &&
			!memcmp (&saved_vars, &movevars, sizeof (movevars)));
		observe_commands = invalid_post = false;
		puts ("COOPERATIVE_INVALID_POST_PASSED actual quiet PostThink/validation/drop; prepared NaN deadline and captured socket");
	}
	printf ("COOPERATIVE_QC_NATIVE_PASSED calls_per_hook=%d actual loader/builtin/input transform/public-private/world duration/owned body move/roomscale/full-send/raw vector restoration; prepared QC/resting starts/nested callback and captured transport\n", expected_calls);
	return 0;
}
