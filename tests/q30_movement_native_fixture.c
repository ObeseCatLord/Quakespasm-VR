/* Exact installed q30 QC and real world hulls. Selection is injected in this
 * component driver; normal admission is proved by the mixed-session fixture.
 * Exercise current-state/native dispatch and compare native or qualified
 * selected movement from the same player state. */
#include "../Quake/sv_phys.c"
#include <assert.h>
#include "native_engine_fixture.h"
#include "native_liquid_fixture.h"

typedef struct
{
	vec3_t origin, velocity;
	int flags;
	float health, deadflag;
	float boots_left, ladder;
	float shells, nails, rockets, cells, weapon, ammo, impulse, attack_finished;
	int completed;
} movement_sample_t;

static float global_float (const char *name)
{
	ddef_t *field = ED_FindGlobal (name);
	assert (field && (field->type & ~DEF_SAVEGLOBAL) == ev_float);
	return G_FLOAT (field->ofs);
}

static eval_t *player_float (edict_t *player, const char *name)
{
	ddef_t *field = ED_FindField (name);
	assert (field && (field->type & ~DEF_SAVEGLOBAL) == ev_float);
	return GetEdictFieldValue (player, field->ofs);
}

static void set_global_float (const char *name, float value)
{
	ddef_t *field = ED_FindGlobal (name);
	assert (field && (field->type & ~DEF_SAVEGLOBAL) == ev_float);
	G_FLOAT (field->ofs) = value;
}

static void initialize_player (edict_t *player, client_t *client)
{
	SV_UnlinkEdict (player);
	memset (&player->v, 0, qcvm->progs->entityfields * sizeof (float));
	SV_ResetPrivateCommandQueue (client);
	SV_ResetPrivateVRContactState (client);
	client->active = client->spawned = client->knowntoqc = true;
	client->edict = player;
	client->protocol_qsvr = QSVR_PROTOCOL_PINNED;
	client->private_completed_move = 0;
	client->lastmovemessage = 0;
	client->message.data = client->msgbuf;
	client->message.maxsize = sizeof (client->msgbuf);
	SZ_Clear (&client->message);
	host_client = client;
	sv_player = player;
	pr_global_struct->self = EDICT_TO_PROG (player);
	PR_ExecuteProgram (pr_global_struct->SetNewParms);
	PR_ExecuteProgram (pr_global_struct->PutClientInServer);
	assert (player->v.health > 0 && !player->v.deadflag);
	assert (SV_PrivateWalkTrialStockHull (player));
	/* Find actual supporting floor from the map's player spawn. */
	vec3_t down = {0, 0, -256};
	trace_t floor = SV_Move (player->v.origin, player->v.mins,
		player->v.maxs, (vec3_t){player->v.origin[0], player->v.origin[1],
			player->v.origin[2] + down[2]}, MOVE_NORMAL, player);
	assert (!floor.allsolid && !floor.startsolid && floor.fraction < 1 &&
		floor.plane.normal[2] > .7f && floor.ent == EDICT_NUM (0));
	VectorCopy (floor.endpos, player->v.origin);
	VectorClear (player->v.velocity);
	player->v.flags = FL_CLIENT | FL_ONGROUND | FL_JUMPRELEASED;
	player->v.groundentity = 0;
	player->v.teleport_time = 0;
	SV_LinkEdict (player, false);
	SV_CheckWater (player);
	assert (player->v.waterlevel == 0);
	set_global_float ("map_jumpheight", 120);
}

static movement_sample_t sample_player (edict_t *player, client_t *client)
{
	movement_sample_t sample;
	VectorCopy (player->v.origin, sample.origin);
	VectorCopy (player->v.velocity, sample.velocity);
	sample.flags = (int)player->v.flags;
	sample.health = player->v.health;
	sample.deadflag = player->v.deadflag;
	sample.boots_left = player_float (player, "jumpboots_airlvl")->_float;
	sample.ladder = player_float (player, "onladder")->_float;
	sample.shells = player->v.ammo_shells;
	sample.nails = player->v.ammo_nails;
	sample.rockets = player->v.ammo_rockets;
	sample.cells = player->v.ammo_cells;
	sample.weapon = player->v.weapon;
	sample.ammo = player->v.currentammo;
	sample.impulse = player->v.impulse;
	sample.attack_finished = player_float (player, "attack_finished")->_float;
	sample.completed = client->private_completed_move;
	return sample;
}

static movement_sample_t step_authored_command (edict_t *player, client_t *client,
	qboolean selected, usercmd_t command)
{
	client->cmd = command;
	client->lastmovemessage = command.sequence;
	VectorCopy (command.viewangles, player->v.v_angle);
	player->v.button0 = (command.buttons & BUTTON_ATTACK) != 0;
	player->v.button2 = (command.buttons & BUTTON_JUMP) != 0;
	player->v.impulse = command.impulse;
	host_frametime = command.seconds;
	pr_global_struct->frametime = command.seconds;
	if (selected)
	{
		client->private_pmove_walk_selected = true;
		client->private_cmd_queue_head = 0;
		client->private_cmd_queue_count = 1;
		client->private_cmd_queue_msec = command.msec;
		client->private_cmd_queue[0] = command;
	}
	else
		SV_ClientThink ();
	SV_Physics_Client (player, 1);
	assert (client->active && !player->free);
	if (client->private_completed_move != (int)command.sequence)
		fprintf (stderr, "Q30_COMPLETION_MISMATCH selected=%d gorilla=%u seq=%u completed=%d native=%d queued=%u state=%d\n",
			selected, command.vr_gorilla.flags, command.sequence, client->private_completed_move,
			client->private_move_native_frame, client->private_cmd_queue_count,
			SV_PrivateWalkTrialClassifyState (client));
	assert (client->private_completed_move == (int)command.sequence);
	if (selected)
		SV_FinishPrivateUsercmds ();
	movement_sample_t sample = sample_player (player, client);
	qcvm->time += host_frametime;
	return sample;
}

static movement_sample_t step_command (edict_t *player, client_t *client,
	qboolean selected, int sequence, unsigned buttons, int impulse, int msec)
{
	usercmd_t command = {0};
	command.sequence = sequence;
	command.msec = msec;
	command.seconds = msec * .001f;
	command.servertime = qcvm->time;
	command.buttons = buttons;
	command.impulse = impulse;
	return step_authored_command (player, client, selected, command);
}

static movement_sample_t step_player (edict_t *player, client_t *client,
	qboolean selected, int sequence, unsigned buttons, int msec)
{
	return step_command (player, client, selected, sequence, buttons, 0, msec);
}

static void selected_maintenance (edict_t *player, client_t *client)
{
	vec3_t origin, velocity;
	VectorCopy (player->v.origin, origin);
	VectorCopy (player->v.velocity, velocity);
	int completed = client->private_completed_move;
	int flags = (int)player->v.flags;
	float boots_left = player_float (player, "jumpboots_airlvl")->_float;
	SV_Physics_Client (player, 1);
	assert (client->active && client->private_completed_move == completed);
	assert (VectorCompare (player->v.origin, origin) &&
		VectorCompare (player->v.velocity, velocity));
	assert ((int)player->v.flags == flags &&
		player_float (player, "jumpboots_airlvl")->_float == boots_left);
}

static void restore_player (edict_t *player, client_t *client,
	const void *vars, size_t vars_size, const void *globals, size_t globals_size,
	double time)
{
	SV_UnlinkEdict (player);
	memcpy (&player->v, vars, vars_size);
	memcpy (qcvm->globals, globals, globals_size);
	qcvm->time = time;
	SV_ResetPrivateCommandQueue (client);
	SV_ResetPrivateVRContactState (client);
	client->private_completed_move = client->lastmovemessage = 0;
	SZ_Clear (&client->message);
	SV_LinkEdict (player, false);
}

/* Real scheduled weapon callbacks can activate a camera through a monster's
 * authored HP target. Entities/HP target are prepared map properties; weapon
 * traces, damage, target dispatch and camera use all execute the pinned QC. */
