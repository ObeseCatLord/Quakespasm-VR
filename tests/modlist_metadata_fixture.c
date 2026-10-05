/* The runner inserts production types/functions at the marked boundaries. */
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

typedef int qboolean;
typedef unsigned char byte;
typedef struct { int unused; } pack_t;
#define true 1
#define false 0
#define MAX_OSPATH 1024
#define MAX_QPATH 64
#define countof(x) (sizeof (x) / sizeof ((x)[0]))
#define q_strcasecmp strcasecmp
#define q_snprintf snprintf
#define q_tolower tolower
#define q_isspace isspace
#define Mem_Free free

/* TYPES */

enum { FS_ENT_FILE = 1, FS_ENT_DIRECTORY = 2, FA_DIRECTORY = 1 };
typedef struct { int attribs; char name[MAX_OSPATH]; } findfile_t;
static searchpath_t *com_searchpaths, *com_base_searchpaths;
static char com_basedirs[2][MAX_OSPATH];
static int com_numbasedirs;
static const char *vfs_metadata;
static unsigned int vfs_path_id;

static void *Mem_Alloc (size_t size)
{
	void *p = calloc (1, size);
	assert (p);
	return p;
}

static size_t q_strlcpy (char *dst, const char *src, size_t size)
{
	size_t len = strlen (src);
	assert (size);
	size_t copied = len < size ? len : size - 1;
	memcpy (dst, src, copied);
	dst[copied] = 0;
	return len;
}

static char *q_strtrim (char *s)
{
	while (isspace ((unsigned char)*s)) s++;
	size_t n = strlen (s);
	while (n && isspace ((unsigned char)s[n - 1])) s[--n] = 0;
	return s;
}

static qboolean COM_ModForbiddenChars (const char *s) { return strpbrk (s, "/\\:") != NULL; }
static const char *COM_FileGetExtension (const char *s)
{
	const char *p = strrchr (s, '.');
	return p ? p + 1 : "";
}
static const char *LOC_GetRawString (const char *s)
{
	return !strcmp (s, "$fixture_name") ? "Localized title" : NULL;
}
static int Sys_FileType (const char *path)
{
	struct stat st;
	if (stat (path, &st)) return 0;
	return S_ISDIR (st.st_mode) ? FS_ENT_DIRECTORY : FS_ENT_FILE;
}

static DIR *scan;
static char scan_root[MAX_OSPATH];
static findfile_t found;
static findfile_t *Sys_FindNext (findfile_t *previous)
{
	(void)previous;
	struct dirent *entry = readdir (scan);
	if (!entry) { closedir (scan); scan = NULL; return NULL; }
	q_strlcpy (found.name, entry->d_name, sizeof (found.name));
	char path[MAX_OSPATH];
	int n = snprintf (path, sizeof (path), "%s/%s", scan_root, found.name);
	assert (n >= 0 && (size_t)n < sizeof (path));
	found.attribs = Sys_FileType (path) == FS_ENT_DIRECTORY ? FA_DIRECTORY : 0;
	return &found;
}
static findfile_t *Sys_FindFirst (const char *root, const char *ext)
{
	assert (!ext && !scan);
	q_strlcpy (scan_root, root, sizeof (scan_root));
	scan = opendir (root);
	return scan ? Sys_FindNext (NULL) : NULL;
}
static byte *COM_LoadMallocFile_TextMode_OSPath (const char *path, void *unused)
{
	assert (!unused);
	FILE *f = fopen (path, "rb");
	if (!f) return NULL;
	assert (!fseek (f, 0, SEEK_END));
	long size = ftell (f);
	assert (size >= 0 && !fseek (f, 0, SEEK_SET));
	byte *text = Mem_Alloc ((size_t)size + 1);
	assert (fread (text, 1, (size_t)size, f) == (size_t)size);
	assert (!fclose (f));
	return text;
}
static byte *COM_LoadFile (const char *name, unsigned int *path_id)
{
	assert (!strcmp (name, "mapdb.json"));
	if (!vfs_metadata) return NULL;
	*path_id = vfs_path_id;
	byte *text = Mem_Alloc (strlen (vfs_metadata) + 1);
	strcpy ((char *)text, vfs_metadata);
	return text;
}

/* PRODUCTION */

