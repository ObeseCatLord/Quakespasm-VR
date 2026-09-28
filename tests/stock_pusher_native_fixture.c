/* Real stock lift QC and BSP support through the admitted command owner.
 * Reuse captured delivery/prepared signon/input/start seams; neither mover
 * physics nor private admission/completion/permission is manufactured here. */
#define STOCK_LIQUID_FIXTURE_ENTRY ImportedStockLiquidMain
#include "stock_liquid_native_fixture.c"

static void ReplacePusherModel (edict_t *plat)
{
	dfunction_t *setmodel = ED_FindFunction ("setmodel");
	assert (setmodel);
	G_INT (OFS_PARM0) = EDICT_TO_PROG (plat);
	G_INT (OFS_PARM1) = PR_SetEngineString ("*22");
	/* Match OP_CALL's existing builtin registry dispatch, not bytecode entry. */
	int builtin = -setmodel->first_statement;
	assert (builtin > 0 && builtin < qcvm->numbuiltins && qcvm->builtins[builtin]);
	int saved_argc = qcvm->argc;
	qcvm->argc = 2; qcvm->builtins[builtin]();
	qcvm->argc = saved_argc;
}

static client_t *pending_palm_peer;
static edict_t *pending_palm_brush;
static int pending_palm_callback_calls;
static void ChangePendingPalmSurface (func_t function)
{
	if (!pending_palm_peer || function != pr_global_struct->PlayerPostThink ||
		pr_global_struct->self != EDICT_TO_PROG (pending_palm_peer->edict) ||
		!pmove.gorilla.touching) return;
	assert (!pending_palm_peer->vr_gorilla_state.touching &&
		pmove.gorilla.surface[0] == NUM_FOR_EDICT (pending_palm_brush));
	liquid_after_qc_composition = NULL;
	if (COM_CheckParm ("-pending-retire")) ED_Free (pending_palm_brush);
	else ReplacePusherModel (pending_palm_brush);
	pending_palm_callback_calls++;
}

static edict_t *FindStockPlat (void)
{
	for (int number = svs.maxclients + 1; number < qcvm->num_edicts; ++number)
	{
		edict_t *ent = EDICT_NUM (number);
		if (!ent->free && ent->v.movetype == MOVETYPE_PUSH && ent->v.solid == SOLID_BSP &&
			!strcmp (PR_GetString (ent->v.classname), "func_plat") &&
			!strcmp (PR_GetString (ent->v.model), "*7"))
			return ent;
	}
	assert (!"e1m1 real stock plat *7 missing");
	return NULL;
}

static void FindPlatRiderStart (edict_t *player, edict_t *plat, vec3_t position)
{
	entvars_t saved = player->v;
	qboolean found = false;
	for (int x = 1; x <= 3 && !found; ++x)
		for (int y = 1; y <= 3 && !found; ++y)
		{
			vec3_t end;
			for (int axis = 0; axis < 2; ++axis)
				position[axis] = plat->v.absmin[axis] +
					(plat->v.absmax[axis] - plat->v.absmin[axis]) * (axis ? y : x) / 4;
			position[2] = plat->v.absmax[2] - 1 - player->v.mins[2] + 2;
			VectorCopy (position, end);
			end[2] -= 4;
			trace_t trace = SV_Move (position, player->v.mins, player->v.maxs,
				end, MOVE_NORMAL, player);
			if (trace.startsolid || trace.allsolid || trace.fraction == 1 ||
				trace.ent != plat || trace.plane.normal[2] < .7f)
				continue;
			VectorCopy (trace.endpos, position);
			VectorCopy (position, player->v.origin);
			found = !SV_TestEntityPosition (player);
		}
	player->v = saved;
	assert (found);
}

static void CheckCompletedSnapshot (client_t *peer, client_state_t *state)
{
	assert (peer->active && peer->edict->v.health > 0);
	GapSnapshot (peer, state);
	if (peer->private_pmove_walk_selected)
	{
		assert (state->ackedmovemessages == peer->private_completed_move);
		if (peer->private_pmove_pusher_interaction)
			assert (!state->move_ack_prediction_allowed);
		vec3_t replay;
		cl = *state;
		assert (CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], replay) ==
			state->move_ack_prediction_allowed);
		*state = cl;
	}
}

