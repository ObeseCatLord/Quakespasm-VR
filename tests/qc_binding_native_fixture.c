/* Offline loaded-QC builtin binding fixture; see qc-binding-final-2.0-plan.md. */
#ifdef NDEBUG
#error "Loaded QC fixture requires assertions"
#endif
#define main ImportedQCBindingNegotiationMain
#include "negotiation_native_fixture.c"
#undef main

#include <math.h>
#include <stdlib.h>

/* Reflection-only fixture links the actual host owner plus this narrow shim. */
#if defined(QC_REFLECTION_NATIVE_HOST_FIXTURE) || defined(QC_ENTITY_NATIVE_HOST_FIXTURE)
extern void FixtureLoadCSProgsNative (void);
#endif

static void FixtureSwitch (qcvm_t *vm)
{
	PR_SwitchQCVM (NULL);
	PR_SwitchQCVM (vm);
}

static ddef_t *FixtureGlobal (const char *name, etype_t type)
{
	ddef_t *global = ED_FindGlobal (name);
	assert (global && (global->type & ~DEF_SAVEGLOBAL) == type);
	return global;
}

static func_t FixtureRef (const char *name)
{
	ddef_t *global = FixtureGlobal (name, ev_function);
	func_t function = G_INT (global->ofs);
	assert (function && function < (func_t)qcvm->progs->numfunctions);
	return function;
}

static float FixtureFloat (const char *name)
{
	return G_FLOAT (FixtureGlobal (name, ev_float)->ofs);
}

static const char *FixtureString (const char *name)
{
	return G_STRING (FixtureGlobal (name, ev_string)->ofs);
}

static void FixtureSetFloat (const char *name, float value)
{
	G_FLOAT (FixtureGlobal (name, ev_float)->ofs) = value;
}

static void FixtureEmptyString (const char *name)
{
	assert (G_INT (FixtureGlobal (name, ev_string)->ofs) == 0);
}

static void FixtureResourcePrime (const char *stem, const char *seed)
{
	char name[128];
	q_snprintf (name, sizeof (name), "fixture_float_resource_prime_%s_size", stem);
	assert (FixtureFloat (name) == 3);
	q_snprintf (name, sizeof (name), "fixture_string_resource_prime_%s_get", stem);
	assert (!strcmp (FixtureString (name), seed));
}

static dfunction_t *FixtureFunction (const char *ref)
{
	return &qcvm->functions[FixtureRef (ref)];
}

static void FixtureQuery (const char *name, float expected)
{
	assert (FixtureFloat (name) == expected);
}

static void FixtureCheckEnabled (qboolean csqc)
{
	float named, numeric;
	PR_ExecuteProgram (FixtureRef ("fixture_ref_discovery_entry"));
	FixtureQuery ("fixture_float_find_random", 7);
	FixtureQuery ("fixture_float_find_random_mixed", 7);
	FixtureQuery ("fixture_float_find_dprint", 25);
	FixtureQuery ("fixture_float_find_changeyaw", 49);
	FixtureQuery ("fixture_float_find_changeyaw_lower", 49);
	FixtureQuery ("fixture_float_find_cvar_setlong", 72);
	FixtureQuery ("fixture_float_find_finale_finished", 79);
	FixtureQuery ("fixture_float_find_unknown", 0);
	if (csqc)
		FixtureQuery ("fixture_float_find_localsound", 177);
	else
	{
		int localsound = PR_ExtensionBuiltinNumber ("ex_localsound");
		assert (localsound > 0 && localsound != 177);
		FixtureQuery ("fixture_float_find_localsound", localsound);
	}
	FixtureQuery ("fixture_float_check_random", 1);
	FixtureQuery ("fixture_float_check_finale_finished", csqc ? 0 : 1);
	FixtureQuery ("fixture_float_check_unknown", 0);
	FixtureQuery ("fixture_float_check_random_mixed", 0);
	assert (FixtureFunction ("fixture_ref_random_named")->first_statement == -7);
	assert (FixtureFunction ("fixture_ref_random_numeric")->first_statement == -7);
	assert (FixtureFunction ("fixture_ref_dprint_named")->first_statement == -25);
	assert (FixtureFunction ("fixture_ref_finale_finished")->first_statement == -79);
	assert (FixtureFunction ("fixture_ref_unknown")->first_statement == 0);
	assert (FixtureFunction ("fixture_ref_random_mixed")->first_statement == 0);
	if (csqc)
	{
		assert (FixtureFunction ("fixture_ref_dprint_277")->first_statement == -25);
		PR_ExecuteProgram (FixtureRef ("fixture_ref_dprint_277_entry"));
	}
	else
		assert (FixtureFunction ("fixture_ref_dprint_277")->first_statement == -277);

	COM_SeedRand (12345);
	PR_ExecuteProgram (FixtureRef ("fixture_ref_named_random_entry"));
	named = FixtureFloat ("fixture_float_named_random");
	COM_SeedRand (12345);
	PR_ExecuteProgram (FixtureRef ("fixture_ref_numeric_random_entry"));
	numeric = FixtureFloat ("fixture_float_numeric_random");
	assert (named >= 0 && named < 1 && numeric == named);
	assert (FixtureFunction ("fixture_ref_random_body")->first_statement > 0);
	PR_ExecuteProgram (FixtureRef ("fixture_ref_random_body_entry"));
	assert (FixtureFloat ("fixture_float_qc_random_body") == 0.25f);
	PR_ExecuteProgram (FixtureRef ("fixture_ref_named_dprint_entry"));
}

static void FixtureCheckDisabledSSQC (void)
{
	assert (FixtureFunction ("fixture_ref_builtin_find")->first_statement == 0);
	assert (FixtureFunction ("fixture_ref_checkbuiltin")->first_statement == 0);
	assert (FixtureFunction ("fixture_ref_random_named")->first_statement == 0);
	assert (FixtureFunction ("fixture_ref_dprint_named")->first_statement == 0);
	assert (FixtureFunction ("fixture_ref_finale_finished")->first_statement == 0);
	assert (FixtureFunction ("fixture_ref_unknown")->first_statement == 0);
	assert (FixtureFunction ("fixture_ref_random_mixed")->first_statement == 0);
	PR_ExecuteProgram (FixtureRef ("fixture_ref_numeric_random_entry"));
	assert (FixtureFloat ("fixture_float_numeric_random") >= 0 &&
		FixtureFloat ("fixture_float_numeric_random") < 1);
	PR_ExecuteProgram (FixtureRef ("fixture_ref_random_body_entry"));
	assert (FixtureFloat ("fixture_float_qc_random_body") == 0.25f);
}

static void FixtureCallsExec (const char *entry)
{
	char name[96];
	q_snprintf (name, sizeof (name), "fixture_ref_calls_%s_entry", entry);
	PR_ExecuteProgram (FixtureRef (name));
}

static float FixtureCallsFloat (const char *name)
{
	char global[96];
	q_snprintf (global, sizeof (global), "fixture_calls_%s", name);
	return FixtureFloat (global);
}

static void FixtureCallsNamed (void)
{
	dfunction_t *nested = FixtureFunction ("fixture_ref_calls_nested");
	dfunction_t *sum = FixtureFunction ("fixture_ref_calls_sum");
	assert (nested->numparms == 2 && nested->locals == 3 && nested->parm_size[0] == 1 && nested->parm_size[1] == 1);
	assert (sum->numparms == 7 && sum->locals == 8);
	for (int i = 0; i < sum->numparms; i++) assert (sum->parm_size[i] == 1);
	for (int i = 0; i < nested->locals; i++) G_FLOAT (nested->parm_start + i) = 100 + i;
	for (int i = 0; i < sum->locals; i++) G_FLOAT (sum->parm_start + i) = 200 + i;
	FixtureCallsExec ("core");
	assert (FixtureCallsFloat ("fabs") == 2.5f && FixtureCallsFloat ("min") == 3 && qcvm->argc == 3);
	FixtureCallsExec ("shapes");
	assert (!strcmp (FixtureString ("fixture_calls_string_result"), "qc-name") && qcvm->argc == 2);
	float *normalized = G_VECTOR (FixtureGlobal ("fixture_calls_vector_result", ev_vector)->ofs);
	assert (fabsf (normalized[0]) < .000001f && fabsf (normalized[1] - .6f) < .000001f &&
		fabsf (normalized[2] - .8f) < .000001f);
	FixtureCallsExec ("nested");
	assert (FixtureCallsFloat ("nested") == 3 && qcvm->argc == 3);
	assert (FixtureCallsFloat ("nested_seen_0") == 3 && FixtureCallsFloat ("nested_seen_1") == 5);
	for (int i = 0; i < nested->locals; i++) assert (G_FLOAT (nested->parm_start + i) == 100 + i);
	FixtureCallsExec ("sum");
	assert (FixtureCallsFloat ("sum") == 28 && qcvm->argc == 8);
	for (int i = 0; i < 7; i++)
	{
		char seen[64];
		q_snprintf (seen, sizeof (seen), "sum_seen_%d", i);
		assert (FixtureCallsFloat (seen) == i + 1);
	}
	for (int i = 0; i < sum->locals; i++) assert (G_FLOAT (sum->parm_start + i) == 200 + i);
	FixtureCallsExec ("noargs");
	assert (FixtureCallsFloat ("noargs") == 3 && qcvm->argc == 1);
	for (const char *const *entry = (const char *const[]){"noarg", "missing", "zero", NULL}; *entry; entry++)
	{
		char prime[64];
		FixtureCallsExec (*entry);
		q_snprintf (prime, sizeof (prime), "%s_prime", *entry);
		assert (FixtureCallsFloat (prime) == 12 && FixtureCallsFloat (*entry) == 12 &&
			qcvm->argc == (!strcmp (*entry, "noarg") ? 0 : 1));
	}
	FixtureCallsExec ("isfunction");
	assert (FixtureCallsFloat ("exists_nested") && FixtureCallsFloat ("exists_zero") &&
		!FixtureCallsFloat ("exists_missing") && FixtureCallsFloat ("exists_readbyte") && qcvm->argc == 1);
}

