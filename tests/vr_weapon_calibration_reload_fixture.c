#include "vr_weapon_calibration.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CALIBRATION_CVAR_COUNT \
	(VR_WEAPON_CALIBRATION_MAX_SLOTS * \
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

byte *__wrap_COM_LoadFile(const char *path, unsigned int *path_id)
{
	++file_load_count;
	assert(!strcmp(path, "vr_weapons.txt"));
	assert(!path_id);
	if (!fixture_file_contents)
		return NULL;
	return (byte *)FixtureDuplicate(fixture_file_contents);
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

	for (size_t index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index)
		AssertClassicProfile(expected[index].path, expected[index].x,
			expected[index].y, expected[index].z, expected[index].scale,
			0.0f, 0.0f, expected[index].z);
}

static void AssertADRootProfiles(void)
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

static void AssertAD171AliasProfiles(void)
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

	/* Enyo defaults remain usable when a legacy MP overlay is present. */
	strcpy(com_gamedir, "/fixtures/enyo");
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 7 && file_free_count == 4);
	AssertEnyoFallbacks();

	fixture_file_contents =
		"global_mp_muzzle_offset 1 2 3 "
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

	/* AD root defaults are scoped to AD-derived game roots. */
	strcpy(com_gamedir, "/fixtures/ad");
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 9 && file_free_count == 5);
	AssertADRootProfiles();

	/* q30a1024 gets missing AD profiles; authored schema fields still win. */
	strcpy(com_gamedir, "/fixtures/q30a1024");
	fixture_file_contents =
		"{ viewmodel progs/v_shot.mdl held_scale 1 "
		"held_offset 9 8 7 muzzle_offset 6 5 4 }";
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 10 && file_free_count == 6);
	AssertClassicProfile("progs/v_shot.mdl", 9.0f, 8.0f, 7.0f, 1.0f,
		6.0f, 5.0f, 4.0f);
	AssertClassicProfile("progs/v_shot3.mdl", -3.5f, 0.4f, 8.5f, 0.8f,
		0.0f, 0.0f, 8.5f);
	AssertClassicProfile("progs/v_shadaxe5.mdl", -1.5f, 43.1f, 41.0f,
		0.25f, 0.0f, 0.0f, 41.0f);

	/* Only hash-confirmed Mjolnir ad171 copies receive canonical AD values. */
	strcpy(com_gamedir, "/fixtures/mjolnir");
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 11 && file_free_count == 6);
	AssertAD171AliasProfiles();
	assert(!VR_WeaponCalibrationLookupHeld(
		"progs/ad171/v_shot2.mdl", false, held, &held_scale));
	assert(!VR_WeaponCalibrationLookupHeld(
		"progs/ad171/v_nail.mdl", false, held, &held_scale));

	/* Mjolnir schema data overrides an alias and reload restores the baseline. */
	fixture_file_contents =
		"{ viewmodel progs/ad171/v_shot.mdl held_scale 0.6 "
		"held_offset 12 13 14 muzzle_offset 2 3 4 }";
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 12 && file_free_count == 7);
	AssertClassicProfile("progs/ad171/v_shot.mdl", 12.0f, 13.0f, 14.0f,
		0.6f, 2.0f, 3.0f, 4.0f);
	AssertClassicProfile("progs/ad171/v_rock.mdl", -3.0f, 1.25f,
		17.0f, 0.5f, 0.0f, 0.0f, 17.0f);
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 13 && file_free_count == 7);
	AssertClassicProfile("progs/ad171/v_shot.mdl", 1.5f, 1.7f, 17.5f,
		0.33f, 0.0f, 0.0f, 17.5f);

	/* These AD-derived games have byte-identical root viewmodel assets.
	 * Their own schema still overrides an individual AD default. */
	strcpy(com_gamedir, "/fixtures/gibtropolis");
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	AssertADRootProfiles();
	strcpy(com_gamedir, "/fixtures/hwjam4");
	fixture_file_contents =
		"{ viewmodel progs/v_shot.mdl held_scale 0.7 "
		"held_offset 1 2 3 muzzle_offset 4 5 6 }";
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_shot.mdl", 1.0f, 2.0f, 3.0f,
		0.7f, 4.0f, 5.0f, 6.0f);
	AssertClassicProfile("progs/v_shadaxe5.mdl", -1.5f, 43.1f, 41.0f,
		0.25f, 0.0f, 0.0f, 41.0f);

	puts("VR weapon calibration reload fixture passed");
	return 0;
}
