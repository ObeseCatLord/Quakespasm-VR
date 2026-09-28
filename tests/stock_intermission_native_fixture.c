/* Actual pinned stock exit/finale QC and frozen native frames through the
 * admitted command owner. Reuse captured delivery/prepared signon/resources;
 * relocation and finale activation are explicit component seams. */
#define STOCK_LIQUID_FIXTURE_ENTRY ImportedStockIntermissionLiquidMain
#include "stock_liquid_native_fixture.c"

static client_t *intermission_peer;
static int intermission_pre, intermission_post, intermission_freeze;
static float intermission_pre_time, intermission_post_time;
static func_t intermission_execute, intermission_finale;
static const char *intermission_phase;
static edict_t *intermission_exit;
static func_t intermission_due_function;
static qboolean intermission_phase_done;
static vec3_t intermission_frozen_origin;
static int intermission_reconnect_broadcasts;
static func_t intermission_axe_sound;
static int intermission_axe_cues;

/* Synthetic qsocket driver metadata is not a live channel. Capture the ordinary
 * reliable reconnect broadcast, like the imported unreliable send boundary; no
 * packet delivery, ACK simulation or alternate transport is provided. */
int __wrap_NET_SendToAll (sizebuf_t *message, double blocktime)
{
	assert (blocktime == 5 && message->cursize == 12 && message->data[0] == svc_stufftext &&
		!strcmp ((const char *)message->data + 1, "reconnect\n"));
	intermission_reconnect_broadcasts++;
	return 0;
}

static float StockGlobal (const char *name)
{
	ddef_t *global = ED_FindGlobal (name);
	assert (global && (global->type & ~DEF_SAVEGLOBAL) == ev_float);
	return G_FLOAT (global->ofs);
}

static void ObserveIntermissionQC (func_t function)
{
	if (function == intermission_execute || function == intermission_finale)
		intermission_freeze++;
	if (pr_global_struct->self != EDICT_TO_PROG (intermission_peer->edict)) return;
	if (function == intermission_axe_sound) intermission_axe_cues++;
	if (function == pr_global_struct->PlayerPreThink)
	{
		intermission_pre++; intermission_pre_time += pr_global_struct->frametime;
	}
	if (function == pr_global_struct->PlayerPostThink)
	{
		intermission_post++; intermission_post_time += pr_global_struct->frametime;
	}
	if (!intermission_phase || !intermission_exit || intermission_phase_done) return;
	qboolean activate = !strcmp (intermission_phase, "pre") ? function == pr_global_struct->PlayerPreThink :
		!strcmp (intermission_phase, "post") ? function == pr_global_struct->PlayerPostThink :
		function == intermission_due_function;
	if (!activate) return;
	/* Prepared callback composition after actual player QC/installed due
	 * Think. The actual stock exit callback freezes all players; restore only
	 * caller context, never its gameplay writes. */
	intermission_phase_done = true;
	int saved_self = pr_global_struct->self, saved_other = pr_global_struct->other;
	liquid_after_qc_composition = NULL;
	pr_global_struct->self = EDICT_TO_PROG (intermission_exit);
	PR_ExecuteProgram (intermission_execute);
	intermission_freeze++;
	pr_global_struct->self = saved_self; pr_global_struct->other = saved_other;
	VectorCopy (intermission_peer->edict->v.origin, intermission_frozen_origin);
	liquid_after_qc_composition = ObserveIntermissionQC;
}

static void ParseIntermissionReliable (client_t *peer, client_state_t *state)
{
	assert (peer->message.cursize);
	cl = *state; cls.netcon = peer->netconnection;
	net_message = peer->message;
	CL_ParseServerMessage ();
	assert (msg_readcount == net_message.cursize && cl.intermission);
	*state = cl;
	SZ_Clear (&peer->message);
}

