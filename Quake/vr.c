

#ifdef __cplusplus
extern "C" {
#endif

#include "vr.h"
#include "quakedef.h"
#include "sys.h"
#include "vr_menu.h"
#include "zone.h"
#include "debug_log.h"

#ifdef __cplusplus
}
#endif

#ifdef _WIN32
#include <GL/gl.h>
#include <windows.h>

#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

#ifndef GL_TEXTURE_2D_MULTISAMPLE
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_MAX_SAMPLES
#define GL_MAX_SAMPLES 0x8D57
#endif
typedef void(APIENTRYP PFNGLTEXIMAGE2DMULTISAMPLEPROC)(
    GLenum target, GLsizei samples, GLenum internalformat, GLsizei width,
    GLsizei height, GLboolean fixedsamplelocations);
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern void VID_Refocus();

// main screen & 2D drawing
extern void SCR_SetUpToDrawConsole(void);
extern void SCR_UpdateScreenContent();
extern void GLSLGamma_GammaCorrect(void);
extern qboolean scr_drawdialog;
extern void SCR_DrawNotifyString(void);
extern qboolean scr_drawloading;
extern void SCR_DrawLoading(void);
extern void SCR_CheckDrawCenterString(void);
extern void SCR_DrawNet(void);
extern void SCR_DrawTurtle(void);
extern void SCR_DrawPause(void);
extern void SCR_DrawDevStats(void);
extern void SCR_DrawFPS(void);
extern void SCR_DrawClock(void);
extern void SCR_DrawConsole(void);
extern void Sbar_IntermissionOverlay();
extern void Sbar_FinaleOverlay();
extern void M_Draw();

static qboolean VR_InputDebugEnabled(void) {
  return Cvar_VariableValue("in_debugkeys") != 0;
}

typedef struct {
  int key;
  const char *binding;
} vr_default_binding_t;

static const vr_default_binding_t vr_default_bindings[] = {
    {K_LTRIGGER, "+jump"},
    {K_RTRIGGER, "+attack"},
    {K_BBUTTON, "impulse 10"},
    {K_LTHUMB, "+speed"},
    {K_RTHUMB, "+jump"},
    {K_VR_ALTFIRE, "+button3"},
    {K_LSHOULDER, "+showscores"},
    {K_RSHOULDER, "+showscores"},
    {K_ABUTTON, "+showscores"},
    {K_XBUTTON, "impulse 12"},
    {K_VR_RIGHT_STICK_UP, "+vr_weaponmenu"},
    {K_VR_RIGHT_STICK_DOWN, "vr_turn180"},
};

void VR_ApplyDefaultBindings(qboolean overwrite) {
  size_t i;

  if (!vr_enabled.value)
    return;

  for (i = 0; i < sizeof(vr_default_bindings) / sizeof(vr_default_bindings[0]);
       i++) {
    int key = vr_default_bindings[i].key;
    if (key < 0 || key >= MAX_KEYS)
      continue;
    if (!overwrite && keybindings[key] && keybindings[key][0])
      continue;
    Key_SetBinding(key, vr_default_bindings[i].binding);
  }
}

static void VR_DefaultBindings_f(void) { VR_ApplyDefaultBindings(false); }

// rendering
extern void R_SetupView(void);
extern void R_RenderScene(void);
extern int glx, gly, glwidth, glheight;
extern refdef_t r_refdef;
extern vec3_t vright;

extern cvar_t gl_farclip;
extern int glwidth, glheight;

#ifdef __cplusplus
}
#endif

#define UNICODE 1
#ifdef _WIN32
#include <mmsystem.h>
#endif
#undef UNICODE

#include "openvr.h"

#ifdef __cplusplus
extern "C" {
#endif

// extern void VID_Refocus();

typedef struct {
  GLuint framebuffer, depth_texture, texture;
  GLuint msaa_framebuffer, msaa_texture, msaa_depth_texture;
  GLenum color_format, depth_format;
  qboolean highprecision_request;
  int msaa;
  struct {
    float width, height;
  } size;
} fbo_t;

typedef struct {
  int index;
  fbo_t fbo;
  vr::EVREye eye;
  vr::HmdVector3_t position;
  vr::HmdQuaternion_t orientation;
  float fov_x, fov_y;
} vr_eye_t;

typedef struct {
  vr::VRControllerState_t state;
  vr::VRControllerState_t lastState;
  uint64_t emittedButtonPressed;
  vr::TrackedDeviceIndex_t deviceIndex;
  qboolean seenThisFrame;
  qboolean triggerDown;
  int triggerKey;
  vec3_t position;
  vec3_t orientation;
  vr::HmdVector3_t rawvector;
  vr::HmdQuaternion_t raworientation;
} vr_controller;

typedef struct {
  char name[256];
  vr::RenderModel_t *model;
  vr::RenderModel_TextureMap_t *texture_map;
  GLuint texture_id;
  qboolean model_failed;
  qboolean texture_failed;
} vr_controller_render_model_t;

// OpenGL Extensions
#define GL_READ_FRAMEBUFFER_EXT 0x8CA8
#define GL_DRAW_FRAMEBUFFER_EXT 0x8CA9
#define GL_FRAMEBUFFER_SRGB_EXT 0x8DB9

typedef void(APIENTRYP PFNGLBLITFRAMEBUFFEREXTPROC)(GLint, GLint, GLint, GLint,
                                                    GLint, GLint, GLint, GLint,
                                                    GLbitfield, GLenum);

static PFNGLBINDFRAMEBUFFEREXTPROC glBindFramebufferEXT;
static PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC glCheckFramebufferStatusEXT;
static PFNGLBLITFRAMEBUFFEREXTPROC glBlitFramebufferEXT;
static PFNGLDELETEFRAMEBUFFERSEXTPROC glDeleteFramebuffersEXT;
static PFNGLGENFRAMEBUFFERSEXTPROC glGenFramebuffersEXT;
static PFNGLFRAMEBUFFERTEXTURE2DEXTPROC glFramebufferTexture2DEXT;
static PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC glFramebufferRenderbufferEXT;
static PFNGLTEXIMAGE2DMULTISAMPLEPROC glTexImage2DMultisampleEXT;

struct {
  void *func;
  const char *name;
} gl_extensions[] = {
    {&glBindFramebufferEXT, "glBindFramebufferEXT"},
    {&glBlitFramebufferEXT, "glBlitFramebufferEXT"},
    {&glDeleteFramebuffersEXT, "glDeleteFramebuffersEXT"},
    {&glGenFramebuffersEXT, "glGenFramebuffersEXT"},
    {&glTexImage2DMultisampleEXT, "glTexImage2DMultisample"},
    {&glFramebufferTexture2DEXT, "glFramebufferTexture2DEXT"},
    {&glFramebufferRenderbufferEXT, "glFramebufferRenderbufferEXT"},
    {&glCheckFramebufferStatusEXT, "glCheckFramebufferStatusEXT"},
    {NULL, NULL},
};

// rendering externs moved to top extern "C" block

static float vrYaw;
static bool readbackYaw;

vec3_t vr_viewOffset;
static vec3_t lastHudPosition{0.0, 0.0, 0.0};
static vec3_t lastMenuPosition{0.0, 0.0, 0.0};

/*
 * The menu is drawn as a 320x200 plane in world space.  Cache the exact
 * surface produced by VR_Draw2D so controller hit-testing in VR_Move uses the
 * rendered transform rather than a separately reconstructed approximation.
 */
typedef struct {
  vec3_t center;
  vec3_t right;
  vec3_t down;
  vec3_t normal;
  float scale;
  qboolean valid;
} vr_menu_surface_t;

static vr_menu_surface_t vr_menu_surface;

vr_weapon_cmd_t vr_weapons[MAX_VR_WEAPONS];
int num_vr_weapons = 0;

static vr::IVRSystem *ovrHMD;
static vr::TrackedDevicePose_t ovr_DevicePose[vr::k_unMaxTrackedDeviceCount];

static vr_eye_t eyes[2];
static vr_eye_t *current_eye = NULL;
static vr_controller controllers[2];
static vr_controller_render_model_t controller_render_models[2];
static qboolean vr_adjust_suppressed_rtrigger = false;
static qboolean vr_adjust_muzzle_return_to_grip = false;

static void VR_AdjustWeaponUpdatePose(void);
static qboolean VR_AdjustWeaponConsumeTrigger(void);
static void VR_AimOffsetToWorld(const vec3_t local, const vec3_t angles,
                                float scale, vec3_t world);
static void VR_FreeControllerRenderModels(void);

static void VR_SetTrigger(vr_controller *controller, int quakeKey,
                          qboolean down) {
  if (down) {
    if (controller->triggerDown) {
      if (controller->triggerKey == quakeKey)
        return;

      Key_Event(controller->triggerKey, false);
    }

    controller->triggerDown = true;
    controller->triggerKey = quakeKey;
    Key_Event(quakeKey, true);
  } else {
    if (!controller->triggerDown)
      return;

    Key_Event(controller->triggerKey ? controller->triggerKey : quakeKey,
              false);
    controller->triggerDown = false;
    controller->triggerKey = 0;
  }
}

static void VR_ReleaseControllerInputs(void) {
  VR_SetTrigger(&controllers[0], K_LTRIGGER, false);
  VR_SetTrigger(&controllers[1], K_RTRIGGER, false);
  vr_adjust_suppressed_rtrigger = false;
  vr_adjust_muzzle_return_to_grip = false;
}

static vec3_t lastOrientation = {0, 0, 0};
static vec3_t lastAim = {0, 0, 0};

static qboolean vr_initialized = false;
extern "C" {
int vr_weaponmenu_selection = -1;
int vr_weaponmenu_selection_type = VR_WEAPONMENU_SELECTION_NONE;
}
static qboolean vr_weaponmenu_anchor_valid = false;
static vec3_t vr_weaponmenu_anchor_viewangles;

extern void GL_ClearBindings(void);

// Dynamic weapon tracking
typedef struct {
  int bitmask; // IT_* bitmask value
  int impulse; // impulse number to switch to this weapon
  const char *model_path; // weapon wheel model path
  int model_index; // precache model index (learned at runtime for mod weapons)
  qboolean discovered; // has the model been discovered at runtime?
  float scale;
  vec3_t offset;
  qboolean has_offset;
  int owned_stat;
  int owned_mask;
  int active_stat;
  int active_mask;
  int ammo_stat;
  int ammo_max;
  qboolean from_schema;
  qboolean use_item_ownership;
} vr_dyn_weapon_t;

#define MAX_DYN_WEAPONS 128
static vr_dyn_weapon_t dyn_weapons[MAX_DYN_WEAPONS] = {
    {4096, 1, "progs/g_axe.mdl", 0, false, 1.0f, {0, 0, 0}, false,
     -1, 0, -1, 0, -1, 0, false, true}, // IT_AXE (pickup model)
    {1, 2, "progs/g_shot.mdl", 0, false, 1.0f, {0, 0, 0}, false,
     -1, 0, -1, 0, STAT_SHELLS, 100, false, true}, // IT_SHOTGUN
    {2, 3, "progs/g_shot2.mdl", 0, false, 1.0f, {0, 0, 0}, false,
     -1, 0, -1, 0, STAT_SHELLS, 100, false, true}, // IT_SUPER_SHOTGUN
    {4, 4, "progs/g_nail.mdl", 0, false, 1.0f, {0, 0, 0}, false,
     -1, 0, -1, 0, STAT_NAILS, 200, false, true}, // IT_NAILGUN
    {8, 5, "progs/g_nail2.mdl", 0, false, 1.0f, {0, 0, 0}, false,
     -1, 0, -1, 0, STAT_NAILS, 200, false, true}, // IT_SUPER_NAILGUN
    {16, 6, "progs/g_rock.mdl", 0, false, 1.0f, {0, 0, 0}, false,
     -1, 0, -1, 0, STAT_ROCKETS, 100, false, true}, // IT_GRENADE_LAUNCHER
    {32, 7, "progs/g_rock2.mdl", 0, false, 1.0f, {0, 0, 0}, false,
     -1, 0, -1, 0, STAT_ROCKETS, 100, false, true}, // IT_ROCKET_LAUNCHER
    {64, 8, "progs/g_light.mdl", 0, false, 1.0f, {0, 0, 0}, false,
     -1, 0, -1, 0, STAT_CELLS, 100, false, true}, // IT_LIGHTNING
};
static int num_dyn_weapons = 8;
static qboolean rogue_weapons_added = false;
static qboolean hipnotic_weapons_added = false;
static qboolean dwell_weapons_added = false;
static int dwell_weapon_indices[3] = {-1, -1, -1};
static int last_tracked_activeweapon = -1;

// Impulse sniffing/discovery state
extern "C" {
int vr_last_sent_impulse = 0;
double vr_last_sent_impulse_time = 0;
}
static int vr_autoscan_impulse = 0;
static double vr_autoscan_next_time = 0;
static bool vr_autoscan_active = false;
double vr_next_weapon_switch_time = 0; // Debounce for switching
static int vr_weapon_cycle_target = -1;
static int vr_weapon_cycle_impulse = 0;
static int vr_weapon_cycle_attempts = 0;
static double vr_weapon_cycle_next_time = 0;

#define VR_WEAPON_CYCLE_MAX_ATTEMPTS 8
#define VR_WEAPON_CYCLE_INTERVAL 0.10

static int VR_ParseStatName(const char *value, int *default_max) {
  if (default_max)
    *default_max = 0;
  if (!value || !value[0])
    return -1;

  if (!q_strcasecmp(value, "ammo"))
    return STAT_AMMO;
  if (!q_strcasecmp(value, "shells")) {
    if (default_max)
      *default_max = 100;
    return STAT_SHELLS;
  }
  if (!q_strcasecmp(value, "nails")) {
    if (default_max)
      *default_max = 200;
    return STAT_NAILS;
  }
  if (!q_strcasecmp(value, "rockets")) {
    if (default_max)
      *default_max = 100;
    return STAT_ROCKETS;
  }
  if (!q_strcasecmp(value, "cells")) {
    if (default_max)
      *default_max = 100;
    return STAT_CELLS;
  }
  if (!q_strcasecmp(value, "activeweapon"))
    return STAT_ACTIVEWEAPON;
  if (!q_strcasecmp(value, "weapon"))
    return STAT_WEAPON;
  if (!q_strcasecmp(value, "weapons"))
    return STAT_VR_WEAPONS;
  if (!q_strcasecmp(value, "items2"))
    return STAT_VR_ITEMS2;
  if (!q_strcasecmp(value, "moditems"))
    return STAT_VR_MODITEMS;
  if (!q_strcasecmp(value, "weapon2"))
    return STAT_VR_WEAPON2;
  if (!q_strcasecmp(value, "weapons2"))
    return STAT_VR_WEAPONS2;

  if ((value[0] >= '0' && value[0] <= '9') ||
      ((value[0] == '-' || value[0] == '+') && value[1] >= '0' &&
       value[1] <= '9')) {
    int stat = Q_atoi(value);
    if (stat >= 0 && stat < MAX_CL_STATS)
      return stat;
  }

  return -1;
}

static void VR_InitDynWeapon(vr_dyn_weapon_t *w) {
  memset(w, 0, sizeof(*w));
  w->scale = 1.0f;
  w->owned_stat = -1;
  w->active_stat = -1;
  w->ammo_stat = -1;
}

static qboolean VR_DynWeaponHasModelDiscriminator(const vr_dyn_weapon_t *w) {
  return (w->model_path && w->model_path[0]) || w->model_index > 0;
}

static int VR_DynWeaponTrustedItemBits(void) {
  int weapon_bits = IT_SHOTGUN | IT_SUPER_SHOTGUN | IT_NAILGUN |
                    IT_SUPER_NAILGUN | IT_GRENADE_LAUNCHER |
                    IT_ROCKET_LAUNCHER | IT_LIGHTNING;

  if (rogue) {
    weapon_bits |= RIT_AXE | RIT_LAVA_NAILGUN | RIT_LAVA_SUPER_NAILGUN |
                   RIT_MULTI_GRENADE | RIT_MULTI_ROCKET | RIT_PLASMA_GUN;
  } else {
    weapon_bits |= IT_AXE;
  }

  if (hipnotic)
    weapon_bits |= HIT_MJOLNIR | HIT_LASER_CANNON | HIT_PROXIMITY_GUN;

  return weapon_bits;
}

static qboolean VR_DynWeaponCanUseItemOwnership(const vr_dyn_weapon_t *w) {
  if (!w->use_item_ownership && !w->from_schema)
    return false;
  if (!w->bitmask)
    return false;
  return (w->bitmask & VR_DynWeaponTrustedItemBits()) == w->bitmask;
}

static int VR_ClientItemBits(void) {
  /*
   * svc_clientdata and stat updates can arrive on different paths during
   * signon/replacement-delta catch-up. Treat either cache as authoritative so
   * late joins do not temporarily hide owned weapons from the wheel.
   */
  return cl.stats[STAT_ITEMS] | cl.items;
}

static qboolean VR_IsDwellGame(void) {
  const char *game = COM_SkipPath(com_gamedir);

  return game && (!q_strcasecmp(game, "dwell") ||
                  !q_strcasecmp(game, "dwellv2p2"));
}

static qboolean VR_GameDirIs(const char *name) {
  const char *game = COM_SkipPath(com_gamedir);

  return game && name && !q_strcasecmp(game, name);
}

static int VR_DwellOwnedMaskForActive(int active) {
  switch (active) {
  case 128: // W_QUAD_SHOTGUN / Rotary Shotgun
    return 4;
  case 256: // W_RAILGUN / Crystal Lance
    return 8;
  case 512: // W_RIFLE
    return 32;
  default:
    return 0;
  }
}

static int VR_DwellImpulseForActive(int active) {
  switch (active) {
  case 2:
    return 23; // Double Shotgun
  case 64:
    return 28; // Lightning Gun
  case 128:
    return 33; // Rotary Shotgun
  case 256:
    return 38; // Crystal Lance
  case 512:
    return 2; // Rifle shares the shotgun selection key
  default:
    return 0;
  }
}

static int VR_DwellAmmoForActive(int active, int *ammo_max) {
  if (ammo_max)
    *ammo_max = 100;

  switch (active) {
  case 1:
  case 2:
  case 128:
  case 512:
    return STAT_SHELLS;
  case 4:
  case 8:
    if (ammo_max)
      *ammo_max = 200;
    return STAT_NAILS;
  case 16:
  case 32:
    return STAT_ROCKETS;
  case 64:
  case 256:
    return STAT_CELLS;
  default:
    return -1;
  }
}

static void VR_ApplyDwellWeaponMetadata(vr_dyn_weapon_t *w, int active) {
  int impulse;
  int owned_mask;
  int ammo_stat;
  int ammo_max;

  if (!w || !VR_IsDwellGame())
    return;

  impulse = VR_DwellImpulseForActive(active);
  owned_mask = VR_DwellOwnedMaskForActive(active);
  if (owned_mask > 0) {
    w->owned_stat = STAT_VR_MODITEMS;
    w->owned_mask = owned_mask;
    if (impulse > 0)
      w->impulse = impulse;
  }

  ammo_stat = VR_DwellAmmoForActive(active, &ammo_max);
  if (ammo_stat >= 0) {
    w->ammo_stat = ammo_stat;
    w->ammo_max = ammo_max;
  }
}

static qboolean VR_FindWeaponOwnedStatForActive(int active, int *owned_stat,
                                                int *owned_mask) {
  static const int ownership_stats[] = {
      STAT_VR_WEAPONS,  STAT_VR_ITEMS2,  STAT_VR_MODITEMS,
      STAT_VR_WEAPON2, STAT_VR_WEAPONS2,
  };
  size_t i;

  if (!active)
    return false;

  if (VR_IsDwellGame()) {
    int dwell_mask = VR_DwellOwnedMaskForActive(active);
    if (dwell_mask && (cl.stats[STAT_VR_MODITEMS] & dwell_mask)) {
      if (owned_stat)
        *owned_stat = STAT_VR_MODITEMS;
      if (owned_mask)
        *owned_mask = dwell_mask;
      return true;
    }
  }

  for (i = 0; i < sizeof(ownership_stats) / sizeof(ownership_stats[0]); i++) {
    int stat = ownership_stats[i];
    if (cl.stats[stat] & active) {
      if (owned_stat)
        *owned_stat = stat;
      if (owned_mask)
        *owned_mask = active;
      return true;
    }
  }

  return false;
}

static qboolean VR_DynWeaponModelMatches(const vr_dyn_weapon_t *w,
                                         const char *model_path,
                                         int model_index) {
  qboolean query_has_model =
      (model_path && model_path[0]) || model_index > 0;

  if (!query_has_model || !VR_DynWeaponHasModelDiscriminator(w))
    return true;
  if (model_path && model_path[0] && w->model_path && w->model_path[0] &&
      !q_strcasecmp(model_path, w->model_path))
    return true;
  if (model_index > 0 && w->model_index == model_index)
    return true;
  return false;
}

static qboolean VR_ModelIndexMatchesPath(int model_index, const char *path) {
  qmodel_t *model;

  if (model_index <= 0 || model_index >= MAX_MODELS || !path || !path[0])
    return false;
  model = cl.model_precache[model_index];
  if (!model || !model->name[0])
    return false;
  return !q_strcasecmp(model->name, path);
}

static const char *VR_ModelPathForIndex(int model_index) {
  qmodel_t *model;

  if (model_index <= 0 || model_index >= MAX_MODELS)
    return NULL;
  model = cl.model_precache[model_index];
  if (!model || !model->name[0])
    return NULL;
  return model->name;
}

static qboolean VR_ModelPathLooksWeapon(const char *path) {
  const char *base;
  static const char *blocked[] = {
      "player", "rune", "sigil", "key", "armor", "health",
      "backpack", "gib", "head", "corpse", NULL};
  static const char *projectile_blocked[] = {
      "spike", "vore", "lavaball", "fireball", "proj", "zgrenade",
      "trsh", NULL};

  if (!path || !path[0])
    return false;

  base = COM_SkipPath(path);
  for (int i = 0; blocked[i]; i++) {
    if (q_strcasestr(base, blocked[i]))
      return false;
  }
  for (int i = 0; projectile_blocked[i]; i++) {
    if (q_strcasestr(base, projectile_blocked[i]))
      return false;
  }

  if ((base[0] == 'v' || base[0] == 'V') && base[1] == '_')
    return true;
  if ((base[0] == 'g' || base[0] == 'G') && base[1] == '_')
    return true;

  return q_strcasestr(base, "weapon") || q_strcasestr(base, "gun") ||
         q_strcasestr(base, "shot") || q_strcasestr(base, "rifle") ||
         q_strcasestr(base, "pistol") || q_strcasestr(base, "launcher") ||
         q_strcasestr(base, "wrench") || q_strcasestr(base, "hammer");
}

static qboolean VR_ModelIndexLooksWeapon(int model_index) {
  return VR_ModelPathLooksWeapon(VR_ModelPathForIndex(model_index));
}

static int VR_FindDynWeapon(int bitmask, int owned_stat, int owned_mask,
                            int active_stat, int active_mask,
                            const char *model_path, int model_index) {
  for (int i = 0; i < num_dyn_weapons; i++) {
    qboolean selector_match = false;

    if (bitmask && dyn_weapons[i].bitmask == bitmask)
      selector_match = true;
    if (owned_stat >= 0 && dyn_weapons[i].owned_stat == owned_stat &&
        dyn_weapons[i].owned_mask == owned_mask)
      selector_match = true;
    if (active_stat >= 0 && dyn_weapons[i].active_stat == active_stat &&
        dyn_weapons[i].active_mask == active_mask)
      selector_match = true;

    if (selector_match &&
        VR_DynWeaponModelMatches(&dyn_weapons[i], model_path, model_index))
      return i;
  }

  return -1;
}

static int VR_FindDynWeaponForActive(int active, int model_index) {
  int unlearned = -1;

  for (int i = 0; i < num_dyn_weapons; i++) {
    vr_dyn_weapon_t *w = &dyn_weapons[i];
    qboolean active_match = false;

    if (w->active_stat >= 0) {
      int value = cl.stats[w->active_stat];
      active_match = w->active_mask ? ((value & w->active_mask) != 0)
                                    : (value != 0);
    } else if (w->bitmask) {
      active_match = (w->bitmask == active);
    }

    if (!active_match)
      continue;
    if (w->model_index > 0 && w->model_index == model_index)
      return i;
    if (w->model_index == 0 && VR_ModelIndexMatchesPath(model_index,
                                                        w->model_path))
      return i;
    if (w->model_index == 0 && unlearned < 0)
      unlearned = i;
  }

  return unlearned;
}

static int VR_FindImpulseForActiveBit(int active) {
  for (int i = 0; i < num_dyn_weapons; i++) {
    if (dyn_weapons[i].bitmask == active && dyn_weapons[i].impulse > 0)
      return dyn_weapons[i].impulse;
  }
  return 0;
}

static vr_dyn_weapon_t *VR_AddOrUpdateDynWeapon(
    int bitmask, int impulse, const char *model_path, int model_index,
    qboolean discovered, float scale, const vec3_t offset, qboolean has_offset,
    int owned_stat, int owned_mask, int active_stat, int active_mask,
    int ammo_stat, int ammo_max, qboolean from_schema) {
  int index = VR_FindDynWeapon(bitmask, owned_stat, owned_mask, active_stat,
                               active_mask, model_path, model_index);
  vr_dyn_weapon_t *w;

  if (index >= 0) {
    w = &dyn_weapons[index];
  } else {
    if (num_dyn_weapons >= MAX_DYN_WEAPONS) {
      Con_Printf("VR: Too many weapon wheel entries (max %d)\n",
                 MAX_DYN_WEAPONS);
      return NULL;
    }
    w = &dyn_weapons[num_dyn_weapons++];
    VR_InitDynWeapon(w);
  }

  if (bitmask)
    w->bitmask = bitmask;
  if (impulse > 0)
    w->impulse = impulse;
  if (model_path && model_path[0])
    w->model_path = model_path;
  if (model_index > 0)
    w->model_index = model_index;
  if (discovered)
    w->discovered = true;
  if (scale > 0.0f)
    w->scale = scale;
  if (has_offset) {
    VectorCopy(offset, w->offset);
    w->has_offset = true;
  }
  if (owned_stat >= 0) {
    w->owned_stat = owned_stat;
    w->owned_mask = owned_mask;
  }
  if (active_stat >= 0) {
    w->active_stat = active_stat;
    w->active_mask = active_mask;
  }
  if (ammo_stat >= 0) {
    w->ammo_stat = ammo_stat;
    w->ammo_max = ammo_max;
  }
  if (!from_schema && !discovered && bitmask)
    w->use_item_ownership = true;
  if (from_schema)
    w->from_schema = true;

  return w;
}

static void VR_AddDwellWeaponDefaults(void) {
  vr_dyn_weapon_t *w;

  if (!VR_IsDwellGame() || dwell_weapons_added)
    return;

  /*
   * Dwell uses vanilla item bits for most weapons, but replaces several pickup
   * models/impulses and stores the three extra weapons in items_dwell, exposed
   * by the server as STAT_VR_MODITEMS.
   */
  VR_AddOrUpdateDynWeapon(IT_SHOTGUN, 2, "progs/g_shotgn.mdl", 0, false, 1.0f,
                          vec3_origin, false, -1, 0, -1, 0, STAT_SHELLS, 100,
                          false);
  VR_AddOrUpdateDynWeapon(IT_SUPER_SHOTGUN, 23, "progs/g_shot.mdl", 0, false,
                          1.0f, vec3_origin, false, -1, 0, -1, 0, STAT_SHELLS,
                          100, false);
  VR_AddOrUpdateDynWeapon(IT_NAILGUN, 4, "progs/g_nail.mdl", 0, false, 1.0f,
                          vec3_origin, false, -1, 0, -1, 0, STAT_NAILS, 200,
                          false);
  VR_AddOrUpdateDynWeapon(IT_SUPER_NAILGUN, 5, "progs/g_nail2.mdl", 0, false,
                          1.0f, vec3_origin, false, -1, 0, -1, 0, STAT_NAILS,
                          200, false);
  VR_AddOrUpdateDynWeapon(IT_GRENADE_LAUNCHER, 6, "progs/g_rock.mdl", 0,
                          false, 1.0f, vec3_origin, false, -1, 0, -1, 0,
                          STAT_ROCKETS, 100, false);
  VR_AddOrUpdateDynWeapon(IT_ROCKET_LAUNCHER, 7, "progs/g_rock2.mdl", 0, false,
                          1.0f, vec3_origin, false, -1, 0, -1, 0,
                          STAT_ROCKETS, 100, false);
  VR_AddOrUpdateDynWeapon(IT_LIGHTNING, 28, "progs/g_light.mdl", 0, false,
                          1.0f, vec3_origin, false, -1, 0, -1, 0, STAT_CELLS,
                          100, false);

  w = VR_AddOrUpdateDynWeapon(128, 33, "progs/g_shot3.mdl", 0, false, 1.0f,
                              vec3_origin, false, STAT_VR_MODITEMS, 4, -1, 0,
                              STAT_SHELLS, 100, false);
  if (w)
    dwell_weapon_indices[0] = (int)(w - dyn_weapons);
  w = VR_AddOrUpdateDynWeapon(256, 38, "progs/g_rail.mdl", 0, false, 1.0f,
                              vec3_origin, false, STAT_VR_MODITEMS, 8, -1, 0,
                              STAT_CELLS, 100, false);
  if (w)
    dwell_weapon_indices[1] = (int)(w - dyn_weapons);
  w = VR_AddOrUpdateDynWeapon(512, 2, "progs/g_rifle.mdl", 0, false, 1.0f,
                              vec3_origin, false, STAT_VR_MODITEMS, 32, -1, 0,
                              STAT_SHELLS, 100, false);
  if (w)
    dwell_weapon_indices[2] = (int)(w - dyn_weapons);

  dwell_weapons_added = true;
}

static void VR_AddBuiltinWeaponDefault(int bitmask, int impulse,
                                       const char *model_path,
                                       int owned_stat, int owned_mask,
                                       int active_stat, int active_mask,
                                       int ammo_stat, int ammo_max,
                                       float scale) {
  VR_AddOrUpdateDynWeapon(bitmask, impulse, model_path, 0, false, scale,
                          vec3_origin, false, owned_stat, owned_mask,
                          active_stat, active_mask, ammo_stat, ammo_max,
                          true);
}

static void VR_AddAlkalineWeaponDefaults(void) {
  if (!VR_GameDirIs("alk") && !VR_GameDirIs("limjam"))
    return;

  VR_AddBuiltinWeaponDefault(4096, 1, "progs/g_axe_alk.mdl", -1, 0, -1, 0,
                             -1, 0, 1.0f);
  VR_AddBuiltinWeaponDefault(1, 2, "progs/g_shotgn.mdl", -1, 0, -1, 0,
                             STAT_SHELLS, 100, 1.0f);
  VR_AddBuiltinWeaponDefault(2, 3, "progs/g_shot.mdl", -1, 0, -1, 0,
                             STAT_SHELLS, 100, 1.0f);
  VR_AddBuiltinWeaponDefault(4, 4, "progs/g_nail.mdl", -1, 0, -1, 0,
                             STAT_NAILS, 200, 1.0f);
  VR_AddBuiltinWeaponDefault(8, 5, "progs/g_nail2.mdl", -1, 0, -1, 0,
                             STAT_NAILS, 200, 1.0f);
  VR_AddBuiltinWeaponDefault(16, 6, "progs/g_rock.mdl", -1, 0, -1, 0,
                             STAT_ROCKETS, 100, 1.0f);
  VR_AddBuiltinWeaponDefault(32, 7, "progs/g_rock2.mdl", -1, 0, -1, 0,
                             STAT_ROCKETS, 100, 1.0f);
  VR_AddBuiltinWeaponDefault(64, 228, "progs/g_light.mdl", -1, 0, -1, 0,
                             STAT_CELLS, 100, 1.0f);
  VR_AddBuiltinWeaponDefault(256, 224, "progs/g_saw.mdl", -1, 0, -1, 0,
                             -1, 0, 1.0f);
  VR_AddBuiltinWeaponDefault(512, 227, "progs/g_plasma.mdl", -1, 0, -1, 0,
                             STAT_CELLS, 100, 1.0f);
  VR_AddBuiltinWeaponDefault(1024, 225, "progs/g_laserg.mdl", -1, 0, -1, 0,
                             STAT_CELLS, 100, 1.0f);
  VR_AddBuiltinWeaponDefault(8192, 226, "progs/g_mine.mdl", -1, 0, -1, 0,
                             STAT_ROCKETS, 100, 1.0f);
}

static void VR_AddEnyoWeaponDefaults(void) {
  if (!VR_GameDirIs("enyo"))
    return;

  VR_AddBuiltinWeaponDefault(4096, 1, "progs/ee_g_sword.mdl", -1, 0, -1, 0,
                             -1, 0, 1.0f);
  VR_AddBuiltinWeaponDefault(1, 2, "progs/ee_g_pistol.mdl", -1, 0, -1, 0,
                             STAT_NAILS, 200, 1.0f);
  VR_AddBuiltinWeaponDefault(2, 3, "progs/ee_g_sgun.mdl", -1, 0, -1, 0,
                             STAT_SHELLS, 100, 1.0f);
  VR_AddBuiltinWeaponDefault(4, 4, "progs/ee_g_smgs.mdl", -1, 0, -1, 0,
                             STAT_NAILS, 200, 1.0f);
  VR_AddBuiltinWeaponDefault(1024, 5, "progs/ee_g_av72.mdl", -1, 0, -1, 0,
                             STAT_NAILS, 200, 1.0f);
  VR_AddBuiltinWeaponDefault(8, 5, "progs/ee_g_plasma.mdl", -1, 0, -1, 0,
                             STAT_CELLS, 100, 1.0f);
  VR_AddBuiltinWeaponDefault(16, 6, "progs/ee_g_glaunch.mdl", -1, 0, -1, 0,
                             STAT_ROCKETS, 100, 1.0f);
  VR_AddBuiltinWeaponDefault(32, 7, "progs/ee_g_rlaunch.mdl", -1, 0, -1, 0,
                             STAT_ROCKETS, 100, 1.0f);
  VR_AddBuiltinWeaponDefault(64, 8, "progs/ee_g_railgun.mdl", -1, 0, -1, 0,
                             STAT_CELLS, 100, 1.0f);
}

static void VR_AddQBJ3WeaponDefaults(void) {
  if (!VR_GameDirIs("qbj3"))
    return;

  VR_AddBuiltinWeaponDefault(4096, 1, "progs/v_wrench.mdl", -1, 0, -1, 0,
                             -1, 0, 0.2f);
  VR_AddBuiltinWeaponDefault(1, 2, "progs/v_pistol.mdl", -1, 0, -1, 0,
                             STAT_SHELLS, 100, 0.2f);
  VR_AddBuiltinWeaponDefault(2, 3, "progs/v_flakshotgun.mdl", -1, 0, -1, 0,
                             STAT_SHELLS, 100, 0.2f);
  VR_AddBuiltinWeaponDefault(4, 4, "progs/v_tnailgun.mdl", -1, 0, -1, 0,
                             STAT_NAILS, 200, 0.2f);
  VR_AddBuiltinWeaponDefault(8, 5, "progs/v_rebar.mdl", -1, 0, -1, 0,
                             STAT_NAILS, 200, 0.2f);
  VR_AddBuiltinWeaponDefault(16, 6, "progs/v_grenlauncher.mdl", -1, 0, -1, 0,
                             STAT_ROCKETS, 100, 0.2f);
  VR_AddBuiltinWeaponDefault(32, 7, "progs/v_mmml.mdl", -1, 0, -1, 0,
                             STAT_ROCKETS, 100, 0.2f);
  VR_AddBuiltinWeaponDefault(64, 8, "progs/v_invoker.mdl", -1, 0, -1, 0,
                             STAT_CELLS, 100, 0.2f);
}

static void VR_AddMjolnirWeaponDefaults(void) {
  if (!VR_GameDirIs("mjolnir") && !VR_GameDirIs("mjolnir1.0"))
    return;

  VR_AddBuiltinWeaponDefault(128, 81, "progs/drake/g_light2.mdl", 42, 128,
                             STAT_ACTIVEWEAPON, 128, STAT_CELLS, 100, 1.0f);
  VR_AddBuiltinWeaponDefault(131072, 76, "progs/hipnotic/g_prox.mdl", 42,
                             131072, STAT_ACTIVEWEAPON, 131072, STAT_ROCKETS,
                             100, 1.0f);
  VR_AddBuiltinWeaponDefault(262144, 77, "progs/violentrumble/g_hammer.mdl",
                             42, 262144, STAT_ACTIVEWEAPON, 262144,
                             STAT_CELLS, 100, 1.0f);
  VR_AddBuiltinWeaponDefault(524288, 75, "progs/hipnotic/g_laserg.mdl", 42,
                             524288, STAT_ACTIVEWEAPON, 524288, STAT_CELLS,
                             100, 1.0f);
  VR_AddBuiltinWeaponDefault(1048576, 80, "progs/drake/g_grpple.mdl", 42,
                             1048576, STAT_ACTIVEWEAPON, 1048576, -1, 0,
                             1.0f);
  VR_AddBuiltinWeaponDefault(4194304, 79, "progs/drake/g_wand.mdl", 42,
                             4194304, STAT_ACTIVEWEAPON, 4194304, -1, 0,
                             1.0f);
}

static void VR_AddBuiltinWeaponDefaults(void) {
  VR_AddDwellWeaponDefaults();
  VR_AddAlkalineWeaponDefaults();
  VR_AddEnyoWeaponDefaults();
  VR_AddQBJ3WeaponDefaults();
  VR_AddMjolnirWeaponDefaults();
}

static qboolean VR_IsDwellDefaultWeaponEntry(const vr_dyn_weapon_t *w) {
  int index;

  if (!w)
    return false;

  index = (int)(w - dyn_weapons);
  for (int i = 0; i < (int)(sizeof(dwell_weapon_indices) /
                            sizeof(dwell_weapon_indices[0]));
       i++) {
    if (dwell_weapon_indices[i] == index)
      return true;
  }

  return false;
}

static qboolean VR_WeaponIsActive(const vr_dyn_weapon_t *w) {
  qboolean active;

  if (w->active_stat >= 0) {
    int value = cl.stats[w->active_stat];
    if (w->active_mask)
      active = (value & w->active_mask) != 0;
    else
      active = value != 0;
  } else {
    active = w->bitmask && w->bitmask == cl.stats[STAT_ACTIVEWEAPON];
  }

  if (!active)
    return false;
  if (w->model_index > 0 && cl.stats[STAT_WEAPON] > 0 &&
      w->model_index != cl.stats[STAT_WEAPON])
    return false;
  return true;
}

static qboolean VR_WeaponIsOwned(const vr_dyn_weapon_t *w) {
  qboolean schema_has_weapon_stat =
      w->from_schema && w->bitmask && cl.stats[STAT_VR_WEAPONS] != 0;

  if (w->owned_stat >= 0) {
    int value = cl.stats[w->owned_stat];
    if (w->owned_mask)
      return (value & w->owned_mask) != 0;
    return value != 0;
  }

  if (w->bitmask && (cl.stats[STAT_VR_WEAPONS] & w->bitmask))
    return true;

  if (schema_has_weapon_stat)
    return VR_WeaponIsActive(w);

  /*
   * Only stock/expansion weapon bits may use STAT_ITEMS ownership. Several
   * custom schemas reuse item bits that mean ammo or armor to vanilla Quake,
   * so treating those as item ownership exposes weapons the player lacks.
   */
  if (VR_DynWeaponCanUseItemOwnership(w) &&
      (VR_ClientItemBits() & w->bitmask))
    return true;

  return VR_WeaponIsActive(w);
}

static qboolean VR_GetWeaponAmmo(const vr_dyn_weapon_t *w, int *ammo,
                                 int *max_ammo) {
  int stat = w->ammo_stat;

  if (stat < 0) {
    switch (w->bitmask) {
    case IT_SHOTGUN:
    case IT_SUPER_SHOTGUN:
      stat = STAT_SHELLS;
      *max_ammo = 100;
      break;
    case IT_NAILGUN:
    case IT_SUPER_NAILGUN:
      stat = STAT_NAILS;
      *max_ammo = 200;
      break;
    case RIT_LAVA_NAILGUN:
    case RIT_LAVA_SUPER_NAILGUN:
      if (!rogue)
        return false;
      stat = STAT_NAILS;
      *max_ammo = 200;
      break;
    case IT_GRENADE_LAUNCHER:
    case IT_ROCKET_LAUNCHER:
    case RIT_MULTI_GRENADE:
    case RIT_MULTI_ROCKET:
      stat = STAT_ROCKETS;
      *max_ammo = 100;
      break;
    case IT_LIGHTNING:
    case HIT_LASER_CANNON:
      stat = STAT_CELLS;
      *max_ammo = 100;
      break;
    case 65536:
      stat = rogue ? STAT_CELLS : STAT_ROCKETS;
      *max_ammo = 100;
      break;
    default:
      return false;
    }
  } else {
    *max_ammo = w->ammo_max;
  }

  if (stat < 0 || stat >= MAX_CL_STATS)
    return false;

  *ammo = cl.stats[stat];
  return true;
}

static void VR_ResetDynWeaponsToBase(void) {
  num_dyn_weapons = 0;
  rogue_weapons_added = false;
  hipnotic_weapons_added = false;

  VR_AddOrUpdateDynWeapon(4096, 1, "progs/g_axe.mdl", 0, false, 1.0f,
                          vec3_origin, false, -1, 0, -1, 0, -1, 0, false);
  VR_AddOrUpdateDynWeapon(IT_SHOTGUN, 2, "progs/g_shot.mdl", 0, false, 1.0f,
                          vec3_origin, false, -1, 0, -1, 0, STAT_SHELLS, 100,
                          false);
  VR_AddOrUpdateDynWeapon(IT_SUPER_SHOTGUN, 3, "progs/g_shot2.mdl", 0, false,
                          1.0f, vec3_origin, false, -1, 0, -1, 0,
                          STAT_SHELLS, 100, false);
  VR_AddOrUpdateDynWeapon(IT_NAILGUN, 4, "progs/g_nail.mdl", 0, false, 1.0f,
                          vec3_origin, false, -1, 0, -1, 0, STAT_NAILS, 200,
                          false);
  VR_AddOrUpdateDynWeapon(IT_SUPER_NAILGUN, 5, "progs/g_nail2.mdl", 0, false,
                          1.0f, vec3_origin, false, -1, 0, -1, 0, STAT_NAILS,
                          200, false);
  VR_AddOrUpdateDynWeapon(IT_GRENADE_LAUNCHER, 6, "progs/g_rock.mdl", 0,
                          false, 1.0f, vec3_origin, false, -1, 0, -1, 0,
                          STAT_ROCKETS, 100, false);
  VR_AddOrUpdateDynWeapon(IT_ROCKET_LAUNCHER, 7, "progs/g_rock2.mdl", 0,
                          false, 1.0f, vec3_origin, false, -1, 0, -1, 0,
                          STAT_ROCKETS, 100, false);
  VR_AddOrUpdateDynWeapon(IT_LIGHTNING, 8, "progs/g_light.mdl", 0, false,
                          1.0f, vec3_origin, false, -1, 0, -1, 0, STAT_CELLS,
                          100, false);
}

// Unused variables, marking them explicitly or removing them later
static GLuint mirror_texture = 0;
static GLuint mirror_fbo = 0;
static int attempt_to_refocus_retry = 0;

static vec3_t headOrigin;
static vec3_t lastHeadOrigin;

vec3_t vr_room_scale_move;

// Wolfenstein 3D, DOOM and QUAKE use the same coordinate/unit system:
// 8 foot (96 inch) height wall == 64 units, 1.5 inches per pixel unit
// 1.0 pixel unit / 1.5 inch == 0.666666 pixel units per inch
#define meters_to_units (vr_world_scale.value / (1.5f * 0.0254f))

// moved to top extern "C" block

#define DEFINE_CVAR(name, defaultValue, type)                                  \
  cvar_t name = {#name, #defaultValue, type}

DEFINE_CVAR(vr_enabled, 0, CVAR_NONE);
DEFINE_CVAR(vr_viewkick, 0, CVAR_NONE);
DEFINE_CVAR(vr_lefthanded, 0, CVAR_NONE);

DEFINE_CVAR(vr_crosshair, 1, CVAR_ARCHIVE);
DEFINE_CVAR(vr_crosshair_depth, 0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_crosshair_size, 3.0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_crosshair_alpha, 0.25, CVAR_ARCHIVE);
DEFINE_CVAR(vr_aimmode, 7, CVAR_ARCHIVE);
DEFINE_CVAR(vr_deadzone, 30, CVAR_ARCHIVE);
DEFINE_CVAR(vr_gunangle, 32, CVAR_ARCHIVE);
DEFINE_CVAR(vr_gunmodeloffsets, 0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_gunmodelpitch, 0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_gunmodelscale, 1.0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_gunmodely, 0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_projectilespawn_z_offset, 24, CVAR_ARCHIVE);
DEFINE_CVAR(vr_crosshairy, 0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_world_scale, 1.0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_floor_offset, -16, CVAR_ARCHIVE);
DEFINE_CVAR(vr_snap_turn, 0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_180_snap_turn, 1, CVAR_ARCHIVE);
DEFINE_CVAR(vr_turn_speed, 2, CVAR_ARCHIVE);
DEFINE_CVAR(vr_haptic, 1, CVAR_ARCHIVE);
DEFINE_CVAR(vr_msaa, 4, CVAR_ARCHIVE);
DEFINE_CVAR(vr_highprecision_targets, 1, CVAR_ARCHIVE);
DEFINE_CVAR(vr_movement_mode, 0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_joystick_yaw_multi, 1.0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_joystick_axis_deadzone, 0.25, CVAR_ARCHIVE);
DEFINE_CVAR(vr_joystick_axis_menu_deadzone_extra, 0.25, CVAR_ARCHIVE);
DEFINE_CVAR(vr_joystick_axis_exponent, 1.0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_joystick_deadzone_trunc, 1, CVAR_ARCHIVE);
DEFINE_CVAR(vr_hud_scale, 0.025, CVAR_ARCHIVE);
DEFINE_CVAR(vr_menu_scale, 0.13, CVAR_ARCHIVE);
DEFINE_CVAR(vr_movement_instant_stop, 0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_movement_defaults_version, 0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_movement_speed, 1.0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_weaponmenu_player_teleport, 1, CVAR_ARCHIVE);

#define VR_MOVEMENT_DEFAULTS_VERSION 2

static qboolean VR_MovementDefaultMatches(float value, float old_default)
{
  const float epsilon = 0.0001f;

  return value > old_default - epsilon && value < old_default + epsilon;
}

void VR_MigrateMovementDefaults_f(void)
{
  int version = (int)vr_movement_defaults_version.value;

  if (version >= VR_MOVEMENT_DEFAULTS_VERSION)
    return;

  if (version < 1 && (int)vr_movement_instant_stop.value == 1)
    Cvar_SetQuick(&vr_movement_instant_stop, "0");

  if (version < 2) {
    if (VR_MovementDefaultMatches(cl_forwardspeed.value, 400.0f))
      Cvar_SetQuick(&cl_forwardspeed, "200");
    if (VR_MovementDefaultMatches(cl_backspeed.value, 400.0f))
      Cvar_SetQuick(&cl_backspeed, "200");
    if (VR_MovementDefaultMatches(vr_movement_speed.value, 1.5f))
      Cvar_SetQuick(&vr_movement_speed, "1.0");
  }

  Cvar_SetQuick(&vr_movement_defaults_version,
                va("%d", VR_MOVEMENT_DEFAULTS_VERSION));
}

static qboolean InitOpenGLExtensions() {
  int i;
  static qboolean extensions_initialized;

  if (extensions_initialized)
    return true;

  for (i = 0; gl_extensions[i].func; i++) {
    void *func = SDL_GL_GetProcAddress(gl_extensions[i].name);
    if (!func)
      return false;

    *((void **)gl_extensions[i].func) = func;
  }

  extensions_initialized = true;
  return extensions_initialized;
}

static qboolean VR_CreateFBOTextures(fbo_t *fbo, int width, int height,
                                     GLenum color_format, GLenum depth_format) {
  glGenTextures(1, &fbo->depth_texture);
  glGenTextures(1, &fbo->texture);

  glBindTexture(GL_TEXTURE_2D, fbo->depth_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, depth_format, width, height, 0,
               GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);

  glBindTexture(GL_TEXTURE_2D, fbo->texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, color_format, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo->framebuffer);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                            GL_TEXTURE_2D, fbo->texture, 0);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
                            GL_TEXTURE_2D, fbo->depth_texture, 0);

  return glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) ==
         GL_FRAMEBUFFER_COMPLETE;
}

