

#ifdef __cplusplus
extern "C" {
#endif

#include "vr.h"
#include "quakedef.h"
#include "sys.h"
#include "vr_menu.h"
#include "zone.h"

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
  qboolean seenThisFrame;
  qboolean triggerDown;
  vec3_t position;
  vec3_t orientation;
  vr::HmdVector3_t rawvector;
  vr::HmdQuaternion_t raworientation;
} vr_controller;

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

vr_weapon_cmd_t vr_weapons[MAX_VR_WEAPONS];
int num_vr_weapons = 0;

static vr::IVRSystem *ovrHMD;
static vr::TrackedDevicePose_t ovr_DevicePose[vr::k_unMaxTrackedDeviceCount];

static vr_eye_t eyes[2];
static vr_eye_t *current_eye = NULL;
static vr_controller controllers[2];

static void VR_SetTrigger(vr_controller *controller, int quakeKey,
                          qboolean down) {
  if (controller->triggerDown == down) {
    return;
  }

  controller->triggerDown = down;
  Key_Event(quakeKey, down);
}

static void VR_ReleaseControllerInputs(void) {
  VR_SetTrigger(&controllers[0], K_LTRIGGER, false);
  VR_SetTrigger(&controllers[1], K_RTRIGGER, false);
}

static vec3_t lastOrientation = {0, 0, 0};
static vec3_t lastAim = {0, 0, 0};

static qboolean vr_initialized = false;
extern "C" int vr_weaponmenu_selection = -1;

// Dynamic weapon tracking
typedef struct {
  int bitmask; // IT_* bitmask value
  int impulse; // impulse number to switch to this weapon
  const char
      *model_path; // viewmodel path (NULL for dynamically discovered weapons)
  int model_index; // precache model index (learned at runtime for mod weapons)
  qboolean discovered; // has the model been discovered at runtime?
  float scale; // weapon wheel model scale multiplier
  vec3_t offset; // weapon wheel model offset
} vr_dyn_weapon_t;

#define MAX_DYN_WEAPONS 32
static vr_dyn_weapon_t dyn_weapons[MAX_DYN_WEAPONS] = {
    {4096, 1, "progs/g_axe.mdl", 0, false, 1.0f, {0, 0, 0}}, // IT_AXE
    {1, 2, "progs/g_shot.mdl", 0, false, 1.0f, {0, 0, 0}},   // IT_SHOTGUN
    {2, 3, "progs/g_shot2.mdl", 0, false, 1.0f, {0, 0, 0}},  // IT_SUPER_SHOTGUN
    {4, 4, "progs/g_nail.mdl", 0, false, 1.0f, {0, 0, 0}},   // IT_NAILGUN
    {8, 5, "progs/g_nail2.mdl", 0, false, 1.0f, {0, 0, 0}},  // IT_SUPER_NAILGUN
    {16, 6, "progs/g_rock.mdl", 0, false, 1.0f, {0, 0, 0}},  // IT_GRENADE_LAUNCHER
    {32, 7, "progs/g_rock2.mdl", 0, false, 1.0f, {0, 0, 0}}, // IT_ROCKET_LAUNCHER
    {64, 8, "progs/g_light.mdl", 0, false, 1.0f, {0, 0, 0}}, // IT_LIGHTNING
};
static int num_dyn_weapons = 8;
static qboolean rogue_weapons_added = false;
static qboolean hipnotic_weapons_added = false;
static int last_tracked_activeweapon = -1;

static void VR_ApplyWeaponSchema(void);

// Impulse sniffing/discovery state
extern "C" int vr_last_sent_impulse = 0;
extern "C" double vr_last_sent_impulse_time = 0;
static int vr_autoscan_impulse = 0;
static double vr_autoscan_next_time = 0;
static bool vr_autoscan_active = false;
double vr_next_weapon_switch_time = 0; // Debounce for switching

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
DEFINE_CVAR(vr_movement_mode, 0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_joystick_yaw_multi, 1.0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_joystick_axis_deadzone, 0.25, CVAR_ARCHIVE);
DEFINE_CVAR(vr_joystick_axis_menu_deadzone_extra, 0.25, CVAR_ARCHIVE);
DEFINE_CVAR(vr_joystick_axis_exponent, 1.0, CVAR_ARCHIVE);
DEFINE_CVAR(vr_joystick_deadzone_trunc, 1, CVAR_ARCHIVE);
DEFINE_CVAR(vr_hud_scale, 0.025, CVAR_ARCHIVE);
DEFINE_CVAR(vr_menu_scale, 0.13, CVAR_ARCHIVE);
DEFINE_CVAR(vr_movement_instant_stop, 1, CVAR_ARCHIVE);
DEFINE_CVAR(vr_movement_speed, 1.5, CVAR_ARCHIVE);

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

void RecreateTextures(fbo_t *fbo, int width, int height) {
  GLuint oldDepth = fbo->depth_texture;
  GLuint oldTexture = fbo->texture;

  glGenTextures(1, &fbo->depth_texture);
  glGenTextures(1, &fbo->texture);

  if (oldDepth) {
    glDeleteTextures(1, &oldDepth);
    glDeleteTextures(1, &oldTexture);
  }

  glBindTexture(GL_TEXTURE_2D, fbo->depth_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0,
               GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);

  glBindTexture(GL_TEXTURE_2D, fbo->texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);

  fbo->size.width = width;
  fbo->size.height = height;

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo->framebuffer);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                            GL_TEXTURE_2D, fbo->texture, 0);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
                            GL_TEXTURE_2D, fbo->depth_texture, 0);
}

fbo_t CreateFBO(int width, int height) {
  fbo_t fbo;
  int swap_chain_length = 0;

  glGenFramebuffersEXT(1, &fbo.framebuffer);

  fbo.depth_texture = 0;

  RecreateTextures(&fbo, width, height);

  fbo.msaa = 0;
  fbo.msaa_framebuffer = 0;
  fbo.msaa_texture = 0;

  return fbo;
}