static void AxeContactSend (client_t *peer, client_state_t *state, int sample)
{
	usercmd_t command = {0};
	cl = *state; cls.netcon = peer->netconnection; cl.time = qcvm->time;
	assert (!cl.intermission); // delayed commands before reliable notification
	command.servertime = cl.time;
	VectorCopy (cl.viewangles, command.viewangles);
	command.vr_active = command.vr_handpos_relative = true;
	VectorSet (command.vr_handpos, 8, 0, 22);
	if (sample < 0)
		command.impulse = 1; // actual stock weapon selection, not injected identity
	else
	{
		command.vr_contact.flags = VR_WEAPON_CONTACT_RIGHT_VALID | VR_WEAPON_CONTACT_IMMERSIVE_MELEE;
		command.vr_contact.weapon = peer->edict->v.weapon;
		command.vr_contact.modelindex = SV_ModelIndex (PR_GetString (peer->edict->v.weaponmodel));
		VectorSet (command.vr_contact.grip[1], 8, 0, 22);
		VectorSet (command.vr_contact.base[1], sample ? 12 : 8, -2, 22);
		VectorSet (command.vr_contact.tip[1], sample ? 12 : 8, 2, 22);
		command.vr_contact.speed[1] = sample == 2 ? 0 : 2;
	}
	captured_length = 0; CL_SendMove (&command); *state = cl;
	assert (captured_length); GapDeliver (peer, captured, captured_length);
	if (sample >= 0)
	{
		const usercmd_t *decoded = peer->private_cmd_queue_count ?
			&peer->private_cmd_queue[(peer->private_cmd_queue_head + peer->private_cmd_queue_count - 1) %
				countof (peer->private_cmd_queue)] : &peer->cmd;
		assert (decoded->vr_contact.flags == command.vr_contact.flags &&
			decoded->vr_contact.weapon == IT_AXE && decoded->vr_contact.modelindex == command.vr_contact.modelindex);
		qboolean frozen = StockGlobal ("intermission_running") > 0;
		assert (SV_VRContactSampleValid (peer, peer->edict, decoded) ==
			!(frozen && peer->private_pmove_walk_selected));
	}
}

static void LiveAxeControl (client_t *peer, client_state_t *state)
{
	Cvar_SetQuick (&sv_immersive_melee, "1");
	const sv_vr_stock_axe_descriptor_t *descriptor = SV_VRStockAxeMeleeDescriptor ();
	assert (descriptor); intermission_axe_sound = descriptor->sound_index;
	realtime += host_frametime; AxeContactSend (peer, state, -1);
	GapWorldFrame (); GapSnapshot (peer, state);
	assert (peer->edict->v.weapon == IT_AXE && !strcmp (PR_GetString (peer->edict->v.weaponmodel), "progs/v_axe.mdl"));
	for (int frame = 0; frame < 24; ++frame)
	{
		realtime += host_frametime; LiquidSend (peer, state, 0, 0, 0, false, 0);
		GapWorldFrame (); GapSnapshot (peer, state);
	}
	int cues = intermission_axe_cues;
	eval_t *cooldown = GetEdictFieldValue (peer->edict, ED_FindFieldOffset ("attack_finished"));
	assert (cooldown && cooldown->_float <= qcvm->time);
	for (int sample = 0; sample < 3; ++sample)
	{
		realtime += host_frametime; AxeContactSend (peer, state, sample);
		GapWorldFrame (); GapSnapshot (peer, state);
	}
	assert (intermission_axe_cues == cues + 1 && cooldown->_float > qcvm->time);
	for (int frame = 0; frame < 24; ++frame)
	{
		realtime += host_frametime; LiquidSend (peer, state, 0, 0, 0, false, 0);
		GapWorldFrame (); GapSnapshot (peer, state);
	}
	assert (cooldown->_float <= qcvm->time);
	puts ("STOCK_LIVE_AXE_CONTROL_PASSED actual impulse/encoded contacts/QC whiff and cooldown");
}