void RecreateTextures(fbo_t *fbo, int width, int height) {
  GLuint oldDepth = fbo->depth_texture;
  GLuint oldTexture = fbo->texture;
  /*
   * SteamVR's OpenGL compositor path does not present RGB10_A2 gamma targets
   * consistently on all HMDs: the mirror blit is correct while the headset
   * receives an over-bright image. Keep the submitted eye colour format at
   * the established RGBA8 format; the optional depth32 attachment still
   * improves depth precision without changing colour-space semantics.
   */
  GLenum color_format = GL_RGBA8;
  GLenum depth_format = vr_highprecision_targets.value ? GL_DEPTH_COMPONENT32
                                                        : GL_DEPTH_COMPONENT24;

  fbo->depth_texture = 0;
  fbo->texture = 0;

  if (oldDepth) {
    glDeleteTextures(1, &oldDepth);
    glDeleteTextures(1, &oldTexture);
  }

  if (!VR_CreateFBOTextures(fbo, width, height, color_format, depth_format)) {
    if (color_format != GL_RGBA8 || depth_format != GL_DEPTH_COMPONENT24) {
      Con_Warning("VR high-precision framebuffer unsupported; using RGBA8/depth24\n");
      glDeleteTextures(1, &fbo->depth_texture);
      glDeleteTextures(1, &fbo->texture);
      fbo->depth_texture = 0;
      fbo->texture = 0;
      color_format = GL_RGBA8;
      depth_format = GL_DEPTH_COMPONENT24;
      if (!VR_CreateFBOTextures(fbo, width, height, color_format,
                                depth_format))
        Sys_Error("Unable to create VR framebuffer");
    } else {
      Sys_Error("Unable to create VR framebuffer");
    }
  }

  fbo->size.width = width;
  fbo->size.height = height;
  fbo->color_format = color_format;
  fbo->depth_format = depth_format;
  fbo->highprecision_request = !!vr_highprecision_targets.value;
}

fbo_t CreateFBO(int width, int height) {
  fbo_t fbo;

  memset(&fbo, 0, sizeof(fbo));

  glGenFramebuffersEXT(1, &fbo.framebuffer);

  RecreateTextures(&fbo, width, height);

  return fbo;
}

void CreateMSAA(fbo_t *fbo, int width, int height, int msaa) {
  if (fbo->msaa_framebuffer) {
    glDeleteFramebuffersEXT(1, &fbo->msaa_framebuffer);
    glDeleteTextures(1, &fbo->msaa_texture);
    glDeleteTextures(1, &fbo->msaa_depth_texture);
    fbo->msaa_framebuffer = 0;
    fbo->msaa_texture = 0;
    fbo->msaa_depth_texture = 0;
  }

  fbo->msaa = msaa;
  if (msaa <= 0)
    return;

  glGenFramebuffersEXT(1, &fbo->msaa_framebuffer);
  glGenTextures(1, &fbo->msaa_texture);
  glGenTextures(1, &fbo->msaa_depth_texture);

  glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, fbo->msaa_texture);
  glTexImage2DMultisampleEXT(GL_TEXTURE_2D_MULTISAMPLE, msaa,
                             fbo->color_format, width, height, false);

  glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, fbo->msaa_depth_texture);
  glTexImage2DMultisampleEXT(GL_TEXTURE_2D_MULTISAMPLE, msaa,
                             fbo->depth_format, width, height, false);

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo->msaa_framebuffer);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                            GL_TEXTURE_2D_MULTISAMPLE, fbo->msaa_texture, 0);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
                            GL_TEXTURE_2D_MULTISAMPLE, fbo->msaa_depth_texture,
                            0);

  GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    Con_Warning("VR MSAA framebuffer incomplete %x; disabling MSAA\n", status);
    glDeleteFramebuffersEXT(1, &fbo->msaa_framebuffer);
    glDeleteTextures(1, &fbo->msaa_texture);
    glDeleteTextures(1, &fbo->msaa_depth_texture);
    fbo->msaa_framebuffer = 0;
    fbo->msaa_texture = 0;
    fbo->msaa_depth_texture = 0;
    fbo->msaa = 0;
  }
}

void DeleteFBO(fbo_t fbo) {
  glDeleteFramebuffersEXT(1, &fbo.framebuffer);
  glDeleteTextures(1, &fbo.depth_texture);
  glDeleteTextures(1, &fbo.texture);
}

void QuatToYawPitchRoll(vr::HmdQuaternion_t q, vec3_t out) {
  auto sqw = q.w * q.w;
  auto sqx = q.x * q.x;
  auto sqy = q.y * q.y;
  auto sqz = q.z * q.z;

  out[ROLL] =
      -atan2(2 * (q.x * q.y + q.w * q.z), sqw - sqx + sqy - sqz) / M_PI_DIV_180;
  out[PITCH] = -asin(-2 * (q.y * q.z - q.w * q.x)) / M_PI_DIV_180;
  out[YAW] =
      atan2(2 * (q.x * q.z + q.w * q.y), sqw - sqx - sqy + sqz) / M_PI_DIV_180 +
      vrYaw;
}

void Vec3RotateZ(vec3_t in, float angle, vec3_t out) {
  out[0] = in[0] * cos(angle) - in[1] * sin(angle);
  out[1] = in[0] * sin(angle) + in[1] * cos(angle);
  out[2] = in[2];
}

vr::HmdMatrix44_t TransposeMatrix(vr::HmdMatrix44_t in) {
  vr::HmdMatrix44_t out;
  int y, x;
  for (y = 0; y < 4; y++)
    for (x = 0; x < 4; x++)
      out.m[x][y] = in.m[y][x];

  return out;
}

vr::HmdVector3_t AddVectors(vr::HmdVector3_t a, vr::HmdVector3_t b) {
  vr::HmdVector3_t out;

  out.v[0] = a.v[0] + b.v[0];
  out.v[1] = a.v[1] + b.v[1];
  out.v[2] = a.v[2] + b.v[2];

  return out;
}

// Rotates a vector by a quaternion and returns the results
// Based on math from
// https://gamedev.stackexchange.com/questions/28395/rotating-vector3-by-a-quaternion
vr::HmdVector3_t RotateVectorByQuaternion(vr::HmdVector3_t v,
                                          vr::HmdQuaternion_t q) {
  vr::HmdVector3_t u, result;
  u.v[0] = q.x;
  u.v[1] = q.y;
  u.v[2] = q.z;
  float s = q.w;

  // Dot products of u,v and u,u
  float uvDot = (u.v[0] * v.v[0] + u.v[1] * v.v[1] + u.v[2] * v.v[2]);
  float uuDot = (u.v[0] * u.v[0] + u.v[1] * u.v[1] + u.v[2] * u.v[2]);

  // Calculate cross product of u, v
  vr::HmdVector3_t uvCross;
  uvCross.v[0] = u.v[1] * v.v[2] - u.v[2] * v.v[1];
  uvCross.v[1] = u.v[2] * v.v[0] - u.v[0] * v.v[2];
  uvCross.v[2] = u.v[0] * v.v[1] - u.v[1] * v.v[0];

  // Calculate each vectors' result individually because there aren't arthimetic
  // functions for HmdVector3_t dsahfkldhsaklfhklsadh
  result.v[0] = u.v[0] * 2.0f * uvDot + (s * s - uuDot) * v.v[0] +
                2.0f * s * uvCross.v[0];
  result.v[1] = u.v[1] * 2.0f * uvDot + (s * s - uuDot) * v.v[1] +
                2.0f * s * uvCross.v[1];
  result.v[2] = u.v[2] * 2.0f * uvDot + (s * s - uuDot) * v.v[2] +
                2.0f * s * uvCross.v[2];

  return result;
}

// Transforms a HMD Matrix34 to a Vector3
// Math borrowed from https://github.com/Omnifinity/OpenVR-Tracking-Example
vr::HmdVector3_t Matrix34ToVector(vr::HmdMatrix34_t in) {
  vr::HmdVector3_t vector;

  vector.v[0] = in.m[0][3];
  vector.v[1] = in.m[1][3];
  vector.v[2] = in.m[2][3];

  return vector;
}

// Transforms a HMD Matrix34 to a Quaternion
// Function logic nicked from
// https://github.com/Omnifinity/OpenVR-Tracking-Example
vr::HmdQuaternion_t Matrix34ToQuaternion(vr::HmdMatrix34_t in) {
  vr::HmdQuaternion_t q;

  q.w = sqrt(fmax(0, 1.0 + in.m[0][0] + in.m[1][1] + in.m[2][2])) / 2.0;
  q.x = sqrt(fmax(0, 1.0 + in.m[0][0] - in.m[1][1] - in.m[2][2])) / 2.0;
  q.y = sqrt(fmax(0, 1.0 - in.m[0][0] + in.m[1][1] - in.m[2][2])) / 2.0;
  q.z = sqrt(fmax(0, 1.0 - in.m[0][0] - in.m[1][1] + in.m[2][2])) / 2.0;
  q.x = copysign(q.x, static_cast<double>(in.m[2][1]) -
                          static_cast<double>(in.m[1][2]));
  q.y = copysign(q.y, static_cast<double>(in.m[0][2]) -
                          static_cast<double>(in.m[2][0]));
  q.z = copysign(q.z, static_cast<double>(in.m[1][0]) -
                          static_cast<double>(in.m[0][1]));
  return q;
}

void HmdVec3RotateY(vr::HmdVector3_t *pos, float angle) {
  float s = sin(angle);
  float c = cos(angle);
  float x = c * pos->v[0] - s * pos->v[2];
  float y = s * pos->v[0] + c * pos->v[2];

  pos->v[0] = x;
  pos->v[2] = y;
}

// ----------------------------------------------------------------------------
// Callbacks for cvars

static void VR_Enabled_f(cvar_t *var) {
  VID_VR_Disable();

  if (!vr_enabled.value)
    return;

  if (!VR_Enable())
    Cvar_SetValueQuick(&vr_enabled, 0);
}

static void VR_Gunmodeloffsets_f(cvar_t *var) { InitAllWeaponCVars(); }

static void VR_Deadzone_f(cvar_t *var) {
  // clamp the mouse to a max of 0 - 70 degrees
  float deadzone = CLAMP(0.0f, vr_deadzone.value, 70.0f);
  if (deadzone != vr_deadzone.value)
    Cvar_SetValueQuick(&vr_deadzone, deadzone);
}

// Weapon scale/position stuff
cvar_t vr_weapon_offset[MAX_WEAPONS * VARS_PER_WEAPON];
cvar_t vr_weapon_muzzle_offset[MAX_WEAPONS * VARS_PER_WEAPON_MUZZLE];
static vec3_t vr_weapon_mp_held_offset[MAX_WEAPONS];
static qboolean vr_weapon_has_mp_held_offset[MAX_WEAPONS];
static vec3_t vr_weapon_schema_mp_held_offset[MAX_WEAPONS];
static qboolean vr_weapon_has_schema_mp_held_offset[MAX_WEAPONS];
static vec3_t vr_weapon_mp_muzzle_offset[MAX_WEAPONS];
static qboolean vr_weapon_has_mp_muzzle_offset[MAX_WEAPONS];
static vec3_t vr_weapon_schema_mp_muzzle_offset[MAX_WEAPONS];
static qboolean vr_weapon_has_schema_mp_muzzle_offset[MAX_WEAPONS];
static vec3_t vr_weapon_muzzle_source_offset[MAX_WEAPONS];
static qboolean vr_weapon_has_muzzle_source_offset[MAX_WEAPONS];
static qboolean vr_weapon_muzzle_source_viewofs[MAX_WEAPONS];
static qboolean vr_weapon_has_muzzle_source_viewofs[MAX_WEAPONS];
static qboolean vr_weapon_spawn_at_self_origin[MAX_WEAPONS];
static qboolean vr_weapon_has_spawn_at_self_origin[MAX_WEAPONS];

aliashdr_t *lastWeaponHeader = NULL;
static qmodel_t *lastWeaponModel = NULL;
int weaponCVarEntry = -1;

static qboolean VR_IsMultiplayerClient(void) { return cl.maxclients > 1; }

/*
 * The rerelease replacement meshes retain the classic model path (for
 * example, progs/v_shot.mdl) but expose a distinct MD5 alias header.  The
 * named VR offset profiles were calibrated for the classic/OSJC MDL geometry,
 * so applying them to that MD5 header can move or shrink a held weapon out of
 * the VR view.  Use the alias data rather than the filename to select the
 * profile, because the pathname intentionally remains the classic one.
 */
static qboolean VR_ViewmodelUsesNeutralProfile(const aliashdr_t *hdr) {
  return hdr && hdr->poseverttype == ALIAS_POSE_MD5;
}

static void VR_ApplyNeutralViewmodelTransform(aliashdr_t *hdr) {
  float scaleCorrect =
      (vr_world_scale.value / 0.75f) * vr_gunmodelscale.value;

  /* Preserve global user tuning, but do not apply an MDL-pack-specific slot. */
  VectorScale(hdr->original_scale, scaleCorrect, hdr->scale);
  VectorCopy(hdr->original_scale_origin, hdr->scale_origin);
  hdr->scale_origin[2] += vr_gunmodely.value;
  VectorScale(hdr->scale_origin, scaleCorrect, hdr->scale_origin);
}

void Mod_Weapon(qmodel_t *model, aliashdr_t *hdr) {
  const char *name = model ? model->name : "";

  if (!model || !hdr)
    return;

  if (lastWeaponHeader != hdr || lastWeaponModel != model) {
    lastWeaponHeader = hdr;
    lastWeaponModel = model;
    weaponCVarEntry = -1;
    if (!VR_ViewmodelUsesNeutralProfile(hdr)) {
      for (int i = 0; i < MAX_WEAPONS; i++) {
        if (!strcmp(vr_weapon_offset[i * VARS_PER_WEAPON + 4].string, name)) {
          weaponCVarEntry = i;
          break;
        }
      }
      if (weaponCVarEntry == -1) {
        DebugLog("VR: no weapon offset for %s\n", name);
      }
    }
  }

  if (VR_ViewmodelUsesNeutralProfile(hdr)) {
    VR_ApplyNeutralViewmodelTransform(hdr);
    return;
  }

  if (weaponCVarEntry != -1) {
    float scaleCorrect =
        (vr_world_scale.value / 0.75f) *
        vr_gunmodelscale.value; // initial version had 0.75 default world scale,
                                // so weapons reflect that
    VectorScale(hdr->original_scale,
                vr_weapon_offset[weaponCVarEntry * VARS_PER_WEAPON + 3].value *
                    scaleCorrect,
                hdr->scale);

    vec3_t ofs = {
        vr_weapon_offset[weaponCVarEntry * VARS_PER_WEAPON].value,
        vr_weapon_offset[weaponCVarEntry * VARS_PER_WEAPON + 1].value,
        vr_weapon_offset[weaponCVarEntry * VARS_PER_WEAPON + 2].value};

    if (VR_IsMultiplayerClient() &&
        vr_weapon_has_mp_held_offset[weaponCVarEntry])
      VectorAdd(ofs, vr_weapon_mp_held_offset[weaponCVarEntry], ofs);

    ofs[2] += vr_gunmodely.value;

    VectorAdd(hdr->original_scale_origin, ofs, hdr->scale_origin);
    VectorScale(hdr->scale_origin, scaleCorrect, hdr->scale_origin);
  }
}

