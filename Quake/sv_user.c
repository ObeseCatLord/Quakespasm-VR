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
// sv_user.c -- server code for moving users

#include "quakedef.h"
#include "vr.h"
#include "pmove.h"
#include "sv_pmove_policy.h"
#include "vrik_codec.h"
#include "player_avatar.h"

edict_t *sv_player;

extern cvar_t sv_friction;
cvar_t sv_edgefriction = {"edgefriction", "2", CVAR_NONE};
extern cvar_t sv_stopspeed;

static vec3_t forward, right, up;

// world
float *angles;
float *origin;
float *velocity;

qboolean onground;

usercmd_t cmd;

static void SV_UpdateClientPMoveMode(client_t *client, qboolean allow_promotion);

#define SV_VANILLA_JUMP_VELOCITY 270.0f
#define SV_VANILLA_WATERJUMP_VELOCITY 225.0f
#define SV_VR_SWIM_JUMP_UPMOVE 200.0f

cvar_t sv_idealpitchscale = {"sv_idealpitchscale", "0.8", CVAR_NONE};
cvar_t sv_altnoclip = {"sv_altnoclip", "1", CVAR_ARCHIVE}; // johnfitz
cvar_t sv_nqplayerphysics = {"sv_nqplayerphysics", "0", CVAR_ARCHIVE | CVAR_SERVERINFO};
cvar_t sv_trustedmovement = {"sv_trustedmovement", "1", CVAR_SERVERINFO};
cvar_t sv_pmove_mode = {"sv_pmove_mode", "1", CVAR_ARCHIVE | CVAR_SERVERINFO};
cvar_t sv_inputtimeout = {"sv_inputtimeout", "0", CVAR_NONE};
cvar_t sv_pmove_legacy_preserve_qc_velocity = {
    "sv_pmove_legacy_preserve_qc_velocity", "1", CVAR_NONE};

static float SV_VRJumpScale(void) {
  if (host_client && host_client->is_vr_client &&
      sv_vr_jump_velocity.value > SV_VANILLA_JUMP_VELOCITY)
    return sv_vr_jump_velocity.value / SV_VANILLA_JUMP_VELOCITY;
  return 1.0f;
}

static float SV_WaterUpMove(void) {
  float upmove = cmd.upmove;

  if (host_client && host_client->is_vr_client &&
      (cmd.buttons & BUTTON_JUMP) && upmove < SV_VR_SWIM_JUMP_UPMOVE)
    upmove = SV_VR_SWIM_JUMP_UPMOVE;

  return upmove;
}

static qboolean SV_PlayerOnLadder(void) {
  eval_t *val;

  if (!sv_player)
    return false;
  val = GetEdictFieldValue(sv_player, qcvm->extfields.onladder);
  return val && val->_float != 0;
}

static qboolean SV_GorillaInputIsValid(const vr_gorilla_input_t *input) {
  int hand;
  vec3_t arm;

  if (!input || (input->flags & ~VR_GORILLA_FLAGS) ||
      (input->flags & VR_GORILLA_HANDS) != VR_GORILLA_HANDS ||
      !isfinite(input->head[0]) || !isfinite(input->head[1]) ||
      !isfinite(input->head[2]) || VectorLength(input->head) > 160.0f)
    return false;
  for (hand = 0; hand < 2; hand++) {
    if (!isfinite(input->hand[hand][0]) || !isfinite(input->hand[hand][1]) ||
        !isfinite(input->hand[hand][2]) ||
        !isfinite(input->velocity[hand][0]) ||
        !isfinite(input->velocity[hand][1]) ||
        !isfinite(input->velocity[hand][2]) ||
        VectorLength(input->velocity[hand]) > VR_GORILLA_MAX_HAND_SPEED)
      return false;
    VectorSubtract(input->hand[hand], input->head, arm);
    if (VectorLength(arm) > VR_GORILLA_MAX_REACH)
      return false;
  }
  return true;
}

static qboolean SV_GorillaNativeLadder(edict_t *ent) {
  eval_t *value;
  if (!ent)
    return false;
  value = GetEdictFieldValue(ent, qcvm->extfields.onladder);
  if (value)
    return value->_float != 0;
  /* Immortal's ladder_touch writes laddercount=1; PreThink consumes it.
   * laddertime is an exit cooldown, not an active-contact predicate. */
  value = GetEdictFieldValueByName(ent, "laddercount");
  return value && value->_float > 0 &&
      GetEdictFieldValueByName(ent, "laddertime") &&
      GetEdictFieldValueByName(ent, "laddersoundtime") &&
      ED_FindFunction("ladder_touch") && ED_FindFunction("trigger_ladder");
}

void SV_GorillaLatchLadder(client_t *client, qboolean begin_frame) {
  if (!client)
    return;
  if (begin_frame)
    client->vr_gorilla_ladder_frame = false;
  client->vr_gorilla_ladder_frame |= SV_GorillaNativeLadder(client->edict);
}

static qboolean SV_GorillaEligibleInput(client_t *client,
                                        const vr_gorilla_input_t *input) {
  edict_t *ent;

  if (!client || !client->active || !client->spawned ||
      !client->vr_gorilla_capable || !sv_gorilla.value ||
      !client->cmd.vr_active || !client->cmd.vr_handpos_relative ||
      !SV_GorillaInputIsValid(input))
    return false;
  ent = client->edict;
  if (!ent || ent->free || ent->v.health <= 0 || ent->v.deadflag ||
      ((int)ent->v.movetype != MOVETYPE_WALK &&
       (int)ent->v.movetype != MOVETYPE_FLY) ||
      client->vr_gorilla_ladder_frame || SV_GorillaNativeLadder(ent))
    return false;
  return true;
}

qboolean SV_GorillaEligible(client_t *client) {
  return client && SV_GorillaEligibleInput(client, &client->cmd.vr_gorilla);
}

static qboolean SV_GorillaStateIsFinite(const vr_gorilla_state_t *state) {
  int hand, axis;

  if (!state || state->initialized > 1 ||
      (state->touching & ~VR_GORILLA_HANDS) ||
      (state->recovering & ~VR_GORILLA_HANDS))
    return false;
  for (hand = 0; hand < 2; hand++) {
    if (state->surface[hand] < 0 || state->surface[hand] >= MAX_EDICTS ||
        state->surface_model[hand] >= MAX_MODELS ||
        (!state->surface[hand] && state->surface_model[hand]) ||
        (state->surface[hand] && !state->surface_model[hand]) ||
        DotProduct(state->recovery_offset[hand], state->recovery_offset[hand]) >
            VR_GORILLA_MAX_REACH * VR_GORILLA_MAX_REACH)
      return false;
    for (axis = 0; axis < 3; axis++)
      if (!isfinite(state->anchor[hand][axis]) ||
          !isfinite(state->recovery_offset[hand][axis]))
        return false;
  }
  for (axis = 0; axis < 3; axis++)
    if (!isfinite(state->velocity[axis]) || !isfinite(state->origin[axis]))
      return false;
  return true;
}

void SV_VRGorillaResetClient(client_t *client) {
  if (!client)
    return;
  Q_memset(&client->vr_gorilla_state, 0, sizeof(client->vr_gorilla_state));
  client->vr_gorilla_button[0] = 0;
  client->vr_gorilla_button[1] = 0;
  client->vr_gorilla_active = false;
  client->vr_gorilla_move_deferred = false;
  client->vr_gorilla_state_sequence = -1;
  client->vr_gorilla_legacy_head = 0;
  client->vr_gorilla_legacy_count = 0;
}

void SV_VRGorillaAcceptLegacy(client_t *client, const usercmd_t *cmd) {
  vr_gorilla_legacy_sample_t *sample;
  unsigned int tail;

  if (!client || !cmd)
    return;
  /* Preserve active, inactive and reset samples in sequence order. Physics
   * consumes those boundaries after PlayerPreThink; only server policy and
   * queue discontinuities may erase an earlier unsimulated push. */
  if (!client->active || !client->spawned || !client->vr_gorilla_capable) {
	return;
  }
  if (!sv_gorilla.value) {
    SV_VRGorillaResetClient(client);
    return;
  }
  /* Capability negotiation only says this client understands the extension.
   * A client that leaves vr_gorilla disabled sends no Gorilla flags and must
   * not enter the auxiliary queue/prepass at all. Preserve an OFF boundary
   * only when it is needed to retire an already-active/queued controller. */
  if (!cmd->vr_gorilla.flags && !client->vr_gorilla_state.initialized &&
      !client->vr_gorilla_legacy_count)
    return;
  if (client->vr_gorilla_legacy_count >= MOVE_BUNDLE_MAX) {
    /* Preserve ordered, bounded samples; an overflow is a pose discontinuity,
     * never permission to reuse an old planted hand. */
    SV_VRGorillaResetClient(client);
    client->move_discontinuity_epoch++;
    client->move_discontinuity_reason = MOVEACK_DISCONTINUITY_TRACKING_OUTLIER;
  }
  tail = (client->vr_gorilla_legacy_head + client->vr_gorilla_legacy_count) %
      MOVE_BUNDLE_MAX;
  sample = &client->vr_gorilla_legacy_queue[tail];
  sample->sequence = cmd->sequence;
  sample->msec = cmd->msec;
  sample->seconds = cmd->msec ? cmd->msec * 0.001f :
      CLAMP(0.001f, host_frametime, 0.1f);
  sample->input = cmd->vr_gorilla;
  client->vr_gorilla_legacy_count++;
}

qboolean SV_VRGorillaDrainLegacy(client_t *client,
                                 vr_gorilla_legacy_sample_t *sample) {
  if (!client || !sample || !client->vr_gorilla_legacy_count)
    return false;
  *sample = client->vr_gorilla_legacy_queue[client->vr_gorilla_legacy_head];
  client->vr_gorilla_legacy_head =
      (client->vr_gorilla_legacy_head + 1) % MOVE_BUNDLE_MAX;
  client->vr_gorilla_legacy_count--;
  return true;
}

void SV_VRGorillaFinishCommand(client_t *client, int sequence) {
  if (!client || sequence < 0 || sequence > client->lastacceptedmovemessage ||
      !SV_GorillaStateIsFinite(&client->vr_gorilla_state)) {
    if (client)
      SV_VRGorillaResetClient(client);
    return;
  }
  client->vr_gorilla_active = client->vr_gorilla_state.initialized != 0;
  client->vr_gorilla_state_sequence = sequence;
}

