#include "quakedef.h"
#include "vr_locomotion.h"
#include "vr_weapon_calibration.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum
{
	VR_WOFS_X,
	VR_WOFS_Y,
	VR_WOFS_Z,
	VR_WOFS_SCALE,
	VR_WOFS_ID
};

enum
{
	VR_WMUZZLE_X,
	VR_WMUZZLE_Y,
	VR_WMUZZLE_Z
};

typedef struct
{
	qboolean has_muzzle_offset;
	qboolean has_mp_muzzle_offset;
	vec3_t mp_muzzle_offset; /* Effective authored + global overlay. */
	qboolean has_schema_mp_muzzle_offset;
	vec3_t schema_mp_muzzle_offset; /* Per-weapon authored portion. */
	qboolean has_enhanced_muzzle_offset;
	vec3_t enhanced_muzzle_offset;
	qboolean has_enhanced_mp_muzzle_offset;
	vec3_t enhanced_mp_muzzle_offset;
	qboolean has_mp_held_offset;
	vec3_t mp_held_offset;
	qboolean has_schema_mp_held_offset;
	vec3_t schema_mp_held_offset;
	qboolean has_enhanced_held_offset;
	vec3_t enhanced_held_offset;
	qboolean has_enhanced_mp_held_offset;
	vec3_t enhanced_mp_held_offset;
	qboolean has_muzzle_source_offset;
	vec3_t muzzle_source_offset;
	qboolean has_muzzle_source_viewofs;
	qboolean muzzle_source_viewofs;
	qboolean has_spawn_at_self_origin;
	qboolean spawn_at_self_origin;
} vr_weapon_calibration_slot_t;

cvar_t vr_weapon_offset[VR_WEAPON_CALIBRATION_MAX_SLOTS *
						VR_WEAPON_CALIBRATION_VARS_PER_WEAPON];
cvar_t vr_weapon_muzzle_offset[VR_WEAPON_CALIBRATION_MAX_SLOTS *
								VR_WEAPON_CALIBRATION_VARS_PER_MUZZLE];

static vr_weapon_calibration_slot_t
	vr_weapon_calibration_slots[VR_WEAPON_CALIBRATION_MAX_SLOTS];
static char vr_weapon_calibration_cvar_names[VR_WEAPON_CALIBRATION_MAX_SLOTS]
													[VR_WEAPON_CALIBRATION_VARS_PER_WEAPON +
													 VR_WEAPON_CALIBRATION_VARS_PER_MUZZLE]
													[24];
static qboolean vr_weapon_calibration_initialized;

/* Enhanced viewmodel fallbacks from the 2021 rerelease calibration. */
static const vr_weapon_schema_entry_t vr_enhanced_weapon_fallbacks[] = {
	{
		.viewmodel_path = "progs/v_axe.mdl",
		.enhanced_held_offset = {-19.72028f, 12.02455f, 23.92703f},
		.has_enhanced_held_offset = true,
		.enhanced_muzzle_offset = {0.0f, 0.0f, 37.0f},
		.has_enhanced_muzzle_offset = true,
	},
	{
		.viewmodel_path = "progs/v_shot.mdl",
		.enhanced_held_offset = {-5.718559f, 0.381319f, 7.430882f},
		.has_enhanced_held_offset = true,
		.enhanced_muzzle_offset = {0.0f, 0.0f, 10.0f},
		.has_enhanced_muzzle_offset = true,
	},
	{
		.viewmodel_path = "progs/v_shot2.mdl",
		.enhanced_held_offset = {-7.427664f, 0.3130722f, 6.934306f},
		.has_enhanced_held_offset = true,
		.enhanced_muzzle_offset = {0.0f, 0.0f, 8.5f},
		.has_enhanced_muzzle_offset = true,
	},
	{
		.viewmodel_path = "progs/v_nail.mdl",
		.enhanced_held_offset = {-12.55625f, 0.4279174f, 13.18545f},
		.has_enhanced_held_offset = true,
		.enhanced_muzzle_offset = {0.0f, 0.0f, 15.0f},
		.has_enhanced_muzzle_offset = true,
	},
	{
		.viewmodel_path = "progs/v_nail2.mdl",
		.enhanced_held_offset = {-18.29596f, 0.3225229f, 14.23901f},
		.has_enhanced_held_offset = true,
		.enhanced_muzzle_offset = {0.0f, 0.0f, 19.0f},
		.has_enhanced_muzzle_offset = true,
	},
	{
		.viewmodel_path = "progs/v_rock.mdl",
		.enhanced_held_offset = {-10.28861f, -0.03083239f, 10.17257f},
		.has_enhanced_held_offset = true,
		.enhanced_muzzle_offset = {0.0f, 0.0f, 13.0f},
		.has_enhanced_muzzle_offset = true,
	},
	{
		.viewmodel_path = "progs/v_rock2.mdl",
		.enhanced_held_offset = {-16.54977f, -0.157425f, 11.40173f},
		.has_enhanced_held_offset = true,
		.enhanced_muzzle_offset = {0.0f, 0.0f, 19.0f},
		.has_enhanced_muzzle_offset = true,
	},
	{
		.viewmodel_path = "progs/v_light.mdl",
		.enhanced_held_offset = {-8.610199f, -0.4010587f, 10.71593f},
		.has_enhanced_held_offset = true,
		.enhanced_muzzle_offset = {0.0f, 0.0f, 13.0f},
		.has_enhanced_muzzle_offset = true,
	},
};