static aliashdr_t *VR_ActiveAliasHeader(qmodel_t *model, int skinnum,
                                        int frame) {
  if (Mod_UseMD3ModelForFrame(model, skinnum, frame)) {
    aliashdr_t *md3 = Mod_GetMD3Extradata(model);
    if (md3)
      return md3;
  }
  if (Mod_UseMD5ModelForFrame(model, skinnum, frame)) {
    aliashdr_t *md5 = Mod_GetMD5Extradata(model);
    if (md5)
      return md5;
  }
  return (aliashdr_t *)Mod_Extradata(model);
}

void VR_ApplyCurrentViewWeaponTransform(void) {
  aliashdr_t *hdr;

  if (!vr_enabled.value || !cl.viewent.model ||
      cl.viewent.model->type != mod_alias)
    return;

  hdr = VR_ActiveAliasHeader(cl.viewent.model, cl.viewent.skinnum,
                             cl.viewent.frame);
  Mod_Weapon(cl.viewent.model, hdr);
}

char *CopyWithNumeral(const char *str, int i) {
  int len = strlen(str);
  char *ret = (char *)malloc(len + 1);
  strcpy(ret, str);
  ret[len - 1] = '0' + (i % 10);
  ret[len - 2] = '0' + (i / 10);
  return ret;
}

void InitWeaponCVar(cvar_t *cvar, const char *name, int i, const char *value) {
  const char *cvarname = CopyWithNumeral(name, i + 1);
  if (!Cvar_FindVar(cvarname)) {
    cvar->name = cvarname;
    cvar->string = value;
    cvar->flags = CVAR_NONE;
    Cvar_RegisterVariable(cvar);
  } else {
    Cvar_SetQuick(cvar, value);
  }
}

static void InitWeaponMuzzleCVars(int i, const char *offsetX,
                                  const char *offsetY,
                                  const char *offsetZ) {
  const char *nameOffsetX = "vr_wmuzzle_x_nn";
  const char *nameOffsetY = "vr_wmuzzle_y_nn";
  const char *nameOffsetZ = "vr_wmuzzle_z_nn";
  InitWeaponCVar(&vr_weapon_muzzle_offset[i * VARS_PER_WEAPON_MUZZLE],
                 nameOffsetX, i, offsetX);
  InitWeaponCVar(&vr_weapon_muzzle_offset[i * VARS_PER_WEAPON_MUZZLE + 1],
                 nameOffsetY, i, offsetY);
  InitWeaponCVar(&vr_weapon_muzzle_offset[i * VARS_PER_WEAPON_MUZZLE + 2],
                 nameOffsetZ, i, offsetZ);
}

void InitWeaponCVars(int i, const char *id, const char *offsetX,
                     const char *offsetY, const char *offsetZ,
                     const char *scale) {
  const char *nameOffsetX = "vr_wofs_x_nn";
  const char *nameOffsetY = "vr_wofs_y_nn";
  const char *nameOffsetZ = "vr_wofs_z_nn";
  const char *nameScale = "vr_wofs_scale_nn";
  const char *nameID = "vr_wofs_id_nn";
  InitWeaponCVar(&vr_weapon_offset[i * VARS_PER_WEAPON], nameOffsetX, i,
                 offsetX);
  InitWeaponCVar(&vr_weapon_offset[i * VARS_PER_WEAPON + 1], nameOffsetY, i,
                 offsetY);
  InitWeaponCVar(&vr_weapon_offset[i * VARS_PER_WEAPON + 2], nameOffsetZ, i,
                 offsetZ);
  InitWeaponCVar(&vr_weapon_offset[i * VARS_PER_WEAPON + 3], nameScale, i,
                 scale);
  InitWeaponCVar(&vr_weapon_offset[i * VARS_PER_WEAPON + 4], nameID, i, id);
  InitWeaponMuzzleCVars(i, "0", "0", offsetZ);
  vr_weapon_mp_held_offset[i][0] = 0.0f;
  vr_weapon_mp_held_offset[i][1] = 0.0f;
  vr_weapon_mp_held_offset[i][2] = 0.0f;
  vr_weapon_has_mp_held_offset[i] = false;
  vr_weapon_schema_mp_held_offset[i][0] = 0.0f;
  vr_weapon_schema_mp_held_offset[i][1] = 0.0f;
  vr_weapon_schema_mp_held_offset[i][2] = 0.0f;
  vr_weapon_has_schema_mp_held_offset[i] = false;
  vr_weapon_mp_muzzle_offset[i][0] = 0.0f;
  vr_weapon_mp_muzzle_offset[i][1] = 0.0f;
  vr_weapon_mp_muzzle_offset[i][2] = 0.0f;
  vr_weapon_has_mp_muzzle_offset[i] = false;
  vr_weapon_schema_mp_muzzle_offset[i][0] = 0.0f;
  vr_weapon_schema_mp_muzzle_offset[i][1] = 0.0f;
  vr_weapon_schema_mp_muzzle_offset[i][2] = 0.0f;
  vr_weapon_has_schema_mp_muzzle_offset[i] = false;
  vr_weapon_muzzle_source_offset[i][0] = 0.0f;
  vr_weapon_muzzle_source_offset[i][1] = 0.0f;
  vr_weapon_muzzle_source_offset[i][2] = 0.0f;
  vr_weapon_has_muzzle_source_offset[i] = false;
  vr_weapon_muzzle_source_viewofs[i] = false;
  vr_weapon_has_muzzle_source_viewofs[i] = false;
  vr_weapon_spawn_at_self_origin[i] = false;
  vr_weapon_has_spawn_at_self_origin[i] = false;
}

static int VR_FindWeaponOffsetSlot(const char *id, int *free_slot) {
  int slot = -1;

  if (free_slot)
    *free_slot = -1;

  if (!id || !id[0])
    return -1;

  for (int i = 0; i < MAX_WEAPONS; i++) {
    const char *slot_id = vr_weapon_offset[i * VARS_PER_WEAPON + 4].string;

    if (slot_id && !Q_strcmp(slot_id, id)) {
      slot = i;
      break;
    }

    if (free_slot && *free_slot < 0 &&
        (!slot_id || !slot_id[0] || !Q_strcmp(slot_id, "-1")))
      *free_slot = i;
  }

  return slot;
}

static qboolean VR_WeaponOffsetSlotHasValidID(int slot) {
  const char *id;

  if (slot < 0 || slot >= MAX_WEAPONS)
    return false;

  id = vr_weapon_offset[slot * VARS_PER_WEAPON + 4].string;
  return id && id[0] && Q_strcmp(id, "-1");
}

static int VR_CreateHeldWeaponOffsetSlot(const char *id, const char *reason) {
  int slot;
  int free_slot = -1;

  if (!id || !id[0])
    return -1;

  slot = VR_FindWeaponOffsetSlot(id, &free_slot);
  if (slot >= 0)
    return slot;

  if (free_slot < 0) {
    Con_Printf("VR: No free held weapon offset slot for %s\n", id);
    return -1;
  }

  /*
   * Unknown mod viewmodels should become adjustable without changing their
   * current pose first. The adjustment save path will persist the real offset.
   */
  InitWeaponCVars(free_slot, id, "0", "0", "0", "1");
  weaponCVarEntry = free_slot;
  lastWeaponHeader = NULL;

  if (reason && reason[0])
    Con_Printf("%s: created held weapon offset slot for %s\n", reason, id);
  else
    Con_Printf("VR: created held weapon offset slot for %s\n", id);

  return free_slot;
}

static void VR_RegisterHeldWeaponOffset(const char *id, const vec3_t offset,
                                        float scale) {
  int slot = -1;
  int free_slot = -1;
  char offsetX[32], offsetY[32], offsetZ[32], scaleValue[32];

  if (!id || !id[0])
    return;

  if (scale <= 0.0f) {
    Con_Printf("VR: Ignoring held weapon offset for %s with scale %g\n", id,
               scale);
    return;
  }

  slot = VR_FindWeaponOffsetSlot(id, &free_slot);
  if (slot < 0)
    slot = free_slot;

  if (slot < 0) {
    Con_Printf("VR: No free held weapon offset slot for %s\n", id);
    return;
  }

  q_snprintf(offsetX, sizeof(offsetX), "%.7g", offset[0]);
  q_snprintf(offsetY, sizeof(offsetY), "%.7g", offset[1]);
  q_snprintf(offsetZ, sizeof(offsetZ), "%.7g", offset[2]);
  q_snprintf(scaleValue, sizeof(scaleValue), "%.7g", scale);
  InitWeaponCVars(slot, id, offsetX, offsetY, offsetZ, scaleValue);
}

static void VR_RegisterHeldWeaponOffsetIfMissing(const char *id,
                                                 const vec3_t offset,
                                                 float scale) {
  if (!id || !id[0])
    return;
  if (VR_FindWeaponOffsetSlot(id, NULL) >= 0)
    return;
  VR_RegisterHeldWeaponOffset(id, offset, scale);
}

static void VR_RegisterHeldWeaponOffsetIfMissing3(const char *id, float x,
                                                  float y, float z,
                                                  float scale) {
  vec3_t offset = {x, y, z};

  VR_RegisterHeldWeaponOffsetIfMissing(id, offset, scale);
}

static void VR_RegisterDwellHeldWeaponDefaults(void) {
  if (!VR_IsDwellGame())
    return;

  VR_RegisterHeldWeaponOffsetIfMissing3("progs/v_axe2.mdl", -3.5f, 34.0f,
                                        41.5f, 0.4f);
  VR_RegisterHeldWeaponOffsetIfMissing3("progs/v_axeb.mdl", -4.0f, 24.0f,
                                        37.0f, 0.4f);
  VR_RegisterHeldWeaponOffsetIfMissing3("progs/v_shot.mdl", 1.5f, 1.0f,
                                        10.0f, 0.3333333f);
  VR_RegisterHeldWeaponOffsetIfMissing3("progs/v_shot2.mdl", -3.5f, 1.0f,
                                        8.5f, 0.5333333f);
  VR_RegisterHeldWeaponOffsetIfMissing3("progs/v_shot3.mdl", -3.5f, 0.4f,
                                        8.5f, 0.5333333f);
  VR_RegisterHeldWeaponOffsetIfMissing3("progs/v_nail.mdl", -5.0f, 3.0f,
                                        15.0f, 0.5f);
  VR_RegisterHeldWeaponOffsetIfMissing3("progs/v_nail2.mdl", 0.0f, 3.0f,
                                        19.0f, 0.5f);
  VR_RegisterHeldWeaponOffsetIfMissing3("progs/v_nail3.mdl", -4.0f, 3.5f,
                                        19.0f, 0.35f);
  VR_RegisterHeldWeaponOffsetIfMissing3("progs/v_rock.mdl", 10.0f, 1.5f,
                                        13.0f, 0.5f);
  VR_RegisterHeldWeaponOffsetIfMissing3("progs/v_rock2.mdl", 10.0f, 7.0f,
                                        19.0f, 0.5f);
  VR_RegisterHeldWeaponOffsetIfMissing3("progs/v_light.mdl", 3.0f, 4.0f,
                                        13.0f, 0.5f);
  VR_RegisterHeldWeaponOffsetIfMissing3("progs/v_rail.mdl", 4.0f, 5.0f,
                                        31.0f, 0.65f);
  VR_RegisterHeldWeaponOffsetIfMissing3("progs/v_rifle.mdl", 1.5f, 1.0f,
                                        10.0f, 0.5f);
}

static void VR_RegisterWeaponMuzzleOffset(const char *id,
                                          const vec3_t offset) {
  int slot;
  int free_slot = -1;
  char offsetX[32], offsetY[32], offsetZ[32];

  if (!id || !id[0])
    return;

  slot = VR_FindWeaponOffsetSlot(id, &free_slot);
  if (slot < 0) {
    slot = free_slot;
    if (slot >= 0)
      InitWeaponCVars(slot, id, "0", "0", "0", "1");
  }

  if (slot < 0) {
    Con_Printf("VR: No free muzzle offset slot for %s\n", id);
    return;
  }

  q_snprintf(offsetX, sizeof(offsetX), "%.7g", offset[0]);
  q_snprintf(offsetY, sizeof(offsetY), "%.7g", offset[1]);
  q_snprintf(offsetZ, sizeof(offsetZ), "%.7g", offset[2]);
  InitWeaponMuzzleCVars(slot, offsetX, offsetY, offsetZ);
}

static void VR_RegisterWeaponMPHeldOffset(const char *id, const vec3_t offset,
                                          qboolean has_schema_offset,
                                          const vec3_t schema_offset) {
  int slot;
  int free_slot = -1;

  if (!id || !id[0])
    return;

  slot = VR_FindWeaponOffsetSlot(id, &free_slot);
  if (slot < 0) {
    slot = free_slot;
    if (slot >= 0)
      InitWeaponCVars(slot, id, "0", "0", "0", "1");
  }

  if (slot < 0) {
    Con_Printf("VR: No free multiplayer held offset slot for %s\n", id);
    return;
  }

  VectorCopy(offset, vr_weapon_mp_held_offset[slot]);
  vr_weapon_has_mp_held_offset[slot] = true;

  if (has_schema_offset) {
    VectorCopy(schema_offset, vr_weapon_schema_mp_held_offset[slot]);
    vr_weapon_has_schema_mp_held_offset[slot] = true;
  } else {
    vr_weapon_schema_mp_held_offset[slot][0] = 0.0f;
    vr_weapon_schema_mp_held_offset[slot][1] = 0.0f;
    vr_weapon_schema_mp_held_offset[slot][2] = 0.0f;
    vr_weapon_has_schema_mp_held_offset[slot] = false;
  }
}

static void VR_RegisterWeaponMPMuzzleOffset(const char *id,
                                            const vec3_t offset,
                                            qboolean has_schema_offset,
                                            const vec3_t schema_offset) {
  int slot;
  int free_slot = -1;

  if (!id || !id[0])
    return;

  slot = VR_FindWeaponOffsetSlot(id, &free_slot);
  if (slot < 0) {
    slot = free_slot;
    if (slot >= 0)
      InitWeaponCVars(slot, id, "0", "0", "0", "1");
  }

  if (slot < 0) {
    Con_Printf("VR: No free multiplayer muzzle offset slot for %s\n", id);
    return;
  }

  VectorCopy(offset, vr_weapon_mp_muzzle_offset[slot]);
  vr_weapon_has_mp_muzzle_offset[slot] = true;

  if (has_schema_offset) {
    VectorCopy(schema_offset, vr_weapon_schema_mp_muzzle_offset[slot]);
    vr_weapon_has_schema_mp_muzzle_offset[slot] = true;
  } else {
    vr_weapon_schema_mp_muzzle_offset[slot][0] = 0.0f;
    vr_weapon_schema_mp_muzzle_offset[slot][1] = 0.0f;
    vr_weapon_schema_mp_muzzle_offset[slot][2] = 0.0f;
    vr_weapon_has_schema_mp_muzzle_offset[slot] = false;
  }
}

static void VR_RegisterWeaponMuzzleSource(const char *id,
                                          const vec3_t offset,
                                          qboolean has_offset,
                                          qboolean viewofs,
                                          qboolean has_viewofs) {
  int slot;
  int free_slot = -1;

  if (!id || !id[0] || (!has_offset && !has_viewofs))
    return;

  slot = VR_FindWeaponOffsetSlot(id, &free_slot);
  if (slot < 0) {
    slot = free_slot;
    if (slot >= 0)
      InitWeaponCVars(slot, id, "0", "0", "0", "1");
  }

  if (slot < 0) {
    Con_Printf("VR: No free muzzle source slot for %s\n", id);
    return;
  }

  if (has_offset) {
    VectorCopy(offset, vr_weapon_muzzle_source_offset[slot]);
    vr_weapon_has_muzzle_source_offset[slot] = true;
  }

  if (has_viewofs) {
    vr_weapon_muzzle_source_viewofs[slot] = viewofs;
    vr_weapon_has_muzzle_source_viewofs[slot] = true;
  }
}

static void VR_RegisterWeaponSpawnStyle(const char *id,
                                        qboolean spawn_at_self_origin) {
  int slot;
  int free_slot = -1;

  if (!id || !id[0])
    return;

  slot = VR_FindWeaponOffsetSlot(id, &free_slot);
  if (slot < 0) {
    slot = free_slot;
    if (slot >= 0)
      InitWeaponCVars(slot, id, "0", "0", "0", "1");
  }

  if (slot < 0) {
    Con_Printf("VR: No free projectile spawn style slot for %s\n", id);
    return;
  }

  vr_weapon_spawn_at_self_origin[slot] = spawn_at_self_origin;
  vr_weapon_has_spawn_at_self_origin[slot] = true;
}

qboolean VR_WeaponSpawnsAtSelfOrigin(const char *viewmodel, int weapon_bit) {
  int slot;

  if (viewmodel && viewmodel[0]) {
    slot = VR_FindWeaponOffsetSlot(viewmodel, NULL);
    if (slot >= 0 && slot < MAX_WEAPONS &&
        vr_weapon_has_spawn_at_self_origin[slot])
      return vr_weapon_spawn_at_self_origin[slot];
  }

  return weapon_bit == IT_GRENADE_LAUNCHER;
}

void VR_GetWeaponProjectileSourceOffset(const char *viewmodel, int weapon_bit,
                                        const vec3_t angles, float viewheight,
                                        vec3_t out) {
  int slot = -1;
  qboolean spawn_at_self_origin;

  out[0] = out[1] = out[2] = 0.0f;

  spawn_at_self_origin = VR_WeaponSpawnsAtSelfOrigin(viewmodel, weapon_bit);
  if (!spawn_at_self_origin) {
    vec3_t forward, right, up;
    vec3_t mutable_angles;

    VectorCopy(angles, mutable_angles);
    AngleVectors(mutable_angles, forward, right, up);
    VectorMA(out, 8.0f, forward, out);
    out[2] += 16.0f;
  }

  if (viewmodel && viewmodel[0])
    slot = VR_FindWeaponOffsetSlot(viewmodel, NULL);
  if (slot < 0 || slot >= MAX_WEAPONS)
    return;

  if (vr_weapon_has_muzzle_source_viewofs[slot] &&
      vr_weapon_muzzle_source_viewofs[slot])
    out[2] += viewheight;

  if (vr_weapon_has_muzzle_source_offset[slot]) {
    vec3_t source_world;

    VR_AimOffsetToWorld(vr_weapon_muzzle_source_offset[slot], angles, 1.0f,
                        source_world);
    VectorAdd(out, source_world, out);
  }
}

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} vr_textbuf_t;

typedef enum {
  VR_ADJUST_NONE,
  VR_ADJUST_WEAPON,
  VR_ADJUST_MP_WEAPON,
  VR_ADJUST_MUZZLE,
  VR_ADJUST_MP_MUZZLE
} vr_adjust_mode_t;

static vr_adjust_mode_t vr_adjust_mode = VR_ADJUST_NONE;
static vec3_t vr_adjust_frozen_handpos = {0, 0, 0};
static vec3_t vr_adjust_frozen_handrot = {0, 0, 0};
static vec3_t vr_adjust_current_handpos = {0, 0, 0};
static vec3_t vr_adjust_current_handrot = {0, 0, 0};
static vec3_t vr_adjust_original_scale_origin = {0, 0, 0};
static int vr_adjust_slot = -1;
static char vr_adjust_model[64];

static qboolean VR_AdjustmentVisualsActive(void) {
  return vr_adjust_mode != VR_ADJUST_NONE || vr_adjust_muzzle_return_to_grip;
}

static void VR_FreeControllerRenderModel(vr_controller_render_model_t *cache) {
  vr::IVRRenderModels *render_models = vr::VRRenderModels();

  if (cache->model && render_models)
    render_models->FreeRenderModel(cache->model);
  if (cache->texture_map && render_models)
    render_models->FreeTexture(cache->texture_map);
  if (cache->texture_id) {
    glDeleteTextures(1, &cache->texture_id);
    GL_ClearBindings();
  }

  memset(cache, 0, sizeof(*cache));
}

static void VR_FreeControllerRenderModels(void) {
  for (int i = 0; i < 2; i++)
    VR_FreeControllerRenderModel(&controller_render_models[i]);
}

static void VR_UploadControllerRenderModelTexture(
    vr_controller_render_model_t *cache) {
  if (!cache->texture_map || cache->texture_id)
    return;

  if (cache->texture_map->format != vr::VRRenderModelTextureFormat_RGBA8_SRGB)
    return;

  glGenTextures(1, &cache->texture_id);
  glBindTexture(GL_TEXTURE_2D, cache->texture_id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cache->texture_map->unWidth,
               cache->texture_map->unHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               cache->texture_map->rubTextureMapData);
  GL_ClearBindings();
}

static vr_controller_render_model_t *VR_GetControllerRenderModel(int hand) {
  vr::IVRRenderModels *render_models = vr::VRRenderModels();
  vr_controller_render_model_t *cache;
  vr::TrackedDeviceIndex_t device;
  vr::ETrackedPropertyError prop_error = vr::TrackedProp_Success;
  char name[sizeof(controller_render_models[0].name)];

  if (!ovrHMD || !render_models)
    return NULL;

  device = controllers[hand].deviceIndex;
  if (device == vr::k_unTrackedDeviceIndexInvalid ||
      device >= vr::k_unMaxTrackedDeviceCount)
    return NULL;

  name[0] = 0;
  ovrHMD->GetStringTrackedDeviceProperty(
      device, vr::Prop_RenderModelName_String, name, sizeof(name),
      &prop_error);
  if (prop_error != vr::TrackedProp_Success || !name[0])
    return NULL;

  cache = &controller_render_models[hand];
  if (strcmp(cache->name, name)) {
    VR_FreeControllerRenderModel(cache);
    q_strlcpy(cache->name, name, sizeof(cache->name));
  }

  if (!cache->model && !cache->model_failed) {
    vr::RenderModel_t *model = NULL;
    vr::EVRRenderModelError error =
        render_models->LoadRenderModel_Async(cache->name, &model);

    if (error == vr::VRRenderModelError_Loading)
      return NULL;
    if (error != vr::VRRenderModelError_None) {
      cache->model_failed = true;
      Con_DPrintf("VR: could not load controller render model %s (%d)\n",
                  cache->name, (int)error);
      return NULL;
    }

    cache->model = model;
  }

  if (cache->model &&
      cache->model->diffuseTextureId != vr::INVALID_TEXTURE_ID &&
      !cache->texture_map && !cache->texture_failed) {
    vr::RenderModel_TextureMap_t *texture_map = NULL;
    vr::EVRRenderModelError error = render_models->LoadTexture_Async(
        cache->model->diffuseTextureId, &texture_map);

    if (error == vr::VRRenderModelError_Loading)
      return cache;
    if (error != vr::VRRenderModelError_None) {
      cache->texture_failed = true;
      Con_DPrintf("VR: could not load controller texture for %s (%d)\n",
                  cache->name, (int)error);
      return cache;
    }

    cache->texture_map = texture_map;
  }

  VR_UploadControllerRenderModelTexture(cache);
  return cache;
}

static void VR_TrackingPointToWorld(const vr::HmdVector3_t point,
                                    vec3_t out) {
  entity_t *player = &cl.entities[cl.viewentity];
  vec3_t tracking, headLocalPreRot, headLocal;

  tracking[0] = (point.v[2] - lastHeadOrigin[0]) * meters_to_units;
  tracking[1] = (point.v[0] - lastHeadOrigin[1]) * meters_to_units;
  tracking[2] = point.v[1] * meters_to_units;

  _VectorSubtract(tracking, headOrigin, headLocalPreRot);
  Vec3RotateZ(headLocalPreRot, vrYaw * M_PI_DIV_180, headLocal);
  _VectorAdd(headLocal, headOrigin, headLocal);

  out[0] = -headLocal[0] + player->origin[0];
  out[1] = -headLocal[1] + player->origin[1];
  out[2] = headLocal[2] + player->origin[2] + vr_floor_offset.value;
}

static qboolean VR_DeviceLocalToWorld(int hand, float x, float y, float z,
                                      vec3_t out) {
  vr::TrackedDeviceIndex_t device = controllers[hand].deviceIndex;
  vr::HmdMatrix34_t *matrix;
  vr::HmdVector3_t tracking;

  if (device == vr::k_unTrackedDeviceIndexInvalid ||
      device >= vr::k_unMaxTrackedDeviceCount ||
      !ovr_DevicePose[device].bPoseIsValid)
    return false;

  matrix = &ovr_DevicePose[device].mDeviceToAbsoluteTracking;
  tracking.v[0] =
      matrix->m[0][0] * x + matrix->m[0][1] * y + matrix->m[0][2] * z +
      matrix->m[0][3];
  tracking.v[1] =
      matrix->m[1][0] * x + matrix->m[1][1] * y + matrix->m[1][2] * z +
      matrix->m[1][3];
  tracking.v[2] =
      matrix->m[2][0] * x + matrix->m[2][1] * y + matrix->m[2][2] * z +
      matrix->m[2][3];

  VR_TrackingPointToWorld(tracking, out);
  return true;
}

static void VR_DrawControllerFallback(int hand) {
  vec3_t origin, end;

  if (!VR_DeviceLocalToWorld(hand, 0.0f, 0.0f, 0.0f, origin))
    return;

  glDisable(GL_TEXTURE_2D);
  glLineWidth(3.0f);
  glBegin(GL_LINES);
  glColor4f(1.0f, 0.2f, 0.2f, 0.9f);
  if (VR_DeviceLocalToWorld(hand, 0.08f, 0.0f, 0.0f, end)) {
    glVertex3fv(origin);
    glVertex3fv(end);
  }
  glColor4f(0.2f, 1.0f, 0.2f, 0.9f);
  if (VR_DeviceLocalToWorld(hand, 0.0f, 0.08f, 0.0f, end)) {
    glVertex3fv(origin);
    glVertex3fv(end);
  }
  glColor4f(0.2f, 0.4f, 1.0f, 0.9f);
  if (VR_DeviceLocalToWorld(hand, 0.0f, 0.0f, -0.12f, end)) {
    glVertex3fv(origin);
    glVertex3fv(end);
  }
  glEnd();
  glLineWidth(1.0f);
}

static void VR_DrawControllerRenderModel(vr_controller_render_model_t *cache,
                                         int hand) {
  const vr::RenderModel_t *model;
  qboolean textured;

  if (!cache || !cache->model) {
    VR_DrawControllerFallback(hand);
    return;
  }

  model = cache->model;
  textured = cache->texture_id != 0;

  if (textured) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, cache->texture_id);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  } else {
    glDisable(GL_TEXTURE_2D);
  }

  glColor4f(1.0f, 1.0f, 1.0f, 0.88f);
  glBegin(GL_TRIANGLES);
  for (uint32_t i = 0; i < model->unTriangleCount * 3; i++) {
    const vr::RenderModel_Vertex_t *vertex =
        &model->rVertexData[model->rIndexData[i]];
    vec3_t point;

    if (!VR_DeviceLocalToWorld(hand, vertex->vPosition.v[0],
                               vertex->vPosition.v[1],
                               vertex->vPosition.v[2], point))
      continue;
    if (textured)
      glTexCoord2f(vertex->rfTextureCoord[0], vertex->rfTextureCoord[1]);
    glVertex3fv(point);
  }
  glEnd();
}

void VR_DrawAdjustmentControllers(void) {
  if (!VR_AdjustmentVisualsActive() || !vr_initialized)
    return;

  glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT |
               GL_POLYGON_BIT | GL_LINE_BIT);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  if (controllers[1].seenThisFrame)
    VR_DrawControllerRenderModel(VR_GetControllerRenderModel(1), 1);

  glPopAttrib();
  GL_ClearBindings();
}

static qboolean VR_AdjustModeIsMuzzle(vr_adjust_mode_t mode) {
  return mode == VR_ADJUST_MUZZLE || mode == VR_ADJUST_MP_MUZZLE;
}

static qboolean VR_AdjustModeIsWeapon(vr_adjust_mode_t mode) {
  return mode == VR_ADJUST_WEAPON || mode == VR_ADJUST_MP_WEAPON;
}

static qboolean VR_IsMuzzleAdjustMode(void) {
  return VR_AdjustModeIsMuzzle(vr_adjust_mode);
}

static qboolean VR_TextReserve(vr_textbuf_t *buf, size_t extra) {
  size_t needed = buf->len + extra + 1;
  char *newdata;
  size_t newcap;

  if (needed <= buf->cap)
    return true;

  newcap = buf->cap ? buf->cap : 1024;
  while (newcap < needed)
    newcap *= 2;

  newdata = (char *)realloc(buf->data, newcap);
  if (!newdata)
    return false;

  buf->data = newdata;
  buf->cap = newcap;
  return true;
}

static qboolean VR_TextAppendN(vr_textbuf_t *buf, const char *text,
                               size_t len) {
  if (!len)
    return true;
  if (!VR_TextReserve(buf, len))
    return false;
  memcpy(buf->data + buf->len, text, len);
  buf->len += len;
  buf->data[buf->len] = 0;
  return true;
}

static qboolean VR_TextAppend(vr_textbuf_t *buf, const char *text) {
  return VR_TextAppendN(buf, text, strlen(text));
}

static qboolean VR_TextAppendLine(vr_textbuf_t *buf, const char *line) {
  return VR_TextAppend(buf, line) && VR_TextAppend(buf, "\n");
}