void CreateMSAA(fbo_t *fbo, int width, int height, int msaa) {
  fbo->msaa = msaa;

  if (fbo->msaa_framebuffer) {
    glDeleteFramebuffersEXT(1, &fbo->msaa_framebuffer);
    glDeleteTextures(1, &fbo->msaa_texture);
    glDeleteTextures(1, &fbo->msaa_depth_texture);
  }

  glGenFramebuffersEXT(1, &fbo->msaa_framebuffer);
  glGenTextures(1, &fbo->msaa_texture);
  glGenTextures(1, &fbo->msaa_depth_texture);

  glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, fbo->msaa_texture);
  glTexImage2DMultisampleEXT(GL_TEXTURE_2D_MULTISAMPLE, msaa, GL_RGBA8, width,
                             height, false);

  glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, fbo->msaa_depth_texture);
  glTexImage2DMultisampleEXT(GL_TEXTURE_2D_MULTISAMPLE, msaa,
                             GL_DEPTH_COMPONENT24, width, height, false);

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo->msaa_framebuffer);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                            GL_TEXTURE_2D_MULTISAMPLE, fbo->msaa_texture, 0);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
                            GL_TEXTURE_2D_MULTISAMPLE, fbo->msaa_depth_texture,
                            0);

  GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    Con_Printf("Framebuffer incomplete %x", status);
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

aliashdr_t *lastWeaponHeader = NULL;
int weaponCVarEntry = -1;
static char vr_current_weapon_model[MAX_QPATH];
static qboolean vr_weapons_edit_active = false;

#define VR_WEAPON_OFFSETS_FILE "vrweapons.txt"
#define VR_WEAPONS_MOVE_STEP 1.0f
#define VR_WEAPONS_SCALE_STEP 0.01f

void Mod_Weapon(const char *name, aliashdr_t *hdr) {
  if (lastWeaponHeader != hdr) {
    lastWeaponHeader = hdr;
    weaponCVarEntry = -1;
    q_strlcpy(vr_current_weapon_model, name, sizeof(vr_current_weapon_model));
    for (int i = 0; i < MAX_WEAPONS; i++) {
      if (!strcmp(vr_weapon_offset[i * VARS_PER_WEAPON + 4].string, name)) {
        weaponCVarEntry = i;
        break;
      }
    }
    if (weaponCVarEntry == -1) {
      Con_Printf("No VR offset for weapon: %s\n", name);
    }
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
        vr_weapon_offset[weaponCVarEntry * VARS_PER_WEAPON + 2].value +
            vr_gunmodely.value};

    VectorAdd(hdr->original_scale_origin, ofs, hdr->scale_origin);
    VectorScale(hdr->scale_origin, scaleCorrect, hdr->scale_origin);
  }
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
}

static qboolean VR_WeaponOffsetSlotValid(int i) {
  const char *id;

  if (i < 0 || i >= MAX_WEAPONS)
    return false;

  id = vr_weapon_offset[i * VARS_PER_WEAPON + 4].string;
  return id && id[0] && Q_strcmp(id, "-1");
}

static int VR_FindWeaponOffsetEntry(const char *model) {
  if (!model || !model[0])
    return -1;

  for (int i = 0; i < MAX_WEAPONS; i++) {
    if (VR_WeaponOffsetSlotValid(i) &&
        !Q_strcmp(vr_weapon_offset[i * VARS_PER_WEAPON + 4].string, model))
      return i;
  }

  return -1;
}

static int VR_FindFreeWeaponOffsetEntry(void) {
  const char *id;

  for (int i = 0; i < MAX_WEAPONS; i++) {
    id = vr_weapon_offset[i * VARS_PER_WEAPON + 4].string;
    if (!id || !id[0] || !Q_strcmp(id, "-1"))
      return i;
  }

  return -1;
}

static void VR_SetWeaponOffsetEntry(int i, const char *model, float x, float y,
                                    float z, float scale) {
  if (i < 0 || i >= MAX_WEAPONS)
    return;

  Cvar_SetValueQuick(&vr_weapon_offset[i * VARS_PER_WEAPON], x);
  Cvar_SetValueQuick(&vr_weapon_offset[i * VARS_PER_WEAPON + 1], y);
  Cvar_SetValueQuick(&vr_weapon_offset[i * VARS_PER_WEAPON + 2], z);
  Cvar_SetValueQuick(&vr_weapon_offset[i * VARS_PER_WEAPON + 3],
                     q_max(0.01f, scale));
  Cvar_SetQuick(&vr_weapon_offset[i * VARS_PER_WEAPON + 4], model);
}

static qboolean VR_AppendText(char *buf, size_t size, size_t *len,
                              const char *format, ...) {
  int ret;
  va_list argptr;

  if (*len >= size)
    return false;

  va_start(argptr, format);
  ret = q_vsnprintf(buf + *len, size - *len, format, argptr);
  va_end(argptr);

  if (ret < 0 || (size_t)ret >= size - *len) {
    buf[size - 1] = '\0';
    *len = size - 1;
    return false;
  }

  *len += ret;
  return true;
}

static qboolean VR_SaveWeaponOffsetFile(qboolean quiet) {
  char buf[32768];
  size_t len = 0;
  int count = 0;

  buf[0] = '\0';

  for (int i = 0; i < MAX_WEAPONS; i++) {
    const char *model;

    if (!VR_WeaponOffsetSlotValid(i))
      continue;

    model = vr_weapon_offset[i * VARS_PER_WEAPON + 4].string;
    if (!VR_AppendText(buf, sizeof(buf), &len,
                       "{\n"
                       "model %s\n"
                       "offset %.6g %.6g %.6g\n"
                       "scale %.6g\n"
                       "}\n",
                       model,
                       vr_weapon_offset[i * VARS_PER_WEAPON].value,
                       vr_weapon_offset[i * VARS_PER_WEAPON + 1].value,
                       vr_weapon_offset[i * VARS_PER_WEAPON + 2].value,
                       vr_weapon_offset[i * VARS_PER_WEAPON + 3].value)) {
      Con_Printf("VR: %s is too large to write.\n", VR_WEAPON_OFFSETS_FILE);
      return false;
    }
    count++;
  }

  COM_WriteFile(VR_WEAPON_OFFSETS_FILE, buf, (int)len);

  if (!quiet)
    Con_Printf("VR: wrote %d weapon offsets to %s/%s\n", count, com_gamedir,
               VR_WEAPON_OFFSETS_FILE);

  return true;
}

