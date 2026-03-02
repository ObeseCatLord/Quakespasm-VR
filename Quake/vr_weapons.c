#include "quakedef.h"
#include "vr.h"
#include "vr_openxr.h"

#ifdef VR_ENABLED

// Weapon tracking and schema state
vr_weapon_cmd_t vr_weapons[MAX_VR_WEAPONS];
int num_vr_weapons = 0;

vr_dyn_weapon_t dyn_weapons[MAX_DYN_WEAPONS];
int num_dyn_weapons = 0;

int vr_last_sent_impulse = 0;
double vr_last_sent_impulse_time = 0;
int vr_weaponmenu_selection = -1;
double vr_next_weapon_switch_time = 0;

qboolean vr_autoscan_active = true;
int vr_autoscan_impulse = 0;
double vr_autoscan_next_time = 0;

qboolean in_vr_weaponmenu = false;

static int last_tracked_activeweapon = -1;

cvar_t vr_weapon_offset[MAX_WEAPONS * VARS_PER_WEAPON];
int weaponCVarEntry = 0;

static void InitWeaponCVar(cvar_t *cvar, const char *name, int i,
                           const char *value) {
  char cvarname[64];
  q_snprintf(cvarname, sizeof(cvarname), name);
  int len = strlen(cvarname);
  if (len >= 2) {
    cvarname[len - 1] = '0' + ((i + 1) % 10);
    cvarname[len - 2] = '0' + (((i + 1) / 10) % 10);
  }

  if (!Cvar_FindVar(cvarname)) {
    cvar->name =
        strdup(cvarname); // This still technically leaks if re-registered, but
                          // ironwail cvars are usually permanent.
    cvar->string = value;
    cvar->flags = CVAR_NONE;
    Cvar_RegisterVariable(cvar);
  } else {
    Cvar_SetQuick(Cvar_FindVar(cvarname), value);
  }
}

