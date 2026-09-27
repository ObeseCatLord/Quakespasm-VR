#include "../Quake/quakedef.h"
#include "../Quake/vr_weapon_calibration.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CALIBRATION_CVAR_COUNT \
	(VR_WEAPON_CALIBRATION_MAX_SLOTS * \
	 (VR_WEAPON_CALIBRATION_VARS_PER_WEAPON + \
	  VR_WEAPON_CALIBRATION_VARS_PER_MUZZLE))

client_state_t cl;

static cvar_t *registered_cvars[CALIBRATION_CVAR_COUNT];
static size_t registered_cvar_count;
static qmodel_t classic_model;
static qmodel_t switched_model;
static qmodel_t enhanced_model;
static aliashdr_t fixture_alias_header;
static aliashdr_t *fixture_alias_header_result = &fixture_alias_header;
static qmodel_t *last_checked_model;
static int last_checked_skin;
static unsigned int check_skin_calls;

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

void *Mod_Extradata_CheckSkin(qmodel_t *model, int skinnum)
{
	++check_skin_calls;
	last_checked_model = model;
	last_checked_skin = skinnum;
	return fixture_alias_header_result;
}

static void AssertVector(const vec3_t actual, float x, float y, float z)
{
	assert(fabsf(actual[0] - x) < 0.0001f);
	assert(fabsf(actual[1] - y) < 0.0001f);
	assert(fabsf(actual[2] - z) < 0.0001f);
}

static void SetPath(vr_weapon_schema_entry_t *entry, const char *path)
{
	assert(strlen(path) < sizeof(entry->viewmodel_path));
	strcpy(entry->viewmodel_path, path);
}

static void InitModel(qmodel_t *model, const char *path)
{
	memset(model, 0, sizeof(*model));
	assert(strlen(path) < sizeof(model->name));
	strcpy(model->name, path);
	model->type = mod_alias;
}