static int VR_LoadWeaponOffsetFile(void) {
  char *data;
  char *start;
  char key[64];
  char path[MAX_OSPATH];
  int handle;
  int len;
  int read;
  int loaded = 0;

  q_snprintf(path, sizeof(path), "%s/%s", com_gamedir,
             VR_WEAPON_OFFSETS_FILE);
  len = Sys_FileOpenRead(path, &handle);
  if (len < 0)
    return 0;

  data = (char *)Z_Malloc(len + 1);
  data[len] = '\0';
  read = Sys_FileRead(handle, data, len);
  Sys_FileClose(handle);
  if (read != len) {
    Z_Free(data);
    Con_Printf("VR: failed reading %s\n", path);
    return 0;
  }

  start = data;
  while (1) {
    start = (char *)COM_Parse(start);
    if (!start || !com_token[0])
      break;

    if (!Q_strcmp(com_token, "{")) {
      char model[MAX_QPATH];
      qboolean have_x = false, have_y = false, have_z = false;
      qboolean have_scale = false;
      float x = 0.0f, y = 0.0f, z = 0.0f, scale = 1.0f;

      model[0] = '\0';

      while (1) {
        start = (char *)COM_Parse(start);
        if (!start || !com_token[0] || !Q_strcmp(com_token, "}"))
          break;

        q_strlcpy(key, com_token, sizeof(key));
        start = (char *)COM_Parse(start);
        if (!start || !com_token[0])
          break;

        if (!Q_strcmp(key, "model") || !Q_strcmp(key, "id")) {
          q_strlcpy(model, com_token, sizeof(model));
        } else if (!Q_strcmp(key, "offset")) {
          x = Q_atof(com_token);
          have_x = true;
          start = (char *)COM_Parse(start);
          if (!start || !com_token[0])
            break;
          y = Q_atof(com_token);
          have_y = true;
          start = (char *)COM_Parse(start);
          if (!start || !com_token[0])
            break;
          z = Q_atof(com_token);
          have_z = true;
        } else if (!Q_strcmp(key, "x")) {
          x = Q_atof(com_token);
          have_x = true;
        } else if (!Q_strcmp(key, "y")) {
          y = Q_atof(com_token);
          have_y = true;
        } else if (!Q_strcmp(key, "z")) {
          z = Q_atof(com_token);
          have_z = true;
        } else if (!Q_strcmp(key, "scale")) {
          scale = Q_atof(com_token);
          have_scale = true;
        }
      }

      if (model[0]) {
        int entry = VR_FindWeaponOffsetEntry(model);
        qboolean existing = (entry >= 0);

        if (entry < 0)
          entry = VR_FindFreeWeaponOffsetEntry();

        if (entry < 0) {
          Con_Printf("VR: no free weapon-offset slot for %s\n", model);
          continue;
        }

        if (existing) {
          if (!have_x)
            x = vr_weapon_offset[entry * VARS_PER_WEAPON].value;
          if (!have_y)
            y = vr_weapon_offset[entry * VARS_PER_WEAPON + 1].value;
          if (!have_z)
            z = vr_weapon_offset[entry * VARS_PER_WEAPON + 2].value;
          if (!have_scale)
            scale = vr_weapon_offset[entry * VARS_PER_WEAPON + 3].value;
        }

        VR_SetWeaponOffsetEntry(entry, model, x, y, z, scale);
        loaded++;
      }
    }
  }

  Z_Free(data);

  if (loaded > 0) {
    lastWeaponHeader = NULL;
    weaponCVarEntry = -1;
    Con_Printf("VR: loaded %d hand weapon offsets from %s\n", loaded,
               VR_WEAPON_OFFSETS_FILE);
  }

  return loaded;
}

static qboolean VR_GetEditableWeaponEntry(int *entry) {
  int i;

  if (weaponCVarEntry >= 0 && VR_WeaponOffsetSlotValid(weaponCVarEntry)) {
    *entry = weaponCVarEntry;
    return true;
  }

  i = VR_FindWeaponOffsetEntry(vr_current_weapon_model);
  if (i >= 0) {
    weaponCVarEntry = i;
    *entry = i;
    return true;
  }

  if (!vr_current_weapon_model[0]) {
    Con_Printf("VR: no active weapon model to edit yet.\n");
    return false;
  }

  i = VR_FindFreeWeaponOffsetEntry();
  if (i < 0) {
    Con_Printf("VR: no free weapon-offset slots left for %s\n",
               vr_current_weapon_model);
    return false;
  }

  VR_SetWeaponOffsetEntry(i, vr_current_weapon_model, 0.0f, 0.0f, 0.0f, 1.0f);
  weaponCVarEntry = i;
  *entry = i;
  Con_Printf("VR: added hand weapon offset entry for %s\n",
             vr_current_weapon_model);
  return true;
}

static void VR_PrintWeaponEditStatus(void) {
  int entry;

  if (!VR_GetEditableWeaponEntry(&entry))
    return;

  Con_Printf("VR weapons: %s  x %.2f  y %.2f  z %.2f  scale %.3f\n",
             vr_weapon_offset[entry * VARS_PER_WEAPON + 4].string,
             vr_weapon_offset[entry * VARS_PER_WEAPON].value,
             vr_weapon_offset[entry * VARS_PER_WEAPON + 1].value,
             vr_weapon_offset[entry * VARS_PER_WEAPON + 2].value,
             vr_weapon_offset[entry * VARS_PER_WEAPON + 3].value);
}

static void VR_AdjustCurrentWeaponOffset(int field, float delta) {
  int entry;
  cvar_t *var;

  if (!VR_GetEditableWeaponEntry(&entry))
    return;

  var = &vr_weapon_offset[entry * VARS_PER_WEAPON + field];
  Cvar_SetValueQuick(var, var->value + delta);
  VR_SaveWeaponOffsetFile(true);
  VR_PrintWeaponEditStatus();
}

static void VR_AdjustAllWeaponScales(float delta) {
  int changed = 0;

  for (int i = 0; i < MAX_WEAPONS; i++) {
    cvar_t *scale;

    if (!VR_WeaponOffsetSlotValid(i))
      continue;

    scale = &vr_weapon_offset[i * VARS_PER_WEAPON + 3];
    Cvar_SetValueQuick(scale, q_max(0.01f, scale->value + delta));
    changed++;
  }

  if (changed > 0)
    VR_SaveWeaponOffsetFile(true);

  VR_PrintWeaponEditStatus();
}

