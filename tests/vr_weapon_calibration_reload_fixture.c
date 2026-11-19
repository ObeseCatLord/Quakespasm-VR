#include "vr_weapon_calibration.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

#define CALIBRATION_CVAR_COUNT \
	(1 + VR_WEAPON_CALIBRATION_MAX_SLOTS * \
	 (VR_WEAPON_CALIBRATION_VARS_PER_WEAPON + \
	  VR_WEAPON_CALIBRATION_VARS_PER_MUZZLE))

static cvar_t *registered_cvars[CALIBRATION_CVAR_COUNT];
static size_t registered_cvar_count;
static const char *fixture_file_contents;
static size_t file_load_count;
static size_t file_free_count;

static char *FixtureDuplicate(const char *value)
{
	size_t length = strlen(value) + 1;
	char *copy = malloc(length);
	assert(copy);
	memcpy(copy, value, length);
	return copy;
}

void Cvar_RegisterVariable(cvar_t *variable)
{
	assert(variable);
	assert(registered_cvar_count < CALIBRATION_CVAR_COUNT);
	assert(!Cvar_FindVar(variable->name));
	variable->string = FixtureDuplicate(variable->string);
	variable->default_string = FixtureDuplicate(variable->string);
	variable->value = (float)atof(variable->string);
	variable->flags |= CVAR_REGISTERED;
	registered_cvars[registered_cvar_count++] = variable;
}

cvar_t *Cvar_FindVar(const char *name)
{
	size_t index;
	for (index = 0; index < registered_cvar_count; ++index)
		if (!strcmp(registered_cvars[index]->name, name))
			return registered_cvars[index];
	return NULL;
}

void Cvar_SetQuick(cvar_t *variable, const char *value)
{
	char *copy;
	assert(variable && (variable->flags & CVAR_REGISTERED));
	copy = FixtureDuplicate(value);
	free((void *)variable->string);
	variable->string = copy;
	variable->value = (float)atof(copy);
}

void Cvar_SetValueQuick(cvar_t *variable, const float value)
{
	char text[64];
	snprintf(text, sizeof(text), "%.7g", value);
	Cvar_SetQuick(variable, text);
}

void Cvar_SetCallback(cvar_t *variable, cvarcallback_t callback)
{
	variable->callback = callback;
	if (callback)
		variable->flags |= CVAR_CALLBACK;
	else
		variable->flags &= ~CVAR_CALLBACK;
}

void Con_Warning(const char *format, ...)
{
	assert(format);
}

byte *__wrap_COM_LoadFile(const char *path, unsigned int *path_id)
{
	++file_load_count;
	assert(!strcmp(path, "vr_weapons.txt"));
	assert(!path_id);
	if (!fixture_file_contents)
		return NULL;
	return (byte *)FixtureDuplicate(fixture_file_contents);
}

/* Host stubs shared by the calibration fixtures that include this harness
 * (alk, AD/Peril); the production filesystem comes from common.c and
 * sys_sdl.c. */
cvar_t developer = {"developer", "0", CVAR_NONE};

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

void Mem_Free(const void *pointer)
{
	if (pointer)
		++file_free_count;
	free((void *)pointer);
}

static void AssertVector(const vec3_t actual, float x, float y, float z)
{
	assert(fabsf(actual[0] - x) < 0.0001f);
	assert(fabsf(actual[1] - y) < 0.0001f);
	assert(fabsf(actual[2] - z) < 0.0001f);
}

static void AssertClassicProfile(const char *path, float x, float y, float z,
								float scale, float muzzle_x, float muzzle_y,
								float muzzle_z)
{
	vec3_t held;
	vec3_t muzzle;
	float actual_scale;

	assert(VR_WeaponCalibrationLookupHeld(path, false, held,
										   &actual_scale));
	AssertVector(held, x, y, z);
	assert(fabsf(actual_scale - scale) < 0.0001f);
	assert(VR_WeaponCalibrationLookupMuzzle(path, false, muzzle));
	if (!isnan(muzzle_x)) /* NAN: the caller checks the muzzle itself */
		AssertVector(muzzle, muzzle_x, muzzle_y, muzzle_z);
}

