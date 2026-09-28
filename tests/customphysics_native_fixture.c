/* Real native dispatcher, loaded map/QC and engine entity allocator. This
 * replaces the executable entry point; no graphics, sockets or peer traffic.
 * Linker wrapping enables Loop_Init solely for offline dedicated startup. */
#include "../Quake/sv_phys.c"
#include <assert.h>
#include "native_engine_fixture.h"

static int think_calls, prethink_calls, postthink_calls, kill_prethink;
static qboolean kill_think, schedule_postthink;
static float expected_world_frametime;
static func_t fixture_think, fixture_prethink, fixture_postthink;
static int callback_slots[3], num_callbacks;
static int expected_impulse;
static int observed_buttons, observed_impulse;
extern cvar_t sv_altnoclip;
static char callback_order[64];
static int callback_count;
static qboolean native_comparison;

static void note_callback (char phase)
{
	assert (callback_count < sizeof (callback_order));
	callback_order[callback_count++] = phase;
}

/* Add a two-statement QC callback calling a diagnostic builtin. Keep every
 * existing function-map entry pointed at its corresponding copied record. */
static func_t install_callback (builtin_t callback)
{
	int count = qcvm->progs->numfunctions;
	int start = qcvm->progs->numstatements;
	dfunction_t *old_functions = qcvm->functions;
	dfunction_t *functions = Mem_Alloc ((count + 2) * sizeof (*functions));
	dstatement_t *statements = Mem_Alloc ((start + 2) * sizeof (*statements));
	int builtin_index = countof (qcvm->builtins) - 1 - num_callbacks;

	assert (builtin_index < countof (qcvm->builtins));
	for (int i = 0; i < count; ++i)
		assert (old_functions[i].first_statement != -builtin_index);
	memcpy (functions, old_functions, count * sizeof (*functions));
	memcpy (statements, qcvm->statements, start * sizeof (*statements));
	functions[count].first_statement = start;
	functions[count + 1].first_statement = -builtin_index;
	/* These no-argument diagnostics use independent high parm slots, reset
	 * after PutClientInServer. No fixture callback passes arguments. */
	assert (num_callbacks < countof (callback_slots));
	int slot = callback_slots[num_callbacks] = OFS_PARM5 + num_callbacks * 3;
	num_callbacks++;
	G_INT (slot) = count + 1;
	statements[start] = (dstatement_t){OP_CALL0, slot, 0, 0};
	statements[start + 1] = (dstatement_t){OP_DONE, 0, 0, 0};
	qcvm->builtins[builtin_index] = callback;
	for (uint32_t i = 0; i < HashMap_Size (qcvm->function_map); ++i)
	{
		dfunction_t **value = HashMap_GetValue (dfunction_t *, qcvm->function_map, i);
		*value = &functions[*value - old_functions];
	}
	qcvm->functions = functions;
	qcvm->statements = statements;
	qcvm->progs->numfunctions += 2;
	qcvm->progs->numstatements += 2;
	return count;
}

static void fixture_death (edict_t *ent)
{
	ent->v.health = 0;
	ent->v.deadflag = DEAD_DEAD;
	ent->v.movetype = MOVETYPE_TOSS;
}

static void observe_think (void)
{
	edict_t *ent = PROG_TO_EDICT (pr_global_struct->self);
	think_calls++;
	note_callback ('T');
	assert (fabsf (pr_global_struct->frametime - expected_world_frametime) < .000001f);
	assert (ent->v.impulse == expected_impulse);
	ent->v.nextthink = qcvm->time + .001f;
	if (kill_think)
		fixture_death (ent);
}

static void observe_prethink (void)
{
	edict_t *ent = PROG_TO_EDICT (pr_global_struct->self);
	prethink_calls++;
	note_callback ('P');
	if (native_comparison)
		assert (fabsf (pr_global_struct->frametime - expected_world_frametime) < .000001f);
	observed_buttons = (ent->v.button0 ? 1 : 0) | (ent->v.button2 ? 2 : 0);
	observed_impulse = ent->v.impulse;
	if (prethink_calls == kill_prethink)
		fixture_death (PROG_TO_EDICT (pr_global_struct->self));
}

