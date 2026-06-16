/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// sv_phys.c

#include "quakedef.h"
#include "pmove.h"
#include "vr.h"

/*


pushmove objects do not obey gravity, and do not interact with each other or
trigger fields, but block normal movement and push normal objects when they
move.

onground is set for toss objects when they come to a complete rest.  it is set
for steping or walking objects

doors, plats, etc are SOLID_BSP, and MOVETYPE_PUSH
bonus items are SOLID_TRIGGER touch, and MOVETYPE_TOSS
corpses are SOLID_NOT and MOVETYPE_TOSS
crates are SOLID_BBOX and MOVETYPE_TOSS
walking monsters are SOLID_SLIDEBOX and MOVETYPE_STEP
flying/floating monsters are SOLID_SLIDEBOX and MOVETYPE_FLY

solid_edge items only clip against bsp models.

*/

cvar_t sv_friction = {"sv_friction", "4", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_stopspeed = {"sv_stopspeed", "100", CVAR_NONE};
cvar_t sv_gravity = {"sv_gravity", "800", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_maxvelocity = {"sv_maxvelocity", "2000", CVAR_NONE};
cvar_t sv_nostep = {"sv_nostep", "0", CVAR_NONE};
cvar_t sv_freezenonclients = {"sv_freezenonclients", "0", CVAR_NONE};
extern cvar_t sv_pmove_legacy_preserve_qc_velocity;

#define MOVE_EPSILON 0.01
#define SV_VANILLA_JUMP_VELOCITY 270.0f

void SV_Physics_Toss(edict_t *ent);

/*
================
SV_CheckAllEnts
================
*/
void SV_CheckAllEnts(void) {
  int e;
  edict_t *check;

  // see if any solid entities are inside the final position
  check = NEXT_EDICT(qcvm->edicts);
  for (e = 1; e < qcvm->num_edicts; e++, check = NEXT_EDICT(check)) {
    if (check->free)
      continue;
    if (check->v.movetype == MOVETYPE_PUSH ||
        check->v.movetype == MOVETYPE_NONE ||
        check->v.movetype == MOVETYPE_NOCLIP)
      continue;

    if (SV_TestEntityPosition(check))
      Con_Printf("entity in invalid position\n");
  }
}

/*
================
SV_CheckVelocity
================
*/
void SV_CheckVelocity(edict_t *ent) {
  int i;

  //
  // bound velocity
  //
  for (i = 0; i < 3; i++) {
    if (IS_NAN(ent->v.velocity[i])) {
      Con_Printf("Got a NaN velocity on %s\n", PR_GetString(ent->v.classname));
      ent->v.velocity[i] = 0;
    }
    if (IS_NAN(ent->v.origin[i])) {
      Con_Printf("Got a NaN origin on %s\n", PR_GetString(ent->v.classname));
      ent->v.origin[i] = 0;
    }
    if (ent->v.velocity[i] > sv_maxvelocity.value)
      ent->v.velocity[i] = sv_maxvelocity.value;
    else if (ent->v.velocity[i] < -sv_maxvelocity.value)
      ent->v.velocity[i] = -sv_maxvelocity.value;
  }
}

/*
=============
Friendly-fire protection helpers

When sv_nofriendlyfire is active in coop, we temporarily:
  - set teamplay = 0  (so QuakeC's teamplay check doesn't block self-damage)
  - set all OTHER players' takedamage = DAMAGE_NO  (so T_Damage skips them)
This allows self-damage (rocket jumping) while blocking inter-player damage.
=============
*/
static float  ff_saved_takedamage[MAX_SCOREBOARD];
static int    ff_saved_teamplay;
static qboolean ff_active = false;

static void SV_FriendlyFireBegin(edict_t *ent) {
  int owner_num;

  if (!sv_nofriendlyfire.value || !coop.value || ff_active)
    return;

  // Determine who the "owner" is — either the entity itself (if it's a
  // player, e.g. during PostThink for hitscan) or the entity's .owner field
  // (if it's a projectile).
  int ent_num = NUM_FOR_EDICT(ent);
  if (ent_num >= 1 && ent_num <= svs.maxclients)
    owner_num = ent_num;
  else {
    edict_t *owner = PROG_TO_EDICT(ent->v.owner);
    owner_num = NUM_FOR_EDICT(owner);
    if (owner_num < 1 || owner_num > svs.maxclients)
      return; // not player-owned, nothing to protect
  }

  ff_active = true;
  ff_saved_teamplay = pr_global_struct->teamplay;
  pr_global_struct->teamplay = 0;

  for (int i = 1; i <= svs.maxclients; i++) {
    edict_t *cl = EDICT_NUM(i);
    ff_saved_takedamage[i - 1] = cl->v.takedamage;
    if (i != owner_num && !cl->free && svs.clients[i - 1].active)
      cl->v.takedamage = DAMAGE_NO;
  }
}

static void SV_FriendlyFireEnd(void) {
  if (!ff_active)
    return;
  pr_global_struct->teamplay = ff_saved_teamplay;
  for (int i = 1; i <= svs.maxclients; i++) {
    edict_t *cl = EDICT_NUM(i);
    cl->v.takedamage = ff_saved_takedamage[i - 1];
  }
  ff_active = false;
}

/*
=============
Coop client helpers
=============
*/
static qboolean SV_CoopIsActiveClient(edict_t *ent) {
  int entnum;

  if (!ent || ent->free)
    return false;

  entnum = NUM_FOR_EDICT(ent);
  return entnum >= 1 && entnum <= svs.maxclients &&
         svs.clients[entnum - 1].active && svs.clients[entnum - 1].spawned;
}

static qboolean SV_CoopIsDeadClient(edict_t *ent) {
  return SV_CoopIsActiveClient(ent) &&
         (ent->v.health <= 0 || ent->v.deadflag >= DEAD_DYING);
}

static void SV_CoopRemoveSpawnTeledeath(edict_t *owner) {
  int i;
  edict_t *ent;

  for (i = svs.maxclients + 1; i < qcvm->num_edicts; i++) {
    ent = EDICT_NUM(i);
    if (ent->free)
      continue;
    if (PROG_TO_EDICT(ent->v.owner) != owner)
      continue;
    if (strcmp(PR_GetString(ent->v.classname), "teledeath"))
      continue;

    ED_Free(ent);
  }
}

/*
=============
Coop revive helpers

Dead clients are SOLID_NOT in the stock and AD QuakeC, so melee traces do not
reliably hit trace_ent. During PlayerPostThink, short player-owned traces are
matched against dead client hulls, then the revive is applied after QuakeC
weapon code returns so its trace globals are not clobbered.
=============
*/
#define COOP_REVIVE_TRACE_PADDING 16.0f
#define COOP_REVIVE_TRACE_EPSILON 0.01f

static edict_t *coop_revive_trace_owner = NULL;
static edict_t *coop_revive_pending_attacker = NULL;
static edict_t *coop_revive_pending_target = NULL;
static float coop_revive_pending_fraction = 0.0f;
static vec3_t coop_revive_pending_origin;
static vec3_t coop_revive_pending_angles;
static vec3_t coop_revive_pending_v_angle;

static void SV_CoopReviveClientBounds(edict_t *ent, vec3_t mins,
                                      vec3_t maxs) {
  int i;

  if (ent->v.size[0] || ent->v.size[1] || ent->v.size[2]) {
    VectorAdd(ent->v.origin, ent->v.mins, mins);
    VectorAdd(ent->v.origin, ent->v.maxs, maxs);
  } else {
    mins[0] = ent->v.origin[0] - 16;
    mins[1] = ent->v.origin[1] - 16;
    mins[2] = ent->v.origin[2] - 24;
    maxs[0] = ent->v.origin[0] + 16;
    maxs[1] = ent->v.origin[1] + 16;
    maxs[2] = ent->v.origin[2] + 32;
  }

  for (i = 0; i < 3; i++) {
    mins[i] -= COOP_REVIVE_TRACE_PADDING;
    maxs[i] += COOP_REVIVE_TRACE_PADDING;
  }
}

static qboolean SV_CoopReviveTraceIntersectsBounds(vec3_t start, vec3_t delta,
                                                   vec3_t mins, vec3_t maxs,
                                                   float *fraction) {
  int i;
  float t1, t2, temp;
  float tmin = 0.0f;
  float tmax = 1.0f;

  for (i = 0; i < 3; i++) {
    if (fabs(delta[i]) < 0.0001f) {
      if (start[i] < mins[i] || start[i] > maxs[i])
        return false;
      continue;
    }

    t1 = (mins[i] - start[i]) / delta[i];
    t2 = (maxs[i] - start[i]) / delta[i];
    if (t1 > t2) {
      temp = t1;
      t1 = t2;
      t2 = temp;
    }

    if (t1 > tmin)
      tmin = t1;
    if (t2 < tmax)
      tmax = t2;
    if (tmin > tmax)
      return false;
  }

  *fraction = tmin;
  return true;
}

static qboolean SV_CoopReviveTraceIsClear(vec3_t start, vec3_t end,
                                          edict_t *attacker) {
  trace_t trace;

  trace = SV_Move(start, vec3_origin, vec3_origin, end, MOVE_NOMONSTERS,
                  attacker);
  return !trace.allsolid && !trace.startsolid &&
         trace.fraction >= 1.0f - COOP_REVIVE_TRACE_EPSILON;
}

static qboolean SV_CoopReviveCanPlaceAt(edict_t *ent, vec3_t origin) {
  trace_t trace;

  trace = SV_Move(origin, ent->v.mins, ent->v.maxs, origin, MOVE_NORMAL, ent);
  return !trace.allsolid && !trace.startsolid;
}

static void SV_CoopReviveSetOrigin(edict_t *ent, vec3_t origin) {
  vec3_t test_origin;
  static const float offsets[] = {0, 8, 16, 24, 32};
  size_t i;

  for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
    VectorCopy(origin, test_origin);
    test_origin[2] += offsets[i];
    if (SV_CoopReviveCanPlaceAt(ent, test_origin)) {
      VectorCopy(test_origin, ent->v.origin);
      return;
    }
  }

  VectorCopy(origin, ent->v.origin);
}

void SV_CoopReviveBeginPostThink(edict_t *ent) {
  coop_revive_trace_owner = ent;
  coop_revive_pending_attacker = NULL;
  coop_revive_pending_target = NULL;
  coop_revive_pending_fraction = 0.0f;
}

void SV_CoopReviveEndPostThink(void) { coop_revive_trace_owner = NULL; }

void SV_CoopReviveFromTrace(vec3_t start, vec3_t end, edict_t *ent,
                            float trace_fraction) {
  int i;
  float range;
  float trace_len;
  vec3_t delta;
  vec3_t mins, maxs;
  vec3_t hit;
  edict_t *target;
  float target_fraction;

  if (!coop.value || !sv_coop_revive.value || !coop_revive_trace_owner)
    return;
  if (ent != coop_revive_trace_owner ||
      !SV_CoopIsActiveClient(coop_revive_trace_owner))
    return;
  if (!sv.active || sv.state != ss_active)
    return;

  range = sv_coop_revive_range.value;
  if (range <= 0)
    return;

  VectorSubtract(end, start, delta);
  trace_len = VectorLength(delta);
  if (trace_len <= 0 || trace_len > range)
    return;

  for (i = 1; i <= svs.maxclients; i++) {
    target = EDICT_NUM(i);
    if (target == ent || !SV_CoopIsDeadClient(target))
      continue;

    SV_CoopReviveClientBounds(target, mins, maxs);
    if (!SV_CoopReviveTraceIntersectsBounds(start, delta, mins, maxs,
                                            &target_fraction))
      continue;
    if (target_fraction < 0.0f || target_fraction > 1.0f)
      continue;
    if (target_fraction > trace_fraction + COOP_REVIVE_TRACE_EPSILON)
      continue;

    hit[0] = start[0] + delta[0] * target_fraction;
    hit[1] = start[1] + delta[1] * target_fraction;
    hit[2] = start[2] + delta[2] * target_fraction;
    if (!SV_CoopReviveTraceIsClear(start, hit, ent))
      continue;

    if (!coop_revive_pending_target ||
        target_fraction < coop_revive_pending_fraction) {
      coop_revive_pending_attacker = ent;
      coop_revive_pending_target = target;
      coop_revive_pending_fraction = target_fraction;
      VectorCopy(target->v.origin, coop_revive_pending_origin);
      VectorCopy(target->v.angles, coop_revive_pending_angles);
      VectorCopy(target->v.v_angle, coop_revive_pending_v_angle);
    }
  }
}

void SV_CoopReviveApplyPending(void) {
  client_t *client;
  client_t *old_host_client;
  edict_t *old_sv_player;
  edict_t *attacker;
  edict_t *target;
  int target_num;
  int i;
  int old_self, old_other;
  float old_time;
  float old_force_retouch;
  vec3_t old_v_forward, old_v_right, old_v_up;
  float health;

  attacker = coop_revive_pending_attacker;
  target = coop_revive_pending_target;
  coop_revive_pending_attacker = NULL;
  coop_revive_pending_target = NULL;

  if (!coop.value || !sv_coop_revive.value)
    return;
  if (!SV_CoopIsActiveClient(attacker) || !SV_CoopIsDeadClient(target))
    return;

  target_num = NUM_FOR_EDICT(target);
  client = &svs.clients[target_num - 1];

  old_self = pr_global_struct->self;
  old_other = pr_global_struct->other;
  old_time = pr_global_struct->time;
  old_force_retouch = pr_global_struct->force_retouch;
  VectorCopy(pr_global_struct->v_forward, old_v_forward);
  VectorCopy(pr_global_struct->v_right, old_v_right);
  VectorCopy(pr_global_struct->v_up, old_v_up);
  old_host_client = host_client;
  old_sv_player = sv_player;

  for (i = 0; i < NUM_SPAWN_PARMS; i++)
    (&pr_global_struct->parm1)[i] = client->spawn_parms[i];

  host_client = client;
  sv_player = target;
  pr_global_struct->time = qcvm->time;
  pr_global_struct->self = EDICT_TO_PROG(target);
  pr_global_struct->other = EDICT_TO_PROG(attacker);
  PR_ExecuteProgram(pr_global_struct->PutClientInServer);
  SV_CoopRemoveSpawnTeledeath(target);
  pr_global_struct->force_retouch = old_force_retouch;

  health = sv_coop_revive_health.value;
  if (health < 1)
    health = 1;

  SV_CoopReviveSetOrigin(target, coop_revive_pending_origin);
  VectorCopy(coop_revive_pending_angles, target->v.angles);
  VectorCopy(coop_revive_pending_v_angle, target->v.v_angle);
  VectorCopy(vec3_origin, target->v.velocity);
  target->v.health = health;
  target->v.deadflag = DEAD_NO;
  target->v.takedamage = DAMAGE_AIM;
  target->v.solid = SOLID_SLIDEBOX;
  target->v.movetype = MOVETYPE_WALK;
  target->v.button0 = target->v.button1 = target->v.button2 = 0;
  target->v.fixangle = true;
  SV_LinkEdict(target, false);

  SV_BroadcastPrintf("%s revived %s\n",
                     svs.clients[NUM_FOR_EDICT(attacker) - 1].name,
                     client->name);

  pr_global_struct->self = old_self;
  pr_global_struct->other = old_other;
  pr_global_struct->time = old_time;
  VectorCopy(old_v_forward, pr_global_struct->v_forward);
  VectorCopy(old_v_right, pr_global_struct->v_right);
  VectorCopy(old_v_up, pr_global_struct->v_up);
  host_client = old_host_client;
  sv_player = old_sv_player;
}

/*
=============
Coop respawn helpers

Let QuakeC perform the normal coop respawn first, then relocate the freshly
respawned player to a safe spot near the death origin. If that fails, try a
safe spot near a living teammate before falling back to the mod's spawn point.
=============
*/
#define COOP_RESPAWN_TRACE_EPSILON 0.01f
#define COOP_RESPAWN_ALL_ITEM_BITS (-1)
#define COOP_RESPAWN_DRAKE_CUSTOM_KEYS (8192 | 16384 | 32768 | 65536)
#define COOP_RESPAWN_AD_KEEP_MODITEMS                                           \
  (2 | 64 | 128 | 4096 | 131072 | 262144 | 524288 | 1048576 | 2097152 |        \
   4194304 | COOP_RESPAWN_DRAKE_CUSTOM_KEYS)

typedef enum {
  COOP_RESPAWN_EXTRA_ITEMS2,
  COOP_RESPAWN_EXTRA_MODITEMS,
  COOP_RESPAWN_EXTRA_CUSTOMKEYS,
  COOP_RESPAWN_EXTRA_WEAPONS,
  COOP_RESPAWN_EXTRA_CURRENTWEAPON,
  COOP_RESPAWN_EXTRA_AMMO_SHELLS1,
  COOP_RESPAWN_EXTRA_AMMO_NAILS1,
  COOP_RESPAWN_EXTRA_AMMO_LAVA_NAILS,
  COOP_RESPAWN_EXTRA_AMMO_ROCKETS1,
  COOP_RESPAWN_EXTRA_AMMO_MULTI_ROCKETS,
  COOP_RESPAWN_EXTRA_AMMO_CELLS1,
  COOP_RESPAWN_EXTRA_AMMO_PLASMA,
  COOP_RESPAWN_EXTRA_CAN_ROCKET,
  COOP_RESPAWN_EXTRA_ROCKET_LAUNCHER_MODE,
  COOP_RESPAWN_EXTRA_JBOOTS_GOT,
  COOP_RESPAWN_EXTRA_JBOOTS_PREVLIMIT,
  COOP_RESPAWN_EXTRA_JBOOTS_RECHARGELIMIT,
  COOP_RESPAWN_EXTRA_JBOOTS_SFX,
  COOP_RESPAWN_EXTRA_JBOOTS_AMMO,
  COOP_RESPAWN_EXTRA_JBOOTS_ONGROUND,
  COOP_RESPAWN_EXTRA_JBOOTS_FINISHED,
  COOP_RESPAWN_EXTRA_JBOOTS_TIME,
  COOP_RESPAWN_EXTRA_JUMPBOOTS_FINISHED,
  COOP_RESPAWN_EXTRA_JUMPBOOTS_TIME,
  COOP_RESPAWN_EXTRA_JUMPBOOTS_AIRLVL,
  COOP_RESPAWN_EXTRA_JUMPBOOTS_AIRMAX,
  COOP_RESPAWN_EXTRA_JUMPBOOTS_HEIGHT,
  COOP_RESPAWN_EXTRA_JUMPBOOTS_FORWARD,
  COOP_RESPAWN_EXTRA_KEYNAME,
  COOP_RESPAWN_EXTRA_CKEYNAME1,
  COOP_RESPAWN_EXTRA_CKEYNAME2,
  COOP_RESPAWN_EXTRA_CKEYNAME3,
  COOP_RESPAWN_EXTRA_CKEYNAME4,
  COOP_RESPAWN_EXTRA_COUNT
} coop_respawn_extra_field_id_t;

typedef enum {
  COOP_RESPAWN_EXTRA_BITMASK,
  COOP_RESPAWN_EXTRA_MAXFLOAT,
  COOP_RESPAWN_EXTRA_RESTORE_FLOAT,
  COOP_RESPAWN_EXTRA_STRING
} coop_respawn_extra_policy_t;

typedef struct {
  const char *name;
  coop_respawn_extra_policy_t policy;
  int mask;
} coop_respawn_extra_field_t;

typedef struct {
  int items;
  float weapon;
  string_t weaponmodel;
  float currentammo;
  float ammo_shells;
  float ammo_nails;
  float ammo_rockets;
  float ammo_cells;
  qboolean extra_valid[COOP_RESPAWN_EXTRA_COUNT];
  int extra_bits[COOP_RESPAWN_EXTRA_COUNT];
  float extra_value[COOP_RESPAWN_EXTRA_COUNT];
  string_t extra_string[COOP_RESPAWN_EXTRA_COUNT];
} coop_respawn_inventory_t;

typedef struct {
  qboolean was_dead;
  qboolean inventory_valid;
  qboolean force_standard_spawn;
  qboolean suppress_respawn_input;
  float old_force_retouch;
  float saved_button0;
  float saved_button1;
  float saved_button2;
  int saved_cmd_buttons;
  vec3_t death_origin;
  vec3_t death_angles;
  vec3_t death_v_angle;
  coop_respawn_inventory_t inventory;
} coop_respawn_postthink_state_t;

static const coop_respawn_extra_field_t coop_respawn_extra_fields[] = {
    {"items2", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
    {"moditems", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_AD_KEEP_MODITEMS},
    {"customkeys", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
    {"weapons", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
    {"currentweapon", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
    {"ammo_shells1", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"ammo_nails1", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"ammo_lava_nails", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"ammo_rockets1", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"ammo_multi_rockets", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"ammo_cells1", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"ammo_plasma", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"can_rocket", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"rocket_launcher_mode", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"jboots_got", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
    {"jboots_prevlimit", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
    {"jboots_rechargelimit", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
    {"jboots_sfx", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
    {"jboots_ammo", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
    {"jboots_onground", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
    {"jboots_finished", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
    {"jboots_time", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
    {"jumpboots_finished", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"jumpboots_time", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"jumpboots_airlvl", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"jumpboots_airmax", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"jumpboots_height", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"jumpboots_forward", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"keyname", COOP_RESPAWN_EXTRA_STRING, 0},
    {"ckeyname1", COOP_RESPAWN_EXTRA_STRING, 0},
    {"ckeyname2", COOP_RESPAWN_EXTRA_STRING, 0},
    {"ckeyname3", COOP_RESPAWN_EXTRA_STRING, 0},
    {"ckeyname4", COOP_RESPAWN_EXTRA_STRING, 0},
};

static coop_respawn_inventory_t
    coop_respawn_last_inventory[MAX_SCOREBOARD];
static qboolean coop_respawn_last_inventory_valid[MAX_SCOREBOARD];
static vec3_t coop_respawn_last_safe_origin[MAX_SCOREBOARD];
static vec3_t coop_respawn_last_safe_angles[MAX_SCOREBOARD];
static vec3_t coop_respawn_last_safe_v_angle[MAX_SCOREBOARD];
static qboolean coop_respawn_last_safe_valid[MAX_SCOREBOARD];
static vec3_t coop_respawn_death_anchor[MAX_SCOREBOARD];
static vec3_t coop_respawn_death_angles[MAX_SCOREBOARD];
static vec3_t coop_respawn_death_v_angle[MAX_SCOREBOARD];
static qboolean coop_respawn_death_anchor_valid[MAX_SCOREBOARD];
static double coop_respawn_dead_since[MAX_SCOREBOARD];
static qboolean coop_respawn_force_standard_spawn[MAX_SCOREBOARD];

static qboolean SV_CoopRespawnCanPlaceAt(edict_t *ent, vec3_t origin);

static qboolean SV_CoopRespawnIsAliveClient(edict_t *ent) {
  return SV_CoopIsActiveClient(ent) && ent->v.health > 0 &&
         ent->v.deadflag == DEAD_NO && ent->v.solid != SOLID_NOT;
}

static qboolean SV_CoopRespawnDelayApplies(void) {
  return coop.value && sv_coop_respawn_near_player.value &&
         sv_coop_respawn_delay.value > 0.0f;
}

static void SV_CoopRespawnSetExtendedButtons(edict_t *ent, int buttons) {
  eval_t *val;

  if (!ent || ent->free)
    return;

  if ((val = GetEdictFieldValue(ent, qcvm->extfields.button3)))
    val->_float = (buttons & (1 << 2)) >> 2;
  if ((val = GetEdictFieldValue(ent, qcvm->extfields.button4)))
    val->_float = (buttons & (1 << 3)) >> 3;
  if ((val = GetEdictFieldValue(ent, qcvm->extfields.button5)))
    val->_float = (buttons & (1 << 4)) >> 4;
  if ((val = GetEdictFieldValue(ent, qcvm->extfields.button6)))
    val->_float = (buttons & (1 << 5)) >> 5;
  if ((val = GetEdictFieldValue(ent, qcvm->extfields.button7)))
    val->_float = (buttons & (1 << 6)) >> 6;
  if ((val = GetEdictFieldValue(ent, qcvm->extfields.button8)))
    val->_float = (buttons & (1 << 7)) >> 7;
}

static qboolean SV_CoopRespawnAnyAliveClient(void) {
  int i;

  for (i = 1; i <= svs.maxclients; i++) {
    if (SV_CoopRespawnIsAliveClient(EDICT_NUM(i)))
      return true;
  }

  return false;
}

static void SV_CoopRespawnMarkTeamWipe(void) {
  int i;
  edict_t *client;

  for (i = 1; i <= svs.maxclients; i++) {
    client = EDICT_NUM(i);
    if (SV_CoopIsDeadClient(client))
      coop_respawn_force_standard_spawn[i - 1] = true;
  }
}

static void SV_CoopRespawnSuppressInput(
    edict_t *ent, int num, coop_respawn_postthink_state_t *state) {
  client_t *client;

  if (state->suppress_respawn_input)
    return;
  if (num < 1 || num > svs.maxclients)
    return;

  client = &svs.clients[num - 1];
  state->suppress_respawn_input = true;
  state->saved_button0 = ent->v.button0;
  state->saved_button1 = ent->v.button1;
  state->saved_button2 = ent->v.button2;
  state->saved_cmd_buttons = client->cmd.buttons;

  ent->v.button0 = ent->v.button1 = ent->v.button2 = 0;
  ent->v.impulse = 0;
  client->cmd.buttons = 0;
  client->cmd.impulse = 0;
  SV_CoopRespawnSetExtendedButtons(ent, 0);
}

static void SV_CoopRespawnRestoreSuppressedInput(
    edict_t *ent, int num, const coop_respawn_postthink_state_t *state) {
  client_t *client;

  if (!state->suppress_respawn_input)
    return;
  if (num < 1 || num > svs.maxclients)
    return;

  client = &svs.clients[num - 1];
  client->cmd.buttons = state->saved_cmd_buttons;
  client->cmd.impulse = 0;

  if (!ent || ent->free)
    return;

  ent->v.button0 = state->saved_button0;
  ent->v.button1 = state->saved_button1;
  ent->v.button2 = state->saved_button2;
  ent->v.impulse = 0;
  SV_CoopRespawnSetExtendedButtons(ent, state->saved_cmd_buttons);
}

static float SV_CoopRespawnMaxFloat(float a, float b) {
  return a > b ? a : b;
}

static float SV_CoopRespawnCurrentAmmoForWeapon(edict_t *ent, float weapon,
                                                float fallback) {
  int weapon_item = (int)weapon;

  if (weapon_item == IT_SHOTGUN || weapon_item == IT_SUPER_SHOTGUN)
    return ent->v.ammo_shells;

  if (weapon_item == IT_NAILGUN || weapon_item == IT_SUPER_NAILGUN ||
      (rogue && (weapon_item == RIT_LAVA_NAILGUN ||
                 weapon_item == RIT_LAVA_SUPER_NAILGUN)))
    return ent->v.ammo_nails;

  if (weapon_item == IT_GRENADE_LAUNCHER ||
      weapon_item == IT_ROCKET_LAUNCHER ||
      (rogue && (weapon_item == RIT_MULTI_GRENADE ||
                 weapon_item == RIT_MULTI_ROCKET)) ||
      (hipnotic && weapon_item == HIT_PROXIMITY_GUN))
    return ent->v.ammo_rockets;

  if (weapon_item == IT_LIGHTNING ||
      (hipnotic && (weapon_item == HIT_LASER_CANNON ||
                    weapon_item == HIT_MJOLNIR)) ||
      (rogue && weapon_item == RIT_PLASMA_GUN))
    return ent->v.ammo_cells;

  return fallback;
}

static int SV_CoopRespawnKeepItemMask(void) {
  int mask;

  mask = IT_SHOTGUN | IT_SUPER_SHOTGUN | IT_NAILGUN | IT_SUPER_NAILGUN |
         IT_GRENADE_LAUNCHER | IT_ROCKET_LAUNCHER | IT_LIGHTNING |
         IT_SUPER_LIGHTNING | IT_AXE | IT_SHELLS | IT_NAILS | IT_ROCKETS |
         IT_CELLS | IT_KEY1 | IT_KEY2;

  if (rogue)
    mask |= RIT_AXE | RIT_LAVA_NAILGUN | RIT_LAVA_SUPER_NAILGUN |
            RIT_MULTI_GRENADE | RIT_MULTI_ROCKET | RIT_PLASMA_GUN |
            RIT_SHELLS | RIT_NAILS | RIT_ROCKETS | RIT_CELLS |
            RIT_LAVA_NAILS | RIT_PLASMA_AMMO | RIT_MULTI_ROCKETS;

  if (hipnotic)
    mask |= HIT_PROXIMITY_GUN | HIT_MJOLNIR | HIT_LASER_CANNON;

  return mask;
}

static eval_t *SV_CoopRespawnGetExtraField(edict_t *ent, int index,
                                           int *type_out) {
  const coop_respawn_extra_field_t *field;
  ddef_t *def;
  int type;

  if (!ent || ent->free || index < 0 || index >= COOP_RESPAWN_EXTRA_COUNT)
    return NULL;

  field = &coop_respawn_extra_fields[index];
  def = ED_FindField(field->name);
  if (!def)
    return NULL;

  type = def->type & ~DEF_SAVEGLOBAL;
  if (field->policy == COOP_RESPAWN_EXTRA_STRING) {
    if (type != ev_string)
      return NULL;
  } else if (field->policy == COOP_RESPAWN_EXTRA_BITMASK) {
    if (type != ev_float && type != ev_ext_integer)
      return NULL;
  } else if (type != ev_float) {
    return NULL;
  }

  if (type_out)
    *type_out = type;
  return GetEdictFieldValue(ent, def->ofs);
}

static void SV_CoopRespawnSaveInventory(edict_t *ent,
                                        coop_respawn_inventory_t *inventory) {
  int i;
  int type;
  eval_t *val;

  memset(inventory, 0, sizeof(*inventory));
  inventory->items = (int)ent->v.items & SV_CoopRespawnKeepItemMask();
  inventory->weapon = ent->v.weapon;
  inventory->weaponmodel = ent->v.weaponmodel;
  inventory->currentammo = ent->v.currentammo;
  inventory->ammo_shells = ent->v.ammo_shells;
  inventory->ammo_nails = ent->v.ammo_nails;
  inventory->ammo_rockets = ent->v.ammo_rockets;
  inventory->ammo_cells = ent->v.ammo_cells;

  for (i = 0; i < COOP_RESPAWN_EXTRA_COUNT; i++) {
    val = SV_CoopRespawnGetExtraField(ent, i, &type);
    if (!val)
      continue;

    if (coop_respawn_extra_fields[i].policy == COOP_RESPAWN_EXTRA_STRING) {
      if (val->string && PR_GetString(val->string)[0]) {
        inventory->extra_valid[i] = true;
        inventory->extra_string[i] = val->string;
      }
    } else {
      inventory->extra_valid[i] = true;
      if (coop_respawn_extra_fields[i].policy == COOP_RESPAWN_EXTRA_BITMASK) {
        if (type == ev_ext_integer)
          inventory->extra_bits[i] =
              val->_int & coop_respawn_extra_fields[i].mask;
        else
          inventory->extra_bits[i] =
              (int)val->_float & coop_respawn_extra_fields[i].mask;
      } else {
        inventory->extra_value[i] = val->_float;
      }
    }
  }
}

static void SV_CoopRespawnRestoreInventory(
    edict_t *ent, const coop_respawn_inventory_t *inventory) {
  int i;
  int type;
  eval_t *val;

  ent->v.items = (int)ent->v.items | inventory->items;
  ent->v.ammo_shells =
      SV_CoopRespawnMaxFloat(ent->v.ammo_shells, inventory->ammo_shells);
  ent->v.ammo_nails =
      SV_CoopRespawnMaxFloat(ent->v.ammo_nails, inventory->ammo_nails);
  ent->v.ammo_rockets =
      SV_CoopRespawnMaxFloat(ent->v.ammo_rockets, inventory->ammo_rockets);
  ent->v.ammo_cells =
      SV_CoopRespawnMaxFloat(ent->v.ammo_cells, inventory->ammo_cells);

  if (inventory->weapon > 0)
    ent->v.weapon = inventory->weapon;
  if (inventory->weaponmodel)
    ent->v.weaponmodel = inventory->weaponmodel;

  for (i = 0; i < COOP_RESPAWN_EXTRA_COUNT; i++) {
    if (!inventory->extra_valid[i])
      continue;

    val = SV_CoopRespawnGetExtraField(ent, i, &type);
    if (!val)
      continue;

    if (coop_respawn_extra_fields[i].policy == COOP_RESPAWN_EXTRA_STRING) {
      val->string = inventory->extra_string[i];
    } else if (coop_respawn_extra_fields[i].policy ==
               COOP_RESPAWN_EXTRA_BITMASK) {
      if (type == ev_ext_integer)
        val->_int = val->_int | inventory->extra_bits[i];
      else
        val->_float = (int)val->_float | inventory->extra_bits[i];
    } else if (coop_respawn_extra_fields[i].policy ==
               COOP_RESPAWN_EXTRA_RESTORE_FLOAT) {
      val->_float = inventory->extra_value[i];
    } else {
      val->_float =
          SV_CoopRespawnMaxFloat(val->_float, inventory->extra_value[i]);
    }
  }

  if (inventory->weapon > 0)
    ent->v.currentammo =
        SV_CoopRespawnCurrentAmmoForWeapon(ent, inventory->weapon,
                                           inventory->currentammo);
  else
    ent->v.currentammo =
        SV_CoopRespawnMaxFloat(ent->v.currentammo, inventory->currentammo);
}

static void SV_CoopRespawnRememberAliveInventory(edict_t *ent, int num) {
  int index;

  if (!coop.value || !SV_CoopRespawnIsAliveClient(ent))
    return;

  index = num - 1;
  if (index < 0 || index >= MAX_SCOREBOARD)
    return;

  SV_CoopRespawnSaveInventory(ent, &coop_respawn_last_inventory[index]);
  coop_respawn_last_inventory_valid[index] = true;
}

static void SV_CoopRespawnRememberSafeOrigin(edict_t *ent, int num) {
  int index;

  if (!coop.value || !SV_CoopRespawnIsAliveClient(ent))
    return;

  index = num - 1;
  if (index < 0 || index >= MAX_SCOREBOARD)
    return;

  if (!SV_CoopRespawnCanPlaceAt(ent, ent->v.origin))
    return;

  VectorCopy(ent->v.origin, coop_respawn_last_safe_origin[index]);
  VectorCopy(ent->v.angles, coop_respawn_last_safe_angles[index]);
  VectorCopy(ent->v.v_angle, coop_respawn_last_safe_v_angle[index]);
  coop_respawn_last_safe_valid[index] = true;
}

static void SV_CoopRespawnRememberAliveState(edict_t *ent, int num) {
  SV_CoopRespawnRememberAliveInventory(ent, num);
  SV_CoopRespawnRememberSafeOrigin(ent, num);
}

static void SV_CoopRespawnRecordDeathAnchor(
    edict_t *ent, int num, const coop_respawn_postthink_state_t *state) {
  int index;

  if (!coop.value || !ent || ent->free)
    return;

  index = num - 1;
  if (index < 0 || index >= MAX_SCOREBOARD)
    return;

  if (coop_respawn_last_safe_valid[index]) {
    VectorCopy(coop_respawn_last_safe_origin[index],
               coop_respawn_death_anchor[index]);
    VectorCopy(coop_respawn_last_safe_angles[index],
               coop_respawn_death_angles[index]);
    VectorCopy(coop_respawn_last_safe_v_angle[index],
               coop_respawn_death_v_angle[index]);
  } else {
    VectorCopy(state->death_origin, coop_respawn_death_anchor[index]);
    VectorCopy(state->death_angles, coop_respawn_death_angles[index]);
    VectorCopy(state->death_v_angle, coop_respawn_death_v_angle[index]);
  }
  coop_respawn_death_anchor_valid[index] = true;
  if (coop_respawn_dead_since[index] <= 0)
    coop_respawn_dead_since[index] = qcvm->time;
}

static void SV_CoopRespawnUseDeathAnchor(
    int num, coop_respawn_postthink_state_t *state) {
  int index;

  index = num - 1;
  if (index < 0 || index >= MAX_SCOREBOARD ||
      !coop_respawn_death_anchor_valid[index])
    return;

  VectorCopy(coop_respawn_death_anchor[index], state->death_origin);
  VectorCopy(coop_respawn_death_angles[index], state->death_angles);
  VectorCopy(coop_respawn_death_v_angle[index], state->death_v_angle);
}

static qboolean SV_CoopRespawnPointContentsOK(vec3_t origin, edict_t *ent) {
  int i, cont;
  vec3_t point;
  float checks[3];

  checks[0] = ent->v.mins[2] + 1.0f;
  checks[1] = 0.0f;
  checks[2] = ent->v.maxs[2] - 1.0f;

  for (i = 0; i < (int)(sizeof(checks) / sizeof(checks[0])); i++) {
    VectorCopy(origin, point);
    point[2] += checks[i];
    cont = SV_PointContents(point);
    if (cont == CONTENTS_SOLID || cont == CONTENTS_LAVA ||
        cont == CONTENTS_SLIME)
      return false;
  }

  return true;
}

static qboolean SV_CoopRespawnCanPlaceAt(edict_t *ent, vec3_t origin) {
  qboolean bottom;
  trace_t trace;
  vec3_t old_origin;

  if (!SV_CoopRespawnPointContentsOK(origin, ent))
    return false;

  trace = SV_Move(origin, ent->v.mins, ent->v.maxs, origin, MOVE_NORMAL, ent);
  if (trace.allsolid || trace.startsolid)
    return false;

  VectorCopy(ent->v.origin, old_origin);
  VectorCopy(origin, ent->v.origin);
  bottom = SV_CheckBottom(ent);
  VectorCopy(old_origin, ent->v.origin);

  return bottom;
}

static qboolean SV_CoopRespawnTraceClear(vec3_t start, vec3_t end,
                                         edict_t *ignore) {
  trace_t trace;

  trace = SV_Move(start, vec3_origin, vec3_origin, end, MOVE_NOMONSTERS,
                  ignore);
  return !trace.allsolid && !trace.startsolid &&
         trace.fraction >= 1.0f - COOP_RESPAWN_TRACE_EPSILON;
}

static qboolean SV_CoopRespawnDropToFloor(edict_t *ent, vec3_t origin,
                                          float max_drop,
                                          vec3_t floor_origin) {
  int i;
  trace_t trace;
  vec3_t start, end;
  static const float raises[] = {96.0f, 64.0f, 48.0f, 32.0f, 16.0f, 8.0f};

  for (i = 0; i < (int)(sizeof(raises) / sizeof(raises[0])); i++) {
    VectorCopy(origin, start);
    start[2] += raises[i];
    VectorCopy(start, end);
    end[2] -= 384.0f;

    trace = SV_Move(start, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent);
    if (trace.allsolid || trace.startsolid || trace.fraction == 1.0f)
      continue;

    VectorCopy(trace.endpos, floor_origin);
    if (max_drop > 0 && floor_origin[2] < origin[2] - max_drop)
      continue;
    if (SV_CoopRespawnCanPlaceAt(ent, floor_origin))
      return true;
  }

  return false;
}

static void SV_CoopRespawnBasis(edict_t *anchor, vec3_t forward,
                                vec3_t right) {
  vec3_t up;

  if (anchor) {
    AngleVectors(anchor->v.angles, forward, right, up);
    forward[2] = 0.0f;
    right[2] = 0.0f;
    if (VectorNormalize(forward) < 0.01f) {
      forward[0] = 1.0f;
      forward[1] = forward[2] = 0.0f;
    }
    if (VectorNormalize(right) < 0.01f) {
      right[0] = 0.0f;
      right[1] = -1.0f;
      right[2] = 0.0f;
    }
  } else {
    forward[0] = 1.0f;
    forward[1] = forward[2] = 0.0f;
    right[0] = 0.0f;
    right[1] = 1.0f;
    right[2] = 0.0f;
  }
}

static qboolean SV_CoopRespawnFindNearbySpot(edict_t *ent, const vec3_t base,
                                             edict_t *anchor,
                                             const float *radii,
                                             int num_radii,
                                             float max_drop,
                                             vec3_t spot) {
  int i, j;
  vec3_t candidate, dropped, forward, right;
  vec3_t trace_start, trace_end;
  static const float dirs[][2] = {
      {0.0f, 0.0f},        {1.0f, 0.0f},        {0.9239f, 0.3827f},
      {0.7071f, 0.7071f},  {0.3827f, 0.9239f},  {0.0f, 1.0f},
      {-0.3827f, 0.9239f}, {-0.7071f, 0.7071f}, {-0.9239f, 0.3827f},
      {-1.0f, 0.0f},       {-0.9239f, -0.3827f},
      {-0.7071f, -0.7071f}, {-0.3827f, -0.9239f}, {0.0f, -1.0f},
      {0.3827f, -0.9239f}, {0.7071f, -0.7071f}, {0.9239f, -0.3827f},
  };

  SV_CoopRespawnBasis(anchor, forward, right);

  for (i = 0; i < num_radii; i++) {
    for (j = 0; j < (int)(sizeof(dirs) / sizeof(dirs[0])); j++) {
      if (radii[i] > 0.0f && dirs[j][0] == 0.0f && dirs[j][1] == 0.0f)
        continue;
      if (radii[i] == 0.0f && j > 0)
        continue;

      VectorCopy(base, candidate);
      candidate[0] += (forward[0] * dirs[j][0] + right[0] * dirs[j][1]) *
                      radii[i];
      candidate[1] += (forward[1] * dirs[j][0] + right[1] * dirs[j][1]) *
                      radii[i];

      if (!SV_CoopRespawnDropToFloor(ent, candidate, max_drop, dropped))
        continue;

      if (anchor) {
        VectorCopy(anchor->v.origin, trace_start);
        trace_start[2] += 16.0f;
        VectorCopy(dropped, trace_end);
        trace_end[2] += 16.0f;
        if (!SV_CoopRespawnTraceClear(trace_start, trace_end, anchor))
          continue;
      }

      VectorCopy(dropped, spot);
      return true;
    }
  }

  return false;
}

typedef struct {
  edict_t *ent;
  float score;
  float dist;
} coop_respawn_anchor_candidate_t;

static qboolean SV_CoopRespawnAnchorIsBetter(
    const coop_respawn_anchor_candidate_t *a,
    const coop_respawn_anchor_candidate_t *b) {
  if (a->score != b->score)
    return a->score > b->score;
  return a->dist < b->dist;
}

static int SV_CoopRespawnBuildAnchorCandidates(
    edict_t *ent, const vec3_t death_origin,
    coop_respawn_anchor_candidate_t *candidates, int max_candidates) {
  int i, j, count = 0;
  vec3_t delta;
  edict_t *client;
  coop_respawn_anchor_candidate_t candidate;

  for (i = 1; i <= svs.maxclients; i++) {
    client = EDICT_NUM(i);
    if (client == ent || !SV_CoopRespawnIsAliveClient(client))
      continue;

    VectorSubtract(client->v.origin, death_origin, delta);
    candidate.ent = client;
    candidate.score = client->v.frags;
    candidate.dist = DotProduct(delta, delta);

    if (max_candidates <= 0)
      break;

    if (count == max_candidates &&
        !SV_CoopRespawnAnchorIsBetter(&candidate, &candidates[count - 1]))
      continue;

    if (count < max_candidates)
      count++;

    for (j = count - 1; j > 0 &&
         SV_CoopRespawnAnchorIsBetter(&candidate, &candidates[j - 1]);
         j--)
      candidates[j] = candidates[j - 1];

    candidates[j] = candidate;
  }

  return count;
}

static qboolean SV_CoopRespawnFindAnchorSpot(edict_t *ent,
                                             const vec3_t death_origin,
                                             edict_t **anchor_out,
                                             vec3_t spot) {
  int i, count;
  coop_respawn_anchor_candidate_t candidates[MAX_SCOREBOARD];
  static const float player_radii[] = {48.0f, 64.0f, 80.0f, 96.0f, 128.0f};

  count = SV_CoopRespawnBuildAnchorCandidates(
      ent, death_origin, candidates,
      (int)(sizeof(candidates) / sizeof(candidates[0])));

  for (i = 0; i < count; i++) {
    if (SV_CoopRespawnFindNearbySpot(
            ent, candidates[i].ent->v.origin, candidates[i].ent, player_radii,
            (int)(sizeof(player_radii) / sizeof(player_radii[0])), 384.0f,
            spot)) {
      if (anchor_out)
        *anchor_out = candidates[i].ent;
      return true;
    }
  }

  return false;
}

static qboolean SV_CoopRespawnFindSpot(edict_t *ent, const vec3_t death_origin,
                                       edict_t **anchor_out, vec3_t spot,
                                       qboolean allow_death_fallback) {
  static const float death_radii[] = {0.0f, 40.0f, 64.0f, 96.0f, 128.0f,
                                      160.0f, 192.0f, 224.0f, 256.0f};

  if (SV_CoopRespawnFindAnchorSpot(ent, death_origin, anchor_out, spot))
    return true;

  if (allow_death_fallback) {
    if (SV_CoopRespawnFindNearbySpot(
            ent, death_origin, NULL, death_radii,
            (int)(sizeof(death_radii) / sizeof(death_radii[0])), 96.0f,
            spot)) {
      if (anchor_out)
        *anchor_out = NULL;
      return true;
    }
  }

  return false;
}

static void
SV_CoopRespawnApplyAngles(edict_t *ent, edict_t *anchor,
                          const coop_respawn_postthink_state_t *state) {
  vec3_t angles;

  if (anchor) {
    angles[0] = 0.0f;
    angles[1] = anchor->v.angles[1];
    angles[2] = 0.0f;
    VectorCopy(angles, ent->v.angles);
    VectorCopy(angles, ent->v.v_angle);
  } else {
    VectorCopy(state->death_angles, ent->v.angles);
    VectorCopy(state->death_v_angle, ent->v.v_angle);
  }

  ent->v.fixangle = true;
}

static void SV_CoopRespawnRelocate(
    edict_t *ent, edict_t *anchor, vec3_t spot,
    const coop_respawn_postthink_state_t *state) {
  SV_CoopRemoveSpawnTeledeath(ent);
  pr_global_struct->force_retouch = state->old_force_retouch;

  VectorCopy(spot, ent->v.origin);
  VectorCopy(vec3_origin, ent->v.velocity);
  SV_CoopRespawnApplyAngles(ent, anchor, state);
  SV_LinkEdict(ent, false);
}

qboolean SV_CoopRespawnPlaceNearPlayer(edict_t *ent) {
  int entnum;
  edict_t *anchor = NULL;
  vec3_t spot;
  coop_respawn_postthink_state_t state;

  if (!coop.value || !sv_coop_respawn_near_player.value || !ent || ent->free)
    return false;

  entnum = NUM_FOR_EDICT(ent);
  if (entnum < 1 || entnum > svs.maxclients ||
      !svs.clients[entnum - 1].active || ent->v.health <= 0 ||
      ent->v.deadflag != DEAD_NO || ent->v.solid == SOLID_NOT)
    return false;

  memset(&state, 0, sizeof(state));
  state.old_force_retouch = pr_global_struct->force_retouch;
  VectorCopy(ent->v.origin, state.death_origin);
  VectorCopy(ent->v.angles, state.death_angles);
  VectorCopy(ent->v.v_angle, state.death_v_angle);

  if (!SV_CoopRespawnFindSpot(ent, state.death_origin, &anchor, spot, false))
    return false;

  SV_CoopRespawnRelocate(ent, anchor, spot, &state);
  return true;
}

static void SV_CoopRespawnBeginPostThink(
    edict_t *ent, int num, coop_respawn_postthink_state_t *state) {
  int index;
  double dead_time;

  memset(state, 0, sizeof(*state));
  state->was_dead = SV_CoopIsDeadClient(ent);
  state->old_force_retouch = pr_global_struct->force_retouch;
  VectorCopy(ent->v.origin, state->death_origin);
  VectorCopy(ent->v.angles, state->death_angles);
  VectorCopy(ent->v.v_angle, state->death_v_angle);

  if (!coop.value)
    return;

  index = num - 1;
  if (index < 0 || index >= MAX_SCOREBOARD)
    return;

  if (!state->was_dead) {
    coop_respawn_dead_since[index] = 0;
    coop_respawn_force_standard_spawn[index] = false;
    SV_CoopRespawnRememberAliveState(ent, num);
    return;
  }

  if (!SV_CoopRespawnAnyAliveClient())
    SV_CoopRespawnMarkTeamWipe();
  state->force_standard_spawn = coop_respawn_force_standard_spawn[index];

  SV_CoopRespawnUseDeathAnchor(num, state);

  if (coop_respawn_last_inventory_valid[index]) {
    state->inventory = coop_respawn_last_inventory[index];
    state->inventory_valid = true;
  } else {
    SV_CoopRespawnSaveInventory(ent, &state->inventory);
    state->inventory_valid = true;
  }

  if (!SV_CoopRespawnDelayApplies())
    return;

  dead_time = coop_respawn_dead_since[index];
  if (dead_time <= 0) {
    dead_time = qcvm->time;
    coop_respawn_dead_since[index] = dead_time;
  }

  if (qcvm->time - dead_time < sv_coop_respawn_delay.value)
    SV_CoopRespawnSuppressInput(ent, num, state);
}

static void SV_CoopRespawnEndPostThink(
    edict_t *ent, int num, const coop_respawn_postthink_state_t *state) {
  int index;
  edict_t *anchor = NULL;
  vec3_t spot;

  if (!coop.value) {
    SV_CoopRespawnRestoreSuppressedInput(ent, num, state);
    return;
  }

  if (!state->was_dead && SV_CoopIsDeadClient(ent)) {
    SV_CoopRespawnRecordDeathAnchor(ent, num, state);
    if (!SV_CoopRespawnAnyAliveClient())
      SV_CoopRespawnMarkTeamWipe();
    SV_CoopRespawnRestoreSuppressedInput(ent, num, state);
    return;
  }

  if (state->was_dead && SV_CoopRespawnIsAliveClient(ent)) {
    if (sv_coop_respawn_keep_weapons_ammo.value && state->inventory_valid)
      SV_CoopRespawnRestoreInventory(ent, &state->inventory);

    if (sv_coop_respawn_near_player.value && !state->force_standard_spawn) {
      if (SV_CoopRespawnFindSpot(ent, state->death_origin, &anchor, spot, true))
        SV_CoopRespawnRelocate(ent, anchor, spot, state);
      else if (net_lagdebug.value)
        Con_Printf("net_lagdebug: coop respawn could not find a safe near-death or teammate spot for client %d death_origin=(%.1f %.1f %.1f)\n",
                   num, state->death_origin[0], state->death_origin[1],
                   state->death_origin[2]);
    }
    index = num - 1;
    if (index >= 0 && index < MAX_SCOREBOARD) {
      coop_respawn_death_anchor_valid[index] = false;
      coop_respawn_dead_since[index] = 0;
      coop_respawn_force_standard_spawn[index] = false;
    }
  }

  SV_CoopRespawnRestoreSuppressedInput(ent, num, state);
  SV_CoopRespawnRememberAliveState(ent, num);
}

/*
=============
SV_RunThink

Runs thinking code if time.  There is some play in the exact time the think
function will be called, because it is called before any movement is done
in a frame.  Not used for pushmove objects, because they must be exact.
Returns false if the entity removed itself.
=============
*/
qboolean SV_RunThink(edict_t *ent) {
  float thinktime;

  thinktime = ent->v.nextthink;
  if (thinktime <= 0 || thinktime > qcvm->time + host_frametime)
    return true;

  if (thinktime < qcvm->time)
    thinktime = qcvm->time; // don't let things stay in the past.
                         // it is possible to start that way
                         // by a trigger with a local time.

  ent->oldthinktime = thinktime;
  ent->oldframe = ent->v.frame; // johnfitz

  ent->v.nextthink = 0;
  pr_global_struct->time = thinktime;
  pr_global_struct->self = EDICT_TO_PROG(ent);
  pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);

  SV_FriendlyFireBegin(ent);
  PR_ExecuteProgram(ent->v.think);
  SV_FriendlyFireEnd();

  return !ent->free;
}

/*
==================
SV_Impact

Two entities have touched, so run their touch functions
==================
*/
static const char *SV_DebugImpactStringField(edict_t *ent, const char *fieldname) {
  eval_t *val;

  val = GetEdictFieldValueByName(ent, fieldname);
  if (!val || !val->string)
    return "";
  return PR_GetString(val->string);
}

static qboolean SV_DebugShouldLogDamageableTrigger(edict_t *ent) {
  const char *classname;

  if (!sv_triggerdebug.value || !ent || ent == qcvm->edicts || ent->free)
    return false;
  if (!ent->v.classname || (ent->v.health <= 0 && ent->v.takedamage <= DAMAGE_NO))
    return false;

  classname = PR_GetString(ent->v.classname);
  return !q_strncasecmp(classname, "trigger_", 8) ||
         !q_strncasecmp(classname, "func_", 5);
}

static void SV_DebugLogTriggerImpact(edict_t *touch, edict_t *other) {
  if (!SV_DebugShouldLogDamageableTrigger(touch))
    return;

  Con_Printf("sv_triggerdebug: impact #%d %s with #%d %s solid=%d health=%.1f takedamage=%.0f targetname=\"%s\" target=\"%s\" target2=\"%s\" target3=\"%s\" target4=\"%s\"\n",
             NUM_FOR_EDICT(touch),
             touch->v.classname ? PR_GetString(touch->v.classname) : "",
             other ? NUM_FOR_EDICT(other) : 0,
             (other && other->v.classname) ? PR_GetString(other->v.classname) : "",
             (int)touch->v.solid, touch->v.health, touch->v.takedamage,
             SV_DebugImpactStringField(touch, "targetname"),
             SV_DebugImpactStringField(touch, "target"),
             SV_DebugImpactStringField(touch, "target2"),
             SV_DebugImpactStringField(touch, "target3"),
             SV_DebugImpactStringField(touch, "target4"));
}

void SV_Impact(edict_t *e1, edict_t *e2) {
  int old_self, old_other;

  old_self = pr_global_struct->self;
  old_other = pr_global_struct->other;

  pr_global_struct->time = qcvm->time;
  SV_DebugLogTriggerImpact(e1, e2);
  SV_DebugLogTriggerImpact(e2, e1);

  if (e1->v.touch && e1->v.solid != SOLID_NOT) {
    pr_global_struct->self = EDICT_TO_PROG(e1);
    pr_global_struct->other = EDICT_TO_PROG(e2);
    SV_FriendlyFireBegin(e1);
    PR_ExecuteProgram(e1->v.touch);
    SV_FriendlyFireEnd();
  }

  if (e2->v.touch && e2->v.solid != SOLID_NOT) {
    pr_global_struct->self = EDICT_TO_PROG(e2);
    pr_global_struct->other = EDICT_TO_PROG(e1);
    SV_FriendlyFireBegin(e2);
    PR_ExecuteProgram(e2->v.touch);
    SV_FriendlyFireEnd();
  }

  pr_global_struct->self = old_self;
  pr_global_struct->other = old_other;
}

/*
==================
ClipVelocity

Slide off of the impacting object
returns the blocked flags (1 = floor, 2 = step / wall)
==================
*/
#define STOP_EPSILON 0.1

int ClipVelocity(vec3_t in, vec3_t normal, vec3_t out, float overbounce) {
  float backoff;
  float change;
  int i, blocked;

  blocked = 0;
  if (normal[2] > 0)
    blocked |= 1; // floor
  if (!normal[2])
    blocked |= 2; // step

  backoff = DotProduct(in, normal) * overbounce;

  for (i = 0; i < 3; i++) {
    change = normal[i] * backoff;
    out[i] = in[i] - change;
    if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
      out[i] = 0;
  }

  return blocked;
}

/*
============
SV_FlyMove

The basic solid body movement clip that slides along multiple planes
Returns the clipflags if the velocity was modified (hit something solid)
1 = floor
2 = wall / step
4 = dead stop
If steptrace is not NULL, the trace of any vertical wall hit will be stored
============
*/
#define MAX_CLIP_PLANES 5
int SV_FlyMove(edict_t *ent, float time, trace_t *steptrace) {
  int bumpcount, numbumps;
  vec3_t dir;
  float d;
  int numplanes;
  vec3_t planes[MAX_CLIP_PLANES];
  vec3_t primal_velocity, original_velocity, new_velocity;
  int i, j;
  trace_t trace;
  vec3_t end;
  float time_left;
  int blocked;

  numbumps = 4;

  blocked = 0;
  VectorCopy(ent->v.velocity, original_velocity);
  VectorCopy(ent->v.velocity, primal_velocity);
  numplanes = 0;

  time_left = time;

  for (bumpcount = 0; bumpcount < numbumps; bumpcount++) {
    if (!ent->v.velocity[0] && !ent->v.velocity[1] && !ent->v.velocity[2])
      break;

    for (i = 0; i < 3; i++)
      end[i] = ent->v.origin[i] + time_left * ent->v.velocity[i];

    trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);

    if (trace.allsolid) { // entity is trapped in another solid
      VectorCopy(vec3_origin, ent->v.velocity);
      return 3;
    }

    if (trace.fraction > 0) { // actually covered some distance
      VectorCopy(trace.endpos, ent->v.origin);
      VectorCopy(ent->v.velocity, original_velocity);
      numplanes = 0;
    }

    if (trace.fraction == 1)
      break; // moved the entire distance

    if (!trace.ent)
      Sys_Error("SV_FlyMove: !trace.ent");

    if (trace.plane.normal[2] > 0.7) {
      blocked |= 1; // floor
      if (trace.ent->v.solid == SOLID_BSP) {
        ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
        ent->v.groundentity = EDICT_TO_PROG(trace.ent);
      }
    }
    if (!trace.plane.normal[2]) {
      blocked |= 2; // step
      if (steptrace)
        *steptrace = trace; // save for player extrafriction
    }

    //
    // run the impact function
    //
    SV_Impact(ent, trace.ent);
    if (ent->free)
      break; // removed by the impact function

    time_left -= time_left * trace.fraction;

    // cliped to another plane
    if (numplanes >= MAX_CLIP_PLANES) { // this shouldn't really happen
      VectorCopy(vec3_origin, ent->v.velocity);
      return 3;
    }

    VectorCopy(trace.plane.normal, planes[numplanes]);
    numplanes++;

    //
    // modify original_velocity so it parallels all of the clip planes
    //
    for (i = 0; i < numplanes; i++) {
      ClipVelocity(original_velocity, planes[i], new_velocity, 1);
      for (j = 0; j < numplanes; j++)
        if (j != i) {
          if (DotProduct(new_velocity, planes[j]) < 0)
            break; // not ok
        }
      if (j == numplanes)
        break;
    }

    if (i != numplanes) { // go along this plane
      VectorCopy(new_velocity, ent->v.velocity);
    } else { // go along the crease
      if (numplanes != 2) {
        //				Con_Printf ("clip velocity, numplanes ==
        //%i\n",numplanes);
        VectorCopy(vec3_origin, ent->v.velocity);
        return 7;
      }
      CrossProduct(planes[0], planes[1], dir);
      d = DotProduct(dir, ent->v.velocity);
      VectorScale(dir, d, ent->v.velocity);
    }

    //
    // if original velocity is against the original velocity, stop dead
    // to avoid tiny occilations in sloping corners
    //
    if (DotProduct(ent->v.velocity, primal_velocity) <= 0) {
      VectorCopy(vec3_origin, ent->v.velocity);
      return blocked;
    }
  }

  return blocked;
}

/*
============
SV_AddGravity

============
*/
void SV_AddGravity(edict_t *ent) {
  float ent_gravity;
  eval_t *val;

  val = GetEdictFieldValueByName(ent, "gravity");
  if (val && val->_float)
    ent_gravity = val->_float;
  else
    ent_gravity = 1.0;

  ent->v.velocity[2] -= ent_gravity * sv_gravity.value * host_frametime;
}

/*
===============================================================================

PUSHMOVE

===============================================================================
*/

/*
============
SV_PushEntity

Does not change the entities velocity at all
============
*/
trace_t SV_PushEntity(edict_t *ent, vec3_t push) {
  trace_t trace;
  vec3_t end;

  VectorAdd(ent->v.origin, push, end);

  if (ent->v.movetype == MOVETYPE_FLYMISSILE)
    trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_MISSILE,
                    ent);
  else if (ent->v.solid == SOLID_TRIGGER || ent->v.solid == SOLID_NOT)
    // only clip against bmodels
    trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end,
                    MOVE_NOMONSTERS, ent);
  else
    trace =
        SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent);

  VectorCopy(trace.endpos, ent->v.origin);
  SV_LinkEdict(ent, true);

  if (trace.ent)
    SV_Impact(ent, trace.ent);

  return trace;
}

/*
============
SV_PushMove
============
*/
cvar_t sv_gameplayfix_elevators = {
    "sv_gameplayfix_elevators", "2",
    CVAR_ARCHIVE}; // 0=off; 1=clients only; 2=all entities
void SV_PushMove(edict_t *pusher, float movetime) {
  int i, e;
  edict_t *check, *block;
  vec3_t mins, maxs, move;
  vec3_t entorig, pushorig;
  int num_moved;
  edict_t **moved_edict; // johnfitz -- dynamically allocate
  vec3_t *moved_from;    // johnfitz -- dynamically allocate
  int mark;              // johnfitz

  if (!pusher->v.velocity[0] && !pusher->v.velocity[1] &&
      !pusher->v.velocity[2]) {
    pusher->v.ltime += movetime;
    return;
  }

  for (i = 0; i < 3; i++) {
    move[i] = pusher->v.velocity[i] * movetime;
    mins[i] = pusher->v.absmin[i] + move[i];
    maxs[i] = pusher->v.absmax[i] + move[i];
  }

  VectorCopy(pusher->v.origin, pushorig);

  // move the pusher to it's final position

  VectorAdd(pusher->v.origin, move, pusher->v.origin);
  pusher->v.ltime += movetime;
  SV_LinkEdict(pusher, false);

  // johnfitz -- dynamically allocate
  mark = Hunk_LowMark();
  moved_edict = (edict_t **)Hunk_Alloc(qcvm->num_edicts * sizeof(edict_t *));
  moved_from = (vec3_t *)Hunk_Alloc(qcvm->num_edicts * sizeof(vec3_t));
  // johnfitz

  // see if any solid entities are inside the final position
  num_moved = 0;
  check = NEXT_EDICT(qcvm->edicts);
  for (e = 1; e < qcvm->num_edicts; e++, check = NEXT_EDICT(check)) {
    qboolean riding;
    if (check->free)
      continue;
    if (check->v.movetype == MOVETYPE_PUSH ||
        check->v.movetype == MOVETYPE_NONE ||
        check->v.movetype == MOVETYPE_NOCLIP)
      continue;

    // if the entity is standing on the pusher, it will definately be moved
    if (!(((int)check->v.flags & FL_ONGROUND) &&
          PROG_TO_EDICT(check->v.groundentity) == pusher)) {
      if (check->v.absmin[0] >= maxs[0] || check->v.absmin[1] >= maxs[1] ||
          check->v.absmin[2] >= maxs[2] || check->v.absmax[0] <= mins[0] ||
          check->v.absmax[1] <= mins[1] || check->v.absmax[2] <= mins[2])
        continue;

      // see if the ent's bbox is inside the pusher's final position
      if (!SV_TestEntityPosition(check))
        continue;

      riding = false;
    } else
      riding = true;

    // remove the onground flag for non-players
    if (check->v.movetype != MOVETYPE_WALK)
      check->v.flags = (int)check->v.flags & ~FL_ONGROUND;

    VectorCopy(check->v.origin, entorig);
    VectorCopy(check->v.origin, moved_from[num_moved]);
    moved_edict[num_moved] = check;
    num_moved++;

    // try moving the contacted entity
    pusher->v.solid = SOLID_NOT;
    SV_PushEntity(check, move);
    pusher->v.solid = SOLID_BSP;

    // if it is still inside the pusher, block
    block = SV_TestEntityPosition(check);
    if (block) { // fail the move
      if (check->v.mins[0] == check->v.maxs[0])
        continue;
      if (check->v.solid == SOLID_NOT ||
          check->v.solid == SOLID_TRIGGER) { // corpse
        check->v.mins[0] = check->v.mins[1] = 0;
        VectorCopy(check->v.mins, check->v.maxs);
        continue;
      }

      // try moving the entity up a bit if it's blocked by the pusher while also
      // standing on it
      if (riding && block == pusher &&
          (sv_gameplayfix_elevators.value >= 2.f ||
           (sv_gameplayfix_elevators.value && e <= svs.maxclients))) {
        check->v.origin[2] += DIST_EPSILON;
        if (!SV_TestEntityPosition(check))
          continue;
      }

      VectorCopy(entorig, check->v.origin);
      SV_LinkEdict(check, true);

      VectorCopy(pushorig, pusher->v.origin);
      SV_LinkEdict(pusher, false);
      pusher->v.ltime -= movetime;

      // if the pusher has a "blocked" function, call it
      // otherwise, just stay in place until the obstacle is gone
      if (pusher->v.blocked) {
        pr_global_struct->self = EDICT_TO_PROG(pusher);
        pr_global_struct->other = EDICT_TO_PROG(check);
        PR_ExecuteProgram(pusher->v.blocked);
      }

      // move back any entities we already moved
      for (i = 0; i < num_moved; i++) {
        VectorCopy(moved_from[i], moved_edict[i]->v.origin);
        SV_LinkEdict(moved_edict[i], false);
      }
      Hunk_FreeToLowMark(mark); // johnfitz
      return;
    }
  }

  Hunk_FreeToLowMark(mark); // johnfitz
}

/*
================
SV_Physics_Pusher

================
*/
void SV_Physics_Pusher(edict_t *ent) {
  float thinktime;
  float oldltime;
  float movetime;

  oldltime = ent->v.ltime;

  thinktime = ent->v.nextthink;
  if (thinktime < ent->v.ltime + host_frametime) {
    movetime = thinktime - ent->v.ltime;
    if (movetime < 0)
      movetime = 0;
  } else
    movetime = host_frametime;

  if (movetime) {
    SV_PushMove(ent, movetime); // advances ent->v.ltime if not blocked
  }

  if (thinktime > oldltime && thinktime <= ent->v.ltime) {
    ent->v.nextthink = 0;
    pr_global_struct->time = qcvm->time;
    pr_global_struct->self = EDICT_TO_PROG(ent);
    pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
    PR_ExecuteProgram(ent->v.think);
    if (ent->free)
      return;
  }
}

/*
===============================================================================

CLIENT MOVEMENT

===============================================================================
*/

/*
=============
SV_CheckStuck

This is a big hack to try and fix the rare case of getting stuck in the world
clipping hull.
=============
*/
void SV_CheckStuck(edict_t *ent) {
  int i, j;
  int z;
  vec3_t org;

  if (!SV_TestEntityPosition(ent)) {
    VectorCopy(ent->v.origin, ent->v.oldorigin);
    return;
  }

  VectorCopy(ent->v.origin, org);
  VectorCopy(ent->v.oldorigin, ent->v.origin);
  if (!SV_TestEntityPosition(ent)) {
    Con_DPrintf("Unstuck.\n");
    SV_LinkEdict(ent, true);
    return;
  }

  for (z = 0; z < 18; z++)
    for (i = -1; i <= 1; i++)
      for (j = -1; j <= 1; j++) {
        ent->v.origin[0] = org[0] + i;
        ent->v.origin[1] = org[1] + j;
        ent->v.origin[2] = org[2] + z;
        if (!SV_TestEntityPosition(ent)) {
          Con_DPrintf("Unstuck.\n");
          SV_LinkEdict(ent, true);
          return;
        }
      }

  VectorCopy(org, ent->v.origin);
  Con_DPrintf("player is stuck.\n");
}

/*
=============
SV_CheckWater
=============
*/
qboolean SV_CheckWater(edict_t *ent) {
  vec3_t point;
  int cont;

  point[0] = ent->v.origin[0];
  point[1] = ent->v.origin[1];
  point[2] = ent->v.origin[2] + ent->v.mins[2] + 1;

  ent->v.waterlevel = 0;
  ent->v.watertype = CONTENTS_EMPTY;
  cont = SV_PointContents(point);
  if (cont <= CONTENTS_WATER) {
    ent->v.watertype = cont;
    ent->v.waterlevel = 1;
    point[2] = ent->v.origin[2] + (ent->v.mins[2] + ent->v.maxs[2]) * 0.5;
    cont = SV_PointContents(point);
    if (cont <= CONTENTS_WATER) {
      ent->v.waterlevel = 2;
      point[2] = ent->v.origin[2] + ent->v.view_ofs[2];
      cont = SV_PointContents(point);
      if (cont <= CONTENTS_WATER)
        ent->v.waterlevel = 3;
    }
  }

  return ent->v.waterlevel > 1;
}

/*
============
SV_WallFriction

============
*/
void SV_WallFriction(edict_t *ent, trace_t *trace) {
  vec3_t forward, right, up;
  float d, i;
  vec3_t into, side;

  AngleVectors(ent->v.v_angle, forward, right, up);
  d = DotProduct(trace->plane.normal, forward);

  d += 0.5;
  if (d >= 0)
    return;

  // cut the tangential velocity
  i = DotProduct(trace->plane.normal, ent->v.velocity);
  VectorScale(trace->plane.normal, i, into);
  VectorSubtract(ent->v.velocity, into, side);

  ent->v.velocity[0] = side[0] * (1 + d);
  ent->v.velocity[1] = side[1] * (1 + d);
}

/*
=====================
SV_TryUnstick

Player has come to a dead stop, possibly due to the problem with limited
float precision at some angle joins in the BSP hull.

Try fixing by pushing one pixel in each direction.

This is a hack, but in the interest of good gameplay...
======================
*/
int SV_TryUnstick(edict_t *ent, vec3_t oldvel) {
  int i;
  vec3_t oldorg;
  vec3_t dir;
  int clip;
  trace_t steptrace;

  VectorCopy(ent->v.origin, oldorg);
  VectorCopy(vec3_origin, dir);

  for (i = 0; i < 8; i++) {
    // try pushing a little in an axial direction
    switch (i) {
    case 0:
      dir[0] = 2;
      dir[1] = 0;
      break;
    case 1:
      dir[0] = 0;
      dir[1] = 2;
      break;
    case 2:
      dir[0] = -2;
      dir[1] = 0;
      break;
    case 3:
      dir[0] = 0;
      dir[1] = -2;
      break;
    case 4:
      dir[0] = 2;
      dir[1] = 2;
      break;
    case 5:
      dir[0] = -2;
      dir[1] = 2;
      break;
    case 6:
      dir[0] = 2;
      dir[1] = -2;
      break;
    case 7:
      dir[0] = -2;
      dir[1] = -2;
      break;
    }

    SV_PushEntity(ent, dir);

    // retry the original move
    ent->v.velocity[0] = oldvel[0];
    ent->v.velocity[1] = oldvel[1];
    ent->v.velocity[2] = 0;
    clip = SV_FlyMove(ent, 0.1, &steptrace);

    if (fabs(oldorg[1] - ent->v.origin[1]) > 4 ||
        fabs(oldorg[0] - ent->v.origin[0]) > 4) {
      //	Con_DPrintf ("unstuck!\n");
      return clip;
    }

    // go back to the original pos and try again
    VectorCopy(oldorg, ent->v.origin);
  }

  VectorCopy(vec3_origin, ent->v.velocity);
  return 7; // still not moving
}

/*
=====================
SV_WalkMove

Only used by players
======================
*/
#define STEPSIZE 18
void SV_WalkMove(edict_t *ent) {
  vec3_t upmove, downmove;
  vec3_t oldorg, oldvel;
  vec3_t nosteporg, nostepvel;
  int clip;
  int oldonground;
  trace_t steptrace, downtrace;

  //
  // do a regular slide move unless it looks like you ran into a step
  //
  oldonground = (int)ent->v.flags & FL_ONGROUND;
  ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;

  VectorCopy(ent->v.origin, oldorg);
  VectorCopy(ent->v.velocity, oldvel);

  clip = SV_FlyMove(ent, host_frametime, &steptrace);

  if (!(clip & 2))
    return; // move didn't block on a step

  if (!oldonground && ent->v.waterlevel == 0)
    return; // don't stair up while jumping

  if (ent->v.movetype != MOVETYPE_WALK)
    return; // gibbed by a trigger

  if (sv_nostep.value)
    return;

  if ((int)sv_player->v.flags & FL_WATERJUMP)
    return;

  VectorCopy(ent->v.origin, nosteporg);
  VectorCopy(ent->v.velocity, nostepvel);

  //
  // try moving up and forward to go up a step
  //
  VectorCopy(oldorg, ent->v.origin); // back to start pos

  VectorCopy(vec3_origin, upmove);
  VectorCopy(vec3_origin, downmove);
  upmove[2] = STEPSIZE;
  downmove[2] = -STEPSIZE + oldvel[2] * host_frametime;

  // move up
  SV_PushEntity(ent, upmove); // FIXME: don't link?

  // move forward
  ent->v.velocity[0] = oldvel[0];
  ent->v.velocity[1] = oldvel[1];
  ent->v.velocity[2] = 0;
  clip = SV_FlyMove(ent, host_frametime, &steptrace);

  // check for stuckness, possibly due to the limited precision of floats
  // in the clipping hulls
  if (clip) {
    if (fabs(oldorg[1] - ent->v.origin[1]) < 0.03125 &&
        fabs(oldorg[0] - ent->v.origin[0]) <
            0.03125) { // stepping up didn't make any progress
      clip = SV_TryUnstick(ent, oldvel);
    }
  }

  // extra friction based on view angle
  if (clip & 2)
    SV_WallFriction(ent, &steptrace);

  // move down
  downtrace = SV_PushEntity(ent, downmove); // FIXME: don't link?

  if (downtrace.plane.normal[2] > 0.7) {
    if (ent->v.solid == SOLID_BSP) {
      ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
      ent->v.groundentity = EDICT_TO_PROG(downtrace.ent);
    }
  } else {
    // if the push down didn't end up on good ground, use the move without
    // the step up.  This happens near wall / slope combinations, and can
    // cause the player to hop up higher on a slope too steep to climb
    VectorCopy(nosteporg, ent->v.origin);
    VectorCopy(nostepvel, ent->v.velocity);
  }
}

// Replace player origin with hand muzzle position for the duration of
// PlayerPostThink (where QuakeC fires weapons).
//
// For remote VR clients: the client sends its calibrated final muzzle position.
// The server subtracts the QuakeC projectile source offset described by
// vr_weapons.txt so mods with custom weapon source rules do not need separate
// hardcoded multiplayer muzzle corrections.
//
// For the local player (singleplayer / listen-server): uses the same
// calibrated muzzle/controller position directly.
static qboolean SV_VRWeaponSpawnsAtSelfOrigin(edict_t *ent) {
  return VR_WeaponSpawnsAtSelfOrigin(PR_GetString(ent->v.weaponmodel),
                                     (int)ent->v.weapon);
}

static void SV_ApplyVRWeaponOffset(edict_t *ent, int num, qboolean is_remote_vr,
                                   vec3_t restoreOrigin) {
  _VectorCopy(ent->v.origin, restoreOrigin);

  if (is_remote_vr) {
    vec3_t adj;
    vec3_t source_offset;

    _VectorCopy(svs.clients[num - 1].vr_handpos, adj);
    VR_GetWeaponProjectileSourceOffset(PR_GetString(ent->v.weaponmodel),
                                       (int)ent->v.weapon, ent->v.v_angle,
                                       ent->v.view_ofs[2], source_offset);
    VectorSubtract(adj, source_offset, adj);

    _VectorCopy(adj, ent->v.origin);
  } else if (vr_enabled.value && !isDedicated && num == cl.viewentity) {
    vec3_t adj;
    vec3_t fwd, right, up;
    qboolean spawns_at_self_origin = SV_VRWeaponSpawnsAtSelfOrigin(ent);
    VR_GetMuzzleAdjustedHandPos(adj);
    AngleVectors(cl.handrot[1], fwd, right, up);

    _VectorCopy(adj, ent->v.origin);
    if (spawns_at_self_origin) {
      VectorMA(ent->v.origin, 8.0f, fwd, ent->v.origin);
      ent->v.origin[2] += 16.0f - vr_projectilespawn_z_offset.value;
    } else {
      ent->v.origin[2] -= vr_projectilespawn_z_offset.value;
    }
  }
}

static void SV_RestoreVRWeaponOffset(edict_t *ent, int num,
                                     qboolean is_remote_vr,
                                     vec3_t restoreOrigin) {
  if (is_remote_vr ||
      (vr_enabled.value && !isDedicated && num == cl.viewentity)) {
    _VectorCopy(restoreOrigin, ent->v.origin);
  }
}

static qboolean SV_IsVRClientSlot(int num) {
  if (num <= 0 || num > svs.maxclients)
    return false;

  if (svs.clients[num - 1].is_vr_client)
    return true;

  return vr_enabled.value && !isDedicated && num == cl.viewentity;
}

static void SV_AdjustVRJumpVelocity(edict_t *ent, int num,
                                    qboolean was_onground,
                                    float prethink_velocity_z) {
  float target = sv_vr_jump_velocity.value;

  if (target <= SV_VANILLA_JUMP_VELOCITY)
    return;
  if (!was_onground || !ent->v.button2 || prethink_velocity_z > 0)
    return;
  if ((int)ent->v.movetype != MOVETYPE_WALK)
    return;
  if (!SV_IsVRClientSlot(num))
    return;

  if (ent->v.velocity[2] >= SV_VANILLA_JUMP_VELOCITY &&
      ent->v.velocity[2] < target)
    ent->v.velocity[2] = target;
}

static int SV_PMoveTypeForEdict(edict_t *ent) {
  switch ((int)ent->v.movetype) {
  case MOVETYPE_WALK:
    return PM_NORMAL;
  case MOVETYPE_TOSS:
  case MOVETYPE_BOUNCE:
  case MOVETYPE_GIB:
    return PM_DEAD;
  case MOVETYPE_FLY:
    return PM_FLY;
  case MOVETYPE_NOCLIP:
    return PM_SPECTATOR;
  case MOVETYPE_NONE:
  case MOVETYPE_STEP:
  case MOVETYPE_PUSH:
  case MOVETYPE_FLYMISSILE:
  default:
    return PM_NONE;
  }
}

static void SV_PMoveSetWater(edict_t *ent) {
  ent->v.waterlevel = pmove.waterlevel;
  if (pmove.watertype & CONTENTBIT_SOLID)
    ent->v.watertype = CONTENTS_SOLID;
  else if (pmove.watertype & CONTENTBIT_SKY)
    ent->v.watertype = CONTENTS_SKY;
  else if (pmove.watertype & CONTENTBIT_LAVA)
    ent->v.watertype = CONTENTS_LAVA;
  else if (pmove.watertype & CONTENTBIT_SLIME)
    ent->v.watertype = CONTENTS_SLIME;
  else if (pmove.watertype & CONTENTBIT_WATER)
    ent->v.watertype = CONTENTS_WATER;
  else
    ent->v.watertype = CONTENTS_EMPTY;
}

static float SV_PMoveLegacySwimJumpSpeed(int watertype) {
  if (watertype == CONTENTS_WATER)
    return 100.0f;
  if (watertype == CONTENTS_SLIME)
    return 80.0f;
  return 50.0f;
}

static void SV_FilterLegacyPMoveQCVelocityDelta(
    const usercmd_t *cmd, const vec3_t prethink_velocity,
    const vec3_t postthink_velocity, int prethink_flags, int postthink_flags,
    int prethink_waterlevel, int prethink_watertype, int prethink_movetype,
    float prethink_health, float prethink_deadflag, vec3_t out_delta) {
  vec3_t qcbase;
  vec3_t water_delta;
  float speed;
  float zdelta;
  int i;

  VectorSubtract(postthink_velocity, prethink_velocity, out_delta);
  if (!sv_pmove_legacy_preserve_qc_velocity.value) {
    VectorClear(out_delta);
    return;
  }

  VectorCopy(prethink_velocity, qcbase);

  /*
   * Legacy PlayerPreThink runs old NQ movement helpers before mod logic. PMove
   * reproduces those helpers itself, so remove their expected velocity edits
   * and preserve only the remaining QC-authored force, such as a grappling hook.
   */
  if (prethink_health >= 0 && prethink_movetype != MOVETYPE_NOCLIP &&
      prethink_waterlevel >= 2 && !(prethink_flags & FL_WATERJUMP)) {
    VectorScale(qcbase, -0.8f * prethink_waterlevel * host_frametime,
                water_delta);
    VectorAdd(qcbase, water_delta, qcbase);
    VectorSubtract(out_delta, water_delta, out_delta);
  }

  if (!(prethink_flags & FL_WATERJUMP) && (postthink_flags & FL_WATERJUMP)) {
    zdelta = 225.0f - qcbase[2];
    qcbase[2] = 225.0f;
    out_delta[2] -= zdelta;
  } else if ((cmd->buttons & BUTTON_JUMP) && prethink_deadflag < DEAD_DYING &&
             prethink_waterlevel >= 2) {
    speed = SV_PMoveLegacySwimJumpSpeed(prethink_watertype);
    zdelta = speed - qcbase[2];
    qcbase[2] = speed;
    out_delta[2] -= zdelta;
  } else if ((cmd->buttons & BUTTON_JUMP) &&
             prethink_deadflag < DEAD_DYING &&
             (prethink_flags & FL_ONGROUND) &&
             (prethink_flags & FL_JUMPRELEASED) &&
             !(postthink_flags & FL_JUMPRELEASED) &&
             !(prethink_flags & FL_WATERJUMP) &&
             out_delta[2] > SV_VANILLA_JUMP_VELOCITY - 1.0f) {
    out_delta[2] -= SV_VANILLA_JUMP_VELOCITY;
  }

  for (i = 0; i < 3; i++) {
    if (fabs(out_delta[i]) < MOVE_EPSILON)
      out_delta[i] = 0;
  }
}

static void SV_RestoreLegacyPMoveOwnedState(edict_t *ent, int prethink_flags,
                                            int postthink_flags,
                                            float prethink_teleport_time,
                                            float postthink_teleport_time) {
  int flags = (int)ent->v.flags;

  flags &= ~(FL_JUMPRELEASED | FL_WATERJUMP);
  flags |= prethink_flags & FL_JUMPRELEASED;
  if (postthink_flags & FL_WATERJUMP) {
    flags |= FL_WATERJUMP;
    ent->v.teleport_time = postthink_teleport_time;
  } else if ((prethink_flags & FL_WATERJUMP) &&
             prethink_teleport_time > qcvm->time) {
    flags |= FL_WATERJUMP;
    ent->v.teleport_time = prethink_teleport_time;
  } else if (prethink_flags & FL_WATERJUMP) {
    ent->v.teleport_time = 0;
  }
  ent->v.flags = flags;
}

void SV_RunPMoveForEntity(edict_t *ent, const usercmd_t *cmd) {
  static vec3_t extents = {256, 256, 256};
  vec3_t bounds[2];
  eval_t *entgrav;
  eval_t *pmflags;
  unsigned int pmflagbits;
  int i;

  PMSV_UpdateMovevars();
  entgrav = GetEdictFieldValue(ent, qcvm->extfields.gravity);
  if (entgrav && entgrav->_float)
    movevars.entgravity = entgrav->_float;

  pmflags = GetEdictFieldValue(ent, qcvm->extfields.pmove_flags);
  pmflagbits = (pmflags && pmflags->_float) ? (unsigned int)pmflags->_float : 0;

  memset(&pmove, 0, sizeof(pmove));
  VectorCopy(ent->v.mins, pmove.player_mins);
  VectorCopy(ent->v.maxs, pmove.player_maxs);
  VectorCopy(ent->v.oldorigin, pmove.safeorigin);
  pmove.safeorigin_known = true;
  VectorCopy(ent->v.origin, pmove.origin);
  VectorCopy(ent->v.velocity, pmove.velocity);
  VectorClear(pmove.gravitydir);
  pmove.waterjumptime =
      (ent->v.teleport_time > qcvm->time) ? ent->v.teleport_time - qcvm->time : 0;
  pmove.jump_held = pmflags ? !!(pmflagbits & PMF_JUMP_HELD) :
      !((int)ent->v.flags & FL_JUMPRELEASED);
  pmove.onladder = !!(pmflagbits & PMF_LADDER);
  pmove.jump_secs = 0;
  pmove.onground = !!((int)ent->v.flags & FL_ONGROUND);
  pmove.pm_type = SV_PMoveTypeForEdict(ent);
  if (cmd)
    pmove.cmd = *cmd;

  VectorSubtract(ent->v.absmin, extents, bounds[0]);
  VectorAdd(ent->v.absmax, extents, bounds[1]);
  World_AddEntsToPmove(ent, bounds);

  PM_PlayerMove(1);

  VectorCopy(pmove.safeorigin, ent->v.oldorigin);
  VectorCopy(pmove.origin, ent->v.origin);
  VectorCopy(pmove.velocity, ent->v.velocity);
  ent->v.teleport_time =
      (pmove.waterjumptime > 0) ? qcvm->time + pmove.waterjumptime : 0;

  if (pmove.jump_held)
    ent->v.flags = (int)ent->v.flags & ~FL_JUMPRELEASED;
  else
    ent->v.flags = (int)ent->v.flags | FL_JUMPRELEASED;

  if (pmflags) {
    pmflagbits &= ~(PMF_JUMP_HELD | PMF_LADDER);
    if (pmove.jump_held)
      pmflagbits |= PMF_JUMP_HELD;
    if (pmove.onladder)
      pmflagbits |= PMF_LADDER;
    pmflags->_float = pmflagbits;
  }

  if (pmove.onground) {
    int ground = pmove.physents[pmove.groundent].info;
    ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
    ent->v.groundentity = (ground < 0) ? 0 : EDICT_TO_PROG(EDICT_NUM(ground));
  } else {
    ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
    ent->v.groundentity = 0;
  }

  SV_PMoveSetWater(ent);
  SV_LinkEdict(ent, true);

  for (i = 0; i < pmove.numtouch && !ent->free; i++) {
    int n = pmove.physents[pmove.touchindex[i]].info;
    if (n < 0 || n >= qcvm->num_edicts)
      continue;
    SV_Impact(ent, EDICT_NUM(n));
  }
}

static void SV_SetQCInputGlobals(const usercmd_t *cmd) {
  if (qcvm->extglobals.input_sequence)
    *qcvm->extglobals.input_sequence = cmd->sequence;
  if (qcvm->extglobals.input_servertime)
    *qcvm->extglobals.input_servertime = cmd->servertime;
  if (qcvm->extglobals.input_timelength)
    *qcvm->extglobals.input_timelength = cmd->seconds;
  if (qcvm->extglobals.input_movevalues) {
    qcvm->extglobals.input_movevalues[0] = cmd->forwardmove;
    qcvm->extglobals.input_movevalues[1] = cmd->sidemove;
    qcvm->extglobals.input_movevalues[2] = cmd->upmove;
  }
  if (qcvm->extglobals.input_angles)
    VectorCopy(cmd->viewangles, qcvm->extglobals.input_angles);
  if (qcvm->extglobals.input_buttons)
    *qcvm->extglobals.input_buttons = cmd->buttons;
  if (qcvm->extglobals.input_impulse)
    *qcvm->extglobals.input_impulse = cmd->impulse;
  if (qcvm->extglobals.input_weapon)
    *qcvm->extglobals.input_weapon = cmd->weapon;
  if (qcvm->extglobals.input_cursor_screen) {
    qcvm->extglobals.input_cursor_screen[0] = cmd->cursor_screen[0];
    qcvm->extglobals.input_cursor_screen[1] = cmd->cursor_screen[1];
  }
  if (qcvm->extglobals.input_cursor_trace_start)
    VectorCopy(cmd->cursor_start, qcvm->extglobals.input_cursor_trace_start);
  if (qcvm->extglobals.input_cursor_trace_endpos)
    VectorCopy(cmd->cursor_impact, qcvm->extglobals.input_cursor_trace_endpos);
  if (qcvm->extglobals.input_cursor_entitynumber)
    *qcvm->extglobals.input_cursor_entitynumber = cmd->cursor_entitynumber;
}

qboolean SV_RunClientPMoveCommands(client_t *client) {
  client_t *saved_host_client;
  edict_t *saved_sv_player;
  edict_t *ent;
  usercmd_t queuedcmd;
  double saved_host_frametime = host_frametime;
  int num;
  int processed = 0;
  qboolean is_remote_vr;

  if (!client || !client->active || !client->edict || client->edict->free)
    return false;

  num = (int)(client - svs.clients) + 1;
  if (num < 1 || num > svs.maxclients)
    return false;

  ent = client->edict;
  is_remote_vr = client->is_vr_client && (isDedicated || num != cl.viewentity);
  saved_host_client = host_client;
  saved_sv_player = sv_player;
  host_client = client;
  sv_player = ent;

  while (!ent->free && SV_PopQueuedUsercmd(client, &queuedcmd)) {
    vec3_t thinkRestoreOrigin;
    vec3_t restoreOrigin;
    vec3_t prethink_velocity;
    vec3_t postthink_velocity;
    vec3_t preserved_velocity_delta;
    int prethink_flags;
    int postthink_flags;
    int prethink_waterlevel;
    int prethink_watertype;
    int prethink_movetype;
    float prethink_health;
    float prethink_deadflag;
    float prethink_teleport_time;
    float postthink_teleport_time;
    qboolean think_ok;
    coop_respawn_postthink_state_t coop_respawn_state;

    SV_ApplyQueuedUsercmd(client, &queuedcmd);
    host_frametime = CLAMP(0.001f, client->cmd.seconds, 0.1f);
    pr_global_struct->frametime = host_frametime;
    SV_CoopRespawnBeginPostThink(ent, num, &coop_respawn_state);
    SV_SetQCInputGlobals(&client->cmd);

    if (client->is_vr_client && VectorLength(client->vr_roomscalemove) > 0) {
      VectorAdd(ent->v.origin, client->vr_roomscalemove, ent->v.origin);
      VectorCopy(vec3_origin, client->vr_roomscalemove);
      VectorCopy(vec3_origin, client->vr_roomscale_accum);
      SV_LinkEdict(ent, true);
    }

    VectorCopy(ent->v.velocity, prethink_velocity);
    prethink_flags = (int)ent->v.flags;
    prethink_waterlevel = (int)ent->v.waterlevel;
    prethink_watertype = (int)ent->v.watertype;
    prethink_movetype = (int)ent->v.movetype;
    prethink_health = ent->v.health;
    prethink_deadflag = ent->v.deadflag;
    prethink_teleport_time = ent->v.teleport_time;

    pr_global_struct->time = qcvm->time;
    pr_global_struct->self = EDICT_TO_PROG(ent);
    PR_ExecuteProgram(pr_global_struct->PlayerPreThink);
    if (ent->free) {
      SV_CoopRespawnRestoreSuppressedInput(ent, num, &coop_respawn_state);
      break;
    }
    if (!qcvm->extfuncs.SV_RunClientCommand) {
      VectorCopy(ent->v.velocity, postthink_velocity);
      postthink_flags = (int)ent->v.flags;
      postthink_teleport_time = ent->v.teleport_time;
      SV_FilterLegacyPMoveQCVelocityDelta(
          &client->cmd, prethink_velocity, postthink_velocity, prethink_flags,
          postthink_flags, prethink_waterlevel, prethink_watertype,
          prethink_movetype, prethink_health, prethink_deadflag,
          preserved_velocity_delta);
      VectorAdd(prethink_velocity, preserved_velocity_delta, ent->v.velocity);
      SV_RestoreLegacyPMoveOwnedState(ent, prethink_flags, postthink_flags,
                                      prethink_teleport_time,
                                      postthink_teleport_time);
    }
    SV_CheckVelocity(ent);

    SV_ApplyVRWeaponOffset(ent, num, is_remote_vr, thinkRestoreOrigin);
    think_ok = SV_RunThink(ent);
    SV_RestoreVRWeaponOffset(ent, num, is_remote_vr, thinkRestoreOrigin);
    if (!think_ok || ent->free) {
      SV_CoopRespawnRestoreSuppressedInput(ent, num, &coop_respawn_state);
      break;
    }

    if (qcvm->extfuncs.SV_RunClientCommand) {
      pr_global_struct->self = EDICT_TO_PROG(ent);
      PR_ExecuteProgram(qcvm->extfuncs.SV_RunClientCommand);
      SV_LinkEdict(ent, true);
    } else {
      SV_RunPMoveForEntity(ent, &client->cmd);
    }
    if (ent->free) {
      SV_CoopRespawnRestoreSuppressedInput(ent, num, &coop_respawn_state);
      break;
    }

    SV_ApplyTrustedClientMove(client);
    SV_LinkEdict(ent, true);

    pr_global_struct->time = qcvm->time;
    SV_ApplyVRWeaponOffset(ent, num, is_remote_vr, restoreOrigin);
    pr_global_struct->self = EDICT_TO_PROG(ent);
    SV_CoopReviveBeginPostThink(ent);
    SV_FriendlyFireBegin(ent);
    PR_ExecuteProgram(pr_global_struct->PlayerPostThink);
    SV_FriendlyFireEnd();
    SV_CoopReviveEndPostThink();
    SV_RestoreVRWeaponOffset(ent, num, is_remote_vr, restoreOrigin);
    SV_CoopRespawnEndPostThink(ent, num, &coop_respawn_state);
    SV_CoopReviveApplyPending();

    SV_FinishQueuedUsercmd(client);
    processed++;
  }

  host_frametime = saved_host_frametime;
  pr_global_struct->frametime = host_frametime;
  host_client = saved_host_client;
  sv_player = saved_sv_player;

  if (!processed)
    client->cmd.seconds = 0;

  return processed > 0;
}

/*
================
SV_Physics_Client

Player character actions
================
*/
void SV_Physics_Client(edict_t *ent, int num) {
  qboolean was_onground;
  float prethink_velocity_z;
  coop_respawn_postthink_state_t coop_respawn_state;

  if (!svs.clients[num - 1].active)
    return; // unconnected slot

  // Exclude the local player: on a listen server / singleplayer, the local
  // player's vr_handpos arrives one frame late through loopback.  Using
  // cl.handpos[1] directly (in the else-if fallback) matches the original
  // master branch behaviour exactly.
  qboolean is_remote_vr =
      (num > 0 && num <= svs.maxclients && svs.clients[num - 1].is_vr_client &&
       (isDedicated || num != cl.viewentity));

  if (svs.clients[num - 1].usingpmove) {
    SV_RunClientPMoveCommands(&svs.clients[num - 1]);
    return;
  }

  // Apply roomscale displacement for remote clients
  if (is_remote_vr &&
      VectorLength(svs.clients[num - 1].vr_roomscale_accum) > 0) {
    VectorAdd(ent->v.origin, svs.clients[num - 1].vr_roomscale_accum,
              ent->v.origin);
    VectorCopy(vec3_origin, svs.clients[num - 1].vr_roomscale_accum);
    SV_LinkEdict(ent, true);
  }

  was_onground = ((int)ent->v.flags & FL_ONGROUND) != 0;
  prethink_velocity_z = ent->v.velocity[2];
  SV_CoopRespawnBeginPostThink(ent, num, &coop_respawn_state);

  //
  // call standard client pre-think
  //
  pr_global_struct->time = qcvm->time;
  pr_global_struct->self = EDICT_TO_PROG(ent);
  PR_ExecuteProgram(pr_global_struct->PlayerPreThink);
  if (ent->free) {
    SV_CoopRespawnRestoreSuppressedInput(ent, num, &coop_respawn_state);
    return;
  }
  SV_AdjustVRJumpVelocity(ent, num, was_onground, prethink_velocity_z);

  //
  // do a move
  //
  SV_CheckVelocity(ent);

  //
  // decide which move function to call
  //
  // SV_RunThink executes the entity's think function, which for players
  // includes weapon animation frames (e.g. nailgun's player_nail1/nail2).
  // These think functions can fire projectiles using self.origin, so we
  // must set origin to the hand position during SV_RunThink — not just
  // during PostThink.  We restore the body origin before movement physics
  // (SV_WalkMove, etc.) which needs the real collision hull position.
  //
  {
    vec3_t thinkRestoreOrigin;
    SV_ApplyVRWeaponOffset(ent, num, is_remote_vr, thinkRestoreOrigin);

    qboolean think_ok = true;
    switch ((int)ent->v.movetype) {
    case MOVETYPE_NONE:
      think_ok = SV_RunThink(ent);
      break;
    case MOVETYPE_WALK:
      think_ok = SV_RunThink(ent);
      break;
    case MOVETYPE_TOSS:
    case MOVETYPE_BOUNCE:
    case MOVETYPE_GIB:
      break; // SV_Physics_Toss handles its own think
    case MOVETYPE_FLY:
      think_ok = SV_RunThink(ent);
      break;
    case MOVETYPE_NOCLIP:
      think_ok = SV_RunThink(ent);
      break;
    default:
      Sys_Error("SV_Physics_client: bad movetype %i", (int)ent->v.movetype);
    }

    SV_RestoreVRWeaponOffset(ent, num, is_remote_vr, thinkRestoreOrigin);

    if (!think_ok) {
      SV_CoopRespawnRestoreSuppressedInput(ent, num, &coop_respawn_state);
      return;
    }
  }

  // Movement physics — uses body origin for collision detection
  switch ((int)ent->v.movetype) {
  case MOVETYPE_NONE:
    break;

  case MOVETYPE_WALK:
    if (!SV_CheckWater(ent) && !((int)ent->v.flags & FL_WATERJUMP))
      SV_AddGravity(ent);
    SV_CheckStuck(ent);
    SV_WalkMove(ent);
    break;

  case MOVETYPE_TOSS:
  case MOVETYPE_BOUNCE:
  case MOVETYPE_GIB:
    SV_Physics_Toss(ent);
    break;

  case MOVETYPE_FLY:
    SV_FlyMove(ent, host_frametime, NULL);
    break;

  case MOVETYPE_NOCLIP:
    VectorMA(ent->v.origin, host_frametime, ent->v.velocity, ent->v.origin);
    break;

  default:
    break;
  }

  if (num == cl.viewentity && vr_enabled.value) {
    vec3_t restoreVel;
    _VectorCopy(ent->v.velocity, restoreVel);
    extern vec3_t vr_room_scale_move;
    VectorScale(vr_room_scale_move, 1.0f / host_frametime, ent->v.velocity);

    switch ((int)ent->v.movetype) {
    case MOVETYPE_NONE:
      break;

    case MOVETYPE_WALK:
      ent->v.velocity[2] = -1.0f;
      SV_CheckStuck(ent);
      SV_WalkMove(ent);

      break;

    case MOVETYPE_TOSS:
    case MOVETYPE_BOUNCE:
      break;

    case MOVETYPE_FLY:
      SV_FlyMove(ent, host_frametime, NULL);
      break;

    case MOVETYPE_NOCLIP:
      VectorMA(ent->v.origin, host_frametime, ent->v.velocity, ent->v.origin);
      break;

    default:
      Sys_Error("SV_Physics_client: bad movetype %i", (int)ent->v.movetype);
    }

    _VectorCopy(restoreVel, ent->v.velocity);
  }

  //
  // call standard player post-think
  //
  SV_ApplyTrustedClientMove(&svs.clients[num - 1]);
  SV_LinkEdict(ent, true);

  pr_global_struct->time = qcvm->time;

  // replace player origin with hand origin for duration of post think (where
  // weapons are done)
  vec3_t restoreOrigin;
  SV_ApplyVRWeaponOffset(ent, num, is_remote_vr, restoreOrigin);

  pr_global_struct->self = EDICT_TO_PROG(ent);

  SV_CoopReviveBeginPostThink(ent);
  SV_FriendlyFireBegin(ent);
  PR_ExecuteProgram(pr_global_struct->PlayerPostThink);
  SV_FriendlyFireEnd();
  SV_CoopReviveEndPostThink();

  SV_RestoreVRWeaponOffset(ent, num, is_remote_vr, restoreOrigin);
  SV_CoopRespawnEndPostThink(ent, num, &coop_respawn_state);
  SV_CoopReviveApplyPending();
}

//============================================================================

/*
=============
SV_Physics_None

Non moving objects can only think
=============
*/
void SV_Physics_None(edict_t *ent) {
  // regular thinking
  SV_RunThink(ent);
}

/*
=============
SV_Physics_Noclip

A moving object that doesn't obey physics
=============
*/
void SV_Physics_Noclip(edict_t *ent) {
  // regular thinking
  if (!SV_RunThink(ent))
    return;

  VectorMA(ent->v.angles, host_frametime, ent->v.avelocity, ent->v.angles);
  VectorMA(ent->v.origin, host_frametime, ent->v.velocity, ent->v.origin);

  SV_LinkEdict(ent, false);
}

/*
==============================================================================

TOSS / BOUNCE

==============================================================================
*/

/*
=============
SV_CheckWaterTransition

=============
*/
void SV_CheckWaterTransition(edict_t *ent) {
  int cont;

  cont = SV_PointContents(ent->v.origin);

  if (!ent->v.watertype) { // just spawned here
    ent->v.watertype = cont;
    ent->v.waterlevel = 1;
    return;
  }

  if (cont <= CONTENTS_WATER) {
    if (ent->v.watertype == CONTENTS_EMPTY) { // just crossed into water
      SV_StartSound(ent, 0, "misc/h2ohit1.wav", 255, 1);
    }
    ent->v.watertype = cont;
    ent->v.waterlevel = 1;
  } else {
    if (ent->v.watertype != CONTENTS_EMPTY) { // just crossed into water
      SV_StartSound(ent, 0, "misc/h2ohit1.wav", 255, 1);
    }
    ent->v.watertype = CONTENTS_EMPTY;
    ent->v.waterlevel = cont;
  }
}

/*
=============
SV_Physics_Toss

Toss, bounce, and fly movement.  When onground, do nothing.
=============
*/
void SV_Physics_Toss(edict_t *ent) {
  trace_t trace;
  vec3_t move;
  float backoff;

  // regular thinking
  if (!SV_RunThink(ent))
    return;

  // if onground, return without moving
  if (((int)ent->v.flags & FL_ONGROUND))
    return;

  SV_CheckVelocity(ent);

  // add gravity
  if (ent->v.movetype != MOVETYPE_FLY && ent->v.movetype != MOVETYPE_FLYMISSILE)
    SV_AddGravity(ent);

  // move angles
  VectorMA(ent->v.angles, host_frametime, ent->v.avelocity, ent->v.angles);

  // move origin
  VectorScale(ent->v.velocity, host_frametime, move);
  trace = SV_PushEntity(ent, move);
  if (trace.fraction == 1)
    return;
  if (ent->free)
    return;

  if (ent->v.movetype == MOVETYPE_BOUNCE)
    backoff = 1.5;
  else
    backoff = 1;

  ClipVelocity(ent->v.velocity, trace.plane.normal, ent->v.velocity, backoff);

  // stop if on ground
  if (trace.plane.normal[2] > 0.7) {
    if (ent->v.velocity[2] < 60 || ent->v.movetype != MOVETYPE_BOUNCE) {
      ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
      ent->v.groundentity = EDICT_TO_PROG(trace.ent);
      VectorCopy(vec3_origin, ent->v.velocity);
      VectorCopy(vec3_origin, ent->v.avelocity);
    }
  }

  // check for in water
  SV_CheckWaterTransition(ent);
}

/*
===============================================================================

STEPPING MOVEMENT

===============================================================================
*/

/*
=============
SV_Physics_Step

Monsters freefall when they don't have a ground entity, otherwise
all movement is done with discrete steps.

This is also used for objects that have become still on the ground, but
will fall if the floor is pulled out from under them.
=============
*/
void SV_Physics_Step(edict_t *ent) {
  qboolean hitsound;

  // freefall if not onground
  if (!((int)ent->v.flags & (FL_ONGROUND | FL_FLY | FL_SWIM))) {
    if (ent->v.velocity[2] < sv_gravity.value * -0.1)
      hitsound = true;
    else
      hitsound = false;

    SV_AddGravity(ent);
    SV_CheckVelocity(ent);
    SV_FlyMove(ent, host_frametime, NULL);
    SV_LinkEdict(ent, true);

    if ((int)ent->v.flags & FL_ONGROUND) // just hit ground
    {
      if (hitsound)
        SV_StartSound(ent, 0, "demon/dland2.wav", 255, 1);
    }
  }

  // regular thinking
  SV_RunThink(ent);

  SV_CheckWaterTransition(ent);
}

//============================================================================

/*
================
SV_Physics

================
*/
void SV_Physics(void) {
  int i;
  int entity_cap; // For sv_freezenonclients
  edict_t *ent;

  // let the progs know that a new frame has started
  pr_global_struct->self = EDICT_TO_PROG(qcvm->edicts);
  pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
  pr_global_struct->time = qcvm->time;
  PR_ExecuteProgram(pr_global_struct->StartFrame);

  // SV_CheckAllEnts ();

  //
  // treat each object in turn
  //
  ent = qcvm->edicts;

  if (sv_freezenonclients.value)
    entity_cap =
        svs.maxclients + 1; // Only run physics on clients and the world
  else
    entity_cap = qcvm->num_edicts;

  // for (i=0 ; i<sv.num_edicts ; i++, ent = NEXT_EDICT(ent))
  for (i = 0; i < entity_cap; i++, ent = NEXT_EDICT(ent)) {
    if (ent->free)
      continue;

    if (pr_global_struct->force_retouch) {
      SV_LinkEdict(ent, true); // force retouch even for stationary
    }

    if (i > 0 && i <= svs.maxclients)
      SV_Physics_Client(ent, i);
    else if (ent->v.movetype == MOVETYPE_PUSH)
      SV_Physics_Pusher(ent);
    else if (ent->v.movetype == MOVETYPE_NONE)
      SV_Physics_None(ent);
    else if (ent->v.movetype == MOVETYPE_NOCLIP)
      SV_Physics_Noclip(ent);
    else if (ent->v.movetype == MOVETYPE_STEP)
      SV_Physics_Step(ent);
    else if (ent->v.movetype == MOVETYPE_TOSS ||
             ent->v.movetype == MOVETYPE_GIB ||
             ent->v.movetype == MOVETYPE_BOUNCE ||
             ent->v.movetype == MOVETYPE_FLY ||
             ent->v.movetype == MOVETYPE_FLYMISSILE)
      SV_Physics_Toss(ent);
    else
      Sys_Error("SV_Physics: bad movetype %i", (int)ent->v.movetype);

    // johnfitz -- PROTOCOL_FITZQUAKE
    // capture interval to nextthink here and send it to client for better
    // lerp timing, but only if interval is not 0.1 (which client assumes)
    ent->sendinterval = false;
    if (!ent->free && ent->v.nextthink > qcvm->time &&
        (ent->v.movetype == MOVETYPE_STEP || ent->v.movetype == MOVETYPE_WALK ||
         ent->v.frame != ent->oldframe)) {
      int j = Q_rint((ent->v.nextthink - ent->oldthinktime) * 255);
      if (j >= 0 && j < 256 && j != 25 &&
          j != 26) // 25 and 26 are close enough to 0.1 to not send
        ent->sendinterval = true;
    }
    // johnfitz
  }

  if (pr_global_struct->force_retouch)
    pr_global_struct->force_retouch--;

  if (!sv_freezenonclients.value)
    qcvm->time += host_frametime;
}
