/* Real native dispatcher, loaded map/QC and engine entity allocator. This
 * replaces the executable entry point; no graphics, sockets or peer traffic.
 * Linker wrapping enables Loop_Init solely for offline dedicated startup. */
#include "../Quake/sv_phys.c"
#include <assert.h>

int __wrap_Loop_Init (void) { return 0; }

static int think_calls, prethink_calls, postthink_calls, kill_prethink;
static qboolean kill_think, schedule_postthink;
static float expected_world_frametime;
static func_t fixture_think, fixture_prethink, fixture_postthink;
static int callback_slots[3], num_callbacks;

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
	assert (fabsf (pr_global_struct->frametime - expected_world_frametime) < .000001f);
	assert (ent->v.impulse == 0);
	ent->v.nextthink = qcvm->time + .001f;
	if (kill_think)
		fixture_death (ent);
}

static void observe_prethink (void)
{
	prethink_calls++;
	if (prethink_calls == kill_prethink)
		fixture_death (PROG_TO_EDICT (pr_global_struct->self));
}

static void observe_postthink (void)
{
	postthink_calls++;
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

int main (int argc, char **argv)
{
	quakeparms_t parms = {0};
	edict_t *owner;
	client_t *client;
	func_t null_function, remove_function;
	float scheduled;

	parms.basedir = ".";
	parms.argc = argc;
	parms.argv = argv;
	host_parms = &parms;
	COM_InitArgv (argc, argv);
	isDedicated = COM_CheckParm ("-dedicated") != 0;
	assert (isDedicated && COM_CheckParm ("-noudp"));
	assert (SDL_Init (0));
	Sys_Init ();
	Host_Init ();
	Cvar_SetQuick (&sv_coop_autosave, "0");
	PR_SwitchQCVM (&sv.qcvm);
	SV_SpawnServer ("e1m1");
	assert (sv.active && qcvm == &sv.qcvm);
	null_function = ED_FindFunction ("SUB_Null") - qcvm->functions;
	remove_function = ED_FindFunction ("SUB_Remove") - qcvm->functions;
	assert (null_function && remove_function);
	owner = EDICT_NUM (1);
	client = &svs.clients[0];
	client->edict = owner;
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
	/* No connected socket was created; don't run dedicated network shutdown. */
	SDL_Quit ();
	return 0;
}
