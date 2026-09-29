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

#include "quakedef.h"
#include "vr_input.h"
#include "vrik_codec.h"
#include "vr_weapon_menu.h"

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

kbutton_t in_mlook = {.state = 1}, in_klook;
kbutton_t in_left, in_right, in_forward, in_back;
kbutton_t in_lookup, in_lookdown, in_moveleft, in_moveright;
kbutton_t in_strafe, in_speed, in_use, in_jump, in_attack;
static kbutton_t in_button4, in_button5, in_button6, in_button7, in_button8;
kbutton_t in_up, in_down;

int in_impulse;
static kbutton_t in_vr_weaponmenu;
static qboolean in_vr_weaponmenu_desktop_capture;

void KeyDown (kbutton_t *b);
void KeyUp (kbutton_t *b);

static void IN_VRWeaponMenuDown (void)
{
	const qboolean already_down = (in_vr_weaponmenu.state & 1) != 0;
	if (!already_down && !VR_WeaponMenu_CanOpen ())
		return;

	KeyDown (&in_vr_weaponmenu);
	if (!already_down && (in_vr_weaponmenu.state & 1))
	{
		/* Desktop uses the absolute cursor; VR keeps mouse capture untouched. */
		in_vr_weaponmenu_desktop_capture = !vulkan_globals.stereo_active;
		if (in_vr_weaponmenu_desktop_capture)
			IN_Deactivate (true);
		VR_WeaponMenu_Open ();
		if (!VR_WeaponMenu_IsOpen ())
		{
			if (in_vr_weaponmenu_desktop_capture)
				IN_Activate ();
			in_vr_weaponmenu_desktop_capture = false;
		}
	}
}

static void IN_VRWeaponMenuUp (void)
{
	int impulse;
	char command[32];
	const qboolean was_open = VR_WeaponMenu_IsOpen ();

	KeyUp (&in_vr_weaponmenu);
	if (in_vr_weaponmenu.state & 1)
		return;

	impulse = was_open ? VR_WeaponMenu_Release () : 0;
	in_vr_weaponmenu.state = 0;
	/* Restore capture after desktop release or cancellation. */
	if (in_vr_weaponmenu_desktop_capture && key_dest == key_game && !con_forcedup)
		IN_Activate ();
	in_vr_weaponmenu_desktop_capture = false;
	if (impulse > 0)
	{
		q_snprintf (command, sizeof (command), "impulse %d\n", impulse);
		Cbuf_AddText (command);
	}
}

static void CL_VRWeaponContactHaptic_f (void)
{
	const char *hand_arg;
	int physical_hand, logical_role;

	if (cmd_source != src_server || Cmd_Argc () != 2 ||
		cls.state != ca_connected || cls.signon != SIGNONS || cls.demoplayback ||
		cl.protocol_qsvr != QSVR_PROTOCOL_PINNED ||
		!cl.vr_weapon_contact_mode)
		return;

	hand_arg = Cmd_Argv (1);
	if (!strcmp (hand_arg, "0"))
		physical_hand = 0;
	else if (!strcmp (hand_arg, "1"))
		physical_hand = 1;
	else
		return;

	logical_role = physical_hand == VR_InputDominantPhysicalHand () ?
		VR_INPUT_ROLE_RIGHT : VR_INPUT_ROLE_LEFT;
	VR_InputTriggerHaptic (logical_role, 0.004f, 1.0f);
}

void KeyDown (kbutton_t *b)
{
	int			k;
	const char *c;

	c = Cmd_Argv (1);
	if (c[0])
		k = atoi (c);
	else
		k = -1; // typed manually at the console for continuous down

	if (k == b->down[0] || k == b->down[1])
		return; // repeating key

	if (!b->down[0])
		b->down[0] = k;
	else if (!b->down[1])
		b->down[1] = k;
	else
	{
		Con_Printf ("Three keys down for a button!\n");
		return;
	}

	if (b->state & 1)
		return;		   // still down
	b->state |= 1 + 2; // down + impulse down
}

