#ifdef __cplusplus
extern "C" {
#endif

#include "vr_menu.h"
#include "quakedef.h"
#include "vr.h"

extern cvar_t vr_enabled;
extern cvar_t vr_crosshair;
extern cvar_t vr_crosshair_depth;
extern cvar_t vr_crosshair_size;
extern cvar_t vr_crosshair_alpha;
extern cvar_t vr_aimmode;
extern cvar_t vr_deadzone;
extern cvar_t vr_world_scale;
extern cvar_t vr_snap_turn;
extern cvar_t vr_180_snap_turn;
extern cvar_t vr_turn_speed;
extern cvar_t vr_haptic;
extern cvar_t vr_movement_instant_stop;
extern cvar_t vr_movement_speed;
extern cvar_t vr_weaponmenu_mode;

extern void M_DrawSlider(int x, int y, float range, float value,
                         const char *format);

#ifdef __cplusplus
}
#endif
#ifdef _WIN32
#include <GL/gl.h>
#include <windows.h>
#ifndef GL_MAX_SAMPLES
#define GL_MAX_SAMPLES 0x8D57
#endif
#endif
#include "cmd.h"
#include <array>
#include <string>

// cvars moved to top extern "C" block

static int vr_options_cursor = 0;
static int vr_options_view_start = 0;

#define VR_MAX_TURN_SPEED 10.0f
#define VR_MAX_FLOOR_OFFSET 200.0f
#define VR_MAX_GUNANGLE 180.0f
#define VR_MENU_ROW_H 8.0f
#define VR_MENU_VISIBLE_ROWS 18

static size_t VR_MenuFBTBoundedLength(const char *text, size_t maximum) {
  size_t length = 0;

  if (!text)
    return 0;
  while (length < maximum && text[length])
    ++length;
  return length;
}

static qboolean VR_MenuFBTSerialIsSafe(const char *serial) {
  size_t i;
  size_t length = VR_MenuFBTBoundedLength(serial, VR_FBT_SERIAL_MAX);

  if (!length || length == VR_FBT_SERIAL_MAX)
    return false;
  for (i = 0; i < length; ++i) {
    unsigned char c = (unsigned char)serial[i];

    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == ':' ||
          c == '-'))
      return false;
  }
  return true;
}

static qboolean VR_MenuFBTSerialEquals(const char *a, const char *b) {
  size_t i;
  size_t a_length = VR_MenuFBTBoundedLength(a, VR_FBT_SERIAL_MAX);
  size_t b_length = VR_MenuFBTBoundedLength(b, VR_FBT_SERIAL_MAX);

  if (!VR_MenuFBTSerialIsSafe(a) || !VR_MenuFBTSerialIsSafe(b) ||
      a_length != b_length)
    return false;
  for (i = 0; i < a_length; ++i)
    if (a[i] != b[i])
      return false;
  return true;
}

static uint32_t VR_MenuFBTSerialHash(const char *serial) {
  uint32_t hash = UINT32_C(2166136261);
  size_t serial_length;
  size_t i;

  if (!VR_MenuFBTSerialIsSafe(serial))
    return 0;
  serial_length = VR_MenuFBTBoundedLength(serial, VR_FBT_SERIAL_MAX);
  for (i = 0; i < serial_length; ++i) {
    hash ^= (unsigned char)serial[i];
    hash *= UINT32_C(16777619);
  }
  return hash;
}

static void VR_MenuFormatFBTRoleStatus(vr_fbt_role_t role, char *value,
                                       size_t size) {
  vr_fbt_role_status_t status = {};

  if (!value || !size)
    return;
  value[0] = 0;
  if (!VR_GetFBTRoleStatus(role, &status)) {
    q_snprintf(value, size, "unavailable");
    return;
  }
  if (status.identity_kind == VR_FBT_IDENTITY_NONE ||
      status.state == VR_FBT_STATE_UNASSIGNED) {
    q_snprintf(value, size, "unassigned");
    return;
  }
  if (status.state == VR_FBT_STATE_LOST || !status.connected) {
    if (status.identity_kind == VR_FBT_IDENTITY_SERIAL &&
        VR_MenuFBTSerialIsSafe(status.serial)) {
      q_snprintf(value, size, "offline #%08x",
                 VR_MenuFBTSerialHash(status.serial));
    } else {
      q_snprintf(value, size, "lost");
    }
    return;
  }
  if (status.identity_kind == VR_FBT_IDENTITY_EPHEMERAL) {
    q_snprintf(value, size, "session #%u", status.device_index);
    return;
  }
  switch (status.state) {
  case VR_FBT_STATE_CONNECTED_INVALID:
    q_snprintf(value, size, "connected-invalid");
    break;
  case VR_FBT_STATE_PREDICTING:
    q_snprintf(value, size, "predicting");
    break;
  case VR_FBT_STATE_TRACKING:
    q_snprintf(value, size, "tracking");
    break;
  default:
    q_snprintf(value, size, "unavailable");
    break;
  }
}

