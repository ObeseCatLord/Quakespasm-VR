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
#include "player_avatar.h"
#include <limits.h>

edict_t *sv_player;

extern cvar_t sv_friction;
cvar_t		  sv_edgefriction = {"edgefriction", "2", CVAR_NONE};
extern cvar_t sv_stopspeed;
extern cvar_t sv_gameplayfix_elevators;

static vec3_t forward, right, up;

// world
static float *angles;
static float *origin;
static float *velocity;

static qboolean onground;
static qboolean sv_gorilla_swim_intent;

static usercmd_t cmd;

cvar_t sv_idealpitchscale = {"sv_idealpitchscale", "0.8", CVAR_NONE};
cvar_t sv_altnoclip = {"sv_altnoclip", "1", CVAR_ARCHIVE_GAME}; // johnfitz
cvar_t vr_movement_instant_stop = {"vr_movement_instant_stop", "0", CVAR_ARCHIVE};

qboolean SV_ClientInstantStopEnabled (const client_t *client)
{
	return client && client->active && client->spawned &&
		client->protocol_qsvr == QSVR_PROTOCOL_PINNED &&
		client->private_pmove_walk_selected &&
		client->vr_instant_stop_offered && client->vr_instant_stop_capable &&
		isfinite (vr_movement_instant_stop.value) &&
		vr_movement_instant_stop.value != 0.0f;
}

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

		if (!sv_analyticphysics_frame || r <= 0 || r >= 1)
		{
			// The crossing calculation requires 0 < r < 1. Zero friction
			// otherwise divides 0 by log(1) at stopspeed and creates NaNs.
			// Use the classic formula for non-decaying or one-tick friction.
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

void SV_ClientUpdateAnglesForClient (client_t *client)
{
	edict_t *saved_player;
	edict_t *ent;
	vec3_t v_angle;

	if (!client || !client->edict)
		return;
	ent = client->edict;
	if (ent->v.movetype == MOVETYPE_NONE)
		return;

	saved_player = sv_player;
	sv_player = ent;
	DropPunchAngle ();
	if (!(ent->v.health <= 0))
	{
		// show 1/3 the pitch angle and all the roll angle
		angles = ent->v.angles;
		VectorAdd (ent->v.v_angle, ent->v.punchangle, v_angle);
		angles[ROLL] = V_CalcRoll (ent->v.angles, ent->v.velocity) * 4;
		if (!ent->v.fixangle)
		{
			angles[PITCH] = -v_angle[PITCH] / 3;
			angles[YAW] = v_angle[YAW];
		}
	}
	sv_player = saved_player;
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

	if (!cmd.forwardmove && !cmd.sidemove && !cmd.upmove &&
		!sv_gorilla_swim_intent)
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
		if (isfinite (vr_movement_instant_stop.value) &&
			vr_movement_instant_stop.value != 0.0f && host_client &&
			host_client->cmd.vr_active && wishspeed == 0 &&
			!SV_GorillaEligible (host_client) &&
			!SV_GorillaNativeLadder (sv_player))
		{
			velocity[0] = 0;
			velocity[1] = 0;
		}
		else
		{
			SV_UserFriction ();
			SV_Accelerate (wishspeed, wishdir);
		}
	}
	else
	{ // not on ground, so little effect on velocity
		SV_AirAccelerate (wishspeed, wishvel);
	}
}

qboolean SV_GorillaNativeLadder (edict_t *ent)
{
	eval_t *value;
	if (!ent)
		return false;
	value = GetEdictFieldValue (ent, ED_FindFieldOffset ("onladder"));
	if (value)
		return value->_float != 0;
	/* Immortal's ladder_touch writes laddercount; the other fields/functions
	 * identify that specific contract rather than a generic cooldown field. */
	value = GetEdictFieldValue (ent, ED_FindFieldOffset ("laddercount"));
	return value && value->_float > 0 &&
		ED_FindFieldOffset ("laddertime") >= 0 &&
		ED_FindFieldOffset ("laddersoundtime") >= 0 &&
		ED_FindFunction ("ladder_touch") && ED_FindFunction ("trigger_ladder");
}

void SV_GorillaLatchLadder (client_t *client, qboolean begin_frame)
{
	if (!client)
		return;
	if (begin_frame)
		client->vr_gorilla_ladder_frame = false;
	client->vr_gorilla_ladder_frame |= SV_GorillaNativeLadder (client->edict);
}

qboolean SV_GorillaEligible (client_t *client)
{
	edict_t *ent;
	eval_t *customphysics;
	if (!client || !client->active || !client->spawned ||
		(SV_PrivateWalkTrialSelected (client) && !client->private_move_native_frame) ||
		client->protocol_qsvr != QSVR_PROTOCOL_PINNED ||
		!client->vr_gorilla_capable || !sv_gorilla.value || sv.paused ||
		(client->cmd.vr_gorilla.flags & VR_GORILLA_HANDS) != VR_GORILLA_HANDS)
		return false;
	ent = client->edict;
	if (!ent || ent->free)
		return false;
	customphysics = GetEdictFieldValue (ent, qcvm->extfields.customphysics);
	if (customphysics && customphysics->function)
		return false;
	return ent->v.health > 0 && !ent->v.deadflag &&
		!SV_PrivateWalkTrialMotionHeld (client) &&
		((int)ent->v.movetype == MOVETYPE_WALK ||
		 (int)ent->v.movetype == MOVETYPE_FLY) &&
		!client->vr_gorilla_ladder_frame && !SV_GorillaNativeLadder (ent);
}

static void SV_ConsumeClientMove (void)
{
	if ((int)sv_player->v.flags & FL_WATERJUMP)
	{
		SV_WaterJump ();
		return;
	}
	if (sv_player->v.movetype == MOVETYPE_NOCLIP && sv_altnoclip.value)
		SV_NoclipMove ();
	else if (sv_player->v.waterlevel >= 2 &&
		sv_player->v.movetype != MOVETYPE_NOCLIP)
		SV_WaterMove ();
	else
		SV_AirMove ();
}