static qboolean VR_IsTokenBreak(char c) {
  return c == 0 || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static qboolean VR_LineStartsWithKey(const char *line, size_t len,
                                     const char *key) {
  size_t keylen = strlen(key);

  while (len && (*line == ' ' || *line == '\t' || *line == '\r')) {
    line++;
    len--;
  }

  return len >= keylen && !strncmp(line, key, keylen) &&
         (len == keylen || VR_IsTokenBreak(line[keylen]));
}

static qboolean VR_LineIsAdjustmentKey(const char *line, size_t len) {
  return VR_LineStartsWithKey(line, len, "held_scale") ||
         VR_LineStartsWithKey(line, len, "held_offset") ||
         VR_LineStartsWithKey(line, len, "mp_held_offset") ||
         VR_LineStartsWithKey(line, len, "muzzle_offset") ||
         VR_LineStartsWithKey(line, len, "mp_muzzle_offset");
}

static qboolean VR_LineIsGlobalAdjustmentKey(const char *line, size_t len) {
  return VR_LineStartsWithKey(line, len, "global_held_scale") ||
         VR_LineStartsWithKey(line, len, "global_held_offset") ||
         VR_LineStartsWithKey(line, len, "global_muzzle_offset");
}

static qboolean VR_TextAppendFilteredLines(vr_textbuf_t *buf, const char *text,
                                           size_t len,
                                           qboolean strip_adjustments,
                                           qboolean strip_globals) {
  const char *p = text;
  const char *end = text + len;

  while (p < end) {
    const char *line_end = p;
    size_t line_len;
    size_t full_len;
    qboolean strip = false;

    while (line_end < end && *line_end != '\n')
      line_end++;

    line_len = line_end - p;
    full_len = line_len + (line_end < end ? 1 : 0);

    if (strip_adjustments && VR_LineIsAdjustmentKey(p, line_len))
      strip = true;
    if (strip_globals && VR_LineIsGlobalAdjustmentKey(p, line_len))
      strip = true;

    if (!strip && !VR_TextAppendN(buf, p, full_len))
      return false;

    p += full_len;
  }

  return true;
}

static qboolean VR_BlockMatchesViewmodel(const char *block, size_t len,
                                         const char *model) {
  char *copy;
  char *parse;
  qboolean matched = false;

  copy = (char *)malloc(len + 1);
  if (!copy)
    return false;

  memcpy(copy, block, len);
  copy[len] = 0;
  parse = copy;

  while ((parse = (char *)COM_Parse(parse)) && com_token[0]) {
    if (!Q_strcmp(com_token, "viewmodel") ||
        !Q_strcmp(com_token, "held_model") || !Q_strcmp(com_token, "model")) {
      parse = (char *)COM_Parse(parse);
      if (!parse || !com_token[0])
        break;
      if (!Q_strcmp(com_token, model)) {
        matched = true;
        break;
      }
    }
  }

  free(copy);
  return matched;
}

static qboolean VR_AppendAdjustmentLines(vr_textbuf_t *buf, int slot) {
  char line[256];

  q_snprintf(line, sizeof(line), "held_scale %.7g",
             vr_weapon_offset[slot * VARS_PER_WEAPON + 3].value);
  if (!VR_TextAppendLine(buf, line))
    return false;

  q_snprintf(line, sizeof(line), "held_offset %.7g %.7g %.7g",
             vr_weapon_offset[slot * VARS_PER_WEAPON].value,
             vr_weapon_offset[slot * VARS_PER_WEAPON + 1].value,
             vr_weapon_offset[slot * VARS_PER_WEAPON + 2].value);
  if (!VR_TextAppendLine(buf, line))
    return false;

  q_snprintf(line, sizeof(line), "muzzle_offset %.7g %.7g %.7g",
             vr_weapon_muzzle_offset[slot * VARS_PER_WEAPON_MUZZLE].value,
             vr_weapon_muzzle_offset[slot * VARS_PER_WEAPON_MUZZLE + 1].value,
             vr_weapon_muzzle_offset[slot * VARS_PER_WEAPON_MUZZLE + 2].value);
  if (!VR_TextAppendLine(buf, line))
    return false;

  if (vr_weapon_has_schema_mp_held_offset[slot]) {
    q_snprintf(line, sizeof(line), "mp_held_offset %.7g %.7g %.7g",
               vr_weapon_schema_mp_held_offset[slot][0],
               vr_weapon_schema_mp_held_offset[slot][1],
               vr_weapon_schema_mp_held_offset[slot][2]);
    if (!VR_TextAppendLine(buf, line))
      return false;
  }

  if (vr_weapon_has_schema_mp_muzzle_offset[slot]) {
    q_snprintf(line, sizeof(line), "mp_muzzle_offset %.7g %.7g %.7g",
               vr_weapon_schema_mp_muzzle_offset[slot][0],
               vr_weapon_schema_mp_muzzle_offset[slot][1],
               vr_weapon_schema_mp_muzzle_offset[slot][2]);
    if (!VR_TextAppendLine(buf, line))
      return false;
  }

  return true;
}

static qboolean VR_WriteUpdatedWeaponBlock(vr_textbuf_t *buf,
                                           const char *block, size_t len,
                                           int slot) {
  const char *p = block;
  const char *end = block + len;
  qboolean inserted = false;

  while (p < end) {
    const char *line_end = p;
    size_t line_len;
    size_t full_len;

    while (line_end < end && *line_end != '\n')
      line_end++;

    line_len = line_end - p;
    full_len = line_len + (line_end < end ? 1 : 0);

    if (VR_LineIsAdjustmentKey(p, line_len)) {
      p += full_len;
      continue;
    }

    if (!inserted && VR_LineStartsWithKey(p, line_len, "}")) {
      if (!VR_AppendAdjustmentLines(buf, slot))
        return false;
      inserted = true;
    }

    if (!VR_TextAppendN(buf, p, full_len))
      return false;
    p += full_len;
  }

  if (!inserted && !VR_AppendAdjustmentLines(buf, slot))
    return false;

  return true;
}

static qboolean VR_SaveWeaponAdjustmentsToSchema(int slot) {
  const char *id;
  char *data;
  const char *src;
  const char *p;
  const char *end;
  vr_textbuf_t out = {0};
  qboolean updated = false;
  qboolean ok = true;

  if (slot < 0 || slot >= MAX_WEAPONS)
    return false;

  id = vr_weapon_offset[slot * VARS_PER_WEAPON + 4].string;
  if (!id || !id[0] || !Q_strcmp(id, "-1"))
    return false;

  data = (char *)COM_LoadZoneFile("vr_weapons.txt", NULL);
  src = data ? data : "";
  p = src;
  end = src + strlen(src);

  while (ok && p < end) {
    const char *open = (const char *)memchr(p, '{', end - p);
    const char *close;

    if (!open) {
      ok = VR_TextAppendN(&out, p, end - p);
      break;
    }

    ok = VR_TextAppendN(&out, p, open - p);
    if (!ok)
      break;

    close = (const char *)memchr(open, '}', end - open);
    if (!close) {
      ok = VR_TextAppendN(&out, open, end - open);
      break;
    }
    close++;

    if (VR_BlockMatchesViewmodel(open, close - open, id)) {
      ok = VR_WriteUpdatedWeaponBlock(&out, open, close - open, slot);
      updated = true;
    } else {
      ok = VR_TextAppendN(&out, open, close - open);
    }

    p = close;
  }

  if (ok && !updated) {
    char line[128];

    if (out.len && out.data[out.len - 1] != '\n')
      ok = VR_TextAppend(&out, "\n");
    if (ok)
      ok = VR_TextAppendLine(&out, "{");
    q_snprintf(line, sizeof(line), "viewmodel %s", id);
    if (ok)
      ok = VR_TextAppendLine(&out, line);
    if (ok)
      ok = VR_AppendAdjustmentLines(&out, slot);
    if (ok)
      ok = VR_TextAppendLine(&out, "}");
  }

  if (ok) {
    COM_WriteFile("vr_weapons.txt", out.data ? out.data : "", out.len);
    Con_Printf("VR: saved weapon adjustments for %s to %s/vr_weapons.txt\n",
               id, com_gamedir);
  } else {
    Con_Printf("VR: failed to save weapon adjustments for %s\n", id);
  }

  if (out.data)
    free(out.data);
  if (data)
    Z_Free(data);

  return ok;
}

static qboolean VR_SchemaFileExistsInGameDir(void) {
  char path[MAX_OSPATH];

  q_snprintf(path, sizeof(path), "%s/vr_weapons.txt", com_gamedir);
  return (Sys_FileType(path) & FS_ENT_FILE) != 0;
}

static qboolean VR_PreviousSlotHasSameID(int slot) {
  const char *id;

  if (!VR_WeaponOffsetSlotHasValidID(slot))
    return false;

  id = vr_weapon_offset[slot * VARS_PER_WEAPON + 4].string;
  for (int i = 0; i < slot; i++) {
    const char *other;

    if (!VR_WeaponOffsetSlotHasValidID(i))
      continue;
    other = vr_weapon_offset[i * VARS_PER_WEAPON + 4].string;
    if (!Q_strcmp(id, other))
      return true;
  }

  return false;
}

static qboolean VR_AppendSchemaBlockForSlot(vr_textbuf_t *buf, int slot) {
  char line[256];
  const char *id;

  if (!VR_WeaponOffsetSlotHasValidID(slot))
    return true;

  id = vr_weapon_offset[slot * VARS_PER_WEAPON + 4].string;
  if (!VR_TextAppendLine(buf, "{"))
    return false;
  q_snprintf(line, sizeof(line), "viewmodel %s", id);
  if (!VR_TextAppendLine(buf, line))
    return false;
  if (!VR_AppendAdjustmentLines(buf, slot))
    return false;
  if (!VR_TextAppendLine(buf, "}"))
    return false;
  return VR_TextAppendLine(buf, "");
}

static qboolean VR_CreateDefaultWeaponSchemaIfMissing(void) {
  vr_textbuf_t out = {0};
  int count = 0;
  qboolean ok = true;

  if (VR_SchemaFileExistsInGameDir())
    return true;

  for (int i = 0; i < MAX_WEAPONS; i++) {
    if (!VR_WeaponOffsetSlotHasValidID(i) || VR_PreviousSlotHasSameID(i))
      continue;
    ok = VR_AppendSchemaBlockForSlot(&out, i);
    if (!ok)
      break;
    count++;
  }

  if (ok && count > 0) {
    COM_WriteFile("vr_weapons.txt", out.data ? out.data : "", out.len);
    Con_Printf("VR: created %s/vr_weapons.txt with %d built-in weapon offsets\n",
               com_gamedir, count);
  } else if (!ok) {
    Con_Printf("VR: failed to create default vr_weapons.txt for %s\n",
               com_gamedir);
  }

  if (out.data)
    free(out.data);

  return ok;
}

static qboolean VR_SchemaHasViewmodelBlock(const char *data, const char *id) {
  const char *p;
  const char *end;

  if (!data || !id || !id[0])
    return false;

  p = data;
  end = data + strlen(data);
  while (p < end) {
    const char *open = (const char *)memchr(p, '{', end - p);
    const char *close;

    if (!open)
      break;

    close = (const char *)memchr(open, '}', end - open);
    if (!close)
      break;
    close++;

    if (VR_BlockMatchesViewmodel(open, close - open, id))
      return true;

    p = close;
  }

  return false;
}

static qboolean VR_FillMissingDefaultWeaponSchemaBlocks(void) {
  char *data;
  vr_textbuf_t out = {0};
  int count = 0;
  qboolean ok = true;

  data = (char *)COM_LoadZoneFile("vr_weapons.txt", NULL);
  if (!data)
    return true;

  ok = VR_TextAppend(&out, data);
  for (int i = 0; ok && i < MAX_WEAPONS; i++) {
    const char *id;

    if (!VR_WeaponOffsetSlotHasValidID(i) || VR_PreviousSlotHasSameID(i))
      continue;

    id = vr_weapon_offset[i * VARS_PER_WEAPON + 4].string;
    if (VR_SchemaHasViewmodelBlock(data, id))
      continue;

    if (out.len && out.data[out.len - 1] != '\n')
      ok = VR_TextAppend(&out, "\n");
    if (ok)
      ok = VR_AppendSchemaBlockForSlot(&out, i);
    if (ok)
      count++;
  }

  if (ok && count > 0) {
    COM_WriteFile("vr_weapons.txt", out.data ? out.data : "", out.len);
    Con_Printf("VR: added %d built-in weapon offsets to %s/vr_weapons.txt\n",
               count, com_gamedir);
  } else if (!ok) {
    Con_Printf("VR: failed to update default vr_weapons.txt for %s\n",
               com_gamedir);
  }

  if (out.data)
    free(out.data);
  Z_Free(data);

  return ok;
}

static void VR_CopyWeaponAdjustmentSlot(int dst, int src) {
  if (dst < 0 || dst >= MAX_WEAPONS || src < 0 || src >= MAX_WEAPONS)
    return;

  for (int i = 0; i < 4; i++)
    Cvar_SetQuick(&vr_weapon_offset[dst * VARS_PER_WEAPON + i],
                  vr_weapon_offset[src * VARS_PER_WEAPON + i].string);

  for (int i = 0; i < VARS_PER_WEAPON_MUZZLE; i++)
    Cvar_SetQuick(
        &vr_weapon_muzzle_offset[dst * VARS_PER_WEAPON_MUZZLE + i],
        vr_weapon_muzzle_offset[src * VARS_PER_WEAPON_MUZZLE + i].string);
}

static void VR_CopyCurrentAdjustmentsToSchemaSlots(int src) {
  for (int i = 0; i < num_vr_weapons; i++) {
    const char *id = vr_weapons[i].viewmodel_path[0]
                         ? vr_weapons[i].viewmodel_path
                         : vr_weapons[i].model_path;
    int slot;

    if (!id || !id[0])
      continue;

    slot = VR_FindWeaponOffsetSlot(id, NULL);
    if (slot >= 0)
      VR_CopyWeaponAdjustmentSlot(slot, src);
  }

  lastWeaponHeader = NULL;
}

static qboolean VR_AppendGlobalAdjustmentLines(vr_textbuf_t *buf, int slot) {
  char line[256];

  q_snprintf(line, sizeof(line), "global_held_scale %.7g",
             vr_weapon_offset[slot * VARS_PER_WEAPON + 3].value);
  if (!VR_TextAppendLine(buf, line))
    return false;

  q_snprintf(line, sizeof(line), "global_held_offset %.7g %.7g %.7g",
             vr_weapon_offset[slot * VARS_PER_WEAPON].value,
             vr_weapon_offset[slot * VARS_PER_WEAPON + 1].value,
             vr_weapon_offset[slot * VARS_PER_WEAPON + 2].value);
  if (!VR_TextAppendLine(buf, line))
    return false;

  q_snprintf(line, sizeof(line), "global_muzzle_offset %.7g %.7g %.7g",
             vr_weapon_muzzle_offset[slot * VARS_PER_WEAPON_MUZZLE].value,
             vr_weapon_muzzle_offset[slot * VARS_PER_WEAPON_MUZZLE + 1].value,
             vr_weapon_muzzle_offset[slot * VARS_PER_WEAPON_MUZZLE + 2].value);
  return VR_TextAppendLine(buf, line);
}

static qboolean VR_SaveGlobalWeaponAdjustmentsToSchema(int slot) {
  const char *id;
  char *data;
  const char *src;
  const char *p;
  const char *end;
  vr_textbuf_t out = {0};
  qboolean ok = true;
  qboolean wrote_block = false;

  if (slot < 0 || slot >= MAX_WEAPONS)
    return false;

  id = vr_weapon_offset[slot * VARS_PER_WEAPON + 4].string;
  if (!id || !id[0] || !Q_strcmp(id, "-1"))
    return false;

  data = (char *)COM_LoadZoneFile("vr_weapons.txt", NULL);
  src = data ? data : "";
  p = src;
  end = src + strlen(src);

  ok = VR_AppendGlobalAdjustmentLines(&out, slot) && VR_TextAppend(&out, "\n");

  while (ok && p < end) {
    const char *open = (const char *)memchr(p, '{', end - p);
    const char *close;

    if (!open) {
      ok = VR_TextAppendFilteredLines(&out, p, end - p, false, true);
      break;
    }

    ok = VR_TextAppendFilteredLines(&out, p, open - p, false, true);
    if (!ok)
      break;

    close = (const char *)memchr(open, '}', end - open);
    if (!close) {
      ok = VR_TextAppendFilteredLines(&out, open, end - open, true, true);
      break;
    }
    close++;

    ok = VR_TextAppendFilteredLines(&out, open, close - open, true, true);
    wrote_block = true;
    p = close;
  }

  if (ok && !wrote_block) {
    char line[128];

    if (out.len && out.data[out.len - 1] != '\n')
      ok = VR_TextAppend(&out, "\n");
    if (ok)
      ok = VR_TextAppendLine(&out, "{");
    q_snprintf(line, sizeof(line), "viewmodel %s", id);
    if (ok)
      ok = VR_TextAppendLine(&out, line);
    if (ok)
      ok = VR_TextAppendLine(&out, "}");
  }

  if (ok) {
    COM_WriteFile("vr_weapons.txt", out.data ? out.data : "", out.len);
    VR_CopyCurrentAdjustmentsToSchemaSlots(slot);
    Con_Printf("VR: saved global weapon adjustments from %s to "
               "%s/vr_weapons.txt\n",
               id, com_gamedir);
  } else {
    Con_Printf("VR: failed to save global weapon adjustments for %s\n", id);
  }

  if (out.data)
    free(out.data);
  if (data)
    Z_Free(data);

  return ok;
}

/*
 * Multiplayer calibration must stay with the active viewmodel. A shared
 * offset moved every weapon (and its projectile source) together, which is
 * particularly wrong for mods such as QBJ3 where each weapon mesh has a
 * different origin. The schema already supports per-viewmodel mp_* fields.
 */
static void VR_SetMPMuzzleOffsetForSlot(int slot, const vec3_t offset) {
  if (slot < 0 || slot >= MAX_WEAPONS)
    return;

  VectorCopy(offset, vr_weapon_mp_muzzle_offset[slot]);
  vr_weapon_has_mp_muzzle_offset[slot] = true;
  VectorCopy(offset, vr_weapon_schema_mp_muzzle_offset[slot]);
  vr_weapon_has_schema_mp_muzzle_offset[slot] = true;
}

static void VR_SetMPHeldOffsetForSlot(int slot, const vec3_t offset) {
  if (slot < 0 || slot >= MAX_WEAPONS)
    return;

  VectorCopy(offset, vr_weapon_mp_held_offset[slot]);
  vr_weapon_has_mp_held_offset[slot] = true;
  VectorCopy(offset, vr_weapon_schema_mp_held_offset[slot]);
  vr_weapon_has_schema_mp_held_offset[slot] = true;
}

static void VR_AimOffsetToWorld(const vec3_t local, const vec3_t angles,
                                float scale, vec3_t world) {
  vec3_t forward, right, up;
  vec3_t mutable_angles;

  VectorCopy(angles, mutable_angles);
  AngleVectors(mutable_angles, forward, right, up);
  world[0] = (right[0] * local[0] + up[0] * local[1] +
              forward[0] * local[2]) *
             scale;
  world[1] = (right[1] * local[0] + up[1] * local[1] +
              forward[1] * local[2]) *
             scale;
  world[2] = (right[2] * local[0] + up[2] * local[1] +
              forward[2] * local[2]) *
             scale;
}

static void VR_WorldToAimOffset(const vec3_t world, const vec3_t angles,
                                float scale, vec3_t local) {
  vec3_t forward, right, up;
  vec3_t mutable_angles;

  if (scale == 0.0f)
    scale = 1.0f;

  VectorCopy(angles, mutable_angles);
  AngleVectors(mutable_angles, forward, right, up);
  local[0] = DotProduct(world, right) / scale;
  local[1] = DotProduct(world, up) / scale;
  local[2] = DotProduct(world, forward) / scale;
}

static qboolean VR_GetMuzzleSourceCompensation(int slot, const vec3_t angles,
                                               vec3_t out) {
  qboolean has_source;

  out[0] = out[1] = out[2] = 0.0f;

  if (slot < 0 || slot >= MAX_WEAPONS)
    return false;

  if (VR_IsMultiplayerClient())
    return false;

  has_source = vr_weapon_has_muzzle_source_offset[slot] ||
               vr_weapon_has_muzzle_source_viewofs[slot];
  if (!has_source)
    return false;

  // The adjustment target is the final QuakeC source. Account for the engine's
  // temporary self.origin compensation before applying mod-authored offsets.
  if (sv.active && !isDedicated) {
    out[2] -= vr_projectilespawn_z_offset.value;
  } else {
    vec3_t forward, right, up;
    vec3_t mutable_angles;

    VectorCopy(angles, mutable_angles);
    AngleVectors(mutable_angles, forward, right, up);
    VectorMA(out, -6.0f, forward, out);
    out[2] -= 16.0f;
  }

  if (vr_weapon_has_muzzle_source_viewofs[slot] &&
      vr_weapon_muzzle_source_viewofs[slot])
    out[2] += cl.viewheight;

  if (vr_weapon_has_muzzle_source_offset[slot]) {
    vec3_t source_world;

    VR_AimOffsetToWorld(vr_weapon_muzzle_source_offset[slot], angles, 1.0f,
                        source_world);
    VectorAdd(out, source_world, out);
  }

  return true;
}

static void VR_HandRotToViewmodelAngles(const vec3_t handrot,
                                        vec3_t viewmodel_angles) {
  viewmodel_angles[YAW] = handrot[YAW];
  viewmodel_angles[PITCH] = -handrot[PITCH] + vr_gunmodelpitch.value;
  viewmodel_angles[ROLL] = handrot[ROLL];
}

static void VR_ModelOffsetToWorld(const vec3_t local,
                                  const vec3_t viewmodel_angles,
                                  float scale, vec3_t world) {
  float yaw = viewmodel_angles[YAW] * M_PI_DIV_180;
  float pitch = viewmodel_angles[PITCH] * M_PI_DIV_180;
  float roll = viewmodel_angles[ROLL] * M_PI_DIV_180;
  float sy = sin(yaw), cy = cos(yaw);
  float sp = sin(pitch), cp = cos(pitch);
  float sr = sin(roll), cr = cos(roll);
  float x1, y1, z1, x2, y2, z2;

  // Match R_RotateForEntity: yaw around Z, -pitch around Y, roll around X.
  x1 = local[0];
  y1 = local[1] * cr - local[2] * sr;
  z1 = local[1] * sr + local[2] * cr;

  x2 = x1 * cp - z1 * sp;
  y2 = y1;
  z2 = x1 * sp + z1 * cp;

  world[0] = (x2 * cy - y2 * sy) * scale;
  world[1] = (x2 * sy + y2 * cy) * scale;
  world[2] = z2 * scale;
}

static void VR_WorldToModelOffset(const vec3_t world,
                                  const vec3_t viewmodel_angles,
                                  float scale, vec3_t local) {
  float yaw = viewmodel_angles[YAW] * M_PI_DIV_180;
  float pitch = viewmodel_angles[PITCH] * M_PI_DIV_180;
  float roll = viewmodel_angles[ROLL] * M_PI_DIV_180;
  float sy = sin(yaw), cy = cos(yaw);
  float sp = sin(pitch), cp = cos(pitch);
  float sr = sin(roll), cr = cos(roll);
  float x, y, z, x1, y1, z1, x2, y2, z2;

  if (scale == 0.0f)
    scale = 1.0f;

  x = world[0] / scale;
  y = world[1] / scale;
  z = world[2] / scale;

  // Inverse of R_RotateForEntity's yaw, -pitch, roll sequence.
  x1 = x * cy + y * sy;
  y1 = -x * sy + y * cy;
  z1 = z;

  x2 = x1 * cp + z1 * sp;
  y2 = y1;
  z2 = -x1 * sp + z1 * cp;

  local[0] = x2;
  local[1] = y2 * cr + z2 * sr;
  local[2] = -y2 * sr + z2 * cr;
}

void VR_GetMuzzleAdjustedHandPos(vec3_t out) {
  if (VR_IsMuzzleAdjustMode()) {
    VectorCopy(vr_adjust_current_handpos, out);
    return;
  }

  VectorCopy(cl.handpos[1], out);

  if (weaponCVarEntry >= 0) {
    vec3_t local = {
        vr_weapon_muzzle_offset[weaponCVarEntry * VARS_PER_WEAPON_MUZZLE]
            .value,
        vr_weapon_muzzle_offset[weaponCVarEntry * VARS_PER_WEAPON_MUZZLE + 1]
            .value,
        vr_weapon_muzzle_offset[weaponCVarEntry * VARS_PER_WEAPON_MUZZLE + 2]
            .value};
    vec3_t world;

    if (VR_IsMultiplayerClient() &&
        vr_weapon_has_mp_muzzle_offset[weaponCVarEntry])
      VectorAdd(local, vr_weapon_mp_muzzle_offset[weaponCVarEntry], local);

    VR_AimOffsetToWorld(local, cl.handrot[1], vr_gunmodelscale.value, world);
    VectorAdd(out, world, out);
  }
}

static qboolean VR_AdjustCanStart(void) {
  if (!vr_enabled.value || !vr_initialized) {
    Con_Printf("vradjust: VR is not enabled.\n");
    return false;
  }

  if ((int)vr_aimmode.value != VR_AIMMODE_CONTROLLER) {
    Con_Printf("vradjust: controller aiming is required.\n");
    return false;
  }

  if (!cl.viewent.model) {
    Con_Printf("vradjust: no active held weapon viewmodel.\n");
    return false;
  }

  if (weaponCVarEntry < 0) {
    weaponCVarEntry =
        VR_CreateHeldWeaponOffsetSlot(cl.viewent.model->name, "vradjust");
    if (weaponCVarEntry < 0)
      return false;
  }

  return true;
}

static void VR_AdjustBegin(vr_adjust_mode_t mode) {
  const char *id;
  aliashdr_t *hdr;

  if (VR_AdjustModeIsMuzzle(mode) && vr_adjust_muzzle_return_to_grip) {
    vr_adjust_muzzle_return_to_grip = false;
    Con_Printf("vradjustmuzzle: return-to-grip hold cancelled.\n");
    return;
  }

  if (vr_adjust_mode == mode) {
    vr_adjust_mode = VR_ADJUST_NONE;
    Con_Printf("vradjust: cancelled.\n");
    return;
  }

  if (!VR_AdjustCanStart())
    return;

  vr_adjust_mode = mode;
  vr_adjust_slot = weaponCVarEntry;
  id = vr_weapon_offset[vr_adjust_slot * VARS_PER_WEAPON + 4].string;
  Q_strncpy(vr_adjust_model, id ? id : "", sizeof(vr_adjust_model));

  VectorCopy(cl.handpos[1], vr_adjust_frozen_handpos);
  VectorCopy(cl.handrot[1], vr_adjust_frozen_handrot);
  VectorCopy(cl.handpos[1], vr_adjust_current_handpos);
  VectorCopy(cl.handrot[1], vr_adjust_current_handrot);
  hdr = VR_ActiveAliasHeader(cl.viewent.model, cl.viewent.skinnum,
                             cl.viewent.frame);
  VectorCopy(hdr->original_scale_origin, vr_adjust_original_scale_origin);

  if (mode == VR_ADJUST_WEAPON) {
    Con_Printf("vradjustweapon: weapon frozen. Move the controller to the "
               "desired grip point and press fire to save.\n");
  } else if (mode == VR_ADJUST_MP_WEAPON) {
    Con_Printf("vradjustmpweapon: weapon frozen. Move the controller to the "
               "desired multiplayer grip point and press fire to save.\n");
  } else if (mode == VR_ADJUST_MP_MUZZLE) {
    Con_Printf("vradjustmpmuzzle: weapon frozen. Move the controller to the "
               "desired multiplayer bullet origin and press fire to save.\n");
  } else {
    Con_Printf("vradjustmuzzle: weapon frozen. Move the controller to the "
               "desired bullet origin and press fire to save.\n");
  }
}

static void VR_AdjustWeapon_f(void) { VR_AdjustBegin(VR_ADJUST_WEAPON); }

static void VR_AdjustMPWeapon_f(void) {
  if (!VR_IsMultiplayerClient()) {
    Con_Printf("vradjustmpweapon: connect to a multiplayer server first.\n");
    return;
  }

  VR_AdjustBegin(VR_ADJUST_MP_WEAPON);
}

static void VR_AdjustMuzzle_f(void) { VR_AdjustBegin(VR_ADJUST_MUZZLE); }

static void VR_AdjustMPMuzzle_f(void) {
  if (!VR_IsMultiplayerClient()) {
    Con_Printf("vradjustmpmuzzle: connect to a multiplayer server first.\n");
    return;
  }

  VR_AdjustBegin(VR_ADJUST_MP_MUZZLE);
}

static void VR_GlobalWeaponOffset_f(void) {
  if (!VR_AdjustCanStart())
    return;

  VR_SaveGlobalWeaponAdjustmentsToSchema(weaponCVarEntry);
}

static void VR_AdjustWeaponUpdatePose(void) {
  if (vr_adjust_mode == VR_ADJUST_NONE && !vr_adjust_muzzle_return_to_grip)
    return;

  VectorCopy(cl.handpos[1], vr_adjust_current_handpos);
  VectorCopy(cl.handrot[1], vr_adjust_current_handrot);

  if (vr_adjust_muzzle_return_to_grip) {
    vec3_t delta;

    VectorSubtract(vr_adjust_current_handpos, vr_adjust_frozen_handpos, delta);
    if (DotProduct(delta, delta) <= 64.0f) {
      vr_adjust_muzzle_return_to_grip = false;
      Con_Printf("vradjustmuzzle: live muzzle restored.\n");
      return;
    }
  }

  VectorCopy(vr_adjust_frozen_handpos, cl.handpos[1]);
  VectorCopy(vr_adjust_frozen_handrot, cl.handrot[1]);
  VectorCopy(vr_adjust_frozen_handrot, cl.aimangles);
}

static qboolean VR_AdjustWeaponCommit(void) {
  int slot = vr_adjust_slot;
  char value[32];

  if (vr_adjust_mode == VR_ADJUST_NONE)
    return false;

  if (slot < 0 || slot >= MAX_WEAPONS || weaponCVarEntry != slot ||
      !cl.viewent.model ||
      Q_strcmp(cl.viewent.model->name, vr_adjust_model) ||
      Q_strcmp(vr_weapon_offset[slot * VARS_PER_WEAPON + 4].string,
               vr_adjust_model)) {
    Con_Printf("vradjust: active weapon changed; cancelled.\n");
    vr_adjust_mode = VR_ADJUST_NONE;
    return true;
  }

  if (VR_AdjustModeIsWeapon(vr_adjust_mode)) {
    vec3_t old_offset, old_anchor_world, target_delta_world;
    vec3_t frozen_angles, new_scale_origin;
    vec3_t base_offset, mp_held_offset, new_effective_offset, new_base_offset;
    float scaleCorrect =
        (vr_world_scale.value / 0.75f) * vr_gunmodelscale.value;

    base_offset[0] = vr_weapon_offset[slot * VARS_PER_WEAPON].value;
    base_offset[1] = vr_weapon_offset[slot * VARS_PER_WEAPON + 1].value;
    base_offset[2] = vr_weapon_offset[slot * VARS_PER_WEAPON + 2].value;
    mp_held_offset[0] = mp_held_offset[1] = mp_held_offset[2] = 0.0f;
    if (VR_IsMultiplayerClient() && vr_weapon_has_mp_held_offset[slot])
      VectorCopy(vr_weapon_mp_held_offset[slot], mp_held_offset);

    VectorCopy(vr_adjust_original_scale_origin, old_offset);
    VectorAdd(old_offset, base_offset, old_offset);
    VectorAdd(old_offset, mp_held_offset, old_offset);

    VR_HandRotToViewmodelAngles(vr_adjust_frozen_handrot, frozen_angles);
    VR_ModelOffsetToWorld(old_offset, frozen_angles, scaleCorrect,
                          old_anchor_world);
    VectorAdd(vr_adjust_frozen_handpos, old_anchor_world, old_anchor_world);

    VectorSubtract(old_anchor_world, vr_adjust_current_handpos,
                   target_delta_world);
    // Weapon adjustment is a recentering tool. Use the frozen rotation so
    // wrist movement while placing the controller cannot skew the saved offset.
    VR_WorldToModelOffset(target_delta_world, frozen_angles, scaleCorrect,
                          new_scale_origin);

    VectorSubtract(new_scale_origin, vr_adjust_original_scale_origin,
                   new_effective_offset);

    if (vr_adjust_mode == VR_ADJUST_MP_WEAPON) {
      VectorSubtract(new_effective_offset, base_offset, mp_held_offset);
      VR_SetMPHeldOffsetForSlot(slot, mp_held_offset);

      Con_Printf("vradjustmpweapon: mp_held_offset %.7g %.7g %.7g\n",
                 mp_held_offset[0], mp_held_offset[1], mp_held_offset[2]);
    } else {
      VectorSubtract(new_effective_offset, mp_held_offset, new_base_offset);

      for (int i = 0; i < 3; i++) {
        q_snprintf(value, sizeof(value), "%.7g", new_base_offset[i]);
        Cvar_SetQuick(&vr_weapon_offset[slot * VARS_PER_WEAPON + i], value);
      }

      Con_Printf("vradjustweapon: held_offset %.7g %.7g %.7g\n",
                 vr_weapon_offset[slot * VARS_PER_WEAPON].value,
                 vr_weapon_offset[slot * VARS_PER_WEAPON + 1].value,
                 vr_weapon_offset[slot * VARS_PER_WEAPON + 2].value);
    }
  } else {
    vec3_t delta, local, source_comp, target_preorigin;

    VectorCopy(vr_adjust_current_handpos, target_preorigin);
    if (VR_GetMuzzleSourceCompensation(slot, vr_adjust_frozen_handrot,
                                       source_comp))
      VectorSubtract(target_preorigin, source_comp, target_preorigin);

    VectorSubtract(target_preorigin, vr_adjust_frozen_handpos, delta);
    VR_WorldToAimOffset(delta, vr_adjust_frozen_handrot,
                        vr_gunmodelscale.value, local);

    if (vr_adjust_mode == VR_ADJUST_MP_MUZZLE) {
      vec3_t base, mp_muzzle_offset;

      base[0] =
          vr_weapon_muzzle_offset[slot * VARS_PER_WEAPON_MUZZLE].value;
      base[1] =
          vr_weapon_muzzle_offset[slot * VARS_PER_WEAPON_MUZZLE + 1].value;
      base[2] =
          vr_weapon_muzzle_offset[slot * VARS_PER_WEAPON_MUZZLE + 2].value;
      VectorSubtract(local, base, mp_muzzle_offset);
      VR_SetMPMuzzleOffsetForSlot(slot, mp_muzzle_offset);

      Con_Printf("vradjustmpmuzzle: mp_muzzle_offset %.7g %.7g %.7g\n",
                 mp_muzzle_offset[0], mp_muzzle_offset[1],
                 mp_muzzle_offset[2]);
      Con_Printf("vradjustmpmuzzle: saved; return the controller to the grip "
                 "to resume live muzzle tracking.\n");
    } else {
      for (int i = 0; i < 3; i++) {
        q_snprintf(value, sizeof(value), "%.7g", local[i]);
        Cvar_SetQuick(
            &vr_weapon_muzzle_offset[slot * VARS_PER_WEAPON_MUZZLE + i],
            value);
      }

      Con_Printf("vradjustmuzzle: muzzle_offset %.7g %.7g %.7g\n",
                 vr_weapon_muzzle_offset[slot * VARS_PER_WEAPON_MUZZLE].value,
                 vr_weapon_muzzle_offset[slot * VARS_PER_WEAPON_MUZZLE + 1]
                     .value,
                 vr_weapon_muzzle_offset[slot * VARS_PER_WEAPON_MUZZLE + 2]
                     .value);
      Con_Printf("vradjustmuzzle: saved; return the controller to the grip to "
                 "resume live muzzle tracking.\n");
    }
    vr_adjust_muzzle_return_to_grip = true;
  }

  VR_SaveWeaponAdjustmentsToSchema(slot);
  lastWeaponHeader = NULL;
  vr_adjust_mode = VR_ADJUST_NONE;
  return true;
}

static qboolean VR_AdjustWeaponConsumeTrigger(void) {
  return VR_AdjustWeaponCommit();
}

void InitAllWeaponCVars() {
  int i = 0;

  // weapons for Arcane Dimensions mod; initially made for v1.70 + patch1
  if (!strcmp(COM_SkipPath(com_gamedir), "ad")) {
    // ad specific models
    InitWeaponCVars(i++, "progs/v_shadaxe0.mdl", "-1.5", "43.1", "41",
                    "0.25"); // shadow axe
    InitWeaponCVars(i++, "progs/v_shadaxe1.mdl", "-1.5", "43.1", "41",
                    "0.25"); // shadow axe glow variant
    InitWeaponCVars(i++, "progs/v_shadaxe2.mdl", "-1.5", "43.1", "41",
                    "0.25"); // shadow axe glow variant
    InitWeaponCVars(i++, "progs/v_shadaxe3.mdl", "-1.5", "43.1", "41",
                    "0.25"); // shadow axe upgrade, same numbers
    InitWeaponCVars(i++, "progs/v_shadaxe4.mdl", "-1.5", "43.1", "41",
                    "0.25"); // shadow axe upgrade glow variant
    InitWeaponCVars(i++, "progs/v_shadaxe5.mdl", "-1.5", "43.1", "41",
                    "0.25"); // shadow axe upgrade glow variant
    InitWeaponCVars(i++, "progs/v_shot3.mdl", "-3.5", "0.4", "8.5",
                    "0.8"); // triple barrel shotgun ("Widowmaker")

    // Plague's models
    // (https://github.com/gameflorist/quake-plague-weapons-vr/releases)
    if (vr_gunmodeloffsets.value == VR_GUNMODELOFFSETS_PLAGUE) {
      InitWeaponCVars(i++, "progs/v_shot.mdl", "-1", "1.3", "7",
                      "0.6"); // shotgun
      InitWeaponCVars(i++, "progs/v_shot2.mdl", "-5.9", "1.1", "8.5",
                      "0.6"); // supershotgun
      InitWeaponCVars(i++, "progs/v_nail.mdl", "-11", "5.1", "19",
                      "0.32"); // nailgun
      InitWeaponCVars(i++, "progs/v_nail2.mdl", "-11.6", "4.6", "21.8",
                      "0.26"); // supernailgun
      InitWeaponCVars(i++, "progs/v_rock.mdl", "-3.5", "2.6", "12",
                      "0.36"); // grenade
      InitWeaponCVars(i++, "progs/v_rock2.mdl", "-7.2", "4", "18.2",
                      "0.32"); // rocket
      InitWeaponCVars(i++, "progs/v_light.mdl", "-3.1", "4.4", "14.2",
                      "0.37"); // lightning
      InitWeaponCVars(i++, "progs/v_plasma.mdl", "-3.1", "4.4", "14.2",
                      "0.37"); // plasma - same as lightning
    } else {
      InitWeaponCVars(i++, "progs/v_shot.mdl", "1.5", "1.7", "17.5",
                      "0.33"); // shotgun
      InitWeaponCVars(i++, "progs/v_shot2.mdl", "-3.5", "0.4", "8.5",
                      "0.8"); // double barrel shotgun
      InitWeaponCVars(i++, "progs/v_nail.mdl", "-9.5", "3", "17",
                      "0.5"); // nailgun
      InitWeaponCVars(i++, "progs/v_nail2.mdl", "-6", "3.5", "20",
                      "0.4"); // supernailgun
      InitWeaponCVars(i++, "progs/v_rock.mdl", "-3", "1.25", "17",
                      "0.5"); // grenade
      InitWeaponCVars(i++, "progs/v_rock2.mdl", "0", "5.55", "22.5",
                      "0.45"); // rocket
      InitWeaponCVars(i++, "progs/v_light.mdl", "-4", "3.1", "13",
                      "0.5"); // lightning
      InitWeaponCVars(i++, "progs/v_plasma.mdl", "2.8", "1.8", "22.5",
                      "0.5"); // plasma
    }
  }
  // weapons for Alkaline
  else if (!strcmp(COM_SkipPath(com_gamedir), "alk")) {
    // alkaline specific models
    InitWeaponCVars(i++, "progs/v_alkaxe20fps.mdl", "12", "54", "39.5",
                    "0.25"); // alkaline axe
    InitWeaponCVars(i++, "progs/v_saw.mdl", "-6.5", "27.5", "42",
                    "0.25"); // chainsaw
    InitWeaponCVars(i++, "progs/v_plasma.mdl", "16", "6", "16",
                    "0.4"); // plasma

    // Plague's models
    // (https://github.com/gameflorist/quake-plague-weapons-vr/releases)
    if (vr_gunmodeloffsets.value == VR_GUNMODELOFFSETS_PLAGUE) {
      InitWeaponCVars(i++, "progs/v_shot40fps.mdl", "-1", "1.3", "7",
                      "0.6"); // shotgun
      InitWeaponCVars(i++, "progs/v_shot2_40fps.mdl", "-5.9", "1.1", "8.5",
                      "0.6"); // supershotgun
      InitWeaponCVars(i++, "progs/v_nail_alk40fps.mdl", "-11", "5.1", "19",
                      "0.32"); // nailgun
      InitWeaponCVars(i++, "progs/v_nail3.mdl", "-11.6", "4.6", "21.8",
                      "0.26"); // supernailgun
      InitWeaponCVars(i++, "progs/v_rock_40fps.mdl", "-3.5", "2.6", "12",
                      "0.36"); // grenade
      InitWeaponCVars(i++, "progs/v_rock2_40fps.mdl", "-7.2", "4", "18.2",
                      "0.32"); // rocket
      InitWeaponCVars(i++, "progs/v_light.mdl", "-3.1", "4.4", "14.2",
                      "0.37"); // lightning
      InitWeaponCVars(i++, "progs/v_laserg40fps.mdl", "-5", "3.4", "22",
                      "0.33"); // laser
      InitWeaponCVars(i++, "progs/v_mine_40fps.mdl", "-3.5", "2.6", "12",
                      "0.36"); // proximity
    }
    // vanilla alkaline models
    else {
      InitWeaponCVars(i++, "progs/v_shot40fps.mdl", "1.5", "1.8", "15.8",
                      "0.33"); // shotgun
      InitWeaponCVars(i++, "progs/v_shot2_40fps.mdl", "-3", "1.7", "12",
                      "0.5"); // supershotgun
      InitWeaponCVars(i++, "progs/v_nail_alk40fps.mdl", "-6", "3.8", "17",
                      "0.38"); // nailgun
      InitWeaponCVars(i++, "progs/v_nail3.mdl", "-4", "3.5", "19",
                      "0.35"); // supernailgun
      InitWeaponCVars(i++, "progs/v_rock_40fps.mdl", "-3", "1.25", "17",
                      "0.5"); // grenade - same as AD grenade
      InitWeaponCVars(i++, "progs/v_rock2_40fps.mdl", "20", "8.3", "21",
                      "0.38"); // rocket
      InitWeaponCVars(i++, "progs/v_light.mdl", "-4", "3.1", "13",
                      "0.5"); // lightning
      InitWeaponCVars(i++, "progs/v_laserg40fps.mdl", "45", "3", "15",
                      "0.22"); // laser
      InitWeaponCVars(i++, "progs/v_mine_40fps.mdl", "-3", "1.3", "15",
                      "0.5"); // proximity
    }
  }
  // weapons for Slave Zero X: Episode Enyo mod
  else if (!strcmp(COM_SkipPath(com_gamedir), "enyo")) {
    InitWeaponCVars(i++, "progs/ee_v_sword.mdl", "25", "49", "60",
                    "0.2"); // Sword
    InitWeaponCVars(i++, "progs/ee_v_pistol.mdl", "12", "24", "29",
                    "0.2"); // Pistol
    InitWeaponCVars(i++, "progs/ee_v_sgun.mdl", "-2.3", "21.3", "35.3",
                    "0.2"); // Shotgun
    InitWeaponCVars(i++, "progs/ee_v_smgs.mdl", "3.5", "24.6", "29.8",
                    "0.2"); // SMGs
    InitWeaponCVars(i++, "progs/ee_v_plasma.mdl", "-1.5", "21.8", "36",
                    "0.2"); // Plasma Gun
    InitWeaponCVars(i++, "progs/ee_v_glaunch.mdl", "-3.8", "24", "35.5",
                    "0.2"); // Grenade Launcher
    InitWeaponCVars(i++, "progs/ee_v_rlaunch.mdl", "4", "28.5", "40.5",
                    "0.2"); // Rocket Launcher
    InitWeaponCVars(i++, "progs/ee_v_railgun.mdl", "-1", "22.5", "34.5",
                    "0.2"); // Railgun
    InitWeaponCVars(i++, "progs/ee_v_av72.mdl", "0.5", "24", "38.5",
                    "0.2"); // AV-72
    InitWeaponCVars(i++, "progs/ee_v_legal.mdl", "0", "55", "29",
                    "0.2"); // Legal Notice

  }
  else if (VR_IsDwellGame()) {
    InitWeaponCVars(i++, "progs/v_axe2.mdl", "-3.5", "34", "41.5",
                    "0.4"); // axe
    InitWeaponCVars(i++, "progs/v_axeb.mdl", "-4", "24", "37",
                    "0.4"); // axe
    InitWeaponCVars(i++, "progs/v_shot.mdl", "1.5", "1", "10",
                    "0.3333333"); // shotgun
    InitWeaponCVars(i++, "progs/v_shot2.mdl", "-3.5", "1", "8.5",
                    "0.5333333"); // double-barrel shotgun
    InitWeaponCVars(i++, "progs/v_shot3.mdl", "-3.5", "0.4", "8.5",
                    "0.5333333"); // rotary shotgun
    InitWeaponCVars(i++, "progs/v_nail.mdl", "-5", "3", "15",
                    "0.5"); // nailgun
    InitWeaponCVars(i++, "progs/v_nail2.mdl", "0", "3", "19",
                    "0.5"); // super nailgun
    InitWeaponCVars(i++, "progs/v_nail3.mdl", "-4", "3.5", "19",
                    "0.35"); // perforator
    InitWeaponCVars(i++, "progs/v_rock.mdl", "10", "1.5", "13",
                    "0.5"); // grenade launcher
    InitWeaponCVars(i++, "progs/v_rock2.mdl", "10", "7", "19",
                    "0.5"); // rocket launcher
    InitWeaponCVars(i++, "progs/v_light.mdl", "3", "4", "13",
                    "0.5"); // lightning gun
    InitWeaponCVars(i++, "progs/v_rail.mdl", "4", "5", "31",
                    "0.65"); // railstaff
    InitWeaponCVars(i++, "progs/v_rifle.mdl", "1.5", "1", "10",
                    "0.5"); // rifle
  }
  else {
    // weapons for vanilla Quake, Scourge of Armagon, Dissolution of Eternity

    // enhanced model conversion pack
    // (https://quakeone.com/forum/quake-mod-releases/finished-works/283295-osjc-s-enhanced-quake1-model-conversions-pack-v1)
    if (!strcmp(COM_SkipPath(com_gamedir), "enhanced") ||
        vr_gunmodeloffsets.value == VR_GUNMODELOFFSETS_ENHANCED) {
      // vanilla weapons
      InitWeaponCVars(i++, "progs/v_axe.mdl", "-4", "24", "37", "0.33"); // axe
      InitWeaponCVars(i++, "progs/v_shot.mdl", "1.5", "1", "10",
                      "0.5"); // shotgun
      InitWeaponCVars(i++, "progs/v_shot2.mdl", "-6", "0.3", "7",
                      "0.9"); // supershotgun
      InitWeaponCVars(i++, "progs/v_nail.mdl", "-1.9", "5.7", "15",
                      "0.4"); // nailgun
      InitWeaponCVars(i++, "progs/v_nail2.mdl", "5.5", "3.6", "19",
                      "0.4"); // supernailgun
      InitWeaponCVars(i++, "progs/v_rock.mdl", "10", "1.2", "13",
                      "0.5"); // grenade
      InitWeaponCVars(i++, "progs/v_rock2.mdl", "26", "4.5", "21",
                      "0.3"); // rocket
      InitWeaponCVars(i++, "progs/v_light.mdl", "12", "3", "13",
                      "0.5"); // lightning
      // hipnotic weapons
      InitWeaponCVars(i++, "progs/v_hammer.mdl", "-4", "17.5", "36",
                      "0.33"); // mjolnir hammer (vanilla model)
      InitWeaponCVars(i++, "progs/v_laserg.mdl", "65", "3.7", "15",
                      "0.33"); // laser (vanilla model)
      InitWeaponCVars(i++, "progs/v_prox.mdl", "10", "1.5", "13",
                      "0.5"); // proximity (vanilla model)
      // rogue weapons
      InitWeaponCVars(i++, "progs/v_lava.mdl", "-5", "3", "15",
                      "0.5"); // lava nailgun (vanilla model)
      InitWeaponCVars(i++, "progs/v_lava2.mdl", "0", "3", "19",
                      "0.5"); // lava supernailgun (vanilla model)
      InitWeaponCVars(i++, "progs/v_multi.mdl", "10", "1.5", "13",
                      "0.5"); // multigrenade (vanilla model)
      InitWeaponCVars(i++, "progs/v_multi2.mdl", "10", "7", "19",
                      "0.5"); // multirocket (vanilla model)
      InitWeaponCVars(i++, "progs/v_plasma.mdl", "3", "4", "13",
                      "0.5"); // plasma (vanilla model)
    }
    // authentic model improvements
    // (https://github.com/NightFright2k19/quake_authmdl)
    else if (vr_gunmodeloffsets.value == VR_GUNMODELOFFSETS_AUTHENTIC) {
      // vanilla weapons
      InitWeaponCVars(i++, "progs/v_axe.mdl", "-1", "24", "37", "0.33"); // axe
      InitWeaponCVars(i++, "progs/v_shot.mdl", "-1", "2", "15.3",
                      "0.33"); // shotgun
      InitWeaponCVars(i++, "progs/v_shot2.mdl", "-2", "2", "11.4",
                      "0.5"); // supershotgun
      InitWeaponCVars(i++, "progs/v_nail.mdl", "-7", "4", "15.7",
                      "0.4"); // nailgun
      InitWeaponCVars(i++, "progs/v_nail2.mdl", "-13.6", "4", "17.8",
                      "0.4"); // supernailgun
      InitWeaponCVars(i++, "progs/v_rock.mdl", "11", "2", "12.5",
                      "0.5"); // grenade
      InitWeaponCVars(i++, "progs/v_rock2.mdl", "23", "5", "31",
                      "0.3"); // rocket
      InitWeaponCVars(i++, "progs/v_light.mdl", "-6", "3.6", "11",
                      "0.5"); // lightning
      // hipnotic weapons
      InitWeaponCVars(i++, "progs/v_hammer.mdl", "-4", "17.5", "36",
                      "0.33"); // mjolnir hammer (vanilla model)
      InitWeaponCVars(i++, "progs/v_laserg.mdl", "65", "3.7", "15",
                      "0.33"); // laser (vanilla model)
      InitWeaponCVars(i++, "progs/v_prox.mdl", "-2.4", "1.8", "14.6",
                      "0.5"); // proximity
      // rogue weapons
      InitWeaponCVars(i++, "progs/v_lava.mdl", "-10.2", "4", "15.7",
                      "0.4"); // lava nailgun
      InitWeaponCVars(i++, "progs/v_lava2.mdl", "-13.6", "4", "17.8",
                      "0.4"); // lava supernailgun - same as supernailgun
      InitWeaponCVars(i++, "progs/v_multi.mdl", "11", "2", "12.5",
                      "0.5"); // multigrenade - same as grenade
      InitWeaponCVars(i++, "progs/v_multi2.mdl", "23", "5", "31",
                      "0.3"); // multirocket - same as rocket
      InitWeaponCVars(i++, "progs/v_plasma.mdl", "-6", "3.6", "11",
                      "0.5"); // plasma - same as lightning
    }
    // Plague's models
    // (https://github.com/gameflorist/quake-plague-weapons-vr/releases)
    else if (vr_gunmodeloffsets.value == VR_GUNMODELOFFSETS_PLAGUE) {
      // vanilla weapons
      InitWeaponCVars(i++, "progs/v_axe.mdl", "-4", "24", "37",
                      "0.33"); // axe (enhanced model)
      InitWeaponCVars(i++, "progs/v_shot.mdl", "-1", "1.3", "7",
                      "0.6"); // shotgun
      InitWeaponCVars(i++, "progs/v_shot2.mdl", "-5.9", "1.1", "8.5",
                      "0.6"); // supershotgun
      InitWeaponCVars(i++, "progs/v_nail.mdl", "-11", "5.1", "19",
                      "0.32"); // nailgun
      InitWeaponCVars(i++, "progs/v_nail2.mdl", "-11.6", "4.6", "21.8",
                      "0.26"); // supernailgun
      InitWeaponCVars(i++, "progs/v_rock.mdl", "-3.5", "2.6", "12",
                      "0.36"); // grenade
      InitWeaponCVars(i++, "progs/v_rock2.mdl", "-7.2", "4", "18.2",
                      "0.32"); // rocket
      InitWeaponCVars(i++, "progs/v_light.mdl", "-3.1", "4.4", "14.2",
                      "0.37"); // lightning
      // hipnotic weapons
      InitWeaponCVars(i++, "progs/v_hammer.mdl", "-4", "17.5", "36",
                      "0.33"); // mjolnir hammer (vanilla model)
      InitWeaponCVars(i++, "progs/v_laserg.mdl", "-5", "3.4", "22",
                      "0.33"); // laser
      InitWeaponCVars(i++, "progs/v_prox.mdl", "-3.5", "2.6", "12",
                      "0.36"); // proximity
      // rogue weapons
      InitWeaponCVars(i++, "progs/v_lava.mdl", "-11", "5.1", "19",
                      "0.32"); // lava nailgun - same as nailgun
      InitWeaponCVars(i++, "progs/v_lava2.mdl", "-11.6", "4.6", "21.8",
                      "0.26"); // lava supernailgun - same as supernailgun
      InitWeaponCVars(i++, "progs/v_multi.mdl", "-3.5", "2.6", "12",
                      "0.36"); // multigrenade - same as grenade
      InitWeaponCVars(i++, "progs/v_multi2.mdl", "-7.2", "4", "18.2",
                      "0.32"); // multirocket - same as rocket
      InitWeaponCVars(i++, "progs/v_plasma.mdl", "-3.1", "4.4", "14.2",
                      "0.37"); // plasma - same as lightning
    }
    // Block Quake models (https://kebby-quake.itch.io/block-quake)
    else if (vr_gunmodeloffsets.value == VR_GUNMODELOFFSETS_BLOCKQUAKE) {
      InitWeaponCVars(i++, "progs/v_axe.mdl", "-9", "38", "45", "0.2"); // axe
      InitWeaponCVars(i++, "progs/v_shot.mdl", "-7", "6.8", "35.5",
                      "0.2"); // shotgun
      InitWeaponCVars(i++, "progs/v_shot2.mdl", "-5.6", "10.2", "42",
                      "0.2"); // supershotgun
      InitWeaponCVars(i++, "progs/v_nail.mdl", "-9", "15", "40",
                      "0.2"); // nailgun
      InitWeaponCVars(i++, "progs/v_nail2.mdl", "-6", "13.5", "39",
                      "0.2"); // supernailgun
      InitWeaponCVars(i++, "progs/v_rock.mdl", "0", "11.8", "72",
                      "0.2"); // grenade
      InitWeaponCVars(i++, "progs/v_rock2.mdl", "26", "13.8", "69",
                      "0.2"); // rocket
      InitWeaponCVars(i++, "progs/v_light.mdl", "-9", "13.5", "51",
                      "0.2"); // lightning
    }
    // Vanilla models
    else {
      // vanilla weapons
      InitWeaponCVars(i++, "progs/v_axe.mdl", "-4", "24", "37", "0.33"); // axe
      InitWeaponCVars(i++, "progs/v_shot.mdl", "1.5", "1", "10",
                      "0.5"); // shotgun
      InitWeaponCVars(i++, "progs/v_shot2.mdl", "-3.5", "1", "8.5",
                      "0.8"); // supershotgun
      InitWeaponCVars(i++, "progs/v_nail.mdl", "-5", "3", "15",
                      "0.5"); // nailgun
      InitWeaponCVars(i++, "progs/v_nail2.mdl", "0", "3", "19",
                      "0.5"); // supernailgun
      InitWeaponCVars(i++, "progs/v_rock.mdl", "10", "1.5", "13",
                      "0.5"); // grenade
      InitWeaponCVars(i++, "progs/v_rock2.mdl", "10", "7", "19",
                      "0.5"); // rocket
      InitWeaponCVars(i++, "progs/v_light.mdl", "3", "4", "13",
                      "0.5"); // lightning
      // hipnotic weapons
      InitWeaponCVars(i++, "progs/v_hammer.mdl", "-4", "17.5", "36",
                      "0.33"); // mjolnir hammer
      InitWeaponCVars(i++, "progs/v_laserg.mdl", "65", "3.7", "15",
                      "0.33"); // laser
      InitWeaponCVars(i++, "progs/v_prox.mdl", "10", "1.5", "13",
                      "0.5"); // proximity - same as grenade
      // rogue weapons — offsets matching master branch (quakevr)
      InitWeaponCVars(i++, "progs/v_lava.mdl", "-5", "3", "15",
                      "0.5"); // lava nailgun (same as nailgun)
      InitWeaponCVars(i++, "progs/v_lava2.mdl", "0", "3", "19",
                      "0.5"); // lava supernailgun (same as supernailgun)
      InitWeaponCVars(i++, "progs/v_multi.mdl", "10", "1.5", "13",
                      "0.5"); // multigrenade (same as grenade)
      InitWeaponCVars(i++, "progs/v_multi2.mdl", "10", "7", "19",
                      "0.5"); // multirocket (same as rocket)
      InitWeaponCVars(i++, "progs/v_plasma.mdl", "3", "4", "13",
                      "0.5"); // plasma gun (same as lightning)
    }

    // axe from copper mod (used by many mods, including Underdark Overbright,
    // Spiritworld, Tainted, Tomb of Thunder, etc.)
    InitWeaponCVars(i++, "progs/v_axe2.mdl", "-3.5", "34", "41.5",
                    "0.33"); // axe

    // Tomb of Thunder
    if (!strcmp(COM_SkipPath(com_gamedir), "tombofthunder")) {
      InitWeaponCVars(i++, "progs/v_axe2.mdl", "-4", "34", "37", "0.33"); // axe
      InitWeaponCVars(i++, "progs/v_chain.mdl", "-4", "5", "15", "0.5");
      InitWeaponCVars(i++, "progs/v_hot.mdl", "4", "5", "31", "0.5");
      InitWeaponCVars(i++, "progs/v_rail.mdl", "4", "5", "31", "0.5");
      InitWeaponCVars(i++, "progs/v_shotcl.mdl", "4", "5", "31", "0.5");
    }
  }

  while (i < MAX_WEAPONS) {
    InitWeaponCVars(i++, "-1", "1.5", "1", "10", "0.5");
  }
}

// ----------------------------------------------------------------------------
// Public vars and functions

void VID_VR_Init() {
  // This is only called once at game start
  Cvar_RegisterVariable(&vr_enabled);
  Cvar_SetCallback(&vr_enabled, VR_Enabled_f);
  Cvar_RegisterVariable(&vr_weaponmenu_player_teleport);
  if (COM_CheckParm("-novr")) {
    return;
  }
  Cvar_RegisterVariable(&vr_aimmode);
  Cvar_RegisterVariable(&vr_crosshair_alpha);
  Cvar_RegisterVariable(&vr_crosshair_depth);
  Cvar_RegisterVariable(&vr_crosshair_size);
  Cvar_RegisterVariable(&vr_crosshair);
  Cvar_RegisterVariable(&vr_deadzone);
  Cvar_RegisterVariable(&vr_floor_offset);
  Cvar_RegisterVariable(&vr_gunangle);
  Cvar_RegisterVariable(&vr_gunmodeloffsets);
  Cvar_SetCallback(&vr_gunmodeloffsets, VR_Gunmodeloffsets_f);
  Cvar_RegisterVariable(&vr_gunmodelpitch);
  Cvar_RegisterVariable(&vr_gunmodelscale);
  Cvar_RegisterVariable(&vr_gunmodely);
  Cvar_RegisterVariable(&vr_crosshairy);
  Cvar_RegisterVariable(&vr_joystick_axis_deadzone);
  Cvar_RegisterVariable(&vr_joystick_axis_exponent);
  Cvar_RegisterVariable(&vr_joystick_deadzone_trunc);
  Cvar_RegisterVariable(&vr_joystick_yaw_multi);
  Cvar_RegisterVariable(&vr_haptic);
  Cvar_RegisterVariable(&vr_joystick_axis_menu_deadzone_extra);
  Cvar_RegisterVariable(&vr_lefthanded);
  Cvar_RegisterVariable(&vr_movement_mode);
  Cvar_RegisterVariable(&vr_movement_speed);
  Cvar_RegisterVariable(&vr_msaa);
  Cvar_RegisterVariable(&vr_highprecision_targets);
  Cvar_RegisterVariable(&vr_snap_turn);
  Cvar_RegisterVariable(&vr_180_snap_turn);
  Cvar_RegisterVariable(&vr_turn_speed);
  Cvar_RegisterVariable(&vr_world_scale);
  Cvar_RegisterVariable(&vr_projectilespawn_z_offset);
  Cvar_RegisterVariable(&vr_hud_scale);
  Cvar_RegisterVariable(&vr_menu_scale);
  // vr_movement_instant_stop is registered in SV_Init so it's available on
  // dedicated servers too; skip re-registration here.
  Cvar_SetCallback(&vr_deadzone, VR_Deadzone_f);

  InitAllWeaponCVars();
  Cmd_AddCommand("vradjustweapon", VR_AdjustWeapon_f);
  Cmd_AddCommand("vradjustmpweapon", VR_AdjustMPWeapon_f);
  Cmd_AddCommand("vradjustmuzzle", VR_AdjustMuzzle_f);
  Cmd_AddCommand("vradjustmpmuzzle", VR_AdjustMPMuzzle_f);
  Cmd_AddCommand("vrweaponoffsetglobal", VR_GlobalWeaponOffset_f);
  Cmd_AddCommand("vrglobalweaponoffset", VR_GlobalWeaponOffset_f);
  Cmd_AddCommand("vr_defaultbindings", VR_DefaultBindings_f);

  // Sickness stuff
  Cvar_RegisterVariable(&vr_viewkick);

  VR_Menu_Init();

  // Only enable VR if -vr was passed on the command line. Without this gate
  // the binary forces VR mode at every launch even when the user just wants
  // to run a desktop client (the SetQuick triggers VR_Enabled_f -> VR_Enable
  // which grabs the GL context via xrizer/OpenVR and produces a black window).
  if (COM_CheckParm("-vr"))
    Cvar_SetQuick(&vr_enabled, "1");
}

void VR_LoadWeaponSchema(void) {
  char *data;
  char *start;
  char key[64];
  float global_held_scale = 1.0f;
  vec3_t global_held_offset = {0, 0, 0};
  vec3_t global_muzzle_offset = {0, 0, 0};
  vec3_t global_mp_held_offset = {0, 0, 0};
  vec3_t global_mp_muzzle_offset = {0, 0, 0};
  qboolean has_global_held_scale = false;
  qboolean has_global_held_offset = false;
  qboolean has_global_muzzle_offset = false;
  qboolean has_global_mp_held_offset = false;
  qboolean has_global_mp_muzzle_offset = false;

  num_vr_weapons = 0;

  // Try to load vr_weapons.txt from the active search path. This is freed
  // below, so it must come from the zone allocator rather than temp hunk.
  data = (char *)COM_LoadZoneFile("vr_weapons.txt", NULL);
  if (!data) {
    DebugLog("VR: no vr_weapons.txt found for %s\n", com_gamedir);
    return;
  }

  start = data;
  while (1) {
    start = (char *)COM_Parse(start);
    if (!start || !com_token[0])
      break;

    if (!Q_strcmp(com_token, "{")) {
      if (num_vr_weapons >= MAX_VR_WEAPONS) {
        Con_Printf("VR: Too many weapons in vr_weapons.txt (max %d)\n",
                   MAX_VR_WEAPONS);
        break;
      }

      vr_weapon_cmd_t *w = &vr_weapons[num_vr_weapons];
      memset(w, 0, sizeof(*w));
      w->scale = 1.0f; // Default scale
      w->held_scale = 1.0f;
      w->owned_stat = -1;
      w->active_stat = -1;
      w->ammo_stat = -1;

      while (1) {
        start = (char *)COM_Parse(start);
        if (!start || !com_token[0] || !Q_strcmp(com_token, "}"))
          break;

        Q_strncpy(key, com_token, sizeof(key));
        start = (char *)COM_Parse(start); // Get value
        if (!start || !com_token[0])
          break;

        if (!Q_strcmp(key, "bitmask")) {
          w->bitmask = Q_atoi(com_token);
        } else if (!Q_strcmp(key, "model")) {
          Q_strncpy(w->model_path, com_token, sizeof(w->model_path));
        } else if (!Q_strcmp(key, "viewmodel") ||
                   !Q_strcmp(key, "held_model")) {
          Q_strncpy(w->viewmodel_path, com_token, sizeof(w->viewmodel_path));
        } else if (!Q_strcmp(key, "impulse")) {
          w->impulse = Q_atoi(com_token);
        } else if (!Q_strcmp(key, "scale")) {
          w->scale = Q_atof(com_token);
        } else if (!Q_strcmp(key, "offset")) {
          w->offset[0] = Q_atof(com_token);
          start = (char *)COM_Parse(start);
          w->offset[1] = Q_atof(com_token);
          start = (char *)COM_Parse(start);
          w->offset[2] = Q_atof(com_token);
          w->has_offset = true;
        } else if (!Q_strcmp(key, "held_scale")) {
          w->held_scale = Q_atof(com_token);
          w->has_held_scale = true;
        } else if (!Q_strcmp(key, "held_offset")) {
          w->held_offset[0] = Q_atof(com_token);
          start = (char *)COM_Parse(start);
          w->held_offset[1] = Q_atof(com_token);
          start = (char *)COM_Parse(start);
          w->held_offset[2] = Q_atof(com_token);
          w->has_held_offset = true;
        } else if (!Q_strcmp(key, "mp_held_offset")) {
          w->mp_held_offset[0] = Q_atof(com_token);
          start = (char *)COM_Parse(start);
          w->mp_held_offset[1] = Q_atof(com_token);
          start = (char *)COM_Parse(start);
          w->mp_held_offset[2] = Q_atof(com_token);
          VectorCopy(w->mp_held_offset, w->schema_mp_held_offset);
          w->has_mp_held_offset = true;
          w->has_schema_mp_held_offset = true;
        } else if (!Q_strcmp(key, "muzzle_offset")) {
          w->muzzle_offset[0] = Q_atof(com_token);
          start = (char *)COM_Parse(start);
          w->muzzle_offset[1] = Q_atof(com_token);
          start = (char *)COM_Parse(start);
          w->muzzle_offset[2] = Q_atof(com_token);
          w->has_muzzle_offset = true;
        } else if (!Q_strcmp(key, "mp_muzzle_offset")) {
          w->mp_muzzle_offset[0] = Q_atof(com_token);
          start = (char *)COM_Parse(start);
          w->mp_muzzle_offset[1] = Q_atof(com_token);
          start = (char *)COM_Parse(start);
          w->mp_muzzle_offset[2] = Q_atof(com_token);
          VectorCopy(w->mp_muzzle_offset, w->schema_mp_muzzle_offset);
          w->has_mp_muzzle_offset = true;
          w->has_schema_mp_muzzle_offset = true;
        } else if (!Q_strcmp(key, "muzzle_source_offset")) {
          w->muzzle_source_offset[0] = Q_atof(com_token);
          start = (char *)COM_Parse(start);
          w->muzzle_source_offset[1] = Q_atof(com_token);
          start = (char *)COM_Parse(start);
          w->muzzle_source_offset[2] = Q_atof(com_token);
          w->has_muzzle_source_offset = true;
        } else if (!Q_strcmp(key, "muzzle_source_viewofs")) {
          w->muzzle_source_viewofs = Q_atoi(com_token) != 0;
          w->has_muzzle_source_viewofs = true;
        } else if (!Q_strcmp(key, "spawn_at_self_origin") ||
                   !Q_strcmp(key, "muzzle_spawn_at_self_origin") ||
                   !Q_strcmp(key, "projectile_spawn_at_self_origin")) {
          w->spawn_at_self_origin = Q_atoi(com_token) != 0;
          w->has_spawn_at_self_origin = true;
        } else if (!Q_strcmp(key, "owned_stat")) {
          w->owned_stat = VR_ParseStatName(com_token, NULL);
        } else if (!Q_strcmp(key, "owned_mask")) {
          w->owned_mask = Q_atoi(com_token);
        } else if (!Q_strcmp(key, "active_stat")) {
          w->active_stat = VR_ParseStatName(com_token, NULL);
        } else if (!Q_strcmp(key, "active_mask")) {
          w->active_mask = Q_atoi(com_token);
        } else if (!Q_strcmp(key, "ammo")) {
          int default_max = 0;
          w->ammo_stat = VR_ParseStatName(com_token, &default_max);
          if (!w->ammo_max)
            w->ammo_max = default_max;
        } else if (!Q_strcmp(key, "ammo_stat")) {
          w->ammo_stat = VR_ParseStatName(com_token, NULL);
        } else if (!Q_strcmp(key, "ammo_max")) {
          w->ammo_max = Q_atoi(com_token);
        }
      }

      if (!w->has_held_scale && has_global_held_scale) {
        w->held_scale = global_held_scale;
        w->has_held_scale = true;
      }

      if (!w->has_held_offset && has_global_held_offset) {
        VectorCopy(global_held_offset, w->held_offset);
        w->has_held_offset = true;
      }

      if (!w->has_muzzle_offset && has_global_muzzle_offset) {
        VectorCopy(global_muzzle_offset, w->muzzle_offset);
        w->has_muzzle_offset = true;
      }

      if (has_global_mp_held_offset) {
        VectorAdd(w->mp_held_offset, global_mp_held_offset,
                  w->mp_held_offset);
        w->has_mp_held_offset = true;
      }

      if (has_global_mp_muzzle_offset) {
        VectorAdd(w->mp_muzzle_offset, global_mp_muzzle_offset,
                  w->mp_muzzle_offset);
        w->has_mp_muzzle_offset = true;
      }

      if (!w->viewmodel_path[0] &&
          (w->has_held_scale || w->has_held_offset ||
           w->has_mp_held_offset || w->has_muzzle_offset ||
           w->has_mp_muzzle_offset || w->has_muzzle_source_offset ||
           w->has_muzzle_source_viewofs || w->has_spawn_at_self_origin) &&
          w->model_path[0]) {
        Q_strncpy(w->viewmodel_path, w->model_path, sizeof(w->viewmodel_path));
      }

      if (!w->model_path[0] && w->viewmodel_path[0])
        Q_strncpy(w->model_path, w->viewmodel_path, sizeof(w->model_path));

      if (!w->bitmask && w->owned_stat < 0 && w->active_stat >= 0) {
        w->owned_stat = w->active_stat;
        w->owned_mask = w->active_mask;
      }

      if (!w->bitmask && w->owned_stat < 0 && w->active_stat < 0 &&
          !(w->viewmodel_path[0] &&
            (w->has_held_scale || w->has_held_offset ||
             w->has_mp_held_offset || w->has_muzzle_offset ||
             w->has_mp_muzzle_offset || w->has_muzzle_source_offset ||
             w->has_muzzle_source_viewofs || w->has_spawn_at_self_origin))) {
        Con_Printf("VR: Ignoring vr_weapons.txt entry without bitmask/stat ownership or held viewmodel\n");
        continue;
      }

      num_vr_weapons++;
    } else if (!Q_strcmp(com_token, "global_held_scale")) {
      start = (char *)COM_Parse(start);
      if (!start || !com_token[0])
        break;
      global_held_scale = Q_atof(com_token);
      has_global_held_scale = true;
    } else if (!Q_strcmp(com_token, "global_held_offset")) {
      start = (char *)COM_Parse(start);
      if (!start || !com_token[0])
        break;
      global_held_offset[0] = Q_atof(com_token);
      start = (char *)COM_Parse(start);
      global_held_offset[1] = Q_atof(com_token);
      start = (char *)COM_Parse(start);
      global_held_offset[2] = Q_atof(com_token);
      has_global_held_offset = true;
    } else if (!Q_strcmp(com_token, "global_muzzle_offset")) {
      start = (char *)COM_Parse(start);
      if (!start || !com_token[0])
        break;
      global_muzzle_offset[0] = Q_atof(com_token);
      start = (char *)COM_Parse(start);
      global_muzzle_offset[1] = Q_atof(com_token);
      start = (char *)COM_Parse(start);
      global_muzzle_offset[2] = Q_atof(com_token);
      has_global_muzzle_offset = true;
    } else if (!Q_strcmp(com_token, "global_mp_held_offset")) {
      start = (char *)COM_Parse(start);
      if (!start || !com_token[0])
        break;
      global_mp_held_offset[0] = Q_atof(com_token);
      start = (char *)COM_Parse(start);
      global_mp_held_offset[1] = Q_atof(com_token);
      start = (char *)COM_Parse(start);
      global_mp_held_offset[2] = Q_atof(com_token);
      has_global_mp_held_offset = true;
    } else if (!Q_strcmp(com_token, "global_mp_muzzle_offset")) {
      start = (char *)COM_Parse(start);
      if (!start || !com_token[0])
        break;
      global_mp_muzzle_offset[0] = Q_atof(com_token);
      start = (char *)COM_Parse(start);
      global_mp_muzzle_offset[1] = Q_atof(com_token);
      start = (char *)COM_Parse(start);
      global_mp_muzzle_offset[2] = Q_atof(com_token);
      has_global_mp_muzzle_offset = true;
    }
  }

  Z_Free(data);
  Con_Printf("VR: Loaded %d weapons from vr_weapons.txt\n", num_vr_weapons);

  // Precache models
  for (int i = 0; i < num_vr_weapons; i++) {
    vr_weapon_cmd_t *w = &vr_weapons[i];
    if (w->model_path[0])
      Mod_ForName(w->model_path, false);
    if (w->viewmodel_path[0] &&
        (w->has_held_scale || w->has_held_offset ||
         w->has_mp_held_offset)) {
      vec3_t held_offset = {0, 0, 0};
      if (w->has_held_offset)
        VectorCopy(w->held_offset, held_offset);
      VR_RegisterHeldWeaponOffset(w->viewmodel_path, held_offset,
                                  w->held_scale);
    }
    if (w->viewmodel_path[0] && w->has_muzzle_offset)
      VR_RegisterWeaponMuzzleOffset(w->viewmodel_path, w->muzzle_offset);
    if (w->viewmodel_path[0] && w->has_mp_held_offset)
      VR_RegisterWeaponMPHeldOffset(w->viewmodel_path, w->mp_held_offset,
                                    w->has_schema_mp_held_offset,
                                    w->schema_mp_held_offset);
    if (w->viewmodel_path[0] && w->has_mp_muzzle_offset)
      VR_RegisterWeaponMPMuzzleOffset(w->viewmodel_path,
                                      w->mp_muzzle_offset,
                                      w->has_schema_mp_muzzle_offset,
                                      w->schema_mp_muzzle_offset);
    if (w->viewmodel_path[0] &&
        (w->has_muzzle_source_offset || w->has_muzzle_source_viewofs))
      VR_RegisterWeaponMuzzleSource(w->viewmodel_path, w->muzzle_source_offset,
                                    w->has_muzzle_source_offset,
                                    w->muzzle_source_viewofs,
                                    w->has_muzzle_source_viewofs);
    if (w->viewmodel_path[0] && w->has_spawn_at_self_origin)
      VR_RegisterWeaponSpawnStyle(w->viewmodel_path,
                                  w->spawn_at_self_origin);
    if (w->bitmask || w->owned_stat >= 0 || w->active_stat >= 0)
      VR_AddOrUpdateDynWeapon(w->bitmask, w->impulse, w->model_path, 0, false,
                              w->scale, w->offset, w->has_offset,
                              w->owned_stat, w->owned_mask, w->active_stat,
                              w->active_mask, w->ammo_stat, w->ammo_max, true);
  }
}

// Per-game extra Z compensation applied on top of vr_projectilespawn_z_offset.
// Stored as a plain C float so config.cfg can never override it.
float vr_game_projectile_z_extra = 0.0f;

void VR_InitGame() {
  VR_ResetDynWeaponsToBase();
  InitAllWeaponCVars();
  VR_CreateDefaultWeaponSchemaIfMissing();
  VR_FillMissingDefaultWeaponSchemaBlocks();
  VR_LoadWeaponSchema();
  VR_RegisterDwellHeldWeaponDefaults();
  VR_AddBuiltinWeaponDefaults();
  lastWeaponHeader = NULL;
  weaponCVarEntry = -1;

  // Per-game extra Z offset — currently 0 for all games.
  // The base vr_projectilespawn_z_offset cvar (default 24) handles QuakeC's
  // '0 0 16' compensation for both local and remote paths.
  vr_game_projectile_z_extra = 0.0f;
}

qboolean VR_Enable() {
  if (vr_initialized) {
    return true;
  }
  vr::EVRInitError eInit = vr::VRInitError_None;
  ovrHMD = vr::VR_Init(&eInit, vr::VRApplication_Scene);

  if (eInit != vr::VRInitError_None) {
    Con_Printf("%s\nFailed to Initialize Steam VR",
               VR_GetVRInitErrorAsEnglishDescription(eInit));
    vr_enabled.value = 0;
    return false;
  }

  if (!InitOpenGLExtensions()) {
    Con_Printf("Failed to initialize OpenGL extensions");
    vr_enabled.value = 0;
    return false;
  }

  eyes[0].eye = vr::Eye_Left;
  eyes[1].eye = vr::Eye_Right;

  for (int i = 0; i < 2; i++) {
    uint32_t vrwidth;

    uint32_t vrheight;
    float LeftTan;

    float RightTan;

    float UpTan;

    float DownTan;

    ovrHMD->GetRecommendedRenderTargetSize(&vrwidth, &vrheight);
    ovrHMD->GetProjectionRaw(eyes[i].eye, &LeftTan, &RightTan, &UpTan,
                             &DownTan);

    eyes[i].index = i;
    eyes[i].position = {0.0f, 0.0f, 0.0f};
    eyes[i].orientation = {1.0f, 0.0f, 0.0f, 0.0f};
    eyes[i].fbo = CreateFBO(vrwidth, vrheight);
    eyes[i].fov_x = (atan(-LeftTan) + atan(RightTan)) / M_PI_DIV_180;
    eyes[i].fov_y = (atan(-UpTan) + atan(DownTan)) / M_PI_DIV_180;
  }

  current_eye = &eyes[1];

  vr::VRCompositor()->SetTrackingSpace(vr::TrackingUniverseStanding);
  VR_ResetOrientation(); // Recenter the HMD

  Cbuf_AddText("exec vr_autoexec.cfg\n"
               "vr_migrate_movement_defaults\n"
               "vr_defaultbindings\n"); // Load user VR settings, then ensure
                                         // core controller actions exist.

  attempt_to_refocus_retry =
      900; // Try to refocus our for the first 900 frames :/
  vr_initialized = true;
  return true;
}

void VR_PushYaw() { readbackYaw = true; }

void VID_VR_Shutdown() { VID_VR_Disable(); }

void VID_VR_Disable() {
  if (!vr_initialized) {
    return;
  }

  VR_ReleaseControllerInputs();
  VR_FreeControllerRenderModels();
  vr::VR_Shutdown();
  ovrHMD = NULL;

  // Reset the view height
  cl.viewheight = DEFAULT_VIEWHEIGHT;

  // TODO: Cleanup frame buffers

  vr_initialized = false;
}

static void RenderScreenForCurrentEye_OVR() {
  // Remember the current glwidht/height; we have to modify it here for each eye
  int oldglheight = glheight;
  int oldglwidth = glwidth;

  uint32_t cglwidth = glwidth;
  uint32_t cglheight = glheight;
  ovrHMD->GetRecommendedRenderTargetSize(&cglwidth, &cglheight);
  glwidth = cglwidth;
  glheight = cglheight;

  bool newTextures = glwidth != current_eye->fbo.size.width ||
                     glheight != current_eye->fbo.size.height ||
                     (!!vr_highprecision_targets.value) !=
                         current_eye->fbo.highprecision_request;
  if (newTextures) {
    RecreateTextures(&current_eye->fbo, glwidth, glheight);
  }

  if (newTextures || vr_msaa.value != current_eye->fbo.msaa) {
    CreateMSAA(&current_eye->fbo, glwidth, glheight, vr_msaa.value);
  }

  // Set up current FBO
  if (current_eye->fbo.msaa > 0) {
    glEnable(GL_MULTISAMPLE);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, current_eye->fbo.msaa_framebuffer);
  } else {
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, current_eye->fbo.framebuffer);
  }

  glViewport(0, 0, current_eye->fbo.size.width, current_eye->fbo.size.height);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Draw everything
  srand((int)(cl.time * 1000)); // sync random stuff between eyes

  r_refdef.fov_x = current_eye->fov_x;
  r_refdef.fov_y = current_eye->fov_y;

  SCR_UpdateScreenContent();

  // Generate the eye texture and send it to the HMD

  if (current_eye->fbo.msaa > 0) {
    glDisable(GL_MULTISAMPLE);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, current_eye->fbo.framebuffer);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER,
                         current_eye->fbo.msaa_framebuffer);
    glDrawBuffer(GL_BACK);
    glBlitFramebufferEXT(0, 0, glwidth, glheight, 0, 0, glwidth, glheight,
                         GL_COLOR_BUFFER_BIT, GL_NEAREST);
    GLSLGamma_GammaCorrect();
  } else {
    GLSLGamma_GammaCorrect();
  }

  vr::Texture_t eyeTexture = {
      reinterpret_cast<void *>(uintptr_t(current_eye->fbo.texture)),
      vr::TextureType_OpenGL, vr::ColorSpace_Gamma};
  vr::VRCompositor()->Submit(current_eye->eye, &eyeTexture);

  // Reset
  glwidth = oldglwidth;
  glheight = oldglheight;

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}