static void FixtureCallsRead (qboolean modern, qboolean event)
{
	byte bytes[128];
	sizebuf_t payload = {.data = bytes, .maxsize = sizeof (bytes)};
	sizebuf_t saved_message = net_message;
	int saved_readcount = msg_readcount;
	qboolean saved_badread = msg_badread;
	unsigned saved_flags = cl.protocolflags;
	unsigned saved_pext1 = cl.protocol_pext1, saved_pext2 = cl.protocol_pext2;
	double saved_mtime[2];
	memcpy (saved_mtime, cl.mtime, sizeof (saved_mtime));
	unsigned flags = modern ? PRFL_FLOATCOORD | PRFL_SHORTANGLE : 0;
	if (event) MSG_WriteByte (&payload, svcfte_cgamepacket);
	MSG_WriteByte (&payload, 250); MSG_WriteChar (&payload, -12); MSG_WriteShort (&payload, -1234);
	MSG_WriteLong (&payload, -1234567); MSG_WriteCoord (&payload, modern ? 123.125f : -12.25f, flags);
	MSG_WriteAngle (&payload, modern ? -90 : 45, flags); MSG_WriteString (&payload, "fixture-calls-read");
	MSG_WriteFloat (&payload, -3.25f);
	if (event)
	{
		MSG_WriteByte (&payload, svc_time); MSG_WriteFloat (&payload, modern ? 43.5f : 17.25f);
		MSG_WriteByte (&payload, svc_nop);
	}
	else MSG_WriteByte (&payload, 0x5a);
	cl.protocolflags = flags;
	net_message = payload;
	MSG_BeginReading ();
	if (event)
	{
		cl.protocol_pext1 = PEXT1_CSQC; cl.protocol_pext2 = 0;
		FixtureSwitch (NULL);
		CL_ParseServerMessage ();
		assert (qcvm == NULL && msg_badread && msg_readcount == payload.cursize);
		FixtureSwitch (&cl.qcvm);
		assert (FixtureCallsFloat ("event_count") == 1 && cl.mtime[0] == (modern ? 43.5 : 17.25));
	}
	else FixtureCallsExec ("read");
	assert (FixtureCallsFloat ("read_byte") == 250 && FixtureCallsFloat ("read_char") == -12 &&
		FixtureCallsFloat ("read_short") == -1234 && FixtureCallsFloat ("read_long") == -1234567 &&
		FixtureCallsFloat ("read_coord") == (modern ? 123.125f : -12.25f) &&
		FixtureCallsFloat ("read_angle") == (modern ? -90 : 45) &&
		!strcmp (FixtureString ("fixture_calls_read_string"), "fixture-calls-read") &&
		FixtureCallsFloat ("read_float") == -3.25f);
	if (!event)
	{
		assert (!msg_badread && msg_readcount == payload.cursize - 1);
		assert (MSG_ReadByte () == 0x5a && !msg_badread && msg_readcount == payload.cursize);
	}
	FixtureCallsExec ("read_eof");
	assert (FixtureCallsFloat ("read_eof") == -1 && msg_badread && msg_readcount == payload.cursize);
	net_message = saved_message; msg_readcount = saved_readcount; msg_badread = saved_badread; cl.protocolflags = saved_flags;
	cl.protocol_pext1 = saved_pext1; cl.protocol_pext2 = saved_pext2;
	memcpy (cl.mtime, saved_mtime, sizeof (saved_mtime));
}

static void FixtureCallsCases (void)
{
	dprograms_t *server_program = sv.qcvm.progs;
	FixtureSwitch (&sv.qcvm); FixtureCallsNamed ();
	FixtureSwitch (&cl.qcvm);
	assert (PR_LoadProgs ("fixture-csqc.dat", true, PROGHEADER_CRC, pr_csqcbuiltins, pr_csqcnumbuiltins));
	FixtureCallsNamed (); FixtureCallsRead (false, false); FixtureCallsRead (true, false);
	PR_ClearProgs (&cl.qcvm); assert (sv.qcvm.progs == server_program);
	FixtureSwitch (&sv.qcvm); FixtureCallsNamed ();
	FixtureSwitch (&cl.qcvm);
	assert (PR_LoadProgs ("fixture-csqc.dat", true, PROGHEADER_CRC, pr_csqcbuiltins, pr_csqcnumbuiltins));
	FixtureCallsNamed (); FixtureCallsRead (true, false);
	PR_ClearProgs (&cl.qcvm); assert (sv.qcvm.progs == server_program);
	puts ("QC_BINDING_CALLS_NATIVE_PASSED SSQC/CSQC named calls, locals and native message readers");
}

static void FixtureEventCases (void)
{
	dprograms_t *server_program = sv.qcvm.progs;
	for (int modern = 0; modern < 2; modern++)
	{
		FixtureSwitch (&cl.qcvm);
		assert (PR_LoadProgs ("fixture-csqc.dat", true, PROGHEADER_CRC, pr_csqcbuiltins, pr_csqcnumbuiltins));
		assert (cl.qcvm.extfuncs.CSQC_Parse_Event == FixtureRef ("fixture_ref_calls_event"));
		assert (FixtureCallsFloat ("event_count") == 0);
		FixtureCallsRead (modern, true);
		PR_ClearProgs (&cl.qcvm);
		assert (sv.qcvm.progs == server_program);
	}
	puts ("QC_BINDING_EVENTS_NATIVE_PASSED loaded hook, real parser continuation and CSQC reload");
}

#ifdef QC_REFLECTION_NATIVE_HOST_FIXTURE
static void FixtureReflectExec (const char *entry)
{
	char name[96];
	q_snprintf (name, sizeof (name), "fixture_ref_reflect_%s", entry);
	PR_ExecuteProgram (FixtureRef (name));
}

static int FixtureReflectIndex (const char *name)
{
	for (int i = 0; i < qcvm->progs->numfielddefs; i++)
		if (!strcmp (PR_GetString (qcvm->fielddefs[i].s_name), name)) return i;
	assert (!"Expected loaded field missing");
	return -1;
}

static void FixtureReflectMetadata (qboolean complete)
{
	ddef_t *original = (ddef_t *)((byte *)qcvm->progs + qcvm->progs->ofs_fielddefs);
	assert ((qcvm->fielddefs == original) == complete);
	for (int i = 1; i < qcvm->progs->numfielddefs; i++)
	{
		const char *name = PR_GetString (qcvm->fielddefs[i].s_name);
		int first = FixtureReflectIndex (name);
		G_INT (FixtureGlobal ("fixture_reflect_input_name", ev_string)->ofs) = qcvm->fielddefs[i].s_name;
		FixtureReflectExec ("lookup");
		if (FixtureFloat ("fixture_reflect_prime") != 12 || FixtureFloat ("fixture_reflect_index") != first)
			fprintf (stderr, "Reflection lookup %s: prime %g, index %g, expected %d\n", name,
				FixtureFloat ("fixture_reflect_prime"), FixtureFloat ("fixture_reflect_index"), first);
		assert (FixtureFloat ("fixture_reflect_prime") == 12 && FixtureFloat ("fixture_reflect_index") == first);
		assert (FixtureFloat ("fixture_reflect_count") == qcvm->progs->numfielddefs);
		FixtureSetFloat ("fixture_reflect_input_index", i);
		FixtureReflectExec ("metadata");
		assert (!strcmp (FixtureString ("fixture_reflect_name"), name));
		assert (FixtureFloat ("fixture_reflect_type") == (qcvm->fielddefs[i].type & ~DEF_SAVEGLOBAL));
		assert (G_INT (FixtureGlobal ("fixture_reflect_offset", ev_field)->ofs) == qcvm->fielddefs[i].ofs);
	}
	int component = FixtureReflectIndex ("colormod_x");
	assert ((qcvm->fielddefs[component].type & DEF_SAVEGLOBAL) == (complete ? 0 : DEF_SAVEGLOBAL));
	int first = FixtureReflectIndex ("fixture_reflect_duplicate"), second = -1;
	for (int i = first + 1; i < qcvm->progs->numfielddefs; i++)
		if (!strcmp (PR_GetString (qcvm->fielddefs[i].s_name), "fixture_reflect_duplicate")) { second = i; break; }
	assert (second > first && qcvm->fielddefs[first].ofs != qcvm->fielddefs[second].ofs);
	G_INT (FixtureGlobal ("fixture_reflect_input_name", ev_string)->ofs) = PR_SetEngineString ("fixture_absent_field");
	FixtureReflectExec ("lookup");
	assert (FixtureFloat ("fixture_reflect_prime") == 12 && FixtureFloat ("fixture_reflect_index") == 0);
	FixtureSetFloat ("fixture_reflect_input_index", qcvm->progs->numfielddefs + 7);
	FixtureReflectExec ("metadata");
	assert (FixtureFloat ("fixture_reflect_prime") == 12 && FixtureFloat ("fixture_reflect_type") == ev_void);
	assert (G_INT (FixtureGlobal ("fixture_reflect_name", ev_string)->ofs) == 0);
	assert (G_INT (FixtureGlobal ("fixture_reflect_offset", ev_field)->ofs) == 0);
	assert (!strcmp (FixtureString ("fixture_reflect_string_prime"), "return-prime"));
}

static qboolean FixtureReflectWrite (edict_t *ent, int index, const char *value, string_t alias)
{
	FixtureSetFloat ("fixture_reflect_input_index", index);
	G_INT (FixtureGlobal ("fixture_reflect_input_entity", ev_entity)->ofs) = EDICT_TO_PROG (ent);
	G_INT (FixtureGlobal ("fixture_reflect_input_value", ev_string)->ofs) = alias ? alias : PR_SetEngineString (value);
	FixtureReflectExec ("write");
	assert (FixtureFloat ("fixture_reflect_prime") == 12);
	return FixtureFloat ("fixture_reflect_put") != 0;
}

static link_t *FixtureReflectArea (edict_t *ent)
{
	link_t *owner = NULL;
	for (int n = 0; n < qcvm->numareanodes; n++)
		for (int trigger = 0; trigger < 2; trigger++)
		{
			link_t *head = trigger ? &qcvm->areanodes[n].trigger_edicts : &qcvm->areanodes[n].solid_edicts;
			int steps = 0;
			for (link_t *p = head->next; p != head; p = p->next)
			{
				assert (++steps <= qcvm->num_edicts);
				if (p == &ent->area) { assert (!owner); owner = head; }
			}
		}
	return owner;
}

