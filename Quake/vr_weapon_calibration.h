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
qboolean VR_WeaponCalibrationApplySchema(
	const vr_weapon_schema_entry_t *entries, size_t count);
qboolean VR_WeaponCalibrationLookupHeld(const char *model_name,
										qboolean enhanced_format,
										qboolean multiplayer,
										vec3_t out_offset,
										float *out_scale);
qboolean VR_WeaponCalibrationLookupMuzzle(const char *model_name,
										  qboolean enhanced_format,
										  qboolean multiplayer, vec3_t out);
qboolean VR_WeaponCalibrationCurrentMuzzle(vec3_t out);
qboolean VR_WeaponCalibrationStockRangedViewmodel(const char *name);
void VR_WeaponCalibrationProjectileSourceOffset(const char *viewmodel,
	int weapon_bit, const vec3_t angles, float viewheight, vec3_t out);

#endif