void VR_HandleGammaCorrect() {
  if (!vr_initialized || !glBindFramebufferEXT) {
    return;
  }
  if (current_eye) {
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, current_eye->fbo.framebuffer);
  } else {
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, eyes[0].fbo.framebuffer);
  }
  glReadBuffer(GL_FRONT);
}

void SetHandPos(int index, entity_t *player) {
  vec3_t headLocalPreRot;
  _VectorSubtract(controllers[index].position, headOrigin, headLocalPreRot);
  vec3_t headLocal;
  Vec3RotateZ(headLocalPreRot, vrYaw * M_PI_DIV_180, headLocal);
  _VectorAdd(headLocal, headOrigin, headLocal);

  cl.handpos[index][0] = -headLocal[0] + player->origin[0];
  cl.handpos[index][1] = -headLocal[1] + player->origin[1];
  cl.handpos[index][2] =
      headLocal[2] + player->origin[2] + vr_floor_offset.value;
}

void IdentifyAxes(int controllerIndex, int device);

static bool modelIdentified[2] = {false, false};
static bool isViveWand[2] = {false, false};
static bool isIndexController[2] = {false, false};

static qboolean VR_IsIndexControllerName(const char *name) {
  return name && (q_strcasestr(name, "index") || q_strcasestr(name, "knuckles")
                  || q_strcasestr(name, "knu"));
}

