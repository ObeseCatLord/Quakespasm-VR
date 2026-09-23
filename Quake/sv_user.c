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

edict_t *sv_player;

extern cvar_t sv_friction;
cvar_t		  sv_edgefriction = {"edgefriction", "2", CVAR_NONE};
extern cvar_t sv_stopspeed;

static vec3_t forward, right, up;

// world
static float *angles;
static float *origin;
static float *velocity;

static qboolean onground;

static usercmd_t cmd;

cvar_t sv_idealpitchscale = {"sv_idealpitchscale", "0.8", CVAR_NONE};
cvar_t sv_altnoclip = {"sv_altnoclip", "1", CVAR_ARCHIVE_GAME}; // johnfitz

/*
===============
SV_SetIdealPitch
===============
*/
#define MAX_FORWARD 6
void SV_SetIdealPitch (void)
{
	float	angleval, sinval, cosval;
	trace_t tr;
	vec3_t	top, bottom;
	float	z[MAX_FORWARD];
	int		i, j;
	int		step, dir, steps;

	if (!((int)sv_player->v.flags & FL_ONGROUND))
		return;

	angleval = sv_player->v.angles[YAW] * M_PI * 2 / 360;
	sinval = sin (angleval);
	cosval = cos (angleval);

	for (i = 0; i < MAX_FORWARD; i++)
	{
		top[0] = sv_player->v.origin[0] + cosval * (i + 3) * 12;
		top[1] = sv_player->v.origin[1] + sinval * (i + 3) * 12;
		top[2] = sv_player->v.origin[2] + sv_player->v.view_ofs[2];

		bottom[0] = top[0];
		bottom[1] = top[1];
		bottom[2] = top[2] - 160;

		tr = SV_Move (top, vec3_origin, vec3_origin, bottom, 1, sv_player);
		if (tr.allsolid)
			return; // looking at a wall, leave ideal the way is was

		if (tr.fraction == 1)
			return; // near a dropoff

		z[i] = top[2] + tr.fraction * (bottom[2] - top[2]);
	}

	dir = 0;
	steps = 0;
	for (j = 1; j < i; j++)
	{
		step = z[j] - z[j - 1];
		if (step > -ON_EPSILON && step < ON_EPSILON)
			continue;

		if (dir && (step - dir > ON_EPSILON || step - dir < -ON_EPSILON))
			return; // mixed changes

		steps++;
		dir = step;
	}

	if (!dir)
	{
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
void SV_UserFriction (void)
{
	float  *vel;
	float	speed, newspeed, control;
	vec3_t	start, stop;
	float	friction;
	trace_t trace;

	vel = velocity;

	speed = sqrt (vel[0] * vel[0] + vel[1] * vel[1]);
	if (!speed)
		return;

	// if the leading edge is over a dropoff, increase friction
	start[0] = stop[0] = origin[0] + vel[0] / speed * 16;
	start[1] = stop[1] = origin[1] + vel[1] / speed * 16;
	start[2] = origin[2] + sv_player->v.mins[2];
	stop[2] = start[2] - 34;

	trace = SV_Move (start, vec3_origin, vec3_origin, stop, true, sv_player);

	if (trace.fraction == 1.0)
		friction = sv_friction.value * sv_edgefriction.value;
	else
		friction = sv_friction.value;

	// apply friction, matching the canonical 72Hz decay for any frame duration:
	// exponential while speed is above stopspeed, then linear below it
	{
		extern qboolean sv_analyticphysics_frame;

		const double tau = 1.0 / MAX_PHYSICS_FREQ;
		double		 s = host_frametime / tau;
		double		 r = 1.0 - friction * tau;
		double		 ns = speed;

		if (!sv_analyticphysics_frame || r <= 0)
		{
			// classic frame-dependent formula; also for degenerate friction values that stop within one tick
			control = speed < sv_stopspeed.value ? sv_stopspeed.value : speed;
			ns = speed - host_frametime * control * friction;
		}
		else
		{
			if (ns >= sv_stopspeed.value)
			{
				double k_cross = log (sv_stopspeed.value / ns) / log (r);
				if (s <= k_cross)
				{
					ns *= pow (r, s);
					s = 0;
				}
				else
				{
					ns = sv_stopspeed.value;
					s -= k_cross;
				}
			}
			ns -= s * tau * friction * sv_stopspeed.value;
		}
		newspeed = (float)ns;
	}

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
void   SV_Accelerate (float wishspeed, const vec3_t wishdir)
{
	int	  i;
	float addspeed, accelspeed, currentspeed;

	currentspeed = DotProduct (velocity, wishdir);
	addspeed = wishspeed - currentspeed;
	if (addspeed <= 0)
		return;
	accelspeed = sv_accelerate.value * host_frametime * wishspeed;
	if (accelspeed > addspeed)
		accelspeed = addspeed;

	for (i = 0; i < 3; i++)
		velocity[i] += accelspeed * wishdir[i];
}

void SV_AirAccelerate (float wishspeed, vec3_t wishveloc)
{
	int	  i;
	float addspeed, wishspd, accelspeed, currentspeed;

	wishspd = VectorNormalize (wishveloc);
	if (wishspd > 30)
		wishspd = 30;
	currentspeed = DotProduct (velocity, wishveloc);
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

void DropPunchAngle (void)
{
	float len;

	len = VectorNormalize (sv_player->v.punchangle);

	len -= 10 * host_frametime;
	if (len < 0)
		len = 0;
	VectorScale (sv_player->v.punchangle, len, sv_player->v.punchangle);
}

/*
===================
SV_WaterMove

===================
*/
void SV_WaterMove (void)
{
	int	   i;
	vec3_t wishvel;
	float  speed, newspeed, wishspeed, addspeed, accelspeed;

	//
	// user intentions
	//
	AngleVectors (sv_player->v.v_angle, forward, right, up);

	for (i = 0; i < 3; i++)
		wishvel[i] = forward[i] * cmd.forwardmove + right[i] * cmd.sidemove;

	if (!cmd.forwardmove && !cmd.sidemove && !cmd.upmove)
		wishvel[2] -= 60; // drift towards bottom
	else
		wishvel[2] += cmd.upmove;

	wishspeed = VectorLength (wishvel);
	if (wishspeed > sv_maxspeed.value)
	{
		VectorScale (wishvel, sv_maxspeed.value / wishspeed, wishvel);
		wishspeed = sv_maxspeed.value;
	}
	wishspeed *= 0.7;

	//
	// water friction, matching the canonical 72Hz decay for any frame duration
	//
	speed = VectorLength (velocity);
	if (speed)
	{
		extern qboolean sv_analyticphysics_frame;

		const double tau = 1.0 / MAX_PHYSICS_FREQ;
		double		 r = 1.0 - sv_friction.value * tau;
		if (!sv_analyticphysics_frame || r <= 0)
			newspeed = speed - host_frametime * speed * sv_friction.value;
		else
			newspeed = speed * pow (r, host_frametime / tau);
		if (newspeed < 0)
			newspeed = 0;
		VectorScale (velocity, newspeed / speed, velocity);
	}
	else
		newspeed = 0;

	//
	// water acceleration
	//
	if (!wishspeed)
		return;

	addspeed = wishspeed - newspeed;
	if (addspeed <= 0)
		return;

	VectorNormalize (wishvel);
	accelspeed = sv_accelerate.value * wishspeed * host_frametime;
	if (accelspeed > addspeed)
		accelspeed = addspeed;

	for (i = 0; i < 3; i++)
		velocity[i] += accelspeed * wishvel[i];
}

void SV_WaterJump (void)
{
	if (qcvm->time > sv_player->v.teleport_time || !sv_player->v.waterlevel)
	{
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
void SV_NoclipMove (void)
{
	AngleVectors (sv_player->v.v_angle, forward, right, up);

	velocity[0] = forward[0] * cmd.forwardmove + right[0] * cmd.sidemove;
	velocity[1] = forward[1] * cmd.forwardmove + right[1] * cmd.sidemove;
	velocity[2] = forward[2] * cmd.forwardmove + right[2] * cmd.sidemove;
	velocity[2] += cmd.upmove * 2; // doubled to match running speed

	if (VectorLength (velocity) > sv_maxspeed.value)
	{
		VectorNormalize (velocity);
		VectorScale (velocity, sv_maxspeed.value, velocity);
	}
}

/*
===================
SV_AirMove
===================
*/
void SV_AirMove (void)
{
	int	   i;
	vec3_t wishvel, wishdir;
	float  wishspeed;
	float  fmove, smove;

	AngleVectors (sv_player->v.angles, forward, right, up);

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

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize (wishdir);
	if (wishspeed > sv_maxspeed.value)
	{
		VectorScale (wishvel, sv_maxspeed.value / wishspeed, wishvel);
		wishspeed = sv_maxspeed.value;
	}

	if (sv_player->v.movetype == MOVETYPE_NOCLIP)
	{ // noclip
		VectorCopy (wishvel, velocity);
	}
	else if (onground)
	{
		SV_UserFriction ();
		SV_Accelerate (wishspeed, wishdir);
	}
	else
	{ // not on ground, so little effect on velocity
		SV_AirAccelerate (wishspeed, wishvel);
	}
}

/*
===================
SV_ClientThink

the move fields specify an intended velocity in pix/sec
the angle fields specify an exact angular motion in degrees
===================
*/
void SV_ClientThink (void)
{
	vec3_t v_angle;

	if (sv_player->v.movetype == MOVETYPE_NONE)
		return;

	onground = (int)sv_player->v.flags & FL_ONGROUND;

	origin = sv_player->v.origin;
	velocity = sv_player->v.velocity;

	DropPunchAngle ();

	//
	// if dead, behave differently
	//
	if (sv_player->v.health <= 0)
		return;

	//
	// angles
	// show 1/3 the pitch angle and all the roll angle
	cmd = host_client->cmd;
	angles = sv_player->v.angles;

	VectorAdd (sv_player->v.v_angle, sv_player->v.punchangle, v_angle);
	angles[ROLL] = V_CalcRoll (sv_player->v.angles, sv_player->v.velocity) * 4;
	if (!sv_player->v.fixangle)
	{
		angles[PITCH] = -v_angle[PITCH] / 3;
		angles[YAW] = v_angle[YAW];
	}

	if ((int)sv_player->v.flags & FL_WATERJUMP)
	{
		SV_WaterJump ();
		return;
	}
	//
	// walk
	//
	// johnfitz -- alternate noclip
	if (sv_player->v.movetype == MOVETYPE_NOCLIP && sv_altnoclip.value)
		SV_NoclipMove ();
	else if (sv_player->v.waterlevel >= 2 && sv_player->v.movetype != MOVETYPE_NOCLIP)
		SV_WaterMove ();
	else
		SV_AirMove ();
	// johnfitz
}

/* Pinned source codec from 1327f795; admission and receipt ownership stay with the caller. */
qboolean SV_ReadPrivateUsercmd (usercmd_t *readcmd, unsigned int sequence,
	unsigned int protocolflags, unsigned int capabilities)
{
	int i;
	int extbits;

	memset (readcmd, 0, sizeof (*readcmd));
	readcmd->sequence = sequence;
	readcmd->servertime = MSG_ReadFloat ();
	if (!isfinite (readcmd->servertime))
	{
		msg_badread = true;
		return false;
	}
	readcmd->msec = MSG_ReadByte ();
	if (readcmd->msec < 1 || readcmd->msec > 125)
	{
		msg_badread = true;
		return false;
	}

	for (i = 0; i < 3; i++)
	{
		readcmd->viewangles[i] = MSG_ReadAngle16 (protocolflags);
		if (!isfinite (readcmd->viewangles[i]))
		{
			msg_badread = true;
			return false;
		}
	}

	readcmd->forwardmove = MSG_ReadShort ();
	readcmd->sidemove = MSG_ReadShort ();
	readcmd->upmove = MSG_ReadShort ();
	readcmd->buttons = MSG_ReadByte ();
	readcmd->impulse = MSG_ReadByte ();

	extbits = MSG_ReadByte ();
	if (extbits & ~(MOVEEXT_VR | MOVEEXT_VR_RELATIVE | MOVEEXT_QCINPUT |
		MOVEEXT_VR_AKIMBO | MOVEEXT_VR_AKIMBO_BERSERK |
		MOVEEXT_VR_CONTACT | MOVEEXT_VR_GORILLA | MOVEEXT_GORILLA_TRUSTED))
	{
		msg_badread = true;
		return false;
	}

	if ((extbits & MOVEEXT_VR_RELATIVE) && !(extbits & MOVEEXT_VR))
	{
		msg_badread = true;
		return false;
	}
	if ((extbits & MOVEEXT_VR_AKIMBO_BERSERK) && !(extbits & MOVEEXT_VR_AKIMBO))
	{
		msg_badread = true;
		return false;
	}
	if ((extbits & MOVEEXT_VR_AKIMBO) != 0 &&
		(extbits & (MOVEEXT_VR | MOVEEXT_VR_RELATIVE)) !=
			(MOVEEXT_VR | MOVEEXT_VR_RELATIVE))
	{
		msg_badread = true;
		return false;
	}
	if ((extbits & MOVEEXT_VR_CONTACT) != 0 &&
		(extbits & (MOVEEXT_VR | MOVEEXT_VR_RELATIVE)) !=
			(MOVEEXT_VR | MOVEEXT_VR_RELATIVE))
	{
		msg_badread = true;
		return false;
	}
	if ((extbits & (MOVEEXT_VR_GORILLA | MOVEEXT_GORILLA_TRUSTED)) != 0 &&
		(extbits & (MOVEEXT_VR | MOVEEXT_VR_RELATIVE)) !=
			(MOVEEXT_VR | MOVEEXT_VR_RELATIVE))
	{
		msg_badread = true;
		return false;
	}

	if ((extbits & MOVEEXT_GORILLA_TRUSTED) &&
		((extbits & MOVEEXT_VR_GORILLA) ||
		 !(capabilities & QSVR_MOVE_CAP_GORILLA_TRUSTED)))
	{
		msg_badread = true;
		return false;
	}

	if (extbits & MOVEEXT_VR)
	{
		if (net_message.cursize - msg_readcount < 9 * 4)
		{
			msg_badread = true;
			return false;
		}

		readcmd->vr_active = true;
		readcmd->vr_handpos_relative = (extbits & MOVEEXT_VR_RELATIVE) != 0;
		readcmd->vr_handpos[0] = MSG_ReadFloat ();
		readcmd->vr_handpos[1] = MSG_ReadFloat ();
		readcmd->vr_handpos[2] = MSG_ReadFloat ();
		readcmd->vr_handrot[0] = MSG_ReadFloat ();
		readcmd->vr_handrot[1] = MSG_ReadFloat ();
		readcmd->vr_handrot[2] = MSG_ReadFloat ();
		readcmd->vr_roomscalemove[0] = MSG_ReadFloat ();
		readcmd->vr_roomscalemove[1] = MSG_ReadFloat ();
		readcmd->vr_roomscalemove[2] = MSG_ReadFloat ();
		for (i = 0; i < 3; i++)
			if (!isfinite (readcmd->vr_handpos[i]) ||
				!isfinite (readcmd->vr_handrot[i]) ||
				!isfinite (readcmd->vr_roomscalemove[i]))
			{
				msg_badread = true;
				return false;
			}
	}

	if (extbits & MOVEEXT_VR_AKIMBO)
	{
		if (net_message.cursize - msg_readcount < 12 * 4)
		{
			msg_badread = true;
			return false;
		}
		readcmd->vr_akimbo_active = true;
		readcmd->vr_akimbo_berserk = (extbits & MOVEEXT_VR_AKIMBO_BERSERK) != 0;
		for (i = 0; i < 2; i++)
		{
			readcmd->vr_akimbo_muzzle[i][0] = MSG_ReadFloat ();
			readcmd->vr_akimbo_muzzle[i][1] = MSG_ReadFloat ();
			readcmd->vr_akimbo_muzzle[i][2] = MSG_ReadFloat ();
		}
		for (i = 0; i < 2; i++)
		{
			readcmd->vr_akimbo_angles[i][0] = MSG_ReadFloat ();
			readcmd->vr_akimbo_angles[i][1] = MSG_ReadFloat ();
			readcmd->vr_akimbo_angles[i][2] = MSG_ReadFloat ();
		}
		for (i = 0; i < 2; i++)
			if (!isfinite (readcmd->vr_akimbo_muzzle[i][0]) ||
				!isfinite (readcmd->vr_akimbo_muzzle[i][1]) ||
				!isfinite (readcmd->vr_akimbo_muzzle[i][2]) ||
				!isfinite (readcmd->vr_akimbo_angles[i][0]) ||
				!isfinite (readcmd->vr_akimbo_angles[i][1]) ||
				!isfinite (readcmd->vr_akimbo_angles[i][2]))
			{
				msg_badread = true;
				return false;
			}
	}

	if (extbits & MOVEEXT_QCINPUT)
	{
		if (net_message.cursize - msg_readcount < 34)
		{
			msg_badread = true;
			return false;
		}

		readcmd->weapon = MSG_ReadLong ();
		readcmd->cursor_screen[0] = MSG_ReadShort () / 32767.0f;
		readcmd->cursor_screen[1] = MSG_ReadShort () / 32767.0f;
		readcmd->cursor_start[0] = MSG_ReadFloat ();
		readcmd->cursor_start[1] = MSG_ReadFloat ();
		readcmd->cursor_start[2] = MSG_ReadFloat ();
		readcmd->cursor_impact[0] = MSG_ReadFloat ();
		readcmd->cursor_impact[1] = MSG_ReadFloat ();
		readcmd->cursor_impact[2] = MSG_ReadFloat ();
		readcmd->cursor_entitynumber = MSG_ReadEntity (QSVR_PEXT2_REQUIRED);
		for (i = 0; i < 3; i++)
			if (!isfinite (readcmd->cursor_start[i]) ||
				!isfinite (readcmd->cursor_impact[i]))
			{
				msg_badread = true;
				return false;
			}
	}

	if (extbits & MOVEEXT_VR_CONTACT)
	{
		unsigned int flags;

		if (net_message.cursize - msg_readcount < 7)
		{
			msg_badread = true;
			return false;
		}
		flags = (unsigned int)MSG_ReadByte ();
		if ((flags & ~VR_WEAPON_CONTACT_KNOWN_FLAGS) ||
			!(flags & (VR_WEAPON_CONTACT_LEFT_VALID |
				VR_WEAPON_CONTACT_RIGHT_VALID)))
		{
			msg_badread = true;
			return false;
		}
		readcmd->vr_contact.flags = flags;
		readcmd->vr_contact.modelindex = (unsigned short)MSG_ReadShort ();
		readcmd->vr_contact.weapon = MSG_ReadFloat ();
		if (!isfinite (readcmd->vr_contact.weapon))
		{
			msg_badread = true;
			return false;
		}
		for (i = 0; i < 2; i++)
		{
			if (!(flags & (1 << i)))
				continue;
			if (net_message.cursize - msg_readcount < 10 * 4)
			{
				msg_badread = true;
				return false;
			}
			readcmd->vr_contact.grip[i][0] = MSG_ReadFloat ();
			readcmd->vr_contact.grip[i][1] = MSG_ReadFloat ();
			readcmd->vr_contact.grip[i][2] = MSG_ReadFloat ();
			readcmd->vr_contact.base[i][0] = MSG_ReadFloat ();
			readcmd->vr_contact.base[i][1] = MSG_ReadFloat ();
			readcmd->vr_contact.base[i][2] = MSG_ReadFloat ();
			readcmd->vr_contact.tip[i][0] = MSG_ReadFloat ();
			readcmd->vr_contact.tip[i][1] = MSG_ReadFloat ();
			readcmd->vr_contact.tip[i][2] = MSG_ReadFloat ();
			readcmd->vr_contact.speed[i] = MSG_ReadFloat ();
			if (!isfinite (readcmd->vr_contact.grip[i][0]) ||
				!isfinite (readcmd->vr_contact.grip[i][1]) ||
				!isfinite (readcmd->vr_contact.grip[i][2]) ||
				!isfinite (readcmd->vr_contact.base[i][0]) ||
				!isfinite (readcmd->vr_contact.base[i][1]) ||
				!isfinite (readcmd->vr_contact.base[i][2]) ||
				!isfinite (readcmd->vr_contact.tip[i][0]) ||
				!isfinite (readcmd->vr_contact.tip[i][1]) ||
				!isfinite (readcmd->vr_contact.tip[i][2]) ||
				!isfinite (readcmd->vr_contact.speed[i]))
			{
				msg_badread = true;
				return false;
			}
		}
	}

	if (extbits & MOVEEXT_VR_GORILLA)
	{
		vec3_t arm;
		int hand;
		float length2;

		if (net_message.cursize - msg_readcount < 1 + 15 * 4)
		{
			msg_badread = true;
			return false;
		}
		readcmd->vr_gorilla.flags = (unsigned char)MSG_ReadByte ();
		for (i = 0; i < 3; i++)
			readcmd->vr_gorilla.head[i] = MSG_ReadFloat ();
		for (i = 0; i < 2; i++)
		{
			readcmd->vr_gorilla.hand[i][0] = MSG_ReadFloat ();
			readcmd->vr_gorilla.hand[i][1] = MSG_ReadFloat ();
			readcmd->vr_gorilla.hand[i][2] = MSG_ReadFloat ();
			readcmd->vr_gorilla.velocity[i][0] = MSG_ReadFloat ();
			readcmd->vr_gorilla.velocity[i][1] = MSG_ReadFloat ();
			readcmd->vr_gorilla.velocity[i][2] = MSG_ReadFloat ();
		}
		if ((readcmd->vr_gorilla.flags & ~VR_GORILLA_FLAGS) ||
			(readcmd->vr_gorilla.flags & VR_GORILLA_HANDS) != VR_GORILLA_HANDS ||
			!isfinite (readcmd->vr_gorilla.head[0]) ||
			!isfinite (readcmd->vr_gorilla.head[1]) ||
			!isfinite (readcmd->vr_gorilla.head[2]))
		{
			msg_badread = true;
			return false;
		}
		length2 = DotProduct (readcmd->vr_gorilla.head, readcmd->vr_gorilla.head);
		if (length2 > 160.0f * 160.0f)
		{
			msg_badread = true;
			return false;
		}
		for (hand = 0; hand < 2; hand++)
		{
			if (!isfinite (readcmd->vr_gorilla.hand[hand][0]) ||
				!isfinite (readcmd->vr_gorilla.hand[hand][1]) ||
				!isfinite (readcmd->vr_gorilla.hand[hand][2]) ||
				!isfinite (readcmd->vr_gorilla.velocity[hand][0]) ||
				!isfinite (readcmd->vr_gorilla.velocity[hand][1]) ||
				!isfinite (readcmd->vr_gorilla.velocity[hand][2]))
			{
				msg_badread = true;
				return false;
			}
			length2 = DotProduct (readcmd->vr_gorilla.velocity[hand],
				readcmd->vr_gorilla.velocity[hand]);
			if (length2 > VR_GORILLA_MAX_HAND_SPEED * VR_GORILLA_MAX_HAND_SPEED)
			{
				msg_badread = true;
				return false;
			}
			VectorSubtract (readcmd->vr_gorilla.hand[hand],
				readcmd->vr_gorilla.head, arm);
			length2 = DotProduct (arm, arm);
			if (length2 > VR_GORILLA_MAX_REACH * VR_GORILLA_MAX_REACH)
			{
				msg_badread = true;
				return false;
			}
		}
	}

	if (extbits & MOVEEXT_GORILLA_TRUSTED)
	{
		vr_gorilla_motion_t *motion = &readcmd->vr_gorilla_motion;
		int flags;

		if (net_message.cursize - msg_readcount < 13)
		{
			msg_badread = true;
			return false;
		}
		flags = MSG_ReadByte ();
		motion->flags = flags & VR_GORILLA_MOTION_FLAGS;
		motion->generation = (unsigned int)MSG_ReadLong ();
		for (i = 0; i < 2; ++i)
		{
			motion->contact[i] = (MSG_ReadShort () & 0xffff) - 1;
			motion->contact_model[i] = MSG_ReadShort () & 0xffff;
			if (motion->contact[i] >= MAX_EDICTS ||
				motion->contact_model[i] >= QSVR_MODEL_LIMIT ||
				(motion->contact[i] < 0 && motion->contact_model[i]))
				msg_badread = true;
		}
		if ((flags & ~63) || !(flags & VR_GORILLA_MOTION_ACTIVE) ||
			net_message.cursize - msg_readcount <
				((flags & 16) ? 12 : 0) + ((flags & 32) ? 12 : 0))
		{
			msg_badread = true;
			return false;
		}
		if (flags & 16)
			for (i = 0; i < 3; ++i)
				motion->displacement[i] = MSG_ReadFloat ();
		if (flags & 32)
			for (i = 0; i < 3; ++i)
				motion->impulse[i] = MSG_ReadFloat ();
		for (i = 0; i < 3; ++i)
			if (!isfinite (motion->displacement[i]) ||
				!isfinite (motion->impulse[i]) ||
				fabsf (motion->displacement[i]) > 64 ||
				fabsf (motion->impulse[i]) > 1024)
				msg_badread = true;
	}
	return !msg_badread;
}

/*
===================
SV_ReadClientMove
===================
*/
void SV_ReadClientMove (usercmd_t *move)
{
	int		 i;
	vec3_t	 angle;
	int		 buttonbits;
	int		 newimpulse;
	qboolean drop = false;
	vec3_t	 movevalues;
	int		 sequence;

	if (host_client->protocol_pext2 & PEXT2_PREDINFO)
	{
		i = (unsigned short)MSG_ReadShort ();
		sequence = (host_client->lastmovemessage & 0xffff0000) | (i & 0xffff);

		// tollerance of a few old frames, so we can have redundancy for packetloss
		if (sequence + 0x100 < host_client->lastmovemessage)
			sequence += 0x10000;

		if (sequence <= host_client->lastmovemessage)
			drop = true;
	}
	else
		sequence = 0;

	// read ping time
	host_client->ping_times[host_client->num_pings % NUM_PING_TIMES] = qcvm->time - MSG_ReadFloat ();
	host_client->num_pings++;

	for (i = 0; i < 3; i++)
	{
		if (sv.protocol == PROTOCOL_NETQUAKE && !NET_QSocketGetProQuakeAngleHack (cls.netcon) && !(host_client->protocol_pext2 & PEXT2_PREDINFO))
			angle[i] = MSG_ReadAngle (sv.protocolflags);
		else
			angle[i] = MSG_ReadAngle16 (sv.protocolflags); // johnfitz -- 16-bit angles for PROTOCOL_FITZQUAKE
	}
	movevalues[0] = MSG_ReadShort ();
	movevalues[1] = MSG_ReadShort ();
	movevalues[2] = MSG_ReadShort ();
	buttonbits = MSG_ReadByte ();
	newimpulse = MSG_ReadByte ();

	if (drop)
		return; // okay, we don't care about that then

	// calc ping times
	host_client->lastmovemessage = sequence;

	// read movement
	VectorCopy (angle, host_client->edict->v.v_angle);
	move->forwardmove = movevalues[0];
	move->sidemove = movevalues[1];
	move->upmove = movevalues[2];

	// read buttons
	host_client->edict->v.button0 = (buttonbits & 1) >> 0;
	// button1 was meant to be 'use', but got reused by too many mods to get implemented now
	host_client->edict->v.button2 = (buttonbits & 2) >> 1;

	if (newimpulse)
		host_client->edict->v.impulse = newimpulse;
}

/*
===================
SV_ReadClientMessage

Returns false if the client should be killed
===================
*/
qboolean SV_ReadClientMessage (void)
{
	int			ccmd;
	const char *s;

	MSG_BeginReading ();

	while (1)
	{
		if (!host_client->active)
			return false; // a command caused an error

		if (msg_badread)
		{
			Sys_Printf ("SV_ReadClientMessage: badread\n");
			return false;
		}

		ccmd = MSG_ReadChar ();

		switch (ccmd)
		{
		case -1:
			return true; // msg_badread, meaning we just hit eof.

		default:
			Sys_Printf ("SV_ReadClientMessage: unknown command char\n");
			return false;

		case clc_nop:
			//			Sys_Printf ("clc_nop\n");
			break;

		case clc_stringcmd: {
			s = MSG_ReadString ();
			// The engine must see its protocol offer before a mod's client-command
			// hook can consume it. Keep other client strings on their existing path.
			const qboolean pext_offer = !q_strncasecmp (s, "pext", 4) &&
				(s[4] == '\0' || s[4] == ' ' || s[4] == '\t');
			if (!pext_offer && q_strncasecmp (s, "spawn", 5) && q_strncasecmp (s, "begin", 5) && q_strncasecmp (s, "prespawn", 8) && qcvm->extfuncs.SV_ParseClientCommand)
			{ // the spawn/begin/prespawn are because of numerous mods that disobey the rules.
				// at a minimum, we must be able to join the server, so that we can see any sprints/bprints (because dprint sucks, yes there's proper ways
				// to deal with this, but moders don't always know them).
				client_t *ohc = host_client;
				G_INT (OFS_PARM0) = PR_SetEngineString (s);
				pr_global_struct->time = qcvm->time;
				pr_global_struct->self = EDICT_TO_PROG (host_client->edict);
				PR_ExecuteProgram (qcvm->extfuncs.SV_ParseClientCommand);
				host_client = ohc;
			}
			else
				Cmd_ExecuteString (s, src_client);
			break;
		}

		case clc_disconnect:
			//			Sys_Printf ("SV_ReadClientMessage: client disconnected\n");
			return false;

		case clc_move:
			if (!host_client->spawned)
				return true; // this is to suck up any stale moves on map changes, so we don't get confused (quite so easily) when protocols are changed
							 // between maps
			SV_ReadClientMove (&host_client->cmd);
			break;
		case clcdp_ackframe:
			SVFTE_Ack (host_client, MSG_ReadLong ());
			break;
		}
	}

	return true;
}

/*
==================
SV_RunClients
==================
*/
void SV_RunClients (void)
{
	int i;

	// receive from clients first
	// Spike -- reworked this to query the network code for an active connection.
	// this allows the network code to serve multiple clients with the same listening port.
	// this solves server-side nats, which is important for coop etc.
	while (1)
	{
		struct qsocket_s *sock = NET_GetServerMessage ();
		if (!sock)
			break; // no more this frame

		for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		{
			if (host_client->netconnection == sock)
			{
				sv_player = host_client->edict;
				if (!SV_ReadClientMessage ())
				{
					SV_DropClient (false); // client misbehaved...
					break;
				}
			}
		}
	}

	// then do the per-frame stuff
	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
	{
		if (!host_client->active)
			continue;

		sv_player = host_client->edict;

		if (!host_client->spawned)
		{
			// clear client movement until a new packet is received
			memset (&host_client->cmd, 0, sizeof (host_client->cmd));
			continue;
		}

		if (!host_client->netconnection)
		{
			host_client->cmd.viewangles[0] = host_client->edict->v.v_angle[0];
			host_client->cmd.viewangles[1] = host_client->edict->v.v_angle[1];
			host_client->cmd.viewangles[2] = host_client->edict->v.v_angle[2];
		}

		// always pause in single player if in console or menus
		if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game))
			SV_ClientThink ();
	}
}
