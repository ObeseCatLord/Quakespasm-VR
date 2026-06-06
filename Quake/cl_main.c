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
// cl_main.c  -- client main loop

#include "quakedef.h"
#include "bgmusic.h"
#include "vr.h"
#include "debug_log.h"
#include "pmove.h"

// we need to declare some mouse variables here, because the menu system
// references them even when on a unix system.

// these two are not intended to be set directly
cvar_t	cl_name = {"_cl_name", "player", CVAR_ARCHIVE};
cvar_t	cl_color = {"_cl_color", "0", CVAR_ARCHIVE};

cvar_t	cl_shownet = {"cl_shownet","0",CVAR_NONE};	// can be 0, 1, or 2
cvar_t	cl_nolerp = {"cl_nolerp","0",CVAR_NONE};
cvar_t	cl_lerpdebug = {"cl_lerpdebug","0",CVAR_NONE};
cvar_t	cl_lerpdebug_models = {"cl_lerpdebug_models","",CVAR_NONE};
cvar_t	cl_beams_polygons = {"cl_beams_polygons","0",CVAR_ARCHIVE};
// Max seconds of linear extrapolation past the latest server snapshot.
// At sys_ticrate 0.05 (20 Hz) a fast client renders many frames between
// snapshots and would otherwise freeze entities at frac=1 until the next
// packet, producing visible 20 Hz judder. 0 reproduces the legacy clamp.
cvar_t	cl_extrapolate = {"cl_extrapolate","0.05",CVAR_ARCHIVE};
cvar_t	cl_extrapolate_adaptive = {"cl_extrapolate_adaptive","0",CVAR_ARCHIVE};
cvar_t	cl_extrapolate_adaptive_max = {"cl_extrapolate_adaptive_max","0.12",CVAR_ARCHIVE};
cvar_t	cl_extrapolate_adaptive_time = {"cl_extrapolate_adaptive_time","0.75",CVAR_NONE};
cvar_t	cl_net_lerpbuffer = {"cl_net_lerpbuffer","0.025",CVAR_ARCHIVE};
cvar_t	cl_predict_smooth = {"cl_predict_smooth","1",CVAR_ARCHIVE};
cvar_t	cl_predict_smooth_time = {"cl_predict_smooth_time","0.08",CVAR_ARCHIVE};
cvar_t	cl_predict_smooth_min = {"cl_predict_smooth_min","0.5",CVAR_NONE};
cvar_t	cl_predict_smooth_max = {"cl_predict_smooth_max","64",CVAR_NONE};
cvar_t	cl_predict_error_log = {"cl_predict_error_log","1",CVAR_NONE};

cvar_t	cfg_unbindall = {"cfg_unbindall", "1", CVAR_ARCHIVE};

extern cvar_t sv_accelerate;
extern cvar_t sv_friction;
extern cvar_t sv_gravity;
extern cvar_t sv_maxspeed;
extern cvar_t sv_stopspeed;
extern cvar_t sv_vr_jump_velocity;
extern cvar_t vr_movement_instant_stop;

cvar_t	freelook = {"freelook","1", CVAR_ARCHIVE};
cvar_t	lookspring = {"lookspring","0", CVAR_ARCHIVE};
cvar_t	lookstrafe = {"lookstrafe","0", CVAR_ARCHIVE};
cvar_t	sensitivity = {"sensitivity","3", CVAR_ARCHIVE};

cvar_t	m_pitch = {"m_pitch","0.022", CVAR_ARCHIVE};
cvar_t	m_yaw = {"m_yaw","0.022", CVAR_ARCHIVE};
cvar_t	m_forward = {"m_forward","1", CVAR_ARCHIVE};
cvar_t	m_side = {"m_side","0.8", CVAR_ARCHIVE};

cvar_t	cl_maxpitch = {"cl_maxpitch", "90", CVAR_ARCHIVE}; //johnfitz -- variable pitch clamping
cvar_t	cl_minpitch = {"cl_minpitch", "-90", CVAR_ARCHIVE}; //johnfitz -- variable pitch clamping

cvar_t	cl_mwheelpitch = {"cl_mwheelpitch", "5", CVAR_ARCHIVE};

cvar_t	cl_startdemos = {"cl_startdemos", "1", CVAR_ARCHIVE};
cvar_t	cl_confirmquit = {"cl_confirmquit", "0", CVAR_ARCHIVE};

cvar_t	cl_mousemenu = {"cl_mousemenu", "1", CVAR_ARCHIVE};

client_static_t	cls;
client_state_t	cl;
// FIXME: put these on hunk?
entity_t		cl_static_entities[MAX_STATIC_ENTITIES];
lightstyle_t	cl_lightstyle[MAX_LIGHTSTYLES];
dlight_t		cl_dlights[MAX_DLIGHTS];

int				cl_numvisedicts;
entity_t		*cl_visedicts[MAX_VISEDICTS];

extern cvar_t	r_lerpmodels, r_lerpmove; //johnfitz
extern float	host_netinterval;	//Spike

extern vec3_t	v_punchangles[2];

#ifdef PSET_SCRIPT
void CL_ClearTrailStates(void)
{
	int i;

	for (i = 0; i < cl.num_statics; i++)
	{
		PScript_DelinkTrailstate (&cl_static_entities[i].trailstate);
		PScript_DelinkTrailstate (&cl_static_entities[i].emitstate);
	}
	if (cl.entities)
	{
		for (i = 0; i < cl.max_edicts; i++)
		{
			PScript_DelinkTrailstate (&cl.entities[i].trailstate);
			PScript_DelinkTrailstate (&cl.entities[i].emitstate);
		}
	}
	for (i = 0; i < MAX_BEAMS; i++)
		PScript_DelinkTrailstate (&cl_beams[i].trailstate);
}
#endif

void CL_FreeState(void)
{
	int i;
	for (i = 0; i < MAX_CL_STATS; i++)
		free (cl.statss[i]);
#ifdef PSET_SCRIPT
	CL_ClearTrailStates ();
#endif
	cl.entities = NULL;
	cl.max_edicts = 0;
	cl_numvisedicts = 0;
	PR_ClearProgs (&cl.qcvm);
	free (cl.ssqc_to_csqc);
	memset (&cl, 0, sizeof(cl));
}

/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState (void)
{
	VR_ResetWeaponTracking();
	CL_ClearPendingCmd();
	cls.trusted_clientmove_allowed = false;
	cls.moveext_allowed = false;

	if (cl.qcvm.extfuncs.CSQC_Shutdown)
	{
		PR_SwitchQCVM(&cl.qcvm);
		PR_ExecuteProgram(qcvm->extfuncs.CSQC_Shutdown);
		qcvm->extfuncs.CSQC_Shutdown = 0;
		PR_SwitchQCVM(NULL);
	}

	if (!sv.active)
		Host_ClearMemory ();

// wipe the entire cl structure
	CL_FreeState ();

	SZ_Clear (&cls.message);

// clear other arrays
	memset (cl_dlights, 0, sizeof(cl_dlights));
	memset (cl_lightstyle, 0, sizeof(cl_lightstyle));
	memset (cl_temp_entities, 0, sizeof(cl_temp_entities));
	memset (cl_beams, 0, sizeof(cl_beams));

	//johnfitz -- cl.entities is now dynamically allocated
	cl.max_edicts = CLAMP (MIN_EDICTS,(int)max_edicts.value,MAX_EDICTS);
	cl.entities = (entity_t *) Hunk_AllocName (cl.max_edicts*sizeof(entity_t), "cl_entities");
	//johnfitz

	memset (v_punchangles, 0, sizeof (v_punchangles));
	cl.ackedmovemessages = -1;
}