static void FixtureReflectZone (edict_t *ent, const char *expected)
{
	assert (ent->v.classname < 0);
	size_t id = (size_t)(-1 - ent->v.classname);
	assert (id < qcvm->knownzonesize && (qcvm->knownzone[id >> 3] & (1u << (id & 7))));
	assert (!strcmp (PR_GetString (ent->v.classname), expected));
	FixtureSetFloat ("fixture_reflect_input_index", FixtureReflectIndex ("classname"));
	G_INT (FixtureGlobal ("fixture_reflect_input_entity", ev_entity)->ofs) = EDICT_TO_PROG (ent);
	FixtureReflectExec ("read");
	assert (!strcmp (FixtureString ("fixture_reflect_get"), expected));
}

static edict_t *FixtureReflectBody (qboolean server)
{
	edict_t *ent = ED_Alloc (), *trigger = NULL;
	int axis = qcvm->areanodes[0].axis;
	char origin[128];
	assert (axis >= 0 && axis < 2);
	VectorSet (ent->v.mins, -2, -2, -2); VectorSet (ent->v.maxs, 2, 2, 2);
	ent->v.origin[axis] = qcvm->areanodes[0].dist + 128;
	ent->v.origin[2] = qcvm->worldmodel->maxs[2] + 512;
	ent->v.solid = SOLID_BBOX; ent->v.health = 100;
	if (server) SV_LinkEdict (ent, false);
	link_t *initial = FixtureReflectArea (ent);
	assert ((initial != NULL) == server);
	int classname = FixtureReflectIndex ("classname"), think = FixtureReflectIndex ("think");
	assert (FixtureReflectWrite (ent, classname, "native-zone", 0)); FixtureReflectZone (ent, "native-zone");
	assert (FixtureReflectWrite (ent, classname, NULL, ent->v.classname)); FixtureReflectZone (ent, "native-zone");
	string_t interior = PR_SetEngineString (PR_GetString (ent->v.classname) + 7);
	assert (FixtureReflectWrite (ent, classname, NULL, interior)); FixtureReflectZone (ent, "zone");
	assert (FixtureReflectWrite (ent, classname, "", 0)); FixtureReflectZone (ent, "");
	assert (FixtureReflectWrite (ent, classname, "replacement", 0)); FixtureReflectZone (ent, "replacement");
	FixtureSetFloat ("fixture_reflect_input_index", qcvm->progs->numfielddefs + 7);
	FixtureReflectExec ("read");
	assert (G_INT (FixtureGlobal ("fixture_reflect_get", ev_string)->ofs) == 0);
	assert (!strcmp (FixtureString ("fixture_reflect_string_prime"), "return-prime"));
	vec3_t target; VectorCopy (ent->v.origin, target); target[axis] = qcvm->areanodes[0].dist - 128;
	if (server)
	{
		trigger = ED_Alloc (); trigger->v.solid = SOLID_TRIGGER;
		VectorCopy (target, trigger->v.origin); VectorSet (trigger->v.mins, -16, -16, -16); VectorSet (trigger->v.maxs, 16, 16, 16);
		trigger->v.touch = FixtureRef ("fixture_ref_reflect_touch"); SV_LinkEdict (trigger, false);
	}
	q_snprintf (origin, sizeof (origin), "%g %g %g", target[0], target[1], target[2]);
	assert (FixtureReflectWrite (ent, FixtureReflectIndex ("origin"), origin, 0));
	assert (VectorCompare (ent->v.origin, target));
	link_t *moved = FixtureReflectArea (ent);
	assert (server ? moved && moved != initial : moved == NULL);
	assert (FixtureReflectWrite (ent, FixtureReflectIndex ("mins"), "-4 -5 -6", 0));
	assert (FixtureReflectWrite (ent, FixtureReflectIndex ("maxs"), "4 5 6", 0));
	for (int i = 0; i < 3; i++)
	{
		assert (ent->v.mins[i] == -4 - i && ent->v.maxs[i] == 4 + i);
		assert (ent->v.absmin[i] == (server ? target[i] - 5 - i : 0));
		assert (ent->v.absmax[i] == (server ? target[i] + 5 + i : 0));
	}
	assert (FixtureReflectWrite (ent, FixtureReflectIndex ("solid"), "0", 0));
	assert (!FixtureReflectArea (ent) && !ent->area.prev);
	assert (FixtureReflectWrite (ent, FixtureReflectIndex ("solid"), "2", 0));
	assert ((FixtureReflectArea (ent) != NULL) == server);
	assert (FixtureReflectWrite (ent, think, "fixture_reflect_touch", 0));
	assert (ent->v.think == FixtureRef ("fixture_ref_reflect_touch"));
	func_t saved_think = ent->v.think;
	ent->v.origin[axis] = qcvm->areanodes[0].dist + 96; // Prepared stale spatial state.
	link_t *prev = ent->area.prev, *next = ent->area.next;
	byte before[4096]; size_t bytes = qcvm->progs->entityfields * 4;
	assert (bytes <= sizeof (before)); memcpy (before, &ent->v, bytes);
	assert (!FixtureReflectWrite (ent, qcvm->progs->numfielddefs + 7, "0 0 0", 0));
	assert (!memcmp (before, &ent->v, bytes) && ent->area.prev == prev && ent->area.next == next);
	assert (!FixtureReflectWrite (ent, think, "fixture_absent_function", 0) && ent->v.think == saved_think);
	link_t *failed_parse_owner = FixtureReflectArea (ent);
	assert (server ? failed_parse_owner && failed_parse_owner != moved : !failed_parse_owner);
	assert (ent->v.absmin[axis] == (server ? ent->v.origin[axis] - 5 - axis : 0));
	assert (ent->v.health == 100 && FixtureFloat ("fixture_reflect_touch_count") == 0);
	if (server)
	{
		VectorCopy (ent->v.origin, trigger->v.origin); SV_LinkEdict (trigger, false);
		SV_LinkEdict (ent, true);
		assert (ent->v.health == 99 && FixtureFloat ("fixture_reflect_touch_count") == 1);
	}
	return ent;
}

static void FixtureReflectionCases (void)
{
	qboolean complete = COM_CheckParm ("-reflection-fields-complete") != 0;
	dprograms_t *server_program = sv.qcvm.progs;
	qmodel_t *world = sv.qcvm.worldmodel;
	FixtureSwitch (&sv.qcvm); FixtureReflectMetadata (complete);
	edict_t *server_entity = FixtureReflectBody (true);
	link_t *prev = server_entity->area.prev, *next = server_entity->area.next;
	for (int load = 0; load < 2; load++)
	{
		FixtureSwitch (NULL);
		cl.worldmodel = world; cl.model_precache[1] = world;
		FixtureLoadCSProgsNative ();
		assert (qcvm == NULL && cl.qcvm.progs && cl.qcvm.edicts && cl.qcvm.extfuncs.CSQC_Ent_Update);
		FixtureSwitch (&cl.qcvm);
		FixtureReflectMetadata (complete); FixtureReflectBody (false);
		PR_ClearProgs (&cl.qcvm);
		assert (!cl.qcvm.progs && !cl.qcvm.knownzone && sv.qcvm.progs == server_program);
		FixtureSwitch (&sv.qcvm);
		assert (server_entity->area.prev == prev && server_entity->area.next == next && server_entity->v.health == 99);
		FixtureReflectZone (server_entity, "replacement"); FixtureReflectMetadata (complete);
	}
	PR_ClearProgs (&sv.qcvm);
	assert (!sv.qcvm.progs && !sv.qcvm.knownzone);
	SV_SpawnServer ("e1m1");
	FixtureSwitch (&sv.qcvm);
	assert (sv.active && sv.qcvm.edicts && sv.qcvm.worldmodel);
	FixtureReflectMetadata (complete); FixtureReflectBody (true);
	puts ("QC_BINDING_REFLECTION_NATIVE_PASSED actual field maps, zoned aliases and native relinking");
}
#endif

#ifdef QC_ENTITY_NATIVE_HOST_FIXTURE
static void FixtureEntityInput (const char *name, etype_t type, int value)
{
	G_INT (FixtureGlobal (name, type)->ofs) = value;
}

static int FixtureEntityField (const char *name)
{
	int offset = ED_FindFieldOffset (name);
	assert (offset >= 0);
	return offset;
}

static edict_t *FixtureEntityQuery (const char *entry, edict_t *start, const char *field, const char *text, float match)
{
	char name[96];
	int prime = qcvm->num_edicts - 1;
	while (prime > 0 && EDICT_NUM (prime)->free) prime--;
	assert (prime > 0);
	FixtureSetFloat ("fixture_entity_input_prime_index", prime);
	FixtureEntityInput ("fixture_entity_input_start", ev_entity, EDICT_TO_PROG (start));
	FixtureEntityInput ("fixture_entity_input_field", ev_field, FixtureEntityField (field));
	FixtureEntityInput ("fixture_entity_input_string", ev_string, PR_SetEngineString (text));
	FixtureEntityInput ("fixture_entity_input_chainfield", ev_field, FixtureEntityField ("fixture_entity_chain"));
	FixtureSetFloat ("fixture_entity_input_match", match);
	q_snprintf (name, sizeof (name), "fixture_ref_entity_%s", entry);
	PR_ExecuteProgram (FixtureRef (name));
	assert (G_INT (FixtureGlobal ("fixture_entity_prime", ev_entity)->ofs) == EDICT_TO_PROG (EDICT_NUM (prime)));
	assert (G_INT (FixtureGlobal ("fixture_entity_prime", ev_entity)->ofs) != 0);
	return PROG_TO_EDICT (G_INT (FixtureGlobal ("fixture_entity_result", ev_entity)->ofs));
}

static void FixtureEntityString (edict_t *ent, const char *field, const char *text)
{
	GetEdictFieldValue (ent, FixtureEntityField (field))->string = PR_SetEngineString (text);
}

