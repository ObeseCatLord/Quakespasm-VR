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
static edict_t *shared_qc_observed_player;
static int shared_qc_pre, shared_qc_post, shared_qc_impulse;
static unsigned shared_qc_buttons;
static double shared_qc_pre_seconds, shared_qc_post_seconds;
static unsigned shared_qc_pre_sequence, shared_qc_post_sequence;
static vec3_t shared_qc_pre_angles, shared_qc_post_angles;
static int shared_qc_post_impulse;
static int shared_qc_composition;
static edict_t *shared_qc_rotation_trigger;
static int shared_qc_rotation_calls;
static qboolean liquid_defer_delivery;
static edict_t *liquid_late_entity, *liquid_late_player, *liquid_late_trigger;
static int liquid_late_calls;
static float liquid_late_deadline;
void __real_PR_ExecuteProgram (func_t function);
void __wrap_PR_ExecuteProgram (func_t function)
{
	if (shared_qc_observed_player &&
		pr_global_struct->self == EDICT_TO_PROG (shared_qc_observed_player))
	{
		if (function == pr_global_struct->PlayerPreThink)
		{
			++shared_qc_pre;
			shared_qc_pre_seconds += pr_global_struct->frametime;
			shared_qc_impulse = shared_qc_observed_player->v.impulse;
			shared_qc_buttons = host_client->cmd.buttons;
			shared_qc_pre_sequence = host_client->cmd.sequence;
			VectorCopy (host_client->cmd.viewangles, shared_qc_pre_angles);
		}
		else if (function == pr_global_struct->PlayerPostThink)
		{
			++shared_qc_post;
			shared_qc_post_seconds += pr_global_struct->frametime;
			shared_qc_post_sequence = host_client->cmd.sequence;
			shared_qc_post_impulse = shared_qc_observed_player->v.impulse;
			VectorCopy (host_client->cmd.viewangles, shared_qc_post_angles);
		}
	}
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
	/* Explicit callback-composition seams, after actual QC/trigger dispatch.
	 * They demonstrate lifecycle preservation, not authored mod/map behavior. */
	if (shared_qc_observed_player &&
		pr_global_struct->self == EDICT_TO_PROG (shared_qc_observed_player))
	{
		if (function == pr_global_struct->PlayerPreThink)
		{
			if (shared_qc_composition == 1) shared_qc_observed_player->v.impulse = 0;
			if (shared_qc_composition == 2) shared_qc_observed_player->v.movetype = MOVETYPE_FLY;
		}
		if (function == pr_global_struct->PlayerPostThink && shared_qc_composition == 4)
		{
			shared_qc_observed_player->v.angles[YAW] += 5;
			shared_qc_observed_player->v.v_angle[YAW] += 5;
		}
	}
	if (shared_qc_composition == 4 && shared_qc_rotation_trigger &&
		pr_global_struct->self == EDICT_TO_PROG (shared_qc_rotation_trigger) &&
		!shared_qc_rotation_calls)
	{
		++shared_qc_rotation_calls;
		shared_qc_observed_player->v.angles[YAW] = 90;
		shared_qc_observed_player->v.v_angle[YAW] = 90;
		shared_qc_observed_player->v.fixangle = 0; // explicit unfixed-rotation counterexample
	}
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

#include "native_liquid_fixture.h"

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

static void LiquidSendInput (client_t *peer, client_state_t *state, unsigned buttons,
	float forward, float up, qboolean vr, float roomscale, int impulse)
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
	command.impulse = impulse;
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
	if (captured_length && !liquid_defer_delivery) GapDeliver (peer, captured, captured_length);
}