static void AssertStockClassicProfiles(void)
{
	static const struct
	{
		const char *path;
		float x, y, z, scale;
	} expected[] = {
		{"progs/v_axe.mdl", -4.0f, 24.0f, 37.0f, 0.33f},
		{"progs/v_shot.mdl", 1.5f, 1.0f, 10.0f, 0.5f},
		{"progs/v_shot2.mdl", -3.5f, 1.0f, 8.5f, 0.8f},
		{"progs/v_nail.mdl", -5.0f, 3.0f, 15.0f, 0.5f},
		{"progs/v_nail2.mdl", 0.0f, 3.0f, 19.0f, 0.5f},
		{"progs/v_rock.mdl", 10.0f, 1.5f, 13.0f, 0.5f},
		{"progs/v_rock2.mdl", 10.0f, 7.0f, 19.0f, 0.5f},
		{"progs/v_light.mdl", 3.0f, 4.0f, 13.0f, 0.5f},
		{"progs/v_hammer.mdl", -4.0f, 17.5f, 36.0f, 0.33f},
		{"progs/v_laserg.mdl", 65.0f, 3.7f, 15.0f, 0.33f},
		{"progs/v_prox.mdl", 10.0f, 1.5f, 13.0f, 0.5f},
		{"progs/v_lava.mdl", -5.0f, 3.0f, 15.0f, 0.5f},
		{"progs/v_lava2.mdl", 0.0f, 3.0f, 19.0f, 0.5f},
		{"progs/v_multi.mdl", 10.0f, 1.5f, 13.0f, 0.5f},
		{"progs/v_multi2.mdl", 10.0f, 7.0f, 19.0f, 0.5f},
		{"progs/v_plasma.mdl", 3.0f, 4.0f, 13.0f, 0.5f},
	};

	/* With the installed id1 progs.dat and meshes mounted, geometry families
	 * replace the implicit (0, 0, held Z) muzzle with the stock barrel tip
	 * through the same grip (docs/vr-weapon-families.md). */
	static const struct
	{
		const char *path;
		float x, y, z;
	} barrel[] = {
		{"progs/v_axe.mdl", -6.05256f, 4.25832f, 10.6164f},
		{"progs/v_shot.mdl", -0.142509f, -1.45655f, 6.11379f},
		{"progs/v_shot2.mdl", -0.806307f, -0.631002f, 6.29255f},
		{"progs/v_nail.mdl", 0.070343f, -0.448291f, 7.06118f},
		{"progs/v_nail2.mdl", -0.063868f, -0.710397f, 8.20543f},
		{"progs/v_rock.mdl", -0.006432f, -0.856543f, 5.33836f},
		{"progs/v_rock2.mdl", 0.742481f, -0.41139f, 18.944f},
		{"progs/v_light.mdl", 0.448125f, -0.994796f, 12.6097f},
	};

	for (size_t index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index)
	{
		vec3_t muzzle;
		size_t b;
		AssertClassicProfile(expected[index].path, expected[index].x,
			expected[index].y, expected[index].z, expected[index].scale,
			NAN, NAN, NAN);
		assert(VR_WeaponCalibrationLookupMuzzle(expected[index].path, false,
			muzzle));
		for (b = 0; b < sizeof(barrel) / sizeof(barrel[0]); ++b)
			if (!strcmp(barrel[b].path, expected[index].path) &&
				fabsf(muzzle[0] - barrel[b].x) < 0.001f &&
				fabsf(muzzle[1] - barrel[b].y) < 0.001f &&
				fabsf(muzzle[2] - barrel[b].z) < 0.001f)
				break;
		if (b == sizeof(barrel) / sizeof(barrel[0]))
			AssertVector(muzzle, 0.0f, 0.0f, expected[index].z);
	}
}

