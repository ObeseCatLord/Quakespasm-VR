/* Offline loaded-QC builtin binding fixture; see qc-binding-final-2.0-plan.md. */
#ifdef NDEBUG
#error "Loaded QC fixture requires assertions"
#endif
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

int main (int argc, char **argv)
{
	dprograms_t *server_program;
	int server_result_offset;
	float server_result;

	Fixture_InitNativeEngine (argc, argv, "e1m1", true);
	assert (sv.active);
	if (COM_CheckParm ("-resources"))
	{
		FixtureResourceCases ();
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