static void FixtureEntityChain (const char *entry, const char *field, const char *text, float match,
	edict_t *a, edict_t *b, qboolean custom)
{
	int alternate = FixtureEntityField ("fixture_entity_chain");
	for (int i = 0; i < 2; i++)
	{
		edict_t *e = i ? b : a;
		e->v.chain = EDICT_TO_PROG (e);
		GetEdictFieldValue (e, alternate)->edict = EDICT_TO_PROG (e);
	}
	assert (FixtureEntityQuery (entry, qcvm->edicts, field, text, match) == b);
	assert ((custom ? GetEdictFieldValue (b, alternate)->edict : b->v.chain) == EDICT_TO_PROG (a));
	assert ((custom ? GetEdictFieldValue (a, alternate)->edict : a->v.chain) == 0);
	assert ((custom ? b->v.chain : GetEdictFieldValue (b, alternate)->edict) == EDICT_TO_PROG (b));
	assert ((custom ? a->v.chain : GetEdictFieldValue (a, alternate)->edict) == EDICT_TO_PROG (a));
}

static void FixtureEntityBody (qboolean server, qboolean fingerprint)
{
	edict_t *world = qcvm->edicts, *a = ED_Alloc (), *b = ED_Alloc (), *freed = ED_Alloc ();
	if (NUM_FOR_EDICT (a) > NUM_FOR_EDICT (b)) { edict_t *swap = a; a = b; b = swap; }
	edict_t *names[] = {a, b, freed};
	for (int i = 0; i < 3; i++)
	{
		char *text;
		names[i]->v.classname = PR_AllocString (sizeof ("fixture-native-search"), &text);
		memcpy (text, "fixture-native-search", sizeof ("fixture-native-search"));
	}
	assert (a->v.classname != b->v.classname && a->v.classname != freed->v.classname);
	a->v.health = b->v.health = freed->v.health = 8123;
	a->v.flags = 524288; b->v.flags = 524296; freed->v.flags = 524288;
	ED_Free (freed);
	assert (freed->free && freed->v.health == 8123 && freed->v.flags == 524288 &&
		!strcmp (PR_GetString (freed->v.classname), "fixture-native-search"));
	assert (FixtureEntityQuery ("find", world, "classname", "fixture-native-search", 0) == a);
	assert (FixtureEntityQuery ("find", a, "classname", "fixture-native-search", 0) == b);
	assert (FixtureEntityQuery ("find", b, "classname", "fixture-native-search", 0) == world);
	assert (FixtureEntityQuery ("find", world, "classname", "fixture-absent-search", 0) == world);
	const char *iter[] = {"findfloat", "findflags"};
	for (int i = 0; i < 2; i++)
	{
		const char *field = i ? "flags" : "health"; float value = i ? 1572864 : 8123;
		assert (FixtureEntityQuery (iter[i], world, field, "", value) == a);
		assert (FixtureEntityQuery (iter[i], a, field, "", value) == b);
		assert (FixtureEntityQuery (iter[i], b, field, "", value) == world);
		assert (FixtureEntityQuery (iter[i], world, field, "", i ? 0 : 8124) == world);
	}
	const char *chains[] = {"findchain", "findchainfloat", "findchainflags"};
	const char *fields[] = {"classname", "health", "flags"};
	for (int i = 0; i < 3; i++)
	{
		char custom[64]; q_snprintf (custom, sizeof (custom), "%s_custom", chains[i]);
		FixtureEntityChain (chains[i], fields[i], "fixture-native-search", i == 1 ? 8123 : 1572864, a, b, false);
		FixtureEntityChain (custom, fields[i], "fixture-native-search", i == 1 ? 8123 : 1572864, a, b, true);
		assert (FixtureEntityQuery (chains[i], world, fields[i], "fixture-absent-search", i == 1 ? 8124 : 0) == world);
	}
	VectorSet (G_VECTOR (FixtureGlobal ("fixture_entity_input_origin", ev_vector)->ofs), 1048576, 0, 0);
	a->v.solid = b->v.solid = SOLID_BBOX;
	VectorSet (a->v.origin, 1048571, 0, 0); VectorSet (a->v.mins, 0, 0, 0); VectorSet (a->v.maxs, 10, 0, 0);
	VectorSet (b->v.origin, 1048580, 0, 0);
	FixtureSetFloat ("fixture_entity_input_radius", 4);
	assert (FixtureEntityQuery ("findradius", world, "classname", "", 0) == b && b->v.chain == EDICT_TO_PROG (a) && a->v.chain == 0);
	FixtureSetFloat ("fixture_entity_input_radius", 3);
	assert (FixtureEntityQuery ("findradius", world, "classname", "", 0) == a);
	a->v.solid = SOLID_NOT;
	assert (FixtureEntityQuery ("findradius", world, "classname", "", 0) == world);
	if (server)
	{
		assert (svs.maxclients >= 2);
		edict_t *one = EDICT_NUM (1), *two = EDICT_NUM (2);
		qboolean active0 = svs.clients[0].active, active1 = svs.clients[1].active;
		one->free = two->free = false; one->v.classname = two->v.classname = PR_SetEngineString ("fixture-native-client");
		svs.clients[0].active = false; svs.clients[1].active = true;
		assert (FixtureEntityQuery ("find", world, "classname", "fixture-native-client", 0) == (fingerprint ? two : one));
		assert (FixtureEntityQuery ("nextent", world, "classname", "", 0) == (fingerprint ? two : one));
		assert (FixtureEntityQuery ("find", two, "classname", "fixture-native-client", 0) == world);
		one->v.health = two->v.health = 9135; one->v.flags = two->v.flags = 1048576;
		assert (FixtureEntityQuery ("findfloat", world, "health", "", 9135) == one);
		assert (FixtureEntityQuery ("findflags", world, "flags", "", 1048576) == one);
		FixtureEntityChain ("findchain", "classname", "fixture-native-client", 0, one, two, false);
		FixtureEntityChain ("findchainfloat", "health", "", 9135, one, two, false);
		FixtureEntityChain ("findchainflags", "flags", "", 1048576, one, two, false);
		ED_Free (one);
		assert (FixtureEntityQuery ("nextent", world, "classname", "", 0) == two);
		one->free = false;
		svs.clients[0].active = active0; svs.clients[1].active = active1;
	}
	else
		assert (FixtureEntityQuery ("nextent", world, "classname", "", 0) == EDICT_NUM (1));
	int last = qcvm->num_edicts - 1;
	while (last > 0 && EDICT_NUM (last)->free) last--;
	assert (last > 0 && FixtureEntityQuery ("nextent", EDICT_NUM (last), "classname", "", 0) == world);
}

static void FixtureEntityRoundQuery (edict_t *target, const char *field, const char *text, qboolean suppressed)
{
	FixtureEntityString (target, field, text);
	assert (FixtureEntityQuery ("find", qcvm->edicts, field, text, 0) == (suppressed ? qcvm->edicts : target));
}

static edict_t *FixtureEntityRounds (void)
{
	edict_t *target = ED_Alloc (), *monster = ED_Alloc (), *attacker = ED_Alloc ();
	const char *fields[] = {"targetname", "targetname2", "targetname3", "targetname4"};
	const char *absent_counters[] = {"wincnt", "losscnt"}, *opposites[] = {"loss", "win"};
	pr_global_struct->self = 0;
	for (int i = 0; i < 2; i++)
	{
		FixtureEntityRoundQuery (target, "targetname", "rounds", false);
		FixtureEntityString (target, "targetname", "fixture-no-counter");
		assert (FixtureEntityQuery ("find", qcvm->edicts, "targetname", absent_counters[i], 0) == qcvm->edicts);
		FixtureEntityRoundQuery (target, "targetname", opposites[i], true);
	}
	for (int i = 0; i < 4; i++)
	{
		pr_global_struct->self = 0;
		FixtureEntityRoundQuery (target, fields[i], "rounds", false);
		FixtureEntityRoundQuery (target, fields[i], "win", false);
		FixtureEntityRoundQuery (target, fields[i], "loss", false);
		FixtureEntityRoundQuery (target, fields[i], "wincnt", false);
		FixtureEntityRoundQuery (target, fields[i], "wincnt", false);
		FixtureEntityRoundQuery (target, fields[i], "win", false);
		FixtureEntityRoundQuery (target, fields[i], "loss", true);
		FixtureEntityRoundQuery (target, fields[i], "losscnt", true);
		monster->v.classname = PR_SetEngineString ("monster_fixture");
		FixtureEntityString (monster, "target2", "clearer"); FixtureEntityString (monster, "target", "loss");
		pr_global_struct->self = EDICT_TO_PROG (monster);
		FixtureEntityRoundQuery (target, fields[i], "clearer", true);
		FixtureEntityString (monster, "target", "win"); FixtureEntityRoundQuery (target, fields[i], "clearer", false);
		FixtureEntityRoundQuery (target, fields[i], "rounds", false);
		FixtureEntityRoundQuery (target, fields[i], "losscnt", false);
		FixtureEntityRoundQuery (target, fields[i], "losscnt", false);
		FixtureEntityRoundQuery (target, fields[i], "loss", false);
		FixtureEntityRoundQuery (target, fields[i], "win", true);
		FixtureEntityRoundQuery (target, fields[i], "wincnt", true);
		FixtureEntityRoundQuery (target, fields[i], "clearer", true);
	}
	const char *attackers[] = {"trigger_hurt", "trigger_teleport", "teledeath"};
	FixtureEntityRoundQuery (target, "targetname", "rounds", false);
	FixtureEntityString (monster, "target", "win"); monster->v.enemy = EDICT_TO_PROG (attacker);
	for (int i = 0; i < 3; i++)
	{
		attacker->v.classname = PR_SetEngineString (attackers[i]); FixtureEntityString (attacker, "targetname", "hurter");
		FixtureEntityRoundQuery (target, "targetname", "win", true);
		FixtureEntityRoundQuery (target, "targetname", "loss", true);
		FixtureEntityRoundQuery (target, "targetname", "clearer", true);
		FixtureEntityRoundQuery (target, "classname", "win", false);
	}
	attacker->v.classname = PR_SetEngineString ("trigger_hurt"); FixtureEntityString (attacker, "targetname", "other");
	FixtureEntityRoundQuery (target, "targetname", "win", false);
	FixtureEntityString (attacker, "targetname", "hurter"); monster->v.classname = PR_SetEngineString ("item_fixture");
	FixtureEntityRoundQuery (target, "targetname", "win", false);
	monster->v.classname = PR_SetEngineString ("monster_fixture"); FixtureEntityString (monster, "target2", "other");
	FixtureEntityRoundQuery (target, "targetname", "win", false);
	FixtureEntityString (monster, "target2", "clearer"); monster->v.enemy = 0;
	FixtureEntityRoundQuery (target, "targetname", "win", false);
	monster->v.enemy = EDICT_TO_PROG (attacker); FixtureEntityString (monster, "target", "other");
	FixtureEntityRoundQuery (target, "targetname", "win", false);
	FixtureEntityString (monster, "target", "win");
	monster->v.enemy = EDICT_TO_PROG (attacker) + 1;
	FixtureEntityRoundQuery (target, "targetname", "win", false);
	monster->v.enemy = qcvm->num_edicts * qcvm->edict_size;
	FixtureEntityRoundQuery (target, "targetname", "win", false);
	monster->v.enemy = EDICT_TO_PROG (attacker); ED_Free (attacker);
	FixtureEntityRoundQuery (target, "targetname", "win", false);
	pr_global_struct->self = 0;
	FixtureEntityRoundQuery (target, "targetname", "wincnt", false);
	return target;
}