__attribute__((unused)) static void AssertADRootProfiles(void)
{
	static const struct
	{
		const char *path;
		float x;
		float y;
		float z;
		float scale;
	} expected[] = {
		{"progs/v_shadaxe0.mdl", -1.5f, 43.1f, 41.0f, 0.25f},
		{"progs/v_shadaxe1.mdl", -1.5f, 43.1f, 41.0f, 0.25f},
		{"progs/v_shadaxe2.mdl", -1.5f, 43.1f, 41.0f, 0.25f},
		{"progs/v_shadaxe3.mdl", -1.5f, 43.1f, 41.0f, 0.25f},
		{"progs/v_shadaxe4.mdl", -1.5f, 43.1f, 41.0f, 0.25f},
		{"progs/v_shadaxe5.mdl", -1.5f, 43.1f, 41.0f, 0.25f},
		{"progs/v_shot3.mdl", -3.5f, 0.4f, 8.5f, 0.8f},
		{"progs/v_shot.mdl", 1.5f, 1.7f, 17.5f, 0.33f},
		{"progs/v_shot2.mdl", -3.5f, 0.4f, 8.5f, 0.8f},
		{"progs/v_nail.mdl", -9.5f, 3.0f, 17.0f, 0.5f},
		{"progs/v_nail2.mdl", -6.0f, 3.5f, 20.0f, 0.4f},
		{"progs/v_rock.mdl", -3.0f, 1.25f, 17.0f, 0.5f},
		{"progs/v_rock2.mdl", 0.0f, 5.55f, 22.5f, 0.45f},
		{"progs/v_light.mdl", -4.0f, 3.1f, 13.0f, 0.5f},
		{"progs/v_plasma.mdl", 2.8f, 1.8f, 22.5f, 0.5f},
	};
	size_t index;

	for (index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index)
		AssertClassicProfile(expected[index].path, expected[index].x,
			expected[index].y, expected[index].z, expected[index].scale,
			0.0f, 0.0f, expected[index].z);
}

/* AD values come from byte-identified installed assets
 * (tests/vr_ad_calibration_fixture.c). This harness mounts no assets, so a
 * game name alone must leave stock paths on their stock values and give AD-only
 * paths no calibration. */
static void AssertNoNameOnlyADProfiles(void)
{
	static const char *const ad_only[] = {
		"progs/v_shadaxe0.mdl", "progs/v_shadaxe5.mdl", "progs/v_shot3.mdl",
		"progs/ad171/v_shot.mdl", "progs/ad171/v_rock.mdl",
		"progs/ad171/v_shadaxe0.mdl",
	};
	vec3_t held;
	float scale;
	AssertClassicProfile("progs/v_shot.mdl", 1.5f, 1.0f, 10.0f, 0.5f,
		0.0f, 0.0f, 10.0f);
	AssertClassicProfile("progs/v_nail.mdl", -5.0f, 3.0f, 15.0f, 0.5f,
		0.0f, 0.0f, 15.0f);
	for (size_t i = 0; i < sizeof(ad_only) / sizeof(ad_only[0]); ++i)
		assert(!VR_WeaponCalibrationLookupHeld(ad_only[i], false, held, &scale));
}

__attribute__((unused)) static void AssertAD171AliasProfiles(void)
{
	static const struct
	{
		const char *path;
		float x;
		float y;
		float z;
		float scale;
	} expected[] = {
		{"progs/ad171/v_shot.mdl", 1.5f, 1.7f, 17.5f, 0.33f},
		{"progs/ad171/v_shot3.mdl", -3.5f, 0.4f, 8.5f, 0.8f},
		{"progs/ad171/v_rock.mdl", -3.0f, 1.25f, 17.0f, 0.5f},
		{"progs/ad171/v_rock2.mdl", 0.0f, 5.55f, 22.5f, 0.45f},
		{"progs/ad171/v_light.mdl", -4.0f, 3.1f, 13.0f, 0.5f},
		{"progs/ad171/v_plasma.mdl", 2.8f, 1.8f, 22.5f, 0.5f},
		{"progs/ad171/v_shadaxe0.mdl", -1.5f, 43.1f, 41.0f, 0.25f},
		{"progs/ad171/v_shadaxe1.mdl", -1.5f, 43.1f, 41.0f, 0.25f},
		{"progs/ad171/v_shadaxe2.mdl", -1.5f, 43.1f, 41.0f, 0.25f},
		{"progs/ad171/v_shadaxe3.mdl", -1.5f, 43.1f, 41.0f, 0.25f},
		{"progs/ad171/v_shadaxe4.mdl", -1.5f, 43.1f, 41.0f, 0.25f},
		{"progs/ad171/v_shadaxe5.mdl", -1.5f, 43.1f, 41.0f, 0.25f},
		{"progs/ad171/v_nail2.mdl", -6.0f, 3.5f, 20.0f, 0.4f},
	};
	size_t index;

	for (index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index)
		AssertClassicProfile(expected[index].path, expected[index].x,
			expected[index].y, expected[index].z, expected[index].scale,
			0.0f, 0.0f, expected[index].z);
}