/* Classic viewmodel calibration from the donor's Enyo InitWeaponCVars. */
static const vr_weapon_schema_entry_t vr_enyo_weapon_fallbacks[] = {
	{
		.viewmodel_path = "progs/ee_v_sword.mdl",
		.held_offset = {25.0f, 49.0f, 60.0f},
		.has_held_offset = true,
		.held_scale = 0.2f,
		.has_held_scale = true,
	},
	{
		.viewmodel_path = "progs/ee_v_pistol.mdl",
		.held_offset = {12.0f, 24.0f, 29.0f},
		.has_held_offset = true,
		.held_scale = 0.2f,
		.has_held_scale = true,
	},
	{
		.viewmodel_path = "progs/ee_v_sgun.mdl",
		.held_offset = {-2.3f, 21.3f, 35.3f},
		.has_held_offset = true,
		.held_scale = 0.2f,
		.has_held_scale = true,
	},
	{
		.viewmodel_path = "progs/ee_v_smgs.mdl",
		.held_offset = {3.5f, 24.6f, 29.8f},
		.has_held_offset = true,
		.held_scale = 0.2f,
		.has_held_scale = true,
	},
	{
		.viewmodel_path = "progs/ee_v_plasma.mdl",
		.held_offset = {-1.5f, 21.8f, 36.0f},
		.has_held_offset = true,
		.held_scale = 0.2f,
		.has_held_scale = true,
	},
	{
		.viewmodel_path = "progs/ee_v_glaunch.mdl",
		.held_offset = {-3.8f, 24.0f, 35.5f},
		.has_held_offset = true,
		.held_scale = 0.2f,
		.has_held_scale = true,
	},
	{
		.viewmodel_path = "progs/ee_v_rlaunch.mdl",
		.held_offset = {4.0f, 28.5f, 40.5f},
		.has_held_offset = true,
		.held_scale = 0.2f,
		.has_held_scale = true,
	},
	{
		.viewmodel_path = "progs/ee_v_railgun.mdl",
		.held_offset = {-1.0f, 22.5f, 34.5f},
		.has_held_offset = true,
		.held_scale = 0.2f,
		.has_held_scale = true,
	},
	{
		.viewmodel_path = "progs/ee_v_av72.mdl",
		.held_offset = {0.5f, 24.0f, 38.5f},
		.has_held_offset = true,
		.held_scale = 0.2f,
		.has_held_scale = true,
	},
	{
		.viewmodel_path = "progs/ee_v_legal.mdl",
		.held_offset = {0.0f, 55.0f, 29.0f},
		.has_held_offset = true,
		.held_scale = 0.2f,
		.has_held_scale = true,
	},
};

#define VR_WeaponOffsetCvar(slot, field) \
	vr_weapon_offset[(slot) * VR_WEAPON_CALIBRATION_VARS_PER_WEAPON + (field)]