/*
=====================
CL_Disconnect

Sends a disconnect message to the server
This is also called on Host_Error, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect (void)
{
	DebugLog("CL_Disconnect: state=%d signon=%d\n", cls.state, cls.signon);
	CL_ClearPendingCmd();
	cls.trusted_clientmove_allowed = false;
	cls.moveext_allowed = false;

	if (key_dest == key_message)
		Key_EndChat ();	// don't get stuck in chat mode

// stop sounds (especially looping!)
	S_StopAllSounds (true);
	BGM_Stop();
	CDAudio_Stop();

// if running a local server, shut it down
	if (cls.demoplayback)
		CL_StopPlayback ();
	else if (cls.state == ca_connected)
	{
		if (cls.demorecording)
			CL_Stop_f ();

		Con_DPrintf ("Sending clc_disconnect\n");
		SZ_Clear (&cls.message);
		MSG_WriteByte (&cls.message, clc_disconnect);
		NET_SendUnreliableMessage (cls.netcon, &cls.message);
		SZ_Clear (&cls.message);
		NET_Close (cls.netcon);

		cls.state = ca_disconnected;
		if (sv.active)
			Host_ShutdownServer(false);
	}

	cls.demoplayback = cls.timedemo = false;
	cls.demopaused = false;
	cl.intermission = 0;
	cl.sendprespawn = false;
	CL_ClearSignons ();

	V_ResetEffects ();
}

static void CL_TrustedClientMoveAck_f (void)
{
	cls.trusted_clientmove_allowed = true;
	if (net_lagdebug.value)
		Con_Printf ("net_lagdebug: server enabled trusted co-op client movement\n");
}

static void CL_MoveExtAck_f (void)
{
	cls.moveext_allowed = true;
	if (net_lagdebug.value)
		Con_Printf ("net_lagdebug: server enabled sequenced co-op client movement\n");
}

void CL_Disconnect_f (void)
{
	CL_Disconnect ();
	if (sv.active)
		Host_ShutdownServer (false);
}


/*
=====================
CL_EstablishConnection

Host should be either "local" or a net address to be passed on
=====================
*/
void CL_EstablishConnection (const char *host)
{
	if (cls.state == ca_dedicated)
		return;

	if (cls.demoplayback)
		return;

	CL_Disconnect ();

	cls.netcon = NET_Connect (host);
	if (!cls.netcon)
		Host_Error ("CL_Connect: connect failed");
	Con_DPrintf ("CL_EstablishConnection: connected to %s\n", host);

	cls.demonum = -1;			// not in the demo loop now
	cls.state = ca_connected;
	CL_ClearSignons ();			// need all the signon messages before playing
	MSG_WriteByte (&cls.message, clc_nop);	// NAT Fix from ProQuake
}

/*
=====================
CL_SignonReply

An svc_signonnum has been received, perform a client side setup
=====================
*/
void CL_SignonReply (void)
{
	char 	str[8192];

	DebugLog ("CL_SignonReply: signon=%d\n", cls.signon);
	Con_DPrintf ("CL_SignonReply: %i\n", cls.signon);

	switch (cls.signon)
	{
	case 1:
		cl.sendprespawn = true;
		break;

	case 2:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va("name \"%s\"\n", cl_name.string));

		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va("color %i %i\n", ((int)cl_color.value)>>4, ((int)cl_color.value)&15));

		MSG_WriteByte (&cls.message, clc_stringcmd);
		sprintf (str, "spawn %s", cls.spawnparms);
		MSG_WriteString (&cls.message, str);
		break;

	case 3:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "begin");
		Cache_Report ();		// print remaining memory
		break;

	case 4:
		SCR_EndLoadingPlaque ();		// allow normal screen updates
		break;
	}
}

/*
=====================
CL_NextDemo

Called to play the next demo in the demo loop
=====================
*/
void CL_NextDemo (void)
{
	char	str[1024];

	if (cls.demonum == -1)
		return;		// don't play demos

	if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS)
	{
		cls.demonum = 0;
		if (!cls.demos[cls.demonum][0])
		{
			Con_Printf ("No demos listed with startdemos\n");
			cls.demonum = -1;
			CL_Disconnect();
			return;
		}
	}

	SCR_BeginLoadingPlaque ();

	sprintf (str,"playdemo %s 1\n", cls.demos[cls.demonum]);
	Cbuf_InsertText (str);
	cls.demonum++;
}

/*
==============
CL_PrintEntities_f
==============
*/
void CL_PrintEntities_f (void)
{
	entity_t	*ent;
	int			i;

	if (cls.state != ca_connected)
		return;

	for (i=0,ent=cl.entities ; i<cl.num_entities ; i++,ent++)
	{
		Con_Printf ("%3i:",i);
		if (!ent->model)
		{
			Con_Printf ("EMPTY\n");
			continue;
		}
		Con_Printf ("%s:%2i  (%5.1f,%5.1f,%5.1f) [%5.1f %5.1f %5.1f]\n"
		,ent->model->name,ent->frame, ent->origin[0], ent->origin[1], ent->origin[2], ent->angles[0], ent->angles[1], ent->angles[2]);
	}
}

/*
===============
CL_AllocDlight

===============
*/
dlight_t *CL_AllocDlight (int key)
{
	int		i;
	dlight_t	*dl;

// first look for an exact key match
	if (key)
	{
		dl = cl_dlights;
		for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
		{
			if (dl->key == key)
			{
				memset (dl, 0, sizeof(*dl));
				dl->key = key;
				dl->color[0] = dl->color[1] = dl->color[2] = 1; //johnfitz -- lit support via lordhavoc
				return dl;
			}
		}
	}

// then look for anything else
	dl = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (dl->die < cl.time)
		{
			memset (dl, 0, sizeof(*dl));
			dl->key = key;
			dl->color[0] = dl->color[1] = dl->color[2] = 1; //johnfitz -- lit support via lordhavoc
			return dl;
		}
	}

	dl = &cl_dlights[0];
	memset (dl, 0, sizeof(*dl));
	dl->key = key;
	dl->color[0] = dl->color[1] = dl->color[2] = 1; //johnfitz -- lit support via lordhavoc
	return dl;
}


/*
===============
CL_DecayLights

===============
*/
void CL_DecayLights (void)
{
	int			i;
	dlight_t	*dl;
	float		time;

	time = cl.time - cl.oldtime;

	dl = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (dl->die < cl.time || !dl->radius)
			continue;

		dl->radius -= time*dl->decay;
		if (dl->radius < 0)
			dl->radius = 0;
	}
}


/*
===============
CL_LerpPoint

Determines the fraction between the last two messages that the objects
should be put at.
===============
*/
float	CL_LerpPoint (void)
{
	float	f, frac, lerptime, lerpbuffer;
	static double	last_lerp_log;

	f = cl.mtime[0] - cl.mtime[1];
	lerpbuffer = CLAMP (0.0f, cl_net_lerpbuffer.value, 0.2f);
	lerptime = cl.time - lerpbuffer;

	if (!f || cls.timedemo || (sv.active && !host_netinterval))
	{
		if (!f && !cls.timedemo && !sv.active && net_lagdebug.value &&
			cls.state == ca_connected && cls.signon == SIGNONS &&
			cl.mtime[0] > 0 && realtime - last_lerp_log > 0.5)
		{
			Con_Printf ("net_lagdebug: client interpolation collapsed mtime=%.3f cl_time=%.3f lastmsg_age=%.3f\n",
				cl.mtime[0], lerptime, realtime - cl.last_received_message);
			last_lerp_log = realtime;
		}
		cl.time = cl.mtime[0];
		return 1;
	}

	if (f > 0.1) // dropped packet, or start of demo
	{
		if (net_lagdebug.value && cls.state == ca_connected && cls.signon == SIGNONS &&
			realtime - last_lerp_log > 0.5)
		{
			Con_Printf ("net_lagdebug: client snapshot gap mtime_delta=%.3f cl_time=%.3f lastmsg_age=%.3f\n",
				f, lerptime, realtime - cl.last_received_message);
			last_lerp_log = realtime;
		}
		cl.mtime[1] = cl.mtime[0] - 0.1;
		f = 0.1;
	}

	frac = (lerptime - cl.mtime[1]) / f;

	if (frac < 0)
	{
		if (frac < -0.01)
			cl.time = cl.mtime[1] + lerpbuffer;
		frac = 0;
	}
	else if (frac > 1)
	{
		float maxextrap = cl_extrapolate.value;

		if (cl_extrapolate_adaptive.value &&
			(lerptime - cl.mtime[0] > net_lagdebug_frame_threshold.value ||
			 realtime < cl.net_snapshot_smooth_until))
		{
			cl.net_snapshot_smooth_until = realtime +
				q_max (0.0, cl_extrapolate_adaptive_time.value);
			if (cl_extrapolate_adaptive_max.value > maxextrap)
				maxextrap = cl_extrapolate_adaptive_max.value;
		}
		if (net_lagdebug.value && cls.state == ca_connected && cls.signon == SIGNONS &&
			lerptime - cl.mtime[0] > net_lagdebug_frame_threshold.value &&
			realtime - last_lerp_log > 0.5)
		{
			cl.net_snapshot_interpolation_overruns++;
			Con_Printf ("net_lagdebug: client interpolation overrun over=%.3f frac=%.3f mtime_delta=%.3f maxextrap=%.3f lerpbuffer=%.3f lastmsg_age=%.3f\n",
				lerptime - cl.mtime[0], frac, f, maxextrap, lerpbuffer,
				realtime - cl.last_received_message);
			last_lerp_log = realtime;
		}
		// Bounded linear extrapolation past mtime[0]: instead of freezing
		// entities at the latest snapshot, allow up to cl_extrapolate
		// seconds of forward projection using the last inter-snapshot delta.
		// Eliminates the "freeze then snap" stutter when the render rate
		// exceeds the server tick rate. A small cap keeps overshoot bounded
		// when entities suddenly stop or change direction.
		if (maxextrap > 0 && (lerptime - cl.mtime[0]) <= maxextrap)
		{
			float maxfrac = 1.0f + (maxextrap / f);
			if (frac > maxfrac)
			{
				cl.time = cl.mtime[0] + maxextrap + lerpbuffer;
				frac = maxfrac;
			}
		}
		else
		{
			if (frac > 1.01)
				cl.time = cl.mtime[0] + lerpbuffer;
			frac = 1;
		}
	}

	//johnfitz -- better nolerp behavior
	if (cl_nolerp.value)
		return 1;
	//johnfitz

	return frac;
}

