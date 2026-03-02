// Internal OpenXR Wrapper for IronwailVR

#ifndef __VR_OPENXR_H
#define __VR_OPENXR_H

#ifdef VR_ENABLED

// Define OpenXR platform macros BEFORE including its headers
#ifdef _WIN32
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <windows.h>
#else
#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#include <GL/glx.h>
#include <X11/Xlib.h>
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

// Platform-specific graphics binding type
#ifdef _WIN32
typedef XrGraphicsBindingOpenGLWin32KHR XrGraphicsBindingOpenGL;
#define XR_TYPE_GRAPHICS_BINDING_OPENGL                                        \
  XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR
#else
typedef XrGraphicsBindingOpenGLXlibKHR XrGraphicsBindingOpenGL;
#define XR_TYPE_GRAPHICS_BINDING_OPENGL XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR
#endif

// Assumes quakedef.h is included before this header in .c files

// These are also defined in vr.h; guard against redefinition when include order varies
#ifndef VR_HAND_LEFT
#define VR_HAND_LEFT 0
#define VR_HAND_RIGHT 1
#endif

typedef struct {
  XrSwapchain handle;
  XrSwapchain depth_handle;
  uint32_t width;
  uint32_t height;
  uint32_t image_count;
  XrSwapchainImageOpenGLKHR *images;
  XrSwapchainImageOpenGLKHR *depth_images;
  GLuint *fbos;
  GLuint *depth_textures; // One depth texture per swapchain image
} vr_swapchain_t;

typedef struct {
  // Core handles
  XrInstance instance;
  XrSystemId system_id;
  XrSession session;
  XrSessionState state;
  XrSpace stage_space;
  XrSpace local_space;
  XrViewConfigurationType view_config_type;
  XrEnvironmentBlendMode blend_mode;

  // Extension support
  int depth_extension_supported;

  // Views / swapchains
  uint32_t view_count;
  XrViewConfigurationView *view_config_views;
  XrView *views;
  vr_swapchain_t *swapchains;

  // Lifecycle
  int initialized;
  int session_running;
  int actions_initialized;

  // Frame timing (used by VR_UpdatePoses to locate spaces)
  XrTime last_predicted_time; // set in VR_Render

  // Head pose (center of HMD, from xrLocateViews during frame)
  XrPosef head_pose;

  // --- Action System (Phase 5) ---
  XrActionSet action_set;
  XrPath hand_subaction_path[2]; // [VR_HAND_LEFT / VR_HAND_RIGHT]

  XrAction hand_pose_action;  // grip pose per hand
  XrAction thumbstick_action; // vec2: locomotion/turning
  XrAction trigger_action;    // bool: fire
  XrAction grip_action;       // bool: use/interact
  XrAction button_a_action;   // bool: jump
  XrAction button_b_action;   // bool: weapon wheel
  XrAction haptic_action;     // haptic: vibration

  XrSpace hand_space[2];  // action spaces for grip poses
  XrPosef hand_pose[2];   // latest located hand poses
  int hand_pose_valid[2]; // whether the pose is fresh

  // --- UI System (Phase 11) ---
  GLuint hud_fbo;
  GLuint hud_texture;
  uint32_t hud_width;
  uint32_t hud_height;

  GLuint menu_fbo;
  GLuint menu_texture;
  uint32_t menu_width;
  uint32_t menu_height;

  // --- Profiling (Phase 12) ---
  GLuint queries[8]; // Timer queries
  double frame_times[8];
} vr_openxr_state_t;

extern vr_openxr_state_t oxr;

void VR_InitUI(void);
void VR_ShutdownUI(void);
void VR_DrawSbar(void);
void VR_Draw2D(void);

// Core VR functions
void VR_OpenXR_Init(void);

// Internal API
const char *VR_OpenXR_ErrorString(XrResult result);
void VR_OpenXR_InitActions(void);
void VR_OpenXR_Shutdown(void);
void VR_OpenXR_PollEvents(void);

#define OXR_CHECK(res, msg)                                                    \
  if (XR_FAILED(res)) {                                                        \
    Con_Printf("OpenXR Error: %s (%s)\n", msg, VR_OpenXR_ErrorString(res));    \
    return;                                                                    \
  }

#define OXR_CHECK_RET(res, msg, ret)                                           \
  if (XR_FAILED(res)) {                                                        \
    Con_Printf("OpenXR Error: %s (%s)\n", msg, VR_OpenXR_ErrorString(res));    \
    return ret;                                                                \
  }

#endif // VR_ENABLED
#endif // __VR_OPENXR_H
