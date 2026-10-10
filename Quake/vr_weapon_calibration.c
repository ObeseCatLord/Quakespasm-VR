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
	vr_melee_gesture_profile_t melee;
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

enum
{
	VR_WEAPON_PRESET_VANILLA,
	VR_WEAPON_PRESET_ENHANCED,
	VR_WEAPON_PRESET_AUTHENTIC,
	VR_WEAPON_PRESET_PLAGUE,
	VR_WEAPON_PRESET_BLOCKQUAKE,
	VR_WEAPON_PRESET_AD,
	VR_WEAPON_PRESET_COUNT
};

static cvar_t vr_gunmodeloffsets = {"vr_gunmodeloffsets", "0", CVAR_ARCHIVE};
static int vr_weapon_preset_accepted = VR_WEAPON_PRESET_VANILLA;

static qboolean VR_WeaponCalibrationPreflightSchema(
	const vr_weapon_schema_entry_t *entries, size_t count);
static void VR_WeaponCalibrationPresetChanged(cvar_t *var);

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
	const aliashdr_t *geometry;
	char model_name[MAX_QPATH];
	float world_scale;
	float gunmodelscale;
	float gunmodelpitch;
	float model_offset_scale;
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

/* Generic donor v_axe2 default. The inherited QBJ3 profile authors its own
 * wrench/fist geometry and deliberately excludes this fallback. */
static const vr_weapon_schema_entry_t vr_copper_axe_fallback[] = {
	{
		.viewmodel_path = "progs/v_axe2.mdl",
		.held_offset = {-3.5f, 34.0f, 41.5f}, .has_held_offset = true,
		.held_scale = 0.33f, .has_held_scale = true,
	},
};

/* Bonk has no authored hammer offsets in the donor InitAllWeaponCVars or
 * installed schema. Preserve its neutral source-model calibration (0,0,0; 1)
 * for every audited cosmetic. File/user schema fields still override these.
 * These held-only entries deliberately do not enable gesture attack input. */
#define VR_BONK_HELD(path) \
	{ .viewmodel_path = path, .held_offset = {0.0f, 0.0f, 0.0f}, \
	  .has_held_offset = true, .held_scale = 1.0f, .has_held_scale = true }
static const vr_weapon_schema_entry_t vr_bonk_hammer_fallbacks[] = {
	VR_BONK_HELD("progs/v_hammer_default.mdl"),
	VR_BONK_HELD("progs/v_hammer_default_bloody.mdl"),
	VR_BONK_HELD("progs/v_hammer_default_gold.mdl"),
	VR_BONK_HELD("progs/v_hammer_default_gold_bloody.mdl"),
	VR_BONK_HELD("progs/v_hammer_alkaline_axe.mdl"),
	VR_BONK_HELD("progs/v_hammer_sblade.mdl"),
	VR_BONK_HELD("progs/v_hammer_buster_sword.mdl"),
	VR_BONK_HELD("progs/v_hammer_pickaxe.mdl"),
	VR_BONK_HELD("progs/v_hammer_katana.mdl"),
	VR_BONK_HELD("progs/v_hammer_copper_axe.mdl"),
	VR_BONK_HELD("progs/v_hammer_baseball.mdl"),
	VR_BONK_HELD("progs/v_hammer_moving_past_it.mdl"),
	VR_BONK_HELD("progs/v_hammer_mailbox.mdl"),
	VR_BONK_HELD("progs/v_hammer_heavy_rocket.mdl"),
	VR_BONK_HELD("progs/v_hammer_burger.mdl"),
	VR_BONK_HELD("progs/v_hammer_guitar.mdl"),
	VR_BONK_HELD("progs/v_hammer_dwarven.mdl"),
	VR_BONK_HELD("progs/v_hammer_jester_mallet.mdl"),
	VR_BONK_HELD("progs/v_hammer_error.mdl"),
	VR_BONK_HELD("progs/v_hammer_sailor_sceptre.mdl"),
	VR_BONK_HELD("progs/v_hammer_floyd.mdl"),
	VR_BONK_HELD("progs/v_hammer_kebby_gears.mdl"),
	VR_BONK_HELD("progs/v_hammer_squeaky.mdl"),
	VR_BONK_HELD("progs/v_hammer_sentinel.mdl"),
	VR_BONK_HELD("progs/v_hammer_pirate_skull.mdl"),
	VR_BONK_HELD("progs/v_hammer_stop_sign.mdl"),
	VR_BONK_HELD("progs/v_hammer_blocky_axe.mdl"),
	VR_BONK_HELD("progs/v_hammer_brown_brick.mdl"),
	VR_BONK_HELD("progs/v_hammer_mace.mdl"),
};
#undef VR_BONK_HELD

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

/* Full-byte FNV-1a signatures of installed AD assets, not mod identities.
 * The older nailgun differs only in scale/origin/radius float encoding:
 * all bytes after its 84-byte header match, with decoded position differences
 * below 0.000115 Quake units. Both versions use the same held calibration.
 * Installed reskins (d3d8b117, c79ffca6, 18f7a730, 7242f947) retain topology,
 * animation poses/timing and normals; only skin/UV data and the same nailgun
 * header encoding differ for the first three. The fourth reuses shot3 geometry
 * (the same calibrated tuple), with float rounding and ignored trailing bytes.
 * See docs/ad-calibration-installed-audit-2026-10-04.md. */
static const struct
{
	const char *path;
	int length;
	unsigned hash;
} vr_ad_weapon_signatures[] = {
	{"progs/v_shot.mdl",     22260, 0x77592100u},
	{"progs/v_shot2.mdl",    22764, 0x8fb04e6fu},
	{"progs/v_shot2.mdl",   131164, 0x7242f947u},
	{"progs/v_shot3.mdl",    77868, 0xabc6f98du},
	{"progs/v_nail.mdl",     47140, 0xa82e429au},
	{"progs/v_nail.mdl",     47140, 0xd5e4c067u},
	{"progs/v_nail.mdl",     47140, 0xd3d8b117u},
	{"progs/v_nail2.mdl",    48964, 0xb9f1559eu},
	{"progs/v_nail2.mdl",    48964, 0xc79ffca6u},
	{"progs/v_rock.mdl",     37420, 0x88977215u},
	{"progs/v_rock2.mdl",    39668, 0x0a7cfc81u},
	{"progs/v_rock2.mdl",    39668, 0x18f7a730u},
	{"progs/v_light.mdl",    22212, 0xf7098ba4u},
	{"progs/v_plasma.mdl",   54940, 0xc5de8c00u},
	{"progs/v_shadaxe0.mdl", 97860, 0xadc010afu},
	{"progs/v_shadaxe1.mdl", 97860, 0x730d17bcu},
	{"progs/v_shadaxe2.mdl", 97860, 0x05185939u},
	{"progs/v_shadaxe3.mdl", 97860, 0x22f9a757u},
	{"progs/v_shadaxe4.mdl", 97860, 0x581c9d25u},
	{"progs/v_shadaxe5.mdl", 97860, 0x27c623d3u},
};

/* These aliases reuse canonical profiles only after effective-asset identity
 * succeeds. Similarly named replacements can have different geometry. */
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
	{"progs/ad171/v_nail.mdl", "progs/v_nail.mdl"},
	{"progs/ad171/v_nail2.mdl", "progs/v_nail2.mdl"},
};

/* Only the generic branch receives AD inheritance; authored special presets
 * keep ownership of their calibration and bypass the copied-default filter. */
static qboolean vr_weapon_calibration_inherits_ad;
static qboolean vr_ad_weapon_identified[countof(vr_ad_weapon_fallbacks)];
static qboolean vr_ad171_weapon_identified[countof(vr_ad171_weapon_aliases)];

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

typedef struct
{
	const char *path;
	vec3_t held;
	float scale;
} vr_weapon_preset_row_t;

typedef struct
{
	const char *path;
	vec3_t held;
	float scale;
	vec3_t muzzle;
} vr_weapon_preset_muzzle_row_t;

static const vr_weapon_preset_row_t vr_enhanced_classic_overrides[] = {
	{"progs/v_shot2.mdl", {-6.0f, 0.3f, 7.0f}, 0.9f},
	{"progs/v_nail.mdl", {-1.9f, 5.7f, 15.0f}, 0.4f},
	{"progs/v_nail2.mdl", {5.5f, 3.6f, 19.0f}, 0.4f},
	{"progs/v_rock.mdl", {10.0f, 1.2f, 13.0f}, 0.5f},
	{"progs/v_rock2.mdl", {26.0f, 4.5f, 21.0f}, 0.3f},
	{"progs/v_light.mdl", {12.0f, 3.0f, 13.0f}, 0.5f},
};

static const vr_weapon_preset_row_t vr_authentic_classic_overrides[] = {
	{"progs/v_axe.mdl", {-1.0f, 24.0f, 37.0f}, 0.33f},
	{"progs/v_shot.mdl", {-1.0f, 2.0f, 15.3f}, 0.33f},
	{"progs/v_shot2.mdl", {-2.0f, 2.0f, 11.4f}, 0.5f},
	{"progs/v_nail.mdl", {-7.0f, 4.0f, 15.7f}, 0.4f},
	{"progs/v_nail2.mdl", {-13.6f, 4.0f, 17.8f}, 0.4f},
	{"progs/v_rock.mdl", {11.0f, 2.0f, 12.5f}, 0.5f},
	{"progs/v_rock2.mdl", {23.0f, 5.0f, 31.0f}, 0.3f},
	{"progs/v_light.mdl", {-6.0f, 3.6f, 11.0f}, 0.5f},
	{"progs/v_prox.mdl", {-2.4f, 1.8f, 14.6f}, 0.5f},
	{"progs/v_lava.mdl", {-10.2f, 4.0f, 15.7f}, 0.4f},
	{"progs/v_lava2.mdl", {-13.6f, 4.0f, 17.8f}, 0.4f},
	{"progs/v_multi.mdl", {11.0f, 2.0f, 12.5f}, 0.5f},
	{"progs/v_multi2.mdl", {23.0f, 5.0f, 31.0f}, 0.3f},
	{"progs/v_plasma.mdl", {-6.0f, 3.6f, 11.0f}, 0.5f},
};