static qboolean CheckPlatCarry (client_t *peer, edict_t *plat,
	const vec3_t body_before, const vec3_t plat_before)
{
	vec3_t body_delta, plat_delta, applied;
	VectorSubtract (plat->v.origin, plat_before, plat_delta);
	VectorSubtract (peer->edict->v.origin, body_before, body_delta);
	SV_GetAppliedPusherSupportMove (peer->edict, applied);
	if (VectorLength (plat_delta) < .00001f)
		return false;
	assert (SV_GetGroundPusher (peer->edict) == plat);
	for (int axis = 0; axis < 3; ++axis)
	{
		/* A stationary rider has no intentional body travel. Native support
		 * records exact carry; allow only PMove's 1/8 position nudge plus the
		 * hull contact DIST_EPSILON, never a trajectory-sized tolerance. */
		assert (fabsf (applied[axis] - plat_delta[axis]) < .0001f);
		if (fabsf (body_delta[axis] - plat_delta[axis]) >= .125f + DIST_EPSILON + .001f)
			fprintf (stderr, "STOCK_PUSHER_CARRY_MISMATCH body=%g platform=%g axis=%d flags=%g ground=%d\n",
				body_delta[axis], plat_delta[axis], axis, peer->edict->v.flags, peer->edict->v.groundentity);
		assert (fabsf (body_delta[axis] - plat_delta[axis]) < .125f + DIST_EPSILON + .001f);
	}
	if (peer->private_pmove_walk_selected)
		assert (peer->private_pmove_pusher_interaction);
	return true;
}

static void SendPusherPalmSample (client_t *peer, client_state_t *state, unsigned flags,
	const vec3_t point, float stroke)
{
	usercmd_t command = {0};
	cl = *state;
	cls.netcon = peer->netconnection;
	cl.time = qcvm->time;
	command.servertime = cl.time;
	command.vr_active = command.vr_handpos_relative = true;
	command.vr_gorilla.flags = flags;
	command.vr_gorilla.head[2] = 28;
	for (int hand = 0; hand < 2; ++hand)
	{
		command.vr_gorilla.hand[hand][0] = hand ? 8 : -8;
		command.vr_gorilla.hand[hand][2] = -24;
		if (point)
			VectorSubtract (point, peer->edict->v.origin, command.vr_gorilla.hand[hand]);
		command.vr_gorilla.hand[hand][0] -= stroke;
		command.vr_gorilla.velocity[hand][0] = -stroke / host_frametime;
	}
	captured_length = 0;
	CL_SendMove (&command);
	*state = cl;
	assert (captured_length);
	GapDeliver (peer, captured, captured_length);
}

static void SendPusherPalms (client_t *peer, client_state_t *state, unsigned flags)
{
	SendPusherPalmSample (peer, state, flags, NULL, 0);
}

static void FindPalmOnlyStart (edict_t *player, edict_t *plat, vec3_t position, vec3_t palm)
{
	entvars_t saved = player->v;
	qboolean found = false;
	for (int axis = 0; axis < 2 && !found; ++axis)
		for (int side = -1; side <= 1 && !found; side += 2)
			for (int offset = 1; offset < 4 && !found; ++offset)
			{
				vec3_t end, head;
				VectorSet (position, (plat->v.absmin[0] + plat->v.absmax[0]) * .5f,
					(plat->v.absmin[1] + plat->v.absmax[1]) * .5f, plat->v.absmax[2] + 72);
				position[axis] = (side < 0 ? plat->v.absmin[axis] : plat->v.absmax[axis]) + side * 20;
				position[1 - axis] = plat->v.absmin[1 - axis] +
					(plat->v.absmax[1 - axis] - plat->v.absmin[1 - axis]) * offset / 4;
				VectorCopy (position, end); end[2] -= 160;
				trace_t floor = SV_Move (position, player->v.mins, player->v.maxs, end, MOVE_NORMAL, player);
				if (floor.startsolid || floor.allsolid || floor.fraction == 1 ||
					floor.ent != qcvm->edicts || floor.plane.normal[2] < .7f) continue;
				VectorCopy (floor.endpos, position);
				VectorCopy (position, player->v.origin);
				if (SV_TestEntityPosition (player)) continue;
				VectorCopy (position, palm);
				palm[axis] = (side < 0 ? plat->v.absmin[axis] + 8 : plat->v.absmax[axis] - 8);
				palm[2] = plat->v.absmax[2] - 1;
				VectorCopy (position, head); head[2] += 28;
				vec3_t reach; VectorSubtract (palm, head, reach);
				if (VectorLength (reach) > VR_GORILLA_MAX_REACH) continue;
				trace_t path = SV_Move (head, vec3_origin, vec3_origin, palm, MOVE_NORMAL, player);
				found = !path.startsolid && !path.allsolid && (path.fraction == 1 || path.ent == plat);
			}
	player->v = saved;
	assert (found);
}

