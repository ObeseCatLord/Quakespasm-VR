/* Reuse actual admission, client resource/baseline preparation and captured
 * send/full-snapshot helpers. Native/selected comparisons use separate starts,
 * not a partial world checkpoint. Commands/context remain synthetic. */
#define MIXED_NATIVE_FIXTURE_ENTRY ImportedMixedNativeMain
#include "mixed_native_fixture.c"
/* Compile the actual physics owner here to use its internal contents/force
 * helpers. The make target excludes the normal sv_phys.o, avoiding a copy or
 * production test API. */
#include "../Quake/sv_phys.c"
#include "../Quake/cl_main.c"

static edict_t *liquid_trace_player;
static const char *liquid_trace_case;
static int liquid_trace_frame;
/* Composition probes only: actual pinned Think/QC still executes first.
 * These prepared callback outputs are not claimed as ordinary stock writers. */
static edict_t *liquid_think_player;
static func_t liquid_think_function;
static int liquid_think_override, liquid_think_override_calls;
static float liquid_think_deadline;
static vec3_t liquid_think_velocity;
/* Optional composition seam after real QC, used by the moving-brush driver
 * to change a newly acquired surface before the existing publication tail. */
static void (*liquid_after_qc_composition) (func_t function);
static edict_t *liquid_late_entity, *liquid_late_player, *liquid_late_trigger;
static int liquid_late_calls;
static float liquid_late_deadline;
void __real_PR_ExecuteProgram (func_t function);
void __wrap_PR_ExecuteProgram (func_t function)
{
	qboolean late = liquid_late_entity &&
		pr_global_struct->self == EDICT_TO_PROG (liquid_late_entity);
	qboolean late_touch = liquid_late_trigger && liquid_late_player &&
		pr_global_struct->self == EDICT_TO_PROG (liquid_late_trigger) &&
		pr_global_struct->other == EDICT_TO_PROG (liquid_late_player) &&
		function == liquid_late_trigger->v.touch;
	float saved_time = pr_global_struct->time;
	if (late_touch)
		pr_global_struct->time = liquid_late_deadline - .7f;
	const qboolean record = liquid_trace_player &&
		pr_global_struct->self == EDICT_TO_PROG (liquid_trace_player) &&
		function == pr_global_struct->PlayerPreThink;
	vec3_t before;
	float before_health = liquid_trace_player ? liquid_trace_player->v.health : 0;
	float before_level = liquid_trace_player ? liquid_trace_player->v.waterlevel : 0;
	eval_t *damage_time = liquid_trace_player ? GetEdictFieldValue (liquid_trace_player, ED_FindFieldOffset ("dmgtime")) : NULL;
	float before_damage_time = damage_time ? damage_time->_float : 0;
	if (liquid_trace_player) VectorCopy (liquid_trace_player->v.velocity, before);
	__real_PR_ExecuteProgram (function);
	if (liquid_after_qc_composition)
		liquid_after_qc_composition (function);
	if (late_touch)
	{
		assert (liquid_late_player->v.teleport_time == liquid_late_deadline);
		pr_global_struct->time = saved_time;
		liquid_late_calls++;
	}
	if (liquid_think_override && liquid_think_player &&
		pr_global_struct->self == EDICT_TO_PROG (liquid_think_player) &&
		function == liquid_think_function)
	{
		if (liquid_think_override == 1)
			liquid_think_player->v.teleport_time = liquid_think_deadline;
		else if (liquid_think_override == 2)
			liquid_think_player->v.flags = (int)liquid_think_player->v.flags & ~FL_WATERJUMP;
		else
			VectorCopy (liquid_think_velocity, liquid_think_player->v.velocity);
		liquid_think_override_calls++;
	}
	if (late)
	{
		/* Prepared callback composition after a real later-world Think. The
		 * world trigger dispatcher and identified pinned teleport_touch run
		 * unchanged; its supplied QC time forces an equal-value deadline. */
		int saved_self = pr_global_struct->self, saved_other = pr_global_struct->other;
		liquid_late_entity = NULL;
		liquid_late_trigger->v.solid = SOLID_TRIGGER;
		SV_LinkEdict (liquid_late_trigger, false);
		SV_LinkEdict (liquid_late_player, true);
		pr_global_struct->self = saved_self;
		pr_global_struct->other = saved_other;
	}
	if (liquid_trace_player && !record &&
		(!VectorCompare (before, liquid_trace_player->v.velocity) || before_health != liquid_trace_player->v.health))
		fprintf (stderr, "STOCK_QC_EXTERNAL_FORCE case=%s frame=%d function=%s health=%g->%g velocity=%.6f,%.6f,%.6f->%.6f,%.6f,%.6f\n",
			liquid_trace_case, liquid_trace_frame, PR_GetString (qcvm->functions[function].s_name), before_health,
			liquid_trace_player->v.health, before[0], before[1], before[2], liquid_trace_player->v.velocity[0],
			liquid_trace_player->v.velocity[1], liquid_trace_player->v.velocity[2]);
	if (record)
	{
		if (before_health != liquid_trace_player->v.health)
			printf ("STOCK_QC_DAMAGE case=%s frame=%d time=%.6f liquid=%g depth=%g->%g health=%g->%g amount=%g dmgtime=%.6f->%.6f\n",
				liquid_trace_case, liquid_trace_frame, qcvm->time, liquid_trace_player->v.watertype,
				before_level, liquid_trace_player->v.waterlevel, before_health,
				liquid_trace_player->v.health, before_health - liquid_trace_player->v.health,
				before_damage_time, damage_time ? damage_time->_float : 0);
		printf ("STOCK_QC_FORCE case=%s frame=%d water=%g button=%g before=%.6f,%.6f,%.6f after=%.6f,%.6f,%.6f deadline=%.6f time=%.6f\n",
			liquid_trace_case, liquid_trace_frame, liquid_trace_player->v.waterlevel,
			liquid_trace_player->v.button2, before[0], before[1], before[2],
			liquid_trace_player->v.velocity[0], liquid_trace_player->v.velocity[1],
			liquid_trace_player->v.velocity[2], liquid_trace_player->v.teleport_time, qcvm->time);
	}
}