static void observe_postthink (void)
{
	postthink_calls++;
	note_callback ('Q');
	if (native_comparison)
		assert (fabsf (pr_global_struct->frametime - expected_world_frametime) < .000001f);
	if (schedule_postthink)
		PROG_TO_EDICT (pr_global_struct->self)->v.nextthink = qcvm->time + .001f;
}

static void prepare_selected (edict_t *owner, client_t *client, int commands)
{
	SV_UnlinkEdict (owner);
	if (owner->free)
		ED_RemoveFromFreeList (owner);
	memset (&owner->v, 0, qcvm->progs->entityfields * sizeof (float));
	SV_ResetPrivateCommandQueue (client);
	owner->free = false;
	client->active = client->spawned = client->knowntoqc = true;
	client->message.data = client->msgbuf;
	client->message.maxsize = sizeof (client->msgbuf);
	SZ_Clear (&client->message);
	pr_global_struct->self = EDICT_TO_PROG (owner);
	PR_ExecuteProgram (pr_global_struct->SetNewParms);
	PR_ExecuteProgram (pr_global_struct->PutClientInServer);
	assert (owner->v.health > 0 && owner->v.deadflag == DEAD_NO);
	client->private_pmove_walk_selected = true;
	pr_global_struct->PlayerPreThink = fixture_prethink;
	pr_global_struct->PlayerPostThink = fixture_postthink;
	owner->v.think = fixture_think;
	owner->v.nextthink = qcvm->time + .001f;
	think_calls = prethink_calls = postthink_calls = kill_prethink = 0;
	callback_count = 0;
	kill_think = schedule_postthink = false;
	client->private_completed_move = 0;
	client->lastmovemessage = commands;
	client->private_cmd_queue_count = commands;
	client->private_cmd_queue_msec = 5 * commands;
	for (int i = 0; i < commands; ++i)
	{
		usercmd_t *command = &client->private_cmd_queue[i];
		command->sequence = i + 1;
		command->msec = 5;
	}
	G_INT (callback_slots[0]) = fixture_think + 1;
	G_INT (callback_slots[1]) = fixture_prethink + 1;
	G_INT (callback_slots[2]) = fixture_postthink + 1;
}

/* Compare the adapter with the existing native chain from the identical
 * loaded player/QC/client checkpoint. Diagnostic QC observes callbacks;
 * gameplay QC and negotiated admission are exercised by the mixed fixture. */