static void RunPalmOnly (client_t *peer, client_state_t *state, client_t *public_peer,
	edict_t *plat)
{
	vec3_t position, palm;
	FindPalmOnlyStart (peer->edict, plat, position, palm);
	PrepareLiquidCase (peer, state, position, 0);
	assert (SV_GetGroundPusher (peer->edict) != plat);
	const qboolean pending_surface = COM_CheckParm ("-pending-replace") || COM_CheckParm ("-pending-retire");
	unsigned initial_generation = peer->vr_gorilla_reset_generation;
	if (pending_surface)
	{
		pending_palm_peer = peer; pending_palm_brush = plat;
		liquid_after_qc_composition = ChangePendingPalmSurface;
	}
	for (int frame = 0; frame < 3; ++frame)
	{
		realtime += host_frametime;
		SendPusherPalmSample (peer, state, VR_GORILLA_HANDS | (frame == 0 ? VR_GORILLA_RESET : 0), palm, 0);
		GapWorldFrame ();
		CheckCompletedSnapshot (peer, state);
		if (pending_palm_callback_calls)
		{
			assert (pending_palm_callback_calls == 1);
			printf ("STOCK_PENDING_PALM_OBSERVATION initialized=%d touching=%d reset=%u->%u raw_baseline=%d\n",
				peer->vr_gorilla_state.initialized, peer->vr_gorilla_state.touching,
				initial_generation, peer->vr_gorilla_reset_generation, state->vr_gorilla_state_valid);
			assert (!peer->vr_gorilla_state.initialized && peer->vr_gorilla_reset_generation != initial_generation &&
				!state->vr_gorilla_state_valid);
			realtime += host_frametime;
			GapWorldFrame (); CheckCompletedSnapshot (peer, state);
			assert (!state->vr_gorilla_state_valid && state->move_ack_prediction_allowed);
			puts ("STOCK_PENDING_PALM_SURFACE_PASSED actual PostThink/fresh binding/mutation/publication fence");
			return;
		}
	}
	assert (!pending_surface);
	assert (peer->vr_gorilla_state.touching && SV_PrivateMoveHasPusherPalm (peer));
	vec3_t still; VectorCopy (peer->edict->v.origin, still);
	int ack = peer->private_completed_move;
	realtime += host_frametime;
	GapWorldFrame (); // no new raw command or body overlap, stationary brush
	CheckCompletedSnapshot (peer, state);
	assert (!peer->private_pmove_pusher_interaction && !state->move_ack_prediction_allowed &&
		peer->private_completed_move == ack && VectorCompare (still, peer->edict->v.origin) &&
		peer->private_input_phase == PRIVATE_INPUT_RUNNING && !sv.paused &&
		peer->private_pmove_waterjump_secs == 0 && peer->edict->v.teleport_time <= qcvm->time);
	const double original_frame = host_frametime;
	const int batch = original_frame > .05 ? 2 : 3;
	for (int command = 0; command < batch; ++command)
	{
		realtime += original_frame;
		SendPusherPalmSample (peer, state, VR_GORILLA_HANDS, palm, 0);
	}
	host_frametime = original_frame * batch;
	GapWorldFrame (); host_frametime = original_frame;
	CheckCompletedSnapshot (peer, state);
	assert (!peer->private_cmd_queue_count && peer->private_completed_move == state->movemessages - 1 &&
		SV_PrivateMoveHasPusherPalm (peer) && !state->move_ack_prediction_allowed);
	realtime += host_frametime;
	const float requested_stroke = host_frametime * 8; // below launch threshold
	SendPusherPalmSample (peer, state, VR_GORILLA_HANDS, palm, requested_stroke);
	GapWorldFrame ();
	CheckCompletedSnapshot (peer, state);
	float stroke_travel = peer->edict->v.origin[0] - still[0];
	assert (stroke_travel > .00001f && fabsf (stroke_travel - requested_stroke) <
		.125f + DIST_EPSILON && !state->move_ack_prediction_allowed);
	if (COM_CheckParm ("-palms-replace") || COM_CheckParm ("-palms-retire"))
	{
		unsigned generation = peer->vr_gorilla_reset_generation;
		if (COM_CheckParm ("-palms-retire")) ED_Free (plat);
		else
			ReplacePusherModel (plat);
		assert (peer->vr_gorilla_reset_generation != generation && !peer->vr_gorilla_state.initialized);
		realtime += host_frametime;
		GapWorldFrame (); CheckCompletedSnapshot (peer, state);
		assert (!SV_PrivateMoveHasPusherPalm (peer) && !state->vr_gorilla_state_valid && state->move_ack_prediction_allowed);
		printf ("STOCK_PUSHER_SURFACE_LIFETIME_PASSED action=%s actual owner/reset/no stale ACK/replay return\n",
			COM_CheckParm ("-palms-retire") ? "ED_Free" : "QC_setmodel");
		return;
	}
	/* Real public player's placement activates the stock plat trigger. It
	 * does not modify the private player's hand/reset or recovery owner. */
	vec3_t rider; FindPlatRiderStart (public_peer->edict, plat, rider);
	char relocate[128];
	q_snprintf (relocate, sizeof relocate, "setpos %g %g %g 0 0 0", rider[0], rider[1], rider[2]);
	host_client = public_peer; sv_player = public_peer->edict;
	Cmd_ExecuteString (relocate, src_client);
	Cmd_ExecuteString ("noclip 0", src_client);
	VectorClear (public_peer->edict->v.velocity);
	SV_LinkEdict (public_peer->edict, true);
	int moving_bound = 0;
	for (int frame = 0; frame < 5; ++frame)
	{
		if (plat->v.absmax[2] - 1 + plat->v.velocity[2] * host_frametime >= peer->edict->v.origin[2] + 27)
			break; // keep the next actual contact below the generated head's clear reach
		vec3_t before, plat_before;
		VectorCopy (peer->edict->v.origin, before);
		VectorCopy (plat->v.origin, plat_before);
		palm[2] = plat->v.absmax[2] - 1;
		realtime += host_frametime;
		int before_ack = peer->private_completed_move;
		if (frame != 1) SendPusherPalmSample (peer, state, VR_GORILLA_HANDS, palm, 0);
		GapWorldFrame ();
		CheckCompletedSnapshot (peer, state);
		assert (SV_GetGroundPusher (peer->edict) != plat);
		if (peer->vr_gorilla_state.touching && plat->v.origin[2] > plat_before[2]) moving_bound++;
		vec3_t body_delta; VectorSubtract (peer->edict->v.origin, before, body_delta);
		assert (VectorLength (body_delta) < .126f && !state->move_ack_prediction_allowed);
		if (frame == 1)
			assert (!peer->private_pmove_pusher_interaction && peer->private_completed_move == before_ack);
	}
	assert (moving_bound >= 2);
	for (int frame = 0; frame < 3; ++frame)
	{
		realtime += host_frametime;
		SendPusherPalmSample (peer, state, 0, palm, 0);
		GapWorldFrame ();
		CheckCompletedSnapshot (peer, state);
	}
	assert (!SV_PrivateMoveHasPusherPalm (peer) && state->move_ack_prediction_allowed);
	printf ("STOCK_PUSHER_PALM_ONLY_PASSED stationary/moving quiet flag=0 replay=0 batch=%d stroke=%g moving=%d OFF/replay return\n",
		batch, stroke_travel, moving_bound);
}

