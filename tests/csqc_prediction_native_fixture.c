/* Loaded-CSQC witness for CAND-NET-005. The generated program calls #345 and
 * #347 through the actual CSQC builtin table; this is not a renderer claim. */
#ifdef NDEBUG
#error "CSQC prediction fixture requires assertions"
#endif
#define MIXED_NATIVE_FIXTURE_ENTRY CsqcPredictionImportedMixedMain
#include "mixed_native_fixture.c"
#undef MIXED_NATIVE_FIXTURE_ENTRY

static qcvm_t *FixtureSwitchVM (qcvm_t *vm)
{
	qcvm_t *old = qcvm;
	PR_SwitchQCVM (NULL);
	if (vm)
		PR_SwitchQCVM (vm);
	return old;
}

static void FixtureLoadClientProgs (void)
{
	qcvm_t *old_vm = qcvm;
	qmodel_t *world = sv.qcvm.worldmodel;

	FixtureSwitchVM (NULL);
	cl.worldmodel = world;
	cl.model_precache[1] = sv.models[1];
	FixtureSwitchVM (&cl.qcvm);
	assert (PR_LoadProgs ("csprogs.dat", true, PROGHEADER_CRC,
		pr_csqcbuiltins, pr_csqcnumbuiltins));
	assert (!memcmp (sv.qcvm.progssha256, qcvm->progssha256,
		sizeof (sv.qcvm.progssha256)));
	qcvm->worldmodel = world;
	qcvm->max_edicts = q_min ((int)max_edicts.value, MAX_EDICTS);
	assert (qcvm->max_edicts >= MIN_EDICTS);
	qcvm->edicts = Mem_Alloc ((size_t)qcvm->max_edicts * qcvm->edict_size);
	assert (qcvm->edicts);
	qcvm->num_edicts = qcvm->reserved_edicts = 1;
	SV_ClearWorld ();
#if defined(DEBUG) || defined(_DEBUG)
	for (int i = 0; i < qcvm->max_edicts; ++i)
	{
		edict_t *ed = EDICT_NUM_NO_CHECK (i);
		ed->qcvm_owner = qcvm;
		ed->edict_ptr = ed;
		ed->edict_num = i;
	}
#endif
	FixtureSwitchVM (old_vm);
}

static void FixturePrepareClientTransport (client_t *peer)
{
	qsocket_t *socket = NET_NewQSocket ();

	assert (socket);
	cl.protocol_pext1 = peer->protocol_pext1 | PEXT1_CSQC;
	cl.protocol_pext2 = peer->protocol_pext2;
	cl.protocol_qsvr = peer->protocol_qsvr;
	cl.protocolflags = sv.protocolflags;
	cl.viewentity = NUM_FOR_EDICT (peer->edict);
	cl.num_entities = 1;
	cl.max_edicts = MAX_EDICTS;
	cl.entities = Mem_Alloc ((size_t)cl.max_edicts * sizeof (*cl.entities));
	assert (cl.entities);
	cl.ackframes_count = 0;
	cl.net_snapshot_have = false;
	cl.time = 0;
	cl.mtime[0] = cl.mtime[1] = 0;
	cls.netcon = socket;
	cls.state = ca_connected;
	cls.signon = SIGNONS;
	cls.demoplayback = false;
	assert (cl.protocol_qsvr == QSVR_PROTOCOL_PINNED &&
		(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS) &&
		(sv_protocol_pext1 & PEXT1_CSQC));
	assert (SV_CSQCTransportAllowed (peer));
	peer->limit_unreliable = 128;
	peer->limit_entities = q_min (peer->limit_entities, (unsigned int)cl.max_edicts);
}

static ddef_t *PredictionGlobal (const char *name, etype_t type)
{
	ddef_t *def = ED_FindGlobal (name);
	assert (def && (def->type & ~DEF_SAVEGLOBAL) == type);
	return def;
}

static void PredictionSetFloat (const char *name, float value)
{
	G_FLOAT (PredictionGlobal (name, ev_float)->ofs) = value;
}

static float PredictionFloat (const char *name)
{
	return G_FLOAT (PredictionGlobal (name, ev_float)->ofs);
}

static void PredictionCall (const char *name)
{
	ddef_t *def = PredictionGlobal (name, ev_function);
	PR_ExecuteProgram (G_INT (def->ofs));
}