static void VR_Weapons_f(void) {
  const char *arg;

  if (Cmd_Argc() > 1) {
    arg = Cmd_Argv(1);

    if (!q_strcasecmp(arg, "save")) {
      VR_SaveWeaponOffsetFile(false);
      return;
    }
    if (!q_strcasecmp(arg, "reload")) {
      VR_LoadWeaponOffsetFile();
      return;
    }
    if (!q_strcasecmp(arg, "status")) {
      VR_PrintWeaponEditStatus();
      return;
    }
    if (!q_strcasecmp(arg, "off")) {
      vr_weapons_edit_active = false;
      VR_SaveWeaponOffsetFile(false);
      Con_Printf("VR weapons edit mode off.\n");
      return;
    }
    if (!q_strcasecmp(arg, "on")) {
      vr_weapons_edit_active = true;
    }
  } else {
    vr_weapons_edit_active = !vr_weapons_edit_active;
  }

  if (!vr_weapons_edit_active) {
    VR_SaveWeaponOffsetFile(false);
    Con_Printf("VR weapons edit mode off.\n");
    return;
  }

  if (cls.state == ca_connected) {
    IN_Activate();
    key_dest = key_game;
  }

  Con_Printf("VR weapons edit mode on. Arrows move hand X/Y, PGUP/PGDN move Z, KP +/- scales all, ESC exits.\n");
  VR_PrintWeaponEditStatus();
}

qboolean VR_WeaponsKey(int key, qboolean down) {
  if (!vr_weapons_edit_active)
    return false;

  switch (key) {
  case K_ESCAPE:
  case K_LEFTARROW:
  case K_RIGHTARROW:
  case K_UPARROW:
  case K_DOWNARROW:
  case K_KP_LEFTARROW:
  case K_KP_RIGHTARROW:
  case K_KP_UPARROW:
  case K_KP_DOWNARROW:
  case K_PGUP:
  case K_PGDN:
  case K_KP_PGUP:
  case K_KP_PGDN:
  case K_KP_PLUS:
  case K_KP_MINUS:
    break;
  default:
    return false;
  }

  if (!down)
    return true;

  switch (key) {
  case K_ESCAPE:
    vr_weapons_edit_active = false;
    VR_SaveWeaponOffsetFile(false);
    Con_Printf("VR weapons edit mode off.\n");
    break;
  case K_LEFTARROW:
  case K_KP_LEFTARROW:
    VR_AdjustCurrentWeaponOffset(0, -VR_WEAPONS_MOVE_STEP);
    break;
  case K_RIGHTARROW:
  case K_KP_RIGHTARROW:
    VR_AdjustCurrentWeaponOffset(0, VR_WEAPONS_MOVE_STEP);
    break;
  case K_UPARROW:
  case K_KP_UPARROW:
    VR_AdjustCurrentWeaponOffset(1, VR_WEAPONS_MOVE_STEP);
    break;
  case K_DOWNARROW:
  case K_KP_DOWNARROW:
    VR_AdjustCurrentWeaponOffset(1, -VR_WEAPONS_MOVE_STEP);
    break;
  case K_PGUP:
  case K_KP_PGUP:
    VR_AdjustCurrentWeaponOffset(2, VR_WEAPONS_MOVE_STEP);
    break;
  case K_PGDN:
  case K_KP_PGDN:
    VR_AdjustCurrentWeaponOffset(2, -VR_WEAPONS_MOVE_STEP);
    break;
  case K_KP_PLUS:
    VR_AdjustAllWeaponScales(VR_WEAPONS_SCALE_STEP);
    break;
  case K_KP_MINUS:
    VR_AdjustAllWeaponScales(-VR_WEAPONS_SCALE_STEP);
    break;
  }

  return true;
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
  // weapons for QBJ3
  else if (!strcmp(COM_SkipPath(com_gamedir), "qbj3")) {
    InitWeaponCVars(i++, "progs/v_wrench.mdl", "0", "0", "0",
                    "0.15"); // Wrench
    InitWeaponCVars(i++, "progs/v_pistol.mdl", "0", "0", "0",
                    "0.15"); // Pistol
    InitWeaponCVars(i++, "progs/v_flakshotgun.mdl", "0", "0", "0",
                    "0.15"); // Flak Shotgun
    InitWeaponCVars(i++, "progs/v_tnailgun.mdl", "0", "0", "0",
                    "0.15"); // Twin Nailgun
    InitWeaponCVars(i++, "progs/v_rebar.mdl", "0", "0", "0",
                    "0.15"); // Rebar Cannon
    InitWeaponCVars(i++, "progs/v_grenlauncher.mdl", "0", "0", "0",
                    "0.15"); // Grenade Launcher
    InitWeaponCVars(i++, "progs/v_mmml.mdl", "0", "0", "0",
                    "0.15"); // Multi-Missile Launcher
    InitWeaponCVars(i++, "progs/v_invoker.mdl", "0", "0", "0",
                    "0.15"); // The Invoker
    InitWeaponCVars(i++, "progs/v_berserk.mdl", "0", "0", "0",
                    "0.15"); // Berserk wrench replacement
  } else {
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

  VR_LoadWeaponOffsetFile();
}

// ----------------------------------------------------------------------------
// Public vars and functions

void VID_VR_Init() {
  // This is only called once at game start
  Cvar_RegisterVariable(&vr_enabled);
  Cvar_SetCallback(&vr_enabled, VR_Enabled_f);
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
  Cmd_AddCommand("vrweapons", VR_Weapons_f);

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

  num_vr_weapons = 0;

  // Try to load vr_weapons.txt from the active search path. This is freed
  // below, so it must come from the zone allocator rather than temp hunk.
  data = (char *)COM_LoadZoneFile("vr_weapons.txt", NULL);
  if (!data) {
    Con_Printf("VR: No vr_weapons.txt found.\n");
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
        }
      }
      num_vr_weapons++;
    }
  }

  Con_Printf("VR: Loaded %d weapons from vr_weapons.txt\n", num_vr_weapons);
  VR_ApplyWeaponSchema();

  // Precache models
  for (int i = 0; i < num_vr_weapons; i++) {
    Mod_ForName(vr_weapons[i].model_path, false);
  }

  Z_Free(data);
}

// Per-game extra Z compensation applied on top of vr_projectilespawn_z_offset.
// Stored as a plain C float so config.cfg can never override it.
float vr_game_projectile_z_extra = 0.0f;

void VR_InitGame() {
  InitAllWeaponCVars();
  VR_LoadWeaponSchema();
  lastWeaponHeader = NULL;
  weaponCVarEntry = -1;

  // Per-game extra Z offset — currently 0 for all games.
  // The base vr_projectilespawn_z_offset cvar (default 24) handles QuakeC's
  // '0 0 16' compensation for both local and remote paths.
  vr_game_projectile_z_extra = 0.0f;
}

