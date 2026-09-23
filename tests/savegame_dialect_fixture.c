#include <stdio.h>
#include <string.h>

#include "../Quake/savegame_dialect.h"

#define COMMENT "map___________________kills:  0/  0____"

static int cases_run;

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
	static int passed = 1;

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
	if (!passed)
		return 1;
	printf ("savegame_dialect_fixture: %d cases passed\n", cases_run);
	return 0;
}