static void LiquidSend (client_t *peer, client_state_t *state, unsigned buttons,
	float forward, float up, qboolean vr, float roomscale)
{
	LiquidSendInput (peer, state, buttons, forward, up, vr, roomscale, 0);
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

static edict_t *q30_boots_item;

static void StartLiquidPeers (int argc, char **argv, const char *map,
	int command_msec, client_t *peers[2], client_state_t *states[2])
{
	char public_offer[1024];
	srand (1); // identical initialized runs, including stock QC random effects
	Fixture_InitNativeEngine (argc, argv, map, true);
	if (COM_CheckParm ("-genericboots") || COM_CheckParm ("-sharedqcboundary"))
	{
		/* Test identity only: mount shipped older AD, not identical pak2. */
		const byte sha256[] = {0xf3,0xc4,0x21,0x82,0x16,0xea,0x0d,0x3b,
			0x00,0xdb,0x35,0xee,0xf9,0xe7,0x29,0x45,0xee,0x66,0x87,0x00,0x6b,
			0x3e,0x82,0xc3,0x7e,0xa2,0xae,0x33,0xa0,0xee,0xa9,0x22};
		assert (!SV_PrivateWalkTrialQ30Program () && !SV_PrivateWalkTrialStockProgram () &&
			qcvm->progssize == 2345354 && !memcmp (qcvm->progssha256, sha256, sizeof (sha256)));
	}
	if (COM_CheckParm ("-q30boots") || COM_CheckParm ("-genericboots"))
	{
		/* Prepared artifact, actual installed spawn QC and assets, before
		 * the existing client resource copy. Setup/contacts run in the world. */
		dfunction_t *spawn = ED_FindFunction ("item_artifact_jumpboots");
		if (COM_CheckParm ("-genericboots"))
		{
			extern void IN_JumpDown (void);
			Cmd_AddCommand ("+jump", IN_JumpDown); // dedicated bootstrap lacks client input registration
		}
		else assert (SV_PrivateWalkTrialQ30Program ());
		assert (spawn);
		q30_boots_item = ED_Alloc ();
		q30_boots_item->v.classname = PR_SetEngineString ("item_artifact_jumpboots");
		GetEdictFieldValue (q30_boots_item, ED_FindFieldOffset ("cnt"))->_float = 2;
		GetEdictFieldValue (q30_boots_item, ED_FindFieldOffset ("count"))->_float = 2;
		GetEdictFieldValue (q30_boots_item, ED_FindFieldOffset ("height"))->_float = 300;
		for (int i = svs.maxclients + 1; i < qcvm->num_edicts; ++i)
		{
			edict_t *start = EDICT_NUM (i);
			if (!start->free && !strcmp (PR_GetString (start->v.classname), "info_player_start"))
			{
				VectorCopy (start->v.origin, q30_boots_item->v.origin);
				q30_boots_item->v.origin[2] += 32;
				break;
			}
		}
		const int saved_self = pr_global_struct->self;
		pr_global_struct->self = EDICT_TO_PROG (q30_boots_item);
		pr_global_struct->time = qcvm->time;
		sv.state = ss_loading;
		PR_ExecuteProgram (spawn - qcvm->functions);
		sv.state = ss_active;
		pr_global_struct->self = saved_self;
		assert (!q30_boots_item->free && q30_boots_item->v.nextthink > qcvm->time);
	}
	const qboolean defaults = COM_CheckParm ("-defaultselection") != 0;
	const qboolean selected = defaults || COM_CheckParm ("-selected") != 0;
	const qboolean vr = COM_CheckParm ("-vr") != 0;
	ConfigurePrivateMovementFixture (selected, defaults);
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

static void LiquidDisposablePreview (client_state_t *state, qboolean forward)
{
	cl = *state;
	if (forward)
		assert (cl.move_ack_prediction_allowed && cl.stats[STAT_HEALTH] > 0);
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
	if (forward)
		cl.pendingcmd.forwardmove = 100;
	else
		cl.pendingcmd.upmove = 100;
	realtime += host_frametime * .5;
	usercmd_t preview;
	CL_PrepareReplayPreview (&preview, true);
	assert (preview.msec > 0 && preview.seconds > 0);
	vec3_t origin;
	assert (CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], origin));
	if (forward)
		assert (hypotf (origin[0] - baseline.origin[0], origin[1] - baseline.origin[1]) > .01f);
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

static void SharedQCJumpPreview (client_state_t *state)
{
	cl = *state;
	assert (cl.move_ack_prediction_allowed && cl.stats[STAT_HEALTH] > 0 &&
		cl.move_ack_authority == MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT);
	const usercmd_t pending = cl.pendingcmd;
	const entity_state_t baseline = cl.entities[cl.viewentity].netstate;
	const int ack = cl.ackedmovemessages;
	extern kbutton_t in_jump;
	const kbutton_t saved_jump = in_jump;
	Cmd_ExecuteString ("+jump", src_command); // real preview reads key levels
	const double clock = realtime;
	/* Existing NQ-style PMove applies jump after the first movement substep;
	 * use enough forecast time to observe actual upward displacement too. */
	realtime += .030;
	vec3_t origin = {0};
	usercmd_t preview;
	CL_PrepareReplayPreview (&preview, true);
	const qboolean replayed = CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], origin);
	printf ("SHARED_QC_JUMP_PREVIEW_SAMPLE replay=%d buttons=%u msec=%u seconds=%g baseline=%g origin=%g ground=%d held=%d jump=%g flags=%d\n",
		replayed, preview.buttons, preview.msec, preview.seconds, baseline.origin[2], origin[2],
		pmove.onground, pmove.jump_held, movevars.jumpspeed, baseline.pmovetype);
	assert (replayed &&
		origin[2] > baseline.origin[2] && cl.ackedmovemessages == ack &&
		!memcmp (&baseline, &cl.entities[cl.viewentity].netstate, sizeof (baseline)));
	realtime = clock;
	in_jump = saved_jump;
	cl.pendingcmd = pending;
	*state = cl;
}

#ifndef STOCK_LIQUID_FIXTURE_ENTRY
#define STOCK_LIQUID_FIXTURE_ENTRY main
#endif

static int q30_boots_contacts;
static func_t q30_boots_touch;
static void Q30BootsObserveQC (func_t function)
{
	if (pr_global_struct->self == EDICT_TO_PROG (q30_boots_item) &&
		function == q30_boots_touch)
		++q30_boots_contacts;
}

