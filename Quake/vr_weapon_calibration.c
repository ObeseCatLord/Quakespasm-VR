#include "quakedef.h"
#include "console.h"
#include "keys.h"
#include "glquake.h"
#include "vr_locomotion.h"
#include "vr_aim.h"
#include "vr_input.h"
#include "vr_weapon_calibration.h"
#include "view.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
	qboolean muzzle_seeded_from_held;
	qboolean has_enhanced_muzzle_offset;
	vec3_t enhanced_muzzle_offset;
	qboolean has_enhanced_held_offset;
	vec3_t enhanced_held_offset;
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
static qboolean vr_weapon_calibration_commands_registered;

extern cvar_t vr_aimmode;
extern cvar_t vr_world_scale;

typedef struct
{
	qboolean active;
	qboolean armed;
	qboolean muzzle_mode;
	qboolean return_to_grip;
	qboolean created_slot;
	int physical_hand;
	int model_index;
	int poseverttype;
	int slot;
	qmodel_t *model;
	char model_name[MAX_QPATH];
	float world_scale;
	float gunmodelscale;
	float gunmodelpitch;
	vec3_t frozen_origin;
	vec3_t frozen_hand_angles;
	vec3_t frozen_model_angles;
	vec3_t frozen_muzzle_world;
} vr_weapon_calibration_adjustment_t;

static vr_weapon_calibration_adjustment_t vr_weapon_calibration_adjustment;

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

/* Donor classic id1/Hipnotic/Rogue defaults. InitWeaponCVars derives muzzle Z
 * from held Z; keep that implicit relation until a schema authors a muzzle. */
#define VR_CLASSIC_FALLBACK(model, x, y, z, scale) \
	{ .viewmodel_path = "progs/" model ".mdl", \
	  .held_offset = {x, y, z}, .has_held_offset = true, \
	  .held_scale = scale, .has_held_scale = true }
static const vr_weapon_schema_entry_t vr_stock_classic_fallbacks[] = {
	VR_CLASSIC_FALLBACK("v_axe",   -4.0f, 24.0f, 37.0f, 0.33f),
	VR_CLASSIC_FALLBACK("v_shot",   1.5f,  1.0f, 10.0f, 0.5f),
	VR_CLASSIC_FALLBACK("v_shot2", -3.5f,  1.0f,  8.5f, 0.8f),
	VR_CLASSIC_FALLBACK("v_nail",  -5.0f,  3.0f, 15.0f, 0.5f),
	VR_CLASSIC_FALLBACK("v_nail2",  0.0f,  3.0f, 19.0f, 0.5f),
	VR_CLASSIC_FALLBACK("v_rock",  10.0f,  1.5f, 13.0f, 0.5f),
	VR_CLASSIC_FALLBACK("v_rock2", 10.0f,  7.0f, 19.0f, 0.5f),
	VR_CLASSIC_FALLBACK("v_light",  3.0f,  4.0f, 13.0f, 0.5f),
	VR_CLASSIC_FALLBACK("v_hammer", -4.0f, 17.5f, 36.0f, 0.33f),
	VR_CLASSIC_FALLBACK("v_laserg", 65.0f, 3.7f, 15.0f, 0.33f),
	VR_CLASSIC_FALLBACK("v_prox",  10.0f,  1.5f, 13.0f, 0.5f),
	VR_CLASSIC_FALLBACK("v_lava",  -5.0f,  3.0f, 15.0f, 0.5f),
	VR_CLASSIC_FALLBACK("v_lava2",  0.0f,  3.0f, 19.0f, 0.5f),
	VR_CLASSIC_FALLBACK("v_multi", 10.0f,  1.5f, 13.0f, 0.5f),
	VR_CLASSIC_FALLBACK("v_multi2",10.0f,  7.0f, 19.0f, 0.5f),
	VR_CLASSIC_FALLBACK("v_plasma", 3.0f,  4.0f, 13.0f, 0.5f),
};
#undef VR_CLASSIC_FALLBACK

/* Donor Alkaline axe defaults, also present in the installed alk profile.
 * LimJam uses the same viewmodel but omits its calibration from vr_weapons.txt. */
static const vr_weapon_schema_entry_t vr_alk_axe_fallback[] = {
	{
		.viewmodel_path = "progs/v_alkaxe20fps.mdl",
		.held_offset = {12.0f, 54.0f, 39.5f},
		.has_held_offset = true,
		.held_scale = 0.25f,
		.has_held_scale = true,
		.muzzle_offset = {0.0f, 0.0f, 39.5f},
		.has_muzzle_offset = true,
	},
};

/* Arcane Dimensions defaults from the donor InitAllWeaponCVars AD branch
 * and the installed AD vr_weapons.txt. Classic muzzle Z follows held Z. */
#define VR_AD_WEAPON_PROFILE(path, x, y, z, scale) \
	{ \
		.viewmodel_path = path, \
		.held_offset = {x, y, z}, \
		.has_held_offset = true, \
		.held_scale = scale, \
		.has_held_scale = true, \
		.muzzle_offset = {0.0f, 0.0f, z}, \
		.has_muzzle_offset = true, \
	}
static const vr_weapon_schema_entry_t vr_ad_weapon_fallbacks[] = {
	VR_AD_WEAPON_PROFILE("progs/v_shadaxe0.mdl", -1.5f, 43.1f, 41.0f, 0.25f),
	VR_AD_WEAPON_PROFILE("progs/v_shadaxe1.mdl", -1.5f, 43.1f, 41.0f, 0.25f),
	VR_AD_WEAPON_PROFILE("progs/v_shadaxe2.mdl", -1.5f, 43.1f, 41.0f, 0.25f),
	VR_AD_WEAPON_PROFILE("progs/v_shadaxe3.mdl", -1.5f, 43.1f, 41.0f, 0.25f),
	VR_AD_WEAPON_PROFILE("progs/v_shadaxe4.mdl", -1.5f, 43.1f, 41.0f, 0.25f),
	VR_AD_WEAPON_PROFILE("progs/v_shadaxe5.mdl", -1.5f, 43.1f, 41.0f, 0.25f),
	VR_AD_WEAPON_PROFILE("progs/v_shot3.mdl", -3.5f, 0.4f, 8.5f, 0.8f),
	VR_AD_WEAPON_PROFILE("progs/v_shot.mdl", 1.5f, 1.7f, 17.5f, 0.33f),
	VR_AD_WEAPON_PROFILE("progs/v_shot2.mdl", -3.5f, 0.4f, 8.5f, 0.8f),
	VR_AD_WEAPON_PROFILE("progs/v_nail.mdl", -9.5f, 3.0f, 17.0f, 0.5f),
	VR_AD_WEAPON_PROFILE("progs/v_nail2.mdl", -6.0f, 3.5f, 20.0f, 0.4f),
	VR_AD_WEAPON_PROFILE("progs/v_rock.mdl", -3.0f, 1.25f, 17.0f, 0.5f),
	VR_AD_WEAPON_PROFILE("progs/v_rock2.mdl", 0.0f, 5.55f, 22.5f, 0.45f),
	VR_AD_WEAPON_PROFILE("progs/v_light.mdl", -4.0f, 3.1f, 13.0f, 0.5f),
	VR_AD_WEAPON_PROFILE("progs/v_plasma.mdl", 2.8f, 1.8f, 22.5f, 0.5f),
};
#undef VR_AD_WEAPON_PROFILE

/* These Mjolnir paths are hash-confirmed copies of the named AD pak0 models.
 * Keep this list exact: similarly named AD replacements can have different
 * geometry and need their own authored schema values. */