static qboolean FindLiquidPosition (edict_t *player, int contents, int depth, vec3_t found)
{
	qmodel_t *world = qcvm->worldmodel;
	vec3_t saved_origin;
	float saved_level = player->v.waterlevel, saved_type = player->v.watertype;
	qboolean located = false;
	VectorCopy (player->v.origin, saved_origin);
	for (int leafnum = 1; leafnum <= world->numleafs && !located; ++leafnum)
	{
		mleaf_t *leaf = &world->leafs[leafnum];
		if (leaf->contents != contents) continue;
		for (int z = 0; z < 9 && !located; ++z)
			for (int x = 1; x < 4 && !located; ++x)
				for (int y = 1; y < 4 && !located; ++y)
				{
					vec3_t position = {
						leaf->minmaxs[0] + (leaf->minmaxs[3] - leaf->minmaxs[0]) * x / 4.0f,
						leaf->minmaxs[1] + (leaf->minmaxs[4] - leaf->minmaxs[1]) * y / 4.0f,
						leaf->minmaxs[5] + 16.0f - z * 8.0f};
					VectorCopy (position, player->v.origin);
					SV_CheckWater (player);
					if (player->v.waterlevel != depth || player->v.watertype != contents || SV_TestEntityPosition (player))
						continue;
					VectorCopy (position, found);
					located = true;
				}
	}
	VectorCopy (saved_origin, player->v.origin);
	player->v.waterlevel = saved_level;
	player->v.watertype = saved_type;
	return located;
}