static qboolean VR_MenuFBTCandidateAssignedElsewhere(
    const vr_fbt_candidate_status_t *candidate, vr_fbt_role_t requested_role,
    const vr_fbt_role_status_t roles[VR_FBT_ROLE_COUNT]) {
  int role;

  for (role = 0; role < VR_FBT_ROLE_COUNT; ++role) {
    const vr_fbt_role_status_t *assigned = &roles[role];

    if ((vr_fbt_role_t)role == requested_role)
      continue;
    if (assigned->identity_kind == VR_FBT_IDENTITY_SERIAL &&
        candidate->has_safe_serial &&
        VR_MenuFBTSerialEquals(assigned->serial, candidate->serial))
      return true;
    if (assigned->identity_kind == VR_FBT_IDENTITY_EPHEMERAL &&
        !candidate->has_safe_serial && candidate->ephemeral_identity &&
        assigned->ephemeral_identity == candidate->ephemeral_identity)
      return true;
  }
  return false;
}

static qboolean VR_MenuFBTCandidateIsEligible(
    const vr_fbt_candidate_status_t *candidate, vr_fbt_role_t requested_role,
    const vr_fbt_role_status_t roles[VR_FBT_ROLE_COUNT]) {
  if (!candidate->connected)
    return false;
  if (candidate->has_safe_serial) {
    if (candidate->serial_ambiguous ||
        !VR_MenuFBTSerialIsSafe(candidate->serial))
      return false;
  } else if (!candidate->ephemeral_identity || candidate->ephemeral_ambiguous) {
    return false;
  }
  return !VR_MenuFBTCandidateAssignedElsewhere(candidate, requested_role,
                                                roles);
}

static qboolean VR_MenuFBTCandidateMatchesRole(
    const vr_fbt_candidate_status_t *candidate,
    const vr_fbt_role_status_t *role) {
  if (role->identity_kind == VR_FBT_IDENTITY_SERIAL)
    return candidate->has_safe_serial &&
           VR_MenuFBTSerialEquals(candidate->serial, role->serial);
  if (role->identity_kind == VR_FBT_IDENTITY_EPHEMERAL)
    return !candidate->has_safe_serial && candidate->ephemeral_identity &&
           candidate->ephemeral_identity == role->ephemeral_identity;
  return false;
}

static void VR_MenuCycleFBTRole(vr_fbt_role_t requested_role,
                                qboolean is_left) {
  vr_fbt_role_status_t roles[VR_FBT_ROLE_COUNT] = {};
  unsigned int candidate_ordinals[VR_FBT_MAX_CANDIDATES];
  unsigned int candidate_devices[VR_FBT_MAX_CANDIDATES];
  unsigned int candidate_count;
  unsigned int eligible_count = 0;
  int current_option = -1;
  int target_option;
  int option_count;
  int role;
  unsigned int ordinal;

  for (role = 0; role < VR_FBT_ROLE_COUNT; ++role)
    if (!VR_GetFBTRoleStatus((vr_fbt_role_t)role, &roles[role]))
      return;

  candidate_count = VR_GetFBTCandidateCount();
  if (candidate_count > VR_FBT_MAX_CANDIDATES)
    candidate_count = VR_FBT_MAX_CANDIDATES;
  for (ordinal = 0; ordinal < candidate_count; ++ordinal) {
    vr_fbt_candidate_status_t candidate = {};
    unsigned int insertion;

    if (!VR_GetFBTCandidateStatus(ordinal, &candidate) ||
        !VR_MenuFBTCandidateIsEligible(&candidate, requested_role, roles))
      continue;
    insertion = eligible_count;
    while (insertion > 0 &&
           candidate.device_index < candidate_devices[insertion - 1]) {
      candidate_ordinals[insertion] = candidate_ordinals[insertion - 1];
      candidate_devices[insertion] = candidate_devices[insertion - 1];
      --insertion;
    }
    candidate_ordinals[insertion] = ordinal;
    candidate_devices[insertion] = candidate.device_index;
    ++eligible_count;
  }

  /* Unassigned is available only when at least one real candidate exists. */
  if (!eligible_count)
    return;

  if (roles[requested_role].identity_kind == VR_FBT_IDENTITY_NONE)
    current_option = 0;
  else {
    for (ordinal = 0; ordinal < eligible_count; ++ordinal) {
      vr_fbt_candidate_status_t candidate = {};

      if (VR_GetFBTCandidateStatus(candidate_ordinals[ordinal], &candidate) &&
          VR_MenuFBTCandidateMatchesRole(&candidate, &roles[requested_role])) {
        current_option = (int)ordinal + 1;
        break;
      }
    }
  }

  option_count = (int)eligible_count + 1;
  if (current_option < 0)
    target_option = is_left ? (int)eligible_count : 0;
  else
    target_option = (current_option + (is_left ? option_count - 1 : 1)) %
                    option_count;

  if (target_option == 0)
    VR_UnassignFBTRole(requested_role);
  else
    VR_AssignFBTCandidate(requested_role,
                          candidate_ordinals[target_option - 1]);
}