#define VR_WeaponMuzzleCvar(slot, field) \
	vr_weapon_muzzle_offset[(slot) * VR_WEAPON_CALIBRATION_VARS_PER_MUZZLE + \
							 (field)]

static qboolean VR_CalibrationVectorIsFinite(const vec3_t vector)
{
	return isfinite(vector[0]) && isfinite(vector[1]) && isfinite(vector[2]);
}

static qboolean VR_CalibrationEntryHasFields(
	const vr_weapon_schema_entry_t *entry)
{
	return entry->has_held_offset || entry->has_held_scale ||
		entry->has_mp_held_offset || entry->has_muzzle_offset ||
		entry->has_mp_muzzle_offset || entry->has_muzzle_source_offset ||
		entry->has_muzzle_source_viewofs || entry->has_spawn_at_self_origin ||
		entry->has_enhanced_held_offset ||
		entry->has_enhanced_mp_held_offset ||
		entry->has_enhanced_muzzle_offset ||
		entry->has_enhanced_mp_muzzle_offset;
}

static qboolean VR_CalibrationEntryIsFinite(
	const vr_weapon_schema_entry_t *entry)
{
	if ((entry->has_held_offset &&
		 !VR_CalibrationVectorIsFinite(entry->held_offset)) ||
		(entry->has_held_scale && !isfinite(entry->held_scale)) ||
		(entry->has_mp_held_offset &&
		 !VR_CalibrationVectorIsFinite(entry->mp_held_offset)) ||
		(entry->has_schema_mp_held_offset &&
		 !VR_CalibrationVectorIsFinite(entry->schema_mp_held_offset)) ||
		(entry->has_muzzle_offset &&
		 !VR_CalibrationVectorIsFinite(entry->muzzle_offset)) ||
		(entry->has_mp_muzzle_offset &&
		 !VR_CalibrationVectorIsFinite(entry->mp_muzzle_offset)) ||
		(entry->has_schema_mp_muzzle_offset &&
		 !VR_CalibrationVectorIsFinite(entry->schema_mp_muzzle_offset)) ||
		(entry->has_muzzle_source_offset &&
		 !VR_CalibrationVectorIsFinite(entry->muzzle_source_offset)) ||
		(entry->has_enhanced_held_offset &&
		 !VR_CalibrationVectorIsFinite(entry->enhanced_held_offset)) ||
		(entry->has_enhanced_mp_held_offset &&
		 !VR_CalibrationVectorIsFinite(entry->enhanced_mp_held_offset)) ||
		(entry->has_enhanced_muzzle_offset &&
		 !VR_CalibrationVectorIsFinite(entry->enhanced_muzzle_offset)) ||
		(entry->has_enhanced_mp_muzzle_offset &&
		 !VR_CalibrationVectorIsFinite(entry->enhanced_mp_muzzle_offset)))
		return false;

	if ((entry->has_schema_mp_held_offset && !entry->has_mp_held_offset) ||
		(entry->has_schema_mp_muzzle_offset && !entry->has_mp_muzzle_offset))
		return false;

	return true;
}

static void VR_RegisterCalibrationCvar(cvar_t *cvar, char *name,
									   const char *initial_value)
{
	memset(cvar, 0, sizeof(*cvar));
	cvar->name = name;
	cvar->string = initial_value;
	cvar->flags = CVAR_NONE;
	Cvar_RegisterVariable(cvar);
}

static int VR_FindCalibrationSlot(const char *viewmodel_path)
{
	int slot;

	for (slot = 0; slot < VR_WEAPON_CALIBRATION_MAX_SLOTS; ++slot)
	{
		const char *slot_id = VR_WeaponOffsetCvar(slot, VR_WOFS_ID).string;
		if (slot_id && !strcmp(slot_id, viewmodel_path))
			return slot;
	}
	return -1;
}

static int VR_FindFreeCalibrationSlot(void)
{
	int slot;

	for (slot = 0; slot < VR_WEAPON_CALIBRATION_MAX_SLOTS; ++slot)
	{
		const char *slot_id = VR_WeaponOffsetCvar(slot, VR_WOFS_ID).string;
		if (!slot_id || !slot_id[0] || !strcmp(slot_id, "-1"))
			return slot;
	}
	return -1;
}