static void AssertFallbackMuzzles(void)
{
	static const struct
	{
		const char *path;
		float muzzle_z;
	} expected[] = {
		{"progs/v_axe.mdl", 37.0f},
		{"progs/v_shot.mdl", 10.0f},
		{"progs/v_shot2.mdl", 8.5f},
		{"progs/v_nail.mdl", 15.0f},
		{"progs/v_nail2.mdl", 19.0f},
		{"progs/v_rock.mdl", 13.0f},
		{"progs/v_rock2.mdl", 19.0f},
		{"progs/v_light.mdl", 13.0f},
	};
	vec3_t muzzle;
	size_t index;

	for (index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index)
	{
		assert(VR_WeaponCalibrationLookupMuzzle(expected[index].path, true, muzzle));
		AssertVector(muzzle, 0.0f, 0.0f, expected[index].muzzle_z);
	}
}

static void AssertEnyoFallbacks(void)
{
	static const struct
	{
		const char *path;
		float x;
		float y;
		float z;
		float scale;
	} expected[] = {
		{"progs/ee_v_sword.mdl", 25.0f, 49.0f, 60.0f, 0.2f},
		{"progs/ee_v_pistol.mdl", 12.0f, 24.0f, 29.0f, 0.2f},
		{"progs/ee_v_sgun.mdl", -2.3f, 21.3f, 35.3f, 0.2f},
		{"progs/ee_v_smgs.mdl", 3.5f, 24.6f, 29.8f, 0.2f},
		{"progs/ee_v_plasma.mdl", -1.5f, 21.8f, 36.0f, 0.2f},
		{"progs/ee_v_glaunch.mdl", -3.8f, 24.0f, 35.5f, 0.2f},
		{"progs/ee_v_rlaunch.mdl", 4.0f, 28.5f, 40.5f, 0.2f},
		{"progs/ee_v_railgun.mdl", -1.0f, 22.5f, 34.5f, 0.2f},
		{"progs/ee_v_av72.mdl", 0.5f, 24.0f, 38.5f, 0.2f},
		{"progs/ee_v_legal.mdl", 0.0f, 55.0f, 29.0f, 0.2f},
	};
	vec3_t held;
	vec3_t muzzle;
	float scale;
	size_t index;

	for (index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index)
	{
		assert(VR_WeaponCalibrationLookupHeld(expected[index].path, false, held, &scale));
		AssertVector(held, expected[index].x, expected[index].y,
				 expected[index].z);
		assert(fabsf(scale - expected[index].scale) < 0.0001f);
		assert(VR_WeaponCalibrationLookupMuzzle(expected[index].path, false, muzzle));
		AssertVector(muzzle, 0.0f, 0.0f, expected[index].z);
	}
}