static void InitWeaponCVars(int i, const char *id, const char *offsetX,
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

void InitAllWeaponCVars(void) {
  int i = 0;

  // Vanilla Quake, Scourge of Armagon, Dissolution of Eternity
  // This logic is simplified to just the vanilla/enhanced profiles for now
  // to avoid massive code bloat, but we include the core ones.

  if (vr_gunmodeloffsets.value == 1) // ENHANCED
  {
    InitWeaponCVars(i++, "progs/v_axe.mdl", "-4", "24", "37", "0.33");
    InitWeaponCVars(i++, "progs/v_shot.mdl", "1.5", "1", "10", "0.5");
    InitWeaponCVars(i++, "progs/v_shot2.mdl", "-6", "0.3", "7", "0.9");
    InitWeaponCVars(i++, "progs/v_nail.mdl", "-1.9", "5.7", "15", "0.4");
    InitWeaponCVars(i++, "progs/v_nail2.mdl", "5.5", "3.6", "19", "0.4");
    InitWeaponCVars(i++, "progs/v_rock.mdl", "10", "1.2", "13", "0.5");
    InitWeaponCVars(i++, "progs/v_rock2.mdl", "26", "4.5", "21", "0.3");
    InitWeaponCVars(i++, "progs/v_light.mdl", "12", "3", "13", "0.5");
  } else {
    // Vanilla
    InitWeaponCVars(i++, "progs/v_axe.mdl", "-4", "24", "37", "0.33");
    InitWeaponCVars(i++, "progs/v_shot.mdl", "1.5", "1", "10", "0.5");
    InitWeaponCVars(i++, "progs/v_shot2.mdl", "-3.5", "1", "8.5", "0.8");
    InitWeaponCVars(i++, "progs/v_nail.mdl", "-5", "3", "15", "0.5");
    InitWeaponCVars(i++, "progs/v_nail2.mdl", "0", "3", "19", "0.5");
    InitWeaponCVars(i++, "progs/v_rock.mdl", "10", "1.5", "13", "0.5");
    InitWeaponCVars(i++, "progs/v_rock2.mdl", "10", "7", "19", "0.5");
    InitWeaponCVars(i++, "progs/v_light.mdl", "3", "4", "13", "0.5");
  }

  while (i < MAX_WEAPONS) {
    InitWeaponCVars(i++, "-1", "1.5", "1", "10", "0.5");
  }
}

int VR_GetWeaponOffset(const char *name, vec3_t offset, float *scale) {
  for (int i = 0; i < MAX_WEAPONS; i++) {
    if (!strcmp(vr_weapon_offset[i * VARS_PER_WEAPON + 4].string, name)) {
      offset[0] = vr_weapon_offset[i * VARS_PER_WEAPON].value;
      offset[1] = vr_weapon_offset[i * VARS_PER_WEAPON + 1].value;
      offset[2] = vr_weapon_offset[i * VARS_PER_WEAPON + 2].value;
      *scale = vr_weapon_offset[i * VARS_PER_WEAPON + 3].value;
      return i;
    }
  }
  // Default offsets
  VectorSet(offset, 1.5, 1, 10);
  *scale = 0.5;
  return -1;
}

// Client-side wall collision detection
qboolean VR_CheckWallCollision(vec3_t hand_origin, vec3_t hand_angles) {
  vec3_t forward, end, impact;
  AngleVectors(hand_angles, forward, NULL, NULL);

  // Trace forward from hand to see if we're hitting a wall
  // 12 units is a decent 'muzzle' distance
  VectorMA(hand_origin, 12, forward, end);
  TraceLine(hand_origin, end, impact);

  // If the impact point isn't the end point, we hit something
  vec3_t diff;
  VectorSubtract(impact, end, diff);
  if (VectorLength(diff) > 0.1)
    return true;

  return false;
}

void VR_InitWeaponWheel(void) {
  memset(dyn_weapons, 0, sizeof(dyn_weapons));
  num_dyn_weapons = 0;
  last_tracked_activeweapon = -1;
}

// Ported from quakespasm-openvr
void VR_LoadWeaponSchema(void) {
  char *data;
  char *start;
  char key[64];

  num_vr_weapons = 0;

  data = (char *)COM_LoadMallocFile("vr_weapons.txt", NULL);
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
      w->scale = 1.0f;

      while (1) {
        start = (char *)COM_Parse(start);
        if (!start || !com_token[0] || !Q_strcmp(com_token, "}"))
          break;

        Q_strncpy(key, com_token, sizeof(key));
        start = (char *)COM_Parse(start);
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

  free(data);
  Con_Printf("VR: Loaded %d weapons from vr_weapons.txt\n", num_vr_weapons);

  for (int i = 0; i < num_vr_weapons; i++) {
    Mod_ForName(vr_weapons[i].model_path, false);
  }
}

void VR_InitGame(void) { VR_LoadWeaponSchema(); }

void VR_TrackWeapons(void) {
  int active = cl.stats[STAT_ACTIVEWEAPON];
  int model_idx = cl.stats[STAT_WEAPON];

  if (active == 0 || model_idx == 0)
    return;

  // Pulse auto-scanner if active
  if (vr_autoscan_active && realtime > vr_autoscan_next_time) {
    if (vr_autoscan_impulse < 32) {
      vr_autoscan_impulse++;
      char cmd[32];
      q_snprintf(cmd, sizeof(cmd), "impulse %d\n", vr_autoscan_impulse);
      Cbuf_AddText(cmd);
      vr_autoscan_next_time = realtime + 2.0;
    } else {
      vr_autoscan_active = false;
      Con_Printf("VR: Weapon auto-scan complete.\n");
    }
  }

  // Check if we already know this weapon's bitmask
  vr_dyn_weapon_t *w = NULL;
  for (int i = 0; i < num_dyn_weapons; i++) {
    if (dyn_weapons[i].bitmask == active) {
      w = &dyn_weapons[i];
      break;
    }
  }

  // Get model name for persistence
  const char *model_name = "";
  if (model_idx > 0 && model_idx < MAX_MODELS && cl.model_precache[model_idx]) {
    model_name = cl.model_precache[model_idx]->name;
  }

  if (w) {
    if (!w->discovered || w->model_index != model_idx) {
      w->model_index = model_idx;
      w->discovered = true;
      if (model_name[0])
        Q_strncpy(w->model_path, model_name, sizeof(w->model_path));
    }

    if (w->impulse == 0 && vr_last_sent_impulse > 0 &&
        (realtime - vr_last_sent_impulse_time) < 0.5) {
      w->impulse = vr_last_sent_impulse;
      Con_Printf("VR: Learned impulse %d for weapon bitmask %d\n", w->impulse,
                 active);
    }
  } else if (num_dyn_weapons < MAX_DYN_WEAPONS) {
    w = &dyn_weapons[num_dyn_weapons];
    memset(w, 0, sizeof(*w));
    w->bitmask = active;
    w->model_index = model_idx;
    w->discovered = true;
    if (model_name[0])
      Q_strncpy(w->model_path, model_name, sizeof(w->model_path));

    if (vr_last_sent_impulse > 0 &&
        (realtime - vr_last_sent_impulse_time) < 0.5) {
      w->impulse = vr_last_sent_impulse;
    }

    num_dyn_weapons++;
    Con_Printf("VR: Discovered new mod weapon %s bitmask %d (impulse %d)\n",
               w->model_path, active, w->impulse);
  }
}

void VR_ResetWeaponTracking(void) {
  last_tracked_activeweapon = -1;
  if (cls.state != ca_connected) {
    vr_autoscan_active = true;
    vr_autoscan_impulse = 0;
    vr_autoscan_next_time = realtime + 2.0;
  }
}

static int VR_GetVisibleWeapons(vr_dyn_weapon_t **out, int max) {
  int count = 0;
  for (int i = 0; i < num_dyn_weapons && count < max; i++) {
    if (cl.items & dyn_weapons[i].bitmask) {
      out[count++] = &dyn_weapons[i];
    }
  }
  return count;
}

int VR_GetSelectedWeaponImpulse(int selection) {
  vr_dyn_weapon_t *visible[MAX_DYN_WEAPONS];
  int num_visible = VR_GetVisibleWeapons(visible, MAX_DYN_WEAPONS);
  if (selection >= 0 && selection < num_visible) {
    return visible[selection]->impulse;
  }
  return 0;
}

void VR_UpdateWeaponSelection(void) {
  if (!VR_Enabled())
    return;

  float x, y;
  VR_GetThumbstick(VR_HAND_RIGHT, &x, &y);

  if (fabsf(x) > 0.5f || fabsf(y) > 0.5f) {
    if (!in_vr_weaponmenu) {
      in_vr_weaponmenu = true;
    }
  } else {
    if (in_vr_weaponmenu) {
      // Finalize selection
      if (vr_weaponmenu_selection != -1) {
        vr_dyn_weapon_t *visible[MAX_DYN_WEAPONS];
        int num_visible = VR_GetVisibleWeapons(visible, MAX_DYN_WEAPONS);
        if (vr_weaponmenu_selection >= 0 &&
            vr_weaponmenu_selection < num_visible) {
          int impulse = visible[vr_weaponmenu_selection]->impulse;
          if (impulse > 0) {
            char cmd[32];
            q_snprintf(cmd, sizeof(cmd), "impulse %d\n", impulse);
            Cbuf_AddText(cmd);
          }
        }
      }
      in_vr_weaponmenu = false;
    }
  }
}

static void VR_SetupWeaponEntity(entity_t *ent, vr_dyn_weapon_t *dyn,
                                 vec3_t pos, vec3_t angles,
                                 float scale_override) {
  memset(ent, 0, sizeof(*ent));
  ent->alpha = ENTALPHA_DEFAULT;
  ent->lerpflags = LERP_RESETANIM | LERP_RESETMOVE;
  ent->colormap = 0;

  // Find model for this weapon
  char *model_path = NULL;
  vec3_t offset = {0, 0, 0};

  // Check schema weapons first
  for (int i = 0; i < num_vr_weapons; i++) {
    if (vr_weapons[i].bitmask == dyn->bitmask) {
      model_path = vr_weapons[i].model_path;
      VectorCopy(vr_weapons[i].offset, offset);
      break;
    }
  }

  if (!model_path) {
    // Fallback to dynamic model path if we captured it
    model_path = dyn->model_path;
  }

  if (model_path) {
    ent->model = Mod_ForName(model_path, false);
  }

  if (!ent->model)
    return;

  VectorCopy(pos, ent->origin);
  VectorCopy(angles, ent->angles);

  // Apply offset in local space
  vec3_t f, r, u;
  AngleVectors(angles, f, r, u);
  VectorMA(ent->origin, offset[0], f, ent->origin);
  VectorMA(ent->origin, offset[1], r, ent->origin);
  VectorMA(ent->origin, offset[2], u, ent->origin);

  // Applying scale_override via ENTSCALE_ENCODE
  ent->scale = ENTSCALE_ENCODE(scale_override);
}

void VR_DrawWeaponMenu(void) {
  if (!in_vr_weaponmenu)
    return;

  vr_dyn_weapon_t *visible[MAX_DYN_WEAPONS];
  int visible_count = VR_GetVisibleWeapons(visible, MAX_DYN_WEAPONS);

  if (visible_count == 0)
    return;

  // Calculate selection based on thumbstick
  float tx, ty;
  VR_GetThumbstick(VR_HAND_RIGHT, &tx, &ty);
  float angle = atan2f(tx, ty) * 180.0f / (float)M_PI;
  if (angle < 0)
    angle += 360.0f;

  float slice = 360.0f / visible_count;
  int best_idx = -1;
  if (fabsf(tx) > 0.3f || fabsf(ty) > 0.3f) {
    best_idx = (int)((angle + slice / 2.0f) / slice) % visible_count;
  }

  if (best_idx != vr_weaponmenu_selection && best_idx != -1) {
    VR_TriggerHaptic(VR_HAND_RIGHT, 0.05f, 0, 0.3f);
  }

  vr_weaponmenu_selection = best_idx;

  // Render weapons in a circle
  vec3_t vieworg, viewangles;
  VectorCopy(r_refdef.vieworg, vieworg);
  VectorCopy(r_refdef.viewangles, viewangles);

  vec3_t forward, right, up;
  AngleVectors(viewangles, forward, right, up);

  float radius = 12.0f;
  float dist = 24.0f; // distance in front of player

  vec3_t center;
  VectorMA(vieworg, dist, forward, center);

  static entity_t menu_ents[MAX_DYN_WEAPONS];
  static entity_t *menu_ent_ptrs[MAX_DYN_WEAPONS];
  int ent_count = 0;

  for (int i = 0; i < visible_count; i++) {
    float a = (i * slice) * (float)M_PI / 180.0f;
    vec3_t pos;
    // Orientation: 0 degrees is "up" (positive Y in 2D, but in Quake 3D depends
    // on vectors) Use right and up vectors to form the circle plane.
    VectorMA(center, sinf(a) * radius, right, pos);
    VectorMA(pos, cosf(a) * radius, up, pos);

    float s = (vr_weaponmenu_selection == i) ? 1.5f : 1.0f;

    // Face the viewer
    vec3_t ent_angles;
    VectorCopy(viewangles, ent_angles);
    ent_angles[PITCH] = -ent_angles[PITCH];
    ent_angles[YAW] += 180;

    VR_SetupWeaponEntity(&menu_ents[i], visible[i], pos, ent_angles, s);
    if (menu_ents[i].model) {
      menu_ent_ptrs[ent_count++] = &menu_ents[i];
    }
  }

  if (ent_count > 0) {
    R_DrawAliasModels(menu_ent_ptrs, ent_count);
  }
}

#endif // VR_ENABLED
