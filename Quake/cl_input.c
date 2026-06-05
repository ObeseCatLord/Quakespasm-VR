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
// cl.input.c  -- builds an intended movement command to send to the server

// Quake is a trademark of Id Software, Inc., (c) 1996 Id Software, Inc. All
// rights reserved.

#include "keys.h"
#include "quakedef.h"
#include "vr.h"

extern cvar_t cl_maxpitch; // johnfitz -- variable pitch clamping
extern cvar_t cl_minpitch; // johnfitz -- variable pitch clamping

/*
===============================================================================

KEY BUTTONS

Continuous button event tracking is complicated by the fact that two different
input sources (say, mouse button 1 and the control key) can both press the
same button, but the button should only be released when both of the
pressing key have been released.

When a key event issues a button command (+forward, +attack, etc), it appends
its key number as a parameter to the command so it can be matched up with
the release.

state bit 0 is the current state of the key
state bit 1 is edge triggered on the up to down transition
state bit 2 is edge triggered on the down to up transition

===============================================================================
*/

kbutton_t in_mlook, in_klook;
kbutton_t in_left, in_right, in_forward, in_back;
kbutton_t in_lookup, in_lookdown, in_moveleft, in_moveright;
kbutton_t in_strafe, in_speed, in_use, in_jump, in_attack;
kbutton_t in_up, in_down;
kbutton_t in_vr_weaponmenu;

int in_impulse;
static double cl_lagdebug_last_sendmove;
static double cl_lagdebug_last_sendmove_log;

void KeyDown(kbutton_t *b) {
  int k;
  const char *c;

  c = Cmd_Argv(1);
  if (c[0])
    k = atoi(c);
  else
    k = -1; // typed manually at the console for continuous down

  if (k == b->down[0] || k == b->down[1])
    return; // repeating key

  if (!b->down[0])
    b->down[0] = k;
  else if (!b->down[1])
    b->down[1] = k;
  else {
    Con_Printf("Three keys down for a button!\n");
    return;
  }

  if (b->state & 1)
    return;          // still down
  b->state |= 1 + 2; // down + impulse down
}

void KeyUp(kbutton_t *b) {
  int k;
  const char *c;

  c = Cmd_Argv(1);
  if (c[0])
    k = atoi(c);
  else { // typed manually at the console, assume for unsticking, so clear all
    b->down[0] = b->down[1] = 0;
    b->state = 4; // impulse up
    return;
  }

  if (b->down[0] == k)
    b->down[0] = 0;
  else if (b->down[1] == k)
    b->down[1] = 0;
  else
    return; // key up without coresponding down (menu pass through)
  if (b->down[0] || b->down[1])
    return; // some other key is still holding it down

  if (!(b->state & 1))
    return;       // still up (this should not happen)
  b->state &= ~1; // now up
  b->state |= 4;  // impulse up
}

void IN_KLookDown(void) { KeyDown(&in_klook); }
void IN_KLookUp(void) { KeyUp(&in_klook); }
void IN_MLookDown(void) { KeyDown(&in_mlook); }
void IN_MLookUp(void) {
  KeyUp(&in_mlook);
  if (!(in_mlook.state & 1) && lookspring.value)
    V_StartPitchDrift();
}
void IN_UpDown(void) { KeyDown(&in_up); }
void IN_UpUp(void) { KeyUp(&in_up); }
void IN_DownDown(void) { KeyDown(&in_down); }
void IN_DownUp(void) { KeyUp(&in_down); }
void IN_LeftDown(void) { KeyDown(&in_left); }
void IN_LeftUp(void) { KeyUp(&in_left); }
void IN_RightDown(void) { KeyDown(&in_right); }
void IN_RightUp(void) { KeyUp(&in_right); }
void IN_ForwardDown(void) { KeyDown(&in_forward); }
void IN_ForwardUp(void) { KeyUp(&in_forward); }
void IN_BackDown(void) { KeyDown(&in_back); }
void IN_BackUp(void) { KeyUp(&in_back); }
void IN_LookupDown(void) { KeyDown(&in_lookup); }
void IN_LookupUp(void) { KeyUp(&in_lookup); }
void IN_LookdownDown(void) { KeyDown(&in_lookdown); }
void IN_LookdownUp(void) { KeyUp(&in_lookdown); }
void IN_MoveleftDown(void) { KeyDown(&in_moveleft); }
void IN_MoveleftUp(void) { KeyUp(&in_moveleft); }
void IN_MoverightDown(void) { KeyDown(&in_moveright); }
void IN_MoverightUp(void) { KeyUp(&in_moveright); }

