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
#include "pmove.h"
#include "vr.h"
#include "vrik_codec.h"

extern cvar_t cl_maxpitch; // johnfitz -- variable pitch clamping
extern cvar_t cl_minpitch; // johnfitz -- variable pitch clamping

cvar_t cl_iDrive = {"cl_iDrive", "1", CVAR_ARCHIVE};
cvar_t cl_nettest_vr = {"cl_nettest_vr", "0", CVAR_NONE};
cvar_t cl_nettest_roomscale_x = {"cl_nettest_roomscale_x", "0", CVAR_NONE};
cvar_t cl_nettest_roomscale_y = {"cl_nettest_roomscale_y", "0", CVAR_NONE};
cvar_t cl_nettest_roomscale_z = {"cl_nettest_roomscale_z", "0", CVAR_NONE};
cvar_t cl_nettest_hand_x = {"cl_nettest_hand_x", "16", CVAR_NONE};
cvar_t cl_nettest_hand_y = {"cl_nettest_hand_y", "0", CVAR_NONE};
cvar_t cl_nettest_hand_z = {"cl_nettest_hand_z", "16", CVAR_NONE};

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
kbutton_t in_strafe, in_speed, in_jump, in_attack;
kbutton_t in_button3, in_button4, in_button5, in_button6, in_button7, in_button8;
kbutton_t in_up, in_down;
kbutton_t in_vr_weaponmenu;

int in_impulse;
static double cl_lagdebug_last_sendmove;
static double cl_lagdebug_last_sendmove_log;

#define CL_MOVE_PACKET_LIMIT DATAGRAM_MTU

static qboolean CL_InputContentsIsWater(int contents)
{
  switch (contents) {
  case CONTENTS_WATER:
  case CONTENTS_SLIME:
  case CONTENTS_LAVA:
    return true;
  default:
    return false;
  }
}

static qboolean CL_InputInWater(void)
{
  mleaf_t *leaf;

  if (cl.worldmodel) {
    leaf = Mod_PointInLeaf(listener_origin, cl.worldmodel);
    return leaf && CL_InputContentsIsWater(leaf->contents);
  }
  return cl.inwater;
}

static qboolean CL_ShouldTranslateJumpToMoveUp(void)
{
  return !vr_enabled.value && cl.stats[STAT_HEALTH] > 0 && CL_InputInWater();
}