static void RunPusherPalms (client_t *peer, client_state_t *state, edict_t *plat)
{
	int bound_frames = 0, moving_contacts = 0;
	const int plat_number = NUM_FOR_EDICT (plat);
	for (int frame = 0; frame < 20; ++frame)
	{
		vec3_t body_before, plat_before;
		VectorCopy (peer->edict->v.origin, body_before);
		VectorCopy (plat->v.origin, plat_before);
		const double original_frame = host_frametime;
		const qboolean quiet = frame == 8;
		const int commands = frame == 10 ? 2 : 1;
		int ack = peer->private_completed_move;
		float stroke = frame == 5 ? host_frametime * 8 : 0;
		for (int command = 0; command < commands; ++command)
		{
			realtime += original_frame;
			if (!quiet) SendPusherPalmSample (peer, state, VR_GORILLA_HANDS | (frame == 0 ? VR_GORILLA_RESET : 0), NULL, stroke);
		}
		host_frametime = original_frame * commands;
		GapWorldFrame ();
		host_frametime = original_frame;
		assert (peer->active && !peer->private_cmd_queue_count);
		assert (quiet ? peer->private_completed_move == ack : peer->private_completed_move == state->movemessages - 1);
		body_before[0] += stroke; // explicitly subtract the requested subthreshold hand stroke
		qboolean carried = CheckPlatCarry (peer, plat, body_before, plat_before);
		if (peer->vr_gorilla_state.touching)
		{
			bound_frames++;
			if (carried) moving_contacts++;
			for (int hand = 0; hand < 2; ++hand)
				if (peer->vr_gorilla_state.touching & (1 << hand))
				{
					assert (peer->vr_gorilla_state.surface[hand] == plat_number &&
						peer->vr_gorilla_state.surface_model[hand] == (unsigned)plat->v.modelindex);
					/* PM's saved anchor precedes native world carry. Its local
					 * z plus current brush origin must follow the carried body. */
					float world_z = peer->vr_gorilla_state.anchor[hand][2] + plat->v.origin[2];
					assert (fabsf (world_z - peer->edict->v.origin[2] + 24 - VR_GORILLA_RADIUS) < .125f + DIST_EPSILON + .001f);
				}
		}
		CheckCompletedSnapshot (peer, state);
		assert (!state->move_ack_prediction_allowed);
	}
	assert (bound_frames > 2 && moving_contacts > 2);
	realtime += host_frametime;
	SendPusherPalms (peer, state, 0);
	GapWorldFrame ();
	CheckCompletedSnapshot (peer, state);
	assert (!peer->vr_gorilla_state.touching && !peer->vr_gorilla_state.initialized);
	printf ("STOCK_PUSHER_PALMS_PASSED bindings=%d moving=%d actual raw codec/local anchors/carry/OFF\n",
		bound_frames, moving_contacts);
}