static void VR_ApplyWeaponSchema(void) {
  for (int i = 0; i < num_vr_weapons; i++) {
    vr_weapon_cmd_t *schema = &vr_weapons[i];
    vr_dyn_weapon_t *target = NULL;

    if (!schema->bitmask || !schema->model_path[0])
      continue;

    for (int j = 0; j < num_dyn_weapons; j++) {
      if (dyn_weapons[j].bitmask == schema->bitmask) {
        target = &dyn_weapons[j];
        break;
      }
    }

    if (!target && num_dyn_weapons < MAX_DYN_WEAPONS)
      target = &dyn_weapons[num_dyn_weapons++];

    if (!target)
      continue;

    target->bitmask = schema->bitmask;
    target->impulse = schema->impulse;
    target->model_path = schema->model_path;
    target->model_index = 0;
    target->discovered = false;
    target->scale = schema->scale > 0.0f ? schema->scale : 1.0f;
    VectorCopy(schema->offset, target->offset);
  }
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
    eyes[i].fbo = CreateFBO(vrwidth, vrheight);
    eyes[i].fov_x = (atan(-LeftTan) + atan(RightTan)) / M_PI_DIV_180;
    eyes[i].fov_y = (atan(-UpTan) + atan(DownTan)) / M_PI_DIV_180;
  }

  vr::VRCompositor()->SetTrackingSpace(vr::TrackingUniverseStanding);
  VR_ResetOrientation(); // Recenter the HMD

  Cbuf_AddText(
      "exec vr_autoexec.cfg\n"); // Load the vr autosec config file incase
                                 // the user has settings they want

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
                     glheight != current_eye->fbo.size.height;
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

void IdentifyAxes(int device);