static qboolean VR_CalibrationEntryHasHeldFields(
	const vr_weapon_schema_entry_t *entry)
{
	return entry->has_held_offset || entry->has_held_scale ||
		entry->has_mp_held_offset;
}

static void VR_SetCalibrationSlotDefaults(int slot)
{
	Cvar_SetQuick(&VR_WeaponOffsetCvar(slot, VR_WOFS_X), "0");
	Cvar_SetQuick(&VR_WeaponOffsetCvar(slot, VR_WOFS_Y), "0");
	Cvar_SetQuick(&VR_WeaponOffsetCvar(slot, VR_WOFS_Z), "0");
	Cvar_SetQuick(&VR_WeaponOffsetCvar(slot, VR_WOFS_SCALE), "1");
	Cvar_SetQuick(&VR_WeaponOffsetCvar(slot, VR_WOFS_ID), "-1");
	Cvar_SetQuick(&VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_X), "0");
	Cvar_SetQuick(&VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Y), "0");
	Cvar_SetQuick(&VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Z), "0");
}

static void VR_ActivateCalibrationSlot(int slot, const char *viewmodel_path)
{
	memset(&vr_weapon_calibration_slots[slot], 0,
		   sizeof(vr_weapon_calibration_slots[slot]));
	VR_SetCalibrationSlotDefaults(slot);
	Cvar_SetQuick(&VR_WeaponOffsetCvar(slot, VR_WOFS_ID), viewmodel_path);
}

void VR_WeaponCalibrationInit(void)
{
	static const char *held_suffixes[VR_WEAPON_CALIBRATION_VARS_PER_WEAPON] = {
		"vr_wofs_x_%02d", "vr_wofs_y_%02d", "vr_wofs_z_%02d",
		"vr_wofs_scale_%02d", "vr_wofs_id_%02d"};
	static const char *muzzle_suffixes[VR_WEAPON_CALIBRATION_VARS_PER_MUZZLE] = {
		"vr_wmuzzle_x_%02d", "vr_wmuzzle_y_%02d", "vr_wmuzzle_z_%02d"};
	int slot;

	if (vr_weapon_calibration_initialized)
		return;

	memset(vr_weapon_calibration_slots, 0,
		   sizeof(vr_weapon_calibration_slots));
	for (slot = 0; slot < VR_WEAPON_CALIBRATION_MAX_SLOTS; ++slot)
	{
		int field;
		for (field = 0; field < VR_WEAPON_CALIBRATION_VARS_PER_WEAPON; ++field)
		{
			char *name = vr_weapon_calibration_cvar_names[slot][field];
			snprintf(name, 24, held_suffixes[field], slot + 1);
			VR_RegisterCalibrationCvar(
				&VR_WeaponOffsetCvar(slot, field), name,
				field == VR_WOFS_SCALE ? "1" :
				field == VR_WOFS_ID ? "-1" : "0");
		}
		for (field = 0; field < VR_WEAPON_CALIBRATION_VARS_PER_MUZZLE;
			 ++field)
		{
			char *name = vr_weapon_calibration_cvar_names[slot]
												 [VR_WEAPON_CALIBRATION_VARS_PER_WEAPON +
												  field];
			snprintf(name, 24, muzzle_suffixes[field], slot + 1);
			VR_RegisterCalibrationCvar(
				&VR_WeaponMuzzleCvar(slot, field), name, "0");
		}
	}
	vr_weapon_calibration_initialized = true;
}

void VR_WeaponCalibrationReset(void)
{
	int slot;

	memset(vr_weapon_calibration_slots, 0,
		   sizeof(vr_weapon_calibration_slots));
	if (!vr_weapon_calibration_initialized)
		return;

	for (slot = 0; slot < VR_WEAPON_CALIBRATION_MAX_SLOTS; ++slot)
		VR_SetCalibrationSlotDefaults(slot);
}