static void RunBlockedPlat (client_t *peer, client_state_t *state, edict_t *plat, qboolean palms)
{
	for (int frame = 0; frame < 4; ++frame)
	{
		realtime += host_frametime;
		if (palms) SendPusherPalms (peer, state, VR_GORILLA_HANDS | (frame == 0 ? VR_GORILLA_RESET : 0));
		else LiquidSend (peer, state, 0, 0, 0, false, 0);
		GapWorldFrame ();
		CheckCompletedSnapshot (peer, state);
	}
	assert (plat->v.velocity[2] > 0 && (!palms || peer->vr_gorilla_state.touching));
	/* Prepared physical ceiling, not mocked collision or a staged callback.
	 * Native push/rollback and pinned plat_crush damage/reversal execute. */
	edict_t *wall = ED_Alloc ();
	wall->v.classname = PR_SetEngineString ("fixture_pusher_obstruction");
	wall->v.movetype = MOVETYPE_NONE;
	wall->v.solid = SOLID_BBOX;
	VectorCopy (peer->edict->v.origin, wall->v.origin);
	wall->v.origin[2] += peer->edict->v.maxs[2] + .5f;
	VectorSet (wall->v.mins, -64, -64, 0);
	VectorSet (wall->v.maxs, 64, 64, 16);
	VectorSubtract (wall->v.maxs, wall->v.mins, wall->v.size); // same contract as QC setsize
	SV_LinkEdict (wall, false);
	vec3_t body_before, plat_before;
	VectorCopy (peer->edict->v.origin, body_before);
	VectorCopy (plat->v.origin, plat_before);
	float health = peer->edict->v.health;
	int ack = peer->private_completed_move;
	realtime += host_frametime;
	if (palms) SendPusherPalms (peer, state, VR_GORILLA_HANDS);
	else LiquidSend (peer, state, 0, 0, 0, false, 0);
	GapWorldFrame ();
	CheckCompletedSnapshot (peer, state);
	printf ("STOCK_BLOCKED_OBSERVATION plat=%g->%g velocity=%g body=%g->%g health=%g->%g\n",
		plat_before[2], plat->v.origin[2], plat->v.velocity[2], body_before[2], peer->edict->v.origin[2],
		health, peer->edict->v.health);
	assert (plat->v.velocity[2] < 0 && VectorCompare (plat_before, plat->v.origin) &&
		peer->edict->v.health == health - 1);
	for (int axis = 0; axis < 3; ++axis)
		assert (fabsf (body_before[axis] - peer->edict->v.origin[axis]) < .125f + DIST_EPSILON + .001f);
	if (peer->private_pmove_walk_selected)
		assert (peer->private_completed_move > ack && !state->move_ack_prediction_allowed);
	if (palms)
	{
		assert (SV_PrivateMoveHasPusherPalm (peer));
		for (int hand = 0; hand < 2; ++hand)
			assert (fabsf (peer->vr_gorilla_state.anchor[hand][2] + plat->v.origin[2] -
				peer->edict->v.origin[2] + 24 - VR_GORILLA_RADIUS) < .125f + DIST_EPSILON + .001f);
	}
	ED_Free (wall);
	vec3_t returned_from; VectorCopy (plat->v.origin, returned_from);
	realtime += host_frametime;
	if (palms) SendPusherPalms (peer, state, VR_GORILLA_HANDS);
	else LiquidSend (peer, state, 0, 0, 0, false, 0);
	GapWorldFrame ();
	CheckCompletedSnapshot (peer, state);
	assert (plat->v.origin[2] < returned_from[2]);
	printf ("STOCK_PUSHER_BLOCKED_PASSED selected=%d palms=%d actual ceiling/QC damage/reversal/rollback anchors\n",
		peer->private_pmove_walk_selected, palms);
}