static void AssertInvalidMuzzle(void)
{
	vec3_t muzzle = {123.0f, 456.0f, 789.0f};
	assert(!VR_WeaponCalibrationCurrentMuzzle(muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 0.0f);
}

static void AssertGripMuzzle(void)
{
	vec3_t muzzle = {123.0f, 456.0f, 789.0f};
	assert(VR_WeaponCalibrationCurrentMuzzle(muzzle));
	AssertVector(muzzle, 0.0f, 0.0f, 0.0f);
}

static void AssertSelectedMuzzle(poseverttype_t pose_type,
								qmodel_t *expected_model, float x, float y, float z)
{
	vec3_t muzzle = {-1.0f, -1.0f, -1.0f};
	fixture_alias_header.poseverttype = pose_type;
	fixture_alias_header_result = &fixture_alias_header;
	assert(VR_WeaponCalibrationCurrentMuzzle(muzzle));
	AssertVector(muzzle, x, y, z);
	assert(last_checked_model == expected_model);
	assert(last_checked_skin == cl.viewent.skinnum);
}

int main(void)
{
	vr_weapon_schema_entry_t entries[3];
	unsigned int calls_before_rejection;

	VR_WeaponCalibrationInit();
	assert(registered_cvar_count == CALIBRATION_CVAR_COUNT);

	memset(entries, 0, sizeof(entries));
	SetPath(&entries[0], "progs/classic.mdl");
	entries[0].has_muzzle_offset = true;
	entries[0].muzzle_offset[0] = 1.0f;
	entries[0].muzzle_offset[1] = 2.0f;
	entries[0].muzzle_offset[2] = 3.0f;
	entries[0].has_mp_muzzle_offset = true;
	entries[0].mp_muzzle_offset[0] = 4.0f;
	entries[0].mp_muzzle_offset[1] = 5.0f;
	entries[0].mp_muzzle_offset[2] = 6.0f;

	SetPath(&entries[1], "progs/switched.mdl");
	entries[1].has_muzzle_offset = true;
	entries[1].muzzle_offset[0] = 7.0f;
	entries[1].muzzle_offset[1] = 8.0f;
	entries[1].muzzle_offset[2] = 9.0f;

	SetPath(&entries[2], "progs/enhanced.mdl");
	entries[2].has_enhanced_muzzle_offset = true;
	entries[2].enhanced_muzzle_offset[0] = 10.0f;
	entries[2].enhanced_muzzle_offset[1] = 20.0f;
	entries[2].enhanced_muzzle_offset[2] = 30.0f;
	entries[2].has_enhanced_mp_muzzle_offset = true;
	entries[2].enhanced_mp_muzzle_offset[0] = 1.0f;
	entries[2].enhanced_mp_muzzle_offset[1] = 2.0f;
	entries[2].enhanced_mp_muzzle_offset[2] = 3.0f;
	assert(VR_WeaponCalibrationApplySchema(entries, 3));

	InitModel(&classic_model, "progs/classic.mdl");
	InitModel(&switched_model, "progs/switched.mdl");
	InitModel(&enhanced_model, "progs/enhanced.mdl");
	cl.viewent.skinnum = 7;
	cl.maxclients = 1;
	cl.viewent.model = &classic_model;
	cl.stats[STAT_WEAPON] = 1;
	cl.model_precache[1] = &classic_model;
	AssertSelectedMuzzle(PV_QUAKE1, &classic_model, 1.0f, 2.0f, 3.0f);
	AssertSelectedMuzzle(PV_QUAKE3, &classic_model, 1.0f, 2.0f, 3.0f);

	/* The stat selects the new weapon before viewent.model has caught up. */
	cl.stats[STAT_WEAPON] = 2;
	cl.model_precache[2] = &switched_model;
	assert(cl.viewent.model == &classic_model);
	AssertSelectedMuzzle(PV_QUAKE1, &switched_model, 7.0f, 8.0f, 9.0f);

	cl.stats[STAT_WEAPON] = 3;
	cl.model_precache[3] = &enhanced_model;
	cl.maxclients = 2;
	AssertSelectedMuzzle(PV_MD5, &enhanced_model, 10.0f, 20.0f, 30.0f);
	AssertSelectedMuzzle(PV_MD5_8, &enhanced_model, 10.0f, 20.0f, 30.0f);

	/* Multiplayer uses the same authored muzzle as a local game. */
	cl.stats[STAT_WEAPON] = 1;
	AssertSelectedMuzzle(PV_QUAKE1, &classic_model, 1.0f, 2.0f, 3.0f);

	calls_before_rejection = check_skin_calls;
	cl.stats[STAT_WEAPON] = 0;
	AssertInvalidMuzzle();
	cl.stats[STAT_WEAPON] = -1;
	AssertInvalidMuzzle();
	cl.stats[STAT_WEAPON] = MAX_MODELS;
	AssertInvalidMuzzle();
	assert(check_skin_calls == calls_before_rejection);

	calls_before_rejection = check_skin_calls;
	cl.stats[STAT_WEAPON] = 4;
	cl.model_precache[4] = NULL;
	AssertInvalidMuzzle();
	cl.model_precache[4] = &classic_model;
	classic_model.needload = true;
	AssertInvalidMuzzle();
	classic_model.needload = false;
	classic_model.type = mod_brush;
	AssertInvalidMuzzle();
	classic_model.type = mod_alias;
	assert(check_skin_calls == calls_before_rejection);

	cl.stats[STAT_WEAPON] = 1;
	fixture_alias_header_result = NULL;
	AssertInvalidMuzzle();
	fixture_alias_header_result = &fixture_alias_header;
	fixture_alias_header.poseverttype = PV_SIZE;
	AssertInvalidMuzzle();

	strcpy(classic_model.name, "progs/missing.mdl");
	fixture_alias_header.poseverttype = PV_QUAKE1;
	AssertGripMuzzle();
	strcpy(classic_model.name, "progs/classic.mdl");

	Cvar_SetQuick(&vr_weapon_muzzle_offset[0], "nan");
	AssertInvalidMuzzle();
	Cvar_SetQuick(&vr_weapon_muzzle_offset[0], "1");

	puts("weapon model selector fixture passed");
	return 0;
}
