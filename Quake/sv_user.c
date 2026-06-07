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

cvar_t sv_idealpitchscale = {"sv_idealpitchscale", "0.8", CVAR_NONE};
cvar_t sv_altnoclip = {"sv_altnoclip", "1", CVAR_ARCHIVE}; // johnfitz
cvar_t sv_inputtimeout = {"sv_inputtimeout", "0.50", CVAR_NONE};
cvar_t sv_pmove = {"sv_pmove", "1", CVAR_NONE};
cvar_t sv_pmove_legacy = {"sv_pmove_legacy", "1", CVAR_NONE};
cvar_t sv_move_timeclamp = {"sv_move_timeclamp", "1", CVAR_NONE};

static void SV_SetExtendedButtons(edict_t *ent, int buttons) {
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

static qboolean SV_TrustedClientMoveVectorOK(const vec3_t v, float limit) {
  int i;

  for (i = 0; i < 3; i++) {
    if (IS_NAN(v[i]) || fabs(v[i]) > limit)
      return false;
  }

  return true;
}

static void SV_ClearTrustedClientMove(client_t *client) {
  client->trusted_clientmove_valid = false;
  VectorCopy(vec3_origin, client->trusted_clientmove_origin);
  VectorCopy(vec3_origin, client->trusted_clientmove_velocity);
}

void SV_ResetClientMoveState(client_t *client) {
  Q_memset(&client->cmd, 0, sizeof(client->cmd));
  VectorCopy(vec3_origin, client->wishdir);
  client->last_move_time = 0;
  client->lastmovetime = 0;
  client->input_stale = false;
  client->moveext = false;
  client->lastmovemessage = -1;
  client->lastreceivedmovemessage = -1;
  client->pendingmovemessage = -1;
  client->move_pending = false;
  client->move_queue_start = 0;
  client->move_queue_count = 0;

  client->net_move_packets_received = 0;
  client->net_move_cmds_received = 0;
  client->net_move_cmds_accepted = 0;
  client->net_move_cmds_stale = 0;
  client->net_move_cmds_simulated = 0;
  client->net_move_stale_log_suppressed = 0;
  client->net_move_bundle_max = 0;
  client->net_move_last_bundle = 0;
  client->net_move_last_gap = 0;
  client->net_move_queue_max = 0;
  client->net_move_stale_log_time = 0;
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
  client->net_snapshot_partial_ack_seq = -1;
  client->net_snapshot_partial_ack_last_part = SNAPSHOT_PART_UNKNOWN;
  Q_memset(client->net_snapshot_partial_ack_mask, 0,
           sizeof(client->net_snapshot_partial_ack_mask));
  client->net_snapshot_partial_ack_time = 0;
  client->net_snapshot_resend_sequence = -1;
  client->net_snapshot_resend_parts = 0;
  Q_memset(client->net_snapshot_resend_part_len, 0,
           sizeof(client->net_snapshot_resend_part_len));
  client->net_snapshot_last_part_resend_time = 0;
  client->net_snapshot_part_resends = 0;

  client->is_vr_client = false;
  VectorCopy(vec3_origin, client->vr_handpos);
  VectorCopy(vec3_origin, client->vr_handrot);
  VectorCopy(vec3_origin, client->vr_roomscalemove);
  VectorCopy(vec3_origin, client->vr_roomscale_accum);
  SV_ClearTrustedClientMove(client);

  if (client->edict && !client->edict->free) {
    client->edict->v.button0 = 0;
    client->edict->v.button2 = 0;
    SV_SetExtendedButtons(client->edict, 0);
    client->edict->v.impulse = 0;
  }
}

void SV_ApplyTrustedClientMove(client_t *client) {
  edict_t *ent;
  float delta_len;
  float maxdelta;
  float move_len;
  vec3_t delta;
  trace_t trace;

  if (!client->trusted_clientmove_valid)
    return;

  if (!coop.value || !sv_coop_trusted_clientmove.value) {
    SV_ClearTrustedClientMove(client);
    return;
  }

  ent = client->edict;
  if (!ent || ent->free || (int)ent->v.movetype == MOVETYPE_NONE) {
    SV_ClearTrustedClientMove(client);
    return;
  }

  if (!SV_TrustedClientMoveVectorOK(client->trusted_clientmove_origin, 65536.0f) ||
      !SV_TrustedClientMoveVectorOK(client->trusted_clientmove_velocity, 10000.0f)) {
    if (net_lagdebug.value)
      Con_Printf("net_lagdebug: rejected trusted client movement for %s origin=(%g,%g,%g) velocity=(%g,%g,%g)\n",
                 client->name,
                 client->trusted_clientmove_origin[0],
                 client->trusted_clientmove_origin[1],
                 client->trusted_clientmove_origin[2],
                 client->trusted_clientmove_velocity[0],
                 client->trusted_clientmove_velocity[1],
                 client->trusted_clientmove_velocity[2]);
    SV_ClearTrustedClientMove(client);
    return;
  }

  VectorSubtract(client->trusted_clientmove_origin, ent->v.origin, delta);
  maxdelta = sv_coop_trusted_clientmove_maxdelta.value;
  if (maxdelta > 0) {
    delta_len = VectorLength(delta);
    if (delta_len > maxdelta) {
      if (net_lagdebug.value)
        Con_Printf("net_lagdebug: rejected trusted client movement for %s delta=%.1f max=%.1f\n",
                   client->name, delta_len, maxdelta);
      SV_ClearTrustedClientMove(client);
      return;
    }
  }

  move_len = fabs(client->cmd.forwardmove) + fabs(client->cmd.sidemove) +
             fabs(client->cmd.upmove);
  if (move_len > 1 &&
      DotProduct(delta, client->trusted_clientmove_velocity) < -1.0f) {
    SV_ClearTrustedClientMove(client);
    return;
  }

  if (VectorLength(delta) > 0.1f) {
    trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs,
                    client->trusted_clientmove_origin, MOVE_NORMAL, ent);
    if (trace.allsolid || trace.startsolid || trace.fraction < 1.0f) {
      if (net_lagdebug.value)
        Con_Printf("net_lagdebug: rejected trusted client movement for %s blocked fraction=%.3f startsolid=%d allsolid=%d\n",
                   client->name, trace.fraction, trace.startsolid,
                   trace.allsolid);
      SV_ClearTrustedClientMove(client);
      return;
    }
  }

  VectorCopy(client->trusted_clientmove_origin, ent->v.origin);
  VectorCopy(client->trusted_clientmove_velocity, ent->v.velocity);
  SV_ClearTrustedClientMove(client);
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
void SV_WaterMove(void) {
  int i;
  vec3_t wishvel;
  float speed, newspeed, wishspeed, addspeed, accelspeed;

  //
  // user intentions
  //
  AngleVectors(sv_player->v.v_angle, forward, right, up);

  for (i = 0; i < 3; i++)
    wishvel[i] = forward[i] * cmd.forwardmove + right[i] * cmd.sidemove;

  if (!cmd.forwardmove && !cmd.sidemove && !cmd.upmove)
    wishvel[2] -= 60; // drift towards bottom
  else
    wishvel[2] += cmd.upmove;

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
  if (qcvm->time > sv_player->v.teleport_time || !sv_player->v.waterlevel) {
    sv_player->v.flags = (int)sv_player->v.flags & ~FL_WATERJUMP;
    sv_player->v.teleport_time = 0;
  }
  sv_player->v.velocity[0] = sv_player->v.movedir[0];
  sv_player->v.velocity[1] = sv_player->v.movedir[1];
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
        host_client->is_vr_client && wishspeed == 0) {
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

/*
===================
SV_ClientThink

the move fields specify an intended velocity in pix/sec
the angle fields specify an exact angular motion in degrees
===================
*/
void SV_ClientThink(void) {
  vec3_t v_angle;
  float saved_host_frametime;

  cmd = host_client->cmd;

  if (sv_player->v.movetype == MOVETYPE_NONE)
    return;

  onground = (int)sv_player->v.flags & FL_ONGROUND;

  origin = sv_player->v.origin;
  velocity = sv_player->v.velocity;

  DropPunchAngle();

  //
  // if dead, behave differently
  //
  if (sv_player->v.health <= 0)
    return;

  //
  // angles
  // show 1/3 the pitch angle and all the roll angle
  saved_host_frametime = host_frametime;
  if (cmd.seconds > 0)
    host_frametime = CLAMP(0.001f, cmd.seconds, 0.1f);
  angles = sv_player->v.angles;

  VectorAdd(sv_player->v.v_angle, sv_player->v.punchangle, v_angle);
  angles[ROLL] = V_CalcRoll(sv_player->v.angles, sv_player->v.velocity) * 4;
  if (!sv_player->v.fixangle) {
    angles[PITCH] = -v_angle[PITCH] / 3;
    angles[YAW] = v_angle[YAW];
  }

  if ((int)sv_player->v.flags & FL_WATERJUMP) {
    SV_WaterJump();
    host_frametime = saved_host_frametime;
    return;
  }
  //
  // walk
  //
  // johnfitz -- alternate noclip
  if (sv_player->v.movetype == MOVETYPE_NOCLIP && sv_altnoclip.value)
    SV_NoclipMove();
  else if (sv_player->v.waterlevel >= 2 &&
           sv_player->v.movetype != MOVETYPE_NOCLIP)
    SV_WaterMove();
  else
    SV_AirMove();
  // johnfitz
  host_frametime = saved_host_frametime;
}

/*
===================
SV_ReadClientMove
===================
*/
static int SV_ExpandClientSequence(int sequence16) {
  int sequence;
  int base;

  sequence16 &= 0xffff;
  base = host_client->lastreceivedmovemessage > host_client->lastmovemessage ?
         host_client->lastreceivedmovemessage : host_client->lastmovemessage;
  if (base < 0)
    return sequence16;

  sequence = (base & ~0xffff) | sequence16;
  if (sequence <= base - 0x8000)
    sequence += 0x10000;
  else if (sequence > base + 0x8000)
    sequence -= 0x10000;

  return sequence;
}

static qboolean SV_ReadUsercmd(usercmd_t *readcmd, int sequence) {
  int i;
  int extbits;

  Q_memset(readcmd, 0, sizeof(*readcmd));
  readcmd->sequence = sequence;
  readcmd->servertime = MSG_ReadFloat();

  for (i = 0; i < 3; i++)
    readcmd->viewangles[i] = MSG_ReadAngle16(sv.protocolflags);

  readcmd->forwardmove = MSG_ReadShort();
  readcmd->sidemove = MSG_ReadShort();
  readcmd->upmove = MSG_ReadShort();
  readcmd->buttons = MSG_ReadByte();
  readcmd->impulse = MSG_ReadByte();

  extbits = MSG_ReadByte();
  if (extbits & ~(MOVEEXT_VR | MOVEEXT_TRUSTED | MOVEEXT_QCINPUT)) {
    msg_badread = true;
    return false;
  }

  if (extbits & MOVEEXT_VR) {
    if (net_message.cursize - msg_readcount < 9 * 4) {
      msg_badread = true;
      return false;
    }

    readcmd->vr_active = true;
    readcmd->vr_handpos[0] = MSG_ReadFloat();
    readcmd->vr_handpos[1] = MSG_ReadFloat();
    readcmd->vr_handpos[2] = MSG_ReadFloat();
    readcmd->vr_handrot[0] = MSG_ReadFloat();
    readcmd->vr_handrot[1] = MSG_ReadFloat();
    readcmd->vr_handrot[2] = MSG_ReadFloat();
    readcmd->vr_roomscalemove[0] = MSG_ReadFloat();
    readcmd->vr_roomscalemove[1] = MSG_ReadFloat();
    readcmd->vr_roomscalemove[2] = MSG_ReadFloat();
  }

  if (extbits & MOVEEXT_TRUSTED) {
    if (net_message.cursize - msg_readcount < 6 * 4) {
      msg_badread = true;
      SV_ClearTrustedClientMove(host_client);
      return false;
    }

    readcmd->trusted_active = true;
    readcmd->trusted_origin[0] = MSG_ReadFloat();
    readcmd->trusted_origin[1] = MSG_ReadFloat();
    readcmd->trusted_origin[2] = MSG_ReadFloat();
    readcmd->trusted_velocity[0] = MSG_ReadFloat();
    readcmd->trusted_velocity[1] = MSG_ReadFloat();
    readcmd->trusted_velocity[2] = MSG_ReadFloat();
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

  return !msg_badread;
}

static void SV_NormalizeAcceptedUsercmd(client_t *client, usercmd_t *acceptedcmd)
{
  float original_seconds;
  float fallback_seconds;
  double timestamp;
  double clamped_timestamp;
  double seconds_from_time;

  fallback_seconds = CLAMP(0.001f, host_frametime, 0.1f);
  original_seconds = acceptedcmd->seconds;
  if (original_seconds <= 0 || original_seconds > 0.1f)
    original_seconds = fallback_seconds;

  acceptedcmd->seconds = original_seconds;
  if (!sv_move_timeclamp.value || acceptedcmd->servertime <= 0)
    return;

  timestamp = acceptedcmd->servertime;
  clamped_timestamp = timestamp;
  if (clamped_timestamp > qcvm->time)
    clamped_timestamp = qcvm->time;
  if (clamped_timestamp < qcvm->time - 0.5)
    clamped_timestamp = qcvm->time - 0.5;

  if (client->lastmovetime <= 0)
    client->lastmovetime = clamped_timestamp - original_seconds;
  if (client->lastmovetime < qcvm->time - 0.5)
    client->lastmovetime = qcvm->time - 0.5;
  if (clamped_timestamp < client->lastmovetime)
    clamped_timestamp = client->lastmovetime;

  seconds_from_time = clamped_timestamp - client->lastmovetime;
  client->lastmovetime = clamped_timestamp;

  /*
   * QSS-M derives independent movement time from the client timestamp and
   * clamps it to server time. The frame time fallback is only used for the
   * first command or malformed timestamps.
   */
  if (seconds_from_time >= 0.001 && seconds_from_time <= 0.1)
    acceptedcmd->seconds = seconds_from_time;
}

static qboolean SV_QueueAcceptedUsercmd(const usercmd_t *acceptedcmd) {
  usercmd_t *slot;
  int index;
  usercmd_t queuedcmd;

  if (host_client->move_queue_count == (int)countof(host_client->move_queue)) {
    if (net_lagdebug.value)
      Con_DPrintf("net_lagdebug: move queue overflow for %s, rejecting seq=%d last_ack=%d queued=%d\n",
                  host_client->name, acceptedcmd->sequence,
                  host_client->lastmovemessage,
                  host_client->move_queue_count);
    return false;
  }

  host_client->ping_times[host_client->num_pings % NUM_PING_TIMES] =
      qcvm->time - acceptedcmd->servertime;
  host_client->num_pings++;

  queuedcmd = *acceptedcmd;
  SV_NormalizeAcceptedUsercmd(host_client, &queuedcmd);

  index = (host_client->move_queue_start + host_client->move_queue_count) %
          countof(host_client->move_queue);
  slot = &host_client->move_queue[index];
  *slot = queuedcmd;
  host_client->move_queue_count++;
  if (host_client->move_queue_count > host_client->net_move_queue_max)
    host_client->net_move_queue_max = host_client->move_queue_count;

  if (!host_client->move_pending) {
    host_client->cmd = queuedcmd;
    host_client->move_pending = true;
  }

  host_client->last_move_time = realtime;
  host_client->input_stale = false;
  host_client->pendingmovemessage = queuedcmd.sequence;
  return true;
}

qboolean SV_PopQueuedUsercmd(client_t *client, usercmd_t *out) {
  if (client->move_queue_count <= 0)
    return false;

  *out = client->move_queue[client->move_queue_start];
  client->move_queue_start =
      (client->move_queue_start + 1) % countof(client->move_queue);
  client->move_queue_count--;
  return true;
}

static qboolean SV_CoalesceLegacyUsercmd(client_t *client, usercmd_t *out) {
  usercmd_t queuedcmd;
  usercmd_t mergedcmd;
  usercmd_t latest_vr_cmd;
  qboolean have_cmd = false;
  qboolean have_vr_cmd = false;
  int buttons = 0;
  int impulse = 0;
  vec3_t vr_roomscalemove;

  memset(&mergedcmd, 0, sizeof(mergedcmd));
  memset(&latest_vr_cmd, 0, sizeof(latest_vr_cmd));
  VectorCopy(vec3_origin, vr_roomscalemove);

  while (SV_PopQueuedUsercmd(client, &queuedcmd)) {
    mergedcmd = queuedcmd;
    buttons |= queuedcmd.buttons;
    if (queuedcmd.impulse)
      impulse = queuedcmd.impulse;

    if (queuedcmd.vr_active) {
      latest_vr_cmd = queuedcmd;
      have_vr_cmd = true;
      VectorAdd(vr_roomscalemove, queuedcmd.vr_roomscalemove,
                vr_roomscalemove);
    }

    have_cmd = true;
  }

  if (!have_cmd)
    return false;

  mergedcmd.buttons = buttons;
  mergedcmd.impulse = impulse;
  /*
   * Legacy Quake movement applies acceleration in SV_ClientThink and moves the
   * entity once later in SV_Physics_Client.  Do not use bundled client msec
   * here, or bursts of redundant commands turn into stacked acceleration.
   */
  mergedcmd.seconds = 0;

  if (have_vr_cmd) {
    mergedcmd.vr_active = true;
    VectorCopy(latest_vr_cmd.vr_handpos, mergedcmd.vr_handpos);
    VectorCopy(latest_vr_cmd.vr_handrot, mergedcmd.vr_handrot);
    VectorCopy(vr_roomscalemove, mergedcmd.vr_roomscalemove);
  } else {
    mergedcmd.vr_active = false;
    VectorCopy(vec3_origin, mergedcmd.vr_roomscalemove);
  }

  *out = mergedcmd;
  return true;
}

void SV_ApplyQueuedUsercmd(client_t *client, const usercmd_t *queuedcmd) {
  client->cmd = *queuedcmd;
  VectorCopy(client->cmd.viewangles, client->edict->v.v_angle);
  client->edict->v.button0 = client->cmd.buttons & 1;
  client->edict->v.button2 = (client->cmd.buttons & 2) >> 1;
  SV_SetExtendedButtons(client->edict, client->cmd.buttons);
  if (client->cmd.impulse)
    client->edict->v.impulse = client->cmd.impulse;

  if (queuedcmd->vr_active) {
    client->is_vr_client = true;
    VectorCopy(queuedcmd->vr_handpos, client->vr_handpos);
    VectorCopy(queuedcmd->vr_handrot, client->vr_handrot);
    VectorCopy(queuedcmd->vr_roomscalemove, client->vr_roomscalemove);
    VectorAdd(client->vr_roomscale_accum, queuedcmd->vr_roomscalemove,
              client->vr_roomscale_accum);
  } else {
    client->is_vr_client = false;
    VectorCopy(vec3_origin, client->vr_roomscalemove);
  }

  if (queuedcmd->trusted_active) {
    client->trusted_clientmove_valid = true;
    VectorCopy(queuedcmd->trusted_origin, client->trusted_clientmove_origin);
    VectorCopy(queuedcmd->trusted_velocity, client->trusted_clientmove_velocity);
  } else {
    SV_ClearTrustedClientMove(client);
  }

  client->pendingmovemessage = queuedcmd->sequence;
  client->move_pending = true;
}

void SV_FinishQueuedUsercmd(client_t *client) {
  client->net_move_cmds_simulated++;
  client->net_move_last_sim_seconds = client->cmd.seconds;
  client->lastmovemessage = client->cmd.sequence;
  client->pendingmovemessage = -1;
  client->move_pending = false;
  client->cmd.seconds = 0;
}

void SV_ReadClientMove(usercmd_t *move) {
  int sequence16;
  int sequence;
  int gap;
  int accepted_base;
  usercmd_t readcmd;

  sequence16 = MSG_ReadShort() & 0xffff;
  sequence = SV_ExpandClientSequence(sequence16);
  accepted_base = host_client->lastreceivedmovemessage;

  host_client->net_move_packets_received++;
  host_client->net_move_cmds_received++;
  host_client->net_move_last_bundle = 1;
  if (host_client->net_move_bundle_max < 1)
    host_client->net_move_bundle_max = 1;

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
    if (net_lagdebug.value)
      Con_DPrintf("net_lagdebug: accepted move gap from %s gap=%d seq=%d last=%d\n",
                  host_client->name, gap, sequence, accepted_base);
  }

  if (!SV_QueueAcceptedUsercmd(&readcmd))
    return;

  host_client->moveext = true;
  host_client->net_move_cmds_accepted++;
  host_client->lastreceivedmovemessage = sequence;
  *move = host_client->cmd;

  if (host_client->usingpmove && host_client->spawned && !sv.paused &&
      (svs.maxclients > 1 || key_dest == key_game))
    SV_RunClientPMoveCommands(host_client);
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
  }

  client->cmd.forwardmove = 0;
  client->cmd.sidemove = 0;
  client->cmd.upmove = 0;
  client->cmd.seconds = 0;
  client->lastmovetime = 0;
  client->pendingmovemessage = -1;
  client->move_pending = false;
  client->move_queue_start = 0;
  client->move_queue_count = 0;
  client->edict->v.button0 = 0;
  client->edict->v.button2 = 0;
  SV_SetExtendedButtons(client->edict, 0);
  client->edict->v.impulse = 0;
  VectorCopy(vec3_origin, client->vr_roomscalemove);
  SV_ClearTrustedClientMove(client);
}

/*
===================
SV_ParseClientMessage

Returns false if the client should be killed
===================
*/
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

  MSG_BeginReading();

  while (1) {
    int allowed;

    if (!host_client->active)
      return false; // a command caused an error

    if (msg_badread) {
      Sys_Printf("SV_ReadClientMessage: badread\n");
      return false;
    }

    ccmd = MSG_ReadChar();

    switch (ccmd) {
    case -1:
      return true; // end of message

    default:
      Sys_Printf("SV_ReadClientMessage: unknown command char\n");
      return false;

    case clc_nop:
      //				Sys_Printf ("clc_nop\n");
      break;

    case clc_stringcmd:
      s = MSG_ReadString();
      if (q_strncasecmp(s, "spawn", 5) && q_strncasecmp(s, "begin", 5) &&
          q_strncasecmp(s, "prespawn", 8) && q_strncasecmp(s, "enablecsqc", 10) &&
          q_strncasecmp(s, "disablecsqc", 11) && qcvm->extfuncs.SV_ParseClientCommand) {
        client_t *ohc = host_client;
        G_INT(OFS_PARM0) = PR_SetEngineString(s);
        pr_global_struct->time = qcvm->time;
        pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
        PR_ExecuteProgram(qcvm->extfuncs.SV_ParseClientCommand);
        host_client = ohc;
        break;
      }
      allowed = 0;
      if (q_strncasecmp(s, "status", 6) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "god", 3) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "notarget", 8) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "fly", 3) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "name", 4) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "noclip", 6) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "setpos", 6) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "say", 3) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "say_team", 8) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "tell", 4) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "color", 5) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "kill", 4) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "pause", 5) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "spawn", 5) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "begin", 5) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "prespawn", 8) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "enablecsqc", 10) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "disablecsqc", 11) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "kick", 4) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "ping", 4) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "give", 4) == 0)
        allowed = 1;
      else if (q_strncasecmp(s, "ban", 3) == 0)
        allowed = 1;

      if (allowed == 1)
        Cmd_ExecuteString(s, src_client);
      else
        Con_DPrintf("%s tried to %s\n", host_client->name, s);
      break;

    case clc_disconnect:
      //	Sys_Printf ("SV_ReadClientMessage: client disconnected\n");
      return false;

    case clc_move:
      SV_ReadClientMove(&host_client->cmd);
      break;

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