static void CallStockBrushFunction (edict_t *brush, const char *name)
{
	dfunction_t *function = ED_FindFunction (name);
	assert (function);
	pr_global_struct->self = EDICT_TO_PROG (brush);
	pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	PR_ExecuteProgram (function - qcvm->functions);
}

static void RunPusherPalmRelocation (client_t *peer, client_state_t *state, edict_t *plat)
{
	assert (peer->vr_gorilla_state.touching);
	vec3_t landing, unused_palm;
	FindPalmOnlyStart (peer->edict, plat, landing, unused_palm);
	/* Reuse the liquid-contract composition seam: prepared trigger and real
	 * clear destination, actual pinned teleport_touch during normal movement. */
	edict_t *trigger = ED_Alloc (), *destination = ED_Alloc ();
	dfunction_t *touch = ED_FindFunction ("teleport_touch");
	assert (touch);
	trigger->v.classname = PR_SetEngineString ("trigger_teleport");
	trigger->v.target = PR_SetEngineString ("stock_pusher_destination");
	trigger->v.touch = touch - qcvm->functions;
	trigger->v.solid = SOLID_TRIGGER;
	trigger->v.movetype = MOVETYPE_NONE;
	VectorCopy (peer->edict->v.origin, trigger->v.origin);
	VectorSet (trigger->v.mins, -20, -20, -24);
	VectorSet (trigger->v.maxs, 20, 20, 40);
	VectorSubtract (trigger->v.maxs, trigger->v.mins, trigger->v.size);
	SV_LinkEdict (trigger, false);
	destination->v.classname = PR_SetEngineString ("info_teleport_destination");
	destination->v.targetname = trigger->v.target;
	VectorCopy (landing, destination->v.origin);
	unsigned short epoch = peer->private_move_discontinuity_epoch;
	unsigned generation = peer->vr_gorilla_reset_generation;
	realtime += host_frametime;
	SendPusherPalms (peer, state, VR_GORILLA_HANDS);
	GapWorldFrame (); CheckCompletedSnapshot (peer, state);
	assert (peer->private_move_discontinuity_epoch != epoch &&
		peer->vr_gorilla_reset_generation != generation &&
		!peer->vr_gorilla_state.initialized && !state->vr_gorilla_state_valid &&
		!SV_PrivateMoveHasPusherPalm (peer) && !state->move_ack_prediction_allowed);
	assert (peer->edict->v.fixangle && peer->edict->v.teleport_time > qcvm->time);
	ED_Free (trigger); ED_Free (destination);
	puts ("STOCK_PUSHER_PALM_RELOCATION_PASSED actual teleport callback/reset generation/no stale anchor publication");
}

