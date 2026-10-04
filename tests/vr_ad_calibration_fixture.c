/* Native search-path evidence using the caller's read-only game assets.
 * Link common.c + sys_sdl.c so COM_OpenFile actually resolves packs/loose files.
 * Reuse only the existing reload fixture's cvar and schema-input harness.
 * Do not run its game-name-only assertions: AD inheritance now needs assets.
 * Usage: vr-ad-calibration-fixture /path/to/quakespasm_straight
 */
#define main VR_UnusedLegacyCalibrationFixtureMain
#define __wrap_COM_LoadFile FixtureLegacyLoadFile
#include "vr_weapon_calibration_reload_fixture.c"
#undef __wrap_COM_LoadFile
#undef main

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/stat.h>

cvar_t developer = {"developer", "0", CVAR_NONE};
static size_t asset_reads;
static qboolean short_asset_read;
static qboolean native_profile_load;

byte *__real_COM_LoadFile(const char *path, unsigned int *path_id);
byte *__wrap_COM_LoadFile(const char *path, unsigned int *path_id)
{
	if (native_profile_load)
		return __real_COM_LoadFile(path, path_id);
	return FixtureLegacyLoadFile(path, path_id);
}

void *Mem_AllocNonZero(size_t size)
{
	void *result = malloc(size);
	assert(result);
	return result;
}

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

/* Independent held/muzzle reference, in the bit order used by the installed
 * audit below. Missing signatures must retain the proven id1 defaults. */
static const struct
{
	const char *name;
	vec3_t held;
	float scale;
} fixture_ad_profiles[] = {
	{"v_shot", {1.5f, 1.7f, 17.5f}, 0.33f},
	{"v_shot2", {-3.5f, 0.4f, 8.5f}, 0.8f},
	{"v_shot3", {-3.5f, 0.4f, 8.5f}, 0.8f},
	{"v_nail", {-9.5f, 3, 17}, 0.5f},
	{"v_nail2", {-6, 3.5f, 20}, 0.4f},
	{"v_rock", {-3, 1.25f, 17}, 0.5f},
	{"v_rock2", {0, 5.55f, 22.5f}, 0.45f},
	{"v_light", {-4, 3.1f, 13}, 0.5f},
	{"v_plasma", {2.8f, 1.8f, 22.5f}, 0.5f},
	{"v_shadaxe0", {-1.5f, 43.1f, 41}, 0.25f},
	{"v_shadaxe1", {-1.5f, 43.1f, 41}, 0.25f},
	{"v_shadaxe2", {-1.5f, 43.1f, 41}, 0.25f},
	{"v_shadaxe3", {-1.5f, 43.1f, 41}, 0.25f},
	{"v_shadaxe4", {-1.5f, 43.1f, 41}, 0.25f},
	{"v_shadaxe5", {-1.5f, 43.1f, 41}, 0.25f},
};

static const struct
{
	const char *game;
	unsigned canonical, aliases;
} fixture_installed_audit[] = {
	{"Tershibboleth", 0, 0},
	{"ad", 0x7fff, 0},
	{"alk", 0x0088, 0},
	{"bonkjam", 0, 0},
	{"ctsj2", 0, 0},
	{"dopa", 0, 0},
	{"dwellv2p2", 0x00e9, 0},
	{"dwellv2p2/dwellv2p2", 0x00e9, 0},
	{"enyo", 0, 0},
	{"gibtropolis", 0x7fff, 0},
	{"hipnotic", 0, 0},
	{"honey", 0, 0},
	{"hwjam2", 0x7fff, 0},
	{"hwjam4", 0x7fff, 0},
	{"id1", 0, 0},
	{"immortal", 0, 0},
	{"limjam", 0x0088, 0},
	{"mg1", 0, 0},
	{"mg3", 0, 0},
	{"mjolnir", 0, 0x7ffd},
	{"nyarlathotep", 0, 0},
	{"peril3.0", 0x0200, 0},
	{"q30a1024", 0x7fff, 0},
	{"qbj3", 0, 0},
	{"qdoom", 0, 0},
	{"quake_rooftop_jam_v2", 0x7fff, 0},
	{"ravenkeep", 0x7f6c, 0},
	{"reliq", 0x00fa, 0},
	{"rm1.2", 0, 0},
	{"rogue", 0, 0},
	{"sacrilege", 0, 0},
	{"smej2", 0, 0},
	{"snack3", 0, 0},
	{"something_wicked", 0, 0},
	{"spiritworld", 0, 0},
	{"tombofthunder", 0, 0},
	{"udob", 0, 0},
	{"vr", 0, 0},
	{"warpspasm", 0, 0},
};

typedef struct
{
	qboolean held_present, muzzle_present;
	vec3_t held, muzzle;
	float scale;
} fixture_profile_t;

