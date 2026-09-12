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
cvar_t sv_gameplayfix_spawnbeforethinks = {
    "sv_gameplayfix_spawnbeforethinks", "0", CVAR_NONE};
extern cvar_t sv_pmove_legacy_preserve_qc_velocity;

#define MOVE_EPSILON 0.01
#define SV_VANILLA_JUMP_VELOCITY 270.0f

void SV_Physics_Toss(edict_t *ent);
trace_t SV_ClipMoveToEntity(edict_t *ent, vec3_t start, vec3_t mins,
                            vec3_t maxs, vec3_t end);

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
static qboolean ff_protected_clients[MAX_SCOREBOARD];
static int    ff_saved_teamplay;
static qboolean ff_active = false;
static edict_t *ff_saved_edicts;
static int ff_saved_maxclients;

static qboolean SV_FriendlyFireBegin(edict_t *ent) {
  int owner_num;

  if (!sv_nofriendlyfire.value || !coop.value || ff_active)
    return false;

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
      return false; // not player-owned, nothing to protect
  }

  ff_active = true;
  ff_saved_edicts = qcvm->edicts;
  ff_saved_maxclients = svs.maxclients;
  ff_saved_teamplay = pr_global_struct->teamplay;
  pr_global_struct->teamplay = 0;

  for (int i = 1; i <= svs.maxclients; i++) {
    edict_t *cl = EDICT_NUM(i);
    ff_protected_clients[i - 1] = false;
    if (i != owner_num && !cl->free && svs.clients[i - 1].active &&
        cl->v.takedamage != DAMAGE_NO) {
      ff_saved_takedamage[i - 1] = cl->v.takedamage;
      ff_protected_clients[i - 1] = true;
      cl->v.takedamage = DAMAGE_NO;
    }
  }

  return true;
}

static void SV_FriendlyFireEnd(void) {
  if (!ff_active)
    return;

  /* A level change invalidates the saved edict pointers. */
  if (!qcvm || qcvm->edicts != ff_saved_edicts ||
      svs.maxclients != ff_saved_maxclients) {
    ff_active = false;
    ff_saved_edicts = NULL;
    return;
  }

  pr_global_struct->teamplay = ff_saved_teamplay;
  for (int i = 1; i <= svs.maxclients; i++) {
    edict_t *cl = EDICT_NUM(i);
    /* Restore only our temporary shield. The owner was never shielded and
     * may have respawned or left teleport limbo during this callback. Restoring
     * its old DAMAGE_NO here made an otherwise living player invulnerable.
     * Likewise, preserve a teammate's QC change to another damage mode. */
    if (ff_protected_clients[i - 1] && !cl->free &&
        svs.clients[i - 1].active && cl->v.takedamage == DAMAGE_NO)
      cl->v.takedamage = ff_saved_takedamage[i - 1];
  }
  ff_active = false;
  ff_saved_edicts = NULL;
}

static void SV_FriendlyFireReset(void) {
  SV_FriendlyFireEnd();
  ff_active = false;
  ff_saved_edicts = NULL;
  ff_saved_maxclients = 0;
}

/* Physical weapon interaction is a command consumer, not another physics
 * simulation. Keep the real player body and the mod's damage implementation. */
#define VR_CONTACT_HISTORY 24
typedef enum {
  VR_CONTACT_HIT,
  VR_CONTACT_WHIFF,
  VR_CONTACT_PARRIED
} sv_vr_contact_outcome_t;
typedef struct {
  vr_weapon_contact_t contact;
  vec3_t handrot, handpos, akimbo_angles[2];
  qboolean akimbo_active;
  int sequence, buttons;
  float time;
  double received;
  byte impulse;
} sv_vr_contact_sample_t;
typedef struct {
  vr_weapon_contact_t previous;
  int sequence;
  float sample_time;
  vec3_t body;
  float arc[2];
  float peak_speed[2], tier[2];
  qboolean consumed[2];
  qboolean authorized[2];
  int subtype[2], hit_count[2], hit_entities[2][2];
  float recovery_deadline[2];
  qboolean valid;
  unsigned int movement_epoch;
  int button[2];
  byte defensive_valid;
  double defensive_received, defensive_processed;
  qboolean parry_rearm[2];
  double parry_received[2];
  sv_vr_contact_sample_t pending[VR_CONTACT_HISTORY];
  unsigned int count;
} sv_vr_contact_state_t;
static sv_vr_contact_state_t sv_vr_contacts[MAX_SCOREBOARD];
static struct {
  edict_t *player;
  dfunction_t *function;
  trace_t trace;
  qboolean active;
  qboolean force_miss;
  int depth;
  vec3_t target_origin;
  float target_modelindex;
  vec3_t muzzle;
  qboolean has_muzzle;
} sv_vr_contact_call;

static void SV_ClampVRMuzzleToWorld(edict_t *ent, vec3_t muzzle);
#include "vr_melee_qc.h"

qboolean SV_VRContactFindRadius(void) {
  edict_t *target;
  if (!SV_VRMeleeScimitarSwingScope() || qcvm->xstatement != 112997)
    return false;
  target = sv_vr_contact_call.trace.ent;
  G_INT(OFS_RETURN) = !sv_vr_contact_call.force_miss && target && !target->free &&
      target != sv_vr_contact_call.player ? EDICT_TO_PROG(target) : 0;
  return true;
}

qboolean SV_VRContactLoadEntity(int statement) {
  /* Terminate the admitted single-result iterator before dereferencing its
   * possibly removed target. Never write or restore an entity's .chain. */
  return statement == 113032 && SV_VRMeleeScimitarSwingScope();
}

qboolean SV_VRContactBranch(int statement) {
  if (!sv_vr_contact_call.active || statement != 116237 ||
      qcvm->depth != sv_vr_contact_call.depth || !SV_VRMeleeGungnirProgs() ||
      sv_vr_contact_call.function != &qcvm->functions[2654] ||
      qcvm->xfunction != sv_vr_contact_call.function ||
      pr_global_struct->self != EDICT_TO_PROG(sv_vr_contact_call.player))
    return false;
  /* Entry saved these locals but did not initialize them. Set usedAmmo only
   * inside this admitted call; normal VM return restores its previous value.
   * Take the exact no-ammo jump before any inventory writes can occur. */
  G_FLOAT(34042) = 0;
  return true;
}

/* Called before QC locals are restored. Copper's trace helper normalizes its
 * result to the desktop ray length after doing native filtering. That length
 * has no relation to an accepted physical blade sweep. Preserve every native
 * result except that normalization, and only for an unfiltered acquisition. */
void SV_VRContactLeaveFunction(void) {
  prstack_t *caller;
  const trace_t *contact;
  if (!sv_vr_contact_call.active || !sv_vr_contact_call.force_miss)
    return;
  if (SV_VRMeleeCopperLeaveFunction())
    return;
  if (!SV_VRMeleeDwellProgs() || !sv_vr_contact_call.player ||
      qcvm->xfunction != &qcvm->functions[396] || qcvm->xstatement != 13044 ||
      qcvm->depth <= 0 || sv_vr_contact_call.function != &qcvm->functions[438])
    return;
  caller = &qcvm->stack[qcvm->depth - 1];
  if (caller->f != sv_vr_contact_call.function ||
      (caller->s != 14578 && caller->s != 14586) ||
      pr_global_struct->self != EDICT_TO_PROG(sv_vr_contact_call.player) ||
      G_INT(7680) != pr_global_struct->self || G_FLOAT(7681) != 0 ||
      G_FLOAT(7689) != 1)
    return;
  contact = &sv_vr_contact_call.trace;
  if (!contact->ent || contact->ent->free || !isfinite(contact->fraction) ||
      contact->fraction < 0 || contact->fraction >= 1 ||
      !SV_VRMeleeFiniteVector(contact->endpos) ||
      pr_global_struct->trace_startsolid || pr_global_struct->trace_allsolid ||
      pr_global_struct->trace_ent != EDICT_TO_PROG(contact->ent) ||
      pr_global_struct->trace_endpos[0] != contact->endpos[0] ||
      pr_global_struct->trace_endpos[1] != contact->endpos[1] ||
      pr_global_struct->trace_endpos[2] != contact->endpos[2])
    return;
  pr_global_struct->trace_fraction = contact->fraction;
}

static qboolean SV_VRContactStockProgs(void) {
  return SV_VRMeleeStockProgs();
}

int SV_VRContactMode(void) {
  int mode = sv_weapon_collision.value ? VR_WEAPON_CONTACT_CAP_COLLISION : 0;
  if (sv_immersive_melee.value && SV_VRContactProfile() != VR_WEAPON_CONTACT_PROFILE_NONE)
    mode |= VR_WEAPON_CONTACT_CAP_MELEE;
  return mode;
}

int SV_VRContactProfile(void) {
  switch (SV_VRMeleeFamily()) {
  case SV_VR_MELEE_FAMILY_STOCK:
    return SV_VRContactStockProgs() ? VR_WEAPON_CONTACT_PROFILE_STOCK :
        VR_WEAPON_CONTACT_PROFILE_NONE;
  case SV_VR_MELEE_FAMILY_QBJ3: return VR_WEAPON_CONTACT_PROFILE_QBJ3;
  case SV_VR_MELEE_FAMILY_ENYO: return VR_WEAPON_CONTACT_PROFILE_ENYO;
  case SV_VR_MELEE_FAMILY_BONK: return VR_WEAPON_CONTACT_PROFILE_BONK;
  case SV_VR_MELEE_FAMILY_DWELL: return VR_WEAPON_CONTACT_PROFILE_DWELL;
  case SV_VR_MELEE_FAMILY_HONEY: return VR_WEAPON_CONTACT_PROFILE_STOCK;
  case SV_VR_MELEE_FAMILY_AD: return VR_WEAPON_CONTACT_PROFILE_AD;
  case SV_VR_MELEE_FAMILY_COPPER: return VR_WEAPON_CONTACT_PROFILE_COPPER;
  case SV_VR_MELEE_FAMILY_ALK: return VR_WEAPON_CONTACT_PROFILE_ALK;
  case SV_VR_MELEE_FAMILY_IMMORTAL: return VR_WEAPON_CONTACT_PROFILE_IMMORTAL;
  case SV_VR_MELEE_FAMILY_DRAKE: return VR_WEAPON_CONTACT_PROFILE_DRAKE;
  case SV_VR_MELEE_FAMILY_MJOLNIR: return VR_WEAPON_CONTACT_PROFILE_MJOLNIR;
  default: return VR_WEAPON_CONTACT_PROFILE_NONE;
  }
}

static sv_vr_melee_subtype_t SV_VRContactWeapon(edict_t *p) {
  sv_vr_melee_subtype_t subtype;
  if (!(SV_VRContactMode() & VR_WEAPON_CONTACT_CAP_MELEE))
    return SV_VR_MELEE_NONE;
  subtype = SV_VRMeleeWeapon(p);
  if (subtype == SV_VR_MELEE_QBJ3_BERSERK)
    return SV_QBJ3BerserkAkimboSupported() ? subtype : SV_VR_MELEE_NONE;
  if (subtype == SV_VR_MELEE_DWELL_BERSERK)
    return SV_DwellBerserkAkimboSupported() ? subtype : SV_VR_MELEE_NONE;
  return subtype == SV_VR_MELEE_STOCK_AXE || subtype == SV_VR_MELEE_QBJ3_WRENCH ||
      subtype == SV_VR_MELEE_ENYO_KATANA || subtype == SV_VR_MELEE_BONK_HAMMER ||
      subtype == SV_VR_MELEE_DWELL_AXE || subtype == SV_VR_MELEE_HONEY_AXE ||
      subtype == SV_VR_MELEE_AD_AXE || subtype == SV_VR_MELEE_COPPER_AXE ||
      subtype == SV_VR_MELEE_ALK_AXE || subtype == SV_VR_MELEE_IMMORTAL_AXE ||
      subtype == SV_VR_MELEE_IMMORTAL_HAMMER || subtype == SV_VR_MELEE_DRAKE_AXE ||
      subtype == SV_VR_MELEE_MJOLNIR_AXE || SV_VRMeleeNativeTrigger(subtype) ?
      subtype : SV_VR_MELEE_NONE;
}

void SV_VRContactResetClient(client_t *client) {
  int n = (int)(client - svs.clients);
  if (n >= 0 && n < MAX_SCOREBOARD)
    memset(&sv_vr_contacts[n], 0, sizeof(sv_vr_contacts[n]));
}

void SV_VRContactAcceptLegacy(client_t *client, const usercmd_t *cmd) {
  int n = (int)(client - svs.clients);
  sv_vr_contact_state_t *s;
  if (n < 0 || n >= MAX_SCOREBOARD)
    return;
  s = &sv_vr_contacts[n];
  if (!cmd->vr_active || !cmd->vr_handpos_relative || !cmd->vr_contact.flags) {
    SV_VRContactResetClient(client);
    return;
  }
  if (s->count == VR_CONTACT_HISTORY)
    SV_VRContactResetClient(client); /* gap: never join the old/new arcs */
  s->pending[s->count].contact = cmd->vr_contact;
  VectorCopy(cmd->vr_handrot, s->pending[s->count].handrot);
  VectorCopy(cmd->vr_handpos, s->pending[s->count].handpos);
  memcpy(s->pending[s->count].akimbo_angles, cmd->vr_akimbo_angles,
      sizeof(cmd->vr_akimbo_angles));
  s->pending[s->count].akimbo_active = cmd->vr_akimbo_active;
  s->pending[s->count].sequence = cmd->sequence;
  s->pending[s->count].impulse = cmd->impulse;
  s->pending[s->count].buttons = cmd->buttons;
  s->pending[s->count].received = cmd->vr_contact_received;
  s->pending[s->count++].time = cmd->servertime;
}

/* Honey's corpse prelude uses find rather than a trace. Keep the native
 * iterator and the mod's qualification/effects, but only expose the edict
 * physically contacted. Nested death-target queries remain ordinary find. */
qboolean SV_VRContactFindAllows(edict_t *candidate, int field, const char *match) {
  if (!sv_vr_contact_call.active)
    return true;
  if (SV_VRMeleeMjolnirAxeGibFindSite() &&
      field == SV_VRMeleeMjolnirDescriptor()->corpse_field && !strcmp(match, "TRUE"))
    return candidate == sv_vr_contact_call.trace.ent;
  if (SV_VRMeleeADScoped()) {
    const sv_vr_melee_ad_descriptor_t *d = SV_VRMeleeADDescriptor();
    if ((qcvm->xstatement == d->find_first || qcvm->xstatement == d->find_next) &&
        field == d->corpse_field && !strcmp(match, "TRUE"))
      return candidate == sv_vr_contact_call.trace.ent;
  }
  if (!sv_vr_contact_call.active || !SV_VRMeleeHoneyProgs() ||
      sv_vr_contact_call.function != &qcvm->functions[218] ||
      qcvm->xfunction != sv_vr_contact_call.function ||
      (qcvm->xstatement != 4672 && qcvm->xstatement != 4703) ||
      pr_global_struct->self != EDICT_TO_PROG(sv_vr_contact_call.player) ||
      field != 105 || strcmp(match, "ZOMBIE_ONGROUND"))
    return true;
  return candidate == sv_vr_contact_call.trace.ent;
}

qboolean SV_VRContactNormalize(void) {
  prstack_t *caller;
  if (!sv_vr_contact_call.active)
    return false;
  if (SV_VRMeleeMjolnirAxeGibNormalizeSite() &&
      SV_VRMeleeFiniteVector(pr_global_struct->v_forward)) {
    VectorCopy(pr_global_struct->v_forward, G_VECTOR(OFS_RETURN));
    return true;
  }
  if (SV_VRMeleeADScoped()) {
    const sv_vr_melee_ad_descriptor_t *d = SV_VRMeleeADDescriptor();
    if (qcvm->xstatement == d->normalize && sv_vr_contact_call.trace.ent &&
        !sv_vr_contact_call.trace.ent->free &&
        G_INT(d->parm + 12) == EDICT_TO_PROG(sv_vr_contact_call.trace.ent) &&
        SV_VRMeleeFiniteVector(pr_global_struct->v_forward)) {
      /* The original local is consumed only by the desktop facing predicate.
       * Keep native range, upgrade qualification and all gib callbacks. */
      VectorCopy(pr_global_struct->v_forward, G_VECTOR(OFS_RETURN));
      return true;
    }
  }
  if (!sv_vr_contact_call.active || !SV_VRMeleeHoneyProgs() ||
      sv_vr_contact_call.function != &qcvm->functions[218] ||
      qcvm->xfunction != &qcvm->functions[146] ||
      qcvm->xstatement != 1809 || qcvm->depth <= 0 ||
      pr_global_struct->self != EDICT_TO_PROG(sv_vr_contact_call.player))
    return false;
  caller = &qcvm->stack[qcvm->depth - 1];
  if (caller->f != sv_vr_contact_call.function || caller->s != 4685 ||
      !sv_vr_contact_call.trace.ent || sv_vr_contact_call.trace.ent->free ||
      ((int *)qcvm->globals)[4049] != EDICT_TO_PROG(sv_vr_contact_call.trace.ent) ||
      !SV_VRMeleeFiniteVector(pr_global_struct->v_forward))
    return false;
  /* This exact normalized vector is used only by infront's dot test.
   * Verified physical contact replaces the desktop facing cone: downward
   * chops and coincident origins must still work. Retain native global
   * makevectors state for every subsequent blood/death-target callback. */
  VectorCopy(pr_global_struct->v_forward, G_VECTOR(OFS_RETURN));
  return true;
}

/* Only verified immediate acquisition sites are replaced. Damage, blood,
 * sounds, powerups and nested gameplay traces execute in the installed progs.
 * A completed Bonk whiff masks its five fan rays, not its ground/dash checks. */
qboolean SV_VRContactTrace(edict_t *ent, const vec3_t start,
    const vec3_t end, int nomonsters, trace_t *trace) {
  if (SV_VRMeleeScimitarSwingScope() && qcvm->xstatement == 113038) {
    edict_t *target = sv_vr_contact_call.trace.ent;
    /* Native death callbacks may leave QC self changed. Once owned, never
     * fall back to an unrestricted ray because a callback invalidated it. */
    if (sv_vr_contact_call.force_miss || !target || target->free ||
        target->v.modelindex != sv_vr_contact_call.target_modelindex ||
        !VectorCompare(target->v.origin, sv_vr_contact_call.target_origin)) {
      memset(trace, 0, sizeof(*trace));
      trace->fraction = 1;
      trace->inopen = true;
      trace->ent = qcvm->edicts;
      VectorCopy(end, trace->endpos);
    } else {
      *trace = sv_vr_contact_call.trace;
    }
    sv_vr_contact_call.active = false;
    return true;
  }
  if (sv_vr_contact_call.active &&
      (SV_VRMeleeHammerPrimaryTrace(ent, start, end, nomonsters, trace) ||
       SV_VRMeleeRapierAttackTrace(ent, start, end, nomonsters, trace) ||
       SV_VRMeleeDwellTrace(ent, start, end, nomonsters, trace) ||
       SV_VRMeleeDrakeTrace(ent, start, end, nomonsters, trace) ||
       SV_VRMeleeCopperTrace(ent, start, end, nomonsters, trace)))
    return true;
  if (!sv_vr_contact_call.active || ent != sv_vr_contact_call.player ||
      qcvm->xfunction != sv_vr_contact_call.function ||
      nomonsters ||
      pr_global_struct->self != EDICT_TO_PROG(ent))
    return false;
  if (qcvm->xstatement == 116259 && SV_VRMeleeGungnirProgs() &&
      qcvm->depth == sv_vr_contact_call.depth &&
      sv_vr_contact_call.function == &qcvm->functions[2654]) {
    if (sv_vr_contact_call.force_miss) {
      memset(trace, 0, sizeof(*trace));
      trace->fraction = 1;
      trace->inopen = true;
      trace->ent = qcvm->edicts;
      VectorCopy(end, trace->endpos);
    } else {
      *trace = sv_vr_contact_call.trace;
    }
    sv_vr_contact_call.active = false;
    return true;
  }
  if (qcvm->xstatement == 117777 && SV_VRMeleeMjolnirHammerProgs() &&
      qcvm->depth == sv_vr_contact_call.depth &&
      sv_vr_contact_call.function == &qcvm->functions[2682]) {
    *trace = sv_vr_contact_call.trace;
    /* The root's immediately following corpse query is scoped too. Leave
     * ownership alive; no other trace site in its effects is overridden. */
    return true;
  }
  if (SV_VRMeleeImmortalDescriptor() &&
      qcvm->xstatement == SV_VRMeleeImmortalTraceStatement(SV_VRMeleeImmortalSelected(ent))) {
    *trace = sv_vr_contact_call.trace;
    sv_vr_contact_call.active = false;
    return true;
  }
  if (SV_VRMeleeALKDescriptor() &&
      qcvm->xstatement == SV_VRMeleeALKDescriptor()->trace) {
    *trace = sv_vr_contact_call.trace;
    sv_vr_contact_call.active = false;
    return true;
  }
  if (SV_VRMeleeMjolnirDescriptor() &&
      (qcvm->xstatement == SV_VRMeleeMjolnirAxeTraceStatement() ||
       qcvm->xstatement == SV_VRMeleeMjolnirAxeDebugTraceStatement())) {
    if (sv_vr_contact_call.force_miss) {
      memset(trace, 0, sizeof(*trace));
      trace->fraction = 1;
      trace->inopen = true;
      trace->ent = qcvm->edicts;
      VectorCopy(end, trace->endpos);
    } else {
      *trace = sv_vr_contact_call.trace;
    }
    sv_vr_contact_call.active = false;
    return true;
  }
  if (SV_VRMeleeADScoped()) {
    const sv_vr_melee_ad_descriptor_t *d = SV_VRMeleeADDescriptor();
    if (qcvm->xstatement != d->trace && qcvm->xstatement != d->debug_trace)
      return false;
    if (sv_vr_contact_call.force_miss) {
      memset(trace, 0, sizeof(*trace));
      trace->fraction = 1;
      trace->inopen = true;
      trace->ent = qcvm->edicts;
      VectorCopy(end, trace->endpos);
    } else {
      *trace = sv_vr_contact_call.trace;
    }
    sv_vr_contact_call.active = false;
    return true;
  }
  if (SV_VRMeleeHoneyProgs() && qcvm->xstatement == 4712) {
    /* noStoneHit is the pinned W_FireAxe local set after Killed. Read it,
     * not the old corpse: ThrowHead may already have repurposed that edict.
     * A supplemental non-solid contact never permits normal tail damage. */
    if (sv_vr_contact_call.force_miss || qcvm->globals[4144]) {
      memset(trace, 0, sizeof(*trace));
      trace->fraction = 1;
      trace->inopen = true;
      trace->ent = qcvm->edicts;
      VectorCopy(end, trace->endpos);
    } else {
      *trace = sv_vr_contact_call.trace;
    }
    sv_vr_contact_call.active = false;
    return true;
  }
  if (sv_vr_contact_call.force_miss) {
    switch (qcvm->xstatement) {
    case 13531: case 13547: case 13578: case 13616: case 13649:
      break;
    default: return false;
    }
    memset(trace, 0, sizeof(*trace));
    trace->fraction = 1;
    trace->inopen = true;
    trace->ent = qcvm->edicts;
    VectorCopy(end, trace->endpos);
    return true;
  }
  if (qcvm->xstatement != SV_VRMeleeStockTraceStatement())
    return false;
  *trace = sv_vr_contact_call.trace;
  sv_vr_contact_call.active = false;
  return true;
}