static void RunDoorPush (client_t *peer, client_state_t *state)
{
	edict_t *door = NULL;
	for (int number = svs.maxclients + 1; number < qcvm->num_edicts; ++number)
	{
		edict_t *ent = EDICT_NUM (number);
		if (!ent->free && !strcmp (PR_GetString (ent->v.classname), "func_door") &&
			!strcmp (PR_GetString (ent->v.model), "*17")) door = ent;
	}
	assert (door);
	vec3_t return_position, closed;
	VectorCopy (peer->edict->v.origin, return_position);
	eval_t *pos1 = GetEdictFieldValue (door, ED_FindFieldOffset ("pos1"));
	assert (pos1); VectorCopy (pos1->vector, closed);
	/* Prepared activation of the installed QC function, not mover field
	 * writes. This targeted door has no proximity trigger that reopens it. */
	CallStockBrushFunction (door, "door_go_up");
	for (int frame = 0; frame < 200 && VectorLength (door->v.velocity); ++frame)
	{
		realtime += host_frametime;
		LiquidSend (peer, state, 0, 0, 0, false, 0);
		GapWorldFrame (); CheckCompletedSnapshot (peer, state);
	}
	assert (VectorLength (door->v.velocity) == 0 && !VectorCompare (closed, door->v.origin));
	vec3_t position;
	qboolean found = false;
	for (int x = 1; x <= 3 && !found; ++x)
		for (int y = 1; y <= 3 && !found; ++y)
		{
			VectorSet (position, closed[0] + door->v.mins[0] + (door->v.maxs[0] - door->v.mins[0]) * x / 4,
				closed[1] + door->v.mins[1] + (door->v.maxs[1] - door->v.mins[1]) * y / 4,
				closed[2] + door->v.mins[2] + 40);
			vec3_t end; VectorCopy (position, end); end[2] -= 64;
			trace_t floor = SV_Move (position, peer->edict->v.mins, peer->edict->v.maxs, end, MOVE_NORMAL, peer->edict);
			if (floor.startsolid || floor.allsolid || floor.fraction == 1 || floor.ent != qcvm->edicts) continue;
			VectorCopy (floor.endpos, position);
			vec3_t open; VectorCopy (door->v.origin, open);
			VectorCopy (closed, door->v.origin); // query original real brush hull at its closed transform
			trace_t closed_touch = SV_ClipMoveToEntity (door, position, peer->edict->v.mins,
				peer->edict->v.maxs, position, CONTENTMASK_ANYSOLID);
			VectorCopy (open, door->v.origin);
			found = closed_touch.startsolid;
		}
	assert (found);
	PrepareLiquidCase (peer, state, position, 0);
	realtime += host_frametime; LiquidSend (peer, state, 0, 0, 0, false, 0);
	GapWorldFrame (); CheckCompletedSnapshot (peer, state);
	CallStockBrushFunction (door, "door_go_down");
	qboolean pushed = false;
	for (int frame = 0; frame < 200 && !pushed; ++frame)
	{
		vec3_t before, brush_before, body_delta, brush_delta, applied;
		VectorCopy (peer->edict->v.origin, before);
		VectorCopy (door->v.origin, brush_before);
		realtime += host_frametime;
		LiquidSend (peer, state, 0, 0, 0, false, 0);
		GapWorldFrame (); CheckCompletedSnapshot (peer, state);
		VectorSubtract (peer->edict->v.origin, before, body_delta);
		VectorSubtract (door->v.origin, brush_before, brush_delta);
		SV_GetAppliedPusherSupportMove (peer->edict, applied);
		if (VectorLength (body_delta) > .01f && VectorLength (brush_delta) > .01f &&
			(!peer->private_pmove_walk_selected || peer->private_pmove_pusher_interaction))
		{
			assert (SV_GetGroundPusher (peer->edict) != door && VectorLength (applied) == 0);
			assert (DotProduct (body_delta, brush_delta) > 0 &&
				VectorLength (body_delta) <= VectorLength (brush_delta) + .125f + DIST_EPSILON + .001f);
			if (peer->private_pmove_walk_selected) assert (!state->move_ack_prediction_allowed);
			pushed = true;
		}
	}
	assert (pushed);
	PrepareLiquidCase (peer, state, return_position, 0);
	realtime += host_frametime; LiquidSend (peer, state, 0, 0, 0, false, 0);
	GapWorldFrame (); CheckCompletedSnapshot (peer, state);
	assert (!peer->private_pmove_walk_selected || state->move_ack_prediction_allowed);
	printf ("STOCK_DOOR_PUSH_PASSED selected=%d real targeted brush/QC activation/non-rider displacement/replay return\n",
		peer->private_pmove_walk_selected);
}