/*
===============
CL_ResetTrail
===============
*/
static void CL_ResetTrail (entity_t *ent)
{
	ent->traildelay = 1.f / 72.f;
	VectorCopy (ent->origin, ent->trailorg);
}

int CL_ParticleEffectColor (int effectnum)
{
	const char *name;

	if (effectnum <= 0 || effectnum >= MAX_PARTICLETYPES)
		return 0;

#ifdef PSET_SCRIPT
	name = cl.particle_precache[effectnum].name;
	if (!name)
		return 0;
#else
	name = cl.particle_precache[effectnum];
#endif
	if (q_strcasestr (name, "blood") || q_strcasestr (name, "gib"))
		return 67;
	if (q_strcasestr (name, "poison") || q_strcasestr (name, "acid") ||
		q_strcasestr (name, "slime"))
		return 68;
	if (q_strcasestr (name, "plasma") || q_strcasestr (name, "laser") ||
		q_strcasestr (name, "bolt"))
		return 224;
	if (q_strcasestr (name, "fire") || q_strcasestr (name, "flame") ||
		q_strcasestr (name, "pyro") || q_strcasestr (name, "rocket") ||
		q_strcasestr (name, "explode"))
		return 230;
	if (q_strcasestr (name, "smoke"))
		return 4;
	if (q_strcasestr (name, "snow"))
		return 15;
	if (q_strcasestr (name, "rain") || q_strcasestr (name, "water"))
		return 73;
	if (q_strcasestr (name, "secret"))
		return 150;

	return (effectnum * 13) & 255;
}

void CL_RunNamedParticleEffect (int effectnum, vec3_t org, vec3_t dir, int count)
{
	if (effectnum <= 0 || count <= 0)
		return;
#ifdef PSET_SCRIPT
	if (effectnum < MAX_PARTICLETYPES && cl.particle_precache[effectnum].name)
	{
		PScript_RunParticleEffectState (org, dir, count, cl.particle_precache[effectnum].index, NULL);
		return;
	}
#endif
	R_RunParticleEffect (org, dir, CL_ParticleEffectColor (effectnum), CLAMP (1, count, 255));
}

static void CL_NamedParticleTrail (entity_t *ent, int effectnum)
{
	vec3_t dir, org, step;
	float len;
	int samples, i;

	ent->traildelay -= cl.time - cl.oldtime;
	if (ent->traildelay > 0.f)
		return;

	VectorSubtract (ent->origin, ent->trailorg, dir);
	len = VectorNormalize (dir);
	if (len <= 0)
	{
		CL_ResetTrail (ent);
		return;
	}

	samples = CLAMP (1, (int)(len / 32.0f) + 1, 16);
	VectorScale (dir, len / samples, step);
	VectorCopy (ent->trailorg, org);
	for (i = 0; i < samples; i++)
	{
		CL_RunNamedParticleEffect (effectnum, org, dir, 1);
		VectorAdd (org, step, org);
	}
	CL_ResetTrail (ent);
}

/*
===============
CL_RocketTrail

Rate-limiting wrapper over R_RocketTrail
===============
*/
static void CL_RocketTrail (entity_t *ent, int type)
{
	ent->traildelay -= cl.time - cl.oldtime;
	if (ent->traildelay > 0.f)
		return;
	R_RocketTrail (ent->trailorg, ent->origin, type);
	CL_ResetTrail (ent);
}

/*
====================
CL_PredictWorldTrace

Runs the player-sized hull against the static world BSP. Dynamic brush entities
are still validated on the server through the trusted movement trace.
====================
*/
static qboolean CL_PredictWorldTrace (vec3_t start, vec3_t end, trace_t *trace)
{
	static const vec3_t default_player_mins = {-16, -16, -24};
	qmodel_t	*model;
	hull_t		*hull;
	vec3_t		offset;
	vec3_t		player_mins;
	vec3_t		start_l, end_l;

	memset (trace, 0, sizeof(*trace));
	trace->fraction = 1;
	trace->allsolid = true;
	VectorCopy (end, trace->endpos);

	model = cl.worldmodel;
	if (!model || model->type != mod_brush)
		return false;

	hull = &model->hulls[1];
	if (hull->firstclipnode > hull->lastclipnode)
		return false;

	if (cl.predstate_valid)
	{
		VectorCopy (cl.predstate_mins, player_mins);
	}
	else
	{
		VectorCopy (default_player_mins, player_mins);
	}

	VectorSubtract (hull->clip_mins, player_mins, offset);
	VectorSubtract (start, offset, start_l);
	VectorSubtract (end, offset, end_l);

	SV_RecursiveHullCheck (hull, hull->firstclipnode, 0, 1, start_l, end_l, trace);

	if (trace->fraction != 1)
		VectorAdd (trace->endpos, offset, trace->endpos);

	return true;
}

static void CL_PredictClipVelocity (vec3_t in, vec3_t normal, vec3_t out)
{
	float	backoff;
	float	change;
	int		i;

	backoff = DotProduct (in, normal);
	for (i = 0; i < 3; i++)
	{
		change = normal[i] * backoff;
		out[i] = in[i] - change;
		if (out[i] > -0.1f && out[i] < 0.1f)
			out[i] = 0;
	}
}

static qboolean CL_PredictGrounded (vec3_t origin)
{
	trace_t	trace;
	vec3_t	end;

	VectorCopy (origin, end);
	end[2] -= 1;
	if (!CL_PredictWorldTrace (origin, end, &trace))
		return false;
	return trace.fraction < 1 && trace.plane.normal[2] >= 0.7f;
}

static int CL_PredictSlideMove (vec3_t origin, vec3_t velocity, float frametime)
{
	int		bumpcount, numbumps;
	int		numplanes;
	int		i, j;
	int		blocked;
	float	d;
	float	time_left;
	vec3_t	end;
	vec3_t	dir;
	vec3_t	planes[5];
	vec3_t	primal_velocity, original_velocity;
	trace_t	trace;

	numbumps = 4;
	blocked = 0;
	numplanes = 0;
	time_left = frametime;
	VectorCopy (velocity, original_velocity);
	VectorCopy (velocity, primal_velocity);

	for (bumpcount = 0; bumpcount < numbumps; bumpcount++)
	{
		for (i = 0; i < 3; i++)
			end[i] = origin[i] + time_left * velocity[i];

		if (!CL_PredictWorldTrace (origin, end, &trace))
			return blocked;

		if (trace.startsolid || trace.allsolid)
		{
			VectorCopy (vec3_origin, velocity);
			return 3;
		}

		if (trace.fraction > 0)
		{
			VectorCopy (trace.endpos, origin);
			VectorCopy (velocity, original_velocity);
			numplanes = 0;
		}

		if (trace.fraction == 1)
			break;

		if (trace.plane.normal[2] >= 0.7f)
			blocked |= 1;
		else if (!trace.plane.normal[2])
			blocked |= 2;
		else
			blocked |= 4;

		time_left -= time_left * trace.fraction;
		if (numplanes >= 5)
		{
			VectorCopy (vec3_origin, velocity);
			break;
		}

		VectorCopy (trace.plane.normal, planes[numplanes]);
		numplanes++;

		for (i = 0; i < numplanes; i++)
		{
			CL_PredictClipVelocity (original_velocity, planes[i], velocity);
			for (j = 0; j < numplanes; j++)
				if (j != i && DotProduct (velocity, planes[j]) < 0)
					break;
			if (j == numplanes)
				break;
		}

		if (i == numplanes)
		{
			if (numplanes != 2)
			{
				VectorCopy (vec3_origin, velocity);
				break;
			}
			CrossProduct (planes[0], planes[1], dir);
			d = DotProduct (dir, velocity);
			VectorScale (dir, d, velocity);
		}

		if (DotProduct (velocity, primal_velocity) <= 0)
		{
			VectorCopy (vec3_origin, velocity);
			break;
		}
	}

	return blocked;
}