static void FixtureEntityCases (void)
{
	int argument = COM_CheckParm ("-entity-fingerprint"), present = 0;
	assert (argument > 0 && argument + 1 < com_argc);
	const char *variant = com_argv[argument + 1];
	qboolean fingerprint = !strcmp (variant, "complete");
	assert (fingerprint || !strcmp (variant, "missing") || !strcmp (variant, "none"));
	const char *functions[] = {"centerprintlocal", "teleport_check_for_client", "teleport_enter_limbo", "spawn_tpush"};
	for (int i = 0; i < 4; i++) present += ED_FindFunction (functions[i]) != NULL;
	assert (present == (fingerprint ? 4 : !strcmp (variant, "missing") ? 3 : 0));
	assert (fingerprint || !ED_FindFunction ("spawn_tpush"));
	char saved_name[sizeof (sv.name)]; q_strlcpy (saved_name, sv.name, sizeof (saved_name));
	FixtureEntityBody (true, fingerprint);
	q_strlcpy (sv.name, "shubswager", sizeof (sv.name));
	edict_t *target = FixtureEntityRounds ();
	dprograms_t *server_program = sv.qcvm.progs; qmodel_t *world = sv.qcvm.worldmodel;
	for (int load = 0; load < 2; load++)
	{
		FixtureSwitch (NULL); cl.worldmodel = world; cl.model_precache[1] = world;
		FixtureLoadCSProgsNative (); assert (qcvm == NULL && cl.qcvm.progs && cl.qcvm.edicts);
		FixtureSwitch (&cl.qcvm); FixtureEntityBody (false, fingerprint);
		edict_t *client_target = ED_Alloc ();
		FixtureEntityRoundQuery (client_target, "targetname", "losscnt", false);
		FixtureEntityRoundQuery (client_target, "targetname", "win", false);
		PR_ClearProgs (&cl.qcvm); assert (!cl.qcvm.progs && sv.qcvm.progs == server_program);
		FixtureSwitch (&sv.qcvm); FixtureEntityRoundQuery (target, "targetname", "losscnt", true);
	}
	q_strlcpy (sv.name, saved_name, sizeof (sv.name));
	FixtureEntityRoundQuery (target, "targetname", "losscnt", false);
	FixtureEntityRoundQuery (target, "targetname", "wincnt", false);
	SV_SpawnServer ("e1m1"); FixtureSwitch (&sv.qcvm);
	assert (sv.active && sv.qcvm.edicts && sv.qcvm.worldmodel);
	FixtureEntityBody (true, fingerprint);
	puts ("QC_BINDING_ENTITIES_NATIVE_PASSED loaded searches, chains, fingerprint and round predicates");
}
#endif

static void FixtureResourceOwn (qboolean csqc)
{
	const char *seed = csqc ? "CSQC_OWNER" : "SSQC_OWNER";
	const char *invalids[] = {"nan", "infinity", "minus_infinity", "negative", "huge"};
	char name[96];
	float handle;
	PR_ExecuteProgram (FixtureRef (csqc ? "fixture_ref_resource_setup_client" :
		"fixture_ref_resource_setup_server"));
	handle = FixtureFloat ("fixture_resource_handle");
	assert (isfinite (handle) && handle >= 1 && handle == floorf (handle));
	PR_ExecuteProgram (FixtureRef ("fixture_ref_resource_owned"));
	assert (!strcmp (FixtureString ("fixture_string_resource_owned_set"), "set-value"));
	FixtureQuery ("fixture_float_resource_owned_add", 2);
	FixtureQuery ("fixture_float_resource_owned_size", 3);
	FixtureQuery ("fixture_float_resource_copy_size", 3);
	assert (!strcmp (FixtureString ("fixture_string_resource_copy_get"), "set-value"));
	PR_ExecuteProgram (FixtureRef ("fixture_ref_resource_sort"));
	FixtureQuery ("fixture_float_resource_sort_add", 2);
	assert (!strcmp (FixtureString ("fixture_string_resource_sort_middle"), "charlie"));
	assert (!strcmp (FixtureString ("fixture_string_resource_sort_tail"), "delta"));
	FixtureEmptyString ("fixture_string_resource_sort_freed");
	PR_ExecuteProgram (FixtureRef ("fixture_ref_resource_cvarlist"));
	FixtureQuery ("fixture_resource_cvar_populated", 1);
	assert (!strcmp (FixtureString ("fixture_string_resource_cvar_name"), "pr_checkextension"));
	FixtureQuery ("fixture_resource_cvar_empty", 0);
	PR_ExecuteProgram (FixtureRef ("fixture_ref_resource_strings"));
	FixtureQuery ("fixture_float_resource_compare_offset", 0);
	FixtureQuery ("fixture_float_resource_compare_insensitive", 0);
	FixtureQuery ("fixture_float_resource_compare_negative", 0);
	assert (FixtureFloat ("fixture_float_resource_compare_negative_second") < 0);
	assert (!strcmp (FixtureString ("fixture_string_resource_replace_case"), "Ab"));
	assert (!strcmp (FixtureString ("fixture_string_resource_replace_long"), "bbbbbb"));
	assert (!strcmp (FixtureString ("fixture_string_resource_replace_insensitive"), "bbbbbb"));
	assert (!strcmp (FixtureString ("fixture_string_resource_replace_case_insensitive"), "bb"));
	PR_ExecuteProgram (FixtureRef ("fixture_ref_resource_invalid"));
	for (unsigned i = 0; i < sizeof (invalids) / sizeof (invalids[0]); i++)
	{
		q_snprintf (name, sizeof (name), "invalid_%s", invalids[i]);
		FixtureResourcePrime (name, seed);
		q_snprintf (name, sizeof (name), "fixture_float_resource_invalid_%s_size", invalids[i]);
		FixtureQuery (name, 0);
		q_snprintf (name, sizeof (name), "fixture_string_resource_invalid_%s_get", invalids[i]);
		FixtureEmptyString (name);
	}
	FixtureQuery ("fixture_resource_live_size", 3);
	assert (!strcmp (FixtureString ("fixture_resource_live_zero"), seed));
	assert (!strcmp (FixtureString ("fixture_resource_live_one"), "set-value"));
}

static void FixtureResourceForeign (float foreign_handle, qboolean csqc)
{
	const char *seed = csqc ? "CSQC_OWNER" : "SSQC_OWNER";
	FixtureSetFloat ("fixture_resource_foreign_handle", foreign_handle);
	PR_ExecuteProgram (FixtureRef ("fixture_ref_resource_foreign"));
	FixtureResourcePrime ("foreign", seed);
	FixtureQuery ("fixture_resource_foreign_size", 0);
	FixtureEmptyString ("fixture_resource_foreign_get");
	FixtureQuery ("fixture_resource_live_size", 3);
	assert (!strcmp (FixtureString ("fixture_resource_live_zero"), seed));
	assert (!strcmp (FixtureString ("fixture_resource_live_one"), "set-value"));
}

static void FixtureResourceCases (void)
{
	dprograms_t *server_program = sv.qcvm.progs;
	float server_handle, client_handle;
	FixtureSwitch (&sv.qcvm);
	FixtureResourceOwn (false);
	server_handle = FixtureFloat ("fixture_resource_handle");
	FixtureSwitch (&cl.qcvm);
	assert (PR_LoadProgs ("fixture-csqc.dat", true, PROGHEADER_CRC,
		pr_csqcbuiltins, pr_csqcnumbuiltins));
	FixtureResourceOwn (true);
	client_handle = FixtureFloat ("fixture_resource_handle");
	assert (client_handle != server_handle);
	FixtureResourceForeign (server_handle, true);
	FixtureSwitch (&sv.qcvm);
	FixtureResourceForeign (client_handle, false);
	FixtureSwitch (&cl.qcvm);
	PR_ExecuteProgram (FixtureRef ("fixture_ref_resource_assert_owner"));
	FixtureQuery ("fixture_resource_live_size", 3);
	assert (!strcmp (FixtureString ("fixture_resource_live_zero"), "CSQC_OWNER"));
	PR_ClearProgs (&cl.qcvm);
	assert (sv.qcvm.progs == server_program);
	FixtureSwitch (&sv.qcvm);
	PR_ExecuteProgram (FixtureRef ("fixture_ref_resource_assert_owner"));
	FixtureQuery ("fixture_resource_live_size", 3);
	assert (!strcmp (FixtureString ("fixture_resource_live_zero"), "SSQC_OWNER"));
	assert (PR_LoadProgs ("progs.dat", true, PROGHEADER_CRC,
		pr_ssqcbuiltins, pr_ssqcnumbuiltins));
	FixtureSetFloat ("fixture_resource_probe_handle", server_handle);
	PR_ExecuteProgram (FixtureRef ("fixture_ref_resource_probe"));
	FixtureResourcePrime ("probe", "abc");
	FixtureQuery ("fixture_resource_probe_size", 0);
	FixtureEmptyString ("fixture_resource_probe_get");
	puts ("QC_BINDING_RESOURCES_NATIVE_PASSED SSQC/CSQC buffers, invalid/foreign handles, sort/cvar refill and string cases");
}