static int VR_MenuVisibleRows(void) {
  return (int)VR_OPTION_MAX < VR_MENU_VISIBLE_ROWS ? (int)VR_OPTION_MAX
                                                   : VR_MENU_VISIBLE_ROWS;
}

static void VR_MenuKeepVisible(void) {
  int visible_rows = VR_MenuVisibleRows();
  int max_start = q_max(0, VR_OPTION_MAX - visible_rows);

  vr_options_cursor = CLAMP(0, vr_options_cursor, VR_OPTION_MAX - 1);
  if (vr_options_view_start < 0)
    vr_options_view_start = 0;
  if (vr_options_view_start > max_start)
    vr_options_view_start = max_start;

  if (vr_options_cursor < vr_options_view_start) {
    vr_options_view_start = vr_options_cursor;
  } else if (vr_options_cursor >= vr_options_view_start + visible_rows) {
    vr_options_view_start = vr_options_cursor - visible_rows + 1;
  }

  vr_options_view_start = CLAMP(0, vr_options_view_start, max_start);
}

// M_DrawSlider moved to top extern "C" block

void VR_Menu_Init() {
  // VR menu function pointers
  vr_menucmdfn = VR_Menu_f;
  vr_menudrawfn = VR_MenuDraw;
  vr_menukeyfn = VR_MenuKey;
}

static void VR_MenuPlaySound(const char *sound, float fvol) {
  sfx_t *sfx = S_PrecacheSound(sound);

  if (sfx) {
    S_StartSound(cl.viewentity, 0, sfx, vec3_origin, fvol, 1);
  }
}

