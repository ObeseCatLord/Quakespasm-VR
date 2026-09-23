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

/* Reload exercises the production built-in enhanced profiles in this fixture;
 * game-file loading/parsing is covered by the separate reload fixture. */
byte *COM_LoadFile(const char *path, unsigned int *path_id)
{
	(void)path;
	(void)path_id;
	return NULL;
}

void Mem_Free(const void *ptr)
{
	(void)ptr;
}

qboolean VR_WeaponSchemaParse(const char *text,
							  vr_weapon_schema_entry_t *entries,
							  size_t capacity, size_t *out_count)
{
	(void)text;
	(void)entries;
	(void)capacity;
	(void)out_count;
	abort();
	return false;
}

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

static void AssertVector(const vec3_t actual, float x, float y, float z)
{
	assert(fabsf(actual[0] - x) < 0.0001f);
	assert(fabsf(actual[1] - y) < 0.0001f);
	assert(fabsf(actual[2] - z) < 0.0001f);
}

static void AssertHeldFailure(const char *model_name,
							  qboolean enhanced_format,
							  qboolean multiplayer)
{
	vec3_t offset = {9.0f, 8.0f, 7.0f};
	float scale = 4.0f;
	assert(!VR_WeaponCalibrationLookupHeld(model_name, enhanced_format,
										   multiplayer, offset, &scale));
	AssertVector(offset, 0.0f, 0.0f, 0.0f);
	assert(scale == 1.0f);
}

static void SetPath(vr_weapon_schema_entry_t *entry, const char *path)
{
	assert(strlen(path) < sizeof(entry->viewmodel_path));
	strcpy(entry->viewmodel_path, path);
}