void IN_SpeedDown(void) { KeyDown(&in_speed); }
void IN_SpeedUp(void) { KeyUp(&in_speed); }
void IN_StrafeDown(void) { KeyDown(&in_strafe); }
void IN_StrafeUp(void) { KeyUp(&in_strafe); }

void IN_AttackDown(void) { KeyDown(&in_attack); }
void IN_AttackUp(void) { KeyUp(&in_attack); }

void IN_UseDown(void) { KeyDown(&in_use); }
void IN_UseUp(void) { KeyUp(&in_use); }
void IN_JumpDown(void) { KeyDown(&in_jump); }
void IN_JumpUp(void) { KeyUp(&in_jump); }

void IN_VRWeaponMenuDown(void) {
  KeyDown(&in_vr_weaponmenu);
  cl.in_vr_weaponmenu = true;
}

void IN_VRWeaponMenuUp(void) {
  KeyUp(&in_vr_weaponmenu);
  cl.in_vr_weaponmenu = false;

  // If a weapon is selected when the menu is released, send the impulse
  int sel = vr_weaponmenu_selection;
  vr_weaponmenu_selection = -1; // Reset selection first

  if (sel >= 0) {
    int impulse = VR_GetSelectedWeaponImpulse(sel);
    if (impulse > 0) {
      char cmd[64];
      q_snprintf(cmd, sizeof(cmd), "impulse %d\n", impulse);
      Cbuf_AddText(cmd);
    }
  }
}

void IN_Impulse(void) {
  in_impulse = Q_atoi(Cmd_Argv(1));
  vr_last_sent_impulse = in_impulse;
  vr_last_sent_impulse_time = Sys_DoubleTime();
}

/*
===============
CL_KeyState

Returns 0.25 if a key was pressed and released during the frame,
0.5 if it was pressed and held
0 if held then released, and
1.0 if held for the entire time
===============
*/
float CL_KeyState(kbutton_t *key) {
  float val;
  qboolean impulsedown, impulseup, down;

  impulsedown = key->state & 2;
  impulseup = key->state & 4;
  down = key->state & 1;
  val = 0;

  if (impulsedown && !impulseup) {
    if (down)
      val = 0.5; // pressed and held this frame
    else
      val = 0; //	I_Error ();
  }
  if (impulseup && !impulsedown) {
    if (down)
      val = 0; //	I_Error ();
    else
      val = 0; // released this frame
  }
  if (!impulsedown && !impulseup) {
    if (down)
      val = 1.0; // held the entire frame
    else
      val = 0; // up the entire frame
  }
  if (impulsedown && impulseup) {
    if (down)
      val = 0.75; // released and re-pressed this frame
    else
      val = 0.25; // pressed and released this frame
  }

  key->state &= 1; // clear impulses

  return val;
}

//==========================================================================

cvar_t cl_upspeed = {"cl_upspeed", "200", CVAR_NONE};
cvar_t cl_forwardspeed = {"cl_forwardspeed", "200", CVAR_ARCHIVE};
cvar_t cl_backspeed = {"cl_backspeed", "200", CVAR_ARCHIVE};
cvar_t cl_sidespeed = {"cl_sidespeed", "350", CVAR_NONE};
cvar_t cl_desktop_vanilla_run = {"cl_desktop_vanilla_run", "1", CVAR_ARCHIVE};
cvar_t cl_trusted_clientmove = {"cl_trusted_clientmove", "1", CVAR_ARCHIVE};
cvar_t cl_trusted_clientmove_desktop = {"cl_trusted_clientmove_desktop", "0", CVAR_ARCHIVE};
cvar_t cl_predictmove = {"cl_predictmove", "1", CVAR_ARCHIVE};
cvar_t cl_move_redundancy = {"cl_move_redundancy", "3", CVAR_ARCHIVE};
cvar_t cl_move_packetdup = {"cl_move_packetdup", "1", CVAR_ARCHIVE};
cvar_t cl_nopred = {"cl_nopred", "0", CVAR_NONE};