static void VR_MenuPrintOptionValue(int cx, int cy, int option) {
  char value_buffer[32] = {0};
  const char *value_string = NULL;

  const auto printAsStr = [&](const auto &cvar) {
    snprintf(value_buffer, sizeof(value_buffer), "%.4f", cvar.value);
    M_Print(cx, cy, value_buffer);
  };

#ifdef _MSC_VER
#define snprintf sprintf_s
#endif
  switch (option) {
  default:
    break;

  case VR_OPTION_ENABLED:
    M_DrawCheckbox(cx, cy, (int)vr_enabled.value);
    break;
  case VR_OPTION_VRIK:
    if (!VR_VRIKAvailable())
      value_string = "unavailable";
    else if (!VR_VRIKAllowedForGame())
      value_string = "unsupported";
    else
      M_DrawCheckbox(cx, cy, (int)vr_vrik.value);
    break;
  case VR_OPTION_FBT_ENABLED:
    M_DrawCheckbox(cx, cy, (int)vr_fbt_enabled.value);
    break;
  case VR_OPTION_FBT_HIP:
    VR_MenuFormatFBTRoleStatus(VR_FBT_ROLE_HIP, value_buffer,
                               sizeof(value_buffer));
    value_string = value_buffer;
    break;
  case VR_OPTION_FBT_LEFT_FOOT:
    VR_MenuFormatFBTRoleStatus(VR_FBT_ROLE_LEFT_FOOT, value_buffer,
                               sizeof(value_buffer));
    value_string = value_buffer;
    break;
  case VR_OPTION_FBT_RIGHT_FOOT:
    VR_MenuFormatFBTRoleStatus(VR_FBT_ROLE_RIGHT_FOOT, value_buffer,
                               sizeof(value_buffer));
    value_string = value_buffer;
    break;
  case VR_OPTION_FBT_RESCAN: {
    vr_fbt_role_status_t status = {};

    if (!VR_GetFBTRoleStatus(VR_FBT_ROLE_HIP, &status))
      value_string = "unavailable";
    else if (!VR_GetFBTCandidateCount())
      value_string = "no trackers";
    else
      value_string = "rescan";
    break;
  }
  case VR_OPTION_FBT_PROFILE:
    value_string = VR_GetFBTSelectedProfileName();
    break;
  case VR_OPTION_FBT_CALIBRATION:
    value_string = VR_GetFBTCalibrationStatus();
    break;
  case VR_OPTION_FBT_CALIBRATION_CANCEL:
    value_string = "cancel";
    break;
  /*case VR_OPTION_PERFHUD:
      if (vr_perfhud.value == 1) value_string = "Latency Timing";
      else if (vr_perfhud.value == 2) value_string = "Render Timing";
      else if (vr_perfhud.value == 3) value_string = "Perf Headroom";
      else if (vr_perfhud.value == 4) value_string = "Version Info";
      else value_string = "off";
      break;*/
  case VR_OPTION_AIMMODE:
    switch ((int)vr_aimmode.value) {
    case VR_AIMMODE_HEAD_MYAW:
      value_string = "HEAD_MYAW";
      break;
    case VR_AIMMODE_HEAD_MYAW_MPITCH:
      value_string = "HEAD_MYAW_MPITCH";
      break;
    case VR_AIMMODE_MOUSE_MYAW:
      value_string = "MOUSE_MYAW";
      break;
    case VR_AIMMODE_MOUSE_MYAW_MPITCH:
      value_string = "MOUSE_MYAW_MPITCH";
      break;
    default:
    case VR_AIMMODE_BLENDED:
      value_string = "BLENDED";
      break;
    case VR_AIMMODE_BLENDED_NOPITCH:
      value_string = "BLENDED_NOPITCH";
      break;
    case VR_AIMMODE_CONTROLLER:
      value_string = "CONTROLLER";
      break;
    }
    break;
  case VR_OPTION_DEADZONE:
    if (vr_deadzone.value > 0) {
      snprintf(value_buffer, sizeof(value_buffer), "%.0f degrees",
               vr_deadzone.value);
      value_string = value_buffer;
    } else {
      value_string = "off";
    }
    break;
  case VR_OPTION_CROSSHAIR:
    if ((int)vr_crosshair.value == 2) {
      value_string = "line";
    } else if ((int)vr_crosshair.value == 1) {
      value_string = "point";
    } else {
      value_string = "off";
    }
    break;
  case VR_OPTION_CROSSHAIR_DEPTH:
    if (vr_crosshair_depth.value > 0) {
      snprintf(value_buffer, sizeof(value_buffer), "%.0f units",
               vr_crosshair_depth.value);
      value_string = value_buffer;
    } else {
      value_string = "off";
    }
    break;
  case VR_OPTION_CROSSHAIR_SIZE:
    if (vr_crosshair_size.value > 0) {
      snprintf(value_buffer, sizeof(value_buffer), "%.0f pixels",
               vr_crosshair_size.value);
      value_string = value_buffer;
    } else {
      value_string = "off";
    }
    break;
  case VR_OPTION_CROSSHAIR_ALPHA:
    M_DrawSlider(cx, cy, vr_crosshair_alpha.value, vr_crosshair_alpha.value,
                 "%.2f");
    break;
  case VR_OPTION_WORLD_SCALE:
    M_DrawSlider(cx, cy, vr_world_scale.value / 2.0f, vr_world_scale.value,
                 "%.2f");
    break;
  case VR_OPTION_MOVEMENT_MODE:
    switch ((int)vr_movement_mode.value) {
    case VR_MOVEMENT_MODE_FOLLOW_HEAD:
      value_string = "Follow head";
      break;
    case VR_MOVEMENT_MODE_FOLLOW_HAND:
      value_string = "Follow hand";
      break;
    case VR_MOVEMENT_MODE_RAW_INPUT:
      value_string = "Raw input";
      break;
    }
    break;
  case VR_OPTION_SNAP_TURN:
    if (vr_snap_turn.value == 0) {
      value_string = "Smooth";
    } else {
      snprintf(value_buffer, sizeof(value_buffer), "%d Degrees",
               (int)vr_snap_turn.value);
      value_string = value_buffer;
    }
    break;
  case VR_OPTION_180_SNAP_TURN:
    M_DrawCheckbox(cx, cy, (int)vr_180_snap_turn.value);
    break;
  case VR_OPTION_TURN_SPEED:
    M_DrawSlider(cx, cy, vr_turn_speed.value / VR_MAX_TURN_SPEED,
                 vr_turn_speed.value, "%.2f");
    break;
  case VR_OPTION_MSAA:
    if (vr_msaa.value == 0) {
      value_string = "Off";
    } else {
      snprintf(value_buffer, sizeof(value_buffer), "%d Samples",
               (int)vr_msaa.value);
      value_string = value_buffer;
    }
    break;
  case VR_OPTION_HIDDEN_AREA:
    M_DrawCheckbox(cx, cy, (int)vr_hidden_area.value);
    break;
  case VR_OPTION_GUNMODELOFFSETS:
    switch ((int)vr_gunmodeloffsets.value) {
    case VR_GUNMODELOFFSETS_VANILLA:
      value_string = "Vanilla";
      break;
    case VR_GUNMODELOFFSETS_ENHANCED:
      value_string = "Enhanced";
      break;
    case VR_GUNMODELOFFSETS_AUTHENTIC:
      value_string = "Authentic";
      break;
    case VR_GUNMODELOFFSETS_PLAGUE:
      value_string = "Plague";
      break;
    case VR_GUNMODELOFFSETS_BLOCKQUAKE:
      value_string = "Block-Quake";
      break;
    }
    break;
  case VR_OPTION_GUNANGLE:
    printAsStr(vr_gunangle);
    break;
  case VR_OPTION_FLOOR_OFFSET:
    printAsStr(vr_floor_offset);
    break;
  case VR_OPTION_GUNMODELPITCH:
    printAsStr(vr_gunmodelpitch);
    break;
  case VR_OPTION_GUNMODELSCALE:
    printAsStr(vr_gunmodelscale);
    break;
  case VR_OPTION_GUNMODELY:
    printAsStr(vr_gunmodely);
    break;
  case VR_OPTION_CROSSHAIRY:
    printAsStr(vr_crosshairy);
    break;
  case VR_OPTION_PROJECTILESPAWN_Z_OFFSET:
    printAsStr(vr_projectilespawn_z_offset);
    break;
  case VR_OPTION_WEAPONMENU_MODE:
    switch ((int)vr_weaponmenu_mode.value) {
    case VR_WEAPONMENU_MODE_PLAYSPACE:
      value_string = "Playspace";
      break;
    case VR_WEAPONMENU_MODE_VIEW:
      value_string = "View-locked";
      break;
    default:
      value_string = "Unknown";
      break;
    }
    break;
  case VR_OPTION_HUD_SCALE:
    printAsStr(vr_hud_scale);
    break;
  case VR_OPTION_MENU_SCALE:
    printAsStr(vr_menu_scale);
    break;
  case VR_OPTION_HAPTIC:
    M_DrawCheckbox(cx, cy, (int)vr_haptic.value);
    break;
  case VR_OPTION_INSTANT_STOP:
    M_DrawCheckbox(cx, cy, (int)vr_movement_instant_stop.value);
    break;
  case VR_OPTION_MOVEMENT_SPEED:
    printAsStr(vr_movement_speed);
    break;
  case VR_OPTION_IMPULSE9:
    break;
  case VR_OPTION_GOD:
    break;
  case VR_OPTION_NOCLIP:
    break;
  case VR_OPTION_FLY:
    break;
  }
#ifdef _MSC_VER
#undef snprintf
#endif
  if (value_string) {
    M_Print(cx, cy, value_string);
  }
}