static void SV_VRContactFeedback(client_t *client, int hand) {
  char command[48];
  /* Cosmetic feedback cannot overflow/delay a gameplay message. Only this
   * validated contact path produces it; pain/footstep sounds are irrelevant. */
  if (client->message.cursize + (int)sizeof(command) + 2 > client->message.maxsize)
    return;
  q_snprintf(command, sizeof(command), "//vr_weapon_contact_haptic %d\n", hand);
  MSG_WriteByte(&client->message, svc_stufftext);
  MSG_WriteString(&client->message, command);
}

static float SV_VRContactDistance(const vec3_t a, const vec3_t b) {
  vec3_t d;
  VectorSubtract(a, b, d);
  return sqrtf(DotProduct(d, d));
}

/* A short, deliberate physical stroke should be enough to commit an authored
 * melee attack. Keep rest/rearm below activation so slow deliberate motion
 * can accumulate instead of resetting the stroke on every command. */
#define SV_VR_CONTACT_STRIKE_SPEED .1f
#define SV_VR_CONTACT_STRIKE_ARC .01f
#define SV_VR_CONTACT_REST_SPEED .05f
/* Only reject unchanged geometry, not a second rate/world-scale-dependent
 * minimum swing speed. Contact endpoints are transmitted as floats. */
#define SV_VR_CONTACT_MOTION_EPSILON .0001f

/* Physical metres and metres/second, independent of world scale, locomotion
 * or collision retraction. Bonk's original three tiers remain QC-owned.
 * The chosen tier is committed at the first outcome, not upgraded on a
 * second victim reached later in the same swing. */
static float SV_VRContactTier(sv_vr_melee_subtype_t subtype,
    float arc, float peak_speed) {
  if (subtype != SV_VR_MELEE_BONK_HAMMER)
    return 0;
  if (arc >= .25f && peak_speed >= 1.5f)
    return 4;
  if (arc >= .12f && peak_speed >= .85f)
    return 2;
  return .2f;
}

/* Earliest point-sweep entry into a finite blade capsule, not the closest
 * approach time (which could lie behind an intervening world/body hit).
 * The radius covers the bounded spacing between sampled blade points. */
static float SV_VRContactCapsuleFraction(const vec3_t start, const vec3_t end,
    const vec3_t base, const vec3_t tip, float radius) {
  vec3_t ray, axis, offset, nearest;
  double aa, rr, ar, ao, ro, oo, a, b, c, disc, t;
  float first = 2;
  VectorSubtract(end, start, ray);
  VectorSubtract(tip, base, axis);
  VectorSubtract(start, base, offset);
  aa = DotProduct(axis, axis);
  rr = DotProduct(ray, ray);
  ao = DotProduct(axis, offset);
  t = aa > 0 ? CLAMP(0, ao / aa, 1) : 0;
  for (int i = 0; i < 3; i++)
    nearest[i] = base[i] + t * axis[i];
  if (SV_VRContactDistance(start, nearest) <= radius)
    return 0;
  if (rr < 1e-12)
    return first;
  ar = DotProduct(axis, ray);
  ro = DotProduct(ray, offset);
  oo = DotProduct(offset, offset);
  if (aa > 1e-12) {
    a = rr - ar * ar / aa;
    b = ro - ar * ao / aa;
    c = oo - ao * ao / aa - radius * radius;
    disc = b * b - a * c;
    if (a > 1e-12 && disc >= 0) {
      t = (-b - sqrt(disc)) / a;
      if (t >= 0 && t <= 1 && ao + t * ar >= 0 && ao + t * ar <= aa)
        first = t;
    }
  }
  for (int cap = 0; cap < 2; cap++) {
    VectorSubtract(start, cap ? tip : base, offset);
    b = DotProduct(ray, offset);
    c = DotProduct(offset, offset) - radius * radius;
    disc = b * b - rr * c;
    if (disc < 0)
      continue;
    t = (-b - sqrt(disc)) / rr;
    if (t >= 0 && t <= 1 && t < first)
      first = t;
  }
  return first;
}

static qboolean SV_VRContactSampleValid(const vr_weapon_contact_t *c) {
  for (int hand = 0; hand < 2; hand++) {
    if (!(c->flags & (1u << hand)))
      continue;
    if (!isfinite(c->speed[hand]) || c->speed[hand] < 0 ||
        c->speed[hand] > 20 ||
        SV_VRContactDistance(c->grip[hand], vec3_origin) > 96 ||
        SV_VRContactDistance(c->base[hand], c->grip[hand]) > 96 ||
        SV_VRContactDistance(c->tip[hand], c->grip[hand]) > 96)
      return false;
    for (int i = 0; i < 3; i++)
      if (!isfinite(c->grip[hand][i]) || !isfinite(c->base[hand][i]) ||
          !isfinite(c->tip[hand][i]))
        return false;
  }
  return isfinite(c->weapon) &&
      !(c->flags & ~VR_WEAPON_CONTACT_KNOWN_FLAGS) &&
      (c->flags & (VR_WEAPON_CONTACT_LEFT_VALID | VR_WEAPON_CONTACT_RIGHT_VALID));
}

static qboolean SV_VRContactButtonTouch(edict_t *button) {
  const char *classname = PR_GetString(button->v.classname);
  dfunction_t *touch;
  int index = 0, statement = 0;

  /* These constructors install .touch only for physical (not shoot-only)
   * buttons. Invoke that original callback so skill validity, elevator state
   * and native enemy bookkeeping stay QC-owned. Similar names in another VM
   * are not enough to authorize a new interaction. */
  if (!strcmp(classname, "func_button_skill"))
    return SV_VRMeleeQBJ3SkillButtonTouch(button->v.touch);
  if (!strcmp(classname, "func_elvtr_button"))
    return qcvm->crc == 30793 && SV_VRMeleeALKDescriptor() &&
        button->v.touch == 937 &&
        SV_VRMeleeFunctionPin(937, "elvtr_button_touch", 30399, 0, 0, 0, NULL);
  if (strcmp(classname, "func_button"))
    return false;
  touch = ED_FindFunction("button_touch");
  if (touch && !touch->numparms && touch->first_statement > 0 &&
      button->v.touch == touch - qcvm->functions)
    return true;
  /* AD-derived buttons use a different callback name. These exact original
   * callbacks were checked to require a real FL_CLIENT other and to retain
   * their mod's activation/state rules. Never call an arbitrary .use here.
   * The caller still excludes shoot-only buttons: these callbacks themselves
   * do not reject a positive-health button. */
  switch (qcvm->crc) {
  case 10963: index = 1790; statement = 85767; break; /* AD and derivatives */
  case 10710: index = 1514; statement = 69393; break; /* Ravenkeep */
  case 43865: index = 3413; statement = 142718; break; /* Mjolnir */
  default: return false;
  }
  if (!qcvm->progs || index >= qcvm->progs->numfunctions ||
      button->v.touch != index)
    return false;
  touch = &qcvm->functions[index];
  return !touch->numparms && touch->first_statement == statement &&
      !strcmp(PR_GetString(touch->s_name), "func_button_touch");
}

/* Consume only the original W_Attack -> W_FireGungnir -> Attack1 call.
 * Do not launch a second firing loop, or schedule/cancel animation thinks. */
qboolean SV_VRContactCall(int function) {
  client_t *client;
  edict_t *player;
  const usercmd_t *cmd;
  const vr_weapon_contact_t *pose;
  sv_vr_melee_qc_context_t saved;
  eval_t *ammo;
  int self, slot, statement;
  if (sv_vr_contact_call.active && qcvm == &sv.qcvm &&
      qcvm->xfunction == sv_vr_contact_call.function &&
      qcvm->depth == sv_vr_contact_call.depth &&
      pr_global_struct->self == EDICT_TO_PROG(sv_vr_contact_call.player)) {
    if (SV_VRMeleeMaceSwing1Call(function) || SV_VRMeleeRapierAnimationCall(function)) {
      float parameters[OFS_PARM7 + 3 - OFS_RETURN];
      int argc = qcvm->argc;
      statement = qcvm->xstatement;
      memcpy(parameters, qcvm->globals + OFS_RETURN, sizeof(parameters));
      if (SV_VRMeleeRapierAnimationCall(function)) {
        qcvm->argc = 0;
        PR_ExecuteProgram(SV_VRMeleeRapierFirstSwipeFunction() - qcvm->functions);
      } else
        SV_VRMeleeMaceReload(sv_vr_contact_call.player);
      memcpy(qcvm->globals + OFS_RETURN, parameters, sizeof(parameters));
      qcvm->argc = argc;
      qcvm->xstatement = statement;
      return true;
    }
    if (function == 2443 && qcvm->xstatement == 112962 &&
        qcvm->xfunction == &qcvm->functions[2461] && SV_VRMeleeScimitarProgs())
      return true; /* Root already executed reload, sound and lunge. */
  }
  if (function != 2621 || !qcvm || qcvm != &sv.qcvm ||
      qcvm->xstatement != 116184 || !SV_VRMeleeGungnirProgs() ||
      qcvm->xfunction != &qcvm->functions[2652] || qcvm->depth < 2 ||
      qcvm->stack[qcvm->depth - 1].f != &qcvm->functions[2713] ||
      qcvm->stack[qcvm->depth - 1].s != 121357 ||
      !(SV_VRContactMode() & VR_WEAPON_CONTACT_CAP_MELEE))
    return false;
  self = pr_global_struct->self;
  if (self <= 0 || self % qcvm->edict_size ||
      (slot = self / qcvm->edict_size) > svs.maxclients)
    return false;
  player = PROG_TO_EDICT(self);
  client = &svs.clients[slot - 1];
  cmd = &client->cmd;
  pose = &cmd->vr_contact;
  if (!SV_VRMeleeGungnirSelected(player) || !cmd->vr_active ||
      !cmd->vr_handpos_relative ||
      !(pose->flags & VR_WEAPON_CONTACT_IMMERSIVE_MELEE))
    return false; /* unsupported/disabled clients retain original behavior */
  /* Recognized immersive requests fail closed when busy/stale, never fall
   * back into Attack1's forbidden trigger stab or its delayed combo. */
  if (!client->active || !client->spawned || client->input_stale ||
      client->edict != player || !(cmd->buttons & BUTTON_ATTACK) ||
      !SV_VRContactSampleValid(pose) || !SV_VRMeleeGungnirIdle(player) ||
      pose->weapon != player->v.weapon || pose->modelindex <= 0 ||
      pose->modelindex >= MAX_MODELS || !sv.model_precache[pose->modelindex] ||
      strcmp(sv.model_precache[pose->modelindex], PR_GetString(player->v.weaponmodel)) ||
      sv_vr_contact_call.active)
    return true;
  ammo = GetEdictFieldValueByName(player, "ammo_voidshards");
  if (!ammo || !isfinite(ammo->_float) ||
      !SV_VRMeleeContextBegin(&saved, player, player->v.v_angle, 0))
    return true;
  statement = qcvm->xstatement;
  SV_VRMeleeGungnirReload(player);
  if (ammo->_float > 0) {
    qcvm->argc = 0;
    PR_ExecuteProgram(2654);
  }
  SV_VRMeleeContextEnd(&saved, player);
  /* Reentrant execution leaves xstatement at the callee's return. The
   * interpreter resumes its original CALL with its original return address. */
  qcvm->xstatement = statement;
  return true;
}

static qboolean SV_VRContactParryEnabled(void) {
  return svs.maxclients > 1 && sv_weapon_collision.value &&
      (SV_VRContactMode() & VR_WEAPON_CONTACT_CAP_MELEE) &&
      (!coop.value || !SV_CoopFeatureEnabled(&sv_coop_noplayerclip, true));
}

static qboolean SV_VRContactBladeReachable(edict_t *player,
    const vr_weapon_contact_t *pose, int hand) {
  vec3_t eye, grip, base, tip;
  trace_t trace;
  sv_vr_melee_subtype_t subtype = SV_VRContactWeapon(player);
  float max_length = subtype == SV_VR_MELEE_STOCK_AXE ||
      subtype == SV_VR_MELEE_HONEY_AXE ? 32 :
      subtype == SV_VR_MELEE_QBJ3_WRENCH ? 48 : 96;
  /* Both endpoints passing the generic reach envelope must not permit a
   * fabricated 192-unit shield. Bound supported tool lengths separately. */
  if (subtype == SV_VR_MELEE_NONE ||
      SV_VRContactDistance(pose->base[hand], pose->tip[hand]) > max_length)
    return false;
  VectorAdd(player->v.origin, player->v.view_ofs, eye);
  VectorAdd(player->v.origin, pose->grip[hand], grip);
  VectorAdd(player->v.origin, pose->base[hand], base);
  VectorAdd(player->v.origin, pose->tip[hand], tip);
  for (int edge = 0; edge < 4; edge++) {
    trace = SV_Move(edge == 0 ? eye : edge == 3 ? base : grip,
        vec3_origin, vec3_origin,
        edge == 0 ? grip : edge == 1 ? base : tip, MOVE_NOMONSTERS, player);
    if (trace.startsolid || trace.allsolid || trace.fraction < 1)
      return false;
  }
  return true;
}

typedef struct {
  vec3_t base, tip;
  int client, hand;
} sv_vr_defending_blade_t;

/* Latest processed geometry, not a rewind history. Packet receipt and
 * consumption clocks are both server-owned; draining a delayed queue must
 * not make its old pose look fresh. Validate again after gameplay has run. */
static int SV_VRContactDefenders(edict_t *attacker,
    sv_vr_defending_blade_t blades[MAX_SCOREBOARD * 2]) {
  int count = 0;
  if (!SV_VRContactParryEnabled())
    return 0;
  for (int i = 0; i < svs.maxclients && i < MAX_SCOREBOARD; i++) {
    client_t *client = &svs.clients[i];
    sv_vr_contact_state_t *state = &sv_vr_contacts[i];
    edict_t *player = client->edict;
    const vr_weapon_contact_t *pose = &state->previous;
    if (!client->active || !client->spawned || client->input_stale ||
        !client->is_vr_client || !client->vr_handpos_relative ||
        !player || player == attacker || player->free ||
        player->v.health <= 0 || player->v.deadflag || !state->valid ||
        !state->defensive_valid || state->defensive_received <= 0 ||
        realtime < state->defensive_received || realtime < state->defensive_processed ||
        realtime - state->defensive_received > .1 ||
        realtime - state->defensive_processed > .1 ||
        state->movement_epoch != client->move_discontinuity_epoch ||
        SV_VRContactDistance(player->v.origin, state->body) > 64 ||
        !(pose->flags & VR_WEAPON_CONTACT_IMMERSIVE_MELEE) ||
        pose->weapon != player->v.weapon || pose->modelindex <= 0 ||
        pose->modelindex >= MAX_MODELS || !sv.model_precache[pose->modelindex] ||
        strcmp(sv.model_precache[pose->modelindex], PR_GetString(player->v.weaponmodel)))
      continue;
    for (int hand = 0; hand < 2; hand++) {
      if (!(state->defensive_valid & (1u << hand)) ||
          !SV_VRContactBladeReachable(player, pose, hand))
        continue;
      VectorAdd(player->v.origin, pose->base[hand], blades[count].base);
      VectorAdd(player->v.origin, pose->tip[hand], blades[count].tip);
      blades[count].client = i;
      blades[count++].hand = hand;
    }
  }
  return count;
}

/* Downed zombies retain their tall standing collision bounds. Use the native
 * MDL frame's bounds for this supplemental contact, otherwise an axe hovering
 * well above the fallen body could gib it. This is a temporary query shape,
 * never a change to the edict or its ordinary collision/animation. */
static qboolean SV_VRContactCorpseTrace(edict_t *corpse,
    const vec3_t start, const vec3_t end, trace_t *trace) {
  qmodel_t *model;
  aliashdr_t *header;
  maliasframedesc_t *frame;
  edict_t shape;
  vec3_t local_start, local_end, delta, angles, forward, right, up, normal;
  int index, frameindex;
  if (!isfinite(corpse->v.modelindex) || !isfinite(corpse->v.frame) ||
      corpse->v.modelindex < 1 || corpse->v.modelindex >= MAX_MODELS ||
      corpse->v.frame < 0 || corpse->v.frame >= MAXALIASFRAMES ||
      !SV_VRMeleeFiniteVector(corpse->v.angles))
    return false;
  index = (int)corpse->v.modelindex;
  frameindex = (int)corpse->v.frame;
  if (!(model = sv.models[index]) || model->type != mod_alias)
    return false;
  header = (aliashdr_t *)Mod_Extradata(model);
  if (!header || header->poseverttype != ALIAS_POSE_MDL ||
      frameindex < 0 || frameindex >= header->numframes)
    return false;
  frame = &header->frames[frameindex];
  memset(&shape, 0, sizeof(shape));
  shape.v.solid = SOLID_BBOX;
  for (int axis = 0; axis < 3; axis++) {
    shape.v.mins[axis] = frame->bboxmin.v[axis] * header->original_scale[axis] +
        header->original_scale_origin[axis];
    shape.v.maxs[axis] = frame->bboxmax.v[axis] * header->original_scale[axis] +
        header->original_scale_origin[axis];
    if (!isfinite(shape.v.mins[axis]) || !isfinite(shape.v.maxs[axis]) ||
        shape.v.mins[axis] >= shape.v.maxs[axis])
      return false;
  }
  VectorCopy(corpse->v.angles, angles);
  angles[PITCH] = -angles[PITCH]; /* native alias model rotation */
  AngleVectors(angles, forward, right, up);
  VectorSubtract(start, corpse->v.origin, delta);
  local_start[0] = DotProduct(delta, forward);
  local_start[1] = -DotProduct(delta, right);
  local_start[2] = DotProduct(delta, up);
  VectorSubtract(end, corpse->v.origin, delta);
  local_end[0] = DotProduct(delta, forward);
  local_end[1] = -DotProduct(delta, right);
  local_end[2] = DotProduct(delta, up);
  *trace = SV_ClipMoveToEntity(&shape, local_start, vec3_origin, vec3_origin, local_end);
  if (trace->fraction >= 1 || trace->startsolid || trace->allsolid)
    return false;
  VectorCopy(trace->endpos, delta);
  VectorMA(corpse->v.origin, delta[0], forward, trace->endpos);
  VectorMA(trace->endpos, -delta[1], right, trace->endpos);
  VectorMA(trace->endpos, delta[2], up, trace->endpos);
  VectorCopy(trace->plane.normal, normal);
  VectorScale(forward, normal[0], trace->plane.normal);
  VectorMA(trace->plane.normal, -normal[1], right, trace->plane.normal);
  VectorMA(trace->plane.normal, normal[2], up, trace->plane.normal);
  trace->plane.dist = DotProduct(trace->plane.normal, trace->endpos);
  trace->ent = corpse;
  return true;
}

/* Honey marks revivable zombies; AD and Mjolnir mark floor corpses. Query only
 * these authored non-solid candidates in the physical sweep; do not link them as
 * solid, change world collision, or make arbitrary dead entities hittable. */
static void SV_VRContactCorpses(edict_t *player, vec3_t start,
    vec3_t end, trace_t *nearest) {
  const sv_vr_melee_ad_descriptor_t *ad = SV_VRMeleeADDescriptor();
  const sv_vr_melee_mjolnir_descriptor_t *mjolnir = SV_VRMeleeMjolnirDescriptor();
  for (int n = svs.maxclients + 1; n < qcvm->num_edicts; n++) {
    edict_t *corpse = EDICT_NUM(n);
    trace_t trace;
    vec3_t delta;
    if (corpse->free || corpse->v.solid != SOLID_NOT)
      continue;
    if (ad || mjolnir) {
      if (strcmp(E_STRING(corpse, ad ? ad->corpse_field : mjolnir->corpse_field), "TRUE"))
        continue;
    } else if (strcmp(PR_GetString(corpse->v.classname), "monster_zombie") ||
        strcmp(E_STRING(corpse, 105), "ZOMBIE_ONGROUND"))
      continue;
    VectorSubtract(corpse->v.origin, player->v.origin, delta);
    if (!SV_VRMeleeFiniteVector(delta) || DotProduct(delta, delta) > 128 * 128)
      continue;
    if (SV_VRContactCorpseTrace(corpse, start, end, &trace) &&
        trace.fraction < nearest->fraction - .00001f)
      *nearest = trace;
  }
}