cvar_t cl_movespeedkey = {"cl_movespeedkey", "2.0", CVAR_NONE};

cvar_t cl_yawspeed = {"cl_yawspeed", "140", CVAR_NONE};
cvar_t cl_pitchspeed = {"cl_pitchspeed", "150", CVAR_NONE};

cvar_t cl_anglespeedkey = {"cl_anglespeedkey", "1.5", CVAR_NONE};

cvar_t cl_alwaysrun = {"cl_alwaysrun", "0",
                       CVAR_ARCHIVE}; // QuakeSpasm -- new always run

/*
================
CL_AdjustAngles

Moves the local angle positions
================
*/
void CL_AdjustAngles(void) {
  float speed;
  float up, down;

  if ((in_speed.state & 1) ^ (cl_alwaysrun.value != 0.0))
    speed = host_frametime * cl_anglespeedkey.value;
  else
    speed = host_frametime;

  if (!(in_strafe.state & 1)) {
    cl.aimangles[YAW] -= speed * cl_yawspeed.value * CL_KeyState(&in_right);
    cl.aimangles[YAW] += speed * cl_yawspeed.value * CL_KeyState(&in_left);
    cl.aimangles[YAW] = anglemod(cl.aimangles[YAW]);
  }
  if (in_klook.state & 1) {
    V_StopPitchDrift();
    cl.aimangles[PITCH] -=
        speed * cl_pitchspeed.value * CL_KeyState(&in_forward);
    cl.aimangles[PITCH] += speed * cl_pitchspeed.value * CL_KeyState(&in_back);
  }

  up = CL_KeyState(&in_lookup);
  down = CL_KeyState(&in_lookdown);

  cl.aimangles[PITCH] -= speed * cl_pitchspeed.value * up;
  cl.aimangles[PITCH] += speed * cl_pitchspeed.value * down;

  if (up || down)
    V_StopPitchDrift();

  // johnfitz -- variable pitch clamping
  if (cl.aimangles[PITCH] > cl_maxpitch.value)
    cl.aimangles[PITCH] = cl_maxpitch.value;
  if (cl.aimangles[PITCH] < cl_minpitch.value)
    cl.aimangles[PITCH] = cl_minpitch.value;
  // johnfitz

  if (cl.aimangles[ROLL] > 50)
    cl.aimangles[ROLL] = 50;
  if (cl.aimangles[ROLL] < -50)
    cl.aimangles[ROLL] = -50;

  // Flat (non-VR) play has no head/aim divergence — keyboard look must
  // also turn the rendered camera, so mirror aim to view.
  if (!vr_enabled.value)
    VectorCopy(cl.aimangles, cl.viewangles);
}