static float Q30BootsField (edict_t *player, const char *name)
{
	ddef_t *field = ED_FindField (name);
	assert (field && (field->type & ~DEF_SAVEGLOBAL) == ev_float);
	return GetEdictFieldValue (player, field->ofs)->_float;
}

static void SharedQCPrefix (client_t *peer, client_state_t *state)
{
	edict_t *player = peer->edict;
	const int completed = peer->private_completed_move;
	assert (peer->private_pmove_walk_selected && !peer->private_cmd_queue_count &&
		SV_PrivateWalkTrialClassifyState (peer) == SV_PRIVATE_MOVE_WALK);
	shared_qc_observed_player = player;
	shared_qc_pre = shared_qc_post = 0;
	shared_qc_pre_seconds = shared_qc_post_seconds = 0;
	/* Arrival gap: real world callbacks retain world duration without ACK or
	 * accepted movement duration. Hold delivery, not the producer clock, so
	 * the redundant packet contains three actual25ms accepted commands. */
	byte packet[NET_MAXMESSAGE];
	int packet_length = 0;
	liquid_defer_delivery = true;
	for (int frame = 0; frame < 4; ++frame)
	{
		realtime += host_frametime;
		if (frame < 3)
		{
			if (frame) state->viewangles[YAW] += 30;
			LiquidSendInput (peer, state, frame == 2 ? BUTTON_JUMP : 0,
				0, 0, true, frame == 2 ? .75f : .25f,
				frame == 0 ? 1 : frame == 2 ? 2 : 0);
			assert (captured_length > 0 && captured_length < sizeof (packet));
			packet_length = captured_length;
			memcpy (packet, captured, packet_length);
		}
		GapWorldFrame ();
		GapSnapshot (peer, state);
		assert (peer->private_completed_move == completed &&
			state->ackedmovemessages == completed && !peer->private_cmd_queue_count);
	}
	assert (shared_qc_pre == 4 && shared_qc_post == 4 &&
		fabs (shared_qc_pre_seconds - 4 * host_frametime) < .000001 &&
		fabs (shared_qc_post_seconds - 4 * host_frametime) < .000001);
	liquid_defer_delivery = false;
	GapDeliver (peer, packet, packet_length);
	assert (peer->private_cmd_queue_count == 3);
	const usercmd_t first = peer->private_cmd_queue[peer->private_cmd_queue_head];
	const usercmd_t second = peer->private_cmd_queue[(peer->private_cmd_queue_head + 1) % SV_PRIVATE_CMD_QUEUE_SIZE];
	const usercmd_t suffix = peer->private_cmd_queue[(peer->private_cmd_queue_head + 2) % SV_PRIVATE_CMD_QUEUE_SIZE];
	assert (first.impulse == 1 && !second.impulse && suffix.impulse == 2 &&
		!first.buttons && !second.buttons && suffix.buttons == BUTTON_JUMP);
	vec3_t before, after;
	VectorCopy (player->v.origin, before);
	GapWorldFrame ();
	VectorCopy (player->v.origin, after);
	printf ("SHARED_QC_PREFIX_SAMPLE pre=%d post=%d impulse=%d buttons=%u completed=%d expected=%u contact=%d queued=%u credit=%g room=%.6f weapon=%g worlddt=%.6f,%.6f native=%d\n",
		shared_qc_pre, shared_qc_post, shared_qc_impulse, shared_qc_buttons,
		peer->private_completed_move, second.sequence, peer->private_vr_contact_last_sequence,
		peer->private_cmd_queue_count, peer->private_pmove_credit_msec,
		after[1] - before[1], player->v.weapon, shared_qc_pre_seconds,
		shared_qc_post_seconds, peer->private_move_native_frame);
	assert (peer->active && shared_qc_pre == 5 && shared_qc_post == 5 &&
		shared_qc_impulse == 1 && !shared_qc_buttons &&
		shared_qc_pre_sequence == first.sequence && shared_qc_post_sequence == first.sequence &&
		VectorCompare (shared_qc_pre_angles, first.viewangles) &&
		VectorCompare (shared_qc_post_angles, first.viewangles) &&
		fabs (shared_qc_pre_seconds - 5 * host_frametime) < .000001 &&
		fabs (shared_qc_post_seconds - 5 * host_frametime) < .000001 &&
		peer->private_completed_move == (int)second.sequence &&
		peer->private_vr_contact_last_sequence == (int)second.sequence &&
		peer->private_cmd_queue_count == 1 &&
		!memcmp (&suffix, &peer->private_cmd_queue[peer->private_cmd_queue_head], sizeof (suffix)) &&
		fabsf (after[1] - before[1] - .5f) < .001f &&
		player->v.weapon == IT_AXE && player->v.impulse == 0);
	GapSnapshot (peer, state);
	assert (state->ackedmovemessages == (int)second.sequence &&
		state->move_ack_prediction_allowed);
	vec3_t replay;
	assert (LiquidPendingReplay (state, replay, (vec3_t){0}) &&
		replay[2] > player->v.origin[2]); // actual pending jump is positively forecast
	/* Duplicate delivery cannot apply prefix motion/impulse again. */
	GapDeliver (peer, packet, packet_length);
	assert (peer->private_cmd_queue_count == 1 && VectorCompare (after, player->v.origin));
	realtime += host_frametime;
	GapWorldFrame ();
	assert (peer->active && shared_qc_pre == 6 && shared_qc_post == 6 &&
		shared_qc_impulse == 2 && shared_qc_buttons == BUTTON_JUMP &&
		peer->private_completed_move == (int)suffix.sequence &&
		peer->private_vr_contact_last_sequence == (int)suffix.sequence &&
		!peer->private_cmd_queue_count && player->v.weapon == IT_SHOTGUN &&
		player->v.velocity[2] > 0 && fabsf (player->v.origin[1] - after[1] - .75f) < .001f);
	GapSnapshot (peer, state);
	assert (state->ackedmovemessages == (int)suffix.sequence && state->move_ack_prediction_allowed);
	assert (LiquidPendingReplay (state, replay, (vec3_t){0}));
	shared_qc_observed_player = NULL;
	puts ("SHARED_QC_PREFIX_PASSED actual world maintenance/batch/unsampled impulse-button suffix/single roomscale/empty contact cursor/full-send/pending jump/correction; captured delivery and prepared axes/VR samples");
}