static const struct
{
	const char *alias_path;
	const char *ad_path;
} vr_ad171_weapon_aliases[] = {
	{"progs/ad171/v_shot.mdl", "progs/v_shot.mdl"},
	{"progs/ad171/v_shot3.mdl", "progs/v_shot3.mdl"},
	{"progs/ad171/v_rock.mdl", "progs/v_rock.mdl"},
	{"progs/ad171/v_rock2.mdl", "progs/v_rock2.mdl"},
	{"progs/ad171/v_light.mdl", "progs/v_light.mdl"},
	{"progs/ad171/v_plasma.mdl", "progs/v_plasma.mdl"},
	{"progs/ad171/v_shadaxe0.mdl", "progs/v_shadaxe0.mdl"},
	{"progs/ad171/v_shadaxe1.mdl", "progs/v_shadaxe1.mdl"},
	{"progs/ad171/v_shadaxe2.mdl", "progs/v_shadaxe2.mdl"},
	{"progs/ad171/v_shadaxe3.mdl", "progs/v_shadaxe3.mdl"},
	{"progs/ad171/v_shadaxe4.mdl", "progs/v_shadaxe4.mdl"},
	{"progs/ad171/v_shadaxe5.mdl", "progs/v_shadaxe5.mdl"},
	{"progs/ad171/v_nail2.mdl", "progs/v_nail2.mdl"},
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

static qboolean VR_CalibrationPoseTypeIsEnhanced(int poseverttype)
{
	return poseverttype == PV_MD5 || poseverttype == PV_MD5_8;
}

static qboolean VR_CalibrationPoseTypeIsSupported(int poseverttype)
{
	return poseverttype == PV_QUAKE1 || poseverttype == PV_QUAKE3 ||
		VR_CalibrationPoseTypeIsEnhanced(poseverttype);
}

static qboolean VR_CalibrationEntryHasFields(
	const vr_weapon_schema_entry_t *entry)
{
	return entry->has_held_offset || entry->has_held_scale ||
		entry->has_muzzle_offset || entry->has_muzzle_source_offset ||
		entry->has_muzzle_source_viewofs || entry->has_spawn_at_self_origin ||
		entry->has_enhanced_held_offset ||
		entry->has_enhanced_muzzle_offset;
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

typedef struct
{
	char *data;
	size_t len;
	size_t cap;
} vr_calibration_textbuf_t;

static qboolean VR_CalibrationTextReserve(vr_calibration_textbuf_t *buf,
										  size_t extra)
{
	size_t needed;
	size_t newcap;
	char *newdata;

	if (extra > (size_t)-1 - buf->len - 1)
		return false;
	needed = buf->len + extra + 1;
	if (needed <= buf->cap)
		return true;

	newcap = buf->cap ? buf->cap : 1024;
	while (newcap < needed)
	{
		if (newcap > (size_t)-1 / 2)
		{
			newcap = needed;
			break;
		}
		newcap *= 2;
	}
	newdata = (char *)realloc(buf->data, newcap);
	if (!newdata)
		return false;
	buf->data = newdata;
	buf->cap = newcap;
	return true;
}

static qboolean VR_CalibrationTextAppendN(vr_calibration_textbuf_t *buf,
										  const char *text, size_t len)
{
	if (!VR_CalibrationTextReserve(buf, len))
		return false;
	if (len)
		memcpy(buf->data + buf->len, text, len);
	buf->len += len;
	buf->data[buf->len] = '\0';
	return true;
}

static qboolean VR_CalibrationTextAppend(vr_calibration_textbuf_t *buf,
										 const char *text)
{
	return VR_CalibrationTextAppendN(buf, text, strlen(text));
}

static qboolean VR_CalibrationTextAppendLine(vr_calibration_textbuf_t *buf,
										 const char *line)
{
	return VR_CalibrationTextAppend(buf, line) &&
		VR_CalibrationTextAppend(buf, "\n");
}

static qboolean VR_CalibrationIsTokenBreak(char c)
{
	return c == '\0' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static qboolean VR_CalibrationLineStartsWithKey(const char *line,
											 size_t len, const char *key)
{
	size_t keylen = strlen(key);

	while (len && (*line == ' ' || *line == '\t' || *line == '\r'))
	{
		++line;
		--len;
	}
	return len >= keylen && !strncmp(line, key, keylen) &&
		(len == keylen || VR_CalibrationIsTokenBreak(line[keylen]));
}

static qboolean VR_CalibrationLineIsClassicKey(const char *line, size_t len)
{
	return VR_CalibrationLineStartsWithKey(line, len, "held_scale") ||
		VR_CalibrationLineStartsWithKey(line, len, "held_offset") ||
		VR_CalibrationLineStartsWithKey(line, len, "mp_held_offset") ||
		VR_CalibrationLineStartsWithKey(line, len, "muzzle_offset") ||
		VR_CalibrationLineStartsWithKey(line, len, "mp_muzzle_offset");
}

static qboolean VR_CalibrationLineIsEnhancedKey(const char *line, size_t len)
{
	return VR_CalibrationLineStartsWithKey(line, len,
											"enhanced_held_offset") ||
		VR_CalibrationLineStartsWithKey(line, len,
										"enhanced_mp_held_offset") ||
		VR_CalibrationLineStartsWithKey(line, len,
										"enhanced_muzzle_offset") ||
		VR_CalibrationLineStartsWithKey(line, len,
										"enhanced_mp_muzzle_offset");
}

static qboolean VR_CalibrationLineIsMultiplayerKey(const char *line, size_t len)
{
	return VR_CalibrationLineStartsWithKey(line, len, "mp_held_offset") ||
		VR_CalibrationLineStartsWithKey(line, len, "mp_muzzle_offset") ||
		VR_CalibrationLineStartsWithKey(line, len, "enhanced_mp_held_offset") ||
		VR_CalibrationLineStartsWithKey(line, len, "enhanced_mp_muzzle_offset");
}

static const char *VR_CalibrationFindBrace(const char *text,
									   const char *end, char brace)
{
	char token[COM_PARSE_MAX_TOKEN_SIZE];
	const char *cursor = text;
	const char *start;
	const char *next;
	qboolean parse_error;

	while (cursor < end &&
		(next = COM_ParseExBufferSpan(cursor, CPE_NOTRUNC, token,
			sizeof(token), &parse_error, &start)) != NULL)
	{
		if (next > end || !start || start >= end)
			return NULL;
		if (*start == brace && token[0] == brace && !token[1])
			return start;
		cursor = next;
	}
	return NULL;
}

static qboolean VR_CalibrationBlockMatchesViewmodel(const char *block,
												 size_t len,
												 const char *model)
{
	char explicit_viewmodel[MAX_QPATH] = {0};
	char model_fallback[MAX_QPATH] = {0};
	char *copy;
	char *parse;
	const char *selected_viewmodel;

	copy = (char *)malloc(len + 1);
	if (!copy)
		return false;
	memcpy(copy, block, len);
	copy[len] = '\0';
	parse = copy;
	while ((parse = (char *)COM_Parse(parse)) && com_token[0])
	{
		if (!strcmp(com_token, "viewmodel") ||
			!strcmp(com_token, "held_model") || !strcmp(com_token, "model"))
		{
			qboolean explicit_key = !strcmp(com_token, "viewmodel") ||
				!strcmp(com_token, "held_model");
			char *destination = explicit_key ? explicit_viewmodel : model_fallback;
			size_t destination_size = explicit_key ?
				sizeof(explicit_viewmodel) : sizeof(model_fallback);

			parse = (char *)COM_Parse(parse);
			if (!parse || !com_token[0])
				break;
			q_strlcpy(destination, com_token, destination_size);
		}
	}
	free(copy);
	/* VR_WeaponSchemaParse uses model only when no non-empty explicit
	 * viewmodel/held_model path was supplied. */
	selected_viewmodel = explicit_viewmodel[0] ? explicit_viewmodel :
		model_fallback;
	return selected_viewmodel[0] && !strcmp(selected_viewmodel, model);
}

static qboolean VR_CalibrationAppendAdjustmentLines(
	vr_calibration_textbuf_t *buf, int slot, qboolean enhanced_format)
{
	char line[192];
	const vr_weapon_calibration_slot_t *calibration =
		&vr_weapon_calibration_slots[slot];

	if (enhanced_format)
	{
		if (calibration->has_enhanced_held_offset)
		{
			q_snprintf(line, sizeof(line),
				"enhanced_held_offset %.7g %.7g %.7g",
				calibration->enhanced_held_offset[0],
				calibration->enhanced_held_offset[1],
				calibration->enhanced_held_offset[2]);
			if (!VR_CalibrationTextAppendLine(buf, line))
				return false;
		}
		if (calibration->has_enhanced_muzzle_offset)
		{
			q_snprintf(line, sizeof(line),
				"enhanced_muzzle_offset %.7g %.7g %.7g",
				calibration->enhanced_muzzle_offset[0],
				calibration->enhanced_muzzle_offset[1],
				calibration->enhanced_muzzle_offset[2]);
			if (!VR_CalibrationTextAppendLine(buf, line))
				return false;
		}
		return true;
	}

	{
		float held_scale = VR_WeaponOffsetCvar(slot, VR_WOFS_SCALE).value;

		q_snprintf(line, sizeof(line), "held_scale %.7g", held_scale);
		if (!VR_CalibrationTextAppendLine(buf, line))
			return false;
	}
	q_snprintf(line, sizeof(line), "held_offset %.7g %.7g %.7g",
		VR_WeaponOffsetCvar(slot, VR_WOFS_X).value,
		VR_WeaponOffsetCvar(slot, VR_WOFS_Y).value,
		VR_WeaponOffsetCvar(slot, VR_WOFS_Z).value);
	if (!VR_CalibrationTextAppendLine(buf, line))
		return false;
	if (calibration->has_muzzle_offset)
	{
		q_snprintf(line, sizeof(line), "muzzle_offset %.7g %.7g %.7g",
			VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_X).value,
			VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Y).value,
			VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Z).value);
		if (!VR_CalibrationTextAppendLine(buf, line))
			return false;
	}

	return true;
}