static void CL_PredictStepSlideMove (vec3_t origin, vec3_t velocity, float frametime)
{
	vec3_t	start_o, start_v;
	vec3_t	down_o, down_v;
	vec3_t	up, down;
	trace_t	trace;
	float	down_dist, up_dist;

	VectorCopy (origin, start_o);
	VectorCopy (velocity, start_v);

	CL_PredictSlideMove (origin, velocity, frametime);
	VectorCopy (origin, down_o);
	VectorCopy (velocity, down_v);

	VectorCopy (start_o, up);
	up[2] += 18;
	if (!CL_PredictWorldTrace (start_o, up, &trace) || trace.startsolid || trace.allsolid)
		return;
	VectorCopy (trace.endpos, up);

	VectorCopy (up, origin);
	VectorCopy (start_v, velocity);
	CL_PredictSlideMove (origin, velocity, frametime);

	VectorCopy (origin, down);
	down[2] -= 18;
	if (CL_PredictWorldTrace (origin, down, &trace) && !trace.allsolid)
	{
		VectorCopy (trace.endpos, origin);
		if (trace.fraction < 1 && trace.plane.normal[2] < 0.7f)
		{
			VectorCopy (down_o, origin);
			VectorCopy (down_v, velocity);
			return;
		}
	}

	down_dist = (down_o[0] - start_o[0]) * (down_o[0] - start_o[0]) +
				(down_o[1] - start_o[1]) * (down_o[1] - start_o[1]);
	up_dist = (origin[0] - start_o[0]) * (origin[0] - start_o[0]) +
			  (origin[1] - start_o[1]) * (origin[1] - start_o[1]);
	if (down_dist > up_dist)
	{
		VectorCopy (down_o, origin);
		VectorCopy (down_v, velocity);
	}
}

static void CL_PredictFriction (vec3_t velocity, float frametime)
{
	float	speed, newspeed, control;

	speed = sqrt (velocity[0] * velocity[0] + velocity[1] * velocity[1]);
	if (!speed)
		return;

	control = speed < sv_stopspeed.value ? sv_stopspeed.value : speed;
	newspeed = speed - frametime * control * sv_friction.value;
	if (newspeed < 0)
		newspeed = 0;
	newspeed /= speed;

	velocity[0] *= newspeed;
	velocity[1] *= newspeed;
	velocity[2] *= newspeed;
}

static void CL_PredictFriction3D (vec3_t velocity, float frametime)
{
	float	speed, newspeed;

	speed = VectorLength (velocity);
	if (!speed)
		return;

	newspeed = speed - frametime * speed * sv_friction.value;
	if (newspeed < 0)
		newspeed = 0;
	newspeed /= speed;

	VectorScale (velocity, newspeed, velocity);
}

static void CL_PredictAccelerate (vec3_t velocity, const vec3_t wishdir, float wishspeed, float frametime)
{
	int		i;
	float	addspeed, accelspeed, currentspeed;

	currentspeed = DotProduct (velocity, wishdir);
	addspeed = wishspeed - currentspeed;
	if (addspeed <= 0)
		return;

	accelspeed = sv_accelerate.value * frametime * wishspeed;
	if (accelspeed > addspeed)
		accelspeed = addspeed;

	for (i = 0; i < 3; i++)
		velocity[i] += accelspeed * wishdir[i];
}

static void CL_PredictAirAccelerate (vec3_t velocity, vec3_t wishvel, float wishspeed, float frametime)
{
	int		i;
	float	wishspd, currentspeed, addspeed, accelspeed;

	wishspd = VectorNormalize (wishvel);
	if (wishspd > 30)
		wishspd = 30;

	currentspeed = DotProduct (velocity, wishvel);
	addspeed = wishspd - currentspeed;
	if (addspeed <= 0)
		return;

	accelspeed = sv_accelerate.value * wishspeed * frametime;
	if (accelspeed > addspeed)
		accelspeed = addspeed;

	for (i = 0; i < 3; i++)
		velocity[i] += accelspeed * wishvel[i];
}

static qboolean CL_PredictRunCommand (const usercmd_t *cmd, vec3_t origin, vec3_t velocity, qboolean *jump_released)
{
	int		i;
	float	frametime;
	float	wishspeed;
	float	fmove, smove;
	qboolean	onground;
	vec3_t	angles;
	vec3_t	forward, right, up;
	vec3_t	wishvel, wishdir;

	frametime = cmd->seconds;
	if (frametime <= 0)
		return CL_PredictGrounded (origin);
	if (frametime > 0.1f)
		frametime = 0.1f;

	if (!(cmd->buttons & 2))
		*jump_released = true;

	onground = CL_PredictGrounded (origin);

	fmove = cmd->forwardmove;
	smove = cmd->sidemove;

	VectorCopy (cmd->viewangles, angles);
	if (cl.predstate_valid && cl.predstate_movetype == MOVETYPE_NOCLIP)
	{
		AngleVectors (angles, forward, right, up);
		for (i = 0; i < 3; i++)
			wishvel[i] = forward[i] * fmove + right[i] * smove;
		wishvel[2] += cmd->upmove * 2;
		wishspeed = VectorLength (wishvel);
		if (wishspeed > sv_maxspeed.value)
			VectorScale (wishvel, sv_maxspeed.value / wishspeed, wishvel);
		for (i = 0; i < 3; i++)
			origin[i] += wishvel[i] * frametime;
		VectorCopy (wishvel, velocity);
		return false;
	}

	angles[PITCH] = 0;
	AngleVectors (angles, forward, right, up);

	for (i = 0; i < 3; i++)
		wishvel[i] = forward[i] * fmove + right[i] * smove;
	if (cl.predstate_valid &&
		(cl.predstate_movetype == MOVETYPE_NOCLIP ||
		 cl.predstate_movetype == MOVETYPE_FLY ||
		 (cl.predstate_flags & PREDINFO_INWATER)))
		wishvel[2] += cmd->upmove;
	else
		wishvel[2] = 0;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize (wishdir);
	if (wishspeed > sv_maxspeed.value)
	{
		VectorScale (wishvel, sv_maxspeed.value / wishspeed, wishvel);
		wishspeed = sv_maxspeed.value;
	}

	if (cl.predstate_valid &&
		(cl.predstate_movetype == MOVETYPE_FLY ||
		 (cl.predstate_flags & PREDINFO_INWATER)))
	{
		if (cl.predstate_flags & PREDINFO_INWATER)
		{
			VectorScale (wishvel, 0.7f, wishvel);
			wishspeed *= 0.7f;
		}
		CL_PredictFriction3D (velocity, frametime);
		CL_PredictAccelerate (velocity, wishdir, wishspeed, frametime);
		CL_PredictSlideMove (origin, velocity, frametime);
		return CL_PredictGrounded (origin);
	}

	if (cl.predstate_valid && cl.predstate_sequence == cl.ackedmovemessages &&
		(cl.predstate_flags & PREDINFO_ONGROUND))
		onground = true;

	if (onground)
	{
		if ((cmd->buttons & 2) && *jump_released && velocity[2] <= 0)
		{
			velocity[2] = (vr_enabled.value && sv_vr_jump_velocity.value > 270) ?
				sv_vr_jump_velocity.value : 270;
			*jump_released = false;
			onground = false;
		}
		else
		{
			if (!(vr_movement_instant_stop.value && vr_enabled.value && wishspeed == 0))
				CL_PredictFriction (velocity, frametime);
			else
			{
				velocity[0] = 0;
				velocity[1] = 0;
			}
			CL_PredictAccelerate (velocity, wishdir, wishspeed, frametime);
		}
	}
	else
		CL_PredictAirAccelerate (velocity, wishvel, wishspeed, frametime);

	if (!onground)
		velocity[2] -= sv_gravity.value * frametime;

	CL_PredictStepSlideMove (origin, velocity, frametime);
	return CL_PredictGrounded (origin);
}

static qboolean CL_LocalSingleplayerActive (void)
{
	return sv.active && svs.maxclients <= 1;
}