void VR_MenuKeyOption(int key, int option) {
#define _sizeofarray(x) ((sizeof(x) / sizeof(x[0])))
#define _maxarray(x) (_sizeofarray(x) - 1)

  qboolean isLeft = (key == K_LEFTARROW);
  int intValue = 0;
  float floatValue = 0.0f;
  int i = 0;

  int debug[] = {0, 1, 2, 3, 4};
  float ipdDiff = 0.2f;
  int position[] = {0, 1, 2};
  float multisample[] = {1.0f, 1.25f, 1.50f, 1.75f, 2.0f};
  int aimmode[] = {1, 2, 3, 4, 5, 6, 7};
  int deadzoneDiff = 5;
  int crosshair[] = {0, 1, 2};
  int crosshairDepthDiff = 32;
  int crosshairSizeDiff = 1;
  float crosshairAlphaDiff = 0.05f;

  const auto adjustF = [&isLeft](const cvar_t &cvar, auto incr, auto min,
                                 auto max) {
    Cvar_SetValue(cvar.name, CLAMP((float)min,
                                   isLeft ? (float)(cvar.value - incr)
                                          : (float)(cvar.value + incr),
                                   (float)max));
  };

  const auto adjustI = [&isLeft](const cvar_t &cvar, auto incr, auto min,
                                 auto max) {
    Cvar_SetValue(cvar.name, (int)CLAMP((int)min,
                                        isLeft ? (int)(cvar.value - incr)
                                               : (int)(cvar.value + incr),
                                        (int)max));
  };

  switch (option) {
  case VR_OPTION_ENABLED:
    // Cvar_SetValue( "vr_enabled", ! (int)vr_enabled.value );
    // if ( (int)vr_enabled.value ) {
    //    VR_MenuPlaySound( "items/r_item2.wav", 0.5 );
    // }
    break;
  case VR_OPTION_VRIK:
    if (VR_VRIKAllowedForGame())
      Cvar_SetValue(vr_vrik.name, !(int)vr_vrik.value);
    break;
  case VR_OPTION_FBT_ENABLED:
    if (key == K_LEFTARROW || key == K_RIGHTARROW || key == K_ENTER)
      Cvar_SetValue(vr_fbt_enabled.name, !(int)vr_fbt_enabled.value);
    break;
  case VR_OPTION_FBT_HIP:
    if (key == K_LEFTARROW || key == K_RIGHTARROW)
      VR_MenuCycleFBTRole(VR_FBT_ROLE_HIP, isLeft);
    break;
  case VR_OPTION_FBT_LEFT_FOOT:
    if (key == K_LEFTARROW || key == K_RIGHTARROW)
      VR_MenuCycleFBTRole(VR_FBT_ROLE_LEFT_FOOT, isLeft);
    break;
  case VR_OPTION_FBT_RIGHT_FOOT:
    if (key == K_LEFTARROW || key == K_RIGHTARROW)
      VR_MenuCycleFBTRole(VR_FBT_ROLE_RIGHT_FOOT, isLeft);
    break;
  case VR_OPTION_FBT_RESCAN:
    if (key == K_LEFTARROW || key == K_RIGHTARROW || key == K_ENTER)
      Cmd_ExecuteString("vr_fbt_rescan", cmd_source_t::src_command);
    break;
  case VR_OPTION_FBT_PROFILE:
    /* Profile names are selected explicitly from the console so the menu
       never needs to expose filesystem paths or tracker identities. */
    break;
  case VR_OPTION_FBT_CALIBRATION:
    if (key == K_LEFTARROW || key == K_RIGHTARROW || key == K_ENTER)
      VR_ActivateFBTCalibrationMenu();
    break;
  case VR_OPTION_FBT_CALIBRATION_CANCEL:
    if (key == K_LEFTARROW || key == K_RIGHTARROW || key == K_ENTER)
      VR_CancelFBTCalibrationMenu();
    break;
  /*case VR_OPTION_PERFHUD:
      intValue = (int)vr_perfhud.value;
      intValue = CLAMP( debug[0], isLeft ? intValue - 1 : intValue + 1,
     debug[_maxarray( debug )] ); Cvar_SetValue( "vr_perfhud", intValue );
      break;*/
  case VR_OPTION_AIMMODE:
    intValue = (int)vr_aimmode.value;
    intValue = CLAMP(aimmode[0], isLeft ? intValue - 1 : intValue + 1,
                     (int)_sizeofarray(aimmode));
    intValue -= 1;
    Cvar_SetValue("vr_aimmode", aimmode[intValue]);
    break;
  case VR_OPTION_DEADZONE:
    adjustF(vr_deadzone, deadzoneDiff, 0.f, 180.f);
    break;
  case VR_OPTION_CROSSHAIR:
    adjustI(vr_crosshair, 1, crosshair[0],
            (int)crosshair[_maxarray(crosshair)]);
    break;
  case VR_OPTION_CROSSHAIR_DEPTH:
    adjustF(vr_crosshair_depth, crosshairDepthDiff, 0.f, 4096.f);
    break;
  case VR_OPTION_CROSSHAIR_SIZE:
    adjustF(vr_crosshair_size, crosshairSizeDiff, 0.f, 32.f);
    break;
  case VR_OPTION_CROSSHAIR_ALPHA:
    adjustF(vr_crosshair_alpha, crosshairAlphaDiff, 0.f, 1.f);
    break;
  case VR_OPTION_WORLD_SCALE:
    adjustF(vr_world_scale, crosshairAlphaDiff, 0.f, 2.f);
    break;
  case VR_OPTION_MOVEMENT_MODE:
    adjustI(vr_movement_mode, 1, 0, VR_MAX_MOVEMENT_MODE);
    break;
  case VR_OPTION_SNAP_TURN:
    adjustI(vr_snap_turn, 45, 0.f, 90.f);
    break;
  case VR_OPTION_180_SNAP_TURN:
    adjustI(vr_180_snap_turn, 1, 0.f, 1.f);
    break;
  case VR_OPTION_TURN_SPEED:
    adjustF(vr_turn_speed, 0.25f, 0.f, VR_MAX_TURN_SPEED);
    break;
  case VR_OPTION_MSAA:
    int max;
    glGetIntegerv(GL_MAX_SAMPLES, &max);
    adjustI(vr_msaa, 1, 0, max);
    break;
  case VR_OPTION_HIDDEN_AREA:
    Cvar_SetValue(vr_hidden_area.name, !(int)vr_hidden_area.value);
    break;
  case VR_OPTION_GUNANGLE:
    adjustF(vr_gunangle, 2.5f, -VR_MAX_GUNANGLE, VR_MAX_GUNANGLE);
    break;
  case VR_OPTION_FLOOR_OFFSET:
    adjustF(vr_floor_offset, 2.5f, -VR_MAX_FLOOR_OFFSET, VR_MAX_FLOOR_OFFSET);
    break;
  case VR_OPTION_GUNMODELOFFSETS:
    adjustI(vr_gunmodeloffsets, 1, 0.f, VR_MAX_GUNMODELOFFSETS);
    break;
  case VR_OPTION_GUNMODELPITCH:
    adjustF(vr_gunmodelpitch, 0.5f, -90.f, 90.f);
    break;
  case VR_OPTION_GUNMODELSCALE:
    adjustF(vr_gunmodelscale, 0.05f, 0.1f, 2.f);
    break;
  case VR_OPTION_GUNMODELY:
    adjustF(vr_gunmodely, 0.1f, -5.0f, 5.f);
    break;
  case VR_OPTION_CROSSHAIRY:
    adjustF(vr_crosshairy, 0.05f, -10.0f, 10.f);
    break;
  case VR_OPTION_PROJECTILESPAWN_Z_OFFSET:
    adjustF(vr_projectilespawn_z_offset, 1.f, -24.0f, 24.f);
    break;
  case VR_OPTION_HUD_SCALE:
    adjustF(vr_hud_scale, 0.005f, 0.01f, 0.1f);
    break;
  case VR_OPTION_MENU_SCALE:
    adjustF(vr_menu_scale, 0.01f, 0.05f, 0.6f);
    break;
  case VR_OPTION_HAPTIC:
    adjustI(vr_haptic, 1, 0, 1);
    break;
  case VR_OPTION_INSTANT_STOP:
    adjustI(vr_movement_instant_stop, 1, 0, 1);
    break;
  case VR_OPTION_MOVEMENT_SPEED:
    adjustF(vr_movement_speed, 0.1f, 0.5f, 3.0f);
    break;
  case VR_OPTION_WEAPONMENU_MODE:
    adjustI(vr_weaponmenu_mode, 1, 0, VR_WEAPONMENU_MODE_VIEW);
    break;
  case VR_OPTION_IMPULSE9:
    VR_MenuPlaySound("items/r_item2.wav", 0.5);
    Cmd_ExecuteString("impulse 9", cmd_source_t::src_command);
    break;
  case VR_OPTION_GOD:
    VR_MenuPlaySound("items/r_item2.wav", 0.5);
    Cmd_ExecuteString("god", cmd_source_t::src_command);
    break;
  case VR_OPTION_NOCLIP:
    VR_MenuPlaySound("items/r_item2.wav", 0.5);
    Cmd_ExecuteString("noclip", cmd_source_t::src_command);
    break;
  case VR_OPTION_FLY:
    VR_MenuPlaySound("items/r_item2.wav", 0.5);
    Cmd_ExecuteString("fly", cmd_source_t::src_command);
    break;
  }

#undef _maxarray
#undef _sizeofarray
}