static qboolean VR_CalibrationWriteUpdatedBlock(
	vr_calibration_textbuf_t *buf, const char *block, size_t len, int slot,
	qboolean enhanced_format)
{
	const char *cursor = block;
	const char *copied = block;
	const char *end = block + len;
	char key[COM_PARSE_MAX_TOKEN_SIZE];
	char value[COM_PARSE_MAX_TOKEN_SIZE];

	while (cursor < end)
	{
		const char *key_start;
		const char *next;
		qboolean parse_error = false;
		int values;
		qboolean selected;

		next = COM_ParseExBufferSpan(cursor, CPE_NOTRUNC, key,
			sizeof(key), &parse_error, &key_start);
		if (!next || parse_error || next > end || !key_start ||
			key_start < cursor || key_start >= end)
			return false;
		if (*key_start == '}' && !strcmp(key, "}"))
		{
			/* A fresh line keeps compact braces and trailing block comments
			 * unambiguous without changing any preserved token/comment bytes. */
			return VR_CalibrationTextAppendN(buf, copied,
				(size_t)(key_start - copied)) &&
				VR_CalibrationTextAppend(buf, "\n") &&
				VR_CalibrationAppendAdjustmentLines(buf, slot,
					enhanced_format) &&
				VR_CalibrationTextAppendN(buf, key_start,
					(size_t)(end - key_start));
		}
		if (!strcmp(key, "{"))
		{
			cursor = next;
			continue;
		}
		/* Match VR_SchemaParseEntry's value arity. Unknown keys consume one
		 * scalar; all vector keys consume three tokens, even when not edited. */
		values = !strcmp(key, "offset") || !strcmp(key, "held_offset") ||
			!strcmp(key, "mp_held_offset") || !strcmp(key, "muzzle_offset") ||
			!strcmp(key, "mp_muzzle_offset") ||
			!strcmp(key, "enhanced_held_offset") ||
			!strcmp(key, "enhanced_mp_held_offset") ||
			!strcmp(key, "enhanced_muzzle_offset") ||
			!strcmp(key, "enhanced_mp_muzzle_offset") ||
			!strcmp(key, "muzzle_source_offset") ? 3 : 1;
		selected = VR_CalibrationLineIsMultiplayerKey(key, strlen(key)) || (enhanced_format ?
			VR_CalibrationLineIsEnhancedKey(key, strlen(key)) :
			VR_CalibrationLineIsClassicKey(key, strlen(key)));
		if (selected && !VR_CalibrationTextAppendN(buf, copied,
			(size_t)(key_start - copied)))
			return false;
		cursor = next;
		for (int i = 0; i < values; ++i)
		{
			const char *value_start;
			const char *value_next = COM_ParseExBufferSpan(cursor,
				CPE_NOTRUNC, value, sizeof(value), &parse_error,
				&value_start);
			if (!value_next || parse_error || value_next > end ||
				!value_start || value_start < cursor ||
				value_start >= end || !strcmp(value, "{") ||
				!strcmp(value, "}"))
				return false;
			if (selected && !VR_CalibrationTextAppendN(buf, cursor,
				(size_t)(value_start - cursor)))
				return false;
			cursor = value_next;
		}
		if (selected)
			copied = cursor;
	}
	return false; /* The validated block must have a closing-brace token. */
}

static qboolean VR_CalibrationSafeModelToken(const char *path)
{
	const char *p;
	const char *segment;
	const char *terminator;

	if (!path || !(terminator = (const char *)memchr(path, '\0', MAX_QPATH)) ||
		terminator == path || path[0] == '/')
		return false;
	segment = path;
	for (p = path; p <= terminator; ++p)
	{
		unsigned char c = (unsigned char)*p;
		if (c == '/' || c == '\0')
		{
			size_t segment_len = (size_t)(p - segment);
			if (!segment_len ||
				(segment_len == 1 && segment[0] == '.') ||
				(segment_len == 2 && segment[0] == '.' && segment[1] == '.'))
				return false;
			if (!c)
				break;
			segment = p + 1;
			continue;
		}
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
			return false;
	}
	return true;
}

static qboolean VR_CalibrationActiveFileMatches(const char *expected,
												 size_t expected_len)
{
	char path[MAX_OSPATH];
	byte *actual;
	int handle = -1;
	int path_len;
	int bytes_read;
	qfilesize_t file_len;
	qboolean matches;

	path_len = q_snprintf(path, sizeof(path), "%s/%s", com_gamedir,
		"vr_weapons.txt");
	if (path_len < 0 || (size_t)path_len >= sizeof(path))
		return false;
	file_len = Sys_FileOpenRead(path, &handle);
	if (handle < 0 || file_len < 0 || (qfilesize_t)expected_len != file_len)
	{
		if (handle >= 0)
			Sys_FileClose(handle);
		return false;
	}
	actual = (byte *)malloc(expected_len ? expected_len : 1);
	if (!actual)
	{
		Sys_FileClose(handle);
		return false;
	}
	bytes_read = Sys_FileRead(handle, actual, (int)expected_len);
	Sys_FileClose(handle);
	matches = bytes_read == (int)expected_len &&
		(!expected_len || !memcmp(actual, expected, expected_len));
	free(actual);
	return matches;
}

static qboolean VR_CalibrationSavedFloatMatches(float actual, float expected)
{
	return isfinite(actual) && isfinite(expected) &&
		fabsf(actual - expected) <=
		1e-5f * fmaxf(1.0f, fmaxf(fabsf(actual), fabsf(expected)));
}

static qboolean VR_CalibrationSavedVectorMatches(const vec3_t actual,
	const vec3_t expected)
{
	for (int i = 0; i < 3; ++i)
		if (!VR_CalibrationSavedFloatMatches(actual[i], expected[i]))
			return false;
	return true;
}

/* Parsing alone accepts misplaced or comment-swallowed edits. Check that the
 * active entry carries the exact requested effective values after rewriting. */
