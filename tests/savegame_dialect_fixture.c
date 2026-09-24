#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../Quake/savegame_dialect.h"

#define COMMENT "map___________________kills:  0/  0____"
#define FIXTURE_CAPACITY 16384

static int cases_run;
static char save[FIXTURE_CAPACITY];
static size_t save_length;

static int Check (const char *name, const char *data, size_t length, savegame_dialect_t expected, int expected_version)
{
	int version = -2;
	savegame_dialect_t actual = Savegame_ClassifyHeader (data, length, &version);

	cases_run++;
	if (actual != expected || version != expected_version)
	{
		fprintf (stderr, "%s: got dialect %d/version %d, expected %d/version %d\n", name, actual, version, expected, expected_version);
		return 0;
	}
	return 1;
}

static int Appendf (const char *format, ...)
{
	va_list args;
	int written;
	size_t available = sizeof (save) - save_length;

	va_start (args, format);
	written = vsnprintf (save + save_length, available, format, args);
	va_end (args);
	if (written < 0 || (size_t)written >= available)
		return 0;
	save_length += (size_t)written;
	return 1;
}

static int BuildSave (int version, int maxclients, int name_bytes)
{
	int i, j;

	save_length = 0;
	if (!Appendf ("%d\n%s\n%d\n", version, COMMENT, maxclients))
		return 0;
	for (i = 0; i < maxclients; i++)
	{
		if (!Appendf ("%d\n", i == 0 ? 1 : 0))
			return 0;
		if (version == 7)
		{
			for (j = 0; j < name_bytes; j++)
				if (!Appendf ("%02x", 'a' + j % 26))
					return 0;
			if (!Appendf ("\n"))
				return 0;
		}
		if (!Appendf ("-2\n17\n"))
			return 0;
		for (j = 0; j < SAVEGAME_PREFLIGHT_SPAWN_PARMS; j++)
			if (!Appendf ("%f\n", (double)j + 0.25))
				return 0;
	}
	if (!Appendf ("1.000000\nstart\n100.5\n"))
		return 0;
	for (i = 0; i < SAVEGAME_PREFLIGHT_LIGHTSTYLES; i++)
		if (!Appendf ("m\n"))
			return 0;
	return Appendf ("{\n");
}

static size_t LineStart (const char *data, size_t length, size_t line_number)
{
	size_t offset = 0, line;

	for (line = 0; line < line_number; line++)
	{
		while (offset < length && data[offset] != '\n')
			offset++;
		if (offset == length)
			return length;
		offset++;
	}
	return offset;
}

static int ReplaceLine (char *data, size_t *length, size_t line_number, const char *replacement)
{
	size_t start = LineStart (data, *length, line_number);
	size_t end = start, old_size, new_size, replacement_length = strlen (replacement);

	if (start >= *length)
		return 0;
	while (end < *length && data[end] != '\n')
		end++;
	if (end == *length)
		return 0;
	old_size = end + 1 - start;
	new_size = replacement_length + 1;
	if (new_size > old_size && new_size - old_size > sizeof (save) - *length)
		return 0;
	memmove (data + start + new_size, data + start + old_size, *length - start - old_size);
	memcpy (data + start, replacement, replacement_length);
	data[start + replacement_length] = '\n';
	*length = *length - old_size + new_size;
	return 1;
}