Legacy per-client receive path used by loopback and non-virtual datagram sockets.
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
  } while (ret == 1);

  return true;
}

static void SV_UpdateClientPMoveMode(client_t *client) {
  qboolean usingpmove;

  if (!client || !client->active)
    return;

  usingpmove = client->spawned && sv_pmove.value &&
    (client->protocol_pext2 & PEXT2_PREDINFO) &&
    (qcvm->extfuncs.SV_RunClientCommand || sv_pmove_legacy.value);

  if (usingpmove != client->usingpmove && net_lagdebug.value)
    Con_Printf("net_lagdebug: server PMove %s for %s pext2=0x%x sv_runclientcommand=%d sv_pmove_legacy=%d\n",
               usingpmove ? "enabled" : "disabled",
               client->name,
               client->protocol_pext2,
               qcvm->extfuncs.SV_RunClientCommand ? 1 : 0,
               sv_pmove_legacy.value ? 1 : 0);
  client->usingpmove = usingpmove;
}

static void SV_GotServerMessage(struct qsocket_s *sock) {
  int i;

  for (i = 0, host_client = svs.clients; i < svs.maxclients;
       i++, host_client++) {
    if (host_client->netconnection == sock) {
      sv_player = host_client->edict;
      SV_UpdateClientPMoveMode(host_client);
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

  NET_GetServerMessages(SV_GotServerMessage);

  for (i = 0, host_client = svs.clients; i < svs.maxclients;
       i++, host_client++) {
    if (!host_client->active)
      continue;

    sv_player = host_client->edict;
    SV_UpdateClientPMoveMode(host_client);

    if (NET_IsVirtualConnection(host_client->netconnection)) {
      if (NET_IsTimedOut(host_client->netconnection)) {
        SV_DropClient(false);
        continue;
      }
    } else {
      if (!SV_ReadClientMessage()) {
        SV_DropClient(false); // client misbehaved...
        continue;
      }
    }

    if (!host_client->spawned) {
      // clear client movement until a new packet is received
      memset(&host_client->cmd, 0, sizeof(host_client->cmd));
      host_client->input_stale = false;
      host_client->lastmovetime = 0;
      host_client->pendingmovemessage = -1;
      host_client->move_pending = false;
      host_client->move_queue_start = 0;
      host_client->move_queue_count = 0;
      SV_ClearTrustedClientMove(host_client);
      continue;
    }

    SV_UpdateClientPMoveMode(host_client);

    // always pause in single player if in console or menus
    if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game)) {
      usercmd_t queuedcmd;

      SV_ClearStaleClientInput(host_client);
      if (!host_client->usingpmove) {
        if (SV_CoalesceLegacyUsercmd(host_client, &queuedcmd)) {
          SV_ApplyQueuedUsercmd(host_client, &queuedcmd);
          SV_ClientThink();
          SV_FinishQueuedUsercmd(host_client);
        }
        else {
          SV_ClientThink();
        }
      }
    }
  }
}