static const vr_weapon_preset_row_t vr_plague_classic_overrides[] = {
	{"progs/v_shot.mdl", {-1.0f, 1.3f, 7.0f}, 0.6f},
	{"progs/v_shot2.mdl", {-5.9f, 1.1f, 8.5f}, 0.6f},
	{"progs/v_nail.mdl", {-11.0f, 5.1f, 19.0f}, 0.32f},
	{"progs/v_nail2.mdl", {-11.6f, 4.6f, 21.8f}, 0.26f},
	{"progs/v_rock.mdl", {-3.5f, 2.6f, 12.0f}, 0.36f},
	{"progs/v_rock2.mdl", {-7.2f, 4.0f, 18.2f}, 0.32f},
	{"progs/v_light.mdl", {-3.1f, 4.4f, 14.2f}, 0.37f},
	{"progs/v_laserg.mdl", {-5.0f, 3.4f, 22.0f}, 0.33f},
	{"progs/v_prox.mdl", {-3.5f, 2.6f, 12.0f}, 0.36f},
	{"progs/v_lava.mdl", {-11.0f, 5.1f, 19.0f}, 0.32f},
	{"progs/v_lava2.mdl", {-11.6f, 4.6f, 21.8f}, 0.26f},
	{"progs/v_multi.mdl", {-3.5f, 2.6f, 12.0f}, 0.36f},
	{"progs/v_multi2.mdl", {-7.2f, 4.0f, 18.2f}, 0.32f},
	{"progs/v_plasma.mdl", {-3.1f, 4.4f, 14.2f}, 0.37f},
};

static const vr_weapon_preset_row_t vr_blockquake_classic[] = {
	{"progs/v_axe.mdl", {-9.0f, 38.0f, 45.0f}, 0.2f},
	{"progs/v_shot.mdl", {-7.0f, 6.8f, 35.5f}, 0.2f},
	{"progs/v_shot2.mdl", {-5.6f, 10.2f, 42.0f}, 0.2f},
	{"progs/v_nail.mdl", {-9.0f, 15.0f, 40.0f}, 0.2f},
	{"progs/v_nail2.mdl", {-6.0f, 13.5f, 39.0f}, 0.2f},
	{"progs/v_rock.mdl", {0.0f, 11.8f, 72.0f}, 0.2f},
	{"progs/v_rock2.mdl", {26.0f, 13.8f, 69.0f}, 0.2f},
	{"progs/v_light.mdl", {-9.0f, 13.5f, 51.0f}, 0.2f},
};

static const vr_weapon_preset_row_t vr_alk_fixed_fallbacks[] = {
	{"progs/v_saw.mdl", {-6.5f, 27.5f, 42.0f}, 0.25f},
	{"progs/v_plasma.mdl", {16.0f, 6.0f, 16.0f}, 0.4f},
};

static const vr_weapon_preset_row_t vr_alk_classic[] = {
	{"progs/v_shot40fps.mdl", {1.5f, 1.8f, 15.8f}, 0.33f},
	{"progs/v_shot2_40fps.mdl", {-3.0f, 1.7f, 12.0f}, 0.5f},
	{"progs/v_nail_alk40fps.mdl", {-6.0f, 3.8f, 17.0f}, 0.38f},
	{"progs/v_nail3.mdl", {-4.0f, 3.5f, 19.0f}, 0.35f},
	{"progs/v_rock_40fps.mdl", {-3.0f, 1.25f, 17.0f}, 0.5f},
	{"progs/v_rock2_40fps.mdl", {20.0f, 8.3f, 21.0f}, 0.38f},
	{"progs/v_light.mdl", {-4.0f, 3.1f, 13.0f}, 0.5f},
	{"progs/v_laserg40fps.mdl", {45.0f, 3.0f, 15.0f}, 0.22f},
	{"progs/v_mine_40fps.mdl", {-3.0f, 1.3f, 15.0f}, 0.5f},
};

static const vr_weapon_preset_row_t vr_alk_plague[] = {
	{"progs/v_shot40fps.mdl", {-1.0f, 1.3f, 7.0f}, 0.6f},
	{"progs/v_shot2_40fps.mdl", {-5.9f, 1.1f, 8.5f}, 0.6f},
	{"progs/v_nail_alk40fps.mdl", {-11.0f, 5.1f, 19.0f}, 0.32f},
	{"progs/v_nail3.mdl", {-11.6f, 4.6f, 21.8f}, 0.26f},
	{"progs/v_rock_40fps.mdl", {-3.5f, 2.6f, 12.0f}, 0.36f},
	{"progs/v_rock2_40fps.mdl", {-7.2f, 4.0f, 18.2f}, 0.32f},
	{"progs/v_light.mdl", {-3.1f, 4.4f, 14.2f}, 0.37f},
	{"progs/v_laserg40fps.mdl", {-5.0f, 3.4f, 22.0f}, 0.33f},
	{"progs/v_mine_40fps.mdl", {-3.5f, 2.6f, 12.0f}, 0.36f},
};

static const vr_weapon_preset_row_t vr_dwell_weapon_fallbacks[] = {
	{"progs/v_axe2.mdl", {-3.5f, 34.0f, 41.5f}, 0.4f},
	{"progs/v_axeb.mdl", {-4.0f, 24.0f, 37.0f}, 0.4f},
	{"progs/v_shot.mdl", {1.5f, 1.0f, 10.0f}, 0.3333333f},
	{"progs/v_shot2.mdl", {-3.5f, 1.0f, 8.5f}, 0.5333333f},
	{"progs/v_shot3.mdl", {-3.5f, 0.4f, 8.5f}, 0.5333333f},
	{"progs/v_nail.mdl", {-5.0f, 3.0f, 15.0f}, 0.5f},
	{"progs/v_nail2.mdl", {0.0f, 3.0f, 19.0f}, 0.5f},
	{"progs/v_nail3.mdl", {-4.0f, 3.5f, 19.0f}, 0.35f},
	{"progs/v_rock.mdl", {10.0f, 1.5f, 13.0f}, 0.5f},
	{"progs/v_rock2.mdl", {10.0f, 7.0f, 19.0f}, 0.5f},
	{"progs/v_light.mdl", {3.0f, 4.0f, 13.0f}, 0.5f},
	{"progs/v_rail.mdl", {4.0f, 5.0f, 31.0f}, 0.65f},
	{"progs/v_rifle.mdl", {1.5f, 1.0f, 10.0f}, 0.5f},
};

static const vr_weapon_preset_muzzle_row_t vr_snack_weapon_fallbacks[] = {
	{"progs/v_axe2.mdl", {-3.5f, 34.0f, 41.5f}, 0.33f, {0.0f, 0.0f, 41.5f}},
	{"progs/v_shot.mdl", {1.5f, 1.0f, 10.0f}, 0.5f, {0.0f, 0.0f, 10.0f}},
	{"progs/v_shot2.mdl", {-3.5f, 1.0f, 8.5f}, 0.8f, {0.0f, 0.0f, 8.5f}},
	{"progs/v_nail.mdl", {-18.119550f, 29.178477f, 113.478652f}, 0.1666667f,
		{0.015233f, 7.372823f, 18.445326f}},
	{"progs/v_nail2.mdl", {-0.301060f, 69.561041f, 136.059723f}, 0.1666667f,
		{-0.072624f, 5.060089f, 31.53750f}},
	{"progs/v_rock.mdl", {10.0f, 1.5f, 13.0f}, 0.5f, {0.0f, 0.0f, 13.0f}},
	{"progs/v_rock2.mdl", {10.0f, 7.0f, 19.0f}, 0.5f, {0.0f, 0.0f, 19.0f}},
	{"progs/v_light.mdl", {3.0f, 4.0f, 13.0f}, 0.5f, {0.0f, 0.0f, 13.0f}},
	{"progs/v_shot3.mdl", {-3.5f, 0.4f, 8.5f}, 0.5333333f, {0.0f, 0.0f, 8.5f}},
};

/* Peril 3.0 pak0 ready-pose landmarks, not the generic installed profile.
 * R_AliasModelMatrix translates by origin + held, then scales vertices:
 * held = -scale*grip - (1-scale)*origin. Muzzles use right/up/forward
 * coordinates at the default world scale (the renderer multiplies by 1/.75).
 * Pair anchors retain decoded source coordinates and use the draw matrix.
 * Includes dormant viewmodels; see docs/peril-vr-weapons-2.0-plan.md. */
static const vr_weapon_preset_muzzle_row_t vr_peril_weapon_fallbacks[] = {
	{"progs/v_shadaxe0.mdl", {-2.875082f, 42.074010f, 37.998579f}, 0.25f, {0.000000f, 0.000000f, 0.000000f}},
	{"progs/v_shadaxe3.mdl", {26.605468f, 74.755644f, 42.675916f}, 0.25f, {0.000000f, 0.000000f, 0.000000f}},
	{"progs/v_ghook.mdl", {0.398149f, 3.974375f, 20.716595f}, 0.5f, {0.000000f, 0.466667f, 9.333333f}},
	{"progs/v_shot.mdl", {-5.473787f, 1.827324f, 16.828860f}, 0.5f, {0.666667f, 0.000000f, 5.266667f}},
	{"progs/v_shot2.mdl", {-1.245300f, 5.547600f, 18.678106f}, 0.5f, {2.833333f, 2.200000f, 7.666667f}},
	{"progs/v_shot3.mdl", {-3.576900f, 7.645268f, 20.651891f}, 0.5f, {2.800000f, -0.133333f, 10.266667f}},
	{"progs/v_nail.mdl", {-10.759785f, 16.344555f, 22.411512f}, 0.5f, {-1.992039f, 6.550682f, 11.076865f}},
	{"progs/v_nail2.mdl", {-2.792916f, 12.398437f, 17.963036f}, 0.5f, {-5.573333f, 1.466667f, 8.266667f}},
	{"progs/v_rock.mdl", {12.130318f, 4.749184f, 14.611477f}, 0.5f, {0.000000f, 1.066667f, 8.000000f}},
	{"progs/v_rock2.mdl", {-8.031301f, 10.252375f, 13.595063f}, 0.5f, {-4.666667f, 0.733333f, 6.333333f}},
	{"progs/v_light.mdl", {-12.000000f, 9.519683f, 20.873978f}, 0.5f, {-1.666667f, 6.666667f, 8.666667f}},
	{"progs/v_plasma.mdl", {-11.000000f, 3.594348f, 18.724479f}, 0.5f, {0.000000f, 2.333333f, 11.333333f}},
	{"progs/v_axe.mdl", {13.807513f, 17.004711f, 45.156413f}, 0.33f, {0.000000f, 0.000000f, 0.000000f}},
	{"progs/v_nail3.mdl", {-10.800000f, 13.374923f, 20.290589f}, 0.4f, {-2.666667f, 3.200000f, 17.066667f}},
	{"progs/v_rock3.mdl", {-4.330201f, 4.048523f, 17.036097f}, 0.5f, {0.000000f, 2.000000f, 7.333333f}},
	{"progs/v_zershot.mdl", {2.352700f, 2.125000f, 17.102712f}, 0.5f, {-0.666667f, 4.000000f, 4.666667f}},
	{"progs/v_shadaxe1.mdl", {26.605468f, 74.755644f, 42.675916f}, 0.25f, {0.000000f, 0.000000f, 0.000000f}},
	{"progs/v_shadaxe2.mdl", {26.605468f, 74.755644f, 42.675916f}, 0.25f, {0.000000f, 0.000000f, 0.000000f}},
	{"progs/v_shadaxe4.mdl", {26.605468f, 74.755644f, 42.675916f}, 0.25f, {0.000000f, 0.000000f, 0.000000f}},
	{"progs/v_shadaxe5.mdl", {26.605468f, 74.755644f, 42.675916f}, 0.25f, {0.000000f, 0.000000f, 0.000000f}},
};