qboolean VR_WeaponCalibrationApplySchema(
	const vr_weapon_schema_entry_t *entries, size_t count)
{
	size_t index;
	int new_slots_needed = 0;
	int free_slots = 0;
	int slot;

	if (!vr_weapon_calibration_initialized || count >
		VR_WEAPON_CALIBRATION_MAX_SLOTS || (count && !entries))
		return false;

	for (slot = 0; slot < VR_WEAPON_CALIBRATION_MAX_SLOTS; ++slot)
	{
		const char *slot_id = VR_WeaponOffsetCvar(slot, VR_WOFS_ID).string;
		if (!slot_id || !slot_id[0] || !strcmp(slot_id, "-1"))
			++free_slots;
	}

	for (index = 0; index < count; ++index)
	{
		const vr_weapon_schema_entry_t *entry = &entries[index];
		size_t earlier;

		if (!VR_CalibrationEntryHasFields(entry) ||
			!entry->viewmodel_path[0])
			continue;
		if (!memchr(entry->viewmodel_path, '\0',
					sizeof(entry->viewmodel_path)) ||
			!VR_CalibrationEntryIsFinite(entry))
			return false;

		if (VR_FindCalibrationSlot(entry->viewmodel_path) >= 0)
			continue;
		for (earlier = 0; earlier < index; ++earlier)
		{
			if (VR_CalibrationEntryHasFields(&entries[earlier]) &&
				entries[earlier].viewmodel_path[0] &&
				!strcmp(entries[earlier].viewmodel_path,
						entry->viewmodel_path))
				break;
		}
		if (earlier == index)
			++new_slots_needed;
	}

	if (new_slots_needed > free_slots)
		return false;

	for (index = 0; index < count; ++index)
	{
		const vr_weapon_schema_entry_t *entry = &entries[index];
		vr_weapon_calibration_slot_t *calibration;

		if (!VR_CalibrationEntryHasFields(entry) ||
			!entry->viewmodel_path[0])
			continue;

		slot = VR_FindCalibrationSlot(entry->viewmodel_path);
		if (slot < 0)
		{
			slot = VR_FindFreeCalibrationSlot();
			if (slot < 0)
				return false; /* Preflight above makes this unreachable. */
			VR_ActivateCalibrationSlot(slot, entry->viewmodel_path);
		}
		calibration = &vr_weapon_calibration_slots[slot];

		if (VR_CalibrationEntryHasHeldFields(entry) &&
			!entry->has_muzzle_offset && !calibration->has_muzzle_offset)
		{
			/* InitWeaponCVars seeds classic muzzle Z from held Z. */
			Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Z),
						   entry->has_held_offset ? entry->held_offset[2] : 0.0f);
			calibration->has_muzzle_offset = true;
		}
		if (entry->has_held_offset)
		{
			Cvar_SetValueQuick(&VR_WeaponOffsetCvar(slot, VR_WOFS_X),
							   entry->held_offset[0]);
			Cvar_SetValueQuick(&VR_WeaponOffsetCvar(slot, VR_WOFS_Y),
							   entry->held_offset[1]);
			Cvar_SetValueQuick(&VR_WeaponOffsetCvar(slot, VR_WOFS_Z),
							   entry->held_offset[2]);
		}
		if (entry->has_held_scale)
			Cvar_SetValueQuick(&VR_WeaponOffsetCvar(slot, VR_WOFS_SCALE),
						   entry->held_scale);
		if (entry->has_muzzle_offset)
		{
			Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_X),
						   entry->muzzle_offset[0]);
			Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Y),
						   entry->muzzle_offset[1]);
			Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Z),
						   entry->muzzle_offset[2]);
			calibration->has_muzzle_offset = true;
		}
		if (entry->has_mp_held_offset)
		{
			memcpy(calibration->mp_held_offset, entry->mp_held_offset,
				   sizeof(vec3_t));
			calibration->has_mp_held_offset = true;
			calibration->has_schema_mp_held_offset =
				entry->has_schema_mp_held_offset;
			if (entry->has_schema_mp_held_offset)
				memcpy(calibration->schema_mp_held_offset,
					   entry->schema_mp_held_offset, sizeof(vec3_t));
			else
				memset(calibration->schema_mp_held_offset, 0, sizeof(vec3_t));
		}
		if (entry->has_mp_muzzle_offset)
		{
			memcpy(calibration->mp_muzzle_offset, entry->mp_muzzle_offset,
				   sizeof(vec3_t));
			calibration->has_mp_muzzle_offset = true;
			calibration->has_schema_mp_muzzle_offset =
				entry->has_schema_mp_muzzle_offset;
			if (entry->has_schema_mp_muzzle_offset)
				memcpy(calibration->schema_mp_muzzle_offset,
					   entry->schema_mp_muzzle_offset, sizeof(vec3_t));
			else
				memset(calibration->schema_mp_muzzle_offset, 0,
					   sizeof(vec3_t));
		}
		if (entry->has_enhanced_held_offset)
		{
			memcpy(calibration->enhanced_held_offset,
				   entry->enhanced_held_offset, sizeof(vec3_t));
			calibration->has_enhanced_held_offset = true;
		}
		if (entry->has_enhanced_mp_held_offset)
		{
			memcpy(calibration->enhanced_mp_held_offset,
				   entry->enhanced_mp_held_offset, sizeof(vec3_t));
			calibration->has_enhanced_mp_held_offset = true;
		}
		if (entry->has_enhanced_muzzle_offset)
		{
			memcpy(calibration->enhanced_muzzle_offset,
				   entry->enhanced_muzzle_offset, sizeof(vec3_t));
			calibration->has_enhanced_muzzle_offset = true;
		}
		if (entry->has_enhanced_mp_muzzle_offset)
		{
			memcpy(calibration->enhanced_mp_muzzle_offset,
				   entry->enhanced_mp_muzzle_offset, sizeof(vec3_t));
			calibration->has_enhanced_mp_muzzle_offset = true;
		}
		if (entry->has_muzzle_source_offset)
		{
			memcpy(calibration->muzzle_source_offset,
				   entry->muzzle_source_offset, sizeof(vec3_t));
			calibration->has_muzzle_source_offset = true;
		}
		if (entry->has_muzzle_source_viewofs)
		{
			calibration->muzzle_source_viewofs =
				entry->muzzle_source_viewofs;
			calibration->has_muzzle_source_viewofs = true;
		}
		if (entry->has_spawn_at_self_origin)
		{
			calibration->spawn_at_self_origin =
				entry->spawn_at_self_origin;
			calibration->has_spawn_at_self_origin = true;
		}
	}

	return true;
}