static void FixtureFilesExec (const char *entry)
{
	char name[128];
	q_snprintf (name, sizeof (name), "fixture_ref_files_%s", entry);
	PR_ExecuteProgram (FixtureRef (name));
}

static float FixtureFilesFloat (const char *name)
{
	char global[128];
	q_snprintf (global, sizeof (global), "fixture_files_%s", name);
	return FixtureFloat (global);
}

static const char *FixtureFilesString (const char *name)
{
	char global[128];
	q_snprintf (global, sizeof (global), "fixture_files_%s", name);
	return FixtureString (global);
}

static int FixtureFilesRawString (const char *name)
{
	char global[128];
	q_snprintf (global, sizeof (global), "fixture_files_%s", name);
	return G_INT (FixtureGlobal (global, ev_string)->ofs);
}

static void FixtureFilesSet (const char *name, float value)
{
	char global[128];
	q_snprintf (global, sizeof (global), "fixture_files_%s", name);
	FixtureSetFloat (global, value);
}

static void FixtureFilesPrime (const char *name)
{
	char global[128];
	q_snprintf (global, sizeof (global), "prime_%s", name);
	assert (FixtureFilesFloat (global) == 12);
}

static void FixtureFilesOutputBytes (qboolean csqc, qboolean after_foreign)
{
	const char *filename = csqc ? "fixture-client-output.txt" : "fixture-server-output.txt";
	const char *expected = csqc ?
		(after_foreign ? "CSQC_OWNER\n\nlast\nowner-after-foreign\n" : "CSQC_OWNER\n\nlast\n") :
		(after_foreign ? "SSQC_OWNER\n\nlast\nowner-after-foreign\n" : "SSQC_OWNER\n\nlast\n");
	char path[MAX_OSPATH], actual[64];
	FILE *file;
	size_t length = strlen (expected);
	q_snprintf (path, sizeof (path), "%s/data/%s", com_gamedir, filename);
	file = fopen (path, "rb");
	assert (file);
	assert (fread (actual, 1, sizeof (actual), file) == length);
	assert (fgetc (file) == EOF);
	assert (!memcmp (actual, expected, length));
	fclose (file);
}

static void FixtureFilesRetired (float buffer, float stream, float search)
{
	FixtureFilesSet ("retired_buffer", buffer);
	FixtureFilesSet ("retired_stream", stream);
	FixtureFilesSet ("retired_search", search);
	FixtureFilesExec ("probe_retired");
	FixtureFilesPrime ("retired_buffer_size");
	FixtureFilesPrime ("retired_search_size");
	assert (FixtureFilesFloat ("retired_buffer_size") == 0 && FixtureFilesFloat ("retired_search_size") == 0);
	assert (FixtureFilesRawString ("retired_buffer_string") == 0 && FixtureFilesRawString ("retired_file_string") == 0);
	assert (FixtureFilesRawString ("retired_search_string") == 0);
	assert (!strcmp (FixtureFilesString ("query_prime"), "return-prime"));
}

static void FixtureFilesSearchOnly (float search)
{
	FixtureFilesSet ("search", search);
	FixtureFilesSet ("query_index", 0);
	FixtureFilesExec ("search_only");
	assert (FixtureFilesFloat ("search_size") == 4 && FixtureFilesRawString ("search_name") != 0);
}

static void FixtureFilesAssertBuffer (qboolean csqc)
{
	const char *seed = csqc ? "CSQC_OWNER" : "SSQC_OWNER";
	assert (FixtureFilesFloat ("size") == 4);
	assert (!strcmp (FixtureFilesString ("buffer_zero"), seed) && FixtureFilesRawString ("buffer_hole") == 0);
	assert (FixtureFilesRawString ("buffer_blank") != 0 && !strcmp (FixtureFilesString ("buffer_blank"), ""));
	assert (!strcmp (FixtureFilesString ("buffer_last"), "last"));
}

static void FixtureFilesSearch (qboolean csqc)
{
	static const char *expected[] = {
		"fixture-search/dup.txt", "fixture-search/pack.txt",
		"fixture-search/nested/packed.txt", "fixture-search/loose.txt"
	};
	unsigned seen = 0;
	assert (isfinite (FixtureFilesFloat ("search")) && FixtureFilesFloat ("search") >= 0 &&
		FixtureFilesFloat ("search") == floorf (FixtureFilesFloat ("search")));
	for (int i = 0; i < 4; i++)
	{
		unsigned j;
		FixtureFilesSet ("query_index", i);
		FixtureFilesExec ("assert_owner");
		assert (FixtureFilesRawString ("search_name") != 0);
		for (j = 0; j < sizeof (expected) / sizeof (expected[0]); j++)
			if (!strcmp (FixtureFilesString ("search_name"), expected[j]))
				break;
		assert (j < sizeof (expected) / sizeof (expected[0]));
		assert (!(seen & (1u << j)));
		seen |= 1u << j;
		assert (FixtureFilesFloat ("search_size") == 4);
		FixtureFilesAssertBuffer (csqc);
	}
	assert (seen == 15);
}

static void FixtureFilesInitial (qboolean csqc)
{
	char name[128];
	FixtureFilesExec (csqc ? "setup_client" : "setup_server");
	assert (FixtureFloat (csqc ? "fixture_files_load_client" : "fixture_files_load_server") == 1);
	assert (FixtureFloat (csqc ? "fixture_files_empty_load_client" : "fixture_files_empty_load_server") == 1);
	assert (FixtureFloat (csqc ? "fixture_files_setup_size_client" : "fixture_files_setup_size_server") == 4);
	assert (FixtureFloat (csqc ? "fixture_files_post_free_size_client" : "fixture_files_post_free_size_server") == 4);
	assert (!strcmp (FixtureString (csqc ? "fixture_files_loaded_first_client" : "fixture_files_loaded_first_server"), "data-first"));
	assert (!strcmp (FixtureString (csqc ? "fixture_files_loaded_blank_client" : "fixture_files_loaded_blank_server"), ""));
	assert (!strcmp (FixtureString (csqc ? "fixture_files_loaded_last_client" : "fixture_files_loaded_last_server"), "last"));
	for (const char *const *key = (const char *const[]){"buffer", "stream", "writer", NULL}; *key; key++)
	{
		float value = FixtureFilesFloat (*key);
		assert (isfinite (value) && value >= 1 && value == floorf (value));
	}
	assert (FixtureFilesFloat ("write_result") == 1);
	FixtureFilesOutputBytes (csqc, false); /* Native writer was closed by the loaded QC. */
	FixtureFilesSearch (csqc);
	FixtureFilesExec ("empty_search");
	assert (FixtureFilesFloat ("empty_search") == -1);
	FixtureFilesExec ("invalid_paths");
	for (const char *const *path = (const char *const[]){"parent", "absolute", "colon", "backslash", NULL}; *path; path++)
	{
		q_snprintf (name, sizeof (name), "invalid_open_%s", *path);
		assert (FixtureFilesFloat (name) == -1);
		q_snprintf (name, sizeof (name), "invalid_search_%s", *path);
		assert (FixtureFilesFloat (name) == -1);
	}
	FixtureFilesExec ("closed_query");
	FixtureFilesPrime ("closed_size");
	assert (FixtureFilesFloat ("search_probe") >= 0);
	assert (FixtureFilesFloat ("closed_handle") == 0);
	assert (FixtureFilesRawString ("foreign_search_name") == 0);
	FixtureFilesExec ("invalid_queries");
	assert (FixtureFilesRawString ("query_prime") != 0);
	assert (!strcmp (FixtureFilesString ("file_prime"), "return-prime"));
	for (const char *const *key = (const char *const[]){"nan", "infinity", "minus_infinity", "negative", "huge", NULL}; *key; key++)
	{
		q_snprintf (name, sizeof (name), "invalid_handle_%s", *key);
		assert (FixtureFilesRawString (name) == 0);
		q_snprintf (name, sizeof (name), "invalid_size_%s", *key);
		assert (FixtureFilesFloat (name) == 0);
		FixtureFilesPrime (name);
		q_snprintf (name, sizeof (name), "invalid_index_%s", *key);
		assert (FixtureFilesRawString (name) == 0);
		q_snprintf (name, sizeof (name), "invalid_stream_%s", *key);
		assert (FixtureFilesRawString (name) == 0);
	}
	FixtureFilesExec ("read_first");
	assert (FixtureFilesRawString ("line_first") != 0 && !strcmp (FixtureFilesString ("line_first"), "data-first"));
	FixtureFilesExec ("read_rest");
	assert (FixtureFilesRawString ("line_blank") != 0 && !strcmp (FixtureFilesString ("line_blank"), ""));
	assert (!strcmp (FixtureFilesString ("line_last"), "last"));
	assert (FixtureFilesRawString ("line_eof") == 0);
	FixtureFilesExec ("reopen_stream");
	FixtureFilesExec (csqc ? "reopen_writer_client" : "reopen_writer_server");
	assert (isfinite (FixtureFilesFloat ("writer")) && FixtureFilesFloat ("writer") >= 1 &&
		FixtureFilesFloat ("writer") == floorf (FixtureFilesFloat ("writer")));
}

static void FixtureFilesForeign (float buffer, float stream, float writer, float search)
{
	FixtureFilesSet ("foreign_buffer", buffer);
	FixtureFilesSet ("foreign_stream", stream);
	FixtureFilesSet ("foreign_writer", writer);
	FixtureFilesSet ("foreign_search", search);
	FixtureFilesExec ("foreign");
	FixtureFilesPrime ("foreign_write_file");
	FixtureFilesPrime ("foreign_write_buffer");
	FixtureFilesPrime ("foreign_load");
	FixtureFilesPrime ("foreign_size");
	assert (!strcmp (FixtureFilesString ("file_prime"), "return-prime"));
	assert (FixtureFilesRawString ("foreign_line") == 0);
	assert (FixtureFilesFloat ("foreign_write_file_result") == 0);
	assert (FixtureFilesFloat ("foreign_write_buffer_result") == 0);
	assert (FixtureFilesFloat ("foreign_load_result") == 0);
	assert (FixtureFilesRawString ("foreign_search_name") == 0);
	assert (FixtureFilesFloat ("foreign_search_size") == 0);
	assert (FixtureFilesRawString ("query_prime") != 0);
}

