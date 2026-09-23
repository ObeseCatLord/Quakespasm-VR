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
		assert(VR_WeaponCalibrationLookupMuzzle(expected[index].path, true,
											false, muzzle));
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
		assert(VR_WeaponCalibrationLookupHeld(expected[index].path, false,
											 false, held, &scale));
		AssertVector(held, expected[index].x, expected[index].y,
				 expected[index].z);
		assert(fabsf(scale - expected[index].scale) < 0.0001f);
		assert(VR_WeaponCalibrationLookupMuzzle(expected[index].path, false,
												 false, muzzle));
		AssertVector(muzzle, 0.0f, 0.0f, expected[index].z);
	}
}

int main(void)
{
	vec3_t muzzle;
	vec3_t held;
	float held_scale;

	strcpy(com_gamedir, "/fixtures/id1");

	/* A missing file still publishes the eight enhanced fallback profiles. */
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 1 && file_free_count == 0);
	assert(registered_cvar_count == CALIBRATION_CVAR_COUNT);
	AssertFallbackMuzzles();
	assert(!VR_WeaponCalibrationLookupHeld(
		"progs/ee_v_sword.mdl", false, false, held, &held_scale));

	/* Authored classic fields override only themselves; enhanced fallback stays. */
	fixture_file_contents =
		"{ model progs/v_shot.mdl muzzle_offset 1 2 3 } "
		"{ model progs/custom.mdl muzzle_offset 4 5 6 }";
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 2 && file_free_count == 1);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/v_shot.mdl", false, false, muzzle));
	AssertVector(muzzle, 1.0f, 2.0f, 3.0f);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/v_shot.mdl", true, false, muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 10.0f);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/custom.mdl", false, false, muzzle));
	AssertVector(muzzle, 4.0f, 5.0f, 6.0f);

	/* Held-only schema data seeds classic muzzle Z on enhanced fallback slots. */
	fixture_file_contents =
		"{ model progs/v_shot.mdl held_offset 1 2 11 }";
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 3 && file_free_count == 2);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/v_shot.mdl", false, false, muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 11.0f);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/v_shot.mdl", true, false, muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 10.0f);

	/* A later reload replaces the prior file and applies authored enhanced data. */
	fixture_file_contents =
		"{ model progs/v_shot.mdl enhanced_muzzle_offset 7 8 9 }";
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 4 && file_free_count == 3);
	assert(!VR_WeaponCalibrationLookupMuzzle(
		"progs/custom.mdl", false, false, muzzle));
	assert(!VR_WeaponCalibrationLookupMuzzle(
		"progs/v_shot.mdl", false, false, muzzle));
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/v_shot.mdl", true, false, muzzle));
	AssertVector(muzzle, 7.0f, 8.0f, 9.0f);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/v_axe.mdl", true, false, muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 37.0f);

	/* Invalid input is rejected while the known-good built-ins remain active. */
	fixture_file_contents =
		"{ model progs/v_shot.mdl muzzle_offset 1 2 not-a-number }";
	assert(!VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 5 && file_free_count == 4);
	AssertFallbackMuzzles();
	assert(!VR_WeaponCalibrationLookupMuzzle(
		"progs/custom.mdl", false, false, muzzle));
	assert(!strcmp(vr_weapon_offset[4].string, "progs/v_axe.mdl"));

	/* Missing-file reload discards the old game profile and restores defaults. */
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	assert(file_load_count == 6 && file_free_count == 4);
	AssertFallbackMuzzles();

	/* Enyo classic defaults precede identity-only schema entries and MP overlay. */
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
		"progs/ee_v_sword.mdl", false, false, held, &held_scale));
	AssertVector(held, 25.0f, 49.0f, 60.0f);
	assert(fabsf(held_scale - 0.2f) < 0.0001f);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/ee_v_sword.mdl", false, false, muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 60.0f);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/ee_v_sword.mdl", false, true, muzzle));
	AssertVector(muzzle, 1.0f, 2.0f, 63.0f);
	assert(VR_WeaponCalibrationLookupHeld(
		"progs/ee_v_pistol.mdl", false, false, held, &held_scale));
	AssertVector(held, 1.0f, 2.0f, 3.0f);
	assert(fabsf(held_scale - 0.5f) < 0.0001f);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/ee_v_pistol.mdl", false, false, muzzle));
	AssertVector(muzzle, 9.0f, 10.0f, 11.0f);
	assert(VR_WeaponCalibrationLookupMuzzle(
		"progs/ee_v_pistol.mdl", false, true, muzzle));
	AssertVector(muzzle, 10.0f, 12.0f, 14.0f);

	puts("VR weapon calibration reload fixture passed");
	return 0;
}
