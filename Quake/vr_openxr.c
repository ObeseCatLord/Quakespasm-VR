// OpenXR Subsystem for IronwailVR
// Handles instance, session, swapchain lifecycle, pose tracking, and input.

#include "vr_openxr.h"
#include "quakedef.h"
#include "vr.h"

#ifdef VR_ENABLED

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_RGB10_A2
#define GL_RGB10_A2 0x8059
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif

extern cvar_t gl_farclip;

vr_openxr_state_t oxr;

vec3_t vr_room_scale_move;
static XrVector3f last_head_pos;
static int head_pos_initialized = false;

static float vr_yaw_offset = 0.0f;
static qboolean vr_reset_requested = false;

// CVARs

cvar_t vr_enabled = {"vr_enabled", "0", CVAR_ARCHIVE};
cvar_t vr_aimmode = {"vr_aimmode", "7", CVAR_ARCHIVE};
cvar_t vr_movement_mode = {"vr_movement_mode", "0", CVAR_ARCHIVE};
cvar_t vr_world_scale = {"vr_world_scale", "40.0", CVAR_ARCHIVE};
cvar_t vr_floor_offset = {"vr_floor_offset", "0.0", CVAR_ARCHIVE};
cvar_t vr_snap_turn = {"vr_snap_turn", "45", CVAR_ARCHIVE};
cvar_t vr_turn_speed = {"vr_turn_speed", "100.0", CVAR_ARCHIVE};
cvar_t vr_hud_scale = {"vr_hud_scale", "1.0", CVAR_ARCHIVE};
cvar_t vr_hud_dist = {"vr_hud_dist", "60.0", CVAR_ARCHIVE};
cvar_t vr_hud_voffset = {"vr_hud_voffset", "-15.0", CVAR_ARCHIVE};
cvar_t vr_menu_scale = {"vr_menu_scale", "1.0", CVAR_ARCHIVE};
cvar_t vr_menu_dist = {"vr_menu_dist", "50.0", CVAR_ARCHIVE};
cvar_t vr_vanilla_compat = {"vr_vanilla_compat", "0", CVAR_ARCHIVE};
cvar_t vr_mirror = {"vr_mirror", "1", CVAR_ARCHIVE};	// blit left-eye view to the desktop window

// Weapon offsets
cvar_t vr_gunangle = {"vr_gunangle", "32", CVAR_ARCHIVE};
cvar_t vr_gunmodeloffsets = {"vr_gunmodeloffsets", "0", CVAR_ARCHIVE};
cvar_t vr_gunmodelpitch = {"vr_gunmodelpitch", "0", CVAR_ARCHIVE};
cvar_t vr_gunmodelscale = {"vr_gunmodelscale", "1.0", CVAR_ARCHIVE};
cvar_t vr_gunmodely = {"vr_gunmodely", "0", CVAR_ARCHIVE};

static void VR_GunModelOffsets_Callback(cvar_t *var) { InitAllWeaponCVars(); }
static void VR_OpenXR_CreateSwapchain(int eye);

void VR_InitCommands(void) {
  Cvar_RegisterVariable(&vr_enabled);
  Cvar_RegisterVariable(&vr_aimmode);
  Cvar_RegisterVariable(&vr_movement_mode);
  Cvar_RegisterVariable(&vr_world_scale);
  Cvar_RegisterVariable(&vr_floor_offset);
  Cvar_RegisterVariable(&vr_snap_turn);
  Cvar_RegisterVariable(&vr_turn_speed);
  Cvar_RegisterVariable(&vr_hud_scale);
  Cvar_RegisterVariable(&vr_hud_dist);
  Cvar_RegisterVariable(&vr_hud_voffset);
  Cvar_RegisterVariable(&vr_menu_scale);
  Cvar_RegisterVariable(&vr_menu_dist);
  Cvar_RegisterVariable(&vr_vanilla_compat);
  Cvar_RegisterVariable(&vr_mirror);

  Cvar_RegisterVariable(&vr_gunangle);
  Cvar_RegisterVariable(&vr_gunmodeloffsets);
  vr_gunmodeloffsets.callback = VR_GunModelOffsets_Callback;
  Cvar_RegisterVariable(&vr_gunmodelpitch);
  Cvar_RegisterVariable(&vr_gunmodelscale);
  Cvar_RegisterVariable(&vr_gunmodely);

  InitAllWeaponCVars();
}

// Error string helper
const char *VR_OpenXR_ErrorString(XrResult result) {
  static char buf[64];
  if (oxr.instance != XR_NULL_HANDLE)
    xrResultToString(oxr.instance, result, buf);
  else
    q_snprintf(buf, sizeof(buf), "0x%x", (unsigned)result);
  return buf;
}

// ============================================================
// Swapchain
// ============================================================

static void VR_OpenXR_HandleSessionStateChange(XrSessionState state) {
  oxr.state = state;
  switch (state) {
  case XR_SESSION_STATE_READY: {
    XrSessionBeginInfo beginInfo = {XR_TYPE_SESSION_BEGIN_INFO};
    beginInfo.primaryViewConfigurationType = oxr.view_config_type;
    XrResult res = xrBeginSession(oxr.session, &beginInfo);
    OXR_CHECK(res, "Failed to begin session");
    oxr.session_running = true;

    if (!oxr.swapchains) {
      oxr.swapchains =
          (vr_swapchain_t *)malloc(sizeof(vr_swapchain_t) * oxr.view_count);
      memset(oxr.swapchains, 0, sizeof(vr_swapchain_t) * oxr.view_count);
      for (uint32_t i = 0; i < oxr.view_count; i++)
        VR_OpenXR_CreateSwapchain(i);
      VR_InitUI();
    }
    break;
  }
  case XR_SESSION_STATE_STOPPING: {
    xrEndSession(oxr.session);
    oxr.session_running = false;
    break;
  }
  case XR_SESSION_STATE_LOSS_PENDING:
  case XR_SESSION_STATE_EXITING:
    break;
  default:
    break;
  }
}

void VR_OpenXR_PollEvents(void) {
  XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};
  while (xrPollEvent(oxr.instance, &event) == XR_SUCCESS) {
    switch (event.type) {
    case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
      XrEventDataSessionStateChanged *sessionStateChangedEvent =
          (XrEventDataSessionStateChanged *)&event;
      VR_OpenXR_HandleSessionStateChange(sessionStateChangedEvent->state);
      break;
    }
    case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
      break;
    default:
      break;
    }
    event.type = XR_TYPE_EVENT_DATA_BUFFER;
  }
}

