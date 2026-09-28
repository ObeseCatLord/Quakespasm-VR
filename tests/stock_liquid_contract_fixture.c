/* Reuse the admitted stock/BSP/QC/codec/snapshot driver, with separate checks
 * for command-time ownership. Captured transport/prepared client/input remain
 * the established fixture boundary; no production hooks or contents stubs. */
#define STOCK_LIQUID_FIXTURE_ENTRY ImportedStockLiquidMain
#include "stock_liquid_native_fixture.c"

int main (int argc, char **argv)
{
	setvbuf (stdout, NULL, _IONBF, 0);
	client_t *peers[2];
	client_state_t *states[2];
	vec3_t ledge;
	float yaw;
	int command_msec = 25;
	for (int arg = 1; arg + 1 < argc; ++arg)
		if (!strcmp (argv[arg], "-fixturemsec")) command_msec = atoi (argv[arg + 1]);
	assert (command_msec >= 1 && command_msec <= 125);
	StartLiquidPeers (argc, argv, "e1m2", command_msec, peers, states);
	assert (peers[0]->private_pmove_walk_selected);
	assert (FindLiquidJumpPosition (peers[0]->edict, CONTENTS_WATER, ledge, &yaw));
	PrepareLiquidCase (peers[0], states[0], ledge, yaw);
	if (COM_CheckParm ("-roundingboundary"))
	{
		qcvm->time = 1.5 + 9 * ldexp (1.0, -26);
		assert ((float)qcvm->time + 2.0f != (float)(qcvm->time + 2.0));
	}
	GapWorldFrame ();
	GapSnapshot (peers[0], states[0]);
	printf ("STOCK_QUIET_NEW_LEDGE velocity_z=%g flags=%g timer=%g deadline=%g time=%g ack=%d\n",
		peers[0]->edict->v.velocity[2], peers[0]->edict->v.flags,
		peers[0]->private_pmove_waterjump_secs, peers[0]->edict->v.teleport_time,
		qcvm->time, peers[0]->private_completed_move);
	if (COM_CheckParm ("-requirecontract"))
		assert (peers[0]->private_pmove_waterjump_secs == 0 &&
			!((int)peers[0]->edict->v.flags & FL_WATERJUMP) &&
			peers[0]->edict->v.teleport_time == 0 &&
			VectorCompare (peers[0]->edict->v.velocity, vec3_origin));
	PrepareLiquidCase (peers[0], states[0], ledge, yaw);
	if (COM_CheckParm ("-roundingboundary"))
	{
		qcvm->time = 3.0 + 9 * ldexp (1.0, -25);
		assert ((float)qcvm->time + 2.0f != (float)(qcvm->time + 2.0));
	}
	realtime += host_frametime;
	LiquidSend (peers[0], states[0], 0, 200, 0, false, 0);
	GapWorldFrame ();
	GapSnapshot (peers[0], states[0]);
	assert (peers[0]->private_pmove_waterjump_secs > 0 &&
		((int)peers[0]->edict->v.flags & FL_WATERJUMP));
	liquid_trace_player = peers[0]->edict;
	liquid_trace_case = "quiet-ledge";
	for (int frame = 0; frame < 3; ++frame)
	{
		vec3_t origin, velocity;
		VectorCopy (peers[0]->edict->v.origin, origin);
		VectorCopy (peers[0]->edict->v.velocity, velocity);
		float timer = peers[0]->private_pmove_waterjump_secs;
		float deadline = peers[0]->edict->v.teleport_time;
		int completed = peers[0]->private_completed_move;
		liquid_trace_frame = frame;
		realtime += host_frametime;
		GapWorldFrame ();
		GapSnapshot (peers[0], states[0]);
		assert (peers[0]->active && peers[0]->private_completed_move == completed &&
			VectorCompare (origin, peers[0]->edict->v.origin) &&
			peers[0]->private_pmove_waterjump_secs == timer &&
			states[0]->statsf[STAT_PRIVATE_WATERJUMP_SECS] == timer);
		printf ("STOCK_QUIET_LEDGE frame=%d velocity_z=%g->%g deadline=%g->%g timer=%g ack=%d\n",
			frame, velocity[2], peers[0]->edict->v.velocity[2], deadline,
			peers[0]->edict->v.teleport_time, timer, completed);
		if (COM_CheckParm ("-requirecontract"))
			assert (VectorCompare (velocity, peers[0]->edict->v.velocity) &&
				deadline == peers[0]->edict->v.teleport_time);
	}
	liquid_trace_player = NULL;
	/* Pinned scheduled Think plus prepared deadline-only/flag-only callback
	 * outputs exercises composition, not a claim that stock Think authors
	 * those fields. Each mode writes exactly one independent field. */
	dfunction_t *stand = ED_FindFunction ("player_stand1");
	assert (stand);
	for (int mode = 1; mode <= 3; ++mode)
	{
		PrepareLiquidCase (peers[0], states[0], ledge, yaw);
		liquid_think_player = peers[0]->edict;
		liquid_think_function = stand - qcvm->functions;
		liquid_think_override = mode;
		liquid_think_override_calls = 0;
		liquid_think_deadline = (float)qcvm->time + .7f;
		VectorSet (liquid_think_velocity, 7, 3, 10);
		peers[0]->edict->v.think = liquid_think_function;
		peers[0]->edict->v.nextthink = qcvm->time;
		int completed = peers[0]->private_completed_move;
		realtime += host_frametime;
		GapWorldFrame ();
		GapSnapshot (peers[0], states[0]);
		assert (liquid_think_override_calls == 1 &&
			peers[0]->private_completed_move == completed);
		if (COM_CheckParm ("-requirecontract"))
			assert (!peers[0]->private_pmove_waterjump_secs &&
				!((int)peers[0]->edict->v.flags & FL_WATERJUMP) &&
				VectorCompare (peers[0]->edict->v.velocity, mode == 3 ? liquid_think_velocity : vec3_origin) &&
				peers[0]->edict->v.teleport_time == (mode == 1 ? liquid_think_deadline : 0));
		printf ("STOCK_QUIET_THINK_COMPOSITION mode=%d velocity_z=%g deadline=%g ack=%d\n",
			mode, peers[0]->edict->v.velocity[2], peers[0]->edict->v.teleport_time, completed);
		liquid_think_override = 0;
		liquid_think_player = NULL;
	}
	/* Command-time counterparts. A callback deadline blocks acquisition; a
	 * flag-only cancellation releases the provisional deadline, leaving the
	 * actual command free to acquire a solver jump. A velocity write survives
	 * the handoff; face open water so an unrelated new ledge impulse cannot
	 * obscure that independent force case. */
	for (int mode = 1; mode <= 3; ++mode)
	{
		PrepareLiquidCase (peers[0], states[0], ledge, mode == 3 ? yaw + 180 : yaw);
		liquid_think_player = peers[0]->edict;
		liquid_think_function = stand - qcvm->functions;
		liquid_think_override = mode;
		liquid_think_override_calls = 0;
		liquid_think_deadline = (float)qcvm->time + .7f;
		VectorSet (liquid_think_velocity, 7, 3, 10);
		peers[0]->edict->v.think = liquid_think_function;
		peers[0]->edict->v.nextthink = qcvm->time;
		int completed = peers[0]->private_completed_move;
		realtime += host_frametime;
		LiquidSend (peers[0], states[0], 0, 0, 0, false, 0);
		GapWorldFrame ();
		GapSnapshot (peers[0], states[0]);
		assert (liquid_think_override_calls == 1 && peers[0]->private_completed_move > completed);
		if (COM_CheckParm ("-requirecontract"))
		{
			if (mode == 1)
				assert (!peers[0]->private_pmove_waterjump_secs &&
					peers[0]->edict->v.teleport_time == liquid_think_deadline);
			if (mode == 2)
				assert (peers[0]->private_pmove_waterjump_secs > 0 &&
					((int)peers[0]->edict->v.flags & FL_WATERJUMP));
			if (mode == 3)
				assert (peers[0]->edict->v.velocity[0] > 0 && peers[0]->edict->v.velocity[1] > 0);
		}
		printf ("STOCK_COMMAND_THINK_COMPOSITION mode=%d velocity=%g,%g,%g deadline=%g timer=%g ack=%d\n",
			mode, peers[0]->edict->v.velocity[0], peers[0]->edict->v.velocity[1],
			peers[0]->edict->v.velocity[2], peers[0]->edict->v.teleport_time,
			peers[0]->private_pmove_waterjump_secs, peers[0]->private_completed_move);
		liquid_think_override = 0;
		liquid_think_player = NULL;
	}
	/* A real pinned teleport_touch callback overlaps an in-flight ledge
	 * command. Its trigger box/destination are prepared in this test world;
	 * actual BSP movement, QC, world trigger dispatch and semantic epoch are
	 * not stubbed. No installed map or production test API is changed. */
	PrepareLiquidCase (peers[0], states[0], ledge, yaw);
	realtime += host_frametime;
	LiquidSend (peers[0], states[0], 0, 200, 0, false, 0);
	GapWorldFrame ();
	GapSnapshot (peers[0], states[0]);
	assert (peers[0]->private_pmove_waterjump_secs > 0);
	edict_t *trigger = ED_Alloc ();
	edict_t *destination = ED_Alloc ();
	dfunction_t *touch = ED_FindFunction ("teleport_touch");
	assert (touch);
	trigger->v.classname = PR_SetEngineString ("trigger_teleport");
	trigger->v.target = PR_SetEngineString ("stock_liquid_contract_destination");
	trigger->v.touch = touch - qcvm->functions;
	trigger->v.solid = SOLID_TRIGGER;
	trigger->v.movetype = MOVETYPE_NONE;
	VectorCopy (peers[0]->edict->v.origin, trigger->v.origin);
	VectorSet (trigger->v.mins, -20, -20, -24);
	VectorSet (trigger->v.maxs, 20, 20, 40);
	SV_LinkEdict (trigger, false);
	destination->v.classname = PR_SetEngineString ("info_teleport_destination");
	destination->v.targetname = trigger->v.target;
	/* Real dry start geometry, offset from the public peer; trace rejects a
	 * blocked prepared destination instead of fabricating a collision hull. */
	qboolean found_destination = false;
	for (int x = -4; x <= 4 && !found_destination; ++x)
		for (int y = -4; y <= 4 && !found_destination; ++y)
		{
			if (x*x + y*y < 4) continue;
			vec3_t position = {peers[1]->edict->v.origin[0] + x * 32,
				peers[1]->edict->v.origin[1] + y * 32, peers[1]->edict->v.origin[2] + 16};
			trace_t floor = SV_Move (position, peers[0]->edict->v.mins,
				peers[0]->edict->v.maxs, (vec3_t){position[0], position[1], position[2] - 256},
				MOVE_NORMAL, peers[0]->edict);
			if (floor.startsolid || floor.allsolid || floor.fraction == 1 ||
				SV_PointContents (floor.endpos) != CONTENTS_EMPTY) continue;
			VectorCopy (floor.endpos, destination->v.origin);
			found_destination = true;
		}
	assert (found_destination);
	unsigned short epoch = peers[0]->private_move_discontinuity_epoch;
	realtime += host_frametime;
	LiquidSend (peers[0], states[0], 0, 200, 0, false, 0);
	GapWorldFrame ();
	GapSnapshot (peers[0], states[0]);
	printf ("STOCK_LEDGE_TELEPORT timer=%g flags=%g deadline=%g time=%g epoch=%u->%u velocity=%.6f,%.6f,%.6f\n",
		peers[0]->private_pmove_waterjump_secs, peers[0]->edict->v.flags,
		peers[0]->edict->v.teleport_time, qcvm->time, epoch,
		peers[0]->private_move_discontinuity_epoch, peers[0]->edict->v.velocity[0],
		peers[0]->edict->v.velocity[1], peers[0]->edict->v.velocity[2]);
	assert (peers[0]->active && peers[0]->edict->v.fixangle &&
		peers[0]->private_move_discontinuity_epoch != epoch &&
		peers[0]->edict->v.teleport_time > qcvm->time);
	if (COM_CheckParm ("-requirecontract"))
		assert (peers[0]->private_pmove_waterjump_secs == 0 &&
			!states[0]->move_ack_prediction_allowed);
	float hold = peers[0]->edict->v.teleport_time;
	for (int frame = 0; frame < 3; ++frame)
	{
		realtime += host_frametime;
		LiquidSend (peers[0], states[0], 0, -100, 0, false, 0);
		GapWorldFrame ();
		GapSnapshot (peers[0], states[0]);
		printf ("STOCK_POST_TELEPORT frame=%d timer=%g flags=%g deadline=%g expected_hold=%g prediction=%d\n",
			frame, peers[0]->private_pmove_waterjump_secs, peers[0]->edict->v.flags,
			peers[0]->edict->v.teleport_time, hold, states[0]->move_ack_prediction_allowed);
		if (COM_CheckParm ("-requirecontract"))
			assert (peers[0]->edict->v.teleport_time == hold &&
				!states[0]->move_ack_prediction_allowed);
	}
	/* A teleporter can land at another wet ledge. Its real authored hold must
	 * survive stock provisional writes and prevent solver reacquisition. */
	vec3_t dry_destination;
	VectorCopy (destination->v.origin, dry_destination);
	trigger->v.solid = SOLID_NOT;
	SV_LinkEdict (trigger, false);
	PrepareLiquidCase (peers[0], states[0], ledge, yaw);
	realtime += host_frametime;
	LiquidSend (peers[0], states[0], 0, 200, 0, false, 0);
	GapWorldFrame ();
	GapSnapshot (peers[0], states[0]);
	assert (peers[0]->private_pmove_waterjump_secs > 0);
	VectorCopy (ledge, destination->v.origin);
	destination->v.angles[YAW] = yaw;
	trigger->v.solid = SOLID_TRIGGER;
	VectorCopy (peers[0]->edict->v.origin, trigger->v.origin);
	SV_LinkEdict (trigger, false);
	epoch = peers[0]->private_move_discontinuity_epoch;
	realtime += host_frametime;
	LiquidSend (peers[0], states[0], 0, 200, 0, false, 0);
	GapWorldFrame ();
	GapSnapshot (peers[0], states[0]);
	assert (peers[0]->private_move_discontinuity_epoch != epoch &&
		peers[0]->edict->v.waterlevel > 0);
	hold = peers[0]->edict->v.teleport_time;
	assert (hold > qcvm->time);
	for (int frame = 0; frame < 3; ++frame)
	{
		realtime += host_frametime;
		LiquidSend (peers[0], states[0], 0, -100, 0, false, 0);
		GapWorldFrame ();
		GapSnapshot (peers[0], states[0]);
		if (COM_CheckParm ("-requirecontract"))
			assert (peers[0]->edict->v.teleport_time == hold &&
				!peers[0]->private_pmove_waterjump_secs &&
				!((int)peers[0]->edict->v.flags & FL_WATERJUMP) &&
				!states[0]->move_ack_prediction_allowed);
		printf ("STOCK_WET_TELEPORT_HOLD frame=%d water=%g timer=%g deadline=%g\n", frame,
			peers[0]->edict->v.waterlevel, peers[0]->private_pmove_waterjump_secs,
			peers[0]->edict->v.teleport_time);
	}
	VectorCopy (dry_destination, destination->v.origin);
	destination->v.angles[YAW] = 0;
	/* The same real callback during maintenance must publish cancellation at
	 * an equal ACK, without a manufactured command or timer decrement. */
	trigger->v.solid = SOLID_NOT;
	SV_LinkEdict (trigger, false);
	PrepareLiquidCase (peers[0], states[0], ledge, yaw);
	realtime += host_frametime;
	LiquidSend (peers[0], states[0], 0, 200, 0, false, 0);
	GapWorldFrame ();
	GapSnapshot (peers[0], states[0]);
	assert (peers[0]->private_pmove_waterjump_secs > 0);
	int completed = peers[0]->private_completed_move;
	trigger->v.solid = SOLID_TRIGGER;
	VectorCopy (peers[0]->edict->v.origin, trigger->v.origin);
	SV_LinkEdict (trigger, false);
	realtime += host_frametime;
	GapWorldFrame ();
	GapSnapshot (peers[0], states[0]);
	assert (peers[0]->private_completed_move == completed &&
		peers[0]->edict->v.teleport_time > qcvm->time);
	if (COM_CheckParm ("-requirecontract"))
		assert (!peers[0]->private_pmove_waterjump_secs &&
			!states[0]->statsf[STAT_PRIVATE_WATERJUMP_SECS] &&
			!((int)peers[0]->edict->v.flags & FL_WATERJUMP));
	printf ("STOCK_QUIET_TELEPORT ack=%d timer=%g deadline=%g prediction=%d\n", completed,
		peers[0]->private_pmove_waterjump_secs, peers[0]->edict->v.teleport_time,
		states[0]->move_ack_prediction_allowed);

	/* Later-world callback composition: dispatch a real identified teleport
	 * after the selected player's pass, with supplied QC time chosen to write
	 * the same numeric deadline. The prepared callback/link context is an
	 * explicit seam; movement/QC/semantic hook/full snapshot are unchanged. */
	trigger->v.solid = SOLID_NOT;
	SV_LinkEdict (trigger, false);
	PrepareLiquidCase (peers[0], states[0], ledge, yaw);
	realtime += host_frametime;
	LiquidSend (peers[0], states[0], 0, 200, 0, false, 0);
	GapWorldFrame ();
	GapSnapshot (peers[0], states[0]);
	assert (peers[0]->private_pmove_waterjump_secs > 0);
	completed = peers[0]->private_completed_move;
	epoch = peers[0]->private_move_discontinuity_epoch;
	VectorCopy (peers[0]->edict->v.origin, trigger->v.origin);
	dfunction_t *null_function = ED_FindFunction ("SUB_Null");
	assert (null_function);
	liquid_late_entity = ED_Alloc ();
	assert (NUM_FOR_EDICT (liquid_late_entity) > svs.maxclients);
	liquid_late_entity->v.movetype = MOVETYPE_NONE;
	liquid_late_entity->v.think = null_function - qcvm->functions;
	liquid_late_entity->v.nextthink = qcvm->time;
	liquid_late_player = peers[0]->edict;
	liquid_late_trigger = trigger;
	liquid_late_deadline = peers[0]->edict->v.teleport_time;
	liquid_late_calls = 0;
	assert ((liquid_late_deadline - .7f) + .7f == liquid_late_deadline);
	realtime += host_frametime;
	GapWorldFrame ();
	GapSnapshot (peers[0], states[0]);
	printf ("STOCK_LATE_TELEPORT_DISPATCH calls=%d ack=%d->%d epoch=%u->%u timer=%g deadline=%g\n",
		liquid_late_calls, completed, peers[0]->private_completed_move, epoch,
		peers[0]->private_move_discontinuity_epoch, peers[0]->private_pmove_waterjump_secs,
		peers[0]->edict->v.teleport_time);
	assert (liquid_late_calls == 1 && peers[0]->private_completed_move == completed &&
		peers[0]->private_move_discontinuity_epoch != epoch);
	if (COM_CheckParm ("-requirecontract"))
		assert (!peers[0]->private_pmove_waterjump_secs &&
			!states[0]->statsf[STAT_PRIVATE_WATERJUMP_SECS] &&
			!((int)peers[0]->edict->v.flags & FL_WATERJUMP) &&
			peers[0]->edict->v.teleport_time == liquid_late_deadline &&
			!states[0]->move_ack_prediction_allowed);
	printf ("STOCK_LATE_TELEPORT_EQUAL_DEADLINE ack=%d timer=%g deadline=%g epoch=%u->%u\n",
		completed, peers[0]->private_pmove_waterjump_secs, peers[0]->edict->v.teleport_time,
		epoch, peers[0]->private_move_discontinuity_epoch);
	liquid_late_trigger = liquid_late_player = NULL;

	/* Explicit no-hold relocation uses the existing command boundary. */
	trigger->v.solid = SOLID_NOT;
	SV_LinkEdict (trigger, false);
	PrepareLiquidCase (peers[0], states[0], ledge, yaw);
	realtime += host_frametime;
	LiquidSend (peers[0], states[0], 0, 200, 0, false, 0);
	GapWorldFrame ();
	GapSnapshot (peers[0], states[0]);
	assert (peers[0]->private_pmove_waterjump_secs > 0);
	completed = peers[0]->private_completed_move;
	char setpos[128];
	q_snprintf (setpos, sizeof (setpos), "setpos %g %g %g",
		destination->v.origin[0], destination->v.origin[1], destination->v.origin[2]);
	host_client = peers[0]; sv_player = peers[0]->edict;
	Cmd_ExecuteString (setpos, src_client);
	GapSnapshot (peers[0], states[0]);
	if (COM_CheckParm ("-requirecontract"))
		assert (peers[0]->private_completed_move == completed &&
			!peers[0]->private_pmove_waterjump_secs &&
			!states[0]->statsf[STAT_PRIVATE_WATERJUMP_SECS] &&
			!peers[0]->edict->v.teleport_time &&
			!((int)peers[0]->edict->v.flags & FL_WATERJUMP));
	printf ("STOCK_NO_HOLD_RELOCATION ack=%d timer=%g deadline=%g\n", completed,
		peers[0]->private_pmove_waterjump_secs, peers[0]->edict->v.teleport_time);
	puts ("STOCK_QUIET_COMPONENT_PASSED actual admitted no-command QC/world/snapshot; velocity/deadline contract only asserted with -requirecontract");
	for (int slot = 0; slot < 2; ++slot)
	{
		Mem_Free (states[slot]->entities);
		Mem_Free (states[slot]->scores);
		Mem_Free (states[slot]);
	}
	return 0;
}