static void FixtureFilesAfterForeign (qboolean csqc)
{
	FixtureFilesExec ("read_after_foreign");
	assert (FixtureFilesRawString ("line_first") != 0 && !strcmp (FixtureFilesString ("line_first"), "data-first"));
	FixtureFilesExec ("read_blank");
	assert (FixtureFilesRawString ("line_blank") != 0 && !strcmp (FixtureFilesString ("line_blank"), ""));
	FixtureFilesSearch (csqc);
	FixtureFilesExec ("close_writer");
	FixtureFilesOutputBytes (csqc, true); /* Owner append proves foreign close/write refusal. */
}

static void FixtureFilesCases (void)
{
	dprograms_t *server_program;
	float server_buffer, server_stream, server_writer, server_search;
	float client_buffer, client_stream, client_writer, client_search;
	float slots[15], closed_slot, reused_slot;
	dprograms_t *client_program;
	FixtureSwitch (&sv.qcvm);
	FixtureFilesInitial (false);
	server_program = sv.qcvm.progs;
	server_buffer = FixtureFilesFloat ("buffer");
	server_stream = FixtureFilesFloat ("stream");
	server_writer = FixtureFilesFloat ("writer");
	server_search = FixtureFilesFloat ("search");
	FixtureSwitch (&cl.qcvm);
	assert (PR_LoadProgs ("fixture-csqc.dat", true, PROGHEADER_CRC,
		pr_csqcbuiltins, pr_csqcnumbuiltins));
	FixtureFilesInitial (true);
	client_buffer = FixtureFilesFloat ("buffer");
	client_stream = FixtureFilesFloat ("stream");
	client_writer = FixtureFilesFloat ("writer");
	client_search = FixtureFilesFloat ("search");
	assert (server_search != client_search);
	FixtureFilesForeign (server_buffer, server_stream, server_writer, server_search);
	FixtureSwitch (&sv.qcvm);
	FixtureFilesForeign (client_buffer, client_stream, client_writer, client_search);
	FixtureFilesAfterForeign (false);
	FixtureSwitch (&cl.qcvm);
	FixtureFilesAfterForeign (true);
	FixtureFilesExec ("read_tail");
	assert (!strcmp (FixtureFilesString ("line_last"), "last") && FixtureFilesRawString ("line_eof") == 0);
	FixtureFilesExec ("reopen_stream"); /* Retirement must reject an unread live stream, not merely EOF. */
	client_stream = FixtureFilesFloat ("stream");
	assert (isfinite (client_stream) && client_stream >= 1 && client_stream == floorf (client_stream));
	FixtureFilesExec ("read_first");
	assert (FixtureFilesRawString ("line_first") != 0 && !strcmp (FixtureFilesString ("line_first"), "data-first"));
	FixtureSwitch (&sv.qcvm);
	FixtureFilesSet ("close_handle", server_search);
	FixtureFilesExec ("close_search");
	for (int i = 0; i < 15; i++)
	{
		FixtureFilesExec ("open_extra");
		slots[i] = FixtureFilesFloat ("extra_handle");
		assert (isfinite (slots[i]) && slots[i] >= 0 && slots[i] == floorf (slots[i]));
		assert (slots[i] != client_search);
		for (int j = 0; j < i; j++)
			assert (slots[i] != slots[j]);
	}
	FixtureFilesExec ("open_extra");
	assert (FixtureFilesFloat ("extra_handle") == -1);
	closed_slot = slots[0];
	FixtureFilesSet ("close_handle", closed_slot);
	FixtureFilesExec ("close_search");
	FixtureFilesExec ("open_extra");
	reused_slot = FixtureFilesFloat ("extra_handle");
	assert (reused_slot == closed_slot);
	FixtureSwitch (&cl.qcvm);
	PR_ClearProgs (&cl.qcvm);
	assert (sv.qcvm.progs == server_program);
	assert (PR_LoadProgs ("fixture-csqc.dat", true, PROGHEADER_CRC,
		pr_csqcbuiltins, pr_csqcnumbuiltins));
	FixtureSwitch (&cl.qcvm);
	FixtureFilesRetired (client_buffer, client_stream, client_search);
	FixtureSwitch (&sv.qcvm);
	FixtureFilesSet ("search", reused_slot);
	FixtureFilesSearch (false);
	FixtureFilesExec ("read_tail");
	assert (!strcmp (FixtureFilesString ("line_last"), "last"));
	assert (FixtureFilesRawString ("line_eof") == 0);
	FixtureSwitch (&cl.qcvm);
	FixtureFilesExec ("open_extra");
	assert (FixtureFilesFloat ("extra_handle") == client_search);
	client_search = FixtureFilesFloat ("extra_handle");
	client_program = cl.qcvm.progs;
	FixtureSwitch (&sv.qcvm);
	FixtureFilesExec ("reopen_stream");
	server_stream = FixtureFilesFloat ("stream");
	assert (isfinite (server_stream) && server_stream >= 1 && server_stream == floorf (server_stream));
	FixtureFilesExec ("read_first");
	assert (FixtureFilesRawString ("line_first") != 0 && !strcmp (FixtureFilesString ("line_first"), "data-first"));
	PR_ClearProgs (&sv.qcvm);
	assert (cl.qcvm.progs == client_program);
	assert (PR_LoadProgs ("progs.dat", true, PROGHEADER_CRC,
		pr_ssqcbuiltins, pr_ssqcnumbuiltins));
	FixtureSwitch (&sv.qcvm);
	FixtureFilesRetired (server_buffer, server_stream, reused_slot);
	for (int i = 0; i < 15; i++)
	{
		FixtureFilesExec ("open_extra");
		slots[i] = FixtureFilesFloat ("extra_handle");
		assert (isfinite (slots[i]) && slots[i] >= 0 && slots[i] != client_search);
		for (int j = 0; j < i; j++) assert (slots[i] != slots[j]);
	}
	FixtureFilesExec ("open_extra");
	assert (FixtureFilesFloat ("extra_handle") == -1);
	FixtureSwitch (&cl.qcvm);
	FixtureFilesSearchOnly (client_search);
	puts ("QC_BINDING_FILES_NATIVE_PASSED SSQC/CSQC loaded file, buffer, search ownership and retirement");
}

static void FixtureTokensExec (const char *entry)
{
	char name[96];
	q_snprintf (name, sizeof (name), "fixture_ref_tokens_%s", entry);
	PR_ExecuteProgram (FixtureRef (name));
}

static float FixtureTokensFloat (const char *name)
{
	char global[96];
	q_snprintf (global, sizeof (global), "fixture_tokens_%s", name);
	return FixtureFloat (global);
}

static int FixtureTokensRaw (const char *name)
{
	char global[96];
	q_snprintf (global, sizeof (global), "fixture_tokens_%s", name);
	return G_INT (FixtureGlobal (global, ev_string)->ofs);
}

static const char *FixtureTokensString (const char *name)
{
	char global[96];
	q_snprintf (global, sizeof (global), "fixture_tokens_%s", name);
	return FixtureString (global);
}