void SV_SetExtendedButtons(edict_t *ent, int buttons) {
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

static void SV_ClearClientPMoveState(client_t *client) {
  client->cmd.seconds = 0;
  client->lastmovetime = 0;
  client->pendingmovemessage = -1;
  client->move_pending = false;
  client->move_queue_head = 0;
  client->move_queue_count = 0;
}

void SV_ResetClientMoveState(client_t *client) {
  SV_VRContactResetClient(client);
  SV_VRGorillaResetClient(client);
  Q_memset(&client->cmd, 0, sizeof(client->cmd));
  VectorCopy(vec3_origin, client->wishdir);
  client->last_move_time = 0;
  client->input_stale = false;
  client->moveext = false;
  client->lastmovemessage = 0;
  /* Latest-client deliberately retains but never transmits sequences 0 and 1. */
  client->lastacceptedmovemessage = 1;
  client->move_authority = MOVE_AUTHORITY_UNKNOWN;
  client->move_prediction_allowed = false;
  client->move_client_quarantined = false;
  client->move_mode_epoch = 0;
  client->move_discontinuity_epoch = 0;
  client->move_discontinuity_reason = MOVEACK_DISCONTINUITY_NONE;
  client->net_move_last_servertime = 0;
  client->net_move_last_msec = 0;
  SV_ClearClientPMoveState(client);

  client->net_move_packets_received = 0;
  client->net_move_cmds_received = 0;
  client->net_move_cmds_accepted = 0;
  client->net_move_cmds_stale = 0;
  client->net_move_cmds_simulated = 0;
  client->net_move_msec_accepted = 0;
  client->net_move_msec_simulated = 0;
  client->net_move_queue_overflows = 0;
  client->net_move_roomscale_outliers = 0;
  client->net_move_qc_prethinks = 0;
  client->net_move_qc_postthinks = 0;
  client->net_move_qc_commands = 0;
  client->net_move_touches = 0;
  client->net_move_dynamic_contacts = 0;
  client->net_move_stale_log_suppressed = 0;
  client->net_move_bundle_max = 0;
  client->net_move_last_bundle = 0;
  client->net_move_last_gap = 0;
  client->net_move_stale_log_time = 0;
  client->net_move_input_log_time = 0;
  client->net_move_last_sim_seconds = 0;
  client->net_snapshot_sequence = 0;
  client->net_snapshot_ack = -1;
  client->net_snapshot_packets_sent = 0;
  client->net_snapshot_split_packets = 0;
  client->net_snapshot_unsent_entities = 0;
  client->net_snapshot_updates_sent = 0;
  client->net_snapshot_last_packets = 0;
  client->net_snapshot_last_bytes = 0;
  client->net_snapshot_max_bytes = 0;
  client->net_snapshot_max_packets = 0;
  client->net_snapshot_ack_lag_max = 0;
  client->net_snapshot_last_ack_time = 0;
  client->net_snapshot_ack_age_max = 0;
  client->net_snapshot_last_summary_time = 0;

  client->is_vr_client = false;
  client->vr_handpos_relative = false;
  client->net_latched_buttons = 0;
  client->net_latched_impulse = 0;
  client->net_latest_buttons = 0;
  VectorCopy(vec3_origin, client->vr_handpos);
  VectorCopy(vec3_origin, client->vr_handrot);
  VectorCopy(vec3_origin, client->vr_roomscalemove);
  VectorCopy(vec3_origin, client->vr_roomscale_accum);

  if (client->edict && !client->edict->free) {
    client->edict->v.button0 = 0;
    client->edict->v.button2 = 0;
    SV_SetExtendedButtons(client->edict, 0);
    client->edict->v.impulse = 0;
  }
}

/*
===============
SV_SetIdealPitch
===============
*/
#define MAX_FORWARD 6
void SV_SetIdealPitch(void) {
  float angleval, sinval, cosval;
  trace_t tr;
  vec3_t top, bottom;
  float z[MAX_FORWARD];
  int i, j;
  int step, dir, steps;

  if (!((int)sv_player->v.flags & FL_ONGROUND))
    return;

  angleval = sv_player->v.angles[YAW] * M_PI * 2 / 360;
  sinval = sin(angleval);
  cosval = cos(angleval);

  for (i = 0; i < MAX_FORWARD; i++) {
    top[0] = sv_player->v.origin[0] + cosval * (i + 3) * 12;
    top[1] = sv_player->v.origin[1] + sinval * (i + 3) * 12;
    top[2] = sv_player->v.origin[2] + sv_player->v.view_ofs[2];

    bottom[0] = top[0];
    bottom[1] = top[1];
    bottom[2] = top[2] - 160;

    tr = SV_Move(top, vec3_origin, vec3_origin, bottom, 1, sv_player);
    if (tr.allsolid)
      return; // looking at a wall, leave ideal the way is was

    if (tr.fraction == 1)
      return; // near a dropoff

    z[i] = top[2] + tr.fraction * (bottom[2] - top[2]);
  }

  dir = 0;
  steps = 0;
  for (j = 1; j < i; j++) {
    step = z[j] - z[j - 1];
    if (step > -ON_EPSILON && step < ON_EPSILON)
      continue;

    if (dir && (step - dir > ON_EPSILON || step - dir < -ON_EPSILON))
      return; // mixed changes

    steps++;
    dir = step;
  }

  if (!dir) {
    sv_player->v.idealpitch = 0;
    return;
  }

  if (steps < 2)
    return;
  sv_player->v.idealpitch = -dir * sv_idealpitchscale.value;
}

/*
==================
SV_UserFriction

==================
*/
void SV_UserFriction(void) {
  float *vel;
  float speed, newspeed, control;
  vec3_t start, stop;
  float friction;
  trace_t trace;

  vel = velocity;

  speed = sqrt(vel[0] * vel[0] + vel[1] * vel[1]);
  if (!speed)
    return;

  // if the leading edge is over a dropoff, increase friction
  start[0] = stop[0] = origin[0] + vel[0] / speed * 16;
  start[1] = stop[1] = origin[1] + vel[1] / speed * 16;
  start[2] = origin[2] + sv_player->v.mins[2];
  stop[2] = start[2] - 34;

  trace = SV_Move(start, vec3_origin, vec3_origin, stop, true, sv_player);

  if (trace.fraction == 1.0)
    friction = sv_friction.value * sv_edgefriction.value;
  else
    friction = sv_friction.value;

  // apply friction
  control = speed < sv_stopspeed.value ? sv_stopspeed.value : speed;
  newspeed = speed - host_frametime * control * friction;

  if (newspeed < 0)
    newspeed = 0;
  newspeed /= speed;

  vel[0] = vel[0] * newspeed;
  vel[1] = vel[1] * newspeed;
  vel[2] = vel[2] * newspeed;
}

/*
==============
SV_Accelerate
==============
*/
cvar_t sv_maxspeed = {"sv_maxspeed", "320", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_accelerate = {"sv_accelerate", "10", CVAR_NONE};
void SV_Accelerate(float wishspeed, const vec3_t wishdir) {
  int i;
  float addspeed, accelspeed, currentspeed;

  currentspeed = DotProduct(velocity, wishdir);
  addspeed = wishspeed - currentspeed;
  if (addspeed <= 0)
    return;
  accelspeed = sv_accelerate.value * host_frametime * wishspeed;
  if (accelspeed > addspeed)
    accelspeed = addspeed;

  for (i = 0; i < 3; i++)
    velocity[i] += accelspeed * wishdir[i];
}

void SV_AirAccelerate(float wishspeed, vec3_t wishveloc) {
  int i;
  float addspeed, wishspd, accelspeed, currentspeed;

  wishspd = VectorNormalize(wishveloc);
  if (wishspd > 30)
    wishspd = 30;
  currentspeed = DotProduct(velocity, wishveloc);
  addspeed = wishspd - currentspeed;
  if (addspeed <= 0)
    return;
  //	accelspeed = sv_accelerate.value * host_frametime;
  accelspeed = sv_accelerate.value * wishspeed * host_frametime;
  if (accelspeed > addspeed)
    accelspeed = addspeed;

  for (i = 0; i < 3; i++)
    velocity[i] += accelspeed * wishveloc[i];
}

void DropPunchAngle(void) {
  float len;

  len = VectorNormalize(sv_player->v.punchangle);

  len -= 10 * host_frametime;
  if (len < 0)
    len = 0;
  VectorScale(sv_player->v.punchangle, len, sv_player->v.punchangle);
}

/*
===================
SV_WaterMove

===================
*/
static qboolean sv_gorilla_swim_intent;

void SV_WaterMove(void) {
  int i;
  vec3_t wishvel;
  float speed, newspeed, wishspeed, addspeed, accelspeed;
  float upmove;
  qboolean onladder;

  //
  // user intentions
  //
  AngleVectors(sv_player->v.v_angle, forward, right, up);

  for (i = 0; i < 3; i++)
    wishvel[i] = forward[i] * cmd.forwardmove + right[i] * cmd.sidemove;

  upmove = SV_WaterUpMove();
  onladder = SV_PlayerOnLadder();

  if (onladder) {
    wishvel[2] *= 1 + fabs(wishvel[2] / 200) * 9;
    if (sv_player->v.button2)
      wishvel[2] += 400;
  }

  if (!cmd.forwardmove && !cmd.sidemove && !upmove && !onladder &&
      !sv_gorilla_swim_intent)
    wishvel[2] -= 60; // drift towards bottom
  else
    wishvel[2] += upmove;

  wishspeed = VectorLength(wishvel);
  if (wishspeed > sv_maxspeed.value) {
    VectorScale(wishvel, sv_maxspeed.value / wishspeed, wishvel);
    wishspeed = sv_maxspeed.value;
  }
  wishspeed *= 0.7;

  //
  // water friction
  //
  speed = VectorLength(velocity);
  if (speed) {
    newspeed = speed - host_frametime * speed * sv_friction.value;
    if (newspeed < 0)
      newspeed = 0;
    VectorScale(velocity, newspeed / speed, velocity);
  } else
    newspeed = 0;

  //
  // water acceleration
  //
  if (!wishspeed)
    return;

  addspeed = wishspeed - newspeed;
  if (addspeed <= 0)
    return;

  VectorNormalize(wishvel);
  accelspeed = sv_accelerate.value * wishspeed * host_frametime;
  if (accelspeed > addspeed)
    accelspeed = addspeed;

  for (i = 0; i < 3; i++)
    velocity[i] += accelspeed * wishvel[i];
}

void SV_WaterJump(void) {
  float jumpvelocity;

  if (qcvm->time > sv_player->v.teleport_time || !sv_player->v.waterlevel) {
    sv_player->v.flags = (int)sv_player->v.flags & ~FL_WATERJUMP;
    sv_player->v.teleport_time = 0;
  }
  sv_player->v.velocity[0] = sv_player->v.movedir[0];
  sv_player->v.velocity[1] = sv_player->v.movedir[1];
  jumpvelocity = SV_VANILLA_WATERJUMP_VELOCITY * SV_VRJumpScale();
  if (sv_player->v.velocity[2] > 0 &&
      sv_player->v.velocity[2] < jumpvelocity)
    sv_player->v.velocity[2] = jumpvelocity;
}

/*
===================
SV_NoclipMove -- johnfitz

new, alternate noclip. old noclip is still handled in SV_AirMove
===================
*/
void SV_NoclipMove(void) {
  AngleVectors(sv_player->v.v_angle, forward, right, up);

  velocity[0] = forward[0] * cmd.forwardmove + right[0] * cmd.sidemove;
  velocity[1] = forward[1] * cmd.forwardmove + right[1] * cmd.sidemove;
  velocity[2] = forward[2] * cmd.forwardmove + right[2] * cmd.sidemove;
  velocity[2] += cmd.upmove * 2; // doubled to match running speed

  if (VectorLength(velocity) > sv_maxspeed.value) {
    VectorNormalize(velocity);
    VectorScale(velocity, sv_maxspeed.value, velocity);
  }
}

/*
===================
SV_AirMove
===================
*/
void SV_AirMove(void) {
  int i;
  vec3_t wishvel, wishdir;
  float wishspeed;
  float fmove, smove;

  AngleVectors(sv_player->v.angles, forward, right, up);

  fmove = cmd.forwardmove;
  smove = cmd.sidemove;

  // hack to not let you back into teleporter
  if (qcvm->time < sv_player->v.teleport_time && fmove < 0)
    fmove = 0;

  for (i = 0; i < 3; i++)
    wishvel[i] = forward[i] * fmove + right[i] * smove;

  if ((int)sv_player->v.movetype != MOVETYPE_WALK)
    wishvel[2] = cmd.upmove;
  else
    wishvel[2] = 0;

  VectorCopy(wishvel, wishdir);
  wishspeed = VectorNormalize(wishdir);
  if (wishspeed > sv_maxspeed.value) {
    VectorScale(wishvel, sv_maxspeed.value / wishspeed, wishvel);
    wishspeed = sv_maxspeed.value;
  }

  if (sv_player->v.movetype == MOVETYPE_NOCLIP) { // noclip
    VectorCopy(wishvel, velocity);
  } else if (onground) {
    if (vr_movement_instant_stop.value && host_client &&
        host_client->is_vr_client && wishspeed == 0 &&
        !SV_GorillaEligible(host_client)) {
      velocity[0] = 0;
      velocity[1] = 0;
    } else {
      SV_UserFriction();
      SV_Accelerate(wishspeed, wishdir);
    }
  } else { // not on ground, so little effect on velocity
    SV_AirAccelerate(wishspeed, wishvel);
  }
}

static void SV_ConsumeClientMove(void) {
  if ((int)sv_player->v.flags & FL_WATERJUMP) {
    SV_WaterJump();
    return;
  }
  if (sv_player->v.movetype == MOVETYPE_NOCLIP && sv_altnoclip.value)
    SV_NoclipMove();
  else if ((sv_player->v.waterlevel >= 2 || SV_PlayerOnLadder()) &&
           sv_player->v.movetype != MOVETYPE_NOCLIP)
    SV_WaterMove();
  else
    SV_AirMove();
}

static void SV_GorillaConsumeDeferredMove(client_t *client,
    qboolean water_step, qboolean swim_intent) {
  client_t *saved_client;
  edict_t *saved_player;
  usercmd_t saved_cmd;
  float *saved_origin, *saved_velocity, *saved_angles;
  qboolean saved_onground;
  qboolean gorilla;
  qboolean saved_swim_intent;
  vec3_t saved_forward, saved_right, saved_up;
  if (!client || !client->vr_gorilla_move_deferred)
    return;
  if (!client->edict || client->edict->free || client->usingpmove ||
      client->edict->v.movetype == MOVETYPE_NONE || client->edict->v.health <= 0) {
    client->vr_gorilla_move_deferred = false;
    return;
  }
  gorilla = SV_GorillaEligible(client);
  /* Wait for the ordered hand samples before deciding idle sinking. A
   * ladder transition instead resumes its original native input now. */
  if (gorilla && !water_step)
    return;
  client->vr_gorilla_move_deferred = false;

  /* PlayerPreThink may have entered a ladder/water/custom state after the
   * command was deferred. Replay only normal movement consumption, never QC,
   * angle setup or a second full SV_ClientThink pass. */
  /* SV_Physics_Client iterates independently of SV_RunClients: these globals
   * can still describe the last client in that earlier loop. Scope all native
   * movement context to this player, then restore the caller's context. */
  saved_client = host_client;
  saved_player = sv_player;
  saved_cmd = cmd;
  saved_origin = origin;
  saved_velocity = velocity;
  saved_angles = angles;
  saved_onground = onground;
  saved_swim_intent = sv_gorilla_swim_intent;
  VectorCopy(forward, saved_forward);
  VectorCopy(right, saved_right);
  VectorCopy(up, saved_up);
  host_client = client;
  sv_player = client->edict;
  cmd = client->cmd;
  /* Keep native drag, sinking and waterjump maintenance once per frame.
   * Only the joystick vector is suppressed; the raw accepted command stays
   * intact for native ladder transitions and mod button semantics. */
  if (gorilla)
    cmd.forwardmove = cmd.sidemove = cmd.upmove = 0;
  sv_gorilla_swim_intent = gorilla && swim_intent;
  onground = (int)sv_player->v.flags & FL_ONGROUND;
  origin = sv_player->v.origin;
  velocity = sv_player->v.velocity;
  angles = sv_player->v.angles;
  SV_ConsumeClientMove();
  host_client = saved_client;
  sv_player = saved_player;
  cmd = saved_cmd;
  origin = saved_origin;
  velocity = saved_velocity;
  angles = saved_angles;
  onground = saved_onground;
  sv_gorilla_swim_intent = saved_swim_intent;
  VectorCopy(saved_forward, forward);
  VectorCopy(saved_right, right);
  VectorCopy(saved_up, up);
}

void SV_GorillaResumeDeferredMove(client_t *client) {
  SV_GorillaConsumeDeferredMove(client, false, false);
}

void SV_GorillaConsumeWater(client_t *client, qboolean swim_intent) {
  SV_GorillaConsumeDeferredMove(client, true, swim_intent);
}

/*
===================
SV_ClientThink

the move fields specify an intended velocity in pix/sec
the angle fields specify an exact angular motion in degrees
===================
*/
void SV_ClientThink(void) {
  vec3_t v_angle;

  cmd = host_client->cmd;
  host_client->vr_gorilla_move_deferred = false;
  SV_GorillaLatchLadder(host_client, true);

  if (sv_player->v.movetype == MOVETYPE_NONE)
    return;

  onground = (int)sv_player->v.flags & FL_ONGROUND;

  origin = sv_player->v.origin;
  velocity = sv_player->v.velocity;

  DropPunchAngle();

  if (host_client->usingpmove) {
    if (!sv_player->v.fixangle) {
      VectorAdd(sv_player->v.v_angle, sv_player->v.punchangle, v_angle);
      sv_player->v.angles[ROLL] =
          V_CalcRoll(sv_player->v.angles, sv_player->v.velocity) * 4;
      sv_player->v.angles[PITCH] = -v_angle[PITCH] / 3;
      sv_player->v.angles[YAW] = v_angle[YAW];
    }
    return;
  }

  //
  // if dead, behave differently
  //
  if (sv_player->v.health <= 0)
    return;

  //
  // angles
  // show 1/3 the pitch angle and all the roll angle
  angles = sv_player->v.angles;

  VectorAdd(sv_player->v.v_angle, sv_player->v.punchangle, v_angle);
  angles[ROLL] = V_CalcRoll(sv_player->v.angles, sv_player->v.velocity) * 4;
  if (!sv_player->v.fixangle) {
    angles[PITCH] = -v_angle[PITCH] / 3;
    angles[YAW] = v_angle[YAW];
  }

  if (SV_GorillaEligible(host_client)) {
    /* Preserve raw cmd for QC. The physics owner consumes Gorilla after
     * PlayerPreThink; if QC establishes a non-Gorilla state, it calls the
     * narrowly scoped resume API below exactly once. */
    host_client->vr_gorilla_move_deferred = true;
    return;
  }
  SV_ConsumeClientMove();
}

/*
===================
SV_ReadClientMove
===================
*/
static int SV_ExpandClientSequence(int sequence16) {
  int last;
  int sequence;

  sequence16 &= 0xffff;
  last = host_client->lastacceptedmovemessage;
  if (last < 0)
    return sequence16;

  sequence = (last & ~0xffff) | sequence16;
  /* Choose the representation nearest the last accepted sequence.  This
   * maps redundant records immediately before a 16-bit wrap back into the
   * previous epoch instead of misreading 65535 as 131071. */
  if (sequence - last > 0x8000)
    sequence -= 0x10000;
  else if (last - sequence > 0x8000)
    sequence += 0x10000;

  return sequence;
}

static qboolean SV_ReadUsercmd(usercmd_t *readcmd, int sequence) {
  int i;
  int extbits;

  Q_memset(readcmd, 0, sizeof(*readcmd));
  readcmd->sequence = sequence;
  readcmd->servertime = MSG_ReadFloat();
  if (host_client->protocol_pext2 & PEXT2_EXPLICITCMDMSEC) {
    readcmd->msec = MSG_ReadByte();
    if (readcmd->msec < 1 || readcmd->msec > 125) {
      msg_badread = true;
      return false;
    }
  }

  for (i = 0; i < 3; i++)
    readcmd->viewangles[i] = MSG_ReadAngle16(sv.protocolflags);

  readcmd->forwardmove = MSG_ReadShort();
  readcmd->sidemove = MSG_ReadShort();
  readcmd->upmove = MSG_ReadShort();
  readcmd->buttons = MSG_ReadByte();
  readcmd->impulse = MSG_ReadByte();

  extbits = MSG_ReadByte();
  if (extbits & ~(MOVEEXT_VR | MOVEEXT_VR_RELATIVE | MOVEEXT_QCINPUT |
                  MOVEEXT_VR_AKIMBO | MOVEEXT_VR_AKIMBO_BERSERK |
                  MOVEEXT_VR_CONTACT | MOVEEXT_VR_GORILLA)) {
    msg_badread = true;
    return false;
  }

  if ((extbits & MOVEEXT_VR_RELATIVE) && !(extbits & MOVEEXT_VR)) {
    msg_badread = true;
    return false;
  }
  if ((extbits & MOVEEXT_VR_AKIMBO_BERSERK) && !(extbits & MOVEEXT_VR_AKIMBO)) {
    msg_badread = true;
    return false;
  }
  if ((extbits & MOVEEXT_VR_AKIMBO) != 0 &&
      (extbits & (MOVEEXT_VR | MOVEEXT_VR_RELATIVE)) !=
          (MOVEEXT_VR | MOVEEXT_VR_RELATIVE)) {
    msg_badread = true;
    return false;
  }
  if ((extbits & MOVEEXT_VR_CONTACT) != 0 &&
      (extbits & (MOVEEXT_VR | MOVEEXT_VR_RELATIVE)) !=
          (MOVEEXT_VR | MOVEEXT_VR_RELATIVE)) {
    msg_badread = true;
    return false;
  }
  if ((extbits & MOVEEXT_VR_GORILLA) != 0 &&
      (extbits & (MOVEEXT_VR | MOVEEXT_VR_RELATIVE)) !=
          (MOVEEXT_VR | MOVEEXT_VR_RELATIVE)) {
    msg_badread = true;
    return false;
  }

  if (extbits & MOVEEXT_VR) {
    if (net_message.cursize - msg_readcount < 9 * 4) {
      msg_badread = true;
      return false;
    }

    readcmd->vr_active = true;
    readcmd->vr_handpos_relative = (extbits & MOVEEXT_VR_RELATIVE) != 0;
    readcmd->vr_handpos[0] = MSG_ReadFloat();
    readcmd->vr_handpos[1] = MSG_ReadFloat();
    readcmd->vr_handpos[2] = MSG_ReadFloat();
    readcmd->vr_handrot[0] = MSG_ReadFloat();
    readcmd->vr_handrot[1] = MSG_ReadFloat();
    readcmd->vr_handrot[2] = MSG_ReadFloat();
    readcmd->vr_roomscalemove[0] = MSG_ReadFloat();
    readcmd->vr_roomscalemove[1] = MSG_ReadFloat();
    readcmd->vr_roomscalemove[2] = MSG_ReadFloat();
    for (i = 0; i < 3; i++) {
      if (!isfinite(readcmd->vr_handpos[i]) ||
          !isfinite(readcmd->vr_handrot[i]) ||
          !isfinite(readcmd->vr_roomscalemove[i])) {
        msg_badread = true;
        return false;
      }
    }
  }

  if (extbits & MOVEEXT_VR_AKIMBO) {
    if (net_message.cursize - msg_readcount < 12 * 4) {
      msg_badread = true;
      return false;
    }
    readcmd->vr_akimbo_active = true;
    readcmd->vr_akimbo_berserk = (extbits & MOVEEXT_VR_AKIMBO_BERSERK) != 0;
    for (i = 0; i < 2; i++) {
      readcmd->vr_akimbo_muzzle[i][0] = MSG_ReadFloat();
      readcmd->vr_akimbo_muzzle[i][1] = MSG_ReadFloat();
      readcmd->vr_akimbo_muzzle[i][2] = MSG_ReadFloat();
    }
    for (i = 0; i < 2; i++) {
      readcmd->vr_akimbo_angles[i][0] = MSG_ReadFloat();
      readcmd->vr_akimbo_angles[i][1] = MSG_ReadFloat();
      readcmd->vr_akimbo_angles[i][2] = MSG_ReadFloat();
    }
    for (i = 0; i < 2; i++) {
      if (!isfinite(readcmd->vr_akimbo_muzzle[i][0]) ||
          !isfinite(readcmd->vr_akimbo_muzzle[i][1]) ||
          !isfinite(readcmd->vr_akimbo_muzzle[i][2]) ||
          !isfinite(readcmd->vr_akimbo_angles[i][0]) ||
          !isfinite(readcmd->vr_akimbo_angles[i][1]) ||
          !isfinite(readcmd->vr_akimbo_angles[i][2])) {
        msg_badread = true;
        return false;
      }
    }
  }

  if (extbits & MOVEEXT_QCINPUT) {
    if (net_message.cursize - msg_readcount < 34) {
      msg_badread = true;
      return false;
    }

    readcmd->weapon = MSG_ReadLong();
    readcmd->cursor_screen[0] = MSG_ReadShort() / 32767.0f;
    readcmd->cursor_screen[1] = MSG_ReadShort() / 32767.0f;
    readcmd->cursor_start[0] = MSG_ReadFloat();
    readcmd->cursor_start[1] = MSG_ReadFloat();
    readcmd->cursor_start[2] = MSG_ReadFloat();
    readcmd->cursor_impact[0] = MSG_ReadFloat();
    readcmd->cursor_impact[1] = MSG_ReadFloat();
    readcmd->cursor_impact[2] = MSG_ReadFloat();
    readcmd->cursor_entitynumber = MSG_ReadEntity(host_client->protocol_pext2);
  }

  if (extbits & MOVEEXT_VR_CONTACT) {
    unsigned int flags;

    if (net_message.cursize - msg_readcount < 7) {
      msg_badread = true;
      return false;
    }
    flags = (unsigned int)MSG_ReadByte();
    if ((flags & ~VR_WEAPON_CONTACT_KNOWN_FLAGS) ||
        !(flags & (VR_WEAPON_CONTACT_LEFT_VALID |
                   VR_WEAPON_CONTACT_RIGHT_VALID))) {
      msg_badread = true;
      return false;
    }
    readcmd->vr_contact.flags = flags;
    readcmd->vr_contact.modelindex = (unsigned short)MSG_ReadShort();
    readcmd->vr_contact.weapon = MSG_ReadFloat();
    if (!isfinite(readcmd->vr_contact.weapon)) {
      msg_badread = true;
      return false;
    }
    for (i = 0; i < 2; i++) {
      if (!(flags & (1 << i)))
        continue;
      if (net_message.cursize - msg_readcount < 10 * 4) {
        msg_badread = true;
        return false;
      }
      readcmd->vr_contact.grip[i][0] = MSG_ReadFloat();
      readcmd->vr_contact.grip[i][1] = MSG_ReadFloat();
      readcmd->vr_contact.grip[i][2] = MSG_ReadFloat();
      readcmd->vr_contact.base[i][0] = MSG_ReadFloat();
      readcmd->vr_contact.base[i][1] = MSG_ReadFloat();
      readcmd->vr_contact.base[i][2] = MSG_ReadFloat();
      readcmd->vr_contact.tip[i][0] = MSG_ReadFloat();
      readcmd->vr_contact.tip[i][1] = MSG_ReadFloat();
      readcmd->vr_contact.tip[i][2] = MSG_ReadFloat();
      readcmd->vr_contact.speed[i] = MSG_ReadFloat();
      if (!isfinite(readcmd->vr_contact.grip[i][0]) ||
          !isfinite(readcmd->vr_contact.grip[i][1]) ||
          !isfinite(readcmd->vr_contact.grip[i][2]) ||
          !isfinite(readcmd->vr_contact.base[i][0]) ||
          !isfinite(readcmd->vr_contact.base[i][1]) ||
          !isfinite(readcmd->vr_contact.base[i][2]) ||
          !isfinite(readcmd->vr_contact.tip[i][0]) ||
          !isfinite(readcmd->vr_contact.tip[i][1]) ||
          !isfinite(readcmd->vr_contact.tip[i][2]) ||
          !isfinite(readcmd->vr_contact.speed[i])) {
        msg_badread = true;
        return false;
      }
    }
  }

  if (extbits & MOVEEXT_VR_GORILLA) {
	vec3_t arm;
	int hand;
	float length2;

    if (net_message.cursize - msg_readcount < 1 + 15 * 4) {
      msg_badread = true;
      return false;
    }
    readcmd->vr_gorilla.flags = (unsigned char)MSG_ReadByte();
    for (i = 0; i < 3; i++)
      readcmd->vr_gorilla.head[i] = MSG_ReadFloat();
    for (i = 0; i < 2; i++) {
      readcmd->vr_gorilla.hand[i][0] = MSG_ReadFloat();
      readcmd->vr_gorilla.hand[i][1] = MSG_ReadFloat();
      readcmd->vr_gorilla.hand[i][2] = MSG_ReadFloat();
      readcmd->vr_gorilla.velocity[i][0] = MSG_ReadFloat();
      readcmd->vr_gorilla.velocity[i][1] = MSG_ReadFloat();
      readcmd->vr_gorilla.velocity[i][2] = MSG_ReadFloat();
    }
	if ((readcmd->vr_gorilla.flags & ~VR_GORILLA_FLAGS) ||
		(readcmd->vr_gorilla.flags & VR_GORILLA_HANDS) != VR_GORILLA_HANDS ||
		!isfinite(readcmd->vr_gorilla.head[0]) ||
		!isfinite(readcmd->vr_gorilla.head[1]) ||
		!isfinite(readcmd->vr_gorilla.head[2])) {
      msg_badread = true;
      return false;
    }
	length2 = DotProduct(readcmd->vr_gorilla.head, readcmd->vr_gorilla.head);
	if (length2 > 160.0f * 160.0f) {
		msg_badread = true;
		return false;
	}
    for (hand = 0; hand < 2; hand++) {
		if (!isfinite(readcmd->vr_gorilla.hand[hand][0]) ||
			!isfinite(readcmd->vr_gorilla.hand[hand][1]) ||
			!isfinite(readcmd->vr_gorilla.hand[hand][2]) ||
			!isfinite(readcmd->vr_gorilla.velocity[hand][0]) ||
			!isfinite(readcmd->vr_gorilla.velocity[hand][1]) ||
			!isfinite(readcmd->vr_gorilla.velocity[hand][2])) {
			msg_badread = true;
			return false;
		}
		length2 = DotProduct(readcmd->vr_gorilla.velocity[hand],
			readcmd->vr_gorilla.velocity[hand]);
		if (length2 > VR_GORILLA_MAX_HAND_SPEED * VR_GORILLA_MAX_HAND_SPEED) {
			msg_badread = true;
			return false;
		}
		VectorSubtract(readcmd->vr_gorilla.hand[hand],
			readcmd->vr_gorilla.head, arm);
		length2 = DotProduct(arm, arm);
		if (length2 > VR_GORILLA_MAX_REACH * VR_GORILLA_MAX_REACH) {
			msg_badread = true;
			return false;
		}
	}
  }

  return !msg_badread;
}

static void SV_NormalizeAcceptedUsercmd(client_t *client, usercmd_t *acceptedcmd)
{
  float fallback_seconds;
  double timestamp;
  double seconds;

  client->net_move_last_servertime = acceptedcmd->servertime;
  client->net_move_last_msec = acceptedcmd->msec;
  if (client->protocol_pext2 & PEXT2_EXPLICITCMDMSEC) {
    /* Do not reconstruct lost sequence time from the diagnostic timestamp. */
    acceptedcmd->seconds = acceptedcmd->msec * 0.001f;
    return;
  }

  fallback_seconds = CLAMP(0.001f, host_frametime, 0.1f);
  if (acceptedcmd->servertime <= 0)
  {
    acceptedcmd->seconds = fallback_seconds;
    return;
  }

  timestamp = acceptedcmd->servertime;
  if (timestamp > qcvm->time)
    timestamp = qcvm->time;
  if (timestamp < qcvm->time - 0.5)
    timestamp = qcvm->time - 0.5;

  if (client->lastmovetime < qcvm->time - 0.5)
    client->lastmovetime = qcvm->time - 0.5;
  if (client->lastmovetime <= 0)
    client->lastmovetime = timestamp - fallback_seconds;
  if (timestamp < client->lastmovetime)
    timestamp = client->lastmovetime;

  seconds = timestamp - client->lastmovetime;
  if (seconds < 0)
    seconds = 0;
  if (seconds > 0.5)
    seconds = 0.5;
  acceptedcmd->seconds = (float)seconds;
  client->lastmovetime = timestamp;
}

static void SV_SyncPMovePresentationAngles(client_t *client) {
  vec3_t v_angle;
  edict_t *ent;

  if (!client || !client->usingpmove)
    return;
  ent = client->edict;
  if (!ent || ent->v.fixangle)
    return;

  VectorAdd(ent->v.v_angle, ent->v.punchangle, v_angle);
  ent->v.angles[ROLL] = V_CalcRoll(ent->v.angles, ent->v.velocity) * 4;
  ent->v.angles[PITCH] = -v_angle[PITCH] / 3;
  ent->v.angles[YAW] = v_angle[YAW];
}

static void SV_ApplyAcceptedUsercmd(client_t *client, const usercmd_t *acceptedcmd) {
  client->cmd = *acceptedcmd;
  VectorCopy(client->cmd.viewangles, client->edict->v.v_angle);
  /* Publish the body orientation with the queued command before its next
   * physics-frame PMove execution can produce an entity update. */
  SV_SyncPMovePresentationAngles(client);
  client->edict->v.button0 = client->cmd.buttons & 1;
  client->edict->v.button2 = (client->cmd.buttons & 2) >> 1;
  SV_SetExtendedButtons(client->edict, client->cmd.buttons);
  if (client->cmd.impulse)
    client->edict->v.impulse = client->cmd.impulse;

  if (acceptedcmd->vr_active) {
    client->is_vr_client = true;
    client->vr_handpos_relative = acceptedcmd->vr_handpos_relative;
    VectorCopy(acceptedcmd->vr_handpos, client->vr_handpos);
    VectorCopy(acceptedcmd->vr_handrot, client->vr_handrot);
    VectorCopy(acceptedcmd->vr_roomscalemove, client->vr_roomscalemove);
    VectorAdd(client->vr_roomscale_accum, acceptedcmd->vr_roomscalemove,
              client->vr_roomscale_accum);
  } else {
    client->is_vr_client = false;
    client->vr_handpos_relative = false;
    VectorCopy(vec3_origin, client->vr_handpos);
    VectorCopy(vec3_origin, client->vr_handrot);
    VectorCopy(vec3_origin, client->vr_roomscalemove);
  }
}

void SV_LoadQueuedPMoveUsercmd(client_t *client) {
  usercmd_t *queuedcmd;

  if (!client->move_queue_count) {
    client->pendingmovemessage = -1;
    client->move_pending = false;
    client->cmd.seconds = 0;
    return;
  }

  queuedcmd = &client->move_queue[client->move_queue_head];
  /* Reloading a deferred head must replace, not accumulate, its tracking. */
  VectorClear(client->vr_roomscale_accum);
  SV_ApplyAcceptedUsercmd(client, queuedcmd);
  client->pendingmovemessage = queuedcmd->sequence;
  client->move_pending = true;
}

static qboolean SV_QueuePMoveUsercmd(client_t *client,
                                     const usercmd_t *acceptedcmd) {
  unsigned int tail;

  if (client->move_queue_count >= MOVE_BUNDLE_MAX) {
    /* Never overwrite an unsimulated pose/input record.  Drop the queue as a
     * discontinuity and retain the newest command as the restart point. */
    SV_ClearClientPMoveState(client);
    SV_VRContactResetClient(client);
    SV_VRGorillaResetClient(client);
    VectorCopy(vec3_origin, client->cmd.vr_roomscalemove);
    client->cmd.vr_akimbo_active = false;
    client->cmd.vr_akimbo_berserk = false;
    VectorClear(client->cmd.vr_akimbo_muzzle[0]);
    VectorClear(client->cmd.vr_akimbo_muzzle[1]);
    VectorClear(client->cmd.vr_akimbo_angles[0]);
    VectorClear(client->cmd.vr_akimbo_angles[1]);
    VectorCopy(vec3_origin, client->vr_roomscalemove);
    VectorCopy(vec3_origin, client->vr_roomscale_accum);
    client->move_discontinuity_epoch++;
    client->move_discontinuity_reason = MOVEACK_DISCONTINUITY_GAP;
    client->net_move_queue_overflows++;
  }

  tail = (client->move_queue_head + client->move_queue_count) % MOVE_BUNDLE_MAX;
  client->move_queue[tail] = *acceptedcmd;
  client->move_queue_count++;
  if (client->move_queue_count == 1)
    SV_LoadQueuedPMoveUsercmd(client);
  return true;
}

/* Validate each fresh sample before batching; a frame may legitimately
 * accumulate several individually valid tracking displacements. */
static void SV_ValidateVRRoomScaleUsercmd(client_t *client, usercmd_t *cmd) {
  float horizontal = sqrtf(cmd->vr_roomscalemove[0] * cmd->vr_roomscalemove[0] +
                           cmd->vr_roomscalemove[1] * cmd->vr_roomscalemove[1]);

  if (!isfinite(horizontal) || !isfinite(cmd->vr_roomscalemove[2]) ||
      fabsf(cmd->vr_roomscalemove[2]) > 16.0f || horizontal > 16.0f) {
    VectorClear(cmd->vr_roomscalemove);
    SV_VRContactResetClient(client);
    SV_VRGorillaResetClient(client);
    client->move_discontinuity_epoch++;
    client->move_discontinuity_reason = MOVEACK_DISCONTINUITY_TRACKING_OUTLIER;
    client->net_move_roomscale_outliers++;
  }
}

static qboolean SV_AcceptPMoveUsercmd(client_t *client,
                                      const usercmd_t *acceptedcmd) {
  usercmd_t pmovecmd;
  qboolean has_input;

  pmovecmd = *acceptedcmd;
  pmovecmd.vr_contact_received = realtime;
  SV_NormalizeAcceptedUsercmd(client, &pmovecmd);
  SV_ValidateVRRoomScaleUsercmd(client, &pmovecmd);
  if (!SV_QueuePMoveUsercmd(client, &pmovecmd))
    return false;

  client->last_move_time = realtime;
  client->input_stale = false;
  client->lastacceptedmovemessage = pmovecmd.sequence;

  has_input = pmovecmd.forwardmove || pmovecmd.sidemove || pmovecmd.upmove ||
              pmovecmd.buttons || pmovecmd.impulse ||
              pmovecmd.vr_roomscalemove[0] ||
              pmovecmd.vr_roomscalemove[1] ||
              pmovecmd.vr_roomscalemove[2];
  if (net_lagdebug.value && has_input &&
      (pmovecmd.impulse ||
       realtime - client->net_move_input_log_time > 0.25)) {
    Con_Printf("net_lagdebug: accepted input from %s seq=%d q=direct pmove=1 move=(%g,%g,%g) buttons=%d impulse=%d seconds=%.3f vr=%d vrmove=(%.3f,%.3f,%.3f)\n",
               client->name, pmovecmd.sequence,
               pmovecmd.forwardmove, pmovecmd.sidemove, pmovecmd.upmove,
               pmovecmd.buttons, pmovecmd.impulse, pmovecmd.seconds,
               pmovecmd.vr_active ? 1 : 0, pmovecmd.vr_roomscalemove[0],
               pmovecmd.vr_roomscalemove[1], pmovecmd.vr_roomscalemove[2]);
    client->net_move_input_log_time = realtime;
  }

  return true;
}

static void SV_AcceptLatestUsercmd(client_t *client,
                                   const usercmd_t *acceptedcmd,
                                   qboolean fresh) {
  usercmd_t latestcmd;
  qboolean has_input;

  latestcmd = *acceptedcmd;
  if (fresh)
    latestcmd.vr_contact_received = realtime;
  SV_ValidateVRRoomScaleUsercmd(client, &latestcmd);
  if (fresh)
    SV_VRContactAcceptLegacy(client, &latestcmd);
  if (fresh)
    SV_VRGorillaAcceptLegacy(client, &latestcmd);
  latestcmd.seconds = 0;

  client->net_latest_buttons = latestcmd.buttons;
  /* Preserve a brief jump as well as fire until the frame's QuakeC pass.
   * Redundant/reordered commands have already been rejected by sequence. */
  client->net_latched_buttons |= latestcmd.buttons & (BUTTON_ATTACK | BUTTON_JUMP);
  if (latestcmd.impulse)
    client->net_latched_impulse = latestcmd.impulse;
  latestcmd.buttons |= client->net_latched_buttons;
  if (!latestcmd.impulse)
    latestcmd.impulse = client->net_latched_impulse;

  SV_ApplyAcceptedUsercmd(client, &latestcmd);

  client->last_move_time = realtime;
  client->input_stale = false;
  client->lastmovemessage = latestcmd.sequence;
  client->lastacceptedmovemessage = latestcmd.sequence;
  SV_ClearClientPMoveState(client);

  has_input = latestcmd.forwardmove || latestcmd.sidemove || latestcmd.upmove ||
              latestcmd.buttons || latestcmd.impulse ||
              latestcmd.vr_roomscalemove[0] ||
              latestcmd.vr_roomscalemove[1] ||
              latestcmd.vr_roomscalemove[2];
  if (net_lagdebug.value && has_input &&
      (latestcmd.impulse ||
       realtime - client->net_move_input_log_time > 0.25)) {
    Con_Printf("net_lagdebug: accepted input from %s seq=%d q=latest pmove=0 move=(%g,%g,%g) buttons=%d impulse=%d seconds=0 vr=%d vrmove=(%.3f,%.3f,%.3f)\n",
               client->name, latestcmd.sequence, latestcmd.forwardmove,
               latestcmd.sidemove, latestcmd.upmove, latestcmd.buttons,
               latestcmd.impulse, latestcmd.vr_active ? 1 : 0,
               latestcmd.vr_roomscalemove[0], latestcmd.vr_roomscalemove[1],
               latestcmd.vr_roomscalemove[2]);
    client->net_move_input_log_time = realtime;
  }
}

static void SV_FinishLatestUsercmd(client_t *client) {
  client->net_latched_buttons = 0;
  client->net_latched_impulse = 0;
  client->cmd.buttons = client->net_latest_buttons;
  client->cmd.impulse = 0;
  client->edict->v.button0 = client->cmd.buttons & 1;
  client->edict->v.button2 = (client->cmd.buttons & 2) >> 1;
  SV_SetExtendedButtons(client->edict, client->cmd.buttons);
  client->edict->v.impulse = 0;
}

/*
 * Latest-command clients use the engine's normal movement path, but QuakeC
 * does not consume attack buttons and impulses until PlayerPreThink/
 * PlayerPostThink in SV_Physics.  Finish the one-frame latch only after that
 * physics pass; clearing it in SV_RunClients drops taps and weapon impulses
 * before mods can observe them.
 */
void SV_FinishLatestUsercmds(void) {
  int i;
  client_t *client;

  for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++) {
    if (!client->active || !client->spawned || client->usingpmove)
      continue;
    SV_FinishLatestUsercmd(client);
  }
}

void SV_FinishPMoveUsercmd(client_t *client) {
  client->net_move_cmds_simulated++;
  client->net_move_msec_simulated += client->cmd.msec;
  client->net_move_last_sim_seconds = client->cmd.seconds;
  client->lastmovemessage = client->cmd.sequence;
  if (client->move_queue_count) {
    client->move_queue_head = (client->move_queue_head + 1) % MOVE_BUNDLE_MAX;
    client->move_queue_count--;
  }
  SV_LoadQueuedPMoveUsercmd(client);
}

void SV_ReadClientMove(usercmd_t *move) {
  int sequence16;
  int sequence;
  int gap;
  int accepted_base;
  usercmd_t readcmd;

  sequence16 = MSG_ReadShort() & 0xffff;
  sequence = SV_ExpandClientSequence(sequence16);
  accepted_base = host_client->lastacceptedmovemessage;

  host_client->net_move_cmds_received++;

  if (accepted_base >= 0 && sequence <= accepted_base) {
    host_client->net_move_cmds_stale++;
    if (!SV_ReadUsercmd(&readcmd, sequence))
      return;
    return;
  }

  if (!SV_ReadUsercmd(&readcmd, sequence))
    return;

  gap = accepted_base >= 0 ? sequence - accepted_base - 1 : 0;
  if (gap > 0) {
    host_client->net_move_last_gap = gap;
    host_client->move_discontinuity_epoch++;
    host_client->move_discontinuity_reason = MOVEACK_DISCONTINUITY_GAP;
    SV_VRContactResetClient(host_client);
    SV_VRGorillaResetClient(host_client);
    if (net_lagdebug.value)
      Con_Printf("net_lagdebug: accepted move gap from %s gap=%d seq=%d last=%d\n",
                  host_client->name, gap, sequence, accepted_base);
  }

  SV_UpdateClientPMoveMode(host_client, false);
  if (host_client->usingpmove) {
    if (!SV_AcceptPMoveUsercmd(host_client, &readcmd))
      return;
  } else {
    SV_AcceptLatestUsercmd(host_client, &readcmd, true);
  }

  host_client->moveext = true;
  host_client->net_move_cmds_accepted++;
  host_client->net_move_msec_accepted += readcmd.msec;
  *move = host_client->cmd;
}

static qboolean SV_ClientHasInput(const client_t *client) {
  return client->cmd.forwardmove || client->cmd.sidemove ||
         client->cmd.upmove || client->edict->v.button0 ||
         client->edict->v.button2 || (client->cmd.buttons & ~3) ||
         client->edict->v.impulse ||
         client->vr_roomscalemove[0] || client->vr_roomscalemove[1] ||
         client->vr_roomscalemove[2];
}

static void SV_ClearStaleClientInput(client_t *client) {
  double age;
  double timeout;

  timeout = sv_inputtimeout.value;
  if (timeout <= 0 || client->last_move_time <= 0)
    return;

  age = realtime - client->last_move_time;
  if (age <= timeout) {
    client->input_stale = false;
    return;
  }

  if (!client->input_stale) {
    if (net_lagdebug.value && SV_ClientHasInput(client)) {
      Con_Printf("net_lagdebug: clearing stale input for %s (%s) age=%.3f timeout=%.3f move=(%g,%g,%g) buttons=%g/%g impulse=%g vrmove=(%.3f,%.3f,%.3f)\n",
                 client->name,
                 NET_QSocketGetAddressString(client->netconnection), age,
                 timeout, client->cmd.forwardmove, client->cmd.sidemove,
                 client->cmd.upmove, client->edict->v.button0,
                 client->edict->v.button2, client->edict->v.impulse,
                 client->vr_roomscalemove[0], client->vr_roomscalemove[1],
                 client->vr_roomscalemove[2]);
    }
    client->input_stale = true;
    SV_VRContactResetClient(client);
    SV_VRGorillaResetClient(client);
  }

  client->cmd.forwardmove = 0;
  client->cmd.sidemove = 0;
  client->cmd.upmove = 0;
  client->cmd.buttons = 0;
  client->cmd.impulse = 0;
  client->net_latest_buttons = 0;
  client->net_latched_buttons = 0;
  client->net_latched_impulse = 0;
  VectorClear(client->cmd.vr_roomscalemove);
  client->cmd.vr_akimbo_active = false;
  client->cmd.vr_akimbo_berserk = false;
  VectorClear(client->cmd.vr_akimbo_muzzle[0]);
  VectorClear(client->cmd.vr_akimbo_muzzle[1]);
  VectorClear(client->cmd.vr_akimbo_angles[0]);
  VectorClear(client->cmd.vr_akimbo_angles[1]);
  VectorClear(client->vr_roomscale_accum);
  SV_ClearClientPMoveState(client);
  client->edict->v.button0 = 0;
  client->edict->v.button2 = 0;
  SV_SetExtendedButtons(client->edict, 0);
  client->edict->v.impulse = 0;
  VectorCopy(vec3_origin, client->vr_roomscalemove);
}

/*
===================
SV_ParseClientMessage

Returns false if the client should be killed
===================
*/
static qboolean SV_ClientCommandIs(const char *s, const char *name) {
  size_t len;

  while (*s == ' ' || *s == '\t')
    s++;

  len = strlen(name);
  return !q_strncasecmp(s, name, len) && (unsigned char)s[len] <= ' ';
}

static qboolean SV_AvatarCommandPrefix(const char *s, const char *name)
{
	while (*s == ' ' || *s == '\t')
		s++;
	return !q_strncasecmp(s, name, strlen(name));
}

static qboolean SV_HandleVRIKCapability(const char *s)
{
	const char *value;
	int version;

  if (!SV_ClientCommandIs(s, "vrik_cap"))
    return false;

  value = s;
  while (*value == ' ' || *value == '\t')
    value++;
  while (*value && *value != ' ' && *value != '\t')
    value++;
  while (*value == ' ' || *value == '\t')
    value++;

	if (*value == '3')
		version = 3;
	else if (*value == '2')
		version = 2;
	else
		return true;
	value++;
  while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
    value++;
  if (*value)
    return true;

  /* Capability is latched for the connection. Repeated declarations must
   * not reset sequence or rate-limit state. */
	if (host_client->vrik_capable)
		return true;

	if (vrik_latch_protocol_version((uint8_t)version,
		&host_client->vrik_capable, &host_client->vrik_protocol_version) !=
		VRIK_CODEC_OK)
		return true;
  host_client->vrik_sequence_valid = false;
  host_client->vrik_inactive_sent = false;
  host_client->vrik_generation = 0;
  host_client->vrik_next_accept_time = 0;
	Con_DPrintf("VRIK: client %s negotiated protocol %d\n", host_client->name,
		version);
	return true;
}

static qboolean SV_HandleGorillaCapability(const char *s)
{
  const char *value;

  if (!SV_ClientCommandIs(s, "vr_gorilla_cap"))
    return false;
  value = s;
  while (*value && *value != ' ' && *value != '\t')
    value++;
  while (*value == ' ' || *value == '\t')
    value++;
  if (*value++ != '1')
    return true;
  while (*value == ' ' || *value == '\t' || *value == '\r' ||
         *value == '\n')
    value++;
  if (*value || host_client->vr_gorilla_capable)
    return true;

  host_client->vr_gorilla_capable = true;
  SV_VRGorillaResetClient(host_client);
  Con_DPrintf("Gorilla: client %s negotiated protocol 1\n",
              host_client->name);
  return true;
}

#ifdef USE_VOICECHAT
static qboolean SV_HandleVoiceCapability(const char *s)
{
	const char *value;
	int source_slot;

	if (!SV_ClientCommandIs(s, "voice_cap"))
		return false;
	value = s;
	while (*value == ' ' || *value == '\t')
		value++;
	while (*value && *value != ' ' && *value != '\t')
		value++;
	while (*value == ' ' || *value == '\t')
		value++;
	if (*value++ != '1')
		return true;
	while (*value == ' ' || *value == '\t' || *value == '\r' ||
		*value == '\n')
		value++;
	if (*value || host_client->voice_capable || !sv_voice.value)
		return true;

	host_client->voice_capable = true;
	/* Do not replay packets which predate this recipient's negotiation. */
	for (source_slot = 0; source_slot < svs.maxclients &&
		source_slot < MAX_SCOREBOARD; ++source_slot)
	{
		host_client->voice_relay_generation[source_slot] =
			svs.clients[source_slot].voice_generation;
		host_client->voice_relay_serial[source_slot] =
			svs.clients[source_slot].voice_next_serial;
	}
	Con_DPrintf("Voice: client %s negotiated protocol %d\n",
		host_client->name, VOICE_PROTOCOL_VERSION);
	return true;
}

static qboolean SV_ReadVoicePacket(qboolean accept)
{
	voice_packet_t packet;
	unsigned int payload_bytes;

	Q_memset(&packet, 0, sizeof(packet));
	packet.sequence = (uint16_t)MSG_ReadShort();
	packet.timestamp = (uint32_t)MSG_ReadLong();
	packet.talkspurt = (uint8_t)MSG_ReadByte();
	packet.flags = (uint8_t)MSG_ReadByte();
	payload_bytes = (uint16_t)MSG_ReadShort();
	if (msg_badread || net_message.cursize - msg_readcount < (int)payload_bytes)
	{
		msg_badread = true;
		return false;
	}
	if (payload_bytes <= VOICE_MAX_PAYLOAD)
		Q_memcpy(packet.payload, net_message.data + msg_readcount, payload_bytes);
	msg_readcount += payload_bytes;
	if (payload_bytes > VOICE_MAX_PAYLOAD ||
		(packet.flags & ~VOICE_FLAG_KNOWN) ||
		(!payload_bytes && !(packet.flags & VOICE_FLAG_END)))
		return true;
	packet.payload_bytes = (uint16_t)payload_bytes;
	if (accept)
		SV_ReceiveVoicePacket(host_client, &packet);
	return true;
}
#endif

static qboolean SV_HandleAvatarCapability(const char *s)
{
	if (!SV_AvatarCommandPrefix(s, "avatar_cap"))
		return false;
	if (!SV_ClientCommandIs(s, "avatar_cap"))
		return true;
	if (!PlayerAvatar_ParseCapabilityCommand(s))
		return true;
	/* Avatar capability is connection-latched.  A duplicate must not trigger
	 * a second signon snapshot or change the negotiated version. */
	if (host_client->avatar_capable)
		return true;
	host_client->avatar_capable = true;
	/* Capability normally arrives during signon, before spawned is set. Queue
	 * the full current table now; SV_FlushAvatarSlots keeps any entries that
	 * do not fit in the reliable buffer for a later send. */
	SV_SendAvatarTable(host_client);
	Con_DPrintf("Avatar: client %s negotiated protocol %d\n", host_client->name,
		PLAYER_AVATAR_PROTOCOL_VERSION);
	return true;
}

static qboolean SV_HandleAvatarSet(const char *s)
{
	int avatar_id;
	qboolean had_custom;

	if (!SV_AvatarCommandPrefix(s, "avatar_set"))
		return false;
	if (!SV_ClientCommandIs(s, "avatar_set"))
		return true;
	if (!host_client->avatar_capable ||
		!PlayerAvatar_ParseSetCommand(s, &avatar_id))
		return true;
	/* The legacy numeric projection of a custom choice is Ranger.  Clear the
	 * authoritative descriptor before the duplicate check so selecting Ranger
	 * after a custom avatar cannot be mistaken for a no-op. */
	had_custom = host_client->avatar_custom_key[0] != 0;
	host_client->avatar_custom_key[0] = 0;
	host_client->avatar_custom_digest[0] = 0;
	if (host_client->avatar_id == avatar_id)
	{
		if (had_custom)
			SV_BroadcastAvatarSlot((int)(host_client - svs.clients), avatar_id);
		return true;
	}
	host_client->avatar_id = (unsigned char)avatar_id;
	SV_BroadcastAvatarSlot((int)(host_client - svs.clients), avatar_id);
	return true;
}

static qboolean SV_HandleCustomAvatarCapability(const char *s)
{
	if (!SV_AvatarCommandPrefix(s, "avatar_custom_cap"))
		return false;
	if (!SV_ClientCommandIs(s, "avatar_custom_cap"))
		return true;
	if (!host_client->avatar_capable ||
		!PlayerAvatar_ParseCustomCapabilityCommand(s))
		return true;
	if (host_client->avatar_custom_capable)
		return true;
	host_client->avatar_custom_capable = true;
	SV_SendAvatarTable(host_client);
	Con_DPrintf("Avatar: client %s negotiated custom protocol %d\n",
		host_client->name, PLAYER_AVATAR_CUSTOM_PROTOCOL_VERSION);
	return true;
}

static qboolean SV_HandleCustomAvatarSet(const char *s)
{
	char key[PLAYER_AVATAR_CUSTOM_KEY_MAX + 1];
	char digest[PLAYER_AVATAR_CUSTOM_DIGEST_MAX + 1];

	if (!SV_AvatarCommandPrefix(s, "avatar_custom_set"))
		return false;
	if (!SV_ClientCommandIs(s, "avatar_custom_set"))
		return true;
	if (!host_client->avatar_capable || !host_client->avatar_custom_capable ||
		!PlayerAvatar_ParseCustomSetCommand(s, key, sizeof(key), digest,
			sizeof(digest)))
		return true;
	if (host_client->avatar_id == PLAYER_AVATAR_RANGER &&
		!strcmp(host_client->avatar_custom_key, key) &&
		!strcmp(host_client->avatar_custom_digest, digest))
		return true;
	strcpy(host_client->avatar_custom_key, key);
	strcpy(host_client->avatar_custom_digest, digest);
	/* Stable custom identities never use local numeric IDs on the wire. */
	host_client->avatar_id = PLAYER_AVATAR_RANGER;
	SV_BroadcastAvatarSlot((int)(host_client - svs.clients),
		PLAYER_AVATAR_RANGER);
	return true;
}

static qboolean SV_ReadVRIKPose(qboolean accept)
{
	vrik_v2_pose_t pose_v2;
	vrik_codec_pose_t pose_v3;
	vrik_codec_status_t status;
	size_t consumed;
	int body_bytes;

	if (host_client->vrik_protocol_version >= VRIK_PROTOCOL_VERSION)
	{
		body_bytes = MSG_ReadByte();
		if (msg_badread || body_bytes < 0 || body_bytes > VRIK_V3_MAX_BODY_BYTES ||
			net_message.cursize - msg_readcount < body_bytes)
		{
			msg_badread = true;
			return false;
		}
		status = vrik_v3_decode(net_message.data + msg_readcount,
			(size_t)body_bytes, &pose_v3, &consumed);
		/* Keep the datagram aligned on a rejected body.  This is deliberately
		 * not a connection error unless its declared bytes were truncated. */
		msg_readcount += body_bytes;
		if (status != VRIK_CODEC_OK || consumed != (size_t)body_bytes)
			return true;
		if (accept)
			SV_ReceiveVRIKPoseV3(host_client, &pose_v3);
		return true;
	}

	if (net_message.cursize - msg_readcount < VRIK_POSE_WIRE_BYTES) {
		msg_badread = true;
		return false;
	}
	status = vrik_v2_decode(net_message.data + msg_readcount,
		VRIK_POSE_WIRE_BYTES, &pose_v2, &consumed);
	msg_readcount += VRIK_POSE_WIRE_BYTES;
	if (status != VRIK_CODEC_OK || consumed != VRIK_POSE_WIRE_BYTES ||
		vrik_v2_validate_legacy_pose(&pose_v2) != VRIK_CODEC_OK)
		return true;
	if (accept)
		SV_ReceiveVRIKPoseV2(host_client, &pose_v2,
			net_message.data + msg_readcount - VRIK_POSE_WIRE_BYTES);
	return true;
}

static void SV_SetClientPredictionStatus(const char *s) {
  const char *value = s;

  while (*value && *value != ' ' && *value != '\t')
    value++;
  while (*value == ' ' || *value == '\t')
    value++;
  host_client->move_client_quarantined = !atoi(value);
  if (host_client->move_client_quarantined) {
    host_client->move_discontinuity_epoch++;
    host_client->move_discontinuity_reason =
        MOVEACK_DISCONTINUITY_CLIENT_QUARANTINE;
  }
}

static qboolean SV_IsEngineClientCommand(const char *s) {
  return SV_ClientCommandIs(s, "download") ||
         SV_ClientCommandIs(s, "sv_startdownload") ||
         SV_ClientCommandIs(s, "nextdl") ||
         SV_ClientCommandIs(s, "pings") ||
         SV_ClientCommandIs(s, "enablecsqc") ||
         SV_ClientCommandIs(s, "disablecsqc");
}

static void SV_ReadQCRequest(void) {
  char args[9];
  int i = 0;
  int type;
  const char *eventname;
  const char *funcname;
  dfunction_t *func;

  while (i < 8) {
    type = MSG_ReadByte();
    if (!type)
      break;
    switch (type) {
    case ev_float:
      args[i] = 'f';
      G_FLOAT(OFS_PARM0 + i * 3) = MSG_ReadFloat();
      break;
    case ev_vector:
      args[i] = 'v';
      G_FLOAT(OFS_PARM0 + i * 3 + 0) = MSG_ReadFloat();
      G_FLOAT(OFS_PARM0 + i * 3 + 1) = MSG_ReadFloat();
      G_FLOAT(OFS_PARM0 + i * 3 + 2) = MSG_ReadFloat();
      break;
    case ev_ext_integer:
      args[i] = 'i';
      G_INT(OFS_PARM0 + i * 3) = MSG_ReadLong();
      break;
    case ev_string:
      args[i] = 's';
      G_INT(OFS_PARM0 + i * 3) = PR_MakeTempString(MSG_ReadString());
      break;
    case ev_entity: {
      unsigned int entnum = MSG_ReadEntity(host_client->protocol_pext2);
      args[i] = 'e';
      if (entnum >= (unsigned int)qcvm->num_edicts)
        entnum = 0;
      G_INT(OFS_PARM0 + i * 3) = EDICT_TO_PROG(EDICT_NUM(entnum));
      break;
    }
    default:
      Con_DPrintf("SV_ReadQCRequest: unsupported argument type %i\n", type);
      MSG_ReadString();
      return;
    }
    i++;
  }

  args[i] = 0;
  eventname = MSG_ReadString();
  funcname = i ? va("CSEv_%s_%s", eventname, args) : va("CSEv_%s", eventname);
  func = ED_FindFunction(funcname);
  if (!func) {
    SV_ClientPrintf("qcrequest \"%s\" not supported\n", funcname);
    return;
  }

  pr_global_struct->time = qcvm->time;
  pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
  PR_ExecuteProgram(func - qcvm->functions);
}

static qboolean SV_ParseClientMessage(void) {
  int ccmd;
  const char *s;
  int movecommands = 0;
  int vrikcommands = 0;
#ifdef USE_VOICECHAT
  int voicecommands = 0;
#endif

  MSG_BeginReading();

  while (1) {
    if (!host_client->active)
      return false; // a command caused an error

    if (msg_badread) {
      Sys_Printf("SV_ReadClientMessage: badread\n");
      return false;
    }

    ccmd = MSG_ReadChar();

    switch (ccmd) {
    case -1:
      if (movecommands > 0) {
        host_client->net_move_packets_received++;
        host_client->net_move_last_bundle = movecommands;
        if (host_client->net_move_bundle_max < movecommands)
          host_client->net_move_bundle_max = movecommands;
      }
      return true; // end of message

    default:
      Sys_Printf("SV_ReadClientMessage: unknown command char\n");
      return false;

    case clc_nop:
      //				Sys_Printf ("clc_nop\n");
      break;

    case clc_stringcmd:
      s = MSG_ReadString();
      if (SV_ClientCommandIs(s, "predstatus"))
        SV_SetClientPredictionStatus(s);
		else if (SV_HandleAvatarCapability(s))
			;
		else if (SV_HandleCustomAvatarCapability(s))
			;
		else if (SV_HandleAvatarSet(s))
			;
		else if (SV_HandleCustomAvatarSet(s))
			;
      else if (SV_HandleVRIKCapability(s))
        ;
      else if (SV_HandleGorillaCapability(s))
        ;
#ifdef USE_VOICECHAT
      else if (SV_HandleVoiceCapability(s))
        ;
#endif
      else if (SV_IsEngineClientCommand(s))
        Cmd_ExecuteString(s, src_client);
      else if (!SV_ClientCommandIs(s, "spawn") &&
          !SV_ClientCommandIs(s, "begin") &&
          !SV_ClientCommandIs(s, "prespawn") &&
          qcvm->extfuncs.SV_ParseClientCommand) {
        client_t *ohc = host_client;
        G_INT(OFS_PARM0) = PR_SetEngineString(s);
        pr_global_struct->time = qcvm->time;
        pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
        PR_ExecuteProgram(qcvm->extfuncs.SV_ParseClientCommand);
        host_client = ohc;
      }
      else
        Cmd_ExecuteString(s, src_client);
      break;

    case clc_disconnect:
      //	Sys_Printf ("SV_ReadClientMessage: client disconnected\n");
      return false;

    case clc_move:
      if (!host_client->spawned)
        return true;
      movecommands++;
      SV_ReadClientMove(&host_client->cmd);
      break;

    case clc_vrikpose:
      /* One freshest pose per received datagram is enough.  Consume extras
       * so a malicious peer cannot turn packet contents into extra relay
       * rate, while retaining normal parsing alignment. */
      if (!SV_ReadVRIKPose(vrikcommands++ == 0))
        return false;
      break;

#ifdef USE_VOICECHAT
    case clc_voice:
      /* Normal 72 Hz client cadence produces at most one 50 Hz voice frame,
       * but allow a short VAD pre-roll burst without permitting unbounded
       * amplification from one datagram. */
      if (!SV_ReadVoicePacket(voicecommands++ < 4))
        return false;
      break;
#endif

    case clcdp_ackframe:
      SVFTE_Ack(host_client, MSG_ReadLong());
      break;

    case clcfte_qcrequest:
      SV_ReadQCRequest();
      break;
    }
  }
}

/*
===================
SV_ReadClientMessage

Legacy direct receive path. SV_RunClients normally uses NET_GetServerMessages so
shared datagram sockets, loopback, and virtual clients are drained once per frame.
===================
*/
qboolean SV_ReadClientMessage(void) {
  int ret;

  do {
    ret = NET_GetMessage(host_client->netconnection);
    if (ret == -1) {
      Sys_Printf("SV_ReadClientMessage: NET_GetMessage failed\n");
      return false;
    }
    if (!ret)
      return true;
    if (!SV_ParseClientMessage())
      return false;
  } while (ret > 0);

  return true;
}

static move_authority_t
SV_PMovePolicyAuthority(sv_pmove_policy_authority_t authority)
{
  switch (authority) {
  case SV_PMOVE_POLICY_AUTHORITY_ENGINE_COMPAT:
    return MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT;
  case SV_PMOVE_POLICY_AUTHORITY_QC_COMMAND:
    return MOVE_AUTHORITY_PMOVE_QC_COMMAND;
  case SV_PMOVE_POLICY_AUTHORITY_LEGACY_FRAME:
  default:
    return MOVE_AUTHORITY_LEGACY_FRAME;
  }
}

static moveack_discontinuity_t
SV_PMovePolicyFallback(sv_pmove_policy_fallback_t fallback)
{
  switch (fallback) {
  case SV_PMOVE_POLICY_FALLBACK_CUSTOMPHYSICS:
    return MOVEACK_DISCONTINUITY_CUSTOMPHYSICS;
  case SV_PMOVE_POLICY_FALLBACK_UNSUPPORTED_STATE:
    return MOVEACK_DISCONTINUITY_UNSUPPORTED_STATE;
  case SV_PMOVE_POLICY_FALLBACK_INVALID_STATE:
    return MOVEACK_DISCONTINUITY_INVALID_STATE;
  case SV_PMOVE_POLICY_FALLBACK_ADMIN:
  default:
    return MOVEACK_DISCONTINUITY_ADMIN;
  }
}

/* This adapter is verified against the installed QBJ3 progs, not every mod
 * declaring .onladder. Keep unknown revisions on their original QC contract.
 * The CRC is the existing whole-progs checksum calculated by PR_LoadProgs. */
static qboolean SV_QBJ3PMoveCompatible(void) {
  return COM_GameDirMatches("qbj3") &&
      (qcvm->crc == 35566 || qcvm->crc == 15169) &&
      qcvm->extfields.onladder >= 0 && !qcvm->extfuncs.SV_RunClientCommand;
}

static qboolean SV_QBJ3LegacyState(edict_t *ent) {
  eval_t *val;

  if (!SV_QBJ3PMoveCompatible())
    return false;
  if (!ent || ent->free || ent->v.health <= 0 || ent->v.waterlevel > 0)
    return true;
  val = GetEdictFieldValue(ent, qcvm->extfields.onladder);
  if (val && val->_float)
    return true;
  val = GetEdictFieldValueByName(ent, "wasonladder");
  if (!val || val->_float)
    return true;
  val = GetEdictFieldValue(ent, qcvm->extfields.gravity);
  if (val && val->_float != 0 && val->_float != 1)
    return true;
  val = GetEdictFieldValueByName(ent, "pausetime");
  return val && val->_float > qcvm->time;
}

/* Long commands can cross a thin QC trigger between endpoint links. Close
 * to a ladder, use the existing server-frame sampler instead. This is only
 * a conservative admission bound; actual touch/facing/lock rules stay in QC. */
static qboolean SV_QBJ3LongMoveNearLadder(client_t *client) {
  extern cvar_t sv_maxvelocity, sv_gravity;
  edict_t *other, *ent = client->edict;
  vec3_t tracking = {0, 0, 0};
  float seconds = 0, reach;
  int j, n;
  qboolean longcmd = false;

  if (!SV_QBJ3PMoveCompatible() || !ent)
    return false;
  if (client->move_queue_count) {
    const usercmd_t *cmd = &client->move_queue[client->move_queue_head];
    seconds = cmd->seconds;
    longcmd = cmd->msec > 25;
    for (j = 0; j < 3; j++)
      tracking[j] = fabsf(cmd->vr_roomscalemove[j]);
  }
  if (!longcmd && client->usingpmove)
    return false;
  if (!client->usingpmove) {
    /* Once QC owns a nearby ladder, shorter packets must not cause mode
     * oscillation. Resume only outside the maximum-command neighborhood. */
    seconds = q_max(seconds, 0.125f);
    for (j = 0; j < 3; j++)
      tracking[j] = q_max(tracking[j], 16.0f);
  }
  /* Use one common velocity envelope for entry and exit. Include speed
   * gained over up to five 25ms substeps, a jump and gravity, rather than
   * assuming the pre-batch component clamp also limits PMove's output. */
  reach = q_max(fabsf(sv_maxvelocity.value), VectorLength(ent->v.velocity));
  reach += 5 * fabsf(sv_maxspeed.value) +
      q_max(270.0f, sv_vr_jump_velocity.value) + fabsf(sv_gravity.value) * 0.125f;
  reach = reach * seconds + 5 * q_max(18.0f, Cvar_VariableValue("pm_stepheight"));
  for (n = 1, other = NEXT_EDICT(qcvm->edicts); n < qcvm->num_edicts;
       n++, other = NEXT_EDICT(other)) {
    if (other->free || (int)other->v.solid != SOLID_TRIGGER ||
        strcmp(PR_GetString(other->v.classname), "trigger_ladder"))
      continue;
    for (j = 0; j < 3; j++)
      if (ent->v.origin[j] + ent->v.maxs[j] + reach + tracking[j] < other->v.absmin[j] ||
          ent->v.origin[j] + ent->v.mins[j] - reach - tracking[j] > other->v.absmax[j])
        break;
    if (j == 3)
      return true;
  }
  return false;
}

qboolean SV_QBJ3NeedsLegacyPhysics(client_t *client) {
  return SV_QBJ3LegacyState(client->edict) || SV_QBJ3LongMoveNearLadder(client);
}

/* Move unsimulated records into the existing frame-driven input consumer.
 * Its latest held state, event latches and tracking accumulation remain the
 * only legacy input policy. Already simulated records are outside the queue. */
static void SV_TransferPMoveToLegacy(client_t *client) {
  usercmd_t pending[MOVE_BUNDLE_MAX];
  unsigned int i, count = client->move_queue_count;
  double received_time = client->last_move_time;

  for (i = 0; i < count; i++)
    pending[i] = client->move_queue[
        (client->move_queue_head + i) % MOVE_BUNDLE_MAX];
  VectorClear(client->vr_roomscale_accum);
  VectorClear(client->vr_roomscalemove);
  VectorClear(client->cmd.vr_roomscalemove);
  SV_ClearClientPMoveState(client);
  client->net_latched_buttons = 0;
  client->net_latched_impulse = 0;
  client->net_latest_buttons = client->cmd.buttons;
  client->cmd.impulse = 0;
  for (i = 0; i < count; i++)
    SV_AcceptLatestUsercmd(client, &pending[i], false);
  client->last_move_time = received_time;
}

static void SV_UpdateClientPMoveMode(client_t *client, qboolean allow_promotion) {
  qboolean usingpmove;
  qboolean local_singleplayer;
  qboolean legacy_prethink_mod;
  qboolean legacy_qc_ladder_mod;
  qboolean qbj3_legacy_physics;
  move_authority_t authority;
  moveack_discontinuity_t fallback_reason;
  sv_pmove_policy_input_t pmove_policy_input;
  sv_pmove_policy_result_t pmove_policy_result;
  int requested_mode;
  eval_t *customphysics;
  int movetype;
  qboolean valid_state;
  static double last_pmove_gate_log;

  if (!client || !client->active)
    return;

  local_singleplayer = sv.active && svs.maxclients <= 1;
  /* Re:Mobilize 1.2 consumes its held lighthook impulses exclusively from
   * PlayerPreThink. Engine PMove batches several commands behind one legacy
   * PreThink callback, so a hook press later in the bundle would be lost.
   * Preserve the mod's original per-frame QuakeC input semantics. */
  legacy_prethink_mod = COM_GameDirMatches("rm1.2");
  /* Unknown ladder mods retain their whole-mod gate. Verified QBJ3 uses
   * shared PMove only while its QC-owned ladder/water state is inactive. */
  qbj3_legacy_physics = SV_QBJ3NeedsLegacyPhysics(client);
  legacy_qc_ladder_mod = qcvm->extfields.onladder >= 0 &&
      !qcvm->extfuncs.SV_RunClientCommand &&
      (!SV_QBJ3PMoveCompatible() || qbj3_legacy_physics);

  requested_mode = CLAMP(0, (int)sv_pmove_mode.value, 3);
  customphysics = client->edict ?
      GetEdictFieldValue(client->edict, qcvm->extfields.customphysics) : NULL;
  movetype = client->edict ? (int)client->edict->v.movetype : MOVETYPE_NONE;
  valid_state = client->edict && !client->edict->free &&
      isfinite(client->edict->v.origin[0]) &&
      isfinite(client->edict->v.origin[1]) &&
      isfinite(client->edict->v.origin[2]) &&
      isfinite(client->edict->v.velocity[0]) &&
      isfinite(client->edict->v.velocity[1]) &&
      isfinite(client->edict->v.velocity[2]) &&
      (movetype == MOVETYPE_WALK || movetype == MOVETYPE_TOSS ||
       movetype == MOVETYPE_BOUNCE || movetype == MOVETYPE_GIB ||
       movetype == MOVETYPE_FLY || movetype == MOVETYPE_NOCLIP);
  pmove_policy_input.requested_mode = requested_mode;
  pmove_policy_input.trusted_movement = sv_trustedmovement.value != 0;
  pmove_policy_input.local_singleplayer = local_singleplayer;
  pmove_policy_input.client_spawned = client->spawned;
  pmove_policy_input.client_known_to_qc = client->knowntoqc;
  pmove_policy_input.nq_player_physics = sv_nqplayerphysics.value != 0;
  pmove_policy_input.legacy_prethink_mod = legacy_prethink_mod;
  pmove_policy_input.has_qc_onladder_field = qcvm->extfields.onladder >= 0;
  pmove_policy_input.compatible_qc_ladder_mod = SV_QBJ3PMoveCompatible();
  pmove_policy_input.qc_ladder_legacy_state = qbj3_legacy_physics;
  pmove_policy_input.has_sv_runclientcommand =
      qcvm->extfuncs.SV_RunClientCommand != 0;
  pmove_policy_input.has_explicit_cmd_msec =
      (client->protocol_pext2 & PEXT2_EXPLICITCMDMSEC) != 0;
  pmove_policy_input.customphysics_active =
      customphysics && customphysics->function;
  pmove_policy_input.valid_state = valid_state;
  pmove_policy_result = SV_PMovePolicyEvaluate(&pmove_policy_input);
  usingpmove = pmove_policy_result.using_pmove;
  authority = SV_PMovePolicyAuthority(pmove_policy_result.authority);
  fallback_reason = SV_PMovePolicyFallback(pmove_policy_result.fallback);

  /* Never promote after legacy input was accepted in this frame. Finish its
   * QC pass first, then begin command movement at the next frame boundary. */
  if (usingpmove && !client->usingpmove &&
      (!allow_promotion || client->net_latched_buttons ||
       client->net_latched_impulse || client->vr_roomscale_accum[0] ||
       client->vr_roomscale_accum[1] || client->vr_roomscale_accum[2])) {
    usingpmove = false;
    authority = MOVE_AUTHORITY_LEGACY_FRAME;
  }

  if (usingpmove != client->usingpmove || authority != client->move_authority) {
    if (!usingpmove) {
      if (client->usingpmove && valid_state && SV_QBJ3PMoveCompatible())
        SV_TransferPMoveToLegacy(client);
      else {
        SV_ClearClientPMoveState(client);
        VectorClear(client->cmd.vr_roomscalemove);
        VectorClear(client->vr_roomscalemove);
        VectorClear(client->vr_roomscale_accum);
      }
    }
    client->move_mode_epoch++;
    client->move_discontinuity_epoch++;
    client->move_discontinuity_reason = usingpmove ?
        MOVEACK_DISCONTINUITY_RESET_TELEPORT : fallback_reason;
    SV_VRContactResetClient(client);
    SV_VRGorillaResetClient(client);
    if (net_lagdebug.value)
      Con_Printf("net_lagdebug: server PMove %s for %s mode=%s sv_runclientcommand=%d\n",
                 usingpmove ? "enabled" : "disabled",
                 client->name,
                 local_singleplayer ? "local-singleplayer" :
                 legacy_prethink_mod ? "legacy-prethink-mod" :
                 legacy_qc_ladder_mod ? "legacy-qc-ladder-mod" :
                 "latest-client",
                 qcvm->extfuncs.SV_RunClientCommand ? 1 : 0);
  }
  else if (net_lagdebug.value && !sv_trustedmovement.value &&
           !sv_nqplayerphysics.value && client->spawned && client->knowntoqc)
  {
    if (realtime - last_pmove_gate_log > 1.0)
    {
      Con_Printf("net_lagdebug: server PMove held off by sv_trustedmovement 0 for %s\n",
                 client->name);
      last_pmove_gate_log = realtime;
    }
  }
  client->usingpmove = usingpmove;
  client->move_authority = authority;
  client->move_prediction_allowed = usingpmove &&
      !client->move_client_quarantined &&
      ((client->protocol_pext2 & PEXT2_PREDINFO) != 0);
}

static void SV_GotServerMessage(struct qsocket_s *sock) {
  int i;

  for (i = 0, host_client = svs.clients; i < svs.maxclients;
       i++, host_client++) {
    if (host_client->netconnection == sock) {
      sv_player = host_client->edict;
      SV_UpdateClientPMoveMode(host_client, false);
      if (!SV_ParseClientMessage())
        SV_DropClient(false);
      break;
    }
  }
}

/*
==================
SV_RunClients
==================
*/
void SV_RunClients(void) {
  int i;

  /* Decide promotions before receiving this frame's input. A ladder contact
   * can leave unsimulated commands queued from the preceding physics frame. */
  for (i = 0; i < svs.maxclients; i++)
    if (svs.clients[i].active)
      SV_UpdateClientPMoveMode(&svs.clients[i], true);

  NET_GetServerMessages(SV_GotServerMessage);

  for (i = 0, host_client = svs.clients; i < svs.maxclients;
       i++, host_client++) {
    if (!host_client->active)
      continue;

    sv_player = host_client->edict;
    SV_UpdateClientPMoveMode(host_client, false);

    if (host_client->netconnection &&
        NET_IsTimedOut(host_client->netconnection)) {
      SV_DropClient(false);
      continue;
    }

    if (!host_client->spawned) {
      // clear client movement until a new packet is received
      memset(&host_client->cmd, 0, sizeof(host_client->cmd));
      host_client->net_latched_buttons = 0;
      host_client->net_latched_impulse = 0;
      host_client->net_latest_buttons = 0;
      host_client->input_stale = false;
      SV_ClearClientPMoveState(host_client);
      continue;
    }

    SV_UpdateClientPMoveMode(host_client, false);

    if (!host_client->netconnection) {
      eval_t *ev;

      ev = GetEdictFieldValue(host_client->edict, qcvm->extfields.movement);
      if (ev) {
        host_client->cmd.forwardmove = ev->vector[0];
        host_client->cmd.sidemove = ev->vector[1];
        host_client->cmd.upmove = ev->vector[2];
      }
      host_client->cmd.viewangles[0] = host_client->edict->v.v_angle[0];
      host_client->cmd.viewangles[1] = host_client->edict->v.v_angle[1];
      host_client->cmd.viewangles[2] = host_client->edict->v.v_angle[2];
    }

    // always pause in single player if in console or menus
    if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game)) {
      if (!host_client->usingpmove)
        SV_ClearStaleClientInput(host_client);
      SV_ClientThink();
    } else if (!host_client->usingpmove) {
      /* Discard released fire/jump taps across a pause; retain held input.
       * Keep impulses pending so console commands such as "impulse 9"
       * still reach QuakeC after the single-player console closes. */
      host_client->net_latched_buttons = 0;
      host_client->cmd.buttons = host_client->net_latest_buttons;
      host_client->edict->v.button0 = host_client->cmd.buttons & BUTTON_ATTACK;
      host_client->edict->v.button2 =
          (host_client->cmd.buttons & BUTTON_JUMP) >> 1;
      SV_SetExtendedButtons(host_client->edict, host_client->cmd.buttons);
    }
  }
}