static qboolean VR_WeaponCalibrationApplyEnhancedFallbacks(void)
{
	return VR_WeaponCalibrationApplySchema(
		vr_enhanced_weapon_fallbacks,
		sizeof(vr_enhanced_weapon_fallbacks) /
		sizeof(vr_enhanced_weapon_fallbacks[0]));
}

static qboolean VR_WeaponCalibrationApplyEnyoFallbacks(void)
{
	const char *game = COM_SkipPath(com_gamedir);

	if (!game || q_strcasecmp(game, "enyo"))
		return true;
	return VR_WeaponCalibrationApplySchema(
		vr_enyo_weapon_fallbacks,
		sizeof(vr_enyo_weapon_fallbacks) /
		sizeof(vr_enyo_weapon_fallbacks[0]));
}

static qboolean VR_WeaponCalibrationApplyBuiltinFallbacks(void)
{
	return VR_WeaponCalibrationApplyEnhancedFallbacks() &&
		VR_WeaponCalibrationApplyEnyoFallbacks();
}

qboolean VR_WeaponCalibrationReloadGame(void)
{
	vr_weapon_schema_entry_t entries[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	byte *file;
	size_t count = 0;
	qboolean parsed;
	qboolean applied;

	VR_WeaponCalibrationInit();
	VR_WeaponCalibrationReset();
	if (!VR_WeaponCalibrationApplyBuiltinFallbacks())
	{
		VR_WeaponCalibrationReset();
		return false;
	}

	file = COM_LoadFile("vr_weapons.txt", NULL);
	if (!file)
		return true;

	parsed = VR_WeaponSchemaParse((const char *)file, entries,
								  VR_WEAPON_SCHEMA_MAX_ENTRIES, &count);
	Mem_Free(file);
	if (!parsed)
		return false;

	applied = VR_WeaponCalibrationApplySchema(entries, count);
	if (!applied)
	{
		VR_WeaponCalibrationReset();
		if (!VR_WeaponCalibrationApplyBuiltinFallbacks())
			VR_WeaponCalibrationReset();
		return false;
	}

	return true;
}

qboolean VR_WeaponCalibrationLookupHeld(const char *model_name,
										qboolean enhanced_format,
										qboolean multiplayer,
										vec3_t out_offset,
										float *out_scale)
{
	const vr_weapon_calibration_slot_t *calibration;
	vec3_t offset = {0.0f, 0.0f, 0.0f};
	float scale = 1.0f;
	int slot;
	int component;

	if (out_offset)
		memset(out_offset, 0, sizeof(vec3_t));
	if (out_scale)
		*out_scale = 1.0f;
	if (!vr_weapon_calibration_initialized || !model_name || !model_name[0] ||
		!out_offset || !out_scale)
		return false;

	slot = VR_FindCalibrationSlot(model_name);
	if (slot < 0)
		return false;
	calibration = &vr_weapon_calibration_slots[slot];

	if (enhanced_format)
	{
		/* The donor's neutral MD5 viewmodel has a zero held offset when
		 * no enhanced offset was authored for an existing slot. */
		if (calibration->has_enhanced_held_offset)
			memcpy(offset, calibration->enhanced_held_offset, sizeof(offset));
		if (multiplayer && calibration->has_enhanced_mp_held_offset)
			for (component = 0; component < 3; ++component)
				offset[component] +=
					calibration->enhanced_mp_held_offset[component];
	}
	else
	{
		offset[0] = VR_WeaponOffsetCvar(slot, VR_WOFS_X).value;
		offset[1] = VR_WeaponOffsetCvar(slot, VR_WOFS_Y).value;
		offset[2] = VR_WeaponOffsetCvar(slot, VR_WOFS_Z).value;
		scale = VR_WeaponOffsetCvar(slot, VR_WOFS_SCALE).value;
		if (!isfinite(scale) || scale <= 0.0f)
			return false;
		if (multiplayer && calibration->has_mp_held_offset)
			for (component = 0; component < 3; ++component)
				offset[component] += calibration->mp_held_offset[component];
	}

	if (!VR_CalibrationVectorIsFinite(offset))
		return false;
	memcpy(out_offset, offset, sizeof(vec3_t));
	*out_scale = scale;
	return true;
}

qboolean VR_WeaponCalibrationLookupMuzzle(const char *model_name,
										  qboolean enhanced_format,
										  qboolean multiplayer, vec3_t out)
{
	const vr_weapon_calibration_slot_t *calibration;
	float base[3];
	int slot;
	int component;

	if (out)
		memset(out, 0, sizeof(vec3_t));
	if (!vr_weapon_calibration_initialized || !model_name || !model_name[0] ||
		!out)
		return false;

	slot = VR_FindCalibrationSlot(model_name);
	if (slot < 0)
		return false;
	calibration = &vr_weapon_calibration_slots[slot];

	if (enhanced_format)
	{
		if (!calibration->has_enhanced_muzzle_offset)
			return false;
		memcpy(base, calibration->enhanced_muzzle_offset, sizeof(base));
		if (multiplayer && calibration->has_enhanced_mp_muzzle_offset)
			for (component = 0; component < 3; ++component)
				base[component] +=
					calibration->enhanced_mp_muzzle_offset[component];
	}
	else
	{
	if (!calibration->has_muzzle_offset)
			return false;
		base[0] = VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_X).value;
		base[1] = VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Y).value;
		base[2] = VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Z).value;
		if (multiplayer && calibration->has_mp_muzzle_offset)
			for (component = 0; component < 3; ++component)
				base[component] += calibration->mp_muzzle_offset[component];
	}

	if (!isfinite(base[0]) || !isfinite(base[1]) || !isfinite(base[2]))
		return false;
	for (component = 0; component < 3; ++component)
		out[component] = base[component];
	return true;
}

