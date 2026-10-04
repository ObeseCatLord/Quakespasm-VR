/* Native search-path evidence using the caller's read-only game assets.
 * Link common.c + sys_sdl.c so COM_OpenFile actually resolves packs/loose files.
 * Reuse only the existing reload fixture's cvar and schema-input harness.
 * Do not run its game-name-only assertions: AD inheritance now needs assets.
 * Usage: vr-ad-calibration-fixture /path/to/quakespasm_straight
 */
#define main VR_UnusedLegacyCalibrationFixtureMain
#include "vr_weapon_calibration_reload_fixture.c"
#undef main

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/stat.h>

cvar_t developer = {"developer", "0", CVAR_NONE};
static size_t asset_reads;
static qboolean short_asset_read;

void *Mem_Alloc(size_t size)
{
	void *result = calloc(1, size);
	assert(result);
	return result;
}

void Con_DPrintf(const char *format, ...)
{
	assert(format);
}

const mod_held_melee_recipe_t *Mod_GetHeldMeleeRecipe(const char *source)
{
	(void)source;
	return NULL;
}

void Sys_Error(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	abort();
}

FILE *Sys_fopen(const char *path, const char *mode)
{
	assert(!strcmp(mode, "rb")); /* No profile or installed asset writes. */
	return fopen(path, mode);
}

qfileofs_t Sys_ftell(FILE *file)
{
	return (qfileofs_t)ftello(file);
}

int Sys_fseek(FILE *file, qfileofs_t offset, int origin)
{
	return fseeko(file, (off_t)offset, origin);
}

int Sys_FileType(const char *path)
{
	struct stat info;
	if (stat(path, &info))
		return FS_ENT_NONE;
	return S_ISREG(info.st_mode) ? FS_ENT_FILE :
		S_ISDIR(info.st_mode) ? FS_ENT_DIRECTORY : FS_ENT_NONE;
}

int __real_Sys_FileRead(int handle, void *dest, int count);
int __wrap_Sys_FileRead(int handle, void *dest, int count)
{
	++asset_reads;
	if (short_asset_read && count > 0)
		return 0;
	return __real_Sys_FileRead(handle, dest, count);
}

static uint32_t FixtureLE32(const byte *value)
{
	return (uint32_t)value[0] | (uint32_t)value[1] << 8 |
		(uint32_t)value[2] << 16 | (uint32_t)value[3] << 24;
}

static void FixtureClearPaths(void)
{
	while (com_searchpaths)
	{
		searchpath_t *next = com_searchpaths->next;
		if (com_searchpaths->pack)
		{
			Sys_FileClose(com_searchpaths->pack->handle);
			free(com_searchpaths->pack->files);
			free(com_searchpaths->pack);
		}
		free(com_searchpaths);
		com_searchpaths = next;
	}
}

/* Only constructs fixture search nodes; production COM_FindFile decides
 * effective asset priority. Mount contiguous packs in native order. */
