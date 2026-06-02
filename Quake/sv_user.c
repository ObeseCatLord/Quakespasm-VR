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
cvar_t sv_inputtimeout = {"sv_inputtimeout", "0.25", CVAR_NONE};

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
  host_client->cmd.seconds = 0;

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

  sequence16 &= 0xffff;
  if (host_client->lastmovemessage < 0)
    return sequence16;

  sequence = (host_client->lastmovemessage & ~0xffff) | sequence16;
  if (sequence <= host_client->lastmovemessage - 0x8000)
    sequence += 0x10000;
  else if (sequence > host_client->lastmovemessage + 0x8000)
    sequence -= 0x10000;

  return sequence;
}

static int SV_ExpandSnapshotAck(int ack16) {
  int ack;

  ack16 &= 0xffff;
  if (host_client->net_snapshot_ack < 0)
    return ack16;

  ack = (host_client->net_snapshot_ack & ~0xffff) | ack16;
  if (ack <= host_client->net_snapshot_ack - 0x8000)
    ack += 0x10000;
  else if (ack > host_client->net_snapshot_ack + 0x8000)
    ack -= 0x10000;

  return ack;
}

static qboolean SV_ReadUsercmd(usercmd_t *readcmd, int sequence) {
  int i;
  int extbits;
  int msec;

  Q_memset(readcmd, 0, sizeof(*readcmd));
  readcmd->sequence = sequence;
  msec = MSG_ReadByte();
  readcmd->seconds = CLAMP(1, msec, 255) * 0.001f;
  readcmd->servertime = MSG_ReadFloat();

  for (i = 0; i < 3; i++)
    readcmd->viewangles[i] = MSG_ReadAngle16(sv.protocolflags);

  readcmd->forwardmove = MSG_ReadShort();
  readcmd->sidemove = MSG_ReadShort();
  readcmd->upmove = MSG_ReadShort();
  readcmd->buttons = MSG_ReadByte();
  readcmd->impulse = MSG_ReadByte();

  extbits = MSG_ReadByte();
  if (extbits & ~(MOVEEXT_VR | MOVEEXT_TRUSTED)) {
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

  return !msg_badread;
}

void SV_ReadClientMove(usercmd_t *move) {
  int i;
  int latest16;
  int latest;
  int first;
  int count;
  int flags;
  int accepted;
  int bundled_buttons;
  int bundled_impulse;
  float bundled_seconds;
  vec3_t bundled_roomscale;
  usercmd_t readcmd;
  usercmd_t mergedcmd;

  latest16 = MSG_ReadShort() & 0xffff;
  count = MSG_ReadByte();
  flags = MSG_ReadByte();

  if (count <= 0 || count > MOVE_BUNDLE_MAX ||
      (flags & ~MOVE_BUNDLE_SNAPSHOTACK)) {
    msg_badread = true;
    return;
  }

  if (flags & MOVE_BUNDLE_SNAPSHOTACK) {
    int snapshot_ack = SV_ExpandSnapshotAck(MSG_ReadShort() & 0xffff);
    if (snapshot_ack > host_client->net_snapshot_ack)
      host_client->net_snapshot_ack = snapshot_ack;
  }

  latest = SV_ExpandClientSequence(latest16);
  first = latest - count + 1;
  accepted = 0;
  bundled_buttons = 0;
  bundled_impulse = 0;
  bundled_seconds = 0;
  VectorCopy(vec3_origin, bundled_roomscale);
  Q_memset(&mergedcmd, 0, sizeof(mergedcmd));

  host_client->net_move_packets_received++;
  host_client->net_move_cmds_received += count;
  host_client->net_move_last_bundle = count;
  if (count > host_client->net_move_bundle_max)
    host_client->net_move_bundle_max = count;

  for (i = 0; i < count; i++) {
    int sequence = first + i;
    int gap;

    if (!SV_ReadUsercmd(&readcmd, sequence))
      return;

    if (host_client->lastmovemessage >= 0 &&
        sequence <= host_client->lastmovemessage) {
      host_client->net_move_cmds_stale++;
      if (net_lagdebug.value) {
        Con_DPrintf("net_lagdebug: dropping stale bundled move from %s seq=%d last=%d\n",
                    host_client->name, sequence, host_client->lastmovemessage);
      }
      continue;
    }

    gap = host_client->lastmovemessage >= 0 ?
          sequence - host_client->lastmovemessage - 1 : 0;
    if (gap > 0) {
      host_client->net_move_last_gap = gap;
      if (net_lagdebug.value)
        Con_DPrintf("net_lagdebug: accepted move gap from %s gap=%d seq=%d last=%d bundle=%d\n",
                    host_client->name, gap, sequence,
                    host_client->lastmovemessage, count);
    }

    host_client->lastmovemessage = sequence;
    mergedcmd = readcmd;
    bundled_seconds += readcmd.seconds;
    bundled_buttons |= readcmd.buttons;
    if (readcmd.impulse)
      bundled_impulse = readcmd.impulse;
    if (readcmd.vr_active)
      VectorAdd(bundled_roomscale, readcmd.vr_roomscalemove, bundled_roomscale);
    accepted++;
  }

  if (!accepted)
    return;

  host_client->moveext = true;
  host_client->net_move_cmds_accepted += accepted;
  mergedcmd.buttons |= bundled_buttons;
  if (bundled_impulse)
    mergedcmd.impulse = bundled_impulse;
  if (bundled_seconds > 0)
    mergedcmd.seconds = CLAMP(0.001f, bundled_seconds, 0.1f);
  if (mergedcmd.vr_active)
    VectorCopy(bundled_roomscale, mergedcmd.vr_roomscalemove);
  *move = mergedcmd;

  host_client->ping_times[host_client->num_pings % NUM_PING_TIMES] =
      qcvm->time - mergedcmd.servertime;
  host_client->num_pings++;

  VectorCopy(mergedcmd.viewangles, host_client->edict->v.v_angle);
  host_client->edict->v.button0 = mergedcmd.buttons & 1;
  host_client->edict->v.button2 = (mergedcmd.buttons & 2) >> 1;
  if (mergedcmd.impulse)
    host_client->edict->v.impulse = mergedcmd.impulse;

  if (mergedcmd.vr_active) {
    host_client->is_vr_client = true;
    VectorCopy(mergedcmd.vr_handpos, host_client->vr_handpos);
    VectorCopy(mergedcmd.vr_handrot, host_client->vr_handrot);
    VectorCopy(mergedcmd.vr_roomscalemove, host_client->vr_roomscalemove);
    VectorAdd(host_client->vr_roomscale_accum, host_client->vr_roomscalemove,
              host_client->vr_roomscale_accum);
  } else {
    host_client->is_vr_client = false;
    VectorCopy(vec3_origin, host_client->vr_roomscalemove);
  }

  if (mergedcmd.trusted_active) {
    host_client->trusted_clientmove_valid = true;
    VectorCopy(mergedcmd.trusted_origin, host_client->trusted_clientmove_origin);
    VectorCopy(mergedcmd.trusted_velocity, host_client->trusted_clientmove_velocity);
  } else {
    SV_ClearTrustedClientMove(host_client);
  }

  host_client->last_move_time = realtime;
  host_client->input_stale = false;
}

static qboolean SV_ClientHasInput(const client_t *client) {
  return client->cmd.forwardmove || client->cmd.sidemove ||
         client->cmd.upmove || client->edict->v.button0 ||
         client->edict->v.button2 || client->edict->v.impulse ||
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
  client->edict->v.button0 = 0;
  client->edict->v.button2 = 0;
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
          q_strncasecmp(s, "prespawn", 8) && qcvm->extfuncs.SV_ParseClientCommand) {
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

static void SV_GotServerMessage(struct qsocket_s *sock) {
  int i;

  for (i = 0, host_client = svs.clients; i < svs.maxclients;
       i++, host_client++) {
    if (host_client->netconnection == sock) {
      sv_player = host_client->edict;
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
      SV_ClearTrustedClientMove(host_client);
      continue;
    }

    // always pause in single player if in console or menus
    if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game)) {
      SV_ClearStaleClientInput(host_client);
      SV_ClientThink();
    }
  }
}