static void SharedQCDeferredRoomScale (client_t *peer, client_state_t *state)
{
	edict_t *player = peer->edict;
	usercmd_t crossing;
	vec3_t dry;
	/* Actual private receipt accepts at most16 horizontal units per sample. */
	assert (FindRoomScaleLiquidEntryBounded (player, peer, &crossing, dry, 16));
	host_client = peer;
	sv_player = player;
	char relocation[128];
	q_snprintf (relocation, sizeof (relocation), "setpos %.6f %.6f %.6f", dry[0], dry[1], dry[2]);
	Cmd_ExecuteString (relocation, src_client);
	Cmd_ExecuteString ("noclip 0", src_client); // setpos enables noclip
	/* Match the shared finder/reference's prepared airborne resting body. */
	VectorClear (player->v.velocity);
	player->v.flags = ((int)player->v.flags | FL_JUMPRELEASED) & ~(FL_ONGROUND | FL_WATERJUMP);
	player->v.groundentity = 0;
	SV_CheckWater (player);
	SV_LinkEdict (player, false);
	GapSnapshot (peer, state); // actual discontinuity/resume observer resets producer clock
	assert (!player->v.waterlevel && SV_PrivateWalkTrialClassifyState (peer) == SV_PRIVATE_MOVE_WALK);
	shared_qc_observed_player = player;
	shared_qc_pre = shared_qc_post = 0;
	shared_qc_pre_seconds = shared_qc_post_seconds = 0;
	realtime += .008;
	LiquidSendInput (peer, state, 0, 0, 0, true, 0, 1);
	const usercmd_t first = peer->private_cmd_queue[peer->private_cmd_queue_head];
	/* Actual producer with distinct pose and the real-sweep finder sample. */
	cl = *state; cls.netcon = peer->netconnection; cl.time = qcvm->time;
	crossing.viewangles[YAW] = first.viewangles[YAW] + 45;
	crossing.vr_handpos[1] = 16; crossing.vr_handpos[2] = 22;
	VectorCopy (crossing.viewangles, crossing.vr_handrot);
	realtime += .008;
	captured_length = 0; CL_SendMove (&crossing); *state = cl;
	assert (captured_length); GapDeliver (peer, captured, captured_length);
	assert (peer->private_cmd_queue_count == 2);
	const usercmd_t suffix = peer->private_cmd_queue[(peer->private_cmd_queue_head + 1) % SV_PRIVATE_CMD_QUEUE_SIZE];
	GapWorldFrame ();
	assert (peer->active && shared_qc_pre == 1 && shared_qc_post == 1 &&
		shared_qc_pre_sequence == first.sequence && shared_qc_post_sequence == first.sequence &&
		VectorCompare (shared_qc_pre_angles, first.viewangles) && VectorCompare (shared_qc_post_angles, first.viewangles) &&
		fabs (shared_qc_pre_seconds - host_frametime) < .000001 &&
		fabs (shared_qc_post_seconds - host_frametime) < .000001 &&
		player->v.weapon == IT_AXE && !player->v.impulse && !player->v.waterlevel &&
		peer->private_move_native_frame && peer->private_pmove_credit_msec == 0 &&
		peer->private_completed_move == (int)first.sequence &&
		peer->private_vr_contact_last_sequence == (int)first.sequence &&
		peer->private_cmd_queue_count == 1 &&
		!memcmp (&suffix, &peer->private_cmd_queue[peer->private_cmd_queue_head], sizeof (suffix)));
	GapSnapshot (peer, state);
	assert (state->ackedmovemessages == (int)first.sequence && !state->move_ack_prediction_allowed);
	realtime += host_frametime;
	GapWorldFrame ();
	GapSnapshot (peer, state);
	assert (peer->active && shared_qc_pre == 2 && shared_qc_post == 2 &&
		peer->private_completed_move == (int)suffix.sequence && !peer->private_cmd_queue_count &&
		player->v.waterlevel > 0 && peer->private_move_native_frame &&
		state->ackedmovemessages == (int)suffix.sequence && !state->move_ack_prediction_allowed);
	shared_qc_observed_player = NULL;
	puts ("SHARED_QC_DEFERRED_ROOM_PASSED actual first-head weapon/one world tail/originating pose/untouched later-roomscale/ACK/next-native crossing; prepared airborne start and captured delivery");
}