static filelist_item_t *item_named (const char *dir)
{
	for (filelist_item_t *item = modlist; item; item = item->next)
		if (!strcmp (item->name, dir)) return item;
	assert (!"missing fixture directory");
	return NULL;
}
static void expect_name (const char *dir, const char *name)
{
	const modinfo_t *info = (const modinfo_t *)(item_named (dir) + 1);
	if (strcmp (info->full_name, name))
	{
		fprintf (stderr, "%s: expected '%s', got '%s'\n", dir, name, info->full_name);
		abort ();
	}
}
static void expect_precedence (void)
{
	expect_name ("description", "Description title");
	expect_name ("loose", "Loose title");
	expect_name ("catalog", "Catalog title");
	expect_name ("overlay", "Overlay title");
	expect_name ("derived", "");
	assert (!strcmp (Modlist_GetFullName (item_named ("localized")), "Localized title"));
}
static void rebuild (const char *text, unsigned int id)
{
	vfs_metadata = text;
	vfs_path_id = id;
	Modlist_Rebuild ();
	expect_precedence ();
}

int main (int argc, char **argv)
{
	assert (argc == 3);
	com_numbasedirs = 2;
	for (int i = 0; i < 2; i++) q_strlcpy (com_basedirs[i], argv[i + 1], MAX_OSPATH);
	/* A top-level game sits above a dependency mounted in two roots, with
	 * both pack and loose search nodes sharing the dependency's path_id. */
	pack_t pack = {0};
	searchpath_t paths[4] = {0};
	const char *dirs[] = {"active", "dependency", "dependency", "id1"};
	unsigned int ids[] = {8, 4, 4, 1};
	for (int i = 0; i < 4; i++)
	{
		paths[i].path_id = ids[i];
		q_strlcpy (paths[i].dir, dirs[i], sizeof (paths[i].dir));
		paths[i].next = i < 3 ? &paths[i + 1] : NULL;
	}
	paths[1].pack = &pack;
	com_searchpaths = paths;
	com_base_searchpaths = &paths[3];

	const char *base = "{\"episodes\":[{\"dir\":\"HIPNOTIC\",\"name\":\"Episode one\"},"
		"{\"dir\":\"rogue\",\"name\":\"Episode two\"}]}";
	rebuild (base, 1);
	expect_name ("hipnotic", "Episode one");
	expect_name ("rogue", "Episode two");
	expect_name ("active", "");
	expect_name ("unrelated", "");

	const char *renamed = "{\"episodes\":[{\"dir\":\"original\",\"name\":\"Owner title\"},"
		"{\"dir\":\"DECLARED\",\"name\":\"Declared title\"}]}";
	rebuild (renamed, 8);
	expect_name ("active", "Owner title");
	expect_name ("declared", "Declared title");
	expect_name ("dependency", "");
	expect_name ("unrelated", "");

	rebuild (renamed, 4);
	expect_name ("dependency", "Owner title");
	expect_name ("active", "");
	expect_name ("declared", "Declared title");
	expect_name ("unrelated", "");
	/* The same path_id also resolves through a loose first node. */
	com_searchpaths = &paths[2];
	rebuild (renamed, 4);
	expect_name ("dependency", "Owner title");
	expect_name ("active", "");
	com_searchpaths = paths;

	const char *copper = "{\"episodes\":[{\"dir\":\"CoPpEr\",\"name\":\"Copper title\"}]}";
	rebuild (copper, 8);
	expect_name ("active", "");
	expect_name ("copper", "Copper title");
	expect_name ("unrelated", "");

	/* Missing owners fail closed; explicit matching entries still work. */
	rebuild (renamed, 16);
	expect_name ("active", "");
	expect_name ("dependency", "");
	expect_name ("declared", "Declared title");
	com_base_searchpaths = NULL;
	rebuild (renamed, 8);
	expect_name ("active", "");
	expect_name ("declared", "Declared title");
	com_base_searchpaths = &paths[3];

	/* Switching back and absence of metadata leave no cached ownership/name. */
	rebuild (base, 1);
	expect_name ("active", "");
	expect_name ("dependency", "");
	rebuild (NULL, 0);
	expect_name ("declared", "");
	assert (!strcmp (Modlist_GetFullName (item_named ("hipnotic")), "Scourge of Armagon"));
	FileList_Clear (&modlist);
	puts ("MODLIST_METADATA_PASSED base/exact/renamed/unrelated/Copper/dependency/overlay/precedence/localization/rebuild");
	return 0;
}