static qboolean CL_PredictPlayerLegacy (entity_t *ent)
{
	int		seq;
	int		startseq;
	qboolean	predicted;
	qboolean	onground;
	qboolean	jump_released;
	usercmd_t	pending;
	vec3_t		origin, velocity;

	if (CL_LocalSingleplayerActive ())
		return false;

	VectorCopy (ent->msg_origins[0], origin);
	if (cl.predstate_valid && cl.predstate_sequence >= cl.ackedmovemessages)
	{
		VectorCopy (cl.predstate_velocity, velocity);
	}
	else
	{
		VectorCopy (cl.mvelocity[0], velocity);
	}

	startseq = cl.ackedmovemessages + 1;
	if (startseq < 2)
		startseq = 2;
	if (startseq < cl.movemessages - CL_MOVE_HISTORY)
		startseq = cl.movemessages - CL_MOVE_HISTORY;

	predicted = false;
	onground = cl.onground;
	jump_released = !cl.predstate_valid ||
		(cl.predstate_flags & PREDINFO_JUMPRELEASED);
	for (seq = startseq; seq < cl.movemessages; seq++)
	{
		const usercmd_t *histcmd = &cl.movecmds[seq & (CL_MOVE_HISTORY - 1)];
		if (histcmd->sequence != seq)
			continue;
		onground = CL_PredictRunCommand (histcmd, origin, velocity, &jump_released);
		predicted = true;
	}

	pending = cl.pendingcmd;
	if (pending.seconds > 0)
	{
		VectorCopy (cl.aimangles, pending.viewangles);
		onground = CL_PredictRunCommand (&pending, origin, velocity, &jump_released);
		predicted = true;
	}

	if (!predicted)
		return false;

	VectorCopy (origin, ent->origin);
	VectorCopy (velocity, cl.velocity);
	cl.onground = onground;
	return true;
}

static void CL_PredictDecodeSolidSize (unsigned int solidsize, vec3_t mins, vec3_t maxs)
{
	maxs[0] = maxs[1] = solidsize & 255;
	mins[0] = mins[1] = -maxs[0];
	mins[2] = -(int)((solidsize >> 8) & 255);
	maxs[2] = (int)((solidsize >> 16) & 65535) - 32768;
}