static qboolean VR_CalibrationSavedValuesMatch(const char *text,
	const char *model, int slot, qboolean enhanced_format,
	size_t original_count)
{
	vr_weapon_schema_entry_t entries[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	const vr_weapon_calibration_slot_t *calibration =
		&vr_weapon_calibration_slots[slot];
	size_t count;
	int matches = 0;

	if (!VR_WeaponSchemaParse(text, entries,
		VR_WEAPON_SCHEMA_MAX_ENTRIES, &count) || count < original_count ||
		count > original_count + 1)
		return false;
	for (size_t i = 0; i < count; ++i)
	{
		const vr_weapon_schema_entry_t *entry = &entries[i];
		if (strcmp(entry->viewmodel_path, model))
			continue;
		++matches;
		if (enhanced_format)
		{
			if (entry->has_enhanced_held_offset !=
				calibration->has_enhanced_held_offset ||
				entry->has_enhanced_muzzle_offset !=
				calibration->has_enhanced_muzzle_offset ||
				(calibration->has_enhanced_held_offset &&
				 !VR_CalibrationSavedVectorMatches(entry->enhanced_held_offset,
					calibration->enhanced_held_offset)) ||
				(calibration->has_enhanced_muzzle_offset &&
				 !VR_CalibrationSavedVectorMatches(entry->enhanced_muzzle_offset,
					calibration->enhanced_muzzle_offset)))
				return false;
		}
		else
		{
			vec3_t held, muzzle;
			for (int axis = 0; axis < 3; ++axis)
			{
				held[axis] = VR_WeaponOffsetCvar(slot, axis).value;
				muzzle[axis] = VR_WeaponMuzzleCvar(slot, axis).value;
			}
			if (!entry->has_held_scale || !entry->has_held_offset ||
				!VR_CalibrationSavedFloatMatches(entry->held_scale,
					VR_WeaponOffsetCvar(slot, VR_WOFS_SCALE).value) ||
				!VR_CalibrationSavedVectorMatches(entry->held_offset, held) ||
				entry->has_muzzle_offset != calibration->has_muzzle_offset ||
				(calibration->has_muzzle_offset &&
				 !VR_CalibrationSavedVectorMatches(entry->muzzle_offset, muzzle)))
				return false;
		}
	}
	return matches == 1;
}

static qboolean VR_CalibrationAppendNewBlock(vr_calibration_textbuf_t *buf,
											 const char *model, int slot,
											 qboolean enhanced_format)
{
	char line[MAX_QPATH + 16];

	if (buf->len && buf->data[buf->len - 1] != '\n' &&
		!VR_CalibrationTextAppend(buf, "\n"))
		return false;
	if (!VR_CalibrationTextAppendLine(buf, "{"))
		return false;
	q_snprintf(line, sizeof(line), "viewmodel %s", model);
	return VR_CalibrationTextAppendLine(buf, line) &&
		VR_CalibrationAppendAdjustmentLines(buf, slot, enhanced_format) &&
		VR_CalibrationTextAppendLine(buf, "}");
}

static qboolean VR_WeaponCalibrationSave(void)
{
	vr_calibration_textbuf_t output = {0};
	vr_weapon_schema_entry_t parsed[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	qmodel_t *model;
	aliashdr_t *alias_header;
	byte *file = NULL;
	const char *source;
	const char *p;
	const char *end;
	int model_index;
	int slot;
	size_t parsed_count;
	qboolean enhanced_format;
	qboolean invalid_values;
	qboolean updated = false;
	qboolean ok = false;
	qboolean post_write_failed = false;
	qboolean added_classic_muzzle = false;

	if (!vr_weapon_calibration_initialized || cls.state != ca_connected ||
		cls.signon != SIGNONS || cls.demoplayback)
	{
		Con_Printf("VR: vrweaponsave requires a connected, fully signed-on game\n");
		return false;
	}
	model_index = cl.stats[STAT_WEAPON];
	if (model_index < 1 || model_index >= MAX_MODELS ||
		!cl.viewent.model || !cl.model_precache[model_index] ||
		cl.viewent.model != cl.model_precache[model_index] ||
		cl.viewent.model->needload || cl.viewent.model->type != mod_alias ||
		!cl.viewent.model->name[0])
	{
		Con_Printf("VR: vrweaponsave requires a valid current alias viewmodel\n");
		return false;
	}
	model = cl.viewent.model;
	if (!VR_CalibrationSafeModelToken(model->name))
	{
		Con_Printf("VR: cannot save an unsafe or unterminated viewmodel path token\n");
		return false;
	}
	alias_header = (aliashdr_t *)Mod_Extradata_CheckSkin(model,
		cl.viewent.skinnum);
	if (!alias_header)
	{
		Con_Printf("VR: vrweaponsave cannot read the active viewmodel alias header\n");
		return false;
	}
	enhanced_format = VR_CalibrationPoseTypeIsEnhanced(
		alias_header->poseverttype);
	if (!VR_CalibrationPoseTypeIsSupported(alias_header->poseverttype))
	{
		Con_Printf("VR: vrweaponsave cannot save this viewmodel geometry format\n");
		return false;
	}
	slot = VR_FindCalibrationSlot(model->name);
	if (slot < 0)
	{
		Con_Printf("VR: no weapon calibration slot exists for %s\n", model->name);
		return false;
	}
	if (enhanced_format)
	{
		const vr_weapon_calibration_slot_t *calibration =
			&vr_weapon_calibration_slots[slot];
		invalid_values =
			(calibration->has_enhanced_held_offset &&
			 !VR_CalibrationVectorIsFinite(calibration->enhanced_held_offset)) ||
			(calibration->has_enhanced_muzzle_offset &&
			 !VR_CalibrationVectorIsFinite(
				 calibration->enhanced_muzzle_offset));
	}
	else
	{
		invalid_values =
			!isfinite(VR_WeaponOffsetCvar(slot, VR_WOFS_X).value) ||
			!isfinite(VR_WeaponOffsetCvar(slot, VR_WOFS_Y).value) ||
			!isfinite(VR_WeaponOffsetCvar(slot, VR_WOFS_Z).value) ||
			!isfinite(VR_WeaponOffsetCvar(slot, VR_WOFS_SCALE).value) ||
			VR_WeaponOffsetCvar(slot, VR_WOFS_SCALE).value <= 0.0f ||
			!isfinite(VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_X).value) ||
			!isfinite(VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Y).value) ||
			!isfinite(VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Z).value);
	}
	if (invalid_values)
	{
		Con_Printf("VR: cannot save invalid %s calibration values for %s\n",
			enhanced_format ? "enhanced" : "classic", model->name);
		return false;
	}
	if (!enhanced_format &&
		!vr_weapon_calibration_slots[slot].has_muzzle_offset)
	{
		/* A successful classic save publishes the current muzzle cvars.
		 * Keep presence consistent with the authored key after a reload. */
		vr_weapon_calibration_slots[slot].has_muzzle_offset = true;
		added_classic_muzzle = true;
	}

	file = COM_LoadFile("vr_weapons.txt", NULL);
	source = file ? (const char *)file : "";
	if (!VR_WeaponSchemaParse(source, parsed,
		VR_WEAPON_SCHEMA_MAX_ENTRIES, &parsed_count))
	{
		Con_Printf("VR: refusing to save; existing vr_weapons.txt is invalid\n");
		goto done;
	}
	p = source;
	end = source + strlen(source);
	while (p < end)
	{
		const char *open = VR_CalibrationFindBrace(p, end, '{');
		const char *close;

		if (!open)
		{
			if (!VR_CalibrationTextAppendN(&output, p, (size_t)(end - p)))
				goto done;
			break;
		}
		if (!VR_CalibrationTextAppendN(&output, p, (size_t)(open - p)))
			goto done;
		close = VR_CalibrationFindBrace(open + 1, end, '}');
		if (!close)
			goto done; /* The schema preflight should make this unreachable. */
		++close;
		if (VR_CalibrationBlockMatchesViewmodel(open,
			(size_t)(close - open), model->name))
		{
			if (updated)
			{
				Con_Printf("VR: refusing to save; multiple viewmodel blocks match %s\n",
					model->name);
				goto done;
			}
			if (!VR_CalibrationWriteUpdatedBlock(&output, open,
				(size_t)(close - open), slot, enhanced_format))
				goto done;
			updated = true;
		}
		else if (!VR_CalibrationTextAppendN(&output, open,
			(size_t)(close - open)))
			goto done;
		p = close;
	}

	if (!updated)
	{
		vr_calibration_textbuf_t prefixed = {0};
		/* A new runtime slot has not inherited the file's global offsets.
		 * Place it before those directives so a reload resolves identically. */
		if (!VR_CalibrationAppendNewBlock(&prefixed, model->name, slot,
			enhanced_format) ||
			!VR_CalibrationTextAppendN(&prefixed,
				output.data ? output.data : "", output.len))
		{
			free(prefixed.data);
			goto done;
		}
		free(output.data);
		output = prefixed;
	}
	if (output.len > INT_MAX)
	{
		Con_Printf("VR: refusing to save; vr_weapons.txt is too large\n");
		goto done;
	}
	if (!VR_CalibrationSavedValuesMatch(output.data ? output.data : "",
		model->name, slot, enhanced_format, parsed_count))
	{
		Con_Printf("VR: refusing to save; rewritten vr_weapons.txt does not preserve the requested calibration\n");
		goto done;
	}
	COM_WriteFile("vr_weapons.txt", output.data ? output.data : "",
		(int)output.len);
	if (!VR_CalibrationActiveFileMatches(output.data ? output.data : "",
		output.len))
	{
		Con_Printf("VR: COM_WriteFile returned, but the active-game vr_weapons.txt did not match the requested save; save status is uncertain\n");
		post_write_failed = true;
		goto done;
	}
	Con_Printf("VR: saved %s calibration for %s to %s/vr_weapons.txt\n",
		enhanced_format ? "enhanced" : "classic", model->name, com_gamedir);
	ok = true;

done:
	if (!ok && added_classic_muzzle)
		vr_weapon_calibration_slots[slot].has_muzzle_offset = false;
	if (!ok && output.data && !post_write_failed)
		Con_Printf("VR: failed to save %s calibration for %s\n",
			enhanced_format ? "enhanced" : "classic", model->name);
	free(output.data);
	if (file)
		Mem_Free(file);
	return ok;
}