void VR_MenuKey(int key) {
  switch (key) {
  case K_ESCAPE:
    VID_SyncCvars(); // sync cvars before leaving menu. FIXME: there are other
                     // ways to leave menu
    S_LocalSound("misc/menu1.wav");
    M_Menu_Options_f();
    break;

  case K_UPARROW:
    S_LocalSound("misc/menu1.wav");
    vr_options_cursor--;
    if (vr_options_cursor < 0) {
      vr_options_cursor = VR_OPTION_MAX - 1;
    }
    VR_MenuKeepVisible();
    break;

  case K_DOWNARROW:
    S_LocalSound("misc/menu1.wav");
    vr_options_cursor++;
    if (vr_options_cursor >= VR_OPTION_MAX) {
      vr_options_cursor = 0;
    }
    VR_MenuKeepVisible();
    break;

  case K_LEFTARROW:
    [[fallthrough]];
  case K_RIGHTARROW:
    S_LocalSound("misc/menu3.wav");
    VR_MenuKeyOption(key, vr_options_cursor);
    break;

  case K_ENTER:
    m_entersound = true;
    VR_MenuKeyOption(key, vr_options_cursor);
    break;

  default:
    break;
  }
}

qboolean VR_MenuPointerMove(float x, float y) {
  int option;
  int row;
  int visible_rows = VR_MenuVisibleRows();
  float list_top = 48.0f;
  float list_bottom = list_top + VR_MENU_ROW_H * visible_rows;

  /* VR options begin at the same baseline used by VR_MenuDraw(). */
  if (x < 8.0f || x >= 320.0f || y < list_top || y >= list_bottom)
    return false;

  row = (int)((y - list_top) / VR_MENU_ROW_H);
  option = vr_options_view_start + row;
  if (option < 0 || option >= VR_OPTION_MAX)
    return false;
  if (row < 0 || row >= visible_rows)
    return false;

  vr_options_cursor = option;
  VR_MenuKeepVisible();
  return true;
}