static int CL_PredictPMoveType (int movetype)
{
	switch (movetype & 63)
	{
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

static void CL_RecordPredictedMove (int seq, const vec3_t origin, const vec3_t velocity)
{
	int index;

	if (seq < 0)
		return;
	index = seq & (CL_MOVE_HISTORY - 1);
	cl.predicted_move_sequence[index] = seq;
	VectorCopy (origin, cl.predicted_move_origin[index]);
	VectorCopy (velocity, cl.predicted_move_velocity[index]);
}

static void CL_CheckPredictionError (entity_t *ent)
{
	int		ack;
	int		index;
	float	err;
	float	minerr;
	float	maxerr;
	vec3_t	delta;

	if (!cl.predstate_valid || cl.ackedmovemessages < 2)
		return;

	ack = cl.ackedmovemessages;
	index = ack & (CL_MOVE_HISTORY - 1);
	if (cl.predicted_move_sequence[index] != ack)
		return;
	if (cl.net_prediction_error_last_sequence == ack)
		return;

	VectorSubtract (cl.predicted_move_origin[index], ent->msg_origins[0], delta);
	err = VectorLength (delta);
	cl.net_prediction_error_last_sequence = ack;
	cl.net_prediction_error_last = err;
	if (err > cl.net_prediction_error_max)
		cl.net_prediction_error_max = err;

	minerr = q_max (0.0f, cl_predict_smooth_min.value);
	maxerr = q_max (minerr, cl_predict_smooth_max.value);
	if (err < minerr)
		return;

	cl.net_prediction_errors++;
	if (net_lagdebug.value && cl_predict_error_log.value)
		Con_DPrintf ("net_lagdebug: prediction error ack=%d err=%.2f server=(%.1f %.1f %.1f) predicted=(%.1f %.1f %.1f) vel=(%.1f %.1f %.1f)\n",
			ack, err,
			ent->msg_origins[0][0], ent->msg_origins[0][1], ent->msg_origins[0][2],
			cl.predicted_move_origin[index][0],
			cl.predicted_move_origin[index][1],
			cl.predicted_move_origin[index][2],
			cl.predicted_move_velocity[index][0],
			cl.predicted_move_velocity[index][1],
			cl.predicted_move_velocity[index][2]);

	if (!cl_predict_smooth.value || err > maxerr || cl_predict_smooth_time.value <= 0)
	{
		VectorClear (cl.prediction_error);
		cl.prediction_error_time = 0;
		cl.prediction_error_sequence = ack;
		return;
	}

	VectorCopy (delta, cl.prediction_error);
	cl.prediction_error_time = realtime + CLAMP (0.01f, cl_predict_smooth_time.value, 0.25f);
	cl.prediction_error_sequence = ack;
}

static void CL_ApplyPredictionSmoothing (vec3_t origin)
{
	float	frac;
	float	duration;
	int		i;

	if (!cl_predict_smooth.value || cl.prediction_error_time <= realtime)
	{
		VectorClear (cl.prediction_error);
		cl.prediction_error_time = 0;
		return;
	}

	duration = CLAMP (0.01f, cl_predict_smooth_time.value, 0.25f);
	frac = (cl.prediction_error_time - realtime) / duration;
	frac = CLAMP (0.0f, frac, 1.0f);
	for (i = 0; i < 3; i++)
		origin[i] += cl.prediction_error[i] * frac;
}

static qboolean CL_PredictPlayer (entity_t *ent)
{
	static struct
	{
		int seq;
		float waterjumptime;
	} propagate[CL_MOVE_HISTORY];
	int		seq;
	int		startseq;
	int		i;
	int		raw_pmovetype;
	qboolean	predicted;
	usercmd_t	pending;
	vec3_t		bounds[2];
	vec3_t		default_player_mins = {-16, -16, -24};
	vec3_t		default_player_maxs = {16, 16, 32};
	unsigned int	solidsize;

	if (CL_LocalSingleplayerActive ())
		return false;

	if (!cl_predictmove.value || cl_nopred.value || cls.demoplayback ||
		cls.state != ca_connected || cls.signon != SIGNONS ||
		!cls.moveext_allowed || !cl.worldmodel || cl.viewentity <= 0)
		return false;
	if (ent != &cl.entities[cl.viewentity])
		return false;
	if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
		return CL_PredictPlayerLegacy (ent);
	if (!cl.predstate_valid || cl.predstate_sequence < cl.ackedmovemessages)
		return false;

	PMCL_SetMoveVars ();
	memset (&pmove, 0, sizeof(pmove));

	VectorCopy (ent->msg_origins[0], pmove.origin);
	solidsize = ent->netstate.solidsize;
	if (solidsize && solidsize != ES_SOLID_BSP)
	{
		CL_PredictDecodeSolidSize (solidsize, pmove.player_mins, pmove.player_maxs);
	}
	else if (cl.predstate_valid)
	{
		VectorCopy (cl.predstate_mins, pmove.player_mins);
		VectorCopy (cl.predstate_maxs, pmove.player_maxs);
	}
	else
	{
		VectorCopy (default_player_mins, pmove.player_mins);
		VectorCopy (default_player_maxs, pmove.player_maxs);
	}
	for (i = 0; i < 3; i++)
	{
		pmove.velocity[i] = ent->netstate.velocity[i] * (1.0f / 8.0f);
		bounds[0][i] = pmove.origin[i] + pmove.player_mins[i] - 256;
		bounds[1][i] = pmove.origin[i] + pmove.player_maxs[i] + 256;
	}
	VectorClear (pmove.gravitydir);

	raw_pmovetype = ent->netstate.pmovetype;
	if (!raw_pmovetype && cl.predstate_valid)
	{
		raw_pmovetype = cl.predstate_movetype;
		if (cl.predstate_flags & PREDINFO_ONGROUND)
			raw_pmovetype |= 0x80;
		if (!(cl.predstate_flags & PREDINFO_JUMPRELEASED))
			raw_pmovetype |= 0x40;
	}
	pmove.pm_type = CL_PredictPMoveType (raw_pmovetype);
	if (pmove.pm_type == PM_NONE)
		return false;
	pmove.onground = (raw_pmovetype & 0x80) != 0;
	pmove.jump_held = (raw_pmovetype & 0x40) != 0;
	pmove.safeorigin_known = false;
	pmove.onladder = false;
	pmove.waterjumptime = 0;
	pmove.jump_secs = 0;
	pmove.skipent = -cl.viewentity;
	World_AddEntsToPmove (NULL, bounds);
	CL_CheckPredictionError (ent);

	startseq = cl.ackedmovemessages + 1;
	if (startseq < 2)
		startseq = 2;
	if (startseq < cl.movemessages - CL_MOVE_HISTORY)
		startseq = cl.movemessages - CL_MOVE_HISTORY;
	if (propagate[startseq & (CL_MOVE_HISTORY - 1)].seq == startseq)
		pmove.waterjumptime =
			propagate[startseq & (CL_MOVE_HISTORY - 1)].waterjumptime;

	predicted = false;
	for (seq = startseq; seq < cl.movemessages; seq++)
	{
		const usercmd_t *histcmd = &cl.movecmds[seq & (CL_MOVE_HISTORY - 1)];
		if (histcmd->sequence != seq)
			continue;
		pmove.cmd = *histcmd;
		if (pmove.cmd.seconds > 0.1f)
			pmove.cmd.seconds = 0.1f;
		PM_PlayerMove (1);
		CL_RecordPredictedMove (seq, pmove.origin, pmove.velocity);
		propagate[(seq + 1) & (CL_MOVE_HISTORY - 1)].seq = seq + 1;
		propagate[(seq + 1) & (CL_MOVE_HISTORY - 1)].waterjumptime =
			pmove.waterjumptime;
		predicted = true;
	}

	pending = cl.pendingcmd;
	if (pending.seconds > 0)
	{
		VectorCopy (cl.aimangles, pending.viewangles);
		pmove.cmd = pending;
		if (pmove.cmd.seconds > 0.1f)
			pmove.cmd.seconds = 0.1f;
		PM_PlayerMove (1);
		predicted = true;
	}

	if (!predicted)
		return false;

	CL_ApplyPredictionSmoothing (pmove.origin);
	VectorCopy (pmove.origin, ent->origin);
	VectorCopy (pmove.velocity, cl.velocity);
	cl.onground = pmove.onground;
	cl.inwater = pmove.waterlevel >= 2;
	return true;
}

/*
===============
CL_RelinkEntities
===============
*/
void CL_RelinkEntities (void)
{
	entity_t	*ent;
	int			i, j;
	float		frac, f, d;
	vec3_t		delta;
	float		bobjrotate;
	dlight_t	*dl;

// determine partial update time
	frac = CL_LerpPoint ();

	cl_numvisedicts = 0;

//
// interpolate player info
//
	for (i=0 ; i<3 ; i++)
		cl.velocity[i] = cl.mvelocity[1][i] +
			frac * (cl.mvelocity[0][i] - cl.mvelocity[1][i]);

	if (cls.demoplayback)
	{
	// interpolate the angles
		for (j=0 ; j<3 ; j++)
		{
			d = cl.mviewangles[0][j] - cl.mviewangles[1][j];
			if (d > 180)
				d -= 360;
			else if (d < -180)
				d += 360;
			cl.viewangles[j] = cl.mviewangles[1][j] + frac*d;
		}
	}

	bobjrotate = anglemod(100*cl.time);

// start on the entity after the world
	for (i=1,ent=cl.entities+1 ; i<cl.num_entities ; i++,ent++)
	{
		if (!ent->model)
		{	// empty slot
			
			// ericw -- efrags are only used for static entities in GLQuake
			// ent can't be static, so this is a no-op.
			//if (ent->forcelink)
			//	R_RemoveEfrags (ent);	// just became empty
			continue;
		}

// if the object wasn't included in the last packet, remove it
		if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS) &&
			ent->msgtime != cl.mtime[0])
		{
			ent->model = NULL;
			ent->lerpflags |= LERP_RESETMOVE|LERP_RESETANIM; //johnfitz -- next time this entity slot is reused, the lerp will need to be reset
			continue;
		}

		if (ent->forcelink)
		{	// the entity was not updated in the last message
			// so move to the final spot
			VectorCopy (ent->msg_origins[0], ent->origin);
			VectorCopy (ent->msg_angles[0], ent->angles);
		}
		else
		{	// if the delta is large, assume a teleport and don't lerp
			f = frac;
			for (j=0 ; j<3 ; j++)
			{
				delta[j] = ent->msg_origins[0][j] - ent->msg_origins[1][j];
				if (delta[j] > 100 || delta[j] < -100)
				{
					f = 1;		// assume a teleportation, not a motion
					ent->lerpflags |= LERP_RESETMOVE; //johnfitz -- don't lerp teleports
				}
			}

			//johnfitz -- don't cl_lerp entities that will be r_lerped
			if (r_lerpmove.value && (ent->lerpflags & LERP_MOVESTEP))
				f = 1;
			//johnfitz

		// interpolate the origin and angles
			for (j=0 ; j<3 ; j++)
			{
				ent->origin[j] = ent->msg_origins[1][j] + f*delta[j];

				d = ent->msg_angles[0][j] - ent->msg_angles[1][j];
				if (d > 180)
					d -= 360;
				else if (d < -180)
					d += 360;
				ent->angles[j] = ent->msg_angles[1][j] + f*d;
			}
		}

		if (i == cl.viewentity && CL_PredictPlayer (ent))
		{
			VectorCopy (cl.aimangles, ent->angles);
			ent->angles[PITCH] *= -1.0f / 3.0f;
		}

		if (ent->forcelink || ent->lerpflags & LERP_RESETMOVE)
			CL_ResetTrail (ent);

// rotate binary objects locally
		if (ent->model->flags & EF_ROTATE)
			ent->angles[1] = bobjrotate;

		if (ent->effects & EF_BRIGHTFIELD)
			R_EntityParticles (ent);

		if (ent->effects & EF_MUZZLEFLASH)
		{
			vec3_t		fv, rv, uv;

			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->origin[2] += 16;
			AngleVectors (ent->angles, fv, rv, uv);

			VectorMA (dl->origin, 18, fv, dl->origin);
			dl->radius = 200 + (rand()&31);
			dl->minlight = 32;
			dl->die = cl.time + 0.1;

			//johnfitz -- assume muzzle flash accompanied by muzzle flare, which looks bad when lerped
			if (r_lerpmodels.value != 2)
			{
			if (ent == &cl.entities[cl.viewentity])
				cl.viewent.lerpflags |= LERP_RESETANIM|LERP_RESETANIM2; //no lerping for two frames
			else
				ent->lerpflags |= LERP_RESETANIM|LERP_RESETANIM2; //no lerping for two frames
			}
			//johnfitz
		}
		if (ent->effects & EF_BRIGHTLIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->origin[2] += 16;
			dl->radius = 400 + (rand()&31);
			dl->die = cl.time + 0.001;
		}
		if (ent->effects & EF_DIMLIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->radius = 200 + (rand()&31);
			dl->die = cl.time + 0.001;
		}
		if (ent->effects & EF_QEX_QUADLIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->radius = 200 + (rand()&31);
			dl->die = cl.time + 0.001;
			dl->color[0] = 0.25f;
			dl->color[1] = 0.25f;
			dl->color[2] = 1.0f;
		}
		if (ent->effects & EF_QEX_PENTALIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->radius = 200 + (rand()&31);
			dl->die = cl.time + 0.001;
			dl->color[0] = 1.0f;
			dl->color[1] = 0.25f;
			dl->color[2] = 0.25f;
		}

#ifdef PSET_SCRIPT
		if (cl.paused)
			;
		else if (ent->netstate.traileffectnum > 0 && ent->netstate.traileffectnum < MAX_PARTICLETYPES &&
			cl.particle_precache[ent->netstate.traileffectnum].name)
		{
			vec3_t axis[3];
			AngleVectors (ent->angles, axis[0], axis[1], axis[2]);
			PScript_ParticleTrail (ent->trailorg, ent->origin,
				cl.particle_precache[ent->netstate.traileffectnum].index,
				cl.time - cl.oldtime, i, axis, &ent->trailstate);
			CL_ResetTrail (ent);
		}
		else if (ent->model->traileffect >= 0)
		{
			vec3_t axis[3];
			AngleVectors (ent->angles, axis[0], axis[1], axis[2]);
			PScript_ParticleTrail (ent->trailorg, ent->origin,
				ent->model->traileffect, cl.time - cl.oldtime, i, axis, &ent->trailstate);
			CL_ResetTrail (ent);
		}
		else
#endif
		if (ent->netstate.traileffectnum)
			CL_NamedParticleTrail (ent, ent->netstate.traileffectnum);
		else if (ent->model->flags & EF_GIB)
			CL_RocketTrail (ent, 2);
		else if (ent->model->flags & EF_ZOMGIB)
			CL_RocketTrail (ent, 4);
		else if (ent->model->flags & EF_TRACER)
			CL_RocketTrail (ent, 3);
		else if (ent->model->flags & EF_TRACER2)
			CL_RocketTrail (ent, 5);
		else if (ent->model->flags & EF_ROCKET)
		{
			CL_RocketTrail (ent, 0);
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->radius = 200;
			dl->die = cl.time + 0.01;
		}
		else if (ent->model->flags & EF_GRENADE)
			CL_RocketTrail (ent, 1);
		else if (ent->model->flags & EF_TRACER3)
			CL_RocketTrail (ent, 6);
		else
			CL_ResetTrail (ent);

		ent->forcelink = false;

#ifdef PSET_SCRIPT
		if (ent->netstate.emiteffectnum > 0 && ent->netstate.emiteffectnum < MAX_PARTICLETYPES &&
			cl.particle_precache[ent->netstate.emiteffectnum].name)
		{
			vec3_t axis[3];
			AngleVectors (ent->angles, axis[0], axis[1], axis[2]);
			if (ent->model->type == mod_alias)
				axis[0][2] *= -1;
			PScript_RunParticleEffectState (ent->origin, axis[0], cl.time - cl.oldtime,
				cl.particle_precache[ent->netstate.emiteffectnum].index, &ent->emitstate);
		}
		else if (ent->model->emiteffect >= 0)
		{
			vec3_t axis[3];
			AngleVectors (ent->angles, axis[0], axis[1], axis[2]);
			VectorScale (axis[2], -1, axis[0]);
			PScript_RunParticleEffectState (ent->origin, axis[0], cl.time - cl.oldtime,
				ent->model->emiteffect, &ent->emitstate);
		}
		else
#endif
		if (ent->netstate.emiteffectnum)
			CL_RunNamedParticleEffect (ent->netstate.emiteffectnum, ent->origin, vec3_origin, 1);

		if (i == cl.viewentity && !chase_active.value)
			continue;

		if (cl_numvisedicts < MAX_VISEDICTS)
		{
			cl_visedicts[cl_numvisedicts] = ent;
			cl_numvisedicts++;
		}
	}
}