static void FixtureMountDirectory(const char *base, const char *game,
	unsigned int path_id)
{
	searchpath_t *loose = calloc(1, sizeof(*loose));
	assert(loose);
	assert(snprintf(loose->filename, sizeof(loose->filename), "%s/%s",
		base, game) < (int)sizeof(loose->filename));
	loose->path_id = path_id;
	loose->next = com_searchpaths;
	com_searchpaths = loose;
	for (int i = 0;; ++i)
	{
		char path[MAX_OSPATH];
		int handle;
		byte header[12];
		qfilesize_t size;
		pack_t *pack;
		searchpath_t *node;
		uint32_t directory, directory_size;

		assert(snprintf(path, sizeof(path), "%s/pak%d.pak",
			loose->filename, i) < (int)sizeof(path));
		size = Sys_FileOpenRead(path, &handle);
		if (size < 0)
			break;
		assert(Sys_FileRead(handle, header, sizeof(header)) == sizeof(header));
		assert(!memcmp(header, "PACK", 4));
		directory = FixtureLE32(header + 4);
		directory_size = FixtureLE32(header + 8);
		assert(directory_size % 64 == 0);
		assert((uint64_t)directory + directory_size <= (uint64_t)size);
		assert(directory_size / 64 <= INT_MAX);
		pack = calloc(1, sizeof(*pack));
		node = calloc(1, sizeof(*node));
		assert(pack && node);
		pack->handle = handle;
		pack->numfiles = (int)(directory_size / 64);
		pack->files = calloc((size_t)pack->numfiles, sizeof(*pack->files));
		assert(pack->files);
		assert(Sys_FileSeek(handle, directory) == 0);
		for (int j = 0; j < pack->numfiles; ++j)
		{
			byte entry[64];
			assert(Sys_FileRead(handle, entry, sizeof(entry)) == sizeof(entry));
			assert(memchr(entry, 0, 56));
			memcpy(pack->files[j].name, entry, 56);
			assert(FixtureLE32(entry + 56) <= INT_MAX);
			assert(FixtureLE32(entry + 60) <= INT_MAX);
			pack->files[j].filepos = (int)FixtureLE32(entry + 56);
			pack->files[j].filelen = (int)FixtureLE32(entry + 60);
			assert((uint64_t)pack->files[j].filepos +
				pack->files[j].filelen <= (uint64_t)size);
		}
		node->path_id = path_id;
		node->pack = pack;
		node->next = com_searchpaths;
		com_searchpaths = node;
	}
}

static void FixtureMountMod(const char *base, const char *game)
{
	FixtureClearPaths();
	FixtureMountDirectory(base, "id1", 1);
	if (strcmp(game, "id1"))
		FixtureMountDirectory(base, game, 2);
	/* Deliberately conceal the game name from calibration policy. */
	strcpy(com_gamedir, "/fixtures/unlisted-fork");
}

static byte *FixtureReadModel(const char *name, size_t *size)
{
	int handle;
	qfilesize_t length = COM_OpenFile(name, &handle, NULL);
	byte *data;
	assert(length > 0 && length <= INT_MAX);
	*size = (size_t)length;
	data = malloc(*size);
	assert(data);
	assert(Sys_FileRead(handle, data, (int)length) == length);
	Sys_FileClose(handle);
	return data;
}

/* Exercises the real memory-pack/duplicate-handle path without writing assets.
 * The node owns its pack metadata but the caller keeps the payload alive. */
static void FixturePrependModel(const char *name, const byte *data, size_t size,
	unsigned int path_id)
{
	searchpath_t *node = calloc(1, sizeof(*node));
	pack_t *pack = calloc(1, sizeof(*pack));
	assert(node && pack && size <= INT_MAX);
	pack->files = calloc(1, sizeof(*pack->files));
	assert(pack->files && strlen(name) < sizeof(pack->files[0].name));
	strcpy(pack->files[0].name, name);
	pack->files[0].filelen = (int)size;
	pack->numfiles = 1;
	Sys_MemFileOpenRead(data, (qfilesize_t)size, &pack->handle);
	node->pack = pack;
	node->path_id = path_id;
	node->next = com_searchpaths;
	com_searchpaths = node;
}

static void FixtureAssertNoLookupIO(void)
{
	vec3_t held, muzzle;
	float scale;
	size_t before = asset_reads;
	for (int i = 0; i < 100; ++i)
	{
		assert(VR_WeaponCalibrationLookupHeld("progs/v_shot.mdl", false,
			held, &scale));
		assert(VR_WeaponCalibrationLookupMuzzle("progs/v_shot.mdl", false,
			muzzle));
		assert(!VR_WeaponCalibrationLookupHeld("progs/not_ad.mdl", false,
			held, &scale));
	}
	assert(asset_reads == before);
}