static void scheduled_camera_case (edict_t *player, client_t *client, double time,
	const char *attack, const char *weapon, const char *safe_animation)
{
	qcvm->time = time;
	initialize_player (player, client);
	for (int i = 0; i < 4; ++i)
		step_player (player, client, true, i + 1, 0, 5);
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_WALK);
	set_global_float ("coop", 0); // q30 intentionally ignores camera use in co-op
	set_global_float ("cam_active", 0);
	// The actual q30 config bit selects hitscan shotgun instead of projectiles.
	set_global_float ("configflag", (int)global_float ("configflag") | 131072);
	player->v.weapon = global_float (weapon);
	player->v.items = (int)player->v.items | (int)player->v.weapon;
	player->v.ammo_cells = 100;
	player_float (player, "attack_finished")->_float = 0;
	usercmd_t command = {0};
	command.sequence = 1;
	command.forwardmove = 160;
	command.buttons = BUTTON_ATTACK;
	command.msec = 8;
	command.seconds = .008f;
	vec3_t source, finish, direction, right, up;
	VectorCopy (player->v.origin, source);
	source[2] += 16;
	qboolean clear = false;
	for (int i = 0; i < 4; ++i)
	{
		command.viewangles[YAW] = i * 90;
		AngleVectors (command.viewangles, direction, right, up);
		VectorMA (source, 48, direction, finish);
		trace_t ray = SV_Move (source, vec3_origin, vec3_origin,
			finish, MOVE_NORMAL, player);
		if (!ray.startsolid && !ray.allsolid && ray.fraction == 1)
		{
			clear = true;
			break;
		}
	}
	assert (clear);
	edict_t *target = ED_Alloc (), *camera = ED_Alloc ();
	target->v.solid = SOLID_SLIDEBOX;
	target->v.movetype = MOVETYPE_NONE;
	target->v.flags = FL_MONSTER;
	target->v.takedamage = DAMAGE_AIM;
	target->v.health = 100;
	player_float (target, "max_health")->_float = 100;
	player_float (target, "turrethealth")->_float = .999f;
	GetEdictFieldValue (target, ED_FindFieldOffset ("turrettarget"))->string =
		PR_SetEngineString ("fixture_q30_camera");
	GetEdictFieldValue (target, ED_FindFieldOffset ("th_pain"))->function =
		ED_FindFunction ("SUB_Null") - qcvm->functions;
	VectorMA (player->v.origin, 32, direction, target->v.origin);
	VectorSet (target->v.mins, -8, -8, -24);
	VectorSet (target->v.maxs, 8, 8, 32);
	VectorSubtract (target->v.maxs, target->v.mins, target->v.size);
	SV_LinkEdict (target, false);
	trace_t hit = SV_Move (source, vec3_origin, vec3_origin, finish, MOVE_NORMAL, player);
	assert (hit.ent == target && hit.fraction < 1);
	camera->v.targetname = PR_SetEngineString ("fixture_q30_camera");
	camera->v.use = ED_FindFunction ("misc_camera_use") - qcvm->functions;
	VectorCopy (command.viewangles, player->v.v_angle);
	client->cmd = command;
	player->v.button0 = 1;
	player->v.think = ED_FindFunction (attack) - qcvm->functions;
	player->v.nextthink = qcvm->time + .005f;
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_WALK);
	host_frametime = .010;
	pr_global_struct->frametime = .010f;
	sv_client_think_window_t window = {true, .010, .010f};
	const char *attacks[] = {"player_axe3", "player_axeb3", "player_axec3",
		"player_axed3", "player_axee3", "player_sg1", "player_light1", "player_light2"};
	for (int i = 0; i < countof (attacks); ++i)
	{
		player->v.think = ED_FindFunction (attacks[i]) - qcvm->functions;
		assert (SV_PrivateWalkTrialQ30WeaponThinkNeedsNative (player, &window));
	}
	window.available = false;
	assert (!SV_PrivateWalkTrialQ30WeaponThinkNeedsNative (player, &window));
	window.available = true;
	player->v.nextthink = qcvm->time + .020f;
	assert (!SV_PrivateWalkTrialQ30WeaponThinkNeedsNative (player, &window));
	player->v.nextthink = qcvm->time + .005f;
	player->v.think = ED_FindFunction (safe_animation) - qcvm->functions;
	assert (!SV_PrivateWalkTrialQ30WeaponThinkNeedsNative (player, &window));
	player->v.think = ED_FindFunction (attack) - qcvm->functions;
	const double callback_time = qcvm->time;
	const size_t vars_size = qcvm->progs->entityfields * sizeof (float);
	const size_t globals_size = qcvm->progs->numglobals * sizeof (float);
	void *vars = Mem_Alloc (vars_size), *globals = Mem_Alloc (globals_size);
	void *target_vars = Mem_Alloc (vars_size);
	memcpy (vars, &player->v, vars_size);
	memcpy (globals, qcvm->globals, globals_size);
	memcpy (target_vars, &target->v, vars_size);
	assert (SV_RunClientWeaponThink (player, client, &command, &window));
	assert (!window.available && target->v.health < 100 &&
		global_float ("cam_active") == 1 &&
		SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_NATIVE);
	assert (SV_PrivateWalkTrialStateError (player, client, &command));
	/* Actual production dispatch now chooses native input before the unsafe
	 * Think. Compare observable movement/QC against an ordinary native frame. */
	for (int gorilla = 0; gorilla < 2; ++gorilla)
	{
		movement_sample_t samples[2];
		for (int selected = 0; selected < 2; ++selected)
		{
			restore_player (player, client, vars, vars_size, globals, globals_size, callback_time);
			SV_UnlinkEdict (target);
			memcpy (&target->v, target_vars, vars_size);
			SV_LinkEdict (target, false);
			client->vr_gorilla_capable = gorilla;
			Cvar_SetQuick (&sv_gorilla, gorilla ? "1" : "0");
			command.vr_gorilla.flags = gorilla ? VR_GORILLA_HANDS : 0;
			samples[selected] = step_authored_command (player, client, selected, command);
			assert (target->v.health < 100 && global_float ("cam_active") == 1);
			if (selected)
				assert (client->private_move_native_frame &&
					client->private_pmove_credit_msec == 0 && !client->private_cmd_queue_count);
		}
		assert (VectorCompare (samples[0].origin, samples[1].origin) &&
			VectorCompare (samples[0].velocity, samples[1].velocity) &&
			samples[0].flags == samples[1].flags &&
			samples[0].weapon == samples[1].weapon &&
			samples[0].health == samples[1].health &&
			samples[0].shells == samples[1].shells &&
			samples[0].ammo == samples[1].ammo &&
			samples[0].attack_finished == samples[1].attack_finished &&
			samples[0].completed == samples[1].completed);
		if (!gorilla)
		{
			vec3_t movement;
			VectorSubtract (samples[0].origin, ((entvars_t *)vars)->origin, movement);
			assert (VectorLength (movement) > .01f);
		}
	}
	client->vr_gorilla_capable = false;
	Cvar_SetQuick (&sv_gorilla, "0");
	command.vr_gorilla.flags = 0;
	restore_player (player, client, vars, vars_size, globals, globals_size, callback_time);
	SV_UnlinkEdict (target);
	memcpy (&target->v, target_vars, vars_size);
	SV_LinkEdict (target, false);
	player->v.think = ED_FindFunction (safe_animation) - qcvm->functions;
	command.buttons = 0; // safe animation must not start another shot in PostThink
	step_authored_command (player, client, true, command);
	assert (!client->private_move_native_frame && global_float ("cam_active") == 0 &&
		target->v.health == 100 && player_float (target, "turrethealth")->_float == .999f &&
		!strcmp (PR_GetString (GetEdictFieldValue (target,
			ED_FindFieldOffset ("turrettarget"))->string), "fixture_q30_camera"));
	/* Actual selected no-command dispatch keeps the completed cursor while
	 * native world-time input/QC handles the due attack, before maintenance. */
	restore_player (player, client, vars, vars_size, globals, globals_size, callback_time);
	SV_UnlinkEdict (target);
	memcpy (&target->v, target_vars, vars_size);
	SV_LinkEdict (target, false);
	/* Complete real held input through safe animation first. Cooldown prevents
	 * another PostThink shot; the next quiet world frame receives those actual
	 * completed levels, without a fabricated last-command record or new ACK. */
	player->v.think = ED_FindFunction (safe_animation) - qcvm->functions;
	player_float (player, "attack_finished")->_float = callback_time + 1;
	command.buttons = BUTTON_ATTACK;
	step_authored_command (player, client, true, command);
	assert (client->private_pmove_last_cmd_valid && !client->private_cmd_queue_count &&
		!client->private_move_native_frame && global_float ("cam_active") == 0);
	const int completed = client->private_completed_move;
	player->v.think = ED_FindFunction (attack) - qcvm->functions;
	player->v.nextthink = qcvm->time + .005f;
	player_float (player, "attack_finished")->_float = 0;
	host_frametime = .010;
	pr_global_struct->frametime = .010f;
	SV_Physics_Client (player, 1);
	assert (client->active && client->private_completed_move == completed &&
		client->private_move_native_frame && target->v.health < 100 &&
		global_float ("cam_active") == 1);
	restore_player (player, client, vars, vars_size, globals, globals_size, callback_time);
	client->private_pmove_walk_selected = true;
	player->v.nextthink = NAN;
	assert (SV_PrivateWalkTrialFrameStateError (player, client, &command));
	player->v.nextthink = callback_time + .005f;
	player->v.think = qcvm->progs->numfunctions;
	assert (SV_PrivateWalkTrialFrameStateError (player, client, &command));
	Mem_Free (target_vars);
	Mem_Free (globals);
	Mem_Free (vars);
	ED_Free (target);
	ED_Free (camera);
	printf ("Q30_SCHEDULED_CAMERA_CALLBACK_PASSED think=%s actual native comparison / safe animation / no-command frame\n",
		attack);
}

/* Keep the last-shot prefix authored by actual QC. Inventory is prepared;
 * selection uses the real impulse, firing and animation scheduling. */