static qboolean FindLiquidJumpPosition (edict_t *player, int contents, vec3_t found, float *yaw)
{
	/* Invoke the pinned QC geometry helper while searching, then require the
	 * same start to produce a ledge jump through admitted command execution. */
	dfunction_t *check = ED_FindFunction ("CheckWaterJump");
	entvars_t saved = player->v;
	int saved_self = pr_global_struct->self;
	qboolean located = false;
	assert (check);
	pr_global_struct->self = EDICT_TO_PROG (player);
	pr_global_struct->time = qcvm->time;
	for (int leafnum = 1; leafnum <= qcvm->worldmodel->numleafs && !located; ++leafnum)
	{
		mleaf_t *leaf = &qcvm->worldmodel->leafs[leafnum];
		if (leaf->contents != contents) continue;
		for (int z = 0; z < 32 && !located; ++z)
			for (int x = 0; x <= 16 && !located; ++x)
				for (int y = 0; y <= 16 && !located; ++y)
				{
					player->v = saved;
					VectorSet (player->v.origin,
						leaf->minmaxs[0] + (leaf->minmaxs[3] - leaf->minmaxs[0]) * x / 16.0f,
						leaf->minmaxs[1] + (leaf->minmaxs[4] - leaf->minmaxs[1]) * y / 16.0f,
						leaf->minmaxs[5] + 16.0f - z * 2.0f);
					SV_CheckWater (player);
					if (player->v.waterlevel != 2 || player->v.watertype != contents || SV_TestEntityPosition (player)) continue;
					for (int direction = 0; direction < 4 && !located; ++direction)
					{
						player->v.flags = FL_CLIENT | FL_JUMPRELEASED;
						VectorSet (player->v.angles, 0, direction * 90, 0);
						__real_PR_ExecuteProgram (check - qcvm->functions);
						if (!((int)player->v.flags & FL_WATERJUMP)) continue;
						VectorCopy (player->v.origin, found);
						*yaw = direction * 90;
						located = true;
					}
				}
	}
	player->v = saved;
	pr_global_struct->self = saved_self;
	return located;
}

static void LiquidSend (client_t *peer, client_state_t *state, unsigned buttons,
	float forward, float up, qboolean vr, float roomscale)
{
	usercmd_t command = {0};
	cl = *state;
	cls.netcon = peer->netconnection;
	cl.time = qcvm->time;
	command.servertime = cl.time;
	VectorCopy (cl.viewangles, command.viewangles);
	command.forwardmove = forward;
	command.upmove = up;
	command.buttons = buttons;
	if (vr)
	{
		command.vr_active = command.vr_handpos_relative = true;
		command.vr_handpos[1] = 16;
		command.vr_handpos[2] = 22;
		VectorCopy (command.viewangles, command.vr_handrot);
		command.vr_roomscalemove[1] = roomscale;
	}
	captured_length = 0;
	CL_SendMove (&command);
	*state = cl;
	if (captured_length) GapDeliver (peer, captured, captured_length);
}

static void PrepareLiquidCase (client_t *peer, client_state_t *state,
	const vec3_t position, float yaw)
{
	char command[128];
	host_client = peer;
	sv_player = peer->edict;
	/* Actual stock cooperative ClientKill resets player gameplay fields by
	 * immediately respawning. Then use the existing relocation command. */
	Cmd_ExecuteString ("kill", src_client);
	q_snprintf (command, sizeof (command), "setpos %.6f %.6f %.6f 0 %.6f 0",
		position[0], position[1], position[2], yaw);
	Cmd_ExecuteString (command, src_client);
	Cmd_ExecuteString ("noclip 0", src_client);
	assert (peer->edict->v.health > 0 && peer->edict->v.movetype == MOVETYPE_WALK);
	/* Controlled comparison state: real supporting hull trace, release latch,
	 * and no unrelated spawn teleport hold. Admission itself is never staged. */
	trace_t floor = SV_Move (peer->edict->v.origin, peer->edict->v.mins,
		peer->edict->v.maxs, (vec3_t){position[0], position[1], position[2] - 2},
		MOVE_NORMAL, peer->edict);
	peer->edict->v.flags = FL_CLIENT | FL_JUMPRELEASED;
	Cmd_ExecuteString ("notarget 1", src_client);
	if (floor.fraction < 1 && floor.plane.normal[2] > .7f)
		peer->edict->v.flags += FL_ONGROUND;
	peer->edict->v.groundentity = floor.ent ? EDICT_TO_PROG (floor.ent) : 0;
	peer->edict->v.teleport_time = 0;
	SV_CheckWater (peer->edict);
	sv.paused = true;
	SV_RunClients ();
	sv.paused = false;
	SV_RunClients ();
	/* Native receipt-side thinking can retain the previous case's velocity;
	 * give both comparison starts the same explicit resting velocity. */
	VectorClear (peer->edict->v.velocity);
	GapSnapshot (peer, state);
	VectorClear (state->viewangles);
	state->viewangles[YAW] = yaw;
}

