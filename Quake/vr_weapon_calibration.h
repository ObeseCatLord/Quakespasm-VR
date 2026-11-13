#ifndef VR_WEAPON_CALIBRATION_H
#define VR_WEAPON_CALIBRATION_H

#include "vr_weapon_schema.h"

#define VR_WEAPON_CALIBRATION_MAX_SLOTS 99
#define VR_WEAPON_CALIBRATION_VARS_PER_WEAPON 5
#define VR_WEAPON_CALIBRATION_VARS_PER_MUZZLE 3

/* Classic cvar layout: x, y, z, scale, id; muzzle x, y, z. */
extern cvar_t vr_weapon_offset[VR_WEAPON_CALIBRATION_MAX_SLOTS *
							   VR_WEAPON_CALIBRATION_VARS_PER_WEAPON];
extern cvar_t vr_weapon_muzzle_offset[VR_WEAPON_CALIBRATION_MAX_SLOTS *
									 VR_WEAPON_CALIBRATION_VARS_PER_MUZZLE];

void VR_WeaponCalibrationInit(void);
void VR_WeaponCalibrationRegisterCommands(void);
void VR_WeaponCalibrationReset(void);
qboolean VR_WeaponCalibrationAdjustActive(void);
void VR_WeaponCalibrationAdjustCancel(void);
void VR_WeaponCalibrationAdjustInput(int physical_hand,
	qboolean trigger_down, qboolean pose_valid, const vec3_t live_origin,
	const vec3_t live_hand_angles);
qboolean VR_WeaponCalibrationAdjustPresentation(vec3_t origin,
	vec3_t angles);
qboolean VR_WeaponCalibrationAdjustMuzzleCue(vec3_t world);
qboolean VR_WeaponCalibrationReloadGame(void);
const char *VR_WeaponCalibrationPresetName(void);
qboolean VR_WeaponCalibrationApplySchema(
	const vr_weapon_schema_entry_t *entries, size_t count);
qboolean VR_WeaponCalibrationLookupHeld(const char *model_name,
										qboolean enhanced_format,
										vec3_t out_offset,
										float *out_scale);
/* Pure selected-geometry provenance factor for held model offsets/scaling.
 * Official rerelease companions only; muzzle offsets stay independent. */
float VR_WeaponCalibrationModelOffsetScale(const qmodel_t *model,
	const aliashdr_t *geometry);
qboolean VR_WeaponCalibrationLookupMuzzle(const char *model_name,
										  qboolean enhanced_format,
										  vec3_t out);
qboolean VR_WeaponCalibrationCurrentMuzzle(vec3_t out);
qboolean VR_WeaponCalibrationStockRangedViewmodel(const char *name);
/* Generic fallback convention/override; does not authorize physical damage. */
qboolean VR_WeaponCalibrationLookupMelee(const char *model_name,
	vr_melee_gesture_profile_t *out);
void VR_WeaponCalibrationProjectileSourceOffset(const char *viewmodel,
	int weapon_bit, const vec3_t angles, float viewheight, vec3_t out);

/* QuakeC program identity shared by calibration and the weapon wheel. It is
 * derived from the active game's progs.dat content (field/function names and
 * string markers), so a fork installed under another directory name still
 * receives its family's weapon policy. Unknown means "use directory rules". */
typedef enum
{
	VR_PROGS_FAMILY_UNKNOWN,
	VR_PROGS_FAMILY_AD,        /* Arcane Dimensions weapon upgrades (moditems). */
	VR_PROGS_FAMILY_DRAKE,     /* Drake arsenal/war weapon roster. */
	VR_PROGS_FAMILY_QUOTH,     /* Quoth plasma gun/bomb (warpspasm, immortal). */
	VR_PROGS_FAMILY_ZER,       /* Zerstorer chainsaw/chain-lightning/SRE. */
	VR_PROGS_FAMILY_PD_LASER,  /* progs_dump fork with a 128-bit laser cannon (Sacrilege). */
	VR_PROGS_FAMILY_QUAKEVR    /* QuakeVR mode-based arsenal. */
} vr_progs_family_t;

typedef struct
{
	vr_progs_family_t family;
	qboolean valid;     /* A version 6 progs.dat was found and parsed. */
	qboolean rerelease; /* 2021 rerelease localized QuakeC ($qc_ strings). */
} vr_progs_identity_t;

qboolean VR_ProgsIdentityParse(const byte *data, size_t size,
	vr_progs_identity_t *out);
/* Forced re-read of the active game's progs.dat (calibration game reload). */
void VR_ProgsIdentityRefresh(void);
/* Cached identity; re-read only when com_gamedir differs from the cache. */
const vr_progs_identity_t *VR_ProgsIdentityCurrent(void);
qboolean VR_ProgsIdentityIsAD(void);

#endif