int main(void)
{
	vec3_t muzzle;
	vec3_t held;
	float held_scale;
	vr_melee_gesture_profile_t melee;

	strcpy(com_gamedir, "/fixtures/id1");

	/* A missing file publishes both classic and enhanced stock profiles. */
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 1 && file_free_count == 0);
	assert(registered_cvar_count == CALIBRATION_CVAR_COUNT);
	AssertFallbackMuzzles();
	AssertStockClassicProfiles();
	assert(!VR_WeaponCalibrationLookupHeld(
		"progs/ad171/v_shot.mdl", false, held, &held_scale));
	assert(!VR_WeaponCalibrationLookupHeld(
		"progs/ee_v_sword.mdl", false, held, &held_scale));

	/* Authored classic fields override only themselves; enhanced fallback stays. */
	fixture_file_contents =
		"{ model progs/v_shot.mdl muzzle_offset 1 2 3 } "
		"{ model progs/v_shot.mdl held_offset 4 5 6 } "
		"{ model progs/custom.mdl muzzle_offset 4 5 6 }";
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 2 && file_free_count == 1);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/v_shot.mdl", false, muzzle));
	AssertVector(muzzle, 1.0f, 2.0f, 3.0f);
	assert(VR_WeaponCalibrationLookupHeld(
		"progs/v_shot.mdl", false, held, &held_scale));
	AssertVector(held, 4.0f, 5.0f, 6.0f);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/v_shot.mdl", true, muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 10.0f);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/custom.mdl", false, muzzle));
	AssertVector(muzzle, 4.0f, 5.0f, 6.0f);

	/* Held-only schema data seeds classic muzzle Z on enhanced fallback slots. */
	fixture_file_contents =
		"{ model progs/v_shot.mdl held_offset 1 2 11 }";
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 3 && file_free_count == 2);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/v_shot.mdl", false, muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 11.0f);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/v_shot.mdl", true, muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 10.0f);

	/* A later reload replaces the prior file and applies authored enhanced data. */
	fixture_file_contents =
		"{ model progs/v_shot.mdl enhanced_muzzle_offset 7 8 9 }";
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 4 && file_free_count == 3);
	assert(!VR_WeaponCalibrationLookupMuzzle(
		"progs/custom.mdl", false, muzzle));
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/v_shot.mdl", false, muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 10.0f);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/v_shot.mdl", true, muzzle));
	AssertVector(muzzle, 7.0f, 8.0f, 9.0f);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/v_axe.mdl", true, muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 37.0f);

	/* Invalid input is rejected while the known-good built-ins remain active. */
	fixture_file_contents =
		"{ model progs/v_shot.mdl muzzle_offset 1 2 not-a-number }";
	assert(!VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 5 && file_free_count == 4);
	AssertFallbackMuzzles();
	AssertStockClassicProfiles();
	assert(!VR_WeaponCalibrationLookupMuzzle(
		"progs/custom.mdl", false, muzzle));
	assert(!strcmp(vr_weapon_offset[4].string, "progs/v_axe.mdl"));

	/* Missing-file reload discards the old game profile and restores defaults. */
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 6 && file_free_count == 4);
	AssertFallbackMuzzles();
	AssertStockClassicProfiles();

	/* Enyo defaults remain usable with identity-only schema entries. */
	strcpy(com_gamedir, "/fixtures/enyo");
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 7 && file_free_count == 4);
	AssertEnyoFallbacks();

	fixture_file_contents =
		"{ bitmask 4096 viewmodel progs/ee_v_sword.mdl } "
		"{ bitmask 1 viewmodel progs/ee_v_pistol.mdl "
		"held_offset 1 2 3 held_scale 0.5 muzzle_offset 9 10 11 }";
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 8 && file_free_count == 5);
	assert(VR_WeaponCalibrationLookupHeld(
		"progs/ee_v_sword.mdl", false, held, &held_scale));
	AssertVector(held, 25.0f, 49.0f, 60.0f);
	assert(fabsf(held_scale - 0.2f) < 0.0001f);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/ee_v_sword.mdl", false, muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 60.0f);
	assert(VR_WeaponCalibrationLookupHeld(
		"progs/ee_v_pistol.mdl", false, held, &held_scale));
	AssertVector(held, 1.0f, 2.0f, 3.0f);
	assert(fabsf(held_scale - 0.5f) < 0.0001f);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/ee_v_pistol.mdl", false, muzzle));
	AssertVector(muzzle, 9.0f, 10.0f, 11.0f);

	/* AD defaults need identified AD assets, not an AD-looking game name. */
	strcpy(com_gamedir, "/fixtures/ad");
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 9 && file_free_count == 5);
	AssertNoNameOnlyADProfiles();

	/* Without identified AD meshes q30's old generic profile is ordinary
	 * authored data. It keeps non-AD paths and a changed complete triple. */
	strcpy(com_gamedir, "/fixtures/q30a1024");
	fixture_file_contents =
		"{ viewmodel progs/v_axe.mdl held_scale 0.33 "
		"held_offset -4 24 37 muzzle_offset 0 0 37 } "
		"{ viewmodel progs/v_shot.mdl held_scale 0.5 "
		"held_offset 1.5 1 10 muzzle_offset 0 0 10 } "
		"{ viewmodel progs/v_shot2.mdl held_scale 0.8 "
		"held_offset -3.5 1 8.5 muzzle_offset 0 0 8.5 } "
		"{ viewmodel progs/v_nail.mdl held_scale 0.5 "
		"held_offset -5 3 15 muzzle_offset 0 0 15 } "
		"{ viewmodel progs/v_nail2.mdl held_scale 0.5 "
		"held_offset 0 3 19 muzzle_offset 0 0 19 } "
		"{ viewmodel progs/v_rock.mdl held_scale 0.5 "
		"held_offset 10 1.5 13 muzzle_offset 0 0 13 } "
		"{ viewmodel progs/v_rock2.mdl held_scale 0.5 "
		"held_offset 10 7 19 muzzle_offset 0 0 19 } "
		"{ viewmodel progs/v_light.mdl held_scale 0.5 "
		"held_offset 3 4 13 muzzle_offset 0 0 13 } "
		"{ viewmodel progs/v_hammer.mdl held_scale 0.33 "
		"held_offset -4 17.5 36 muzzle_offset 0 0 36 } "
		"{ viewmodel progs/v_laserg.mdl held_scale 0.33 "
		"held_offset 65 3.7 15 muzzle_offset 0 0 15 } "
		"{ viewmodel progs/v_prox.mdl held_scale 0.5 "
		"held_offset 10 1.5 13 muzzle_offset 0 0 13 } "
		"{ viewmodel progs/v_lava.mdl held_scale 0.5 "
		"held_offset -5 3 15 muzzle_offset 0 0 15 } "
		"{ viewmodel progs/v_lava2.mdl held_scale 0.5 "
		"held_offset 0 3 19 muzzle_offset 0 0 19 } "
		"{ viewmodel progs/v_multi.mdl held_scale 0.5 "
		"held_offset 10 1.5 13 muzzle_offset 0 0 13 } "
		"{ viewmodel progs/v_multi2.mdl held_scale 0.5 "
		"held_offset 10 7 19 muzzle_offset 0 0 19 } "
		"{ viewmodel progs/v_plasma.mdl held_scale 0.5 "
		"held_offset 3 4 13 muzzle_offset 0 0 13 } "
		"{ viewmodel progs/v_axe2.mdl held_scale 0.33 "
		"held_offset -3.5 34 41.5 muzzle_offset 0 0 41.5 }";
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 10 && file_free_count == 6);
	AssertStockClassicProfiles();
	AssertClassicProfile("progs/v_axe.mdl", -4.0f, 24.0f, 37.0f,
		0.33f, 0.0f, 0.0f, 37.0f);
	AssertClassicProfile("progs/v_axe2.mdl", -3.5f, 34.0f, 41.5f,
		0.33f, 0.0f, 0.0f, 41.5f);
	fixture_file_contents =
		"{ viewmodel progs/v_shot.mdl held_scale 0.6 "
		"held_offset 9 8 7 muzzle_offset 6 5 4 }";
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 11 && file_free_count == 7);
	AssertClassicProfile("progs/v_shot.mdl", 9.0f, 8.0f, 7.0f, 0.6f,
		6.0f, 5.0f, 4.0f);
	/* q30's nail QuakeC uses self.origin + 16 up; rockets retain the
	 * ordinary eight-forward source. */
	{
		vec3_t source, angles = {0, 0, 0};
		VR_WeaponCalibrationProjectileSourceOffset("progs/v_nail.mdl",
			IT_NAILGUN, angles, 22.0f, source);
		AssertVector(source, 0.0f, 0.0f, 16.0f);
		VR_WeaponCalibrationProjectileSourceOffset("progs/v_rock.mdl",
			IT_ROCKET_LAUNCHER, angles, 22.0f, source);
		AssertVector(source, 8.0f, 0.0f, 16.0f);
	}

	/* Only hash-confirmed Mjolnir ad171 copies receive canonical AD values;
	 * none are mounted here (see vr_ad_calibration_fixture.c). */
	strcpy(com_gamedir, "/fixtures/mjolnir");
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 12 && file_free_count == 7);
	AssertNoNameOnlyADProfiles();
	assert(!VR_WeaponCalibrationLookupHeld(
		"progs/ad171/v_shot2.mdl", false, held, &held_scale));
	assert(!VR_WeaponCalibrationLookupHeld(
		"progs/ad171/v_nail.mdl", false, held, &held_scale));
	{
		vec3_t source, angles = {0, 0, 0};
		VR_WeaponCalibrationProjectileSourceOffset(
			"progs/ad171/v_nail.mdl", IT_NAILGUN, angles, 22.0f,
			source);
		AssertVector(source, 0.0f, 0.0f, 16.0f);
		VR_WeaponCalibrationProjectileSourceOffset(
			"progs/its/v_crossbow1.mdl", 0, angles, 22.0f,
			source);
		AssertVector(source, 0.0f, 0.0f, 16.0f);
	}

	/* Mjolnir schema data overrides an alias and reload restores the baseline. */
	fixture_file_contents =
		"{ viewmodel progs/ad171/v_shot.mdl held_scale 0.6 "
		"held_offset 12 13 14 muzzle_offset 2 3 4 }";
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 13 && file_free_count == 8);
	AssertClassicProfile("progs/ad171/v_shot.mdl", 12.0f, 13.0f, 14.0f,
		0.6f, 2.0f, 3.0f, 4.0f);
	assert(!VR_WeaponCalibrationLookupHeld(
		"progs/ad171/v_rock.mdl", false, held, &held_scale));
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 14 && file_free_count == 8);
	assert(!VR_WeaponCalibrationLookupHeld(
		"progs/ad171/v_shot.mdl", false, held, &held_scale));

	/* AD-derived game names alone do not imply AD meshes; their own schema
	 * still overrides an individual default. */
	strcpy(com_gamedir, "/fixtures/gibtropolis");
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	AssertNoNameOnlyADProfiles();
	strcpy(com_gamedir, "/fixtures/hwjam4");
	fixture_file_contents =
		"{ viewmodel progs/v_shot.mdl held_scale 0.7 "
		"held_offset 1 2 3 muzzle_offset 4 5 6 }";
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_shot.mdl", 1.0f, 2.0f, 3.0f,
		0.7f, 4.0f, 5.0f, 6.0f);
	assert(!VR_WeaponCalibrationLookupHeld(
		"progs/v_shadaxe5.mdl", false, held, &held_scale));

	/* Hipnotic proximity bombs originate at self.origin in its QuakeC.
	 * Ordinary weapons and other game roots retain their source offsets. */
	{
		vec3_t source, angles = {0, 0, 0};
		strcpy(com_gamedir, "/fixtures/hipnotic");
		fixture_file_contents = NULL;
		assert(VR_WeaponCalibrationReloadGame());
		VR_WeaponCalibrationProjectileSourceOffset("progs/v_prox.mdl",
			HIT_PROXIMITY_GUN, angles, 22.0f, source);
		AssertVector(source, 0.0f, 0.0f, 0.0f);
		VR_WeaponCalibrationProjectileSourceOffset("progs/v_rock.mdl",
			IT_ROCKET_LAUNCHER, angles, 22.0f, source);
		AssertVector(source, 8.0f, 0.0f, 16.0f);
		VR_WeaponCalibrationProjectileSourceOffset("progs/v_prox.mdl",
			IT_ROCKET_LAUNCHER, angles, 22.0f, source);
		AssertVector(source, 8.0f, 0.0f, 16.0f);
		fixture_file_contents =
			"{ viewmodel progs/v_prox.mdl spawn_at_self_origin 0 }";
		assert(VR_WeaponCalibrationReloadGame());
		VR_WeaponCalibrationProjectileSourceOffset("progs/v_prox.mdl",
			HIT_PROXIMITY_GUN, angles, 22.0f, source);
		AssertVector(source, 8.0f, 0.0f, 16.0f);
		strcpy(com_gamedir, "/fixtures/id1");
		fixture_file_contents = NULL;
		assert(VR_WeaponCalibrationReloadGame());
		VR_WeaponCalibrationProjectileSourceOffset("progs/v_prox.mdl",
			HIT_PROXIMITY_GUN, angles, 22.0f, source);
		AssertVector(source, 8.0f, 0.0f, 16.0f);
	}

	/* Snack Pack has dedicated Stakegun and Impaler geometry and source
	 * expressions.  Its Rotary reuses Dwell's calibrated mesh values. */
	strcpy(com_gamedir, "/fixtures/snack");
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_nail.mdl", -11.88f, 32.99599f,
		113.865081f, 0.11f, -2.493333f, 4.4f, 7.773333f);
	AssertClassicProfile("progs/v_nail2.mdl", 0.721246f, 74.173819f,
		139.216208f, 0.11f, 0.0f, 2.493333f, 17.16f);
	AssertClassicProfile("progs/v_shot3.mdl", -3.5f, 0.4f, 8.5f,
		0.5333333f, 0.0f, 0.0f, 8.5f);
	{
		vec3_t source, angles = {0, 0, 0};
		VR_WeaponCalibrationProjectileSourceOffset("progs/v_nail.mdl",
			IT_NAILGUN, angles, 22.0f, source);
		AssertVector(source, 11.0f, 0.0f, 16.0f);
		VR_WeaponCalibrationProjectileSourceOffset("progs/v_nail2.mdl",
			IT_SUPER_NAILGUN, angles, 22.0f, source);
		AssertVector(source, 0.0f, 0.0f, 0.0f);
	}

	/* HWJAM2 has no duplicate preset; AD values need identified assets. */
	strcpy(com_gamedir, "/fixtures/hwjam2");
	assert(VR_WeaponCalibrationReloadGame());
	AssertNoNameOnlyADProfiles();

	/* Unauthored melee profiles require deliberate motion; authored speed
	 * continues to take precedence over the safer default. */
	strcpy(com_gamedir, "/fixtures/id1");
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	assert(VR_WeaponCalibrationLookupMelee("progs/v_axe.mdl", &melee));
	assert(fabsf(melee.speed - 1.75f) < 0.0001f);
	assert(!melee.has_speed);
	fixture_file_contents =
		"{ viewmodel progs/v_axe.mdl melee_speed 0.5 }";
	assert(VR_WeaponCalibrationReloadGame());
	assert(VR_WeaponCalibrationLookupMelee("progs/v_axe.mdl", &melee));
	assert(fabsf(melee.speed - 0.5f) < 0.0001f);
	assert(melee.has_speed);

	/* Legacy detection must never consume a partially customized classic
	 * triple, including representably different values within save tolerance. */
	strcpy(com_gamedir, "/fixtures/q30a1024");
	fixture_file_contents =
		"{ viewmodel progs/v_shot.mdl held_scale 0.5 "
		"held_offset 1.5 1 10.00005 muzzle_offset 0 0 10 }";
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_shot.mdl", 1.5f, 1.0f, 10.00005f,
		0.5f, 0.0f, 0.0f, 10.0f);
	fixture_file_contents =
		"{ viewmodel progs/v_shot.mdl held_scale 0.500002 "
		"held_offset 1.5 1 10 muzzle_offset 0 0 10 }";
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_shot.mdl", 1.5f, 1.0f, 10.0f,
		0.500002f, 0.0f, 0.0f, 10.0f);
	fixture_file_contents =
		"{ viewmodel progs/v_shot.mdl held_scale 0.5 "
		"held_offset 1.5 1 10 muzzle_offset 0 0 10.00005 }";
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_shot.mdl", 1.5f, 1.0f, 10.0f,
		0.5f, 0.0f, 0.0f, 10.00005f);
	fixture_file_contents =
		"{ viewmodel progs/v_shot.mdl held_scale 0.5 held_offset 1.5 1 10 }";
	assert(VR_WeaponCalibrationReloadGame());
	assert(VR_WeaponCalibrationLookupHeld("progs/v_shot.mdl", false, held, &held_scale));
	AssertVector(held, 1.5f, 1.0f, 10.0f);
	assert(held_scale == 0.5f);
	fixture_file_contents =
		"{ viewmodel progs/v_shot.mdl held_scale 0.5 "
		"held_offset 1.5 1 10 muzzle_offset 0 0 10 "
		"enhanced_held_offset 11 12 13 enhanced_muzzle_offset 14 15 16 "
		"melee 1 melee_speed 0.75 "
		"muzzle_source_offset 2 3 4 muzzle_source_viewofs 0 }";
	assert(VR_WeaponCalibrationReloadGame());
	/* Without identified AD meshes the copied classic triple is not filtered;
	 * the other fields are always authored. */
	AssertClassicProfile("progs/v_shot.mdl", 1.5f, 1.0f, 10.0f,
		0.5f, 0.0f, 0.0f, 10.0f);
	assert(VR_WeaponCalibrationLookupHeld("progs/v_shot.mdl", true, held, &held_scale));
	AssertVector(held, 11.0f, 12.0f, 13.0f);
	assert(VR_WeaponCalibrationLookupMuzzle("progs/v_shot.mdl", true, muzzle));
	AssertVector(muzzle, 14.0f, 15.0f, 16.0f);
	assert(VR_WeaponCalibrationLookupMelee("progs/v_shot.mdl", &melee));
	assert(melee.has_speed && melee.speed == 0.75f);
	{
		const vec3_t angles = {0.0f, 0.0f, 0.0f};
		VR_WeaponCalibrationProjectileSourceOffset("progs/v_shot.mdl",
			IT_SHOTGUN, angles, 16.0f, muzzle);
		AssertVector(muzzle, 12.0f, -2.0f, 19.0f);
	}

	puts("VR weapon calibration reload fixture passed");
	return 0;
}