static void FrozenFrame (client_t *peer, client_state_t *state, int commands, int axe_sample)
{
	vec3_t origin; VectorCopy (peer->edict->v.origin, origin);
	int previous_ack = peer->private_completed_move;
	qboolean pending = peer->private_cmd_queue_count != 0;
	int before_pre = intermission_pre, before_post = intermission_post;
	float before_pre_time = intermission_pre_time, before_post_time = intermission_post_time;
	for (int command = 0; command < commands; ++command)
	{
		realtime += host_frametime;
		if (axe_sample >= 0) AxeContactSend (peer, state, axe_sample);
		else LiquidSend (peer, state, 0, 100, 100, true, 1);
	}
	GapWorldFrame (); GapSnapshot (peer, state);
	assert (peer->active && peer->edict->v.health > 0 &&
		peer->edict->v.movetype == MOVETYPE_NONE && peer->edict->v.solid == SOLID_NOT &&
		VectorCompare (peer->edict->v.origin, origin) && !peer->private_cmd_queue_count &&
		!state->move_ack_prediction_allowed && !state->vr_gorilla_state_valid);
	assert (intermission_pre - before_pre == 1 && intermission_post - before_post == 1 &&
		fabsf (intermission_pre_time - before_pre_time - host_frametime) < .00002f &&
		fabsf (intermission_post_time - before_post_time - host_frametime) < .00002f);
	if (peer->private_pmove_walk_selected)
	{
		assert (state->move_ack_selected_owner && state->move_ack_authority == MOVE_AUTHORITY_LEGACY_FRAME);
		assert (state->ackedmovemessages == peer->private_completed_move &&
			(commands || pending ? peer->private_completed_move == state->movemessages - 1 :
			 peer->private_completed_move == previous_ack));
	}
	cl = *state; vec3_t replay;
	assert (!CL_ReplayPlayerMovement (&cl.entities[cl.viewentity], replay));
	*state = cl;
}