/*
================
CL_BaseMove

Send the intended movement message to the server
================
*/
void CL_BaseMove(usercmd_t *cmd) {
  float forwardspeed, backspeed;

  if (cls.signon != SIGNONS)
    return;

  CL_AdjustAngles();

  Q_memset(cmd, 0, sizeof(*cmd));

  forwardspeed = cl_forwardspeed.value;
  backspeed = cl_backspeed.value;
  if (!vr_enabled.value && cl_desktop_vanilla_run.value && !cl_alwaysrun.value) {
    if (forwardspeed == 200.0f)
      forwardspeed *= cl_movespeedkey.value;
    if (backspeed == 200.0f)
      backspeed *= cl_movespeedkey.value;
  }

  if (in_strafe.state & 1) {
    cmd->sidemove += cl_sidespeed.value * CL_KeyState(&in_right);
    cmd->sidemove -= cl_sidespeed.value * CL_KeyState(&in_left);
  }

  cmd->sidemove += cl_sidespeed.value * CL_KeyState(&in_moveright);
  cmd->sidemove -= cl_sidespeed.value * CL_KeyState(&in_moveleft);

  cmd->upmove += cl_upspeed.value * CL_KeyState(&in_up);
  cmd->upmove -= cl_upspeed.value * CL_KeyState(&in_down);

  if (!(in_klook.state & 1)) {
    cmd->forwardmove += forwardspeed * CL_KeyState(&in_forward);
    cmd->forwardmove -= backspeed * CL_KeyState(&in_back);
  }

  //
  // adjust for speed key
  //
  if ((in_speed.state & 1) ^ (cl_alwaysrun.value != 0.0)) {
    cmd->forwardmove *= cl_movespeedkey.value;
    cmd->sidemove *= cl_movespeedkey.value;
    cmd->upmove *= cl_movespeedkey.value;
  }
}

/*
==============
CL_SendMove
==============
*/
static void CL_WriteUsercmd(sizebuf_t *buf, const usercmd_t *histcmd) {
  int i;
  int extbits = 0;
  int msec;

  if (histcmd->vr_active)
    extbits |= MOVEEXT_VR;
  if (histcmd->trusted_active)
    extbits |= MOVEEXT_TRUSTED;
  if (histcmd->weapon || histcmd->cursor_screen[0] || histcmd->cursor_screen[1] ||
      histcmd->cursor_start[0] || histcmd->cursor_start[1] || histcmd->cursor_start[2] ||
      histcmd->cursor_impact[0] || histcmd->cursor_impact[1] || histcmd->cursor_impact[2] ||
      histcmd->cursor_entitynumber)
    extbits |= MOVEEXT_QCINPUT;

  msec = (int)(histcmd->seconds * 1000.0f + 0.5f);
  msec = CLAMP(1, msec, 255);

  MSG_WriteByte(buf, msec);
  MSG_WriteFloat(buf, histcmd->servertime);

  for (i = 0; i < 3; i++)
    MSG_WriteAngle16(buf, histcmd->viewangles[i], cl.protocolflags);

  MSG_WriteShort(buf, histcmd->forwardmove);
  MSG_WriteShort(buf, histcmd->sidemove);
  MSG_WriteShort(buf, histcmd->upmove);
  MSG_WriteByte(buf, histcmd->buttons);
  MSG_WriteByte(buf, histcmd->impulse);
  MSG_WriteByte(buf, extbits);

  if (extbits & MOVEEXT_VR) {
    MSG_WriteFloat(buf, histcmd->vr_handpos[0]);
    MSG_WriteFloat(buf, histcmd->vr_handpos[1]);
    MSG_WriteFloat(buf, histcmd->vr_handpos[2]);
    MSG_WriteFloat(buf, histcmd->vr_handrot[0]);
    MSG_WriteFloat(buf, histcmd->vr_handrot[1]);
    MSG_WriteFloat(buf, histcmd->vr_handrot[2]);
    MSG_WriteFloat(buf, histcmd->vr_roomscalemove[0]);
    MSG_WriteFloat(buf, histcmd->vr_roomscalemove[1]);
    MSG_WriteFloat(buf, histcmd->vr_roomscalemove[2]);
  }

  if (extbits & MOVEEXT_TRUSTED) {
    MSG_WriteFloat(buf, histcmd->trusted_origin[0]);
    MSG_WriteFloat(buf, histcmd->trusted_origin[1]);
    MSG_WriteFloat(buf, histcmd->trusted_origin[2]);
    MSG_WriteFloat(buf, histcmd->trusted_velocity[0]);
    MSG_WriteFloat(buf, histcmd->trusted_velocity[1]);
    MSG_WriteFloat(buf, histcmd->trusted_velocity[2]);
  }

  if (extbits & MOVEEXT_QCINPUT) {
    MSG_WriteLong(buf, histcmd->weapon);
    MSG_WriteShort(buf, histcmd->cursor_screen[0] * 32767);
    MSG_WriteShort(buf, histcmd->cursor_screen[1] * 32767);
    MSG_WriteFloat(buf, histcmd->cursor_start[0]);
    MSG_WriteFloat(buf, histcmd->cursor_start[1]);
    MSG_WriteFloat(buf, histcmd->cursor_start[2]);
    MSG_WriteFloat(buf, histcmd->cursor_impact[0]);
    MSG_WriteFloat(buf, histcmd->cursor_impact[1]);
    MSG_WriteFloat(buf, histcmd->cursor_impact[2]);
    MSG_WriteEntity(buf, histcmd->cursor_entitynumber, cl.protocol_pext2);
  }
}