static const vr_weapon_preset_muzzle_row_t vr_qbj3_weapon_fallbacks[] = {
	{"progs/v_wrench.mdl", {-5.090864f, 45.71518f, 64.70464f}, 0.2f, {0, 0, 0}},
	{"progs/v_pistol.mdl", {3.388845f, 37.75988f, 56.43581f}, 0.2f, {-9.11632f, 9.013277f, -45.533f}},
	{"progs/v_flakshotgun.mdl", {11.55282f, 16.95288f, 38.90591f}, 0.2f, {0.1453177f, 2.258818f, -31.45429f}},
	{"progs/v_tnailgun.mdl", {-3.596274f, 22.3977f, 49.35181f}, 0.2f, {-4.46999f, 4.069127f, -25.89618f}},
	{"progs/v_rebar.mdl", {7.11163f, 33.89061f, 52.88078f}, 0.2f, {0.4432641f, 12.53425f, 4.832447f}},
	{"progs/v_grenlauncher.mdl", {3.906817f, 19.53125f, 46.88503f}, 0.2f, {-0.9725167f, 6.330487f, 6.072494f}},
	{"progs/v_mmml.mdl", {8.254588f, 17.25331f, 53.56533f}, 0.2f, {-0.387794f, 15.84945f, -4.354416f}},
	{"progs/v_invoker.mdl", {-0.4811821f, 24.50682f, 24.40611f}, 0.2f, {0, 0, 0}},
	{"progs/v_berserk.mdl", {3.20228348f, 45.25361633f, 34.16704407f}, 0.2f, {0, 0, 0}},
	{"progs/v_axe.mdl", {0, 0, 0}, 0.33f, {0, 0, 0}},
	{"progs/v_shot.mdl", {0, 0, 0}, 0.5f, {0, 0, 0}},
	{"progs/v_shot2.mdl", {0, 0, 0}, 0.8f, {0, 0, 0}},
	{"progs/v_nail.mdl", {0, 0, 0}, 0.5f, {0, 0, 0}},
	{"progs/v_nail2.mdl", {0, 0, 0}, 0.5f, {0, 0, 0}},
	{"progs/v_rock.mdl", {0, 0, 0}, 0.5f, {0, 0, 0}},
	{"progs/v_rock2.mdl", {0, 0, 0}, 0.5f, {0, 0, 0}},
	{"progs/v_light.mdl", {0, 0, 0}, 0.5f, {0, 0, 0}},
	{"progs/v_hammer.mdl", {0, 0, 0}, 0.33f, {0, 0, 0}},
	{"progs/v_laserg.mdl", {0, 0, 0}, 0.33f, {0, 0, 0}},
	{"progs/v_prox.mdl", {0, 0, 0}, 0.5f, {0, 0, 0}},
	{"progs/v_lava.mdl", {0, 0, 0}, 0.5f, {0, 0, 0}},
	{"progs/v_lava2.mdl", {0, 0, 0}, 0.5f, {0, 0, 0}},
	{"progs/v_multi.mdl", {0, 0, 0}, 0.5f, {0, 0, 0}},
	{"progs/v_multi2.mdl", {0, 0, 0}, 0.5f, {0, 0, 0}},
	{"progs/v_plasma.mdl", {0, 0, 0}, 0.5f, {0, 0, 0}},
	{"progs/v_axe2.mdl", {0, 0, 0}, 0.33f, {0, 0, 0}},
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
		entry->has_enhanced_muzzle_offset || entry->melee.has_enabled ||
		entry->melee.has_base || entry->melee.has_tip || entry->melee.has_speed ||
		entry->melee.has_ready_frame;
}

static qboolean VR_CalibrationEntryIsFinite(
	const vr_weapon_schema_entry_t *entry)
{
	if ((entry->has_held_offset &&
		 !VR_CalibrationVectorIsFinite(entry->held_offset)) ||
		(entry->has_held_scale && !isfinite(entry->held_scale)) ||
		(entry->has_muzzle_offset &&
		 !VR_CalibrationVectorIsFinite(entry->muzzle_offset)) ||
		(entry->has_muzzle_source_offset &&
		 !VR_CalibrationVectorIsFinite(entry->muzzle_source_offset)) ||
		(entry->has_enhanced_held_offset &&
		 !VR_CalibrationVectorIsFinite(entry->enhanced_held_offset)) ||
		(entry->has_enhanced_muzzle_offset &&
		 !VR_CalibrationVectorIsFinite(entry->enhanced_muzzle_offset)))
		return false;

	if ((entry->melee.has_base && !VR_CalibrationVectorIsFinite(entry->melee.base)) ||
		(entry->melee.has_tip && !VR_CalibrationVectorIsFinite(entry->melee.tip)) ||
		(entry->melee.has_speed && (!isfinite(entry->melee.speed) ||
		 entry->melee.speed < 0.25f || entry->melee.speed > 10.0f)) ||
		(entry->melee.has_ready_frame &&
		 (entry->melee.ready_frame < 0 || entry->melee.ready_frame > 65535)))
		return false;
	for (int axis = 0; axis < 3; ++axis)
		if ((entry->melee.has_base && fabsf(entry->melee.base[axis]) > 4096.0f) ||
			(entry->melee.has_tip && fabsf(entry->melee.tip[axis]) > 4096.0f))
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
		VR_CalibrationLineStartsWithKey(line, len, "muzzle_offset");
}

static qboolean VR_CalibrationLineIsEnhancedKey(const char *line, size_t len)
{
	return VR_CalibrationLineStartsWithKey(line, len,
											"enhanced_held_offset") ||
		VR_CalibrationLineStartsWithKey(line, len,
										"enhanced_muzzle_offset");
}

static qboolean VR_CalibrationLineIsMultiplayerKey(const char *line, size_t len)
{
	return VR_CalibrationLineStartsWithKey(line, len, "mp_held_offset") ||
		VR_CalibrationLineStartsWithKey(line, len, "mp_muzzle_offset") ||
		VR_CalibrationLineStartsWithKey(line, len, "enhanced_mp_held_offset") ||
		VR_CalibrationLineStartsWithKey(line, len, "enhanced_mp_muzzle_offset");
}

static int VR_CalibrationSchemaValueArity(const char *key)
{
	return !strcmp(key, "offset") || !strcmp(key, "held_offset") ||
		!strcmp(key, "mp_held_offset") || !strcmp(key, "muzzle_offset") ||
		!strcmp(key, "mp_muzzle_offset") ||
		!strcmp(key, "enhanced_held_offset") ||
		!strcmp(key, "enhanced_mp_held_offset") ||
		!strcmp(key, "enhanced_muzzle_offset") ||
		!strcmp(key, "enhanced_mp_muzzle_offset") ||
		!strcmp(key, "melee_base") || !strcmp(key, "melee_tip") ||
		!strcmp(key, "muzzle_source_offset") ? 3 : 1;
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
	const char *cursor = block;
	const char *end = block + len;
	char key[COM_PARSE_MAX_TOKEN_SIZE];
	char value[COM_PARSE_MAX_TOKEN_SIZE];
	const char *selected_viewmodel;

	while (cursor < end)
	{
		const char *key_start;
		const char *next;
		qboolean parse_error = false;

		next = COM_ParseExBufferSpan(cursor, CPE_NOTRUNC, key,
			sizeof(key), &parse_error, &key_start);
		if (!next || parse_error || next > end || !key_start ||
			key_start < cursor || key_start >= end)
			return false;
		if (!strcmp(key, "}"))
			break;
		if (!strcmp(key, "{"))
		{
			cursor = next;
			continue;
		}
		cursor = next;
		for (int i = 0; i < VR_CalibrationSchemaValueArity(key); ++i)
		{
			const char *value_start;
			const char *value_next = COM_ParseExBufferSpan(cursor,
				CPE_NOTRUNC, value, sizeof(value), &parse_error,
				&value_start);
			if (!value_next || parse_error || value_next > end ||
				!value_start || value_start < cursor || value_start >= end ||
				!strcmp(value, "{") || !strcmp(value, "}"))
				return false;
			if (i == 0 && (!strcmp(key, "viewmodel") ||
				!strcmp(key, "held_model") || !strcmp(key, "model")))
			{
				qboolean explicit_key = !strcmp(key, "viewmodel") ||
					!strcmp(key, "held_model");
				q_strlcpy(explicit_key ? explicit_viewmodel : model_fallback,
					value, explicit_key ? sizeof(explicit_viewmodel) :
					sizeof(model_fallback));
			}
			cursor = value_next;
		}
	}
	/* VR_WeaponSchemaParse uses model only when no non-empty explicit
	 * viewmodel/held_model path was supplied. */
	selected_viewmodel = explicit_viewmodel[0] ? explicit_viewmodel :
		model_fallback;
	return selected_viewmodel[0] && !strcmp(selected_viewmodel, model);
}