static void FixtureTokensFinite (void)
{
	static const char *const modes[] = {"basic", "console", NULL};
	for (const char *const *mode = modes; *mode; mode++)
	{
		char key[96];
		if (!strcmp (*mode, "console"))
		{
			FixtureTokensExec ("shared_client"); FixtureTokensExec ("snapshot_client");
			assert (FixtureTokensFloat ("shared_client_count") == 1 &&
				!strcmp (FixtureTokensString ("shared_client_last"), "client"));
		}
		FixtureTokensExec (*mode);
		q_snprintf (key, sizeof (key), "%s_result", *mode);
		assert (FixtureTokensFloat (key) == 3);
		q_snprintf (key, sizeof (key), "%s_argc", *mode);
		assert (FixtureTokensFloat (key) == 3);
		for (int i = 0; i < 3; i++)
		{
			static const char *const labels[] = {"zero", "one", "two"};
			static const float starts[] = {2, 6, 18}, ends[] = {5, 17, 22};
			q_snprintf (key, sizeof (key), "%s_argv_%s", *mode, labels[i]);
			assert (!strcmp (FixtureTokensString (key), i == 0 ? "one" : i == 1 ? "two three" : "four"));
			q_snprintf (key, sizeof (key), "%s_start_%s", *mode, labels[i]);
			assert (FixtureTokensFloat (key) == starts[i]);
			q_snprintf (key, sizeof (key), "%s_end_%s", *mode, labels[i]);
			assert (FixtureTokensFloat (key) == ends[i]);
		}
		q_snprintf (key, sizeof (key), "%s_argv_last", *mode);
		assert (!strcmp (FixtureTokensString (key), "four"));
	}
	for (const char *const *mode = (const char *const[]) {"comma", "multi", NULL}; *mode; mode++)
	{
		char key[96];
		q_snprintf (key, sizeof (key), "%s", *mode);
		FixtureTokensExec (key);
		q_snprintf (key, sizeof (key), "%s_result", *mode);
		assert (FixtureTokensFloat (key) == 3);
		q_snprintf (key, sizeof (key), "%s_argc", *mode);
		assert (FixtureTokensFloat (key) == 3);
		for (int i = 0; i < 3; i++)
		{
			static const char *const labels[] = {"zero", "one", "two"};
			static const char *const expected[] = {"aa", "bb", "cc"};
			q_snprintf (key, sizeof (key), "%s_argv_%s", *mode, labels[i]);
			assert (!strcmp (FixtureTokensString (key), expected[i]));
			q_snprintf (key, sizeof (key), "%s_start_%s", *mode, labels[i]);
			assert (FixtureTokensFloat (key) == i * (strcmp (*mode, "multi") ? 3 : 4));
			q_snprintf (key, sizeof (key), "%s_end_%s", *mode, labels[i]);
			assert (FixtureTokensFloat (key) == i * (strcmp (*mode, "multi") ? 3 : 4) + 2);
		}
	}
	FixtureTokensExec ("invalid");
	for (const char *const *index = (const char *const[]) {"three", "minus_four", NULL}; *index; index++)
	{
		char key[96];
		q_snprintf (key, sizeof (key), "invalid_%s_witness", *index);
		assert (FixtureTokensRaw (key) != 0);
		q_snprintf (key, sizeof (key), "invalid_%s_prime", *index);
		assert (FixtureTokensFloat (key) == 12);
		q_snprintf (key, sizeof (key), "invalid_%s", *index);
		assert (FixtureTokensRaw (key) == 0);
		for (const char *const *edge = (const char *const[]) {"start", "end", NULL}; *edge; edge++)
		{
			q_snprintf (key, sizeof (key), "invalid_%s_%s_prime", *index, *edge);
			assert (FixtureTokensFloat (key) == 12);
			q_snprintf (key, sizeof (key), "invalid_%s_%s", *index, *edge);
			assert (FixtureTokensFloat (key) == -1);
		}
	}
	FixtureTokensExec ("empty");
	assert (FixtureTokensFloat ("empty_prime") == 12 && FixtureTokensFloat ("empty_result") == 0 &&
		FixtureTokensFloat ("empty_argc") == 0 && FixtureTokensRaw ("empty_argv_prime") != 0 &&
		!strcmp (FixtureTokensString ("empty_argv_prime"), "return-prime") && FixtureTokensRaw ("empty_argv") == 0);
	FixtureTokensExec ("repeat");
	assert (FixtureTokensFloat ("repeat_result") == 2 && FixtureTokensFloat ("repeat_argc") == 2 &&
		!strcmp (FixtureTokensString ("repeat_argv_zero"), "fresh") &&
		!strcmp (FixtureTokensString ("repeat_argv_one"), "token"));
	FixtureTokensExec ("long");
	assert (FixtureTokensFloat ("long_result") == 2 && FixtureTokensFloat ("long_argc") == 2);
	assert (FixtureTokensRaw ("long_argv_zero") != 0 && strlen (FixtureTokensString ("long_argv_zero")) == 1023);
	for (int i = 0; i < 1023; i++) assert (FixtureTokensString ("long_argv_zero")[i] == 'a');
	assert (FixtureTokensString ("long_argv_zero")[1023] == '\0' &&
		FixtureTokensFloat ("long_argv_zero_length") == 1023 &&
		FixtureTokensFloat ("long_start_zero") == 0 && FixtureTokensFloat ("long_end_zero") == 1024 &&
		FixtureTokensFloat ("long_start_one") == 1025 && FixtureTokensFloat ("long_end_one") == 1026 &&
		!strcmp (FixtureTokensString ("long_argv_one"), "b") &&
		!strcmp (FixtureTokensString ("long_argv_last"), "b"));
}

static size_t FixtureTokensCheckZone (void)
{
	int handle = FixtureTokensRaw ("zone_handle");
	size_t id;
	assert (handle < 0);
	id = (size_t)(-1 - handle);
	assert (qcvm->knownzone && id < qcvm->knownzonesize &&
		(qcvm->knownzone[id >> 3] & (1u << (id & 7))));
	assert (!strcmp (FixtureTokensString ("zone_handle"), "two three"));
	return id;
}

static void FixtureTokensCases (void)
{
	dprograms_t *server_program = sv.qcvm.progs;
	size_t zone_id, client_zone_id;
	FixtureSwitch (&sv.qcvm);
	FixtureTokensFinite ();
	FixtureTokensExec ("zone_make");
	zone_id = FixtureTokensCheckZone ();
	FixtureTokensExec ("retokenize");
	assert (FixtureTokensFloat ("retokenize_result") == 2);
	assert (FixtureTokensCheckZone () == zone_id);
	FixtureTokensExec ("shared_server");
	FixtureSwitch (&cl.qcvm);
	assert (PR_LoadProgs ("fixture-csqc.dat", true, PROGHEADER_CRC,
		pr_csqcbuiltins, pr_csqcnumbuiltins));
	FixtureTokensExec ("snapshot_client");
	assert (FixtureTokensFloat ("shared_client_count") == 2 &&
		!strcmp (FixtureTokensString ("shared_client_last"), "tokens"));
	FixtureTokensFinite ();
	FixtureTokensExec ("zone_make");
	client_zone_id = FixtureTokensCheckZone ();
	FixtureTokensExec ("retokenize");
	assert (FixtureTokensFloat ("retokenize_result") == 2 && FixtureTokensCheckZone () == client_zone_id);
	FixtureTokensExec ("zone_free");
	assert (!(qcvm->knownzone[client_zone_id >> 3] & (1u << (client_zone_id & 7))));
	FixtureTokensExec ("shared_client");
	FixtureSwitch (&sv.qcvm);
	FixtureTokensExec ("snapshot_server");
	assert (FixtureTokensFloat ("shared_server_count") == 1 &&
		!strcmp (FixtureTokensString ("shared_server_last"), "client"));
	FixtureSwitch (&cl.qcvm);
	PR_ClearProgs (&cl.qcvm);
	assert (sv.qcvm.progs == server_program);
	FixtureSwitch (&sv.qcvm);
	FixtureTokensExec ("after_clear");
	assert (FixtureTokensRaw ("after_clear_prime") != 0 &&
		!strcmp (FixtureTokensString ("after_clear_prime"), "return-prime") &&
		FixtureTokensFloat ("after_clear_count") == 0 && FixtureTokensRaw ("after_clear_argv") == 0);
	assert (FixtureTokensCheckZone () == zone_id);
	FixtureTokensExec ("zone_free");
	assert (!(qcvm->knownzone[zone_id >> 3] & (1u << (zone_id & 7))));
	puts ("QC_BINDING_TOKENS_NATIVE_PASSED SSQC/CSQC loaded token calls, shared cleanup and zoned ownership");
}

int main (int argc, char **argv)
{
	dprograms_t *server_program;
	int server_result_offset;
	float server_result;

	Fixture_InitNativeEngine (argc, argv, "e1m1", true);
	assert (sv.active);
#ifdef QC_ENTITY_NATIVE_HOST_FIXTURE
	if (COM_CheckParm ("-entities"))
	{
		FixtureEntityCases ();
		return 0;
	}
#endif
#ifdef QC_REFLECTION_NATIVE_HOST_FIXTURE
	if (COM_CheckParm ("-reflection"))
	{
		FixtureReflectionCases ();
		return 0;
	}
#endif
	if (COM_CheckParm ("-events"))
	{
		FixtureEventCases ();
		return 0;
	}
	if (COM_CheckParm ("-calls-forbidden"))
	{
		puts ("QC_BINDING_CALLS_FORBIDDEN_BEGIN");
		FixtureCallsExec ("forbidden");
		return 0;
	}
	if (COM_CheckParm ("-calls-badbuiltin"))
	{
		puts ("QC_BINDING_CALLS_BADBUILTIN_BEGIN");
		FixtureCallsExec ("badbuiltin");
		return 0;
	}
	if (COM_CheckParm ("-calls"))
	{
		FixtureCallsCases ();
		return 0;
	}
	if (COM_CheckParm ("-files"))
	{
		FixtureFilesCases ();
		return 0;
	}
	if (COM_CheckParm ("-resources"))
	{
		FixtureResourceCases ();
		return 0;
	}
	if (COM_CheckParm ("-tokens"))
	{
		FixtureTokensCases ();
		return 0;
	}
	FixtureCheckEnabled (false);
	server_program = sv.qcvm.progs;
	FixtureSwitch (&sv.qcvm);
	server_result_offset = FixtureGlobal ("fixture_float_find_random", ev_float)->ofs;
	server_result = G_FLOAT (server_result_offset);

	FixtureSwitch (&cl.qcvm);
	assert (PR_LoadProgs ("fixture-csqc.dat", true, PROGHEADER_CRC,
		pr_csqcbuiltins, pr_csqcnumbuiltins));
	FixtureCheckEnabled (true);
	PR_ClearProgs (&cl.qcvm);
	assert (sv.qcvm.progs == server_program);
	assert (PR_LoadProgs ("fixture-csqc.dat", true, PROGHEADER_CRC,
		pr_csqcbuiltins, pr_csqcnumbuiltins));
	FixtureCheckEnabled (true);
	FixtureSwitch (&sv.qcvm);
	assert (sv.qcvm.progs == server_program);
	assert (G_FLOAT (server_result_offset) == server_result);
	FixtureCheckEnabled (false); /* Surviving SSQC interpreter, before reload. */

	Cvar_SetQuick (&pr_checkextension, "0");
	assert (PR_LoadProgs ("progs.dat", true, PROGHEADER_CRC,
		pr_ssqcbuiltins, pr_ssqcnumbuiltins));
	FixtureCheckDisabledSSQC ();
	Cvar_SetQuick (&pr_checkextension, "1");
	assert (FixtureFunction ("fixture_ref_random_named")->first_statement == 0);
	assert (FixtureFunction ("fixture_ref_builtin_find")->first_statement == 0);

	Cvar_SetQuick (&pr_checkextension, "0");
	FixtureSwitch (&cl.qcvm);
	assert (PR_LoadProgs ("fixture-csqc.dat", true, PROGHEADER_CRC,
		pr_csqcbuiltins, pr_csqcnumbuiltins));
	FixtureCheckEnabled (true); /* CSQC binding remains enabled with the cvar off. */

	FixtureSwitch (&sv.qcvm);
	Cvar_SetQuick (&pr_checkextension, "1");
	assert (PR_LoadProgs ("progs.dat", true, PROGHEADER_CRC,
		pr_ssqcbuiltins, pr_ssqcnumbuiltins));
	FixtureCheckEnabled (false);
	puts ("QC_BINDING_CORE_NATIVE_PASSED SSQC/CSQC discovery, #0 remap, interpreter calls and reload; no full F03 claim");
	return 0;
}