static void SV_GorillaConsumeDeferredMove (client_t *client,
	qboolean water_step, qboolean swim_intent)
{
	client_t *saved_client;
	edict_t *saved_player;
	usercmd_t saved_cmd;
	float *saved_origin, *saved_velocity, *saved_angles;
	qboolean saved_onground, saved_swim_intent;
	vec3_t saved_forward, saved_right, saved_up;
	qboolean gorilla;
	if (!client || !client->vr_gorilla_move_deferred)
		return;
	if (!client->edict || client->edict->free ||
		(SV_PrivateWalkTrialSelected (client) && !client->private_move_native_frame) ||
		client->edict->v.movetype == MOVETYPE_NONE ||
		(client->edict->v.movetype != MOVETYPE_WALK &&
		 client->edict->v.movetype != MOVETYPE_FLY &&
		 client->edict->v.movetype != MOVETYPE_NOCLIP) ||
		client->edict->v.health <= 0 || SV_PrivateWalkTrialMotionHeld (client))
	{
		client->vr_gorilla_move_deferred = false;
		return;
	}
	gorilla = SV_GorillaEligible (client);
	if (gorilla && !water_step)
		return;
	client->vr_gorilla_move_deferred = false;
	saved_client = host_client;
	saved_player = sv_player;
	saved_cmd = cmd;
	saved_origin = origin;
	saved_velocity = velocity;
	saved_angles = angles;
	saved_onground = onground;
	saved_swim_intent = sv_gorilla_swim_intent;
	VectorCopy (forward, saved_forward);
	VectorCopy (right, saved_right);
	VectorCopy (up, saved_up);
	host_client = client;
	sv_player = client->edict;
	cmd = client->cmd;
	if (gorilla)
		cmd.forwardmove = cmd.sidemove = cmd.upmove = 0;
	sv_gorilla_swim_intent = gorilla && swim_intent;
	onground = (int)sv_player->v.flags & FL_ONGROUND;
	origin = sv_player->v.origin;
	velocity = sv_player->v.velocity;
	angles = sv_player->v.angles;
	SV_ConsumeClientMove ();
	host_client = saved_client;
	sv_player = saved_player;
	cmd = saved_cmd;
	origin = saved_origin;
	velocity = saved_velocity;
	angles = saved_angles;
	onground = saved_onground;
	sv_gorilla_swim_intent = saved_swim_intent;
	VectorCopy (saved_forward, forward);
	VectorCopy (saved_right, right);
	VectorCopy (saved_up, up);
}

void SV_GorillaResumeDeferredMove (client_t *client)
{
	SV_GorillaConsumeDeferredMove (client, false, false);
}