static void VR_OpenXR_CreateSwapchain(int eye) {
  vr_swapchain_t *sc = &oxr.swapchains[eye];
  XrViewConfigurationView *view = &oxr.view_config_views[eye];

  sc->width = view->recommendedImageRectWidth;
  sc->height = view->recommendedImageRectHeight;

  Con_Printf("VR_OpenXR_CreateSwapchain: eye %d, %dx%d\n", eye, sc->width,
             sc->height);

  uint32_t format_count;
  xrEnumerateSwapchainFormats(oxr.session, 0, &format_count, NULL);
  int64_t *formats = (int64_t *)malloc(sizeof(int64_t) * format_count);
  xrEnumerateSwapchainFormats(oxr.session, format_count, &format_count,
                              formats);

  int64_t format = formats[0];
  for (uint32_t i = 0; i < format_count; i++) {
    if (formats[i] == GL_SRGB8_ALPHA8 || formats[i] == GL_RGBA8) {
      format = formats[i];
      break;
    }
  }
  free(formats);

  XrSwapchainCreateInfo createInfo = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
  createInfo.usageFlags =
      XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
  createInfo.format = format;
  createInfo.sampleCount = view->recommendedSwapchainSampleCount;
  createInfo.width = sc->width;
  createInfo.height = sc->height;
  createInfo.faceCount = 1;
  createInfo.arraySize = 1;
  createInfo.mipCount = 1;

  XrResult res = xrCreateSwapchain(oxr.session, &createInfo, &sc->handle);
  OXR_CHECK(res, "Failed to create swapchain");

  res = xrEnumerateSwapchainImages(sc->handle, 0, &sc->image_count, NULL);
  OXR_CHECK(res, "Failed to count swapchain images");

  sc->images = (XrSwapchainImageOpenGLKHR *)malloc(
      sizeof(XrSwapchainImageOpenGLKHR) * sc->image_count);
  for (uint32_t i = 0; i < sc->image_count; i++) {
    sc->images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
    sc->images[i].next = NULL;
  }
  res =
      xrEnumerateSwapchainImages(sc->handle, sc->image_count, &sc->image_count,
                                 (XrSwapchainImageBaseHeader *)sc->images);
  OXR_CHECK(res, "Failed to enumerate swapchain images");

  sc->fbos = (GLuint *)malloc(sizeof(GLuint) * sc->image_count);
  GL_GenFramebuffersFunc(sc->image_count, sc->fbos);

  sc->depth_textures = (GLuint *)malloc(sizeof(GLuint) * sc->image_count);
  glGenTextures(sc->image_count, sc->depth_textures);

  for (uint32_t i = 0; i < sc->image_count; i++) {
    GL_BindNative(GL_TEXTURE0, GL_TEXTURE_2D, sc->depth_textures[i]);
    GL_TexStorage2DFunc(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT24, sc->width,
                        sc->height);
  }

  for (uint32_t i = 0; i < sc->image_count; i++) {
    GL_BindFramebufferFunc(GL_FRAMEBUFFER, sc->fbos[i]);
    GL_FramebufferTexture2DFunc(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, sc->images[i].image, 0);
    GL_FramebufferTexture2DFunc(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_TEXTURE_2D, sc->depth_textures[i], 0);

    GLenum status = GL_CheckFramebufferStatusFunc(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
      Con_Printf("VR_OpenXR_CreateSwapchain: FBO %d NOT COMPLETE (0x%x)\n", i,
                 status);
  }

  // Create depth swapchain if supported
  if (oxr.depth_extension_supported) {
    XrSwapchainCreateInfo depthInfo = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    depthInfo.usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthInfo.format = GL_DEPTH_COMPONENT24; // Standard Quake depth
    depthInfo.sampleCount = createInfo.sampleCount;
    depthInfo.width = sc->width;
    depthInfo.height = sc->height;
    depthInfo.faceCount = 1;
    depthInfo.arraySize = 1;
    depthInfo.mipCount = 1;

    res = xrCreateSwapchain(oxr.session, &depthInfo, &sc->depth_handle);
    if (XR_SUCCEEDED(res)) {
      xrEnumerateSwapchainImages(sc->depth_handle, 0, &sc->image_count, NULL);
      sc->depth_images =
          malloc(sizeof(XrSwapchainImageOpenGLKHR) * sc->image_count);
      for (uint32_t i = 0; i < sc->image_count; i++) {
        sc->depth_images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
        sc->depth_images[i].next = NULL;
      }
      xrEnumerateSwapchainImages(
          sc->depth_handle, sc->image_count, &sc->image_count,
          (XrSwapchainImageBaseHeader *)sc->depth_images);
    } else {
      oxr.depth_extension_supported = false;
    }
  }

  GL_BindFramebufferFunc(GL_FRAMEBUFFER, 0);
}

static void VR_OpenXR_DestroySwapchain(int eye) {
  vr_swapchain_t *sc = &oxr.swapchains[eye];
  if (sc->handle != XR_NULL_HANDLE)
    xrDestroySwapchain(sc->handle);
  if (sc->depth_handle != XR_NULL_HANDLE)
    xrDestroySwapchain(sc->depth_handle);
  if (sc->fbos) {
    GL_DeleteFramebuffersFunc(sc->image_count, sc->fbos);
    free(sc->fbos);
  }
  if (sc->depth_textures) {
    glDeleteTextures(sc->image_count, sc->depth_textures);
    free(sc->depth_textures);
  }
  if (sc->images)
    free(sc->images);
  if (sc->depth_images)
    free(sc->depth_images);
  memset(sc, 0, sizeof(vr_swapchain_t));
}

// ============================================================
// Actions (Phase 5)
// ============================================================

void VR_OpenXR_InitActions(void) {
  if (!oxr.initialized)
    return;

  Con_Printf("VR_OpenXR_InitActions: Creating action set...\n");

  // Create main action set
  XrActionSetCreateInfo actionSetInfo = {XR_TYPE_ACTION_SET_CREATE_INFO};
  q_strlcpy(actionSetInfo.actionSetName, "gameplay",
            XR_MAX_ACTION_SET_NAME_SIZE);
  q_strlcpy(actionSetInfo.localizedActionSetName, "Gameplay",
            XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE);
  actionSetInfo.priority = 0;

  XrResult res =
      xrCreateActionSet(oxr.instance, &actionSetInfo, &oxr.action_set);
  if (XR_FAILED(res)) {
    Con_Printf("VR_OpenXR_InitActions: Failed to create action set (%s)\n",
               VR_OpenXR_ErrorString(res));
    return;
  }

  // Hand subaction paths (for per-hand bindings)
  xrStringToPath(oxr.instance, "/user/hand/left",
                 &oxr.hand_subaction_path[VR_HAND_LEFT]);
  xrStringToPath(oxr.instance, "/user/hand/right",
                 &oxr.hand_subaction_path[VR_HAND_RIGHT]);

  // --- Hand pose actions ---
  {
    XrActionCreateInfo info = {XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = XR_ACTION_TYPE_POSE_INPUT;
    q_strlcpy(info.actionName, "hand_pose", XR_MAX_ACTION_NAME_SIZE);
    q_strlcpy(info.localizedActionName, "Hand Pose",
              XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
    info.countSubactionPaths = 2;
    info.subactionPaths = oxr.hand_subaction_path;
    xrCreateAction(oxr.action_set, &info, &oxr.hand_pose_action);
  }

  // --- Thumbstick (locomotion) ---
  {
    XrActionCreateInfo info = {XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
    q_strlcpy(info.actionName, "thumbstick", XR_MAX_ACTION_NAME_SIZE);
    q_strlcpy(info.localizedActionName, "Thumbstick",
              XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
    info.countSubactionPaths = 2;
    info.subactionPaths = oxr.hand_subaction_path;
    xrCreateAction(oxr.action_set, &info, &oxr.thumbstick_action);
  }

  // --- Trigger (fire) ---
  {
    XrActionCreateInfo info = {XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    q_strlcpy(info.actionName, "trigger", XR_MAX_ACTION_NAME_SIZE);
    q_strlcpy(info.localizedActionName, "Trigger",
              XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
    info.countSubactionPaths = 2;
    info.subactionPaths = oxr.hand_subaction_path;
    xrCreateAction(oxr.action_set, &info, &oxr.trigger_action);
  }

  // --- Grip squeeze (use / interact) ---
  {
    XrActionCreateInfo info = {XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    q_strlcpy(info.actionName, "grip", XR_MAX_ACTION_NAME_SIZE);
    q_strlcpy(info.localizedActionName, "Grip",
              XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
    info.countSubactionPaths = 2;
    info.subactionPaths = oxr.hand_subaction_path;
    xrCreateAction(oxr.action_set, &info, &oxr.grip_action);
  }

  // --- A/B buttons (jump / weapon wheel) ---
  {
    XrActionCreateInfo info = {XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    q_strlcpy(info.actionName, "button_a", XR_MAX_ACTION_NAME_SIZE);
    q_strlcpy(info.localizedActionName, "Button A",
              XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
    xrCreateAction(oxr.action_set, &info, &oxr.button_a_action);

    q_strlcpy(info.actionName, "button_b", XR_MAX_ACTION_NAME_SIZE);
    q_strlcpy(info.localizedActionName, "Button B",
              XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
    xrCreateAction(oxr.action_set, &info, &oxr.button_b_action);

    q_strlcpy(info.actionName, "haptic", XR_MAX_ACTION_NAME_SIZE);
    q_strlcpy(info.localizedActionName, "Haptic feedback",
              XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
    info.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
    info.countSubactionPaths = 2;
    info.subactionPaths = oxr.hand_subaction_path;
    xrCreateAction(oxr.action_set, &info, &oxr.haptic_action);
  }

  // --- Suggest bindings for Oculus Touch ---
  XrPath pose_l, pose_r, stick_l, stick_r, trig_l, trig_r, grip_l, grip_r;
  XrPath haptic_l, haptic_r;
  xrStringToPath(oxr.instance, "/user/hand/left/input/grip/pose", &pose_l);
  xrStringToPath(oxr.instance, "/user/hand/right/input/grip/pose", &pose_r);
  xrStringToPath(oxr.instance, "/user/hand/left/input/thumbstick", &stick_l);
  xrStringToPath(oxr.instance, "/user/hand/right/input/thumbstick", &stick_r);
  xrStringToPath(oxr.instance, "/user/hand/left/input/trigger/value", &trig_l);
  xrStringToPath(oxr.instance, "/user/hand/right/input/trigger/value", &trig_r);
  xrStringToPath(oxr.instance, "/user/hand/left/input/squeeze/value", &grip_l);
  xrStringToPath(oxr.instance, "/user/hand/right/input/squeeze/value", &grip_r);
  xrStringToPath(oxr.instance, "/user/hand/left/output/haptic", &haptic_l);
  xrStringToPath(oxr.instance, "/user/hand/right/output/haptic", &haptic_r);

  XrPath a_path, b_path;
  xrStringToPath(oxr.instance, "/user/hand/right/input/a/click", &a_path);
  xrStringToPath(oxr.instance, "/user/hand/right/input/b/click", &b_path);

  // Oculus Touch bindings
  {
    XrActionSuggestedBinding bindings[] = {
        {oxr.hand_pose_action, pose_l},   {oxr.hand_pose_action, pose_r},
        {oxr.thumbstick_action, stick_l}, {oxr.thumbstick_action, stick_r},
        {oxr.trigger_action, trig_l},     {oxr.trigger_action, trig_r},
        {oxr.grip_action, grip_l},        {oxr.grip_action, grip_r},
        {oxr.button_a_action, a_path},    {oxr.button_b_action, b_path},
        {oxr.haptic_action, haptic_l},    {oxr.haptic_action, haptic_r},
    };
    XrPath profile;
    xrStringToPath(oxr.instance,
                   "/interaction_profiles/oculus/touch_controller", &profile);
    XrInteractionProfileSuggestedBinding suggested = {
        XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = profile;
    suggested.suggestedBindings = bindings;
    suggested.countSuggestedBindings =
        (uint32_t)(sizeof(bindings) / sizeof(bindings[0]));
    res = xrSuggestInteractionProfileBindings(oxr.instance, &suggested);
    if (XR_FAILED(res))
      Con_Printf("VR: Oculus Touch bindings failed (%s)\n",
                 VR_OpenXR_ErrorString(res));
  }

  // Valve Index bindings (has A/B, squeeze/force, thumbstick)
  {
    XrPath idx_a, idx_b, idx_squeeze_l, idx_squeeze_r;
    xrStringToPath(oxr.instance, "/user/hand/right/input/a/click", &idx_a);
    xrStringToPath(oxr.instance, "/user/hand/right/input/b/click", &idx_b);
    xrStringToPath(oxr.instance, "/user/hand/left/input/squeeze/force",
                   &idx_squeeze_l);
    xrStringToPath(oxr.instance, "/user/hand/right/input/squeeze/force",
                   &idx_squeeze_r);
    XrActionSuggestedBinding bindings[] = {
        {oxr.hand_pose_action, pose_l},   {oxr.hand_pose_action, pose_r},
        {oxr.thumbstick_action, stick_l}, {oxr.thumbstick_action, stick_r},
        {oxr.trigger_action, trig_l},     {oxr.trigger_action, trig_r},
        {oxr.grip_action, idx_squeeze_l}, {oxr.grip_action, idx_squeeze_r},
        {oxr.button_a_action, idx_a},     {oxr.button_b_action, idx_b},
        {oxr.haptic_action, haptic_l},    {oxr.haptic_action, haptic_r},
    };
    XrPath profile;
    xrStringToPath(oxr.instance, "/interaction_profiles/valve/index_controller",
                   &profile);
    XrInteractionProfileSuggestedBinding suggested = {
        XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = profile;
    suggested.suggestedBindings = bindings;
    suggested.countSuggestedBindings =
        (uint32_t)(sizeof(bindings) / sizeof(bindings[0]));
    res = xrSuggestInteractionProfileBindings(oxr.instance, &suggested);
    if (XR_FAILED(res))
      Con_Printf("VR: Index bindings failed (%s)\n",
                 VR_OpenXR_ErrorString(res));
  }

  // HTC Vive bindings (trackpad instead of thumbstick, squeeze/click,
  // menu/click)
  {
    XrPath vive_track_l, vive_track_r, vive_squeeze_l, vive_squeeze_r;
    XrPath vive_menu_l, vive_menu_r;
    xrStringToPath(oxr.instance, "/user/hand/left/input/trackpad",
                   &vive_track_l);
    xrStringToPath(oxr.instance, "/user/hand/right/input/trackpad",
                   &vive_track_r);
    xrStringToPath(oxr.instance, "/user/hand/left/input/squeeze/click",
                   &vive_squeeze_l);
    xrStringToPath(oxr.instance, "/user/hand/right/input/squeeze/click",
                   &vive_squeeze_r);
    xrStringToPath(oxr.instance, "/user/hand/left/input/menu/click",
                   &vive_menu_l);
    xrStringToPath(oxr.instance, "/user/hand/right/input/menu/click",
                   &vive_menu_r);
    XrActionSuggestedBinding bindings[] = {
        {oxr.hand_pose_action, pose_l},
        {oxr.hand_pose_action, pose_r},
        {oxr.thumbstick_action, vive_track_l},
        {oxr.thumbstick_action, vive_track_r},
        {oxr.trigger_action, trig_l},
        {oxr.trigger_action, trig_r},
        {oxr.grip_action, vive_squeeze_l},
        {oxr.grip_action, vive_squeeze_r},
        {oxr.button_a_action, vive_menu_l},
        {oxr.button_b_action, vive_menu_r},
        {oxr.haptic_action, haptic_l},
        {oxr.haptic_action, haptic_r},
    };
    XrPath profile;
    xrStringToPath(oxr.instance, "/interaction_profiles/htc/vive_controller",
                   &profile);
    XrInteractionProfileSuggestedBinding suggested = {
        XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = profile;
    suggested.suggestedBindings = bindings;
    suggested.countSuggestedBindings =
        (uint32_t)(sizeof(bindings) / sizeof(bindings[0]));
    res = xrSuggestInteractionProfileBindings(oxr.instance, &suggested);
    if (XR_FAILED(res))
      Con_Printf("VR: Vive bindings failed (%s)\n", VR_OpenXR_ErrorString(res));
  }

  // WMR bindings (thumbstick, trigger, squeeze, menu)
  {
    XrPath wmr_squeeze_l, wmr_squeeze_r, wmr_menu_l, wmr_menu_r;
    xrStringToPath(oxr.instance, "/user/hand/left/input/squeeze/click",
                   &wmr_squeeze_l);
    xrStringToPath(oxr.instance, "/user/hand/right/input/squeeze/click",
                   &wmr_squeeze_r);
    xrStringToPath(oxr.instance, "/user/hand/left/input/menu/click",
                   &wmr_menu_l);
    xrStringToPath(oxr.instance, "/user/hand/right/input/menu/click",
                   &wmr_menu_r);
    XrActionSuggestedBinding bindings[] = {
        {oxr.hand_pose_action, pose_l},    {oxr.hand_pose_action, pose_r},
        {oxr.thumbstick_action, stick_l},  {oxr.thumbstick_action, stick_r},
        {oxr.trigger_action, trig_l},      {oxr.trigger_action, trig_r},
        {oxr.grip_action, wmr_squeeze_l},  {oxr.grip_action, wmr_squeeze_r},
        {oxr.button_a_action, wmr_menu_l}, {oxr.button_b_action, wmr_menu_r},
        {oxr.haptic_action, haptic_l},     {oxr.haptic_action, haptic_r},
    };
    XrPath profile;
    xrStringToPath(oxr.instance,
                   "/interaction_profiles/microsoft/motion_controller",
                   &profile);
    XrInteractionProfileSuggestedBinding suggested = {
        XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = profile;
    suggested.suggestedBindings = bindings;
    suggested.countSuggestedBindings =
        (uint32_t)(sizeof(bindings) / sizeof(bindings[0]));
    res = xrSuggestInteractionProfileBindings(oxr.instance, &suggested);
    if (XR_FAILED(res))
      Con_Printf("VR: WMR bindings failed (%s)\n", VR_OpenXR_ErrorString(res));
  }

  // Khronos Simple Controller (only select/click and menu/click, no thumbstick)
  {
    XrPath sim_pose_l, sim_pose_r, sim_select_l, sim_select_r;
    XrPath sim_menu_l, sim_menu_r, sim_haptic_l, sim_haptic_r;
    xrStringToPath(oxr.instance, "/user/hand/left/input/grip/pose",
                   &sim_pose_l);
    xrStringToPath(oxr.instance, "/user/hand/right/input/grip/pose",
                   &sim_pose_r);
    xrStringToPath(oxr.instance, "/user/hand/left/input/select/click",
                   &sim_select_l);
    xrStringToPath(oxr.instance, "/user/hand/right/input/select/click",
                   &sim_select_r);
    xrStringToPath(oxr.instance, "/user/hand/left/input/menu/click",
                   &sim_menu_l);
    xrStringToPath(oxr.instance, "/user/hand/right/input/menu/click",
                   &sim_menu_r);
    xrStringToPath(oxr.instance, "/user/hand/left/output/haptic",
                   &sim_haptic_l);
    xrStringToPath(oxr.instance, "/user/hand/right/output/haptic",
                   &sim_haptic_r);
    XrActionSuggestedBinding bindings[] = {
        {oxr.hand_pose_action, sim_pose_l}, {oxr.hand_pose_action, sim_pose_r},
        {oxr.trigger_action, sim_select_l}, {oxr.trigger_action, sim_select_r},
        {oxr.button_a_action, sim_menu_l},  {oxr.button_b_action, sim_menu_r},
        {oxr.haptic_action, sim_haptic_l},  {oxr.haptic_action, sim_haptic_r},
    };
    XrPath profile;
    xrStringToPath(oxr.instance, "/interaction_profiles/khr/simple_controller",
                   &profile);
    XrInteractionProfileSuggestedBinding suggested = {
        XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = profile;
    suggested.suggestedBindings = bindings;
    suggested.countSuggestedBindings =
        (uint32_t)(sizeof(bindings) / sizeof(bindings[0]));
    res = xrSuggestInteractionProfileBindings(oxr.instance, &suggested);
    if (XR_FAILED(res))
      Con_Printf("VR: Simple controller bindings failed (%s)\n",
                 VR_OpenXR_ErrorString(res));
  }

  // Attach action set to session
  XrSessionActionSetsAttachInfo attachInfo = {
      XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
  attachInfo.actionSets = &oxr.action_set;
  attachInfo.countActionSets = 1;
  res = xrAttachSessionActionSets(oxr.session, &attachInfo);
  if (XR_FAILED(res))
    Con_Printf("VR_OpenXR_InitActions: Failed to attach action set (%s)\n",
               VR_OpenXR_ErrorString(res));

  // Create action spaces for grip poses
  XrActionSpaceCreateInfo spaceInfo = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
  spaceInfo.action = oxr.hand_pose_action;
  spaceInfo.poseInActionSpace.orientation.w = 1.0f;

  spaceInfo.subactionPath = oxr.hand_subaction_path[VR_HAND_LEFT];
  xrCreateActionSpace(oxr.session, &spaceInfo, &oxr.hand_space[VR_HAND_LEFT]);

  spaceInfo.subactionPath = oxr.hand_subaction_path[VR_HAND_RIGHT];
  xrCreateActionSpace(oxr.session, &spaceInfo, &oxr.hand_space[VR_HAND_RIGHT]);

  oxr.actions_initialized = true;
  Con_Printf("VR_OpenXR_InitActions: SUCCESS\n");
}

// ============================================================
// Init / Shutdown
// ============================================================

void VR_OpenXR_Init(void) {
  if (!vr_enabled.value)
    return;
  if (oxr.initialized)
    return;

  Con_Printf("VR_OpenXR_Init: Initializing OpenXR...\n");

  uint32_t ext_count = 0;
  xrEnumerateInstanceExtensionProperties(NULL, 0, &ext_count, NULL);
  XrExtensionProperties *ext_props =
      malloc(sizeof(XrExtensionProperties) * ext_count);
  for (uint32_t i = 0; i < ext_count; i++)
    ext_props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
  xrEnumerateInstanceExtensionProperties(NULL, ext_count, &ext_count,
                                         ext_props);

  const char *enabled_exts[4];
  uint32_t enabled_ext_count = 0;
  enabled_exts[enabled_ext_count++] = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;

  for (uint32_t i = 0; i < ext_count; i++) {
    if (strcmp(ext_props[i].extensionName, "XR_KHR_composition_layer_depth") ==
        0) {
      enabled_exts[enabled_ext_count++] = "XR_KHR_composition_layer_depth";
      oxr.depth_extension_supported = true;
      break;
    }
  }
  free(ext_props);

  XrInstanceCreateInfo createInfo = {XR_TYPE_INSTANCE_CREATE_INFO};
  q_strlcpy(createInfo.applicationInfo.applicationName, "IronwailVR",
            XR_MAX_APPLICATION_NAME_SIZE);
  createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
  createInfo.enabledExtensionCount = enabled_ext_count;
  createInfo.enabledExtensionNames = enabled_exts;

  XrResult res = xrCreateInstance(&createInfo, &oxr.instance);
  OXR_CHECK(res, "Failed to create OpenXR instance");

  // Initialize profiling queries
  GL_GenQueriesFunc(8, oxr.queries);

  XrSystemGetInfo systemInfo = {XR_TYPE_SYSTEM_GET_INFO};
  systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  res = xrGetSystem(oxr.instance, &systemInfo, &oxr.system_id);
  OXR_CHECK(res, "Failed to get OpenXR system");

  XrGraphicsRequirementsOpenGLKHR graphicsRequirements = {
      XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
  PFN_xrGetOpenGLGraphicsRequirementsKHR pfnGetOpenGLGraphicsRequirementsKHR =
      NULL;
  res = xrGetInstanceProcAddr(
      oxr.instance, "xrGetOpenGLGraphicsRequirementsKHR",
      (PFN_xrVoidFunction *)&pfnGetOpenGLGraphicsRequirementsKHR);
  OXR_CHECK(res, "Failed to get xrGetOpenGLGraphicsRequirementsKHR proc addr");
  res = pfnGetOpenGLGraphicsRequirementsKHR(oxr.instance, oxr.system_id,
                                            &graphicsRequirements);
  OXR_CHECK(res, "Failed to get OpenGL graphics requirements");

  XrGraphicsBindingOpenGL graphicsBinding = {XR_TYPE_GRAPHICS_BINDING_OPENGL};
#ifdef _WIN32
  graphicsBinding.hDC = wglGetCurrentDC();
  graphicsBinding.hGLRC = wglGetCurrentContext();
#else
  graphicsBinding.xDisplay = glXGetCurrentDisplay();
  graphicsBinding.glxContext = glXGetCurrentContext();
  graphicsBinding.glxDrawable = glXGetCurrentDrawable();

  // Populate visualid and FBConfig if possible (required by some runtimes)
  int fbconfigid = 0;
  glXQueryContext(graphicsBinding.xDisplay, graphicsBinding.glxContext,
                  GLX_FBCONFIG_ID, &fbconfigid);
  if (fbconfigid > 0) {
    int nconfigs = 0;
    int attribs[] = {GLX_FBCONFIG_ID, fbconfigid, None};
    GLXFBConfig *configs = glXChooseFBConfig(
        graphicsBinding.xDisplay, DefaultScreen(graphicsBinding.xDisplay),
        attribs, &nconfigs);
    if (nconfigs > 0) {
      graphicsBinding.glxFBConfig = configs[0];
      glXGetConfig(
          graphicsBinding.xDisplay,
          glXGetVisualFromFBConfig(graphicsBinding.xDisplay, configs[0]),
          GLX_VISUAL_ID, (int *)&graphicsBinding.visualid);
      XFree(configs);
    }
  }
#endif

  XrSessionCreateInfo sessionCreateInfo = {XR_TYPE_SESSION_CREATE_INFO};
  sessionCreateInfo.next = &graphicsBinding;
  sessionCreateInfo.systemId = oxr.system_id;
  res = xrCreateSession(oxr.instance, &sessionCreateInfo, &oxr.session);
  OXR_CHECK(res, "Failed to create OpenXR session");

  XrReferenceSpaceCreateInfo spaceCreateInfo = {
      XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;

  spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
  res = xrCreateReferenceSpace(oxr.session, &spaceCreateInfo, &oxr.stage_space);
  OXR_CHECK(res, "Failed to create stage space");

  spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  res = xrCreateReferenceSpace(oxr.session, &spaceCreateInfo, &oxr.local_space);
  OXR_CHECK(res, "Failed to create local space");

  // Views config (needed for session begin later)
  oxr.view_config_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  res = xrEnumerateViewConfigurationViews(oxr.instance, oxr.system_id,
                                          oxr.view_config_type, 0,
                                          &oxr.view_count, NULL);
  OXR_CHECK(res, "Failed to count view configuration views");

  oxr.view_config_views = (XrViewConfigurationView *)malloc(
      sizeof(XrViewConfigurationView) * oxr.view_count);
  for (uint32_t i = 0; i < oxr.view_count; i++) {
    oxr.view_config_views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    oxr.view_config_views[i].next = NULL;
  }
  res = xrEnumerateViewConfigurationViews(
      oxr.instance, oxr.system_id, oxr.view_config_type, oxr.view_count,
      &oxr.view_count, oxr.view_config_views);
  OXR_CHECK(res, "Failed to enumerate view configuration views");

  oxr.views = (XrView *)malloc(sizeof(XrView) * oxr.view_count);
  for (uint32_t i = 0; i < oxr.view_count; i++) {
    oxr.views[i].type = XR_TYPE_VIEW;
    oxr.views[i].next = NULL;
  }

  oxr.blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  oxr.initialized = true;
  Con_Printf("VR_OpenXR_Init: SUCCESS (waiting for READY state)\n");
}

void VR_OpenXR_Shutdown(void) {
  if (!oxr.initialized)
    return;

  if (oxr.hand_space[VR_HAND_LEFT] != XR_NULL_HANDLE)
    xrDestroySpace(oxr.hand_space[VR_HAND_LEFT]);
  if (oxr.hand_space[VR_HAND_RIGHT] != XR_NULL_HANDLE)
    xrDestroySpace(oxr.hand_space[VR_HAND_RIGHT]);
  if (oxr.action_set != XR_NULL_HANDLE)
    xrDestroyActionSet(oxr.action_set);

  if (oxr.swapchains) {
    for (uint32_t i = 0; i < oxr.view_count; i++)
      VR_OpenXR_DestroySwapchain(i);
    free(oxr.swapchains);
    oxr.swapchains = NULL;
  }
  if (oxr.view_config_views) {
    free(oxr.view_config_views);
    oxr.view_config_views = NULL;
  }
  if (oxr.views) {
    free(oxr.views);
    oxr.views = NULL;
  }

  if (oxr.session_running && oxr.session != XR_NULL_HANDLE) {
    xrEndSession(oxr.session);
    oxr.session_running = false;
  }

  if (oxr.stage_space != XR_NULL_HANDLE)
    xrDestroySpace(oxr.stage_space);
  if (oxr.local_space != XR_NULL_HANDLE)
    xrDestroySpace(oxr.local_space);
  if (oxr.session != XR_NULL_HANDLE)
    xrDestroySession(oxr.session);
  if (oxr.instance != XR_NULL_HANDLE)
    xrDestroyInstance(oxr.instance);

  VR_ShutdownUI();

  memset(&oxr, 0, sizeof(oxr));
  Con_Printf("VR_OpenXR_Shutdown: done\n");
}

qboolean VR_Enabled(void) {
  return (qboolean)(oxr.initialized && vr_enabled.value != 0);
}

void VR_ResetOrientation(void) { vr_reset_requested = true; }

// ============================================================
// Phase 4: Pose Tracking
// ============================================================

/*
 * VR_PoseToQuakeAngles
 * Convert an OpenXR quaternion to Quake Euler angles (degrees).
 * OpenXR: right-handed, Y-up    Quake: right-handed, Z-up
 * Mapping: OXR(x, y, z, w) → Quake pitch/yaw/roll
 */
static void VR_QuatToQuakeAngles(const XrQuaternionf *q, vec3_t angles) {
  float x = q->x, y = q->y, z = q->z, w = q->w;

  // Standard quaternion → Euler (roll/pitch/yaw in radians, Y-up)
  float sinr_cosp = 2.0f * (w * x + y * z);
  float cosr_cosp = 1.0f - 2.0f * (x * x + y * y);
  float roll_rad = atan2f(sinr_cosp, cosr_cosp);

  float sinp = 2.0f * (w * y - z * x);
  float pitch_rad;
  if (fabsf(sinp) >= 1.0f)
    pitch_rad = copysignf((float)M_PI / 2.0f, sinp);
  else
    pitch_rad = asinf(sinp);

  float siny_cosp = 2.0f * (w * z + x * y);
  float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
  float yaw_rad = atan2f(siny_cosp, cosy_cosp);

  // Convert to degrees and remap axes:
  // OXR pitch (rotation around X, tilt up/down) → Quake PITCH (index 0, negated
  // for convention) OXR yaw   (rotation around Y, turn left/right) → Quake YAW
  // (index 1, negated: OXR ccw=+, Quake cw=+) OXR roll  (rotation around Z) →
  // Quake ROLL (index 2)
  angles[PITCH] = -RAD2DEG(pitch_rad);
  angles[YAW] = -RAD2DEG(yaw_rad);
  angles[ROLL] = RAD2DEG(roll_rad);
}

/*
 * VR_UpdatePoses
 * Called once per frame (before VR_Render) to sync action state and
 * update the latest predicted head pose. Results stored in oxr.head_pose
 * and oxr.head_angles for use by VR_ApplyViewOverride.
 */
void VR_UpdatePoses(void) {
  if (!oxr.initialized)
    return;

  VR_OpenXR_PollEvents();

  if (!oxr.session_running)
    return;

  // Track room-scale movement delta
  if (head_pos_initialized) {
    float world_scale = vr_world_scale.value;
    if (world_scale <= 0.0f)
      world_scale = 40.0f;

    vr_room_scale_move[0] =
        (oxr.head_pose.position.x - last_head_pos.x) * world_scale;
    vr_room_scale_move[1] =
        -(oxr.head_pose.position.z - last_head_pos.z) * world_scale;
    vr_room_scale_move[2] = 0; // Quake physics handles Z (gravity/jumping)
  } else {
    VectorSet(vr_room_scale_move, 0, 0, 0);
    head_pos_initialized = true;
  }
  last_head_pos = oxr.head_pose.position;

  // Sync action state so we can read inputs this frame
  if (oxr.actions_initialized) {
    XrActiveActionSet activeSet = {0};
    activeSet.actionSet = oxr.action_set;
    activeSet.subactionPath = XR_NULL_PATH;

    XrActionsSyncInfo syncInfo = {XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.activeActionSets = &activeSet;
    syncInfo.countActiveActionSets = 1;
    xrSyncActions(oxr.session, &syncInfo);

    // --- Safety Overrides ---
    if (VR_Enabled()) {
      extern cvar_t r_waterwarp;
      extern cvar_t cl_bob;
      extern cvar_t v_idlescale;
      if (r_waterwarp.value != 0)
        Cvar_SetQuick(&r_waterwarp, "0");
      if (cl_bob.value != 0)
        Cvar_SetQuick(&cl_bob, "0");
      if (v_idlescale.value != 0)
        Cvar_SetQuick(&v_idlescale, "0");
    }

    // Sample hand grip poses
    for (int h = 0; h < 2; h++) {
      XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION};
      XrResult res = xrLocateSpace(oxr.hand_space[h], oxr.stage_space,
                                   oxr.last_predicted_time, &loc);
      if (XR_SUCCEEDED(res) &&
          (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
          (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
        oxr.hand_pose[h] = loc.pose;
        oxr.hand_pose_valid[h] = true;
      } else {
        oxr.hand_pose_valid[h] = false;
      }
    }
  }
}

/*
 * VR_ApplyViewOverride
 * Called at the end of V_CalcRefdef() to replace Quake's camera
 * position/angles with the HMD head pose.
 *
 * origin: r_refdef.vieworg  (Quake world units, Z-up)
 * angles: r_refdef.viewangles (pitch, yaw, roll in degrees)
 */
void VR_ApplyViewOverride(vec3_t origin, vec3_t angles) {
  if (!VR_Enabled())
    return;
  // We use views[0] as the "head center" reference.
  // views[] may not be filled yet on the very first frame — guard against that.
  if (oxr.view_count == 0)
    return;

  float world_scale = vr_world_scale.value;
  if (world_scale <= 0.0f)
    world_scale = 40.0f;

  const XrPosef *head = &oxr.head_pose;

  // OpenXR position (Y-up) → Quake position (Z-up)
  // scale converts metres to Quake units
  // OXR:   right = +X,  up = +Y,  forward = −Z
  // Quake: right = +Y,  up = +Z,  forward = +X
  float ox = head->position.x * world_scale;
  float oy = head->position.y * world_scale;
  float oz = head->position.z * world_scale;

  // Player body origin is already in r_refdef.vieworg at this point;
  // we add the HMD room-scale offset to it.
  // OXR X → Quake X (forward axis of player), OXR Z → Quake -Y, OXR Y → Quake Z
  origin[0] += ox;
  origin[1] -= oz;
  origin[2] += oy + vr_floor_offset.value;

  // Convert HMD quaternion → Quake Euler angles
  VR_QuatToQuakeAngles(&head->orientation, angles);

  if (vr_reset_requested) {
    vr_yaw_offset = angles[YAW];
    vr_reset_requested = false;
    Con_Printf("VR Orientation Reset\n");
  }

  angles[YAW] -= vr_yaw_offset;
}

/*
 * VR_SetMatrices
 * Called per-eye inside VR_Render. Sets r_matproj and r_matview.
 *
 * r_matview is built from the per-eye pose (position + orientation)
 * transformed into Quake's coordinate frame. The matrix is inverted
 * (transpose of rotation + negated position) to form a view matrix.
 */
void VR_SetMatrices(int eye) {
  extern float r_matproj[16];
  extern float r_matview[16];

  XrView *view = &oxr.views[eye];
  float world_scale = vr_world_scale.value;
  if (world_scale <= 0.0f)
    world_scale = 40.0f;

  // ---- Projection matrix ----
  float tan_l = tanf(view->fov.angleLeft);
  float tan_r = tanf(view->fov.angleRight);
  float tan_u = tanf(view->fov.angleUp);
  float tan_d = tanf(view->fov.angleDown);
  float zn = 4.0f;
  float zf = gl_farclip.value;

  memset(r_matproj, 0, 16 * sizeof(float));
  r_matproj[0] = 2.0f / (tan_r - tan_l);
  r_matproj[5] = 2.0f / (tan_u - tan_d);
  r_matproj[8] = (tan_r + tan_l) / (tan_r - tan_l);
  r_matproj[9] = (tan_u + tan_d) / (tan_u - tan_d);
  r_matproj[10] = -(zf + zn) / (zf - zn);
  r_matproj[11] = -1.0f;
  r_matproj[14] = -(2.0f * zf * zn) / (zf - zn);

  // ---- View matrix ----
  // Build rotation from quaternion, then invert (transpose) for view
  // Apply OpenXR→Quake axis swap as part of the rotation.
  const XrQuaternionf *q = &view->pose.orientation;
  float qx = q->x, qy = q->y, qz = q->z, qw = q->w;

  // 3x3 rotation matrix R from quaternion (row-major intermediate)
  float r00 = 1.0f - 2 * (qy * qy + qz * qz);
  float r01 = 2 * (qx * qy - qw * qz);
  float r02 = 2 * (qx * qz + qw * qy);
  float r10 = 2 * (qx * qy + qw * qz);
  float r11 = 1.0f - 2 * (qx * qx + qz * qz);
  float r12 = 2 * (qy * qz - qw * qx);
  float r20 = 2 * (qx * qz - qw * qy);
  float r21 = 2 * (qy * qz + qw * qx);
  float r22 = 1.0f - 2 * (qx * qx + qy * qy);

  // Eye position in Quake space
  float px = view->pose.position.x * world_scale;
  float py = view->pose.position.y * world_scale;
  float pz = view->pose.position.z * world_scale;

  // View matrix (column-major OpenGL) = transpose(R) | -R^T * t
  // With OpenXR→Quake remap applied:
  //   Quake X = OXR X, Quake Y = -OXR Z, Quake Z = OXR Y
  memset(r_matview, 0, 16 * sizeof(float));
  // Column 0: Quake right    = OXR +X row
  r_matview[0] = r00;
  r_matview[1] = r10;
  r_matview[2] = r20;
  // Column 1: Quake forward  = OXR -Z row (negate Z)
  r_matview[4] = -r02;
  r_matview[5] = -r12;
  r_matview[6] = -r22;
  // Column 2: Quake up       = OXR +Y row
  r_matview[8] = r01;
  r_matview[9] = r11;
  r_matview[10] = r21;
  // Translation
  r_matview[12] = -(r00 * px + r10 * py + r20 * pz);
  r_matview[13] = -(-r02 * px - r12 * py - r22 * pz);
  r_matview[14] = -(r01 * px + r11 * py + r21 * pz);
  r_matview[15] = 1.0f;
}

// ============================================================
// Phase 5: Locomotion - VR_Move
// ============================================================

/*
 * VR_Move
 * Reads thumbstick state and converts to Quake move commands.
 * Left stick: forward/strafe movement.
 * Right stick X: snap/smooth turning.
 *
 * Called from CL_SendMove (cl_input.c) via the existing VR_Move stub.
 */
void VR_UpdateWeaponSelection(void);
void VR_DrawWeaponWheel(void);

void VR_Move(usercmd_t *cmd) {
  if (!VR_Enabled())
    return;

  VR_UpdateWeaponSelection();
  if (!oxr.actions_initialized)
    return;

  float move_speed = cl_forwardspeed.value;
  float side_speed = cl_sidespeed.value;

  // --- Left stick: locomotion ---
  {
    XrActionStateVector2f state = {XR_TYPE_ACTION_STATE_VECTOR2F};
    XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO};
    getInfo.action = oxr.thumbstick_action;
    getInfo.subactionPath = oxr.hand_subaction_path[VR_HAND_LEFT];

    if (XR_SUCCEEDED(xrGetActionStateVector2f(oxr.session, &getInfo, &state)) &&
        state.isActive) {
      float fwd = state.currentState.y;  // up = forward
      float side = state.currentState.x; // right = strafe right

      // Apply small deadzone
      if (fabsf(fwd) < 0.1f)
        fwd = 0.0f;
      if (fabsf(side) < 0.1f)
        side = 0.0f;

      // Movement direction depends on movement mode
      if (vr_movement_mode.value == VR_MOVEMENT_MODE_FOLLOW_HEAD) {
        // Move relative to HMD yaw
        cmd->forwardmove += fwd * move_speed;
        cmd->sidemove += side * side_speed;
      } else if (vr_movement_mode.value == VR_MOVEMENT_MODE_FOLLOW_HAND) {
        // Move relative to left hand yaw
        vec3_t hpos, hangles, hfwd, hright;
        VR_GetHandPose(VR_HAND_LEFT, hpos, hangles);
        hangles[PITCH] = 0; // only yaw for movement
        AngleVectors(hangles, hfwd, hright, NULL);

        VectorMA(vec3_origin, fwd * move_speed, hfwd, hfwd);
        VectorMA(hfwd, side * side_speed, hright, hfwd);

        // Quake simplified movement: cmd->forwardmove/sidemove are axes
        // but since we're injecting into cl.pendingcmd, we should just
        // rotate the fwd/side vector by hand yaw.
        float yaw = hangles[YAW] * M_PI / 180.0f;
        float s = sin(yaw);
        float c = cos(yaw);
        cmd->forwardmove += (fwd * c - side * s) * move_speed;
        cmd->sidemove += (fwd * s + side * c) * side_speed;
      } else {
        // Raw: same as head-relative
        cmd->forwardmove += fwd * move_speed;
        cmd->sidemove += side * side_speed;
      }
    }
  }

  // --- Right stick: turning & snap 180 ---
  {
    XrActionStateVector2f state = {XR_TYPE_ACTION_STATE_VECTOR2F};
    XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO};
    getInfo.action = oxr.thumbstick_action;
    getInfo.subactionPath = oxr.hand_subaction_path[VR_HAND_RIGHT];

    static qboolean snap_pending = false;

    if (XR_SUCCEEDED(xrGetActionStateVector2f(oxr.session, &getInfo, &state)) &&
        state.isActive) {
      float turn_x = state.currentState.x;
      float turn_y = state.currentState.y;

      if (!in_vr_weaponmenu && vr_snap_turn.value > 0.0f) {
        // Snap 180: stick DOWN
        if (turn_y < -0.7f && !snap_pending) {
          cl.viewangles[YAW] -= 180.0f;
          snap_pending = true;
          VR_TriggerHaptic(VR_HAND_RIGHT, 0.1f, 0, 0.5f);
        }
        // Horizontal snap
        else if (fabsf(turn_x) > 0.7f && !snap_pending) {
          float snap =
              (turn_x > 0.0f) ? vr_snap_turn.value : -vr_snap_turn.value;
          cl.viewangles[YAW] -= snap;
          snap_pending = true;
          VR_TriggerHaptic(VR_HAND_RIGHT, 0.05f, 0, 0.3f);
        } else if (fabsf(turn_x) < 0.3f && fabsf(turn_y) < 0.3f) {
          snap_pending = false;
        }
      } else {
        // Smooth turning
        if (fabsf(turn_x) > 0.1f)
          cl.viewangles[YAW] -= turn_x * vr_turn_speed.value * host_frametime;
      }
    }
  }
}

static void PoseToQuake(const XrPosef *pose, vec3_t pos, vec3_t angles) {
  // Position: Xr right-handed Y-up -> Quake Z-up
  pos[0] = pose->position.x * vr_world_scale.value;
  pos[1] = -pose->position.z * vr_world_scale.value;
  pos[2] = pose->position.y * vr_world_scale.value;

  // Angles: OpenXR quaternion -> Quake Euler
  VR_QuatToQuakeAngles(&pose->orientation, angles);
}

void VR_GetHandPose(int hand, vec3_t pos, vec3_t angles) {
  if (hand < 0 || hand > 1)
    return;
  PoseToQuake(&oxr.hand_pose[hand], pos, angles);
}

void VR_TriggerHaptic(int hand, float duration, float frequency,
                      float amplitude) {
#ifdef VR_ENABLED
  if (!VR_Enabled() || !oxr.actions_initialized)
    return;

  XrHapticVibration vibration = {XR_TYPE_HAPTIC_VIBRATION};
  vibration.amplitude = amplitude;
  vibration.duration = (XrDuration)(duration * 1000000000.0f); // seconds to ns
  vibration.frequency = frequency;

  XrHapticActionInfo info = {XR_TYPE_HAPTIC_ACTION_INFO};
  info.action = oxr.haptic_action;
  info.subactionPath = oxr.hand_subaction_path[hand];

  xrApplyHapticFeedback(oxr.session, &info, (XrHapticBaseHeader *)&vibration);
#endif
}

void VR_GetRoomscaleAccum(vec3_t v) { VectorCopy(vr_room_scale_move, v); }

void VR_GetThumbstick(int hand, float *x, float *y) {
  if (hand < 0 || hand > 1)
    return;

  XrActionStateVector2f state = {XR_TYPE_ACTION_STATE_VECTOR2F};
  XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO};
  getInfo.action = oxr.thumbstick_action;
  getInfo.subactionPath = oxr.hand_subaction_path[hand];

  XrResult res = xrGetActionStateVector2f(oxr.session, &getInfo, &state);
  if (XR_SUCCEEDED(res) && state.isActive) {
    *x = state.currentState.x;
    *y = state.currentState.y;
  } else {
    *x = 0;
    *y = 0;
  }
}

// ============================================================================

// Rendering parts

#endif // VR_ENABLED