int main(int argc, char **argv)
{
	const char *base;
	byte *stock_shot, *ad_shot, *changed_shot;
	size_t stock_size, ad_size;
	vec3_t held, muzzle;
	vec3_t angles = {0, 0, 0}, source;
	vr_melee_gesture_profile_t melee;
	float scale;
	cvar_t *preset;
	const char *generic = "{ viewmodel progs/v_shot.mdl "
		"held_offset 1.5 1 10 held_scale 0.5 muzzle_offset 0 0 10 }";

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s READ_ONLY_GAME_BASEDIR\n", argv[0]);
		return 77;
	}
	base = argv[1];
	assert(SDL_Init(0));
	Sys_FileInit();
	registered.value = 1.0f;
	FixtureMountMod(base, "id1");
	stock_shot = FixtureReadModel("progs/v_shot.mdl", &stock_size);
	FixtureMountMod(base, "ad");
	ad_shot = FixtureReadModel("progs/v_shot.mdl", &ad_size);
	assert(ad_size == 22260 && COM_HashBlock(ad_shot, ad_size) == 0x77592100u);

	/* All actual calibrated AD models, including the older nailgun version,
	 * get the complete existing AD tuple with no directory-name recognition. */
	for (size_t i = 0; i < 4; ++i)
	{
		static const char *const games[] = {
			"ad", "quake_rooftop_jam_v2", "hwjam2", "q30a1024"
		};
		FixtureMountMod(base, games[i]);
		fixture_file_contents = generic;
		assert(VR_WeaponCalibrationReloadGame());
		AssertADRootProfiles();
		FixtureAssertNoLookupIO();
	}

	/* Schema-owned changed, partial and enhanced fields stay authoritative. */
	fixture_file_contents = "{ viewmodel progs/v_shot.mdl "
		"held_offset 1.5 1 10 held_scale 0.51 muzzle_offset 0 0 10 "
		"enhanced_held_offset 4 5 6 enhanced_muzzle_offset 7 8 9 }";
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_shot.mdl", 1.5f, 1, 10, 0.51f, 0, 0, 10);
	assert(VR_WeaponCalibrationLookupHeld("progs/v_shot.mdl", true, held, &scale));
	AssertVector(held, 4, 5, 6);
	assert(scale == 1);
	assert(VR_WeaponCalibrationLookupMuzzle("progs/v_shot.mdl", true, muzzle));
	AssertVector(muzzle, 7, 8, 9);
	fixture_file_contents = "{ viewmodel progs/v_shot.mdl held_offset 1.5 1 10 }";
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_shot.mdl", 1.5f, 1, 10, 0.33f, 0, 0, 17.5f);
	fixture_file_contents = "{ viewmodel progs/v_shot.mdl "
		"held_offset 1.5 1 10 held_scale 0.5 muzzle_offset 0 0 10 "
		"enhanced_held_offset 4 5 6 enhanced_muzzle_offset 7 8 9 "
		"muzzle_source_offset 2 3 4 melee_speed 2 }";
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_shot.mdl", 1.5f, 1.7f, 17.5f, 0.33f, 0, 0, 17.5f);
	assert(VR_WeaponCalibrationLookupHeld("progs/v_shot.mdl", true, held, &scale));
	AssertVector(held, 4, 5, 6);
	assert(scale == 1);
	assert(VR_WeaponCalibrationLookupMuzzle("progs/v_shot.mdl", true, muzzle));
	AssertVector(muzzle, 7, 8, 9);
	VR_WeaponCalibrationProjectileSourceOffset("progs/v_shot.mdl",
		IT_ROCKET_LAUNCHER, angles, 0, source);
	AssertVector(source, 12, -2, 19);
	(void)VR_WeaponCalibrationLookupMelee("progs/v_shot.mdl", &melee);
	assert(melee.has_speed && melee.speed == 2);

	/* Changing any one saved component protects the entire authored triple. */
	for (int component = 0; component < 7; ++component)
	{
		float values[] = {1.5f, 1, 10, 0.5f, 0, 0, 10};
		char profile[256];
		values[component] += 0.125f;
		assert(snprintf(profile, sizeof(profile),
			"{ viewmodel progs/v_shot.mdl held_offset %g %g %g "
			"held_scale %g muzzle_offset %g %g %g }", values[0], values[1],
			values[2], values[3], values[4], values[5], values[6]) <
			(int)sizeof(profile));
		fixture_file_contents = profile;
		assert(VR_WeaponCalibrationReloadGame());
		AssertClassicProfile("progs/v_shot.mdl", values[0], values[1],
			values[2], values[3], values[4], values[5], values[6]);
	}

	/* Higher-priority stock overrides one weapon; other AD weapons survive. */
	fixture_file_contents = generic;
	FixturePrependModel("progs/v_shot.mdl", stock_shot, stock_size, 4);
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_shot.mdl", 1.5f, 1, 10, 0.5f, 0, 0, 10);
	AssertClassicProfile("progs/v_nail.mdl", -9.5f, 3, 17, 0.5f, 0, 0, 17);

	/* A known name and even an identical byte length do not admit geometry.
	 * Only mutate private memory, never the installed model. */
	changed_shot = malloc(ad_size);
	assert(changed_shot);
	memcpy(changed_shot, ad_shot, ad_size);
	changed_shot[ad_size - 4] ^= 1;
	FixturePrependModel("progs/v_shot.mdl", changed_shot, ad_size, 8);
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_shot.mdl", 1.5f, 1, 10, 0.5f, 0, 0, 10);

	/* One AD file mixed with stock cannot reclassify the whole game. */
	FixtureMountMod(base, "id1");
	FixturePrependModel("progs/v_shot.mdl", ad_shot, ad_size, 2);
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_shot.mdl", 1.5f, 1.7f, 17.5f, 0.33f, 0, 0, 17.5f);
	AssertClassicProfile("progs/v_nail.mdl", -5, 3, 15, 0.5f, 0, 0, 15);
	assert(VR_WeaponCalibrationLookupHeld("progs/v_shot.mdl", true, held, &scale));
	AssertVector(held, -5.718559f, 0.381319f, 7.430882f);
	assert(scale == 1); /* A selected MD5 uses only its enhanced tuple. */
	assert(VR_WeaponCalibrationLookupMuzzle("progs/v_shot.mdl", true, muzzle));
	AssertVector(muzzle, 0, 0, 10);

	/* Explicit generic presets cannot miscalibrate confirmed AD geometry.
	 * Preset changes and cvar lookup do not rescan the filesystem. */
	preset = Cvar_FindVar("vr_gunmodeloffsets");
	assert(preset && preset->callback);
	for (int value = 1; value < 5; ++value)
	{
		size_t before = asset_reads;
		Cvar_SetValueQuick(preset, (float)value);
		preset->callback(preset);
		AssertClassicProfile("progs/v_shot.mdl", 1.5f, 1.7f, 17.5f, 0.33f, 0, 0, 17.5f);
		assert(asset_reads == before);
	}
	Cvar_SetQuick(preset, "0");
	preset->callback(preset);

	/* Short reads fail closed; a subsequent reload retries the same asset. */
	short_asset_read = true;
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_shot.mdl", 1.5f, 1, 10, 0.5f, 0, 0, 10);
	short_asset_read = false;
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_shot.mdl", 1.5f, 1.7f, 17.5f, 0.33f, 0, 0, 17.5f);

	/* Existing AD171 aliases use the same bytes, with no Mjolnir name gate. */
	FixturePrependModel("progs/ad171/v_shot.mdl", ad_shot, ad_size, 4);
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/ad171/v_shot.mdl", 1.5f, 1.7f, 17.5f, 0.33f, 0, 0, 17.5f);

	/* Cached successes disappear when the filesystem changes at game reload. */
	FixtureMountMod(base, "id1");
	fixture_file_contents = generic;
	assert(VR_WeaponCalibrationReloadGame());
	AssertStockClassicProfiles();
	assert(!VR_WeaponCalibrationLookupHeld("progs/ad171/v_shot.mdl", false,
		held, &scale));
	FixtureAssertNoLookupIO();
	FixtureClearPaths();
	free(stock_shot);
	free(ad_shot);
	free(changed_shot);
	puts("Generic AD calibration/native filesystem fixture passed");
	return 0;
}
