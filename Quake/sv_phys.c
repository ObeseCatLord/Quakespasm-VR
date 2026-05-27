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

#define MOVE_EPSILON 0.01

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
  check = NEXT_EDICT(sv.edicts);
  for (e = 1; e < sv.num_edicts; e++, check = NEXT_EDICT(check)) {
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
Coop revive helpers

Dead clients are SOLID_NOT in the stock and AD QuakeC, so melee traces do not
reliably hit trace_ent.  During PlayerPostThink, short player-owned traces are
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

static qboolean SV_CoopReviveIsActiveClient(edict_t *ent) {
  int entnum;

  if (!ent || ent->free)
    return false;

  entnum = NUM_FOR_EDICT(ent);
  return entnum >= 1 && entnum <= svs.maxclients &&
         svs.clients[entnum - 1].active && svs.clients[entnum - 1].spawned;
}

static qboolean SV_CoopReviveIsDeadClient(edict_t *ent) {
  return SV_CoopReviveIsActiveClient(ent) &&
         (ent->v.health <= 0 || ent->v.deadflag >= DEAD_DYING);
}

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

static void SV_CoopReviveRemoveSpawnTeledeath(edict_t *owner) {
  int i;
  edict_t *ent;

  for (i = svs.maxclients + 1; i < sv.num_edicts; i++) {
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
      !SV_CoopReviveIsActiveClient(coop_revive_trace_owner))
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
    if (target == ent || !SV_CoopReviveIsDeadClient(target))
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
  if (!SV_CoopReviveIsActiveClient(attacker) ||
      !SV_CoopReviveIsDeadClient(target))
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
  pr_global_struct->time = sv.time;
  pr_global_struct->self = EDICT_TO_PROG(target);
  pr_global_struct->other = EDICT_TO_PROG(attacker);
  PR_ExecuteProgram(pr_global_struct->PutClientInServer);
  SV_CoopReviveRemoveSpawnTeledeath(target);
  pr_global_struct->force_retouch = old_force_retouch;

  health = sv_coop_revive_health.value;
  if (health < 1)
    health = 1;

  SV_CoopReviveSetOrigin(target, coop_revive_pending_origin);
  VectorCopy(coop_revive_pending_angles, target->v.angles);
  VectorCopy(coop_revive_pending_v_angle, target->v.v_angle);
  VectorClear(target->v.velocity);
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
  if (thinktime <= 0 || thinktime > sv.time + host_frametime)
    return true;

  if (thinktime < sv.time)
    thinktime = sv.time; // don't let things stay in the past.
                         // it is possible to start that way
                         // by a trigger with a local time.

  ent->oldthinktime = thinktime;
  ent->oldframe = ent->v.frame; // johnfitz

  ent->v.nextthink = 0;
  pr_global_struct->time = thinktime;
  pr_global_struct->self = EDICT_TO_PROG(ent);
  pr_global_struct->other = EDICT_TO_PROG(sv.edicts);

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
void SV_Impact(edict_t *e1, edict_t *e2) {
  int old_self, old_other;

  old_self = pr_global_struct->self;
  old_other = pr_global_struct->other;

  pr_global_struct->time = sv.time;
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

  val = GetEdictFieldValue(ent, "gravity");
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
  moved_edict = (edict_t **)Hunk_Alloc(sv.num_edicts * sizeof(edict_t *));
  moved_from = (vec3_t *)Hunk_Alloc(sv.num_edicts * sizeof(vec3_t));
  // johnfitz

  // see if any solid entities are inside the final position
  num_moved = 0;
  check = NEXT_EDICT(sv.edicts);
  for (e = 1; e < sv.num_edicts; e++, check = NEXT_EDICT(check)) {
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
    pr_global_struct->time = sv.time;
    pr_global_struct->self = EDICT_TO_PROG(ent);
    pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
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
// For remote VR clients: the client sends raw handpos plus the barrel-depth
// multiplier (ofs[2]*gunmodelscale) and z_offset.  The server applies the
// forward offset using ent->v.v_angle — the SAME quantized angles that
// QuakeC's makevectors() will use — so the muzzle position and projectile
// direction are always in agreement.  This eliminates the yaw-dependent
// horizontal offset that occurred when the client computed the offset with
// full-precision handrot.
//
// For the local player (singleplayer / listen-server): uses cl.handpos[1]
// directly with the original master-branch formula.
static qboolean SV_VRWeaponSpawnsAtSelfOrigin(edict_t *ent) {
  return (int)ent->v.weapon == IT_GRENADE_LAUNCHER;
}

static void SV_ApplyVRWeaponOffset(edict_t *ent, int num, qboolean is_remote_vr,
                                   vec3_t restoreOrigin) {
  _VectorCopy(ent->v.origin, restoreOrigin);

  if (is_remote_vr) {
    // The client sends raw hand position. Rockets add v_forward*8 and
    // '0 0 16' in QuakeC, while grenades in vanilla/AD-style QC spawn exactly
    // at self.origin. Put grenade self.origin where the rocket would end up.
    vec3_t adj;
    vec3_t fwd, r_dummy, u_dummy;
    qboolean spawns_at_self_origin = SV_VRWeaponSpawnsAtSelfOrigin(ent);

    _VectorCopy(svs.clients[num - 1].vr_handpos, adj);
    AngleVectors(ent->v.v_angle, fwd, r_dummy, u_dummy);

    if (spawns_at_self_origin) {
      VectorMA(adj, 2.0f, fwd, adj);
    } else {
      VectorMA(adj, -6.0f, fwd, adj);
      adj[2] -= 16.0f;
    }

    _VectorCopy(adj, ent->v.origin);
  } else if (vr_enabled.value && !isDedicated && num == cl.viewentity) {
    vec3_t adj;
    vec3_t fwd, right, up;
    qboolean spawns_at_self_origin = SV_VRWeaponSpawnsAtSelfOrigin(ent);
    _VectorCopy(cl.handpos[1], adj);
    AngleVectors(cl.handrot[1], fwd, right, up);

    if (weaponCVarEntry >= 0) {
      vec3_t ofs = {
          vr_weapon_offset[weaponCVarEntry * VARS_PER_WEAPON].value,
          vr_weapon_offset[weaponCVarEntry * VARS_PER_WEAPON + 1].value,
          vr_weapon_offset[weaponCVarEntry * VARS_PER_WEAPON + 2].value +
              vr_gunmodely.value};

      vec3_t fwd2;
      VectorCopy(fwd, fwd2);
      fwd2[0] *= vr_gunmodelscale.value * ofs[2];
      fwd2[1] *= vr_gunmodelscale.value * ofs[2];
      fwd2[2] *= vr_gunmodelscale.value * ofs[2];
      VectorAdd(adj, fwd2, adj);
    }

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

/*
================
SV_Physics_Client

Player character actions
================
*/
void SV_Physics_Client(edict_t *ent, int num) {
  if (!svs.clients[num - 1].active)
    return; // unconnected slot

  // Exclude the local player: on a listen server / singleplayer, the local
  // player's vr_handpos arrives one frame late through loopback.  Using
  // cl.handpos[1] directly (in the else-if fallback) matches the original
  // master branch behaviour exactly.
  qboolean is_remote_vr =
      (num > 0 && num <= svs.maxclients && svs.clients[num - 1].is_vr_client &&
       (isDedicated || num != cl.viewentity));

  // Apply roomscale displacement for remote clients
  if (is_remote_vr &&
      VectorLength(svs.clients[num - 1].vr_roomscale_accum) > 0) {
    VectorAdd(ent->v.origin, svs.clients[num - 1].vr_roomscale_accum,
              ent->v.origin);
    VectorCopy(vec3_origin, svs.clients[num - 1].vr_roomscale_accum);
    SV_LinkEdict(ent, true);
  }

  //
  // call standard client pre-think
  //
  pr_global_struct->time = sv.time;
  pr_global_struct->self = EDICT_TO_PROG(ent);
  PR_ExecuteProgram(pr_global_struct->PlayerPreThink);

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

    if (!think_ok)
      return;
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
  SV_LinkEdict(ent, true);

  pr_global_struct->time = sv.time;

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
  pr_global_struct->self = EDICT_TO_PROG(sv.edicts);
  pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
  pr_global_struct->time = sv.time;
  PR_ExecuteProgram(pr_global_struct->StartFrame);

  // SV_CheckAllEnts ();

  //
  // treat each object in turn
  //
  ent = sv.edicts;

  if (sv_freezenonclients.value)
    entity_cap =
        svs.maxclients + 1; // Only run physics on clients and the world
  else
    entity_cap = sv.num_edicts;

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
    if (!ent->free && ent->v.nextthink > sv.time &&
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
    sv.time += host_frametime;
}