static void VR_WeaponCalibrationSave_f(void)
{
	(void)VR_WeaponCalibrationSave();
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
	return entry->has_held_offset || entry->has_held_scale;
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

static qboolean VR_CalibrationAdjustGameplayContext(int *hand_out)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame();
	const vrxr_device_t *head;
	const vrxr_device_t *hand;
	int dominant;

	if (hand_out)
		*hand_out = -1;
	if (!vr_weapon_calibration_initialized || !frame || !frame->sample_id ||
		frame->reference_changed ||
		!frame->focused || !frame->should_render || !V_UseTrackedView() ||
		!isfinite(vr_aimmode.value) ||
		vr_aimmode.value != VR_AIMMODE_CONTROLLER ||
		cls.state != ca_connected || cls.signon != SIGNONS || cls.demoplayback ||
		cl.intermission || cl.paused || key_dest != key_game ||
		Key_InputGrabActive() || con_forcedup || !cl.worldmodel ||
		cl.worldmodel->needload || !cl.entities || cl.viewentity <= 0 ||
		cl.viewentity >= cl.num_entities || cl.stats[STAT_HEALTH] <= 0 ||
		!isfinite(vr_world_scale.value) || vr_world_scale.value <= 0.0f ||
		!isfinite(vr_gunmodelscale.value) || vr_gunmodelscale.value <= 0.0f ||
		!isfinite(vr_gunmodelpitch.value))
		return false;

	head = &frame->devices[0];
	if (!head->valid || !head->tracked || !head->connected ||
		head->kind != VRXR_DEVICE_HEAD || head->hand != -1)
		return false;
	dominant = VR_InputDominantPhysicalHand();
	if (dominant < 0 || dominant > 1)
		return false;
	hand = &frame->devices[dominant + 1];
	if (!hand->valid || !hand->tracked || !hand->connected ||
		hand->kind != VRXR_DEVICE_HAND || hand->hand != dominant)
		return false;

	if (hand_out)
		*hand_out = dominant;
	return true;
}

static qboolean VR_CalibrationAdjustCurrentModel(qmodel_t **model_out,
	aliashdr_t **alias_out, int *model_index_out)
{
	int model_index = cl.stats[STAT_WEAPON];
	qmodel_t *model;
	aliashdr_t *alias_header;

	if (model_out)
		*model_out = NULL;
	if (alias_out)
		*alias_out = NULL;
	if (model_index_out)
		*model_index_out = 0;
	if (!model_out || !alias_out || !model_index_out || model_index < 1 ||
		model_index >= MAX_MODELS || !cl.viewent.model ||
		!cl.model_precache[model_index] ||
		cl.viewent.model != cl.model_precache[model_index] ||
		cl.viewent.model->needload || cl.viewent.model->type != mod_alias ||
		!cl.viewent.model->name[0] || cl.viewent.skinnum < 0)
		return false;

	model = cl.viewent.model;
	alias_header = (aliashdr_t *)Mod_Extradata_CheckSkin(model,
		cl.viewent.skinnum);
	if (!alias_header)
		return false;
	*model_out = model;
	*alias_out = alias_header;
	*model_index_out = model_index;
	return true;
}

qboolean VR_WeaponCalibrationAdjustActive(void)
{
	return vr_weapon_calibration_adjustment.active;
}

static qboolean VR_CalibrationAdjustStateValid(
	const vr_weapon_calibration_adjustment_t *adjustment,
	const char **reason_out)
{
	qmodel_t *model;
	aliashdr_t *alias_header;
	int dominant, model_index;

	if (reason_out)
		*reason_out = "gameplay or OpenXR context changed";
	if (!adjustment || !adjustment->active ||
		!VR_CalibrationAdjustGameplayContext(&dominant))
		return false;
	if (dominant != adjustment->physical_hand)
	{
		if (reason_out)
			*reason_out = "active controller hand changed";
		return false;
	}
	if (vr_world_scale.value != adjustment->world_scale ||
		vr_gunmodelscale.value != adjustment->gunmodelscale ||
		vr_gunmodelpitch.value != adjustment->gunmodelpitch)
	{
		if (reason_out)
			*reason_out = "viewmodel scale or pitch changed";
		return false;
	}
	if (!VR_CalibrationAdjustCurrentModel(&model, &alias_header,
		&model_index) || model_index != adjustment->model_index ||
		model != adjustment->model || strcmp(model->name,
			adjustment->model_name) ||
		VR_FindCalibrationSlot(adjustment->model_name) != adjustment->slot ||
		alias_header->poseverttype != adjustment->poseverttype ||
		!VR_CalibrationPoseTypeIsSupported(alias_header->poseverttype))
	{
		if (reason_out)
			*reason_out = "viewmodel identity or format changed";
		return false;
	}
	return true;
}

static void VR_CalibrationAdjustInputAbort(const char *reason);

void VR_WeaponCalibrationAdjustCancel(void)
{
	vr_weapon_calibration_adjustment_t *adjustment =
		&vr_weapon_calibration_adjustment;

	if (adjustment->active && adjustment->created_slot &&
		adjustment->slot >= 0 &&
		adjustment->slot < VR_WEAPON_CALIBRATION_MAX_SLOTS &&
		!strcmp(VR_WeaponOffsetCvar(adjustment->slot, VR_WOFS_ID).string,
			adjustment->model_name))
		VR_SetCalibrationSlotDefaults(adjustment->slot);
	memset(adjustment, 0, sizeof(*adjustment));
	adjustment->slot = -1;
}

qboolean VR_WeaponCalibrationAdjustPresentation(vec3_t origin,
	vec3_t angles)
{
	vr_weapon_calibration_adjustment_t *adjustment =
		&vr_weapon_calibration_adjustment;
	const char *reason;
	if (!adjustment->active || !origin || !angles)
		return false;
	/* Rendering and input may observe a context or model change on different
	 * frames. Keep frozen presentation scoped to the captured weapon and hand. */
	if (!VR_CalibrationAdjustStateValid(adjustment, &reason))
	{
		VR_CalibrationAdjustInputAbort(reason);
		return false;
	}
	VectorCopy(adjustment->frozen_origin, origin);
	VectorCopy(adjustment->frozen_model_angles, angles);
	return true;
}

qboolean VR_WeaponCalibrationAdjustMuzzleCue(vec3_t world)
{
	vr_weapon_calibration_adjustment_t *adjustment =
		&vr_weapon_calibration_adjustment;
	const char *reason;

	if (!world || !adjustment->active || !adjustment->muzzle_mode)
		return false;
	if (!VR_CalibrationAdjustStateValid(adjustment, &reason))
	{
		VR_CalibrationAdjustInputAbort(reason);
		return false;
	}
	VectorCopy(adjustment->frozen_muzzle_world, world);
	return true;
}