static void SharedQCCompositions (client_t *peer, client_state_t *state, const vec3_t home)
{
	edict_t *player = peer->edict;
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		host_client = peer; sv_player = player;
		Cmd_ExecuteString ("fly 0", src_client);
		char relocation[128];
		q_snprintf (relocation, sizeof (relocation), "setpos %.6f %.6f %.6f", home[0], home[1], home[2]);
		Cmd_ExecuteString (relocation, src_client);
		Cmd_ExecuteString ("noclip 0", src_client);
		VectorClear (player->v.velocity);
		player->v.flags = FL_CLIENT | FL_JUMPRELEASED | FL_ONGROUND;
		player->v.groundentity = 0;
		SV_CheckWater (player); SV_LinkEdict (player, false);
		GapSnapshot (peer, state);
		assert (SV_PrivateWalkTrialClassifyState (peer) == SV_PRIVATE_MOVE_WALK);
		/* Establish a discriminating weapon through actual QC, not a field
		 * assignment: clearing impulse2 must leave this axe selected. */
		realtime += host_frametime;
		LiquidSendInput (peer, state, 0, 0, 0, scenario != 1, 0, 1);
		GapWorldFrame (); GapSnapshot (peer, state);
		assert (player->v.weapon == IT_AXE && !peer->private_cmd_queue_count);
		shared_qc_observed_player = player;
		shared_qc_pre = shared_qc_post = 0;
		shared_qc_pre_seconds = shared_qc_post_seconds = 0;
		shared_qc_composition = scenario == 0 ? 1 : scenario == 1 ? 4 : 2;
		state->viewangles[YAW] = 0;
		if (scenario == 1)
		{
			dfunction_t *null_touch = ED_FindFunction ("SUB_Null");
			assert (null_touch);
			shared_qc_rotation_trigger = ED_Alloc ();
			shared_qc_rotation_trigger->v.classname = PR_SetEngineString ("trigger_multiple");
			shared_qc_rotation_trigger->v.movetype = MOVETYPE_NONE;
			shared_qc_rotation_trigger->v.solid = SOLID_TRIGGER;
			shared_qc_rotation_trigger->v.touch = null_touch - qcvm->functions;
			VectorCopy (player->v.origin, shared_qc_rotation_trigger->v.origin);
			VectorSet (shared_qc_rotation_trigger->v.mins, -64, -64, -64);
			VectorSet (shared_qc_rotation_trigger->v.maxs, 64, 64, 64);
			SV_LinkEdict (shared_qc_rotation_trigger, false);
			shared_qc_rotation_calls = 0;
		}
		realtime += .008;
		LiquidSendInput (peer, state, 0, 0, 0, scenario != 1, 0, scenario == 1 ? 0 : 2);
		assert (peer->private_cmd_queue_count == 1);
		const unsigned first = peer->private_cmd_queue[peer->private_cmd_queue_head].sequence;
		state->viewangles[YAW] = 30;
		realtime += .008;
		LiquidSendInput (peer, state, 0, 0, 0, scenario != 1, 0, 0);
		assert (peer->private_cmd_queue_count == 2);
		const usercmd_t suffix = peer->private_cmd_queue[(peer->private_cmd_queue_head + 1) % SV_PRIVATE_CMD_QUEUE_SIZE];
		GapWorldFrame ();
		assert (peer->active && shared_qc_pre == 1 && shared_qc_post == 1 &&
			shared_qc_pre_sequence == first && shared_qc_post_sequence == first);
		if (scenario == 0)
			assert (shared_qc_impulse == 2 && shared_qc_post_impulse == 0 && player->v.weapon == IT_AXE &&
				peer->private_completed_move == (int)suffix.sequence && !peer->private_cmd_queue_count);
		else if (scenario == 1)
		{
			printf ("SHARED_QC_ROTATION_SAMPLE calls=%d body=%g view=%g completed=%d expected=%u queue=%u post=%g fix=%g\n",
				shared_qc_rotation_calls, player->v.angles[YAW], player->v.v_angle[YAW],
				peer->private_completed_move, suffix.sequence, peer->private_cmd_queue_count, shared_qc_post_angles[YAW], player->v.fixangle);
			assert (shared_qc_rotation_calls == 1 && player->v.angles[YAW] == 95 && player->v.v_angle[YAW] == 95 &&
				peer->private_completed_move == (int)suffix.sequence && !peer->private_cmd_queue_count);
			ED_Free (shared_qc_rotation_trigger); shared_qc_rotation_trigger = NULL;
		}
		else
		{
			assert (peer->private_completed_move == (int)first && peer->private_cmd_queue_count == 1 &&
				peer->private_vr_contact_last_sequence == (int)first && player->v.weapon == IT_SHOTGUN &&
				!memcmp (&suffix, &peer->private_cmd_queue[peer->private_cmd_queue_head], sizeof (suffix)));
			shared_qc_composition = 0;
			realtime += host_frametime; GapWorldFrame ();
			assert (shared_qc_pre == 2 && shared_qc_post == 2 &&
				peer->private_completed_move == (int)suffix.sequence && !peer->private_cmd_queue_count);
		}
		GapSnapshot (peer, state);
		shared_qc_composition = 0;
	}
	/* Actual pause recovery invalidates last input while retaining a positive
	 * completed cursor. A prepared living transition in maintenance must not
	 * invent sequence0 completion or finish a resume marker. */
	host_client = peer; sv_player = player;
	Cmd_ExecuteString ("fly 0", src_client);
	StartupPauseCommand (peer, state);
	assert (sv.paused);
	SV_RunClients (); // real host skips SV_Physics while paused
	StartupPauseCommand (peer, state);
	assert (!sv.paused && !peer->private_pmove_last_cmd_valid);
	const int completed = peer->private_completed_move, retired = peer->private_retired_move;
	const private_input_phase_t phase = peer->private_input_phase;
	assert (completed > 0 && !peer->private_cmd_queue_count);
	shared_qc_pre = shared_qc_post = 0;
	shared_qc_composition = 2;
	realtime += host_frametime; GapWorldFrame ();
	assert (peer->active && shared_qc_pre == 1 && shared_qc_post == 1 &&
		peer->private_completed_move == completed && peer->private_retired_move == retired &&
		peer->private_input_phase == phase && !peer->private_cmd_queue_count);
	GapSnapshot (peer, state);
	assert (state->ackedmovemessages == completed);
	shared_qc_composition = 0; shared_qc_observed_player = NULL;
	puts ("SHARED_QC_COMPOSITIONS_PASSED actual QC plus prepared clear/contact rotation/Post read-modify/living continuation; actual pause maintenance keeps completed ACK/resume cursor");
}