static void prepare_empty_ammo_prefix (edict_t *player, client_t *client, double time)
{
	qcvm->time = time;
	initialize_player (player, client);
	for (int i = 0; i < 4; ++i)
		step_player (player, client, false, i + 1, 0, 5);
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_WALK);
	player->v.items = (int)player->v.items | (int)global_float ("IT_NAILGUN") |
		(int)global_float ("IT_SUPER_SHOTGUN");
	player->v.ammo_nails = 1;
	player->v.ammo_rockets = player->v.ammo_cells = 0;
	player->v.ammo_shells = 25;
	player_float (player, "attack_finished")->_float = 0;
	usercmd_t command = {0};
	command.forwardmove = 100;
	command.buttons = BUTTON_ATTACK;
	command.msec = 8;
	command.seconds = .008f;
	VectorCopy (player->v.v_angle, command.viewangles);
	const int first = client->private_completed_move + 1;
	command.sequence = first;
	command.impulse = 4;
	step_authored_command (player, client, false, command);
	assert (player->v.weapon == global_float ("IT_NAILGUN") && player->v.ammo_nails == 0);
	command.impulse = 0;
	for (int i = 1; i < 40; ++i)
	{
		if (player->v.think == ED_FindFunction ("player_nail2") - qcvm->functions &&
			player->v.nextthink > 0 && player->v.nextthink <= qcvm->time + .008)
		{
			assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_WALK);
			printf ("Q30_EMPTY_AMMO_AUTHORED_PREFIX_PASSED actual QC impulse/last nail/due fallback frame=%d\n", i);
			return;
		}
		command.sequence = first + i;
		step_authored_command (player, client, false, command);
	}
	assert (!"authored empty-ammo callback was not reached");
}

static void scheduled_empty_ammo_cases (edict_t *player, client_t *client, double time)
{
	prepare_empty_ammo_prefix (player, client, time);
	const double callback_time = qcvm->time;
	const size_t vars_size = qcvm->progs->entityfields * sizeof (float);
	const size_t globals_size = qcvm->progs->numglobals * sizeof (float);
	void *prefix = Mem_Alloc (vars_size), *vars = Mem_Alloc (vars_size);
	void *globals = Mem_Alloc (globals_size);
	memcpy (prefix, &player->v, vars_size);
	memcpy (globals, qcvm->globals, globals_size);
	const char *attacks[] = {"player_nail2", "player_nail1", "player_nail3", "player_nail4",
		"player_nail5", "player_nail6", "player_nail7", "player_nail8",
		"player_snail1", "player_snail2", "player_snail3", "player_snail4",
		"player_snail5", "player_snail6", "player_snail7", "player_snail8",
		"player_grenade1", "player_rocket1", "player_plasma1", "player_plasma2"};
	for (int scenario = 0; scenario <= countof (attacks); ++scenario)
	{
		// Final case lets QC choose ordinary shotgun rather than super shotgun.
		const int index = scenario == countof (attacks) ? 0 : scenario;
		const char *attack = attacks[index];
		restore_player (player, client, prefix, vars_size, globals, globals_size, callback_time);
		const char *weapon = index < 8 ? "IT_NAILGUN" : index < 16 ? "IT_SUPER_NAILGUN" :
			index == 16 ? "IT_GRENADE_LAUNCHER" : index == 17 ? "IT_ROCKET_LAUNCHER" : "IT_LIGHTNING";
		player->v.weapon = global_float (weapon);
		player->v.items = (int)player->v.items | (int)player->v.weapon;
		if (scenario == countof (attacks))
			player->v.items = (int)player->v.items & ~(int)global_float ("IT_SUPER_SHOTGUN");
		if (index >= 18)
			player_float (player, "moditems")->_float = 64; // actual plasma upgrade bit
		player_float (player, "attack_finished")->_float = 0;
		// The first nail2 case retains the actual deadline from the authored prefix.
		if (scenario != 0)
		{
			player->v.think = ED_FindFunction (attack) - qcvm->functions;
			player->v.nextthink = callback_time + .005f;
		}
		const float deadline = player->v.nextthink;
		assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_WALK);
		memcpy (vars, &player->v, vars_size);
		usercmd_t command = {0};
		command.sequence = 1;
		command.forwardmove = 100;
		command.buttons = BUTTON_ATTACK;
		command.msec = 8;
		command.seconds = .008f;
		VectorCopy (player->v.v_angle, command.viewangles);
		sv_client_think_window_t window = {true, .008, .008f};
		assert (SV_PrivateWalkTrialQ30WeaponThinkNeedsNative (player, &window));
		window.available = false;
		assert (!SV_PrivateWalkTrialQ30WeaponThinkNeedsNative (player, &window));
		window.available = true;
		player->v.nextthink = callback_time + .020f;
		assert (!SV_PrivateWalkTrialQ30WeaponThinkNeedsNative (player, &window));
		player->v.nextthink = deadline;
		player->v.think = ED_FindFunction ("player_run") - qcvm->functions;
		assert (!SV_PrivateWalkTrialQ30WeaponThinkNeedsNative (player, &window));
		player->v.think = ED_FindFunction (attack) - qcvm->functions;
		client->private_pmove_walk_selected = true;
		assert (!SV_PrivateWalkTrialStateError (player, client, &command));
		assert (SV_RunClientWeaponThink (player, client, &command, &window));
		const float fallback = global_float (scenario == countof (attacks) ? "IT_SHOTGUN" : "IT_SUPER_SHOTGUN");
		assert (player->v.weapon == fallback);
		assert (!SV_PrivateWalkTrialFrameStateError (player, client, &command));
		if (scenario < countof (attacks))
			assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_NATIVE &&
				SV_PrivateWalkTrialStateError (player, client, &command));
		else
			assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_WALK &&
				!SV_PrivateWalkTrialStateError (player, client, &command));
		for (int gorilla = 0; gorilla < 2; ++gorilla)
		{
			movement_sample_t samples[2];
			for (int selected = 0; selected < 2; ++selected)
			{
				restore_player (player, client, vars, vars_size, globals, globals_size, callback_time);
				client->vr_gorilla_capable = gorilla;
				Cvar_SetQuick (&sv_gorilla, gorilla ? "1" : "0");
				command.vr_gorilla.flags = gorilla ? VR_GORILLA_HANDS : 0;
				samples[selected] = step_authored_command (player, client, selected, command);
				assert (player->v.weapon == fallback);
				if (selected)
					assert (client->private_move_native_frame &&
						client->private_pmove_credit_msec == 0 && !client->private_cmd_queue_count);
			}
			assert (VectorCompare (samples[0].origin, samples[1].origin) &&
				VectorCompare (samples[0].velocity, samples[1].velocity) &&
				samples[0].flags == samples[1].flags && samples[0].weapon == samples[1].weapon &&
				samples[0].ammo == samples[1].ammo && samples[0].shells == samples[1].shells &&
				samples[0].nails == samples[1].nails && samples[0].rockets == samples[1].rockets &&
				samples[0].cells == samples[1].cells && samples[0].completed == samples[1].completed);
			if (!gorilla)
			{
				vec3_t moved;
				VectorSubtract (samples[0].origin, ((entvars_t *)vars)->origin, moved);
				assert (VectorLength (moved) > .01f);
			}
		}
		client->vr_gorilla_capable = false;
		Cvar_SetQuick (&sv_gorilla, "0");
		command.vr_gorilla.flags = 0;
		// Exact one-ammo boundary and ample ammo launch through selected movement.
		const float amounts[] = {1, 10};
		for (int i = 0; i < countof (amounts); ++i)
		{
			restore_player (player, client, vars, vars_size, globals, globals_size, callback_time);
			float *ammo = index < 16 ? &player->v.ammo_nails : index < 18 ?
				&player->v.ammo_rockets : &player->v.ammo_cells;
			*ammo = amounts[i];
			window.available = true;
			assert (!SV_PrivateWalkTrialQ30WeaponThinkNeedsNative (player, &window));
			step_authored_command (player, client, true, command);
			assert (!client->private_move_native_frame && player->v.weapon == global_float (weapon) && *ammo < amounts[i]);
		}
		// Complete held input first; quiet native fallback advances no ACK.
		restore_player (player, client, vars, vars_size, globals, globals_size, callback_time);
		player->v.nextthink = callback_time + .020f;
		player_float (player, "attack_finished")->_float = callback_time + 1;
		// Low-ammo not-due callback leaves this actual command selected.
		step_authored_command (player, client, true, command);
		assert (!client->private_move_native_frame && client->private_pmove_last_cmd_valid &&
			!client->private_cmd_queue_count && player->v.weapon == global_float (weapon));
		const int completed = client->private_completed_move;
		const float completed_forward = client->private_pmove_last_cmd.forwardmove;
		const unsigned completed_buttons = client->private_pmove_last_cmd.buttons;
		player->v.nextthink = qcvm->time + .005f;
		player_float (player, "attack_finished")->_float = 0;
		host_frametime = .010;
		pr_global_struct->frametime = .010f;
		SV_Physics_Client (player, 1);
		assert (client->active && client->private_completed_move == completed &&
			client->private_move_native_frame && player->v.weapon == fallback &&
			client->cmd.forwardmove == completed_forward && client->cmd.buttons == completed_buttons &&
			client->private_pmove_last_cmd.forwardmove == completed_forward &&
			client->private_pmove_last_cmd.buttons == completed_buttons);
		printf ("Q30_EMPTY_AMMO_CALLBACK_PASSED think=%s fallback=%.0f native/selected/positive/quiet\n",
			attack, fallback);
	}
	Mem_Free (globals);
	Mem_Free (vars);
	Mem_Free (prefix);
	puts ("Q30_EMPTY_AMMO_HANDOFF_PASSED all20 roots / ordinary fallback / actual last-shot prefix");
}