void CL_SendMove(const usercmd_t *cmd) {
  int seq;
  int start;
  int redundancy;
  int s;
  int i;
  int count;
  int dup;
  int sendseqs[MOVE_BUNDLE_MAX];
  int flags;
  qboolean local_singleplayer;
  usercmd_t sendcmd;
  sizebuf_t buf;
  byte data[MAX_DATAGRAM];

  buf.maxsize = sizeof(data);
  buf.cursize = 0;
  buf.data = data;
  buf.allowoverflow = false;
  buf.overflowed = false;

  //
  // deliver the message
  //
  if (cls.demoplayback)
    return;

  if (!cmd)
    return;

  local_singleplayer = sv.active && svs.maxclients <= 1;

  sendcmd = *cmd;
  VectorCopy(cl.aimangles, sendcmd.viewangles);
  sendcmd.servertime = cl.time;
  if (sendcmd.seconds <= 0)
    sendcmd.seconds = host_frametime;

  //
  // Capture edge-triggered inputs into this command before putting it in the
  // resend history. Redundant sends must replay the original command, not this
  // frame's current button/impulse state.
  //
  sendcmd.buttons = 0;
  if (!cl.in_vr_weaponmenu && (in_attack.state & 3))
    sendcmd.buttons |= 1;
  in_attack.state &= ~2;

  if (in_jump.state & 3)
    sendcmd.buttons |= 2;
  in_jump.state &= ~2;

  sendcmd.impulse = in_impulse;
  in_impulse = 0;

  Q_memset(sendcmd.vr_handpos, 0, sizeof(sendcmd.vr_handpos));
  Q_memset(sendcmd.vr_handrot, 0, sizeof(sendcmd.vr_handrot));
  Q_memset(sendcmd.vr_roomscalemove, 0, sizeof(sendcmd.vr_roomscalemove));
  sendcmd.vr_active = false;

  if (vr_enabled.value && (int)vr_aimmode.value == VR_AIMMODE_CONTROLLER) {
    sendcmd.vr_active = true;
    VR_GetMuzzleAdjustedHandPos(sendcmd.vr_handpos);
    VectorCopy(cl.handrot[1], sendcmd.vr_handrot);
    VectorCopy(vr_room_scale_move, sendcmd.vr_roomscalemove);
  }

  Q_memset(sendcmd.trusted_origin, 0, sizeof(sendcmd.trusted_origin));
  Q_memset(sendcmd.trusted_velocity, 0, sizeof(sendcmd.trusted_velocity));
  sendcmd.trusted_active = false;

  if (!local_singleplayer &&
      cl_trusted_clientmove.value && (vr_enabled.value || cl_trusted_clientmove_desktop.value) &&
      cls.trusted_clientmove_allowed &&
      cls.state == ca_connected && cls.signon == SIGNONS &&
      cl.viewentity > 0 && cl.viewentity < cl.num_entities &&
      cl_entities[cl.viewentity].model) {
    entity_t *ent = &cl_entities[cl.viewentity];
    sendcmd.trusted_active = true;
    VectorCopy(ent->origin, sendcmd.trusted_origin);
    VectorCopy(cl.velocity, sendcmd.trusted_velocity);
  }

  seq = cl.movemessages++;
  sendcmd.sequence = seq;
  cl.movecmds[seq & (CL_MOVE_HISTORY - 1)] = sendcmd;
  cl.cmd = sendcmd;

  if (local_singleplayer) {
    cl.ackedmovemessages = seq;
    cl_lagdebug_last_sendmove = 0;
  } else if (seq < 2) {
    //
    // Always dump the first two network moves, because they may contain
    // leftover inputs from the last level.
    //
    cl.ackedmovemessages = seq;
    cl_lagdebug_last_sendmove = 0;
    return;
  }

  if (cmd && net_lagdebug.value && cl_lagdebug_last_sendmove > 0) {
    double gap = realtime - cl_lagdebug_last_sendmove;
    if (gap > net_lagdebug_threshold.value &&
        realtime - cl_lagdebug_last_sendmove_log > 0.5) {
      Con_Printf("net_lagdebug: client sendmove gap %.3f sec host_dt=%.3f move=(%g,%g,%g) buttons=%d impulse=%d state=%d signon=%d lastmsg_age=%.3f\n",
                 gap, host_frametime, cmd->forwardmove, cmd->sidemove,
                 cmd->upmove, sendcmd.buttons, sendcmd.impulse, cls.state, cls.signon,
                 realtime - cl.last_received_message);
      cl_lagdebug_last_sendmove_log = realtime;
    }
  }
  cl_lagdebug_last_sendmove = realtime;

  redundancy = local_singleplayer ? 0 : (int)cl_move_redundancy.value;
  if (redundancy < 0)
    redundancy = 0;
  if (redundancy > MOVE_BUNDLE_MAX - 1)
    redundancy = MOVE_BUNDLE_MAX - 1;

  start = seq - redundancy;
  if (start < 2)
    start = 2;
  if (cl.ackedmovemessages >= 2 && start < cl.ackedmovemessages + 1)
    start = cl.ackedmovemessages + 1;
  if (start > seq)
    start = seq;

  count = 0;
  for (s = start; s <= seq; s++) {
    const usercmd_t *histcmd = &cl.movecmds[s & (CL_MOVE_HISTORY - 1)];
    if (histcmd->sequence != s)
      continue;
    if (count < MOVE_BUNDLE_MAX)
      sendseqs[count++] = s;
  }

  if (!count) {
    sendseqs[count++] = seq;
  }

  for (i = 0; i < cl.ackframes_count; i++) {
    MSG_WriteByte(&buf, clcdp_ackframe);
    MSG_WriteLong(&buf, cl.ackframes[i]);
  }
  cl.ackframes_count = 0;

  flags = 0;
  if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)) {
    if (cl.net_snapshot_have)
      flags |= MOVE_BUNDLE_SNAPSHOTACK;
    if (cl.net_snapshot_partial_active &&
        (!cl.net_snapshot_have ||
         cl.net_snapshot_partial_sequence > cl.net_snapshot_sequence))
      flags |= MOVE_BUNDLE_SNAPSHOTPARTS;
  }

  MSG_WriteByte(&buf, clc_move);
  MSG_WriteShort(&buf, seq & 0xffff);
  MSG_WriteByte(&buf, count);
  MSG_WriteByte(&buf, flags);
  if (flags & MOVE_BUNDLE_SNAPSHOTACK) {
    MSG_WriteShort(&buf, cl.net_snapshot_sequence & 0xffff);
    cl.net_snapshot_acks_sent++;
  }
  if (flags & MOVE_BUNDLE_SNAPSHOTPARTS) {
    MSG_WriteShort(&buf, cl.net_snapshot_partial_sequence & 0xffff);
    MSG_WriteByte(&buf, cl.net_snapshot_partial_last_part >= 0 ?
                  cl.net_snapshot_partial_last_part : SNAPSHOT_PART_UNKNOWN);
    for (i = 0; i < SNAPSHOT_ACK_MASK_WORDS; i++)
      MSG_WriteLong(&buf, cl.net_snapshot_partial_mask[i]);
    cl.net_snapshot_part_resend_acks_sent++;
  }

  for (i = 0; i < count; i++) {
    const usercmd_t *histcmd = &cl.movecmds[sendseqs[i] & (CL_MOVE_HISTORY - 1)];
    CL_WriteUsercmd(&buf, histcmd);
  }

  if (buf.overflowed) {
    Con_Printf("CL_SendMove: move bundle overflowed (%d cmds)\n", count);
    return;
  }

  dup = local_singleplayer ? 0 : (int)cl_move_packetdup.value;
  dup = CLAMP(0, dup, 3);

  for (i = 0; i <= dup; i++) {
    if (NET_SendUnreliableMessage(cls.netcon, &buf) == -1) {
      Con_Printf("CL_SendMove: lost server connection\n");
      CL_Disconnect();
      return;
    }
  }

  cl.net_move_packets_sent += dup + 1;
  cl.net_move_dup_packets_sent += dup;
  cl.net_move_cmds_sent += count;
  cl.net_move_last_packet_cmds = count;

  if (net_lagdebug.value && (count > 1 || dup > 0))
    Con_DPrintf("net_lagdebug: client sent move bundle seq=%d cmds=%d dup=%d acksnap=%d bytes=%d\n",
                seq, count, dup, (flags & MOVE_BUNDLE_SNAPSHOTACK) ? cl.net_snapshot_sequence : -1,
                buf.cursize);
}