qboolean VR_WeaponCalibrationStockRangedViewmodel(const char *name)
{
	static const char *const models[] = {
		"progs/v_shot.mdl", "progs/v_shot2.mdl",
		"progs/v_nail.mdl", "progs/v_nail2.mdl",
		"progs/v_rock.mdl", "progs/v_rock2.mdl",
		"progs/v_light.mdl"
	};

	if (!name)
		return false;
	for (size_t i = 0; i < sizeof (models) / sizeof (models[0]); ++i)
		if (!strcmp (name, models[i]))
			return true;
	return false;
}

qboolean VR_WeaponCalibrationCurrentMuzzle(vec3_t out)
{
	qmodel_t *model;
	aliashdr_t *alias_header;
	qboolean enhanced_format;
	int model_index;

	if (!out)
		return false;
	memset(out, 0, sizeof(vec3_t));

	model_index = cl.stats[STAT_WEAPON];
	if (model_index < 1 || model_index >= MAX_MODELS)
		return false;

	model = cl.model_precache[model_index];
	if (!model || model->needload || model->type != mod_alias)
		return false;

	alias_header = (aliashdr_t *)Mod_Extradata_CheckSkin(
		model, cl.viewent.skinnum);
	if (!alias_header)
		return false;

	switch (alias_header->poseverttype)
	{
	case PV_MD5:
	case PV_MD5_8:
		enhanced_format = true;
		break;
	case PV_QUAKE1:
	case PV_QUAKE3:
		enhanced_format = false;
		break;
	default:
		return false;
	}

	if (!VR_WeaponCalibrationLookupMuzzle(model->name, enhanced_format,
										  cl.maxclients > 1, out) ||
		!VR_CalibrationVectorIsFinite(out))
	{
		memset(out, 0, sizeof(vec3_t));
		return false;
	}

	return true;
}

