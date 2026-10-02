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

int main (int argc, char **argv)
{
	dprograms_t *server_program;
	int server_result_offset;
	float server_result;

	Fixture_InitNativeEngine (argc, argv, "e1m1", true);
	assert (sv.active);
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
