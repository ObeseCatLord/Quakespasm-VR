/* Production pack resolver and platform enumerator; synthetic files only. */
#include "../Quake/common.c"
#undef NDEBUG
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

static void touch (const	char *dir, const	char *name)
{
	char path[MAX_OSPATH];
	assert (q_snprintf (path, sizeof (path), "%s/%s", dir, name) < sizeof (path));
	FILE *file = fopen (path, "wb");
	assert (file && fclose (file) == 0);
}

static void expect (const	char *dir, int number, const	char *name)
{
	char path[MAX_OSPATH];
	assert (COM_FindNumberedPack (dir, number, path, sizeof (path)) == (name != NULL));
	if (name) assert (!strcmp (COM_SkipPath (path), name));
}

int main (int argc,	char **argv)
{
	char path[MAX_OSPATH];
	assert (argc == 2);
 const	char *dir = argv[1];
	assert (mkdir (dir, 0700) == 0);
	expect (dir, 0, NULL);
	touch (dir, "PAK0.PAK"); expect (dir, 0, "PAK0.PAK");
	touch (dir, "Pak0.pak"); expect (dir, 0, "PAK0.PAK");
	touch (dir, "pak0.pak"); expect (dir, 0, "pak0.pak");
	touch (dir, "pAk1.PaK"); expect (dir, 1, "pAk1.PaK");
	touch (dir, "PAK3.PAK"); expect (dir, 2, NULL);
	q_snprintf (path, sizeof (path), "%s/PAK2.PAK", dir);
	assert (mkdir (path, 0700) == 0); expect (dir, 2, NULL);
	assert (!COM_FindNumberedPack (dir, 0, path, 4));
	puts ("NUMBERED_PACK_CASE_PASSED exact priority, deterministic case fallback, directories/missing/truncated paths rejected");
	return 0;
}