static void StartLiquidPeers (int argc, char **argv, const char *map,
	int command_msec, client_t *peers[2], client_state_t *states[2])
{
	char public_offer[1024];
	srand (1); // identical initialized runs, including stock QC random effects
	Fixture_InitNativeEngine (argc, argv, map, true);
	const qboolean selected = COM_CheckParm ("-selected") != 0;
	const qboolean vr = COM_CheckParm ("-vr") != 0;
	Cvar_SetQuick (&sv_qsvr_private, "1");
	if (selected) Cvar_SetQuick (&sv_private_pmove_walk, "1");
	ClientOffer (0, false, modern_offer, sizeof (modern_offer));
	ClientOffer (QSVR_PROTOCOL_PINNED, false, public_offer, sizeof (public_offer));
	peers[0] = SpawnPeer (0, modern_offer, QSVR_PROTOCOL_PINNED);
	peers[1] = SpawnPeer (1, public_offer, 0);
	cls.signon = SIGNONS;
	cls.state = ca_connected;
	cls.demoplayback = false;
	cls.legacy_qsvr = 0;
	for (int slot = 0; slot < 2; ++slot)
	{
		states[slot] = CreateMixedPeerState (peers[slot], slot);
		host_client = peers[slot];
		sv_player = peers[slot]->edict;
		/* Movement qualification excludes asynchronous monster knockback.
		 * Use the real cheat command; environmental QC remains enabled. */
		Cmd_ExecuteString ("notarget 1", src_client);
	}
	host_frametime = command_msec * .001;
	/* Normal producer startup suppresses the first two movement commands. */
	for (int frame = 0; frame < 3; ++frame)
	{
		realtime += host_frametime;
		LiquidSend (peers[0], states[0], 0, 0, 0, vr, 0);
		GapWorldFrame ();
		GapSnapshot (peers[0], states[0]);
	}
}

static qboolean LiquidPendingReplay (client_state_t *state, vec3_t origin,
	vec3_t velocity)
{
	cl = *state;
	const qboolean expected = cl.move_ack_prediction_allowed && cl.stats[STAT_HEALTH] > 0;
	qboolean replayed = CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], origin);
	assert (replayed == expected);
	if (replayed) VectorCopy (cl.velocity, velocity);
	*state = cl;
	return replayed;
}

static void LiquidDisposablePreview (client_state_t *state)
{
	cl = *state;
	if (!cl.move_ack_prediction_allowed || cl.stats[STAT_HEALTH] <= 0)
		return;
	usercmd_t *history = Mem_Alloc (sizeof (cl.movecmds));
	memcpy (history, cl.movecmds, sizeof (cl.movecmds));
	const usercmd_t pending = cl.pendingcmd;
	const float jump = cl.statsf[STAT_PRIVATE_JUMP_SECS];
	const float waterjump = cl.statsf[STAT_PRIVATE_WATERJUMP_SECS];
	const entity_state_t baseline = cl.entities[cl.viewentity].netstate;
	const int ack = cl.ackedmovemessages;
	const double clock = realtime;
	/* Prepared desktop axis state; the committed VR-history cases exercise
	 * actual serialized VR fields separately. No headset input is claimed. */
	cl.pendingcmd.upmove = 100;
	realtime += host_frametime * .5;
	usercmd_t preview;
	CL_PrepareReplayPreview (&preview, true);
	assert (preview.msec > 0 && preview.seconds > 0);
	vec3_t origin;
	assert (CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], origin));
	assert (!memcmp (history, cl.movecmds, sizeof (cl.movecmds)) &&
		cl.statsf[STAT_PRIVATE_JUMP_SECS] == jump &&
		cl.statsf[STAT_PRIVATE_WATERJUMP_SECS] == waterjump &&
		!memcmp (&baseline, &cl.entities[cl.viewentity].netstate, sizeof (baseline)) &&
		cl.ackedmovemessages == ack);
	realtime = clock;
	cl.pendingcmd = pending;
	Mem_Free (history);
	*state = cl;
}