static qboolean VR_RightAltFireUsesTouchpad(void) {
  return isIndexController[1];
}

static void VR_IdentifyControllerModel(int controllerIndex,
                                       vr::TrackedDeviceIndex_t device) {
  char modelNumber[1024] = {0};
  char renderModel[1024] = {0};

  if (modelIdentified[controllerIndex])
    return;

  vr::VRSystem()->GetStringTrackedDeviceProperty(
      device, vr::Prop_ModelNumber_String, modelNumber, sizeof(modelNumber),
      nullptr);
  vr::VRSystem()->GetStringTrackedDeviceProperty(
      device, vr::Prop_RenderModelName_String, renderModel, sizeof(renderModel),
      nullptr);
  if (strstr(modelNumber, "Vive") || strstr(modelNumber, "VIVE")) {
    isViveWand[controllerIndex] = true;
  }
  if (VR_IsIndexControllerName(modelNumber) ||
      VR_IsIndexControllerName(renderModel)) {
    isIndexController[controllerIndex] = true;
  }
  modelIdentified[controllerIndex] = true;
}

static void VR_PollControllerInputOnly(int controllerIndex,
                                       vr::TrackedDeviceIndex_t device) {
  vr_controller *controller;

  if (device == vr::k_unTrackedDeviceIndexInvalid)
    return;

  controller = &controllers[controllerIndex];
  VR_IdentifyControllerModel(controllerIndex, device);
  IdentifyAxes(controllerIndex, device);

  controller->lastState = controller->state;
  if (!vr::VRSystem()->GetControllerState(device, &controller->state,
                                          sizeof(controller->state))) {
    return;
  }

  controller->seenThisFrame = true;
  controller->deviceIndex = device;
}

void VR_PollPoses() {
  if (vr_initialized) {
    vr::VRCompositor()->WaitGetPoses(ovr_DevicePose,
                                     vr::k_unMaxTrackedDeviceCount, nullptr, 0);
  }
}

void VR_UpdateScreenContent() {
  int i;
  vec3_t orientation;
  GLint w, h;

  if (!vr_enabled.value) {
    return;
  }

  VR_TrackWeapons();

  // Last chance to enable VR Mode - we get here when the game already start up
  // with vr_enabled 1 If enabling fails, unset the cvar and return.
  if (!vr_initialized && !VR_Enable()) {
    Cvar_Set("vr_enabled", "0");
    return;
  }

  w = glwidth;
  h = glheight;

  entity_t *player = &cl.entities[cl.viewentity];

  // Update poses
  vr::VRCompositor()->WaitGetPoses(ovr_DevicePose,
                                   vr::k_unMaxTrackedDeviceCount, nullptr, 0);

  controllers[0].seenThisFrame = false;
  controllers[1].seenThisFrame = false;
  controllers[0].deviceIndex = vr::k_unTrackedDeviceIndexInvalid;
  controllers[1].deviceIndex = vr::k_unTrackedDeviceIndexInvalid;

  // Get the VR devices' orientation and position
  for (uint32_t iDevice = 0; iDevice < vr::k_unMaxTrackedDeviceCount;
       iDevice++) {
    // HMD vectors update
    if (ovr_DevicePose[iDevice].bPoseIsValid &&
        ovrHMD->GetTrackedDeviceClass(iDevice) == vr::TrackedDeviceClass_HMD) {
      vr::HmdVector3_t headPos =
          Matrix34ToVector(ovr_DevicePose->mDeviceToAbsoluteTracking);
      headOrigin[0] = headPos.v[2];
      headOrigin[1] = headPos.v[0];
      headOrigin[2] = headPos.v[1];

      vec3_t moveInTracking;
      _VectorSubtract(headOrigin, lastHeadOrigin, moveInTracking);
      moveInTracking[0] *= -meters_to_units;
      moveInTracking[1] *= -meters_to_units;
      moveInTracking[2] = 0;
      Vec3RotateZ(moveInTracking, vrYaw * M_PI_DIV_180, vr_room_scale_move);

      _VectorCopy(headOrigin, lastHeadOrigin);
      _VectorSubtract(headOrigin, lastHeadOrigin, headOrigin);
      headPos.v[0] -= lastHeadOrigin[1];
      headPos.v[2] -= lastHeadOrigin[0];

      vr::HmdQuaternion_t headQuat =
          Matrix34ToQuaternion(ovr_DevicePose->mDeviceToAbsoluteTracking);
      vr::HmdVector3_t leyePos =
          Matrix34ToVector(ovrHMD->GetEyeToHeadTransform(eyes[0].eye));
      vr::HmdVector3_t reyePos =
          Matrix34ToVector(ovrHMD->GetEyeToHeadTransform(eyes[1].eye));

      leyePos = RotateVectorByQuaternion(leyePos, headQuat);
      reyePos = RotateVectorByQuaternion(reyePos, headQuat);

      HmdVec3RotateY(&headPos, -vrYaw * M_PI_DIV_180);

      HmdVec3RotateY(&leyePos, -vrYaw * M_PI_DIV_180);
      HmdVec3RotateY(&reyePos, -vrYaw * M_PI_DIV_180);

      eyes[0].position = AddVectors(headPos, leyePos);
      eyes[1].position = AddVectors(headPos, reyePos);
      eyes[0].orientation = headQuat;
      eyes[1].orientation = headQuat;
    }
    // Controller vectors update
    else if (ovr_DevicePose[iDevice].bPoseIsValid &&
             ovrHMD->GetTrackedDeviceClass(iDevice) ==
                 vr::TrackedDeviceClass_Controller) {
      vr::HmdVector3_t rawControllerPos =
          Matrix34ToVector(ovr_DevicePose[iDevice].mDeviceToAbsoluteTracking);
      vr::HmdQuaternion_t rawControllerQuat = Matrix34ToQuaternion(
          ovr_DevicePose[iDevice].mDeviceToAbsoluteTracking);
      vr::HmdVector3_t rawControllerVel = ovr_DevicePose[iDevice].vVelocity;

      int controllerIndex = -1;

      if (ovrHMD->GetControllerRoleForTrackedDeviceIndex(iDevice) ==
          vr::TrackedControllerRole_LeftHand) {
        // Swap controller values for our southpaw players
        controllerIndex = vr_lefthanded.value ? 1 : 0;
      } else if (ovrHMD->GetControllerRoleForTrackedDeviceIndex(iDevice) ==
                 vr::TrackedControllerRole_RightHand) {
        // Swap controller values for our southpaw players
        controllerIndex = vr_lefthanded.value ? 0 : 1;
      }

      if (controllerIndex != -1) {
        vr_controller *controller = &controllers[controllerIndex];

        VR_IdentifyControllerModel(controllerIndex, iDevice);

        IdentifyAxes(controllerIndex, iDevice);

        controller->lastState = controller->state;
        if (!vr::VRSystem()->GetControllerState(iDevice, &controller->state,
                                                sizeof(controller->state))) {
          continue;
        }

        controller->seenThisFrame = true;
        controller->deviceIndex = iDevice;
        controller->rawvector = rawControllerPos;
        controller->raworientation = rawControllerQuat;
        controller->position[0] =
            (rawControllerPos.v[2] - lastHeadOrigin[0]) * meters_to_units;
        controller->position[1] =
            (rawControllerPos.v[0] - lastHeadOrigin[1]) * meters_to_units;
        controller->position[2] = (rawControllerPos.v[1]) * meters_to_units;
        QuatToYawPitchRoll(rawControllerQuat, controller->orientation);
      }
    }
  }

  if (!controllers[0].seenThisFrame) {
    vr::TrackedDeviceIndex_t device =
        ovrHMD->GetTrackedDeviceIndexForControllerRole(
            vr_lefthanded.value ? vr::TrackedControllerRole_RightHand
                                : vr::TrackedControllerRole_LeftHand);
    VR_PollControllerInputOnly(0, device);
  }
  if (!controllers[1].seenThisFrame) {
    vr::TrackedDeviceIndex_t device =
        ovrHMD->GetTrackedDeviceIndexForControllerRole(
            vr_lefthanded.value ? vr::TrackedControllerRole_LeftHand
                                : vr::TrackedControllerRole_RightHand);
    VR_PollControllerInputOnly(1, device);
  }

  if (!controllers[0].seenThisFrame)
    VR_SetTrigger(&controllers[0], K_LTRIGGER, false);
  if (!controllers[1].seenThisFrame)
    VR_SetTrigger(&controllers[1], K_RTRIGGER, false);

  // Reset the aim roll value before calculation, incase the user switches
  // aimmode from 7 to another.
  cl.aimangles[ROLL] = 0.0;

  QuatToYawPitchRoll(eyes[1].orientation, orientation);
  if (readbackYaw) {
    vrYaw = cl.viewangles[YAW] - (orientation[YAW] - vrYaw);
    readbackYaw = false;
  }

  switch ((int)vr_aimmode.value) {
    // 1: (Default) Head Aiming; View YAW is mouse+head, PITCH is head
  default:
  case VR_AIMMODE_HEAD_MYAW:
    cl.viewangles[PITCH] = cl.aimangles[PITCH] = orientation[PITCH];
    cl.aimangles[YAW] = cl.viewangles[YAW] =
        cl.aimangles[YAW] + orientation[YAW] - lastOrientation[YAW];
    break;

    // 2: Head Aiming; View YAW and PITCH is mouse+head (this is stupid)
  case VR_AIMMODE_HEAD_MYAW_MPITCH:
    cl.viewangles[PITCH] = cl.aimangles[PITCH] =
        cl.aimangles[PITCH] + orientation[PITCH] - lastOrientation[PITCH];
    cl.aimangles[YAW] = cl.viewangles[YAW] =
        cl.aimangles[YAW] + orientation[YAW] - lastOrientation[YAW];
    break;

    // 3: Mouse Aiming; View YAW is mouse+head, PITCH is head
  case VR_AIMMODE_MOUSE_MYAW:
    cl.viewangles[PITCH] = orientation[PITCH];
    cl.viewangles[YAW] = cl.aimangles[YAW] + orientation[YAW];
    break;

    // 4: Mouse Aiming; View YAW and PITCH is mouse+head
  case VR_AIMMODE_MOUSE_MYAW_MPITCH:
    cl.viewangles[PITCH] = cl.aimangles[PITCH] + orientation[PITCH];
    cl.viewangles[YAW] = cl.aimangles[YAW] + orientation[YAW];
    break;

  case VR_AIMMODE_BLENDED:
  case VR_AIMMODE_BLENDED_NOPITCH: {
    float diffHMDYaw = orientation[YAW] - lastOrientation[YAW];
    float diffHMDPitch = orientation[PITCH] - lastOrientation[PITCH];
    float diffAimYaw = cl.aimangles[YAW] - lastAim[YAW];
    float diffYaw;

    // find new view position based on orientation delta
    cl.viewangles[YAW] += diffHMDYaw;

    // find difference between view and aim yaw
    diffYaw = cl.viewangles[YAW] - cl.aimangles[YAW];

    if (fabs(diffYaw) > vr_deadzone.value / 2.0f) {
      // apply the difference from each set of angles to the other
      cl.aimangles[YAW] += diffHMDYaw;
      cl.viewangles[YAW] += diffAimYaw;
    }
    if ((int)vr_aimmode.value == VR_AIMMODE_BLENDED) {
      cl.aimangles[PITCH] += diffHMDPitch;
    }
    cl.viewangles[PITCH] = orientation[PITCH];
  } break;

  // 7: Controller Aiming;
  case VR_AIMMODE_CONTROLLER:
    cl.viewangles[PITCH] = orientation[PITCH];
    cl.viewangles[YAW] = orientation[YAW];

    vec3_t contMat[3], gunMat[3];
    CreateRotMat(0, vr_gunangle.value, gunMat);

    for (int i = 0; i < 2; i++) {
      RotMatFromAngleVector(controllers[i].orientation, contMat);

      vec3_t mat[3];
      R_ConcatRotations(gunMat, contMat, mat);

      AngleVectorFromRotMat(mat, cl.handrot[i]);
    }

    VR_ApplyCurrentViewWeaponTransform();

    VectorCopy(cl.handrot[1], cl.aimangles); // Sets the shooting angle

    break;
  }

  SetHandPos(0, player);
  SetHandPos(1, player);
  VR_AdjustWeaponUpdatePose();

  cl.viewangles[ROLL] = orientation[ROLL];

  VectorCopy(orientation, lastOrientation);
  VectorCopy(cl.aimangles, lastAim);

  VectorCopy(cl.viewangles, r_refdef.viewangles);
  VectorCopy(cl.aimangles, r_refdef.aimangles);

  // Render the scene for each eye into their FBOs
  R_BeginVRFrame();
  for (int i = 0; i < 2; i++) {
    R_SetVREye(i, 2);
    current_eye = &eyes[i];

    vec3_t temp, orientation;

    // We need to scale the view offset position to quake units and rotate it by
    // the current input angles (viewangle - eye orientation)
    QuatToYawPitchRoll(current_eye->orientation, orientation);
    temp[0] = -current_eye->position.v[2] * meters_to_units; // X
    temp[1] = -current_eye->position.v[0] * meters_to_units; // Y
    temp[2] = current_eye->position.v[1] * meters_to_units;  // Z
    Vec3RotateZ(temp,
                (r_refdef.viewangles[YAW] - orientation[YAW]) * M_PI_DIV_180,
                vr_viewOffset);
    vr_viewOffset[2] += vr_floor_offset.value;

    RenderScreenForCurrentEye_OVR();
  }
  R_EndVRFrame();

  // Blit mirror texture to backbuffer
  glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, eyes[0].fbo.framebuffer);
  glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, 0);
  glBlitFramebufferEXT(0, eyes[0].fbo.size.width, eyes[0].fbo.size.height, 0, 0,
                       h, w, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR);
  glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, 0);
}

void VR_SetMatrices() {
  vr::HmdMatrix44_t projection;

  // Calculate HMD projection matrix and view offset position
  projection = TransposeMatrix(
      ovrHMD->GetProjectionMatrix(current_eye->eye, 4.f, gl_farclip.value));

  // Set OpenGL projection and view matrices
  glMatrixMode(GL_PROJECTION);
  glLoadMatrixf((GLfloat *)projection.m);
}

void VR_AddOrientationToViewAngles(vec3_t angles) {
  vec3_t orientation;
  QuatToYawPitchRoll(current_eye->orientation, orientation);

  angles[PITCH] = angles[PITCH] + orientation[PITCH];
  angles[YAW] = angles[YAW] + orientation[YAW];
  angles[ROLL] = orientation[ROLL];
}