static int CheckPreflight (const char *name, const char *data, size_t length, int maxclients_limit, int expected_valid, int expected_version, int expected_clients)
{
	char before[FIXTURE_CAPACITY];
	savegame_preflight_metadata_t metadata = {-91, -92, 93, 94, 95, 96};
	savegame_preflight_metadata_t sentinel = metadata;
	int actual;

	if (length > sizeof (before))
		return 0;
	if (data)
		memcpy (before, data, length);
	actual = Savegame_ValidateInheritedHeader (data, length, maxclients_limit, &metadata);
	cases_run++;
	if (actual != expected_valid)
	{
		fprintf (stderr, "%s: got preflight %d, expected %d\n", name, actual, expected_valid);
		return 0;
	}
	if (data && memcmp (before, data, length))
	{
		fprintf (stderr, "%s: validator modified its input\n", name);
		return 0;
	}
	if (!expected_valid)
	{
		if (memcmp (&metadata, &sentinel, sizeof (metadata)))
		{
			fprintf (stderr, "%s: failure modified output metadata\n", name);
			return 0;
		}
		return 1;
	}
	if (metadata.version != expected_version || metadata.saved_maxclients != expected_clients ||
		metadata.map_length != 5 || memcmp (data + metadata.map_offset, "start", 5) ||
		metadata.globals_brace_offset >= length || data[metadata.globals_brace_offset] != '{' ||
		metadata.fixed_header_end_offset != metadata.globals_brace_offset + 1)
	{
		fprintf (stderr, "%s: incorrect success metadata\n", name);
		return 0;
	}
	return 1;
}

static size_t SkillLine (int version, int maxclients)
{
	return 3 + (size_t)maxclients * (version == 7 ? 20 : 19);
}

static size_t FirstParmLine (int version)
{
	return version == 7 ? 7 : 6;
}