#ifdef PSET_SCRIPT
int CL_GenerateRandomParticlePrecache(const char *pname)
{
	int i;

	if (!pname || !pname[0])
		return 0;

	for (i = 1; i < MAX_PARTICLETYPES; i++)
	{
		if (!cl.particle_precache[i].name)
		{
			cl.particle_precache[i].name = strcpy (Hunk_AllocName (strlen(pname) + 1, "particles"), pname);
			cl.particle_precache[i].index = PScript_FindParticleType (cl.particle_precache[i].name);
			return i;
		}
		if (!strcmp (cl.particle_precache[i].name, pname))
			return i;
	}
	return 0;
}
#endif


/*
===============
CL_ReadFromServer

Read all incoming data from the server
===============
*/
int CL_ReadFromServer (void)
{
	int			ret;
	extern int	num_temp_entities; //johnfitz
	int			num_beams = 0; //johnfitz
	int			num_dlights = 0; //johnfitz
	beam_t		*b; //johnfitz
	dlight_t	*l; //johnfitz
	int			i; //johnfitz
	// Diagnostic state for the reconnect-wait window: snapshot waiting status
	// for this frame and accumulate packet stats between 1Hz heartbeat ticks.
	qboolean	waiting_for_serverinfo;
	int			pkts_this_frame = 0;
	static double	last_wait_tick_time = 0;
	static int		pkts_since_last_tick = 0;
	static int		nonzero_rets_since_last_tick = 0;


	cl.oldtime = cl.time;
	cl.time += host_frametime;

	waiting_for_serverinfo = (cls.state == ca_connected && cls.signon < SIGNONS);

	do
	{
		ret = CL_GetMessage ();
		if (ret == -1)
			Host_Error ("CL_ReadFromServer: lost server connection");
		if (!ret)
			break;

		if (waiting_for_serverinfo)
		{
			byte first = (net_message.cursize > 0) ? (byte)net_message.data[0] : 0xff;
			DebugLog("CL_GetMessage(waiting): ret=%d size=%d firstByte=%d signon=%d\n",
				ret, net_message.cursize, first, cls.signon);
			pkts_this_frame++;
			pkts_since_last_tick++;
		}

		cl.last_received_message = realtime;
		CL_ParseServerMessage ();
	} while (ret && cls.state == ca_connected);

	if (waiting_for_serverinfo)
	{
		if (pkts_this_frame > 0)
			nonzero_rets_since_last_tick++;
		if ((realtime - last_wait_tick_time) >= 1.0)
		{
			DebugLog("CL_ReadFromServer: WAITING signon=%d state=%d pkts=%d frames_with_pkts=%d staleness=%.2fs\n",
				cls.signon, cls.state, pkts_since_last_tick,
				nonzero_rets_since_last_tick, realtime - cl.last_received_message);
			last_wait_tick_time = realtime;
			pkts_since_last_tick = 0;
			nonzero_rets_since_last_tick = 0;
		}
	}
	else
	{
		// Reset accumulators when we're no longer waiting so the next reconnect starts clean.
		last_wait_tick_time = realtime;
		pkts_since_last_tick = 0;
		nonzero_rets_since_last_tick = 0;
	}

	if (cl_shownet.value)
		Con_Printf ("\n");

	CL_RelinkEntities ();
	CL_UpdateTEnts ();

//johnfitz -- devstats

	//visedicts
	if (cl_numvisedicts > 256 && dev_peakstats.visedicts <= 256)
		Con_DWarning ("%i visedicts exceeds standard limit of 256 (max = %d).\n", cl_numvisedicts, MAX_VISEDICTS);
	dev_stats.visedicts = cl_numvisedicts;
	dev_peakstats.visedicts = q_max(cl_numvisedicts, dev_peakstats.visedicts);

	//temp entities
	if (num_temp_entities > 64 && dev_peakstats.tempents <= 64)
		Con_DWarning ("%i tempentities exceeds standard limit of 64 (max = %d).\n", num_temp_entities, MAX_TEMP_ENTITIES);
	dev_stats.tempents = num_temp_entities;
	dev_peakstats.tempents = q_max(num_temp_entities, dev_peakstats.tempents);

	//beams
	for (i=0, b=cl_beams ; i< MAX_BEAMS ; i++, b++)
		if (b->model && b->endtime >= cl.time)
			num_beams++;
	if (num_beams > 24 && dev_peakstats.beams <= 24)
		Con_DWarning ("%i beams exceeded standard limit of 24 (max = %d).\n", num_beams, MAX_BEAMS);
	dev_stats.beams = num_beams;
	dev_peakstats.beams = q_max(num_beams, dev_peakstats.beams);

	//dlights
	for (i=0, l=cl_dlights ; i<MAX_DLIGHTS ; i++, l++)
		if (l->die >= cl.time && l->radius)
			num_dlights++;
	if (num_dlights > 32 && dev_peakstats.dlights <= 32)
		Con_DWarning ("%i dlights exceeded standard limit of 32 (max = %d).\n", num_dlights, MAX_DLIGHTS);
	dev_stats.dlights = num_dlights;
	dev_peakstats.dlights = q_max(num_dlights, dev_peakstats.dlights);

//johnfitz

//
// bring the links up to date
//
	return 0;
}

/*
=================
CL_SendCmd
=================
*/
static usercmd_t cl_pendingcmd;
static int cl_pendingcmd_samples;
static float cl_pendingcmd_seconds;

void CL_ClearPendingCmd (void)
{
	Q_memset(&cl_pendingcmd, 0, sizeof(cl_pendingcmd));
	Q_memset(&cl.pendingcmd, 0, sizeof(cl.pendingcmd));
	cl_pendingcmd_samples = 0;
	cl_pendingcmd_seconds = 0;
}

void CL_AccumulateCmd (void)
{
	usercmd_t cmd;

	if (cls.state != ca_connected || cls.signon != SIGNONS)
		return;

	CL_BaseMove (&cmd);
	IN_Move (&cmd);
	VR_Move (&cmd);

	cl_pendingcmd.forwardmove += cmd.forwardmove;
	cl_pendingcmd.sidemove += cmd.sidemove;
	cl_pendingcmd.upmove += cmd.upmove;
	cl_pendingcmd_samples++;
	cl_pendingcmd_seconds += host_frametime;

	cl.pendingcmd = cl_pendingcmd;
	cl.pendingcmd.seconds = cl_pendingcmd_seconds;
	VectorCopy(cl.aimangles, cl.pendingcmd.viewangles);
	cl.pendingcmd.buttons = 0;
	if (!cl.in_vr_weaponmenu && (in_attack.state & 1))
		cl.pendingcmd.buttons |= 1;
	if (in_jump.state & 1)
		cl.pendingcmd.buttons |= 2;
}