void KeyDown(kbutton_t *b) {
  int k;
  const char *c;

  c = Cmd_Argv(1);
  if (c[0])
    k = atoi(c);
  else
    k = -1; // typed manually at the console for continuous down

  if (b == &in_jump && CL_ShouldTranslateJumpToMoveUp())
    b = &in_up;

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
  b->downtime = realtime;
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
    b->uptime = realtime;
    return;
  }

  if (b == &in_jump && !vr_enabled.value) {
    if (k == in_up.down[0] || k == in_up.down[1])
      b = &in_up;
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
  b->uptime = realtime;
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

void IN_UseDown(void) { KeyDown(&in_button3); }
void IN_UseUp(void) { KeyUp(&in_button3); }
void IN_JumpDown(void) { KeyDown(&in_jump); }
void IN_JumpUp(void) { KeyUp(&in_jump); }
void IN_Button3Down(void) { KeyDown(&in_button3); }
void IN_Button3Up(void) { KeyUp(&in_button3); }
void IN_Button4Down(void) { KeyDown(&in_button4); }
void IN_Button4Up(void) { KeyUp(&in_button4); }
void IN_Button5Down(void) { KeyDown(&in_button5); }
void IN_Button5Up(void) { KeyUp(&in_button5); }
void IN_Button6Down(void) { KeyDown(&in_button6); }
void IN_Button6Up(void) { KeyUp(&in_button6); }
void IN_Button7Down(void) { KeyDown(&in_button7); }
void IN_Button7Up(void) { KeyUp(&in_button7); }
void IN_Button8Down(void) { KeyDown(&in_button8); }
void IN_Button8Up(void) { KeyUp(&in_button8); }

void IN_VRWeaponMenuDown(void) {
  qboolean was_down = (in_vr_weaponmenu.state & 1) != 0;

  KeyDown(&in_vr_weaponmenu);
  if (!was_down)
    VR_BeginWeaponMenu();
  cl.in_vr_weaponmenu = true;
}

void IN_VRWeaponMenuUp(void) {
  KeyUp(&in_vr_weaponmenu);
  cl.in_vr_weaponmenu = false;

  // If a weapon is selected when the menu is released, send the impulse
  int sel = vr_weaponmenu_selection;
  int sel_type = vr_weaponmenu_selection_type;
  VR_EndWeaponMenu();
  vr_weaponmenu_selection = -1; // Reset selection first
  vr_weaponmenu_selection_type = VR_WEAPONMENU_SELECTION_NONE;

  if (sel < 0)
    return;

  if (sel_type == VR_WEAPONMENU_SELECTION_PLAYER)
    VR_SelectPlayerFromMenu(sel);
  else if (sel_type == VR_WEAPONMENU_SELECTION_RESPAWN)
    VR_SelectRespawnFromMenu();
  else if (sel_type == VR_WEAPONMENU_SELECTION_QUICKSAVE)
    VR_SelectQuickSaveFromMenu();
  else if (sel_type == VR_WEAPONMENU_SELECTION_QUICKLOAD)
    VR_SelectQuickLoadFromMenu();
  else
    VR_SelectWeaponFromMenu(sel);
}

void IN_Impulse(void) {
  in_impulse = Q_atoi(Cmd_Argv(1));
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
float CL_KeyState(kbutton_t *key, qboolean isfinal) {
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

  if (isfinal)
    key->state &= 1; // clear impulses

  return val;
}

//==========================================================================

cvar_t cl_upspeed = {"cl_upspeed", "200", CVAR_NONE};
cvar_t cl_forwardspeed = {"cl_forwardspeed", "200", CVAR_ARCHIVE};
cvar_t cl_backspeed = {"cl_backspeed", "200", CVAR_ARCHIVE};
cvar_t cl_sidespeed = {"cl_sidespeed", "350", CVAR_NONE};
cvar_t cl_desktop_vanilla_run = {"cl_desktop_vanilla_run", "1", CVAR_ARCHIVE};
cvar_t cl_predictmove = {"cl_predictmove", "1", CVAR_ARCHIVE};
cvar_t cl_nopred = {"cl_nopred", "0", CVAR_NONE};

cvar_t cl_movespeedkey = {"cl_movespeedkey", "2.0", CVAR_NONE};

cvar_t cl_yawspeed = {"cl_yawspeed", "140", CVAR_NONE};
cvar_t cl_pitchspeed = {"cl_pitchspeed", "150", CVAR_NONE};

cvar_t cl_anglespeedkey = {"cl_anglespeedkey", "1.5", CVAR_NONE};

cvar_t cl_alwaysrun = {"cl_alwaysrun", "0",
                       CVAR_ARCHIVE}; // QuakeSpasm -- new always run

/*
================
CL_InCutscene

Scripted camera entities repeatedly set the view angle while hiding the
viewmodel.  Do not let keyboard/controller pitch drift fight those updates;
the tracked VR head pose remains independent of the game-input angles.
================
*/
static qboolean CL_InCutscene(void) {
  return cl.fixangle && !cl.viewent.model;
}

/*
================
CL_AdjustAngles

Moves the local angle positions
================
*/
void CL_AdjustAngles(void) {
  float speed;
  float up, down;

  if (CL_InCutscene()) {
    V_StopPitchDrift();
    return;
  }

  if ((in_speed.state & 1) ^ (cl_alwaysrun.value != 0.0))
    speed = host_frametime * cl_anglespeedkey.value;
  else
    speed = host_frametime;

  if (!(in_strafe.state & 1)) {
    cl.aimangles[YAW] -= speed * cl_yawspeed.value * CL_KeyState(&in_right, true);
    cl.aimangles[YAW] += speed * cl_yawspeed.value * CL_KeyState(&in_left, true);
    cl.aimangles[YAW] = anglemod(cl.aimangles[YAW]);
  }
  if (in_klook.state & 1) {
    V_StopPitchDrift();
    cl.aimangles[PITCH] -=
        speed * cl_pitchspeed.value * CL_KeyState(&in_forward, true);
    cl.aimangles[PITCH] += speed * cl_pitchspeed.value * CL_KeyState(&in_back, true);
  }

  up = CL_KeyState(&in_lookup, true);
  down = CL_KeyState(&in_lookdown, true);

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
static void CL_KeyStatePair(kbutton_t *positive, kbutton_t *negative,
                            qboolean isfinal, float *pos, float *neg)
{
  *pos = CL_KeyState(positive, isfinal);
  *neg = CL_KeyState(negative, isfinal);
  if (!cl_iDrive.value || !*pos || !*neg)
    return;

  if (positive->downtime > negative->downtime)
    *neg = 0;
  else if (positive->downtime < negative->downtime)
    *pos = 0;
}

void CL_BaseMove(usercmd_t *cmd, qboolean isfinal) {
  float forwardspeed, backspeed;
  float s1, s2;

  Q_memset(cmd, 0, sizeof(*cmd));
  VectorCopy(cl.aimangles, cmd->viewangles);

  if (cls.signon != SIGNONS)
    return;

  forwardspeed = cl_forwardspeed.value;
  backspeed = cl_backspeed.value;
  if (!vr_enabled.value && cl_desktop_vanilla_run.value && !cl_alwaysrun.value) {
    if (forwardspeed == 200.0f)
      forwardspeed *= cl_movespeedkey.value;
    if (backspeed == 200.0f)
      backspeed *= cl_movespeedkey.value;
  }

  if (in_strafe.state & 1) {
    CL_KeyStatePair(&in_right, &in_left, isfinal, &s1, &s2);
    cmd->sidemove += cl_sidespeed.value * s1;
    cmd->sidemove -= cl_sidespeed.value * s2;
  }

  CL_KeyStatePair(&in_moveright, &in_moveleft, isfinal, &s1, &s2);
  cmd->sidemove += cl_sidespeed.value * s1;
  cmd->sidemove -= cl_sidespeed.value * s2;

  CL_KeyStatePair(&in_up, &in_down, isfinal, &s1, &s2);
  cmd->upmove += cl_upspeed.value * s1;
  cmd->upmove -= cl_upspeed.value * s2;

  if (!(in_klook.state & 1)) {
    CL_KeyStatePair(&in_forward, &in_back, isfinal, &s1, &s2);
    cmd->forwardmove += forwardspeed * s1;
    cmd->forwardmove -= backspeed * s2;
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

void CL_FinishMove(usercmd_t *cmd, qboolean isfinal)
{
  static kbutton_t *buttons[] = {
      &in_attack,
      &in_jump,
      &in_button3,
      &in_button4,
      &in_button5,
      &in_button6,
      &in_button7,
      &in_button8,
  };
  int i;

  cmd->buttons = 0;
  for (i = 0; i < (int)countof(buttons); i++) {
    if (buttons[i]->state & 3) {
      if (i != 0 || !cl.in_vr_weaponmenu)
        cmd->buttons |= 1 << i;
      if (isfinal)
        buttons[i]->state &= ~2;
    }
  }

  cmd->impulse = in_impulse;
  if (isfinal)
    in_impulse = 0;

  cmd->forwardmove += cl.accummoves[0];
  cmd->sidemove += cl.accummoves[1];
  cmd->upmove += cl.accummoves[2];

  if (vr_enabled.value && cl.inwater && (cmd->buttons & BUTTON_JUMP) &&
      cmd->upmove < cl_upspeed.value)
    cmd->upmove = cl_upspeed.value;

  if (vr_enabled.value && (int)vr_aimmode.value == VR_AIMMODE_CONTROLLER)
    VectorAdd(cmd->vr_roomscalemove, cl.vr_roomscalemove_accum,
              cmd->vr_roomscalemove);

  if (isfinal) {
    VectorCopy(vec3_origin, cl.accummoves);
    VectorCopy(vec3_origin, cl.vr_roomscalemove_accum);
  }

  cmd->sequence = cl.movemessages;
  cmd->servertime = cl.time;
  cmd->seconds = cmd->servertime - cl.lastcmdtime;
}

/*
==============
CL_SendMove
==============
*/
static void CL_WriteUsercmd(sizebuf_t *buf, const usercmd_t *histcmd) {
  int i;
  int extbits = 0;

  if (histcmd->vr_active) {
    extbits |= MOVEEXT_VR;
    if (histcmd->vr_handpos_relative)
      extbits |= MOVEEXT_VR_RELATIVE;
  }
  if (histcmd->weapon || histcmd->cursor_screen[0] || histcmd->cursor_screen[1] ||
      histcmd->cursor_start[0] || histcmd->cursor_start[1] || histcmd->cursor_start[2] ||
      histcmd->cursor_impact[0] || histcmd->cursor_impact[1] || histcmd->cursor_impact[2] ||
      histcmd->cursor_entitynumber)
    extbits |= MOVEEXT_QCINPUT;

  MSG_WriteFloat(buf, histcmd->servertime);
  if (cl.protocol_pext2 & PEXT2_EXPLICITCMDMSEC)
    MSG_WriteByte(buf, histcmd->msec);

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

static void CL_WriteAckFrames(sizebuf_t *buf)
{
  unsigned int i;

  for (i = 0; i < cl.ackframes_count; i++) {
    MSG_WriteByte(buf, clcdp_ackframe);
    MSG_WriteLong(buf, cl.ackframes[i]);
    cl.net_snapshot_acks_sent++;
  }

  cl.ackframes_count = 0;
}

void CL_FlushAckFrames(void)
{
  sizebuf_t buf;
  byte data[CL_MOVE_PACKET_LIMIT];

  if (!cl.ackframes_count || cls.demoplayback || !cls.netcon)
    return;

  buf.maxsize = sizeof(data);
  buf.cursize = 0;
  buf.data = data;
  buf.allowoverflow = false;
  buf.overflowed = false;

  CL_WriteAckFrames(&buf);
  if (!buf.cursize)
    return;

  if (NET_SendUnreliableMessage(cls.netcon, &buf) == -1) {
    Con_Printf("CL_FlushAckFrames: lost server connection\n");
    CL_Disconnect();
  }
}

/*
 * The server advances negotiated commands by this duration rather than by a
 * servertime gap.  realtime is monotonic, unlike the server-clock diagnostic
 * carried in usercmd_t::servertime.  Keep the fractional remainder so normal
 * frame pacing does not accumulate a millisecond of drift every command.
 */
static unsigned char CL_SampleMoveMsec(void)
{
  double elapsed;
  double milliseconds;
  int msec;

  if (!(cl.protocol_pext2 & PEXT2_EXPLICITCMDMSEC))
    return 0;

  if (!cl.move_msec_sample_valid) {
    cl.move_msec_sample_valid = true;
    cl.move_msec_sample_time = realtime;
    cl.move_msec_fractional_carry = 0;
    elapsed = host_frametime;
  } else {
    elapsed = realtime - cl.move_msec_sample_time;
    cl.move_msec_sample_time = realtime;
  }

  if (elapsed < 0) {
    elapsed = 0;
    cl.move_msec_fractional_carry = 0;
  }

  milliseconds = elapsed * 1000.0 + cl.move_msec_fractional_carry;
  msec = (int)milliseconds;
  if (msec < 1)
    msec = 1;
  if (msec > 125) {
    /* A suspend/hitch must not donate its elapsed gap to game movement. */
    msec = 125;
    cl.move_msec_fractional_carry = 0;
  } else {
    cl.move_msec_fractional_carry = milliseconds - msec;
  }

  return (unsigned char)msec;
}

static qboolean CL_VRVectorIsFinite(const vec3_t value)
{
  return isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]);
}

static qboolean CL_VRRoomScaleSampleAccepted(const vec3_t move)
{
  float horizontal_length;

  if (!CL_VRVectorIsFinite(move))
    return false;
  horizontal_length = sqrtf(move[0] * move[0] + move[1] * move[1]);
  return horizontal_length <= 16.0f && fabsf(move[2]) <= 16.0f;
}

static qboolean CL_VRIKPoseToV2(const vrik_pose_t *pose,
                                vrik_v2_pose_t *out)
{
  int tracker;

  if (!pose || !out)
    return false;
  Q_memset(out, 0, sizeof(*out));
  out->sequence = pose->sequence;
  out->flags = pose->flags;
  out->body_yaw = pose->body_yaw;
  for (tracker = 0; tracker < VRIK_TRACKER_COUNT; tracker++) {
    VectorCopy(pose->position[tracker], out->targets[tracker].position);
    VectorCopy(pose->orientation[tracker], out->targets[tracker].orientation);
  }
  VectorCopy(pose->aim_orientation, out->aim_orientation);
  return vrik_v2_validate_legacy_pose(out) == VRIK_CODEC_OK;
}

static qboolean CL_VRIKPoseToV3(const vrik_pose_t *pose,
                                vrik_codec_pose_t *out)
{
  vrik_v2_pose_t legacy;

  if (!CL_VRIKPoseToV2(pose, &legacy))
    return false;
  /* The tracker adapter will extend this normalized sample with calibrated
   * HIP/foot targets.  Keeping the conversion here makes v3's wire path
   * independent of the legacy renderer-facing pose type. */
  return vrik_v2_to_normalized(&legacy, out) == VRIK_CODEC_OK;
}

/* VRIK is a standalone newest-pose message: never put it in move history. */
static void CL_AppendVRIKPose(sizebuf_t *buf)
{
  vrik_pose_t pose;
  vrik_v2_pose_t pose_v2;
  vrik_codec_pose_t pose_v3;
  uint8_t encoded[VRIK_V3_MAX_BODY_BYTES];
  size_t encoded_bytes;
  size_t available;
  qboolean captured;
  qboolean v3;
  int i;

  if (!cl.vrik_protocol_offered || !cl.vrik_cap_sent ||
      realtime < cl.vrik_next_send_time)
    return;

  v3 = cl.vrik_protocol_version >= VRIK_PROTOCOL_VERSION;

  Q_memset(&pose, 0, sizeof(pose));
  captured = VR_GetVRIKPose(&pose);
  if (captured && !CL_VRIKPoseToV2(&pose, &pose_v2)) {
    Con_DPrintf("CL_SendMove: discarded invalid VRIK tracking sample\n");
    captured = false;
  }

  /* Transmit one immediate inactive sample when VRIK is turned off. */
  if (!captured) {
    if (!cl.vrik_last_sent_active)
      return;
    pose.flags = 0;
  }

  pose.sequence = cl.vrik_next_sequence;
  if (!CL_VRIKPoseToV2(&pose, &pose_v2))
    return;

  encoded_bytes = 0;
  if (v3) {
    if (!CL_VRIKPoseToV3(&pose, &pose_v3))
      return;
    /* The bridge appends only calibrated, filtered FBT roles from the
     * current successful host snapshot.  No active profile leaves pose_v3
     * unchanged, preserving the ordinary head/hands v3 path. */
    if (pose.flags & VRIK_FLAG_ACTIVE)
      (void)VR_AppendFBTToVRIKPose(&pose_v3);
    if (vrik_v3_encode(&pose_v3, encoded, sizeof(encoded), &encoded_bytes) !=
            VRIK_CODEC_OK ||
        encoded_bytes > VRIK_V3_MAX_BODY_BYTES)
      return;
    available = 1 + 1 + encoded_bytes;
  } else {
    if (vrik_v2_encode(&pose_v2, encoded, sizeof(encoded), &encoded_bytes) !=
        VRIK_CODEC_OK)
      return;
    available = 1 + encoded_bytes;
  }
  if (buf->cursize + (int)available > buf->maxsize)
    return;
  MSG_WriteByte(buf, clc_vrikpose);
  if (v3)
    MSG_WriteByte(buf, (int)encoded_bytes);
  for (i = 0; i < (int)encoded_bytes; ++i)
    MSG_WriteByte(buf, encoded[i]);
  /* Do not consume a sequence number until the complete framed message fits
   * and was written.  A later retry must retain this newest-pose sequence. */
  cl.vrik_next_sequence++;
  if ((pose.flags & VRIK_FLAG_ACTIVE) && !cl.vrik_last_sent_active)
    Con_DPrintf("VRIK: publishing active tracked pose\n");
  cl.vrik_last_sent_active = (pose.flags & VRIK_FLAG_ACTIVE) != 0;
  cl.vrik_next_send_time = realtime + 0.05;
}

void CL_SendMove(const usercmd_t *cmd) {
  int seq;
  int first_seq;
  int packet_cmds;
  qboolean local_singleplayer;
  usercmd_t sendcmd;
  sizebuf_t buf;
  byte data[CL_MOVE_PACKET_LIMIT];

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

  if (!cmd) {
    CL_FlushAckFrames();
    return;
  }

  local_singleplayer = sv.active && svs.maxclients <= 1;

  sendcmd = *cmd;
  VectorCopy(cl.aimangles, sendcmd.viewangles);
  VR_UpdateCommandViewAngles(&sendcmd);
  if (sendcmd.servertime <= 0)
    sendcmd.servertime = cl.time;
  sendcmd.msec = CL_SampleMoveMsec();
  if (sendcmd.msec)
    sendcmd.seconds = sendcmd.msec * 0.001f;

  Q_memset(sendcmd.vr_handpos, 0, sizeof(sendcmd.vr_handpos));
  Q_memset(sendcmd.vr_handrot, 0, sizeof(sendcmd.vr_handrot));
  sendcmd.vr_active = false;
  sendcmd.vr_handpos_relative = false;

  if (cl_nettest_vr.value) {
    sendcmd.vr_active = true;
    sendcmd.vr_handpos_relative = true;
    sendcmd.vr_handpos[0] = cl_nettest_hand_x.value;
    sendcmd.vr_handpos[1] = cl_nettest_hand_y.value;
    sendcmd.vr_handpos[2] = cl_nettest_hand_z.value;
    VectorCopy(vec3_origin, sendcmd.vr_handrot);
    sendcmd.vr_roomscalemove[0] = cl_nettest_roomscale_x.value;
    sendcmd.vr_roomscalemove[1] = cl_nettest_roomscale_y.value;
    sendcmd.vr_roomscalemove[2] = cl_nettest_roomscale_z.value;
  } else if (vr_enabled.value &&
             (int)vr_aimmode.value == VR_AIMMODE_CONTROLLER) {
    vec3_t world_muzzle;

    sendcmd.vr_active = true;
    VR_GetMuzzleAdjustedHandPos(world_muzzle);
    if (cl.vr_relative_muzzle_supported) {
	  vec3_t pose_origin;
	  VectorCopy(cl.entities[cl.viewentity].origin, pose_origin);
	  if (CL_VRRoomScaleSampleAccepted(sendcmd.vr_roomscalemove)) {
	    pose_origin[0] += sendcmd.vr_roomscalemove[0];
	    pose_origin[1] += sendcmd.vr_roomscalemove[1];
	  }
      sendcmd.vr_handpos_relative = true;
      VectorSubtract(world_muzzle, pose_origin, sendcmd.vr_handpos);
    } else {
      VectorCopy(world_muzzle, sendcmd.vr_handpos);
    }
    VectorCopy(cl.handrot[1], sendcmd.vr_handrot);
  } else {
    VectorCopy(vec3_origin, sendcmd.vr_roomscalemove);
  }

  if (sendcmd.vr_active &&
      (!CL_VRVectorIsFinite(sendcmd.vr_handpos) ||
       !CL_VRVectorIsFinite(sendcmd.vr_handrot) ||
       !CL_VRVectorIsFinite(sendcmd.vr_roomscalemove))) {
    Con_DPrintf("CL_SendMove: discarded non-finite VR tracking sample\n");
    sendcmd.vr_active = false;
    sendcmd.vr_handpos_relative = false;
    VectorCopy(vec3_origin, sendcmd.vr_handpos);
    VectorCopy(vec3_origin, sendcmd.vr_handrot);
    VectorCopy(vec3_origin, sendcmd.vr_roomscalemove);
  }

  seq = cl.movemessages++;
  sendcmd.sequence = seq;
	cl.net_move_msec_generated += sendcmd.msec;
  cl.movecmds[seq & (CL_MOVE_HISTORY - 1)] = sendcmd;
  cl.cmd = sendcmd;

  if (local_singleplayer) {
    cl.ackedmovemessages = seq;
    if (cl.qcvm.extglobals.servercommandframe)
      *cl.qcvm.extglobals.servercommandframe = cl.ackedmovemessages;
    cl_lagdebug_last_sendmove = 0;
  } else if (seq < 2) {
    //
    // Match QSS-M: keep the first two commands in history so sequence numbers
    // stay coherent, but do not send or predict their movement. They can carry
    // stale input from the previous level.
    //
    cl.movecmds[seq & (CL_MOVE_HISTORY - 1)].seconds = 0;
    CL_WriteAckFrames(&buf);
    CL_AppendVRIKPose(&buf);
    if (buf.cursize) {
      if (NET_SendUnreliableMessage(cls.netcon, &buf) == -1) {
        Con_Printf("CL_SendMove: lost server connection\n");
        CL_Disconnect();
      }
    }
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

  CL_WriteAckFrames(&buf);
  CL_AppendVRIKPose(&buf);

  /* Repeat the last two sequenced commands.  The server discards sequences it
   * already accepted, while a dropped packet no longer loses a one-frame fire
   * press or weapon impulse. */
  first_seq = q_max(2, sendcmd.sequence - 2);
  packet_cmds = 0;
  for (seq = first_seq; seq <= sendcmd.sequence; ++seq) {
    const usercmd_t *packetcmd = &cl.movecmds[seq & (CL_MOVE_HISTORY - 1)];
    if (packetcmd->sequence != seq)
      continue;
    MSG_WriteByte(&buf, clc_move);
    MSG_WriteShort(&buf, packetcmd->sequence & 0xffff);
    CL_WriteUsercmd(&buf, packetcmd);
    packet_cmds++;
  }
  if (buf.overflowed) {
    Con_Printf("CL_SendMove: move packet overflowed\n");
    return;
  }

  if (NET_SendUnreliableMessage(cls.netcon, &buf) == -1) {
    Con_Printf("CL_SendMove: lost server connection\n");
    CL_Disconnect();
    return;
  }

  cl.net_move_packets_sent++;
  cl.net_move_cmds_sent += packet_cmds;
  cl.net_move_last_packet_cmds = packet_cmds;
}

static void CL_VRRelativeMuzzle_f(void) {
  if (cmd_source != src_server)
    return;
  cl.vr_relative_muzzle_supported =
      Cmd_Argc() < 2 || Q_atoi(Cmd_Argv(1)) != 0;
}

/*
============
CL_InitInput
============
*/
void CL_InitInput(void) {
  Cvar_RegisterVariable(&cl_nettest_vr);
  Cvar_RegisterVariable(&cl_nettest_roomscale_x);
  Cvar_RegisterVariable(&cl_nettest_roomscale_y);
  Cvar_RegisterVariable(&cl_nettest_roomscale_z);
  Cvar_RegisterVariable(&cl_nettest_hand_x);
  Cvar_RegisterVariable(&cl_nettest_hand_y);
  Cvar_RegisterVariable(&cl_nettest_hand_z);
  Cmd_AddCommand_ServerCommand("vr_relative_muzzle", CL_VRRelativeMuzzle_f);
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
  Cmd_AddCommand("+button3", IN_Button3Down);
  Cmd_AddCommand("-button3", IN_Button3Up);
  Cmd_AddCommand("+button4", IN_Button4Down);
  Cmd_AddCommand("-button4", IN_Button4Up);
  Cmd_AddCommand("+button5", IN_Button5Down);
  Cmd_AddCommand("-button5", IN_Button5Up);
  Cmd_AddCommand("+button6", IN_Button6Down);
  Cmd_AddCommand("-button6", IN_Button6Up);
  Cmd_AddCommand("+button7", IN_Button7Down);
  Cmd_AddCommand("-button7", IN_Button7Up);
  Cmd_AddCommand("+button8", IN_Button8Down);
  Cmd_AddCommand("-button8", IN_Button8Up);
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