int main (void)
{
	static const char kex6[] = "6\nid1\n" COMMENT "\n";
	static const char fork6[] = "6\n" COMMENT "\n4\n";
	static const char fork7[] = "7\n" COMMENT "\n4\n";
	static const char legacy5[] = "5\n" COMMENT "\n0.000000\n";
	static const char malformed_version[] = "six\nid1\n" COMMENT "\n";
	static const char truncated[] = "6\nid1\n";
	static const char malformed_legacy[] = "5\n" COMMENT "\nnot-a-float\n";
	static const char ambiguous_comments[] = "6\n" COMMENT "\n" COMMENT "\n";
	static const char ambiguous_fields[] = "6\nid1\n4\n";
	static const char unknown_version[] = "8\nid1\n" COMMENT "\n";
	static const char unknown_v6[] = "6\nid1\nnot-a-comment\n";
	char long_map[SAVEGAME_PREFLIGHT_MAX_MAP_LENGTH + 2];
	char long_name[SAVEGAME_PREFLIGHT_MAX_NAME_BYTES * 2 + 2];
	char long_number[SAVEGAME_PREFLIGHT_NUMBER_LINE_CAPACITY + 1];
	char long_style[SAVEGAME_PREFLIGHT_STRING_LINE_CAPACITY + 1];
	int passed = 1;
	size_t skill, brace;

	passed &= Check ("KEX6", kex6, sizeof (kex6) - 1, SAVEGAME_DIALECT_KEX6, 6);
	passed &= Check ("inherited6", fork6, sizeof (fork6) - 1, SAVEGAME_DIALECT_INHERITED6, 6);
	passed &= Check ("inherited7", fork7, sizeof (fork7) - 1, SAVEGAME_DIALECT_INHERITED7, 7);
	passed &= Check ("legacy5", legacy5, sizeof (legacy5) - 1, SAVEGAME_DIALECT_LEGACY5, 5);
	passed &= Check ("malformed version", malformed_version, sizeof (malformed_version) - 1, SAVEGAME_DIALECT_MALFORMED, -1);
	passed &= Check ("truncated header", truncated, sizeof (truncated) - 1, SAVEGAME_DIALECT_MALFORMED, 6);
	passed &= Check ("malformed legacy header", malformed_legacy, sizeof (malformed_legacy) - 1, SAVEGAME_DIALECT_MALFORMED, 5);
	passed &= Check ("ambiguous comments", ambiguous_comments, sizeof (ambiguous_comments) - 1, SAVEGAME_DIALECT_AMBIGUOUS, 6);
	passed &= Check ("ambiguous fields", ambiguous_fields, sizeof (ambiguous_fields) - 1, SAVEGAME_DIALECT_AMBIGUOUS, 6);
	passed &= Check ("unknown version", unknown_version, sizeof (unknown_version) - 1, SAVEGAME_DIALECT_UNKNOWN, 8);
	passed &= Check ("unknown v6", unknown_v6, sizeof (unknown_v6) - 1, SAVEGAME_DIALECT_UNKNOWN, 6);

	BuildSave (6, 2, 0);
	passed &= CheckPreflight ("valid inherited v6", save, save_length, 2, 1, 6, 2);
	passed &= CheckPreflight ("v6 caller maxclients limit", save, save_length, 1, 0, 0, 0);

	BuildSave (7, 2, SAVEGAME_PREFLIGHT_MAX_NAME_BYTES);
	passed &= CheckPreflight ("valid inherited v7 with 31-byte names", save, save_length, 2, 1, 7, 2);
	passed &= CheckPreflight ("v7 caller maxclients limit", save, save_length, 1, 0, 0, 0);

	BuildSave (6, 1, 0);
	ReplaceLine (save, &save_length, 3, "not-an-int");
	passed &= CheckPreflight ("invalid active integer", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	ReplaceLine (save, &save_length, 3, "2");
	passed &= CheckPreflight ("active flag outside writer values", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	ReplaceLine (save, &save_length, 4, "2junk");
	passed &= CheckPreflight ("invalid colors integer", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	ReplaceLine (save, &save_length, 5, "999999999999999999999");
	passed &= CheckPreflight ("overflowing frags integer", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	ReplaceLine (save, &save_length, FirstParmLine (6), "nan");
	passed &= CheckPreflight ("nonfinite spawn parm", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	ReplaceLine (save, &save_length, FirstParmLine (6), "1.25junk");
	passed &= CheckPreflight ("partial spawn float", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	ReplaceLine (save, &save_length, FirstParmLine (6), "1e1000");
	passed &= CheckPreflight ("out-of-range spawn float", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	memset (long_number, '1', SAVEGAME_PREFLIGHT_NUMBER_LINE_CAPACITY);
	long_number[SAVEGAME_PREFLIGHT_NUMBER_LINE_CAPACITY] = '\0';
	ReplaceLine (save, &save_length, FirstParmLine (6), long_number);
	passed &= CheckPreflight ("oversized numeric line", save, save_length, 16, 0, 0, 0);

	BuildSave (6, 1, 0);
	skill = SkillLine (6, 1);
	ReplaceLine (save, &save_length, skill, "nan");
	passed &= CheckPreflight ("nonfinite skill", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	skill = SkillLine (6, 1);
	ReplaceLine (save, &save_length, skill + 1, "");
	passed &= CheckPreflight ("empty map", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	memset (long_map, 'x', sizeof (long_map) - 1);
	long_map[sizeof (long_map) - 1] = '\0';
	ReplaceLine (save, &save_length, SkillLine (6, 1) + 1, long_map);
	passed &= CheckPreflight ("oversized map", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	ReplaceLine (save, &save_length, SkillLine (6, 1) + 1, "bad\tmap");
	passed &= CheckPreflight ("map control character", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	ReplaceLine (save, &save_length, SkillLine (6, 1) + 1, "bad map");
	passed &= CheckPreflight ("map whitespace desynchronizes donor parser", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	ReplaceLine (save, &save_length, SkillLine (6, 1) + 1, "../start");
	passed &= CheckPreflight ("map traversal component", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	ReplaceLine (save, &save_length, SkillLine (6, 1) + 2, "inf");
	passed &= CheckPreflight ("nonfinite time", save, save_length, 16, 0, 0, 0);

	BuildSave (6, 1, 0);
	ReplaceLine (save, &save_length, SkillLine (6, 1) + 3, "");
	passed &= CheckPreflight ("empty lightstyle", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	memset (long_style, 'm', SAVEGAME_PREFLIGHT_STRING_LINE_CAPACITY);
	long_style[SAVEGAME_PREFLIGHT_STRING_LINE_CAPACITY] = '\0';
	ReplaceLine (save, &save_length, SkillLine (6, 1) + 3, long_style);
	passed &= CheckPreflight ("oversized lightstyle line", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	ReplaceLine (save, &save_length, SkillLine (6, 1) + 3, "a b");
	passed &= CheckPreflight ("lightstyle whitespace desynchronizes donor parser", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	brace = LineStart (save, save_length, SkillLine (6, 1) + 3 + SAVEGAME_PREFLIGHT_LIGHTSTYLES);
	passed &= CheckPreflight ("missing globals brace", save, brace, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	brace = LineStart (save, save_length, SkillLine (6, 1) + 3 + SAVEGAME_PREFLIGHT_LIGHTSTYLES);
	ReplaceLine (save, &save_length, SkillLine (6, 1) + 3 + SAVEGAME_PREFLIGHT_LIGHTSTYLES, "not-a-brace");
	passed &= CheckPreflight ("wrong globals token", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	brace = LineStart (save, save_length, SkillLine (6, 1) + 3 + SAVEGAME_PREFLIGHT_LIGHTSTYLES);
	passed &= CheckPreflight ("bounded brace without trailing newline", save, brace + 1, 16, 1, 6, 1);
	BuildSave (6, 1, 0);
	brace = LineStart (save, save_length, SkillLine (6, 1) + 3 + SAVEGAME_PREFLIGHT_LIGHTSTYLES);
	memmove (save + brace + 1, save + brace, save_length - brace);
	save[brace] = '\0';
	save_length++;
	passed &= CheckPreflight ("embedded NUL before globals brace", save, save_length, 16, 0, 0, 0);

	BuildSave (7, 1, 31);
	ReplaceLine (save, &save_length, 4, "abc");
	passed &= CheckPreflight ("odd-length v7 client name", save, save_length, 16, 0, 0, 0);
	BuildSave (7, 1, 31);
	ReplaceLine (save, &save_length, 4, "00gg");
	passed &= CheckPreflight ("non-hex v7 client name", save, save_length, 16, 0, 0, 0);
	BuildSave (7, 1, 31);
	memset (long_name, 'a', 64);
	long_name[64] = '\0';
	ReplaceLine (save, &save_length, 4, long_name);
	passed &= CheckPreflight ("overlong v7 client name", save, save_length, 16, 0, 0, 0);
	BuildSave (7, 1, 31);
	ReplaceLine (save, &save_length, 4, "6162006364");
	passed &= CheckPreflight ("embedded NUL in v7 client name", save, save_length, 16, 0, 0, 0);

	BuildSave (6, 1, 0);
	ReplaceLine (save, &save_length, 2, "0");
	passed &= CheckPreflight ("zero maxclients", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	ReplaceLine (save, &save_length, 2, "999999999999999999999");
	passed &= CheckPreflight ("overflowing maxclients", save, save_length, 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	passed &= CheckPreflight ("truncated slot", save, LineStart (save, save_length, 3), 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	passed &= CheckPreflight ("truncated final spawn parm", save, LineStart (save, save_length, 21), 16, 0, 0, 0);
	BuildSave (6, 1, 0);
	passed &= CheckPreflight ("truncated inside spawn parm line", save,
		LineStart (save, save_length, FirstParmLine (6)) + 3, 16, 0, 0, 0);

	passed &= CheckPreflight ("KEX6 collision rejected by inherited validator", kex6, sizeof (kex6) - 1, 16, 0, 0, 0);
	passed &= CheckPreflight ("ambiguous v6 collision rejected", ambiguous_fields, sizeof (ambiguous_fields) - 1, 16, 0, 0, 0);
	passed &= CheckPreflight ("ambiguous comments rejected", ambiguous_comments, sizeof (ambiguous_comments) - 1, 16, 0, 0, 0);
	passed &= CheckPreflight ("legacy v5 rejected by inherited validator", legacy5, sizeof (legacy5) - 1, 16, 0, 0, 0);
	passed &= CheckPreflight ("null buffer", NULL, 0, 16, 0, 0, 0);

	if (!passed)
		return 1;
	printf ("savegame_dialect_fixture: %d cases passed\n", cases_run);
	return 0;
}