void VR_ShowCrosshair() {
  vec3_t forward, up, right;
  vec3_t start, end, impact;
  float size, alpha, pixel_size;

  // leads to exception in multiplayer
  /*     if((int)(sv_player->v.weapon) == IT_AXE)
      {
          return;
      } */

  size = CLAMP(0.0f, vr_crosshair_size.value, 32.0f);
  alpha = CLAMP(0.0f, vr_crosshair_alpha.value, 1.0f);
  pixel_size = size * glwidth / vid.width;
  if (VR_IsDwellGame())
    pixel_size *= 2.0f;
  if (vr_enabled.value)
    pixel_size = q_max(pixel_size, VR_IsDwellGame() ? 8.0f : 4.0f);

  if (size <= 0 || alpha <= 0) {
    return;
  }

  // setup gl
  glDisable(GL_DEPTH_TEST);
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  GL_PolygonOffset(OFFSET_SHOWTRIS);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_CULL_FACE);

  // calc the line and draw
  // TODO: Make the laser align correctly
  if (vr_aimmode.value == VR_AIMMODE_CONTROLLER) {
    VR_GetMuzzleAdjustedHandPos(start);
    AngleVectors(cl.handrot[1], forward, right, up);
    if (weaponCVarEntry >= 0 && !VR_IsMuzzleAdjustMode()) {
      vec3_t source_comp;

      if (VR_GetMuzzleSourceCompensation(weaponCVarEntry, cl.handrot[1],
                                         source_comp))
        VectorAdd(start, source_comp, start);
    }
  } else {
    VectorCopy(cl.viewent.origin, start);
    start[2] -= cl.viewheight - 10;
    AngleVectors(cl.aimangles, forward, right, up);
  }

  switch ((int)vr_crosshair.value) {
  default:
  case VR_CROSSHAIR_POINT:
    if (vr_crosshair_depth.value <= 0) {
      // trace to first wall
      VectorMA(start, 4096, forward, end);

      end[2] += vr_crosshairy.value;
      TraceLine(start, end, impact);
    } else {
      // fix crosshair to specific depth
      VectorMA(start, vr_crosshair_depth.value * meters_to_units, forward,
               impact);
    }

    glEnable(GL_POINT_SMOOTH);
    glColor4f(1, 0, 0, alpha);
    glPointSize(pixel_size);

    glBegin(GL_POINTS);
    glVertex3f(impact[0], impact[1], impact[2]);
    glEnd();
    glDisable(GL_POINT_SMOOTH);
    break;

  case VR_CROSSHAIR_LINE:
    // trace to first entity
    VectorMA(start, 4096, forward, end);
    TraceLine(start, end, impact);

    glColor4f(1, 0, 0, alpha);
    glLineWidth(pixel_size);
    glBegin(GL_LINES);
    impact[2] += vr_crosshairy.value * 10.f;
    glVertex3f(start[0], start[1], start[2]);
    glVertex3f(impact[0], impact[1], impact[2]);
    glEnd();
    break;
  }

  // cleanup gl
  glColor3f(1, 1, 1);
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  GL_PolygonOffset(OFFSET_NONE);
  glEnable(GL_DEPTH_TEST);
}

static double VR_Lerp(double a, double b, double f) {
  return (a * (1.0 - f)) + (b * f);
}

void vec3lerp(vec3_t out, vec3_t start, vec3_t end, double f) {
  out[0] = VR_Lerp(start[0], end[0], f);
  out[1] = VR_Lerp(start[1], end[1], f);
  out[2] = VR_Lerp(start[2], end[2], f);
}

void VR_Draw2D() {
  qboolean draw_sbar = false;
  vec3_t menu_angles, forward, right, up, target, smoothedTarget;
  float scale_hud = vr_menu_scale.value;

  int oldglwidth = glwidth, oldglheight = glheight, oldconwidth = vid.conwidth,
      oldconheight = vid.conheight;

  glwidth = 320;
  glheight = 200;

  vid.conwidth = 320;
  vid.conheight = 200;

  // draw 2d elements 1m from the users face, centered
  glPushMatrix();
  glDisable(GL_DEPTH_TEST); // prevents drawing sprites on sprites from
                            // interferring with one another
  glEnable(GL_BLEND);

  // TODO: Make the menus' position sperate from the right hand. Centered on
  // last view dir?
  VectorCopy(cl.viewangles, menu_angles);

  if (vr_aimmode.value == VR_AIMMODE_HEAD_MYAW ||
      vr_aimmode.value == VR_AIMMODE_HEAD_MYAW_MPITCH)
    menu_angles[PITCH] = 0;

  AngleVectors(menu_angles, forward, right, up);

  VectorMA(r_refdef.vieworg, 48, forward, target);
  vec3lerp(smoothedTarget, lastMenuPosition, target, 0.2);
  VectorCopy(smoothedTarget, lastMenuPosition);

  /* Local +X is screen right, local +Y is screen down, local +Z faces out. */
  VectorCopy(smoothedTarget, vr_menu_surface.center);
  VectorCopy(right, vr_menu_surface.right);
  VectorScale(up, -1.0f, vr_menu_surface.down);
  VectorCopy(forward, vr_menu_surface.normal);
  vr_menu_surface.scale = scale_hud;
  vr_menu_surface.valid = scale_hud > 0.0001f;
  glTranslatef(smoothedTarget[0], smoothedTarget[1], smoothedTarget[2]);

  glRotatef(menu_angles[YAW] - 90, 0, 0, 1); // rotate around z
  glRotatef(90 + menu_angles[PITCH], -1, 0,
            0); // keep bar at constant angled pitch towards user
  glTranslatef(-(320.0 * scale_hud / 2), -(200.0 * scale_hud / 2),
               0); // center the status bar
  glScalef(scale_hud, scale_hud, scale_hud);

  if (scr_drawdialog) // new game confirm
  {
    if (con_forcedup) {
      Draw_ConsoleBackground();
    } else {
      draw_sbar = true; // Sbar_Draw ();
    }
    Draw_FadeScreen();
    SCR_DrawNotifyString();
  } else if (scr_drawloading) // loading
  {
    SCR_DrawLoading();
    draw_sbar = true;                                      // Sbar_Draw ();
  } else if (cl.intermission == 1 && key_dest == key_game) // end of level
  {
    Sbar_IntermissionOverlay();
  } else if (cl.intermission == 2 && key_dest == key_game) // end of episode
  {
    Sbar_FinaleOverlay();
    SCR_CheckDrawCenterString();
  } else {
    // SCR_DrawCrosshair (); //johnfitz
    SCR_DrawNet();
    SCR_DrawTurtle();
    SCR_DrawPause();
    SCR_CheckDrawCenterString();
    draw_sbar = true;   // Sbar_Draw ();
    SCR_DrawDevStats(); // johnfitz
    SCR_DrawFPS();      // johnfitz
    SCR_DrawClock();    // johnfitz
    SCR_DrawConsole();
    M_Draw();
  }

  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glPopMatrix();

  if (draw_sbar) {
    VR_DrawSbar();
  }

  glwidth = oldglwidth;
  glheight = oldglheight;
  vid.conwidth = oldconwidth;
  vid.conheight = oldconheight;
}

void VR_DrawSbar() {
  vec3_t sbar_angles, forward, right, up, target;
  float scale_hud = vr_hud_scale.value;

  glPushMatrix();
  glDisable(GL_DEPTH_TEST); // prevents drawing sprites on sprites from
                            // interferring with one another

  if (vr_aimmode.value == VR_AIMMODE_CONTROLLER) {
    AngleVectors(cl.handrot[1], forward, right, up);

    VectorCopy(cl.handrot[1], sbar_angles)

        AngleVectors(sbar_angles, forward, right, up);

    VectorMA(cl.handpos[1], -5, right, target);
  } else {
    VectorCopy(cl.aimangles, sbar_angles)

        if (vr_aimmode.value == VR_AIMMODE_HEAD_MYAW ||
            vr_aimmode.value == VR_AIMMODE_HEAD_MYAW_MPITCH)
            sbar_angles[PITCH] = 0;

    AngleVectors(sbar_angles, forward, right, up);

    VectorMA(cl.viewent.origin, 1.0, forward, target);
  }

  vec3_t smoothedTarget;
  vec3lerp(smoothedTarget, lastHudPosition, target, 1.0);
  VectorCopy(smoothedTarget, lastHudPosition);

  glTranslatef(smoothedTarget[0], smoothedTarget[1], smoothedTarget[2]);

  glRotatef(sbar_angles[YAW] - 90, 0, 0, 1); // rotate around z
  glRotatef(90 + 45 + sbar_angles[PITCH], -1, 0,
            0); // keep bar at constant angled pitch towards user
  glTranslatef(-(320.0 * scale_hud / 2), 0, 0); // center the status bar
  glTranslatef(0, 0, 10);                       // move hud down a bit
  glScalef(scale_hud, scale_hud, scale_hud);

  Sbar_Draw();

  glEnable(GL_DEPTH_TEST);
  glPopMatrix();
}

void VR_SetAngles(vec3_t angles) {
  VectorCopy(angles, cl.aimangles);
  VectorCopy(angles, cl.viewangles);
  VectorCopy(angles, lastAim);
}

void VR_ResetOrientation() {
  cl.aimangles[YAW] = cl.viewangles[YAW];
  cl.aimangles[PITCH] = cl.viewangles[PITCH];
  if (vr_enabled.value) {
    // IVRSystem_ResetSeatedZeroPose(ovrHMD);
    VectorCopy(cl.aimangles, lastAim);
  }
}

typedef struct {
  int trackpad;
  int joystick;
  int trigger;
  int grip;
  qboolean identified;
} vr_controller_axes_t;

static vr_controller_axes_t controllerAxes[2] = {
    {-1, -1, -1, -1, false},
    {-1, -1, -1, -1, false},
};

static int VR_ControllerIndex(const vr_controller *controller) {
  return controller == &controllers[1] ? 1 : 0;
}

void IdentifyAxes(int controllerIndex, int device) {
  vr_controller_axes_t *axes = &controllerAxes[controllerIndex];

  if (axes->identified && axes->trigger != -1 &&
      (axes->joystick != -1 || axes->trackpad != -1)) {
    return;
  }

  for (uint32_t i = 0; i < vr::k_unControllerStateAxisCount; i++) {
    switch (vr::VRSystem()->GetInt32TrackedDeviceProperty(
        device, (vr::ETrackedDeviceProperty)(vr::Prop_Axis0Type_Int32 + i),
        nullptr)) {
    case vr::k_eControllerAxis_TrackPad:
      if (axes->trackpad == -1) {
        axes->trackpad = i;
      }
      break;
    case vr::k_eControllerAxis_Joystick:
      if (axes->joystick == -1) {
        axes->joystick = i;
      }
      break;
    case vr::k_eControllerAxis_Trigger:
      if (axes->trigger == -1) {
        axes->trigger = i;
      } else if (axes->grip == -1) {
        axes->grip = i;
      }
      break;
    }
  }

  axes->identified = true;
}

static qboolean VR_AddAxisCandidate(int *axes, int *count, int axis) {
  if (axis < 0 || axis >= (int)vr::k_unControllerStateAxisCount) {
    return false;
  }

  for (int i = 0; i < *count; i++) {
    if (axes[i] == axis) {
      return false;
    }
  }

  axes[(*count)++] = axis;
  return true;
}

static float VR_ReadStickAxis(vr::VRControllerState_t *state,
                              int controllerIndex, int axis) {
  vr_controller_axes_t *axes = &controllerAxes[controllerIndex];
  int candidates[4];
  int candidateCount = 0;
  float best = 0.0f;

  if (axes->joystick != -1) {
    VR_AddAxisCandidate(candidates, &candidateCount, axes->joystick);
  }

  if (isIndexController[controllerIndex]) {
    VR_AddAxisCandidate(candidates, &candidateCount, 3);
  }

  if (axes->trackpad != -1) {
    VR_AddAxisCandidate(candidates, &candidateCount, axes->trackpad);
  }

  VR_AddAxisCandidate(candidates, &candidateCount, 0);

  for (int i = 0; i < candidateCount; i++) {
    float value = axis == 0 ? state->rAxis[candidates[i]].x
                            : state->rAxis[candidates[i]].y;
    if (fabsf(value) > fabsf(best)) {
      best = value;
    }
  }

  return best;
}

static float VR_ReadTrigger(vr_controller *controller, int controllerIndex) {
  vr_controller_axes_t *axes = &controllerAxes[controllerIndex];
  float triggerValue = 0.0f;

  if (axes->trigger != -1) {
    triggerValue = controller->state.rAxis[axes->trigger].x;
  }

  if (fabsf(controller->state.rAxis[1].x) > fabsf(triggerValue)) {
    triggerValue = controller->state.rAxis[1].x;
  }

  if (controller->state.ulButtonPressed &
      vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)) {
    triggerValue = 1.0f;
  }

  return triggerValue;
}

float GetAxis(vr::VRControllerState_t *state, int controllerIndex, int axis,
              double deadzoneExtra) {
  float v = VR_ReadStickAxis(state, controllerIndex, axis);

  int sign = (v > 0) - (v < 0);
  v = fabsf(v);

  if (v < vr_joystick_axis_deadzone.value + deadzoneExtra) {
    return 0.0f;
  }

  if (vr_joystick_deadzone_trunc.value == 0) {
    v = (v - vr_joystick_axis_deadzone.value) /
        (1 - vr_joystick_axis_deadzone.value);
  }

  if (vr_joystick_axis_exponent.value >= 0) {
    v = powf(v, vr_joystick_axis_exponent.value);
  }

  return sign * v;
}

static qboolean VR_ShouldUseMovementViewAngles(void) {
  return vr_enabled.value && vr_initialized &&
         (int)vr_aimmode.value == VR_AIMMODE_CONTROLLER &&
         (int)vr_movement_mode.value != VR_MOVEMENT_MODE_RAW_INPUT;
}

static void VR_GetMovementViewAngles(vec3_t angles) {
  if ((int)vr_movement_mode.value == VR_MOVEMENT_MODE_FOLLOW_HAND) {
    VectorCopy(cl.handrot[0], angles);
  } else {
    QuatToYawPitchRoll((current_eye ? current_eye : &eyes[1])->orientation,
                       angles);
  }

  angles[ROLL] = 0.0f;
}

void VR_UpdateCommandViewAngles(usercmd_t *cmd) {
  if (!cmd || !VR_ShouldUseMovementViewAngles())
    return;

  VR_GetMovementViewAngles(cmd->viewangles);
}

void DoKey(vr_controller *controller, vr::EVRButtonId vrButton, int quakeKey) {
  uint64_t mask = vr::ButtonMaskFromId(vrButton);
  bool wasDown = (controller->emittedButtonPressed & mask) != 0;
  bool isDown =
      (controller->state.ulButtonPressed & mask) != 0;
  if (isDown != wasDown) {
    if (VR_InputDebugEnabled()) {
      DebugLog("VR_KEY hand=%d vrbutton=%d quakekey=%s down=%d pressed=0x%llx touched=0x%llx\n",
               VR_ControllerIndex(controller), (int)vrButton,
               Key_KeynumToString(quakeKey), isDown ? 1 : 0,
               (unsigned long long)controller->state.ulButtonPressed,
               (unsigned long long)controller->state.ulButtonTouched);
    }
    if (isDown)
      controller->emittedButtonPressed |= mask;
    else
      controller->emittedButtonPressed &= ~mask;
    Key_Event(quakeKey, isDown);
  }
}

void DoGrip(vr_controller *controller, int quakeKey) {
  // Always use digital grip button for reliable input on all controllers
  DoKey(controller, vr::k_EButton_Grip, quakeKey);
}
void DoTrigger(vr_controller *controller, int quakeKey) {
  const float triggerDownThreshold = 0.55f;
  const float triggerUpThreshold = 0.45f;
  int controllerIndex = VR_ControllerIndex(controller);

  if (!controller->seenThisFrame) {
    VR_SetTrigger(controller, quakeKey, false);
    return;
  }

  float triggerValue = VR_ReadTrigger(controller, controllerIndex);

  if (!controller->triggerDown && triggerValue > triggerDownThreshold) {
    if (quakeKey == K_RTRIGGER && VR_AdjustWeaponConsumeTrigger()) {
      controller->triggerDown = true;
      vr_adjust_suppressed_rtrigger = true;
      return;
    }
    if (VR_InputDebugEnabled()) {
      DebugLog("VR_TRIGGER hand=%d quakekey=%s down=1 value=%.3f\n",
               controllerIndex, Key_KeynumToString(quakeKey), triggerValue);
    }
    VR_SetTrigger(controller, quakeKey, true);
  } else if (controller->triggerDown && triggerValue < triggerUpThreshold) {
    if (quakeKey == K_RTRIGGER && vr_adjust_suppressed_rtrigger) {
      controller->triggerDown = false;
      vr_adjust_suppressed_rtrigger = false;
      return;
    }
    if (VR_InputDebugEnabled()) {
      DebugLog("VR_TRIGGER hand=%d quakekey=%s down=0 value=%.3f\n",
               controllerIndex, Key_KeynumToString(quakeKey), triggerValue);
    }
    VR_SetTrigger(controller, quakeKey, false);
  }
}

/*
 * Keep menu trigger edges separate from Key_Event.  A held attack that opens a
 * menu is released safely. A new trigger press prefers the current VR pointer
 * hit; when the ray is off the panel it retains the established selected-item
 * behavior. Neither path can leak into a gameplay attack.
 */
static void VR_DoMenuTrigger(vr_controller *controller) {
  const float triggerDownThreshold = 0.55f;
  const float triggerUpThreshold = 0.45f;
  int controllerIndex = VR_ControllerIndex(controller);
  float triggerValue;

  /* A binding screen must receive the physical controller trigger, not Enter. */
  if (M_BindGrabActive()) {
    if (!controller->seenThisFrame) {
      if (controller->triggerDown && controller->triggerKey)
        VR_SetTrigger(controller, controller->triggerKey, false);
      controller->triggerDown = false;
      controller->triggerKey = 0;
      return;
    }

    triggerValue = VR_ReadTrigger(controller, controllerIndex);
    if (!controller->triggerDown && triggerValue > triggerDownThreshold)
      VR_SetTrigger(controller, K_RTRIGGER, true);
    else if (controller->triggerDown && triggerValue < triggerUpThreshold) {
      if (controller->triggerKey)
        VR_SetTrigger(controller, controller->triggerKey, false);
      else
        controller->triggerDown = false;
    }
    return;
  }

  if (!controller->seenThisFrame) {
    if (controller->triggerDown && controller->triggerKey)
      VR_SetTrigger(controller, controller->triggerKey, false);
    controller->triggerDown = false;
    controller->triggerKey = 0;
    return;
  }

  triggerValue = VR_ReadTrigger(controller, controllerIndex);

  /* Release an attack/old key before treating the held trigger as menu input. */
  if (controller->triggerDown && controller->triggerKey == K_RTRIGGER) {
    VR_SetTrigger(controller, controller->triggerKey, false);
    if (triggerValue >= triggerUpThreshold) {
      controller->triggerDown = true;
      controller->triggerKey = 0;
      return;
    }
  }

  if (!controller->triggerDown && triggerValue > triggerDownThreshold) {
    controller->triggerDown = true;
    controller->triggerKey = 0;
    if (M_PointerCanActivate(M_POINTER_VR))
      M_PointerActivate(M_POINTER_VR);
    else {
      /* Preserve the old stick/arrow-navigation trigger behavior off-panel. */
      controller->triggerDown = false;
      VR_SetTrigger(controller, K_ENTER, true);
    }
  } else if (controller->triggerDown && triggerValue < triggerUpThreshold) {
    if (controller->triggerKey)
      VR_SetTrigger(controller, controller->triggerKey, false);
    else {
      controller->triggerDown = false;
      controller->triggerKey = 0;
    }
  }
}

static void VR_UpdateMenuPointer(void) {
  vec3_t ray_origin, ray_forward, ray_right, ray_up;
  vec3_t center_to_origin, hit, relative;
  float denominator, distance, x, y;

  if (key_dest != key_menu || !controllers[1].seenThisFrame ||
      !vr_menu_surface.valid) {
    M_PointerLeave(M_POINTER_VR);
    return;
  }

  VectorCopy(cl.handpos[1], ray_origin);
  /* Raw controller orientation is valid in every VR aim mode; cl.handrot is not. */
  AngleVectors(controllers[1].orientation, ray_forward, ray_right, ray_up);
  VectorSubtract(vr_menu_surface.center, ray_origin, center_to_origin);
  denominator = DotProduct(ray_forward, vr_menu_surface.normal);
  if (denominator > -0.0001f && denominator < 0.0001f) {
    M_PointerLeave(M_POINTER_VR);
    return;
  }

  distance = DotProduct(center_to_origin, vr_menu_surface.normal) / denominator;
  if (distance <= 0.0f) {
    M_PointerLeave(M_POINTER_VR);
    return;
  }

  VectorMA(ray_origin, distance, ray_forward, hit);
  VectorSubtract(hit, vr_menu_surface.center, relative);
  x = DotProduct(relative, vr_menu_surface.right) / vr_menu_surface.scale +
      160.0f;
  y = DotProduct(relative, vr_menu_surface.down) / vr_menu_surface.scale +
      100.0f;
  M_PointerMove(x, y, M_POINTER_VR);
}

void DoAxis(vr_controller *controller, int axis, int quakeKeyNeg,
            int quakeKeyPos, double deadzoneExtra) {
  int controllerIndex = VR_ControllerIndex(controller);
  float lastVal =
      GetAxis(&controller->lastState, controllerIndex, axis, deadzoneExtra);
  float val = GetAxis(&controller->state, controllerIndex, axis, deadzoneExtra);

  bool posWasDown = lastVal > 0.0f;
  bool posDown = val > 0.0f;
  if (posDown != posWasDown) {
    Key_Event(quakeKeyPos, posDown);
  }

  bool negWasDown = lastVal < 0.0f;
  bool negDown = val < 0.0f;
  if (negDown != negWasDown) {
    Key_Event(quakeKeyNeg, negDown);
  }
}

void VR_Move(usercmd_t *cmd) {
  static int input_events_frame = -1;
  qboolean emit_input_events;

  if (!vr_enabled.value) {
    VR_ReleaseControllerInputs();
    return;
  }

  if (!vr_initialized)
    return;

  emit_input_events = input_events_frame != host_framecount;
  if (emit_input_events)
    input_events_frame = host_framecount;

  if (emit_input_events) {
    // k_EButton_Axis1 === k_EButton_SteamVR_Trigger
    DoTrigger(&controllers[0], K_LTRIGGER);
    if (key_dest == key_menu) {
      VR_UpdateMenuPointer();
      VR_DoMenuTrigger(&controllers[1]);
    } else {
      M_PointerLeave(M_POINTER_VR);
      DoTrigger(&controllers[1], K_RTRIGGER);
    }

    // k_EButton_Grip (uses DoGrip for squeeze-only behavior on Index)
    DoGrip(&controllers[0], K_LSHOULDER);
    DoGrip(&controllers[1],
           VR_RightAltFireUsesTouchpad() ? K_RSHOULDER : K_VR_ALTFIRE);

    // k_EButton_Axis0 === k_EButton_SteamVR_Touchpad
    DoKey(&controllers[0], vr::k_EButton_SteamVR_Touchpad, K_LTHUMB);
    DoKey(&controllers[1], vr::k_EButton_SteamVR_Touchpad,
          VR_RightAltFireUsesTouchpad() ? K_VR_ALTFIRE : K_RTHUMB);

    // k_EButton_ApplicationMenu / k_EButton_IndexController_B
    DoKey(&controllers[0], vr::k_EButton_ApplicationMenu, K_ESCAPE);
    DoKey(&controllers[1], vr::k_EButton_ApplicationMenu, K_BBUTTON);

    // k_EButton_A
    DoKey(&controllers[0], vr::k_EButton_A, K_ABUTTON);
    DoKey(&controllers[1], vr::k_EButton_A, K_XBUTTON);

    // k_EButton_Axis2 === SteamVR-binding "Right Axis 2 Press" (at least on
    // Index Controller)
    DoKey(&controllers[0], vr::k_EButton_Axis2, K_YBUTTON);
    DoKey(&controllers[1], vr::k_EButton_Axis2, K_YBUTTON);

    // k_EButton_Axis3 (unknown if used by any controller at all)
    DoKey(&controllers[0], vr::k_EButton_Axis3, K_JOY1);
    DoKey(&controllers[1], vr::k_EButton_Axis3, K_JOY2);

    // k_EButton_Axis4 (unknown if used by any controller at all)
    DoKey(&controllers[0], vr::k_EButton_Axis4, K_JOY3);
    DoKey(&controllers[1], vr::k_EButton_Axis4, K_JOY4);
  }

  if (key_dest == key_menu) {
    if (emit_input_events) {
      DoAxis(&controllers[0], 0, K_LEFTARROW, K_RIGHTARROW,
             vr_joystick_axis_menu_deadzone_extra.value);
      DoAxis(&controllers[0], 1, K_DOWNARROW, K_UPARROW,
             vr_joystick_axis_menu_deadzone_extra.value);

      // Allow binding right stick in menu
      DoAxis(&controllers[1], 1, K_VR_RIGHT_STICK_DOWN, K_VR_RIGHT_STICK_UP,
             vr_joystick_axis_menu_deadzone_extra.value);
    }

  } else {
    float vr_movementspeed = cl_forwardspeed.value;

    if (cl_desktop_vanilla_run.value && !cl_alwaysrun.value &&
        vr_movementspeed == 200.0f) {
      vr_movementspeed *= cl_movespeedkey.value;
    }

    if (emit_input_events) {
      DoAxis(&controllers[1], 0, K_LEFTARROW, K_RIGHTARROW,
             vr_joystick_axis_menu_deadzone_extra.value);
    }
    // Right stick Y-axis intentionally unbound to prevent accidental forward
    // movement It is used exclusively for opening the weapon wheel.

    vec3_t lfwd, lright, lup;
    float stick_forward = GetAxis(&controllers[0].state, 0, 1, 0.0f);
    float stick_side = GetAxis(&controllers[0].state, 0, 0, 0.0f);

    // Get HMD orientation for head based movement
    vec3_t orientation;
    QuatToYawPitchRoll((current_eye ? current_eye : &eyes[1])->orientation,
                       orientation);

    if (vr_movement_mode.value == VR_MOVEMENT_MODE_FOLLOW_HEAD) {
      AngleVectors(orientation, lfwd, lright, lup);
    } else {
      AngleVectors(cl.handrot[0], lfwd, lright, lup);
    }

    if (vr_movement_mode.value == VR_MOVEMENT_MODE_RAW_INPUT) {
      cmd->forwardmove += vr_movementspeed * stick_forward;
      cmd->sidemove += vr_movementspeed * stick_side;
    } else {
      vec3_t vfwd;

      vec3_t vright;

      vec3_t vup;
      vec3_t playerYawOnly = {0, orientation[YAW], 0};
      if (vr_movement_mode.value == VR_MOVEMENT_MODE_FOLLOW_HAND)
        playerYawOnly[YAW] = cl.handrot[0][YAW];

      AngleVectors(playerYawOnly, vfwd, vright, vup);

      // avoid gimbal by using up if we are point up/down
      if (fabsf(lfwd[2]) > 0.8f) {
        if (lfwd[2] < -0.8f) {
          lfwd[0] *= -1;
          lfwd[1] *= -1;
          lfwd[2] *= -1;
        } else {
          lup[0] *= -1;
          lup[1] *= -1;
          lup[2] *= -1;
        }

        VectorSwap(lup, lfwd);
      }

      // Scale up directions so tilting doesn't affect speed
      float fac = 1.0f / lup[2];
      for (int i = 0; i < 3; i++) {
        lfwd[i] *= fac;
        lright[i] *= fac;
      }

      vec3_t move = {0, 0, 0};
      VectorMA(move, stick_forward, lfwd, move);
      VectorMA(move, stick_side, lright, move);

      float fwd = DotProduct(move, vfwd);
      float right = DotProduct(move, vright);

      // Quake run doesn't affect the value of cl_sidespeed.value, so just use
      // forward speed here for consistency
      cmd->forwardmove += vr_movementspeed * fwd;
      cmd->sidemove += vr_movementspeed * right;
    }

    if (vr_movement_mode.value == VR_MOVEMENT_MODE_FOLLOW_HEAD) {
      AngleVectors(orientation, lfwd, lright, lup);
    } else {
      AngleVectors(cl.handrot[0], lfwd, lright, lup);
    }

    cmd->upmove += cl_upspeed.value * stick_forward * lfwd[2];

    if (vr_movement_speed.value != 1.0f) {
      cmd->forwardmove *= vr_movement_speed.value;
      cmd->sidemove *= vr_movement_speed.value;
      cmd->upmove *= vr_movement_speed.value;
    }

    if ((in_speed.state & 1) ^ (cl_alwaysrun.value != 0.0)) {
      cmd->forwardmove *= cl_movespeedkey.value;
      cmd->sidemove *= cl_movespeedkey.value;
      cmd->upmove *= cl_movespeedkey.value;
    }

    float yawMove = GetAxis(&controllers[1].state, 1, 0, 0.0);

    if (emit_input_events) {
      if (vr_snap_turn.value != 0) {
        static int lastSnap = 0;
        int snap = yawMove > 0.0f ? 1 : yawMove < 0.0f ? -1 : 0;
        if (snap != lastSnap) {
          vrYaw -= snap * vr_snap_turn.value;
          lastSnap = snap;
        }
      } else {
        vrYaw -=
            (yawMove * host_frametime * 100.0f * vr_joystick_yaw_multi.value) *
            vr_turn_speed.value;
      }
    }

    if (emit_input_events) {
      DoAxis(&controllers[1], 1, K_VR_RIGHT_STICK_DOWN, K_VR_RIGHT_STICK_UP,
             vr_joystick_axis_menu_deadzone_extra.value);
    }

    if (emit_input_events && isViveWand[1]) {
      // Vive wands only have a trackpad and a menu button. Provide trackpad
      // click left/right to cycle weapons.
      bool wasClick =
          (controllers[1].lastState.ulButtonPressed &
           vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Touchpad)) != 0;
      bool isClick =
          (controllers[1].state.ulButtonPressed &
           vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Touchpad)) != 0;
      if (isClick && !wasClick) {
        float x = GetAxis(&controllers[1].state, 1, 0, 0.0);
        if (x > 0.3f) {
          Cbuf_AddText("impulse 10\n");
        } else if (x < -0.3f) {
          Cbuf_AddText("impulse 12\n");
        }
      }
    }
  }
}

extern "C" void IN_VRTurn180_f(void) {
  if (vr_enabled.value && vr_180_snap_turn.value) {
    vrYaw -= 180.0f;
  }
}

extern "C" void VR_TriggerHaptic(int controller, float durationSeconds) {
  if (!vr::VRSystem())
    return;
  unsigned short usDuration = (unsigned short)(durationSeconds * 1000000.0f);

  vr::TrackedDeviceIndex_t deviceIndex =
      vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(
          controller == 0 ? vr::TrackedControllerRole_LeftHand
                          : vr::TrackedControllerRole_RightHand);

  if (deviceIndex != vr::k_unTrackedDeviceIndexInvalid) {
    vr::VRSystem()->TriggerHapticPulse(deviceIndex, 0, usDuration);
  }
}

static void VR_SendWeaponImpulse(int impulse) {
  char cmd[32];

  if (impulse <= 0)
    return;
  q_snprintf(cmd, sizeof(cmd), "impulse %d\n", impulse);
  Cbuf_AddText(cmd);
}

static void VR_ContinueWeaponSelection(void) {
  if (vr_weapon_cycle_target < 0 ||
      vr_weapon_cycle_target >= num_dyn_weapons ||
      vr_weapon_cycle_impulse <= 0)
    return;

  if (VR_WeaponIsActive(&dyn_weapons[vr_weapon_cycle_target])) {
    vr_weapon_cycle_target = -1;
    return;
  }

  if (vr_weapon_cycle_attempts >= VR_WEAPON_CYCLE_MAX_ATTEMPTS) {
    vr_weapon_cycle_target = -1;
    return;
  }

  if (Sys_DoubleTime() < vr_weapon_cycle_next_time)
    return;

  VR_SendWeaponImpulse(vr_weapon_cycle_impulse);
  vr_weapon_cycle_attempts++;
  vr_weapon_cycle_next_time = Sys_DoubleTime() + VR_WEAPON_CYCLE_INTERVAL;
}