int main (int argc, char **argv)
{
	setvbuf (stdout, NULL, _IONBF, 0);
	client_t *peers[2]; client_state_t *states[2];
	qboolean finale_case = false;
	for (int arg = 1; arg < argc; ++arg)
		if (!strcmp (argv[arg], "-finale")) finale_case = true;
	StartLiquidPeers (argc, argv, finale_case ? "end" : "e1m1", 25, peers, states);
	/* The component bootstrap deliberately prepares signon/map directly.
	 * Discard its unexecuted quake.rc/map-start queue before testing the real
	 * server's later NextLevel command buffer; do not add a test transport. */
	Cbuf_Init ();
	intermission_peer = peers[0];
	dfunction_t *execute = ED_FindFunction ("execute_changelevel");
	dfunction_t *finale = ED_FindFunction ("finale_1");
	assert (execute && finale && StockGlobal ("intermission_running") == 0);
	intermission_execute = execute - qcvm->functions;
	intermission_finale = finale - qcvm->functions;
	liquid_after_qc_composition = ObserveIntermissionQC;
	int phase_arg = COM_CheckParm ("-freezephase");
	if (phase_arg)
	{
		assert (phase_arg + 1 < com_argc && !finale_case);
		intermission_phase = com_argv[phase_arg + 1];
		assert (!strcmp (intermission_phase, "pre") || !strcmp (intermission_phase, "think") ||
			!strcmp (intermission_phase, "post"));
	}
	if (COM_CheckParm ("-delayedcontacts")) LiveAxeControl (peers[0], states[0]);
	if (peers[0]->private_pmove_walk_selected)
	{
		entvars_t saved = peers[0]->edict->v;
		peers[0]->edict->v.movetype = MOVETYPE_NONE;
		peers[0]->edict->v.solid = SOLID_NOT;
		assert (SV_PrivateWalkTrialClassifyState (peers[0]) == SV_PRIVATE_MOVE_REJECTED);
		peers[0]->edict->v = saved; // predicate-only unrecognized NONE negative
	}
	for (int slot = 0; slot < 2; ++slot) SZ_Clear (&peers[slot]->message);
	if (finale_case)
	{
		/* Actual end-map boss/train context with prepared death-callback activation;
		 * not natural boss combat or rendering its scene. */
		edict_t *callback = NULL;
		for (int number = svs.maxclients + 1; number < qcvm->num_edicts; ++number)
		{
			edict_t *ent = EDICT_NUM (number);
			if (!ent->free && !strcmp (PR_GetString (ent->v.classname), "monster_oldone"))
				{ callback = ent; break; }
		}
		assert (callback);
		ddef_t *die = ED_FindField ("th_die");
		assert (die && (die->type & ~DEF_SAVEGLOBAL) == ev_function);
		func_t installed = GetEdictFieldValue (callback, die->ofs)->function;
		assert (installed == intermission_finale);
		pr_global_struct->self = EDICT_TO_PROG (callback);
		pr_global_struct->other = EDICT_TO_PROG (peers[0]->edict);
		pr_global_struct->time = qcvm->time;
		PR_ExecuteProgram (installed);
	}
	else
	{
		edict_t *exit = NULL;
		ddef_t *map = ED_FindField ("map");
		assert (map && (map->type & ~DEF_SAVEGLOBAL) == ev_string);
		for (int number = svs.maxclients + 1; number < qcvm->num_edicts; ++number)
		{
			edict_t *ent = EDICT_NUM (number);
			if (!ent->free && !strcmp (PR_GetString (ent->v.classname), "trigger_changelevel") &&
				!strcmp (PR_GetString (GetEdictFieldValue (ent, map->ofs)->string), "e1m2"))
				{ exit = ent; break; }
		}
		assert (exit && exit->v.solid == SOLID_TRIGGER);
		vec3_t position;
		for (int axis = 0; axis < 3; ++axis)
			position[axis] = (exit->v.absmin[axis] + exit->v.absmax[axis]) * .5f;
		char relocate[128];
		q_snprintf (relocate, sizeof relocate, "setpos %g %g %g 0 0 0",
			position[0], position[1], position[2]);
		host_client = peers[0]; sv_player = peers[0]->edict;
		Cmd_ExecuteString (relocate, src_client);
		Cmd_ExecuteString ("noclip 0", src_client);
		SV_LinkEdict (peers[0]->edict, true); // actual geometry-trigger dispatch
		assert (exit->v.think == intermission_execute && exit->v.nextthink > 0);
		intermission_exit = exit;
		if (intermission_phase)
			exit->v.nextthink = 0; // prepared composition consumes its installed callback instead
	}
	for (int frame = 0; frame < 12 && !StockGlobal ("intermission_running"); ++frame)
	{
		int before_pre = intermission_pre, before_post = intermission_post;
		int expected_completed = peers[0]->private_completed_move;
		intermission_due_function = peers[0]->edict->v.think;
		int commands = intermission_phase && COM_CheckParm ("-phasequiet") ? 0 :
			intermission_phase && COM_CheckParm ("-phasebatch") ? 3 : 1;
		for (int command = 0; command < commands; ++command)
		{
			realtime += host_frametime;
			LiquidSend (peers[0], states[0], 0, 0, 0, false, 0);
		}
		if (peers[0]->private_cmd_queue_count)
			expected_completed = peers[0]->private_cmd_queue[peers[0]->private_cmd_queue_head].sequence;
		GapWorldFrame ();
		if (StockGlobal ("intermission_running") > 0)
		{
			assert (intermission_pre - before_pre == 1 && intermission_post - before_post == 1);
			if (peers[0]->private_pmove_walk_selected)
				assert (peers[0]->private_completed_move == expected_completed);
		}
	}
	assert (StockGlobal ("intermission_running") > 0 && intermission_freeze == 1 && peers[0]->active);
	if (intermission_phase)
	{
		assert (intermission_phase_done);
		if (peers[0]->private_pmove_walk_selected)
			assert (VectorCompare (peers[0]->edict->v.origin, intermission_frozen_origin));
		printf ("STOCK_FREEZE_PHASE_PASSED phase=%s quiet=%d batch=%d pending=%u pre=%d post=%d actual QC composition/no restarted movement\n",
			intermission_phase, !!COM_CheckParm ("-phasequiet"), !!COM_CheckParm ("-phasebatch"),
			peers[0]->private_cmd_queue_count, intermission_pre, intermission_post);
	}
	if (COM_CheckParm ("-delayedcontacts"))
	{
		eval_t *cooldown = GetEdictFieldValue (peers[0]->edict, ED_FindFieldOffset ("attack_finished"));
		eval_t *hostile = GetEdictFieldValue (peers[0]->edict, ED_FindFieldOffset ("show_hostile"));
		assert (cooldown && hostile && cooldown->_float <= qcvm->time && !states[0]->intermission);
		float previous_cooldown = cooldown->_float, previous_hostile = hostile->_float;
		int cues = intermission_axe_cues;
		for (int sample = 0; sample < 3; ++sample)
		{
			FrozenFrame (peers[0], states[0], 1, sample);
			assert (!peers[0]->private_vr_contact_previous_valid &&
				!peers[0]->private_vr_melee_arc[1] && !peers[0]->private_vr_melee_consumed[1]);
		}
		assert (intermission_axe_cues == cues && cooldown->_float == previous_cooldown && hostile->_float == previous_hostile);
		puts ("STOCK_FROZEN_CONTACTS_PASSED delayed encoded axe contacts before notification/no QC cues or cooldown/complete native retirement");
	}
	SV_SendClientMessages (); // actual MSG_ALL reliable fanout, socket sends captured
	ParseIntermissionReliable (peers[0], states[0]);
	GapSnapshot (peers[0], states[0]);
	for (int frame = 0; frame < 12; ++frame)
		FrozenFrame (peers[0], states[0], frame == 2 ? 3 : frame % 3 ? 1 : 0, -1);
	assert (intermission_freeze == 1);
	if (finale_case)
	{
		puts ("STOCK_FINALE_NATIVE_PASSED actual prepared finale callback/living freeze/native clocks/quiet/batch/no replay");
		return 0;
	}
	int quiet = 0;
	while (qcvm->time <= StockGlobal ("intermission_exittime"))
	{
		assert (++quiet < 300);
		FrozenFrame (peers[0], states[0], 0, -1);
	}
	realtime += host_frametime;
	LiquidSend (peers[0], states[0], BUTTON_ATTACK, 0, 0, false, 0);
	GapWorldFrame (); // actual IntermissionThink/NextLevel queues changelevel
	liquid_after_qc_composition = NULL;
	/* Host_Frame executes console commands outside a QC VM. This combined
	 * server/client fixture must restore that ordinary dedicated host context. */
	int client_state = cls.state;
	cls.state = ca_dedicated;
	PR_SwitchQCVM (NULL);
	Cbuf_Execute (); // actual host command/world reload, not SV_SpawnServer injection
	PR_SwitchQCVM (&sv.qcvm);
	cls.state = client_state;
	assert (sv.active && !strcmp (sv.name, "e1m2") && peers[0]->active && !peers[0]->spawned &&
		!peers[0]->private_pmove_walk_selected && intermission_reconnect_broadcasts == 1);
	/* The existing component boundary prepares client signon resources. The
	 * refreshed production serverinfo and actual spawn/begin still select anew. */
	host_client = peers[0]; sv_player = peers[0]->edict;
	SZ_Clear (&peers[0]->message); SV_SendServerinfo (peers[0]); Header (peers[0], QSVR_PROTOCOL_PINNED);
	Cmd_ExecuteString ("spawn", src_client); Cmd_ExecuteString ("begin", src_client);
	assert (peers[0]->spawned && peers[0]->private_pmove_walk_selected == !!sv_private_pmove_walk.value);
	states[0] = CreateMixedPeerState (peers[0], 0);
	cls.signon = SIGNONS;
	for (int frame = 0; frame < 8; ++frame)
	{
		realtime += host_frametime;
		LiquidSend (peers[0], states[0], 0, 25, 0, false, 0);
		GapWorldFrame (); GapSnapshot (peers[0], states[0]);
	}
	assert (states[0]->move_ack_prediction_allowed == !!sv_private_pmove_walk.value);
	puts ("STOCK_EXIT_NATIVE_PASSED actual exit/Think/freeze/quiet/batch/IntermissionThink/button/changelevel/renewed begin; prepared resources/captured delivery");
	return 0;
}
