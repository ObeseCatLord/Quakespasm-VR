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
qboolean VR_WeaponsKey(int key, qboolean down);
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

#define MAX_WEAPONS 25 // not sure what this number should actually be...
#define VARS_PER_WEAPON 5

extern cvar_t vr_weapon_offset[MAX_WEAPONS * VARS_PER_WEAPON];
extern int weaponCVarEntry;
extern vec3_t vr_room_scale_move;

#define MAX_VR_WEAPONS 16

typedef struct {
  int bitmask;
  char model_path[64];
  int impulse;
  float scale;
  vec3_t offset;
} vr_weapon_cmd_t;

extern vr_weapon_cmd_t vr_weapons[MAX_VR_WEAPONS];
extern int num_vr_weapons;

#ifdef __cplusplus
}
#endif

#endif