int main(void)
{
	vr_weapon_schema_entry_t entry;
	vr_weapon_schema_entry_t update;
	vr_weapon_schema_entry_t capacity[VR_WEAPON_CALIBRATION_MAX_SLOTS];
	vr_weapon_schema_entry_t too_many[VR_WEAPON_CALIBRATION_MAX_SLOTS + 1];
	vec3_t muzzle;
	vec3_t held;
	float held_scale;
	size_t index;

	VR_WeaponCalibrationInit();
	assert(registered_cvar_count == CALIBRATION_CVAR_COUNT);
	assert(!strcmp(vr_weapon_offset[0].name, "vr_wofs_x_01"));
	assert(!strcmp(vr_weapon_offset[4].name, "vr_wofs_id_01"));
	assert(!strcmp(vr_weapon_muzzle_offset[0].name, "vr_wmuzzle_x_01"));
	assert(!strcmp(vr_weapon_muzzle_offset[
										  (VR_WEAPON_CALIBRATION_MAX_SLOTS - 1) *
										  VR_WEAPON_CALIBRATION_VARS_PER_MUZZLE + 2]
					.name,
				"vr_wmuzzle_z_99"));
	assert(!strcmp(vr_weapon_offset[3].string, "1"));
	assert(!strcmp(vr_weapon_offset[4].string, "-1"));
	VR_WeaponCalibrationInit();
	assert(registered_cvar_count == CALIBRATION_CVAR_COUNT);

	memset(&entry, 0, sizeof(entry));
	SetPath(&entry, "progs/v_shot.mdl");
	entry.has_held_offset = true;
	entry.held_offset[0] = 2.0f;
	entry.held_offset[1] = 3.0f;
	entry.held_offset[2] = 10.0f;
	entry.has_held_scale = true;
	entry.held_scale = 0.5f;
	entry.has_mp_held_offset = true;
	entry.mp_held_offset[0] = 1.0f;
	entry.mp_held_offset[1] = -2.0f;
	entry.mp_held_offset[2] = 3.0f;
	entry.has_schema_mp_held_offset = true;
	memcpy(entry.schema_mp_held_offset, entry.mp_held_offset, sizeof(vec3_t));
	entry.has_enhanced_held_offset = true;
	entry.enhanced_held_offset[0] = 5.0f;
	entry.enhanced_held_offset[1] = 6.0f;
	entry.enhanced_held_offset[2] = 7.0f;
	entry.has_enhanced_mp_held_offset = true;
	entry.enhanced_mp_held_offset[0] = 2.0f;
	entry.enhanced_mp_held_offset[1] = 3.0f;
	entry.enhanced_mp_held_offset[2] = 4.0f;
	entry.has_muzzle_offset = true;
	entry.muzzle_offset[0] = 3.0f;
	entry.muzzle_offset[1] = 4.0f;
	entry.muzzle_offset[2] = 10.0f;
	entry.has_mp_muzzle_offset = true;
	entry.mp_muzzle_offset[0] = 1.0f;
	entry.mp_muzzle_offset[1] = 2.0f;
	entry.mp_muzzle_offset[2] = 3.0f;
	entry.has_schema_mp_muzzle_offset = true;
	entry.schema_mp_muzzle_offset[0] = 0.25f;
	entry.schema_mp_muzzle_offset[1] = 0.5f;
	entry.schema_mp_muzzle_offset[2] = 1.0f;
	entry.has_enhanced_muzzle_offset = true;
	entry.enhanced_muzzle_offset[0] = 0.0f;
	entry.enhanced_muzzle_offset[1] = 0.0f;
	entry.enhanced_muzzle_offset[2] = 20.0f;
	entry.has_enhanced_mp_muzzle_offset = true;
	entry.enhanced_mp_muzzle_offset[0] = 1.0f;
	entry.enhanced_mp_muzzle_offset[1] = 2.0f;
	entry.enhanced_mp_muzzle_offset[2] = 3.0f;
	assert(VR_WeaponCalibrationApplySchema(&entry, 1));
	assert(VR_WeaponCalibrationLookupHeld("progs/v_shot.mdl", false, false,
										  held, &held_scale));
	AssertVector(held, 2.0f, 3.0f, 10.0f);
	assert(held_scale == 0.5f);
	assert(VR_WeaponCalibrationLookupHeld("progs/v_shot.mdl", false, true,
										  held, &held_scale));
	AssertVector(held, 3.0f, 1.0f, 13.0f);
	assert(held_scale == 0.5f);
	assert(VR_WeaponCalibrationLookupHeld("progs/v_shot.mdl", true, false,
										  held, &held_scale));
	AssertVector(held, 5.0f, 6.0f, 7.0f);
	assert(held_scale == 1.0f); /* Enhanced neutral scale ignores classic. */
	assert(VR_WeaponCalibrationLookupHeld("progs/v_shot.mdl", true, true,
										  held, &held_scale));
	AssertVector(held, 7.0f, 9.0f, 11.0f);
	assert(held_scale == 1.0f);
	assert(VR_WeaponCalibrationLookupMuzzle("progs/v_shot.mdl", false,
										 false, muzzle));
	AssertVector(muzzle, 3.0f, 4.0f, 10.0f); /* held scale is independent */
	assert(VR_WeaponCalibrationLookupMuzzle("progs/v_shot.mdl", false, true,
										 muzzle));
	AssertVector(muzzle, 4.0f, 6.0f, 13.0f);
	assert(VR_WeaponCalibrationLookupMuzzle("progs/v_shot.mdl", true, false,
										 muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 20.0f);
	assert(VR_WeaponCalibrationLookupMuzzle("progs/v_shot.mdl", true, true,
										 muzzle));
	AssertVector(muzzle, 1.0f, 2.0f, 23.0f);

	Cvar_SetQuick(&vr_weapon_muzzle_offset[0], "9");
	Cvar_SetQuick(&vr_weapon_offset[0], "-4.25");
	Cvar_SetQuick(&vr_weapon_offset[1], "2.75");
	Cvar_SetQuick(&vr_weapon_offset[2], "18.5");
	Cvar_SetQuick(&vr_weapon_offset[3], "1.25");
	assert(VR_WeaponCalibrationLookupHeld("progs/v_shot.mdl", false, false,
										  held, &held_scale));
	AssertVector(held, -4.25f, 2.75f, 18.5f);
	assert(held_scale == 1.25f);
	assert(VR_WeaponCalibrationLookupHeld("progs/v_shot.mdl", false, true,
										  held, &held_scale));
	AssertVector(held, -3.25f, 0.75f, 21.5f);
	assert(held_scale == 1.25f);
	Cvar_SetQuick(&vr_weapon_offset[0], "2");
	Cvar_SetQuick(&vr_weapon_offset[1], "3");
	Cvar_SetQuick(&vr_weapon_offset[2], "10");
	Cvar_SetQuick(&vr_weapon_offset[3], "0.5");
	assert(VR_WeaponCalibrationLookupMuzzle("progs/v_shot.mdl", false,
										 false, muzzle));
	AssertVector(muzzle, 9.0f, 4.0f, 10.0f);
	Cvar_SetQuick(&vr_weapon_offset[4], "progs/v_rekeyed.mdl");
	assert(!VR_WeaponCalibrationLookupMuzzle("progs/v_shot.mdl", false,
										  false, muzzle));
	assert(VR_WeaponCalibrationLookupMuzzle("progs/v_rekeyed.mdl", false,
										 false, muzzle));
	AssertVector(muzzle, 9.0f, 4.0f, 10.0f);
	Cvar_SetQuick(&vr_weapon_offset[4], "progs/v_shot.mdl");

	/* Reapplying effective MP data replaces it; it never adds the global sum twice. */
	assert(VR_WeaponCalibrationApplySchema(&entry, 1));
	assert(VR_WeaponCalibrationLookupMuzzle("progs/v_shot.mdl", false, true,
										 muzzle));
	AssertVector(muzzle, 4.0f, 6.0f, 13.0f);

	memset(&update, 0, sizeof(update));
	SetPath(&update, "progs/v_shot.mdl");
	update.has_held_offset = true;
	update.held_offset[0] = 7.0f;
	update.held_offset[1] = 8.0f;
	update.held_offset[2] = 9.0f;
	assert(VR_WeaponCalibrationApplySchema(&update, 1));
	assert(!strcmp(vr_weapon_offset[4].string, "progs/v_shot.mdl"));
	assert(fabsf(vr_weapon_offset[0].value - 7.0f) < 0.0001f);
	assert(fabsf(vr_weapon_offset[1].value - 8.0f) < 0.0001f);
	assert(fabsf(vr_weapon_offset[2].value - 9.0f) < 0.0001f);
	assert(fabsf(vr_weapon_offset[3].value - 0.5f) < 0.0001f);
	assert(VR_WeaponCalibrationLookupMuzzle("progs/v_shot.mdl", true, false,
										 muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 20.0f);

	memset(&update, 0, sizeof(update));
	SetPath(&update, "progs/v_enhanced_only.mdl");
	update.has_enhanced_muzzle_offset = true;
	update.enhanced_muzzle_offset[2] = 17.0f;
	assert(VR_WeaponCalibrationApplySchema(&update, 1));
	assert(!VR_WeaponCalibrationLookupMuzzle("progs/v_enhanced_only.mdl", false,
										  false, muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 0.0f);
	assert(VR_WeaponCalibrationLookupMuzzle("progs/v_enhanced_only.mdl", true,
										 false, muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 17.0f);

	memset(&update, 0, sizeof(update));
	SetPath(&update, "progs/fallback.mdl");
	update.has_held_offset = true;
	update.held_offset[0] = 2.0f;
	update.held_offset[1] = 4.0f;
	update.held_offset[2] = 11.0f;
	update.has_held_scale = true;
	update.held_scale = 0.25f;
	assert(VR_WeaponCalibrationApplySchema(&update, 1));
	assert(VR_WeaponCalibrationLookupMuzzle("progs/fallback.mdl", false,
										 false, muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 11.0f);

	memset(&update, 0, sizeof(update));
	SetPath(&update, "progs/fallback.mdl");
	update.has_muzzle_offset = true;
	update.muzzle_offset[0] = 1.0f;
	update.muzzle_offset[1] = 2.0f;
	update.muzzle_offset[2] = 30.0f;
	assert(VR_WeaponCalibrationApplySchema(&update, 1));
	memset(&update, 0, sizeof(update));
	SetPath(&update, "progs/fallback.mdl");
	update.has_held_offset = true;
	update.held_offset[2] = 44.0f;
	assert(VR_WeaponCalibrationApplySchema(&update, 1));
	assert(VR_WeaponCalibrationLookupMuzzle("progs/fallback.mdl", false,
										 false, muzzle));
	AssertVector(muzzle, 1.0f, 2.0f, 30.0f);

	memset(&update, 0, sizeof(update));
	SetPath(&update, "progs/generic-offset.mdl");
	update.has_offset = true;
	update.offset[2] = 99.0f;
	assert(VR_WeaponCalibrationApplySchema(&update, 1));
	assert(!VR_WeaponCalibrationLookupMuzzle("progs/generic-offset.mdl", false,
										  false, muzzle));

	Cvar_SetQuick(&vr_weapon_muzzle_offset[0], "nan");
	assert(!VR_WeaponCalibrationLookupMuzzle("progs/v_shot.mdl", false,
										 false, muzzle));
	Cvar_SetQuick(&vr_weapon_muzzle_offset[0], "9");
	AssertHeldFailure("progs/not-registered.mdl", false, false);
	Cvar_SetQuick(&vr_weapon_offset[3], "nan");
	AssertHeldFailure("progs/v_shot.mdl", false, false);
	Cvar_SetQuick(&vr_weapon_offset[3], "0");
	AssertHeldFailure("progs/v_shot.mdl", false, false);
	Cvar_SetQuick(&vr_weapon_offset[3], "-1");
	AssertHeldFailure("progs/v_shot.mdl", false, false);
	Cvar_SetQuick(&vr_weapon_offset[3], "0.5");

	/* Invalid input and capacity failures are rejected before publishing slots. */
	update.has_muzzle_offset = true;
	update.muzzle_offset[0] = NAN;
	assert(!VR_WeaponCalibrationApplySchema(&update, 1));
	assert(!strcmp(vr_weapon_offset[4].string, "progs/v_shot.mdl"));
	memset(too_many, 0, sizeof(too_many));
	for (index = 0; index < sizeof(too_many) / sizeof(too_many[0]); ++index)
	{
		char path[64];
		SetPath(&too_many[index], "");
		snprintf(path, sizeof(path), "test/%zu.mdl", index);
		SetPath(&too_many[index], path);
		too_many[index].has_held_offset = true;
	}
	assert(!VR_WeaponCalibrationApplySchema(too_many,
										 sizeof(too_many) / sizeof(too_many[0])));
	assert(!VR_WeaponCalibrationLookupMuzzle("test/0.mdl", false, false,
										 muzzle));

	VR_WeaponCalibrationReset();
	VR_WeaponCalibrationReset();
	assert(registered_cvar_count == CALIBRATION_CVAR_COUNT);
	assert(!strcmp(vr_weapon_offset[4].string, "-1"));
	assert(!VR_WeaponCalibrationLookupMuzzle("progs/v_shot.mdl", false, false,
										 muzzle));
	assert(VR_WeaponCalibrationReloadGame());
	assert(VR_WeaponCalibrationLookupHeld("progs/v_axe.mdl", true, false,
										  held, &held_scale));
	AssertVector(held, -19.72028f, 12.02455f, 23.92703f);
	assert(held_scale == 1.0f);
	memset(&update, 0, sizeof(update));
	SetPath(&update, "progs/neutral-only.mdl");
	update.has_enhanced_muzzle_offset = true;
	update.enhanced_muzzle_offset[2] = 8.0f;
	assert(VR_WeaponCalibrationApplySchema(&update, 1));
	assert(VR_WeaponCalibrationLookupHeld("progs/neutral-only.mdl", true,
		false, held, &held_scale));
	AssertVector(held, 0.0f, 0.0f, 0.0f);
	assert(held_scale == 1.0f);
	VR_WeaponCalibrationReset();
	for (index = 0; index < VR_WEAPON_CALIBRATION_MAX_SLOTS; ++index)
	{
		char path[64];
		snprintf(path, sizeof(path), "slot/%zu.mdl", index);
		memset(&capacity[index], 0, sizeof(capacity[index]));
		SetPath(&capacity[index], path);
		capacity[index].has_muzzle_offset = true;
	}
	assert(VR_WeaponCalibrationApplySchema(capacity,
										 sizeof(capacity) / sizeof(capacity[0])));
	assert(!strcmp(vr_weapon_offset[
										  (VR_WEAPON_CALIBRATION_MAX_SLOTS - 1) *
										  VR_WEAPON_CALIBRATION_VARS_PER_WEAPON + 4]
					.string,
				"slot/98.mdl"));
	memset(&update, 0, sizeof(update));
	SetPath(&update, "slot/overflow.mdl");
	update.has_muzzle_offset = true;
	assert(!VR_WeaponCalibrationApplySchema(&update, 1));
	assert(VR_WeaponCalibrationLookupMuzzle("slot/0.mdl", false, false,
										 muzzle));
	assert(!VR_WeaponCalibrationLookupMuzzle("slot/overflow.mdl", false,
										 false, muzzle));

	puts("VR weapon calibration fixture passed");
	return 0;
}