static void FixtureAssertADProfile(const char *path, size_t index)
{
	const float *h = fixture_ad_profiles[index].held;
	AssertClassicProfile(path, h[0], h[1], h[2],
		fixture_ad_profiles[index].scale, 0, 0, h[2]);
}

/* Actual installed schema fields remain authored unless a full classic triple
 * is the exact generic copy already covered by the inheritance policy. */
static void FixtureAssertInstalledSchema(size_t mod,
	const fixture_profile_t *stock)
{
	vr_weapon_schema_entry_t entries[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	size_t count;
	byte *file = __real_COM_LoadFile("vr_weapons.txt", NULL);
	qboolean special = !strcmp(fixture_installed_audit[mod].game, "alk") ||
		strstr(fixture_installed_audit[mod].game, "dwellv2p2") != NULL;
	if (!file)
		return;
	assert(VR_WeaponSchemaParse((const char *)file, entries,
		VR_WEAPON_SCHEMA_MAX_ENTRIES, &count));
	Mem_Free(file);
	for (size_t j = 0; j < count; ++j)
	{
		const vr_weapon_schema_entry_t *e = &entries[j];
		qboolean filtered = false;
		vec3_t held, muzzle;
		float scale;
		for (size_t k = 0; k < countof(fixture_ad_profiles); ++k)
		{
			char path[MAX_QPATH], alias[MAX_QPATH];
			snprintf(path, sizeof(path), "progs/%s.mdl", fixture_ad_profiles[k].name);
			snprintf(alias, sizeof(alias), "progs/ad171/%s.mdl", fixture_ad_profiles[k].name);
			qboolean identified = (!strcmp(path, e->viewmodel_path) &&
				(fixture_installed_audit[mod].canonical & (1u << k))) ||
				(!strcmp(alias, e->viewmodel_path) &&
				(fixture_installed_audit[mod].aliases & (1u << k)));
			if (!special && identified && stock[k].held_present &&
				e->has_held_offset && e->has_held_scale && e->has_muzzle_offset &&
				VectorCompare(e->held_offset, stock[k].held) &&
				e->held_scale == stock[k].scale &&
				VectorCompare(e->muzzle_offset, stock[k].muzzle))
			{
				filtered = true;
				FixtureAssertADProfile(e->viewmodel_path, k);
			}
		}
		if (!filtered && (e->has_held_offset || e->has_held_scale))
		{
			assert(VR_WeaponCalibrationLookupHeld(e->viewmodel_path, false, held, &scale));
			if (e->has_held_offset)
				AssertVector(held, e->held_offset[0], e->held_offset[1], e->held_offset[2]);
			if (e->has_held_scale)
				assert(fabsf(scale - e->held_scale) < 0.0001f);
		}
		if (!filtered && e->has_muzzle_offset)
		{
			assert(VR_WeaponCalibrationLookupMuzzle(e->viewmodel_path, false, muzzle));
			AssertVector(muzzle, e->muzzle_offset[0], e->muzzle_offset[1], e->muzzle_offset[2]);
		}
		if (e->has_enhanced_held_offset)
		{
			assert(VR_WeaponCalibrationLookupHeld(e->viewmodel_path, true, held, &scale));
			AssertVector(held, e->enhanced_held_offset[0], e->enhanced_held_offset[1],
				e->enhanced_held_offset[2]);
			assert(scale == 1);
		}
		if (e->has_enhanced_muzzle_offset)
		{
			assert(VR_WeaponCalibrationLookupMuzzle(e->viewmodel_path, true, muzzle));
			AssertVector(muzzle, e->enhanced_muzzle_offset[0],
				e->enhanced_muzzle_offset[1], e->enhanced_muzzle_offset[2]);
		}
	}
}

static void FixtureAuditInstalled(const char *base)
{
	fixture_profile_t stock[countof(fixture_ad_profiles)];
	fixture_file_contents = NULL;
	FixtureMountMod(base, "id1");
	assert(VR_WeaponCalibrationReloadGame());
	AssertStockClassicProfiles();
	for (size_t k = 0; k < countof(fixture_ad_profiles); ++k)
	{
		char path[MAX_QPATH];
		snprintf(path, sizeof(path), "progs/%s.mdl", fixture_ad_profiles[k].name);
		stock[k].held_present = VR_WeaponCalibrationLookupHeld(path, false,
			stock[k].held, &stock[k].scale);
		stock[k].muzzle_present = VR_WeaponCalibrationLookupMuzzle(path, false,
			stock[k].muzzle);
	}
	for (size_t mod = 0; mod < countof(fixture_installed_audit); ++mod)
	{
		printf("Installed calibration audit: %s\n", fixture_installed_audit[mod].game);
		FixtureMountMod(base, fixture_installed_audit[mod].game);
		assert(VR_WeaponCalibrationReloadGame());
		for (size_t k = 0; k < countof(fixture_ad_profiles); ++k)
			for (int alias = 0; alias < 2; ++alias)
			{
				char path[MAX_QPATH];
				vec3_t held, muzzle;
				float scale;
				unsigned mask = alias ? fixture_installed_audit[mod].aliases :
					fixture_installed_audit[mod].canonical;
				snprintf(path, sizeof(path), "progs/%s%s.mdl", alias ? "ad171/" : "",
					fixture_ad_profiles[k].name);
				if (mask & (1u << k))
					FixtureAssertADProfile(path, k);
				else
				{
					qboolean has_held = VR_WeaponCalibrationLookupHeld(path, false, held, &scale);
					qboolean has_muzzle = VR_WeaponCalibrationLookupMuzzle(path, false, muzzle);
					assert(has_held == (!alias && stock[k].held_present));
					assert(has_muzzle == (!alias && stock[k].muzzle_present));
					if (has_held)
					{
						AssertVector(held, stock[k].held[0], stock[k].held[1], stock[k].held[2]);
						assert(scale == stock[k].scale);
					}
					if (has_muzzle)
						AssertVector(muzzle, stock[k].muzzle[0], stock[k].muzzle[1], stock[k].muzzle[2]);
				}
			}
		FixtureAssertNoLookupIO();
		/* Restore the actual game name and load its effective installed profile
		 * through production COM_LoadFile, including inherited profile priority. */
		assert(snprintf(com_gamedir, sizeof(com_gamedir), "%s/%s", base,
			fixture_installed_audit[mod].game) < (int)sizeof(com_gamedir));
		native_profile_load = true;
		assert(VR_WeaponCalibrationReloadGame());
		FixtureAssertInstalledSchema(mod, stock);
		native_profile_load = false;
	}
}

static void FixtureAssertReskinAuthorship(const char *base)
{
	static const struct
	{
		const char *path;
		size_t ad_index;
		unsigned hash;
		float generic[7];
	} variants[] = {
		{"progs/v_shot2.mdl", 1, 0x7242f947u, {-3.5f, 1, 8.5f, 0.8f, 0, 0, 8.5f}},
		{"progs/v_nail.mdl", 3, 0xd3d8b117u, {-5, 3, 15, 0.5f, 0, 0, 15}},
		{"progs/v_nail2.mdl", 4, 0xc79ffca6u, {0, 3, 19, 0.5f, 0, 0, 19}},
		{"progs/v_rock2.mdl", 6, 0x18f7a730u, {10, 7, 19, 0.5f, 0, 0, 19}},
	};
	for (size_t i = 0; i < countof(variants); ++i)
	{
		byte *data;
		size_t size;
		char profile[256];
		unsigned path_id = 0;
		int handle;
		FixtureMountMod(base, "reliq");
		assert(COM_OpenFile(variants[i].path, &handle, &path_id) > 0);
		assert(path_id == 2 && file_from_pak);
		Sys_FileClose(handle);
		data = FixtureReadModel(variants[i].path, &size);
		assert(COM_HashBlock(data, size) == variants[i].hash);
		for (int component = -1; component < 7; ++component)
		{
			float values[7];
			memcpy(values, variants[i].generic, sizeof(values));
			if (component >= 0)
				values[component] += 0.125f;
			assert(snprintf(profile, sizeof(profile),
				"{ viewmodel %s held_offset %g %g %g held_scale %g "
				"muzzle_offset %g %g %g }", variants[i].path,
				values[0], values[1], values[2], values[3], values[4],
				values[5], values[6]) < (int)sizeof(profile));
			fixture_file_contents = profile;
			assert(VR_WeaponCalibrationReloadGame());
			if (component < 0)
				FixtureAssertADProfile(variants[i].path, variants[i].ad_index);
			else
				AssertClassicProfile(variants[i].path, values[0], values[1],
					values[2], values[3], values[4], values[5], values[6]);
		}
		/* A changed full-byte signature is rejected even at a recognized length,
		 * including changes in the shotgun's ignored trailing padding. */
		data[size - 4] ^= 1;
		FixturePrependModel(variants[i].path, data, size, 4);
		fixture_file_contents = NULL;
		assert(VR_WeaponCalibrationReloadGame());
		const float *g = variants[i].generic;
		AssertClassicProfile(variants[i].path, g[0], g[1], g[2], g[3], g[4], g[5], g[6]);
		FixtureClearPaths();
		free(data);
	}
	fixture_file_contents = NULL;
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
	FixtureAuditInstalled(base);
	FixtureAssertReskinAuthorship(base);

	/* All actual calibrated AD models, including the older nailgun version,
	 * get the complete existing AD tuple with no directory-name recognition. */
	for (size_t i = 0; i < 6; ++i)
	{
		static const char *const games[] = {
			"ad", "quake_rooftop_jam_v2", "hwjam2", "q30a1024", "hwjam4", "gibtropolis"
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