static void compare_native_states (edict_t *owner, client_t *client)
{
	const size_t vars_size = qcvm->progs->entityfields * sizeof (float);
	const size_t globals_size = qcvm->progs->numglobals * sizeof (float);
	void *vars = Mem_Alloc (vars_size), *globals = Mem_Alloc (globals_size);
	client_t *saved_client = Mem_Alloc (sizeof (*client));
	const int modes[] = {MOVETYPE_NOCLIP, MOVETYPE_NOCLIP, MOVETYPE_FLY, MOVETYPE_FLY};
	const int counts[] = {0, 2, 8};
	vec3_t native_origin, native_velocity, native_punch;
	vec3_t maintenance_origin, maintenance_velocity, maintenance_punch;
	vr_gorilla_state_t native_gorilla;
	const float saved_noclip = sv_altnoclip.value;
	const float saved_gorilla = sv_gorilla.value;
	Cvar_SetQuick (&sv_gorilla, "1");
	native_comparison = true;
	for (int mode = 0; mode < countof (modes); ++mode)
	for (int n = 0; n < countof (counts); ++n)
	{
		const int commands = counts[n];
		usercmd_t staged = {0};
		prepare_selected (owner, client, commands);
		owner->v.movetype = modes[mode];
		VectorSet (owner->v.velocity, 30, 12, 0);
		VectorSet (owner->v.punchangle, 5, 0, 0);
		client->vr_gorilla_capable = mode == 3;
		SV_ResetPrivateVRContactState (client);
		staged.forwardmove = 120;
		staged.sidemove = 40;
		staged.upmove = 20;
		staged.vr_active = true;
		VectorCopy (owner->v.v_angle, staged.viewangles);
		if (mode == 3)
		{
			staged.vr_gorilla.flags = VR_GORILLA_HANDS;
			VectorSet (staged.vr_gorilla.head, 0, 0, 4);
			VectorSet (staged.vr_gorilla.hand[0], 12, -12, 4);
			VectorSet (staged.vr_gorilla.hand[1], 12, 12, 4);
			assert (VRG_InputValid (&staged.vr_gorilla));
		}
		client->private_pmove_last_cmd = staged;
		client->private_pmove_last_cmd_valid = true;
		for (int i = 0; i < commands; ++i)
		{
			usercmd_t *queued = &client->private_cmd_queue[i];
			*queued = staged;
			queued->sequence = i + 1;
			queued->msec = 5;
			queued->seconds = .005f;
			queued->vr_roomscalemove[0] = .5f;
			if (!i) { queued->buttons = 3; queued->impulse = 7; }
		}
		/* What the ordinary parser would retain: last levels plus brief
		 * latches, and each fresh head translation exactly once. */
		staged.sequence = commands;
		staged.buttons = commands ? 3 : 0;
		staged.impulse = commands ? 7 : 0;
		staged.vr_roomscalemove[0] = .5f * commands;
		client->cmd = staged;
		client->private_latest_buttons = 0;
		VectorCopy (staged.viewangles, owner->v.v_angle);
		owner->v.button0 = owner->v.button2 = commands != 0;
		owner->v.impulse = staged.impulse;
		memcpy (vars, &owner->v, vars_size);
		memcpy (globals, qcvm->globals, globals_size);
		*saved_client = *client;
		Cvar_SetValueQuick (&sv_altnoclip, mode == 0 ? 0 : 1);
		for (int selected = 0; selected < 2; ++selected)
		{
			SV_UnlinkEdict (owner);
			memcpy (&owner->v, vars, vars_size);
			memcpy (qcvm->globals, globals, globals_size);
			*client = *saved_client;
			client->private_pmove_walk_selected = selected;
			host_client = client;
			sv_player = owner;
			SV_LinkEdict (owner, false);
			think_calls = prethink_calls = postthink_calls = 0;
			callback_count = 0;
			host_frametime = .04;
			pr_global_struct->frametime = expected_world_frametime = .04f;
			expected_impulse = commands ? 7 : 0;
			if (!selected) SV_ClientThink ();
			SV_Physics_Client (owner, 1);
			assert (client->active && owner->retain_count == 0);
			assert (think_calls == 1 && prethink_calls == 1 && postthink_calls == 1);
			assert (callback_count == 3 && !memcmp (callback_order, "PTQ", 3));
			assert (fabsf (owner->v.punchangle[0] - 4.6f) < .0001f);
			assert (observed_buttons == (commands ? 3 : 0) &&
				observed_impulse == expected_impulse);
			assert (client->private_completed_move == commands);
			assert (host_frametime == .04 && pr_global_struct->frametime == .04f);
			if (!selected)
			{
				VectorCopy (owner->v.origin, native_origin);
				VectorCopy (owner->v.velocity, native_velocity);
				VectorCopy (owner->v.punchangle, native_punch);
				native_gorilla = client->vr_gorilla_state;
			}
			else
			{
				assert (client->private_move_native_frame && !client->private_pmove_credit_msec);
				for (int axis = 0; axis < 3; ++axis)
				{
					assert (fabsf (owner->v.origin[axis] - native_origin[axis]) < .0001f);
					assert (fabsf (owner->v.velocity[axis] - native_velocity[axis]) < .0001f);
					assert (fabsf (owner->v.punchangle[axis] - native_punch[axis]) < .0001f);
				}
				assert (client->vr_gorilla_state.initialized == native_gorilla.initialized);
				assert (client->vr_gorilla_state.touching == native_gorilla.touching);
				if (mode == 3 && commands)
					assert (client->vr_gorilla_state.initialized &&
						client->vr_gorilla_last_sequence == commands);
			}
			SV_FinishPrivateUsercmds ();
			assert (!client->private_cmd_queue_count && !client->cmd.impulse &&
				!owner->v.impulse && !owner->v.button0 && !owner->v.button2 &&
				VectorCompare (client->cmd.vr_roomscalemove, vec3_origin));
			/* Both paths get a subsequent world frame with no packet. */
			{
				expected_impulse = 0;
				think_calls = prethink_calls = postthink_calls = 0;
				callback_count = 0;
				if (!selected) SV_ClientThink ();
				SV_Physics_Client (owner, 1); // no new input: levels only
				assert (think_calls == 1 && prethink_calls == 1 && postthink_calls == 1);
				assert (callback_count == 3 && !memcmp (callback_order, "PTQ", 3));
				assert (!observed_buttons && !observed_impulse &&
					client->private_completed_move == commands);
				if (!selected)
				{
					VectorCopy (owner->v.origin, maintenance_origin);
					VectorCopy (owner->v.velocity, maintenance_velocity);
					VectorCopy (owner->v.punchangle, maintenance_punch);
				}
				else for (int axis = 0; axis < 3; ++axis)
				{
					assert (fabsf (owner->v.origin[axis] - maintenance_origin[axis]) < .0001f);
					assert (fabsf (owner->v.velocity[axis] - maintenance_velocity[axis]) < .0001f);
					assert (fabsf (owner->v.punchangle[axis] - maintenance_punch[axis]) < .0001f);
				}
			}
		}
	}
	Cvar_SetValueQuick (&sv_altnoclip, saved_noclip);
	Cvar_SetValueQuick (&sv_gorilla, saved_gorilla);
	expected_impulse = 0;
	native_comparison = false;
	Mem_Free (saved_client);
	Mem_Free (globals);
	Mem_Free (vars);
	puts ("SELECTED_NATIVE_EQUIVALENCE_PASSED noclip0/noclip1/fly/rawhands; empty/two/eight commands");
}