static qboolean VR_CalibrationAppendAdjustmentLines(
	vr_calibration_textbuf_t *buf, int slot, qboolean enhanced_format,
	qboolean force_classic_muzzle)
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
	if (calibration->has_muzzle_offset || force_classic_muzzle)
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
	qboolean enhanced_format, qboolean global_mode)
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
					enhanced_format, global_mode) &&
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
		values = VR_CalibrationSchemaValueArity(key);
		selected = (!global_mode &&
			VR_CalibrationLineIsMultiplayerKey(key, strlen(key))) || (enhanced_format ?
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
	size_t original_count, const vr_weapon_schema_metadata_t *original_metadata)
{
	vr_weapon_schema_entry_t entries[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	vr_weapon_schema_metadata_t metadata;
	const vr_weapon_calibration_slot_t *calibration =
		&vr_weapon_calibration_slots[slot];
	size_t count;
	int matches = 0;

	if (!VR_WeaponSchemaParseWithMetadata(text, entries,
		VR_WEAPON_SCHEMA_MAX_ENTRIES, &count, &metadata) ||
		metadata.complete_roster != original_metadata->complete_roster ||
		count < original_count ||
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

static qboolean VR_CalibrationAppendTopLevelWithoutClassicGlobals(
	vr_calibration_textbuf_t *buf, const char *text, size_t len)
{
	const char *cursor = text;
	const char *copied = text;
	const char *end = text + len;
	char key[COM_PARSE_MAX_TOKEN_SIZE];
	char value[COM_PARSE_MAX_TOKEN_SIZE];

	while (cursor < end)
	{
		const char *key_start;
		const char *next;
		qboolean parse_error = false;
		int values = 0;
		qboolean selected;

		next = COM_ParseExBufferSpan(cursor, CPE_NOTRUNC, key,
			sizeof(key), &parse_error, &key_start);
		if (parse_error)
			return false;
		if (!next)
			break;
		if (!key_start || key_start < cursor)
			return false;
		/* The native lexer may skip trailing trivia to the next block. */
		if (key_start >= end)
			break;
		if (next > end)
			return false;
		selected = !strcmp(key, "global_held_scale") ||
			!strcmp(key, "global_held_offset") || !strcmp(key, "global_muzzle_offset");
		if (!strcmp(key, "global_held_scale"))
			values = 1;
		else if (!strcmp(key, "global_held_offset") ||
			!strcmp(key, "global_muzzle_offset") ||
			!strcmp(key, "global_mp_held_offset") || !strcmp(key, "global_mp_muzzle_offset"))
			values = 3;
		else if (!strcmp(key, "roster"))
			values = 1; /* Native unknown top-level keys consume zero values. */
		if (!values)
		{
			cursor = next;
			continue;
		}
		if (selected && !VR_CalibrationTextAppendN(buf, copied,
			(size_t)(key_start - copied)))
			return false;
		cursor = next;
		for (int i = 0; i < values; ++i)
		{
			const char *value_start;
			const char *value_next;
			parse_error = false;
			value_next = COM_ParseExBufferSpan(cursor, CPE_NOTRUNC, value,
				sizeof(value), &parse_error, &value_start);
			if (!value_next || parse_error || value_next > end ||
				!value_start || value_start < cursor || value_start >= end ||
				!strcmp(value, "{") || !strcmp(value, "}"))
				return false;
			if (selected && !VR_CalibrationTextAppendN(buf, cursor,
				(size_t)(value_start - cursor)))
				return false;
			cursor = value_next;
		}
		if (selected)
			copied = cursor;
	}
	return VR_CalibrationTextAppendN(buf, copied, (size_t)(end - copied));
}

static qboolean VR_CalibrationAppendGlobalAdjustmentLines(
	vr_calibration_textbuf_t *buf, int slot)
{
	char line[192];

	q_snprintf(line, sizeof(line), "global_held_scale %.7g",
		VR_WeaponOffsetCvar(slot, VR_WOFS_SCALE).value);
	if (!VR_CalibrationTextAppendLine(buf, line))
		return false;
	q_snprintf(line, sizeof(line), "global_held_offset %.7g %.7g %.7g",
		VR_WeaponOffsetCvar(slot, VR_WOFS_X).value,
		VR_WeaponOffsetCvar(slot, VR_WOFS_Y).value,
		VR_WeaponOffsetCvar(slot, VR_WOFS_Z).value);
	if (!VR_CalibrationTextAppendLine(buf, line))
		return false;
	q_snprintf(line, sizeof(line), "global_muzzle_offset %.7g %.7g %.7g",
		VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_X).value,
		VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Y).value,
		VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Z).value);
	return VR_CalibrationTextAppendLine(buf, line);
}

static qboolean VR_CalibrationGeneratedGlobalsVisible(const char *text,
	size_t first, size_t last)
{
	static const char *const keys[] = {
		"global_held_scale", "global_held_offset", "global_muzzle_offset"};
	const char *cursor = text;
	char token[COM_PARSE_MAX_TOKEN_SIZE];
	int found = 0;

	/* Walk from byte zero: lexing the emitter slice alone misses an open
	 * source comment. Offsets remain valid when the output buffer reallocates. */
	for (;;)
	{
		const char *start;
		qboolean parse_error;
		const char *next = COM_ParseExBufferSpan(cursor, CPE_NOTRUNC,
			token, sizeof(token), &parse_error, &start);
		if (parse_error)
			return false;
		if (!next)
			return found == 3;
		if (found < 3 && start && start >= text + first && start < text + last &&
			next <= text + last && !strcmp(token, keys[found]))
			++found;
		cursor = next;
	}
}

static void VR_CalibrationFillSnapshotEntry(vr_weapon_schema_entry_t *entry,
	const char *model, int snapshot_slot)
{
	const vr_weapon_calibration_slot_t *calibration =
		&vr_weapon_calibration_slots[snapshot_slot];

	memset(entry, 0, sizeof(*entry));
	q_strlcpy(entry->viewmodel_path, model, sizeof(entry->viewmodel_path));
	entry->has_enhanced_held_offset = calibration->has_enhanced_held_offset;
	VectorCopy(calibration->enhanced_held_offset, entry->enhanced_held_offset);
	entry->has_enhanced_muzzle_offset = calibration->has_enhanced_muzzle_offset;
	VectorCopy(calibration->enhanced_muzzle_offset, entry->enhanced_muzzle_offset);
	entry->has_muzzle_source_offset = calibration->has_muzzle_source_offset;
	VectorCopy(calibration->muzzle_source_offset, entry->muzzle_source_offset);
	entry->has_muzzle_source_viewofs = calibration->has_muzzle_source_viewofs;
	entry->muzzle_source_viewofs = calibration->muzzle_source_viewofs;
	entry->has_spawn_at_self_origin = calibration->has_spawn_at_self_origin;
	entry->spawn_at_self_origin = calibration->spawn_at_self_origin;
	entry->melee = calibration->melee;
}

static qboolean VR_CalibrationAppendSnapshotBlock(vr_calibration_textbuf_t *buf,
	const char *model, int snapshot_slot)
{
	vr_weapon_schema_entry_t snapshot;
	char line[192];

	VR_CalibrationFillSnapshotEntry(&snapshot, model, snapshot_slot);
	if (!VR_CalibrationEntryIsFinite(&snapshot) ||
		!VR_CalibrationTextAppendLine(buf, "{"))
		return false;
	q_snprintf(line, sizeof(line), "viewmodel %s", model);
	if (!VR_CalibrationTextAppendLine(buf, line))
		return false;
#define VR_APPEND_SNAPSHOT_VECTOR(key, field) \
	do { \
		q_snprintf(line, sizeof(line), key " %.9g %.9g %.9g", \
			snapshot.field[0], snapshot.field[1], snapshot.field[2]); \
		if (!VR_CalibrationTextAppendLine(buf, line)) return false; \
	} while (0)
	if (snapshot.has_enhanced_held_offset)
		VR_APPEND_SNAPSHOT_VECTOR("enhanced_held_offset", enhanced_held_offset);
	if (snapshot.has_enhanced_muzzle_offset)
		VR_APPEND_SNAPSHOT_VECTOR("enhanced_muzzle_offset", enhanced_muzzle_offset);
	if (snapshot.has_muzzle_source_offset)
		VR_APPEND_SNAPSHOT_VECTOR("muzzle_source_offset", muzzle_source_offset);
	if (snapshot.has_muzzle_source_viewofs)
	{
		q_snprintf(line, sizeof(line), "muzzle_source_viewofs %d",
			snapshot.muzzle_source_viewofs ? 1 : 0);
		if (!VR_CalibrationTextAppendLine(buf, line)) return false;
	}
	if (snapshot.has_spawn_at_self_origin)
	{
		q_snprintf(line, sizeof(line), "spawn_at_self_origin %d",
			snapshot.spawn_at_self_origin ? 1 : 0);
		if (!VR_CalibrationTextAppendLine(buf, line)) return false;
	}
	if (snapshot.melee.has_enabled)
	{
		q_snprintf(line, sizeof(line), "melee %d",
			snapshot.melee.enabled ? 1 : 0);
		if (!VR_CalibrationTextAppendLine(buf, line)) return false;
	}
	if (snapshot.melee.has_base)
		VR_APPEND_SNAPSHOT_VECTOR("melee_base", melee.base);
	if (snapshot.melee.has_tip)
		VR_APPEND_SNAPSHOT_VECTOR("melee_tip", melee.tip);
	if (snapshot.melee.has_speed)
	{
		q_snprintf(line, sizeof(line), "melee_speed %.9g", snapshot.melee.speed);
		if (!VR_CalibrationTextAppendLine(buf, line)) return false;
	}
	if (snapshot.melee.has_ready_frame)
	{
		q_snprintf(line, sizeof(line), "melee_frame %d",
			snapshot.melee.ready_frame);
		if (!VR_CalibrationTextAppendLine(buf, line)) return false;
	}
#undef VR_APPEND_SNAPSHOT_VECTOR
	return VR_CalibrationTextAppendLine(buf, "}");
}