static void Q30Boots (client_t *peers[2], client_state_t *states[2], qboolean vr)
{
	client_t *peer = peers[0];
	edict_t *player = peer->edict;
	const qboolean selected = peer->private_pmove_walk_selected;
	const qboolean generic = COM_CheckParm ("-genericboots") != 0;
	const int boots = 1048576; // actual pinned constant, not a decompiler alias
	assert (q30_boots_item && !Q30BootsField (player, "moditems"));
	liquid_after_qc_composition = Q30BootsObserveQC; // observation only
	qboolean saw_before = false, saw_boots = false, saw_expiry = false, saw_return = false;
	qboolean saw_airjump = false, saw_public = false;
	vec3_t public_start;
	VectorCopy (peers[1]->edict->v.origin, public_start);
	/* Actual scheduled setup; notarget deliberately prevents early pickup. */
	for (int frame = 0; frame < 32; ++frame)
	{
		realtime += host_frametime;
		LiquidSend (peer, states[0], 0, 0, 0, vr, 0);
		LiquidSend (peers[1], states[1], 0, -40, 0, false, 0);
		GapWorldFrame ();
		GapSnapshot (peer, states[0]);
		vec3_t replay;
		if (selected && states[0]->move_ack_prediction_allowed)
		{
			assert (LiquidPendingReplay (states[0], replay, (vec3_t){0}));
			saw_before = true;
		}
	}
	assert (q30_boots_item->v.solid == SOLID_TRIGGER &&
		!Q30BootsField (player, "moditems") &&
		(!selected || saw_before));
	if (selected)
	{
		LiquidDisposablePreview (states[0], true);
		puts ("Q30_BOOTS_POSITIVE_PREVIEW_PASSED before pickup; positive duration/horizontal motion/history preserved");
	}
	q30_boots_touch = q30_boots_item->v.touch;
	assert (q30_boots_touch > 0);
	/* Prepared placement of an actual spawned artifact; contact dispatch
	 * awards all player fields. Never call artifact_touch or grant bits here. */
	VectorCopy (player->v.origin, q30_boots_item->v.origin);
	SV_LinkEdict (q30_boots_item, false);
	host_client = peer;
	sv_player = player;
	Cmd_ExecuteString ("notarget 0", src_client);
	for (int frame = 0; frame < 112; ++frame)
	{
		const float before_charge = Q30BootsField (player, "jumpboots_airlvl");
		const qboolean airborne = !((int)player->v.flags & FL_ONGROUND);
		realtime += host_frametime;
		LiquidSend (peer, states[0], frame == 2 || frame == 4 ? BUTTON_JUMP : 0,
			generic ? 20 : 0, 0, vr, 0);
		LiquidSend (peers[1], states[1], 0, -40, 0, false, 0);
		const int sequence = peer->lastmovemessage;
		GapWorldFrame ();
		assert (peer->active && peers[1]->active && player->v.health > 0 &&
			peer->private_pmove_walk_selected == selected);
		const qboolean owned = (int)Q30BootsField (player, "moditems") & boots;
		const float charge = Q30BootsField (player, "jumpboots_airlvl");
		if (owned)
		{
			assert (q30_boots_contacts > 0 && Q30BootsField (player, "jumpboots_height") == 300);
			saw_boots = true;
			if (frame == 4 && airborne && charge < before_charge && player->v.velocity[2] > 0)
				saw_airjump = true;
		}
		else if (saw_boots) saw_expiry = true;
		GapSnapshot (peer, states[0]);
		GapSnapshot (peers[1], states[1]);
		assert (!peers[1]->private_pmove_walk_selected && !states[1]->move_ack_selected_owner &&
			!states[1]->move_ack_prediction_allowed);
		saw_public |= states[0]->entities[2].netstate.modelindex > 0 &&
			states[1]->entities[1].netstate.modelindex > 0;
		vec3_t replay;
		const qboolean replayed = LiquidPendingReplay (states[0], replay, (vec3_t){0});
		if (selected)
		{
			assert (peer->private_completed_move == sequence && !peer->private_cmd_queue_count &&
				!peer->private_cmd_queue_msec && states[0]->move_snapshot_valid &&
				states[0]->move_ack_selected_owner && states[0]->ackedmovemessages == sequence);
			if (owned)
			{
				if (generic)
					assert (!peer->private_move_native_frame && replayed &&
						states[0]->move_ack_authority == MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT);
				else
					assert (peer->private_move_native_frame &&
						states[0]->move_ack_authority == MOVE_AUTHORITY_LEGACY_FRAME && !replayed);
			}
			else if (saw_expiry && replayed) saw_return = true;
		}
		else assert (!replayed && !states[0]->move_ack_selected_owner);
		if (generic && selected && frame == 40)
		{
			SharedQCJumpPreview (states[0]);
			puts ("SHARED_QC_ABILITY_PREVIEW_PASSED positive ordinary jump forecast while boots owned; unknown airborne QC still corrected by snapshots");
		}
		printf ("Q30_BOOTS_SAMPLE frame=%d owned=%d charge=%g deadline=%.6f origin=%.6f,%.6f,%.6f velocity=%.6f,%.6f,%.6f flags=%d health=%g\n",
			frame, owned != 0, charge, Q30BootsField (player, "jumpboots_finished"),
			player->v.origin[0], player->v.origin[1], player->v.origin[2],
			player->v.velocity[0], player->v.velocity[1], player->v.velocity[2], (int)player->v.flags,
			player->v.health);
	}
	assert (saw_boots && saw_airjump && saw_expiry && saw_public && (!selected || saw_return) &&
		hypotf (peers[1]->edict->v.origin[0] - public_start[0],
			peers[1]->edict->v.origin[1] - public_start[1]) > 1);
	if (selected)
	{
		LiquidDisposablePreview (states[0], true);
		puts ("Q30_BOOTS_POSITIVE_PREVIEW_PASSED after expiry; positive duration/horizontal motion/history preserved");
	}
	if (selected && generic)
	{
		vec3_t home;
		VectorCopy (player->v.origin, home);
		SharedQCPrefix (peer, states[0]);
		SharedQCCompositions (peer, states[0], home);
	}
	printf ("Q30_BOOTS_PASSED selected=%d vr=%d actual spawn/setup/contact/airjump/expiry/full-send/replay-return/public peer; prepared artifact placement and captured resources/delivery\n", selected, vr);
	liquid_after_qc_composition = NULL;
	for (int slot = 0; slot < 2; ++slot)
	{
		Mem_Free (states[slot]->entities);
		Mem_Free (states[slot]->scores);
		Mem_Free (states[slot]);
	}
}