static trace_t SV_VRContactSweepQuery(edict_t *p, const vr_weapon_contact_t *old,
    const vr_weapon_contact_t *now, int hand, qboolean test_parry,
    const int *hit_entities, int hit_count, float min_time, float *event_time,
    int *parry_client, int *parry_hand, qboolean current_only) {
  vec3_t eye, grip, a, b;
  trace_t result, trace, reach;
  sv_vr_defending_blade_t blades[MAX_SCOREBOARD * 2];
  int numblades = test_parry && (now->flags & VR_WEAPON_CONTACT_IMMERSIVE_MELEE) &&
      SV_VRContactWeapon(p) != SV_VR_MELEE_NONE ? SV_VRContactDefenders(p, blades) : 0;
  float first_time = 2;
  sv_vr_melee_subtype_t subtype = SV_VRContactWeapon(p);
  eval_t *tome = subtype == SV_VR_MELEE_MJOLNIR_AXE ?
      GetEdictFieldValueByName(p, "tome_finished") : NULL;
  qboolean authored_corpses = test_parry &&
      (now->flags & VR_WEAPON_CONTACT_IMMERSIVE_MELEE) &&
      (subtype == SV_VR_MELEE_HONEY_AXE ||
       (subtype == SV_VR_MELEE_MJOLNIR_AXE &&
        (SV_VRMeleeMjolnirAxeVariant(p) == SV_VR_MJOLNIR_AXE_SHADOW ||
         (tome && isfinite(tome->_float) && tome->_float != 0))) ||
       (subtype == SV_VR_MELEE_AD_AXE &&
        ((int)GetEdictFieldValueByName(p, "moditems")->_float & 4096)));
  int steps = CLAMP(1, (int)ceilf(SV_VRContactDistance(
      now->base[hand], now->tip[hand]) / 3), 32);
  memset(&result, 0, sizeof(result));
  result.fraction = 1;
  *event_time = 2;
  *parry_client = *parry_hand = -1;
  VectorAdd(p->v.origin, p->v.view_ofs, eye);
  VectorAdd(p->v.origin, now->grip[hand], grip);
  /* Quake BSPs have discrete point/player hulls, not arbitrary box sweeps.
   * A 3-unit-wide "blade box" selects the 32-unit player hull. Use true point
   * traces along the blade so a nearby wall cannot block an in-reach button. */
  reach = SV_Move(eye, vec3_origin, vec3_origin, grip, MOVE_NOMONSTERS, p);
  if (reach.startsolid || reach.allsolid || reach.fraction < 1)
    return result;
  /* Current blade, plus bounded sweeps of points along it. All points use the
   * same current body translation, so walking itself is not a swing. */
  for (int i = -1; i <= (current_only ? -1 : steps); i++) {
    for (int axis = 0; axis < 3; axis++) {
      float t = i < 0 ? 0 : (float)i / steps;
      a[axis] = p->v.origin[axis] + (i < 0 ? now->base[hand][axis] :
          old->base[hand][axis] + t * (old->tip[hand][axis] - old->base[hand][axis]));
      b[axis] = p->v.origin[axis] + (i < 0 ? now->tip[hand][axis] :
          now->base[hand][axis] + t * (now->tip[hand][axis] - now->base[hand][axis]));
      /* After an outcome mutates geometry, only the remaining physical time
       * interval may be retraced against that new geometry. */
      if (i >= 0)
        a[axis] += min_time * (b[axis] - a[axis]);
    }
    trace = SV_Move(a, vec3_origin, vec3_origin, b, MOVE_NORMAL, p);
    if (trace.startsolid || trace.allsolid)
      continue;
    if (authored_corpses)
      SV_VRContactCorpses(p, a, b, &trace);
    float nearest_guard = 2;
    int guard = -1;
    for (int blade = 0; blade < numblades; blade++) {
      vec3_t point, delta;
      float entry = SV_VRContactCapsuleFraction(a, b,
          blades[blade].base, blades[blade].tip, 4);
      /* First resolve spatial ordering within this ray. Current-segment
       * contacts all happen at time 1, but a body behind a guard must not
       * overwrite that nearer guard merely because their times are equal. */
      if (entry > 1 || entry >= trace.fraction - .00001f ||
          entry >= nearest_guard)
        continue;
      VectorSubtract(b, a, delta);
      VectorMA(a, entry, delta, point);
      reach = SV_Move(grip, vec3_origin, vec3_origin, point, MOVE_NOMONSTERS, p);
      if (reach.startsolid || reach.allsolid || reach.fraction < 1)
        continue;
      nearest_guard = entry;
      guard = blade;
    }
    if (guard >= 0) {
      float contact_time = i < 0 ? 1 : min_time + (1 - min_time) * nearest_guard;
      if (contact_time < first_time - .00001f) {
        memset(&result, 0, sizeof(result));
        result.fraction = 1; /* A parry is never a QC body damage hit. */
        first_time = contact_time;
        *parry_client = blades[guard].client;
        *parry_hand = blades[guard].hand;
      }
      continue; /* This ray cannot also hit a body behind its guard. */
    }
    if (trace.fraction >= 1 || !trace.ent)
      continue;
    /* Previously hit entities still obstruct the real trace. They are only
     * excluded as new outcomes; never change .solid or trace through them to
     * manufacture the original mod's second target. Other blade points may
     * independently reach a distinct target during the same physical sweep. */
    qboolean already_hit = false;
    for (int target = 0; target < hit_count; target++)
      if (hit_entities[target] == NUM_FOR_EDICT(trace.ent))
        already_hit = true;
    if (already_hit)
      continue;
    /* A client endpoint cannot grant reach through intervening world/doors. */
    reach = SV_Move(grip, vec3_origin, vec3_origin, trace.endpos, MOVE_NOMONSTERS, p);
    if (reach.startsolid || reach.allsolid ||
        (reach.fraction < 1 && SV_VRContactDistance(reach.endpos, trace.endpos) > 2))
      continue;
    /* A fraction along the current blade is not a time fraction. Prefer
     * earliest swept contact; the current segment is the endpoint fallback. */
    float contact_time = i < 0 ? 1 : min_time + (1 - min_time) * trace.fraction;
    if (contact_time <= first_time + .00001f) {
      result = trace;
      first_time = contact_time;
      *parry_client = *parry_hand = -1;
    }
  }
  *event_time = first_time;
  return result;
}

static trace_t SV_VRContactSweep(edict_t *p, const vr_weapon_contact_t *old,
    const vr_weapon_contact_t *now, int hand, qboolean test_parry,
    const int *hit_entities, int hit_count, float min_time, float *event_time,
    int *parry_client, int *parry_hand) {
  return SV_VRContactSweepQuery(p, old, now, hand, test_parry, hit_entities,
      hit_count, min_time, event_time, parry_client, parry_hand, false);
}

/* Combat-only comfort reach, independent of the player's movement hull.
 * Quake VR also supplements physical hand contact with forward attack rays.
 * Keep ours bounded, current-time-only and subordinate to real geometry.
 * Never use these synthetic endpoints for speed/history or defensive blades. */
static trace_t SV_VRContactAssist(edict_t *p, const vr_weapon_contact_t *now,
    int hand, const vec3_t aim, const int *hit_entities, int hit_count,
    float min_time, float *event_time, int *parry_client, int *parry_hand) {
  vr_weapon_contact_t assist = *now;
  vec3_t angles, forward, right, up, delta;
  trace_t result;
  float projection = 0, reach;
  memset(&result, 0, sizeof(result));
  result.fraction = 1;
  *event_time = 2;
  *parry_client = *parry_hand = -1;
  if (!isfinite(sv_melee_hitassist.value) || sv_melee_hitassist.value <= 0 ||
      min_time >= 1 || !(now->flags & VR_WEAPON_CONTACT_IMMERSIVE_MELEE) ||
      SV_VRContactWeapon(p) == SV_VR_MELEE_NONE || !SV_VRMeleeFiniteVector(aim))
    return result;
  VectorCopy(aim, angles);
  AngleVectors(angles, forward, right, up);
  VectorSubtract(now->base[hand], now->grip[hand], delta);
  projection = q_max(projection, DotProduct(delta, forward));
  VectorSubtract(now->tip[hand], now->grip[hand], delta);
  projection = q_max(projection, DotProduct(delta, forward));
  /* Eight extra world units by default, never past sixteen from the grip.
   * A long weapon keeps its full physical reach without gaining more here. */
  reach = q_min(16, projection + q_min(16, sv_melee_hitassist.value));
  if (reach <= projection)
    return result;
  VectorCopy(now->grip[hand], assist.base[hand]);
  VectorMA(assist.base[hand], reach, forward, assist.tip[hand]);
  result = SV_VRContactSweepQuery(p, &assist, &assist, hand, true,
      hit_entities, hit_count, min_time, event_time, parry_client, parry_hand, true);
  /* Shootable switches can have takedamage too. Only actors gain assistance;
   * ordinary buttons, walls and other obstructions remain physical-only. */
  if (*parry_client < 0 && (!result.ent || result.ent->free ||
      !result.ent->v.takedamage ||
      (!((int)result.ent->v.flags & FL_MONSTER) && !SV_IsActiveClientEdict(result.ent)))) {
    memset(&result, 0, sizeof(result));
    result.fraction = 1;
    *event_time = 2;
  }
  return result;
}

void SV_VRContactProcessCommand(client_t *client, const usercmd_t *cmd) {
  int n = (int)(client - svs.clients);
  sv_vr_contact_state_t *s;
  const vr_weapon_contact_t sample = cmd->vr_contact;
  const vr_weapon_contact_t *c = &sample;
  edict_t *p = client->edict;
  byte defensive_valid = 0;
  float dt;
  if (n < 0 || n >= MAX_SCOREBOARD)
    return;
  s = &sv_vr_contacts[n];
  s->defensive_valid = 0;
  if (!SV_VRContactMode() || !client->active || !client->spawned ||
      !p || p->free || p->v.health <= 0 || p->v.deadflag || client->input_stale ||
      !cmd->vr_active || !cmd->vr_handpos_relative || !SV_VRContactSampleValid(c) ||
      c->modelindex <= 0 || c->modelindex >= MAX_MODELS ||
      !sv.model_precache[c->modelindex] ||
      strcmp(sv.model_precache[c->modelindex], PR_GetString(p->v.weaponmodel)) ||
      c->weapon != p->v.weapon || !isfinite(cmd->servertime)) {
    s->valid = false;
    return;
  }
  dt = cmd->servertime - s->sample_time;
  if (!s->valid || cmd->sequence != s->sequence + 1 || dt <= 0 || dt > .1f ||
      c->modelindex != s->previous.modelindex || c->flags != s->previous.flags ||
      c->weapon != s->previous.weapon ||
      client->move_discontinuity_epoch != s->movement_epoch || cmd->impulse ||
      SV_VRContactDistance(p->v.origin, s->body) > 64) {
    memset(s->arc, 0, sizeof(s->arc));
    memset(s->peak_speed, 0, sizeof(s->peak_speed));
    memset(s->consumed, 0, sizeof(s->consumed));
    memset(s->authorized, 0, sizeof(s->authorized));
    memset(s->hit_count, 0, sizeof(s->hit_count));
    memset(s->button, 0, sizeof(s->button));
    goto remember;
  }
  for (int hand = 0; hand < 2; hand++) {
    trace_t hit;
    vec3_t aim;
    sv_vr_melee_subtype_t subtype = SV_VRContactWeapon(p);
    float distance, endpoint_motion, contact_time;
    qboolean test_parry;
    int button = 0, parry_client, parry_hand;
    if (!(c->flags & (1u << hand)))
      continue;
    distance = SV_VRContactDistance(c->tip[hand], s->previous.tip[hand]);
    endpoint_motion = q_max(distance,
        SV_VRContactDistance(c->base[hand], s->previous.base[hand]));
    /* Tracking discontinuity (including snap turn): restart, not a long hit
     * ray. Raw physical point speed is independent of locomotion/retraction. */
    if (endpoint_motion > 32 || endpoint_motion > c->speed[hand] * dt * 80 + 3) {
      s->arc[hand] = 0;
      s->peak_speed[hand] = 0;
      s->consumed[hand] = true;
      s->authorized[hand] = false;
      s->hit_count[hand] = 0;
      continue;
    }
    if (s->authorized[hand] && (s->subtype[hand] != subtype ||
        qcvm->time >= s->recovery_deadline[hand])) {
      s->authorized[hand] = false;
      s->hit_count[hand] = 0;
      s->consumed[hand] = true;
    }
    if (SV_VRContactParryEnabled() &&
        (c->flags & VR_WEAPON_CONTACT_IMMERSIVE_MELEE) &&
        SV_VRContactBladeReachable(p, c, hand))
      defensive_valid |= 1u << hand;
    /* Only an eligible moving stroke needs opponent blade queries. Ordinary
     * gun pokes, resting guards and already-consumed swings still use their
     * existing world/button sweep without O(players) defensive traces. */
    test_parry = !s->consumed[hand] && !s->parry_rearm[hand] &&
        s->arc[hand] + (c->speed[hand] >= SV_VR_CONTACT_STRIKE_SPEED &&
            endpoint_motion > SV_VR_CONTACT_MOTION_EPSILON ? c->speed[hand] * dt : 0) >=
        SV_VR_CONTACT_STRIKE_ARC;
    hit = SV_VRContactSweep(p, &s->previous, c, hand, test_parry,
        s->hit_entities[hand], s->hit_count[hand], 0, &contact_time,
        &parry_client, &parry_hand);
    if (sv_weapon_collision.value && hit.ent && !hit.ent->free &&
        hit.ent->v.solid == SOLID_BSP && hit.ent->v.touch &&
        hit.ent->v.health <= 0) {
      if (SV_VRContactButtonTouch(hit.ent)) {
        button = NUM_FOR_EDICT(hit.ent);
        if (button != s->button[hand]) {
          int self = pr_global_struct->self, other = pr_global_struct->other;
          float time = pr_global_struct->time;
          pr_global_struct->self = EDICT_TO_PROG(hit.ent);
          pr_global_struct->other = EDICT_TO_PROG(p);
          pr_global_struct->time = qcvm->time;
          PR_ExecuteProgram(hit.ent->v.touch);
          pr_global_struct->self = self;
          pr_global_struct->other = other;
          pr_global_struct->time = time;
          SV_VRContactFeedback(client, hand);
          if (p->free || p->v.health <= 0 || p->v.deadflag ||
              SV_VRContactDistance(p->v.origin, s->body) > 64) {
            SV_VRContactResetClient(client);
            return;
          }
        }
      }
    }
    s->button[hand] = button;
    if (!(c->flags & VR_WEAPON_CONTACT_IMMERSIVE_MELEE) ||
        subtype == SV_VR_MELEE_NONE || subtype != SV_VRContactWeapon(p))
      continue;
    if (SV_VRMeleeNativeTrigger(subtype) && (cmd->buttons & BUTTON_ATTACK)) {
      s->arc[hand] = s->peak_speed[hand] = 0;
      s->consumed[hand] = true;
      s->authorized[hand] = false;
      s->hit_count[hand] = 0;
      continue;
    }
    if (s->parry_rearm[hand]) {
      /* A cancelled active swing needs a later received rest sample. Queued
       * commands from the clash's packet cannot immediately resurrect it. */
      s->arc[hand] = 0;
      s->peak_speed[hand] = 0;
      s->consumed[hand] = true;
      s->authorized[hand] = false;
      s->hit_count[hand] = 0;
      if (cmd->vr_contact_received > s->parry_received[hand] &&
          c->speed[hand] < SV_VR_CONTACT_REST_SPEED) {
        s->parry_rearm[hand] = false;
        s->consumed[hand] = false;
      }
      continue;
    }
    /* Speed is already physical metres/second, including wrist rotation.
     * World-coordinate motion is a witness, not a second distance estimate:
     * dividing it by a fixed units/metre constant changes the effort needed
     * whenever the client changes vr_world_scale. A stationary blade cannot
     * accumulate a swing from a stale nonzero speed sample. */
    if (c->speed[hand] >= SV_VR_CONTACT_STRIKE_SPEED &&
        endpoint_motion > SV_VR_CONTACT_MOTION_EPSILON) {
      s->arc[hand] += c->speed[hand] * dt;
      s->peak_speed[hand] = q_max(s->peak_speed[hand], c->speed[hand]);
    }
    /* The accepted tracked aim supplies QC's forward/right/up basis. A
     * cutting edge need not point forward (the axe ridge is perpendicular). */
    VectorCopy(cmd->vr_akimbo_active ? cmd->vr_akimbo_angles[hand] :
        cmd->vr_handrot, aim);
    if (!s->consumed[hand] && s->arc[hand] >= SV_VR_CONTACT_STRIKE_ARC &&
        hit.fraction >= 1 && parry_client < 0)
      hit = SV_VRContactAssist(p, c, hand, aim, s->hit_entities[hand],
          s->hit_count[hand], 0, &contact_time, &parry_client, &parry_hand);
    for (int contact_number = 0; contact_number < 2 && !s->consumed[hand] &&
        s->arc[hand] >= SV_VR_CONTACT_STRIKE_ARC &&
        (hit.fraction < 1 || parry_client >= 0 ||
         c->speed[hand] < SV_VR_CONTACT_REST_SPEED);
        contact_number++) {
      qboolean first = !s->authorized[hand];
      int target = hit.ent ? NUM_FOR_EDICT(hit.ent) : 0;
      int target_limit = subtype == SV_VR_MELEE_STOCK_AXE ||
          subtype == SV_VR_MELEE_HONEY_AXE ||
          subtype == SV_VR_MELEE_AD_AXE ||
          subtype == SV_VR_MELEE_COPPER_AXE ||
          subtype == SV_VR_MELEE_ALK_AXE ||
          subtype == SV_VR_MELEE_IMMORTAL_AXE ||
          subtype == SV_VR_MELEE_IMMORTAL_HAMMER ||
          subtype == SV_VR_MELEE_DRAKE_AXE ||
          subtype == SV_VR_MELEE_MJOLNIR_AXE ||
          SV_VRMeleeNativeTrigger(subtype) ||
          subtype == SV_VR_MELEE_DWELL_AXE ||
          subtype == SV_VR_MELEE_DWELL_BERSERK ? 1 : 2;
      float deadline;
      float tier = first ? SV_VRContactTier(subtype, s->arc[hand],
          s->peak_speed[hand]) : s->tier[hand];
      sv_vr_contact_outcome_t outcome = parry_client >= 0 ? VR_CONTACT_PARRIED :
          hit.fraction < 1 ? VR_CONTACT_HIT : VR_CONTACT_WHIFF;
      /* Ending a stroke after an impact is not a second missed attack. */
      if (!first && outcome == VR_CONTACT_WHIFF) {
        s->consumed[hand] = true;
        break;
      }
      /* This exact accepted command owns the source, including historical
       * legacy samples. Never borrow a newer client's pose for a Tome shot. */
      VectorAdd(p->v.origin, cmd->vr_handpos, sv_vr_contact_call.muzzle);
      sv_vr_contact_call.has_muzzle = SV_VRMeleeFiniteVector(sv_vr_contact_call.muzzle);
      qboolean accepted = SV_VRMeleeOutcome(p, subtype, outcome,
          outcome == VR_CONTACT_HIT ? &hit : NULL, aim, tier, hand, first, &deadline);
      sv_vr_contact_call.has_muzzle = false;
      if (accepted) {
          if (first) {
            s->authorized[hand] = true;
            s->subtype[hand] = subtype;
            s->tier[hand] = tier;
            s->recovery_deadline[hand] = deadline;
          }
          if (outcome != VR_CONTACT_WHIFF)
            SV_VRContactFeedback(client, hand);
          if (outcome == VR_CONTACT_PARRIED) {
            sv_vr_contact_state_t *opponent = &sv_vr_contacts[parry_client];
            s->parry_rearm[hand] = true;
            s->parry_received[hand] = realtime;
            /* An idle guard is not an attack and incurs no invented cooldown.
             * Cancel only an opponent's already-moving physical stroke. */
            if (opponent->arc[parry_hand] > 0 ||
                opponent->previous.speed[parry_hand] >= SV_VR_CONTACT_STRIKE_SPEED) {
              opponent->consumed[parry_hand] = true;
              opponent->parry_rearm[parry_hand] = true;
              opponent->parry_received[parry_hand] = realtime;
              opponent->arc[parry_hand] = 0;
              opponent->peak_speed[parry_hand] = 0;
              opponent->authorized[parry_hand] = false;
              opponent->hit_count[parry_hand] = 0;
            }
            SV_VRContactFeedback(&svs.clients[parry_client], parry_hand);
          }
          if (outcome == VR_CONTACT_HIT && s->hit_count[hand] < 2)
            s->hit_entities[hand][s->hit_count[hand]++] = target;
          if (outcome != VR_CONTACT_HIT || target == 0 ||
              s->hit_count[hand] >= target_limit)
            s->consumed[hand] = true;
      } else {
        /* A rejected gesture is consumed, not authorized. It cannot obtain
         * a follow-up by bypassing the original cooldown or draw scheduler. */
        s->consumed[hand] = true;
      }
      if (p->free || p->v.health <= 0 || p->v.deadflag ||
          SV_VRContactDistance(p->v.origin, s->body) > 64) {
        SV_VRContactResetClient(client);
        return;
      }
      if (!s->consumed[hand]) {
        float remaining_time = contact_time;
        hit = SV_VRContactSweep(p, &s->previous, c, hand, true,
            s->hit_entities[hand], s->hit_count[hand], contact_time, &contact_time,
            &parry_client, &parry_hand);
        if (hit.fraction >= 1 && parry_client < 0)
          hit = SV_VRContactAssist(p, c, hand, aim, s->hit_entities[hand],
              s->hit_count[hand], remaining_time, &contact_time,
              &parry_client, &parry_hand);
      }
    }
    if (c->speed[hand] < SV_VR_CONTACT_REST_SPEED && !s->parry_rearm[hand]) {
      s->arc[hand] = 0;
      s->peak_speed[hand] = 0;
      s->consumed[hand] = false;
      s->authorized[hand] = false;
      s->hit_count[hand] = 0;
    }
  }
remember:
  s->defensive_valid = defensive_valid;
  s->defensive_received = cmd->vr_contact_received;
  s->defensive_processed = realtime;
  s->valid = true;
  s->previous = *c;
  s->sequence = cmd->sequence;
  s->sample_time = cmd->servertime;
  s->movement_epoch = client->move_discontinuity_epoch;
  VectorCopy(p->v.origin, s->body);
}