static void roomscale_liquid_traversal (edict_t *player, client_t *client, double time)
{
	qcvm->time = time;
	initialize_player (player, client);
	for (int i = 0; i < 4; ++i)
		step_player (player, client, false, i + 1, 0, 5);
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_WALK);
	usercmd_t crossing = {0};
	vec3_t dry = {0};
	assert (FindRoomScaleLiquidEntry (player, client, &crossing, dry));
	VectorCopy (dry, player->v.origin);
	VectorClear (player->v.velocity);
	player->v.flags = ((int)player->v.flags | FL_JUMPRELEASED) & ~(FL_ONGROUND | FL_WATERJUMP);
	player->v.groundentity = 0;
	SV_LinkEdict (player, false);
	SV_CheckWater (player);
	assert (!player->v.waterlevel && !SV_TestEntityPosition (player) &&
		SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_WALK);
	crossing.forwardmove = 100;
	const double dry_time = qcvm->time;
	const size_t vars_size = qcvm->progs->entityfields * sizeof (float);
	const size_t globals_size = qcvm->progs->numglobals * sizeof (float);
	void *vars = Mem_Alloc (vars_size), *globals = Mem_Alloc (globals_size);
	memcpy (vars, &player->v, vars_size);
	memcpy (globals, qcvm->globals, globals_size);
	link_t *prev = player->area.prev, *next = player->area.next;
	const unsigned num_leafs = player->num_leafs;
	int leaves[MAX_ENT_LEAFS];
	memcpy (leaves, player->leafnums, sizeof (leaves));
	usercmd_t saved_cmd = client->cmd;
	const double frame_time = host_frametime;
	assert (SV_PrivateWalkTrialQ30NeedsNative (player, client, &crossing));
	assert (!memcmp (&player->v, vars, vars_size) && player->area.prev == prev && player->area.next == next &&
		player->num_leafs == num_leafs && !memcmp (leaves, player->leafnums, sizeof (leaves)) &&
		!memcmp (&client->cmd, &saved_cmd, sizeof (saved_cmd)) && host_frametime == frame_time);
	movement_sample_t samples[2];
	for (int selected = 0; selected < 2; ++selected)
	{
		restore_player (player, client, vars, vars_size, globals, globals_size, dry_time);
		samples[selected] = step_authored_command (player, client, selected, crossing);
		assert (player->v.waterlevel > 0);
		if (selected)
			assert (client->private_move_native_frame && !client->private_cmd_queue_count &&
				client->private_pmove_credit_msec == 0);
	}
	assert (VectorCompare (samples[0].origin, samples[1].origin) &&
		VectorCompare (samples[0].velocity, samples[1].velocity) &&
		samples[0].flags == samples[1].flags && samples[0].health == samples[1].health &&
		samples[0].completed == samples[1].completed);
	/* Reference: one actual selected dry head, then a fresh native crossing.
	 * The pending command lasts 30ms, but its native world interval is 10ms.
	 * Match the same QC/world opportunities when both heads arrive together. */
	usercmd_t first = crossing, pending = crossing;
	VectorClear (first.vr_roomscalemove);
	first.forwardmove = 0;
	pending.sequence = 2;
	pending.msec = 30;
	pending.seconds = .030f;
	pending.servertime = dry_time + first.seconds;
	restore_player (player, client, vars, vars_size, globals, globals_size, dry_time);
	movement_sample_t prefix = step_authored_command (player, client, true, first);
	assert (!player->v.waterlevel && !client->private_move_native_frame &&
		client->private_pmove_last_cmd_valid && !client->private_cmd_queue_count);
	usercmd_t reference = pending;
	reference.seconds = .010f; // native owns world time, not the queued 30ms
	client->private_pmove_walk_selected = false; // ordinary native reference
	movement_sample_t next_native = step_authored_command (player, client, false, reference);
	assert (player->v.waterlevel > 0);

	restore_player (player, client, vars, vars_size, globals, globals_size, dry_time);
	client->private_pmove_walk_selected = true; // component seam, not admission
	client->cmd = first;
	client->lastmovemessage = 2;
	client->private_cmd_queue_head = 0;
	client->private_cmd_queue_count = 2;
	client->private_cmd_queue_msec = first.msec + pending.msec;
	client->private_cmd_queue[0] = first;
	client->private_cmd_queue[1] = pending;
	host_frametime = first.seconds;
	pr_global_struct->frametime = first.seconds;
	SV_Physics_Client (player, 1);
	assert (client->active && !player->free && client->private_completed_move == 1 &&
		client->private_move_native_frame && client->private_pmove_credit_msec == 0 &&
		client->private_pmove_last_cmd_valid &&
		client->private_pmove_last_cmd.sequence == 1 &&
		client->vr_gorilla_cursor_valid && client->vr_gorilla_last_sequence == 1 &&
		!memcmp (&client->private_cmd_queue[1], &pending, sizeof (pending)));
	movement_sample_t deferred = sample_player (player, client);
	assert (!player->v.waterlevel && VectorCompare (prefix.origin, deferred.origin) &&
		VectorCompare (prefix.velocity, deferred.velocity) && prefix.flags == deferred.flags &&
		prefix.health == deferred.health && prefix.completed == deferred.completed);
	SV_FinishPrivateUsercmds ();
	assert (client->private_cmd_queue_count == 1 && client->private_cmd_queue_msec == pending.msec &&
		!memcmp (&client->private_cmd_queue[client->private_cmd_queue_head], &pending, sizeof (pending)));
	qcvm->time += host_frametime;
	host_frametime = reference.seconds;
	pr_global_struct->frametime = reference.seconds;
	SV_Physics_Client (player, 1);
	assert (client->active && !player->free && client->private_completed_move == 2 &&
		client->private_move_native_frame && client->private_pmove_credit_msec == 0 &&
		player->v.waterlevel > 0 &&
		client->private_pmove_last_cmd.sequence == 2 &&
		VectorCompare (client->private_pmove_last_cmd.vr_roomscalemove, vec3_origin));
	movement_sample_t consumed = sample_player (player, client);
	assert (VectorCompare (next_native.origin, consumed.origin) &&
		VectorCompare (next_native.velocity, consumed.velocity) &&
		next_native.flags == consumed.flags && next_native.health == consumed.health &&
		next_native.completed == consumed.completed);
	SV_FinishPrivateUsercmds ();
	assert (!client->private_cmd_queue_count && !client->private_cmd_queue_msec);
	puts ("Q30_ROOMSCALE_LIQUID_LATER_HEAD_PASSED real dry prefix / raw pending / next native once without credit");
	Mem_Free (globals);
	Mem_Free (vars);
	puts ("Q30_ROOMSCALE_LIQUID_TRAVERSAL_PASSED actual sweep/native/QC/completion; prepared airborne start");
}

/* Commands share a world opportunity; QC time does not advance within the
 * batch. The native single-world comparison below deliberately uses the
 * existing coalesced adapter, rather than pretending eight native frames
 * have the same callback clock as one selected batch. */
static movement_sample_t batch_commands (edict_t *player, client_t *client,
	qboolean native_frame, int count, unsigned buttons, int impulse, float forwardmove)
{
	const int first = client->private_completed_move + 1;
	assert (count > 0 && count <= 8);
	client->private_pmove_walk_selected = true; // component seam, not admission
	client->private_cmd_queue_head = 0;
	client->private_cmd_queue_count = count;
	client->private_cmd_queue_msec = count * 5;
	client->lastmovemessage = first + count - 1;
	for (int i = 0; i < count; ++i)
	{
		usercmd_t *command = &client->private_cmd_queue[i];
		memset (command, 0, sizeof (*command));
		command->sequence = first + i;
		command->msec = 5;
		command->seconds = .005f;
		command->servertime = qcvm->time + i * .005;
		command->buttons = buttons;
		command->impulse = i ? 0 : impulse;
		command->forwardmove = forwardmove;
	}
	host_frametime = count * .005;
	pr_global_struct->frametime = host_frametime;
	if (native_frame)
		SV_Physics_ClientSelectedNativeFrame (player, 1, client, false);
	else
		SV_Physics_Client (player, 1);
	assert (client->active && !player->free && client->private_completed_move == first + count - 1);
	SV_FinishPrivateUsercmds ();
	assert (!client->private_cmd_queue_count && !client->private_cmd_queue_msec);
	return sample_player (player, client);
}

