/* Exact installed q30 QC and real world hulls. Selection is injected only
 * here; production admission stays stock-only. Compare native and selected
 * dry takeoff/release using the same inputs and initial player state. */
#include "../Quake/sv_phys.c"
#include <assert.h>
#include "native_engine_fixture.h"

typedef struct
{
	vec3_t origin, velocity;
	int flags;
	float health, deadflag;
	float boots_left, ladder;
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
	sample.completed = client->private_completed_move;
	return sample;
}

static movement_sample_t step_player (edict_t *player, client_t *client,
	qboolean selected, int sequence, unsigned buttons, int msec)
{
	usercmd_t command = {0};
	command.sequence = sequence;
	command.msec = msec;
	command.seconds = msec * .001f;
	command.servertime = qcvm->time;
	command.buttons = buttons;
	client->cmd = command;
	client->lastmovemessage = sequence;
	VectorCopy (command.viewangles, player->v.v_angle);
	player->v.button0 = 0;
	player->v.button2 = (buttons & BUTTON_JUMP) != 0;
	player->v.impulse = 0;
	host_frametime = msec * .001;
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
	assert (client->private_completed_move == sequence);
	if (selected)
		SV_FinishPrivateUsercmds ();
	movement_sample_t sample = sample_player (player, client);
	qcvm->time += host_frametime;
	return sample;
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

int main (int argc, char **argv)
{
	Fixture_InitNativeEngine (argc, argv, "e1m1", true);
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
				if (selected && scenario < 2 && frame == 0)
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
				if (selected && !frame)
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
	memcpy (vars_checkpoint, &player->v, vars_size);
	memcpy (globals_checkpoint, qcvm->globals, globals_size);
	step_player (player, client, false, 1, BUTTON_JUMP, 5);
	movement_sample_t native = step_player (player, client, false, 2, 0, 5);
	restore_player (player, client, vars_checkpoint, vars_size,
		globals_checkpoint, globals_size, baseline_time);
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
		command->servertime = baseline_time + i * .005;
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
	Mem_Free (vars_checkpoint);
	Mem_Free (globals_checkpoint);
	puts ("Q30_MOVEMENT_NATIVE_PASSED");
	SDL_Quit ();
	return 0;
}