void VR_MenuDraw(void) {
  int y = 4;
  int i;
  int visible_rows = VR_MenuVisibleRows();
  int first_visible;
  int last_visible;
  const int indicator_x = 304;
  const int top_indicator_y = 48;
  const int bottom_indicator_y = 48 + (VR_MENU_ROW_H * visible_rows) - VR_MENU_ROW_H;

  VR_MenuKeepVisible();
  first_visible = vr_options_view_start;
  last_visible = (int)VR_OPTION_MAX < first_visible + visible_rows
                     ? (int)VR_OPTION_MAX
                     : first_visible + visible_rows;

  // plaque
  M_DrawTransPic(16, y, Draw_CachePic("gfx/qplaque.lmp"));

  // customize header
  qpic_t *p = Draw_CachePic("gfx/ttl_cstm.lmp");
  M_DrawPic((320 - p->width) / 2, y, p);

  y += 28;

  // title
  const char *title = "VR/HMD OPTIONS";
  M_PrintWhite((320 - 8 * strlen(title)) / 2, y, title);

  y += 16;

  if (first_visible > 0) {
    M_Print(indicator_x - 4, top_indicator_y, "^");
  }
  if (last_visible < VR_OPTION_MAX) {
    M_Print(indicator_x - 4, bottom_indicator_y, "v");
  }

  for (i = first_visible; i < last_visible; i++) {
    switch (i) {
    case VR_OPTION_ENABLED:
      M_Print(16, y, "               VR Enabled");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_VRIK:
      M_Print(16, y, "           Networked VRIK");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_FBT_ENABLED:
      M_Print(16, y, "      Full Body Tracking");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_FBT_HIP:
      M_Print(16, y, "             Hip Tracker");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_FBT_LEFT_FOOT:
      M_Print(16, y, "       Left Foot Tracker");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_FBT_RIGHT_FOOT:
      M_Print(16, y, "      Right Foot Tracker");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_FBT_RESCAN:
      M_Print(16, y, "          Rescan Trackers");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_FBT_PROFILE:
      M_Print(16, y, "       FBT Profile (console)");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_FBT_CALIBRATION:
      M_Print(16, y, "         FBT Calibration");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_FBT_CALIBRATION_CANCEL:
      M_Print(16, y, "      Cancel FBT Calibration");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_AIMMODE:
      M_Print(16, y, "                 Aim Mode");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_DEADZONE:
      M_Print(16, y, "                 Deadzone");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_CROSSHAIR:
      M_Print(16, y, "                Crosshair");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_CROSSHAIR_DEPTH:
      M_Print(16, y, "          Crosshair Depth");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_CROSSHAIR_SIZE:
      M_Print(16, y, "           Crosshair Size");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_CROSSHAIR_ALPHA:
      M_Print(16, y, "          Crosshair Alpha");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_WORLD_SCALE:
      M_Print(16, y, "              World Scale");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_MOVEMENT_MODE:
      M_Print(16, y, "            Movement mode");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_SNAP_TURN:
      M_Print(16, y, "                     Turn");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_180_SNAP_TURN:
      M_Print(16, y, "            180 Snap Turn");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_TURN_SPEED:
      M_Print(16, y, "               Turn Speed");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_MSAA:
      M_Print(16, y, "                     MSAA");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_HIDDEN_AREA:
      M_Print(16, y, "         Hidden Area Mask");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_GUNANGLE:
      M_Print(16, y, "                Gun Angle");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_FLOOR_OFFSET:
      M_Print(16, y, "             Floor Offset");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_GUNMODELOFFSETS:
      M_Print(16, y, "        Gun Model Offsets");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_GUNMODELPITCH:
      M_Print(16, y, "          Gun Model Pitch");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_GUNMODELSCALE:
      M_Print(16, y, "          Gun Model Scale");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_GUNMODELY:
      M_Print(16, y, "              Gun Model Y");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_CROSSHAIRY:
      M_Print(16, y, "              Crosshair Y");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_PROJECTILESPAWN_Z_OFFSET:
      M_Print(16, y, "       Projectile Spawn Z");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_WEAPONMENU_MODE:
      M_Print(16, y, "      Weapon Wheel Mode");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_HUD_SCALE:
      M_Print(16, y, "                HUD Scale");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_MENU_SCALE:
      M_Print(16, y, "               Menu Scale");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_HAPTIC:
      M_Print(16, y, "          Weapon Haptics");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_INSTANT_STOP:
      M_Print(16, y, "            Instant Stop");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_MOVEMENT_SPEED:
      M_Print(16, y, "         Movement Speed");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_IMPULSE9:
      M_Print(16, y, "         Give all weapons");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_GOD:
      M_Print(16, y, "                 God Mode");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_NOCLIP:
      M_Print(16, y, "             No Clip Mode");
      VR_MenuPrintOptionValue(240, y, i);
      break;
    case VR_OPTION_FLY:
      M_Print(16, y, "                 Fly Mode");
      VR_MenuPrintOptionValue(240, y, i);
      break;

    default:
      break;
    }

    // draw the blinking cursor
    if (vr_options_cursor == i) {
      M_DrawCharacter(220, y, 12 + ((int)(realtime * 4) & 1));
    }

    y += 8;
  }
}

void VR_Menu_f(void) {
  const char *sound = "items/r_item1.wav";

  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_vr;
  m_entersound = true;

  VR_MenuPlaySound(sound, 0.5);
}