static void ordinary_dry_cases (edict_t *player, client_t *client,
	void *vars, size_t vars_size, void *globals, size_t globals_size, double time)
{
	movement_sample_t trajectory[2][48];
	qcvm->time = time;
	initialize_player (player, client);
	/* Warm actual QC once without movement before checkpointing. This is a
	 * comparison of initialized ordinary play, not a qualification of startup. */
	for (int i = 0; i < 4; ++i)
		step_player (player, client, false, i + 1, 0, 5);
	const double dry_time = qcvm->time;
	memcpy (vars, &player->v, vars_size);
	memcpy (globals, qcvm->globals, globals_size);
	assert (global_float ("chaoscount") > 2);
	for (int selected = 0; selected < 2; ++selected)
	{
		restore_player (player, client, vars, vars_size, globals, globals_size, dry_time);
		int takeoffs = 0;
		for (int i = 0; i < 48; ++i)
		{
			/* Hold through an actual landing, release, then re-jump. */
			unsigned buttons = i == 40 ? 0 : BUTTON_JUMP;
			qboolean supported = ((int)player->v.flags & FL_ONGROUND) != 0;
			trajectory[selected][i] = step_player (player, client, selected,
				i + 1, buttons, 8);
			movement_sample_t *sample = &trajectory[selected][i];
			if (supported && sample->velocity[2] > 0)
				++takeoffs;
			assert (sample->health > 0 && !sample->deadflag);
			if (i == 39)
				assert ((sample->flags & FL_ONGROUND) && takeoffs == 1 &&
					!(sample->flags & FL_JUMPRELEASED));
			if (i == 40)
				assert (sample->flags & FL_JUMPRELEASED);
		}
		assert (takeoffs == 2);
	}
	float max_position_error = 0;
	for (int i = 0; i < 48; ++i)
	{
		assert (trajectory[0][i].flags == trajectory[1][i].flags);
		for (int axis = 0; axis < 3; ++axis)
		{
			float error = fabsf (trajectory[0][i].origin[axis] - trajectory[1][i].origin[axis]);
			max_position_error = fmaxf (max_position_error, error);
			/* The 48-command native/PMove survey reaches 0.660 units
			 * before landing reconverges. This one-unit local bound catches
			 * larger displacement; it does not claim identical integration. */
			assert (isfinite (error) && error < 1.f);
			assert (fabsf (trajectory[0][i].velocity[axis] - trajectory[1][i].velocity[axis]) < .01f);
		}
	}
	printf ("Q30_DRY_HELD_LANDING_PASSED commands=48 takeoffs=2 max_position_error=%.6f\n",
		max_position_error);

	if (COM_CheckParm ("-negative-cooldown") || COM_CheckParm ("-negative-frame-cooldown"))
	{
		/* Shotgun QC has its own cooldown return. Frame-only bypass must
		 * still pass; bypassing both returns must fail the one-shell check.
		 * Mutate only the test VM, never licensed files or production code. */
		const char *functions[] = {"W_WeaponFrame", "W_FireShotgun"};
		const int offsets[] = {3, 2};
		int count = COM_CheckParm ("-negative-cooldown") ? 2 : 1;
		for (int i = 0; i < count; ++i)
		{
			dfunction_t *function = ED_FindFunction (functions[i]);
			assert (function && function->first_statement > 0 &&
				function->first_statement + offsets[i] + 1 < qcvm->progs->numstatements);
			dstatement_t *guard = &qcvm->statements[function->first_statement + offsets[i]];
			assert (guard->op == OP_IFNOT && guard->b == 2 && guard[1].op == OP_RETURN);
			guard->op = OP_GOTO;
			guard->a = 2;
			guard->b = guard->c = 0;
		}
		fprintf (stderr, "Q30_COOLDOWN_CONTROL bypass=%s\n",
			count == 1 ? "frame-only" : "frame-and-shotgun");
	}

	for (int count = 2; count <= 8; count += 6)
	{
		movement_sample_t fire[2];
		for (int native_frame = 0; native_frame < 2; ++native_frame)
		{
			restore_player (player, client, vars, vars_size, globals, globals_size, dry_time);
			/* The fixture uses the actual spawned shotgun inventory and QC
			 * impulse selection, not a replacement weapon callback. */
			float initial_shells = player->v.ammo_shells;
			fire[native_frame] = batch_commands (player, client, native_frame,
				count, BUTTON_ATTACK, 2, 0);
			assert (fire[native_frame].shells == initial_shells - 1);
			assert (fire[native_frame].weapon == global_float ("IT_SHOTGUN"));
			assert (fire[native_frame].impulse == 0 &&
				fire[native_frame].attack_finished > dry_time);
		}
		assert (fire[0].shells == fire[1].shells && fire[0].weapon == fire[1].weapon &&
			fire[0].ammo == fire[1].ammo && fire[0].attack_finished == fire[1].attack_finished);
		printf ("Q30_DRY_FIRE_BATCH_PASSED count=%d shells=%.0f cooldown=%.6f\n",
			count, fire[0].shells, fire[0].attack_finished);
	}

	restore_player (player, client, vars, vars_size, globals, globals_size, dry_time);
	movement_sample_t fired = batch_commands (player, client, false, 8, BUTTON_ATTACK, 2, 0);
	/* Quiet maintenance retains held levels and world QC effects. Before the
	 * deadline no second shell is spent; at the actual deadline QC can fire
	 * without inventing command duration or another completion ACK. */
	selected_maintenance (player, client);
	assert (player->v.ammo_shells == fired.shells);
	qcvm->time = fired.attack_finished + .001;
	selected_maintenance (player, client);
	assert (player->v.ammo_shells == fired.shells - 1 && client->private_completed_move == 8);
	float switched_shells = player->v.ammo_shells;
	step_command (player, client, true, 9, 0, 1, 5);
	assert (player->v.weapon == global_float ("IT_AXE") && player->v.impulse == 0 &&
		player->v.ammo_shells == switched_shells);
	selected_maintenance (player, client);
	assert (player->v.weapon == global_float ("IT_AXE") &&
		player->v.ammo_shells == switched_shells && client->private_completed_move == 9);
	puts ("Q30_DRY_QUIET_FIRE_IMPULSE_PASSED");

	restore_player (player, client, vars, vars_size, globals, globals_size, dry_time);
	vec3_t hold_origin;
	VectorCopy (player->v.origin, hold_origin);
	player_float (player, "pausetime")->_float = dry_time + 1;
	movement_sample_t held = batch_commands (player, client, true, 2, 0, 0, 200);
	assert (VectorCompare (held.origin, hold_origin) &&
		VectorCompare (held.velocity, vec3_origin) && held.completed == 2);
	assert (client->private_move_native_frame && client->private_pmove_credit_msec == 0);
	qcvm->time = dry_time + 1.001;
	movement_sample_t released = batch_commands (player, client, true, 2, 0, 0, 200);
	/* Spawn fixangle may retain its native yaw until a real client consumes
	 * setangle. Input must resume along that orientation, not fixture +X. */
	assert (hypotf (released.velocity[0], released.velocity[1]) > 0 &&
		hypotf (released.origin[0] - held.origin[0], released.origin[1] - held.origin[1]) > 0 &&
		released.completed == 4 && client->private_pmove_credit_msec == 0);
	puts ("Q30_NATIVE_AUTHORED_HOLD_RELEASE_PASSED");
}

/* Compare the real client policy adapter with actual server QC + selected
 * PMove in the same world. Physent collection is a test seam using the server
 * collector: this does not claim serialization, normal admission or signon. */