static qboolean VR_CalibrationUnselectedMatches(
	const vr_weapon_schema_entry_t *actual,
	const vr_weapon_schema_entry_t *expected)
{
	/* Compare present values, including explicit false, without struct padding
	 * or stale storage behind absent private flags participating. */
#define VR_MATCH_OPTIONAL(flag, field) \
	do { \
		if (actual->flag != expected->flag || (actual->flag && \
			memcmp(&actual->field, &expected->field, sizeof(actual->field)))) \
			return false; \
	} while (0)
	VR_MATCH_OPTIONAL(has_enhanced_held_offset, enhanced_held_offset);
	VR_MATCH_OPTIONAL(has_enhanced_muzzle_offset, enhanced_muzzle_offset);
	VR_MATCH_OPTIONAL(has_muzzle_source_offset, muzzle_source_offset);
	VR_MATCH_OPTIONAL(has_muzzle_source_viewofs, muzzle_source_viewofs);
	VR_MATCH_OPTIONAL(has_spawn_at_self_origin, spawn_at_self_origin);
	VR_MATCH_OPTIONAL(melee.has_enabled, melee.enabled);
	VR_MATCH_OPTIONAL(melee.has_base, melee.base);
	VR_MATCH_OPTIONAL(melee.has_tip, melee.tip);
	VR_MATCH_OPTIONAL(melee.has_speed, melee.speed);
	VR_MATCH_OPTIONAL(melee.has_ready_frame, melee.ready_frame);
#undef VR_MATCH_OPTIONAL
	return true;
}

static qboolean VR_CalibrationSelectedMatches(
	const vr_weapon_schema_entry_t *entry, int selected_slot)
{
	vec3_t held, muzzle;
	for (int axis = 0; axis < 3; ++axis)
	{
		held[axis] = VR_WeaponOffsetCvar(selected_slot, axis).value;
		muzzle[axis] = VR_WeaponMuzzleCvar(selected_slot, axis).value;
	}
	return entry->has_held_scale && entry->has_held_offset &&
		entry->has_muzzle_offset &&
		VR_CalibrationSavedFloatMatches(entry->held_scale,
			VR_WeaponOffsetCvar(selected_slot, VR_WOFS_SCALE).value) &&
		VR_CalibrationSavedVectorMatches(entry->held_offset, held) &&
		VR_CalibrationSavedVectorMatches(entry->muzzle_offset, muzzle);
}

static qboolean VR_CalibrationSnapshotMatches(
	const vr_weapon_schema_entry_t *entry, const char *model, int snapshot_slot)
{
	vr_weapon_schema_entry_t expected;

	VR_CalibrationFillSnapshotEntry(&expected, model, snapshot_slot);
	return !strcmp(entry->viewmodel_path, model) && !strcmp(entry->model_path, model) &&
		entry->wheel.fields == VR_SCHEMA_WHEEL_VIEWMODEL &&
		!strcmp(entry->wheel.viewmodel_path, model) &&
		VR_CalibrationUnselectedMatches(entry, &expected);
}

