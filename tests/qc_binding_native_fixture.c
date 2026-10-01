/* Offline loaded-QC builtin binding fixture; see qc-binding-final-2.0-plan.md. */
#define main ImportedQCBindingNegotiationMain
#include "negotiation_native_fixture.c"
#undef main

#include <math.h>
#include <stdlib.h>

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

int main (int argc, char **argv)
{
	dprograms_t *server_program;
	int server_result_offset;
	float server_result;

	Fixture_InitNativeEngine (argc, argv, "e1m1", true);
	assert (sv.active);
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
