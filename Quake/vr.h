// IronwailVR Public API Header
// Based on QuakeSpasm-OpenVR, modified for OpenXR

#ifndef __VR_H
#define __VR_H

#ifdef VR_ENABLED

#ifdef __cplusplus
extern "C" {
#endif

// Aiming Modes
#define VR_AIMMODE_HEAD_MYAW 1
#define VR_AIMMODE_HEAD_MYAW_MPITCH 2
#define VR_AIMMODE_MOUSE_MYAW 3
#define VR_AIMMODE_MOUSE_MYAW_MPITCH 4
#define VR_AIMMODE_BLENDED 5
#define VR_AIMMODE_BLENDED_NOPITCH 6
#define VR_AIMMODE_CONTROLLER 7

// Crosshair Modes
#define VR_CROSSHAIR_NONE 0
#define VR_CROSSHAIR_POINT 1
#define VR_CROSSHAIR_LINE 2

// Movement Modes
#define VR_MOVEMENT_MODE_FOLLOW_HEAD 0
#define VR_MOVEMENT_MODE_FOLLOW_HAND 1
#define VR_MOVEMENT_MODE_RAW_INPUT 2

// Weapon Offset Presets
#define VR_GUNMODELOFFSETS_VANILLA 0
#define VR_GUNMODELOFFSETS_ENHANCED 1
#define VR_GUNMODELOFFSETS_AUTHENTIC 2
#define VR_GUNMODELOFFSETS_PLAGUE 3
#define VR_DEFAULT_SCALE 0.03125f

#define VR_HAND_LEFT 0
#define VR_HAND_RIGHT 1

// Core lifecycle
void VR_InitCommands(void);
void VR_OpenXR_Init(void);
void VR_OpenXR_InitActions(void);
void VR_OpenXR_Shutdown(void);
qboolean VR_Enabled(void);

// Frame updates
void VR_UpdatePoses(void);
void VR_Move(usercmd_t *cmd);
void VR_GetHandPose(int hand, vec3_t pos, vec3_t angles);
void VR_GetRoomscaleAccum(vec3_t v);
void VR_GetThumbstick(int hand, float *x, float *y);

// Rendering integration
void VR_Draw2D(void);
void VR_DrawSbar(void);
void VR_DrawWeaponMenu(void);

void VR_Render(void);

// Weapon Tracking
void VR_LoadWeaponSchema(void);
void VR_InitGame(void);
void VR_TrackWeapons(void);
void VR_ResetWeaponTracking(void);
void InitAllWeaponCVars(void);
int VR_GetWeaponOffset(const char *name, vec3_t offset, float *scale);
qboolean VR_CheckWallCollision(vec3_t hand_origin, vec3_t hand_angles);

// View / Rendering
void VR_SetMatrices(int eye);
void VR_ApplyViewOverride(vec3_t origin, vec3_t angles);
void VR_PostProcess(void);

// Controls / Comfort
void VR_TriggerHaptic(int hand, float duration, float frequency,
                      float amplitude);
void VR_ResetOrientation(void);

// External CVARs
extern cvar_t vr_enabled;
extern cvar_t vr_aimmode;
extern cvar_t vr_movement_mode;
extern cvar_t vr_world_scale;
extern cvar_t vr_floor_offset;
extern cvar_t vr_snap_turn;
extern cvar_t vr_turn_speed;
extern cvar_t vr_hud_scale;
extern cvar_t vr_menu_scale;
extern cvar_t vr_vanilla_compat;

extern cvar_t vr_gunangle;
extern cvar_t vr_gunmodeloffsets;
extern cvar_t vr_gunmodelpitch;
extern cvar_t vr_gunmodelscale;
extern cvar_t vr_gunmodely;

#define MAX_VR_WEAPONS 32
#define MAX_WEAPONS 25
#define VARS_PER_WEAPON 5

extern cvar_t vr_weapon_offset[MAX_WEAPONS * VARS_PER_WEAPON];
extern int weaponCVarEntry;

typedef struct {
  int bitmask;
  char model_path[64];
  int impulse;
  float scale;
  vec3_t offset;
} vr_weapon_cmd_t;

typedef struct {
  int bitmask;
  int model_index;
  char model_path[64];
  int impulse;
  qboolean discovered;
} vr_dyn_weapon_t;

#define MAX_DYN_WEAPONS 32

extern vr_weapon_cmd_t vr_weapons[MAX_VR_WEAPONS];
extern int num_vr_weapons;

extern vr_dyn_weapon_t dyn_weapons[MAX_DYN_WEAPONS];
extern int num_dyn_weapons;

extern int vr_last_sent_impulse;
extern double vr_last_sent_impulse_time;
extern int vr_weaponmenu_selection;
extern double vr_next_weapon_switch_time;
extern qboolean vr_autoscan_active;
extern int vr_autoscan_impulse;
extern double vr_autoscan_next_time;

extern qboolean in_vr_weaponmenu;

#ifdef __cplusplus
}
#endif

#endif // VR_ENABLED
#endif // __VR_H