void SV_GorillaConsumeWater (client_t *client, qboolean swim_intent)
{
	SV_GorillaConsumeDeferredMove (client, true, swim_intent);
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
	host_client->vr_gorilla_move_deferred = false;
	SV_GorillaLatchLadder (host_client, true);
	if (sv_player->v.movetype == MOVETYPE_NONE)
		return;

	onground = (int)sv_player->v.flags & FL_ONGROUND;

	origin = sv_player->v.origin;
	velocity = sv_player->v.velocity;

	SV_ClientUpdateAnglesForClient (host_client);
	if (sv_player->v.health <= 0)
		return;
	/* Cooperative QC replaces input acceleration as well as body physics.
	 * Angle/recoil ownership stays above; do not erase later authored forces. */
	if (qcvm->extfuncs.SV_RunClientCommand)
		return;
	cmd = host_client->cmd;
	if (SV_GorillaEligible (host_client))
	{
		host_client->vr_gorilla_move_deferred = true;
		return;
	}
	SV_ConsumeClientMove ();
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
	/* Raw palms carry their own body-relative head and hand positions. They
	 * need no weapon pose, unlike authored trusted motion for now. */
	if ((extbits & MOVEEXT_GORILLA_TRUSTED) != 0 &&
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
		/* Framing is flag-owned even after a weapon switch/offer revocation.
		 * Exact Bonk and current capability admission belongs to sv_phys. */
		if (flags & VR_WEAPON_CONTACT_HEAD_PRESENT)
		{
			if (net_message.cursize - msg_readcount < VR_WEAPON_CONTACT_HEAD_BYTES)
			{
				msg_badread = true;
				return false;
			}
			for (i = 0; i < 3; ++i)
			{
				readcmd->vr_contact.head_angles[i] = MSG_ReadFloat ();
				if (!isfinite (readcmd->vr_contact.head_angles[i]) ||
					readcmd->vr_contact.head_angles[i] < -180.0f ||
					readcmd->vr_contact.head_angles[i] >= 180.0f)
				{
					msg_badread = true;
					return false;
				}
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
	float timestamp = MSG_ReadFloat ();
	host_client->ping_times[host_client->num_pings % NUM_PING_TIMES] = qcvm->time - timestamp;
	host_client->num_pings++;

	for (i = 0; i < 3; i++)
	{
		if (sv.protocol == PROTOCOL_NETQUAKE && !NET_QSocketGetProQuakeAngleHack (host_client->netconnection) &&
			!(host_client->protocol_pext2 & PEXT2_PREDINFO))
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
	SV_SetClientExtraButtons (host_client->edict, buttonbits);

	if (newimpulse)
		host_client->edict->v.impulse = newimpulse;
	if (qcvm->extfuncs.SV_RunClientCommand)
	{
		/* Public wire input previously lived partly on the edict. Retain its
		 * decoded metadata for the existing cooperative input bridge, keeping
		 * native impulse latching across a later zero-impulse packet. */
		move->sequence = sequence;
		move->servertime = isfinite (timestamp) ? timestamp : qcvm->time;
		VectorCopy (angle, move->viewangles);
		move->buttons = buttonbits;
		move->impulse = newimpulse; // wire value; native projection owns QC latching
	}
}

/* QSS-M's optional QuakeC button3..8 fields use the remaining bits of the
 * existing move byte. The same projection is used by public and selected
 * private commands, so a mod sees its buttons before each QC callback. */
void SV_SetClientExtraButtons (edict_t *ent, unsigned int buttons)
{
	eval_t *field;
	if ((field = GetEdictFieldValue (ent, qcvm->extfields.button3)))
		field->_float = (buttons >> 2) & 1u;
	if ((field = GetEdictFieldValue (ent, qcvm->extfields.button4)))
		field->_float = (buttons >> 3) & 1u;
	if ((field = GetEdictFieldValue (ent, qcvm->extfields.button5)))
		field->_float = (buttons >> 4) & 1u;
	if ((field = GetEdictFieldValue (ent, qcvm->extfields.button6)))
		field->_float = (buttons >> 5) & 1u;
	if ((field = GetEdictFieldValue (ent, qcvm->extfields.button7)))
		field->_float = (buttons >> 6) & 1u;
	if ((field = GetEdictFieldValue (ent, qcvm->extfields.button8)))
		field->_float = (buttons >> 7) & 1u;
}

/* The explicit private profile uses complete, redundant commands in each
 * datagram. Decode even stale records so the following command starts at the
 * right byte; only fresh records may mutate accepted gameplay state. */
void SV_ResetGorillaClient (client_t *client)
{
	if (!client)
		return;
	memset (&client->vr_gorilla_state, 0, sizeof (client->vr_gorilla_state));
	client->vr_gorilla_reset_generation++;
	client->vr_gorilla_last_sequence = 0;
	client->vr_gorilla_cursor_valid = false;
	/* A callback can invalidate hand continuity mid-frame. Keep the native
	 * movement pass pending; SV_ClientThink owns its once-per-frame latch. */
	memset (client->vr_gorilla_button, 0, sizeof (client->vr_gorilla_button));
}

/* Relocation invalidates every hand sample accepted at the old origin, but
 * the ordinary movement owner still retires those commands in sequence. */
void SV_GorillaInvalidateAccepted (client_t *client)
{
	if (!client)
		return;
	SV_ResetGorillaClient (client);
	client->vr_gorilla_last_sequence = client->lastmovemessage;
	client->vr_gorilla_cursor_valid = true;
}

static void SV_DiscardPrivateCommandQueue (client_t *client, int through_sequence)
{
	while (client->private_cmd_queue_count)
	{
		memset (&client->private_cmd_queue[client->private_cmd_queue_head], 0,
			sizeof (client->private_cmd_queue[client->private_cmd_queue_head]));
		client->private_cmd_queue_head = (client->private_cmd_queue_head + 1) %
			SV_PRIVATE_CMD_QUEUE_SIZE;
		client->private_cmd_queue_count--;
	}
	if (through_sequence > client->private_discarded_move)
		client->private_discarded_move = through_sequence;
	client->private_cmd_queue_head = 0;
	client->private_cmd_queue_msec = 0;
	SV_ResetGorillaClient (client);
}

void SV_ResetPrivateCommandQueue (client_t *client)
{
	memset (client->private_cmd_queue, 0, sizeof (client->private_cmd_queue));
	client->private_cmd_queue_head = 0;
	client->private_cmd_queue_count = 0;
	client->private_cmd_queue_msec = 0;
	client->private_retired_move = 0;
	client->private_discarded_move = 0;
	client->private_move_discontinuity_epoch = 0;
	client->private_move_discontinuity_reason = MOVEACK_DISCONTINUITY_NONE;
	client->private_move_mode_epoch = 0;
	client->private_move_published_authority = MOVE_AUTHORITY_UNKNOWN;
	client->private_move_published_authority_valid = false;
	client->private_move_native_frame = false;
	client->private_input_phase = PRIVATE_INPUT_RUNNING;
	client->private_resume_first_sequence = 0;
	client->private_pmove_walk_selected = false;
	client->private_pmove_pusher_interaction = false;
	client->private_pmove_credit_msec = 0.0;
	client->private_pmove_jump_secs = 0.0f;
	client->private_pmove_waterjump_secs = 0.0f;
	memset (&client->private_pmove_last_cmd, 0, sizeof (client->private_pmove_last_cmd));
	client->private_pmove_last_cmd_valid = false;
	SV_ResetGorillaClient (client);
}

/* Semantic relocation is narrower than contact invalidation: QuakeC may
 * adjust a player with setorigin without intending a teleport. The explicit
 * server relocation owners call this after committing a new location. */
void SV_PrivatePlayerTeleported (edict_t *ent, qboolean preserve_deadline)
{
	if (!ent)
		return;
	for (int slot = 0; slot < svs.maxclients; ++slot)
	{
		client_t *client = &svs.clients[slot];
		if (client->edict != ent || !client->active ||
			client->protocol_qsvr != QSVR_PROTOCOL_PINNED)
			continue;
		client->private_move_discontinuity_epoch++;
		client->private_move_discontinuity_reason =
			MOVEACK_DISCONTINUITY_RESET_TELEPORT;
		if (SV_PrivateWalkTrialSelected (client))
		{
			client->private_pmove_jump_secs = 0.0f;
			client->private_pmove_waterjump_secs = 0.0f;
			ent->v.flags = (int)ent->v.flags & ~FL_WATERJUMP;
			/* Explicit setpos/recovery/co-op relocation has no new hold.
			 * Identified QC teleport_touch owns its authored deadline, even
			 * when numerically equal to an in-flight solver deadline. */
			if (!preserve_deadline)
				ent->v.teleport_time = 0.0f;
		}
		SV_GorillaInvalidateAccepted (client);
		return;
	}
}

static qboolean SV_QueuePrivateCommand (client_t *client, const usercmd_t *command,
	double received_at)
{
	unsigned int duration = command->msec;
	unsigned int tail;
	usercmd_t *queued;

	if (duration < 1 || duration > 125 ||
		client->private_cmd_queue_count >= SV_PRIVATE_CMD_QUEUE_SIZE ||
		client->private_cmd_queue_msec + duration > SV_PRIVATE_CMD_QUEUE_MAX_MSEC)
	{
		if (SV_PrivateWalkTrialSelected (client))
		{
			Sys_Printf ("%s: private WALK trial command queue overflow/discontinuity\n",
				client->name);
			return false;
		}
		/* A queue gap is safer than replaying stale input after falling behind. */
		SV_DiscardPrivateCommandQueue (client, command->sequence);
		return true;
	}

	tail = (client->private_cmd_queue_head + client->private_cmd_queue_count) %
		SV_PRIVATE_CMD_QUEUE_SIZE;
	queued = &client->private_cmd_queue[tail];
	*queued = *command;
	queued->seconds = duration * 0.001f;
	queued->vr_contact_received = received_at;
	client->private_cmd_queue_count++;
	client->private_cmd_queue_msec += duration;
	return true;
}

static void SV_RetirePrivateCommandsThrough (client_t *client, int completed_sequence)
{
	if (completed_sequence <= client->private_retired_move)
		return;

	while (client->private_cmd_queue_count)
	{
		usercmd_t *command = &client->private_cmd_queue[client->private_cmd_queue_head];
		if ((int)command->sequence > completed_sequence)
			break;
		if (client->private_cmd_queue_msec >= command->msec)
			client->private_cmd_queue_msec -= command->msec;
		else
			client->private_cmd_queue_msec = 0;
		memset (command, 0, sizeof (*command));
		client->private_cmd_queue_head = (client->private_cmd_queue_head + 1) %
			SV_PRIVATE_CMD_QUEUE_SIZE;
		client->private_cmd_queue_count--;
	}
	if (!client->private_cmd_queue_count)
		client->private_cmd_queue_head = 0;
	client->private_retired_move = completed_sequence;
}

static qboolean SV_PrivateWalkTrialFail (client_t *client, const char *reason)
{
	Sys_Printf ("%s: private WALK trial failed: %s\n", client->name, reason);
	return false;
}

static qboolean SV_PrivateWalkTrialStateValid (client_t *client)
{
	const char *failure;
	if (sv.paused)
		return SV_PrivateWalkTrialFail (client, "server paused");
	failure = SV_PrivateWalkTrialFrameStateError (client->edict, client, NULL);
	return failure ? SV_PrivateWalkTrialFail (client, failure) : true;
}

/* Pause and arrival recovery share one transient-input and completion owner.
 * Clearing work is not execution: preserve the completed ACK and player state. */
static void SV_PrivateClearTransientInput (client_t *client)
{
	SV_DiscardPrivateCommandQueue (client, client->lastmovemessage);
	SV_ResetPrivateVRContactState (client);
	client->private_pmove_last_cmd_valid = false;
	memset (&client->private_pmove_last_cmd, 0,
		sizeof (client->private_pmove_last_cmd));
	client->private_pmove_credit_msec = 0.0;
	client->private_latest_buttons = 0;
	client->private_latched_buttons = 0;
	client->private_latched_impulse = 0;
	client->cmd.forwardmove = 0;
	client->cmd.sidemove = 0;
	client->cmd.upmove = 0;
	client->cmd.buttons = 0;
	client->cmd.impulse = 0;
	memset (client->cmd.vr_roomscalemove, 0,
		sizeof (client->cmd.vr_roomscalemove));
	memset (&client->cmd.vr_gorilla, 0, sizeof (client->cmd.vr_gorilla));
	memset (&client->cmd.vr_gorilla_motion, 0,
		sizeof (client->cmd.vr_gorilla_motion));
	if (client->edict && !client->edict->free)
	{
		client->edict->v.button0 = 0;
		client->edict->v.button2 = 0;
		SV_SetClientExtraButtons (client->edict, 0);
		client->edict->v.impulse = 0;
	}
	client->private_resume_first_sequence = 0;
}

static void SV_PrivatePublishRecoveryFence (client_t *client)
{
	client->private_move_discontinuity_epoch++;
	/* A not-yet-observed relocation still needs the owner's teleport snap.
	 * Keep that stronger reason without introducing a second receipt owner. */
	if (client->private_move_discontinuity_reason != MOVEACK_DISCONTINUITY_RESET_TELEPORT)
		client->private_move_discontinuity_reason = MOVEACK_DISCONTINUITY_GAP;
	client->private_input_phase = PRIVATE_INPUT_AWAIT_MARKER;
}

static qboolean SV_PrivateInputGloballySuspended (void)
{
	return sv.paused ||
		(svs.maxclients <= 1 && key_dest != key_game);
}

/* Read-only: the terminal guard reads client/edict fields only, with no qcvm
 * requirement. Keep this predicate safe for the client-side local query. */
static qboolean SV_PrivateArrivalRecoveryDue (client_t *client)
{
	return (client->private_input_phase == PRIVATE_INPUT_RUNNING ||
		client->private_input_phase == PRIVATE_INPUT_AWAIT_COMPLETION) &&
		!SV_PrivateWalkTrialTerminalState (client) && client->lastmovetime > 0 &&
		realtime - client->lastmovetime > 1.0;
}

qboolean SV_LocalPrivateInputSuspended (const struct qsocket_s *socket)
{
	if (!sv.active)
		return false;
	for (int slot = 0; slot < svs.maxclients; ++slot)
	{
		client_t *client = &svs.clients[slot];
		if (!client->active || client->protocol_qsvr != QSVR_PROTOCOL_PINNED ||
			!NET_QSocketIsLoopbackPeer (socket, client->netconnection))
			continue;
		return SV_PrivateInputGloballySuspended () ||
			(SV_PrivateWalkTrialSelected (client) &&
			 (client->private_input_phase == PRIVATE_INPUT_SUSPENDED ||
			  client->private_input_phase == PRIVATE_INPUT_AWAIT_MARKER ||
			  SV_PrivateArrivalRecoveryDue (client)));
	}
	return false;
}

static void SV_PrivateSyncPauseState (client_t *client)
{
	const qboolean suspended = SV_PrivateInputGloballySuspended ();

	if (!SV_PrivateWalkTrialSelected (client))
		return;
	if (suspended)
	{
		if (client->private_input_phase == PRIVATE_INPUT_SUSPENDED)
			return;
		SV_PrivateClearTransientInput (client);
		client->private_input_phase = PRIVATE_INPUT_SUSPENDED;
		return;
	}
	if (client->private_input_phase == PRIVATE_INPUT_SUSPENDED)
	{
		/* Publish only after resume: an earlier marker could still refer to
		 * commands produced while paused. */
		SV_PrivatePublishRecoveryFence (client);
		return;
	}
	if (SV_PrivateArrivalRecoveryDue (client))
	{
		SV_PrivateClearTransientInput (client);
		SV_PrivatePublishRecoveryFence (client);
	}
}

/* Capture pause/fence state at the toggle, not at a later world tick. The
 * existing private ACK immediately precedes the ordinary pause service;
 * its source epoch remains meaningful across reliable/snapshot reordering. */
void SV_SendPauseNotifications (void)
{
	for (int slot = 0; slot < svs.maxclients; ++slot)
	{
		client_t *client = &svs.clients[slot];
		if (!client->active)
			continue;
		if (client->protocol_qsvr == QSVR_PROTOCOL_PINNED && SV_PrivateWalkTrialSelected (client))
		{
			int flags = MOVEACK_FLAG_SELECTED;
			SV_PrivateSyncPauseState (client);
			if (client->private_input_phase == PRIVATE_INPUT_AWAIT_MARKER)
				flags |= MOVEACK_FLAG_RESUME_PENDING;
			if (client->private_move_discontinuity_reason != MOVEACK_DISCONTINUITY_NONE)
				flags |= MOVEACK_FLAG_DISCONTINUITY;
			MSG_WriteByte (&client->message, QSVR_SVC_MOVEACK);
			MSG_WriteShort (&client->message, client->private_completed_move & 0xffff);
			MSG_WriteByte (&client->message, flags);
			MSG_WriteByte (&client->message, MOVE_AUTHORITY_UNKNOWN);
			MSG_WriteShort (&client->message, client->private_move_mode_epoch);
			MSG_WriteShort (&client->message, client->private_move_discontinuity_epoch);
			MSG_WriteByte (&client->message, client->private_move_discontinuity_reason);
		}
		MSG_WriteByte (&client->message, svc_setpause);
		MSG_WriteByte (&client->message, sv.paused);
	}
}

static qboolean SV_HandlePrivateResumeMarker (const char *s)
{
	unsigned int epoch, first_sequence;
	char trailing;
	client_t *client = host_client;

	if (strncmp (s, "qsvr_resume", 11) ||
		(s[11] != ' ' && s[11] != '\t'))
		return false;
	/* This private engine command must never reach a mod's QC handler. */
	if (!SV_PrivateWalkTrialSelected (client))
		return true;
	SV_PrivateSyncPauseState (client);
	if (sscanf (s + 11, "%u %u %c", &epoch, &first_sequence,
		&trailing) != 2 || epoch > 0xffff || first_sequence < 2 ||
		first_sequence > INT_MAX ||
		(int)first_sequence <= client->private_completed_move ||
		client->private_input_phase != PRIVATE_INPUT_AWAIT_MARKER ||
		epoch != client->private_move_discontinuity_epoch)
		return true;
	client->private_resume_first_sequence = (int)first_sequence;
	/* The producer sends its full sequence in this reliable marker. It also
	 * restores 16-bit move expansion if generation crossed a wrap during the
	 * suspension before the client learned that it was paused. */
	if (client->lastmovemessage < (int)first_sequence - 1)
		client->lastmovemessage = (int)first_sequence - 1;
	SV_DiscardPrivateCommandQueue (client, client->lastmovemessage);
	client->lastmovetime = realtime;
	client->private_input_phase = PRIVATE_INPUT_AWAIT_COMPLETION;
	return true;
}

static qboolean SV_ReadPrivateClientMove (void)
{
	usercmd_t readcmd;
	int sequence16 = MSG_ReadShort () & 0xffff;
	int last = host_client->lastmovemessage;
	int sequence = (last & ~0xffff) | sequence16;
	vec3_t roomscale;
	float horizontal;

	SV_PrivateSyncPauseState (host_client);
	if (msg_badread)
		return false;
	if (sequence - last > 0x8000)
		sequence -= 0x10000;
	else if (last - sequence > 0x8000)
		sequence += 0x10000;
	if (!SV_ReadPrivateUsercmd (&readcmd, sequence, sv.protocolflags, 0))
		return false;
	if (sequence <= last)
		return true;
	if (SV_PrivateWalkTrialSelected (host_client))
	{
		if (host_client->private_input_phase == PRIVATE_INPUT_SUSPENDED ||
			host_client->private_input_phase == PRIVATE_INPUT_AWAIT_MARKER ||
			(host_client->private_input_phase == PRIVATE_INPUT_AWAIT_COMPLETION &&
			 sequence < host_client->private_resume_first_sequence))
		{
			host_client->lastmovemessage = sequence;
			host_client->lastmovetime = realtime;
			SV_DiscardPrivateCommandQueue (host_client, sequence);
			return true;
		}
		if (!SV_PrivateWalkTrialTerminalState (host_client) &&
			!SV_PrivateWalkTrialStateValid (host_client))
			return false;
		if (readcmd.vr_gorilla_motion.flags)
			return SV_PrivateWalkTrialFail (host_client,
				"trusted Gorilla motion is outside the raw trial");
	}

	/* A long arrival gap starts a new queue epoch, then this fresh command may
	 * begin the new queue. The legacy latest-command path remains unchanged. */
	if (!SV_PrivateWalkTrialSelected (host_client) && host_client->lastmovetime > 0 &&
		realtime - host_client->lastmovetime > 1.0)
		SV_DiscardPrivateCommandQueue (host_client, last);

	/* Validate each sample, not the total accumulated across a server frame. */
	horizontal = sqrtf (readcmd.vr_roomscalemove[0] * readcmd.vr_roomscalemove[0] +
		readcmd.vr_roomscalemove[1] * readcmd.vr_roomscalemove[1]);
	if (!isfinite (horizontal) || horizontal > 16.0f ||
		fabsf (readcmd.vr_roomscalemove[2]) > 16.0f)
		memset (readcmd.vr_roomscalemove, 0, sizeof (readcmd.vr_roomscalemove));
	if (SV_PrivateWalkTrialSelected (host_client))
	{
		if (!readcmd.vr_active)
			memset (readcmd.vr_roomscalemove, 0, sizeof (readcmd.vr_roomscalemove));
		if (!SV_QueuePrivateCommand (host_client, &readcmd, realtime))
			return false;
		host_client->lastmovemessage = sequence;
		host_client->lastmovetime = realtime;
		host_client->ping_times[host_client->num_pings % NUM_PING_TIMES] =
			qcvm->time - readcmd.servertime;
		host_client->num_pings++;
		return true;
	}
	/* Tracking received while gameplay is suspended must not become motion
	 * debt when the next physics frame eventually runs. */
	if (!readcmd.vr_active || sv.paused || (svs.maxclients <= 1 && key_dest != key_game) ||
		host_client->edict->v.movetype == MOVETYPE_NONE)
	{
		memset (host_client->cmd.vr_roomscalemove, 0,
			sizeof (host_client->cmd.vr_roomscalemove));
		memset (readcmd.vr_roomscalemove, 0, sizeof (readcmd.vr_roomscalemove));
	}
	if (sv.paused || (svs.maxclients <= 1 && key_dest != key_game) ||
		host_client->edict->v.movetype == MOVETYPE_NONE)
		SV_DiscardPrivateCommandQueue (host_client, sequence);
	else if (!SV_QueuePrivateCommand (host_client, &readcmd, realtime))
		return false;
	VectorAdd (host_client->cmd.vr_roomscalemove, readcmd.vr_roomscalemove, roomscale);
	VectorCopy (roomscale, readcmd.vr_roomscalemove);
	readcmd.seconds = 0; // latest-command mode uses the normal server frame clock
	readcmd.vr_contact_received = realtime;
	host_client->private_latest_buttons = readcmd.buttons;
	host_client->private_latched_buttons |= readcmd.buttons & 3; // attack and jump
	if (readcmd.impulse)
		host_client->private_latched_impulse = readcmd.impulse;
	readcmd.buttons |= host_client->private_latched_buttons;
	if (!readcmd.impulse)
		readcmd.impulse = host_client->private_latched_impulse;

	host_client->lastmovemessage = sequence;
	host_client->lastmovetime = realtime;
	host_client->ping_times[host_client->num_pings % NUM_PING_TIMES] =
		qcvm->time - readcmd.servertime;
	host_client->num_pings++;
	host_client->cmd = readcmd;
	VectorCopy (readcmd.viewangles, host_client->edict->v.v_angle);
	host_client->edict->v.button0 = (readcmd.buttons & 1) != 0;
	host_client->edict->v.button2 = (readcmd.buttons & 2) != 0;
	SV_SetClientExtraButtons (host_client->edict, readcmd.buttons);
	if (readcmd.impulse)
		host_client->edict->v.impulse = readcmd.impulse;
	return true;
}

static qboolean SV_ClearPrivateInput (client_t *client)
{
	if (SV_PrivateWalkTrialSelected (client))
		return SV_PrivateWalkTrialFail (client, "selected input cannot be silently cleared");

	SV_DiscardPrivateCommandQueue (client, client->lastmovemessage);
	client->private_latest_buttons = 0;
	client->private_latched_buttons = 0;
	client->private_latched_impulse = 0;
	client->cmd.forwardmove = 0;
	client->cmd.sidemove = 0;
	client->cmd.upmove = 0;
	client->cmd.buttons = 0;
	client->cmd.impulse = 0;
	memset (client->cmd.vr_roomscalemove, 0, sizeof (client->cmd.vr_roomscalemove));
	memset (&client->cmd.vr_gorilla, 0, sizeof (client->cmd.vr_gorilla));
	memset (&client->cmd.vr_gorilla_motion, 0, sizeof (client->cmd.vr_gorilla_motion));
	client->edict->v.button0 = 0;
	client->edict->v.button2 = 0;
	SV_SetClientExtraButtons (client->edict, 0);
	client->edict->v.impulse = 0;
	return true;
}

/* Called after SV_Physics: retire only through each owner's completed cursor,
 * then release one-frame latches. Public clients keep their existing lifetime. */
void SV_FinishPrivateUsercmds (void)
{
	int i;
	client_t *client;
	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		if (!client->active || !client->spawned ||
			client->protocol_qsvr != QSVR_PROTOCOL_PINNED)
			continue;
		SV_RetirePrivateCommandsThrough (client, client->private_completed_move);
		if (SV_PrivateWalkTrialSelected (client))
		{
			if (client->private_pmove_last_cmd_valid)
			{
				client->cmd = client->private_pmove_last_cmd;
				client->cmd.impulse = 0;
				memset (client->cmd.vr_roomscalemove, 0,
					sizeof (client->cmd.vr_roomscalemove));
				if (client->private_move_native_frame &&
					!SV_PrivateWalkTrialTerminalState (client))
				{
					/* Native coalescing used brief latches for this frame.
					 * Publish the latest levels as the ordinary private path does. */
					client->edict->v.button0 = (client->cmd.buttons & 1) != 0;
					client->edict->v.button2 = (client->cmd.buttons & 2) != 0;
					SV_SetClientExtraButtons (client->edict, client->cmd.buttons);
				}
			}
			continue;
		}
		client->private_latched_buttons = 0;
		client->private_latched_impulse = 0;
		client->cmd.buttons = client->private_latest_buttons;
		client->cmd.impulse = 0;
		memset (client->cmd.vr_roomscalemove, 0, sizeof (client->cmd.vr_roomscalemove));
		client->edict->v.button0 = (client->cmd.buttons & 1) != 0;
		client->edict->v.button2 = (client->cmd.buttons & 2) != 0;
		SV_SetClientExtraButtons (client->edict, client->cmd.buttons);
		client->edict->v.impulse = 0;
	}
}

/*
===================
SV_ReadClientMessage

Returns false if the client should be killed
===================
*/
static qboolean SV_AvatarCommandPrefix (const char *s, const char *name)
{
	while (*s == ' ' || *s == '\t')
		s++;
	return !q_strncasecmp (s, name, strlen (name));
}

static qboolean SV_AvatarCommandIs (const char *s, const char *name)
{
	size_t length = strlen (name);
	while (*s == ' ' || *s == '\t')
		s++;
	return !q_strncasecmp (s, name, length) && (unsigned char)s[length] <= ' ';
}

static qboolean SV_HandleAvatarCapability (const char *s)
{
	if (!SV_AvatarCommandPrefix (s, "avatar_cap"))
		return false;
	if (!SV_AvatarCommandIs (s, "avatar_cap") ||
		!PlayerAvatar_ParseCapabilityCommand (s) || host_client->avatar_capable)
		return true;
	host_client->avatar_capable = true;
	SV_SendAvatarTable (host_client);
	Con_DPrintf ("Avatar: client %s negotiated protocol %d\n", host_client->name,
		PLAYER_AVATAR_PROTOCOL_VERSION);
	return true;
}

static qboolean SV_HandleCustomAvatarCapability (const char *s)
{
	if (!SV_AvatarCommandPrefix (s, "avatar_custom_cap"))
		return false;
	if (!SV_AvatarCommandIs (s, "avatar_custom_cap") ||
		!host_client->avatar_capable ||
		!PlayerAvatar_ParseCustomCapabilityCommand (s) ||
		host_client->avatar_custom_capable)
		return true;
	host_client->avatar_custom_capable = true;
	SV_SendAvatarTable (host_client);
	Con_DPrintf ("Avatar: client %s negotiated custom protocol %d\n",
		host_client->name, PLAYER_AVATAR_CUSTOM_PROTOCOL_VERSION);
	return true;
}

static qboolean SV_HandleAvatarSet (const char *s)
{
	int avatar_id;
	qboolean had_custom;

	if (!SV_AvatarCommandPrefix (s, "avatar_set"))
		return false;
	if (!SV_AvatarCommandIs (s, "avatar_set") ||
		!host_client->avatar_capable ||
		!PlayerAvatar_ParseSetCommand (s, &avatar_id))
		return true;
	had_custom = host_client->avatar_custom_key[0] != 0;
	host_client->avatar_custom_key[0] = 0;
	host_client->avatar_custom_digest[0] = 0;
	if (host_client->avatar_id == avatar_id)
	{
		if (had_custom)
			SV_BroadcastAvatarSlot ((int)(host_client - svs.clients), avatar_id);
		return true;
	}
	host_client->avatar_id = (unsigned char)avatar_id;
	SV_BroadcastAvatarSlot ((int)(host_client - svs.clients), avatar_id);
	return true;
}

static qboolean SV_HandleCustomAvatarSet (const char *s)
{
	char key[PLAYER_AVATAR_CUSTOM_KEY_MAX + 1];
	char digest[PLAYER_AVATAR_CUSTOM_DIGEST_MAX + 1];

	if (!SV_AvatarCommandPrefix (s, "avatar_custom_set"))
		return false;
	if (!SV_AvatarCommandIs (s, "avatar_custom_set") ||
		!host_client->avatar_capable || !host_client->avatar_custom_capable ||
		!PlayerAvatar_ParseCustomSetCommand (s, key, sizeof (key), digest,
			sizeof (digest)))
		return true;
	if (host_client->avatar_id == PLAYER_AVATAR_RANGER &&
		!strcmp (host_client->avatar_custom_key, key) &&
		!strcmp (host_client->avatar_custom_digest, digest))
		return true;
	strcpy (host_client->avatar_custom_key, key);
	strcpy (host_client->avatar_custom_digest, digest);
	host_client->avatar_id = PLAYER_AVATAR_RANGER;
	SV_BroadcastAvatarSlot ((int)(host_client - svs.clients),
		PLAYER_AVATAR_RANGER);
	return true;
}

static qboolean SV_HandleVRIKCapability(const char *s)
{
    const char *value = s;
    int version;

    while (*value == ' ' || *value == '\t')
        value++;
    if (q_strncasecmp(value, "vrik_cap", 8) ||
        (value[8] && value[8] != ' ' && value[8] != '\t'))
        return false;
    value += 8;
    while (*value == ' ' || *value == '\t')
        value++;
    if (*value == '4')
        version = VRIK_ADMISSION_PROTOCOL_VERSION;
    else if (*value == '3')
        version = VRIK_PROTOCOL_VERSION;
    else if (*value == '2')
        version = VRIK_PROTOCOL_LEGACY_VERSION;
    else
        return true;
    value++;
    while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
        value++;
    if (*value || host_client->vrik_capable)
        return true;
    {
        int latched = host_client->vrik_capable;
        uint8_t latched_version = host_client->vrik_protocol_version;
        if (vrik_latch_protocol_version((uint8_t)version, &latched,
            &latched_version) != VRIK_CODEC_OK)
            return true;
        host_client->vrik_capable = latched;
        host_client->vrik_protocol_version = latched_version;
    }
    host_client->vrik_sequence_valid = false;
    host_client->vrik_inactive_sent = false;
    host_client->vrik_last_sequence = 0;
    host_client->vrik_generation = 0;
    host_client->vrik_pose_time = 0;
    host_client->vrik_next_accept_time = 0;
    Con_DPrintf("VRIK: client %s negotiated protocol %d\n", host_client->name,
        version);
    return true;
}

static qboolean SV_HandleGorillaCapability (const char *s)
{
	static const char command[] = "vr_gorilla_cap";
	const char *value = s;

	while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
		value++;
	if (q_strncasecmp (value, command, sizeof (command) - 1) ||
		(value[sizeof (command) - 1] &&
		 value[sizeof (command) - 1] != ' ' &&
		 value[sizeof (command) - 1] != '\t' &&
		 value[sizeof (command) - 1] != '\r' &&
		 value[sizeof (command) - 1] != '\n'))
		return false;

	value += sizeof (command) - 1;
	while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
		value++;
	if (*value != '1')
		return true;
	value++;
	while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
		value++;
	if (*value || !host_client->spawned ||
		host_client->protocol_qsvr != QSVR_PROTOCOL_PINNED)
		return true;

	host_client->vr_gorilla_capable = true;
	return true;
}

static qboolean SV_HandleInstantStopCapability (const char *s)
{
	static const char command[] = "vr_instant_stop_cap";
	const size_t length = sizeof (command) - 1;

	if (strncmp (s, command, length) ||
		(s[length] && s[length] != ' ' && s[length] != '\t' &&
		 s[length] != '\r' && s[length] != '\n'))
		return false;
	/* Reserve the command before the mod hook, but latch only the exact reply. */
	if (!strcmp (s, "vr_instant_stop_cap 1") && host_client->active &&
		host_client->spawned &&
		host_client->protocol_qsvr == QSVR_PROTOCOL_PINNED &&
		SV_PrivateWalkTrialSelected (host_client) &&
		host_client->vr_instant_stop_offered)
		host_client->vr_instant_stop_capable = true;
	return true;
}

static qboolean SV_HandleVoiceCapability (const char *s)
{
	const char *value = s;
	int source_slot;

	while (*value == ' ' || *value == '\t')
		value++;
	if (q_strncasecmp (value, "voice_cap", 9) ||
		(value[9] && value[9] != ' ' && value[9] != '\t'))
		return false;
	value += 9;
	while (*value == ' ' || *value == '\t')
		value++;
	if (*value++ != '1')
		return true;
	while (*value == ' ' || *value == '\t' || *value == '\r' ||
		*value == '\n')
		value++;
	if (*value || host_client->voice_capable ||
		!host_client->voice_protocol_offered)
		return true;

	host_client->voice_capable = true;
	/* Do not replay packets received before this recipient opted in. */
	for (source_slot = 0; source_slot < svs.maxclients &&
		source_slot < MAX_SCOREBOARD; ++source_slot)
	{
		host_client->voice_relay_generation[source_slot] =
			svs.clients[source_slot].voice_generation;
		host_client->voice_relay_serial[source_slot] =
			svs.clients[source_slot].voice_next_serial;
	}
	Con_DPrintf ("Voice: client %s negotiated protocol %d\n",
		host_client->name, VOICE_PROTOCOL_VERSION);
	return true;
}

static qboolean SV_ReadVoicePacket (qboolean accept)
{
	voice_packet_t packet;
	unsigned int payload_bytes;

	memset (&packet, 0, sizeof (packet));
	packet.sequence = (uint16_t)MSG_ReadShort ();
	packet.timestamp = (uint32_t)MSG_ReadLong ();
	packet.talkspurt = (uint8_t)MSG_ReadByte ();
	packet.flags = (uint8_t)MSG_ReadByte ();
	payload_bytes = (uint16_t)MSG_ReadShort ();
	if (msg_badread)
		return false;
	if (net_message.cursize - msg_readcount < (int)payload_bytes)
	{
		msg_badread = true;
		return false;
	}
	if (payload_bytes <= VOICE_MAX_PAYLOAD)
		memcpy (packet.payload, net_message.data + msg_readcount,
			payload_bytes);
	msg_readcount += payload_bytes;
	packet.payload_bytes = (uint16_t)payload_bytes;
	if (!accept || !Voice_PacketIsValid (&packet))
		return true;
	SV_ReceiveVoicePacket (host_client, &packet);
	return true;
}

static qboolean SV_ReadVRIKPose(qboolean accept)
{
    vrik_v2_pose_t pose_v2;
    vrik_codec_pose_t pose_v3;
    vrik_codec_status_t status;
    size_t consumed;
    int body_bytes;

    if (!host_client->vrik_capable)
        return false;
    if (host_client->vrik_protocol_version >= VRIK_PROTOCOL_VERSION)
    {
        body_bytes = MSG_ReadByte();
        if (msg_badread || body_bytes < 0 ||
            body_bytes > VRIK_V3_MAX_BODY_BYTES ||
            net_message.cursize - msg_readcount < body_bytes)
        {
            msg_badread = true;
            return false;
        }
        if (!accept || !host_client->spawned ||
            realtime < host_client->vrik_next_accept_time)
        {
            msg_readcount += body_bytes;
            return true;
        }
        host_client->vrik_next_accept_time = realtime + VRIK_SERVER_MIN_INTERVAL;
        status = vrik_v3_decode(net_message.data + msg_readcount,
            (size_t)body_bytes, &pose_v3, &consumed);
        msg_readcount += body_bytes;
        if (status != VRIK_CODEC_OK || consumed != (size_t)body_bytes)
            return true;
        if (accept && host_client->spawned)
            SV_ReceiveVRIKPoseV3(host_client, &pose_v3);
        return true;
    }

    if (host_client->vrik_protocol_version != VRIK_PROTOCOL_LEGACY_VERSION)
        return false;
    if (net_message.cursize - msg_readcount < VRIK_POSE_WIRE_BYTES)
    {
        msg_badread = true;
        return false;
    }
    if (!accept || !host_client->spawned ||
        realtime < host_client->vrik_next_accept_time)
    {
        msg_readcount += VRIK_POSE_WIRE_BYTES;
        return true;
    }
    host_client->vrik_next_accept_time = realtime + VRIK_SERVER_MIN_INTERVAL;
    status = vrik_v2_decode(net_message.data + msg_readcount,
        VRIK_POSE_WIRE_BYTES, &pose_v2, &consumed);
    msg_readcount += VRIK_POSE_WIRE_BYTES;
    if (status != VRIK_CODEC_OK || consumed != VRIK_POSE_WIRE_BYTES ||
        vrik_v2_validate_legacy_pose(&pose_v2) != VRIK_CODEC_OK)
        return true;
    if (accept && host_client->spawned)
        SV_ReceiveVRIKPoseV2(host_client, &pose_v2,
            net_message.data + msg_readcount - VRIK_POSE_WIRE_BYTES);
    return true;
}

static qboolean SV_ReadQCRequest (void)
{
	char args[8], eventname[MSG_READSTRING_SIZE];
	char funcname[MSG_READSTRING_SIZE + sizeof (args) + 7];
	int count = 0, start;
	dfunction_t *func;
	client_t *requester = host_client;

	// The VM copies declared parameters, even for a request with no arguments.
	memset (&qcvm->globals[OFS_PARM0], 0, MAX_PARMS * 3 * sizeof (qcvm->globals[0]));
	for (;;)
	{
		int type = MSG_ReadByte ();
		int parm = OFS_PARM0 + count * 3;
		if (msg_badread)
			return false;
		if (type == ev_void)
			break;
		if (count >= sizeof (args) - 1)
		{
			msg_badread = true;
			return false;
		}
		switch (type)
		{
		case ev_float:
			args[count] = 'f';
			G_FLOAT (parm) = MSG_ReadFloat ();
			break;
		case ev_vector:
			args[count] = 'v';
			for (int i = 0; i < 3; ++i)
				G_FLOAT (parm + i) = MSG_ReadFloat ();
			break;
		case ev_ext_integer:
			args[count] = 'i';
			G_INT (parm) = MSG_ReadLong ();
			break;
		case ev_string:
			args[count] = 's';
			G_INT (parm) = PR_MakeTempString (MSG_ReadString ());
			break;
		case ev_entity:
		{
			unsigned int number = MSG_ReadEntity (requester->protocol_pext2);
			args[count] = 'e';
			if (number >= (unsigned int)qcvm->num_edicts)
				number = 0;
			/* A delayed request may name a freed slot; retain its numbered byte offset. */
			G_INT (parm) = (int)((byte *)EDICT_NUM (number) - (byte *)qcvm->edicts);
			break;
		}
		default:
			msg_badread = true;
			return false;
		}
		if (msg_badread)
			return false;
		++count;
	}
	args[count] = 0;
	start = msg_readcount;
	MSG_ReadStringBuffer (eventname, sizeof (eventname));
	if (msg_badread || (size_t)(msg_readcount - start) != strlen (eventname) + 1)
	{
		msg_badread = true;
		return false;
	}
	if (count)
		q_snprintf (funcname, sizeof (funcname), "CSEv_%s_%s", eventname, args);
	else
		q_snprintf (funcname, sizeof (funcname), "CSEv_%s", eventname);
	func = ED_FindFunction (funcname);
	if (!func || func->first_statement <= 0)
	{
		SV_ClientPrintf ("qcrequest \"%s\" not supported\n", funcname);
		return true;
	}

	int saved_argc = qcvm->argc;
	pr_global_struct->time = qcvm->time;
	pr_global_struct->self = EDICT_TO_PROG (requester->edict);
	qcvm->argc = count;
	PR_ExecuteProgram (func - qcvm->functions);
	qcvm->argc = saved_argc;
	host_client = requester;
	return true;
}

qboolean SV_ReadClientMessage (void)
{
	int			ccmd;
    const char *s;
    int vrikcommands = 0;
    int voicecommands = 0;

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
			if (msg_badread)
				return false;
			if (SV_HandlePrivateResumeMarker (s))
				break;
			if (SV_HandleAvatarCapability (s) ||
				SV_HandleCustomAvatarCapability (s) ||
				SV_HandleAvatarSet (s) ||
				SV_HandleCustomAvatarSet (s))
				break;
			if (SV_HandleGorillaCapability (s))
				break;
			if (SV_HandleInstantStopCapability (s))
				break;
			if (SV_HandleVoiceCapability (s))
				break;
			if (SV_HandleVRIKCapability (s))
				break;
			// The engine must see its protocol offer before a mod's client-command
			// hook can consume it. Keep other client strings on their existing path.
			const qboolean pext_offer = !q_strncasecmp (s, "pext", 4) &&
				(s[4] == '\0' || s[4] == ' ' || s[4] == '\t');
			client_t *requester = host_client;
			struct qsocket_s *socket = requester->netconnection;
			if (!pext_offer && q_strncasecmp (s, "spawn", 5) && q_strncasecmp (s, "begin", 5) && q_strncasecmp (s, "prespawn", 8) && qcvm->extfuncs.SV_ParseClientCommand)
			{ // the spawn/begin/prespawn are because of numerous mods that disobey the rules.
				// at a minimum, we must be able to join the server, so that we can see any sprints/bprints (because dprint sucks, yes there's proper ways
				// to deal with this, but moders don't always know them).
				G_INT (OFS_PARM0) = PR_SetEngineString (s);
				pr_global_struct->time = qcvm->time;
				pr_global_struct->self = EDICT_TO_PROG (host_client->edict);
				PR_ExecuteProgram (qcvm->extfuncs.SV_ParseClientCommand);
			}
			else
				Cmd_ExecuteString (s, src_client);
			host_client = requester;
			/* Match clcfte_qcrequest: end the old message without a second drop. */
			if (!requester->active || requester->netconnection != socket)
				return true;
			break;
		}

		case clc_disconnect:
			//			Sys_Printf ("SV_ReadClientMessage: client disconnected\n");
			return false;

		case clc_move:
			if (!host_client->spawned)
				return true; // this is to suck up any stale moves on map changes, so we don't get confused (quite so easily) when protocols are changed
							 // between maps
			if (host_client->protocol_qsvr == QSVR_PROTOCOL_PINNED)
			{
				if (!SV_ReadPrivateClientMove ())
					return false;
			}
			else
				SV_ReadClientMove (&host_client->cmd);
			break;

		case clc_vrikpose:
			/* A client may send its first unreliable pose before the reliable
			 * vrik_cap reply arrives. Its framing is unknown until admission:
			 * discard the remainder of this datagram, not the connection. */
			if (!host_client->vrik_capable)
				return true;
			/* Consume extra bodies to preserve packet alignment, but accept at
			 * most one pose from any one datagram. */
			if (!SV_ReadVRIKPose (vrikcommands++ == 0))
				return false;
			break;

		case clc_voice:
			{
				qboolean accept = host_client->voice_capable &&
					voicecommands < VOICE_SERVER_MAX_PACKETS_PER_DATAGRAM;
				voicecommands++;
				if (!SV_ReadVoicePacket (accept))
					return false;
			}
			break;

		case clcdp_ackframe:
			SVFTE_Ack (host_client, MSG_ReadLong ());
			break;

		case clcfte_qcrequest:
		{
			client_t *requester = host_client;
			struct qsocket_s *socket = requester->netconnection;
			if (!SV_ReadQCRequest ())
				return false;
			if (!requester->active || requester->netconnection != socket)
				return true; // QC already retired/replaced this client; don't drop it twice.
			break;
		}
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
		SV_PrivateSyncPauseState (host_client);

		if (!host_client->spawned)
		{
			// clear client movement until a new packet is received
			memset (&host_client->cmd, 0, sizeof (host_client->cmd));
			if (host_client->protocol_qsvr == QSVR_PROTOCOL_PINNED &&
				!SV_PrivateWalkTrialSelected (host_client) &&
				!SV_ClearPrivateInput (host_client))
			{
				SV_DropClient (false);
				continue;
			}
			continue;
		}

		if (!host_client->netconnection)
		{
			host_client->cmd.viewangles[0] = host_client->edict->v.v_angle[0];
			host_client->cmd.viewangles[1] = host_client->edict->v.v_angle[1];
			host_client->cmd.viewangles[2] = host_client->edict->v.v_angle[2];
		}
		if (host_client->protocol_qsvr == QSVR_PROTOCOL_PINNED &&
			!SV_PrivateWalkTrialSelected (host_client) &&
			(sv.paused || (svs.maxclients <= 1 && key_dest != key_game) ||
			 (host_client->lastmovetime > 0 &&
			  realtime - host_client->lastmovetime > 1.0)))
		{
			if (!SV_ClearPrivateInput (host_client))
			{
				SV_DropClient (false);
				continue;
			}
		}

		if (SV_PrivateWalkTrialSelected (host_client))
			continue;

		// always pause in single player if in console or menus
		if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game))
			SV_ClientThink ();
	}
}
