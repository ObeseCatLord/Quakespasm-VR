// 2016 Dominic Szablewski - phoboslab.org

#include "quakedef.h"

#ifndef __R_VR_H
#define __R_VR_H

#ifdef __cplusplus
extern "C" {
#endif

#define VR_AIMMODE_HEAD_MYAW                                                   \
  1 // Head Aiming; View YAW is mouse+head, PITCH is head
#define VR_AIMMODE_HEAD_MYAW_MPITCH                                            \
  2 // Head Aiming; View YAW and PITCH is mouse+head
#define VR_AIMMODE_MOUSE_MYAW                                                  \
  3 // Mouse Aiming; View YAW is mouse+head, PITCH is head
#define VR_AIMMODE_MOUSE_MYAW_MPITCH                                           \
  4 // Mouse Aiming; View YAW and PITCH is mouse+head
#define VR_AIMMODE_BLENDED                                                     \
  5 // Blended Aiming; Mouse aims, with YAW decoupled for limited area
#define VR_AIMMODE_BLENDED_NOPITCH                                             \
  6 // Blended Aiming; Mouse aims, with YAW decoupled for limited area, pitch
    // decoupled entirely
#define VR_AIMMODE_CONTROLLER 7 // Controller Aiming

#define VR_CROSSHAIR_NONE 0 // No crosshair
#define VR_CROSSHAIR_POINT                                                     \
  1 // Point crosshair projected to depth of object it is in front of
#define VR_CROSSHAIR_LINE 2 // Line crosshair

#define VR_MOVEMENT_MODE_FOLLOW_HEAD 0
#define VR_MOVEMENT_MODE_FOLLOW_HAND 1
#define VR_MOVEMENT_MODE_RAW_INPUT 2
#define VR_MAX_MOVEMENT_MODE VR_MOVEMENT_MODE_RAW_INPUT

#define VR_GUNMODELOFFSETS_VANILLA                                             \
  0 // Gun model offset defaults for vanilla models
#define VR_GUNMODELOFFSETS_ENHANCED                                            \
  1 // Gun model offset defaults for enhanced models pack
    // (https://quakeone.com/forum/quake-mod-releases/finished-works/283295-osjc-s-enhanced-quake1-model-conversions-pack-v1)
#define VR_GUNMODELOFFSETS_AUTHENTIC                                           \
  2 // Gun model offset defaults for authentic models pack
    // (https://github.com/NightFright2k19/quake_authmdl)
#define VR_GUNMODELOFFSETS_PLAGUE                                              \
  3 // Gun model offset defaults for plague's models pack
    // (https://github.com/NightFright2k19/quake_authmdl)
#define VR_GUNMODELOFFSETS_BLOCKQUAKE                                          \
  4 // Gun model offset defaults for Block Quake's model pack
    // (https://kebby-quake.itch.io/block-quake)
#define VR_MAX_GUNMODELOFFSETS VR_GUNMODELOFFSETS_BLOCKQUAKE

void VID_VR_Init();
void VID_VR_Shutdown();
qboolean VR_Enable();
void VID_VR_Disable();
void IN_VRTurn180_f(void);

void VR_UpdateScreenContent();
void VR_ShowCrosshair();
void VR_DrawWeaponMenu();
void VR_DrawAdjustmentControllers();
void VR_ApplyCurrentViewWeaponTransform();
extern int vr_weaponmenu_selection;
void VR_TriggerHaptic(int controller, float durationSeconds);
void VR_Draw2D();
void VR_Draw2D();
void VR_Move(usercmd_t *cmd);
void VR_LoadWeaponSchema();
void VR_InitGame();
void VR_PushYaw();
void VR_TrackWeapons();
void VR_ResetWeaponTracking();
int VR_GetSelectedWeaponImpulse(int selection);
void VR_SelectWeaponFromMenu(int selection);
void VR_DrawSbar();
void VR_AddOrientationToViewAngles(vec3_t angles);
void VR_SetAngles(vec3_t angles);
void VR_ResetOrientation();
void VR_SetMatrices();
void VR_HandleGammaCorrect();
void VR_PollPoses();
void InitAllWeaponCVars();

extern cvar_t vr_enabled;
extern cvar_t vr_aimmode;
extern cvar_t vr_crosshair;
extern cvar_t vr_msaa;
extern cvar_t vr_movement_mode;
extern cvar_t vr_gunangle;
extern cvar_t vr_deadzone;
extern cvar_t vr_snap_turn;
extern cvar_t vr_180_snap_turn;
extern cvar_t vr_turn_speed;
extern cvar_t vr_gunmodeloffsets;
extern cvar_t vr_gunmodelpitch;
extern cvar_t vr_gunmodelscale;
extern cvar_t vr_gunmodely;
extern cvar_t vr_crosshairy;
extern cvar_t vr_floor_offset;
extern cvar_t vr_projectilespawn_z_offset;
extern cvar_t vr_haptic;
extern cvar_t vr_hud_scale;
extern cvar_t vr_menu_scale;
extern cvar_t vr_movement_instant_stop;
extern cvar_t vr_movement_speed;
extern float vr_game_projectile_z_extra;

#define MAX_WEAPONS 64 // schema-driven mod offsets can exceed the old presets
#define VARS_PER_WEAPON 5
#define VARS_PER_WEAPON_MUZZLE 3

extern cvar_t vr_weapon_offset[MAX_WEAPONS * VARS_PER_WEAPON];
extern cvar_t vr_weapon_muzzle_offset[MAX_WEAPONS * VARS_PER_WEAPON_MUZZLE];
extern int weaponCVarEntry;
extern vec3_t vr_room_scale_move;

#define MAX_VR_WEAPONS 64

typedef struct {
  int bitmask;
  char model_path[64];
  char viewmodel_path[64];
  int impulse;
  float scale;
  vec3_t offset;
  qboolean has_offset;
  float held_scale;
  vec3_t held_offset;
  qboolean has_held_scale;
  qboolean has_held_offset;
  vec3_t mp_held_offset;
  qboolean has_mp_held_offset;
  vec3_t schema_mp_held_offset;
  qboolean has_schema_mp_held_offset;
  vec3_t muzzle_offset;
  qboolean has_muzzle_offset;
  vec3_t mp_muzzle_offset;
  qboolean has_mp_muzzle_offset;
  vec3_t schema_mp_muzzle_offset;
  qboolean has_schema_mp_muzzle_offset;
  vec3_t muzzle_source_offset;
  qboolean has_muzzle_source_offset;
  qboolean muzzle_source_viewofs;
  qboolean has_muzzle_source_viewofs;
  int owned_stat;
  int owned_mask;
  int active_stat;
  int active_mask;
  int ammo_stat;
  int ammo_max;
} vr_weapon_cmd_t;

extern vr_weapon_cmd_t vr_weapons[MAX_VR_WEAPONS];
extern int num_vr_weapons;

void VR_GetMuzzleAdjustedHandPos(vec3_t out);
extern int vr_last_sent_impulse;
extern double vr_last_sent_impulse_time;

#ifdef __cplusplus
}
#endif

#endif