// Track weapon changes each frame to discover model mappings
void VR_TrackWeapons(void) {
  int active = cl.stats[STAT_ACTIVEWEAPON];
  int model_idx = cl.stats[STAT_WEAPON];
  const char *model_path;

  if (active == 0 || model_idx == 0) {
    VR_ContinueWeaponSelection();
    return;
  }

  model_path = VR_ModelPathForIndex(model_idx);
  if (!VR_ModelPathLooksWeapon(model_path)) {
    VR_ContinueWeaponSelection();
    return;
  }

  // Pulse auto-scanner if active
  if (vr_autoscan_active && Sys_DoubleTime() > vr_autoscan_next_time) {
    if (vr_autoscan_impulse < 12) {
      vr_autoscan_impulse++;
      VR_SendWeaponImpulse(vr_autoscan_impulse);
      vr_autoscan_next_time =
          Sys_DoubleTime() + 2.0; // 2.0s between probes (passive)
    } else {
      vr_autoscan_active = false;
      Con_DPrintf("VR: Weapon auto-scan complete.\n");
    }
  }

  int found = VR_FindDynWeaponForActive(active, model_idx);
  vr_dyn_weapon_t *w = (found >= 0) ? &dyn_weapons[found] : NULL;

  // Learned info for existing weapon
  if (w) {
    if (!w->discovered || w->model_index != model_idx) {
      w->model_index = model_idx;
      w->discovered = true;
    }

    if (w->owned_stat < 0 && !VR_DynWeaponCanUseItemOwnership(w)) {
      int owned_stat;
      int owned_mask;
      if (VR_FindWeaponOwnedStatForActive(active, &owned_stat, &owned_mask)) {
        w->owned_stat = owned_stat;
        w->owned_mask = owned_mask;
      }
    }

    // Sniff impulse association if it was sent recently (last 0.5s)
    if (w->impulse == 0 && vr_last_sent_impulse > 0 &&
        (Sys_DoubleTime() - vr_last_sent_impulse_time) < 0.5) {
      w->impulse = vr_last_sent_impulse;
      Con_DPrintf("VR: Learned impulse %d for weapon bitmask %d\n", w->impulse,
                  active);
    }
  }
  // Fully new weapon from a mod (not in base table)
  else if (num_dyn_weapons < MAX_DYN_WEAPONS) {
    int impulse;
    int owned_stat = -1;
    int owned_mask = 0;

    if (vr_last_sent_impulse > 0 &&
        (Sys_DoubleTime() - vr_last_sent_impulse_time) < 0.5) {
      impulse = vr_last_sent_impulse;
    } else {
      impulse = VR_FindImpulseForActiveBit(active);
    }

    VR_FindWeaponOwnedStatForActive(active, &owned_stat, &owned_mask);

    w = VR_AddOrUpdateDynWeapon(active, impulse, NULL, model_idx, true, 1.0f,
                                vec3_origin, false, owned_stat, owned_mask, -1,
                                0, -1, 0, false);
    if (!w)
      return;

    Con_DPrintf("VR: Discovered mod weapon bitmask %d model %d impulse %d owned_stat %d\n",
                active, model_idx, w->impulse, owned_stat);
  }

  if (w)
    VR_ApplyDwellWeaponMetadata(w, active);

  VR_ContinueWeaponSelection();
}

// Start/Reset weapon tracking (call on map change / disconnect)
void VR_ResetWeaponTracking(void) {
  last_tracked_activeweapon = -1;
  vr_weapon_cycle_target = -1;

  // Rogue expansion uses different bitmasks: RIT_AXE=2048, and reuses
  // 4096 for RIT_LAVA_NAILGUN. Fix up the axe entry accordingly.
  dyn_weapons[0].bitmask = rogue ? 2048 : 4096;

  // Add expansion-specific weapons to the table (once) so they have proper
  // model paths and impulses instead of relying on dynamic discovery.
  if (rogue && !rogue_weapons_added) {
    VR_AddOrUpdateDynWeapon(RIT_LAVA_NAILGUN, 4, "progs/g_nail.mdl", 0,
                            false, 1.0f, vec3_origin, false, -1, 0, -1, 0,
                            STAT_NAILS, 200, false);
    VR_AddOrUpdateDynWeapon(RIT_LAVA_SUPER_NAILGUN, 5, "progs/g_nail2.mdl", 0,
                            false, 1.0f, vec3_origin, false, -1, 0, -1, 0,
                            STAT_NAILS, 200, false);
    VR_AddOrUpdateDynWeapon(RIT_MULTI_GRENADE, 6, "progs/g_rock.mdl", 0,
                            false, 1.0f, vec3_origin, false, -1, 0, -1, 0,
                            STAT_ROCKETS, 100, false);
    VR_AddOrUpdateDynWeapon(RIT_MULTI_ROCKET, 7, "progs/g_rock2.mdl", 0,
                            false, 1.0f, vec3_origin, false, -1, 0, -1, 0,
                            STAT_ROCKETS, 100, false);
    VR_AddOrUpdateDynWeapon(RIT_PLASMA_GUN, 8, "progs/g_light.mdl", 0, false,
                            1.0f, vec3_origin, false, -1, 0, -1, 0,
                            STAT_CELLS, 100, false);
    rogue_weapons_added = true;
  }
  if (hipnotic && !hipnotic_weapons_added) {
    VR_AddOrUpdateDynWeapon(HIT_MJOLNIR, 1, "progs/g_hammer.mdl", 0, false,
                            1.0f, vec3_origin, false, -1, 0, -1, 0, -1, 0,
                            false);
    VR_AddOrUpdateDynWeapon(HIT_LASER_CANNON, 8, "progs/g_laserg.mdl", 0,
                            false, 1.0f, vec3_origin, false, -1, 0, -1, 0,
                            STAT_CELLS, 100, false);
    VR_AddOrUpdateDynWeapon(HIT_PROXIMITY_GUN, 6, "progs/g_prox.mdl", 0,
                            false, 1.0f, vec3_origin, false, -1, 0, -1, 0,
                            STAT_ROCKETS, 100, false);
    hipnotic_weapons_added = true;
  }
  VR_AddBuiltinWeaponDefaults();

  // Note: We DO NOT reset num_dyn_weapons or discovery state here anymore
  // so that mod weapon knowledge persists across map loads.
  // We just reset the scanning flag if needed.
  if (cls.state != ca_connected) {
    vr_autoscan_active = true;
    vr_autoscan_impulse = 0;
    vr_autoscan_next_time =
        Sys_DoubleTime() + 2.0; // Wait 2s after load to start scan
  }
}

// Build visible weapon list filtered by cl.items, returns count
static int VR_GetVisibleWeapons(vr_dyn_weapon_t **out, int max) {
  int count = 0;
  for (int i = 0; i < num_dyn_weapons && count < max; i++) {
    vr_dyn_weapon_t *w = &dyn_weapons[i];
    qboolean hidden_by_schema = false;

    if (w->discovered && w->model_index > 0 &&
        !VR_ModelIndexLooksWeapon(w->model_index))
      continue;
    if (VR_IsDwellDefaultWeaponEntry(w) && !VR_IsDwellGame())
      continue;

    if (!w->from_schema && w->bitmask) {
      for (int j = 0; j < num_dyn_weapons; j++) {
        if (dyn_weapons[j].from_schema &&
            dyn_weapons[j].bitmask == w->bitmask &&
            VR_WeaponIsOwned(&dyn_weapons[j])) {
          hidden_by_schema = true;
          break;
        }
      }
    }

    if (!hidden_by_schema && VR_WeaponIsOwned(w))
      out[count++] = w;
  }
  return count;
}

static int VR_WeaponSelectionImpulse(const vr_dyn_weapon_t *w) {
  int impulse;

  if (!w)
    return 0;

  if (VR_IsDwellGame()) {
    impulse = VR_DwellImpulseForActive(w->bitmask);
    if (impulse > 0)
      return impulse;
  }

  return w->impulse;
}

static int VR_GetWeaponMenuPlayers(int *out, int max) {
  int count = 0;

  if (!vr_weaponmenu_player_teleport.value || !cl.scores ||
      cl.gametype != GAME_COOP)
    return 0;

  for (int i = 0; i < cl.maxclients && i < MAX_SCOREBOARD && count < max; i++) {
    if (!cl.scores[i].name[0])
      continue;
    if (i + 1 == cl.viewentity)
      continue;
    out[count++] = i;
  }

  return count;
}

/*
 * The wheel is also available in co-op, where a save can be a server-wide
 * operation. Keep these actions strictly single-player: the local server has
 * one client, so no connected peer can be interrupted by a save/load.
 */
static qboolean VR_CanUseQuickSaveMenu(void) {
  return sv.active && svs.maxclients == 1 && !cls.demoplayback &&
         !cl.intermission;
}

// Get impulse for the selected weapon in the visible list
extern "C" int VR_GetSelectedWeaponImpulse(int selection) {
  vr_dyn_weapon_t *visible[MAX_DYN_WEAPONS];
  int num_visible = VR_GetVisibleWeapons(visible, MAX_DYN_WEAPONS);
  if (selection >= 0 && selection < num_visible) {
    return VR_WeaponSelectionImpulse(visible[selection]);
  }
  return 0;
}

extern "C" void VR_SelectWeaponFromMenu(int selection) {
  vr_dyn_weapon_t *visible[MAX_DYN_WEAPONS];
  int num_visible = VR_GetVisibleWeapons(visible, MAX_DYN_WEAPONS);
  vr_dyn_weapon_t *w;
  int impulse;
  int index;

  vr_weapon_cycle_target = -1;

  if (selection < 0 || selection >= num_visible)
    return;

  w = visible[selection];
  impulse = VR_WeaponSelectionImpulse(w);
  if (impulse <= 0) {
    DebugLog("VR: selected weapon has no impulse bitmask=%d model=%d\n",
             w->bitmask, w->model_index);
    return;
  }

  if (VR_WeaponIsActive(w))
    return;

  index = (int)(w - dyn_weapons);
  if (index < 0 || index >= num_dyn_weapons)
    return;

  VR_SendWeaponImpulse(impulse);
  vr_weapon_cycle_target = index;
  vr_weapon_cycle_impulse = impulse;
  vr_weapon_cycle_attempts = 1;
  vr_weapon_cycle_next_time = Sys_DoubleTime() + VR_WEAPON_CYCLE_INTERVAL;
}

extern "C" void VR_SelectPlayerFromMenu(int selection) {
  char cmd[48];

  if (!vr_weaponmenu_player_teleport.value || !cl.scores ||
      cl.gametype != GAME_COOP || selection < 0 ||
      selection >= cl.maxclients || selection >= MAX_SCOREBOARD ||
      !cl.scores[selection].name[0])
    return;
  if (selection + 1 == cl.viewentity)
    return;

  q_snprintf(cmd, sizeof(cmd), "coop_teleport_player %d\n", selection + 1);
  Cbuf_AddText(cmd);
}

extern "C" void VR_SelectQuickSaveFromMenu(void) {
  if (!VR_CanUseQuickSaveMenu())
    return;

  Cbuf_AddText("echo Quicksaving...; wait; save quick\n");
}

extern "C" void VR_SelectQuickLoadFromMenu(void) {
  if (!VR_CanUseQuickSaveMenu())
    return;

  /* Match the standard F9 path: queue the load after input release. */
  Cbuf_AddText("echo Quickloading...; wait; load quick\n");
}

extern gltexture_t *char_texture;
extern void GL_Bind(gltexture_t *tex);

vec3_t vr_weaponcolor = {1.0f, 1.0f, 1.0f};

static void VR_GetPlayerShirtColor(int playernum, float boost, float min_peak,
                                   vec3_t color) {
  int topcolor = (cl.scores[playernum].colors >> 4) & 0xF;
  byte *rgb = (byte *)&d_8to24table[topcolor * 16 + 8];
  float maxc, scale;

  color[0] = rgb[0] / 255.0f;
  color[1] = rgb[1] / 255.0f;
  color[2] = rgb[2] / 255.0f;

  maxc = q_max(color[0], q_max(color[1], color[2]));
  if (maxc <= 0.0f) {
    color[0] = color[1] = color[2] = min_peak;
    return;
  }

  scale = boost;
  if (maxc * scale < min_peak)
    scale = min_peak / maxc;

  color[0] *= scale;
  color[1] *= scale;
  color[2] *= scale;
}

extern "C" void VR_BeginWeaponMenu(void) {
  vr_weaponmenu_selection = -1;
  vr_weaponmenu_selection_type = VR_WEAPONMENU_SELECTION_NONE;
  vr_weaponmenu_anchor_valid = false;
}

static void VR_DrawText3DAligned(vec3_t origin, vec3_t right, vec3_t up,
                                 const char *str, float scale, vec3_t color,
                                 qboolean centered) {
  if (!char_texture)
    return;

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  glEnable(GL_ALPHA_TEST);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glColor3fv(color);

  GL_Bind(char_texture);
  glBegin(GL_QUADS);

  float char_width = 8.0f * scale;
  float char_height = 8.0f * scale;

  vec3_t cur_pos;
  VectorCopy(origin, cur_pos);

  int len = strlen(str);
  if (centered)
    VectorMA(cur_pos, -(len * char_width) / 2.0f, right, cur_pos);

  for (int i = 0; i < len; i++) {
    char c = str[i];
    if (c != ' ') {
      int row = c >> 4;
      int col = c & 15;
      float frow = row * 0.0625f;
      float fcol = col * 0.0625f;
      float size = 0.0625f;

      vec3_t p0, p1, p2, p3;
      VectorCopy(cur_pos, p0);
      VectorMA(p0, -char_height / 2.0f, up, p0);

      VectorCopy(p0, p1);
      VectorMA(p1, char_width, right, p1);
      VectorCopy(p1, p2);
      VectorMA(p2, char_height, up, p2);
      VectorCopy(p0, p3);
      VectorMA(p3, char_height, up, p3);

      glTexCoord2f(fcol, frow + size);
      glVertex3fv(p0);
      glTexCoord2f(fcol + size, frow + size);
      glVertex3fv(p1);
      glTexCoord2f(fcol + size, frow);
      glVertex3fv(p2);
      glTexCoord2f(fcol, frow);
      glVertex3fv(p3);
    }
    VectorMA(cur_pos, char_width, right, cur_pos);
  }
  glEnd();
  glColor3f(1.0f, 1.0f, 1.0f);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

static void VR_DrawText3D(vec3_t origin, vec3_t right, vec3_t up,
                          const char *str, float scale, vec3_t color) {
  VR_DrawText3DAligned(origin, right, up, str, scale, color, true);
}

static void VR_DrawText3DOutlined(vec3_t origin, vec3_t right, vec3_t up,
                                  const char *str, float scale, vec3_t color,
                                  qboolean centered) {
  vec3_t outline_color = {0.0f, 0.0f, 0.0f};
  vec3_t pos;
  float offset = q_max(0.28f, scale * 1.15f);

  VectorCopy(origin, pos);
  VectorMA(pos, offset, right, pos);
  VR_DrawText3DAligned(pos, right, up, str, scale, outline_color, centered);

  VectorCopy(origin, pos);
  VectorMA(pos, -offset, right, pos);
  VR_DrawText3DAligned(pos, right, up, str, scale, outline_color, centered);

  VectorCopy(origin, pos);
  VectorMA(pos, offset, up, pos);
  VR_DrawText3DAligned(pos, right, up, str, scale, outline_color, centered);

  VectorCopy(origin, pos);
  VectorMA(pos, -offset, up, pos);
  VR_DrawText3DAligned(pos, right, up, str, scale, outline_color, centered);

  VR_DrawText3DAligned(origin, right, up, str, scale, color, centered);
}

void VR_DrawWeaponMenu(void) {
  vr_dyn_weapon_t *visible[MAX_DYN_WEAPONS];
  int num_visible = VR_GetVisibleWeapons(visible, MAX_DYN_WEAPONS);
  int player_indices[MAX_SCOREBOARD];
  int num_players = VR_GetWeaponMenuPlayers(player_indices, MAX_SCOREBOARD);
  qboolean quick_actions = VR_CanUseQuickSaveMenu();

  if (num_visible == 0 && num_players == 0 && !quick_actions) {
    vr_weaponmenu_selection = -1;
    vr_weaponmenu_selection_type = VR_WEAPONMENU_SELECTION_NONE;
    return;
  }

  // Position the weapon wheel in front of the player's view. Desktop keeps the
  // opening angle stable for look-to-select, but follows the player's origin so
  // movement does not leave the wheel behind.
  extern refdef_t r_refdef;
  vec3_t origin, menu_angles;
  if (!vr_enabled.value) {
    if (!vr_weaponmenu_anchor_valid) {
      VectorCopy(r_refdef.viewangles, vr_weaponmenu_anchor_viewangles);
      vr_weaponmenu_anchor_valid = true;
    }
    VectorCopy(r_refdef.vieworg, origin);
    VectorCopy(vr_weaponmenu_anchor_viewangles, menu_angles);
  } else {
    vr_weaponmenu_anchor_valid = false;
    VectorCopy(r_refdef.vieworg, origin);
    VectorCopy(r_refdef.viewangles, menu_angles);
  }

  // Get forward/right/up from the view direction
  vec3_t forward, right, up;
  AngleVectors(menu_angles, forward, right, up);

  // Determine what ring sizes we need based on completely filled previous rings
  // Ring 0: Center (1 weapon)
  // Ring 1: Inner (up to 8 weapons)
  // Ring 2: Middle (up to 16 weapons)
  // Ring 3+: Outer (up to 24 weapons, etc...)
  int ring_caps[] = {1, 8, 16, 24, 32};
  int max_rings = sizeof(ring_caps) / sizeof(ring_caps[0]);

  // Calculate which ring is the outermost we will use
  int items_used = 0;
  int target_rings = 0;
  for (int r = 0; r < max_rings; r++) {
    items_used += ring_caps[r];
    target_rings++;
    if (num_visible <= items_used) {
      break;
    }
  }

  // Push the UI slightly further away for every ring added
  float base_radius = 15.0f;                              // Tighter cluster
  float wheel_dist = 75.0f + ((target_rings - 1) * 8.0f); // Further away

  // Move the menu forward and slightly down from the camera
  VectorMA(origin, wheel_dist, forward, origin);
  VectorMA(origin, -10.0f, up, origin);

  // Setup drawing state - clear depth so menu draws on top of world
  glDisable(GL_DEPTH_TEST);
  glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);

  // Store weapon positions for raycast selection (zero-init to avoid
  // garbage positions for weapons whose models fail to load)
  vec3_t weapon_positions[MAX_DYN_WEAPONS];
  qboolean weapon_position_valid[MAX_DYN_WEAPONS];
  vec3_t player_positions[MAX_SCOREBOARD];
  qboolean player_position_valid[MAX_SCOREBOARD];
  vec3_t quick_action_positions[2];
  qboolean quick_action_position_valid[2];
  memset(weapon_positions, 0, sizeof(weapon_positions));
  memset(weapon_position_valid, 0, sizeof(weapon_position_valid));
  memset(player_positions, 0, sizeof(player_positions));
  memset(player_position_valid, 0, sizeof(player_position_valid));
  memset(quick_action_positions, 0, sizeof(quick_action_positions));
  memset(quick_action_position_valid, 0, sizeof(quick_action_position_valid));

  int current_assigned_index = 0;

  for (int r = 0; r < target_rings; r++) {
    // Determine how many items exactly will be placed on THIS specific ring
    int capacity = ring_caps[r];
    int remaining = num_visible - current_assigned_index;
    int items_on_this_ring = (remaining > capacity) ? capacity : remaining;

    // Safety check
    if (items_on_this_ring <= 0)
      break;

    float current_radius = (r == 0) ? 0.0f : base_radius + ((r - 1) * 15.0f);
    float angle_step = (r == 0) ? 0.0f : (2.0f * M_PI) / items_on_this_ring;

    for (int i = 0; i < items_on_this_ring; i++) {
      int w_index = current_assigned_index + i;
      if (w_index >= num_visible)
        break; // Should never hit

      vr_dyn_weapon_t *w = visible[w_index];

      // Load learned/schema models first so mods can replace vanilla slots.
      qmodel_t *mdl = NULL;
      if ((w->discovered || w->from_schema) && w->model_index > 0 &&
          w->model_index < MAX_MODELS &&
          VR_ModelIndexLooksWeapon(w->model_index)) {
        mdl = cl.model_precache[w->model_index];
      }
      if ((!mdl || mdl->type != mod_alias) && w->model_path) {
        mdl = Mod_ForName(w->model_path, false);
        // If pickup model not found, try viewmodel as fallback
        if (!mdl || mdl->type != mod_alias) {
          // Replace g_ with v_ for viewmodel fallback
          char vmodel[64];
          q_strlcpy(vmodel, w->model_path, sizeof(vmodel));
          char *g_pos = strstr(vmodel, "/g_");
          if (g_pos) {
            g_pos[1] = 'v';
            mdl = Mod_ForName(vmodel, false);
          }
        }
      }
      if (!mdl || mdl->type != mod_alias) {
        // We still increment the assigned index so the wheel spacing isn't
        // ruined by a missing model
        continue;
      }

      float angle = (r == 0) ? 0.0f
                             : (i * angle_step) +
                                   (M_PI / 2.0f); // Start at top (12 o'clock)

      // Calculate position in the circle relative to the view
      vec3_t pos;
      VectorCopy(origin, pos);
      VectorMA(pos, cos(angle) * current_radius, right, pos);
      VectorMA(pos, sin(angle) * current_radius, up, pos);
      if (w->has_offset) {
        VectorMA(pos, w->offset[0], right, pos);
        VectorMA(pos, w->offset[1], up, pos);
        VectorMA(pos, w->offset[2], forward, pos);
      }

      // Orient the weapon model to face the player
      vec3_t angles;
      angles[PITCH] = 0;
      angles[YAW] = menu_angles[YAW] + 180.0f + cl.time * 30.0f;
      angles[ROLL] = 0;

      // Setup a temporary entity for rendering
      entity_t ent;
      memset(&ent, 0, sizeof(ent));
      VectorCopy(pos, ent.origin);
      VectorCopy(angles, ent.angles);
      ent.model = mdl;
      ent.frame = 0;
      ent.colormap = vid.colormap;

      // Scale and highlight based on selection
      qboolean is_selected =
          (vr_weaponmenu_selection_type == VR_WEAPONMENU_SELECTION_WEAPON &&
           w_index == vr_weaponmenu_selection);
      qboolean is_equipped = VR_WeaponIsActive(w);

      float schema_scale = w->scale > 0.0f ? w->scale : 1.0f;
      float scale = (is_selected ? 0.40f : 0.25f) * schema_scale;
      if (scale > 0.0f && scale < (1.0f / ENTSCALE_DEFAULT))
        scale = 1.0f / ENTSCALE_DEFAULT;

      // Center model vertically using its bounding box so weapons with
      // low origins (axe, super shotgun) don't clip into neighbours.
      float vert_center = (mdl->mins[2] + mdl->maxs[2]) / 2.0f;
      VectorMA(ent.origin, -vert_center * scale, up, ent.origin);

      ent.scale = ENTSCALE_ENCODE(scale);
      ent.alpha = ENTALPHA_ENCODE(1.0f);

      // Render without frustum culling
      currententity = &ent;

      if (is_selected) {
        vr_weaponcolor[0] = 0.5f;
        vr_weaponcolor[1] = 4.0f;
        vr_weaponcolor[2] = 0.5f;
      } else if (is_equipped) {
        vr_weaponcolor[0] = 4.0f;
        vr_weaponcolor[1] = 4.0f;
        vr_weaponcolor[2] = 0.0f;
      } else {
        vr_weaponcolor[0] = 1.5f; // Boosted to be clearly visible/glowy
        vr_weaponcolor[1] = 1.5f;
        vr_weaponcolor[2] = 1.5f;
      }

      R_DrawAliasModel_NoCull(&ent);

      vr_weaponcolor[0] = 1.0f;
      vr_weaponcolor[1] = 1.0f;
      vr_weaponcolor[2] = 1.0f;

      // Draw Ammo Text
      int ammo = -1;
      int max_ammo = 0;

      if (VR_GetWeaponAmmo(w, &ammo, &max_ammo)) {
        char ammo_str[32];
        if (max_ammo > 0)
          q_snprintf(ammo_str, sizeof(ammo_str), "%d/%d", ammo, max_ammo);
        else
          q_snprintf(ammo_str, sizeof(ammo_str), "%d", ammo);
        vec3_t text_color = {1.0f, 1.0f, 1.0f}; // white
        if (ammo == 0) {
          text_color[0] = 1.0f;
          text_color[1] = 0.0f;
          text_color[2] = 0.0f; // red
        }

        vec3_t text_pos;
        // Position text above the model's visual top.  ent.origin is the
        // vertically-centered model origin; add the distance from center to
        // model top so text always clears the weapon.
        float model_top = (mdl->maxs[2] - vert_center) * scale;
        VectorCopy(ent.origin, text_pos);
        VectorMA(text_pos, -2.0f, forward, text_pos);
        VectorMA(text_pos, model_top + 1.5f, up, text_pos);

        float tscale = 0.15f;
        if (is_selected) {
          tscale = 0.20f; // Bigger for selected
        }

        VR_DrawText3D(text_pos, right, up, ammo_str, tscale, text_color);
      }

      // Save position for selection raycasting.
      // Use ent.origin (vertically centred) plus a small rightward nudge
      // to align with the visual weapon mass.
      vec3_t target_pos;
      VectorCopy(ent.origin, target_pos);
      VectorMA(target_pos, 3.0f, right, target_pos);
      VectorCopy(target_pos, weapon_positions[w_index]);
      weapon_position_valid[w_index] = true;
    }
    current_assigned_index += items_on_this_ring;
  }

  if (num_players > 0) {
    float outer_radius =
        base_radius + ((target_rings > 1) ? ((target_rings - 2) * 15.0f) : 0.0f);
    float list_offset = outer_radius + 16.0f;
    float text_scale = 0.30f;
    float char_width = 8.0f * text_scale;
    float line_spacing = 5.0f;
    float start_y = ((num_players - 1) * line_spacing) * 0.5f;

    for (int i = 0; i < num_players; i++) {
      int playernum = player_indices[i];
      vec3_t text_pos, text_center, text_color;
      char label[MAX_SCOREBOARDNAME + 3];
      const char *name = cl.scores[playernum].name;
      qboolean is_selected =
          (vr_weaponmenu_selection_type == VR_WEAPONMENU_SELECTION_PLAYER &&
           vr_weaponmenu_selection == playernum);
      int len;

      q_snprintf(label, sizeof(label), "%s%s", is_selected ? "> " : "  ", name);
      len = strlen(label);

      VR_GetPlayerShirtColor(playernum, is_selected ? 2.4f : 0.9f,
                             is_selected ? 0.85f : 0.38f, text_color);

      VectorCopy(origin, text_pos);
      VectorMA(text_pos, list_offset, right, text_pos);
      VectorMA(text_pos, start_y - i * line_spacing, up, text_pos);
      VR_DrawText3DOutlined(text_pos, right, up, label, text_scale, text_color,
                            false);

      VectorCopy(text_pos, text_center);
      VectorMA(text_center, (len * char_width) * 0.5f, right, text_center);
      VectorCopy(text_center, player_positions[playernum]);
      player_position_valid[playernum] = true;
    }
  }

  if (quick_actions) {
    float outer_radius =
        base_radius + ((target_rings > 1) ? ((target_rings - 2) * 15.0f) : 0.0f);
    float list_offset = outer_radius + 16.0f;
    const char *labels[2] = {"QUICK SAVE", "QUICK LOAD"};
    const int selection_types[2] = {
        VR_WEAPONMENU_SELECTION_QUICKSAVE,
        VR_WEAPONMENU_SELECTION_QUICKLOAD};
    float text_scale = 0.30f;
    float char_width = 8.0f * text_scale;
    float line_spacing = 5.0f;

    for (int i = 0; i < 2; i++) {
      vec3_t text_pos, text_center, text_color;
      char label[24];
      qboolean is_selected =
          vr_weaponmenu_selection_type == selection_types[i];
      int len;

      q_snprintf(label, sizeof(label), "%s%s", is_selected ? "> " : "  ",
                 labels[i]);
      len = strlen(label);
      text_color[0] = is_selected ? 0.45f : 0.82f;
      text_color[1] = is_selected ? 1.85f : 0.82f;
      text_color[2] = is_selected ? 0.45f : 0.82f;

      VectorCopy(origin, text_pos);
      VectorMA(text_pos, -list_offset, right, text_pos);
      VectorMA(text_pos, (0.5f - i) * line_spacing, up, text_pos);
      VR_DrawText3DOutlined(text_pos, right, up, label, text_scale, text_color,
                            false);

      VectorCopy(text_pos, text_center);
      VectorMA(text_center, (len * char_width) * 0.5f, right, text_center);
      VectorCopy(text_center, quick_action_positions[i]);
      quick_action_position_valid[i] = true;
    }
  }

  // --- Pointer-based selection: determine which weapon or player the right
  // controller is aiming at. Desktop falls back to view direction.
  vec3_t aim_origin, aim_fwd, aim_right_dummy, aim_up_dummy;
  qboolean use_view_aim =
      (!vr_enabled.value ||
       (cl.handpos[1][0] == 0 && cl.handpos[1][1] == 0 &&
        cl.handpos[1][2] == 0));
  float best_score = 0.85f; // Minimum threshold (~31 degree cone)
  int best_index = -1;
  int best_type = VR_WEAPONMENU_SELECTION_NONE;

  if (use_view_aim) {
    VectorCopy(r_refdef.vieworg, aim_origin);
    AngleVectors(r_refdef.viewangles, aim_fwd, aim_right_dummy, aim_up_dummy);
  } else {
    VectorCopy(cl.handpos[1], aim_origin);
    AngleVectors(cl.handrot[1], aim_fwd, aim_right_dummy, aim_up_dummy);
  }

  for (int i = 0; i < num_visible; i++) {
    vec3_t dir;
    float score;

    if (!weapon_position_valid[i])
      continue;
    VectorSubtract(weapon_positions[i], aim_origin, dir);
    VectorNormalize(dir);

    score = DotProduct(aim_fwd, dir);
    if (score > best_score) {
      best_score = score;
      best_index = i;
      best_type = VR_WEAPONMENU_SELECTION_WEAPON;
    }
  }

  for (int i = 0; i < num_players; i++) {
    int playernum = player_indices[i];
    vec3_t dir;
    float score;

    if (!player_position_valid[playernum])
      continue;
    VectorSubtract(player_positions[playernum], aim_origin, dir);
    VectorNormalize(dir);

    score = DotProduct(aim_fwd, dir);
    if (score > best_score) {
      best_score = score;
      best_index = playernum;
      best_type = VR_WEAPONMENU_SELECTION_PLAYER;
    }
  }

  for (int i = 0; i < 2; i++) {
    vec3_t dir;
    float score;

    if (!quick_action_position_valid[i])
      continue;
    VectorSubtract(quick_action_positions[i], aim_origin, dir);
    VectorNormalize(dir);

    score = DotProduct(aim_fwd, dir);
    if (score > best_score) {
      best_score = score;
      best_index = 0;
      best_type = i == 0 ? VR_WEAPONMENU_SELECTION_QUICKSAVE
                         : VR_WEAPONMENU_SELECTION_QUICKLOAD;
    }
  }

  vr_weaponmenu_selection = best_index;
  vr_weaponmenu_selection_type = best_type;

  // Trigger haptic if selection changed
  static int last_vr_weaponmenu_selection = -1;
  static int last_vr_weaponmenu_selection_type = VR_WEAPONMENU_SELECTION_NONE;
  if ((vr_weaponmenu_selection != last_vr_weaponmenu_selection ||
       vr_weaponmenu_selection_type != last_vr_weaponmenu_selection_type) &&
      vr_weaponmenu_selection != -1) {
    VR_TriggerHaptic(1, 0.05f); // Haptic on right controller
  }
  last_vr_weaponmenu_selection = vr_weaponmenu_selection;
  last_vr_weaponmenu_selection_type = vr_weaponmenu_selection_type;

  // Restore currententity reference
  currententity = &cl.viewent;
}

#ifdef __cplusplus
}
#endif