int main (int argc, char **argv)
{
	setvbuf (stdout, NULL, _IONBF, 0);
	client_t *peers[2]; client_state_t *states[2];
	int msec = 25;
	/* COM argv is installed by the bootstrap, so read this bounded fixture
	 * option directly before engine initialization. */
	for (int i = 1; i + 1 < argc; ++i)
		if (!strcmp (argv[i], "-commandmsec")) msec = atoi (argv[i + 1]);
	assert (msec > 0 && msec <= 100);
	StartLiquidPeers (argc, argv, "e1m1", msec, peers, states);
	if (COM_CheckParm ("-door"))
	{
		RunDoorPush (peers[0], states[0]);
		return 0;
	}
	const qboolean vr = COM_CheckParm ("-vr") != 0;
	edict_t *plat = FindStockPlat ();
	vec3_t position, initial_plat;
	FindPlatRiderStart (peers[0]->edict, plat, position);
	VectorCopy (plat->v.origin, initial_plat);
	PrepareLiquidCase (peers[0], states[0], position, 0);
	assert (SV_GetGroundPusher (peers[0]->edict) == plat);
	if (COM_CheckParm ("-palms") || COM_CheckParm ("-palms-only") || COM_CheckParm ("-palms-blocked") ||
		COM_CheckParm ("-palms-replace") || COM_CheckParm ("-palms-retire") ||
		COM_CheckParm ("-pending-replace") || COM_CheckParm ("-pending-retire"))
	{
		/* Dedicated startup deliberately omits CL_Init. Prepare its normal
		 * command registration/reliable storage, then execute actual offers. */
		byte reliable[256];
		Cmd_AddCommand_ServerCommand ("vr_gorilla_protocol", CL_ServerExtension_GorillaProtocol_f);
		cls.message.data = reliable; cls.message.maxsize = sizeof reliable;
		SZ_Clear (&peers[0]->message);
		SZ_Clear (&cls.message);
		/* Exercise the actual permission offer/update owner; bootstrap
		 * previously queued an unconsumed reliable signon offer. */
		Cvar_SetQuick (&sv_gorilla, "0");
		SV_AppendGorillaProtocol (peers[0]);
		Cvar_SetQuick (&sv_gorilla, "1");
		SV_AppendGorillaProtocol (peers[0]);
		cl = *states[0];
		net_message = peers[0]->message;
		CL_ParseServerMessage ();
		*states[0] = cl;
		assert (cl.vr_gorilla_supported && cl.vr_gorilla_allowed && cls.message.cursize);
		GapDeliver (peers[0], cls.message.data, cls.message.cursize);
		SZ_Clear (&cls.message);
		SZ_Clear (&peers[0]->message);
		assert (peers[0]->private_pmove_walk_selected && peers[0]->vr_gorilla_capable);
		if (COM_CheckParm ("-palms-blocked"))
		{
			RunBlockedPlat (peers[0], states[0], plat, true);
			RunPusherPalmRelocation (peers[0], states[0], plat);
		}
		else if (COM_CheckParm ("-palms-only") || COM_CheckParm ("-palms-replace") || COM_CheckParm ("-palms-retire"))
			RunPalmOnly (peers[0], states[0], peers[1], plat);
		else if (COM_CheckParm ("-pending-replace") || COM_CheckParm ("-pending-retire"))
			RunPalmOnly (peers[0], states[0], peers[1], plat);
		else RunPusherPalms (peers[0], states[0], plat);
		return 0;
	}
	if (COM_CheckParm ("-blocked"))
	{
		RunBlockedPlat (peers[0], states[0], plat, false);
		return 0;
	}
	int moving_frames = 0, quiet_carries = 0, batch_carries = 0;
	float rise = 0;
	qboolean stopped_after_motion = false;
	for (int frame = 0; frame < 400 && !stopped_after_motion; ++frame)
	{
		vec3_t body_before, plat_before;
		VectorCopy (peers[0]->edict->v.origin, body_before);
		VectorCopy (plat->v.origin, plat_before);
		const qboolean quiet = moving_frames == 2 && !quiet_carries;
		const int commands = moving_frames == 4 && !batch_carries ? (msec == 100 ? 2 : 3) : 1;
		const double original_frame = host_frametime;
		int previous_ack = peers[0]->private_completed_move;
		for (int command = 0; command < commands; ++command)
		{
			realtime += original_frame;
			if (!quiet) LiquidSend (peers[0], states[0], 0, 0, 0, vr, 0);
		}
		host_frametime = original_frame * commands;
		GapWorldFrame ();
		host_frametime = original_frame;
		qboolean carried = CheckPlatCarry (peers[0], plat, body_before, plat_before);
		if (carried)
		{
			moving_frames++;
			if (quiet) quiet_carries++;
			if (commands > 1) batch_carries++;
		}
		if (peers[0]->private_pmove_walk_selected && quiet)
			assert (peers[0]->private_completed_move == previous_ack);
		if (!quiet && peers[0]->private_pmove_walk_selected)
			assert (!peers[0]->private_cmd_queue_count &&
				peers[0]->private_completed_move == states[0]->movemessages - 1);
		CheckCompletedSnapshot (peers[0], states[0]);
		rise = plat->v.origin[2] - initial_plat[2];
		stopped_after_motion = moving_frames > 5 && !carried && VectorLength (plat->v.velocity) == 0;
	}
	assert (moving_frames > 5 && quiet_carries == 1 && batch_carries == 1 &&
		rise > 16 && stopped_after_motion);
	printf ("STOCK_PUSHER_RIDE_PASSED selected=%d vr=%d msec=%d rise=%g frames=%d quiet=%d batch=%d\n",
		peers[0]->private_pmove_walk_selected, vr, msec, rise, moving_frames, quiet_carries, batch_carries);
	/* Jump uses actual stock PreThink plus the shared solver. The native
	 * pusher record must not carry a player after support is explicitly left. */
	qboolean airborne = false, replay_returned = false;
	for (int frame = 0; frame < 12; ++frame)
	{
		realtime += host_frametime;
		LiquidSend (peers[0], states[0], frame == 0 ? 2 : 0, 0, 0, vr, 0);
		GapWorldFrame ();
		CheckCompletedSnapshot (peers[0], states[0]);
		if (SV_GetGroundPusher (peers[0]->edict) != plat && peers[0]->edict->v.velocity[2] > 0)
			airborne = true;
		if (peers[0]->private_pmove_walk_selected && states[0]->move_ack_prediction_allowed)
			replay_returned = true;
	}
	assert (airborne && (!peers[0]->private_pmove_walk_selected || replay_returned));
	puts ("STOCK_PUSHER_RELEASE_PASSED actual jump/support release/full-snapshot replay return");
	return 0;
}