static void ordinary_replay_cases (edict_t *player, client_t *client, double time)
{
	extern cvar_t vr_movement_instant_stop;
	const char *names[] = {"held-landing", "zero-height", "clamped-height",
		"airborne-press", "roomscale-stop-jump", "substepped-jump", "huge-finite-height"};
	for (int scenario = 0; scenario < countof (names); ++scenario)
	{
		qcvm->time = time;
		initialize_player (player, client);
		for (int i = 0; i < 4; ++i)
			step_player (player, client, false, i + 1, 0, 5);
		SV_ResetPrivateCommandQueue (client);
		client->private_completed_move = client->lastmovemessage = 0;
		client->private_pmove_walk_selected = true; // component seam, not admission
		client->offered_pmove_policies = QSVR_PMOVE_CAP_Q30_JUMP;
		client->vr_instant_stop_offered = client->vr_instant_stop_capable = scenario == 4;
		Cvar_SetQuick (&vr_movement_instant_stop, scenario == 4 ? "1" : "0");
		set_global_float ("map_jumpheight", scenario == 1 ? 0 :
			scenario == 2 ? 4000 : scenario == 6 ? 1e30f : 120);
		player->v.fixangle = 0;
		if (scenario == 3)
		{
			player->v.origin[2] += 64;
			player->v.flags = (int)player->v.flags & ~FL_ONGROUND;
			SV_LinkEdict (player, false);
		}
		if (scenario == 4)
			player->v.velocity[0] = 100;
		const int frames = scenario < 2 ? 48 : 4;
		for (int i = 0; i < frames; ++i)
		{
			usercmd_t command = {0};
			vec3_t bounds[2], predicted_origin, predicted_velocity;
			command.sequence = i + 1;
			command.msec = scenario == 5 ? 125 : scenario == 4 ? 5 : 8;
			command.seconds = command.msec * .001f;
			command.servertime = qcvm->time;
			command.buttons = i == 40 || (scenario >= 2 && i == 1) ? 0 : BUTTON_JUMP;
			command.vr_active = scenario == 4;
			if (scenario == 4)
				command.vr_roomscalemove[0] = .25f;
			if (scenario == 0 && i == 41)
			{
				assert (((int)player->v.flags & FL_ONGROUND) &&
					((int)player->v.flags & FL_JUMPRELEASED));
				set_global_float ("map_jumpheight", 70); // consumed by this released, supported re-jump
			}
			memset (&pmove, 0, sizeof (pmove));
			assert (SV_PrivateWalkTrialBuildMoveVars (client, &movevars));
			assert ((movevars.flags & MOVEFLAG_QC_JUMP_ORDINARY) &&
				movevars.jumpspeed == global_float ("map_jumpheight") &&
				movevars.qc_maxvelocity == sv_maxvelocity.value);
			assert (SV_PrivateWalkTrialCollect (player, &movevars,
				command.seconds, &command, bounds));
			if (scenario == 6)
				assert (bounds[1][2] - player->v.origin[2] < 128); // clamped reach, authored height retained
			pmove.pm_type = PM_NORMAL;
			pmove.cmd = command;
			VectorCopy (player->v.origin, pmove.origin);
			VectorCopy (player->v.velocity, pmove.velocity);
			VectorCopy (player->v.mins, pmove.player_mins);
			VectorCopy (player->v.maxs, pmove.player_maxs);
			VectorSet (pmove.gravitydir, 0, 0, -1);
			pmove.onground = ((int)player->v.flags & FL_ONGROUND) != 0;
			pmove.jump_held = ((int)player->v.flags & FL_JUMPRELEASED) == 0;
			pmove.qc_jump_owner = true;
			assert (PM_PlayerMoveQCReplay (1));
			assert (memcmp (&pmove.cmd, &command, sizeof (command)) == 0);
			assert (pmove.jump_secs == 0);
			VectorCopy (pmove.origin, predicted_origin);
			VectorCopy (pmove.velocity, predicted_velocity);
			qboolean predicted_ground = pmove.onground, predicted_held = pmove.jump_held;
			movement_sample_t actual = step_authored_command (player, client, true, command);
			for (int axis = 0; axis < 3; ++axis)
			{
				if (fabsf (actual.origin[axis] - predicted_origin[axis]) >= .01f ||
					fabsf (actual.velocity[axis] - predicted_velocity[axis]) >= .01f)
					printf ("Q30_REPLAY_DIFFERENCE case=%s frame=%d axis=%d server=(%.6f,%.6f) replay=(%.6f,%.6f)\n",
						names[scenario], i + 1, axis, actual.origin[axis], actual.velocity[axis],
						predicted_origin[axis], predicted_velocity[axis]);
				assert (fabsf (actual.origin[axis] - predicted_origin[axis]) < .01f);
				assert (fabsf (actual.velocity[axis] - predicted_velocity[axis]) < .01f);
			}
			assert (((actual.flags & FL_ONGROUND) != 0) == predicted_ground);
			assert (((actual.flags & FL_JUMPRELEASED) == 0) == predicted_held);
		}
		printf ("Q30_ORDINARY_REPLAY_PASSED %s frames=%d\n", names[scenario], frames);
	}
	client->offered_pmove_policies = 0;
	client->vr_instant_stop_offered = client->vr_instant_stop_capable = false;
	Cvar_SetQuick (&vr_movement_instant_stop, "0");
}