static void VR_WeaponCalibrationAdjustBegin_f(qboolean muzzle_mode)
{
	vr_weapon_calibration_adjustment_t *adjustment =
		&vr_weapon_calibration_adjustment;
	qmodel_t *model;
	aliashdr_t *alias_header;
	vec3_t raw_origin, raw_hand_angles, model_angles;
	vec3_t effective_muzzle, muzzle_world;
	int dominant, model_index, slot, component;
	qboolean created_slot = false;
	qboolean enhanced_format;

	if (adjustment->active)
	{
		if (adjustment->return_to_grip)
		{
			VR_WeaponCalibrationAdjustCancel();
			if (muzzle_mode)
			{
				Con_Printf("VR: muzzle recenter return-to-grip hold canceled; run vradjustmuzzle again to begin a new recenter\n");
				return;
			}
			Con_Printf("VR: muzzle recenter return-to-grip hold canceled; starting a fresh grip adjustment\n");
		}
		else if (adjustment->muzzle_mode == muzzle_mode)
		{
			VR_WeaponCalibrationAdjustCancel();
			Con_Printf("VR: %s adjustment canceled\n",
				muzzle_mode ? "muzzle recenter" : "grip");
			return;
		}
		else
		{
			VR_WeaponCalibrationAdjustCancel();
			Con_Printf("VR: active calibration canceled; starting %s adjustment\n",
				muzzle_mode ? "muzzle" : "grip");
		}
	}
	if (!VR_CalibrationAdjustGameplayContext(&dominant))
	{
		Con_Printf("VR: %s requires focused, renderable controller aim during live gameplay\n",
			muzzle_mode ? "vradjustmuzzle" : "vradjustweapon");
		return;
	}
	if (!VR_CalibrationAdjustCurrentModel(&model, &alias_header,
		&model_index))
	{
		Con_Printf("VR: %s requires the valid alias viewmodel selected by STAT_WEAPON\n",
			muzzle_mode ? "vradjustmuzzle" : "vradjustweapon");
		return;
	}
	enhanced_format = VR_CalibrationPoseTypeIsEnhanced(
		alias_header->poseverttype);
	if (!VR_CalibrationPoseTypeIsSupported(alias_header->poseverttype))
	{
		Con_Printf("VR: %s requires supported Quake 1, Quake 3, or enhanced MD5 alias geometry\n",
			muzzle_mode ? "vradjustmuzzle" : "vradjustweapon");
		return;
	}
	if (!VR_CalibrationSafeModelToken(model->name))
	{
		Con_Printf("VR: %s cannot save an unsafe or unterminated viewmodel path\n",
			muzzle_mode ? "vradjustmuzzle" : "vradjustweapon");
		return;
	}
	if (!V_TrackedPresentationHandWorldPose(dominant, raw_origin,
		raw_hand_angles) || !VR_CalibrationVectorIsFinite(raw_origin) ||
		!VR_CalibrationVectorIsFinite(raw_hand_angles) ||
		!VR_LocomotionHandRotToViewmodelAngles(raw_hand_angles, model_angles,
			vr_gunmodelpitch.value) || !VR_CalibrationVectorIsFinite(model_angles))
	{
		Con_Printf("VR: %s could not capture the live controller grip pose\n",
			muzzle_mode ? "vradjustmuzzle" : "vradjustweapon");
		return;
	}

	slot = VR_FindCalibrationSlot(model->name);
	if (slot < 0)
	{
		slot = VR_FindFreeCalibrationSlot();
		if (slot < 0)
		{
			Con_Printf("VR: %s has no free weapon calibration slot\n",
				muzzle_mode ? "vradjustmuzzle" : "vradjustweapon");
			return;
		}
		VR_ActivateCalibrationSlot(slot, model->name);
		created_slot = true;
	}

	if (muzzle_mode)
	{
		if (!VR_WeaponCalibrationLookupMuzzle(model->name, enhanced_format,
			effective_muzzle))
		{
			if (enhanced_format)
			{
				/* A missing enhanced muzzle starts at the controller grip. */
				memset(effective_muzzle, 0, sizeof(effective_muzzle));
			}
			else
			{
				/* Held-only classic entries use their muzzle cvar as a base. */
				for (component = 0; component < 3; ++component)
					effective_muzzle[component] =
						VR_WeaponMuzzleCvar(slot, component).value;
			}
		}
		if (!VR_CalibrationVectorIsFinite(effective_muzzle) ||
			!VR_LocomotionMuzzleOffsetToWorld(effective_muzzle,
				raw_hand_angles, vr_gunmodelscale.value,
				vr_gunmodelpitch.value, dominant == 0, muzzle_world) ||
			!VR_CalibrationVectorIsFinite(muzzle_world))
		{
			if (created_slot)
				VR_SetCalibrationSlotDefaults(slot);
			Con_Printf("VR: vradjustmuzzle could not calculate the current muzzle point\n");
			return;
		}
		VectorAdd(raw_origin, muzzle_world, muzzle_world);
		if (!VR_CalibrationVectorIsFinite(muzzle_world))
		{
			if (created_slot)
				VR_SetCalibrationSlotDefaults(slot);
			Con_Printf("VR: vradjustmuzzle calculated an invalid world cue\n");
			return;
		}
	}

	memset(adjustment, 0, sizeof(*adjustment));
	adjustment->active = true;
	adjustment->muzzle_mode = muzzle_mode;
	adjustment->physical_hand = dominant;
	adjustment->model_index = model_index;
	adjustment->poseverttype = alias_header->poseverttype;
	adjustment->slot = slot;
	adjustment->created_slot = created_slot;
	adjustment->model = model;
	q_strlcpy(adjustment->model_name, model->name,
		sizeof(adjustment->model_name));
	adjustment->world_scale = vr_world_scale.value;
	adjustment->gunmodelscale = vr_gunmodelscale.value;
	adjustment->gunmodelpitch = vr_gunmodelpitch.value;
	VectorCopy(raw_origin, adjustment->frozen_origin);
	VectorCopy(raw_hand_angles, adjustment->frozen_hand_angles);
	VectorCopy(model_angles, adjustment->frozen_model_angles);
	if (muzzle_mode)
	{
		VectorCopy(muzzle_world, adjustment->frozen_muzzle_world);
		Con_Printf("VR: %s muzzle recenter active for %s; move the grip to place the muzzle cue, release the trigger, then press to set it\n",
			enhanced_format ? "enhanced" : "classic", adjustment->model_name);
	}
	else
		Con_Printf("VR: %s grip adjustment active for %s; release the trigger, then press to set the grip\n",
			enhanced_format ? "enhanced" : "classic", adjustment->model_name);
}

static void VR_WeaponCalibrationAdjustGrip_f(void)
{
	VR_WeaponCalibrationAdjustBegin_f(false);
}

static void VR_WeaponCalibrationAdjustMuzzle_f(void)
{
	VR_WeaponCalibrationAdjustBegin_f(true);
}

static void VR_CalibrationAdjustInputAbort(const char *reason)
{
	Con_Printf("VR: calibration canceled (%s)\n", reason);
	VR_WeaponCalibrationAdjustCancel();
}