/* Reuse admitted peers, actual native/QC owner, shared real-BSP finder and
 * full production publication. The late relocation is prepared, not authored
 * map traversal; checkpoints do not restore every actor/effect stream. */
static void Q30Publication (client_t *peers[2], client_state_t *states[2])
{
	client_t *peer = peers[0];
	edict_t *player = peer->edict;
	assert (SV_PrivateWalkTrialQ30Program () && peer->private_pmove_walk_selected);
	/* Complete a real native fly command, then use the existing command
	 * owner to return to WALK before publication. Do not stage a frame flag. */
	host_client = peer;
	sv_player = player;
	Cmd_ExecuteString ("fly 1", src_client);
	assert (player->v.movetype == MOVETYPE_FLY);
	realtime += host_frametime;
	LiquidSend (peer, states[0], 0, 80, 0, true, 0);
	GapWorldFrame ();
	assert (peer->private_move_native_frame && peer->private_completed_move >= 2 &&
		!peer->private_cmd_queue_count);
	host_client = peer;
	sv_player = player;
	Cmd_ExecuteString ("fly 0", src_client);
	assert (SV_PrivateWalkTrialClassifyState (peer) == SV_PRIVATE_MOVE_WALK &&
		peer->private_move_native_frame && !peer->private_cmd_queue_count &&
		!player->v.waterlevel);
	vec3_t wet = {0};
	assert (FindLiquidPosition (player, CONTENTS_WATER, 2, wet));
	VectorCopy (wet, player->v.origin);
	SV_LinkEdict (player, false);
	assert (SV_PrivateWalkTrialClassifyState (peer) == SV_PRIVATE_MOVE_WALK &&
		SV_PrivateWalkTrialQ30NeedsNative (player, peer, NULL) && !player->v.waterlevel);
	const float cached_level = player->v.waterlevel, cached_type = player->v.watertype;
	realtime += host_frametime;
	LiquidSend (peer, states[0], 0, 100, 0, true, 0);
	assert (peer->private_cmd_queue_count == 1);
	assert (peer->private_cmd_queue[peer->private_cmd_queue_head].forwardmove == 100 &&
		!peer->private_cmd_queue[peer->private_cmd_queue_head].upmove &&
		!peer->private_cmd_queue[peer->private_cmd_queue_head].buttons &&
		!peer->private_cmd_queue[peer->private_cmd_queue_head].impulse);
	const unsigned sequence = peer->private_cmd_queue[peer->private_cmd_queue_head].sequence;
	const size_t vars_size = qcvm->progs->entityfields * sizeof (float);
	const size_t globals_size = qcvm->progs->numglobals * sizeof (float);
	void *vars = Mem_Alloc (vars_size), *globals = Mem_Alloc (globals_size);
	client_t *saved_peer = Mem_Alloc (sizeof (*peer));
	memcpy (vars, &player->v, vars_size);
	memcpy (globals, qcvm->globals, globals_size);
	memcpy (saved_peer, peer, sizeof (*peer));
	const int datagram_size = sv.datagram.cursize;
	const double time = qcvm->time;
	/* Reference consumes the same actually received head through the existing
	 * fresh native owner, without any intervening publication. */
	SV_Physics_ClientSelectedNativeFrame (player, 1, peer, false);
	SV_FinishPrivateUsercmds ();
	assert (peer->active && peer->private_completed_move == (int)sequence &&
		!peer->private_cmd_queue_count && player->v.waterlevel == 2);
	vec3_t origin, velocity;
	VectorCopy (player->v.origin, origin);
	VectorCopy (player->v.velocity, velocity);
	const float flags = player->v.flags, health = player->v.health;
	SV_UnlinkEdict (player);
	memcpy (&player->v, vars, vars_size);
	memcpy (qcvm->globals, globals, globals_size);
	memcpy (peer, saved_peer, sizeof (*peer));
	qcvm->time = time;
	sv.datagram.cursize = datagram_size;
	SV_LinkEdict (player, false);
	GapSnapshot (peer, states[0]); // actual SV_SendClientDatagram, one captured packet
	assert (peer->active && player->v.waterlevel == cached_level &&
		player->v.watertype == cached_type && peer->private_move_native_frame &&
		SV_PrivateWalkTrialClassifyState (peer) == SV_PRIVATE_MOVE_WALK &&
		states[0]->move_snapshot_valid && states[0]->move_ack_selected_owner &&
		states[0]->move_ack_authority == MOVE_AUTHORITY_LEGACY_FRAME &&
		!states[0]->move_ack_prediction_allowed);
	SV_Physics_Client (player, 1);
	SV_FinishPrivateUsercmds ();
	assert (peer->active && peer->private_move_native_frame &&
		peer->private_completed_move == (int)sequence && !peer->private_cmd_queue_count &&
		!peer->private_cmd_queue_msec && peer->private_pmove_credit_msec == 0 &&
		player->v.waterlevel == 2 && VectorCompare (origin, player->v.origin) &&
		VectorCompare (velocity, player->v.velocity) && player->v.flags == flags &&
		player->v.health == health);
	puts ("Q30_PUBLICATION_WATER_PASSED actual begin/received head/native-completed WALK/full-send/cached-water/next-native parity; prepared late relocation and bounded checkpoint");
	Mem_Free (saved_peer);
	Mem_Free (globals);
	Mem_Free (vars);
	for (int slot = 0; slot < 2; ++slot)
	{
		Mem_Free (states[slot]->entities);
		Mem_Free (states[slot]->scores);
		Mem_Free (states[slot]);
	}
}

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
	if (COM_CheckParm ("-sharedqcboundary"))
	{
		/* Older AD QC on explicitly selected real shoreline geometry. e1m1
		 * supplies no dry neighbor to the bounded horizontal sample finder. */
		assert (!SV_PrivateWalkTrialStockProgram () && !SV_PrivateWalkTrialQ30Program () &&
			peers[0]->private_pmove_walk_selected);
		SharedQCDeferredRoomScale (peers[0], states[0]);
		puts ("SHARED_QC_BOUNDARY_PASSED prepared real-BSP shoreline with generic QC; actual admission/input/world/send/parser");
		return 0;
	}
	if (COM_CheckParm ("-q30boots") || COM_CheckParm ("-genericboots"))
	{
		Q30Boots (peers, states, COM_CheckParm ("-vr") != 0);
		return 0;
	}
	if (COM_CheckParm ("-q30publication"))
	{
		Q30Publication (peers, states);
		return 0;
	}
	const qboolean selected = COM_CheckParm ("-defaultselection") || COM_CheckParm ("-selected");
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
			if (selected) LiquidDisposablePreview (states[0], false);
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