static void native_state_cases (edict_t *player, client_t *client, double time,
	void *vars, size_t vars_size, void *globals, size_t globals_size)
{
	qcvm->time = time;
	initialize_player (player, client);
	/* Prepared lifecycle values exercise pre-begin classification without
	 * changing the production admission gate or running QC in the predicate. */
	set_global_float ("prethink", 0);
	set_global_float ("postthink", 0);
	set_global_float ("chaoscount", 0);
	client->spawned = false;
	assert (SV_PrivateWalkTrialBeginState (client) == SV_PRIVATE_MOVE_NATIVE);
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_REJECTED);
	assert (!client->spawned && global_float ("chaoscount") == 0);
	client->spawned = true;
	/* Real fresh native startup consumes actual QC once per world frame. */
	for (int i = 0; i < 4; ++i)
	{
		step_player (player, client, true, i + 1, 0, 5);
		if (i < 3)
			assert (client->private_move_native_frame &&
				client->private_pmove_credit_msec == 0);
	}
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_WALK);
	puts ("Q30_NATIVE_STARTUP_CLASSIFICATION_PASSED");
	const double dry_time = qcvm->time;
	memcpy (vars, &player->v, vars_size);
	memcpy (globals, qcvm->globals, globals_size);
	const char *cameras[] = {"intermission_running", "secloc_running",
		"cinematic_running", "cam_active"};
	for (int i = 0; i < countof (cameras); ++i)
	{
		set_global_float (cameras[i], 1);
		assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_NATIVE);
		assert (SV_PrivateWalkTrialMotionHeld (client));
		assert (!SV_PrivateWalkTrialFrameStateError (player, client, NULL));
		set_global_float (cameras[i], 0);
	}
	const char *abilities[] = {"IT_ARTJUMPBOOTS", "IT_UPGRADE_GHOOK"};
	for (int i = 0; i < countof (abilities); ++i)
	{
		player_float (player, "moditems")->_float = global_float (abilities[i]);
		assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_NATIVE);
		assert (!SV_PrivateWalkTrialFrameStateError (player, client, NULL));
	}
	player_float (player, "moditems")->_float = 0;
	player->v.weapon = global_float ("IT_SUPER_SHOTGUN");
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_NATIVE);
	restore_player (player, client, vars, vars_size, globals, globals_size, dry_time);
	player_float (player, "oldgravity")->_float = .5f;
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_NATIVE);
	step_player (player, client, true, 1, 0, 5);
	assert (player_float (player, "gravity")->_float == .5f &&
		SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_WALK);
	const int types[] = {MOVETYPE_NONE, MOVETYPE_FLY, MOVETYPE_NOCLIP,
		MOVETYPE_TOSS, MOVETYPE_BOUNCE, MOVETYPE_GIB};
	for (int i = 0; i < countof (types); ++i)
	{
		player->v.movetype = types[i];
		assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_NATIVE);
	}
	player->v.movetype = MOVETYPE_WALK;
	player->v.maxs[2] = 16;
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_NATIVE);
	player->v.maxs[2] = NAN;
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_REJECTED);
	restore_player (player, client, vars, vars_size, globals, globals_size, dry_time);
	/* Typed-definition and reference failures are component seams, not
	 * mod-authored invalid-state claims. Check the real predicate's bounds. */
	ddef_t *field = ED_FindField ("hookent");
	const unsigned short oldtype = field->type, oldofs = field->ofs;
	field->type = ev_float;
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_REJECTED);
	field->type = oldtype;
	field->ofs = qcvm->progs->entityfields;
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_REJECTED);
	field->ofs = oldofs;
	eval_t *hook = GetEdictFieldValue (player, field->ofs);
	hook->edict = qcvm->edict_size - 1;
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_REJECTED);
	hook->edict = -1;
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_REJECTED);
	hook->edict = 0;
	set_global_float ("prethink", NAN);
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_REJECTED);
	set_global_float ("prethink", 1);
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_WALK);
	client->private_pmove_walk_selected = true;
	player->v.groundentity = 1;
	assert (SV_PrivateWalkTrialFrameStateError (player, client, NULL));
	player->v.groundentity = 0;
	client->private_move_native_frame = true;
	player->v.movetype = 1234;
	assert (SV_PrivateWalkTrialFrameStateError (player, client, NULL));
	player->v.movetype = MOVETYPE_WALK;
	assert (!SV_PrivateWalkTrialFrameStateError (player, client, NULL));
	puts ("Q30_TYPED_NATIVE_STATE_PASSED");

	/* Real native input can defer before QC. Actual q30 PreThink then zeros
	 * velocity for a newly activated hold; resume must not add analog input. */
	Cvar_SetQuick (&sv_gorilla, "1");
	client->private_move_native_frame = true;
	client->vr_gorilla_capable = true;
	client->cmd.forwardmove = 200;
	client->cmd.vr_gorilla.flags = VR_GORILLA_HANDS;
	assert (SV_GorillaEligible (client));
	SV_ClientThink ();
	assert (client->vr_gorilla_move_deferred);
	player_float (player, "pausetime")->_float = qcvm->time + 1;
	pr_global_struct->self = EDICT_TO_PROG (player);
	pr_global_struct->time = qcvm->time;
	PR_ExecuteProgram (pr_global_struct->PlayerPreThink);
	assert (VectorCompare (player->v.velocity, vec3_origin));
	SV_GorillaResumeDeferredMove (client);
	assert (!client->vr_gorilla_move_deferred &&
		VectorCompare (player->v.velocity, vec3_origin) && !SV_GorillaEligible (client));
	usercmd_t contact = {0};
	contact.sequence = 20;
	contact.msec = 5;
	contact.vr_active = contact.vr_handpos_relative = true;
	contact.vr_contact.flags = VR_WEAPON_CONTACT_LEFT_VALID;
	contact.vr_contact.weapon = player->v.weapon;
	for (int i = 1; i < MAX_MODELS; ++i)
		if (sv.model_precache[i] &&
			!strcmp (sv.model_precache[i], PR_GetString (player->v.weaponmodel)))
		{
			contact.vr_contact.modelindex = i;
			break;
		}
	assert (contact.vr_contact.modelindex);
	const double saved_realtime = realtime, saved_arrival = client->lastmovetime;
	realtime = 100;
	client->lastmovetime = contact.vr_contact_received = realtime;
	Cvar_Set ("sv_weapon_collision", "1");
	player_float (player, "pausetime")->_float = 0;
	assert (SV_VRContactSampleValid (client, player, &contact));
	player_float (player, "pausetime")->_float = qcvm->time + 1;
	assert (!SV_VRContactSampleValid (client, player, &contact));
	client->private_vr_contact_previous_valid = true;
	assert (SV_VRContactProcessCommand (client, player, &contact));
	assert (client->private_vr_contact_cursor_valid &&
		client->private_vr_contact_last_sequence == 20 &&
		!client->private_vr_contact_previous_valid);
	player_float (player, "pausetime")->_float = 0;
	assert (SV_GorillaEligible (client));
	Cvar_Set ("sv_weapon_collision", "-1");
	realtime = saved_realtime;
	client->lastmovetime = saved_arrival;
	client->vr_gorilla_capable = false;
	Cvar_SetQuick (&sv_gorilla, "0");
	puts ("Q30_QC_HOLD_DEFERRED_INPUT_PASSED");

	restore_player (player, client, vars, vars_size, globals, globals_size, dry_time);
	usercmd_t room = {0}, saved_command = client->cmd;
	room.vr_active = true;
	room.sequence = 30;
	room.msec = 5;
	room.vr_roomscalemove[0] = 4;
	link_t *prev = player->area.prev, *next = player->area.next;
	vec3_t before;
	VectorCopy (player->v.origin, before);
	memcpy (vars, &player->v, vars_size);
	const unsigned num_leafs = player->num_leafs;
	int leaves[MAX_ENT_LEAFS];
	memcpy (leaves, player->leafnums, sizeof (leaves));
	const double frame_time = host_frametime;
	assert (!SV_PrivateWalkTrialQ30NeedsNative (player, client, &room));
	assert (player->area.prev == prev && player->area.next == next);
	assert (!memcmp (&player->v, vars, vars_size));
	assert (player->num_leafs == num_leafs &&
		!memcmp (leaves, player->leafnums, sizeof (leaves)));
	assert (!memcmp (&client->cmd, &saved_command, sizeof (saved_command)) &&
		host_frametime == frame_time && VectorCompare (player->v.origin, before));
	/* An originally unlinked owner must remain unlinked after probing. */
	SV_UnlinkEdict (player);
	assert (!SV_PrivateWalkTrialQ30NeedsNative (player, client, &room));
	assert (!player->area.prev && !player->area.next);
	SV_LinkEdict (player, false);
	puts ("Q30_ROOMSCALE_PROBE_RESTORATION_PASSED");

	/* Prepared wet position and stale QC-visible dry state, using the actual
	 * BSP/hull lookup shared with stock. Compare fresh native dispatch order;
	 * this is not an authored dry-to-wet map/roomscale traversal. */
	vec3_t wet = {0};
	assert (FindLiquidPosition (player, CONTENTS_WATER, 2, wet));
	VectorCopy (wet, player->v.origin);
	player->v.waterlevel = 0;
	player->v.watertype = CONTENTS_EMPTY;
	VectorClear (player->v.velocity);
	SV_LinkEdict (player, false);
	memcpy (vars, &player->v, vars_size);
	memcpy (globals, qcvm->globals, globals_size);
	assert (SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_WALK);
	assert (SV_PrivateWalkTrialQ30NeedsNative (player, client, NULL));
	assert (player->v.waterlevel == 0 && player->v.watertype == CONTENTS_EMPTY &&
		!memcmp (&player->v, vars, vars_size));
	movement_sample_t native = step_player (player, client, false, 1, 0, 5);
	restore_player (player, client, vars, vars_size, globals, globals_size, dry_time);
	movement_sample_t selected = step_player (player, client, true, 1, 0, 5);
	assert (client->private_move_native_frame && client->private_pmove_credit_msec == 0);
	assert (VectorCompare (native.origin, selected.origin) &&
		VectorCompare (native.velocity, selected.velocity) && native.flags == selected.flags);
	assert (player->v.waterlevel == 2 &&
		SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_NATIVE);
	puts ("Q30_FRESH_WATER_DISPATCH_ORDER_PASSED");

	/* Prepared after-first-head boundary, not a real horizontal water entry.
	 * Actual native QC/physics must consume the retained raw sample on the next
	 * shorter world frame despite zero credit and a longer command duration. */
	restore_player (player, client, vars, vars_size, globals, globals_size, dry_time);
	client->private_pmove_walk_selected = true;
	client->private_completed_move = 1;
	client->lastmovemessage = 2;
	client->private_cmd_queue_count = 1;
	client->private_cmd_queue_msec = 30;
	usercmd_t pending = {0};
	pending.sequence = 2;
	pending.msec = 30;
	pending.seconds = .030f;
	pending.vr_active = true;
	pending.vr_gorilla.flags = VR_GORILLA_HANDS;
	client->private_cmd_queue[0] = pending;
	client->private_pmove_credit_msec = 100;
	SV_PrivateWalkTrialDeferNativeHead (client);
	assert (!memcmp (&client->private_cmd_queue[0], &pending, sizeof (pending)) &&
		client->private_cmd_queue_count == 1 && client->private_completed_move == 1 &&
		client->vr_gorilla_cursor_valid && client->vr_gorilla_last_sequence == 1);
	client->vr_gorilla_capable = true;
	Cvar_SetQuick (&sv_gorilla, "1");
	host_frametime = .010;
	pr_global_struct->frametime = .010f;
	SV_Physics_Client (player, 1);
	assert (client->active && client->private_completed_move == 2 &&
		client->private_move_native_frame && client->private_pmove_credit_msec == 0 &&
		client->vr_gorilla_cursor_valid && client->vr_gorilla_last_sequence == 2 &&
		client->vr_gorilla_state.initialized);
	SV_FinishPrivateUsercmds ();
	assert (!client->private_cmd_queue_count);
	/* The same handoff must retain a pre-existing relocation cutoff, rather
	 * than revive a pose accepted at the old origin. Selection/QC are still
	 * component-prepared; raw sample consumption is the actual native owner. */
	restore_player (player, client, vars, vars_size, globals, globals_size, dry_time);
	client->private_pmove_walk_selected = true;
	client->private_completed_move = 1;
	client->lastmovemessage = 2;
	client->private_cmd_queue_count = 1;
	client->private_cmd_queue_msec = 30;
	client->private_cmd_queue[0] = pending;
	client->vr_gorilla_last_sequence = 2;
	client->vr_gorilla_cursor_valid = true;
	SV_PrivateWalkTrialDeferNativeHead (client);
	assert (client->vr_gorilla_cursor_valid && client->vr_gorilla_last_sequence == 2);
	host_frametime = .010;
	pr_global_struct->frametime = .010f;
	SV_Physics_Client (player, 1);
	assert (client->active && client->private_completed_move == 2 &&
		client->vr_gorilla_cursor_valid && client->vr_gorilla_last_sequence == 2 &&
		!client->vr_gorilla_state.initialized);
	SV_FinishPrivateUsercmds ();
	assert (!client->private_cmd_queue_count);
	client->vr_gorilla_capable = false;
	Cvar_SetQuick (&sv_gorilla, "0");
	puts ("Q30_RETAINED_NATIVE_HEAD_GORILLA_PASSED pending consumed / invalidated skipped");
}