void KeyUp (kbutton_t *b)
{
	int			k;
	const char *c;

	c = Cmd_Argv (1);
	if (c[0])
		k = atoi (c);
	else
	{ // typed manually at the console, assume for unsticking, so clear all
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
		return;		// still up (this should not happen)
	b->state &= ~1; // now up
	b->state |= 4;	// impulse up
}

void IN_KLookDown (void)
{
	KeyDown (&in_klook);
}
void IN_KLookUp (void)
{
	KeyUp (&in_klook);
}
void IN_MLookDown (void)
{
	KeyDown (&in_mlook);
}
void IN_MLookUp (void)
{
	KeyUp (&in_mlook);
	if (!(in_mlook.state & 1) && lookspring.value)
		V_StartPitchDrift ();
}
void IN_UpDown (void)
{
	KeyDown (&in_up);
}
void IN_UpUp (void)
{
	KeyUp (&in_up);
}
void IN_DownDown (void)
{
	KeyDown (&in_down);
}
void IN_DownUp (void)
{
	KeyUp (&in_down);
}
void IN_LeftDown (void)
{
	KeyDown (&in_left);
}
void IN_LeftUp (void)
{
	KeyUp (&in_left);
}
void IN_RightDown (void)
{
	KeyDown (&in_right);
}
void IN_RightUp (void)
{
	KeyUp (&in_right);
}
void IN_ForwardDown (void)
{
	KeyDown (&in_forward);
}
void IN_ForwardUp (void)
{
	KeyUp (&in_forward);
}
void IN_BackDown (void)
{
	KeyDown (&in_back);
}
void IN_BackUp (void)
{
	KeyUp (&in_back);
}
void IN_LookupDown (void)
{
	KeyDown (&in_lookup);
}
void IN_LookupUp (void)
{
	KeyUp (&in_lookup);
}
void IN_LookdownDown (void)
{
	KeyDown (&in_lookdown);
}
void IN_LookdownUp (void)
{
	KeyUp (&in_lookdown);
}
void IN_MoveleftDown (void)
{
	KeyDown (&in_moveleft);
}
void IN_MoveleftUp (void)
{
	KeyUp (&in_moveleft);
}
void IN_MoverightDown (void)
{
	KeyDown (&in_moveright);
}
void IN_MoverightUp (void)
{
	KeyUp (&in_moveright);
}

void IN_SpeedDown (void)
{
	KeyDown (&in_speed);
}
void IN_SpeedUp (void)
{
	KeyUp (&in_speed);
}
void IN_StrafeDown (void)
{
	KeyDown (&in_strafe);
}
void IN_StrafeUp (void)
{
	KeyUp (&in_strafe);
}

void IN_AttackDown (void)
{
	KeyDown (&in_attack);
}
void IN_AttackUp (void)
{
	KeyUp (&in_attack);
}

void IN_UseDown (void)
{
	KeyDown (&in_use);
}
void IN_UseUp (void)
{
	KeyUp (&in_use);
}

/* +use and +button3 share the same QuakeC button, as in the inherited
 * input mapping. Keep the later mod buttons in the existing command byte. */
static void IN_Button4Down (void) { KeyDown (&in_button4); }
static void IN_Button4Up (void) { KeyUp (&in_button4); }
static void IN_Button5Down (void) { KeyDown (&in_button5); }
static void IN_Button5Up (void) { KeyUp (&in_button5); }
static void IN_Button6Down (void) { KeyDown (&in_button6); }
static void IN_Button6Up (void) { KeyUp (&in_button6); }
static void IN_Button7Down (void) { KeyDown (&in_button7); }
static void IN_Button7Up (void) { KeyUp (&in_button7); }
static void IN_Button8Down (void) { KeyDown (&in_button8); }
static void IN_Button8Up (void) { KeyUp (&in_button8); }
void IN_JumpDown (void)
{
	KeyDown (&in_jump);
}
void IN_JumpUp (void)
{
	KeyUp (&in_jump);
}

void IN_Impulse (void)
{
	in_impulse = atoi (Cmd_Argv (1));
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
static float CL_KeyStateInternal (kbutton_t *key, qboolean isfinal)
{
	float	 val;
	qboolean impulsedown, impulseup, down;

	impulsedown = key->state & 2;
	impulseup = key->state & 4;
	down = key->state & 1;
	val = 0;

	if (impulsedown && !impulseup)
	{
		if (down)
			val = 0.5; // pressed and held this frame
		else
			val = 0; //	I_Error ();
	}
	if (impulseup && !impulsedown)
	{
		if (down)
			val = 0; //	I_Error ();
		else
			val = 0; // released this frame
	}
	if (!impulsedown && !impulseup)
	{
		if (down)
			val = 1.0; // held the entire frame
		else
			val = 0; // up the entire frame
	}
	if (impulsedown && impulseup)
	{
		if (down)
			val = 0.75; // released and re-pressed this frame
		else
			val = 0.25; // pressed and released this frame
	}

	if (isfinal)
		key->state &= 1; // clear impulses

	return val;
}

float CL_KeyState (kbutton_t *key)
{
	return CL_KeyStateInternal (key, true);
}

//==========================================================================

cvar_t cl_upspeed = {"cl_upspeed", "200", CVAR_NONE};
cvar_t cl_forwardspeed = {"cl_forwardspeed", "200", CVAR_ARCHIVE_GAME};
// Preserve the inherited configuration control and its stock-speed mapping.
cvar_t cl_desktop_vanilla_run = {"cl_desktop_vanilla_run", "1", CVAR_ARCHIVE};
cvar_t cl_backspeed = {"cl_backspeed", "200", CVAR_ARCHIVE_GAME};
cvar_t cl_sidespeed = {"cl_sidespeed", "350", CVAR_NONE};

cvar_t cl_movespeedkey = {"cl_movespeedkey", "2.0", CVAR_NONE};

cvar_t cl_yawspeed = {"cl_yawspeed", "140", CVAR_NONE};
cvar_t cl_pitchspeed = {"cl_pitchspeed", "150", CVAR_NONE};

cvar_t cl_anglespeedkey = {"cl_anglespeedkey", "1.5", CVAR_NONE};

cvar_t cl_alwaysrun = {"cl_alwaysrun", "1", CVAR_ARCHIVE_GAME}; // QuakeSpasm -- new always run

/*
==============
CL_AngleLocked

Returns true if the server sent a fixangle recently
==============
*/
qboolean CL_AngleLocked (void)
{
	return cl.fixangle_time == cl.mtime[0] || cl.fixangle_time == cl.mtime[1];
}

/*
================
CL_AdjustAngles

Moves the local angle positions
================
*/
void CL_AdjustAngles (void)
{
	float speed;
	float up, down;

	if (CL_AngleLocked ())
		return;

	if ((in_speed.state & 1) ^ (cl_alwaysrun.value != 0.0))
		speed = host_frametime * cl_anglespeedkey.value;
	else
		speed = host_frametime;

	if (!(in_strafe.state & 1))
	{
		cl.viewangles[YAW] -= speed * cl_yawspeed.value * CL_KeyState (&in_right);
		cl.viewangles[YAW] += speed * cl_yawspeed.value * CL_KeyState (&in_left);
		cl.viewangles[YAW] = anglemod (cl.viewangles[YAW]);
	}
	if (in_klook.state & 1)
	{
		V_StopPitchDrift ();
		cl.viewangles[PITCH] -= speed * cl_pitchspeed.value * CL_KeyState (&in_forward);
		cl.viewangles[PITCH] += speed * cl_pitchspeed.value * CL_KeyState (&in_back);
	}

	up = CL_KeyState (&in_lookup);
	down = CL_KeyState (&in_lookdown);

	cl.viewangles[PITCH] -= speed * cl_pitchspeed.value * up;
	cl.viewangles[PITCH] += speed * cl_pitchspeed.value * down;

	if (up || down)
		V_StopPitchDrift ();

	// johnfitz -- variable pitch clamping
	if (cl.viewangles[PITCH] > cl_maxpitch.value)
		cl.viewangles[PITCH] = cl_maxpitch.value;
	if (cl.viewangles[PITCH] < cl_minpitch.value)
		cl.viewangles[PITCH] = cl_minpitch.value;
	// johnfitz

	if (cl.viewangles[ROLL] > 50)
		cl.viewangles[ROLL] = 50;
	if (cl.viewangles[ROLL] < -50)
		cl.viewangles[ROLL] = -50;
}

/*
================
CL_BaseMove

Send the intended movement message to the server
================
*/
static void CL_BaseMoveInternal (usercmd_t *cmd, qboolean isfinal)
{
	float forwardspeed = cl_forwardspeed.value, backspeed = cl_backspeed.value;
	memset (cmd, 0, sizeof (*cmd));

	VectorCopy (cl.viewangles, cmd->viewangles);

	if (cls.signon != SIGNONS)
		return;
	if (cl_desktop_vanilla_run.value && !cl_alwaysrun.value && !V_TrackedSessionActive ())
	{
		if (forwardspeed == 200.f)
			forwardspeed *= cl_movespeedkey.value;
		if (backspeed == 200.f)
			backspeed *= cl_movespeedkey.value;
	}

	if (in_strafe.state & 1)
	{
		cmd->sidemove += cl_sidespeed.value * CL_KeyStateInternal (&in_right, isfinal);
		cmd->sidemove -= cl_sidespeed.value * CL_KeyStateInternal (&in_left, isfinal);
	}

	cmd->sidemove += cl_sidespeed.value * CL_KeyStateInternal (&in_moveright, isfinal);
	cmd->sidemove -= cl_sidespeed.value * CL_KeyStateInternal (&in_moveleft, isfinal);

	cmd->upmove += cl_upspeed.value * CL_KeyStateInternal (&in_up, isfinal);
	cmd->upmove -= cl_upspeed.value * CL_KeyStateInternal (&in_down, isfinal);

	if (!(in_klook.state & 1))
	{
		cmd->forwardmove += forwardspeed * CL_KeyStateInternal (&in_forward, isfinal);
		cmd->forwardmove -= backspeed * CL_KeyStateInternal (&in_back, isfinal);
	}

	//
	// adjust for speed key
	//
	if ((in_speed.state & 1) ^ (cl_alwaysrun.value != 0.0))
	{
		cmd->forwardmove *= cl_movespeedkey.value;
		cmd->sidemove *= cl_movespeedkey.value;
		cmd->upmove *= cl_movespeedkey.value;
	}
}

void CL_BaseMove (usercmd_t *cmd)
{
	CL_BaseMoveInternal (cmd, true);
}

static void CL_FinishMoveInternal (usercmd_t *cmd, qboolean isfinal)
{
	unsigned int bits;
	kbutton_t *extra_buttons[] = {&in_button4, &in_button5, &in_button6,
		&in_button7, &in_button8};
	//
	// send button bits
	//
	bits = 0;

	if (in_attack.state & 3)
		bits |= 1;
	if (isfinal)
		in_attack.state &= ~2;

	if (in_jump.state & 3)
		bits |= 2;
	if (isfinal)
		in_jump.state &= ~2;

	if (in_use.state & 3)
		bits |= 4;
	if (isfinal)
		in_use.state &= ~2;
	for (size_t i = 0; i < countof (extra_buttons); ++i)
	{
		if (extra_buttons[i]->state & 3)
			bits |= 1u << (i + 3);
		if (isfinal)
			extra_buttons[i]->state &= ~2;
	}

	bits = VR_InputMergeMeleeAttack (bits, isfinal);
	if (VR_InputSuppressUncalibratedAttack (cmd))
		bits &= ~1u;
	cmd->buttons = bits;
	cmd->impulse = in_impulse;

	if (isfinal)
		in_impulse = 0;
}

void CL_FinishMove (usercmd_t *cmd)
{
	CL_FinishMoveInternal (cmd, true);
}

void CL_PreviewMove (usercmd_t *cmd)
{
	CL_BaseMoveInternal (cmd, false);

	cmd->forwardmove += cl.pendingcmd.forwardmove + cl.pendingcmd.forwardmove_accumulator;
	cmd->sidemove += cl.pendingcmd.sidemove + cl.pendingcmd.sidemove_accumulator;
	cmd->upmove += cl.pendingcmd.upmove + cl.pendingcmd.upmove_accumulator;
	VR_InputApplyPending (cmd);

	CL_FinishMoveInternal (cmd, false);
}

/* Pinned source codec from 1327f795; the admitted caller supplies a prepared command. */
void CL_WritePrivateUsercmd (sizebuf_t *buf, const usercmd_t *cmd,
	unsigned int protocolflags, unsigned int capabilities)
{
	int i;
	int extbits = 0;

	if (cmd->vr_active)
	{
		extbits |= MOVEEXT_VR;
		if (cmd->vr_handpos_relative)
			extbits |= MOVEEXT_VR_RELATIVE;
	}
	if (cmd->vr_akimbo_active && cmd->vr_active && cmd->vr_handpos_relative)
	{
		extbits |= MOVEEXT_VR_AKIMBO;
		if (cmd->vr_akimbo_berserk)
			extbits |= MOVEEXT_VR_AKIMBO_BERSERK;
	}
	if (cmd->weapon || cmd->cursor_screen[0] || cmd->cursor_screen[1] ||
		cmd->cursor_start[0] || cmd->cursor_start[1] || cmd->cursor_start[2] ||
		cmd->cursor_impact[0] || cmd->cursor_impact[1] || cmd->cursor_impact[2] ||
		cmd->cursor_entitynumber)
		extbits |= MOVEEXT_QCINPUT;
	if (cmd->vr_contact.flags && cmd->vr_active && cmd->vr_handpos_relative)
		extbits |= MOVEEXT_VR_CONTACT;
	if (cmd->vr_gorilla_motion.flags && cmd->vr_active &&
		cmd->vr_handpos_relative &&
		(capabilities & QSVR_MOVE_CAP_GORILLA_TRUSTED))
		extbits |= MOVEEXT_GORILLA_TRUSTED;
	else if (cmd->vr_gorilla.flags &&
		(capabilities & QSVR_MOVE_CAP_GORILLA_RAW))
		extbits |= MOVEEXT_VR_GORILLA;

	MSG_WriteFloat (buf, cmd->servertime);
	MSG_WriteByte (buf, cmd->msec);

	for (i = 0; i < 3; i++)
		MSG_WriteAngle16 (buf, cmd->viewangles[i], protocolflags);

	MSG_WriteShort (buf, cmd->forwardmove);
	MSG_WriteShort (buf, cmd->sidemove);
	MSG_WriteShort (buf, cmd->upmove);
	MSG_WriteByte (buf, cmd->buttons);
	MSG_WriteByte (buf, cmd->impulse);
	MSG_WriteByte (buf, extbits);

	if (extbits & MOVEEXT_VR)
	{
		MSG_WriteFloat (buf, cmd->vr_handpos[0]);
		MSG_WriteFloat (buf, cmd->vr_handpos[1]);
		MSG_WriteFloat (buf, cmd->vr_handpos[2]);
		MSG_WriteFloat (buf, cmd->vr_handrot[0]);
		MSG_WriteFloat (buf, cmd->vr_handrot[1]);
		MSG_WriteFloat (buf, cmd->vr_handrot[2]);
		MSG_WriteFloat (buf, cmd->vr_roomscalemove[0]);
		MSG_WriteFloat (buf, cmd->vr_roomscalemove[1]);
		MSG_WriteFloat (buf, cmd->vr_roomscalemove[2]);
	}

	if (extbits & MOVEEXT_VR_AKIMBO)
	{
		for (i = 0; i < 2; i++)
		{
			MSG_WriteFloat (buf, cmd->vr_akimbo_muzzle[i][0]);
			MSG_WriteFloat (buf, cmd->vr_akimbo_muzzle[i][1]);
			MSG_WriteFloat (buf, cmd->vr_akimbo_muzzle[i][2]);
		}
		for (i = 0; i < 2; i++)
		{
			MSG_WriteFloat (buf, cmd->vr_akimbo_angles[i][0]);
			MSG_WriteFloat (buf, cmd->vr_akimbo_angles[i][1]);
			MSG_WriteFloat (buf, cmd->vr_akimbo_angles[i][2]);
		}
	}

	if (extbits & MOVEEXT_QCINPUT)
	{
		MSG_WriteLong (buf, cmd->weapon);
		MSG_WriteShort (buf, cmd->cursor_screen[0] * 32767);
		MSG_WriteShort (buf, cmd->cursor_screen[1] * 32767);
		MSG_WriteFloat (buf, cmd->cursor_start[0]);
		MSG_WriteFloat (buf, cmd->cursor_start[1]);
		MSG_WriteFloat (buf, cmd->cursor_start[2]);
		MSG_WriteFloat (buf, cmd->cursor_impact[0]);
		MSG_WriteFloat (buf, cmd->cursor_impact[1]);
		MSG_WriteFloat (buf, cmd->cursor_impact[2]);
		MSG_WriteEntity (buf, cmd->cursor_entitynumber, QSVR_PEXT2_REQUIRED);
	}

	if (extbits & MOVEEXT_VR_CONTACT)
	{
		MSG_WriteByte (buf, cmd->vr_contact.flags);
		MSG_WriteShort (buf, cmd->vr_contact.modelindex);
		MSG_WriteFloat (buf, cmd->vr_contact.weapon);
		for (i = 0; i < 2; i++)
		{
			if (!(cmd->vr_contact.flags & (1 << i)))
				continue;
			MSG_WriteFloat (buf, cmd->vr_contact.grip[i][0]);
			MSG_WriteFloat (buf, cmd->vr_contact.grip[i][1]);
			MSG_WriteFloat (buf, cmd->vr_contact.grip[i][2]);
			MSG_WriteFloat (buf, cmd->vr_contact.base[i][0]);
			MSG_WriteFloat (buf, cmd->vr_contact.base[i][1]);
			MSG_WriteFloat (buf, cmd->vr_contact.base[i][2]);
			MSG_WriteFloat (buf, cmd->vr_contact.tip[i][0]);
			MSG_WriteFloat (buf, cmd->vr_contact.tip[i][1]);
			MSG_WriteFloat (buf, cmd->vr_contact.tip[i][2]);
			MSG_WriteFloat (buf, cmd->vr_contact.speed[i]);
		}
	}

	if (extbits & MOVEEXT_VR_GORILLA)
	{
		MSG_WriteByte (buf, cmd->vr_gorilla.flags);
		for (i = 0; i < 3; i++)
			MSG_WriteFloat (buf, cmd->vr_gorilla.head[i]);
		for (i = 0; i < 2; i++)
		{
			MSG_WriteFloat (buf, cmd->vr_gorilla.hand[i][0]);
			MSG_WriteFloat (buf, cmd->vr_gorilla.hand[i][1]);
			MSG_WriteFloat (buf, cmd->vr_gorilla.hand[i][2]);
			MSG_WriteFloat (buf, cmd->vr_gorilla.velocity[i][0]);
			MSG_WriteFloat (buf, cmd->vr_gorilla.velocity[i][1]);
			MSG_WriteFloat (buf, cmd->vr_gorilla.velocity[i][2]);
		}
	}
	if (extbits & MOVEEXT_GORILLA_TRUSTED)
	{
		const vr_gorilla_motion_t *motion = &cmd->vr_gorilla_motion;
		int flags = motion->flags;

		if (motion->displacement[0] || motion->displacement[1] ||
			motion->displacement[2])
			flags |= 16;
		if (motion->impulse[0] || motion->impulse[1] || motion->impulse[2])
			flags |= 32;
		MSG_WriteByte (buf, flags);
		MSG_WriteLong (buf, motion->generation);
		for (i = 0; i < 2; ++i)
		{
			MSG_WriteShort (buf, motion->contact[i] + 1);
			MSG_WriteShort (buf, motion->contact_model[i]);
		}
		if (flags & 16)
			for (i = 0; i < 3; ++i)
				MSG_WriteFloat (buf, motion->displacement[i]);
		if (flags & 32)
			for (i = 0; i < 3; ++i)
				MSG_WriteFloat (buf, motion->impulse[i]);
	}
}

/* Pinned source ACK drain and paced command sender (1327f795).
 * The runtime/weapon owner supplies the prepared command; this code owns only
 * its duration, history and transport. Private admission is still separate. */
static void CL_WriteAckFrames (sizebuf_t *buf)
{
	unsigned int i, count = 0;
	while (count < cl.ackframes_count && buf->cursize + 5 <= buf->maxsize)
	{
		MSG_WriteByte (buf, clcdp_ackframe);
		MSG_WriteLong (buf, cl.ackframes[count]);
		cl.net_snapshot_acks_sent++;
		count++;
	}
	for (i = count; i < cl.ackframes_count; i++)
		cl.ackframes[i - count] = cl.ackframes[i];
	cl.ackframes_count -= count;
}

void CL_FlushAckFrames (void)
{
	byte data[DATAGRAM_MTU];
	sizebuf_t buf = {0};
	if (cl.protocol_qsvr != QSVR_PROTOCOL_PINNED || !cl.ackframes_count || cls.demoplayback || !cls.netcon)
		return;
	buf.data = data;
	buf.maxsize = sizeof data;
	CL_WriteAckFrames (&buf);
	if (buf.cursize && NET_SendUnreliableMessage (cls.netcon, &buf) == -1)
	{
		Con_Printf ("CL_FlushAckFrames: lost server connection\n");
		CL_Disconnect ();
	}
}

static unsigned char CL_SampleMoveMsec (void)
{
	double elapsed, milliseconds;
	int msec;
	if (!cl.move_msec_sample_valid)
	{
		cl.move_msec_sample_valid = true;
		cl.move_msec_sample_time = realtime;
		cl.move_msec_fractional_carry = 0;
		elapsed = host_frametime;
	}
	else
	{
		elapsed = realtime - cl.move_msec_sample_time;
		cl.move_msec_sample_time = realtime;
	}
	if (elapsed < 0)
	{
		elapsed = 0;
		cl.move_msec_fractional_carry = 0;
	}
	milliseconds = elapsed * 1000.0 + cl.move_msec_fractional_carry;
	// Clamp before converting: a long suspend can exceed the range of int.
	if (!isfinite (milliseconds) || milliseconds >= 126)
	{
		cl.move_msec_fractional_carry = 0;
		return 125;
	}
	msec = (int)milliseconds;
	if (msec < 1)
		msec = 1;
	cl.move_msec_fractional_carry = milliseconds - msec;
	return (unsigned char)msec;
}

/* VRIK is a newest-pose message, independent of QSVR movement admission and
 * command history. Encode and reserve the whole framed body before touching
 * the packet or advancing sender state. */
static void CL_AppendVRIKPose (sizebuf_t *buf)
{
	vrik_codec_pose_t pose;
	vrik_v2_pose_t pose_v2;
	uint8_t encoded[VRIK_V3_MAX_BODY_BYTES];
	size_t encoded_bytes = 0, available;
	qboolean v3, active;
	uint16_t sequence;
	int start;

	if (!buf || !buf->data || !cl.vrik_protocol_offered || !cl.vrik_cap_sent ||
		(cls.state != ca_connected) || cls.demoplayback || cls.signon != SIGNONS ||
		realtime < cl.vrik_next_send_time ||
		(cl.vrik_protocol_version != VRIK_PROTOCOL_LEGACY_VERSION &&
		 cl.vrik_protocol_version != VRIK_PROTOCOL_VERSION &&
		 cl.vrik_protocol_version != VRIK_ADMISSION_PROTOCOL_VERSION))
		return;
	if (!VR_InputBuildVRIKPose (&pose))
	{
		if (!cl.vrik_last_sent_active)
			return;
		/* Publish one canonical clear after tracking or its context is lost. */
		memset (&pose, 0, sizeof (pose));
	}
	active = (pose.flags & VRIK_V3_FLAG_ACTIVE) != 0;
	if (!active && !cl.vrik_last_sent_active)
		return;

	sequence = cl.vrik_next_sequence;
	pose.sequence = sequence;
	if (vrik_normalized_to_v2 (&pose, &pose_v2) != VRIK_CODEC_OK ||
		vrik_v2_validate_legacy_pose (&pose_v2) != VRIK_CODEC_OK)
		return;

	v3 = cl.vrik_protocol_version >= VRIK_PROTOCOL_VERSION;
	if (v3)
	{
		if (vrik_v3_encode (&pose, encoded, sizeof (encoded), &encoded_bytes) != VRIK_CODEC_OK ||
			encoded_bytes > VRIK_V3_MAX_BODY_BYTES)
			return;
		available = 2 + encoded_bytes;
	}
	else
	{
		if (vrik_v2_encode (&pose_v2, encoded, sizeof (encoded), &encoded_bytes) != VRIK_CODEC_OK ||
			encoded_bytes != VRIK_V2_BODY_BYTES)
			return;
		available = 1 + encoded_bytes;
	}

	if (buf->cursize < 0 || buf->maxsize < 0 || buf->cursize > buf->maxsize ||
		available > (size_t)(buf->maxsize - buf->cursize))
		return;

	start = buf->cursize;
	buf->data[buf->cursize++] = clc_vrikpose;
	if (v3)
		buf->data[buf->cursize++] = (byte)encoded_bytes;
	memcpy (buf->data + buf->cursize, encoded, encoded_bytes);
	buf->cursize += (int)encoded_bytes;
	if ((size_t)(buf->cursize - start) != available)
	{
		buf->cursize = start;
		return;
	}

	cl.vrik_next_sequence++;
	cl.vrik_next_send_time = realtime + 0.05;
	cl.vrik_last_sent_active = active;
}

static qboolean CL_AppendVoicePacket (sizebuf_t *buf)
{
	voice_packet_t *packet;
	size_t required;
	int start;

	if (!buf || !buf->data || !CL_VoiceTransportAvailable () ||
		!cl.voice_outgoing_count || cls.state != ca_connected ||
		cls.demoplayback || cls.signon != SIGNONS)
		return false;
	packet = &cl.voice_outgoing[cl.voice_outgoing_head];
	if (!Voice_PacketIsValid (packet))
		return false;
	required = VOICE_CLC_HEADER_BYTES + packet->payload_bytes;
	if (required > VOICE_CLIENT_DATAGRAM_BUDGET || buf->overflowed ||
		buf->cursize < 0 || buf->maxsize < 0 || buf->cursize > buf->maxsize ||
		required > (size_t)(buf->maxsize - buf->cursize))
		return false;

	start = buf->cursize;
	MSG_WriteByte (buf, clc_voice);
	MSG_WriteShort (buf, packet->sequence);
	MSG_WriteLong (buf, (int)packet->timestamp);
	MSG_WriteByte (buf, packet->talkspurt);
	MSG_WriteByte (buf, packet->flags);
	MSG_WriteShort (buf, packet->payload_bytes);
	if (packet->payload_bytes)
		SZ_Write (buf, packet->payload, packet->payload_bytes);
	if (buf->overflowed)
	{
		buf->cursize = start;
		return false;
	}
	return true;
}

static void CL_ConsumeSentVoicePacket (void)
{
	voice_packet_t *packet;

	if (!cl.voice_outgoing_count)
		return;
	packet = &cl.voice_outgoing[cl.voice_outgoing_head];
	memset (packet, 0, sizeof (*packet));
	cl.voice_outgoing_head = (cl.voice_outgoing_head + 1) %
		VOICE_CLIENT_QUEUE_CAPACITY;
	cl.voice_outgoing_count--;
}

/* Called when a selected server's new resume epoch is parsed, before the
 * next host-side command is built. Preserve held keys, but drop edges and
 * tracking accumulated before the client knew the server had resumed. */
void CL_PrivateMoveResumeObserved (void)
{
	kbutton_t *buttons[] = {
		&in_mlook, &in_klook,
		&in_left, &in_right, &in_forward, &in_back,
		&in_lookup, &in_lookdown, &in_moveleft, &in_moveright,
		&in_strafe, &in_speed, &in_use, &in_jump, &in_attack,
		&in_up, &in_down, &in_button4, &in_button5,
		&in_button6, &in_button7, &in_button8
	};
	for (size_t i = 0; i < countof (buttons); ++i)
		buttons[i]->state &= 1;
	in_impulse = 0;
	VR_InputResetMotionContinuity ();
	memset (&cl.pendingcmd, 0, sizeof (cl.pendingcmd));
	cl.pendingcmd.servertime = cl.time;
	cl.move_msec_sample_valid = true;
	cl.move_msec_sample_time = realtime;
	cl.move_msec_fractional_carry = 0;
}

static qboolean CL_QueuePrivateResumeMarker (void)
{
	char command[64];
	int length;
	size_t required;
	/* Remote startup reserves sequences 0/1. A pause before their sampling
	 * must fence at the first command the server can actually consume. */
	const int first_sequence = q_max (2, cl.movemessages);

	length = q_snprintf (command, sizeof (command), "qsvr_resume %u %d",
		(unsigned)cl.move_ack_discontinuity_epoch, first_sequence);
	if (length < 0 || length >= (int)sizeof (command))
		return false;
	required = 1 + (size_t)length + 1;
	if (!cls.message.overflowed && cls.message.cursize >= 0 &&
		cls.message.maxsize >= 0 &&
		cls.message.cursize <= cls.message.maxsize &&
		required <= (size_t)(cls.message.maxsize - cls.message.cursize))
	{
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, command);
	}
	cl.move_resume_marker_epoch_sent = cl.move_ack_discontinuity_epoch;
	cl.move_resume_marker_first_sequence = first_sequence;
	cl.move_resume_marker_epoch_valid = true;
	return true;
}

static void CL_SendPrivateMove (const usercmd_t *cmd)
{
	byte data[DATAGRAM_MTU];
	sizebuf_t buf = {0};
	usercmd_t sendcmd;
	qboolean voice_appended;
	int send_result;
	int seq, first_seq, packet_cmds = 0;
	unsigned capabilities = 0;
	if (cls.demoplayback)
		return;
	if (cl.paused)
	{
		/* Paused time is not command duration to replay on resume. */
		cl.move_msec_sample_valid = true;
		cl.move_msec_sample_time = realtime;
		cl.move_msec_fractional_carry = 0;
		CL_FlushAckFrames ();
		return;
	}
	if (cl.protocol_qsvr == QSVR_PROTOCOL_PINNED &&
		cl.move_ack_selected_owner &&
		cl.move_ack_resume_pending &&
		(!cl.move_resume_marker_epoch_valid ||
			(cl.move_resume_marker_first_sequence > 0 &&
			 cl.move_resume_marker_epoch_sent != cl.move_ack_discontinuity_epoch)))
	{
		/* The parser cleared older latches at the observed epoch. This marker
		 * also rides before moves in the current datagram, so reliable-channel
		 * backpressure cannot stall resumed input. */
		if (!CL_QueuePrivateResumeMarker ())
		{
			CL_FlushAckFrames ();
			return;
		}
	}
	/* A full reliable buffer at offer time defers the capability reply until
	 * the next command, without making the unreliable pose claim admission. */
	CL_QueueGorillaCapability ();
	CL_QueueInstantStopCapability ();
	if (!cmd)
	{
		CL_FlushAckFrames ();
		return;
	}
	buf.data = data;
	buf.maxsize = sizeof data;
	sendcmd = *cmd;
	if (sendcmd.servertime <= 0)
		sendcmd.servertime = cl.time;
	sendcmd.msec = CL_SampleMoveMsec ();
	sendcmd.seconds = sendcmd.msec * 0.001f;
	seq = cl.movemessages;
	sendcmd.sequence = seq;
	VR_InputCommitGorillaCommand (&sendcmd);
	cl.movemessages++;
	cl.net_move_msec_generated += sendcmd.msec;
	cl.movecmds[seq & MOVECMDS_MASK] = sendcmd;
	cl.cmd = sendcmd;

	if (seq < 2)
	{
		cl.movecmds[seq & MOVECMDS_MASK].seconds = 0;
		CL_FlushAckFrames ();
		CL_AppendVRIKPose (&buf);
		voice_appended = CL_AppendVoicePacket (&buf);
		send_result = buf.cursize ? NET_SendUnreliableMessage (cls.netcon, &buf) : 0;
		if (send_result < 0)
		{
			Con_Printf ("CL_SendMove: lost server connection\n");
			CL_Disconnect ();
		}
		else if (send_result == 1 && voice_appended)
			CL_ConsumeSentVoicePacket ();
		return;
	}
	if (cl.vr_gorilla_supported && cl.vr_gorilla_allowed)
		capabilities |= QSVR_MOVE_CAP_GORILLA_RAW;
	if (cl.vr_gorilla_trusted_supported && cl.vr_gorilla_trusted_cap_sent)
		capabilities |= QSVR_MOVE_CAP_GORILLA_TRUSTED;
	if (cl.move_ack_selected_owner && cl.move_resume_marker_epoch_valid &&
		cl.move_resume_marker_epoch_sent == cl.move_ack_discontinuity_epoch &&
		cl.move_resume_marker_first_sequence > 0 &&
		cl.ackedmovemessages < cl.move_resume_marker_first_sequence)
	{
		/* Put the marker before moves in the same datagram. Repeating it
		 * until completion avoids losing early resumed actions while an older
		 * reliable message delays the separately queued marker. */
		MSG_WriteByte (&buf, clc_stringcmd);
		MSG_WriteString (&buf, va ("qsvr_resume %u %d",
			(unsigned)cl.move_resume_marker_epoch_sent,
			cl.move_resume_marker_first_sequence));
	}

	first_seq = q_max (2, seq - 2);
	for (int previous = first_seq; previous <= seq; ++previous)
	{
		const usercmd_t *packetcmd = &cl.movecmds[previous & MOVECMDS_MASK];
		if (packetcmd->sequence != (unsigned)previous)
			continue;
		MSG_WriteByte (&buf, clc_move);
		MSG_WriteShort (&buf, packetcmd->sequence & 0xffff);
		CL_WritePrivateUsercmd (&buf, packetcmd, cl.protocolflags, capabilities);
		packet_cmds++;
	}
	// Movement precedes transport ACKs; retain any ACKs that do not fit.
	CL_WriteAckFrames (&buf);
	CL_AppendVRIKPose (&buf);
	voice_appended = CL_AppendVoicePacket (&buf);
	if (!buf.cursize)
		return;
	send_result = NET_SendUnreliableMessage (cls.netcon, &buf);
	if (send_result < 0)
	{
		Con_Printf ("CL_SendMove: lost server connection\n");
		CL_Disconnect ();
		return;
	}
	if (send_result == 1 && voice_appended)
		CL_ConsumeSentVoicePacket ();
	cl.net_move_packets_sent++;
	cl.net_move_cmds_sent += packet_cmds;
	cl.net_move_last_packet_cmds = packet_cmds;
}

/*
==============
CL_SendMove
==============
*/
void CL_SendMove (const usercmd_t *cmd)
{
	if (cl.protocol_qsvr == QSVR_PROTOCOL_PINNED)
	{
		CL_SendPrivateMove (cmd);
		return;
	}
	unsigned int i;
	qboolean voice_appended;
	int send_result;
	sizebuf_t	 buf = {0};
	byte		 data[1024];

	buf.maxsize = sizeof (data);
	buf.cursize = 0;
	buf.data = data;
	buf.allowoverflow = false;

	for (i = 0; i < cl.ackframes_count; i++)
	{
		MSG_WriteByte (&buf, clcdp_ackframe);
		MSG_WriteLong (&buf, cl.ackframes[i]);
	}
	cl.ackframes_count = 0;

	if (cmd)
	{
		int			 dump = buf.cursize;
		unsigned int bits = cmd->buttons;

		//
		// send the movement message
		//
		MSG_WriteByte (&buf, clc_move);

		if (cl.protocol_pext2 & PEXT2_PREDINFO)
		{
			MSG_WriteShort (&buf, cl.movemessages & 0xffff); // server will ack this once it has been applied to the player's entity state
			MSG_WriteFloat (&buf, cmd->servertime);			 // so server can get cmd timing (pings will be calculated by entframe acks).
		}
		else
			MSG_WriteFloat (&buf, cl.mtime[0]); // so server can get ping times

		for (i = 0; i < 3; i++)
			// johnfitz -- 16-bit angles for PROTOCOL_FITZQUAKE
			// spike -- nq+bjp3 use 8bit angles. all other supported protocols use 16bit ones.
			// spike -- proquake servers bump client->server angles up to at least 16bit. this is safe because it only happens when both client+server advertise
			// it, and because it never actually gets recorded into demos anyway. spike -- predinfo also always means 16bit angles, even if for some reason the
			// server doesn't advertise proquake (like dp).
			if (cl.protocol == PROTOCOL_NETQUAKE && !NET_QSocketGetProQuakeAngleHack (cls.netcon) && !(cl.protocol_pext2 & PEXT2_PREDINFO))
				MSG_WriteAngle (&buf, cmd->viewangles[i], cl.protocolflags);
			else
				MSG_WriteAngle16 (&buf, cmd->viewangles[i], cl.protocolflags);
		// johnfitz

		MSG_WriteShort (&buf, cmd->forwardmove);
		MSG_WriteShort (&buf, cmd->sidemove);
		MSG_WriteShort (&buf, cmd->upmove);

		MSG_WriteByte (&buf, bits & 0xff);
		MSG_WriteByte (&buf, cmd->impulse & 0xff);
		if (bits & (1u << 30))
			MSG_WriteLong (&buf, cmd->weapon);
		in_impulse = 0;

		cl.movecmds[cl.movemessages & MOVECMDS_MASK] = *cmd;

		//
		// allways dump the first two message, because it may contain leftover inputs
		// from the last level
		//
		if (++cl.movemessages <= 2)
			buf.cursize = dump;
	}
	CL_AppendVRIKPose (&buf);
	voice_appended = CL_AppendVoicePacket (&buf);

	// fixme: nops if we're still connecting, or something.

	//
	// deliver the message
	//
	if (cls.demoplayback || !buf.cursize)
		return;

	send_result = NET_SendUnreliableMessage (cls.netcon, &buf);
	if (send_result < 0)
	{
		Con_Printf ("CL_SendMove: lost server connection\n");
		CL_Disconnect ();
		return;
	}
	if (send_result == 1 && voice_appended)
		CL_ConsumeSentVoicePacket ();
}

/*
============
CL_InitInput
============
*/
void CL_InitInput (void)
{
	Cmd_AddCommand_ServerCommand ("vr_weapon_contact_haptic",
		CL_VRWeaponContactHaptic_f);
	Cmd_AddCommand ("+vr_weaponmenu", IN_VRWeaponMenuDown);
	Cmd_AddCommand ("-vr_weaponmenu", IN_VRWeaponMenuUp);
	Cmd_AddCommand ("+moveup", IN_UpDown);
	Cmd_AddCommand ("-moveup", IN_UpUp);
	Cmd_AddCommand ("+movedown", IN_DownDown);
	Cmd_AddCommand ("-movedown", IN_DownUp);
	Cmd_AddCommand ("+left", IN_LeftDown);
	Cmd_AddCommand ("-left", IN_LeftUp);
	Cmd_AddCommand ("+right", IN_RightDown);
	Cmd_AddCommand ("-right", IN_RightUp);
	Cmd_AddCommand ("+forward", IN_ForwardDown);
	Cmd_AddCommand ("-forward", IN_ForwardUp);
	Cmd_AddCommand ("+back", IN_BackDown);
	Cmd_AddCommand ("-back", IN_BackUp);
	Cmd_AddCommand ("+lookup", IN_LookupDown);
	Cmd_AddCommand ("-lookup", IN_LookupUp);
	Cmd_AddCommand ("+lookdown", IN_LookdownDown);
	Cmd_AddCommand ("-lookdown", IN_LookdownUp);
	Cmd_AddCommand ("+strafe", IN_StrafeDown);
	Cmd_AddCommand ("-strafe", IN_StrafeUp);
	Cmd_AddCommand ("+moveleft", IN_MoveleftDown);
	Cmd_AddCommand ("-moveleft", IN_MoveleftUp);
	Cmd_AddCommand ("+moveright", IN_MoverightDown);
	Cmd_AddCommand ("-moveright", IN_MoverightUp);
	Cmd_AddCommand ("+speed", IN_SpeedDown);
	Cmd_AddCommand ("-speed", IN_SpeedUp);
	Cmd_AddCommand ("+attack", IN_AttackDown);
	Cmd_AddCommand ("-attack", IN_AttackUp);
	Cmd_AddCommand ("+use", IN_UseDown);
	Cmd_AddCommand ("-use", IN_UseUp);
	Cmd_AddCommand ("+button3", IN_UseDown);
	Cmd_AddCommand ("-button3", IN_UseUp);
	Cmd_AddCommand ("+button4", IN_Button4Down);
	Cmd_AddCommand ("-button4", IN_Button4Up);
	Cmd_AddCommand ("+button5", IN_Button5Down);
	Cmd_AddCommand ("-button5", IN_Button5Up);
	Cmd_AddCommand ("+button6", IN_Button6Down);
	Cmd_AddCommand ("-button6", IN_Button6Up);
	Cmd_AddCommand ("+button7", IN_Button7Down);
	Cmd_AddCommand ("-button7", IN_Button7Up);
	Cmd_AddCommand ("+button8", IN_Button8Down);
	Cmd_AddCommand ("-button8", IN_Button8Up);
	Cmd_AddCommand ("+jump", IN_JumpDown);
	Cmd_AddCommand ("-jump", IN_JumpUp);
	Cmd_AddCommand ("impulse", IN_Impulse);
	Cmd_AddCommand ("+klook", IN_KLookDown);
	Cmd_AddCommand ("-klook", IN_KLookUp);
	Cmd_AddCommand ("+mlook", IN_MLookDown);
	Cmd_AddCommand ("-mlook", IN_MLookUp);
}