static bool modelIdentified[2] = {false, false};
static bool isViveWand[2] = {false, false};

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

  entity_t *player = &cl_entities[cl.viewentity];

  // Update poses
  vr::VRCompositor()->WaitGetPoses(ovr_DevicePose,
                                   vr::k_unMaxTrackedDeviceCount, nullptr, 0);

  controllers[0].seenThisFrame = false;
  controllers[1].seenThisFrame = false;

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

        IdentifyAxes(iDevice);

        if (!modelIdentified[controllerIndex]) {
          char modelNumber[1024];
          vr::VRSystem()->GetStringTrackedDeviceProperty(
              iDevice, vr::Prop_ModelNumber_String, modelNumber,
              sizeof(modelNumber), nullptr);
          if (strstr(modelNumber, "Vive") || strstr(modelNumber, "VIVE")) {
            isViveWand[controllerIndex] = true;
          }
          modelIdentified[controllerIndex] = true;
        }

        controller->lastState = controller->state;
        vr::VRSystem()->GetControllerState(iDevice, &controller->state,
                                           sizeof(controller->state));
        controller->seenThisFrame = true;
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

    if (cl.viewent.model) {
      aliashdr_t *hdr = (aliashdr_t *)Mod_Extradata(cl.viewent.model);
      Mod_Weapon(cl.viewent.model->name, hdr);
    }

    VectorCopy(cl.handrot[1], cl.aimangles); // Sets the shooting angle

    break;
  }

  SetHandPos(0, player);
  SetHandPos(1, player);

  cl.viewangles[ROLL] = orientation[ROLL];

  VectorCopy(orientation, lastOrientation);
  VectorCopy(cl.aimangles, lastAim);

  VectorCopy(cl.viewangles, r_refdef.viewangles);
  VectorCopy(cl.aimangles, r_refdef.aimangles);

  // Render the scene for each eye into their FBOs
  for (int i = 0; i < 2; i++) {
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
  float size, alpha;

  // leads to exception in multiplayer
  /*     if((int)(sv_player->v.weapon) == IT_AXE)
      {
          return;
      } */

  size = CLAMP(0.0f, vr_crosshair_size.value, 32.0f);
  alpha = CLAMP(0.0f, vr_crosshair_alpha.value, 1.0f);

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
    VectorCopy(cl.handpos[1], start);

    AngleVectors(cl.handrot[1], forward, right, up);
    if (weaponCVarEntry >= 0) {
      vec3_t ofs = {
          vr_weapon_offset[weaponCVarEntry * VARS_PER_WEAPON].value,
          vr_weapon_offset[weaponCVarEntry * VARS_PER_WEAPON + 1].value,
          vr_weapon_offset[weaponCVarEntry * VARS_PER_WEAPON + 2].value +
              vr_gunmodely.value};
      vec3_t fwd2;
      VectorCopy(forward, fwd2);
      fwd2[0] *= vr_gunmodelscale.value * ofs[2];
      fwd2[1] *= vr_gunmodelscale.value * ofs[2];
      fwd2[2] *= vr_gunmodelscale.value * ofs[2];
      VectorAdd(start, fwd2, start);
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
    glPointSize(size * glwidth / vid.width);

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
    glLineWidth(size * glwidth / vid.width);
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

double lerp(double a, double b, double f) { return (a * (1.0 - f)) + (b * f); }

void vec3lerp(vec3_t out, vec3_t start, vec3_t end, double f) {
  out[0] = lerp(start[0], end[0], f);
  out[1] = lerp(start[1], end[1], f);
  out[2] = lerp(start[2], end[2], f);
}

void VR_Draw2D() {
  qboolean draw_sbar = false;
  vec3_t menu_angles, forward, right, up, target;
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

  vec3_t smoothedTarget;
  vec3lerp(smoothedTarget, lastMenuPosition, target, 0.2);
  VectorCopy(smoothedTarget, lastMenuPosition);

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

int axisTrackpad = -1;
int axisJoystick = -1;
int axisTrigger = -1;
int axisGrip = -1;
bool identified = false;

void IdentifyAxes(int device) {
  if (identified) {
    return;
  }

  for (uint32_t i = 0; i < vr::k_unControllerStateAxisCount; i++) {
    switch (vr::VRSystem()->GetInt32TrackedDeviceProperty(
        device, (vr::ETrackedDeviceProperty)(vr::Prop_Axis0Type_Int32 + i),
        nullptr)) {
    case vr::k_eControllerAxis_TrackPad:
      if (axisTrackpad == -1) {
        axisTrackpad = i;
      }
      break;
    case vr::k_eControllerAxis_Joystick:
      if (axisJoystick == -1) {
        axisJoystick = i;
      }
      break;
    case vr::k_eControllerAxis_Trigger:
      if (axisTrigger == -1) {
        axisTrigger = i;
      } else if (axisGrip == -1) {
        axisGrip = i;
      }
      break;
    }
  }

  identified = true;
}

float GetAxis(vr::VRControllerState_t *state, int axis, double deadzoneExtra) {
  float v = 0;

  if (axis == 0) {
    if (axisTrackpad != -1) {
      v += state->rAxis[axisTrackpad].x;
    }
    if (axisJoystick != -1) {
      v += state->rAxis[axisJoystick].x;
    }
  } else {
    if (axisTrackpad != -1) {
      v += state->rAxis[axisTrackpad].y;
    }
    if (axisJoystick != -1) {
      v += state->rAxis[axisJoystick].y;
    }
  }

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

void DoKey(vr_controller *controller, vr::EVRButtonId vrButton, int quakeKey) {
  bool wasDown = (controller->lastState.ulButtonPressed &
                  vr::ButtonMaskFromId(vrButton)) != 0;
  bool isDown =
      (controller->state.ulButtonPressed & vr::ButtonMaskFromId(vrButton)) != 0;
  if (isDown != wasDown) {
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

  if (!controller->seenThisFrame) {
    VR_SetTrigger(controller, quakeKey, false);
    return;
  }

  if (axisTrigger != -1) {
    float triggerValue = controller->state.rAxis[axisTrigger].x;

    if (!controller->triggerDown && triggerValue > triggerDownThreshold)
      VR_SetTrigger(controller, quakeKey, true);
    else if (controller->triggerDown && triggerValue < triggerUpThreshold)
      VR_SetTrigger(controller, quakeKey, false);
  }
}

void DoAxis(vr_controller *controller, int axis, int quakeKeyNeg,
            int quakeKeyPos, double deadzoneExtra) {
  float lastVal = GetAxis(&controller->lastState, axis, deadzoneExtra);
  float val = GetAxis(&controller->state, axis, deadzoneExtra);

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
  if (!vr_enabled.value) {
    VR_ReleaseControllerInputs();
    return;
  }

  if (!current_eye) {
    return;
  }

  // k_EButton_Axis1 === k_EButton_SteamVR_Trigger
  DoTrigger(&controllers[0], K_LTRIGGER);
  DoTrigger(&controllers[1], K_RTRIGGER);

  // k_EButton_Grip (uses DoGrip for squeeze-only behavior on Index)
  DoGrip(&controllers[0], K_LSHOULDER);
  DoGrip(&controllers[1], K_RSHOULDER);

  // k_EButton_Axis0 === k_EButton_SteamVR_Touchpad
  DoKey(&controllers[0], vr::k_EButton_SteamVR_Touchpad, K_LTHUMB);
  DoKey(&controllers[1], vr::k_EButton_SteamVR_Touchpad, K_RTHUMB);

  // k_EButton_ApplicationMenu / k_EButton_IndexController_B
  DoKey(&controllers[0], vr::k_EButton_ApplicationMenu, K_ESCAPE);
  DoKey(&controllers[1], vr::k_EButton_ApplicationMenu, K_BBUTTON);

  // k_EButton_A
  DoKey(&controllers[0], vr::k_EButton_A, K_ABUTTON);
  DoKey(&controllers[1], vr::k_EButton_A, K_XBUTTON);

  // k_EButton_Axis2 === SteamVR-binding "Right Axis 2 Press" (at least on Index
  // Controller)
  DoKey(&controllers[0], vr::k_EButton_Axis2, K_YBUTTON);
  DoKey(&controllers[1], vr::k_EButton_Axis2, K_YBUTTON);

  // k_EButton_Axis3 (unknown if used by any controller at all)
  DoKey(&controllers[0], vr::k_EButton_Axis3, K_JOY1);
  DoKey(&controllers[1], vr::k_EButton_Axis3, K_JOY2);

  // k_EButton_Axis4 (unknown if used by any controller at all)
  DoKey(&controllers[0], vr::k_EButton_Axis4, K_JOY3);
  DoKey(&controllers[1], vr::k_EButton_Axis4, K_JOY4);

  if (key_dest == key_menu) {
    DoAxis(&controllers[0], 0, K_LEFTARROW, K_RIGHTARROW,
           vr_joystick_axis_menu_deadzone_extra.value);
    DoAxis(&controllers[0], 1, K_DOWNARROW, K_UPARROW,
           vr_joystick_axis_menu_deadzone_extra.value);

    // Allow binding right stick in menu
    DoAxis(&controllers[1], 1, K_VR_RIGHT_STICK_DOWN, K_VR_RIGHT_STICK_UP,
           vr_joystick_axis_menu_deadzone_extra.value);

    // Enter in menu with Trigger
    DoTrigger(&controllers[1], K_ENTER);
  } else {
    DoAxis(&controllers[1], 0, K_LEFTARROW, K_RIGHTARROW,
           vr_joystick_axis_menu_deadzone_extra.value);
    // Right stick Y-axis intentionally unbound to prevent accidental forward
    // movement It is used exclusively for opening the weapon wheel.

    vec3_t lfwd, lright, lup;

    // Get HMD orientation for head based movement
    vec3_t orientation;
    QuatToYawPitchRoll(current_eye->orientation, orientation);

    if (vr_movement_mode.value == VR_MOVEMENT_MODE_FOLLOW_HEAD) {
      AngleVectors(orientation, lfwd, lright, lup);
    } else {
      AngleVectors(cl.handrot[0], lfwd, lright, lup);
    }

    if (vr_movement_mode.value == VR_MOVEMENT_MODE_RAW_INPUT) {
      cmd->forwardmove +=
          cl_forwardspeed.value * GetAxis(&controllers[0].state, 1, 0.15f);
      cmd->sidemove +=
          cl_forwardspeed.value * GetAxis(&controllers[0].state, 0, 0.15f);
    } else {
      vec3_t vfwd;

      vec3_t vright;

      vec3_t vup;
      vec3_t playerYawOnly = {0, cl.aimangles[YAW], 0};

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
      VectorMA(move, GetAxis(&controllers[0].state, 1, 0.15f), lfwd, move);
      VectorMA(move, GetAxis(&controllers[0].state, 0, 0.15f), lright, move);

      float fwd = DotProduct(move, vfwd);
      float right = DotProduct(move, vright);

      // Quake run doesn't affect the value of cl_sidespeed.value, so just use
      // forward speed here for consistency
      cmd->forwardmove += cl_forwardspeed.value * fwd;
      cmd->sidemove += cl_forwardspeed.value * right;
    }

    if (vr_movement_mode.value == VR_MOVEMENT_MODE_FOLLOW_HEAD) {
      AngleVectors(orientation, lfwd, lright, lup);
    } else {
      AngleVectors(cl.handrot[0], lfwd, lright, lup);
    }

    cmd->upmove +=
        cl_upspeed.value * GetAxis(&controllers[0].state, 1, 0.15f) * lfwd[2];

    // Compensate for instant stop removing momentum-based speed
    if (vr_movement_speed.value != 1.0f) {
      cmd->forwardmove *= vr_movement_speed.value;
      cmd->sidemove *= vr_movement_speed.value;
      cmd->upmove *= vr_movement_speed.value;
    }

    if (cl_forwardspeed.value > 200 && cl_movespeedkey.value) {
      cmd->forwardmove /= cl_movespeedkey.value;
    }
    if ((cl_forwardspeed.value > 200) ^ (in_speed.state & 1)) {
      cmd->forwardmove *= cl_movespeedkey.value;
      cmd->sidemove *= cl_movespeedkey.value;
      cmd->upmove *= cl_movespeedkey.value;
    }

    float yawMove = GetAxis(&controllers[1].state, 0, 0.0);

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

    DoAxis(&controllers[1], 1, K_VR_RIGHT_STICK_DOWN, K_VR_RIGHT_STICK_UP,
           vr_joystick_axis_menu_deadzone_extra.value);

    if (isViveWand[1]) {
      // Vive wands only have a trackpad and a menu button. Provide trackpad
      // click left/right to cycle weapons.
      bool wasClick =
          (controllers[1].lastState.ulButtonPressed &
           vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Touchpad)) != 0;
      bool isClick =
          (controllers[1].state.ulButtonPressed &
           vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Touchpad)) != 0;
      if (isClick && !wasClick) {
        float x = GetAxis(&controllers[1].state, 0, 0.0);
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

// Track weapon changes each frame to discover model mappings
void VR_TrackWeapons(void) {
  int active = cl.stats[STAT_ACTIVEWEAPON];
  int model_idx = cl.stats[STAT_WEAPON];

  if (active == 0 || model_idx == 0)
    return;

  // Pulse auto-scanner if active
  if (vr_autoscan_active && Sys_DoubleTime() > vr_autoscan_next_time) {
    if (vr_autoscan_impulse < 12) {
      vr_autoscan_impulse++;
      char cmd[32];
      q_snprintf(cmd, sizeof(cmd), "impulse %d\n", vr_autoscan_impulse);
      Cbuf_AddText(cmd);
      vr_autoscan_next_time =
          Sys_DoubleTime() + 2.0; // 2.0s between probes (passive)
    } else {
      vr_autoscan_active = false;
      Con_DPrintf("VR: Weapon auto-scan complete.\n");
    }
  }

  // Check if we already know this weapon's model
  vr_dyn_weapon_t *w = NULL;
  for (int i = 0; i < num_dyn_weapons; i++) {
    if (dyn_weapons[i].bitmask == active) {
      w = &dyn_weapons[i];
      break;
    }
  }

  // Learned info for existing weapon
  if (w) {
    if (!w->discovered || w->model_index != model_idx) {
      w->model_index = model_idx;
      w->discovered = true;
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
    w = &dyn_weapons[num_dyn_weapons];
    w->bitmask = active;
    w->model_index = model_idx;
    w->model_path = NULL;
    w->discovered = true;
    w->scale = 1.0f;
    w->offset[0] = w->offset[1] = w->offset[2] = 0.0f;

    // Attempt to sniff impulse immediately
    if (vr_last_sent_impulse > 0 &&
        (Sys_DoubleTime() - vr_last_sent_impulse_time) < 0.5) {
      w->impulse = vr_last_sent_impulse;
    } else {
      w->impulse = 0;
    }

    num_dyn_weapons++;
    Con_DPrintf("VR: Discovered new mod weapon bitmask %d (impulse %d)\n",
                active, w->impulse);
  }
}

// Start/Reset weapon tracking (call on map change / disconnect)
void VR_ResetWeaponTracking(void) {
  last_tracked_activeweapon = -1;

  // Rogue expansion uses different bitmasks: RIT_AXE=2048, and reuses
  // 4096 for RIT_LAVA_NAILGUN. Fix up the axe entry accordingly.
  dyn_weapons[0].bitmask = rogue ? 2048 : 4096;

  // Add expansion-specific weapons to the table (once) so they have proper
  // model paths and impulses instead of relying on dynamic discovery.
  if (rogue && !rogue_weapons_added) {
    int i = num_dyn_weapons;
    dyn_weapons[i++] = vr_dyn_weapon_t{RIT_LAVA_NAILGUN, 4,
                                       "progs/g_nail.mdl", 0, false, 1.0f,
                                       {0, 0, 0}};
    dyn_weapons[i++] = vr_dyn_weapon_t{RIT_LAVA_SUPER_NAILGUN, 5,
                                       "progs/g_nail2.mdl", 0, false, 1.0f,
                                       {0, 0, 0}};
    dyn_weapons[i++] = vr_dyn_weapon_t{RIT_MULTI_GRENADE, 6,
                                       "progs/g_rock.mdl", 0, false, 1.0f,
                                       {0, 0, 0}};
    dyn_weapons[i++] = vr_dyn_weapon_t{RIT_MULTI_ROCKET, 7,
                                       "progs/g_rock2.mdl", 0, false, 1.0f,
                                       {0, 0, 0}};
    dyn_weapons[i++] = vr_dyn_weapon_t{RIT_PLASMA_GUN, 8,
                                       "progs/g_light.mdl", 0, false, 1.0f,
                                       {0, 0, 0}};
    num_dyn_weapons = i;
    rogue_weapons_added = true;
  }
  if (hipnotic && !hipnotic_weapons_added) {
    int i = num_dyn_weapons;
    dyn_weapons[i++] = vr_dyn_weapon_t{HIT_MJOLNIR, 1,
                                       "progs/g_hammer.mdl", 0, false, 1.0f,
                                       {0, 0, 0}};
    dyn_weapons[i++] = vr_dyn_weapon_t{HIT_LASER_CANNON, 8,
                                       "progs/g_laserg.mdl", 0, false, 1.0f,
                                       {0, 0, 0}};
    dyn_weapons[i++] = vr_dyn_weapon_t{HIT_PROXIMITY_GUN, 6,
                                       "progs/g_prox.mdl", 0, false, 1.0f,
                                       {0, 0, 0}};
    num_dyn_weapons = i;
    hipnotic_weapons_added = true;
  }

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
    if (cl.items & dyn_weapons[i].bitmask) {
      out[count++] = &dyn_weapons[i];
    }
  }
  return count;
}

// Get impulse for the selected weapon in the visible list
extern "C" int VR_GetSelectedWeaponImpulse(int selection) {
  vr_dyn_weapon_t *visible[MAX_DYN_WEAPONS];
  int num_visible = VR_GetVisibleWeapons(visible, MAX_DYN_WEAPONS);
  if (selection >= 0 && selection < num_visible) {
    return visible[selection]->impulse;
  }
  return 0;
}
extern gltexture_t *char_texture;
extern void GL_Bind(gltexture_t *tex);

vec3_t vr_weaponcolor = {1.0f, 1.0f, 1.0f};

static void VR_DrawText3D(vec3_t origin, vec3_t right, vec3_t up,
                          const char *str, float scale, vec3_t color) {
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

void VR_DrawWeaponMenu(void) {
  vr_dyn_weapon_t *visible[MAX_DYN_WEAPONS];
  int num_visible = VR_GetVisibleWeapons(visible, MAX_DYN_WEAPONS);

  if (num_visible == 0)
    return;

  // Position the weapon wheel in front of the player's view
  extern refdef_t r_refdef;
  vec3_t origin;
  VectorCopy(r_refdef.vieworg, origin);

  // Get forward/right/up from the view direction
  vec3_t forward, right, up;
  AngleVectors(r_refdef.viewangles, forward, right, up);

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

  // Use R_DrawAliasModel_NoCull to bypass frustum culling for UI models
  extern void R_DrawAliasModel_NoCull(entity_t * e);

  // Store weapon positions for raycast selection (zero-init to avoid
  // garbage positions for weapons whose models fail to load)
  vec3_t weapon_positions[MAX_DYN_WEAPONS];
  memset(weapon_positions, 0, sizeof(weapon_positions));

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

      // Load the model by path
      qmodel_t *mdl = NULL;
      if (w->model_path) {
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
      if (!mdl && w->discovered && w->model_index > 0 &&
          w->model_index < MAX_MODELS) {
        mdl = cl.model_precache[w->model_index];
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

      // Orient the weapon model to face the player
      vec3_t angles;
      angles[PITCH] = 0;
      angles[YAW] = r_refdef.viewangles[YAW] + 180.0f + cl.time * 30.0f;
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
      qboolean is_selected = (w_index == vr_weaponmenu_selection);
      qboolean is_equipped = (w->bitmask == cl.stats[STAT_ACTIVEWEAPON]);

      float schema_scale = w->scale > 0.0f ? w->scale : 1.0f;
      float scale = (is_selected ? 0.40f : 0.25f) * schema_scale;
      if (scale > 0.0f && scale < (1.0f / ENTSCALE_DEFAULT))
        scale = 1.0f / ENTSCALE_DEFAULT;

      // Center model vertically using its bounding box so weapons with
      // low origins (axe, super shotgun) don't clip into neighbours.
      float vert_center = (mdl->mins[2] + mdl->maxs[2]) / 2.0f;
      VectorMA(ent.origin, -vert_center * scale, up, ent.origin);
      VectorMA(ent.origin, w->offset[0], right, ent.origin);
      VectorMA(ent.origin, w->offset[1], up, ent.origin);
      VectorMA(ent.origin, w->offset[2], forward, ent.origin);

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
      switch (w->bitmask) {
      case IT_SHOTGUN:
      case IT_SUPER_SHOTGUN:
        ammo = cl.stats[STAT_SHELLS];
        max_ammo = 100;
        break;
      case IT_NAILGUN:
      case IT_SUPER_NAILGUN:
      case RIT_LAVA_NAILGUN:
      case RIT_LAVA_SUPER_NAILGUN:
        ammo = cl.stats[STAT_NAILS];
        max_ammo = 200;
        break;
      case IT_GRENADE_LAUNCHER:
      case IT_ROCKET_LAUNCHER:
      case RIT_MULTI_GRENADE:
      case RIT_MULTI_ROCKET:
        ammo = cl.stats[STAT_ROCKETS];
        max_ammo = 100;
        break;
      case IT_LIGHTNING:
      case HIT_LASER_CANNON:
        ammo = cl.stats[STAT_CELLS];
        max_ammo = 100;
        break;
      // HIT_PROXIMITY_GUN and RIT_PLASMA_GUN share bitmask 65536.
      // Disambiguate by active game type.
      case 65536:
        if (rogue) {
          ammo = cl.stats[STAT_CELLS];  // plasma gun
        } else {
          ammo = cl.stats[STAT_ROCKETS]; // proximity gun
        }
        max_ammo = 100;
        break;
      }

      if (ammo >= 0) {
        char ammo_str[32];
        q_snprintf(ammo_str, sizeof(ammo_str), "%d/%d", ammo, max_ammo);
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
    }
    current_assigned_index += items_on_this_ring;
  }

  // --- Pointer-based selection: determine which weapon the right controller is
  // aiming at ---
  vec3_t aim_fwd, aim_right_dummy, aim_up_dummy;
  AngleVectors(cl.handrot[1], aim_fwd, aim_right_dummy, aim_up_dummy);

  float best_score = 0.85f; // Minimum threshold (~31 degree cone)
  int best_index = -1;

  for (int i = 0; i < num_visible; i++) {
    vec3_t dir;
    VectorSubtract(weapon_positions[i], cl.handpos[1], dir);
    VectorNormalize(dir);

    float score = DotProduct(aim_fwd, dir);
    if (score > best_score) {
      best_score = score;
      best_index = i;
    }
  }

  // If controller aiming doesn't work (zero pos), fallback to view direction
  if (cl.handpos[1][0] == 0 && cl.handpos[1][1] == 0 && cl.handpos[1][2] == 0) {
    vec3_t view_fwd, view_right_dummy, view_up_dummy;
    AngleVectors(r_refdef.viewangles, view_fwd, view_right_dummy,
                 view_up_dummy);

    best_score = 0.85f;
    best_index = -1;
    for (int i = 0; i < num_visible; i++) {
      vec3_t dir;
      VectorSubtract(weapon_positions[i], r_refdef.vieworg, dir);
      VectorNormalize(dir);
      float score = DotProduct(view_fwd, dir);
      if (score > best_score) {
        best_score = score;
        best_index = i;
      }
    }
  }

  vr_weaponmenu_selection = best_index;

  // Trigger haptic if selection changed
  static int last_vr_weaponmenu_selection = -1;
  if (vr_weaponmenu_selection != last_vr_weaponmenu_selection &&
      vr_weaponmenu_selection != -1) {
    VR_TriggerHaptic(1, 0.05f); // Haptic on right controller
  }
  last_vr_weaponmenu_selection = vr_weaponmenu_selection;

  // Restore currententity reference
  currententity = &cl.viewent;
}

#ifdef __cplusplus
}
#endif