static void check_native_state_guards (edict_t *owner, client_t *client)
{
	prepare_selected (owner, client, 0);
	memset (&client->cmd, 0, sizeof (client->cmd));
	client->vr_gorilla_capable = false;
	owner->v.movetype = MOVETYPE_FLY;
	owner->v.waterlevel = 2; // validator must not recategorize the dry map position
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_NATIVE);
	assert (!SV_PrivateWalkTrialFrameStateError (owner, client, &client->cmd));
	assert (owner->v.waterlevel == 2 && !client->private_pmove_pusher_interaction);
	assert (SV_PrivateWalkTrialStateError (owner, client, &client->cmd));
	assert (owner->v.waterlevel == 2); // strict WALK rejects before categorization
	float saved = owner->v.origin[0];
	owner->v.origin[0] = NAN;
	assert (SV_PrivateWalkTrialFrameStateError (owner, client, &client->cmd));
	owner->v.origin[0] = saved;
	owner->v.groundentity = -1;
	assert (SV_PrivateWalkTrialFrameStateError (owner, client, &client->cmd));
	owner->v.groundentity = 1;
	assert (SV_PrivateWalkTrialFrameStateError (owner, client, &client->cmd));
	owner->v.groundentity = qcvm->num_edicts * qcvm->edict_size;
	assert (SV_PrivateWalkTrialFrameStateError (owner, client, &client->cmd));
	owner->v.groundentity = 0;
	owner->v.health = NAN;
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_REJECTED);
	owner->v.health = 100;
	owner->v.movetype = 3.5f;
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_REJECTED);
	owner->v.movetype = MOVETYPE_NOCLIP;
	owner->v.maxs[2] = 31;
	assert (SV_PrivateWalkTrialFrameStateError (owner, client, &client->cmd));
	owner->v.maxs[2] = 32;
	qcvm->extfields.customphysics = ED_FindFieldOffset ("think");
	assert (SV_PrivateWalkTrialFrameStateError (owner, client, &client->cmd));
	qcvm->extfields.customphysics = -1;
	assert (!SV_PrivateWalkTrialFrameStateError (owner, client, &client->cmd));
	owner->v.waterlevel = CONTENTS_EMPTY;
	assert (SV_PrivateWalkTrialFrameStateError (owner, client, &client->cmd));
	owner->v.health = -99;
	owner->v.deadflag = DEAD_DEAD;
	owner->v.movetype = MOVETYPE_GIB;
	owner->v.solid = SOLID_NOT;
	assert (!SV_PrivateWalkTrialFrameStateError (owner, client, &client->cmd));
	owner->v.waterlevel = CONTENTS_SOLID;
	assert (!SV_PrivateWalkTrialFrameStateError (owner, client, &client->cmd));
	owner->v.waterlevel = CONTENTS_SOLID - 1;
	assert (SV_PrivateWalkTrialFrameStateError (owner, client, &client->cmd));
	owner->v.waterlevel = NAN;
	assert (SV_PrivateWalkTrialFrameStateError (owner, client, &client->cmd));
	puts ("SELECTED_NATIVE_GUARDS_PASSED strict WALK/pure classification/finite/hull/customphysics/ground");
}