void VR_WeaponCalibrationAdjustInput(int physical_hand,
	qboolean trigger_down, qboolean pose_valid, const vec3_t live_origin,
	const vec3_t live_hand_angles)
{
	vr_weapon_calibration_adjustment_t *adjustment =
		&vr_weapon_calibration_adjustment;
	const char *reason;
	vec3_t world_delta, local_delta, effective_offset, new_base;
	float effective_scale, inverse_scale;
	int component;
	qboolean enhanced_format;

	if (!adjustment->active)
		return;
	if (physical_hand != adjustment->physical_hand)
	{
		VR_CalibrationAdjustInputAbort("active controller hand changed");
		return;
	}
	if (!VR_CalibrationAdjustStateValid(adjustment, &reason))
	{
		VR_CalibrationAdjustInputAbort(reason);
		return;
	}
	if (!pose_valid || !live_origin || !live_hand_angles ||
		!VR_CalibrationVectorIsFinite(live_origin) ||
		!VR_CalibrationVectorIsFinite(live_hand_angles))
	{
		VR_CalibrationAdjustInputAbort("controller pose became invalid");
		return;
	}
	if (adjustment->return_to_grip)
	{
		VectorSubtract(live_origin, adjustment->frozen_origin, world_delta);
		if (DotProduct(world_delta, world_delta) <= 64.0f)
		{
			Con_Printf("VR: muzzle recenter complete; returned to the starting grip\n");
			VR_WeaponCalibrationAdjustCancel();
		}
		return;
	}
	if (!adjustment->armed)
	{
		if (!trigger_down)
			adjustment->armed = true;
		return;
	}
	if (!trigger_down)
		return;

	if (adjustment->muzzle_mode)
	{
		VectorSubtract(adjustment->frozen_muzzle_world, live_origin,
			world_delta);
		/* The frozen weapon, not the rotated live wrist, defines the inverse. */
		if (!VR_LocomotionWorldToMuzzleOffset(world_delta,
			adjustment->frozen_hand_angles,
			adjustment->gunmodelscale, adjustment->gunmodelpitch,
			adjustment->physical_hand == 0, local_delta) ||
			!VR_CalibrationVectorIsFinite(local_delta))
		{
			VR_CalibrationAdjustInputAbort("could not convert the muzzle cue movement");
			return;
		}
		enhanced_format = VR_CalibrationPoseTypeIsEnhanced(
			adjustment->poseverttype);
		for (component = 0; component < 3; ++component)
			new_base[component] = local_delta[component];
		if (!VR_CalibrationVectorIsFinite(new_base))
		{
			VR_CalibrationAdjustInputAbort("resulting muzzle offset is invalid");
			return;
		}
		if (enhanced_format)
		{
			vr_weapon_calibration_slot_t *calibration =
				&vr_weapon_calibration_slots[adjustment->slot];
			const qboolean old_has_offset =
				calibration->has_enhanced_muzzle_offset;
			vec3_t old_base;
			memcpy(old_base, calibration->enhanced_muzzle_offset,
				sizeof(old_base));
			memcpy(calibration->enhanced_muzzle_offset, new_base,
				sizeof(new_base));
			calibration->has_enhanced_muzzle_offset = true;
			if (!VR_WeaponCalibrationSave())
			{
				memcpy(calibration->enhanced_muzzle_offset, old_base,
					sizeof(old_base));
				calibration->has_enhanced_muzzle_offset = old_has_offset;
				VR_CalibrationAdjustInputAbort("calibration could not be persisted");
				return;
			}
		}
		else
		{
			const float old_base[3] = {
				VR_WeaponMuzzleCvar(adjustment->slot, VR_WMUZZLE_X).value,
				VR_WeaponMuzzleCvar(adjustment->slot, VR_WMUZZLE_Y).value,
				VR_WeaponMuzzleCvar(adjustment->slot, VR_WMUZZLE_Z).value
			};
			Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(adjustment->slot,
				VR_WMUZZLE_X), new_base[0]);
			Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(adjustment->slot,
				VR_WMUZZLE_Y), new_base[1]);
			Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(adjustment->slot,
				VR_WMUZZLE_Z), new_base[2]);
			if (!VR_WeaponCalibrationSave())
			{
				Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(adjustment->slot,
					VR_WMUZZLE_X), old_base[0]);
				Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(adjustment->slot,
					VR_WMUZZLE_Y), old_base[1]);
				Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(adjustment->slot,
					VR_WMUZZLE_Z), old_base[2]);
				VR_CalibrationAdjustInputAbort("calibration could not be persisted");
				return;
			}
			vr_weapon_calibration_slots[adjustment->slot].has_muzzle_offset = true;
		}
		Con_Printf("VR: %s muzzle recentered for %s; return the grip to its starting point\n",
			enhanced_format ? "enhanced" : "classic", adjustment->model_name);
		adjustment->created_slot = false;
		adjustment->return_to_grip = true;
		return;
	}

	VectorSubtract(adjustment->frozen_origin, live_origin, world_delta);
	inverse_scale = (adjustment->world_scale / 0.75f) *
		adjustment->gunmodelscale;
	if (!isfinite(inverse_scale) || inverse_scale <= 0.0f ||
		!VR_LocomotionWorldToModelOffsetChecked(world_delta,
			adjustment->frozen_model_angles, inverse_scale,
			adjustment->physical_hand == 0, local_delta) ||
		!VR_WeaponCalibrationLookupHeld(adjustment->model_name,
			VR_CalibrationPoseTypeIsEnhanced(adjustment->poseverttype),
			effective_offset, &effective_scale))
	{
		VR_CalibrationAdjustInputAbort("could not convert the grip movement");
		return;
	}

	enhanced_format = VR_CalibrationPoseTypeIsEnhanced(
		adjustment->poseverttype);
	for (component = 0; component < 3; ++component)
		new_base[component] = effective_offset[component] + local_delta[component];
	if (!VR_CalibrationVectorIsFinite(new_base) || !isfinite(effective_scale) ||
		effective_scale <= 0.0f)
	{
		VR_CalibrationAdjustInputAbort("resulting held offset is invalid");
		return;
	}

	if (enhanced_format)
	{
		vr_weapon_calibration_slot_t *calibration =
			&vr_weapon_calibration_slots[adjustment->slot];
		const qboolean old_has_offset = calibration->has_enhanced_held_offset;
		vec3_t old_base;
		memcpy(old_base, calibration->enhanced_held_offset,
			sizeof(old_base));
		memcpy(calibration->enhanced_held_offset, new_base,
			sizeof(new_base));
		calibration->has_enhanced_held_offset = true;
		if (!VR_WeaponCalibrationSave())
		{
			memcpy(calibration->enhanced_held_offset, old_base,
				sizeof(old_base));
			calibration->has_enhanced_held_offset = old_has_offset;
			VR_CalibrationAdjustInputAbort("calibration could not be persisted");
			return;
		}
	}
	else
	{
		vr_weapon_calibration_slot_t *calibration =
			&vr_weapon_calibration_slots[adjustment->slot];
		const qboolean old_has_muzzle = calibration->has_muzzle_offset;
		const float old_base[3] = {
			VR_WeaponOffsetCvar(adjustment->slot, VR_WOFS_X).value,
			VR_WeaponOffsetCvar(adjustment->slot, VR_WOFS_Y).value,
			VR_WeaponOffsetCvar(adjustment->slot, VR_WOFS_Z).value
		};
		const float old_muzzle[3] = {
			VR_WeaponMuzzleCvar(adjustment->slot, VR_WMUZZLE_X).value,
			VR_WeaponMuzzleCvar(adjustment->slot, VR_WMUZZLE_Y).value,
			VR_WeaponMuzzleCvar(adjustment->slot, VR_WMUZZLE_Z).value
		};
		Cvar_SetValueQuick(&VR_WeaponOffsetCvar(adjustment->slot, VR_WOFS_X),
			new_base[0]);
		Cvar_SetValueQuick(&VR_WeaponOffsetCvar(adjustment->slot, VR_WOFS_Y),
			new_base[1]);
		Cvar_SetValueQuick(&VR_WeaponOffsetCvar(adjustment->slot, VR_WOFS_Z),
			new_base[2]);
		if (!old_has_muzzle)
		{
			/* Match ApplySchema's held-only seed in the live slot before
			 * saving, so a reload cannot turn on a different muzzle. */
			Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(adjustment->slot,
				VR_WMUZZLE_X), 0.0f);
			Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(adjustment->slot,
				VR_WMUZZLE_Y), 0.0f);
			Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(adjustment->slot,
				VR_WMUZZLE_Z), new_base[2]);
			calibration->has_muzzle_offset = true;
		}
		if (!VR_WeaponCalibrationSave())
		{
			Cvar_SetValueQuick(&VR_WeaponOffsetCvar(adjustment->slot, VR_WOFS_X),
				old_base[0]);
			Cvar_SetValueQuick(&VR_WeaponOffsetCvar(adjustment->slot, VR_WOFS_Y),
				old_base[1]);
			Cvar_SetValueQuick(&VR_WeaponOffsetCvar(adjustment->slot, VR_WOFS_Z),
				old_base[2]);
			if (!old_has_muzzle)
			{
				for (int axis = 0; axis < 3; ++axis)
					Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(adjustment->slot,
						axis), old_muzzle[axis]);
				calibration->has_muzzle_offset = false;
			}
			VR_CalibrationAdjustInputAbort("calibration could not be persisted");
			return;
		}
	}

	Con_Printf("VR: %s grip adjusted for %s\n",
		enhanced_format ? "enhanced" : "classic", adjustment->model_name);
	adjustment->created_slot = false;
	VR_WeaponCalibrationAdjustCancel();
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

void VR_WeaponCalibrationRegisterCommands(void)
{
	if (vr_weapon_calibration_commands_registered)
		return;
	vr_weapon_calibration_commands_registered = true;
	Cmd_AddCommand("vrweaponsave", VR_WeaponCalibrationSave_f);
	Cmd_AddCommand("vradjustweapon", VR_WeaponCalibrationAdjustGrip_f);
	Cmd_AddCommand("vradjustmuzzle", VR_WeaponCalibrationAdjustMuzzle_f);
}