#ifndef STOCK_LIQUID_FIXTURE_ENTRY
#define STOCK_LIQUID_FIXTURE_ENTRY main
#endif
int STOCK_LIQUID_FIXTURE_ENTRY (int argc, char **argv)
{
	client_t *peers[2];
	client_state_t *states[2];
	vec3_t water[3], dry, ledge = {0};
	float ledge_yaw = 0;
	const char *map = "e1m1";
	const char *only_case = NULL;
	const char *liquid = "water";
	int contents;
	int command_msec = 25;
	for (int arg = 1; arg + 1 < argc; ++arg)
	{
		if (!strcmp (argv[arg], "-fixturemap")) map = argv[arg + 1];
		if (!strcmp (argv[arg], "-fixturemsec")) command_msec = atoi (argv[arg + 1]);
		if (!strcmp (argv[arg], "-fixturecase")) only_case = argv[arg + 1];
		if (!strcmp (argv[arg], "-fixtureliquid")) liquid = argv[arg + 1];
	}
	assert (command_msec >= 1 && command_msec <= 125);
	assert (!strcmp (liquid, "water") || !strcmp (liquid, "slime") || !strcmp (liquid, "lava"));
	contents = !strcmp (liquid, "water") ? CONTENTS_WATER :
		!strcmp (liquid, "slime") ? CONTENTS_SLIME : CONTENTS_LAVA;
	StartLiquidPeers (argc, argv, map, command_msec, peers, states);
	const qboolean selected = COM_CheckParm ("-selected") != 0;
	const qboolean vr = COM_CheckParm ("-vr") != 0;
	for (int depth = 1; depth <= 3; ++depth)
	{
		assert (FindLiquidPosition (peers[0]->edict, contents, depth, water[depth - 1]));
		printf ("STOCK_LIQUID_GEOMETRY liquid=%s depth=%d origin=%.3f,%.3f,%.3f\n",
			liquid, depth, water[depth - 1][0], water[depth - 1][1], water[depth - 1][2]);
	}
	trace_t floor = SV_Move (peers[0]->edict->v.origin, peers[0]->edict->v.mins,
		peers[0]->edict->v.maxs, (vec3_t){peers[0]->edict->v.origin[0],
			peers[0]->edict->v.origin[1], peers[0]->edict->v.origin[2] - 256},
		MOVE_NORMAL, peers[0]->edict);
	assert (!floor.startsolid && !floor.allsolid && floor.fraction < 1);
	VectorCopy (floor.endpos, dry);
	qboolean found_ledge = !COM_CheckParm ("-skipledge") &&
		FindLiquidJumpPosition (peers[0]->edict, contents, ledge, &ledge_yaw);
	if (COM_CheckParm ("-requireledge")) assert (found_ledge);
	if (found_ledge)
		printf ("STOCK_WATER_LEDGE map=%s origin=%.3f,%.3f,%.3f yaw=%.3f\n", map, ledge[0], ledge[1], ledge[2], ledge_yaw);
	else
		printf ("STOCK_WATER_LEDGE_NOT_FOUND map=%s; ledge qualification remains open\n", map);
	const char *cases[] = {"dry-jump", "shallow", "surface", "deep-sink", "deep-up", "roomscale", "surface-button", "deep-button", "ledge"};
	for (unsigned scenario = 0; scenario < countof (cases) - !found_ledge; ++scenario)
	{
		if (only_case && strcmp (only_case, cases[scenario])) continue;
		const float *position = scenario == 0 ? dry : scenario == 8 ? ledge : water[scenario == 1 ? 0 : scenario == 2 || scenario == 6 ? 1 : 2];
		PrepareLiquidCase (peers[0], states[0], position, scenario == 8 ? ledge_yaw : 0);
		liquid_trace_player = peers[0]->edict;
		liquid_trace_case = cases[scenario];
		qboolean saw_ledge = false, saw_ledge_end = false;
		for (int frame = 0; frame < (scenario == 8 ? 100 : 12); ++frame)
		{
			liquid_trace_frame = frame;
			unsigned buttons = (scenario == 0 || scenario == 6 || scenario == 7) && frame < 3 ? BUTTON_JUMP : 0;
			realtime += host_frametime;
			LiquidSend (peers[0], states[0], buttons, scenario == 8 ? 200 : 0,
				scenario == 4 ? 100 : 0, vr, scenario == 5 && frame == 0 ? 1 : 0);
			if (scenario == 5 && frame == 0 && captured_length)
				GapDeliver (peers[0], captured, captured_length); // same roomscale twice
			vec3_t pending_origin = {0}, pending_velocity = {0};
			qboolean predicted = selected && LiquidPendingReplay (states[0], pending_origin, pending_velocity);
			const float pending_timer = predicted ? pmove.waterjumptime : 0;
			const float pending_jump = predicted ? pmove.jump_secs : 0;
			cl_replay_result_t ledge_history = {0};
			if (predicted && scenario == 8 && !vr)
			{
				/* Separate the actual live zero-preview presentation from a
				 * diagnostic history-only oracle. Live remains required. */
				cl = *states[0];
				assert (CL_ComputeReplayPlayerMovement (&cl.entities[cl.viewentity],
					&ledge_history, true, cl.movemessages - 1));
			}
			GapWorldFrame ();
			if (scenario == 5)
				assert (fabsf (peers[0]->edict->v.origin[1] - water[2][1] - (vr ? 1 : 0)) < .001f);
			if (predicted)
			{
				/* One-command error is bounded by truncating the
				 * authoritative velocity to 1/8 plus float arithmetic. Ledge
				 * slope history has the same bound; its zero-preview ground
				 * clip is checked explicitly below, not tolerated loosely. */
				for (int axis = 0; axis < 3; ++axis)
				{
					const qboolean zero_preview_clip = scenario == 8 && axis == 2 &&
						fabsf (pending_velocity[axis] - peers[0]->edict->v.velocity[axis]) >= .126f;
					if (fabsf (pending_origin[axis] - peers[0]->edict->v.origin[axis]) >= .125f * host_frametime + .001f ||
						(!zero_preview_clip && fabsf (pending_velocity[axis] - peers[0]->edict->v.velocity[axis]) >= .126f))
						fprintf (stderr, "STOCK_PENDING_MISMATCH map=%s case=%s frame=%d axis=%d predicted=%.9g/%.9g server=%.9g/%.9g water=%g flags=%g timer=%g\n",
							map, cases[scenario], frame, axis, pending_origin[axis], pending_velocity[axis],
							peers[0]->edict->v.origin[axis], peers[0]->edict->v.velocity[axis],
							peers[0]->edict->v.waterlevel, peers[0]->edict->v.flags, peers[0]->private_pmove_waterjump_secs);
					assert (fabsf (pending_origin[axis] - peers[0]->edict->v.origin[axis]) <
						.125f * host_frametime + .001f);
					if (!zero_preview_clip)
						assert (fabsf (pending_velocity[axis] - peers[0]->edict->v.velocity[axis]) < .126f);
					else
					{
						assert ((vr || (ledge_history.onground && !ledge_history.inwater && ledge_history.velocity[2] > 0)) &&
							((int)peers[0]->edict->v.flags & FL_ONGROUND) && peers[0]->edict->v.waterlevel == 0 &&
							peers[0]->edict->v.velocity[2] > 0 &&
							states[0]->onground && !states[0]->inwater && pending_timer == 0 &&
							pending_velocity[2] == 0);
						printf ("STOCK_LEDGE_ZERO_PREVIEW_CLIP frame=%d vr=%d server_z=%g\n",
							frame, vr, peers[0]->edict->v.velocity[2]);
					}
					if (scenario == 8 && !vr)
						assert (fabsf (ledge_history.velocity[axis] - peers[0]->edict->v.velocity[axis]) < .126f);
				}
				if (fabsf (pending_timer - peers[0]->private_pmove_waterjump_secs) >= .000001f)
					/* Zero-duration categorization terminates the disposable
					 * falling timer before the next authoritative command. */
					assert (scenario == 8 && pending_timer == 0 &&
						peers[0]->private_pmove_waterjump_secs > 0 &&
						peers[0]->edict->v.velocity[2] < 0 && pending_velocity[2] < 0);
				assert (fabsf (pending_jump - peers[0]->private_pmove_jump_secs) < .000001f);
				printf ("STOCK_PENDING_LIVE case=%s frame=%d origin_error=%.6f,%.6f,%.6f velocity_error=%.6f,%.6f,%.6f\n",
					cases[scenario], frame, pending_origin[0] - peers[0]->edict->v.origin[0],
					pending_origin[1] - peers[0]->edict->v.origin[1], pending_origin[2] - peers[0]->edict->v.origin[2],
					pending_velocity[0] - peers[0]->edict->v.velocity[0], pending_velocity[1] - peers[0]->edict->v.velocity[1],
					pending_velocity[2] - peers[0]->edict->v.velocity[2]);
			}
			if (scenario == 8)
			{
				if ((int)peers[0]->edict->v.flags & FL_WATERJUMP) saw_ledge = true;
				else if (saw_ledge) saw_ledge_end = true;
			}
			GapSnapshot (peers[0], states[0]);
			assert (peers[0]->active && peers[0]->private_pmove_walk_selected == selected &&
				states[0]->ackedmovemessages == peers[0]->private_completed_move);
			cl = *states[0];
			cl.time = qcvm->time;
			cl.pendingcmd.servertime = cl.time;
			vec3_t replay;
			qboolean replayed = CL_ReplayPlayerMovement (&cl.entities[1], replay);
			if (selected)
			{
				assert (cl.move_snapshot_valid && cl.move_snapshot_ack == cl.ackedmovemessages &&
					cl.move_snapshot_owner == cl.viewentity);
				assert (fabsf (cl.statsf[STAT_PRIVATE_JUMP_SECS] - peers[0]->private_pmove_jump_secs) < .000001f &&
					fabsf (cl.statsf[STAT_PRIVATE_WATERJUMP_SECS] - peers[0]->private_pmove_waterjump_secs) < .000001f);
				assert (cl.move_ack_prediction_allowed && replayed);
			}
			*states[0] = cl;
			if (selected) LiquidDisposablePreview (states[0]);
			printf ("STOCK_LIQUID_SAMPLE selected=%d vr=%d liquid=%s case=%s frame=%d origin=%.6f,%.6f,%.6f velocity=%.6f,%.6f,%.6f flags=%d water=%g health=%g jump=%.6f waterjump=%.6f ack=%d prediction=%d replay=%d\n",
				selected, vr, liquid, cases[scenario], frame,
				peers[0]->edict->v.origin[0], peers[0]->edict->v.origin[1], peers[0]->edict->v.origin[2],
				peers[0]->edict->v.velocity[0], peers[0]->edict->v.velocity[1], peers[0]->edict->v.velocity[2],
				(int)peers[0]->edict->v.flags, peers[0]->edict->v.waterlevel, peers[0]->edict->v.health,
				peers[0]->private_pmove_jump_secs, peers[0]->private_pmove_waterjump_secs,
				peers[0]->private_completed_move, cl.move_ack_prediction_allowed, replayed);
		}
		if (scenario == 8) assert (saw_ledge && saw_ledge_end);
		liquid_trace_player = NULL;
	}
	printf ("STOCK_LIQUID_COMPONENT_PASSED liquid=%s actual admission/geometry/command receipt/QC/world/full snapshots/live pending replay/disposable preview\n", liquid);
	for (int slot = 0; slot < 2; ++slot)
	{
		Mem_Free (states[slot]->entities);
		Mem_Free (states[slot]->scores);
		Mem_Free (states[slot]);
	}
	return 0;
}