int main (int argc, char **argv)
{
	const char *map = "e1m1";
	for (int i = 1; i + 1 < argc; ++i)
		if (!strcmp (argv[i], "-traversal-map"))
			map = argv[i + 1];
	Fixture_InitNativeEngine (argc, argv, map, true);
	assert (SV_PrivateWalkTrialQ30Program ());
	/* The stock comparison map has no boots item spawn to precache them. */
	sv.state = ss_loading;
	assert (SV_Precache_Sound ("items/jumpboots3a.wav"));
	assert (SV_Precache_Sound ("items/jumpboots3b.wav"));
	sv.state = ss_active;
	edict_t *player = EDICT_NUM (1);
	client_t *client = &svs.clients[0];
	movement_sample_t samples[2][3];
	const double baseline_time = qcvm->time;
	const size_t vars_size = qcvm->progs->entityfields * sizeof (float);
	const size_t globals_size = qcvm->progs->numglobals * sizeof (float);
	void *vars_checkpoint = Mem_Alloc (vars_size);
	void *globals_checkpoint = Mem_Alloc (globals_size);
	if (COM_CheckParm ("-traversal"))
	{
		roomscale_liquid_traversal (player, client, baseline_time);
		Mem_Free (vars_checkpoint);
		Mem_Free (globals_checkpoint);
		return EXIT_SUCCESS;
	}
	native_state_cases (player, client, baseline_time, vars_checkpoint,
		vars_size, globals_checkpoint, globals_size);
	if (COM_CheckParm ("-emptyammo"))
	{
		scheduled_empty_ammo_cases (player, client, baseline_time);
		Mem_Free (vars_checkpoint);
		Mem_Free (globals_checkpoint);
		return EXIT_SUCCESS;
	}
	if (COM_CheckParm ("-scheduledcamera"))
	{
		const char *attacks[] = {"player_axe3", "player_sg1", "player_light1", "player_light2"};
		const char *weapons[] = {"IT_AXE", "IT_SHOTGUN", "IT_LIGHTNING", "IT_LIGHTNING"};
		const char *safe[] = {"player_axe2", "player_sg2", "player_run", "player_run"};
		for (int i = 0; i < countof (attacks); ++i)
			scheduled_camera_case (player, client, baseline_time, attacks[i], weapons[i], safe[i]);
		puts ("Q30_SCHEDULED_CAMERA_HANDOFF_PASSED axe / shotgun / both lightning roots");
		Mem_Free (vars_checkpoint);
		Mem_Free (globals_checkpoint);
		return EXIT_SUCCESS;
	}
	const char *cases[] = {"low-release", "boots", "ladder-release", "ladder-rejump"};
	for (int scenario = 0; scenario < countof (cases); ++scenario)
	{
		qcvm->time = baseline_time;
		Cvar_SetQuick (&sv_friction, "4");
		initialize_player (player, client);
		if (scenario == 1)
		{
			player_float (player, "moditems")->_float = global_float ("IT_ARTJUMPBOOTS");
			player_float (player, "jumpboots_airmax")->_float = 2;
			player_float (player, "jumpboots_height")->_float = 300;
			player_float (player, "jumpboots_forward")->_float = 180;
			player_float (player, "jumpboots_finished")->_float = baseline_time + 100;
		}
		if (scenario >= 2)
		{
			/* Isolate QC ladder damping/rearming from native vs PMove floor
			 * friction integration. This is not a trigger-ladder map proof. */
			Cvar_SetQuick (&sv_friction, "0");
			player->v.velocity[0] = 100;
		}
		memcpy (vars_checkpoint, &player->v, vars_size);
		memcpy (globals_checkpoint, qcvm->globals, globals_size);
		const int frames = scenario == 1 ? 3 : 2;
		for (int selected = 0; selected < 2; ++selected)
		{
			restore_player (player, client, vars_checkpoint, vars_size,
				globals_checkpoint, globals_size, baseline_time);
			for (int frame = 0; frame < frames; ++frame)
			{
				unsigned buttons = scenario < 2 && frame != 1 ? BUTTON_JUMP : 0;
				if (scenario == 3 && frame == 1)
					buttons = BUTTON_JUMP;
				if (scenario == 2 || (scenario == 3 && frame == 0))
					player_float (player, "onladder")->_float = global_float ("LADDER_JUMP");
				samples[selected][frame] = step_player (player, client, selected, frame + 1, buttons, 5);
				movement_sample_t *sample = &samples[selected][frame];
				printf ("Q30_MOVE case=%s owner=%s frame=%d z=%.6f vx=%.6f vz=%.6f flags=%d boots=%.0f ladder=%.0f ack=%d\n",
					cases[scenario], selected ? "selected" : "native", frame + 1,
					sample->origin[2], sample->velocity[0], sample->velocity[2],
					sample->flags, sample->boots_left, sample->ladder, sample->completed);
				if (scenario < 2 || (scenario == 3 && frame == 1))
					assert (sample->velocity[2] > 0 && !(sample->flags & FL_ONGROUND));
				else
					assert (sample->ladder == 0);
				if (selected && scenario < 2 && frame == 0 &&
					SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_WALK)
					selected_maintenance (player, client);
			}
		}
		for (int frame = 0; frame < frames; ++frame)
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				assert (fabsf (samples[0][frame].velocity[axis] - samples[1][frame].velocity[axis]) < .01f);
				assert (fabsf (samples[0][frame].origin[axis] - samples[1][frame].origin[axis]) < .2f);
			}
			assert (samples[0][frame].flags == samples[1][frame].flags);
			assert (samples[0][frame].health > 0 && samples[0][frame].health == samples[1][frame].health);
			assert (!samples[0][frame].deadflag && !samples[1][frame].deadflag);
			assert (samples[0][frame].boots_left == samples[1][frame].boots_left);
		}
		if (scenario == 1)
			assert (samples[0][2].velocity[2] == 296 && samples[0][2].boots_left == 1);
		if (scenario == 2)
			assert (samples[0][0].velocity[0] == 90 && samples[0][1].velocity[0] == 81);
		if (scenario == 3)
			assert (samples[0][1].velocity[2] == 116 && samples[1][1].velocity[2] == 116);
		printf ("Q30_CASE_PASSED %s\n", cases[scenario]);
	}

	/* A duration survey is deliberately separate from the strict 5 ms
	 * comparison above. Report integrator differences instead of declaring
	 * broad native trajectory parity from a matching impulse. */
	const int durations[] = {1, 5, 16, 125};
	for (int duration = 0; duration < countof (durations); ++duration)
	{
		qcvm->time = baseline_time;
		initialize_player (player, client);
		Cvar_SetQuick (&sv_friction, "0");
		player->v.velocity[0] = 100;
		memcpy (vars_checkpoint, &player->v, vars_size);
		memcpy (globals_checkpoint, qcvm->globals, globals_size);
		for (int selected = 0; selected < 2; ++selected)
		{
			restore_player (player, client, vars_checkpoint, vars_size,
				globals_checkpoint, globals_size, baseline_time);
			for (int frame = 0; frame < 2; ++frame)
			{
				if (!frame)
					player_float (player, "onladder")->_float = global_float ("LADDER_JUMP");
				samples[selected][frame] = step_player (player, client, selected,
					frame + 1, frame ? BUTTON_JUMP : 0, durations[duration]);
				movement_sample_t *sample = &samples[selected][frame];
				printf ("Q30_DURATION msec=%d owner=%s frame=%d z=%.6f vx=%.6f vz=%.6f flags=%d ack=%d\n",
					durations[duration], selected ? "selected" : "native", frame + 1,
					sample->origin[2], sample->velocity[0], sample->velocity[2],
					sample->flags, sample->completed);
				for (int axis = 0; axis < 3; ++axis)
					assert (isfinite (sample->origin[axis]) && isfinite (sample->velocity[axis]));
				assert (sample->health > 0 && !sample->deadflag && sample->completed == frame + 1);
				if (selected && !frame &&
					SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_WALK)
					selected_maintenance (player, client);
			}
		}
		if (durations[duration] != 1)
		{
			for (int frame = 0; frame < 2; ++frame)
			{
				assert (samples[0][frame].flags == samples[1][frame].flags);
				for (int axis = 0; axis < 3; ++axis)
				{
					assert (fabsf (samples[0][frame].velocity[axis] - samples[1][frame].velocity[axis]) < .01f);
					/* The 125 ms native analytic sweep and five PMove steps
					 * differ by 0.556 units here. This is a bounded local
					 * comparison, not identical trajectories. */
					assert (fabsf (samples[0][frame].origin[axis] - samples[1][frame].origin[axis]) <
						(durations[duration] == 125 ? .6f : .2f));
				}
			}
		}
	}
	puts ("Q30_DURATION_SURVEY_COMPLETE");

	qcvm->time = baseline_time;
	Cvar_SetQuick (&sv_friction, "4");
	initialize_player (player, client);
	for (int i = 0; i < 4; ++i)
		step_player (player, client, false, i + 1, 0, 5);
	const double paired_time = qcvm->time;
	memcpy (vars_checkpoint, &player->v, vars_size);
	memcpy (globals_checkpoint, qcvm->globals, globals_size);
	step_player (player, client, false, 1, BUTTON_JUMP, 5);
	movement_sample_t native = step_player (player, client, false, 2, 0, 5);
	restore_player (player, client, vars_checkpoint, vars_size,
		globals_checkpoint, globals_size, paired_time);
	client->private_pmove_walk_selected = true;
	client->private_cmd_queue_count = 2;
	client->private_cmd_queue_msec = 10;
	client->lastmovemessage = 2;
	for (int i = 0; i < 2; ++i)
	{
		usercmd_t *command = &client->private_cmd_queue[i];
		memset (command, 0, sizeof (*command));
		command->sequence = i + 1;
		command->msec = 5;
		command->seconds = .005f;
		command->servertime = paired_time + i * .005;
		command->buttons = i ? 0 : BUTTON_JUMP;
	}
	host_frametime = .01;
	pr_global_struct->frametime = .01f;
	SV_Physics_Client (player, 1);
	assert (client->active && !player->free && client->private_completed_move == 2);
	SV_FinishPrivateUsercmds ();
	assert (client->private_cmd_queue_count == 0);
	movement_sample_t paired = sample_player (player, client);
	for (int axis = 0; axis < 3; ++axis)
	{
		assert (fabsf (native.velocity[axis] - paired.velocity[axis]) < .01f);
		assert (fabsf (native.origin[axis] - paired.origin[axis]) < .2f);
	}
	assert (native.flags == paired.flags && paired.velocity[2] == 112);
	selected_maintenance (player, client);
	puts ("Q30_PAIRED_RELEASE_PASSED");
	ordinary_dry_cases (player, client, vars_checkpoint, vars_size,
		globals_checkpoint, globals_size, baseline_time);
	ordinary_replay_cases (player, client, baseline_time);
	Mem_Free (vars_checkpoint);
	Mem_Free (globals_checkpoint);
	puts ("Q30_MOVEMENT_NATIVE_PASSED");
	SDL_Quit ();
	return 0;
}