void VR_WeaponCalibrationProjectileSourceOffset(const char *viewmodel,
	int weapon_bit, const vec3_t angles, float viewheight, vec3_t out)
{
	static const vec3_t default_angles = {0.0f, 0.0f, 0.0f};
	static const vec3_t default_forward_offset = {0.0f, 0.0f, 8.0f};
	const vr_weapon_calibration_slot_t *calibration = NULL;
	const float *safe_angles = angles;
	vec3_t source_world;
	int slot;
	int component;
	qboolean spawn_at_self_origin = weapon_bit == IT_GRENADE_LAUNCHER;

	if (!out)
		return;
	memset(out, 0, sizeof(vec3_t));

	if (!angles || !VR_CalibrationVectorIsFinite(angles))
		safe_angles = default_angles;

	if (vr_weapon_calibration_initialized && viewmodel && viewmodel[0])
	{
		slot = VR_FindCalibrationSlot(viewmodel);
		if (slot >= 0)
			calibration = &vr_weapon_calibration_slots[slot];
	}

	if (calibration && calibration->has_spawn_at_self_origin)
		spawn_at_self_origin = calibration->spawn_at_self_origin;

	if (!spawn_at_self_origin)
	{
		if (!VR_LocomotionAimOffsetToWorld(default_forward_offset,
											  safe_angles, 1.0f, source_world))
		{
			source_world[0] = 8.0f;
			source_world[1] = 0.0f;
			source_world[2] = 0.0f;
		}
		for (component = 0; component < 3; ++component)
			out[component] = source_world[component];
		out[2] += 16.0f;
	}

	if (calibration && calibration->has_muzzle_source_viewofs &&
		calibration->muzzle_source_viewofs && isfinite(viewheight))
	{
		float view_z = out[2] + viewheight;
		if (isfinite(view_z))
			out[2] = view_z;
	}

	if (calibration && calibration->has_muzzle_source_offset &&
		VR_CalibrationVectorIsFinite(calibration->muzzle_source_offset) &&
		VR_LocomotionAimOffsetToWorld(calibration->muzzle_source_offset,
										  safe_angles, 1.0f, source_world) &&
		VR_CalibrationVectorIsFinite(source_world))
	{
		vec3_t result;
		for (component = 0; component < 3; ++component)
			result[component] = out[component] + source_world[component];
		if (VR_CalibrationVectorIsFinite(result))
			memcpy(out, result, sizeof(vec3_t));
	}
}