void CL_SendCmd (void)
{
	usercmd_t		cmd;

	if (cls.state != ca_connected)
		return;

	if (cls.signon == SIGNONS)
	{
		if (!cl_pendingcmd_samples)
			CL_AccumulateCmd ();

		cmd = cl_pendingcmd;
		if (cl_pendingcmd_samples > 1)
		{
			cmd.forwardmove /= cl_pendingcmd_samples;
			cmd.sidemove /= cl_pendingcmd_samples;
			cmd.upmove /= cl_pendingcmd_samples;
		}
		cmd.seconds = cl_pendingcmd_seconds;
		CL_ClearPendingCmd ();

	// send the unreliable message
		CL_SendMove (&cmd);
	}

	if (cls.demoplayback)
	{
		SZ_Clear (&cls.message);
		return;
	}

// send the reliable message
	if (!cls.message.cursize)
		return;		// no message at all

	if (!NET_CanSendMessage (cls.netcon))
	{
		Con_DPrintf ("CL_SendCmd: can't send\n");
		return;
	}

	if (NET_SendMessage (cls.netcon, &cls.message) == -1)
		Host_Error ("CL_SendCmd: lost server connection");

	SZ_Clear (&cls.message);
}

/*
=============
CL_Tracepos_f -- johnfitz

display impact point of trace along VPN
=============
*/
void CL_Tracepos_f (void)
{
	vec3_t	v, w;

	if (cls.state != ca_connected)
		return;

	VectorMA(r_refdef.vieworg, 8192.0, vpn, v);
	TraceLine(r_refdef.vieworg, v, w);

	if (VectorLength(w) == 0)
		Con_Printf ("Tracepos: trace didn't hit anything\n");
	else
		Con_Printf ("Tracepos: (%i %i %i)\n", (int)w[0], (int)w[1], (int)w[2]);
}

/*
=============
CL_Viewpos_f -- johnfitz

display client's position and angles
=============
*/
void CL_Viewpos_f (void)
{
	char buf[256];
	if (cls.state != ca_connected)
		return;
#if 0
	//camera position
	q_snprintf (buf, sizeof (buf),
		"(%i %i %i) %i %i %i",
		(int)r_refdef.vieworg[0],
		(int)r_refdef.vieworg[1],
		(int)r_refdef.vieworg[2],
		(int)r_refdef.viewangles[PITCH],
		(int)r_refdef.viewangles[YAW],
		(int)r_refdef.viewangles[ROLL]);
#else
	//player position
	q_snprintf (buf, sizeof (buf),
		"(%i %i %i) %i %i %i",
		(int)cl.entities[cl.viewentity].origin[0],
		(int)cl.entities[cl.viewentity].origin[1],
		(int)cl.entities[cl.viewentity].origin[2],
		(int)cl.viewangles[PITCH],
		(int)cl.viewangles[YAW],
		(int)cl.viewangles[ROLL]
	);
#endif
	Con_Printf ("Viewpos: %s\n", buf);

	if (Cmd_Argc () >= 2 && !q_strcasecmp (Cmd_Argv (1), "copy"))
		if (SDL_SetClipboardText (buf) < 0)
			Con_Printf ("Clipboard copy failed: %s\n", SDL_GetError ());
}

/*
=============
CL_SetStat_f
=============
*/
void CL_SetStat_f (void)
{
	int i, argc, stnum;
	double value;

	for (i = 1, argc = Cmd_Argc (); i + 1 < argc; i += 2)
	{
		stnum = atoi (Cmd_Argv (i));
		if (stnum < 0 || stnum >= MAX_CL_STATS)
			Host_Error ("CL_SetStat_f: stnum(%d) >= MAX_CL_STATS\n", stnum);

		value = atof (Cmd_Argv (i + 1));
		cl.statsf[stnum] = (float)value;
		cl.stats[stnum] = (int)value;
	}
}

/*
=============
CL_SetStatString_f
=============
*/
void CL_SetStatString_f (void)
{
	int i, argc, stnum;

	for (i = 1, argc = Cmd_Argc (); i + 1 < argc; i += 2)
	{
		stnum = atoi (Cmd_Argv (i));
		if (stnum < 0 || stnum >= MAX_CL_STATS)
			Host_Error ("CL_SetStatString_f: stnum(%d) >= MAX_CL_STATS\n", stnum);

		free (cl.statss[stnum]);
		cl.statss[stnum] = strdup (Cmd_Argv (i + 1));
	}
}

/*
=================
CL_Init
=================
*/
void CL_Init (void)
{
	SZ_Alloc (&cls.message, 1024);

	CL_InitInput ();
	CL_InitTEnts ();

	Cvar_RegisterVariable (&cl_name);
	Cvar_RegisterVariable (&cl_color);
	Cvar_RegisterVariable (&cl_upspeed);
	Cvar_RegisterVariable (&cl_forwardspeed);
	Cvar_RegisterVariable (&cl_backspeed);
	Cvar_RegisterVariable (&cl_sidespeed);
	Cvar_RegisterVariable (&cl_desktop_vanilla_run);
	Cvar_RegisterVariable (&cl_trusted_clientmove);
	Cvar_RegisterVariable (&cl_trusted_clientmove_desktop);
	Cvar_RegisterVariable (&cl_predictmove);
	Cvar_RegisterVariable (&cl_move_redundancy);
	Cvar_RegisterVariable (&cl_move_packetdup);
	Cvar_RegisterVariable (&cl_nopred);
	Cvar_RegisterVariable (&cl_predict_smooth);
	Cvar_RegisterVariable (&cl_predict_smooth_time);
	Cvar_RegisterVariable (&cl_predict_smooth_min);
	Cvar_RegisterVariable (&cl_predict_smooth_max);
	Cvar_RegisterVariable (&cl_predict_error_log);
	Cvar_RegisterVariable (&cl_movespeedkey);
	Cvar_RegisterVariable (&cl_yawspeed);
	Cvar_RegisterVariable (&cl_pitchspeed);
	Cvar_RegisterVariable (&cl_anglespeedkey);
	Cvar_RegisterVariable (&cl_shownet);
	Cvar_RegisterVariable (&cl_nolerp);
	Cvar_RegisterVariable (&cl_lerpdebug);
	Cvar_RegisterVariable (&cl_lerpdebug_models);
	Cvar_RegisterVariable (&cl_beams_polygons);
	Cvar_RegisterVariable (&cl_extrapolate);
	Cvar_RegisterVariable (&cl_extrapolate_adaptive);
	Cvar_RegisterVariable (&cl_extrapolate_adaptive_max);
	Cvar_RegisterVariable (&cl_extrapolate_adaptive_time);
	Cvar_RegisterVariable (&cl_net_lerpbuffer);
	Cvar_RegisterVariable (&freelook);
	Cvar_RegisterVariable (&lookspring);
	Cvar_RegisterVariable (&lookstrafe);
	Cvar_RegisterVariable (&sensitivity);
	
	Cvar_RegisterVariable (&cl_alwaysrun);

	Cvar_RegisterVariable (&m_pitch);
	Cvar_RegisterVariable (&m_yaw);
	Cvar_RegisterVariable (&m_forward);
	Cvar_RegisterVariable (&m_side);

	Cvar_RegisterVariable (&cfg_unbindall);

	Cvar_RegisterVariable (&cl_maxpitch); //johnfitz -- variable pitch clamping
	Cvar_RegisterVariable (&cl_minpitch); //johnfitz -- variable pitch clamping

	Cvar_RegisterVariable (&cl_mwheelpitch);

	Cvar_RegisterVariable (&cl_startdemos);
	Cvar_RegisterVariable (&cl_confirmquit);

	Cvar_RegisterVariable (&cl_mousemenu);

	Cmd_AddCommand ("entities", CL_PrintEntities_f);
	Cmd_AddCommand ("disconnect", CL_Disconnect_f);
	Cmd_AddCommand ("record", CL_Record_f);
	Cmd_AddCommand ("stop", CL_Stop_f);
	Cmd_AddCommand ("playdemo", CL_PlayDemo_f);
	Cmd_AddCommand ("timedemo", CL_TimeDemo_f);

	Cmd_AddCommand ("tracepos", CL_Tracepos_f); //johnfitz
	Cmd_AddCommand ("viewpos", CL_Viewpos_f); //johnfitz

	Cmd_AddCommand_ServerCommand ("st", CL_SetStat_f);
	Cmd_AddCommand_ServerCommand ("sts", CL_SetStatString_f);
	Cmd_AddCommand_ServerCommand ("cl_trustedmove_ack", CL_TrustedClientMoveAck_f);
	Cmd_AddCommand_ServerCommand ("cl_moveext_ack", CL_MoveExtAck_f);
}