void VR_WeaponCalibrationReset(void)
{
	int slot;

	VR_WeaponCalibrationAdjustCancel();
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
			!entry->has_muzzle_offset &&
			(!calibration->has_muzzle_offset ||
			 calibration->muzzle_seeded_from_held))
		{
			/* Keep an implicit muzzle at the held Z across later held-only
			 * overrides; an explicitly authored muzzle stays independent. */
			if (entry->has_held_offset)
				Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Z),
							   entry->held_offset[2]);
			calibration->has_muzzle_offset = true;
			calibration->muzzle_seeded_from_held = true;
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
			calibration->muzzle_seeded_from_held = false;
		}
		if (entry->has_enhanced_held_offset)
		{
			memcpy(calibration->enhanced_held_offset,
				   entry->enhanced_held_offset, sizeof(vec3_t));
			calibration->has_enhanced_held_offset = true;
		}
		if (entry->has_enhanced_muzzle_offset)
		{
			memcpy(calibration->enhanced_muzzle_offset,
				   entry->enhanced_muzzle_offset, sizeof(vec3_t));
			calibration->has_enhanced_muzzle_offset = true;
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

static qboolean VR_WeaponCalibrationApplyAlkalineAxeFallback(void)
{
	const char *game = COM_SkipPath(com_gamedir);

	if (!game || (q_strcasecmp(game, "alk") &&
		q_strcasecmp(game, "limjam")))
		return true;
	return VR_WeaponCalibrationApplySchema(vr_alk_axe_fallback,
		sizeof(vr_alk_axe_fallback) / sizeof(vr_alk_axe_fallback[0]));
}

static qboolean VR_WeaponCalibrationApplyADRootFallbacks(void)
{
	const char *game = COM_SkipPath(com_gamedir);

	/* These installed mods contain byte-identical AD root viewmodels. Their
	 * own schemas are applied afterward and remain authoritative. */
	if (!game || (q_strcasecmp(game, "ad") &&
		q_strcasecmp(game, "q30a1024") &&
		q_strcasecmp(game, "gibtropolis") &&
		q_strcasecmp(game, "hwjam4")))
		return true;
	return VR_WeaponCalibrationApplySchema(vr_ad_weapon_fallbacks,
		sizeof(vr_ad_weapon_fallbacks) /
		sizeof(vr_ad_weapon_fallbacks[0]));
}

static qboolean VR_WeaponCalibrationApplyAD171Aliases(void)
{
	const char *game = COM_SkipPath(com_gamedir);
	vr_weapon_schema_entry_t entries[
		sizeof(vr_ad171_weapon_aliases) / sizeof(vr_ad171_weapon_aliases[0])];
	size_t alias_index;

	if (!game || q_strcasecmp(game, "mjolnir"))
		return true;

	for (alias_index = 0;
		 alias_index < sizeof(vr_ad171_weapon_aliases) /
			sizeof(vr_ad171_weapon_aliases[0]); ++alias_index)
	{
		size_t profile_index;
		const vr_weapon_schema_entry_t *profile = NULL;

		for (profile_index = 0;
			 profile_index < sizeof(vr_ad_weapon_fallbacks) /
				sizeof(vr_ad_weapon_fallbacks[0]); ++profile_index)
		{
			if (!strcmp(vr_ad_weapon_fallbacks[profile_index].viewmodel_path,
						vr_ad171_weapon_aliases[alias_index].ad_path))
			{
				profile = &vr_ad_weapon_fallbacks[profile_index];
				break;
			}
		}
		if (!profile)
			return false;

		entries[alias_index] = *profile;
		strcpy(entries[alias_index].viewmodel_path,
			vr_ad171_weapon_aliases[alias_index].alias_path);
	}

	return VR_WeaponCalibrationApplySchema(entries,
		sizeof(entries) / sizeof(entries[0]));
}

static qboolean VR_WeaponCalibrationApplyBuiltinFallbacks(void)
{
	return VR_WeaponCalibrationApplyEnhancedFallbacks() &&
		VR_WeaponCalibrationApplySchema(vr_stock_classic_fallbacks,
			sizeof(vr_stock_classic_fallbacks) /
			sizeof(vr_stock_classic_fallbacks[0])) &&
		VR_WeaponCalibrationApplyAlkalineAxeFallback() &&
		VR_WeaponCalibrationApplyEnyoFallbacks() &&
		VR_WeaponCalibrationApplyADRootFallbacks() &&
		VR_WeaponCalibrationApplyAD171Aliases();
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
										vec3_t out_offset,
										float *out_scale)
{
	const vr_weapon_calibration_slot_t *calibration;
	vec3_t offset = {0.0f, 0.0f, 0.0f};
	float scale = 1.0f;
	int slot;

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
	}
	else
	{
		offset[0] = VR_WeaponOffsetCvar(slot, VR_WOFS_X).value;
		offset[1] = VR_WeaponOffsetCvar(slot, VR_WOFS_Y).value;
		offset[2] = VR_WeaponOffsetCvar(slot, VR_WOFS_Z).value;
		scale = VR_WeaponOffsetCvar(slot, VR_WOFS_SCALE).value;
		if (!isfinite(scale) || scale <= 0.0f)
			return false;
	}

	if (!VR_CalibrationVectorIsFinite(offset))
		return false;
	memcpy(out_offset, offset, sizeof(vec3_t));
	*out_scale = scale;
	return true;
}

qboolean VR_WeaponCalibrationLookupMuzzle(const char *model_name,
										  qboolean enhanced_format,
										  vec3_t out)
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
	}
	else
	{
		if (!calibration->has_muzzle_offset)
			return false;
		base[0] = VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_X).value;
		base[1] = VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Y).value;
		base[2] = VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Z).value;
	}

	if (!isfinite(base[0]) || !isfinite(base[1]) || !isfinite(base[2]))
		return false;
	for (component = 0; component < 3; ++component)
		out[component] = base[component];
	return true;
}

qboolean VR_WeaponCalibrationStockRangedViewmodel(const char *name)
{
	/* Ordinary collision contact publication is pinned to these verified
	 * stock ranged models; generic pose retraction has broader coverage. */
	static const char *const models[] = {
		"progs/v_shot.mdl", "progs/v_shot2.mdl",
		"progs/v_nail.mdl", "progs/v_nail2.mdl",
		"progs/v_rock.mdl", "progs/v_rock2.mdl",
		"progs/v_light.mdl",
		"progs/v_laserg.mdl", "progs/v_prox.mdl",
		"progs/v_lava.mdl", "progs/v_lava2.mdl",
		"progs/v_multi.mdl", "progs/v_multi2.mdl",
		"progs/v_plasma.mdl"
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
										  out))
	{
		const int slot = vr_weapon_calibration_initialized ?
			VR_FindCalibrationSlot(model->name) : -1;
		const qboolean authored_muzzle = slot >= 0 &&
			(enhanced_format ?
			vr_weapon_calibration_slots[slot].has_enhanced_muzzle_offset :
			vr_weapon_calibration_slots[slot].has_muzzle_offset);
		memset(out, 0, sizeof(vec3_t));
		/* The donor fires an otherwise valid uncalibrated alias from the
		 * tracked grip. Do not turn a malformed authored offset into zero. */
		return !authored_muzzle;
	}

	if (!VR_CalibrationVectorIsFinite(out))
	{
		memset(out, 0, sizeof(vec3_t));
		return false;
	}
	return true;
}

/* q30a1024's W_FireSpikes and Mjolnir's W_FireSpikes/Crossbow launch from
 * self.origin + 16 up, without the stock rocket's eight-forward component.
 * Keep this at the QC source boundary rather than creating source-only held
 * calibration slots; an authored muzzle_source_offset remains additive. */
static qboolean VR_WeaponCalibrationUpOnlySource(const char *viewmodel)
{
	const char *game = COM_SkipPath(com_gamedir);
	static const char *const q30_models[] = {
		"progs/v_nail.mdl", "progs/v_nail2.mdl"
	};
	static const char *const mjolnir_models[] = {
		"progs/ad171/v_nail.mdl", "progs/ad171/v_nail2.mdl",
		"progs/its/v_crossbow1.mdl", "progs/its/v_crossbow2.mdl"
	};
	const char *const *models;
	size_t count;

	if (!game || !viewmodel)
		return false;
	if (!q_strcasecmp(game, "q30a1024"))
	{
		models = q30_models;
		count = sizeof(q30_models) / sizeof(q30_models[0]);
	}
	else if (!q_strcasecmp(game, "mjolnir"))
	{
		models = mjolnir_models;
		count = sizeof(mjolnir_models) / sizeof(mjolnir_models[0]);
	}
	else
		return false;
	for (size_t i = 0; i < count; ++i)
		if (!q_strcasecmp(viewmodel, models[i]))
			return true;
	return false;
}

void VR_WeaponCalibrationProjectileSourceOffset(const char *viewmodel,
	int weapon_bit, const vec3_t angles, float viewheight, vec3_t out)
{
	static const vec3_t default_angles = {0.0f, 0.0f, 0.0f};
	static const vec3_t default_forward_offset = {0.0f, 0.0f, 8.0f};
	static const vec3_t up_only_offset = {0.0f, 0.0f, 0.0f};
	const vr_weapon_calibration_slot_t *calibration = NULL;
	const float *safe_angles = angles;
	const float *forward_offset;
	vec3_t source_world;
	int slot;
	int component;
	/* Hipnotic W_FireProximityGrenade calls setorigin(missile, self.origin).
	 * Keep this QC source correction scoped to its game, weapon and model;
	 * an authored vr_weapons.txt source setting still takes precedence. */
	qboolean spawn_at_self_origin = weapon_bit == IT_GRENADE_LAUNCHER ||
		(weapon_bit == HIT_PROXIMITY_GUN && viewmodel &&
		 !q_strcasecmp(COM_SkipPath(com_gamedir), "hipnotic") &&
		 !q_strcasecmp(viewmodel, "progs/v_prox.mdl"));

	if (!out)
		return;
	memset(out, 0, sizeof(vec3_t));

	if (!angles || !VR_CalibrationVectorIsFinite(angles))
		safe_angles = default_angles;
	forward_offset = VR_WeaponCalibrationUpOnlySource(viewmodel) ?
		up_only_offset : default_forward_offset;

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
		if (!VR_LocomotionAimOffsetToWorld(forward_offset,
											  safe_angles, 1.0f, source_world))
		{
			source_world[0] = forward_offset[2];
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