void SV_VRContactDrainLegacy(client_t *client) {
  int n = (int)(client - svs.clients);
  if (n < 0 || n >= MAX_SCOREBOARD)
    return;
  sv_vr_contact_state_t *s = &sv_vr_contacts[n];
  unsigned int count = s->count;
  s->count = 0;
  for (unsigned int i = 0; i < count; i++) {
    usercmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.vr_active = cmd.vr_handpos_relative = true;
    cmd.vr_contact = s->pending[i].contact;
    VectorCopy(s->pending[i].handrot, cmd.vr_handrot);
    VectorCopy(s->pending[i].handpos, cmd.vr_handpos);
    memcpy(cmd.vr_akimbo_angles, s->pending[i].akimbo_angles,
        sizeof(cmd.vr_akimbo_angles));
    cmd.vr_akimbo_active = s->pending[i].akimbo_active;
    cmd.sequence = s->pending[i].sequence;
    cmd.servertime = s->pending[i].time;
    cmd.vr_contact_received = s->pending[i].received;
    cmd.impulse = s->pending[i].impulse;
    cmd.buttons = s->pending[i].buttons;
    SV_VRContactProcessCommand(client, &cmd);
  }
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

  if (!coop.value || !SV_CoopFeatureEnabled(&sv_coop_revive, true) ||
      !coop_revive_trace_owner)
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

  if (!coop.value || !SV_CoopFeatureEnabled(&sv_coop_revive, true))
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
respawned player to a safe spot near the death origin or last dry position. If
that fails, try a safe spot near a living teammate before falling back to the
mod's spawn point. Fresh late-join placement remains teammate-based.
=============
*/
#define COOP_RESPAWN_ALL_ITEM_BITS (-1)
#define COOP_RESPAWN_DRAKE_CUSTOM_KEYS (8192 | 16384 | 32768 | 65536)
#define COOP_RESPAWN_DWELL_WEAPON_BITS (4 | 8 | 32)
#define COOP_RESPAWN_STOCK_KEY_BITS (IT_KEY1 | IT_KEY2 | IT_SIGIL1 | IT_SIGIL2 | IT_SIGIL3 | IT_SIGIL4)
#define COOP_RESPAWN_ITEMS2_KEY_BITS 65536
#define COOP_RESPAWN_WORLDTYPE_KEY_MASK 255
#define COOP_RESPAWN_AD_KEEP_MODITEMS                                           \
  (2 | 64 | 128 | 4096 | 131072 | 262144 | 524288 | 1048576 | 2097152 |        \
   4194304 | COOP_RESPAWN_DRAKE_CUSTOM_KEYS)

typedef enum {
  COOP_RESPAWN_EXTRA_ITEMS2,
  COOP_RESPAWN_EXTRA_ITEMS3,
  COOP_RESPAWN_EXTRA_MODITEMS,
  COOP_RESPAWN_EXTRA_PERMITEMS,
  COOP_RESPAWN_EXTRA_PERMS,
  COOP_RESPAWN_EXTRA_CUSTOMKEYS,
  COOP_RESPAWN_EXTRA_WEAPONS,
  COOP_RESPAWN_EXTRA_WEAPON2,
  COOP_RESPAWN_EXTRA_WEAPONS2,
  COOP_RESPAWN_EXTRA_ITEMS_DWELL,
  COOP_RESPAWN_EXTRA_ITEMS_MOVEMOD,
  COOP_RESPAWN_EXTRA_RUNESHARD_COU,
  COOP_RESPAWN_EXTRA_CURRENTWEAPON,
  COOP_RESPAWN_EXTRA_WORLDTYPE,
  COOP_RESPAWN_EXTRA_KEY_COUNT_SILVER,
  COOP_RESPAWN_EXTRA_KEY_COUNT_GOLD,
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
  COOP_RESPAWN_EXTRA_CKEYSKIN1,
  COOP_RESPAWN_EXTRA_CKEYSKIN2,
  COOP_RESPAWN_EXTRA_CKEYSKIN3,
  COOP_RESPAWN_EXTRA_CKEYSKIN4,
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
  qboolean mod_owns_respawn;
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
    {"items3", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
    {"moditems", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_AD_KEEP_MODITEMS},
    {"permitems", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
    {"perms", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
    {"customkeys", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
    {"weapons", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
    {"weapon2", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
    {"weapons2", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
    {"items_dwell", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_DWELL_WEAPON_BITS},
    {"items_movemod", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
    {"runeshard_cou", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"currentweapon", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
    {"worldtype", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
    {"key_count_silver", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
    {"key_count_gold", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
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
    {"ckeyskin1", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
    {"ckeyskin2", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
    {"ckeyskin3", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
    {"ckeyskin4", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
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
static double coop_respawn_limbo_since[MAX_SCOREBOARD];
static qboolean coop_respawn_force_standard_spawn[MAX_SCOREBOARD];
static qboolean coop_respawn_mod_cleanup_pending[MAX_SCOREBOARD];
static qboolean coop_respawn_frame_started_alive[MAX_SCOREBOARD];
static qboolean coop_respawn_frame_death_handled[MAX_SCOREBOARD];
static coop_respawn_postthink_state_t
    coop_respawn_frame_predeath_state[MAX_SCOREBOARD];

void SV_ResetTransientClientSlot(int slot) {
  if (slot < 0 || slot >= MAX_SCOREBOARD)
    return;

  memset(&coop_respawn_last_inventory[slot], 0,
         sizeof(coop_respawn_last_inventory[slot]));
  coop_respawn_last_inventory_valid[slot] = false;
  VectorClear(coop_respawn_last_safe_origin[slot]);
  VectorClear(coop_respawn_last_safe_angles[slot]);
  VectorClear(coop_respawn_last_safe_v_angle[slot]);
  coop_respawn_last_safe_valid[slot] = false;
  VectorClear(coop_respawn_death_anchor[slot]);
  VectorClear(coop_respawn_death_angles[slot]);
  VectorClear(coop_respawn_death_v_angle[slot]);
  coop_respawn_death_anchor_valid[slot] = false;
  coop_respawn_dead_since[slot] = 0.0;
  coop_respawn_limbo_since[slot] = 0.0;
  coop_respawn_force_standard_spawn[slot] = false;
  coop_respawn_mod_cleanup_pending[slot] = false;
  coop_respawn_frame_started_alive[slot] = false;
  coop_respawn_frame_death_handled[slot] = false;
  memset(&coop_respawn_frame_predeath_state[slot], 0,
         sizeof(coop_respawn_frame_predeath_state[slot]));
}

void SV_ResetTransientClientState(void) {
  int i;

  /* Restore any temporarily protected players before old edicts are freed. */
  SV_FriendlyFireReset();
  for (i = 0; i < MAX_SCOREBOARD; ++i)
    SV_ResetTransientClientSlot(i);
}

static qboolean SV_CoopRespawnCanPlaceAt(edict_t *ent, vec3_t origin);
static qboolean SV_CoopRespawnCanPlaceAtDry(edict_t *ent, vec3_t origin);

static qboolean SV_CoopRespawnIsAliveClient(edict_t *ent) {
  return SV_CoopIsActiveClient(ent) && ent->v.health > 0 &&
         ent->v.deadflag == DEAD_NO && ent->v.solid != SOLID_NOT;
}

static qboolean SV_CoopRespawnDelayApplies(void) {
  return coop.value && SV_CoopFeatureEnabled(&sv_coop_respawn_near_player, true) &&
         sv_coop_respawn_delay.value > 0.0f;
}

#define QBJ3_CFL_PLUNGE 64
#define QBJ3_CFL_LIMBO 2048
#define QBJ3_VOID_CSHIFT_PRIORITY 70.0f
#define QBJ3_VOID_CSHIFT_DENSITY 255.0f

static qboolean SV_CoopRespawnFieldHasType(const char *name,
                                           int expected_type) {
  ddef_t *def = ED_FindField(name);

  return def && (def->type & ~DEF_SAVEGLOBAL) == expected_type;
}

static eval_t *SV_CoopRespawnGetTypedField(edict_t *ent, const char *name,
                                           int expected_type) {
  ddef_t *def;

  if (!ent || ent->free)
    return NULL;
  def = ED_FindField(name);
  if (!def || (def->type & ~DEF_SAVEGLOBAL) != expected_type)
    return NULL;
  return GetEdictFieldValue(ent, def->ofs);
}

static dfunction_t *SV_CoopRespawnFindFunction(const char *name,
                                               int numparms) {
  dfunction_t *func = ED_FindFunction(name);

  if (!func || func->numparms != numparms)
    return NULL;
  if (numparms == 1 && func->parm_size[0] != 1)
    return NULL;
  return func;
}

static qboolean SV_CoopRespawnCshiftMatchesVoidLayer(edict_t *ent,
                                                     const char *suffix) {
  char name[32];
  eval_t *priority, *density, *color;

  q_snprintf(name, sizeof(name), "csf_priority%s", suffix);
  priority = SV_CoopRespawnGetTypedField(ent, name, ev_float);
  q_snprintf(name, sizeof(name), "csf_density%s", suffix);
  density = SV_CoopRespawnGetTypedField(ent, name, ev_float);
  q_snprintf(name, sizeof(name), "csf_color%s", suffix);
  color = SV_CoopRespawnGetTypedField(ent, name, ev_vector);
  if (!priority || !density || !color)
    return false;

  return fabs(priority->_float - QBJ3_VOID_CSHIFT_PRIORITY) < 0.01f &&
         fabs(density->_float - QBJ3_VOID_CSHIFT_DENSITY) < 0.01f &&
         fabs(color->vector[0] - 32.0f) < 0.01f &&
         fabs(color->vector[1]) < 0.01f && fabs(color->vector[2]) < 0.01f;
}

static qboolean SV_CoopRespawnHasVoidCshift(edict_t *ent) {
  return SV_CoopRespawnCshiftMatchesVoidLayer(ent, "") ||
         SV_CoopRespawnCshiftMatchesVoidLayer(ent, "_prev");
}

static int SV_CoopRespawnCustomFlags(edict_t *ent, qboolean *valid) {
  eval_t *customflags =
      SV_CoopRespawnGetTypedField(ent, "customflags", ev_float);

  if (valid)
    *valid = customflags != NULL;
  return customflags ? (int)customflags->_float : 0;
}

static qboolean SV_CoopRespawnHasQBJ3VoidAPI(void) {
  return SV_CoopRespawnFindFunction("player_spawn_void_monitor", 0) != NULL &&
         SV_CoopRespawnFindFunction("void_unplunge", 1) != NULL &&
         SV_CoopRespawnFindFunction("csf_clear_all", 1) != NULL &&
         SV_CoopRespawnFieldHasType("customflags", ev_float) &&
         SV_CoopRespawnFieldHasType("csfcontroller", ev_entity) &&
         SV_CoopRespawnFieldHasType("csf_priority", ev_float) &&
         SV_CoopRespawnFieldHasType("csf_density", ev_float) &&
         SV_CoopRespawnFieldHasType("csf_color", ev_vector) &&
         SV_CoopRespawnFieldHasType("csf_priority_prev", ev_float) &&
         SV_CoopRespawnFieldHasType("csf_density_prev", ev_float) &&
         SV_CoopRespawnFieldHasType("csf_color_prev", ev_vector);
}

static qboolean SV_CoopRespawnHasQBJ3TeleportAPI(void) {
  return SV_CoopRespawnFindFunction("teleport_limbo_think", 0) != NULL &&
         SV_CoopRespawnFindFunction("teleport_exit_limbo", 1) != NULL &&
         SV_CoopRespawnFieldHasType("customflags", ev_float) &&
         SV_CoopRespawnFieldHasType("dest", ev_vector) &&
         SV_CoopRespawnFieldHasType("goalentity", ev_entity);
}

static qboolean SV_CoopRespawnHasValidTeleportGoal(edict_t *ent) {
  const char *classname;
  edict_t *goal;
  int goalref;

  if (!ent)
    return false;
  goalref = ent->v.goalentity;
  if (goalref <= 0 || goalref % qcvm->edict_size != 0 ||
      goalref > (qcvm->num_edicts - 1) * qcvm->edict_size)
    return false;
  goal = PROG_TO_EDICT(goalref);
  if (goal->free || !goal->v.classname)
    return false;

  classname = PR_GetString(goal->v.classname);
  return !strcmp(classname, "info_teleport_destination") ||
         !strcmp(classname, "info_teleport_target") ||
         !strcmp(classname, "misc_teleporttrain") ||
         !strcmp(classname, "info_notnull");
}

static qboolean SV_CoopRespawnModOwnsLifecycle(edict_t *ent) {
  qboolean flags_valid;
  int customflags;

  /* QBJ3's void system owns the physical plunge/respawn transition.  The
   * exact API, typed fields and cshift signature keep this compatibility path
   * from changing unrelated mods that happen to use a customflags bit. */
  if (!coop.value || deathmatch.value || !ent || ent->free ||
      !SV_CoopRespawnHasQBJ3VoidAPI() || !SV_CoopRespawnHasVoidCshift(ent))
    return false;
  customflags = SV_CoopRespawnCustomFlags(ent, &flags_valid);
  return flags_valid && (customflags & QBJ3_CFL_PLUNGE) != 0;
}

static qboolean SV_CoopRespawnCallEntityFunction(edict_t *ent,
                                                 const char *name) {
  dfunction_t *func;
  float old_parms[MAX_PARMS * 3], old_return[3], old_time;
  int old_self, old_other, old_argc;

  if (!ent || ent->free || !(func = SV_CoopRespawnFindFunction(name, 1)))
    return false;

  old_self = pr_global_struct->self;
  old_other = pr_global_struct->other;
  old_time = pr_global_struct->time;
  old_argc = qcvm->argc;
  memcpy(old_parms, &qcvm->globals[OFS_PARM0], sizeof(old_parms));
  memcpy(old_return, &qcvm->globals[OFS_RETURN], sizeof(old_return));

  pr_global_struct->self = EDICT_TO_PROG(ent);
  pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
  pr_global_struct->time = qcvm->time;
  qcvm->argc = 1;
  G_INT(OFS_PARM0) = EDICT_TO_PROG(ent);
  PR_ExecuteProgram(func - qcvm->functions);

  pr_global_struct->self = old_self;
  pr_global_struct->other = old_other;
  pr_global_struct->time = old_time;
  qcvm->argc = old_argc;
  memcpy(&qcvm->globals[OFS_PARM0], old_parms, sizeof(old_parms));
  memcpy(&qcvm->globals[OFS_RETURN], old_return, sizeof(old_return));
  return !ent->free && SV_CoopIsActiveClient(ent);
}

static qboolean SV_CoopRespawnCallSelfFunction(edict_t *ent,
                                               const char *name) {
  dfunction_t *func;
  float old_parms[MAX_PARMS * 3], old_return[3], old_time;
  int old_self, old_other, old_argc;

  if (!ent || ent->free || !(func = SV_CoopRespawnFindFunction(name, 0)))
    return false;

  old_self = pr_global_struct->self;
  old_other = pr_global_struct->other;
  old_time = pr_global_struct->time;
  old_argc = qcvm->argc;
  memcpy(old_parms, &qcvm->globals[OFS_PARM0], sizeof(old_parms));
  memcpy(old_return, &qcvm->globals[OFS_RETURN], sizeof(old_return));

  pr_global_struct->self = EDICT_TO_PROG(ent);
  pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
  pr_global_struct->time = qcvm->time;
  qcvm->argc = 0;
  PR_ExecuteProgram(func - qcvm->functions);

  pr_global_struct->self = old_self;
  pr_global_struct->other = old_other;
  pr_global_struct->time = old_time;
  qcvm->argc = old_argc;
  memcpy(&qcvm->globals[OFS_PARM0], old_parms, sizeof(old_parms));
  memcpy(&qcvm->globals[OFS_RETURN], old_return, sizeof(old_return));
  return !ent->free && SV_CoopIsActiveClient(ent);
}

/* The supported QBJ3 weapon selector is the only safe way to make a retained
 * selected weapon and its viewmodel agree after rejecting a stale fist model. */
static qboolean SV_CoopRespawnRefreshQBJ3WeaponModel(edict_t *ent) {
  dfunction_t *func;
  float old_parms[MAX_PARMS * 3], old_return[3], old_time;
  int old_self, old_other, old_argc;

  if (!ent || ent->free || !SV_VRMeleeQBJ3Progs() ||
      !(func = SV_CoopRespawnFindFunction("W_ChangeWeapon", 2)) ||
      func->parm_size[0] != 1 || func->parm_size[1] != 1)
    return false;

  old_self = pr_global_struct->self;
  old_other = pr_global_struct->other;
  old_time = pr_global_struct->time;
  old_argc = qcvm->argc;
  memcpy(old_parms, &qcvm->globals[OFS_PARM0], sizeof(old_parms));
  memcpy(old_return, &qcvm->globals[OFS_RETURN], sizeof(old_return));

  pr_global_struct->self = EDICT_TO_PROG(ent);
  pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
  pr_global_struct->time = qcvm->time;
  qcvm->argc = 2;
  G_FLOAT(OFS_PARM0) = ent->v.weapon;
  G_FLOAT(OFS_PARM1) = 1.0f;
  PR_ExecuteProgram(func - qcvm->functions);

  pr_global_struct->self = old_self;
  pr_global_struct->other = old_other;
  pr_global_struct->time = old_time;
  qcvm->argc = old_argc;
  memcpy(&qcvm->globals[OFS_PARM0], old_parms, sizeof(old_parms));
  memcpy(&qcvm->globals[OFS_RETURN], old_return, sizeof(old_return));
  return !ent->free && SV_CoopIsActiveClient(ent);
}

static void SV_CoopRespawnRecoverStuckTeleportLimbo(edict_t *ent, int num) {
  qboolean flags_valid;
  eval_t *dest;
  int customflags;
  int index = num - 1;

  if (index < 0 || index >= MAX_SCOREBOARD)
    return;
  if (!coop.value || deathmatch.value || !ent || ent->free ||
      !SV_CoopRespawnHasQBJ3TeleportAPI()) {
    coop_respawn_limbo_since[index] = 0.0;
    return;
  }

  customflags = SV_CoopRespawnCustomFlags(ent, &flags_valid);
  if (!flags_valid || !(customflags & QBJ3_CFL_LIMBO)) {
    coop_respawn_limbo_since[index] = 0.0;
    return;
  }
  if (coop_respawn_limbo_since[index] <= 0.0) {
    coop_respawn_limbo_since[index] = qcvm->time;
    return;
  }

  /* QBJ3 retries a blocked destination every 100 ms and forces it after five
   * seconds.  If that lifecycle is still intact one second later, set the
   * mod's own force flag and run its normal limbo think once.  That completes
   * the destination teleport as well as restoring visibility/collision;
   * calling teleport_exit_limbo alone would leave the player on the source
   * trigger and could immediately enter limbo again. */
  if (qcvm->time - coop_respawn_limbo_since[index] < 6.0 ||
      ent->v.takedamage != DAMAGE_NO || ent->v.solid != SOLID_NOT ||
      ent->v.movetype != MOVETYPE_NONE)
    return;

  /* This is an engine-forced retry, so be more defensive than the ordinary
   * QuakeC path: a destination removed or repurposed while the player was in
   * limbo must not be dereferenced by teleport_limbo_think. */
  if (!SV_CoopRespawnHasValidTeleportGoal(ent)) {
    coop_respawn_limbo_since[index] = 0.0;
    return;
  }

  dest = SV_CoopRespawnGetTypedField(ent, "dest", ev_vector);
  if (!dest)
    return;
  dest->vector[2] = 1.0f;
  if (!SV_CoopRespawnCallSelfFunction(ent, "teleport_limbo_think"))
    return;
  customflags = SV_CoopRespawnCustomFlags(ent, &flags_valid);
  if (flags_valid && !(customflags & QBJ3_CFL_LIMBO)) {
    coop_respawn_limbo_since[index] = 0.0;
    if (net_lagdebug.value)
      Con_Printf("net_lagdebug: completed stuck QBJ3 teleport limbo for client %d\n",
                 num);
  }
}

static void SV_CoopRespawnFinishModLifecycle(edict_t *ent, int num) {
  qboolean flags_valid;
  int customflags;
  int index = num - 1;

  if (index < 0 || index >= MAX_SCOREBOARD ||
      !coop_respawn_mod_cleanup_pending[index])
    return;
  if (!coop.value || deathmatch.value || !ent || ent->free) {
    coop_respawn_mod_cleanup_pending[index] = false;
    return;
  }

  customflags = SV_CoopRespawnCustomFlags(ent, &flags_valid);
  if (!flags_valid) {
    coop_respawn_mod_cleanup_pending[index] = false;
    return;
  }
  if (!SV_CoopRespawnIsAliveClient(ent) ||
      (customflags & (QBJ3_CFL_PLUNGE | QBJ3_CFL_LIMBO)) != 0)
    return;

  /* A blocked coop spawn intentionally uses an opaque priority-110 limbo
   * layer.  Once QC exits limbo, clear only the orphaned priority-70 void-gib
   * layer.  If the mod already repaired it, simply retire the pending state. */
  if (!SV_CoopRespawnHasVoidCshift(ent)) {
    coop_respawn_mod_cleanup_pending[index] = false;
    return;
  }
  if (!SV_CoopRespawnCallEntityFunction(ent, "csf_clear_all"))
    return;
  if (!SV_CoopRespawnHasVoidCshift(ent)) {
    coop_respawn_mod_cleanup_pending[index] = false;
    if (net_lagdebug.value)
      Con_Printf("net_lagdebug: cleared QBJ3 void respawn cshift for client %d\n",
                 num);
  }
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
         IT_CELLS | IT_KEY1 | IT_KEY2 | IT_SIGIL1 | IT_SIGIL2 |
         IT_SIGIL3 | IT_SIGIL4;
  mask |= SV_DeclaredWeaponBits();

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

static int SV_CoopRespawnExtraSharedKeyMask(const char *name) {
  if (!name)
    return 0;
  if (!q_strcasecmp(name, "customkeys"))
    return COOP_RESPAWN_ALL_ITEM_BITS;
  if (!q_strcasecmp(name, "moditems"))
    return COOP_RESPAWN_DRAKE_CUSTOM_KEYS;
  if (!q_strcasecmp(name, "items2"))
    return COOP_RESPAWN_ITEMS2_KEY_BITS;
  return 0;
}

static qboolean SV_CoopRespawnExtraIsSharedKeyCount(const char *name) {
  return name && (!q_strcasecmp(name, "key_count_silver") ||
                  !q_strcasecmp(name, "key_count_gold"));
}

static qboolean SV_CoopRespawnExtraIsKeyMetadata(const char *name) {
  return name && (!q_strncasecmp(name, "ckeyname", 8) ||
                  !q_strncasecmp(name, "ckeyskin", 8));
}

void SV_CoopRespawnSyncSharedKeys(edict_t *source) {
  int i;
  int j;
  int source_items;
  qboolean counted_keys;

  if (!coop.value || !source || source->free)
    return;

  source_items = (int)source->v.items & COOP_RESPAWN_STOCK_KEY_BITS;
  counted_keys = SV_CoopUsesCountedKeys();

  for (i = 0; i < MAX_SCOREBOARD; i++) {
    coop_respawn_inventory_t *inventory;

    if (!coop_respawn_last_inventory_valid[i])
      continue;

    inventory = &coop_respawn_last_inventory[i];
    inventory->items =
        (inventory->items & ~COOP_RESPAWN_STOCK_KEY_BITS) | source_items;

    for (j = 0; j < COOP_RESPAWN_EXTRA_COUNT; j++) {
      const coop_respawn_extra_field_t *field = &coop_respawn_extra_fields[j];
      eval_t *val;
      int type;

      val = SV_CoopRespawnGetExtraField(source, j, &type);
      if (!val)
        continue;

      if (SV_CoopRespawnExtraIsKeyMetadata(field->name)) {
        inventory->extra_valid[j] = true;
        if (field->policy == COOP_RESPAWN_EXTRA_STRING)
          inventory->extra_string[j] = val->string;
        else
          inventory->extra_value[j] = val->_float;
      } else if (field->policy == COOP_RESPAWN_EXTRA_BITMASK) {
        int key_mask = SV_CoopRespawnExtraSharedKeyMask(field->name);
        int source_bits;

        if (!key_mask)
          continue;

        source_bits = type == ev_ext_integer ? val->_int : (int)val->_float;
        inventory->extra_valid[j] = true;
        inventory->extra_bits[j] =
            (inventory->extra_bits[j] & ~key_mask) | (source_bits & key_mask);
      } else if (counted_keys &&
                 SV_CoopRespawnExtraIsSharedKeyCount(field->name)) {
        inventory->extra_valid[j] = true;
        inventory->extra_value[j] = val->_float;
      } else if (counted_keys && !q_strcasecmp(field->name, "worldtype")) {
        int source_bits =
            type == ev_ext_integer ? val->_int : (int)val->_float;
        int existing_bits =
            inventory->extra_valid[j] ? (int)inventory->extra_value[j] : 0;

        inventory->extra_valid[j] = true;
        inventory->extra_value[j] =
            (float)((existing_bits & ~COOP_RESPAWN_WORLDTYPE_KEY_MASK) |
                    (source_bits & COOP_RESPAWN_WORLDTYPE_KEY_MASK));
      }
    }
  }
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

static void SV_CoopRespawnMergeInventory(
    coop_respawn_inventory_t *dst, const coop_respawn_inventory_t *src) {
  int i;

  dst->items |= src->items;
  dst->ammo_shells =
      SV_CoopRespawnMaxFloat(dst->ammo_shells, src->ammo_shells);
  dst->ammo_nails = SV_CoopRespawnMaxFloat(dst->ammo_nails, src->ammo_nails);
  dst->ammo_rockets =
      SV_CoopRespawnMaxFloat(dst->ammo_rockets, src->ammo_rockets);
  dst->ammo_cells = SV_CoopRespawnMaxFloat(dst->ammo_cells, src->ammo_cells);
  dst->currentammo =
      SV_CoopRespawnMaxFloat(dst->currentammo, src->currentammo);

  if (dst->weapon <= 0 && src->weapon > 0)
    dst->weapon = src->weapon;
  if (!dst->weaponmodel && src->weaponmodel)
    dst->weaponmodel = src->weaponmodel;

  for (i = 0; i < COOP_RESPAWN_EXTRA_COUNT; i++) {
    if (!src->extra_valid[i])
      continue;

    if (!dst->extra_valid[i]) {
      dst->extra_valid[i] = true;
      dst->extra_bits[i] = src->extra_bits[i];
      dst->extra_value[i] = src->extra_value[i];
      dst->extra_string[i] = src->extra_string[i];
      continue;
    }

    if (coop_respawn_extra_fields[i].policy == COOP_RESPAWN_EXTRA_BITMASK) {
      dst->extra_bits[i] |= src->extra_bits[i];
    } else if (coop_respawn_extra_fields[i].policy ==
               COOP_RESPAWN_EXTRA_STRING) {
      if (!dst->extra_string[i] && src->extra_string[i])
        dst->extra_string[i] = src->extra_string[i];
    } else if (coop_respawn_extra_fields[i].policy !=
               COOP_RESPAWN_EXTRA_RESTORE_FLOAT) {
      dst->extra_value[i] =
          SV_CoopRespawnMaxFloat(dst->extra_value[i], src->extra_value[i]);
    }
  }
}

/* QBJ3's temporary berserk is deliberately cleared by its PutClientInServer
 * path. Do not put the corpse's fist viewmodel back after that fresh state,
 * or the client will render immersive fists despite the expired powerup. */
static qboolean SV_CoopRespawnRestoreWeaponModel(edict_t *ent,
                                                  string_t weaponmodel) {
  return !SV_VRMeleeQBJ3Progs() ||
         strcmp(PR_GetString(weaponmodel), "progs/v_berserk.mdl") ||
         SV_VRMeleeQBJ3BerserkActive(ent);
}

static void SV_CoopRespawnRestoreInventory(
    edict_t *ent, const coop_respawn_inventory_t *inventory) {
  int i;
  int type;
  eval_t *val;
  qboolean refresh_weaponmodel = false;

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
  if (inventory->weaponmodel) {
    if (SV_CoopRespawnRestoreWeaponModel(ent, inventory->weaponmodel))
      ent->v.weaponmodel = inventory->weaponmodel;
    else
      refresh_weaponmodel = true;
  }

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

  if (refresh_weaponmodel)
    SV_CoopRespawnRefreshQBJ3WeaponModel(ent);
}

/* A loaded dead player has already passed through the mod's normal
 * PutClientInServer path. That fresh state is an entrance default, not a
 * second inventory source: merging it with the serialized projection turns a
 * saved 10 rockets into a default 100. Restore only the typed inventory
 * subset captured above, leaving callbacks, health, movement, references,
 * and other freshly initialized QC state alone. */
static void SV_CoopRespawnRestoreSavedInventoryExact(
    edict_t *ent, const coop_respawn_inventory_t *inventory) {
  int i;
  int type;
  int mask;
  eval_t *val;
  float fresh_weapon = ent->v.weapon;
  string_t fresh_weaponmodel = ent->v.weaponmodel;
  string_t weaponmodel = inventory->weaponmodel;
  qboolean refresh_weaponmodel = false;

  /* An old projection can select an owned weapon without a model. A fresh
   * model is safe only for that exact same selected weapon; otherwise clear it
   * rather than display an incompatible starter model. */
  if (inventory->weapon > 0 && (inventory->items & (int)inventory->weapon) &&
      !weaponmodel) {
    if (fresh_weapon == inventory->weapon && fresh_weaponmodel)
      weaponmodel = fresh_weaponmodel;
  }
  if (weaponmodel &&
      !SV_CoopRespawnRestoreWeaponModel(ent, weaponmodel)) {
    weaponmodel = fresh_weaponmodel;
    refresh_weaponmodel = true;
  }

  mask = SV_CoopRespawnKeepItemMask();
  ent->v.items = ((int)ent->v.items & ~mask) | (inventory->items & mask);
  ent->v.ammo_shells = inventory->ammo_shells;
  ent->v.ammo_nails = inventory->ammo_nails;
  ent->v.ammo_rockets = inventory->ammo_rockets;
  ent->v.ammo_cells = inventory->ammo_cells;
  ent->v.currentammo = inventory->currentammo;
  ent->v.weapon = inventory->weapon;
  ent->v.weaponmodel = weaponmodel;

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
      mask = coop_respawn_extra_fields[i].mask;
      if (type == ev_ext_integer)
        val->_int = (val->_int & ~mask) | (inventory->extra_bits[i] & mask);
      else
        val->_float = ((int)val->_float & ~mask) |
                      (inventory->extra_bits[i] & mask);
    } else {
      /* MAXFLOAT fields are exact here too: a saved zero custom ammo value
       * must not inherit a nonzero entrance default. */
      val->_float = inventory->extra_value[i];
    }
  }

  if (refresh_weaponmodel)
    SV_CoopRespawnRefreshQBJ3WeaponModel(ent);
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

void SV_CoopRespawnRefreshClientInventory(edict_t *ent) {
  int num;

  if (!ent || ent->free)
    return;

  num = NUM_FOR_EDICT(ent);
  if (num < 1 || num > svs.maxclients)
    return;

  SV_CoopRespawnRememberAliveInventory(ent, num);
}

void SV_CoopRespawnSaveClientEdict(edict_t *ent, edict_t *snapshot) {
  int index = NUM_FOR_EDICT(ent) - 1;
  coop_respawn_inventory_t inventory, current;

  /* This projection is for serialization only.  Never run transition QC or
     revive/mutate the live corpse to obtain inventory for a co-op save. */
  memcpy(snapshot, ent, qcvm->edict_size);
  if (!coop.value ||
      !SV_CoopFeatureEnabled(&sv_coop_respawn_keep_weapons_ammo, true) ||
      !SV_CoopIsDeadClient(ent) || index < 0 || index >= MAX_SCOREBOARD ||
      !coop_respawn_last_inventory_valid[index])
    return;

  inventory = coop_respawn_last_inventory[index];
  SV_CoopRespawnSaveInventory(ent, &current);
  SV_CoopRespawnMergeInventory(&inventory, &current);
  SV_CoopRespawnRestoreInventory(snapshot, &inventory);
  /* The ordinary respawn helper maps currentammo to reserve ammo.  Saved
     projections must retain the cached magazine/current-ammo value exactly. */
  snapshot->v.currentammo = inventory.currentammo;
}

void SV_CoopRespawnRestoreSavedInventory(edict_t *ent, edict_t *snapshot) {
  coop_respawn_inventory_t inventory;

  if (!ent || ent->free || !snapshot || snapshot->free)
    return;

  /* The detached saved edict has no live client index.  Transfer only the
     same typed inventory subset as normal respawn; leave the fresh mod's
     callbacks, movement, health and entity references initialized by QC. */
  if (coop.value &&
      SV_CoopFeatureEnabled(&sv_coop_respawn_keep_weapons_ammo, true)) {
    SV_CoopRespawnSaveInventory(snapshot, &inventory);
    SV_CoopRespawnRestoreSavedInventoryExact(ent, &inventory);
  }
  /* Key sharing is independent of optional weapon retention. Pending saved
     players must receive current team keys before physics, not stale keys. */
  SV_CoopSharedApplyToJoiningClient(ent);
}

qboolean SV_CoopRespawnPrepareChangelevel(edict_t *ent) {
  coop_respawn_inventory_t current;
  coop_respawn_inventory_t inventory;
  int index;
  int num;

  if (!coop.value ||
      !SV_CoopFeatureEnabled(&sv_coop_respawn_keep_weapons_ammo, true) ||
      !SV_CoopIsDeadClient(ent))
    return false;

  num = NUM_FOR_EDICT(ent);
  index = num - 1;
  if (index < 0 || index >= MAX_SCOREBOARD)
    return false;

  if (!coop_respawn_last_inventory_valid[index])
    return false;

  /* Death code is allowed to clear inventory before a changelevel.  Rebuild
     the outgoing player from the last alive snapshot plus anything still on
     the corpse, then let the mod's SetChangeParms encode its own supported
     inventory fields. */
  inventory = coop_respawn_last_inventory[index];
  SV_CoopRespawnSaveInventory(ent, &current);
  SV_CoopRespawnMergeInventory(&inventory, &current);
  SV_CoopRespawnRestoreInventory(ent, &inventory);

  return true;
}

static void SV_CoopRespawnRememberSafeOrigin(edict_t *ent, int num) {
  int index;

  if (!coop.value || !SV_CoopRespawnIsAliveClient(ent))
    return;

  index = num - 1;
  if (index < 0 || index >= MAX_SCOREBOARD)
    return;

  if (ent->v.waterlevel > 0)
    return;

  if (!SV_CoopRespawnCanPlaceAtDry(ent, ent->v.origin))
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
  vec3_t death_origin;

  if (!coop.value || !ent || ent->free)
    return;

  index = num - 1;
  if (index < 0 || index >= MAX_SCOREBOARD)
    return;

  VectorCopy(state->death_origin, death_origin);
  if (SV_CoopRespawnCanPlaceAtDry(ent, death_origin)) {
    VectorCopy(state->death_origin, coop_respawn_death_anchor[index]);
    VectorCopy(state->death_angles, coop_respawn_death_angles[index]);
    VectorCopy(state->death_v_angle, coop_respawn_death_v_angle[index]);
  } else if (coop_respawn_last_safe_valid[index]) {
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

static void SV_CoopRespawnBeginFrameDeathTracking(void) {
  int i;

  memset(coop_respawn_frame_started_alive, 0,
         sizeof(coop_respawn_frame_started_alive));
  memset(coop_respawn_frame_death_handled, 0,
         sizeof(coop_respawn_frame_death_handled));
  memset(coop_respawn_frame_predeath_state, 0,
         sizeof(coop_respawn_frame_predeath_state));

  if (qcvm != &sv.qcvm || !coop.value)
    return;

  for (i = 1; i <= svs.maxclients && i <= MAX_SCOREBOARD; ++i) {
    edict_t *ent = EDICT_NUM(i);
    coop_respawn_postthink_state_t *state =
        &coop_respawn_frame_predeath_state[i - 1];

    if (!SV_CoopRespawnIsAliveClient(ent))
      continue;

    coop_respawn_frame_started_alive[i - 1] = true;
    state->old_force_retouch = pr_global_struct->force_retouch;
    VectorCopy(ent->v.origin, state->death_origin);
    VectorCopy(ent->v.angles, state->death_angles);
    VectorCopy(ent->v.v_angle, state->death_v_angle);
  }
}

static void SV_CoopRespawnHandleDeathTransition(
    edict_t *ent, int num, const coop_respawn_postthink_state_t *state) {
  int index = num - 1;

  if (!coop.value || !ent || ent->free || !state || index < 0 ||
      index >= MAX_SCOREBOARD ||
      coop_respawn_frame_death_handled[index])
    return;

  coop_respawn_frame_death_handled[index] = true;
  SV_CoopSharedReconcileClientDeath(ent);
  SV_CoopRespawnRecordDeathAnchor(ent, num, state);
  if (!SV_CoopRespawnAnyAliveClient())
    SV_CoopRespawnMarkTeamWipe();
}

static void SV_CoopRespawnEndFrameDeathTracking(void) {
  int i;

  if (qcvm != &sv.qcvm || !coop.value)
    return;

  for (i = 1; i <= svs.maxclients && i <= MAX_SCOREBOARD; ++i) {
    edict_t *ent;

    if (!coop_respawn_frame_started_alive[i - 1] ||
        coop_respawn_frame_death_handled[i - 1])
      continue;

    ent = EDICT_NUM(i);
    if (!SV_CoopIsDeadClient(ent))
      continue;

    SV_CoopRespawnHandleDeathTransition(
        ent, i, &coop_respawn_frame_predeath_state[i - 1]);
  }
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

static qboolean SV_CoopRespawnPointContentsOK(vec3_t origin, edict_t *ent,
                                              qboolean allow_water) {
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
    if (!allow_water && cont == CONTENTS_WATER)
      return false;
  }

  return true;
}

static qboolean SV_CoopRespawnTriggerLooksHazard(edict_t *touch) {
  const char *classname;

  if (!touch || touch->free || touch->v.solid != SOLID_TRIGGER ||
      !touch->v.touch)
    return false;

  classname = touch->v.classname ? PR_GetString(touch->v.classname) : "";
  if (!classname || !classname[0])
    return false;

  return q_strcasestr(classname, "hurt") ||
         q_strcasestr(classname, "kill") ||
         q_strcasestr(classname, "void") ||
         q_strcasestr(classname, "death") ||
         q_strcasestr(classname, "lava") ||
         q_strcasestr(classname, "slime");
}

static qboolean SV_CoopRespawnTouchesHazardTrigger(edict_t *ent,
                                                   vec3_t origin) {
  int i;
  vec3_t mins, maxs;

  VectorAdd(origin, ent->v.mins, mins);
  VectorAdd(origin, ent->v.maxs, maxs);

  for (i = svs.maxclients + 1; i < qcvm->num_edicts; i++) {
    edict_t *touch = EDICT_NUM(i);

    if (!SV_CoopRespawnTriggerLooksHazard(touch))
      continue;
    if (mins[0] > touch->v.absmax[0] || mins[1] > touch->v.absmax[1] ||
        mins[2] > touch->v.absmax[2] || maxs[0] < touch->v.absmin[0] ||
        maxs[1] < touch->v.absmin[1] || maxs[2] < touch->v.absmin[2])
      continue;
    return true;
  }

  return false;
}

static qboolean SV_CoopRespawnCanPlaceAt(edict_t *ent, vec3_t origin) {
  qboolean bottom;
  trace_t trace;
  vec3_t old_origin;

  if (!SV_CoopRespawnPointContentsOK(origin, ent, true))
    return false;

  trace = SV_Move(origin, ent->v.mins, ent->v.maxs, origin, MOVE_NORMAL, ent);
  if (trace.allsolid || trace.startsolid)
    return false;
  if (SV_CoopRespawnTouchesHazardTrigger(ent, origin))
    return false;

  VectorCopy(ent->v.origin, old_origin);
  VectorCopy(origin, ent->v.origin);
  bottom = SV_CheckBottom(ent);
  VectorCopy(old_origin, ent->v.origin);

  return bottom;
}

static qboolean SV_CoopRespawnCanPlaceAtDry(edict_t *ent, vec3_t origin) {
  qboolean bottom;
  trace_t trace;
  vec3_t old_origin;

  if (!SV_CoopRespawnPointContentsOK(origin, ent, false))
    return false;

  trace = SV_Move(origin, ent->v.mins, ent->v.maxs, origin, MOVE_NORMAL, ent);
  if (trace.allsolid || trace.startsolid)
    return false;
  if (SV_CoopRespawnTouchesHazardTrigger(ent, origin))
    return false;

  VectorCopy(ent->v.origin, old_origin);
  VectorCopy(origin, ent->v.origin);
  bottom = SV_CheckBottom(ent);
  VectorCopy(old_origin, ent->v.origin);

  return bottom;
}

static qboolean SV_CoopRespawnDropToFloor(edict_t *ent, vec3_t origin,
                                          float max_drop,
                                          qboolean allow_water,
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
    if ((allow_water && SV_CoopRespawnCanPlaceAt(ent, floor_origin)) ||
        (!allow_water && SV_CoopRespawnCanPlaceAtDry(ent, floor_origin)))
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
                                             qboolean allow_water,
                                             vec3_t spot) {
  int i, j;
  vec3_t candidate, dropped, forward, right;
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

      if (!SV_CoopRespawnDropToFloor(ent, candidate, max_drop, allow_water,
                                     dropped))
        continue;

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
            (int)(sizeof(player_radii) / sizeof(player_radii[0])), 384.0f, true,
            spot)) {
      if (anchor_out)
        *anchor_out = candidates[i].ent;
      return true;
    }
  }

  return false;
}

static qboolean SV_CoopRespawnFindDeathSpot(edict_t *ent,
                                            const vec3_t death_origin,
                                            edict_t **anchor_out,
                                            vec3_t spot) {
  static const float death_radii[] = {0.0f, 40.0f, 64.0f, 96.0f, 128.0f,
                                      160.0f, 192.0f, 224.0f, 256.0f};

  if (SV_CoopRespawnFindNearbySpot(
          ent, death_origin, NULL, death_radii,
          (int)(sizeof(death_radii) / sizeof(death_radii[0])), 96.0f, false,
          spot)) {
    if (anchor_out)
      *anchor_out = NULL;
    return true;
  }

  return false;
}

static qboolean SV_CoopRespawnFindSpot(edict_t *ent, const vec3_t death_origin,
                                       edict_t **anchor_out, vec3_t spot,
                                       qboolean allow_teammate_fallback) {
  if (SV_CoopRespawnFindDeathSpot(ent, death_origin, anchor_out, spot))
    return true;

  if (allow_teammate_fallback &&
      SV_CoopRespawnFindAnchorSpot(ent, death_origin, anchor_out, spot))
    return true;

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

  if (!coop.value ||
      !SV_CoopFeatureEnabled(&sv_coop_respawn_near_player, true) || !ent ||
      ent->free)
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

  if (!SV_CoopRespawnFindAnchorSpot(ent, state.death_origin, &anchor, spot))
    return false;

  SV_CoopRespawnRelocate(ent, anchor, spot, &state);
  return true;
}

qboolean SV_CoopRespawnTeleportToPlayer(edict_t *ent, edict_t *target) {
  edict_t *anchor;
  vec3_t spot;
  coop_respawn_postthink_state_t state;
  static const float player_radii[] = {0.0f, 40.0f, 48.0f, 64.0f,
                                       80.0f, 96.0f, 128.0f};

  if (!coop.value || deathmatch.value || !ent || ent->free || !target ||
      target->free || ent == target)
    return false;
  if (!SV_CoopRespawnIsAliveClient(ent) ||
      !SV_CoopRespawnIsAliveClient(target))
    return false;

  anchor = target;
  if (!SV_CoopRespawnFindNearbySpot(
          ent, target->v.origin, target, player_radii,
          (int)(sizeof(player_radii) / sizeof(player_radii[0])), 384.0f, true,
          spot)) {
    if (!SV_CoopFeatureEnabled(&sv_coop_player_teleport_fallback, true) ||
        !SV_CoopFeatureEnabled(&sv_coop_noplayerclip, true) ||
        !SV_CoopFeatureEnabled(&sv_coop_notelefrag, true))
      return false;

    /* An explicit player teleport is allowed to fall back to the teammate's
     * exact origin. With player collision and co-op telefrags disabled this
     * remains usable even in maps whose first room has no conservative hull
     * candidate. The safe search above is still always preferred. */
    VectorCopy(target->v.origin, spot);
  }

  memset(&state, 0, sizeof(state));
  state.old_force_retouch = pr_global_struct->force_retouch;
  VectorCopy(ent->v.origin, state.death_origin);
  VectorCopy(ent->v.angles, state.death_angles);
  VectorCopy(ent->v.v_angle, state.death_v_angle);

  SV_CoopRespawnRelocate(ent, anchor, spot, &state);
  return true;
}

qboolean SV_CoopRespawnTeleportToSpawn(edict_t *ent, edict_t *spawn) {
  vec3_t base, spot;
  coop_respawn_postthink_state_t state;
  static const float spawn_radii[] = {0.0f, 32.0f, 48.0f, 64.0f,
                                      80.0f, 96.0f, 128.0f};

  if (!coop.value || deathmatch.value || !ent || ent->free || !spawn ||
      spawn->free || !SV_CoopRespawnIsAliveClient(ent))
    return false;

  VectorCopy(spawn->v.origin, base);
  base[2] += 1.0f;
  if (!SV_CoopRespawnFindNearbySpot(
          ent, base, spawn, spawn_radii,
          (int)(sizeof(spawn_radii) / sizeof(spawn_radii[0])), 128.0f, true,
          spot))
    return false;

  memset(&state, 0, sizeof(state));
  state.old_force_retouch = pr_global_struct->force_retouch;
  VectorCopy(ent->v.origin, state.death_origin);
  VectorCopy(ent->v.angles, state.death_angles);
  VectorCopy(ent->v.v_angle, state.death_v_angle);

  SV_CoopRespawnRelocate(ent, spawn, spot, &state);
  return true;
}

static void SV_CoopRespawnBeginPostThink(
    edict_t *ent, int num, coop_respawn_postthink_state_t *state) {
  int index;
  double dead_time;

  memset(state, 0, sizeof(*state));
  state->was_dead = SV_CoopIsDeadClient(ent);
  if (coop.value && !state->was_dead)
    SV_CoopRespawnRememberAliveState(ent, num);
  state->mod_owns_respawn = SV_CoopRespawnModOwnsLifecycle(ent);
  if (state->mod_owns_respawn) {
    index = num - 1;
    if (state->was_dead && index >= 0 && index < MAX_SCOREBOARD)
      coop_respawn_mod_cleanup_pending[index] = true;
    return;
  }
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
    return;
  }

  if (!SV_CoopRespawnAnyAliveClient())
    SV_CoopRespawnMarkTeamWipe();
  state->force_standard_spawn = coop_respawn_force_standard_spawn[index];

  SV_CoopRespawnUseDeathAnchor(num, state);

  if (coop_respawn_last_inventory_valid[index]) {
    coop_respawn_inventory_t current_inventory;

    state->inventory = coop_respawn_last_inventory[index];
    SV_CoopRespawnSaveInventory(ent, &current_inventory);
    SV_CoopRespawnMergeInventory(&state->inventory, &current_inventory);
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

  SV_CoopRespawnRecoverStuckTeleportLimbo(ent, num);
  SV_CoopRespawnFinishModLifecycle(ent, num);
  if (state->mod_owns_respawn)
    return;

  if (!coop.value) {
    SV_CoopRespawnRestoreSuppressedInput(ent, num, state);
    return;
  }

  if (!state->was_dead && SV_CoopIsDeadClient(ent)) {
    SV_CoopRespawnHandleDeathTransition(ent, num, state);
    SV_CoopRespawnRestoreSuppressedInput(ent, num, state);
    return;
  }

  if (state->was_dead && SV_CoopRespawnIsAliveClient(ent)) {
    if (SV_CoopFeatureEnabled(&sv_coop_respawn_keep_weapons_ammo, true) &&
        state->inventory_valid)
      SV_CoopRespawnRestoreInventory(ent, &state->inventory);

    if (SV_CoopFeatureEnabled(&sv_coop_respawn_near_player, true) &&
        !state->force_standard_spawn) {
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
  qboolean ff_scope;

  thinktime = ent->v.nextthink;
  if (thinktime <= 0 || thinktime > qcvm->time + qcvm->frametime)
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

  ff_scope = SV_FriendlyFireBegin(ent);
  PR_ExecuteProgram(ent->v.think);
  if (ff_scope)
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
  qboolean coop_touch_sync;
  qboolean ff_scope;

  if (!e1 || !e2 || e1->free || e2->free)
    return;

  old_self = pr_global_struct->self;
  old_other = pr_global_struct->other;

  pr_global_struct->time = qcvm->time;
  SV_DebugLogTriggerImpact(e1, e2);
  SV_DebugLogTriggerImpact(e2, e1);

  if (e1->v.touch && e1->v.solid != SOLID_NOT &&
      !SV_ShouldSuppressCoopTelefrag(e1, e2)) {
    coop_touch_sync = SV_CoopSharedBeginClientTouch(e2);
    pr_global_struct->self = EDICT_TO_PROG(e1);
    pr_global_struct->other = EDICT_TO_PROG(e2);
    ff_scope = SV_FriendlyFireBegin(e1);
    PR_ExecuteProgram(e1->v.touch);
    if (ff_scope)
      SV_FriendlyFireEnd();
    if (coop_touch_sync && !e2->free)
      SV_CoopSharedEndClientTouch(e2);
  }

  if (!e1->free && !e2->free && e2->v.touch && e2->v.solid != SOLID_NOT &&
      !SV_ShouldSuppressCoopTelefrag(e2, e1)) {
    coop_touch_sync = SV_CoopSharedBeginClientTouch(e1);
    pr_global_struct->self = EDICT_TO_PROG(e2);
    pr_global_struct->other = EDICT_TO_PROG(e1);
    ff_scope = SV_FriendlyFireBegin(e2);
    PR_ExecuteProgram(e2->v.touch);
    if (ff_scope)
      SV_FriendlyFireEnd();
    if (coop_touch_sync && !e1->free)
      SV_CoopSharedEndClientTouch(e1);
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
static int SV_FlyMoveInternal(edict_t *ent, float time, trace_t *steptrace,
                              qboolean touch) {
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
    if (touch)
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

int SV_FlyMove(edict_t *ent, float time, trace_t *steptrace) {
  return SV_FlyMoveInternal(ent, time, steptrace, true);
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

  ent->v.velocity[2] -= ent_gravity * sv_gravity.value * qcvm->frametime;
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
static trace_t SV_PushEntityInternal(edict_t *ent, vec3_t push, qboolean touch) {
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
  SV_LinkEdict(ent, touch);

  if (touch && trace.ent)
    SV_Impact(ent, trace.ent);

  return trace;
}

trace_t SV_PushEntity(edict_t *ent, vec3_t push) {
  return SV_PushEntityInternal(ent, push, true);
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
  float solid_backup;

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
      if (pusher->v.skin < 0) {
        if (!SV_ClipMoveToEntity(pusher, check->v.origin, check->v.mins,
                                 check->v.maxs, check->v.origin)
                 .startsolid)
          continue;
      } else {
        if (!SV_TestEntityPosition(check))
          continue;
      }

      riding = false;
    } else
      riding = true;

    // remove the onground flag for non-players
    if (check->v.movetype != MOVETYPE_WALK)
      if (!pr_checkextension.value ||
          PROG_TO_EDICT(check->v.groundentity) != pusher)
        check->v.flags = (int)check->v.flags & ~FL_ONGROUND;

    VectorCopy(check->v.origin, entorig);
    VectorCopy(check->v.origin, moved_from[num_moved]);
    moved_edict[num_moved] = check;
    num_moved++;

    solid_backup = pusher->v.solid;
    if (solid_backup == SOLID_BSP || solid_backup == SOLID_BBOX ||
        solid_backup == SOLID_SLIDEBOX) {
      // try moving the contacted entity
      pusher->v.solid = SOLID_NOT;
      SV_PushEntity(check, move);

      // if it is still inside the pusher, block
      if (pusher->v.skin < 0) {
        block = SV_TestEntityPosition(check);
        pusher->v.solid = solid_backup;
      } else {
        pusher->v.solid = solid_backup;
        block = SV_TestEntityPosition(check);
      }
    } else
      block = NULL;

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
        if (!SV_TestEntityPosition(check)) {
          if (sv.mapchecks.active) {
            vec3_t check_center, pusher_center;

            VectorAdd(check->v.absmin, check->v.absmax, check_center);
            VectorScale(check_center, 0.5f, check_center);
            VectorAdd(pusher->v.absmin, pusher->v.absmax, pusher_center);
            VectorScale(pusher_center, 0.5f, pusher_center);
            Con_Warning(
                "sv_gameplayfix_elevators nudged %s #%d at (%.0f %.0f %.0f) above %s #%d at (%.0f %.0f %.0f)\n",
                PR_GetString(check->v.classname), NUM_FOR_EDICT(check),
                check_center[0], check_center[1], check_center[2],
                PR_GetString(pusher->v.classname), NUM_FOR_EDICT(pusher),
                pusher_center[0], pusher_center[1], pusher_center[2]);
          }
          /* SV_PushEntity already evaluated triggers for this movement
           * transaction.  Relink the epsilon-adjusted final position without
           * firing arbitrary mod trigger callbacks a second time. */
          SV_LinkEdict(check, false);
          continue;
        }
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
  if (thinktime < ent->v.ltime + qcvm->frametime) {
    movetime = thinktime - ent->v.ltime;
    if (movetime < 0)
      movetime = 0;
  } else
    movetime = qcvm->frametime;

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

static qboolean SV_EntityOnLadder(edict_t *ent) {
  eval_t *val;

  if (!ent)
    return false;
  val = GetEdictFieldValue(ent, qcvm->extfields.onladder);
  return val && val->_float != 0;
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
static int SV_TryUnstick(edict_t *ent, vec3_t oldvel, qboolean touch) {
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

    SV_PushEntityInternal(ent, dir, touch);

    // retry the original move
    ent->v.velocity[0] = oldvel[0];
    ent->v.velocity[1] = oldvel[1];
    ent->v.velocity[2] = 0;
    clip = SV_FlyMoveInternal(ent, 0.1, &steptrace, touch);

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
static void SV_WalkMoveInternal(edict_t *ent, qboolean touch) {
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

  clip = SV_FlyMoveInternal(ent, qcvm->frametime, &steptrace, touch);

  if (!(clip & 2))
    return; // move didn't block on a step

  if (!oldonground && ent->v.waterlevel == 0)
    return; // don't stair up while jumping

  if (ent->v.movetype != MOVETYPE_WALK)
    return; // gibbed by a trigger

  if (sv_nostep.value)
    return;

  if ((int)ent->v.flags & FL_WATERJUMP)
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
  downmove[2] = -STEPSIZE + oldvel[2] * qcvm->frametime;

  // move up
  SV_PushEntityInternal(ent, upmove, touch); // FIXME: don't link?

  // move forward
  ent->v.velocity[0] = oldvel[0];
  ent->v.velocity[1] = oldvel[1];
  ent->v.velocity[2] = 0;
  clip = SV_FlyMoveInternal(ent, qcvm->frametime, &steptrace, touch);

  // check for stuckness, possibly due to the limited precision of floats
  // in the clipping hulls
  if (clip) {
    if (fabs(oldorg[1] - ent->v.origin[1]) < 0.03125 &&
        fabs(oldorg[0] - ent->v.origin[0]) <
            0.03125) { // stepping up didn't make any progress
      clip = SV_TryUnstick(ent, oldvel, touch);
    }
  }

  // extra friction based on view angle
  if (clip & 2)
    SV_WallFriction(ent, &steptrace);

  // move down
  downtrace = SV_PushEntityInternal(ent, downmove, touch); // FIXME: don't link?

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

void SV_WalkMove(edict_t *ent) {
  SV_WalkMoveInternal(ent, true);
}

// Replace player origin with hand muzzle position for the duration of
// PlayerPostThink (where QuakeC fires weapons).
//
// Network clients send the muzzle relative to their presented player origin.
// Reconstructing it from the authoritative server origin prevents prediction
// error from moving shots behind the player.  The trace also keeps a hand near
// a wall from placing a projectile on the far side of solid geometry.
static void SV_ClampVRMuzzleToWorld(edict_t *ent, vec3_t muzzle) {
  vec3_t start, delta;
  trace_t trace;
  int i;

  VectorAdd(ent->v.origin, ent->v.view_ofs, start);
  VectorSubtract(muzzle, start, delta);
  for (i = 0; i < 3; ++i) {
    if (!isfinite(muzzle[i])) {
      VectorCopy(start, muzzle);
      return;
    }
  }
  if (VectorLength(delta) > 512.0f) {
    VectorCopy(start, muzzle);
    return;
  }

  trace = SV_Move(start, vec3_origin, vec3_origin, muzzle, MOVE_NOMONSTERS,
                  ent);
  if (trace.startsolid || trace.allsolid) {
    VectorCopy(start, muzzle);
  } else if (trace.fraction < 1.0f) {
    VectorCopy(trace.endpos, muzzle);
    if (VectorNormalize(delta) > 0.0f)
      VectorMA(muzzle, -1.0f, delta, muzzle);
  }
}

typedef struct sv_akimbo_context_s {
  edict_t *ent;
  qboolean berserk;
  qboolean enyo;
  qboolean dwell;
  qboolean enyo_makevectors;
  qboolean enyo_clearance_pending;
  qboolean qbj3_shotgun_spread;
  vec3_t body_origin;
  vec3_t muzzle[2];
  vec3_t angles[2];
  float qbj3_shotgun_roll;
  vec3_t enyo_clearance_start;
  vec3_t enyo_clearance_end;
  vec3_t enyo_clearance_adjusted_start;
  float enyo_clearance_t0;
} sv_akimbo_context_t;

static sv_akimbo_context_t sv_akimbo_context;

void SV_ClearAkimboContext(void) {
  memset(&sv_akimbo_context, 0, sizeof(sv_akimbo_context));
}

qboolean SV_QBJ3AkimboSupported(void) {
  return sv_akimbo.value && qcvm == &sv.qcvm &&
      !q_strcasecmp(COM_SkipPath(com_gamedir), "qbj3") &&
      ED_FindFunction("W_FireTwinNailgun") != NULL;
}

qboolean SV_QBJ3BerserkAkimboSupported(void) {
  return sv_akimbo.value && qcvm == &sv.qcvm &&
      !q_strcasecmp(COM_SkipPath(com_gamedir), "qbj3") &&
      ED_FindFunction("W_Fire_Berserker_Multi") != NULL &&
      ED_FindFunction("weaponanim_berserk_loop") != NULL;
}

/* QBJ3's player shotgun invokes FireBullets, where SpreadVector uses the
 * current right/up globals.  Keep this independent from sv_akimbo: normal
 * one-handed shotguns need the same physical roll. */
static qboolean SV_QBJ3ShotgunWeapon(edict_t *ent) {
  return qcvm == &sv.qcvm && !q_strcasecmp(COM_SkipPath(com_gamedir), "qbj3") &&
      (ent->v.weapon == IT_SHOTGUN || ent->v.weapon == IT_SUPER_SHOTGUN);
}

/* This CRC is calculated over the same unmodified progs.dat bytes which
 * produced SHA-256 b0d3865f1192b3858e7410ea31cbc82d88136e635b9c1d13d0aff9bbeddaeb1e.
 * The function-layout checks below make the compact runtime pin specific to
 * the audited W_FireSMG bytecode rather than merely to a mod directory. */
#define ENYO_PROGS_CRC 22413
#define ENYO_W_FIRESMG_STATEMENT 15323
#define ENYO_W_FIRESMG_PARM_START 7335

static qboolean SV_EnyoSMGFunction(const dfunction_t *function) {
  return function && !strcmp(PR_GetString(function->s_name), "W_FireSMG") &&
      function->first_statement == ENYO_W_FIRESMG_STATEMENT &&
      function->parm_start == ENYO_W_FIRESMG_PARM_START &&
      function->locals == 7 && function->numparms == 1 &&
      function->parm_size[0] == 1;
}

qboolean SV_EnyoAkimboSupported(void) {
  dfunction_t *function;

  if (!sv_akimbo.value || qcvm != &sv.qcvm ||
      q_strcasecmp(COM_SkipPath(com_gamedir), "enyo") ||
      qcvm->crc != ENYO_PROGS_CRC)
    return false;
  function = ED_FindFunction("W_FireSMG");
  return SV_EnyoSMGFunction(function);
}

/* The Dwell root is commonly mounted directly as dwellv2p2, while older
 * launchers retain the dwell alias. Keep this allowlist exact. */
static qboolean SV_DwellGameDir(void) {
  const char *game = COM_SkipPath(com_gamedir);

  return !q_strcasecmp(game, "dwell") || !q_strcasecmp(game, "dwellv2p2");
}

/* CRC 505 is the loaded-byte pin for
 * fe7d21d4bdfd1a5e6672d1606efd774cb730d7f5b68941d82175596f22e719fd. */
#define DWELL_PROGS_CRC 505
#define DWELL_W_FIREAXE_STATEMENT 14560
#define DWELL_W_FIREAXE_PARM_START 7805

static qboolean SV_DwellFireAxeFunction(const dfunction_t *function) {
  return function && !strcmp(PR_GetString(function->s_name), "W_FireAxe") &&
      function->first_statement == DWELL_W_FIREAXE_STATEMENT &&
      function->parm_start == DWELL_W_FIREAXE_PARM_START &&
      function->locals == 9 && function->numparms == 0;
}

qboolean SV_DwellBerserkAkimboSupported(void) {
  ddef_t *finished;
  dfunction_t *function;

  if (!sv_akimbo.value || qcvm != &sv.qcvm || !SV_DwellGameDir() ||
      qcvm->crc != DWELL_PROGS_CRC)
    return false;
  finished = ED_FindField("berserk_finished");
  if (!finished || (finished->type & ~DEF_SAVEGLOBAL) != ev_float)
    return false;
  function = ED_FindFunction("W_FireAxe");
  return SV_DwellFireAxeFunction(function);
}

static qboolean SV_QBJ3BerserkWeapon(edict_t *ent) {
  return SV_QBJ3BerserkAkimboSupported() && ent->v.weapon == IT_AXE &&
      !strcmp(PR_GetString(ent->v.weaponmodel), "progs/v_berserk.mdl");
}

static qboolean SV_EnyoSMGWeapon(edict_t *ent) {
  return SV_EnyoAkimboSupported() && ent->v.weapon == 4 &&
      !strcmp(PR_GetString(ent->v.weaponmodel), "progs/ee_v_smgs.mdl");
}

static qboolean SV_DwellBerserkWeapon(edict_t *ent) {
  eval_t *finished;

  if (!SV_DwellBerserkAkimboSupported() || ent->v.weapon != 4096 ||
      strcmp(PR_GetString(ent->v.weaponmodel), "progs/v_axeb.mdl"))
    return false;
  finished = GetEdictFieldValueByName(ent, "berserk_finished");
  return finished && finished->_float > pr_global_struct->time;
}

static qboolean SV_AkimboVectorIsFinite(const vec3_t value) {
  return isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]);
}

static qboolean SV_AkimboVectorsNear(const vec3_t a, const vec3_t b) {
  vec3_t delta;

  VectorSubtract(a, b, delta);
  return DotProduct(delta, delta) <= 0.015625f; /* 1/8 unit endpoint slack */
}

/* Called only at QBJ3's aim builtin, after QC has selected its original
 * alternating fire frame. QC still owns ammunition, cadence and spawning. */
static qboolean SV_QBJ3AkimboAimLegacy(edict_t *ent, vec3_t muzzle) {
  int hand, i;
  float offs;
  vec3_t temporary_origin, source;

  if (!SV_QBJ3AkimboSupported() || sv_akimbo_context.ent != ent ||
      sv_akimbo_context.berserk ||
      !qcvm->xfunction ||
      strcmp(PR_GetString(qcvm->xfunction->s_name), "W_FireTwinNailgun") ||
      strcmp(PR_GetString(ent->v.weaponmodel), "progs/v_tnailgun.mdl") ||
      ent->v.weapon != 4 ||
      (ent->v.weaponframe != 11 && ent->v.weaponframe != 15))
    return false;

  hand = ent->v.weaponframe == 11 ? 1 : 0;
  offs = hand ? 4.0f : -4.0f;
  VectorCopy(sv_akimbo_context.muzzle[hand], muzzle);
  /* The outer scope has already moved ent->origin to the dominant hand.
   * Clamp from the saved body eye, not that temporary projectile source. */
  VectorCopy(ent->v.origin, temporary_origin);
  VectorCopy(sv_akimbo_context.body_origin, ent->v.origin);
  SV_ClampVRMuzzleToWorld(ent, muzzle);
  VectorCopy(temporary_origin, ent->v.origin);

  VectorCopy(sv_akimbo_context.angles[hand], ent->v.v_angle);
  ent->v.v_angle[ROLL] = 0; /* QC roll is camera tilt, not wrist roll. */
  AngleVectors(ent->v.v_angle, pr_global_struct->v_forward,
      pr_global_struct->v_right, pr_global_struct->v_up);
  for (i = 0; i < 3; ++i)
    source[i] = ent->v.view_ofs[i] + 11 * pr_global_struct->v_forward[i] +
        offs * pr_global_struct->v_right[i] - 6 * pr_global_struct->v_up[i];
  VectorSubtract(muzzle, source, ent->v.origin);
  return true;
}

/* W_FireSMG computes org after makevectors, then uses aim and one 16-unit
 * traceline before FireBullets2. The one-shot state below is deliberately
 * consumed only by that exact trace; QC continues to own every firing rule. */
qboolean SV_EnyoAkimboMakevectors(void) {
  dfunction_t *function;
  edict_t *ent;
  int hand;
  float offs, t0;
  vec3_t muzzle, forward, right, up, angles, origin, source_offset, clearance_start;
  trace_t reverse;

  if (!qcvm || !pr_global_struct->self)
    return false;
  ent = PROG_TO_EDICT(pr_global_struct->self);
  function = qcvm->xfunction;
  if (!SV_EnyoSMGWeapon(ent) || sv_akimbo_context.ent != ent ||
      !sv_akimbo_context.enyo || sv_akimbo_context.berserk ||
      !SV_EnyoSMGFunction(function) ||
      !SV_AkimboVectorIsFinite(sv_akimbo_context.muzzle[0]) ||
      !SV_AkimboVectorIsFinite(sv_akimbo_context.muzzle[1]) ||
      !SV_AkimboVectorIsFinite(sv_akimbo_context.angles[0]) ||
      !SV_AkimboVectorIsFinite(sv_akimbo_context.angles[1]))
    return false;

  sv_akimbo_context.enyo_makevectors = false;
  sv_akimbo_context.enyo_clearance_pending = false;
  /* W_FireSMG's scalar offs is in its local frame, not OFS_PARM0: the
   * builtin's vector argument has already occupied that shared parameter. */
  offs = qcvm->globals[function->parm_start];
  if (offs != 0.0f && offs != 1.0f)
    return false;
  hand = offs == 0.0f ? 1 : 0; /* anatomical right, then left */

  VectorCopy(sv_akimbo_context.angles[hand], angles);
  angles[ROLL] = 0;
  AngleVectors(angles, forward, right, up);
  VectorCopy(sv_akimbo_context.muzzle[hand], muzzle);

  /* Clamp from the body eye, never from SV_ApplyVRWeaponOffset's temporary
   * hand origin. This is the calibrated physical muzzle M. */
  VectorCopy(ent->v.origin, clearance_start);
  VectorCopy(sv_akimbo_context.body_origin, ent->v.origin);
  SV_ClampVRMuzzleToWorld(ent, muzzle);
  VectorCopy(clearance_start, ent->v.origin);
  if (!SV_AkimboVectorIsFinite(muzzle))
    return false;

  /* QC's original org B is M - 16F. A brush immediately behind M can leave
   * B in solid even after the usual body-eye-to-M clamp, so move its start
   * forward to B' using a reverse brush-only trace and a one-unit margin. */
  VectorMA(muzzle, -16.0f, forward, clearance_start);
  reverse = SV_Move(muzzle, vec3_origin, vec3_origin, clearance_start,
      MOVE_NOMONSTERS, ent);
  if (reverse.startsolid || reverse.allsolid)
    return false;
  VectorCopy(clearance_start, sv_akimbo_context.enyo_clearance_adjusted_start);
  if (reverse.fraction < 1.0f) {
    VectorCopy(reverse.endpos, sv_akimbo_context.enyo_clearance_adjusted_start);
    VectorMA(sv_akimbo_context.enyo_clearance_adjusted_start, 1.0f, forward,
        sv_akimbo_context.enyo_clearance_adjusted_start);
  }
  VectorSubtract(sv_akimbo_context.enyo_clearance_adjusted_start,
      clearance_start, source_offset);
  t0 = DotProduct(source_offset, forward) / 16.0f;
  if (t0 < 0.0f)
    return false;
  if (t0 >= 1.0f) {
    VectorCopy(muzzle, sv_akimbo_context.enyo_clearance_adjusted_start);
    t0 = 1.0f;
  }

  /* Set self.origin so QC's untouched expression
   * origin + view_ofs - 6up +/- 7right reconstructs B exactly. */
  VectorCopy(ent->v.view_ofs, source_offset);
  VectorMA(source_offset, -6.0f, up, source_offset);
  VectorMA(source_offset, hand ? 7.0f : -7.0f, right, source_offset);
  VectorSubtract(clearance_start, source_offset, origin);
  if (!SV_AkimboVectorIsFinite(origin))
    return false;

  /* Commit only after every collision/finite check succeeds. A rejected
   * off-hand origin must not leave QC with mixed dominant/off-hand bases. */
  VectorCopy(origin, ent->v.origin);
  VectorCopy(angles, ent->v.v_angle);
  VectorCopy(forward, pr_global_struct->v_forward);
  VectorCopy(right, pr_global_struct->v_right);
  VectorCopy(up, pr_global_struct->v_up);

  sv_akimbo_context.enyo_makevectors = true;
  sv_akimbo_context.enyo_clearance_pending = true;
  VectorCopy(clearance_start, sv_akimbo_context.enyo_clearance_start);
  VectorCopy(muzzle, sv_akimbo_context.enyo_clearance_end);
  sv_akimbo_context.enyo_clearance_t0 = t0;
  return true;
}

qboolean SV_EnyoAkimboTrace(edict_t *ent, const vec3_t start,
    const vec3_t end, int nomonsters, trace_t *trace) {
  qboolean valid;

  if (!sv_akimbo_context.enyo_clearance_pending)
    return false;
  sv_akimbo_context.enyo_clearance_pending = false;
  valid = SV_EnyoSMGWeapon(ent) && sv_akimbo_context.ent == ent &&
      sv_akimbo_context.enyo && sv_akimbo_context.enyo_makevectors &&
      qcvm->xfunction && SV_EnyoSMGFunction(qcvm->xfunction) &&
      nomonsters == 0 &&
      SV_AkimboVectorIsFinite(start) && SV_AkimboVectorIsFinite(end) &&
      SV_AkimboVectorIsFinite(sv_akimbo_context.enyo_clearance_start) &&
      SV_AkimboVectorIsFinite(sv_akimbo_context.enyo_clearance_end) &&
      SV_AkimboVectorIsFinite(sv_akimbo_context.enyo_clearance_adjusted_start) &&
      SV_AkimboVectorsNear(start, sv_akimbo_context.enyo_clearance_start) &&
      SV_AkimboVectorsNear(end, sv_akimbo_context.enyo_clearance_end);
  if (!valid)
    return false;

  /* Preserve collision flags, but always express the fraction in QC's
   * original B..M coordinates. QC reconstructs its bullet source from that
   * fraction even when an overlapping entity sets startsolid/allsolid. */
  *trace = SV_Move(sv_akimbo_context.enyo_clearance_adjusted_start,
      vec3_origin, vec3_origin, sv_akimbo_context.enyo_clearance_end,
      nomonsters, ent);
  trace->fraction = sv_akimbo_context.enyo_clearance_t0 +
      (1.0f - sv_akimbo_context.enyo_clearance_t0) * trace->fraction;
  return true;
}

/* W_AxeSwing selects an authored ten-frame sequence. Scheduled float times
 * can put its hit at base+2, +3, +4, or later, so cover every source frame.
 * Choose the larger signed grip advance along each original axe-head axis;
 * this is source-animation data, never a timing assumption or firing toggle.
 * Controller indices: 0 is anatomical left, 1 is anatomical right. */
static qboolean SV_DwellBerserkStrikeHand(float weaponframe, int *hand) {
  static const char hands[] =
      "0000000000" "0110011111" "0100000000"
      "0111000000" "0111000111" "0";
  int frame;

  if (!isfinite(weaponframe) || weaponframe < 0 || weaponframe > 50)
    return false;
  frame = (int)weaponframe;
  if (weaponframe != (float)frame)
    return false;
  *hand = hands[frame] - '0';
  return true;
}

/* Dwell's exact W_FireAxe bytecode calls makevectors then traces from
 * self.origin + self.view_ofs. Let its original sequence choose the fist,
 * replace only that trace source/basis, and leave its range, hit, damage and
 * cadence entirely in QuakeC. */
qboolean SV_DwellBerserkAkimboMakevectors(void) {
  edict_t *ent;
  int hand;
  vec3_t muzzle, temporary_origin, source;

  if (!qcvm || !pr_global_struct->self)
    return false;
  ent = PROG_TO_EDICT(pr_global_struct->self);
  if (!SV_DwellBerserkWeapon(ent) || sv_akimbo_context.ent != ent ||
      !sv_akimbo_context.dwell || !sv_akimbo_context.berserk ||
      sv_akimbo_context.enyo || !qcvm->xfunction ||
      !SV_DwellFireAxeFunction(qcvm->xfunction) ||
      !SV_DwellBerserkStrikeHand(ent->v.weaponframe, &hand) ||
      !SV_AkimboVectorIsFinite(sv_akimbo_context.muzzle[hand]) ||
      !SV_AkimboVectorIsFinite(sv_akimbo_context.angles[hand]) ||
      !SV_AkimboVectorIsFinite(sv_akimbo_context.body_origin) ||
      !SV_AkimboVectorIsFinite(ent->v.view_ofs))
    return false;

  VectorCopy(sv_akimbo_context.muzzle[hand], muzzle);
  VectorCopy(ent->v.origin, temporary_origin);
  VectorCopy(sv_akimbo_context.body_origin, ent->v.origin);
  SV_ClampVRMuzzleToWorld(ent, muzzle);
  VectorCopy(temporary_origin, ent->v.origin);
  if (!SV_AkimboVectorIsFinite(muzzle))
    return false;

  VectorCopy(sv_akimbo_context.angles[hand], ent->v.v_angle);
  ent->v.v_angle[ROLL] = 0;
  AngleVectors(ent->v.v_angle, pr_global_struct->v_forward,
      pr_global_struct->v_right, pr_global_struct->v_up);
  VectorSubtract(muzzle, ent->v.view_ofs, source);
  if (!SV_AkimboVectorIsFinite(source))
    return false;
  VectorCopy(source, ent->v.origin);
  return true;
}

qboolean SV_QBJ3AkimboAim(edict_t *ent, vec3_t muzzle) {
  if (SV_QBJ3AkimboAimLegacy(ent, muzzle))
    return true;
  if (!SV_EnyoSMGWeapon(ent) || sv_akimbo_context.ent != ent ||
      !sv_akimbo_context.enyo || !sv_akimbo_context.enyo_makevectors ||
      !qcvm->xfunction || !SV_EnyoSMGFunction(qcvm->xfunction) ||
      !SV_AkimboVectorIsFinite(sv_akimbo_context.enyo_clearance_end))
    return false;
  VectorCopy(sv_akimbo_context.enyo_clearance_end, muzzle);
  return true;
}

/* Do not give QBJ3 QuakeC a wrist roll: its fixangle handling treats roll as
 * camera tilt.  At this one verified player-shot spread call, restore only
 * the physical roll in the basis. Forward is unchanged by AngleVectors roll. */
qboolean SV_QBJ3ShotgunSpreadBasis(const vec3_t angles) {
  edict_t *ent;
  vec3_t spread_angles;

  if (!sv_akimbo_context.qbj3_shotgun_spread || !qcvm ||
      !pr_global_struct->self || !qcvm->xfunction ||
      strcmp(PR_GetString(qcvm->xfunction->s_name), "FireBullets"))
    return false;
  ent = PROG_TO_EDICT(pr_global_struct->self);
  if (ent != sv_akimbo_context.ent ||
      !SV_AkimboVectorIsFinite(angles) ||
      !isfinite(sv_akimbo_context.qbj3_shotgun_roll))
    return false;

  VectorCopy(angles, spread_angles);
  spread_angles[ROLL] = sv_akimbo_context.qbj3_shotgun_roll;
  AngleVectors(spread_angles, pr_global_struct->v_forward,
      pr_global_struct->v_right, pr_global_struct->v_up);
  return true;
}

typedef struct sv_vr_weapon_pose_restore_s {
  qboolean applied;
  vec3_t origin;
  vec3_t v_angle;
  vec3_t v_forward;
  vec3_t v_right;
  vec3_t v_up;
  sv_akimbo_context_t previous_akimbo;
} sv_vr_weapon_pose_restore_t;

static void SV_ApplyVRWeaponOffset(edict_t *ent, int num, qboolean is_remote_vr,
                                   sv_vr_weapon_pose_restore_t *restore) {
  restore->applied = false;
  restore->previous_akimbo = sv_akimbo_context;
  SV_ClearAkimboContext();

  if (is_remote_vr ||
      (vr_enabled.value && !isDedicated && num == cl.viewentity)) {
    vec3_t muzzle, source_offset;

    restore->applied = true;
    VectorCopy(ent->v.origin, restore->origin);
    VectorCopy(ent->v.v_angle, restore->v_angle);
    VectorCopy(pr_global_struct->v_forward, restore->v_forward);
    VectorCopy(pr_global_struct->v_right, restore->v_right);
    VectorCopy(pr_global_struct->v_up, restore->v_up);

    if ((SV_QBJ3AkimboSupported() &&
        !strcmp(PR_GetString(ent->v.weaponmodel), "progs/v_tnailgun.mdl") &&
        ent->v.weapon == 4) || SV_QBJ3BerserkWeapon(ent) ||
        SV_EnyoSMGWeapon(ent) || SV_DwellBerserkWeapon(ent)) {
      qboolean active = false;
      qboolean qbj3_berserk = SV_QBJ3BerserkWeapon(ent);
      qboolean enyo = SV_EnyoSMGWeapon(ent);
      qboolean dwell = SV_DwellBerserkWeapon(ent);
      qboolean pose_berserk = qbj3_berserk || dwell;
      if (is_remote_vr) {
        const usercmd_t *cmd = &svs.clients[num - 1].cmd;
        if (cmd->vr_active && cmd->vr_handpos_relative &&
            cmd->vr_akimbo_active && !svs.clients[num - 1].input_stale &&
            cmd->vr_akimbo_berserk == pose_berserk) {
          int hand;
          sv_akimbo_context.berserk = cmd->vr_akimbo_berserk;
          sv_akimbo_context.enyo = enyo;
          sv_akimbo_context.dwell = dwell;
          for (hand = 0; hand < 2; ++hand) {
            VectorAdd(restore->origin, cmd->vr_akimbo_muzzle[hand],
                sv_akimbo_context.muzzle[hand]);
            VectorCopy(cmd->vr_akimbo_angles[hand],
                sv_akimbo_context.angles[hand]);
          }
          active = true;
        }
      } else {
        active = VR_GetAkimboPoses(sv_akimbo_context.muzzle,
            sv_akimbo_context.angles, &sv_akimbo_context.berserk);
        sv_akimbo_context.enyo = enyo;
        sv_akimbo_context.dwell = dwell;
        if (sv_akimbo_context.berserk != pose_berserk)
          active = false;
      }
      if (active && (!SV_AkimboVectorIsFinite(sv_akimbo_context.muzzle[0]) ||
          !SV_AkimboVectorIsFinite(sv_akimbo_context.muzzle[1]) ||
          !SV_AkimboVectorIsFinite(sv_akimbo_context.angles[0]) ||
          !SV_AkimboVectorIsFinite(sv_akimbo_context.angles[1])))
        active = false;
      if (active) {
        sv_akimbo_context.ent = ent;
        VectorCopy(restore->origin, sv_akimbo_context.body_origin);
        /* Dwell chooses its striking fist only in W_FireAxe, after the
         * original QC animation sequence has advanced. Do not let the
         * generic dominant-hand fallback invent a second hand policy. */
        if (dwell)
          return;
        /* Unlike the nailgun, QBJ3's berserk loop checks the hit frame
         * BEFORE advancing its animation. Keep its five fan traces and
         * combo/damage logic in QC, changing only the striking hand's pose.
         * Its source expression is self.origin + self.view_ofs. */
        if (SV_QBJ3BerserkWeapon(ent) &&
            (ent->v.weaponframe == 14 || ent->v.weaponframe == 34 ||
             ent->v.weaponframe == 54 || ent->v.weaponframe == 64)) {
          int hand = (ent->v.weaponframe == 14 || ent->v.weaponframe == 64);
          VectorCopy(sv_akimbo_context.muzzle[hand], muzzle);
          SV_ClampVRMuzzleToWorld(ent, muzzle);
          VectorCopy(sv_akimbo_context.angles[hand], ent->v.v_angle);
          ent->v.v_angle[ROLL] = 0;
          AngleVectors(ent->v.v_angle, pr_global_struct->v_forward,
              pr_global_struct->v_right, pr_global_struct->v_up);
          VectorSubtract(muzzle, ent->v.view_ofs, ent->v.origin);
          return;
        }
      }
    }

    if (is_remote_vr) {
      if (svs.clients[num - 1].vr_handpos_relative) {
        VectorAdd(restore->origin, svs.clients[num - 1].vr_handpos, muzzle);
      } else {
        VectorCopy(svs.clients[num - 1].vr_handpos, muzzle); /* old clients */
      }
      VectorCopy(svs.clients[num - 1].vr_handrot, ent->v.v_angle);
    } else {
      VR_GetMuzzleAdjustedHandPos(muzzle);
      VectorCopy(cl.handrot[1], ent->v.v_angle);
    }

    if (SV_QBJ3ShotgunWeapon(ent) && SV_AkimboVectorIsFinite(ent->v.v_angle)) {
      sv_akimbo_context.ent = ent;
      sv_akimbo_context.qbj3_shotgun_spread = true;
      sv_akimbo_context.qbj3_shotgun_roll = ent->v.v_angle[ROLL];
    }

    /* QuakeC v_angle roll is camera tilt, not wrist rotation. QBJ3 decays it
     * via fixangle. Sanitize only this temporary QC angle before calculating
     * both the globals and source compensation, so QC makevectors sees the
     * identical basis. The physical muzzle and replicated hand keep roll. */
    ent->v.v_angle[ROLL] = 0;

    /* Legacy QuakeC commonly computes v_forward during PlayerPreThink and
     * consumes it later while firing in a think/PostThink callback. Keep the
     * global aim basis synchronized with the temporary hand-only v_angle so
     * continuous weapons such as lightning cannot inherit HMD direction. */
    AngleVectors(ent->v.v_angle, pr_global_struct->v_forward,
                 pr_global_struct->v_right, pr_global_struct->v_up);

    SV_ClampVRMuzzleToWorld(ent, muzzle);
    VR_GetWeaponProjectileSourceOffset(PR_GetString(ent->v.weaponmodel),
                                       (int)ent->v.weapon, ent->v.v_angle,
                                       ent->v.view_ofs[2], source_offset);
    if (SV_VRMeleeGungnirSelected(ent)) {
      /* The original spark uses a different source than its stab trace.
       * Compensate only this verified projectile at the existing boundary;
       * the user's calibrated controller muzzle remains authoritative. */
      VectorScale(pr_global_struct->v_forward, 25, source_offset);
      VectorMA(source_offset, 8, pr_global_struct->v_right, source_offset);
      VectorMA(source_offset, 12, pr_global_struct->v_up, source_offset);
    }
    VectorSubtract(muzzle, source_offset, ent->v.origin);

  }
}

static void SV_RestoreVRWeaponOffset(edict_t *ent, int num,
                                     qboolean is_remote_vr,
                                     const sv_vr_weapon_pose_restore_t *restore) {
  (void)num;
  (void)is_remote_vr;
  sv_akimbo_context = restore->previous_akimbo;
  if (!restore->applied)
    return;

  VectorCopy(restore->origin, ent->v.origin);
  VectorCopy(restore->v_angle, ent->v.v_angle);
  VectorCopy(restore->v_forward, pr_global_struct->v_forward);
  VectorCopy(restore->v_right, pr_global_struct->v_right);
  VectorCopy(restore->v_up, pr_global_struct->v_up);
}

qboolean SV_IsVRClientSlot(int num) {
  if (num <= 0 || num > svs.maxclients)
    return false;

  if (svs.clients[num - 1].is_vr_client)
    return true;

  return vr_enabled.value && !isDedicated && num == cl.viewentity;
}

static edict_t *SV_CurrentGroundEntity(edict_t *ent) {
  if (!ent || !((int)ent->v.flags & FL_ONGROUND) || !ent->v.groundentity)
    return NULL;

  return PROG_TO_EDICT(ent->v.groundentity);
}

static void SV_RestorePusherGroundContact(edict_t *ent, edict_t *ground) {
  float drop;
  trace_t trace;
  vec3_t end;

  if ((int)ent->v.flags & FL_ONGROUND)
    return;
  if (ent->v.velocity[2] > 0)
    return;

  if (ground && (ground->free || (int)ground->v.movetype != MOVETYPE_PUSH))
    ground = NULL;

  drop = 4.0f;
  if (ground && ground->v.velocity[2] < 0)
    drop += -ground->v.velocity[2] * qcvm->frametime;

  VectorCopy(ent->v.origin, end);
  end[2] -= drop;
  trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);
  if (trace.startsolid || trace.allsolid || trace.fraction == 1.0f)
    return;
  if (!trace.ent || trace.ent->free ||
      (int)trace.ent->v.movetype != MOVETYPE_PUSH ||
      trace.plane.normal[2] <= 0.7f)
    return;
  if (ground && trace.ent != ground)
    return;

  VectorCopy(trace.endpos, ent->v.origin);
  ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
  ent->v.groundentity = EDICT_TO_PROG(trace.ent);
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
    VectorScale(qcbase, -0.8f * prethink_waterlevel * qcvm->frametime,
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
  float pre_link_teleport_time;
  float pre_teleport_time;
  int i, pre_flags;
  qboolean pre_link_fixangle;

  PMSV_UpdateMovevars();
  if (SV_IsVRClientSlot(NUM_FOR_EDICT(ent)) &&
      sv_vr_jump_velocity.value > SV_VANILLA_JUMP_VELOCITY)
    movevars.jumpspeed = sv_vr_jump_velocity.value;

  entgrav = GetEdictFieldValue(ent, qcvm->extfields.gravity);
  if (entgrav && entgrav->_float)
    movevars.entgravity = entgrav->_float;

  pmflags = GetEdictFieldValue(ent, qcvm->extfields.pmove_flags);
  pmflagbits = (pmflags && pmflags->_float) ? (unsigned int)pmflags->_float : 0;
  pre_flags = (int)ent->v.flags;
  pre_teleport_time = ent->v.teleport_time;

  memset(&pmove, 0, sizeof(pmove));
  VectorCopy(ent->v.mins, pmove.player_mins);
  VectorCopy(ent->v.maxs, pmove.player_maxs);
  VectorCopy(ent->v.oldorigin, pmove.safeorigin);
  pmove.safeorigin_known = true;
  VectorCopy(ent->v.origin, pmove.origin);
  VectorCopy(ent->v.velocity, pmove.velocity);
  VectorClear(pmove.gravitydir);
  pmove.waterjumptime = ((pre_flags & FL_WATERJUMP) &&
                         pre_teleport_time > qcvm->time) ?
      pre_teleport_time - qcvm->time : 0;
  pmove.jump_held = pmflags ? !!(pmflagbits & PMF_JUMP_HELD) :
      !(pre_flags & FL_JUMPRELEASED);
  pmove.onladder = !!(pmflagbits & PMF_LADDER);
  pmove.jump_secs = 0;
  pmove.onground = !!(pre_flags & FL_ONGROUND);
  pmove.pm_type = SV_PMoveTypeForEdict(ent);
  if (cmd)
    pmove.cmd = *cmd;

  VectorSubtract(ent->v.absmin, extents, bounds[0]);
  VectorAdd(ent->v.absmax, extents, bounds[1]);
  World_AddEntsToPmove(ent, bounds);

  PM_PlayerMove(1);

  if (host_client && host_client->edict == ent) {
    qboolean dynamic_contact = false;

    host_client->net_move_touches += pmove.numtouch;
    if (pmove.onground && pmove.groundent >= 0 &&
        pmove.groundent < pmove.numphysent &&
        pmove.physents[pmove.groundent].info > 0)
      dynamic_contact = true;
    for (i = 0; i < pmove.numtouch && !dynamic_contact; i++) {
      int touch = pmove.touchindex[i];
      if (touch >= 0 && touch < pmove.numphysent &&
          pmove.physents[touch].info > 0)
        dynamic_contact = true;
    }
    if (dynamic_contact)
      host_client->net_move_dynamic_contacts++;
  }

  VectorCopy(pmove.safeorigin, ent->v.oldorigin);
  VectorCopy(pmove.origin, ent->v.origin);
  VectorCopy(pmove.velocity, ent->v.velocity);
  if (pmove.waterjumptime > 0)
    ent->v.teleport_time = qcvm->time + pmove.waterjumptime;
  else if (!(pre_flags & FL_WATERJUMP) && pre_teleport_time > qcvm->time)
    ent->v.teleport_time = pre_teleport_time;
  else
    ent->v.teleport_time = 0;

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
  pre_link_teleport_time = ent->v.teleport_time;
  pre_link_fixangle = ent->v.fixangle;
  SV_LinkEdict(ent, true);
  if (ent->free ||
      ent->v.teleport_time > pre_link_teleport_time ||
      (!pre_link_fixangle && ent->v.fixangle))
    return;

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

qboolean SV_RunClientPMoveCommand(client_t *client) {
  client_t *saved_host_client;
  edict_t *saved_sv_player;
  edict_t *ent;
  int num;
  int processed = 0;
  qboolean is_remote_vr;
  qboolean command_hook;
  qboolean think_ok;
  qboolean coop_started = false;
  usercmd_t lastcmd;
  usercmd_t gameplaycmd;
  qboolean have_gameplaycmd = false;
  int latched_buttons = 0;
  int latched_impulse = 0;
  coop_respawn_postthink_state_t coop_respawn_state;

  if (!client || !client->active || !client->edict || client->edict->free)
    return false;

  num = (int)(client - svs.clients) + 1;
  if (num < 1 || num > svs.maxclients)
    return false;

  /* Prefix PostThink restores the last processed pose. A later callback can
   * clear the fallback before the next frame, so reload the actual deferred
   * head before either PreThink or movement consumes input again. */
  if (client->move_pending)
    SV_LoadQueuedPMoveUsercmd(client);

  ent = client->edict;
  is_remote_vr = client->cmd.vr_active &&
      (isDedicated || num != cl.viewentity);
  saved_host_client = host_client;
  saved_sv_player = sv_player;
  host_client = client;
  sv_player = ent;

  if (!client->move_pending) {
    client->cmd.seconds = 0;
    host_client = saved_host_client;
    sv_player = saved_sv_player;
    return false;
  }

  command_hook = client->move_authority == MOVE_AUTHORITY_PMOVE_QC_COMMAND;
  SV_CoopRespawnBeginPostThink(ent, num, &coop_respawn_state);
  coop_started = true;

  /* Legacy QuakeC expects its lifecycle hooks once per server physics frame,
   * while PMove itself consumes each accepted command in sequence. */
  if (!command_hook) {
    sv_vr_weapon_pose_restore_t thinkRestore;
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

    SV_SetQCInputGlobals(&client->cmd);

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
    client->net_move_qc_prethinks++;
    if (ent->free) {
      goto done;
    }
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
    SV_CheckVelocity(ent);

    SV_ApplyVRWeaponOffset(ent, num, is_remote_vr, &thinkRestore);
    think_ok = SV_RunThink(ent);
    SV_RestoreVRWeaponOffset(ent, num, is_remote_vr, &thinkRestore);
    if (!think_ok || ent->free) {
      goto done;
    }
  }

  while (client->move_pending && !ent->free) {
    usercmd_t cmd = client->cmd;

    latched_buttons |= cmd.buttons;
    if (!latched_impulse && cmd.impulse)
      latched_impulse = cmd.impulse;
    if (!have_gameplaycmd && ((cmd.buttons & BUTTON_ATTACK) || cmd.impulse)) {
      gameplaycmd = cmd;
      have_gameplaycmd = true;
    }

    if (command_hook) {
      sv_vr_weapon_pose_restore_t thinkRestore;

      /* Explicit command physics owns its per-command QuakeC callback. */
      is_remote_vr = cmd.vr_active &&
          (isDedicated || num != cl.viewentity);
      SV_SetQCInputGlobals(&cmd);
      pr_global_struct->time = qcvm->time;
      pr_global_struct->self = EDICT_TO_PROG(ent);
      PR_ExecuteProgram(pr_global_struct->PlayerPreThink);
      client->net_move_qc_prethinks++;
      if (ent->free)
        break;
      SV_CheckVelocity(ent);

      SV_ApplyVRWeaponOffset(ent, num, is_remote_vr, &thinkRestore);
      think_ok = SV_RunThink(ent);
      SV_RestoreVRWeaponOffset(ent, num, is_remote_vr, &thinkRestore);
      if (!think_ok || ent->free)
        break;

      pr_global_struct->self = EDICT_TO_PROG(ent);
      PR_ExecuteProgram(qcvm->extfuncs.SV_RunClientCommand);
      client->net_move_qc_commands++;
      SV_LinkEdict(ent, true);
    } else {
      SV_RunPMoveForEntity(ent, &cmd);
    }
    if (ent->free) {
      break;
    }

    {
      usercmd_t contactcmd = cmd;
      /* PostThink services latched trigger events after this batch. A later
       * release/motion command must not sneak in a stab before that shot. */
      contactcmd.buttons |= latched_buttons & BUTTON_ATTACK;
      SV_VRContactProcessCommand(client, &contactcmd);
    }
    if (ent->free)
      break;

    /* The accepted command carries exactly one room-scale sample.  PMove
     * consumes it during its first substep; clear the server-side latches so
     * a later frame cannot replay it (including rejected tracking outliers). */
    VectorCopy(vec3_origin, client->vr_roomscalemove);
    VectorCopy(vec3_origin, client->vr_roomscale_accum);
    VectorCopy(vec3_origin, client->cmd.vr_roomscalemove);

    lastcmd = cmd;
    SV_FinishPMoveUsercmd(client);
    processed++;
    /* A real QC trigger may have entered a ladder or custom liquid. Stop
     * before another command can overwrite that state. Keep the remainder
     * queued until the next frame's legacy handoff, including its events and
     * tracking, and do not advertise prediction for this contact snapshot. */
    if (SV_QBJ3NeedsLegacyPhysics(client)) {
      client->move_prediction_allowed = false;
      break;
    }
  }

  if (!ent->free && processed) {
    sv_vr_weapon_pose_restore_t weaponRestore;

    SV_LinkEdict(ent, false);
    pr_global_struct->time = qcvm->time;
    if (!have_gameplaycmd)
      gameplaycmd = lastcmd;
    gameplaycmd.buttons |= latched_buttons;
    if (latched_impulse)
      gameplaycmd.impulse = latched_impulse;
    client->cmd = gameplaycmd;
    ent->v.button0 = gameplaycmd.buttons & 1;
    ent->v.button2 = (gameplaycmd.buttons & 2) >> 1;
    SV_SetExtendedButtons(ent, gameplaycmd.buttons);
    ent->v.impulse = gameplaycmd.impulse;
    client->is_vr_client = gameplaycmd.vr_active;
    client->vr_handpos_relative = gameplaycmd.vr_handpos_relative;
    VectorCopy(gameplaycmd.vr_handpos, client->vr_handpos);
    VectorCopy(gameplaycmd.vr_handrot, client->vr_handrot);
    SV_SetQCInputGlobals(&gameplaycmd);
    is_remote_vr = gameplaycmd.vr_active &&
        (isDedicated || num != cl.viewentity);
    SV_ApplyVRWeaponOffset(ent, num, is_remote_vr, &weaponRestore);
    pr_global_struct->self = EDICT_TO_PROG(ent);
    SV_CoopReviveBeginPostThink(ent);
    {
      qboolean ff_scope = SV_FriendlyFireBegin(ent);
      PR_ExecuteProgram(pr_global_struct->PlayerPostThink);
      client->net_move_qc_postthinks++;
      if (ff_scope)
        SV_FriendlyFireEnd();
    }
    SV_CoopReviveEndPostThink();
    SV_RestoreVRWeaponOffset(ent, num, is_remote_vr, &weaponRestore);
    SV_CoopRespawnEndPostThink(ent, num, &coop_respawn_state);
    SV_CoopReviveApplyPending();

    /* Restore the newest held state after delivering latched one-frame
     * gameplay events with the pose of the command that originated them. */
    client->cmd = lastcmd;
    client->cmd.impulse = 0;
    ent->v.button0 = lastcmd.buttons & 1;
    ent->v.button2 = (lastcmd.buttons & 2) >> 1;
    SV_SetExtendedButtons(ent, lastcmd.buttons);
    ent->v.impulse = 0;
    client->is_vr_client = lastcmd.vr_active;
    client->vr_handpos_relative = lastcmd.vr_handpos_relative;
    VectorCopy(lastcmd.vr_handpos, client->vr_handpos);
    VectorCopy(lastcmd.vr_handrot, client->vr_handrot);
  }

done:
  if (SV_QBJ3NeedsLegacyPhysics(client))
    client->move_prediction_allowed = false;
  if (coop_started && (ent->free || !processed))
    SV_CoopRespawnRestoreSuppressedInput(ent, num, &coop_respawn_state);

  host_client = saved_host_client;
  sv_player = saved_sv_player;

  if (ent->free || !processed) {
    client->pendingmovemessage = -1;
    client->move_pending = false;
    client->move_queue_head = 0;
    client->move_queue_count = 0;
    client->cmd.seconds = 0;
    VectorCopy(vec3_origin, client->cmd.vr_roomscalemove);
    VectorCopy(vec3_origin, client->vr_roomscalemove);
    VectorCopy(vec3_origin, client->vr_roomscale_accum);
  }

  return processed > 0;
}

/* Sweep auxiliary tracking with the existing collision/step solver.  Only
 * origin is committed: normal physics owns velocity/ground state and the one
 * final trigger pass.  Blocked tracking is consumed, never saved as debt. */
static void SV_ApplyLegacyVRRoomScaleMove(edict_t *ent, client_t *client) {
  vec3_t move, saved_velocity;
  float saved_flags;
  int saved_groundentity;

  VectorCopy(client->vr_roomscale_accum, move);
  VectorClear(client->vr_roomscale_accum);
  move[2] = 0;
  if ((!move[0] && !move[1]) || qcvm->frametime <= 0)
    return;

  VectorCopy(ent->v.velocity, saved_velocity);
  saved_flags = ent->v.flags;
  saved_groundentity = ent->v.groundentity;
  VectorScale(move, 1.0f / qcvm->frametime, ent->v.velocity);
  if (ent->v.movetype == MOVETYPE_NOCLIP) {
    VectorAdd(ent->v.origin, move, ent->v.origin);
  } else if (ent->v.movetype == MOVETYPE_WALK)
    SV_WalkMoveInternal(ent, false);
  else
    SV_FlyMoveInternal(ent, qcvm->frametime, NULL, false);
  VectorCopy(saved_velocity, ent->v.velocity);
  ent->v.flags = saved_flags;
  ent->v.groundentity = saved_groundentity;
  SV_LinkEdict(ent, false);
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
  edict_t *prethink_groundentity;
  eval_t *val;

  if (!svs.clients[num - 1].active)
    return; // unconnected slot
  if (!svs.clients[num - 1].knowntoqc && sv_gameplayfix_spawnbeforethinks.value)
    return;
  /* A death after this client's previous physics pass must clear the stroke
   * before QuakeC can respawn it. This also applies to singleplayer and mods
   * which own their respawn lifecycle, independently of co-op policies. */
  if (ent->free || ent->v.health <= 0 || ent->v.deadflag)
    SV_VRContactResetClient(&svs.clients[num - 1]);

  // Exclude the local player: on a listen server / singleplayer, the local
  // player's vr_handpos arrives one frame late through loopback.  Using
  // cl.handpos[1] directly (in the else-if fallback) matches the original
  // master branch behaviour exactly.
  qboolean is_remote_vr =
      (num > 0 && num <= svs.maxclients && svs.clients[num - 1].is_vr_client &&
       (isDedicated || num != cl.viewentity));

  if (svs.clients[num - 1].usingpmove) {
    SV_RunClientPMoveCommand(&svs.clients[num - 1]);
    return;
  }

  if (is_remote_vr)
    SV_ApplyLegacyVRRoomScaleMove(ent, &svs.clients[num - 1]);

  was_onground = ((int)ent->v.flags & FL_ONGROUND) != 0;
  prethink_groundentity = SV_CurrentGroundEntity(ent);
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
  if ((val = GetEdictFieldValue(ent, qcvm->extfields.customphysics)) &&
      val->function) {
    pr_global_struct->time = qcvm->time;
    pr_global_struct->self = EDICT_TO_PROG(ent);
    PR_ExecuteProgram(val->function);
    if (ent->free) {
      SV_CoopRespawnRestoreSuppressedInput(ent, num, &coop_respawn_state);
      return;
    }
  }
  else {
    // SV_RunThink executes the entity's think function, which for players
    // includes weapon animation frames (e.g. nailgun's player_nail1/nail2).
    // These think functions can fire projectiles using self.origin, so we
    // must set origin to the hand position during SV_RunThink -- not just
    // during PostThink.  We restore the body origin before movement physics
    // (SV_WalkMove, etc.) which needs the real collision hull position.
    {
      sv_vr_weapon_pose_restore_t thinkRestore;
      SV_ApplyVRWeaponOffset(ent, num, is_remote_vr, &thinkRestore);

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

      SV_RestoreVRWeaponOffset(ent, num, is_remote_vr, &thinkRestore);

      if (!think_ok) {
        SV_CoopRespawnRestoreSuppressedInput(ent, num, &coop_respawn_state);
        return;
      }
    }

    // Movement physics -- uses body origin for collision detection
    switch ((int)ent->v.movetype) {
    case MOVETYPE_NONE:
      break;

    case MOVETYPE_WALK:
      if (!SV_CheckWater(ent) && !((int)ent->v.flags & FL_WATERJUMP) &&
          !SV_EntityOnLadder(ent))
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
      SV_FlyMove(ent, qcvm->frametime, NULL);
      break;

    case MOVETYPE_NOCLIP:
      VectorMA(ent->v.origin, qcvm->frametime, ent->v.velocity, ent->v.origin);
      break;

    default:
      break;
    }

    if (num == cl.viewentity && vr_enabled.value && qcvm->frametime > 0 &&
        VectorLength(vr_room_scale_move) > 0.0625f) {
      vec3_t restoreVel;
      _VectorCopy(ent->v.velocity, restoreVel);
      VectorScale(vr_room_scale_move, 1.0f / qcvm->frametime, ent->v.velocity);

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
        SV_FlyMove(ent, qcvm->frametime, NULL);
        break;

      case MOVETYPE_NOCLIP:
        VectorMA(ent->v.origin, qcvm->frametime, ent->v.velocity, ent->v.origin);
        break;

      default:
        Sys_Error("SV_Physics_client: bad movetype %i", (int)ent->v.movetype);
      }

      _VectorCopy(restoreVel, ent->v.velocity);
    }
  }

  SV_RestorePusherGroundContact(ent, prethink_groundentity);

  //
  // call standard player post-think
  //
  SV_LinkEdict(ent, true);

  pr_global_struct->time = qcvm->time;

  // replace player origin with hand origin for duration of post think (where
  // weapons are done)
  sv_vr_weapon_pose_restore_t weaponRestore;
  SV_ApplyVRWeaponOffset(ent, num, is_remote_vr, &weaponRestore);

  pr_global_struct->self = EDICT_TO_PROG(ent);

  SV_CoopReviveBeginPostThink(ent);
  {
    qboolean ff_scope = SV_FriendlyFireBegin(ent);
    PR_ExecuteProgram(pr_global_struct->PlayerPostThink);
    if (ff_scope)
      SV_FriendlyFireEnd();
  }
  SV_CoopReviveEndPostThink();

  SV_RestoreVRWeaponOffset(ent, num, is_remote_vr, &weaponRestore);
  SV_VRContactDrainLegacy(&svs.clients[num - 1]);
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

  VectorMA(ent->v.angles, qcvm->frametime, ent->v.avelocity, ent->v.angles);
  VectorMA(ent->v.origin, qcvm->frametime, ent->v.velocity, ent->v.origin);

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
static qboolean SV_TossGroundIsValid(edict_t *ent) {
  int groundref;
  edict_t *ground;

  groundref = ent->v.groundentity;

  // A zero ground reference is the world entity, which remains solid for the
  // lifetime of the server.  Non-zero references can become stale when QC
  // hides or frees a moving platform underneath a pickup or projectile.
  if (!groundref)
    return true;

  if (groundref < 0 || groundref % qcvm->edict_size != 0 ||
      groundref > (qcvm->num_edicts - 1) * qcvm->edict_size)
    return false;

  ground = PROG_TO_EDICT(groundref);
  return !ground->free && ground->v.solid >= SOLID_BBOX;
}

void SV_Physics_Toss(edict_t *ent) {
  trace_t trace;
  vec3_t move;
  float backoff;

  // regular thinking
  if (!SV_RunThink(ent))
    return;

  // If the supporting entity was hidden or freed by QC, release the toss
  // entity instead of leaving it suspended forever with stale ground state.
  // This preserves stable contact on live pushers while allowing items and
  // projectiles to fall from disappearing platforms.
  if ((int)ent->v.flags & FL_ONGROUND) {
    if (SV_TossGroundIsValid(ent))
      return;
    ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
    ent->v.groundentity = 0;
  }

  SV_CheckVelocity(ent);

  // add gravity
  if (ent->v.movetype != MOVETYPE_FLY && ent->v.movetype != MOVETYPE_FLYMISSILE)
    SV_AddGravity(ent);

  // move angles
  VectorMA(ent->v.angles, qcvm->frametime, ent->v.avelocity, ent->v.angles);

  // move origin
  VectorScale(ent->v.velocity, qcvm->frametime, move);
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
    SV_FlyMove(ent, qcvm->frametime, NULL);
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
void SV_Physics(double frametime) {
  int i;
  int entity_cap; // For sv_freezenonclients
  int physics_mode;
  edict_t *ent;
  eval_t *val;

  /* An aborted QC call must not substitute a later, unrelated trace. */
  sv_vr_contact_call.active = false;
  sv_vr_contact_call.force_miss = false;

  /* PR_ExecuteProgram may abort the host frame through Host_Error.  Repair a
   * no-friendly-fire scope left by such an abort before any enemy think/touch
   * code can observe a player with temporary DAMAGE_NO. */
  if (qcvm == &sv.qcvm && ff_active)
    SV_FriendlyFireReset();

  /* Snapshot before StartFrame and before client/entity physics.  Death QC
   * may run from any of those phases, including missiles and monsters which
   * are processed after the victim's own post-think. */
  SV_CoopRespawnBeginFrameDeathTracking();

  if (qcvm->extglobals.physics_mode)
    physics_mode = *qcvm->extglobals.physics_mode;
  else
    physics_mode = (qcvm == &cl.qcvm) ? 0 : 2;

  if (frametime < 0)
    frametime = 0;
  pr_global_struct->time = qcvm->time;
  pr_global_struct->frametime = qcvm->frametime = frametime;

  if (!physics_mode) {
    SV_CoopRespawnEndFrameDeathTracking();
    qcvm->time += frametime;
    return;
  }
  else if (physics_mode == 1) {
    for (i = 0, ent = qcvm->edicts; i < qcvm->num_edicts;
         i++, ent = NEXT_EDICT(ent)) {
      if (ent->free)
        continue;
      SV_RunThink(ent);
    }
    SV_CoopRespawnEndFrameDeathTracking();
    qcvm->time += frametime;
    return;
  }

  // let the progs know that a new frame has started
  if (pr_global_struct->StartFrame) {
    pr_global_struct->self = EDICT_TO_PROG(qcvm->edicts);
    pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
    pr_global_struct->time = qcvm->time;
    PR_ExecuteProgram(pr_global_struct->StartFrame);
  }

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

    if (i > 0 && i <= svs.maxclients && qcvm == &sv.qcvm)
      SV_Physics_Client(ent, i);
    else if ((val = GetEdictFieldValue(ent, qcvm->extfields.customphysics)) &&
             val->function) {
      pr_global_struct->time = qcvm->time;
      pr_global_struct->self = EDICT_TO_PROG(ent);
      PR_ExecuteProgram(val->function);
    }
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
    else if (ent->v.movetype == MOVETYPE_WALK) {
      if (SV_RunThink(ent)) {
        if (!SV_CheckWater(ent) && !((int)ent->v.flags & FL_WATERJUMP))
          SV_AddGravity(ent);
        SV_CheckStuck(ent);
        SV_WalkMove(ent);
      }
    }
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

  /* Complete alive-to-dead transitions only after every entity has run.
   * The immediate player-postthink path marks deaths it already handled, so
   * this catches later projectile/monster/trigger deaths exactly once. */
  SV_CoopRespawnEndFrameDeathTracking();

  if (pr_global_struct->force_retouch)
    pr_global_struct->force_retouch--;

  if (!sv_freezenonclients.value)
    qcvm->time += frametime;

  /* A normal frame must never retain temporary player damage state. */
  if (qcvm == &sv.qcvm && ff_active)
    SV_FriendlyFireReset();
}