int main (int argc, char **argv)
{
	edict_t *owner;
	client_t *client;
	func_t null_function, remove_function;
	float scheduled;

	Fixture_InitNativeEngine (argc, argv, "e1m1", false);
	null_function = ED_FindFunction ("SUB_Null") - qcvm->functions;
	remove_function = ED_FindFunction ("SUB_Remove") - qcvm->functions;
	assert (null_function && remove_function);
	owner = EDICT_NUM (1);
	client = &svs.clients[0];
	client->edict = owner;
	client->message.data = client->msgbuf;
	client->message.maxsize = sizeof (client->msgbuf);
	client->active = client->spawned = client->knowntoqc = true;
	client->protocol_qsvr = QSVR_PROTOCOL_PINNED;
	host_client = client;
	sv_player = owner;
	pr_global_struct->PlayerPreThink = null_function;
	pr_global_struct->PlayerPostThink = null_function;
	/* Use an actual loaded field slot; stock QC doesn't declare the extension. */
	qcvm->extfields.customphysics = ED_FindFieldOffset ("think");
	assert (qcvm->extfields.customphysics >= 0);
	owner->free = false;
	owner->v.movetype = 999; // native dispatch would fail
	owner->v.solid = SOLID_NOT;
	owner->v.health = 100;
	owner->v.think = null_function;
	owner->v.nextthink = scheduled = qcvm->time + .001f;
	VectorSet (owner->v.origin, 37, 11, 24);
	VectorSet (owner->v.velocity, 100, 0, 0);
	host_frametime = .02;
	assert (SV_Physics_ClientNativeFromPhase (owner, 1, 41,
		SV_CLIENT_NATIVE_FRESH, false, NULL));
	assert (owner->v.movetype == 999 && owner->v.nextthink == scheduled);
	assert (owner->v.origin[0] == 37 && owner->v.origin[1] == 11 &&
		owner->v.origin[2] == 24 && owner->retain_count == 0);
	assert (client->private_completed_move == 41);

	/* Real native input acceleration at and around stopspeed. The analytic
	 * crossing formula must leave zero-friction momentum finite/intact. */
	const float saved_friction = sv_friction.value;
	Cvar_SetQuick (&sv_friction, "0");
	owner->v.movetype = MOVETYPE_WALK;
	owner->v.flags = FL_CLIENT | FL_ONGROUND;
	memset (&client->cmd, 0, sizeof (client->cmd));
	for (int speed = 99; speed <= 101; ++speed)
	{
		VectorSet (owner->v.velocity, speed, 0, 0);
		SV_ClientThink ();
		assert (isfinite (owner->v.velocity[0]) && owner->v.velocity[0] == speed);
	}
	Cvar_SetValueQuick (&sv_friction, saved_friction);
	owner->v.movetype = 999;
	puts ("NATIVE_ZERO_FRICTION_PASSED");

	owner->v.think = remove_function;
	pr_global_struct->PlayerPostThink = remove_function;
	assert (!SV_Physics_ClientNativeFromPhase (owner, 1, 42,
		SV_CLIENT_NATIVE_FRESH, false, NULL));
	assert (owner->free && owner->retain_count == 0);
	assert (client->private_completed_move == 41);
	puts ("CUSTOMPHYSICS_NATIVE_PASSED");

	qcvm->extfields.customphysics = -1;
	fixture_think = install_callback (observe_think);
	fixture_prethink = install_callback (observe_prethink);
	fixture_postthink = install_callback (observe_postthink);
	for (int commands = 2; commands <= 8; commands += 6)
	{
		prepare_selected (owner, client, commands);
		host_frametime = .04;
		pr_global_struct->frametime = expected_world_frametime = .04f;
		SV_Physics_Client (owner, 1);
		assert (client->private_completed_move == commands);
		assert (think_calls == 1 && prethink_calls == commands &&
			postthink_calls == commands);
		assert (host_frametime == .04 && pr_global_struct->frametime == .04f);
		SV_FinishPrivateUsercmds ();
		assert (client->private_cmd_queue_count == 0);
	}
	prepare_selected (owner, client, 2);
	owner->v.nextthink = qcvm->time + 1;
	schedule_postthink = true;
	SV_Physics_Client (owner, 1);
	assert (think_calls == 0 && client->private_completed_move == 2);
	SV_FinishPrivateUsercmds ();
	schedule_postthink = false;
	SV_Physics_Client (owner, 1);
	assert (think_calls == 1 && client->private_completed_move == 2);
	prepare_selected (owner, client, 0);
	SV_Physics_Client (owner, 1);
	assert (think_calls == 1 && client->private_completed_move == 0);
	prepare_selected (owner, client, 1);
	client->private_cmd_queue[0].msec = 125;
	client->private_cmd_queue[0].impulse = 99;
	client->private_cmd_queue[0].vr_roomscalemove[0] = 5;
	client->private_cmd_queue_msec = 125;
	host_frametime = .001;
	pr_global_struct->frametime = expected_world_frametime = .001f;
	SV_Physics_Client (owner, 1);
	assert (think_calls == 1 && client->private_completed_move == 0 &&
		client->private_cmd_queue_count == 1 && owner->v.impulse == 0);

	prepare_selected (owner, client, 3);
	kill_prethink = 2;
	host_frametime = .04;
	pr_global_struct->frametime = expected_world_frametime = .04f;
	SV_Physics_Client (owner, 1);
	assert (think_calls == 1 && prethink_calls == 2 && postthink_calls == 2);
	assert (client->private_completed_move == 2 && client->private_move_native_frame);
	prepare_selected (owner, client, 2);
	kill_think = true;
	SV_Physics_Client (owner, 1);
	assert (think_calls == 1 && prethink_calls == 1 && postthink_calls == 1);
	assert (client->private_completed_move == 1 && client->private_move_native_frame);
	puts ("SELECTED_THINK_NATIVE_PASSED");
	compare_native_states (owner, client);
	check_native_state_guards (owner, client);
	/* No connected socket was created; don't run dedicated network shutdown. */
	SDL_Quit ();
	return 0;
}