static qboolean VR_CalibrationGlobalCandidateMatches(const char *text,
	const vr_weapon_schema_entry_t *original, size_t original_count,
	const vr_weapon_schema_metadata_t *original_metadata,
	char target_ids[][MAX_QPATH], const int *target_slots,
	const int *target_original, size_t target_count, int selected_slot,
	vr_weapon_schema_entry_t *publication)
{
	vr_weapon_schema_entry_t entries[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	vr_weapon_schema_metadata_t metadata;
	size_t count, missing = 0, appended;

	for (size_t i = 0; i < target_count; ++i)
		if (!target_original[i]) ++missing;
	if (!VR_WeaponSchemaParseWithMetadata(text, entries,
		VR_WEAPON_SCHEMA_MAX_ENTRIES, &count, &metadata) ||
		metadata.complete_roster != original_metadata->complete_roster ||
		count != original_count + missing)
		return false;
	for (size_t i = 0; i < original_count; ++i)
		if (strcmp(entries[i].model_path, original[i].model_path) ||
			strcmp(entries[i].viewmodel_path, original[i].viewmodel_path) ||
			memcmp(&entries[i].wheel, &original[i].wheel, sizeof(entries[i].wheel)) ||
			!VR_CalibrationUnselectedMatches(&entries[i], &original[i]))
			return false;
	appended = original_count;
	for (size_t i = 0; i < target_count; ++i)
	{
		int matches = 0;
		const vr_weapon_schema_entry_t *selected = NULL;
		for (size_t j = 0; j < count; ++j)
			if (!strcmp(entries[j].viewmodel_path, target_ids[i]))
			{
				++matches;
				if (!VR_CalibrationSelectedMatches(&entries[j], selected_slot))
					return false;
				selected = &entries[j];
			}
		if (matches != (target_original[i] ? target_original[i] : 1))
			return false;
		if (!target_original[i])
		{
			if (appended >= count || target_slots[i] < 0 ||
				!VR_CalibrationSnapshotMatches(&entries[appended],
					target_ids[i], target_slots[i]))
				return false;
			++appended;
		}
		/* Publish precisely the parsed file values, once per identity. */
		memset(&publication[i], 0, sizeof(publication[i]));
		memcpy(publication[i].viewmodel_path, selected->viewmodel_path,
			sizeof(publication[i].viewmodel_path));
		publication[i].has_held_scale = publication[i].has_held_offset =
			publication[i].has_muzzle_offset = true;
		publication[i].held_scale = selected->held_scale;
		VectorCopy(selected->held_offset, publication[i].held_offset);
		VectorCopy(selected->muzzle_offset, publication[i].muzzle_offset);
	}
	return appended == count;
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
		VR_CalibrationAppendAdjustmentLines(buf, slot, enhanced_format, false) &&
		VR_CalibrationTextAppendLine(buf, "}");
}

static int VR_CalibrationFindTargetIndex(char ids[][MAX_QPATH], size_t count,
	const char *model)
{
	for (size_t i = 0; i < count; ++i)
		if (!strcmp(ids[i], model))
			return (int)i;
	return -1;
}

static qboolean VR_WeaponCalibrationSaveInternal(qboolean global_mode)
{
	vr_calibration_textbuf_t output = {0};
	vr_weapon_schema_entry_t parsed[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	vr_weapon_schema_entry_t publication[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	vr_weapon_schema_metadata_t metadata;
	char target_ids[VR_WEAPON_SCHEMA_MAX_ENTRIES][MAX_QPATH];
	int target_slots[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	int target_original[VR_WEAPON_SCHEMA_MAX_ENTRIES] = {0};
	int target_written[VR_WEAPON_SCHEMA_MAX_ENTRIES] = {0};
	qmodel_t *model;
	aliashdr_t *alias_header;
	byte *file = NULL;
	unsigned int path_id = 0;
	const char *source;
	const char *p;
	const char *end;
	int model_index;
	int slot;
	size_t parsed_count;
	size_t target_count = 0;
	size_t globals_start = 0, globals_end = 0;
	qboolean enhanced_format;
	qboolean invalid_values;
	qboolean updated = false;
	qboolean ok = false;
	qboolean post_write_failed = false;
	qboolean added_classic_muzzle = false;

	if (!vr_weapon_calibration_initialized || cls.state != ca_connected ||
		cls.signon != SIGNONS || cls.demoplayback)
	{
		Con_Printf("VR: %s requires a connected, fully signed-on game\n",
			global_mode ? "vrweaponsaveglobal" : "vrweaponsave");
		return false;
	}
	if (global_mode && vr_weapon_calibration_adjustment.active)
	{
		Con_Printf("VR: finish or cancel the active weapon adjustment before vrweaponsaveglobal; use vrweaponsave for the current weapon\n");
		return false;
	}
	model_index = cl.stats[STAT_WEAPON];
	if (model_index < 1 || model_index >= MAX_MODELS ||
		!cl.viewent.model || !cl.model_precache[model_index] ||
		cl.viewent.model != cl.model_precache[model_index] ||
		cl.viewent.model->needload || cl.viewent.model->type != mod_alias ||
		!cl.viewent.model->name[0])
	{
		Con_Printf("VR: %s requires a valid current alias viewmodel\n",
			global_mode ? "vrweaponsaveglobal" : "vrweaponsave");
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
		Con_Printf("VR: %s cannot read the active viewmodel alias header\n",
			global_mode ? "vrweaponsaveglobal" : "vrweaponsave");
		return false;
	}
	enhanced_format = VR_CalibrationPoseTypeIsEnhanced(
		alias_header->poseverttype);
	if (!VR_CalibrationPoseTypeIsSupported(alias_header->poseverttype))
	{
		Con_Printf("VR: %s cannot save this viewmodel geometry format\n",
			global_mode ? "vrweaponsaveglobal" : "vrweaponsave");
		return false;
	}
	if (global_mode && enhanced_format)
	{
		Con_Printf("VR: vrweaponsaveglobal saves classic calibration only; use vrweaponsave for this enhanced viewmodel\n");
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
	if (!global_mode && !enhanced_format &&
		!vr_weapon_calibration_slots[slot].has_muzzle_offset)
	{
		/* A successful classic save publishes the current muzzle cvars.
		 * Keep presence consistent with the authored key after a reload. */
		vr_weapon_calibration_slots[slot].has_muzzle_offset = true;
		added_classic_muzzle = true;
	}

	file = COM_LoadFile("vr_weapons.txt", &path_id);
	/* Saving inherited text into this game would promote its roster authority.
	 * Keep inherited calibration loads, but author only this game's save. */
	source = file && com_searchpaths && path_id == com_searchpaths->path_id ?
		(const char *)file : "";
	if (!VR_WeaponSchemaParseWithMetadata(source, parsed,
		VR_WEAPON_SCHEMA_MAX_ENTRIES, &parsed_count, &metadata))
	{
		Con_Printf("VR: refusing to save; existing vr_weapons.txt is invalid\n");
		goto done;
	}
	if (global_mode)
	{
		size_t missing = 0;
		for (size_t i = 0; i < parsed_count; ++i)
		{
			const char *id = parsed[i].viewmodel_path;
			int target;
			if (!id[0])
				continue;
			if (!VR_CalibrationSafeModelToken(id))
			{
				Con_Printf("VR: refusing global save; local schema has an unsafe accepted calibration identity\n");
				goto done;
			}
			target = VR_CalibrationFindTargetIndex(target_ids, target_count, id);
			if (target >= 0)
			{
				++target_original[target];
				continue;
			}
			if (target_count >= VR_WEAPON_SCHEMA_MAX_ENTRIES)
				goto done;
			q_strlcpy(target_ids[target_count], id,
				sizeof(target_ids[target_count]));
			target_slots[target_count] = -1;
			target_original[target_count] = 1;
			++target_count;
		}
		for (int live_slot = 0; live_slot < VR_WEAPON_CALIBRATION_MAX_SLOTS;
			 ++live_slot)
		{
			const char *id = VR_WeaponOffsetCvar(live_slot, VR_WOFS_ID).string;
			int target;
			if (!id || !id[0] || !strcmp(id, "-1"))
				continue;
			if (!VR_CalibrationSafeModelToken(id))
			{
				Con_Printf("VR: refusing global save; live calibration has an unsafe identity\n");
				goto done;
			}
			target = VR_CalibrationFindTargetIndex(target_ids, target_count, id);
			if (target >= 0)
			{
				if (target_slots[target] >= 0 && target_slots[target] != live_slot)
				{
					Con_Printf("VR: refusing global save; duplicate live calibration identity %s\n", id);
					goto done;
				}
				target_slots[target] = live_slot;
				continue;
			}
			if (target_count >= VR_WEAPON_SCHEMA_MAX_ENTRIES)
			{
				Con_Printf("VR: refusing global save; known calibration roster exceeds schema capacity\n");
				goto done;
			}
			q_strlcpy(target_ids[target_count], id,
				sizeof(target_ids[target_count]));
			target_slots[target_count] = live_slot;
			++target_count;
		}
		/* Admission found the active slot; the complete live scan includes it. */
		for (size_t i = 0; i < target_count; ++i)
			if (!target_original[i])
				++missing;
		if (parsed_count + missing > VR_WEAPON_SCHEMA_MAX_ENTRIES)
		{
			Con_Printf("VR: refusing global save; materialized calibration roster exceeds schema capacity\n");
			goto done;
		}
	}
	p = source;
	end = source + strlen(source);
	while (p < end)
	{
		const char *open = VR_CalibrationFindBrace(p, end, '{');
		const char *close;
		int target = -1;

		if (!open)
		{
			if (!(global_mode ?
				VR_CalibrationAppendTopLevelWithoutClassicGlobals(&output, p,
					(size_t)(end - p)) :
				VR_CalibrationTextAppendN(&output, p, (size_t)(end - p))))
				goto done;
			break;
		}
		if (!(global_mode ?
			VR_CalibrationAppendTopLevelWithoutClassicGlobals(&output, p,
				(size_t)(open - p)) :
			VR_CalibrationTextAppendN(&output, p, (size_t)(open - p))))
			goto done;
		close = VR_CalibrationFindBrace(open + 1, end, '}');
		if (!close)
			goto done; /* The schema preflight should make this unreachable. */
		++close;
		if (global_mode)
			for (size_t i = 0; i < target_count; ++i)
				if (target_original[i] &&
					VR_CalibrationBlockMatchesViewmodel(open,
						(size_t)(close - open), target_ids[i]))
				{
					target = (int)i;
					break;
				}
		if (global_mode ? target >= 0 : VR_CalibrationBlockMatchesViewmodel(open,
			(size_t)(close - open), model->name))
		{
			if (!global_mode && updated)
			{
				Con_Printf("VR: refusing to save; multiple viewmodel blocks match %s\n",
					model->name);
				goto done;
			}
			if (!VR_CalibrationWriteUpdatedBlock(&output, open,
				(size_t)(close - open), slot, enhanced_format, global_mode))
				goto done;
			if (global_mode)
				++target_written[target];
			else
				updated = true;
		}
		else if (!VR_CalibrationTextAppendN(&output, open, (size_t)(close - open)))
			goto done;
		p = close;
	}

	if (global_mode)
	{
		for (size_t i = 0; i < target_count; ++i)
			if (target_original[i] != target_written[i])
			{
				Con_Printf("VR: refusing global save; source block multiplicity does not match accepted identity %s\n",
					target_ids[i]);
				goto done;
			}
		/* Always separate terminal globals from source text so an EOF line
		 * comment cannot consume the new directives. */
		if (!VR_CalibrationTextAppend(&output, "\n"))
			goto done;
		globals_start = output.len;
		if (!VR_CalibrationAppendGlobalAdjustmentLines(&output, slot))
			goto done;
		globals_end = output.len;
		for (size_t i = 0; i < target_count; ++i)
			if (!target_original[i] &&
				!VR_CalibrationAppendSnapshotBlock(&output, target_ids[i],
					target_slots[i]))
				goto done;
	}
	else if (!updated)
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
	if (global_mode ?
		(!VR_CalibrationGeneratedGlobalsVisible(output.data, globals_start, globals_end) ||
		!VR_CalibrationGlobalCandidateMatches(output.data ? output.data : "",
			parsed, parsed_count, &metadata, target_ids, target_slots,
			target_original, target_count, slot, publication)) :
		!VR_CalibrationSavedValuesMatch(output.data ? output.data : "",
			model->name, slot, enhanced_format, parsed_count, &metadata))
	{
		Con_Printf("VR: refusing to save; rewritten vr_weapons.txt does not preserve the requested calibration\n");
		goto done;
	}
	if (global_mode && !VR_WeaponCalibrationPreflightSchema(publication, target_count))
	{
		Con_Printf("VR: refusing global save; selected calibration roster cannot be published safely\n");
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
	if (global_mode)
	{
		if (!VR_WeaponCalibrationApplySchema(publication, target_count))
		{
			Con_Printf("VR: saved global classic calibration to disk, but could not publish it to live calibration slots\n");
			post_write_failed = true;
			goto done;
		}
		Con_Printf("VR: saved global classic calibration from %s across %u identities to %s/vr_weapons.txt\n",
			model->name, (unsigned int)target_count, com_gamedir);
	}
	else
		Con_Printf("VR: saved %s calibration for %s to %s/vr_weapons.txt\n",
			enhanced_format ? "enhanced" : "classic", model->name, com_gamedir);
	ok = true;

done:
	if (!ok && added_classic_muzzle)
		vr_weapon_calibration_slots[slot].has_muzzle_offset = false;
	if (!ok && output.data && !post_write_failed)
		Con_Printf("VR: failed to save %s calibration for %s\n",
			global_mode ? "global classic" :
			(enhanced_format ? "enhanced" : "classic"), model->name);
	free(output.data);
	if (file)
		Mem_Free(file);
	return ok;
}

static qboolean VR_WeaponCalibrationSave(void)
{
	return VR_WeaponCalibrationSaveInternal(false);
}

static void VR_WeaponCalibrationSave_f(void)
{
	(void)VR_WeaponCalibrationSave();
}

static void VR_WeaponCalibrationSaveGlobal_f(void)
{
	(void)VR_WeaponCalibrationSaveInternal(true);
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
	vrxr_device_t hand;
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
	if (!VR_LocomotionControllerDevice(frame, dominant, &hand) ||
		!hand.tracked || !hand.connected)
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
		alias_header != adjustment->geometry ||
		(!adjustment->muzzle_mode &&
		 VR_WeaponCalibrationModelOffsetScale(model, alias_header) != adjustment->model_offset_scale) ||
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
	adjustment->geometry = alias_header;
	adjustment->model_offset_scale = VR_WeaponCalibrationModelOffsetScale(model, alias_header);
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
		adjustment->gunmodelscale * adjustment->model_offset_scale;
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
	Cvar_RegisterVariable(&vr_gunmodeloffsets);
	Cvar_SetCallback(&vr_gunmodeloffsets, VR_WeaponCalibrationPresetChanged);
}

void VR_WeaponCalibrationRegisterCommands(void)
{
	if (vr_weapon_calibration_commands_registered)
		return;
	vr_weapon_calibration_commands_registered = true;
	Cmd_AddCommand("vrweaponsave", VR_WeaponCalibrationSave_f);
	Cmd_AddCommand("vrweaponsaveglobal", VR_WeaponCalibrationSaveGlobal_f);
	Cmd_AddCommand("vradjustweapon", VR_WeaponCalibrationAdjustGrip_f);
	Cmd_AddCommand("vradjustmuzzle", VR_WeaponCalibrationAdjustMuzzle_f);
}

void VR_WeaponCalibrationReset(void)
{
	int slot;

	VR_WeaponCalibrationAdjustCancel();
	vr_weapon_calibration_inherits_ad = false;
	memset(vr_weapon_calibration_slots, 0,
		   sizeof(vr_weapon_calibration_slots));
	if (!vr_weapon_calibration_initialized)
		return;

	for (slot = 0; slot < VR_WEAPON_CALIBRATION_MAX_SLOTS; ++slot)
		VR_SetCalibrationSlotDefaults(slot);
}

static qboolean VR_WeaponCalibrationPreflightSchema(
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
	return true;
}

static qboolean VR_WeaponCalibrationApplySchemaMode(
	const vr_weapon_schema_entry_t *entries, size_t count, qboolean preset_mode)
{
	size_t index;
	int slot;

	if (!VR_WeaponCalibrationPreflightSchema(entries, count))
		return false;
	if (preset_mode)
		VR_WeaponCalibrationAdjustCancel();

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
		if (preset_mode && entry->has_held_offset &&
			!entry->has_muzzle_offset)
		{
			Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_X), 0.0f);
			Cvar_SetValueQuick(&VR_WeaponMuzzleCvar(slot, VR_WMUZZLE_Y), 0.0f);
			calibration->has_muzzle_offset = false;
			calibration->muzzle_seeded_from_held = false;
		}
		if (entry->melee.has_enabled)
		{
			calibration->melee.enabled = entry->melee.enabled;
			calibration->melee.has_enabled = true;
		}
		if (entry->melee.has_base)
		{
			VectorCopy(entry->melee.base, calibration->melee.base);
			calibration->melee.has_base = true;
		}
		if (entry->melee.has_tip)
		{
			VectorCopy(entry->melee.tip, calibration->melee.tip);
			calibration->melee.has_tip = true;
		}
		if (entry->melee.has_speed)
		{
			calibration->melee.speed = entry->melee.speed;
			calibration->melee.has_speed = true;
		}
		if (entry->melee.has_ready_frame)
		{
			calibration->melee.ready_frame = entry->melee.ready_frame;
			calibration->melee.has_ready_frame = true;
		}

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

qboolean VR_WeaponCalibrationApplySchema(
	const vr_weapon_schema_entry_t *entries, size_t count)
{
	return VR_WeaponCalibrationApplySchemaMode(entries, count, false);
}

static qboolean VR_WeaponCalibrationApplyEnhancedFallbacks(void)
{
	return VR_WeaponCalibrationApplySchema(
		vr_enhanced_weapon_fallbacks,
		sizeof(vr_enhanced_weapon_fallbacks) /
		sizeof(vr_enhanced_weapon_fallbacks[0]));
}

static qboolean VR_WeaponCalibrationGameIs(const char *name)
{
	const char *game = COM_SkipPath(com_gamedir);
	return game && !q_strcasecmp(game, name);
}

/* Cache successes and failures once per ReloadGame. No preset or lookup IO. */
static qboolean VR_WeaponCalibrationADAssetMatches(
	const char *path, const char *canonical_path)
{
	int handle = -1;
	qfilesize_t length = COM_OpenFile(path, &handle, NULL);
	byte *data;
	unsigned hash;
	size_t i;
	qboolean matches = false;

	if (handle < 0)
		return false;
	for (i = 0; i < countof(vr_ad_weapon_signatures); ++i)
		if (!strcmp(canonical_path, vr_ad_weapon_signatures[i].path) &&
			length == vr_ad_weapon_signatures[i].length)
			break;
	if (i == countof(vr_ad_weapon_signatures))
	{
		Sys_FileClose(handle);
		return false;
	}
	/* A matching table length bounds allocation/read to at most 131164 bytes. */
	data = (byte *)malloc((size_t)length);
	if (!data)
	{
		Sys_FileClose(handle);
		return false;
	}
	if (Sys_FileRead(handle, data, (int)length) == (int)length)
	{
		hash = COM_HashBlock(data, (size_t)length);
		for (; i < countof(vr_ad_weapon_signatures); ++i)
			if (!strcmp(canonical_path, vr_ad_weapon_signatures[i].path) &&
				length == vr_ad_weapon_signatures[i].length &&
				hash == vr_ad_weapon_signatures[i].hash)
			{
				matches = true;
				break;
			}
	}
	Sys_FileClose(handle);
	free(data);
	return matches;
}

static void VR_WeaponCalibrationRefreshADIdentity(void)
{
	for (size_t i = 0; i < countof(vr_ad_weapon_fallbacks); ++i)
		vr_ad_weapon_identified[i] = VR_WeaponCalibrationADAssetMatches(
			vr_ad_weapon_fallbacks[i].viewmodel_path,
			vr_ad_weapon_fallbacks[i].viewmodel_path);
	for (size_t i = 0; i < countof(vr_ad171_weapon_aliases); ++i)
		vr_ad171_weapon_identified[i] = VR_WeaponCalibrationADAssetMatches(
			vr_ad171_weapon_aliases[i].alias_path,
			vr_ad171_weapon_aliases[i].ad_path);
}

static const vr_weapon_schema_entry_t *VR_WeaponCalibrationIdentifiedADProfile(
	const char *path)
{
	for (size_t i = 0; i < countof(vr_ad171_weapon_aliases); ++i)
		if (!strcmp(path, vr_ad171_weapon_aliases[i].alias_path))
		{
			if (!vr_ad171_weapon_identified[i])
				return NULL;
			for (size_t j = 0; j < countof(vr_ad_weapon_fallbacks); ++j)
				if (!strcmp(vr_ad171_weapon_aliases[i].ad_path,
					vr_ad_weapon_fallbacks[j].viewmodel_path))
					return &vr_ad_weapon_fallbacks[j];
			return NULL;
		}
	for (size_t i = 0; i < countof(vr_ad_weapon_fallbacks); ++i)
		if (vr_ad_weapon_identified[i] &&
			!strcmp(path, vr_ad_weapon_fallbacks[i].viewmodel_path))
			return &vr_ad_weapon_fallbacks[i];
	return NULL;
}

static qboolean VR_WeaponCalibrationPresetAppendIdentifiedAD(
	vr_weapon_schema_entry_t *entries, size_t *count)
{
	for (size_t i = 0; i < countof(vr_ad_weapon_fallbacks) +
		countof(vr_ad171_weapon_aliases); ++i)
	{
		const char *path = i < countof(vr_ad_weapon_fallbacks) ?
			vr_ad_weapon_fallbacks[i].viewmodel_path :
			vr_ad171_weapon_aliases[i - countof(vr_ad_weapon_fallbacks)].alias_path;
		const vr_weapon_schema_entry_t *profile =
			VR_WeaponCalibrationIdentifiedADProfile(path);
		if (!profile)
			continue;
		if (*count >= VR_WEAPON_SCHEMA_MAX_ENTRIES)
			return false;
		entries[*count] = *profile;
		strcpy(entries[*count].viewmodel_path, path);
		++*count;
	}
	return true;
}

static qboolean VR_WeaponCalibrationPresetAppendSchema(
	vr_weapon_schema_entry_t *entries, size_t *count,
	const vr_weapon_schema_entry_t *source, size_t source_count)
{
	for (size_t i = 0; i < source_count; ++i)
	{
		if (*count >= VR_WEAPON_SCHEMA_MAX_ENTRIES)
			return false;
		entries[*count] = source[i];
		entries[*count].has_muzzle_offset = false;
		++*count;
	}
	return true;
}

static qboolean VR_WeaponCalibrationPresetAppendRows(
	vr_weapon_schema_entry_t *entries, size_t *count,
	const vr_weapon_preset_row_t *rows, size_t row_count)
{
	for (size_t i = 0; i < row_count; ++i)
	{
		vr_weapon_schema_entry_t *entry;
		if (*count >= VR_WEAPON_SCHEMA_MAX_ENTRIES)
			return false;
		entry = &entries[(*count)++];
		memset(entry, 0, sizeof(*entry));
		strcpy(entry->viewmodel_path, rows[i].path);
		VectorCopy(rows[i].held, entry->held_offset);
		entry->held_scale = rows[i].scale;
		entry->has_held_offset = entry->has_held_scale = true;
	}
	return true;
}

static qboolean VR_WeaponCalibrationPresetAppendQBJ3(
	vr_weapon_schema_entry_t *entries, size_t *count)
{
	for (size_t i = 0; i < countof(vr_qbj3_weapon_fallbacks); ++i)
	{
		const vr_weapon_preset_muzzle_row_t *row = &vr_qbj3_weapon_fallbacks[i];
		vr_weapon_schema_entry_t *entry;
		if (*count >= VR_WEAPON_SCHEMA_MAX_ENTRIES)
			return false;
		entry = &entries[(*count)++];
		memset(entry, 0, sizeof(*entry));
		strcpy(entry->viewmodel_path, row->path);
		VectorCopy(row->held, entry->held_offset);
		VectorCopy(row->muzzle, entry->muzzle_offset);
		entry->held_scale = row->scale;
		entry->has_held_offset = entry->has_held_scale =
			entry->has_muzzle_offset = true;
	}
	return true;
}

static qboolean VR_WeaponCalibrationPresetAppendMuzzleRows(
	vr_weapon_schema_entry_t *entries, size_t *count,
	const vr_weapon_preset_muzzle_row_t *rows, size_t row_count)
{
	for (size_t i = 0; i < row_count; ++i)
	{
		const vr_weapon_preset_muzzle_row_t *row = &rows[i];
		vr_weapon_schema_entry_t *entry;
		if (*count >= VR_WEAPON_SCHEMA_MAX_ENTRIES)
			return false;
		entry = &entries[(*count)++];
		memset(entry, 0, sizeof(*entry));
		strcpy(entry->viewmodel_path, row->path);
		VectorCopy(row->held, entry->held_offset);
		VectorCopy(row->muzzle, entry->muzzle_offset);
		entry->held_scale = row->scale;
		entry->has_held_offset = entry->has_held_scale =
			entry->has_muzzle_offset = true;
	}
	return true;
}

static qboolean VR_WeaponCalibrationPresetAppendGeneric(
	vr_weapon_schema_entry_t *entries, size_t *count, int preset)
{
	const vr_weapon_preset_row_t *rows = NULL;
	size_t row_count = 0;

	if (preset == VR_WEAPON_PRESET_AD)
		return VR_WeaponCalibrationPresetAppendSchema(entries, count,
			vr_ad_weapon_fallbacks, countof(vr_ad_weapon_fallbacks));
	if (preset == VR_WEAPON_PRESET_BLOCKQUAKE)
		return VR_WeaponCalibrationPresetAppendRows(entries, count,
			vr_blockquake_classic, countof(vr_blockquake_classic));
	if (!VR_WeaponCalibrationPresetAppendSchema(entries, count,
		vr_stock_classic_fallbacks, countof(vr_stock_classic_fallbacks)))
		return false;
	switch (preset)
	{
	case VR_WEAPON_PRESET_ENHANCED:
		rows = vr_enhanced_classic_overrides;
		row_count = countof(vr_enhanced_classic_overrides);
		break;
	case VR_WEAPON_PRESET_AUTHENTIC:
		rows = vr_authentic_classic_overrides;
		row_count = countof(vr_authentic_classic_overrides);
		break;
	case VR_WEAPON_PRESET_PLAGUE:
		rows = vr_plague_classic_overrides;
		row_count = countof(vr_plague_classic_overrides);
		break;
	}
	return !rows || VR_WeaponCalibrationPresetAppendRows(entries, count,
		rows, row_count);
}

static qboolean VR_WeaponCalibrationBuildPreset(
	vr_weapon_schema_entry_t *entries, size_t *count, int preset,
	qboolean reload_defaults, qboolean *inherits_ad)
{
	*inherits_ad = false;
	*count = 0;
	if (VR_WeaponCalibrationGameIs("peril3.0"))
		return VR_WeaponCalibrationPresetAppendMuzzleRows(entries, count,
			vr_peril_weapon_fallbacks, countof(vr_peril_weapon_fallbacks));
	if (VR_WeaponCalibrationGameIs("qbj3"))
		return VR_WeaponCalibrationPresetAppendQBJ3(entries, count);
	if (VR_WeaponCalibrationGameIs("snack") ||
		VR_WeaponCalibrationGameIs("snack3"))
		return VR_WeaponCalibrationPresetAppendMuzzleRows(entries, count,
			vr_snack_weapon_fallbacks, countof(vr_snack_weapon_fallbacks));
	if (VR_WeaponCalibrationGameIs("alk"))
		return VR_WeaponCalibrationPresetAppendSchema(entries, count,
			vr_alk_axe_fallback, countof(vr_alk_axe_fallback)) &&
			VR_WeaponCalibrationPresetAppendRows(entries, count,
				vr_alk_fixed_fallbacks, countof(vr_alk_fixed_fallbacks)) &&
			VR_WeaponCalibrationPresetAppendRows(entries, count,
				preset == VR_WEAPON_PRESET_PLAGUE ? vr_alk_plague : vr_alk_classic,
				countof(vr_alk_classic));
	if (VR_WeaponCalibrationGameIs("enyo"))
		return VR_WeaponCalibrationPresetAppendSchema(entries, count,
			vr_enyo_weapon_fallbacks, countof(vr_enyo_weapon_fallbacks));
	if (VR_WeaponCalibrationGameIs("dwell") ||
		VR_WeaponCalibrationGameIs("dwellv2p2"))
		return VR_WeaponCalibrationPresetAppendRows(entries, count,
			vr_dwell_weapon_fallbacks, countof(vr_dwell_weapon_fallbacks));
	if (VR_WeaponCalibrationGameIs("enhanced"))
		preset = VR_WEAPON_PRESET_ENHANCED;
	*inherits_ad = true;
	if (!VR_WeaponCalibrationPresetAppendGeneric(entries, count, preset))
		return false;
	if (VR_WeaponCalibrationGameIs("bonkjam") &&
		!VR_WeaponCalibrationPresetAppendSchema(entries, count,
			vr_bonk_hammer_fallbacks, countof(vr_bonk_hammer_fallbacks)))
		return false;
	if ((preset == VR_WEAPON_PRESET_BLOCKQUAKE || preset == VR_WEAPON_PRESET_AD) &&
		!reload_defaults)
		return VR_WeaponCalibrationPresetAppendIdentifiedAD(entries, count);
	return VR_WeaponCalibrationPresetAppendSchema(entries, count,
			vr_copper_axe_fallback, countof(vr_copper_axe_fallback)) &&
		(!VR_WeaponCalibrationGameIs("limjam") ||
		 VR_WeaponCalibrationPresetAppendSchema(entries, count,
			vr_alk_axe_fallback, countof(vr_alk_axe_fallback))) &&
		VR_WeaponCalibrationPresetAppendIdentifiedAD(entries, count);
}

static qboolean VR_WeaponCalibrationApplyPreset(int preset, qboolean reload_defaults)
{
	vr_weapon_schema_entry_t entries[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	size_t count;
	qboolean inherits_ad;
	if (!VR_WeaponCalibrationBuildPreset(entries, &count, preset, reload_defaults,
		&inherits_ad) || !VR_WeaponCalibrationApplySchemaMode(entries, count, true))
		return false;
	vr_weapon_calibration_inherits_ad = inherits_ad;
	return true;
}

static void VR_WeaponCalibrationSetPresetCvar(int preset)
{
	Cvar_SetCallback(&vr_gunmodeloffsets, NULL);
	Cvar_SetValueQuick(&vr_gunmodeloffsets, (float)preset);
	Cvar_SetCallback(&vr_gunmodeloffsets, VR_WeaponCalibrationPresetChanged);
}

static void VR_WeaponCalibrationPresetChanged(cvar_t *var)
{
	int requested;
	if (!isfinite(var->value) || var->value < 0.0f ||
		var->value >= (float)VR_WEAPON_PRESET_COUNT ||
		var->value != (float)(int)var->value)
	{
		requested = VR_WEAPON_PRESET_VANILLA;
		VR_WeaponCalibrationSetPresetCvar(requested);
	}
	else
		requested = (int)var->value;
	if (VR_WeaponCalibrationApplyPreset(requested, false))
		vr_weapon_preset_accepted = requested;
	else
	{
		Con_Warning("VR: weapon preset refused; keeping prior calibration\n");
		VR_WeaponCalibrationSetPresetCvar(vr_weapon_preset_accepted);
	}
}

const char *VR_WeaponCalibrationPresetName(void)
{
	static const char *const names[] = {
		"Vanilla", "Enhanced", "Authentic", "Plague", "Block-Quake",
		"AD"
	};
	return names[vr_weapon_preset_accepted];
}

static qboolean VR_WeaponCalibrationApplyBuiltinFallbacks(void)
{
	return VR_WeaponCalibrationApplyPreset(vr_weapon_preset_accepted, true) &&
		VR_WeaponCalibrationApplyEnhancedFallbacks();
}

/* Suppress only a complete exact copied generic classic triple on an asset
 * receiving identified AD defaults or explicit Peril presets. Changed/partial
 * triples and every other
 * schema field stay authored; special profiles retain their existing behavior.
 * An intentional saved triple equal to generic defaults is indistinguishable. */
static void VR_WeaponCalibrationFilterLegacyGenericDefaults(
	vr_weapon_schema_entry_t *entries, size_t count)
{
	const qboolean peril = VR_WeaponCalibrationGameIs("peril3.0");
	if (!vr_weapon_calibration_inherits_ad && !peril)
		return;
	for (size_t i = 0; i < count; ++i)
	{
		vr_weapon_schema_entry_t *entry = &entries[i];
		const vr_weapon_schema_entry_t *profile = peril ? NULL :
			VR_WeaponCalibrationIdentifiedADProfile(entry->viewmodel_path);
		const char *profile_path = profile ? profile->viewmodel_path : NULL;
		if (peril)
			for (size_t j = 0; j < countof(vr_peril_weapon_fallbacks); ++j)
				if (!strcmp(entry->viewmodel_path, vr_peril_weapon_fallbacks[j].path))
				{
					profile_path = vr_peril_weapon_fallbacks[j].path;
					break;
				}
		if (!profile_path)
			continue;
		for (size_t j = 0; j < countof(vr_stock_classic_fallbacks); ++j)
		{
			const vr_weapon_schema_entry_t *generic = &vr_stock_classic_fallbacks[j];
			if (strcmp(profile_path, generic->viewmodel_path))
				continue;
			if (!entry->has_held_offset || !entry->has_held_scale ||
				!entry->has_muzzle_offset ||
				!VectorCompare(entry->held_offset, generic->held_offset) ||
				entry->held_scale != generic->held_scale ||
				entry->muzzle_offset[0] != 0.0f ||
				entry->muzzle_offset[1] != 0.0f ||
				entry->muzzle_offset[2] != generic->held_offset[2])
				break;
			entry->has_held_offset = false;
			entry->has_held_scale = false;
			entry->has_muzzle_offset = false;
			break;
		}
	}
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
	VR_WeaponCalibrationRefreshADIdentity();
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
	VR_WeaponCalibrationFilterLegacyGenericDefaults(entries, count);

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

qboolean VR_WeaponCalibrationLookupMelee(const char *model_name,
	vr_melee_gesture_profile_t *out)
{
	static const char *const default_melee_models[] = {
		"progs/v_axe.mdl",
		"progs/v_axe2.mdl",
		"progs/v_alkaxe20fps.mdl",
		"progs/v_shadaxe0.mdl",
		"progs/v_shadaxe1.mdl",
		"progs/v_shadaxe2.mdl",
		"progs/v_shadaxe3.mdl",
		"progs/v_shadaxe4.mdl",
		"progs/v_shadaxe5.mdl",
		"progs/v_wrench.mdl",
		"progs/ee_v_sword.mdl",
		"progs/v_axeb.mdl",
		"progs/v_berserk.mdl",
	};
	const char *default_name = model_name;
	int slot;
	if (!out)
		return false;
	memset(out, 0, sizeof(*out));
	if (!vr_weapon_calibration_initialized || !model_name || !model_name[0])
		return false;
	for (size_t i = 0; i < sizeof(vr_ad171_weapon_aliases) / sizeof(vr_ad171_weapon_aliases[0]); ++i)
		if (!strcmp(model_name, vr_ad171_weapon_aliases[i].alias_path))
		{
			default_name = vr_ad171_weapon_aliases[i].ad_path;
			break;
		}
	for (size_t i = 0; i < sizeof(default_melee_models) / sizeof(default_melee_models[0]); ++i)
		if (!strcmp(default_name, default_melee_models[i]))
			out->enabled = true;
	const mod_held_melee_recipe_t *recipe = Mod_GetHeldMeleeRecipe (model_name);
	if (recipe && recipe->contact_profile == VR_WEAPON_CONTACT_PROFILE_BONK)
		out->enabled = true; // Input additionally requires the exact Bonk/head offer.
	out->speed = 1.5f;
	out->ready_frame = 0;
	slot = VR_FindCalibrationSlot(model_name);
	if (slot >= 0)
	{
		const vr_melee_gesture_profile_t *profile = &vr_weapon_calibration_slots[slot].melee;
		if (profile->has_enabled) out->enabled = profile->enabled;
		if (profile->has_base) VectorCopy(profile->base, out->base);
		if (profile->has_tip) VectorCopy(profile->tip, out->tip);
		if (profile->has_speed) out->speed = profile->speed;
		if (profile->has_ready_frame) out->ready_frame = profile->ready_frame;
		out->has_enabled = profile->has_enabled;
		out->has_base = profile->has_base;
		out->has_tip = profile->has_tip;
		out->has_speed = profile->has_speed;
		out->has_ready_frame = profile->has_ready_frame;
	}
	return out->enabled;
}

float VR_WeaponCalibrationModelOffsetScale(const qmodel_t *model,
	const aliashdr_t *geometry)
{
	if (!model || !geometry || !Mod_IsRereleaseReplacementGeometry(model, geometry))
		return 1.0f;
	if (!q_strcasecmp(model->name, "progs/v_axe.mdl"))
		return 1.0f / 3.0f;
	return !q_strcasecmp(model->name, "progs/v_shot2.mdl") ? 1.0f : 0.5f;
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
	static const vec3_t snack_stakegun_forward_offset = {0.0f, 0.0f, 11.0f};
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
		 !q_strcasecmp(viewmodel, "progs/v_prox.mdl")) ||
		(weapon_bit == IT_SUPER_NAILGUN && viewmodel &&
		 (VR_WeaponCalibrationGameIs("snack") ||
		  VR_WeaponCalibrationGameIs("snack3")) &&
		 !q_strcasecmp(viewmodel, "progs/v_nail2.mdl"));

	if (!out)
		return;
	memset(out, 0, sizeof(vec3_t));

	if (!angles || !VR_CalibrationVectorIsFinite(angles))
		safe_angles = default_angles;
	forward_offset = VR_WeaponCalibrationUpOnlySource(viewmodel) ?
		up_only_offset : default_forward_offset;
	if (weapon_bit == IT_NAILGUN && viewmodel &&
		(VR_WeaponCalibrationGameIs("snack") ||
		 VR_WeaponCalibrationGameIs("snack3")) &&
		!q_strcasecmp(viewmodel, "progs/v_nail.mdl"))
		forward_offset = snack_stakegun_forward_offset;

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