/*
============
CL_InitInput
============
*/
void CL_InitInput(void) {
  Cmd_AddCommand("+moveup", IN_UpDown);
  Cmd_AddCommand("-moveup", IN_UpUp);
  Cmd_AddCommand("+movedown", IN_DownDown);
  Cmd_AddCommand("-movedown", IN_DownUp);
  Cmd_AddCommand("+left", IN_LeftDown);
  Cmd_AddCommand("-left", IN_LeftUp);
  Cmd_AddCommand("+right", IN_RightDown);
  Cmd_AddCommand("-right", IN_RightUp);
  Cmd_AddCommand("+forward", IN_ForwardDown);
  Cmd_AddCommand("-forward", IN_ForwardUp);
  Cmd_AddCommand("+back", IN_BackDown);
  Cmd_AddCommand("-back", IN_BackUp);
  Cmd_AddCommand("+lookup", IN_LookupDown);
  Cmd_AddCommand("-lookup", IN_LookupUp);
  Cmd_AddCommand("+lookdown", IN_LookdownDown);
  Cmd_AddCommand("-lookdown", IN_LookdownUp);
  Cmd_AddCommand("+strafe", IN_StrafeDown);
  Cmd_AddCommand("-strafe", IN_StrafeUp);
  Cmd_AddCommand("+moveleft", IN_MoveleftDown);
  Cmd_AddCommand("-moveleft", IN_MoveleftUp);
  Cmd_AddCommand("+moveright", IN_MoverightDown);
  Cmd_AddCommand("-moveright", IN_MoverightUp);
  Cmd_AddCommand("+speed", IN_SpeedDown);
  Cmd_AddCommand("-speed", IN_SpeedUp);
  Cmd_AddCommand("+attack", IN_AttackDown);
  Cmd_AddCommand("-attack", IN_AttackUp);
  Cmd_AddCommand("+use", IN_UseDown);
  Cmd_AddCommand("-use", IN_UseUp);
  Cmd_AddCommand("+jump", IN_JumpDown);
  Cmd_AddCommand("-jump", IN_JumpUp);
  Cmd_AddCommand("impulse", IN_Impulse);
  Cmd_AddCommand("+klook", IN_KLookDown);
  Cmd_AddCommand("-klook", IN_KLookUp);
  Cmd_AddCommand("+mlook", IN_MLookDown);
  Cmd_AddCommand("-mlook", IN_MLookUp);
  Cmd_AddCommand("+vr_weaponmenu", IN_VRWeaponMenuDown);
  Cmd_AddCommand("-vr_weaponmenu", IN_VRWeaponMenuUp);
  Cmd_AddCommand("vr_turn180", IN_VRTurn180_f);
}