int main (int argc, char **argv)
{
	char offer[1024];
	client_t *peer;
	edict_t *player, *trigger, *solid, *reentrant;
	playermove_t saved_pmove;
	movevars_t saved_movevars;
	dfunction_t *impact_touch, *reentrant_touch;
	int impact_before, reentrant_before;
	float prediction_start_x;

	Fixture_InitNativeEngine (argc, argv, "e1m1", true);
	assert (COM_CheckParm ("-csqc-prediction-native"));
	FixtureLoadClientProgs ();
	ClientOffer (0, false, offer, sizeof (offer));
	peer = SpawnPeer (0, offer, QSVR_PROTOCOL_PINNED);
	FixturePrepareClientTransport (peer);
	FixtureSwitchVM (&cl.qcvm);

	/* A loaded-QC CALL1 #345 must see the exact tagged committed command. */
	cl.movemessages = 5;
	for (unsigned int sequence = 1; sequence < 5; sequence++)
	{
		cl.movecmds[sequence & MOVECMDS_MASK].sequence = sequence;
		cl.movecmds[sequence & MOVECMDS_MASK].servertime = 40.0f + sequence;
		cl.movecmds[sequence & MOVECMDS_MASK].forwardmove = 10.0f * sequence;
	}
	FixtureSwitchVM (NULL);
	FixtureSwitchVM (&cl.qcvm);
	assert (*qcvm->extglobals.clientcommandframe == 5 &&
		*qcvm->extglobals.servercommandframe == cl.ackedmovemessages);
	PredictionSetFloat ("fixture_prediction_request", 3);
	PredictionCall ("fixture_ref_prediction_get");
	assert (PredictionFloat ("fixture_prediction_result") == 1);
	assert (*qcvm->extglobals.input_sequence == 3 &&
		*qcvm->extglobals.input_servertime == 43 &&
		qcvm->extglobals.input_movevalues[0] == 30);
	PredictionSetFloat ("fixture_prediction_request", 0);
	PredictionCall ("fixture_ref_prediction_get");
	assert (PredictionFloat ("fixture_prediction_result") == 0);
	PredictionSetFloat ("fixture_prediction_request", 99);
	PredictionCall ("fixture_ref_prediction_get");
	assert (PredictionFloat ("fixture_prediction_result") == 0);

	/* Sequence equal to the journal upper bound is the non-consuming preview. */
	cl.time = 77;
	cl.pendingcmd.forwardmove = 123;
	cl.pendingcmd.seconds = .125f;
	PredictionSetFloat ("fixture_prediction_request", 5);
	PredictionCall ("fixture_ref_prediction_get");
	assert (PredictionFloat ("fixture_prediction_result") == 1 &&
		*qcvm->extglobals.input_sequence == 5 &&
		*qcvm->extglobals.input_servertime == 77);
	puts ("CSQC_PREDICTION_NATIVE_INPUT_PASSED loaded #345 tagged history, rejection and pending preview");
	/* This fixture exercises public client movevars, not the separate pinned
	 * private replay admission path. */
	cl.protocol_qsvr = 0;

	player = ED_Alloc ();
	trigger = ED_Alloc ();
	solid = ED_Alloc ();
	reentrant = ED_Alloc ();
	assert (!player->free && !trigger->free && !solid->free && !reentrant->free);
	VectorSet (player->v.mins, -16, -16, -24);
	VectorSet (player->v.maxs, 16, 16, 32);
	VectorCopy (peer->edict->v.origin, player->v.origin);
	player->v.solid = SOLID_BBOX;
	player->v.movetype = MOVETYPE_WALK;
	VectorSet (trigger->v.mins, -32, -32, -32);
	VectorSet (trigger->v.maxs, 32, 32, 32);
	VectorCopy (player->v.origin, trigger->v.origin);
	trigger->v.origin[0] += 20;
	trigger->v.solid = SOLID_TRIGGER;
	reentrant_touch = ED_FindFunction ("fixture_prediction_reentrant_touch");
	impact_touch = ED_FindFunction ("fixture_prediction_impact_touch");
	assert (reentrant_touch && impact_touch);
	trigger->v.touch = (func_t)(reentrant_touch - qcvm->functions);
	VectorSet (solid->v.mins, -8, -8, -8);
	VectorSet (solid->v.maxs, 8, 8, 8);
	VectorCopy (player->v.origin, solid->v.origin);
	solid->v.origin[0] += 26;
	solid->v.solid = SOLID_BBOX;
	solid->v.touch = (func_t)(impact_touch - qcvm->functions);
	reentrant->v.solid = SOLID_NOT;
	reentrant->v.movetype = MOVETYPE_NONE;
	ClearLink (&trigger->area);
	ClearLink (&solid->area);
	SV_LinkEdict (trigger, false);
	SV_LinkEdict (solid, false);
	G_INT (PredictionGlobal ("fixture_prediction_entity", ev_entity)->ofs) = EDICT_TO_PROG (player);
	G_INT (PredictionGlobal ("fixture_prediction_reentrant_entity", ev_entity)->ofs) =
		EDICT_TO_PROG (reentrant);
	PredictionSetFloat ("input_sequence", 3);
	PredictionSetFloat ("input_servertime", 43);
	PredictionSetFloat ("input_timelength", .1f);
	qcvm->extglobals.input_movevalues[0] = 200;
	qcvm->extglobals.input_movevalues[1] = 0;
	qcvm->extglobals.input_movevalues[2] = 0;
	VectorClear (qcvm->extglobals.input_angles);
	memset (&pmove, 0x5a, sizeof (pmove));
	memset (&movevars, 0x3c, sizeof (movevars));
	saved_pmove = pmove;
	saved_movevars = movevars;
	impact_before = (int)PredictionFloat ("fixture_prediction_impact_count");
	reentrant_before = (int)PredictionFloat ("fixture_prediction_reentrant_touch_count");
	prediction_start_x = player->v.origin[0];
	PredictionCall ("fixture_ref_prediction_move");
	assert (memcmp (&pmove, &saved_pmove, sizeof (pmove)) == 0 &&
		memcmp (&movevars, &saved_movevars, sizeof (movevars)) == 0);
	assert (isfinite (player->v.origin[0]) &&
		fabsf (player->v.origin[0] - prediction_start_x) > .001f &&
		(int)PredictionFloat ("fixture_prediction_reentrant_touch_count") > reentrant_before &&
		(int)PredictionFloat ("fixture_prediction_impact_count") > impact_before);
	puts ("CSQC_PREDICTION_NATIVE_PMOVE_PASSED loaded #347 movement, restored scratch, reentrant trigger and retained solid impact");
	FixtureSwitchVM (NULL);
	return 0;
}
