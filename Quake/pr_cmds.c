/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2016      Spike

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

#include "quakedef.h"
#include "q_ctype.h"
#include "pmove.h"
#include "crc.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#endif

#define	STRINGTEMP_BUFFERS		1024
#define	STRINGTEMP_LENGTH		1024
static	char	pr_string_temp[STRINGTEMP_BUFFERS][STRINGTEMP_LENGTH];
static	byte	pr_string_tempindex = 0;

static char *PR_GetTempString (void)
{
	return pr_string_temp[(STRINGTEMP_BUFFERS-1) & ++pr_string_tempindex];
}

int PR_MakeTempString (const char *s)
{
	char *tmp = PR_GetTempString();

	q_strlcpy(tmp, s ? s : "", STRINGTEMP_LENGTH);
	return PR_SetEngineString(tmp);
}

#define	RETURN_EDICT(e) (((int *)qcvm->globals)[OFS_RETURN] = EDICT_TO_PROG(e))

#define	MSG_BROADCAST	0		// unreliable to all
#define	MSG_ONE		1		// reliable to one (msg_entity)
#define	MSG_ALL		2		// reliable to all
#define	MSG_INIT	3		// write to the init string
#define	MSG_EXT_MULTICAST	4	// temporary extended QC message buffer
#define	MSG_EXT_ENTITY	5		// temporary CSQC entity message buffer

/*
===============================================================================

	BUILT-IN FUNCTIONS

===============================================================================
*/

static const char* PF_GetStringArg(int idx, void* userdata)
{
	if (userdata)
		idx += *(int*)userdata;
	if (idx < 0 || idx >= qcvm->argc)
		return "";
	return LOC_GetString(G_STRING(OFS_PARM0 + idx * 3));
}

static char *PF_VarString (int	first)
{
	int		i;
	static char out[1024];
	const char *format;
	size_t s;

	out[0] = 0;
	s = 0;

	if (first >= qcvm->argc)
		return out;

	format = LOC_GetString(G_STRING((OFS_PARM0 + first * 3)));
	if (LOC_HasPlaceholders(format))
	{
		int offset = first + 1;
		s = LOC_Format(format, PF_GetStringArg, &offset, out, sizeof(out));
	}
	else
	{
		for (i = first; i < qcvm->argc; i++)
		{
			s = q_strlcat(out, LOC_GetString(G_STRING(OFS_PARM0+i*3)), sizeof(out));
			if (s >= sizeof(out))
			{
				Con_Warning("PF_VarString: overflow (string truncated)\n");
				return out;
			}
		}
	}
	if (s > 255)
	{
		if (!dev_overflows.varstring || dev_overflows.varstring + CONSOLE_RESPAM_TIME < realtime)
		{
			Con_DWarning("PF_VarString: %i characters exceeds standard limit of 255 (max = %d).\n",
								(int) s, (int)(sizeof(out) - 1));
			dev_overflows.varstring = realtime;
		}
	}
	return out;
}

void PR_GetSetInputs (usercmd_t *cmd, qboolean set)
{
	if (!cmd)
		return;

	if (set)
	{
		if (qcvm->extglobals.input_sequence)
			*qcvm->extglobals.input_sequence = cmd->sequence;
		if (qcvm->extglobals.input_servertime)
			*qcvm->extglobals.input_servertime = cmd->servertime;
		if (qcvm->extglobals.input_timelength)
			*qcvm->extglobals.input_timelength = cmd->seconds;
		if (qcvm->extglobals.input_angles)
			VectorCopy (cmd->viewangles, qcvm->extglobals.input_angles);
		if (qcvm->extglobals.input_movevalues)
		{
			qcvm->extglobals.input_movevalues[0] = cmd->forwardmove;
			qcvm->extglobals.input_movevalues[1] = cmd->sidemove;
			qcvm->extglobals.input_movevalues[2] = cmd->upmove;
		}
		if (qcvm->extglobals.input_buttons)
			*qcvm->extglobals.input_buttons = cmd->buttons;
		if (qcvm->extglobals.input_impulse)
			*qcvm->extglobals.input_impulse = cmd->impulse;
		if (qcvm->extglobals.input_weapon)
			*qcvm->extglobals.input_weapon = cmd->weapon;
		if (qcvm->extglobals.input_cursor_screen)
		{
			qcvm->extglobals.input_cursor_screen[0] = cmd->cursor_screen[0];
			qcvm->extglobals.input_cursor_screen[1] = cmd->cursor_screen[1];
		}
		if (qcvm->extglobals.input_cursor_trace_start)
			VectorCopy (cmd->cursor_start, qcvm->extglobals.input_cursor_trace_start);
		if (qcvm->extglobals.input_cursor_trace_endpos)
			VectorCopy (cmd->cursor_impact, qcvm->extglobals.input_cursor_trace_endpos);
		if (qcvm->extglobals.input_cursor_entitynumber)
			*qcvm->extglobals.input_cursor_entitynumber = cmd->cursor_entitynumber;
	}
	else
	{
		if (qcvm->extglobals.input_sequence)
			cmd->sequence = *qcvm->extglobals.input_sequence;
		if (qcvm->extglobals.input_servertime)
			cmd->servertime = *qcvm->extglobals.input_servertime;
		if (qcvm->extglobals.input_timelength)
			cmd->seconds = *qcvm->extglobals.input_timelength;
		if (qcvm->extglobals.input_angles)
			VectorCopy (qcvm->extglobals.input_angles, cmd->viewangles);
		if (qcvm->extglobals.input_movevalues)
		{
			cmd->forwardmove = qcvm->extglobals.input_movevalues[0];
			cmd->sidemove = qcvm->extglobals.input_movevalues[1];
			cmd->upmove = qcvm->extglobals.input_movevalues[2];
		}
		if (qcvm->extglobals.input_buttons)
			cmd->buttons = *qcvm->extglobals.input_buttons;
		if (qcvm->extglobals.input_impulse)
			cmd->impulse = *qcvm->extglobals.input_impulse;
		if (qcvm->extglobals.input_weapon)
			cmd->weapon = *qcvm->extglobals.input_weapon;
		if (qcvm->extglobals.input_cursor_screen)
		{
			cmd->cursor_screen[0] = qcvm->extglobals.input_cursor_screen[0];
			cmd->cursor_screen[1] = qcvm->extglobals.input_cursor_screen[1];
		}
		if (qcvm->extglobals.input_cursor_trace_start)
			VectorCopy (qcvm->extglobals.input_cursor_trace_start, cmd->cursor_start);
		if (qcvm->extglobals.input_cursor_trace_endpos)
			VectorCopy (qcvm->extglobals.input_cursor_trace_endpos, cmd->cursor_impact);
		if (qcvm->extglobals.input_cursor_entitynumber)
			cmd->cursor_entitynumber = *qcvm->extglobals.input_cursor_entitynumber;
	}
}


/*
=================
PF_error

This is a TERMINAL error, which will kill off the entire server.
Dumps self.

error(value)
=================
*/
static void PF_error (void)
{
	char	*s;
	edict_t	*ed;

	s = PF_VarString(0);
	Con_Printf ("======SERVER ERROR in %s:\n%s\n",
			PR_GetString(qcvm->xfunction->s_name), s);
	ed = PROG_TO_EDICT(pr_global_struct->self);
	ED_Print (ed);

	Host_Error ("Program error");
}

/*
=================
PF_objerror

Dumps out self, then an error message.  The program is aborted and self is
removed, but the level can continue.

objerror(value)
=================
*/
static void PF_objerror (void)
{
	char	*s;
	edict_t	*ed;

	s = PF_VarString(0);
	Con_Printf ("======OBJECT ERROR in %s:\n%s\n",
			PR_GetString(qcvm->xfunction->s_name), s);
	ed = PROG_TO_EDICT(pr_global_struct->self);
	ED_Print (ed);
	ED_Free (ed);

	//Host_Error ("Program error"); //johnfitz -- by design, this should not be fatal
}



/*
==============
PF_makevectors

Writes new values for v_forward, v_up, and v_right based on angles
makevectors(vector)
==============
*/
static void PF_makevectors (void)
{
	AngleVectors (G_VECTOR(OFS_PARM0), pr_global_struct->v_forward, pr_global_struct->v_right, pr_global_struct->v_up);
}

static void SV_DebugLogSetOrigin (edict_t *ent, const vec3_t oldorg,
	const vec3_t neworg);
static void SV_DebugLogCenterprint (int entnum, const char *s);
static const char *SV_DebugFieldNameForOffset (int ofs);
static void SV_DebugLogFindTargetMatch (edict_t *ent, const char *fieldname,
	const char *match);
static qboolean SV_ShouldSuppressShubRoundResultFind (int fieldofs,
	const char *fieldname, const char *match);
static qboolean SV_ShouldSuppressShubCleanupFind (int fieldofs,
	const char *fieldname, const char *match);

/*
=================
PF_setorigin

This is the only valid way to move an object without using the physics
of the world (setting velocity and waiting).  Directly changing origin
will not set internal links correctly, so clipping would be messed up.

This should be called when an object is spawned, and then only if it is
teleported.

setorigin (entity, origin)
=================
*/
static void PF_setorigin (void)
{
	edict_t	*e;
	float	*org;
	vec3_t	oldorg;

	e = G_EDICT(OFS_PARM0);
	org = G_VECTOR(OFS_PARM1);
	VectorCopy (e->v.origin, oldorg);
	VectorCopy (org, e->v.origin);
	SV_LinkEdict (e, false);
	SV_DebugLogSetOrigin (e, oldorg, org);
}


static void SetMinMaxSize (edict_t *e, float *minvec, float *maxvec, qboolean rotate)
{
	float	*angles;
	vec3_t	rmin, rmax;
	float	bounds[2][3];
	float	xvector[2], yvector[2];
	float	a;
	vec3_t	base, transformed;
	int		i, j, k, l;

	for (i = 0; i < 3; i++)
		if (minvec[i] > maxvec[i])
			PR_RunError ("backwards mins/maxs");

	rotate = false;		// FIXME: implement rotation properly again

	if (!rotate)
	{
		VectorCopy (minvec, rmin);
		VectorCopy (maxvec, rmax);
	}
	else
	{
	// find min / max for rotations
		angles = e->v.angles;

		a = angles[1]/180 * M_PI;

		xvector[0] = cos(a);
		xvector[1] = sin(a);
		yvector[0] = -sin(a);
		yvector[1] = cos(a);

		VectorCopy (minvec, bounds[0]);
		VectorCopy (maxvec, bounds[1]);

		rmin[0] = rmin[1] = rmin[2] = FLT_MAX;
		rmax[0] = rmax[1] = rmax[2] = -FLT_MAX;

		for (i = 0; i <= 1; i++)
		{
			base[0] = bounds[i][0];
			for (j = 0; j <= 1; j++)
			{
				base[1] = bounds[j][1];
				for (k = 0; k <= 1; k++)
				{
					base[2] = bounds[k][2];

				// transform the point
					transformed[0] = xvector[0]*base[0] + yvector[0]*base[1];
					transformed[1] = xvector[1]*base[0] + yvector[1]*base[1];
					transformed[2] = base[2];

					for (l = 0; l < 3; l++)
					{
						if (transformed[l] < rmin[l])
							rmin[l] = transformed[l];
						if (transformed[l] > rmax[l])
							rmax[l] = transformed[l];
					}
				}
			}
		}
	}

// set derived values
	VectorCopy (rmin, e->v.mins);
	VectorCopy (rmax, e->v.maxs);
	VectorSubtract (maxvec, minvec, e->v.size);

	SV_LinkEdict (e, false);
}

/*
=================
PF_setsize

the size box is rotated by the current angle

setsize (entity, minvector, maxvector)
=================
*/
static void PF_setsize (void)
{
	edict_t	*e;
	float	*minvec, *maxvec;

	e = G_EDICT(OFS_PARM0);
	minvec = G_VECTOR(OFS_PARM1);
	maxvec = G_VECTOR(OFS_PARM2);

	SetMinMaxSize (e, minvec, maxvec, false);
}


/*
=================
PF_setmodel

setmodel(entity, model)
=================
*/
static void PF_setmodel (void)
{
	int		i;
	const char	*m, **check;
	qmodel_t	*mod;
	edict_t		*e;

	e = G_EDICT(OFS_PARM0);
	m = G_STRING(OFS_PARM1);

// check to see if model was properly precached
	for (i = 0, check = sv.model_precache; *check; i++, check++)
	{
		if (!strcmp(*check, m))
			break;
	}

	if (!*check)
	{
		PR_RunError ("no precache: %s", m);
	}
	e->v.model = PR_SetEngineString(*check);
	e->v.modelindex = i; //SV_ModelIndex (m);

	mod = sv.models[ (int)e->v.modelindex];  // Mod_ForName (m, true);

	if (mod)
	//johnfitz -- correct physics cullboxes for bmodels
	{
		if (mod->type == mod_brush)
			SetMinMaxSize (e, mod->clipmins, mod->clipmaxs, true);
		else
			SetMinMaxSize (e, mod->mins, mod->maxs, true);
	}
	//johnfitz
	else
		SetMinMaxSize (e, vec3_origin, vec3_origin, true);
}

/*
=================
PF_bprint

broadcast print to everyone on server

bprint(value)
=================
*/
static void PF_bprint (void)
{
	char		*s;

	s = PF_VarString(0);
	SV_BroadcastPrintf ("%s", s);
}

/*
=================
PF_sprint

single print to a specific client

sprint(clientent, value)
=================
*/
static void PF_sprint (void)
{
	char		*s;
	client_t	*client;
	int	entnum;

	entnum = G_EDICTNUM(OFS_PARM0);
	s = PF_VarString(1);

	if (entnum < 1 || entnum > svs.maxclients)
	{
		Con_Printf ("tried to sprint to a non-client\n");
		return;
	}

	client = &svs.clients[entnum-1];

	MSG_WriteChar (&client->message,svc_print);
	MSG_WriteString (&client->message, s );
}


/*
=================
PF_centerprint

single print to a specific client

centerprint(clientent, value)
=================
*/
static void PF_centerprint (void)
{
	char		*s;
	client_t	*client;
	int	entnum;

	entnum = G_EDICTNUM(OFS_PARM0);
	s = PF_VarString(1);

	if (entnum < 1 || entnum > svs.maxclients)
	{
		Con_Printf ("tried to sprint to a non-client\n");
		return;
	}

	client = &svs.clients[entnum-1];

	SV_DebugLogCenterprint (entnum, s);
	MSG_WriteChar (&client->message,svc_centerprint);
	MSG_WriteString (&client->message, s);
}


/*
=================
PF_normalize

vector normalize(vector)
=================
*/
static void PF_normalize (void)
{
	float	*value1;
	vec3_t	newvalue;
	double	new_temp;

	value1 = G_VECTOR(OFS_PARM0);

	new_temp = (double)value1[0] * value1[0] + (double)value1[1] * value1[1] + (double)value1[2]*value1[2];
	new_temp = sqrt (new_temp);

	if (new_temp == 0)
		newvalue[0] = newvalue[1] = newvalue[2] = 0;
	else
	{
		new_temp = 1 / new_temp;
		newvalue[0] = value1[0] * new_temp;
		newvalue[1] = value1[1] * new_temp;
		newvalue[2] = value1[2] * new_temp;
	}

	VectorCopy (newvalue, G_VECTOR(OFS_RETURN));
}

/*
=================
PF_vlen

scalar vlen(vector)
=================
*/
static void PF_vlen (void)
{
	float	*value1;
	double	new_temp;

	value1 = G_VECTOR(OFS_PARM0);

	new_temp = (double)value1[0] * value1[0] + (double)value1[1] * value1[1] + (double)value1[2]*value1[2];
	new_temp = sqrt(new_temp);

	G_FLOAT(OFS_RETURN) = new_temp;
}

/*
=================
PF_vectoyaw

float vectoyaw(vector)
=================
*/
static void PF_vectoyaw (void)
{
	float	*value1;
	float	yaw;

	value1 = G_VECTOR(OFS_PARM0);

	if (value1[1] == 0 && value1[0] == 0)
		yaw = 0;
	else
	{
		yaw = (int) (atan2(value1[1], value1[0]) * 180 / M_PI);
		if (yaw < 0)
			yaw += 360;
	}

	G_FLOAT(OFS_RETURN) = yaw;
}


/*
=================
PF_vectoangles

vector vectoangles(vector)
=================
*/
static void PF_vectoangles (void)
{
	float	*value1;
	float	forward;
	float	yaw, pitch;

	value1 = G_VECTOR(OFS_PARM0);

	if (value1[1] == 0 && value1[0] == 0)
	{
		yaw = 0;
		if (value1[2] > 0)
			pitch = 90;
		else
			pitch = 270;
	}
	else
	{
		yaw = (int) (atan2(value1[1], value1[0]) * 180 / M_PI);
		if (yaw < 0)
			yaw += 360;

		forward = sqrt (value1[0]*value1[0] + value1[1]*value1[1]);
		pitch = (int) (atan2(value1[2], forward) * 180 / M_PI);
		if (pitch < 0)
			pitch += 360;
	}

	G_FLOAT(OFS_RETURN+0) = pitch;
	G_FLOAT(OFS_RETURN+1) = yaw;
	G_FLOAT(OFS_RETURN+2) = 0;
}

/*
=================
PF_Random

Returns a number from 0 < num < 1

random()
=================
*/
cvar_t sv_gameplayfix_random = {"sv_gameplayfix_random", "1", CVAR_ARCHIVE};
static void PF_random (void)
{
	float		num;

	if (sv_gameplayfix_random.value)
		num = ((rand() & 0x7fff) + 0.5f) * (1.f / 0x8000);
	else
		num = (rand() & 0x7fff) / ((float)0x7fff);

	G_FLOAT(OFS_RETURN) = num;
}

/*
=================
PF_particle

particle(origin, color, count)
=================
*/
static void PF_particle (void)
{
	float		*org, *dir;
	float		color;
	float		count;

	org = G_VECTOR(OFS_PARM0);
	dir = G_VECTOR(OFS_PARM1);
	color = G_FLOAT(OFS_PARM2);
	count = G_FLOAT(OFS_PARM3);
	SV_StartParticle (org, dir, color, count);
}

static const char *PF_ParticleEffectName (int effectnum)
{
#ifdef PSET_SCRIPT
	if (qcvm == &cl.qcvm && effectnum < 0)
	{
		effectnum = -effectnum;
		if (effectnum <= 0 || effectnum >= MAX_PARTICLETYPES ||
			!cl.local_particle_precache[effectnum].name)
			return "";
		return cl.local_particle_precache[effectnum].name;
	}
#endif
	if (effectnum <= 0 || effectnum >= MAX_PARTICLETYPES)
		return "";
#ifdef PSET_SCRIPT
	if (qcvm == &cl.qcvm)
		return cl.particle_precache[effectnum].name ? cl.particle_precache[effectnum].name : "";
#else
	if (qcvm == &cl.qcvm)
		return cl.particle_precache[effectnum];
#endif
	return sv.particle_precache[effectnum] ? sv.particle_precache[effectnum] : "";
}

static int PF_ParticleEffectColor (int effectnum)
{
	const char *name = PF_ParticleEffectName (effectnum);

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

static int PF_RegisterParticleEffect (const char *name)
{
	int i;

	if (!name || !name[0])
		return 0;

	if (qcvm == &cl.qcvm)
	{
#ifdef PSET_SCRIPT
		for (i = 1; i < MAX_PARTICLETYPES; i++)
		{
			if (!cl.particle_precache[i].name)
				break;
			if (!strcmp (cl.particle_precache[i].name, name))
				return i;
		}
		for (i = 1; i < MAX_PARTICLETYPES; i++)
		{
			if (!cl.local_particle_precache[i].name)
			{
				cl.local_particle_precache[i].name = strcpy (Hunk_AllocName (strlen(name) + 1, "particles"), name);
				cl.local_particle_precache[i].index = PScript_FindParticleType (cl.local_particle_precache[i].name);
				return -i;
			}
			if (!strcmp (cl.local_particle_precache[i].name, name))
				return -i;
		}
#else
		for (i = 1; i < MAX_PARTICLETYPES; i++)
		{
			if (!cl.particle_precache[i][0])
			{
				q_strlcpy (cl.particle_precache[i], name, sizeof(cl.particle_precache[i]));
				return i;
			}
			if (!strcmp (cl.particle_precache[i], name))
				return i;
		}
#endif
	}
	else
	{
#ifdef PSET_SCRIPT
		if (!sv.particle_precache[1] && !strncmp (name, "effectinfo.", 11))
			COM_Effectinfo_Enumerate (PF_RegisterParticleEffect);
#endif
		for (i = 1; i < MAX_PARTICLETYPES; i++)
		{
			if (!sv.particle_precache[i])
			{
				sv.particle_precache[i] = strcpy (Hunk_AllocName (strlen(name) + 1, "particles"), name);
#ifdef PSET_SCRIPT
				if (sv.state != ss_loading)
				{
					MSG_WriteByte (&sv.reliable_datagram, svcdp_precache);
					MSG_WriteShort (&sv.reliable_datagram, 0x4000 | i);
					MSG_WriteString (&sv.reliable_datagram, sv.particle_precache[i]);
				}
#endif
				return i;
			}
			if (!strcmp (sv.particle_precache[i], name))
				return i;
		}
	}

	PR_RunError ("particleeffectnum: overflow");
	return 0;
}

#ifdef PSET_SCRIPT
static int PF_CL_GetParticle (int effectnum)
{
	if (effectnum < 0)
	{
		effectnum = -effectnum;
		if (effectnum >= MAX_PARTICLETYPES)
			return P_INVALID;
		return cl.local_particle_precache[effectnum].index;
	}
	if (effectnum <= 0 || effectnum >= MAX_PARTICLETYPES)
		return P_INVALID;
	return cl.particle_precache[effectnum].index;
}
#endif

static void PF_particleeffectnum (void)
{
	G_FLOAT(OFS_RETURN) = PF_RegisterParticleEffect (G_STRING(OFS_PARM0));
}

static void PF_StartClassicParticleFallback (int effectnum, const float *org,
	const float *dir, int count)
{
	int color = PF_ParticleEffectColor (effectnum);

	count = CLAMP (1, count, 255);
	if (qcvm == &cl.qcvm)
		R_RunParticleEffect ((float *)org, (float *)dir, color, count);
	else
		SV_StartParticle ((float *)org, (float *)dir, color, count);
}

static void PF_pointparticles (void)
{
	int effectnum = G_FLOAT(OFS_PARM0);
	float *org = G_VECTOR(OFS_PARM1);
	float *dir = qcvm->argc > 2 ? G_VECTOR(OFS_PARM2) : vec3_origin;
	int count = qcvm->argc > 3 ? G_FLOAT(OFS_PARM3) : 1;

	if (effectnum <= 0 || count <= 0)
		return;
#ifdef PSET_SCRIPT
	if (qcvm == &cl.qcvm)
	{
		PScript_RunParticleEffectState (org, dir, count, PF_CL_GetParticle (effectnum), NULL);
		return;
	}
	if (count > 65535)
		count = 65535;
	if (count == 1 && !dir[0] && !dir[1] && !dir[2])
	{
		MSG_WriteByte (&sv.datagram, svcdp_pointparticles1);
		MSG_WriteShort (&sv.datagram, effectnum);
		MSG_WriteCoord (&sv.datagram, org[0], sv.protocolflags);
		MSG_WriteCoord (&sv.datagram, org[1], sv.protocolflags);
		MSG_WriteCoord (&sv.datagram, org[2], sv.protocolflags);
	}
	else
	{
		MSG_WriteByte (&sv.datagram, svcdp_pointparticles);
		MSG_WriteShort (&sv.datagram, effectnum);
		MSG_WriteCoord (&sv.datagram, org[0], sv.protocolflags);
		MSG_WriteCoord (&sv.datagram, org[1], sv.protocolflags);
		MSG_WriteCoord (&sv.datagram, org[2], sv.protocolflags);
		MSG_WriteCoord (&sv.datagram, dir[0], sv.protocolflags);
		MSG_WriteCoord (&sv.datagram, dir[1], sv.protocolflags);
		MSG_WriteCoord (&sv.datagram, dir[2], sv.protocolflags);
		MSG_WriteShort (&sv.datagram, count);
	}
#else
	PF_StartClassicParticleFallback (effectnum, org, dir, count);
#endif
}

static void PF_trailparticles (void)
{
	int effectnum;
	float *start, *end;
#ifdef PSET_SCRIPT
	edict_t *ent;
#else
	vec3_t org, dir, step;
	float len;
	int samples, i;
#endif

	if (qcvm->argc < 4)
		return;

	effectnum = G_FLOAT(OFS_PARM0);
	ent = G_EDICT(OFS_PARM1);
	start = G_VECTOR(OFS_PARM2);
	end = G_VECTOR(OFS_PARM3);

	if (effectnum <= 0)
		return;

#ifdef PSET_SCRIPT
	if (qcvm == &cl.qcvm)
	{
		PScript_ParticleTrail (start, end, PF_CL_GetParticle (effectnum), host_frametime,
			-NUM_FOR_EDICT(ent), NULL, NULL);
		return;
	}
	MSG_WriteByte (&sv.datagram, svcdp_trailparticles);
	MSG_WriteShort (&sv.datagram, NUM_FOR_EDICT(ent));
	MSG_WriteShort (&sv.datagram, effectnum);
	MSG_WriteCoord (&sv.datagram, start[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, start[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, start[2], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, end[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, end[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, end[2], sv.protocolflags);
#else
	VectorSubtract (end, start, dir);
	len = VectorNormalize (dir);
	samples = CLAMP (1, (int)(len / 32.0f) + 1, 16);
	VectorScale (dir, len / samples, step);
	VectorCopy (start, org);
	for (i = 0; i < samples; i++)
	{
		PF_StartClassicParticleFallback (effectnum, org, dir, 1);
		VectorAdd (org, step, org);
	}
#endif
}

static void PF_te_particleweather (qboolean snow)
{
	float *mins = G_VECTOR(OFS_PARM0);
	float *maxs = G_VECTOR(OFS_PARM1);
	float *vel = G_VECTOR(OFS_PARM2);
	int count = G_FLOAT(OFS_PARM3);
	int basecolor = G_FLOAT(OFS_PARM4);
#ifdef PSET_SCRIPT
	int type = snow ? TEDP_PARTICLESNOW : TEDP_PARTICLERAIN;

	if (qcvm == &cl.qcvm)
	{
		PScript_RunParticleWeather (mins, maxs, vel, count, basecolor, snow ? "snow" : "rain");
		return;
	}

	MSG_WriteByte (&sv.datagram, svc_temp_entity);
	MSG_WriteByte (&sv.datagram, type);
	MSG_WriteCoord (&sv.datagram, mins[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, mins[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, mins[2], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, maxs[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, maxs[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, maxs[2], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, vel[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, vel[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, vel[2], sv.protocolflags);
	MSG_WriteShort (&sv.datagram, CLAMP(0, count, 65535));
	MSG_WriteByte (&sv.datagram, CLAMP(0, basecolor, 255));
#else
	vec3_t org, dir;
	int i, j;

	count = CLAMP (0, count, 32);
	VectorCopy (vel, dir);
	if (!VectorNormalize (dir))
	{
		dir[0] = dir[1] = dir[2] = 0;
		dir[2] = snow ? -0.25f : -1.0f;
	}

	if (basecolor <= 0)
		basecolor = snow ? 15 : 73;

	for (i = 0; i < count; i++)
	{
		for (j = 0; j < 3; j++)
			org[j] = mins[j] + ((rand() & 0x7fff) / 32767.0f) * (maxs[j] - mins[j]);
		if (qcvm == &cl.qcvm)
			R_RunParticleEffect (org, dir, basecolor, 1);
		else
			SV_StartParticle (org, dir, basecolor, 1);
	}
#endif
}

static void PF_te_particlerain (void)
{
	PF_te_particleweather (false);
}

static void PF_te_particlesnow (void)
{
	PF_te_particleweather (true);
}


/*
=================
PF_ambientsound

=================
*/
static void PF_ambientsound (void)
{
	const char	*samp, **check;
	float		*pos;
	float		vol, attenuation;
	int		soundnum;
	struct ambientsound_s *st;

	pos = G_VECTOR (OFS_PARM0);
	samp = G_STRING(OFS_PARM1);
	vol = G_FLOAT(OFS_PARM2);
	attenuation = G_FLOAT(OFS_PARM3);

// check to see if samp was properly precached
	for (soundnum = 0, check = sv.sound_precache; *check; check++, soundnum++)
	{
		if (!strcmp(*check, samp))
			break;
	}

	if (!*check)
	{
		Con_Printf ("no precache: %s\n", samp);
		return;
	}

	if (soundnum > 255 && sv.protocol == PROTOCOL_NETQUAKE)
		return; //don't send any info protocol can't support

	if (sv.num_ambients == sv.max_ambients)
	{
		int nm = sv.max_ambients + 128;
		struct ambientsound_s *n = (nm * sizeof(*n) < sv.max_ambients * sizeof(*n)) ?
			NULL : (struct ambientsound_s *)realloc (sv.ambientsounds, nm * sizeof(*n));
		if (!n)
			PR_RunError ("PF_ambientsound: out of memory");
		sv.ambientsounds = n;
		memset (sv.ambientsounds + sv.max_ambients, 0, (nm - sv.max_ambients) * sizeof(*n));
		sv.max_ambients = nm;
	}

	st = &sv.ambientsounds[sv.num_ambients++];
	VectorCopy (pos, st->origin);
	st->soundindex = soundnum;
	st->volume = vol;
	st->attenuation = attenuation;
}

/*
=================
PF_sound

Each entity can have eight independant sound sources, like voice,
weapon, feet, etc.

Channel 0 is an auto-allocate channel, the others override anything
already running on that entity/channel pair.

An attenuation of 0 will play full volume everywhere in the level.
Larger attenuations will drop off.

=================
*/
static void PF_sound (void)
{
	const char	*sample;
	int		channel;
	edict_t		*entity;
	int		volume;
	float	attenuation;

	entity = G_EDICT(OFS_PARM0);
	channel = G_FLOAT(OFS_PARM1);
	sample = G_STRING(OFS_PARM2);
	volume = G_FLOAT(OFS_PARM3) * 255;
	attenuation = G_FLOAT(OFS_PARM4);

	SV_StartSound (entity, channel, sample, volume, attenuation);
}

/*
=================
PF_break

break()
=================
*/
static void PF_break (void)
{
	Con_Printf ("break statement\n");
	*(int *)-4 = 0;	// dump to debugger
//	PR_RunError ("break statement");
}

static const char *SV_DebugEdictStringField (edict_t *ent, const char *fieldname)
{
	eval_t	*val;

	val = GetEdictFieldValueByName(ent, fieldname);
	if (!val || !val->string)
		return "";
	return PR_GetString(val->string);
}

static qboolean SV_DebugEdictStringEquals (edict_t *ent, const char *fieldname,
	const char *match)
{
	return !strcmp(SV_DebugEdictStringField(ent, fieldname), match);
}

static float SV_DebugEdictFloatField (edict_t *ent, const char *fieldname)
{
	eval_t	*val;

	val = GetEdictFieldValueByName(ent, fieldname);
	if (!val)
		return 0;
	return val->_float;
}

static const char *SV_DebugFieldNameForOffset (int ofs)
{
	int		i;
	ddef_t	*def;

	for (i = 0; i < qcvm->progs->numfielddefs; i++)
	{
		def = &qcvm->fielddefs[i];
		if (def->ofs == ofs)
			return PR_GetString(def->s_name);
	}
	return "";
}

static qboolean SV_DebugIsTargetnameField (const char *fieldname)
{
	return !strcmp(fieldname, "targetname")
		|| !strcmp(fieldname, "targetname2")
		|| !strcmp(fieldname, "targetname3")
		|| !strcmp(fieldname, "targetname4");
}

static void SV_DebugLogFindTargetMatch (edict_t *ent, const char *fieldname,
	const char *match)
{
	if (sv_triggerdebug.value < 2 || !SV_DebugIsTargetnameField(fieldname))
		return;

	Con_Printf("sv_triggerdebug: find %s=\"%s\" -> #%d %s targetname=\"%s\" targetname2=\"%s\" target=\"%s\" target2=\"%s\" target3=\"%s\" target4=\"%s\" count=%.0f cnt=%.0f state=%.0f\n",
		fieldname, match, NUM_FOR_EDICT(ent),
		ent->v.classname ? PR_GetString(ent->v.classname) : "",
		SV_DebugEdictStringField(ent, "targetname"),
		SV_DebugEdictStringField(ent, "targetname2"),
		SV_DebugEdictStringField(ent, "target"),
		SV_DebugEdictStringField(ent, "target2"),
		SV_DebugEdictStringField(ent, "target3"),
		SV_DebugEdictStringField(ent, "target4"),
		SV_DebugEdictFloatField(ent, "count"),
		SV_DebugEdictFloatField(ent, "cnt"),
		SV_DebugEdictFloatField(ent, "state"));
}

static void SV_DebugLogCenterprint (int entnum, const char *s)
{
	if (!sv_triggerdebug.value || !s || !*s)
		return;

	Con_Printf("sv_triggerdebug: centerprint to #%d: %s\n", entnum, s);
}

static qboolean SV_DebugIsToggleWall (edict_t *ent)
{
	const char	*classname;

	if (!ent || ent == qcvm->edicts || ent->free || !ent->v.classname)
		return false;
	classname = PR_GetString(ent->v.classname);
	return !strcmp(classname, "togglewall");
}

static void SV_DebugLogSetOrigin (edict_t *ent, const vec3_t oldorg,
	const vec3_t neworg)
{
	if (!sv_triggerdebug.value || !SV_DebugIsToggleWall(ent))
		return;

	Con_Printf("sv_triggerdebug: setorigin #%d %s targetname=\"%s\" old=(%.1f %.1f %.1f) new=(%.1f %.1f %.1f) state=%.0f\n",
		NUM_FOR_EDICT(ent), ent->v.classname ? PR_GetString(ent->v.classname) : "",
		SV_DebugEdictStringField(ent, "targetname"),
		oldorg[0], oldorg[1], oldorg[2],
		neworg[0], neworg[1], neworg[2],
		SV_DebugEdictFloatField(ent, "state"));
}

static qboolean SV_ValidServerProgEdict (int prog, edict_t **out)
{
	edict_t	*ent;
	int		num;

	if (!qcvm || qcvm != &sv.qcvm || prog <= 0)
		return false;
	if (prog % qcvm->edict_size)
		return false;

	num = prog / qcvm->edict_size;
	if (num <= 0 || num >= qcvm->num_edicts)
		return false;

	ent = PROG_TO_EDICT(prog);
	if (ent->free)
		return false;

	if (out)
		*out = ent;
	return true;
}

static qboolean SV_IsShubCleanupAttacker (edict_t *attacker)
{
	const char	*classname;

	if (!attacker || attacker == qcvm->edicts || attacker->free || !attacker->v.classname)
		return false;

	classname = PR_GetString(attacker->v.classname);
	if (!strcmp(classname, "trigger_hurt"))
		return SV_DebugEdictStringEquals(attacker, "targetname", "hurter");

	/* Shub's Wager also routes cleanup through a targeted teleporter. */
	return !strcmp(classname, "trigger_teleport")
		|| !strcmp(classname, "teledeath");
}

static qboolean SV_IsShubWagerMap (void)
{
	return qcvm == &sv.qcvm && sv.active && !q_strcasecmp(sv.name, "shubswager");
}

static int SV_ShubsWagerResultForTarget (const char *match)
{
	if (!match)
		return 0;
	if (!strcmp(match, "win") || !strcmp(match, "wincnt"))
		return 1;
	if (!strcmp(match, "loss") || !strcmp(match, "losscnt"))
		return -1;
	return 0;
}

static qboolean SV_ShubsWagerSelfTargetsResult (int result)
{
	edict_t	*self;

	if (!SV_ValidServerProgEdict(pr_global_struct->self, &self))
		return false;
	if (!self->v.classname || q_strncasecmp(PR_GetString(self->v.classname), "monster_", 8))
		return false;
	if (!SV_DebugEdictStringEquals(self, "target2", "clearer"))
		return false;

	return (result > 0 && SV_DebugEdictStringEquals(self, "target", "win"))
		|| (result < 0 && SV_DebugEdictStringEquals(self, "target", "loss"));
}

static qboolean SV_ShouldSuppressShubRoundResultFind (int fieldofs,
	const char *fieldname, const char *match)
{
	static int	result_latch;
	int			result;

	if (!SV_IsShubWagerMap())
	{
		result_latch = 0;
		return false;
	}
	if (!fieldname || !*fieldname)
		fieldname = SV_DebugFieldNameForOffset(fieldofs);
	if (!SV_DebugIsTargetnameField(fieldname))
		return false;

	if (!strcmp(match, "rounds"))
	{
		if (result_latch && sv_triggerdebug.value)
			Con_Printf("sv_triggerdebug: shubswager round result latch reset\n");
		result_latch = 0;
		return false;
	}

	result = SV_ShubsWagerResultForTarget(match);
	if (result)
	{
		if (result_latch && result_latch != result)
		{
			if (sv_triggerdebug.value)
				Con_Printf("sv_triggerdebug: suppressed shubswager late %s after %s result latched\n",
					match, result_latch > 0 ? "win" : "loss");
			return true;
		}
		if (!strcmp(match, "wincnt") || !strcmp(match, "losscnt"))
		{
			result_latch = result;
			if (sv_triggerdebug.value)
				Con_Printf("sv_triggerdebug: shubswager latched %s result\n",
					result > 0 ? "win" : "loss");
		}
		return false;
	}

	if (!strcmp(match, "clearer") && result_latch
		&& SV_ShubsWagerSelfTargetsResult(-result_latch))
	{
		if (sv_triggerdebug.value)
			Con_Printf("sv_triggerdebug: suppressed shubswager late clearer after %s result latched\n",
				result_latch > 0 ? "win" : "loss");
		return true;
	}

	return false;
}

static qboolean SV_ShouldSuppressShubCleanupFind (int fieldofs,
	const char *fieldname, const char *match)
{
	edict_t		*self, *attacker;
	const char	*classname;

	if (!SV_IsShubWagerMap())
		return false;
	if (!match || (strcmp(match, "win") && strcmp(match, "loss") && strcmp(match, "clearer")))
		return false;
	if (!fieldname || !*fieldname)
		fieldname = SV_DebugFieldNameForOffset(fieldofs);
	if (!SV_DebugIsTargetnameField(fieldname))
		return false;
	if (!SV_ValidServerProgEdict(pr_global_struct->self, &self))
		return false;
	if (!self->v.classname)
		return false;

	classname = PR_GetString(self->v.classname);
	if (q_strncasecmp(classname, "monster_", 8))
		return false;
	if (!SV_DebugEdictStringEquals(self, "target2", "clearer"))
		return false;
	if (!SV_DebugEdictStringEquals(self, "target", "win")
		&& !SV_DebugEdictStringEquals(self, "target", "loss"))
		return false;
	if (!SV_ValidServerProgEdict(self->v.enemy, &attacker)
		|| !SV_IsShubCleanupAttacker(attacker))
		return false;

	if (sv_triggerdebug.value)
		Con_Printf("sv_triggerdebug: suppressed shubswager cleanup target %s from #%d %s killed by #%d %s\n",
			match, NUM_FOR_EDICT(self), classname, NUM_FOR_EDICT(attacker),
			attacker->v.classname ? PR_GetString(attacker->v.classname) : "");
	return true;
}

static qboolean SV_DebugShouldLogDamageableTrigger (edict_t *ent)
{
	const char	*classname;

	if (!sv_triggerdebug.value || !ent || ent == qcvm->edicts || ent->free)
		return false;
	if (!ent->v.classname || (ent->v.health <= 0 && ent->v.takedamage <= DAMAGE_NO))
		return false;

	classname = PR_GetString(ent->v.classname);
	return !q_strncasecmp(classname, "trigger_", 8)
		|| !q_strncasecmp(classname, "func_", 5);
}

static void SV_DebugLogTraceTrigger (edict_t *ent, const vec3_t start,
	const vec3_t end, const trace_t *trace)
{
	if (!SV_DebugShouldLogDamageableTrigger(ent))
		return;

	Con_Printf("sv_triggerdebug: traceline hit #%d %s solid=%d health=%.1f takedamage=%.0f frac=%.3f start=(%.1f %.1f %.1f) end=(%.1f %.1f %.1f) targetname=\"%s\" target=\"%s\" target2=\"%s\" target3=\"%s\" target4=\"%s\"\n",
		NUM_FOR_EDICT(ent), ent->v.classname ? PR_GetString(ent->v.classname) : "",
		(int)ent->v.solid, ent->v.health, ent->v.takedamage,
		trace ? trace->fraction : 1.0f,
		start[0], start[1], start[2], end[0], end[1], end[2],
		SV_DebugEdictStringField(ent, "targetname"),
		SV_DebugEdictStringField(ent, "target"),
		SV_DebugEdictStringField(ent, "target2"),
		SV_DebugEdictStringField(ent, "target3"),
		SV_DebugEdictStringField(ent, "target4"));
}

/*
=================
PF_traceline

Used for use tracing and shot targeting
Traces are blocked by bbox and exact bsp entityes, and also slide box entities
if the tryents flag is set.

traceline (vector1, vector2, tryents)
=================
*/
static void PF_traceline (void)
{
	float	*v1, *v2;
	trace_t	trace;
	int	nomonsters;
	edict_t	*ent;

	v1 = G_VECTOR(OFS_PARM0);
	v2 = G_VECTOR(OFS_PARM1);
	nomonsters = G_FLOAT(OFS_PARM2);
	ent = G_EDICT(OFS_PARM3);

	/* FIXME FIXME FIXME: Why do we hit this with certain progs.dat ?? */
	if (developer.value) {
	  if (IS_NAN(v1[0]) || IS_NAN(v1[1]) || IS_NAN(v1[2]) ||
	      IS_NAN(v2[0]) || IS_NAN(v2[1]) || IS_NAN(v2[2])) {
	    Con_Warning ("NAN in traceline:\nv1(%f %f %f) v2(%f %f %f)\nentity %d\n",
		      v1[0], v1[1], v1[2], v2[0], v2[1], v2[2], NUM_FOR_EDICT(ent));
	  }
	}

	if (IS_NAN(v1[0]) || IS_NAN(v1[1]) || IS_NAN(v1[2]))
		v1[0] = v1[1] = v1[2] = 0;
	if (IS_NAN(v2[0]) || IS_NAN(v2[1]) || IS_NAN(v2[2]))
		v2[0] = v2[1] = v2[2] = 0;

	trace = SV_Move (v1, vec3_origin, vec3_origin, v2, nomonsters, ent);
	SV_DebugLogTraceTrigger(trace.ent, v1, v2, &trace);
	SV_CoopReviveFromTrace (v1, v2, ent, trace.fraction);

	pr_global_struct->trace_allsolid = trace.allsolid;
	pr_global_struct->trace_startsolid = trace.startsolid;
	pr_global_struct->trace_fraction = trace.fraction;
	pr_global_struct->trace_inwater = trace.inwater;
	pr_global_struct->trace_inopen = trace.inopen;
	VectorCopy (trace.endpos, pr_global_struct->trace_endpos);
	VectorCopy (trace.plane.normal, pr_global_struct->trace_plane_normal);
	pr_global_struct->trace_plane_dist =  trace.plane.dist;
	if (trace.ent)
		pr_global_struct->trace_ent = EDICT_TO_PROG(trace.ent);
	else
		pr_global_struct->trace_ent = EDICT_TO_PROG(qcvm->edicts);
}

/*
=================
PF_checkpos

Returns true if the given entity can move to the given position from it's
current position by walking or rolling.
FIXME: make work...
scalar checkpos (entity, vector)
=================
*/
#if 0
static void PF_checkpos (void)
{
}
#endif

//============================================================================

static byte	*checkpvs;	//ericw -- changed to malloc
static int	checkpvs_capacity;

static int PF_newcheckclient (int check)
{
	int		i;
	byte	*pvs;
	edict_t	*ent;
	mleaf_t	*leaf;
	vec3_t	org;
	int	pvsbytes;

// cycle to the next one

	if (check < 1)
		check = 1;
	if (check > svs.maxclients)
		check = svs.maxclients;

	if (check == svs.maxclients)
		i = 1;
	else
		i = check + 1;

	for ( ;  ; i++)
	{
		if (i == svs.maxclients+1)
			i = 1;

		ent = EDICT_NUM(i);

		if (i == check)
			break;	// didn't find anything else

		if (ent->free)
			continue;
		if (ent->v.health <= 0)
			continue;
		if ((int)ent->v.flags & FL_NOTARGET)
			continue;

	// anything that is a client, or has a client as an enemy
		break;
	}

// get the PVS for the entity
	VectorAdd (ent->v.origin, ent->v.view_ofs, org);
	leaf = Mod_PointInLeaf (org, sv.worldmodel);
	pvs = Mod_LeafPVS (leaf, sv.worldmodel);

	pvsbytes = (sv.worldmodel->numleafs+7)>>3;
	if (checkpvs == NULL || pvsbytes > checkpvs_capacity)
	{
		checkpvs_capacity = pvsbytes;
		checkpvs = (byte *) realloc (checkpvs, checkpvs_capacity);
		if (!checkpvs)
			Sys_Error ("PF_newcheckclient: realloc() failed on %d bytes", checkpvs_capacity);
	}
	memcpy (checkpvs, pvs, pvsbytes);

	return i;
}

/*
=================
PF_checkclient

Returns a client (or object that has a client enemy) that would be a
valid target.

If there are more than one valid options, they are cycled each frame

If (self.origin + self.viewofs) is not in the PVS of the current target,
it is not returned at all.

name checkclient ()
=================
*/
#define	MAX_CHECK	16
static int c_invis, c_notvis;
static void PF_checkclient (void)
{
	edict_t	*ent, *self;
	mleaf_t	*leaf;
	int		l;
	vec3_t	view;

// find a new check if on a new frame
	if (qcvm->time - sv.lastchecktime >= 0.1)
	{
		sv.lastcheck = PF_newcheckclient (sv.lastcheck);
		sv.lastchecktime = qcvm->time;
	}

// return check if it might be visible
	ent = EDICT_NUM(sv.lastcheck);
	if (ent->free || ent->v.health <= 0)
	{
		RETURN_EDICT(qcvm->edicts);
		return;
	}

// if current entity can't possibly see the check entity, return 0
	self = PROG_TO_EDICT(pr_global_struct->self);
	VectorAdd (self->v.origin, self->v.view_ofs, view);
	leaf = Mod_PointInLeaf (view, sv.worldmodel);
	l = (leaf - sv.worldmodel->leafs) - 1;
	if ( (l < 0) || !(checkpvs[l>>3] & (1 << (l & 7))) )
	{
		c_notvis++;
		RETURN_EDICT(qcvm->edicts);
		return;
	}

// might be able to see it
	c_invis++;
	RETURN_EDICT(ent);
}

//============================================================================


/*
=================
PF_stuffcmd

Sends text over to the client's execution buffer

stuffcmd (clientent, value)
=================
*/
static void PF_stuffcmd (void)
{
	int		entnum;
	const char	*str;
	client_t	*old;

	entnum = G_EDICTNUM(OFS_PARM0);
	str = G_STRING(OFS_PARM1);
	if (entnum < 1 || entnum > svs.maxclients)
	{
		Con_DPrintf ("PF_stuffcmd: ignored non-client entity %d\n", entnum);
		return;
	}

	old = host_client;
	host_client = &svs.clients[entnum-1];
	Host_ClientCommands ("%s", str);
	host_client = old;
}

/*
=================
PF_localcmd

Sends text over to the client's execution buffer

localcmd (string)
=================
*/
static void PF_localcmd (void)
{
	const char	*str;

	str = G_STRING(OFS_PARM0);
	Cbuf_AddText (str);
}

/*
=================
PF_cvar

float cvar (string)
=================
*/
static void PF_cvar (void)
{
	const char	*str;

	str = G_STRING(OFS_PARM0);

	G_FLOAT(OFS_RETURN) = Cvar_VariableValue (str);
}

/*
=================
PF_cvar_set

float cvar (string)
=================
*/
static void PF_cvar_set (void)
{
	const char	*var, *val;

	var = G_STRING(OFS_PARM0);
	val = G_STRING(OFS_PARM1);

	Cvar_Set (var, val);
}

static void PF_cvar_setf (void)
{
	const char	*var;
	char	val[32];

	var = G_STRING(OFS_PARM0);
	q_snprintf (val, sizeof(val), "%g", G_FLOAT(OFS_PARM1));
	Cvar_Set (var, val);
}

static void PF_setcolors(void)
{
	edict_t *ed = G_EDICT(OFS_PARM0);
	int colors = (int)G_FLOAT(OFS_PARM1) & 0xff;
	int clientnum = NUM_FOR_EDICT(ed) - 1;

	if (clientnum < 0 || clientnum >= svs.maxclients ||
		!svs.clients[clientnum].active)
	{
		Con_Printf("tried to setcolor a non-client\n");
		return;
	}

	svs.clients[clientnum].colors = colors;
	ed->v.team = (colors & 15) + 1;

	MSG_WriteByte(&sv.reliable_datagram, svc_updatecolors);
	MSG_WriteByte(&sv.reliable_datagram, clientnum);
	MSG_WriteByte(&sv.reliable_datagram, colors);
}

static void PF_registercvar (void)
{
	const char	*var, *val;

	var = G_STRING(OFS_PARM0);
	val = qcvm->argc > 1 ? G_STRING(OFS_PARM1) : "0";
	G_FLOAT(OFS_RETURN) = Cvar_Create (var, val) ? 1 : 0;
}

static void PF_cvar_string (void)
{
	const char	*varname = G_STRING(OFS_PARM0);
	cvar_t		*var = Cvar_FindVar (varname);

	if (var && var->string)
	{
		char *result = PR_GetTempString();
		q_strlcpy (result, var->string, STRINGTEMP_LENGTH);
		G_INT(OFS_RETURN) = PR_SetEngineString(result);
	}
	else if (!Q_strcmp (varname, "game"))
	{
		char *result = PR_GetTempString();
		q_strlcpy (result, COM_SkipPath(com_gamedir), STRINGTEMP_LENGTH);
		G_INT(OFS_RETURN) = PR_SetEngineString(result);
	}
	else
		G_INT(OFS_RETURN) = 0;
}

static void PF_cvar_defstring (void)
{
	cvar_t	*var = Cvar_FindVar (G_STRING(OFS_PARM0));

	if (var && var->default_string)
		G_INT(OFS_RETURN) = PR_SetEngineString(var->default_string);
	else
		G_INT(OFS_RETURN) = 0;
}

static void PF_cvar_type (void)
{
	cvar_t	*var = Cvar_FindVar (G_STRING(OFS_PARM0));
	int		ret = 0;

	if (var)
	{
		ret |= 1;	/* CVAR_TYPE_EXISTS */
		if (var->flags & CVAR_ARCHIVE)
			ret |= 2;	/* CVAR_TYPE_SAVED */
		if (!(var->flags & CVAR_USERDEFINED))
			ret |= 8;	/* CVAR_TYPE_ENGINE */
	}
	G_FLOAT(OFS_RETURN) = ret;
}

static void PF_cvar_description (void)
{
	G_INT(OFS_RETURN) = 0;
}

/*
=================
PF_findradius

Returns a chain of entities that have origins within a spherical area

findradius (origin, radius)
=================
*/
static void PF_findradius (void)
{
	edict_t	*ent, *chain;
	float	rad;
	float	*org;
	int		i;

	chain = (edict_t *)qcvm->edicts;

	org = G_VECTOR(OFS_PARM0);
	rad = G_FLOAT(OFS_PARM1);
	rad *= rad;

	ent = NEXT_EDICT(qcvm->edicts);
	for (i = 1; i < qcvm->num_edicts; i++, ent = NEXT_EDICT(ent))
	{
		float d, lensq;
		if (ent->free)
			continue;
		if (ent->v.solid == SOLID_NOT)
			continue;

		d = org[0] - (ent->v.origin[0] + (ent->v.mins[0] + ent->v.maxs[0]) * 0.5);
		lensq = d * d;
		if (lensq > rad)
			continue;
		d = org[1] - (ent->v.origin[1] + (ent->v.mins[1] + ent->v.maxs[1]) * 0.5);
		lensq += d * d;
		if (lensq > rad)
			continue;
		d = org[2] - (ent->v.origin[2] + (ent->v.mins[2] + ent->v.maxs[2]) * 0.5);
		lensq += d * d;
		if (lensq > rad)
			continue;

		ent->v.chain = EDICT_TO_PROG(chain);
		chain = ent;
	}

	RETURN_EDICT(chain);
}

/*
=========
PF_dprint
=========
*/
static void PF_dprint (void)
{
	Con_DPrintf ("%s",PF_VarString(0));
}

static void PF_ftos (void)
{
	float	v;
	char	*s;

	v = G_FLOAT(OFS_PARM0);
	s = PR_GetTempString();
	if (v == (int)v)
		sprintf (s, "%d",(int)v);
	else
		sprintf (s, "%5.1f",v);
	G_INT(OFS_RETURN) = PR_SetEngineString(s);
}

static void PF_fabs (void)
{
	float	v;
	v = G_FLOAT(OFS_PARM0);
	G_FLOAT(OFS_RETURN) = fabs(v);
}

static void PF_vtos (void)
{
	char	*s;

	s = PR_GetTempString();
	sprintf (s, "'%5.1f %5.1f %5.1f'", G_VECTOR(OFS_PARM0)[0], G_VECTOR(OFS_PARM0)[1], G_VECTOR(OFS_PARM0)[2]);
	G_INT(OFS_RETURN) = PR_SetEngineString(s);
}

static void PF_Spawn (void)
{
	edict_t	*ed;

	ed = ED_Alloc();

	RETURN_EDICT(ed);
}

static void PF_Remove (void)
{
	edict_t	*ed;

	ed = G_EDICT(OFS_PARM0);
	ED_Free (ed);
}


// entity (entity start, .string field, string match) find = #5;
static void PF_Find (void)
{
	int		e;
	int		f;
	const char	*s, *t;
	const char	*fieldname;
	edict_t	*ed;

	e = G_EDICTNUM(OFS_PARM0);
	f = G_INT(OFS_PARM1);
	s = G_STRING(OFS_PARM2);
	if (!s)
		PR_RunError ("PF_Find: bad search string");

	fieldname = sv_triggerdebug.value >= 2 ? SV_DebugFieldNameForOffset(f) : "";
	if (SV_ShouldSuppressShubRoundResultFind(f, fieldname, s))
	{
		RETURN_EDICT(qcvm->edicts);
		return;
	}
	if (SV_ShouldSuppressShubCleanupFind(f, fieldname, s))
	{
		RETURN_EDICT(qcvm->edicts);
		return;
	}
	for (e++ ; e < qcvm->num_edicts ; e++)
	{
		ed = EDICT_NUM(e);
		if (ed->free)
			continue;
		t = E_STRING(ed,f);
		if (!t)
			continue;
		if (!strcmp(t,s))
		{
			SV_DebugLogFindTargetMatch (ed, fieldname, s);
			RETURN_EDICT(ed);
			return;
		}
	}

	RETURN_EDICT(qcvm->edicts);
}

static void PR_CheckEmptyString (const char *s)
{
	if (s[0] <= ' ')
		PR_RunError ("Bad string");
}

static void PF_precache_file (void)
{	// precache_file is only used to copy files with qcc, it does nothing
	G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
}

static void PF_precache_sound (void)
{
	const char	*s;
	int		i;

	if (sv.state != ss_loading)
		PR_RunError ("PF_Precache_*: Precache can only be done in spawn functions");

	s = G_STRING(OFS_PARM0);
	G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
	PR_CheckEmptyString (s);

	for (i = 0; i < MAX_SOUNDS; i++)
	{
		if (!sv.sound_precache[i])
		{
			sv.sound_precache[i] = s;
			return;
		}
		if (!strcmp(sv.sound_precache[i], s))
			return;
	}
	PR_RunError ("PF_precache_sound: overflow");
}

static void PF_precache_model (void)
{
	const char	*s;
	int		i;

	if (sv.state != ss_loading)
		PR_RunError ("PF_Precache_*: Precache can only be done in spawn functions");

	s = G_STRING(OFS_PARM0);
	G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
	PR_CheckEmptyString (s);

	for (i = 0; i < MAX_MODELS; i++)
	{
		if (!sv.model_precache[i])
		{
			sv.model_precache[i] = s;
			sv.models[i] = Mod_ForName (s, true);
			return;
		}
		if (!strcmp(sv.model_precache[i], s))
			return;
	}
	PR_RunError ("PF_precache_model: overflow");
}


static void PF_coredump (void)
{
	ED_PrintEdicts ();
}

static void PF_traceon (void)
{
	qcvm->trace = true;
}

static void PF_traceoff (void)
{
	qcvm->trace = false;
}

static void PF_eprint (void)
{
	ED_PrintNum (G_EDICTNUM(OFS_PARM0));
}

/*
===============
PF_walkmove

float(float yaw, float dist) walkmove
===============
*/
static void PF_walkmove (void)
{
	edict_t	*ent;
	float	yaw, dist;
	vec3_t	move;
	dfunction_t	*oldf;
	int	oldself;

	ent = PROG_TO_EDICT(pr_global_struct->self);
	yaw = G_FLOAT(OFS_PARM0);
	dist = G_FLOAT(OFS_PARM1);

	if ( !( (int)ent->v.flags & (FL_ONGROUND|FL_FLY|FL_SWIM) ) )
	{
		G_FLOAT(OFS_RETURN) = 0;
		return;
	}

	yaw = yaw * M_PI * 2 / 360;

	move[0] = cos(yaw) * dist;
	move[1] = sin(yaw) * dist;
	move[2] = 0;

// save program state, because SV_movestep may call other progs
	oldf = qcvm->xfunction;
	oldself = pr_global_struct->self;

	G_FLOAT(OFS_RETURN) = SV_movestep(ent, move, true);


// restore program state
	qcvm->xfunction = oldf;
	pr_global_struct->self = oldself;
}

/*
===============
PF_droptofloor

void() droptofloor
===============
*/
static void PF_droptofloor (void)
{
	edict_t		*ent;
	vec3_t		end;
	trace_t		trace;

	ent = PROG_TO_EDICT(pr_global_struct->self);

	VectorCopy (ent->v.origin, end);
	end[2] -= 256;

	trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);

	if (trace.fraction == 1 || trace.allsolid)
		G_FLOAT(OFS_RETURN) = 0;
	else
	{
		VectorCopy (trace.endpos, ent->v.origin);
		SV_LinkEdict (ent, false);
		ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
		ent->v.groundentity = EDICT_TO_PROG(trace.ent);
		G_FLOAT(OFS_RETURN) = 1;
	}
}

/*
===============
PF_lightstyle

void(float style, string value) lightstyle
===============
*/
static void PF_lightstyle (void)
{
	int		style;
	const char	*val;
	client_t	*client;
	int	j;

	style = G_FLOAT(OFS_PARM0);
	val = G_STRING(OFS_PARM1);

// bounds check to avoid clobbering sv struct
	if (style < 0 || style >= MAX_LIGHTSTYLES)
	{
		Con_DWarning("PF_lightstyle: invalid style %d\n", style);
		return;
	}

// change the string in sv
	sv.lightstyles[style] = val;

// send message to all clients on this server
	if (sv.state != ss_active)
		return;

	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (client->active || client->spawned)
		{
			MSG_WriteChar (&client->message, svc_lightstyle);
			MSG_WriteChar (&client->message, style);
			MSG_WriteString (&client->message, val);
		}
	}
}

static void PF_rint (void)
{
	float	f;
	f = G_FLOAT(OFS_PARM0);
	if (f > 0)
		G_FLOAT(OFS_RETURN) = (int)(f + 0.5);
	else
		G_FLOAT(OFS_RETURN) = (int)(f - 0.5);
}

static void PF_floor (void)
{
	G_FLOAT(OFS_RETURN) = floor(G_FLOAT(OFS_PARM0));
}

static void PF_ceil (void)
{
	G_FLOAT(OFS_RETURN) = ceil(G_FLOAT(OFS_PARM0));
}


/*
=============
PF_checkbottom
=============
*/
static void PF_checkbottom (void)
{
	edict_t	*ent;

	ent = G_EDICT(OFS_PARM0);

	G_FLOAT(OFS_RETURN) = SV_CheckBottom (ent);
}

/*
=============
PF_pointcontents
=============
*/
static void PF_pointcontents (void)
{
	float	*v;

	v = G_VECTOR(OFS_PARM0);

	G_FLOAT(OFS_RETURN) = SV_PointContents (v);
}

/*
=============
PF_nextent

entity nextent(entity)
=============
*/
static void PF_nextent (void)
{
	int		i;
	edict_t	*ent;

	i = G_EDICTNUM(OFS_PARM0);
	while (1)
	{
		i++;
		if (i == qcvm->num_edicts)
		{
			RETURN_EDICT(qcvm->edicts);
			return;
		}
		ent = EDICT_NUM(i);
		if (!ent->free)
		{
			RETURN_EDICT(ent);
			return;
		}
	}
}

/*
=============
PF_aim

Pick a vector for the player to shoot along
vector aim(entity, missilespeed)
=============
*/
cvar_t	sv_aim = {"sv_aim", "1", CVAR_NONE}; // ericw -- turn autoaim off by default. was 0.93
static void PF_aim (void)
{
	edict_t	*ent, *check, *bestent;
	vec3_t	start, dir, end, bestdir;
	int		i, j;
	trace_t	tr;
	float	dist, bestdist;
	float	speed;

	ent = G_EDICT(OFS_PARM0);
	speed = G_FLOAT(OFS_PARM1);
	(void) speed; /* variable set but not used */

	VectorCopy (ent->v.origin, start);
	start[2] += 20;

// try sending a trace straight
	VectorCopy (pr_global_struct->v_forward, dir);
	VectorMA (start, 2048, dir, end);
	tr = SV_Move (start, vec3_origin, vec3_origin, end, false, ent);
	if (tr.ent && tr.ent->v.takedamage == DAMAGE_AIM
		&& (!teamplay.value || ent->v.team <= 0 || ent->v.team != tr.ent->v.team) )
	{
		VectorCopy (pr_global_struct->v_forward, G_VECTOR(OFS_RETURN));
		return;
	}

// try all possible entities
	VectorCopy (dir, bestdir);
	bestdist = sv_aim.value;
	bestent = NULL;

	check = NEXT_EDICT(qcvm->edicts);
	for (i = 1; i < qcvm->num_edicts; i++, check = NEXT_EDICT(check) )
	{
		if (check->v.takedamage != DAMAGE_AIM)
			continue;
		if (check == ent)
			continue;
		if (teamplay.value && ent->v.team > 0 && ent->v.team == check->v.team)
			continue;	// don't aim at teammate
		for (j = 0; j < 3; j++)
			end[j] = check->v.origin[j] + 0.5 * (check->v.mins[j] + check->v.maxs[j]);
		VectorSubtract (end, start, dir);
		VectorNormalize (dir);
		dist = DotProduct (dir, pr_global_struct->v_forward);
		if (dist < bestdist)
			continue;	// to far to turn
		tr = SV_Move (start, vec3_origin, vec3_origin, end, false, ent);
		if (tr.ent == check)
		{	// can shoot at this one
			bestdist = dist;
			bestent = check;
		}
	}

	if (bestent)
	{
		VectorSubtract (bestent->v.origin, ent->v.origin, dir);
		dist = DotProduct (dir, pr_global_struct->v_forward);
		VectorScale (pr_global_struct->v_forward, dist, end);
		end[2] = dir[2];
		VectorNormalize (end);
		VectorCopy (end, G_VECTOR(OFS_RETURN));
	}
	else
	{
		VectorCopy (bestdir, G_VECTOR(OFS_RETURN));
	}
}

/*
==============
PF_changeyaw

This was a major timewaster in progs, so it was converted to C
==============
*/
void PF_changeyaw (void)
{
	edict_t		*ent;
	float		ideal, current, move, speed;

	ent = PROG_TO_EDICT(pr_global_struct->self);
	current = anglemod( ent->v.angles[1] );
	ideal = ent->v.ideal_yaw;
	speed = ent->v.yaw_speed;

	if (current == ideal)
		return;
	move = ideal - current;
	if (ideal > current)
	{
		if (move >= 180)
			move = move - 360;
	}
	else
	{
		if (move <= -180)
			move = move + 360;
	}
	if (move > 0)
	{
		if (move > speed)
			move = speed;
	}
	else
	{
		if (move < -speed)
			move = -speed;
	}

	ent->v.angles[1] = anglemod (current + move);
}

/*
===============================================================================

MESSAGE WRITING

===============================================================================
*/

static sizebuf_t *WriteDest (void)
{
	int		entnum;
	int		dest;
	edict_t	*ent;

	dest = G_FLOAT(OFS_PARM0);
	switch (dest)
	{
	case MSG_BROADCAST:
		return &sv.datagram;

	case MSG_ONE:
		ent = PROG_TO_EDICT(pr_global_struct->msg_entity);
		entnum = NUM_FOR_EDICT(ent);
		if (entnum < 1 || entnum > svs.maxclients)
			PR_RunError ("WriteDest: not a client");
		return &svs.clients[entnum-1].message;

	case MSG_ALL:
		return &sv.reliable_datagram;

	case MSG_INIT:
		return sv.signon;

	case MSG_EXT_MULTICAST:
	case MSG_EXT_ENTITY:
		return &sv.multicast;

	default:
		PR_RunError ("WriteDest: bad destination");
		break;
	}

	return NULL;
}

static void PF_WriteByte (void)
{
	MSG_WriteByte (WriteDest(), G_FLOAT(OFS_PARM1));
}

static void PF_WriteChar (void)
{
	MSG_WriteChar (WriteDest(), G_FLOAT(OFS_PARM1));
}

static void PF_WriteShort (void)
{
	MSG_WriteShort (WriteDest(), G_FLOAT(OFS_PARM1));
}

static void PF_WriteLong (void)
{
	MSG_WriteLong (WriteDest(), G_FLOAT(OFS_PARM1));
}

static void PF_WriteAngle (void)
{
	MSG_WriteAngle (WriteDest(), G_FLOAT(OFS_PARM1), sv.protocolflags);
}

static void PF_WriteCoord (void)
{
	MSG_WriteCoord (WriteDest(), G_FLOAT(OFS_PARM1), sv.protocolflags);
}

static void PF_WriteString (void)
{
	MSG_WriteString (WriteDest(), LOC_GetString(G_STRING(OFS_PARM1)));
}

static void PF_WriteEntity (void)
{
	MSG_WriteShort (WriteDest(), G_EDICTNUM(OFS_PARM1));
}

//=============================================================================

static void PF_makestatic (void)
{
	edict_t	*ent;
	entity_state_t *st;
	int	modelindex;

	ent = G_EDICT(OFS_PARM0);

	//johnfitz -- don't send invisible static entities
	if (ent->alpha == ENTALPHA_ZERO) {
		ED_Free (ent);
		return;
	}
	//johnfitz

	modelindex = SV_ModelIndex(PR_GetString(ent->v.model));

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (sv.protocol == PROTOCOL_NETQUAKE)
	{
		if (modelindex & 0xFF00 || (int)(ent->v.frame) & 0xFF00)
		{
			ED_Free (ent);
			return; //can't display the correct model & frame, so don't show it at all
		}
	}
	//johnfitz

	if (sv.num_statics == sv.max_statics)
	{
		int nm = sv.max_statics + 128;
		entity_state_t *n = (nm * sizeof(*n) < sv.max_statics * sizeof(*n)) ?
			NULL : (entity_state_t *)realloc (sv.static_entities, nm * sizeof(*n));
		if (!n)
			PR_RunError ("PF_makestatic: out of memory");
		sv.static_entities = n;
		memset (sv.static_entities + sv.max_statics, 0, (nm - sv.max_statics) * sizeof(*n));
		sv.max_statics = nm;
	}

	st = &sv.static_entities[sv.num_statics++];
	*st = nullentitystate;
	VectorCopy (ent->v.origin, st->origin);
	VectorCopy (ent->v.angles, st->angles);
	st->modelindex = modelindex;
	st->frame = ent->v.frame;
	st->colormap = ent->v.colormap;
	st->skin = ent->v.skin;
	st->alpha = ent->alpha;
	// Keep static entity scale out of signon data; QBJ3 has enemies that
	// disappear when their spawnstatic scale is encoded here.
	st->scale = ENTSCALE_DEFAULT;

// throw the entity away now
	ED_Free (ent);
}

//=============================================================================

/*
==============
PF_setspawnparms
==============
*/
static void PF_setspawnparms (void)
{
	edict_t	*ent;
	int		i;
	client_t	*client;

	ent = G_EDICT(OFS_PARM0);
	i = NUM_FOR_EDICT(ent);
	if (i < 1 || i > svs.maxclients)
		PR_RunError ("Entity is not a client");

	// copy spawn parms out of the client_t
	client = svs.clients + (i-1);

	for (i = 0; i < NUM_SPAWN_PARMS; i++)
		(&pr_global_struct->parm1)[i] = client->spawn_parms[i];
}

/*
==============
PF_changelevel
==============
*/
static void PF_changelevel (void)
{
	const char	*s;

// make sure we don't issue two changelevels
	if (svs.changelevel_issued)
		return;
	svs.changelevel_issued = true;

	s = G_STRING(OFS_PARM0);
	Cbuf_AddText (va("changelevel %s\n",s));
}

/*
==============
2021 re-release
==============
*/
static void PF_finalefinished (void)
{
	G_FLOAT(OFS_RETURN) = 0;
}
static void PF_CheckPlayerEXFlags (void)
{
	G_FLOAT(OFS_RETURN) = 0;
}
static void PF_walkpathtogoal (void)
{
	G_FLOAT(OFS_RETURN) = 0; /* PATH_ERROR */
}
static void PF_localsound (void)
{
	const char	*sample;
	int		entnum;

	entnum = G_EDICTNUM(OFS_PARM0);
	sample = G_STRING(OFS_PARM1);
	if (entnum < 1 || entnum > svs.maxclients) {
		Con_Printf ("tried to localsound to a non-client\n");
		return;
	}
	SV_LocalSound (&svs.clients[entnum-1], sample);
}

void PF_Fixme (void)
{
	PR_RunError ("unimplemented builtin");
}

/*
===============================================================================

EXTENSION BUILT-INS

===============================================================================
*/

cvar_t pr_checkextension = {"pr_checkextension", "1", CVAR_NONE};	//spike - enables qc extensions. if 0 then they're ALL BLOCKED! MWAHAHAHA! *cough* *splutter*

static qboolean PF_ExtensionSupported(const char *extname)
{
	static const char *supported[] = {
		"FTE_QC_CHECKCOMMAND",
		"FTE_STRINGS",
		"DP_QC_ASINACOSATANATAN2TAN",
		"DP_QC_CVAR_DEFSTRING",
		"DP_QC_CVAR_DESCRIPTION",
		"DP_QC_CVAR_STRING",
		"DP_QC_CVAR_TYPE",
		"DP_QC_COPYENTITY",
		"DP_QC_CRC16",
		"DP_QC_EDICT_NUM",
		"DP_QC_ENTITYDATA",
		"DP_QC_GETSURFACE",
		"DP_QC_GETSURFACEPOINTATTRIBUTE",
		"DP_QC_GETSURFACETRIANGLE",
		"DP_GFX_FOG",
		"DP_QC_ETOS",
		"DP_QC_FINDCHAIN",
		"DP_QC_FINDCHAINFLOAT",
		"DP_QC_FINDFLAGS",
		"DP_QC_FINDFLOAT",
		"DP_QC_MINMAXBOUND",
		"DP_QC_NUM_FOR_EDICT",
		"DP_QC_SEARCH",
		"DP_QC_SINCOSSQRTPOW",
		"DP_QC_SPRINTF",
		"DP_QC_STRFTIME",
		"DP_QC_STRINGBUFFERS",
		"DP_QC_STRING_CASE_FUNCTIONS",
		"DP_QC_STRINGCOLORFUNCTIONS",
		"DP_QC_TOKENIZEBYSEPARATOR",
		"DP_QC_TOKENIZE_CONSOLE",
		"DP_QC_STRREPLACE",
		"DP_QC_URI_ESCAPE",
		"DP_QC_VECTORVECTORS",
		"DP_QC_WHICHPACK",
		"DP_TE_PARTICLERAIN",
		"DP_TE_PARTICLESNOW",
		"DP_SV_POINTPARTICLES",
		"DP_SV_SETCOLOR",
		"DP_ENT_TRAILEFFECTNUM",
		"FTE_CALLFUNCTION",
		"FTE_QC_CHECKBUILTIN",
		"FTE_PART_SCRIPT",
		"FTE_PART_NAMESPACES",
		"FTE_PART_NAMESPACE_EFFECTINFO",
		"FTE_SV_POINTPARTICLES",
		"FRIK_FILE",
		"ZQ_QC_STRINGS",
		NULL
	};
	const char **name;

	for (name = supported; *name; name++)
		if (!q_strcasecmp(extname, *name))
			return true;
	return false;
}

static void PF_checkextension(void)
{
	const char *extname = G_STRING(OFS_PARM0);

	G_FLOAT(OFS_RETURN) = pr_checkextension.value &&
		PF_ExtensionSupported(extname);
}

static void PF_checkbuiltin(void)
{
	func_t funcref = G_FUNCTION(OFS_PARM0);

	G_FLOAT(OFS_RETURN) = 0;
	if ((unsigned int)funcref < (unsigned int)qcvm->progs->numfunctions)
	{
		dfunction_t *func = &qcvm->functions[funcref];
		int binum = -func->first_statement;
		if (binum > 0 && binum < qcvm->numbuiltins &&
			qcvm->builtins[binum] != PF_Fixme)
			G_FLOAT(OFS_RETURN) = 1;
	}
}

static void PF_builtin_find(void)
{
	const char *name = G_STRING(OFS_PARM0);
	int i;

	G_FLOAT(OFS_RETURN) = 0;
	if (!name || !*name)
		return;

	for (i = 0; i < pr_numbuiltindefs; i++)
	{
		if (!q_strcasecmp(name, pr_builtindefs[i].name))
		{
			G_FLOAT(OFS_RETURN) = pr_builtindefs[i].number;
			return;
		}
	}
}

static void PF_cl_print(void)
{
	Con_Printf("%s", PF_VarString(0));
}

static void PF_cl_cprint(void)
{
	SCR_CenterPrint(PF_VarString(0));
}

static void PF_wasfreed(void)
{
	edict_t *ed = G_EDICT(OFS_PARM0);

	G_FLOAT(OFS_RETURN) = ed->free;
}

static void PF_gettime(void)
{
	int timer = qcvm->argc > 0 ? (int)G_FLOAT(OFS_PARM0) : 0;

	switch (timer)
	{
	case 1:
		G_FLOAT(OFS_RETURN) = realtime;
		break;
	default:
		G_FLOAT(OFS_RETURN) = Sys_DoubleTime();
		break;
	}
}

static void PF_cl_getresolution(void)
{
	G_VECTORSET(OFS_RETURN, vid.conwidth, vid.conheight, 0);
}

static float PR_GetVMScale(void)
{	//sigh, this is horrible (divides glwidth)
	return CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
}

#define ishex(c) ((c>='0' && c<= '9') || (c>='a' && c<='f') || (c>='A' && c<='F'))
static int dehex(char c)
{
	if (c >= '0' && c <= '9')
		return c-'0';
	if (c >= 'A' && c <= 'F')
		return c-('A'-10);
	if (c >= 'a' && c <= 'f')
		return c-('a'-10);
	return 0;
}
//returns the next char...
struct markup_s
{
	const unsigned char *txt;
	vec4_t tint;	//predefined colour that applies to the entire string
	vec4_t colour;	//colour for the specific glyph in question
	unsigned char mask;
};
void PR_Markup_Begin(struct markup_s *mu, const char *text, float *rgb, float alpha)
{
	if (*text == '\1' || *text == '\2')
	{
		mu->mask = 128;
		text++;
	}
	else
		mu->mask = 0;
	mu->txt = (const unsigned char *)text;
	VectorCopy(rgb, mu->tint);
	mu->tint[3] = alpha;
	VectorCopy(rgb, mu->colour);
	mu->colour[3] = alpha;
}
int PR_Markup_Parse(struct markup_s *mu)
{
	static const vec4_t q3rgb[10] = {
		{0.00,0.00,0.00, 1.0},
		{1.00,0.33,0.33, 1.0},
		{0.00,1.00,0.33, 1.0},
		{1.00,1.00,0.33, 1.0},
		{0.33,0.33,1.00, 1.0},
		{0.33,1.00,1.00, 1.0},
		{1.00,0.33,1.00, 1.0},
		{1.00,1.00,1.00, 1.0},
		{1.00,1.00,1.00, 0.5},
		{0.50,0.50,0.50, 1.0}
	};
	unsigned int c;
	const float *f;
	while ((c = *mu->txt))
	{
		if (c == '^' && false/*pr_checkextension.value*/)
		{	//parse markup like FTE/DP might.
			switch(mu->txt[1])
			{
			case '^':	//doubled up char for escaping.
				mu->txt++;
				break;
			case '0':	//black
			case '1':	//red
			case '2':	//green
			case '3':	//yellow
			case '4':	//blue
			case '5':	//cyan
			case '6':	//magenta
			case '7':	//white
			case '8':	//white+half-alpha
			case '9':	//grey
				f = q3rgb[mu->txt[1]-'0'];
				mu->colour[0] = mu->tint[0] * f[0];
				mu->colour[1] = mu->tint[1] * f[1];
				mu->colour[2] = mu->tint[2] * f[2];
				mu->colour[3] = mu->tint[3] * f[3];
				mu->txt+=2;
				continue;
			case 'h':	//toggle half-alpha
				if (mu->colour[3] != mu->tint[3] * 0.5)
					mu->colour[3] = mu->tint[3] * 0.5;
				else
					mu->colour[3] = mu->tint[3];
				mu->txt+=2;
				continue;
			case 'd':	//reset to defaults (fixme: should reset ^m without resetting \1)
				mu->colour[0] = mu->tint[0];
				mu->colour[1] = mu->tint[1];
				mu->colour[2] = mu->tint[2];
				mu->colour[3] = mu->tint[3];
				mu->mask = 0;
				mu->txt+=2;
				break;
			case 'b':	//blink
			case 's':	//modstack push
			case 'r':	//modstack restore
				mu->txt+=2;
				continue;
			case 'x':	//RGB 12-bit colour
				if (ishex(mu->txt[2]) && ishex(mu->txt[3]) && ishex(mu->txt[4]))
				{
					mu->colour[0] = mu->tint[0] * dehex(mu->txt[2])/15.0;
					mu->colour[1] = mu->tint[1] * dehex(mu->txt[3])/15.0;
					mu->colour[2] = mu->tint[2] * dehex(mu->txt[4])/15.0;
					mu->txt+=5;
					continue;
				}
				break;	//malformed
			case '[':	//start fte's ^[text\key\value\key\value^] links
			case ']':	//end link
				break;	//fixme... skip the keys, recolour properly, etc
		//				txt+=2;
		//				continue;
			case '&':
				if ((ishex(mu->txt[2])||mu->txt[2]=='-') && (ishex(mu->txt[3])||mu->txt[3]=='-'))
				{	//ignore fte's fore/back ansi colours
					mu->txt += 4;
					continue;
				}
				break;	//malformed
			case 'a':	//alternate charset (read: masked)...
			case 'm':	//toggle masking.
				mu->txt+=2;
				mu->mask ^= 128;
				continue;
			case 'U':	//ucs-2 unicode codepoint
				if (ishex(mu->txt[2]) && ishex(mu->txt[3]) && ishex(mu->txt[4]) && ishex(mu->txt[5]))
				{
					c = (dehex(mu->txt[2])<<12) | (dehex(mu->txt[3])<<8) | (dehex(mu->txt[4])<<4) | dehex(mu->txt[5]);
					mu->txt += 6;

					if (c >= 0xe000 && c <= 0xe0ff)
						c &= 0xff;	//private-use 0xE0XX maps to quake's chars
					else if (c >= 0x20 && c <= 0x7f)
						c &= 0x7f;	//ascii is okay too.
					else
						c = '?'; //otherwise its some unicode char that we don't know how to handle.
					return c;
				}
				break; //malformed
			case '{':	//full unicode codepoint, for chars up to 0x10ffff
				mu->txt += 2;
				c = 0;	//no idea
				while(*mu->txt)
				{
					if (*mu->txt == '}')
					{
						mu->txt++;
						break;
					}
					if (!ishex(*mu->txt))
						break;
					c<<=4;
					c |= dehex(*mu->txt++);
				}

				if (c >= 0xe000 && c <= 0xe0ff)
					c &= 0xff;	//private-use 0xE0XX maps to quake's chars
				else if (c >= 0x20 && c <= 0x7f)
					c &= 0x7f;	//ascii is okay too.
				//it would be nice to include a table to de-accent latin scripts, as well as translate cyrilic somehow, but not really necessary.
				else
					c = '?'; //otherwise its some unicode char that we don't know how to handle.
				return c;
			}
		}

		//regular char
		mu->txt++;
		return c|mu->mask;
	}
	return 0;
}


static void PF_cl_getstat_int(void)
{
	int stnum = G_FLOAT(OFS_PARM0);
	if (stnum < 0 || stnum >= countof(cl.stats))
		G_INT(OFS_RETURN) = 0;
	else
		G_INT(OFS_RETURN) = cl.stats[stnum];
}
static void PF_cl_getstat_float(void)
{
	int stnum = G_FLOAT(OFS_PARM0);
	if (stnum < 0 || stnum >= countof(cl.stats))
		G_FLOAT(OFS_RETURN) = 0;
	else if (qcvm->argc > 1)
	{
		int firstbit = G_FLOAT(OFS_PARM1);
		int bitcount = G_FLOAT(OFS_PARM2);
		G_FLOAT(OFS_RETURN) = (cl.stats[stnum]>>firstbit) & ((1<<bitcount)-1);
	}
	else
		G_FLOAT(OFS_RETURN) = cl.statsf[stnum];
}
static void PF_cl_getstat_string(void)
{
	int stnum = G_FLOAT(OFS_PARM0);
	if (stnum < 0 || stnum >= countof(cl.statss) || !cl.statss[stnum])
		G_INT(OFS_RETURN) = 0;
	else
	{
		char *result = PR_GetTempString();
		q_strlcpy(result, cl.statss[stnum], STRINGTEMP_LENGTH);
		G_INT(OFS_RETURN) = PR_SetEngineString(result);
	}
}

static void PF_cl_playerkey_internal(int player, const char *key, qboolean retfloat)
{
	char buf[1024];
	const char *ret = buf;
	extern int fragsort[MAX_SCOREBOARD];
	extern int scoreboardlines;

	if (player < 0 && player >= -scoreboardlines)
		player = fragsort[-1-player];

	if (!cl.scores || player < 0 || player >= cl.maxclients || player >= MAX_SCOREBOARD)
		ret = NULL;
	else if (!strcmp(key, "viewentity"))
		q_snprintf(buf, sizeof(buf), "%i", player + 1);
	else if (!*cl.scores[player].name)
		ret = NULL;
	else if (!strcmp(key, "name"))
		ret = cl.scores[player].name;
	else if (!strcmp(key, "frags"))
		q_snprintf(buf, sizeof(buf), "%i", cl.scores[player].frags);
	else if (!strcmp(key, "ping"))
		q_snprintf(buf, sizeof(buf), "%i", cl.scores[player].ping);
	else if (!strcmp(key, "pl"))
		ret = NULL;
	else if (!strcmp(key, "entertime"))
		q_snprintf(buf, sizeof(buf), "%g", cl.scores[player].entertime);
	else if (!strcmp(key, "topcolor_rgb"))
	{
		byte *pal = (byte *)&d_8to24table[(cl.scores[player].colors & 0xf0) + 8];
		q_snprintf(buf, sizeof(buf), "%g %g %g", pal[0] / 255.0, pal[1] / 255.0, pal[2] / 255.0);
	}
	else if (!strcmp(key, "bottomcolor_rgb"))
	{
		byte *pal = (byte *)&d_8to24table[((cl.scores[player].colors & 0x0f) << 4) + 8];
		q_snprintf(buf, sizeof(buf), "%g %g %g", pal[0] / 255.0, pal[1] / 255.0, pal[2] / 255.0);
	}
	else if (!strcmp(key, "topcolor"))
		q_snprintf(buf, sizeof(buf), "%i", (cl.scores[player].colors & 0xf0) >> 4);
	else if (!strcmp(key, "bottomcolor"))
		q_snprintf(buf, sizeof(buf), "%i", cl.scores[player].colors & 0x0f);
	else if (!strcmp(key, "team"))
		q_snprintf(buf, sizeof(buf), "%i", (cl.scores[player].colors & 0x0f) + 1);
	else
		ret = NULL;

	if (retfloat)
		G_FLOAT(OFS_RETURN) = ret ? atof(ret) : 0;
	else if (ret)
	{
		char *result = PR_GetTempString();
		q_strlcpy(result, ret, STRINGTEMP_LENGTH);
		G_INT(OFS_RETURN) = PR_SetEngineString(result);
	}
	else
		G_INT(OFS_RETURN) = 0;
}

static void PF_cl_playerkey_s(void)
{
	int playernum = G_FLOAT(OFS_PARM0);
	const char *keyname = G_STRING(OFS_PARM1);
	PF_cl_playerkey_internal(playernum, keyname, false);
}

static void PF_cl_playerkey_f(void)
{
	int playernum = G_FLOAT(OFS_PARM0);
	const char *keyname = G_STRING(OFS_PARM1);
	PF_cl_playerkey_internal(playernum, keyname, true);
}

static void PF_serverkey_internal(const char *key, qboolean retfloat)
{
	char buf[1024];
	const char *ret = NULL;

	if (!strcmp(key, "constate"))
	{
		if (cls.state != ca_connected)
			ret = "disconnected";
		else if (cls.signon == SIGNONS)
			ret = "active";
		else
			ret = "connecting";
	}
	else if (!strcmp(key, "deathmatch"))
		ret = qcvm == &sv.qcvm ? (deathmatch.value ? "1" : "0") :
			(cl.gametype == GAME_DEATHMATCH ? "1" : "0");
	else if (!strcmp(key, "coop"))
		ret = qcvm == &sv.qcvm ? (coop.value ? "1" : "0") :
			((cl.maxclients > 1 && cl.gametype != GAME_DEATHMATCH) ? "1" : "0");
	else if (!strcmp(key, "teamplay"))
		ret = qcvm == &sv.qcvm ? (teamplay.value ? "1" : "0") :
			Cvar_VariableString(key);
	else if (!strcmp(key, "maxclients"))
	{
		q_snprintf(buf, sizeof(buf), "%i", qcvm == &sv.qcvm ? svs.maxclients : cl.maxclients);
		ret = buf;
	}
	else if (!strcmp(key, "mapname"))
		ret = qcvm == &sv.qcvm ? sv.name : cl.mapname;
	else
		ret = Cvar_VariableString(key);

	if (!ret || !*ret)
		ret = "";

	if (retfloat)
		G_FLOAT(OFS_RETURN) = atof(ret);
	else
	{
		char *result = PR_GetTempString();
		q_strlcpy(result, ret, STRINGTEMP_LENGTH);
		G_INT(OFS_RETURN) = PR_SetEngineString(result);
	}
}

static void PF_serverkey_s(void)
{
	PF_serverkey_internal(G_STRING(OFS_PARM0), false);
}

static void PF_serverkey_f(void)
{
	PF_serverkey_internal(G_STRING(OFS_PARM0), true);
}

static void PF_cl_readbyte(void)
{
	G_FLOAT(OFS_RETURN) = MSG_ReadByte();
}

static void PF_cl_readchar(void)
{
	G_FLOAT(OFS_RETURN) = MSG_ReadChar();
}

static void PF_cl_readshort(void)
{
	G_FLOAT(OFS_RETURN) = MSG_ReadShort();
}

static void PF_cl_readlong(void)
{
	G_FLOAT(OFS_RETURN) = MSG_ReadLong();
}

static void PF_cl_readcoord(void)
{
	G_FLOAT(OFS_RETURN) = MSG_ReadCoord(cl.protocolflags);
}

static void PF_cl_readangle(void)
{
	G_FLOAT(OFS_RETURN) = MSG_ReadAngle(cl.protocolflags);
}

static void PF_cl_readstring(void)
{
	char *result = PR_GetTempString();
	q_strlcpy(result, MSG_ReadString(), STRINGTEMP_LENGTH);
	G_INT(OFS_RETURN) = PR_SetEngineString(result);
}

static void PF_cl_readfloat(void)
{
	G_FLOAT(OFS_RETURN) = MSG_ReadFloat();
}

static void PF_cl_readentitynum(void)
{
	G_FLOAT(OFS_RETURN) = MSG_ReadEntity(cl.protocol_pext2);
}

static struct
{
	char name[MAX_QPATH];
	unsigned int flags;
	qpic_t *pic;
} *qcpics;
static size_t numqcpics;
static size_t maxqcpics;
void PR_ReloadPics(qboolean purge)
{
	numqcpics = 0;

	free(qcpics);
	qcpics = NULL;
	maxqcpics = 0;
}
#define PICFLAG_AUTO		0	//value used when no flags known
#define PICFLAG_WAD			(1u<<0)	//name matches that of a wad lump
//#define PICFLAG_TEMP		(1u<<1)
#define PICFLAG_WRAP		(1u<<2)	//make sure npot stuff doesn't break wrapping.
#define PICFLAG_MIPMAP		(1u<<3)	//disable use of scrap...
//#define PICFLAG_DOWNLOAD	(1u<<8)	//request to download it from the gameserver if its not stored locally.
#define PICFLAG_BLOCK		(1u<<9)	//wait until the texture is fully loaded.
#define PICFLAG_NOLOAD		(1u<<31)
static qpic_t *DrawQC_CachePic(const char *picname, unsigned int flags)
{	//okay, so this is silly. we've ended up with 3 different cache levels. qcpics, pics, and images.
	size_t i;
	unsigned int texflags;
	for (i = 0; i < numqcpics; i++)
	{	//binary search? something more sane?
		if (!strcmp(picname, qcpics[i].name))
		{
			if (qcpics[i].pic)
				return qcpics[i].pic;
			break;
		}
	}

	if (strlen(picname) >= MAX_QPATH)
		return NULL;	//too long. get lost.

	if (flags & PICFLAG_NOLOAD)
		return NULL;	//its a query, not actually needed.

	if (i+1 > maxqcpics)
	{
		maxqcpics = i + 32;
		qcpics = realloc(qcpics, maxqcpics * sizeof(*qcpics));
	}

	strcpy(qcpics[i].name, picname);
	qcpics[i].flags = flags;
	qcpics[i].pic = NULL;

	texflags = TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP | TEXPREF_CLAMP;
	if (flags & PICFLAG_WRAP)
		texflags &= ~(TEXPREF_PAD | TEXPREF_CLAMP);	//don't allow padding if its going to need to wrap (even if we don't enable clamp-to-edge normally). I just hope we have npot support.
	if (flags & PICFLAG_MIPMAP)
		texflags |= TEXPREF_MIPMAP;

	//try to load it from a wad if applicable.
	//the extra gfx/ crap is because DP insists on it for wad images. and its a nightmare to get things working in all engines if we don't accept that quirk too.
	if (flags & PICFLAG_WAD)
		qcpics[i].pic = Draw_PicFromWad2 (picname + (strncmp(picname, "gfx/", 4)?0:4), texflags);
	else if (!strncmp(picname, "gfx/", 4) && !strchr(picname+4, '.'))
		qcpics[i].pic = Draw_PicFromWad2(picname+4, texflags);

	//okay, not a wad pic, try and load a lmp/tga/etc
	if (!qcpics[i].pic)
		qcpics[i].pic = Draw_TryCachePic(picname, texflags);

	if (i == numqcpics)
		numqcpics++;

	return qcpics[i].pic;
}
static void DrawQC_CharacterQuad (float x, float y, int num, float w, float h)
{
	Draw_CharacterEx (x, y, w, h, num);
}
static void PF_cl_drawcharacter(void)
{
	extern gltexture_t *char_texture;

	float *pos	= G_VECTOR(OFS_PARM0);
	int charcode= (int)G_FLOAT (OFS_PARM1) & 0xff;
	float *size	= G_VECTOR(OFS_PARM2);
	float *rgb	= G_VECTOR(OFS_PARM3);
	float alpha	= G_FLOAT (OFS_PARM4);
//	int flags	= G_FLOAT (OFS_PARM5);

	if (charcode == 32)
		return; //don't waste time on spaces

	GL_SetCanvasColor (rgb[0], rgb[1], rgb[2], alpha);
	DrawQC_CharacterQuad (pos[0], pos[1], charcode, size[0], size[1]);
	GL_SetCanvasColor (1.f, 1.f, 1.f, 1.f);
}

static void PF_cl_drawrawstring(void)
{
	extern gltexture_t *char_texture;

	float *pos	= G_VECTOR(OFS_PARM0);
	const char *text = G_STRING (OFS_PARM1);
	float *size	= G_VECTOR(OFS_PARM2);
	float *rgb	= G_VECTOR(OFS_PARM3);
	float alpha	= G_FLOAT (OFS_PARM4);
//	int flags	= G_FLOAT (OFS_PARM5);

	float x = pos[0];
	int c;

	if (!*text)
		return; //don't waste time on spaces

	GL_SetCanvasColor (rgb[0], rgb[1], rgb[2], alpha);
	while ((c = *text++))
	{
		DrawQC_CharacterQuad (x, pos[1], c, size[0], size[1]);
		x += size[0];
	}
	GL_SetCanvasColor (1.f, 1.f, 1.f, 1.f);
}
static void PF_cl_drawstring(void)
{
	extern gltexture_t *char_texture;

	float *pos	= G_VECTOR(OFS_PARM0);
	const char *text = G_STRING (OFS_PARM1);
	float *size	= G_VECTOR(OFS_PARM2);
	float *rgb	= G_VECTOR(OFS_PARM3);
	float alpha	= G_FLOAT (OFS_PARM4);
//	int flags	= G_FLOAT (OFS_PARM5);

	float x = pos[0];
	struct markup_s mu;
	int c;

	if (!*text)
		return; //don't waste time on spaces

	PR_Markup_Begin(&mu, text, rgb, alpha);

	while ((c = PR_Markup_Parse(&mu)))
	{
		GL_SetCanvasColor (mu.colour[0], mu.colour[1], mu.colour[2], mu.colour[3]);
		DrawQC_CharacterQuad (x, pos[1], c, size[0], size[1]);
		x += size[0];
	}
	GL_SetCanvasColor (1.f, 1.f, 1.f, 1.f);
}
static void PF_cl_stringwidth(void)
{
	static const float defaultfontsize[] = {8,8};
	const char *text = G_STRING (OFS_PARM0);
	qboolean usecolours = G_FLOAT(OFS_PARM1);
	const float *fontsize = (qcvm->argc>2)?G_VECTOR (OFS_PARM2):defaultfontsize;
	struct markup_s mu;
	int r = 0;

	if (!usecolours)
		r = strlen(text);
	else
	{
		PR_Markup_Begin(&mu, text, vec3_origin, 1);
		while (PR_Markup_Parse(&mu))
		{
			r += 1;
		}
	}

	//primitive and lame, but hey.
	G_FLOAT(OFS_RETURN) = fontsize[0] * r;
}

static void PF_cl_drawline(void)
{
	float width = qcvm->argc > 0 ? G_FLOAT(OFS_PARM0) : 1;
	float *pos1 = G_VECTOR(OFS_PARM1);
	float *pos2 = G_VECTOR(OFS_PARM2);
	float *rgb = G_VECTOR(OFS_PARM3);
	float alpha = qcvm->argc > 4 ? G_FLOAT(OFS_PARM4) : 1;

	Draw_Flush ();
	glDisable (GL_TEXTURE_2D);
	glEnable (GL_BLEND);
	glDisable (GL_ALPHA_TEST);
	glLineWidth (q_max(1.0f, width * PR_GetVMScale()));
	glColor4f (rgb[0], rgb[1], rgb[2], alpha);
	glBegin (GL_LINES);
	glVertex2f (pos1[0], pos1[1]);
	glVertex2f (pos2[0], pos2[1]);
	glEnd ();
	glLineWidth (1);
	glColor4f (1, 1, 1, 1);
	glDisable (GL_BLEND);
	glEnable (GL_ALPHA_TEST);
	glEnable (GL_TEXTURE_2D);
}

static void PF_modelnameforindex(void)
{
	int idx = G_FLOAT(OFS_PARM0);
	const char *name = NULL;

	if (qcvm == &sv.qcvm)
	{
		if (idx >= 0 && idx < MAX_MODELS)
			name = sv.model_precache[idx];
	}
	else if (qcvm == &cl.qcvm)
	{
		if (idx >= 0 && idx < MAX_MODELS && cl.model_precache[idx])
			name = cl.model_precache[idx]->name;
	}

	if (name && *name)
	{
		char *result = PR_GetTempString();
		q_strlcpy(result, name, STRINGTEMP_LENGTH);
		G_INT(OFS_RETURN) = PR_SetEngineString(result);
	}
	else
		G_INT(OFS_RETURN) = 0;
}

static qmodel_t *PF_ModelForEdict(edict_t *ed)
{
	int idx = (int)ed->v.modelindex;

	if (idx <= 0 || idx >= MAX_MODELS)
		return NULL;
	if (qcvm == &sv.qcvm)
		return sv.models[idx];
	if (qcvm == &cl.qcvm)
		return cl.model_precache[idx];
	return NULL;
}

static qboolean PF_GetBrushSurface(edict_t *ed, unsigned int surfidx,
	qmodel_t **model, msurface_t **surface)
{
	qmodel_t *mod = PF_ModelForEdict(ed);
	unsigned int absolute;

	if (!mod || mod->type != mod_brush || mod->needload ||
		surfidx >= (unsigned int)mod->nummodelsurfaces)
		return false;

	absolute = mod->firstmodelsurface + surfidx;
	if (absolute >= (unsigned int)mod->numsurfaces)
		return false;

	*model = mod;
	*surface = &mod->surfaces[absolute];
	return true;
}

static void PF_ReturnVector(vec3_t v)
{
	G_FLOAT(OFS_RETURN + 0) = v[0];
	G_FLOAT(OFS_RETURN + 1) = v[1];
	G_FLOAT(OFS_RETURN + 2) = v[2];
}

static void PF_ReturnZeroVector(void)
{
	G_FLOAT(OFS_RETURN + 0) = 0;
	G_FLOAT(OFS_RETURN + 1) = 0;
	G_FLOAT(OFS_RETURN + 2) = 0;
}

static mvertex_t *PF_GetSurfaceVertex(qmodel_t *mod, msurface_t *surf,
	unsigned int vert)
{
	int edge = mod->surfedges[surf->firstedge + vert];

	if (edge >= 0)
		return &mod->vertexes[mod->edges[edge].v[0]];
	return &mod->vertexes[mod->edges[-edge].v[1]];
}

static void PF_getsurfacenumpoints(void)
{
	edict_t *ed = G_EDICT(OFS_PARM0);
	unsigned int surfidx = (unsigned int)G_FLOAT(OFS_PARM1);
	qmodel_t *mod;
	msurface_t *surf;

	if (PF_GetBrushSurface(ed, surfidx, &mod, &surf))
		G_FLOAT(OFS_RETURN) = surf->numedges;
	else
		G_FLOAT(OFS_RETURN) = 0;
}

static void PF_getsurfacepoint(void)
{
	edict_t *ed = G_EDICT(OFS_PARM0);
	unsigned int surfidx = (unsigned int)G_FLOAT(OFS_PARM1);
	unsigned int point = (unsigned int)G_FLOAT(OFS_PARM2);
	qmodel_t *mod;
	msurface_t *surf;
	mvertex_t *v;

	if (PF_GetBrushSurface(ed, surfidx, &mod, &surf) &&
		point < (unsigned int)surf->numedges)
	{
		v = PF_GetSurfaceVertex(mod, surf, point);
		PF_ReturnVector(v->position);
	}
	else
		PF_ReturnZeroVector();
}

static void PF_getsurfacenumtriangles(void)
{
	edict_t *ed = G_EDICT(OFS_PARM0);
	unsigned int surfidx = (unsigned int)G_FLOAT(OFS_PARM1);
	qmodel_t *mod;
	msurface_t *surf;

	if (PF_GetBrushSurface(ed, surfidx, &mod, &surf) && surf->numedges >= 3)
		G_FLOAT(OFS_RETURN) = surf->numedges - 2;
	else
		G_FLOAT(OFS_RETURN) = 0;
}

static void PF_getsurfacetriangle(void)
{
	edict_t *ed = G_EDICT(OFS_PARM0);
	unsigned int surfidx = (unsigned int)G_FLOAT(OFS_PARM1);
	unsigned int triangleidx = (unsigned int)G_FLOAT(OFS_PARM2);
	qmodel_t *mod;
	msurface_t *surf;

	if (PF_GetBrushSurface(ed, surfidx, &mod, &surf) &&
		surf->numedges >= 3 && triangleidx < (unsigned int)surf->numedges - 2)
	{
		G_FLOAT(OFS_RETURN + 0) = 0;
		G_FLOAT(OFS_RETURN + 1) = triangleidx + 1;
		G_FLOAT(OFS_RETURN + 2) = triangleidx + 2;
	}
	else
		PF_ReturnZeroVector();
}

static void PF_getsurfacenormal(void)
{
	edict_t *ed = G_EDICT(OFS_PARM0);
	unsigned int surfidx = (unsigned int)G_FLOAT(OFS_PARM1);
	qmodel_t *mod;
	msurface_t *surf;
	vec3_t normal;

	if (PF_GetBrushSurface(ed, surfidx, &mod, &surf))
	{
		VectorCopy(surf->plane->normal, normal);
		if (surf->flags & SURF_PLANEBACK)
			VectorInverse(normal);
		PF_ReturnVector(normal);
	}
	else
		PF_ReturnZeroVector();
}

static void PF_getsurfacetexture(void)
{
	edict_t *ed = G_EDICT(OFS_PARM0);
	unsigned int surfidx = (unsigned int)G_FLOAT(OFS_PARM1);
	qmodel_t *mod;
	msurface_t *surf;

	if (PF_GetBrushSurface(ed, surfidx, &mod, &surf) &&
		surf->texinfo && surf->texinfo->texture)
		G_INT(OFS_RETURN) =
			PR_SetEngineString(surf->texinfo->texture->name);
	else
		G_INT(OFS_RETURN) = 0;
}

static float PF_SurfaceClipPointPoly(qmodel_t *model, msurface_t *surf,
	vec3_t point, vec3_t bestcpoint, float bestdist)
{
	int e, edge;
	vec3_t edgedir, edgenormal, cpoint, temp;
	mvertex_t *v1, *v2;
	float dist = DotProduct(point, surf->plane->normal) - surf->plane->dist;

	if (dist * dist >= bestdist)
		return bestdist;

	VectorMA(point, -dist, surf->plane->normal, cpoint);
	for (e = surf->firstedge + surf->numedges; e > surf->firstedge;)
	{
		edge = model->surfedges[--e];
		if (edge < 0)
		{
			v1 = &model->vertexes[model->edges[-edge].v[0]];
			v2 = &model->vertexes[model->edges[-edge].v[1]];
		}
		else
		{
			v2 = &model->vertexes[model->edges[edge].v[0]];
			v1 = &model->vertexes[model->edges[edge].v[1]];
		}

		VectorSubtract(v1->position, v2->position, edgedir);
		CrossProduct(edgedir, surf->plane->normal, edgenormal);
		if (!(surf->flags & SURF_PLANEBACK))
			VectorInverse(edgenormal);
		VectorNormalize(edgenormal);

		dist = DotProduct(v1->position, edgenormal) -
			DotProduct(cpoint, edgenormal);
		if (dist < 0)
			VectorMA(cpoint, dist, edgenormal, cpoint);
	}

	VectorSubtract(cpoint, point, temp);
	dist = DotProduct(temp, temp);
	if (dist < bestdist)
	{
		bestdist = dist;
		VectorCopy(cpoint, bestcpoint);
	}
	return bestdist;
}

static void PF_getsurfacenearpoint(void)
{
	edict_t *ed = G_EDICT(OFS_PARM0);
	float *point = G_VECTOR(OFS_PARM1);
	qmodel_t *model = PF_ModelForEdict(ed);
	msurface_t *surf;
	vec3_t cpoint = {0, 0, 0};
	float bestdist = 256, dist;
	int i, bestsurf = -1;

	if (!model || model->type != mod_brush || model->needload)
	{
		G_FLOAT(OFS_RETURN) = -1;
		return;
	}

	surf = model->surfaces + model->firstmodelsurface;
	for (i = 0; i < model->nummodelsurfaces; i++, surf++)
	{
		dist = PF_SurfaceClipPointPoly(model, surf, point, cpoint, bestdist);
		if (dist < bestdist)
		{
			bestdist = dist;
			bestsurf = i;
		}
	}
	G_FLOAT(OFS_RETURN) = bestsurf;
}

static void PF_getsurfaceclippedpoint(void)
{
	edict_t *ed = G_EDICT(OFS_PARM0);
	unsigned int surfidx = (unsigned int)G_FLOAT(OFS_PARM1);
	float *point = G_VECTOR(OFS_PARM2);
	float *result = G_VECTOR(OFS_RETURN);
	qmodel_t *model;
	msurface_t *surf;

	VectorCopy(point, result);
	if (PF_GetBrushSurface(ed, surfidx, &model, &surf))
		PF_SurfaceClipPointPoly(model, surf, point, result, 0x7fffffff);
}

enum
{
	SPA_POSITION = 0,
	SPA_S_AXIS = 1,
	SPA_T_AXIS = 2,
	SPA_R_AXIS = 3,
	SPA_TEXCOORDS0 = 4,
	SPA_LIGHTMAP0_TEXCOORDS = 5,
	SPA_LIGHTMAP0_COLOR = 6
};

static void PF_getsurfacepointattribute(void)
{
	edict_t *ed = G_EDICT(OFS_PARM0);
	unsigned int surfidx = (unsigned int)G_FLOAT(OFS_PARM1);
	unsigned int point = (unsigned int)G_FLOAT(OFS_PARM2);
	unsigned int attribute = (unsigned int)G_FLOAT(OFS_PARM3);
	qmodel_t *mod;
	msurface_t *fa;
	mvertex_t *v;
	float texwidth, texheight;
	float sc;

	if (!PF_GetBrushSurface(ed, surfidx, &mod, &fa) ||
		point >= (unsigned int)fa->numedges)
	{
		PF_ReturnZeroVector();
		return;
	}

	v = PF_GetSurfaceVertex(mod, fa, point);
	switch (attribute)
	{
	case SPA_POSITION:
		PF_ReturnVector(v->position);
		break;
	case SPA_S_AXIS:
	case SPA_T_AXIS:
		sc = -DotProduct(fa->plane->normal, fa->texinfo->vecs[attribute - 1]);
		VectorMA(fa->texinfo->vecs[attribute - 1], sc,
			fa->plane->normal, G_VECTOR(OFS_RETURN));
		VectorNormalize(G_VECTOR(OFS_RETURN));
		break;
	case SPA_R_AXIS:
		VectorCopy(fa->plane->normal, G_VECTOR(OFS_RETURN));
		if (fa->flags & SURF_PLANEBACK)
			VectorInverse(G_VECTOR(OFS_RETURN));
		break;
	case SPA_TEXCOORDS0:
		texwidth = fa->texinfo->texture ? fa->texinfo->texture->width : 1;
		texheight = fa->texinfo->texture ? fa->texinfo->texture->height : 1;
		G_FLOAT(OFS_RETURN + 0) =
			(DotProduct(v->position, fa->texinfo->vecs[0]) +
			fa->texinfo->vecs[0][3]) / texwidth;
		G_FLOAT(OFS_RETURN + 1) =
			(DotProduct(v->position, fa->texinfo->vecs[1]) +
			fa->texinfo->vecs[1][3]) / texheight;
		G_FLOAT(OFS_RETURN + 2) = 0;
		break;
	case SPA_LIGHTMAP0_TEXCOORDS:
		G_FLOAT(OFS_RETURN + 0) =
			(DotProduct(v->position, fa->texinfo->vecs[0]) +
			fa->texinfo->vecs[0][3] + fa->light_s) / LMBLOCK_WIDTH;
		G_FLOAT(OFS_RETURN + 1) =
			(DotProduct(v->position, fa->texinfo->vecs[1]) +
			fa->texinfo->vecs[1][3] + fa->light_t) / LMBLOCK_HEIGHT;
		G_FLOAT(OFS_RETURN + 2) = 0;
		break;
	case SPA_LIGHTMAP0_COLOR:
		G_FLOAT(OFS_RETURN + 0) = 1;
		G_FLOAT(OFS_RETURN + 1) = 1;
		G_FLOAT(OFS_RETURN + 2) = 1;
		break;
	default:
		Con_Warning("PF_getsurfacepointattribute: attribute %u not supported\n",
			attribute);
		PF_ReturnZeroVector();
		break;
	}
}

static void PF_cl_setcursormode(void)
{
	/* CSQC cursor grabbing/hardware cursors are outside this port's HUD scope. */
}

static void PF_cl_loadfont(void)
{
	G_FLOAT(OFS_RETURN) = 0;
}


static void PF_cl_drawsetclip(void)
{
	float s = PR_GetVMScale();

	float x = G_FLOAT(OFS_PARM0)*s;
	float y = G_FLOAT(OFS_PARM1)*s;
	float w = G_FLOAT(OFS_PARM2)*s;
	float h = G_FLOAT(OFS_PARM3)*s;

	Draw_Flush ();
	glScissor(x, glheight-(y+h), w, h);
	glEnable(GL_SCISSOR_TEST);
}
static void PF_cl_drawresetclip(void)
{
	Draw_Flush ();
	glDisable(GL_SCISSOR_TEST);
}

static void PF_cl_precachepic(void)
{
	const char *name	= G_STRING(OFS_PARM0);
	unsigned int flags = qcvm->argc > 1 ? (unsigned int)G_FLOAT(OFS_PARM1) : 0;

	G_INT(OFS_RETURN) = G_INT(OFS_PARM0);	//return input string, for convienience

	if (!DrawQC_CachePic(name, flags) && (flags & PICFLAG_BLOCK))
		G_INT(OFS_RETURN) = 0;	//return input string, for convienience
}
static void PF_cl_iscachedpic(void)
{
	const char *name	= G_STRING(OFS_PARM0);
	if (DrawQC_CachePic(name, PICFLAG_NOLOAD))
		G_FLOAT(OFS_RETURN) = true;
	else
		G_FLOAT(OFS_RETURN) = false;
}

static void PF_cl_drawpic(void)
{
	float *pos	= G_VECTOR(OFS_PARM0);
	qpic_t *pic	= DrawQC_CachePic(G_STRING(OFS_PARM1), PICFLAG_AUTO);
	float *size	= G_VECTOR(OFS_PARM2);
	float *rgb	= G_VECTOR(OFS_PARM3);
	float alpha	= G_FLOAT (OFS_PARM4);
//	int flags	= G_FLOAT (OFS_PARM5);

	if (pic)
		Draw_SubPic (pos[0], pos[1], size[0], size[1], pic, 0, 0, 1, 1, rgb, alpha);
}

static void PF_cl_getimagesize(void)
{
	qpic_t *pic	= DrawQC_CachePic(G_STRING(OFS_PARM0), PICFLAG_AUTO);
	if (pic)
		G_VECTORSET(OFS_RETURN, pic->width, pic->height, 0);
	else
		G_VECTORSET(OFS_RETURN, 0, 0, 0);
}

static void PF_cl_drawsubpic(void)
{
	float *pos	= G_VECTOR(OFS_PARM0);
	float *size	= G_VECTOR(OFS_PARM1);
	qpic_t *pic	= DrawQC_CachePic(G_STRING(OFS_PARM2), PICFLAG_AUTO);
	float *srcpos	= G_VECTOR(OFS_PARM3);
	float *srcsize	= G_VECTOR(OFS_PARM4);
	float *rgb	= G_VECTOR(OFS_PARM5);
	float alpha	= G_FLOAT (OFS_PARM6);
//	int flags	= G_FLOAT (OFS_PARM7);

	if (pic)
		Draw_SubPic (pos[0], pos[1], size[0], size[1], pic, srcpos[0], srcpos[1], srcsize[0], srcsize[1], rgb, alpha);
}

static void PF_cl_drawfill(void)
{
	float *pos	= G_VECTOR(OFS_PARM0);
	float *size	= G_VECTOR(OFS_PARM1);
	float *rgb	= G_VECTOR(OFS_PARM2);
	float alpha	= G_FLOAT (OFS_PARM3);
//	int flags	= G_FLOAT (OFS_PARM4);

	Draw_FillEx (pos[0], pos[1], size[0], size[1], rgb, alpha);
}

//maths stuff
static void PF_Sin(void)
{
	G_FLOAT(OFS_RETURN) = sin(G_FLOAT(OFS_PARM0));
}
static void PF_asin(void)
{
	G_FLOAT(OFS_RETURN) = asin(G_FLOAT(OFS_PARM0));
}
static void PF_Cos(void)
{
	G_FLOAT(OFS_RETURN) = cos(G_FLOAT(OFS_PARM0));
}
static void PF_acos(void)
{
	G_FLOAT(OFS_RETURN) = acos(G_FLOAT(OFS_PARM0));
}
static void PF_tan(void)
{
	G_FLOAT(OFS_RETURN) = tan(G_FLOAT(OFS_PARM0));
}
static void PF_atan(void)
{
	G_FLOAT(OFS_RETURN) = atan(G_FLOAT(OFS_PARM0));
}
static void PF_atan2(void)
{
	G_FLOAT(OFS_RETURN) = atan2(G_FLOAT(OFS_PARM0), G_FLOAT(OFS_PARM1));
}
static void PF_Sqrt(void)
{
	G_FLOAT(OFS_RETURN) = sqrt(G_FLOAT(OFS_PARM0));
}
static void PF_pow(void)
{
	G_FLOAT(OFS_RETURN) = pow(G_FLOAT(OFS_PARM0), G_FLOAT(OFS_PARM1));
}
static void PF_Logarithm(void)
{
	//log2(v) = ln(v)/ln(2)
	double r;
	r = log(G_FLOAT(OFS_PARM0));
	if (qcvm->argc > 1)
		r /= log(G_FLOAT(OFS_PARM1));
	G_FLOAT(OFS_RETURN) = r;
}
static void PF_mod(void)
{
	float a = G_FLOAT(OFS_PARM0);
	float n = G_FLOAT(OFS_PARM1);

	if (n == 0)
	{
		Con_DWarning("PF_mod: mod by zero\n");
		G_FLOAT(OFS_RETURN) = 0;
	}
	else
	{
		//because QC is inherantly floaty, lets use floats.
		G_FLOAT(OFS_RETURN) = a - (n * (int)(a/n));
	}
}
static void PF_min(void)
{
	float r = G_FLOAT(OFS_PARM0);
	int i;
	for (i = 1; i < qcvm->argc; i++)
	{
		if (r > G_FLOAT(OFS_PARM0 + i*3))
			r = G_FLOAT(OFS_PARM0 + i*3);
	}
	G_FLOAT(OFS_RETURN) = r;
}
static void PF_max(void)
{
	float r = G_FLOAT(OFS_PARM0);
	int i;
	for (i = 1; i < qcvm->argc; i++)
	{
		if (r < G_FLOAT(OFS_PARM0 + i*3))
			r = G_FLOAT(OFS_PARM0 + i*3);
	}
	G_FLOAT(OFS_RETURN) = r;
}
static void PF_bound(void)
{
	float minval = G_FLOAT(OFS_PARM0);
	float curval = G_FLOAT(OFS_PARM1);
	float maxval = G_FLOAT(OFS_PARM2);
	if (curval > maxval)
		curval = maxval;
	if (curval < minval)
		curval = minval;
	G_FLOAT(OFS_RETURN) = curval;
}
static void PF_anglemod(void)
{
	float v = G_FLOAT(OFS_PARM0);

	while (v >= 360)
		v = v - 360;
	while (v < 0)
		v = v + 360;

	G_FLOAT(OFS_RETURN) = v;
}
static void PF_bitshift(void)
{
	int bitmask = G_FLOAT(OFS_PARM0);
	int shift = G_FLOAT(OFS_PARM1);
	if (shift < 0)
		bitmask >>= -shift;
	else
		bitmask <<= shift;
	G_FLOAT(OFS_RETURN) = bitmask;
}
static void PF_crossproduct(void)
{
	CrossProduct(G_VECTOR(OFS_PARM0), G_VECTOR(OFS_PARM1), G_VECTOR(OFS_RETURN));
}
static void PF_vectorvectors(void)
{
	VectorCopy(G_VECTOR(OFS_PARM0), pr_global_struct->v_forward);
	VectorNormalize(pr_global_struct->v_forward);
	if (!pr_global_struct->v_forward[0] && !pr_global_struct->v_forward[1])
	{
		if (pr_global_struct->v_forward[2])
			pr_global_struct->v_right[1] = -1;
		else
			pr_global_struct->v_right[1] = 0;
		pr_global_struct->v_right[0] = pr_global_struct->v_right[2] = 0;
	}
	else
	{
		pr_global_struct->v_right[0] = pr_global_struct->v_forward[1];
		pr_global_struct->v_right[1] = -pr_global_struct->v_forward[0];
		pr_global_struct->v_right[2] = 0;
		VectorNormalize(pr_global_struct->v_right);
	}
	CrossProduct(pr_global_struct->v_right, pr_global_struct->v_forward, pr_global_struct->v_up);
}

//string stuff
static void PF_strlen(void)
{	//FIXME: doesn't try to handle utf-8
	const char *s = G_STRING(OFS_PARM0);
	G_FLOAT(OFS_RETURN) = strlen(s);
}
static void PF_strcat(void)
{
	int		i;
	char *out = PR_GetTempString();
	size_t s;

	out[0] = 0;
	s = 0;
	for (i = 0; i < qcvm->argc; i++)
	{
		s = q_strlcat(out, G_STRING((OFS_PARM0+i*3)), STRINGTEMP_LENGTH);
		if (s >= STRINGTEMP_LENGTH)
		{
			Con_Warning("PF_strcat: overflow (string truncated)\n");
			break;
		}
	}

	G_INT(OFS_RETURN) = PR_SetEngineString(out);
}
static void PF_substring(void)
{
	int start, length, slen;
	const char *s;
	char *string;

	s = G_STRING(OFS_PARM0);
	start = G_FLOAT(OFS_PARM1);
	length = G_FLOAT(OFS_PARM2);

	slen = strlen(s);	//utf-8 should use chars, not bytes.

	if (start < 0)
		start = slen+start;
	if (length < 0)
		length = slen-start+(length+1);
	if (start < 0)
	{
	//	length += start;
		start = 0;
	}

	if (start >= slen || length<=0)
	{
		G_INT(OFS_RETURN) = PR_SetEngineString("");
		return;
	}

	slen -= start;
	if (length > slen)
		length = slen;
	//utf-8 should switch to bytes now.
	s += start;

	if (length >= STRINGTEMP_LENGTH)
	{
		length = STRINGTEMP_LENGTH-1;
		Con_Warning("PF_substring: truncation\n");
	}

	string = PR_GetTempString();
	memcpy(string, s, length);
	string[length] = '\0';
	G_INT(OFS_RETURN) = PR_SetEngineString(string);
}

/*our zoned strings implementation is somewhat specific to quakespasm, so good luck porting*/
static void PF_strzone(void)
{
	char *buf;
	size_t len = 0;
	const char *s[8];
	size_t l[8];
	int i;
	size_t id;

	for (i = 0; i < qcvm->argc; i++)
	{
		s[i] = G_STRING(OFS_PARM0+i*3);
		l[i] = strlen(s[i]);
		len += l[i];
	}
	len++; /*for the null*/

	buf = Z_Malloc(len);
	G_INT(OFS_RETURN) = PR_SetEngineString(buf);
	id = -1-G_INT(OFS_RETURN);
	if (id >= qcvm->knownzonesize)
	{
		qcvm->knownzonesize = (id+32)&~7;
		qcvm->knownzone = Z_Realloc(qcvm->knownzone, (qcvm->knownzonesize+7)>>3);
	}
	qcvm->knownzone[id>>3] |= 1u<<(id&7);

	for (i = 0; i < qcvm->argc; i++)
	{
		memcpy(buf, s[i], l[i]);
		buf += l[i];
	}
	*buf = '\0';
}
static void PF_strunzone(void)
{
	size_t id;
	const char *foo = G_STRING(OFS_PARM0);

	if (!G_INT(OFS_PARM0))
		return;	//don't bug out if they gave a null string
	id = -1-G_INT(OFS_PARM0);
	if (id < qcvm->knownzonesize && (qcvm->knownzone[id>>3] & (1u<<(id&7))))
	{
		qcvm->knownzone[id>>3] &= ~(1u<<(id&7));
		PR_ClearEngineString(G_INT(OFS_PARM0));
		Z_Free((void*)foo);
	}
	else
		Con_Warning("PF_strunzone: string wasn't strzoned\n");
}

static void PF_strstrofs(void)
{
	const char *instr = G_STRING(OFS_PARM0);
	const char *match = G_STRING(OFS_PARM1);
	int firstofs = qcvm->argc > 2 ? G_FLOAT(OFS_PARM2) : 0;
	const char *found;

	if (firstofs < 0 || firstofs > (int)strlen(instr))
	{
		G_FLOAT(OFS_RETURN) = -1;
		return;
	}

	found = strstr(instr + firstofs, match);
	G_FLOAT(OFS_RETURN) = found ? found - instr : -1;
}

static void PF_strpad(void)
{
	char *destbuf = PR_GetTempString();
	int pad = G_FLOAT(OFS_PARM0);
	const char *src = PF_VarString(1);
	size_t srclen = strlen(src);
	int spaces;

	if (pad < 0)
	{
		spaces = -pad - (int)srclen;
		if (spaces < 0)
			spaces = 0;
		if (spaces >= STRINGTEMP_LENGTH)
			spaces = STRINGTEMP_LENGTH - 1;
		memset(destbuf, ' ', spaces);
		q_strlcpy(destbuf + spaces, src, STRINGTEMP_LENGTH - spaces);
	}
	else
	{
		char *out;
		spaces = pad - (int)srclen;
		if (spaces < 0)
			spaces = 0;
		q_strlcpy(destbuf, src, STRINGTEMP_LENGTH);
		out = destbuf + strlen(destbuf);
		while (spaces-- > 0 && out < destbuf + STRINGTEMP_LENGTH - 1)
			*out++ = ' ';
		*out = 0;
	}

	G_INT(OFS_RETURN) = PR_SetEngineString(destbuf);
}

static void PF_strncmp(void)
{
	const char *a = G_STRING(OFS_PARM0);
	const char *b = G_STRING(OFS_PARM1);
	int alen = strlen(a);
	int blen = strlen(b);
	int len = qcvm->argc > 2 ? G_FLOAT(OFS_PARM2) : -1;
	int aofs = qcvm->argc > 3 ? G_FLOAT(OFS_PARM3) : 0;
	int bofs = qcvm->argc > 4 ? G_FLOAT(OFS_PARM4) : 0;

	if (aofs < 0)
		aofs = 0;
	if (bofs < 0)
		bofs = 0;
	if (aofs > alen)
		aofs = alen;
	if (bofs > blen)
		bofs = blen;

	G_FLOAT(OFS_RETURN) = len >= 0 ? Q_strncmp(a + aofs, b + bofs, len) :
		Q_strcmp(a + aofs, b + bofs);
}

static void PF_strncasecmp(void)
{
	const char *a = G_STRING(OFS_PARM0);
	const char *b = G_STRING(OFS_PARM1);
	int alen = strlen(a);
	int blen = strlen(b);
	int len = qcvm->argc > 2 ? G_FLOAT(OFS_PARM2) : -1;
	int aofs = qcvm->argc > 3 ? G_FLOAT(OFS_PARM3) : 0;
	int bofs = qcvm->argc > 4 ? G_FLOAT(OFS_PARM4) : 0;

	if (aofs < 0)
		aofs = 0;
	if (bofs < 0)
		bofs = 0;
	if (aofs > alen)
		aofs = alen;
	if (bofs > blen)
		bofs = blen;

	G_FLOAT(OFS_RETURN) = len >= 0 ? q_strncasecmp(a + aofs, b + bofs, len) :
		q_strcasecmp(a + aofs, b + bofs);
}

static qboolean qc_isascii(unsigned int u)
{
	if (u < 256)	//should be just \n and 32-127, but we don't actually support any actual unicode and we don't really want to make things worse.
		return true;
	return false;
}
static void PF_str2chr(void)
{
	const char *instr = G_STRING(OFS_PARM0);
	int ofs = (qcvm->argc>1)?G_FLOAT(OFS_PARM1):0;

	if (ofs < 0)
		ofs = strlen(instr)+ofs;

	if (ofs && (ofs < 0 || ofs > (int)strlen(instr)))
		G_FLOAT(OFS_RETURN) = '\0';
	else
		G_FLOAT(OFS_RETURN) = (unsigned char)instr[ofs];
}
static void PF_chr2str(void)
{
	char *ret = PR_GetTempString(), *out;
	int i;
	for (i = 0, out=ret; out-ret < STRINGTEMP_LENGTH-6 && i < qcvm->argc; i++)
	{
		unsigned int u = G_FLOAT(OFS_PARM0 + i*3);
		if (u >= 0xe000 && u < 0xe100)
			*out++ = (unsigned char)u;	//quake chars.
		else if (qc_isascii(u))
			*out++ = u;
		else
			*out++ = '?';	//no unicode support
	}
	*out = 0;
	G_INT(OFS_RETURN) = PR_SetEngineString(ret);
}

static int PF_chrconv_number(int i, int base, int conv)
{
	i -= base;
	switch (conv)
	{
	default:
	case 5:
	case 6:
	case 0:
		break;
	case 1:
		base = '0';
		break;
	case 2:
		base = '0' + 128;
		break;
	case 3:
		base = '0' - 30;
		break;
	case 4:
		base = '0' + 128 - 30;
		break;
	}
	return i + base;
}

static int PF_chrconv_punct(int i, int base, int conv)
{
	i -= base;
	switch (conv)
	{
	default:
	case 0:
		break;
	case 1:
		base = 0;
		break;
	case 2:
		base = 128;
		break;
	}
	return i + base;
}

static int PF_chrconv_alpha(int i, int basec, int baset, int convc, int convt, int charnum)
{
	i -= baset + basec;
	switch (convt)
	{
	default:
	case 0:
		break;
	case 1:
		baset = 0;
		break;
	case 2:
		baset = 128;
		break;
	case 5:
	case 6:
		baset = 128 * ((charnum & 1) == (convt - 5));
		break;
	}

	switch (convc)
	{
	default:
	case 0:
		break;
	case 1:
		basec = 'a';
		break;
	case 2:
		basec = 'A';
		break;
	}
	return i + basec + baset;
}

static void PF_strconv(void)
{
	int ccase = G_FLOAT(OFS_PARM0);
	int redalpha = G_FLOAT(OFS_PARM1);
	int rednum = G_FLOAT(OFS_PARM2);
	const unsigned char *string = (const unsigned char *)PF_VarString(3);
	int len = strlen((const char *)string);
	int i;
	unsigned char *resbuf = (unsigned char *)PR_GetTempString();
	unsigned char *result = resbuf;

	if (len >= STRINGTEMP_LENGTH)
		len = STRINGTEMP_LENGTH - 1;

	for (i = 0; i < len; i++, string++, result++)
	{
		if (*string >= '0' && *string <= '9')
			*result = PF_chrconv_number(*string, '0', rednum);
		else if (*string >= '0' + 128 && *string <= '9' + 128)
			*result = PF_chrconv_number(*string, '0' + 128, rednum);
		else if (*string >= '0' + 128 - 30 && *string <= '9' + 128 - 30)
			*result = PF_chrconv_number(*string, '0' + 128 - 30, rednum);
		else if (*string >= '0' - 30 && *string <= '9' - 30)
			*result = PF_chrconv_number(*string, '0' - 30, rednum);
		else if (*string >= 'a' && *string <= 'z')
			*result = PF_chrconv_alpha(*string, 'a', 0, ccase, redalpha, i);
		else if (*string >= 'A' && *string <= 'Z')
			*result = PF_chrconv_alpha(*string, 'A', 0, ccase, redalpha, i);
		else if (*string >= 'a' + 128 && *string <= 'z' + 128)
			*result = PF_chrconv_alpha(*string, 'a', 128, ccase, redalpha, i);
		else if (*string >= 'A' + 128 && *string <= 'Z' + 128)
			*result = PF_chrconv_alpha(*string, 'A', 128, ccase, redalpha, i);
		else if ((*string & 127) < 16 || !redalpha)
			*result = *string;
		else if (*string < 128)
			*result = PF_chrconv_punct(*string, 0, redalpha);
		else
			*result = PF_chrconv_punct(*string, 128, redalpha);
	}
	*result = '\0';

	G_INT(OFS_RETURN) = PR_SetEngineString((char *)resbuf);
}

static void PF_sprintf_internal (const char *s, int firstarg, char *outbuf, int outbuflen)
{
	const char *s0;
	char *o = outbuf, *end = outbuf + outbuflen, *err;
	int width, precision, thisarg, flags;
	char formatbuf[16];
	char *f;
	int argpos = firstarg;
	int isfloat;
	static int dummyivec[3] = {0, 0, 0};
	static float dummyvec[3] = {0, 0, 0};

#define PRINTF_ALTERNATE 1
#define PRINTF_ZEROPAD 2
#define PRINTF_LEFT 4
#define PRINTF_SPACEPOSITIVE 8
#define PRINTF_SIGNPOSITIVE 16

	formatbuf[0] = '%';

#define GETARG_FLOAT(a) (((a)>=firstarg && (a)<qcvm->argc) ? (G_FLOAT(OFS_PARM0 + 3 * (a))) : 0)
#define GETARG_VECTOR(a) (((a)>=firstarg && (a)<qcvm->argc) ? (G_VECTOR(OFS_PARM0 + 3 * (a))) : dummyvec)
#define GETARG_INT(a) (((a)>=firstarg && (a)<qcvm->argc) ? (G_INT(OFS_PARM0 + 3 * (a))) : 0)
#define GETARG_INTVECTOR(a) (((a)>=firstarg && (a)<qcvm->argc) ? ((int*) G_VECTOR(OFS_PARM0 + 3 * (a))) : dummyivec)
#define GETARG_STRING(a) (((a)>=firstarg && (a)<qcvm->argc) ? (G_STRING(OFS_PARM0 + 3 * (a))) : "")

	for(;;)
	{
		s0 = s;
		switch(*s)
		{
			case 0:
				goto finished;
			case '%':
				++s;

				if(*s == '%')
					goto verbatim;

				// complete directive format:
				// %3$*1$.*2$ld

				width = -1;
				precision = -1;
				thisarg = -1;
				flags = 0;
				isfloat = -1;

				// is number following?
				if(*s >= '0' && *s <= '9')
				{
					width = strtol(s, &err, 10);
					if(!err)
					{
						Con_Warning("PF_sprintf: bad format string: %s\n", s0);
						goto finished;
					}
					if(*err == '$')
					{
						thisarg = width + (firstarg-1);
						width = -1;
						s = err + 1;
					}
					else
					{
						if(*s == '0')
						{
							flags |= PRINTF_ZEROPAD;
							if(width == 0)
								width = -1; // it was just a flag
						}
						s = err;
					}
				}

				if(width < 0)
				{
					for(;;)
					{
						switch(*s)
						{
							case '#': flags |= PRINTF_ALTERNATE; break;
							case '0': flags |= PRINTF_ZEROPAD; break;
							case '-': flags |= PRINTF_LEFT; break;
							case ' ': flags |= PRINTF_SPACEPOSITIVE; break;
							case '+': flags |= PRINTF_SIGNPOSITIVE; break;
							default:
								goto noflags;
						}
						++s;
					}
noflags:
					if(*s == '*')
					{
						++s;
						if(*s >= '0' && *s <= '9')
						{
							width = strtol(s, &err, 10);
							if(!err || *err != '$')
							{
								Con_Warning("PF_sprintf: invalid format string: %s\n", s0);
								goto finished;
							}
							s = err + 1;
						}
						else
							width = argpos++;
						width = GETARG_FLOAT(width);
						if(width < 0)
						{
							flags |= PRINTF_LEFT;
							width = -width;
						}
					}
					else if(*s >= '0' && *s <= '9')
					{
						width = strtol(s, &err, 10);
						if(!err)
						{
							Con_Warning("PF_sprintf: invalid format string: %s\n", s0);
							goto finished;
						}
						s = err;
						if(width < 0)
						{
							flags |= PRINTF_LEFT;
							width = -width;
						}
					}
					// otherwise width stays -1
				}

				if(*s == '.')
				{
					++s;
					if(*s == '*')
					{
						++s;
						if(*s >= '0' && *s <= '9')
						{
							precision = strtol(s, &err, 10);
							if(!err || *err != '$')
							{
								Con_Warning("PF_sprintf: invalid format string: %s\n", s0);
								goto finished;
							}
							s = err + 1;
						}
						else
							precision = argpos++;
						precision = GETARG_FLOAT(precision);
					}
					else if(*s >= '0' && *s <= '9')
					{
						precision = strtol(s, &err, 10);
						if(!err)
						{
							Con_Warning("PF_sprintf: invalid format string: %s\n", s0);
							goto finished;
						}
						s = err;
					}
					else
					{
						Con_Warning("PF_sprintf: invalid format string: %s\n", s0);
						goto finished;
					}
				}

				for(;;)
				{
					switch(*s)
					{
						case 'h': isfloat = 1; break;
						case 'l': isfloat = 0; break;
						case 'L': isfloat = 0; break;
						case 'j': break;
						case 'z': break;
						case 't': break;
						default:
							goto nolength;
					}
					++s;
				}
nolength:

				// now s points to the final directive char and is no longer changed
				if (*s == 'p' || *s == 'P')
				{
					//%p is slightly different from %x.
					//always 8-bytes wide with 0 padding, always ints.
					flags |= PRINTF_ZEROPAD;
					if (width < 0) width = 8;
					if (isfloat < 0) isfloat = 0;
				}
				else if (*s == 'i')
				{
					//%i defaults to ints, not floats.
					if(isfloat < 0) isfloat = 0;
				}

				//assume floats, not ints.
				if(isfloat < 0)
					isfloat = 1;

				if(thisarg < 0)
					thisarg = argpos++;

				if(o < end - 1)
				{
					f = &formatbuf[1];
					if(*s != 's' && *s != 'c')
						if(flags & PRINTF_ALTERNATE) *f++ = '#';
					if(flags & PRINTF_ZEROPAD) *f++ = '0';
					if(flags & PRINTF_LEFT) *f++ = '-';
					if(flags & PRINTF_SPACEPOSITIVE) *f++ = ' ';
					if(flags & PRINTF_SIGNPOSITIVE) *f++ = '+';
					*f++ = '*';
					if(precision >= 0)
					{
						*f++ = '.';
						*f++ = '*';
					}
					if (*s == 'p')
						*f++ = 'x';
					else if (*s == 'P')
						*f++ = 'X';
					else if (*s == 'S')
						*f++ = 's';
					else
						*f++ = *s;
					*f++ = 0;

					if(width < 0) // not set
						width = 0;

					switch(*s)
					{
						case 'd': case 'i':
							if(precision < 0) // not set
								q_snprintf(o, end - o, formatbuf, width, (isfloat ? (int) GETARG_FLOAT(thisarg) : (int) GETARG_INT(thisarg)));
							else
								q_snprintf(o, end - o, formatbuf, width, precision, (isfloat ? (int) GETARG_FLOAT(thisarg) : (int) GETARG_INT(thisarg)));
							o += strlen(o);
							break;
						case 'o': case 'u': case 'x': case 'X': case 'p': case 'P':
							if(precision < 0) // not set
								q_snprintf(o, end - o, formatbuf, width, (isfloat ? (unsigned int) GETARG_FLOAT(thisarg) : (unsigned int) GETARG_INT(thisarg)));
							else
								q_snprintf(o, end - o, formatbuf, width, precision, (isfloat ? (unsigned int) GETARG_FLOAT(thisarg) : (unsigned int) GETARG_INT(thisarg)));
							o += strlen(o);
							break;
						case 'e': case 'E': case 'f': case 'F': case 'g': case 'G':
							if(precision < 0) // not set
								q_snprintf(o, end - o, formatbuf, width, (isfloat ? (double) GETARG_FLOAT(thisarg) : (double) GETARG_INT(thisarg)));
							else
								q_snprintf(o, end - o, formatbuf, width, precision, (isfloat ? (double) GETARG_FLOAT(thisarg) : (double) GETARG_INT(thisarg)));
							o += strlen(o);
							break;
						case 'v': case 'V':
							f[-2] += 'g' - 'v';
							if(precision < 0) // not set
								q_snprintf(o, end - o, va("%s %s %s", /* NESTED SPRINTF IS NESTED */ formatbuf, formatbuf, formatbuf),
									width, (isfloat ? (double) GETARG_VECTOR(thisarg)[0] : (double) GETARG_INTVECTOR(thisarg)[0]),
									width, (isfloat ? (double) GETARG_VECTOR(thisarg)[1] : (double) GETARG_INTVECTOR(thisarg)[1]),
									width, (isfloat ? (double) GETARG_VECTOR(thisarg)[2] : (double) GETARG_INTVECTOR(thisarg)[2])
								);
							else
								q_snprintf(o, end - o, va("%s %s %s", /* NESTED SPRINTF IS NESTED */ formatbuf, formatbuf, formatbuf),
									width, precision, (isfloat ? (double) GETARG_VECTOR(thisarg)[0] : (double) GETARG_INTVECTOR(thisarg)[0]),
									width, precision, (isfloat ? (double) GETARG_VECTOR(thisarg)[1] : (double) GETARG_INTVECTOR(thisarg)[1]),
									width, precision, (isfloat ? (double) GETARG_VECTOR(thisarg)[2] : (double) GETARG_INTVECTOR(thisarg)[2])
								);
							o += strlen(o);
							break;
						case 'c':
							//UTF-8-FIXME: figure it out yourself
//							if(flags & PRINTF_ALTERNATE)
							{
								if(precision < 0) // not set
									q_snprintf(o, end - o, formatbuf, width, (isfloat ? (unsigned int) GETARG_FLOAT(thisarg) : (unsigned int) GETARG_INT(thisarg)));
								else
									q_snprintf(o, end - o, formatbuf, width, precision, (isfloat ? (unsigned int) GETARG_FLOAT(thisarg) : (unsigned int) GETARG_INT(thisarg)));
								o += strlen(o);
							}
/*							else
							{
								unsigned int c = (isfloat ? (unsigned int) GETARG_FLOAT(thisarg) : (unsigned int) GETARG_INT(thisarg));
								char charbuf16[16];
								const char *buf = u8_encodech(c, NULL, charbuf16);
								if(!buf)
									buf = "";
								if(precision < 0) // not set
									precision = end - o - 1;
								o += u8_strpad(o, end - o, buf, (flags & PRINTF_LEFT) != 0, width, precision);
							}
*/							break;
						case 'S':
							{	//tokenizable string
								const char *quotedarg = GETARG_STRING(thisarg);

								//try and escape it... hopefully it won't get truncated by precision limits...
								char quotedbuf[65536];
								size_t l;
								l = strlen(quotedarg);
								if (strchr(quotedarg, '\"') || strchr(quotedarg, '\n') || strchr(quotedarg, '\r') || l+3 >= sizeof(quotedbuf))
								{	//our escapes suck...
									Con_Warning("PF_sprintf: unable to safely escape arg: %s\n", s0);
									quotedarg="";
								}
								quotedbuf[0] = '\"';
								memcpy(quotedbuf+1, quotedarg, l);
								quotedbuf[1+l] = '\"';
								quotedbuf[1+l+1] = 0;
								quotedarg = quotedbuf;

								//UTF-8-FIXME: figure it out yourself
//								if(flags & PRINTF_ALTERNATE)
								{
									if(precision < 0) // not set
										q_snprintf(o, end - o, formatbuf, width, quotedarg);
									else
										q_snprintf(o, end - o, formatbuf, width, precision, quotedarg);
									o += strlen(o);
								}
/*								else
								{
									if(precision < 0) // not set
										precision = end - o - 1;
									o += u8_strpad(o, end - o, quotedarg, (flags & PRINTF_LEFT) != 0, width, precision);
								}
*/							}
							break;
						case 's':
							//UTF-8-FIXME: figure it out yourself
//							if(flags & PRINTF_ALTERNATE)
							{
								if(precision < 0) // not set
									q_snprintf(o, end - o, formatbuf, width, GETARG_STRING(thisarg));
								else
									q_snprintf(o, end - o, formatbuf, width, precision, GETARG_STRING(thisarg));
								o += strlen(o);
							}
/*							else
							{
								if(precision < 0) // not set
									precision = end - o - 1;
								o += u8_strpad(o, end - o, GETARG_STRING(thisarg), (flags & PRINTF_LEFT) != 0, width, precision);
							}
*/							break;
						default:
							Con_Warning("PF_sprintf: invalid format string: %s\n", s0);
							goto finished;
					}
				}
				++s;
				break;
			default:
verbatim:
				if(o < end - 1)
					*o++ = *s;
				s++;
				break;
		}
	}
finished:
	*o = 0;
}

static void PF_sprintf(void)
{
	char *outbuf = PR_GetTempString();
	PF_sprintf_internal(G_STRING(OFS_PARM0), 1, outbuf, STRINGTEMP_LENGTH);
	G_INT(OFS_RETURN) = PR_SetEngineString(outbuf);
}

//string tokenizing (gah)
#define MAXQCTOKENS 64
static struct {
	char *token;
	unsigned int start;
	unsigned int end;
} qctoken[MAXQCTOKENS];
static unsigned int qctoken_count;

static void tokenize_flush(void)
{
	while(qctoken_count > 0)
	{
		qctoken_count--;
		free(qctoken[qctoken_count].token);
	}
	qctoken_count = 0;
}

static void PF_ArgC(void)
{
	G_FLOAT(OFS_RETURN) = qctoken_count;
}

static int tokenizeqc(const char *str, qboolean dpfuckage)
{
	//FIXME: if dpfuckage, then we should handle punctuation specially, as well as /*.
	const char *start = str;
	while(qctoken_count > 0)
	{
		qctoken_count--;
		free(qctoken[qctoken_count].token);
	}
	qctoken_count = 0;
	while (qctoken_count < MAXQCTOKENS)
	{
		/*skip whitespace here so the token's start is accurate*/
		while (*str && *(const unsigned char*)str <= ' ')
			str++;

		if (!*str)
			break;

		qctoken[qctoken_count].start = str - start;
//		if (dpfuckage)
//			str = COM_ParseDPFuckage(str);
//		else
			str = COM_Parse(str);
		if (!str)
			break;

		qctoken[qctoken_count].token = strdup(com_token);

		qctoken[qctoken_count].end = str - start;
		qctoken_count++;
	}
	return qctoken_count;
}

/*KRIMZON_SV_PARSECLIENTCOMMAND added these two - note that for compatibility with DP, this tokenize builtin is veeery vauge and doesn't match the console*/
static void PF_Tokenize(void)
{
	G_FLOAT(OFS_RETURN) = tokenizeqc(G_STRING(OFS_PARM0), true);
}

static void PF_tokenize_console(void)
{
	G_FLOAT(OFS_RETURN) = tokenizeqc(G_STRING(OFS_PARM0), false);
}

static void PF_tokenizebyseparator(void)
{
	const char *str = G_STRING(OFS_PARM0);
	const char *sep[7];
	int seplen[7];
	int seps = 0, s;
	const char *start = str;
	int tlen;
	qboolean found = true;

	while (seps < qcvm->argc - 1 && seps < 7)
	{
		sep[seps] = G_STRING(OFS_PARM1 + seps*3);
		seplen[seps] = strlen(sep[seps]);
		seps++;
	}

	tokenize_flush();

	qctoken[qctoken_count].start = 0;
	if (*str)
	for(;;)
	{
		found = false;
		/*see if its a separator*/
		if (!*str)
		{
			qctoken[qctoken_count].end = str - start;
			found = true;
		}
		else
		{
			for (s = 0; s < seps; s++)
			{
				if (!strncmp(str, sep[s], seplen[s]))
				{
					qctoken[qctoken_count].end = str - start;
					str += seplen[s];
					found = true;
					break;
				}
			}
		}
		/*it was, split it out*/
		if (found)
		{
			tlen = qctoken[qctoken_count].end - qctoken[qctoken_count].start;
			qctoken[qctoken_count].token = malloc(tlen + 1);
			memcpy(qctoken[qctoken_count].token, start + qctoken[qctoken_count].start, tlen);
			qctoken[qctoken_count].token[tlen] = 0;

			qctoken_count++;

			if (*str && qctoken_count < MAXQCTOKENS)
				qctoken[qctoken_count].start = str - start;
			else
				break;
		}
		str++;
	}
	G_FLOAT(OFS_RETURN) = qctoken_count;
}

static void PF_argv_start_index(void)
{
	int idx = G_FLOAT(OFS_PARM0);

	/*negative indexes are relative to the end*/
	if (idx < 0)
		idx += qctoken_count;

	if ((unsigned int)idx >= qctoken_count)
		G_FLOAT(OFS_RETURN) = -1;
	else
		G_FLOAT(OFS_RETURN) = qctoken[idx].start;
}

static void PF_argv_end_index(void)
{
	int idx = G_FLOAT(OFS_PARM0);

	/*negative indexes are relative to the end*/
	if (idx < 0)
		idx += qctoken_count;

	if ((unsigned int)idx >= qctoken_count)
		G_FLOAT(OFS_RETURN) = -1;
	else
		G_FLOAT(OFS_RETURN) = qctoken[idx].end;
}

static void PF_ArgV(void)
{
	int idx = G_FLOAT(OFS_PARM0);

	/*negative indexes are relative to the end*/
	if (idx < 0)
		idx += qctoken_count;

	if ((unsigned int)idx >= qctoken_count)
		G_INT(OFS_RETURN) = 0;
	else
	{
		char *ret = PR_GetTempString();
		q_strlcpy(ret, qctoken[idx].token, STRINGTEMP_LENGTH);
		G_INT(OFS_RETURN) = PR_SetEngineString(ret);
	}
}

//conversions (mostly string)
static void PF_strtoupper(void)
{
	const char *in = G_STRING(OFS_PARM0);
	char *out, *result = PR_GetTempString();
	for (out = result; *in && out < result+STRINGTEMP_LENGTH-1;)
		*out++ = q_toupper(*in++);
	*out = 0;
	G_INT(OFS_RETURN) = PR_SetEngineString(result);
}
static void PF_strtolower(void)
{
	const char *in = G_STRING(OFS_PARM0);
	char *out, *result = PR_GetTempString();
	for (out = result; *in && out < result+STRINGTEMP_LENGTH-1;)
		*out++ = q_tolower(*in++);
	*out = 0;
	G_INT(OFS_RETURN) = PR_SetEngineString(result);
}
#include <time.h>
static void PF_strftime(void)
{
	const char *in = G_STRING(OFS_PARM1);
	char *result = PR_GetTempString();

	time_t ctime;
	struct tm *tm;

	ctime = time(NULL);

	if (G_FLOAT(OFS_PARM0))
		tm = localtime(&ctime);
	else
		tm = gmtime(&ctime);

#ifdef _WIN32
	//msvc sucks. this is a crappy workaround.
	if (!strcmp(in, "%R"))
		in = "%H:%M";
	else if (!strcmp(in, "%F"))
		in = "%Y-%m-%d";
#endif

	strftime(result, STRINGTEMP_LENGTH, in, tm);

	G_INT(OFS_RETURN) = PR_SetEngineString(result);
}
static void PF_stof(void)
{
	G_FLOAT(OFS_RETURN) = atof(G_STRING(OFS_PARM0));
}
static void PF_stov(void)
{
	const char *s = G_STRING(OFS_PARM0);
	s = COM_Parse(s);
	G_VECTOR(OFS_RETURN)[0] = atof(com_token);
	s = COM_Parse(s);
	G_VECTOR(OFS_RETURN)[1] = atof(com_token);
	s = COM_Parse(s);
	G_VECTOR(OFS_RETURN)[2] = atof(com_token);
}
static void PF_stoi(void)
{
	G_INT(OFS_RETURN) = atoi(G_STRING(OFS_PARM0));
}
static void PF_itos(void)
{
	char *result = PR_GetTempString();
	q_snprintf(result, STRINGTEMP_LENGTH, "%i", G_INT(OFS_PARM0));
	G_INT(OFS_RETURN) = PR_SetEngineString(result);
}
static void PF_etos(void)
{	//yes, this is lame
	char *result = PR_GetTempString();
	q_snprintf(result, STRINGTEMP_LENGTH, "entity %i", G_EDICTNUM(OFS_PARM0));
	G_INT(OFS_RETURN) = PR_SetEngineString(result);
}
static void PF_stoh(void)
{
	G_INT(OFS_RETURN) = strtoul(G_STRING(OFS_PARM0), NULL, 16);
}
static void PF_htos(void)
{
	char *result = PR_GetTempString();
	q_snprintf(result, STRINGTEMP_LENGTH, "%x", G_INT(OFS_PARM0));
	G_INT(OFS_RETURN) = PR_SetEngineString(result);
}
static void PF_ftoi(void)
{
	G_INT(OFS_RETURN) = G_FLOAT(OFS_PARM0);
}
static void PF_itof(void)
{
	G_FLOAT(OFS_RETURN) = G_INT(OFS_PARM0);
}

static void PF_cl_registercommand(void)
{
	const char *cmdname = G_STRING(OFS_PARM0);
	Cmd_AddCommand(cmdname, NULL);
}

static struct svcustomstat_s *PR_CustomStat(int idx, int type)
{
	size_t i;
	if (idx < 0 || idx >= MAX_CL_STATS)
		return NULL;
	switch(type)
	{
	case ev_ext_integer:
	case ev_float:
	case ev_entity:
	case ev_string:
		break;
	case ev_vector:
		if (idx > MAX_CL_STATS - 3)
			return NULL;
		break;
	default:
		return NULL;
	}

	for (i = 0; i < sv.numcustomstats; i++)
	{
		if (sv.customstats[i].idx == idx && (sv.customstats[i].type==ev_string) == (type==ev_string))
			break;
	}
	if (i == sv.numcustomstats)
		sv.numcustomstats++;
	sv.customstats[i].idx = idx;
	sv.customstats[i].type = type;
	sv.customstats[i].fld = 0;
	sv.customstats[i].ptr = NULL;
	return &sv.customstats[i];
}
static void PF_clientstat(void)
{
	int idx = G_FLOAT(OFS_PARM0);
	int type = G_FLOAT(OFS_PARM1);
	int fldofs = G_INT(OFS_PARM2);
	struct svcustomstat_s *stat = PR_CustomStat(idx, type);
	if (!stat)
		return;
	stat->fld = fldofs;
}

/*
===============================================================================

	QSS/FTE/DP compatibility builtins used by modern mods

===============================================================================
*/

static qboolean PF_QCPathAllowed(const char *name)
{
	const char *p;

	if (!name || !*name)
		return false;
	if (name[0] == '/' || name[0] == '\\' || strchr(name, ':'))
		return false;
	for (p = name; *p; p++)
	{
		if (*p == '\\')
			return false;
		if (p[0] == '.' && p[1] == '.' &&
			(p == name || p[-1] == '/') &&
			(p[2] == 0 || p[2] == '/'))
			return false;
	}
	return true;
}

static void PF_QCCreatePath(char *path)
{
	char *s;

	for (s = path + 1; *s; s++)
	{
		if (*s == '/')
		{
			*s = 0;
			Sys_mkdir(path);
			*s = '/';
		}
	}
}

#define QC_FILE_BASE 1
#define MAX_QC_FILES 32
typedef struct qcfile_s
{
	qcvm_t	*owner;
	FILE	*file;
	int	mode;
	int	size;
	int	pos;
} qcfile_t;
static qcfile_t qcfiles[MAX_QC_FILES];

static qcfile_t *PF_GetQCFile(int handle)
{
	handle -= QC_FILE_BASE;
	if ((unsigned int)handle >= MAX_QC_FILES)
		return NULL;
	if (!qcfiles[handle].file || qcfiles[handle].owner != qcvm)
		return NULL;
	return &qcfiles[handle];
}

static void PF_fopen(void)
{
	const char *name = G_STRING(OFS_PARM0);
	int mode = G_FLOAT(OFS_PARM1);
	FILE *file = NULL;
	char path[MAX_OSPATH];
	int i, size = 0;

	G_FLOAT(OFS_RETURN) = -1;
	if (!PF_QCPathAllowed(name))
	{
		Con_Printf("PF_fopen: access denied: %s\n", name);
		return;
	}

	if (mode == 0)
	{
		size = COM_FOpenFile(name, &file, NULL);
		if (size < 0 || !file)
			return;
	}
	else if (mode == 1 || mode == 2)
	{
		q_snprintf(path, sizeof(path), "%s/%s", com_gamedir, name);
		PF_QCCreatePath(path);
		file = fopen(path, mode == 1 ? "ab" : "wb");
		if (!file)
			return;
	}
	else
	{
		Con_Warning("PF_fopen: unsupported mode %i\n", mode);
		return;
	}

	for (i = 0; i < MAX_QC_FILES; i++)
	{
		if (!qcfiles[i].file)
		{
			qcfiles[i].owner = qcvm;
			qcfiles[i].file = file;
			qcfiles[i].mode = mode;
			qcfiles[i].size = size;
			qcfiles[i].pos = 0;
			G_FLOAT(OFS_RETURN) = i + QC_FILE_BASE;
			return;
		}
	}

	fclose(file);
}

static void PF_fclose(void)
{
	qcfile_t *file = PF_GetQCFile(G_FLOAT(OFS_PARM0));

	if (!file)
		return;
	fclose(file->file);
	memset(file, 0, sizeof(*file));
}

static void PF_fgets(void)
{
	qcfile_t *file = PF_GetQCFile(G_FLOAT(OFS_PARM0));
	char *out = PR_GetTempString();
	int c, len = 0;

	G_INT(OFS_RETURN) = 0;
	if (!file || file->mode != 0)
		return;

	while (file->pos < file->size && len < STRINGTEMP_LENGTH - 1)
	{
		c = fgetc(file->file);
		if (c == EOF)
			break;
		file->pos++;
		if (c == '\n')
			break;
		if (c == '\r')
			continue;
		out[len++] = c;
	}
	if (!len && file->pos >= file->size)
		return;
	out[len] = 0;
	G_INT(OFS_RETURN) = PR_SetEngineString(out);
}

static void PF_fputs(void)
{
	qcfile_t *file = PF_GetQCFile(G_FLOAT(OFS_PARM0));

	if (!file || file->mode == 0)
		return;
	fputs(PF_VarString(1), file->file);
}

#define MAX_QC_SEARCHES 16
typedef struct qcsearchfile_s
{
	char		name[MAX_QPATH];
	int		size;
	searchpath_t	*searchpath;
} qcsearchfile_t;

typedef struct qcsearch_s
{
	qcvm_t		*owner;
	qcsearchfile_t	*files;
	int		numfiles;
	int		maxfiles;
} qcsearch_t;

static qcsearch_t qcsearches[MAX_QC_SEARCHES];

static qboolean PF_WildMatch(const char *pattern, const char *text)
{
	while (*pattern)
	{
		if (*pattern == '*')
		{
			while (*pattern == '*')
				pattern++;
			if (!*pattern)
				return true;
			while (*text)
			{
				if (PF_WildMatch(pattern, text))
					return true;
				text++;
			}
			return false;
		}
		if (*pattern == '?')
		{
			if (!*text)
				return false;
			pattern++;
			text++;
			continue;
		}
		if (q_tolower(*pattern) != q_tolower(*text))
			return false;
		pattern++;
		text++;
	}
	return !*text;
}

static void PF_SearchAdd(qcsearch_t *search, const char *name, int size, searchpath_t *spath)
{
	int i;

	for (i = 0; i < search->numfiles; i++)
		if (!q_strcasecmp(search->files[i].name, name))
			return;

	if (search->numfiles == search->maxfiles)
	{
		search->maxfiles = search->maxfiles ? search->maxfiles * 2 : 32;
		search->files = Z_Realloc(search->files, search->maxfiles * sizeof(*search->files));
	}
	q_strlcpy(search->files[search->numfiles].name, name, sizeof(search->files[search->numfiles].name));
	search->files[search->numfiles].size = size;
	search->files[search->numfiles].searchpath = spath;
	search->numfiles++;
}

static void PF_SearchLooseDir(qcsearch_t *out, searchpath_t *spath, const char *pattern)
{
	char dirname[MAX_QPATH], basename[MAX_QPATH], osdir[MAX_OSPATH], qname[MAX_QPATH];
	const char *slash;
#ifdef _WIN32
	char findpattern[MAX_OSPATH];
	WIN32_FIND_DATA fdat;
	HANDLE fhnd;
#else
	DIR *dir;
	struct dirent *ent;
#endif

	slash = strrchr(pattern, '/');
	if (slash)
	{
		size_t len = slash - pattern;
		if (len >= sizeof(dirname))
			return;
		memcpy(dirname, pattern, len);
		dirname[len] = 0;
		q_strlcpy(basename, slash + 1, sizeof(basename));
		q_snprintf(osdir, sizeof(osdir), "%s/%s", spath->filename, dirname);
	}
	else
	{
		dirname[0] = 0;
		q_strlcpy(basename, pattern, sizeof(basename));
		q_strlcpy(osdir, spath->filename, sizeof(osdir));
	}

#ifdef _WIN32
	q_snprintf(findpattern, sizeof(findpattern), "%s/%s", osdir, basename);
	fhnd = FindFirstFile(findpattern, &fdat);
	if (fhnd == INVALID_HANDLE_VALUE)
		return;
	do
	{
		uint64_t filesize;

		if (!strcmp(fdat.cFileName, ".") || !strcmp(fdat.cFileName, ".."))
			continue;
		if (fdat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		if (dirname[0])
			q_snprintf(qname, sizeof(qname), "%s/%s", dirname, fdat.cFileName);
		else
			q_strlcpy(qname, fdat.cFileName, sizeof(qname));
		filesize = ((uint64_t)fdat.nFileSizeHigh << 32) | fdat.nFileSizeLow;
		PF_SearchAdd(out, qname, filesize > INT_MAX ? INT_MAX : (int)filesize, spath);
	} while (FindNextFile(fhnd, &fdat));
	FindClose(fhnd);
#else
	dir = opendir(osdir);
	if (!dir)
		return;
	while ((ent = readdir(dir)) != NULL)
	{
		char ospath[MAX_OSPATH];
		FILE *f;
		int size;

		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
			continue;
		if (!PF_WildMatch(basename, ent->d_name))
			continue;
		if (dirname[0])
			q_snprintf(qname, sizeof(qname), "%s/%s", dirname, ent->d_name);
		else
			q_strlcpy(qname, ent->d_name, sizeof(qname));
		q_snprintf(ospath, sizeof(ospath), "%s/%s", spath->filename, qname);
		if (!(Sys_FileType(ospath) & FS_ENT_FILE))
			continue;
		f = fopen(ospath, "rb");
		if (!f)
			continue;
		fseek(f, 0, SEEK_END);
		size = ftell(f);
		fclose(f);
		PF_SearchAdd(out, qname, size, spath);
	}
	closedir(dir);
#endif
}

static qcsearch_t *PF_GetQCSearch(int handle)
{
	if ((unsigned int)handle >= MAX_QC_SEARCHES)
		return NULL;
	if (qcsearches[handle].owner != qcvm)
		return NULL;
	return &qcsearches[handle];
}

static void PF_search_begin(void)
{
	const char *pattern = G_STRING(OFS_PARM0);
	searchpath_t *spath;
	int i, j;

	G_FLOAT(OFS_RETURN) = -1;
	if (!PF_QCPathAllowed(pattern))
		return;

	for (i = 0; i < MAX_QC_SEARCHES; i++)
		if (!qcsearches[i].owner)
			break;
	if (i == MAX_QC_SEARCHES)
		return;

	qcsearches[i].owner = qcvm;
	qcsearches[i].numfiles = 0;
	qcsearches[i].maxfiles = 0;
	qcsearches[i].files = NULL;

	for (spath = com_searchpaths; spath; spath = spath->next)
	{
		if (spath->pack)
		{
			for (j = 0; j < spath->pack->numfiles; j++)
				if (PF_WildMatch(pattern, spath->pack->files[j].name))
					PF_SearchAdd(&qcsearches[i], spath->pack->files[j].name,
						spath->pack->files[j].filelen, spath);
		}
		else
			PF_SearchLooseDir(&qcsearches[i], spath, pattern);
	}

	if (!qcsearches[i].numfiles)
	{
		qcsearches[i].owner = NULL;
		if (qcsearches[i].files)
			Z_Free(qcsearches[i].files);
		qcsearches[i].files = NULL;
		return;
	}
	G_FLOAT(OFS_RETURN) = i;
}

static void PF_search_end(void)
{
	qcsearch_t *search = PF_GetQCSearch(G_FLOAT(OFS_PARM0));

	if (!search)
		return;
	if (search->files)
		Z_Free(search->files);
	memset(search, 0, sizeof(*search));
}

static void PF_search_getsize(void)
{
	qcsearch_t *search = PF_GetQCSearch(G_FLOAT(OFS_PARM0));

	G_FLOAT(OFS_RETURN) = search ? search->numfiles : 0;
}

static void PF_search_getfilename(void)
{
	qcsearch_t *search = PF_GetQCSearch(G_FLOAT(OFS_PARM0));
	int index = G_FLOAT(OFS_PARM1);
	char *out;

	G_INT(OFS_RETURN) = 0;
	if (!search || (unsigned int)index >= (unsigned int)search->numfiles)
		return;
	out = PR_GetTempString();
	q_strlcpy(out, search->files[index].name, STRINGTEMP_LENGTH);
	G_INT(OFS_RETURN) = PR_SetEngineString(out);
}

static void PF_whichpack(void)
{
	const char *name = G_STRING(OFS_PARM0);
	searchpath_t *spath;
	int i;

	G_INT(OFS_RETURN) = 0;
	if (!PF_QCPathAllowed(name))
		return;

	for (spath = com_searchpaths; spath; spath = spath->next)
	{
		if (spath->pack)
		{
			for (i = 0; i < spath->pack->numfiles; i++)
			{
				if (!strcmp(spath->pack->files[i].name, name))
				{
					char *out = PR_GetTempString();
					q_strlcpy(out, COM_SkipPath(spath->pack->filename), STRINGTEMP_LENGTH);
					G_INT(OFS_RETURN) = PR_SetEngineString(out);
					return;
				}
			}
		}
		else
		{
			char path[MAX_OSPATH];
			q_snprintf(path, sizeof(path), "%s/%s", spath->filename, name);
			if (Sys_FileType(path) & FS_ENT_FILE)
			{
				char *out = PR_GetTempString();
				q_strlcpy(out, COM_SkipPath(spath->filename), STRINGTEMP_LENGTH);
				G_INT(OFS_RETURN) = PR_SetEngineString(out);
				return;
			}
		}
	}
}

#define BUFSTRBASE 1
#define NUMSTRINGBUFS 64u
typedef struct qcstrbuf_s
{
	qcvm_t		*owner;
	char		**strings;
	unsigned int	used;
	unsigned int	allocated;
} qcstrbuf_t;
static qcstrbuf_t strbuflist[NUMSTRINGBUFS];

static qcstrbuf_t *PF_GetStrBuf(int handle)
{
	handle -= BUFSTRBASE;
	if ((unsigned int)handle >= NUMSTRINGBUFS)
		return NULL;
	if (strbuflist[handle].owner != qcvm)
		return NULL;
	return &strbuflist[handle];
}

static void PF_StrBufClear(qcstrbuf_t *buf)
{
	unsigned int i;

	for (i = 0; i < buf->used; i++)
		if (buf->strings[i])
			Z_Free(buf->strings[i]);
	if (buf->strings)
		Z_Free(buf->strings);
	buf->strings = NULL;
	buf->used = 0;
	buf->allocated = 0;
}

void PR_ClearBuiltinState(qcvm_t *vm)
{
	unsigned int i;

	for (i = 0; i < MAX_QC_FILES; i++)
	{
		if (qcfiles[i].owner == vm)
		{
			if (qcfiles[i].file)
				fclose(qcfiles[i].file);
			memset(&qcfiles[i], 0, sizeof(qcfiles[i]));
		}
	}

	for (i = 0; i < MAX_QC_SEARCHES; i++)
	{
		if (qcsearches[i].owner == vm)
		{
			if (qcsearches[i].files)
				Z_Free(qcsearches[i].files);
			memset(&qcsearches[i], 0, sizeof(qcsearches[i]));
		}
	}

	for (i = 0; i < NUMSTRINGBUFS; i++)
	{
		if (strbuflist[i].owner == vm)
		{
			PF_StrBufClear(&strbuflist[i]);
			memset(&strbuflist[i], 0, sizeof(strbuflist[i]));
		}
	}
}

static void PF_StrBufSet(qcstrbuf_t *buf, unsigned int index, const char *string)
{
	unsigned int old;

	if (index >= buf->allocated)
	{
		old = buf->allocated;
		buf->allocated = index + 256;
		buf->strings = Z_Realloc(buf->strings, buf->allocated * sizeof(*buf->strings));
		memset(buf->strings + old, 0, (buf->allocated - old) * sizeof(*buf->strings));
	}
	if (buf->strings[index])
		Z_Free(buf->strings[index]);
	buf->strings[index] = Z_Strdup(string);
	if (index >= buf->used)
		buf->used = index + 1;
}

static int PF_StrBufAdd(qcstrbuf_t *buf, const char *string, qboolean append)
{
	unsigned int index;

	if (append)
		index = buf->used;
	else
	{
		for (index = 0; index < buf->used; index++)
			if (!buf->strings[index])
				break;
	}
	PF_StrBufSet(buf, index, string);
	return index;
}

static void PF_buf_create(void)
{
	unsigned int i;

	G_FLOAT(OFS_RETURN) = -1;
	if (qcvm->argc > 0 && q_strcasecmp(G_STRING(OFS_PARM0), "string"))
		return;

	for (i = 0; i < NUMSTRINGBUFS; i++)
	{
		if (!strbuflist[i].owner)
		{
			memset(&strbuflist[i], 0, sizeof(strbuflist[i]));
			strbuflist[i].owner = qcvm;
			G_FLOAT(OFS_RETURN) = i + BUFSTRBASE;
			return;
		}
	}
}

static void PF_buf_del(void)
{
	qcstrbuf_t *buf = PF_GetStrBuf(G_FLOAT(OFS_PARM0));

	if (!buf)
		return;
	PF_StrBufClear(buf);
	buf->owner = NULL;
}

static void PF_buf_getsize(void)
{
	qcstrbuf_t *buf = PF_GetStrBuf(G_FLOAT(OFS_PARM0));

	G_FLOAT(OFS_RETURN) = buf ? buf->used : 0;
}

static void PF_buf_copy(void)
{
	qcstrbuf_t *from = PF_GetStrBuf(G_FLOAT(OFS_PARM0));
	qcstrbuf_t *to = PF_GetStrBuf(G_FLOAT(OFS_PARM1));
	unsigned int i;

	if (!from || !to || from == to)
		return;
	PF_StrBufClear(to);
	for (i = 0; i < from->used; i++)
		if (from->strings[i])
			PF_StrBufSet(to, i, from->strings[i]);
	to->used = from->used;
}

static int PF_buf_sort_prefixlen;
static int PF_buf_sort_ascending(const void *a, const void *b)
{
	const char *sa = *(const char *const *)a;
	const char *sb = *(const char *const *)b;
	return strncmp(sa ? sa : "", sb ? sb : "", PF_buf_sort_prefixlen);
}

static int PF_buf_sort_descending(const void *a, const void *b)
{
	return -PF_buf_sort_ascending(a, b);
}

static void PF_buf_sort(void)
{
	qcstrbuf_t *buf = PF_GetStrBuf(G_FLOAT(OFS_PARM0));
	int prefix = G_FLOAT(OFS_PARM1);
	int backwards = G_FLOAT(OFS_PARM2);
	unsigned int s, d, oldused;

	if (!buf)
		return;
	oldused = buf->used;
	for (s = 0, d = 0; s < oldused; s++)
	{
		if (!buf->strings[s])
			continue;
		buf->strings[d++] = buf->strings[s];
	}
	if (d < oldused)
		memset(buf->strings + d, 0, (oldused - d) * sizeof(*buf->strings));
	buf->used = d;
	PF_buf_sort_prefixlen = prefix > 0 ? prefix : 0x7fffffff;
	qsort(buf->strings, buf->used, sizeof(*buf->strings),
		backwards ? PF_buf_sort_descending : PF_buf_sort_ascending);
}

static void PF_buf_implode(void)
{
	qcstrbuf_t *buf = PF_GetStrBuf(G_FLOAT(OFS_PARM0));
	const char *glue = G_STRING(OFS_PARM1);
	char *out = PR_GetTempString();
	size_t gluelen = strlen(glue), len = 0;
	unsigned int i;

	G_INT(OFS_RETURN) = 0;
	if (!buf)
		return;
	out[0] = 0;
	for (i = 0; i < buf->used; i++)
	{
		size_t slen;
		if (!buf->strings[i])
			continue;
		if (len && gluelen)
		{
			if (len + gluelen >= STRINGTEMP_LENGTH)
				break;
			memcpy(out + len, glue, gluelen);
			len += gluelen;
		}
		slen = strlen(buf->strings[i]);
		if (len + slen >= STRINGTEMP_LENGTH)
			slen = STRINGTEMP_LENGTH - len - 1;
		memcpy(out + len, buf->strings[i], slen);
		len += slen;
		if (len >= STRINGTEMP_LENGTH - 1)
			break;
	}
	out[len] = 0;
	G_INT(OFS_RETURN) = PR_SetEngineString(out);
}

static void PF_bufstr_get(void)
{
	qcstrbuf_t *buf = PF_GetStrBuf(G_FLOAT(OFS_PARM0));
	int index = G_FLOAT(OFS_PARM1);
	char *out;

	G_INT(OFS_RETURN) = 0;
	if (!buf || (unsigned int)index >= buf->used || !buf->strings[index])
		return;
	out = PR_GetTempString();
	q_strlcpy(out, buf->strings[index], STRINGTEMP_LENGTH);
	G_INT(OFS_RETURN) = PR_SetEngineString(out);
}

static void PF_bufstr_set(void)
{
	qcstrbuf_t *buf = PF_GetStrBuf(G_FLOAT(OFS_PARM0));
	int index = G_FLOAT(OFS_PARM1);

	if (!buf || index < 0)
		return;
	PF_StrBufSet(buf, index, G_STRING(OFS_PARM2));
}

static void PF_bufstr_add(void)
{
	qcstrbuf_t *buf = PF_GetStrBuf(G_FLOAT(OFS_PARM0));

	G_FLOAT(OFS_RETURN) = -1;
	if (!buf)
		return;
	G_FLOAT(OFS_RETURN) = PF_StrBufAdd(buf, G_STRING(OFS_PARM1), G_FLOAT(OFS_PARM2));
}

static void PF_bufstr_free(void)
{
	qcstrbuf_t *buf = PF_GetStrBuf(G_FLOAT(OFS_PARM0));
	int index = G_FLOAT(OFS_PARM1);

	if (!buf || (unsigned int)index >= buf->used)
		return;
	if (buf->strings[index])
		Z_Free(buf->strings[index]);
	buf->strings[index] = NULL;
}

static void PF_buf_cvarlist(void)
{
	qcstrbuf_t *buf = PF_GetStrBuf(G_FLOAT(OFS_PARM0));
	const char *pattern = G_STRING(OFS_PARM1);
	const char *antipattern = qcvm->argc > 2 ? G_STRING(OFS_PARM2) : "";
	cvar_t *var;

	if (!buf)
		return;
	PF_StrBufClear(buf);
	for (var = Cvar_FindVarAfter("", CVAR_NONE); var; var = var->next)
	{
		if (*pattern && !PF_WildMatch(pattern, var->name))
			continue;
		if (*antipattern && PF_WildMatch(antipattern, var->name))
			continue;
		PF_StrBufAdd(buf, var->name, true);
	}
	PF_buf_sort_prefixlen = 0x7fffffff;
	qsort(buf->strings, buf->used, sizeof(*buf->strings), PF_buf_sort_ascending);
}

static void PF_buf_loadfile(void)
{
	const char *name = G_STRING(OFS_PARM0);
	qcstrbuf_t *buf = PF_GetStrBuf(G_FLOAT(OFS_PARM1));
	char *data, *line;

	G_FLOAT(OFS_RETURN) = 0;
	if (!buf || !PF_QCPathAllowed(name))
		return;
	data = (char *)COM_LoadTempFile(name, NULL);
	if (!data)
		return;
	line = data;
	while (line && *line)
	{
		char *nl = strchr(line, '\n');
		if (nl)
			*nl++ = 0;
		if (*line && line[strlen(line) - 1] == '\r')
			line[strlen(line) - 1] = 0;
		PF_StrBufAdd(buf, line, true);
		line = nl;
	}
	G_FLOAT(OFS_RETURN) = 1;
}

static void PF_buf_writefile(void)
{
	qcfile_t *file = PF_GetQCFile(G_FLOAT(OFS_PARM0));
	qcstrbuf_t *buf = PF_GetStrBuf(G_FLOAT(OFS_PARM1));
	unsigned int start = qcvm->argc > 2 ? q_max(0, (int)G_FLOAT(OFS_PARM2)) : 0;
	unsigned int end = buf ? buf->used : 0;
	unsigned int i;

	G_FLOAT(OFS_RETURN) = 0;
	if (!file || file->mode == 0 || !buf)
		return;
	if (qcvm->argc > 3)
		end = q_min(end, start + q_max(0, (int)G_FLOAT(OFS_PARM3)));
	for (i = start; i < end; i++)
		if (buf->strings[i])
			fprintf(file->file, "%s\n", buf->strings[i]);
	G_FLOAT(OFS_RETURN) = 1;
}

static void PF_copyentity(void)
{
	edict_t *src = G_EDICT(OFS_PARM0);
	edict_t *dst = qcvm->argc > 1 ? G_EDICT(OFS_PARM1) : ED_Alloc();

	if (src->free || dst->free)
	{
		Con_Printf("PF_copyentity: entity is free\n");
		RETURN_EDICT(qcvm->edicts);
		return;
	}
	memcpy(&dst->v, &src->v, qcvm->progs->entityfields * 4);
	dst->alpha = src->alpha;
	dst->scale = src->scale;
	dst->sendinterval = src->sendinterval;
	if (qcvm == &sv.qcvm)
		SV_LinkEdict(dst, false);
	RETURN_EDICT(dst);
}

static void PF_edict_for_num(void)
{
	int num = G_FLOAT(OFS_PARM0);

	if (num < 0 || num >= qcvm->num_edicts)
		num = 0;
	RETURN_EDICT(EDICT_NUM(num));
}

static void PF_num_for_edict(void)
{
	G_FLOAT(OFS_RETURN) = G_EDICTNUM(OFS_PARM0);
}

static void PF_findfloat(void)
{
	int e = G_EDICTNUM(OFS_PARM0);
	int f = G_INT(OFS_PARM1);
	float match = G_FLOAT(OFS_PARM2);
	edict_t *ed;

	for (e++; e < qcvm->num_edicts; e++)
	{
		ed = EDICT_NUM(e);
		if (!ed->free && E_FLOAT(ed, f) == match)
		{
			RETURN_EDICT(ed);
			return;
		}
	}
	RETURN_EDICT(qcvm->edicts);
}

static void PF_findchain(void)
{
	edict_t *ent = NEXT_EDICT(qcvm->edicts), *chain = qcvm->edicts;
	int i, field = G_INT(OFS_PARM0);
	const char *match = G_STRING(OFS_PARM1);
	int chainfield = qcvm->argc > 2 ? G_INT(OFS_PARM2) : (int *)(&ent->v.chain) - (int *)(&ent->v);

	for (i = 1; i < qcvm->num_edicts; i++, ent = NEXT_EDICT(ent))
	{
		if (ent->free || strcmp(E_STRING(ent, field), match))
			continue;
		((int *)&ent->v)[chainfield] = EDICT_TO_PROG(chain);
		chain = ent;
	}
	RETURN_EDICT(chain);
}

static void PF_findchainfloat(void)
{
	edict_t *ent = NEXT_EDICT(qcvm->edicts), *chain = qcvm->edicts;
	int i, field = G_INT(OFS_PARM0);
	float match = G_FLOAT(OFS_PARM1);
	int chainfield = qcvm->argc > 2 ? G_INT(OFS_PARM2) : (int *)(&ent->v.chain) - (int *)(&ent->v);

	for (i = 1; i < qcvm->num_edicts; i++, ent = NEXT_EDICT(ent))
	{
		if (ent->free || E_FLOAT(ent, field) != match)
			continue;
		((int *)&ent->v)[chainfield] = EDICT_TO_PROG(chain);
		chain = ent;
	}
	RETURN_EDICT(chain);
}

static void PF_findflags(void)
{
	int e = G_EDICTNUM(OFS_PARM0);
	int f = G_INT(OFS_PARM1);
	int match = G_FLOAT(OFS_PARM2);
	edict_t *ed;

	for (e++; e < qcvm->num_edicts; e++)
	{
		ed = EDICT_NUM(e);
		if (!ed->free && ((int)E_FLOAT(ed, f) & match))
		{
			RETURN_EDICT(ed);
			return;
		}
	}
	RETURN_EDICT(qcvm->edicts);
}

static void PF_findchainflags(void)
{
	edict_t *ent = NEXT_EDICT(qcvm->edicts), *chain = qcvm->edicts;
	int i, field = G_INT(OFS_PARM0);
	int match = G_FLOAT(OFS_PARM1);
	int chainfield = qcvm->argc > 2 ? G_INT(OFS_PARM2) : (int *)(&ent->v.chain) - (int *)(&ent->v);

	for (i = 1; i < qcvm->num_edicts; i++, ent = NEXT_EDICT(ent))
	{
		if (ent->free || !((int)E_FLOAT(ent, field) & match))
			continue;
		((int *)&ent->v)[chainfield] = EDICT_TO_PROG(chain);
		chain = ent;
	}
	RETURN_EDICT(chain);
}

static void PF_numentityfields(void)
{
	G_FLOAT(OFS_RETURN) = qcvm->progs->numfielddefs;
}

static void PF_findentityfield(void)
{
	ddef_t *field = ED_FindField(G_STRING(OFS_PARM0));
	G_FLOAT(OFS_RETURN) = field ? field - qcvm->fielddefs : 0;
}

static void PF_entityfieldref(void)
{
	unsigned int idx = G_FLOAT(OFS_PARM0);
	G_INT(OFS_RETURN) = idx < (unsigned int)qcvm->progs->numfielddefs ? qcvm->fielddefs[idx].ofs : 0;
}

static void PF_entityfieldname(void)
{
	unsigned int idx = G_FLOAT(OFS_PARM0);
	G_INT(OFS_RETURN) = idx < (unsigned int)qcvm->progs->numfielddefs ? qcvm->fielddefs[idx].s_name : 0;
}

static void PF_entityfieldtype(void)
{
	unsigned int idx = G_FLOAT(OFS_PARM0);
	G_FLOAT(OFS_RETURN) = idx < (unsigned int)qcvm->progs->numfielddefs ? (qcvm->fielddefs[idx].type & ~DEF_SAVEGLOBAL) : ev_void;
}

static void PF_getentityfieldstring(void)
{
	unsigned int idx = G_FLOAT(OFS_PARM0);
	edict_t *ent = G_EDICT(OFS_PARM1);
	char *out;

	G_INT(OFS_RETURN) = 0;
	if (idx >= (unsigned int)qcvm->progs->numfielddefs)
		return;
	out = PR_GetTempString();
	q_strlcpy(out, PR_UglyValueString(qcvm->fielddefs[idx].type,
		(eval_t *)((float *)&ent->v + qcvm->fielddefs[idx].ofs)), STRINGTEMP_LENGTH);
	G_INT(OFS_RETURN) = PR_SetEngineString(out);
}

static void PF_putentityfieldstring(void)
{
	unsigned int idx = G_FLOAT(OFS_PARM0);
	edict_t *ent = G_EDICT(OFS_PARM1);

	G_FLOAT(OFS_RETURN) = 0;
	if (idx >= (unsigned int)qcvm->progs->numfielddefs)
		return;
	G_FLOAT(OFS_RETURN) = ED_ParseEpair((void *)&ent->v, qcvm->fielddefs + idx, G_STRING(OFS_PARM2), qcvm != &sv.qcvm);
	if (qcvm == &sv.qcvm)
		SV_LinkEdict(ent, false);
}

static void PF_globalstat(void)
{
	int idx = G_FLOAT(OFS_PARM0);
	int type = G_FLOAT(OFS_PARM1);
	ddef_t *global = ED_FindGlobal(G_STRING(OFS_PARM2));
	struct svcustomstat_s *stat;

	if (!global)
		return;
	stat = PR_CustomStat(idx, type);
	if (!stat)
		return;
	stat->fld = 0;
	stat->ptr = (eval_t *)&qcvm->globals[global->ofs];
}

static void PF_isbackbuffered(void)
{
	edict_t *ed = G_EDICT(OFS_PARM0);
	int idx = NUM_FOR_EDICT(ed) - 1;

	G_FLOAT(OFS_RETURN) = 0;
	if ((unsigned int)idx < (unsigned int)svs.maxclients && svs.clients[idx].active)
		G_FLOAT(OFS_RETURN) = svs.clients[idx].message.cursize > 0;
}

static void PF_parseentitydata(void)
{
	edict_t *ent = G_EDICT(OFS_PARM0);
	const char *data = G_STRING(OFS_PARM1);
	int offset = qcvm->argc > 2 ? G_FLOAT(OFS_PARM2) : 0;
	const char *start, *end;

	G_FLOAT(OFS_RETURN) = 0;
	if (offset < 0)
		offset = 0;
	if (offset >= (int)strlen(data))
		return;
	start = data + offset;
	end = COM_Parse(start);
	if (!end || com_token[0] != '{')
		return;
	end = ED_ParseEdict(end, ent);
	G_FLOAT(OFS_RETURN) = end ? end - data : 0;
}

static void PF_callfunction(void)
{
	const char *name = G_STRING(OFS_PARM0 + (qcvm->argc - 1) * 3);
	dfunction_t *func = ED_FindFunction(name);

	if (func)
		PR_ExecuteProgram(func - qcvm->functions);
}

static void PF_isfunction(void)
{
	G_FLOAT(OFS_RETURN) = ED_FindFunction(G_STRING(OFS_PARM0)) != NULL;
}

static qboolean PF_IsColorCode(const char *s)
{
	if (s[0] != '^')
		return false;
	if (s[1] >= '0' && s[1] <= '9')
		return true;
	if (s[1] == 'x' && q_isxdigit(s[2]) && q_isxdigit(s[3]) && q_isxdigit(s[4]))
		return true;
	return false;
}

static int PF_ColorCodeLength(const char *s)
{
	if (s[1] == 'x')
		return 5;
	return 2;
}

static void PF_strlennocol(void)
{
	const char *s = G_STRING(OFS_PARM0);
	int len = 0;

	while (*s)
	{
		if (PF_IsColorCode(s))
		{
			s += PF_ColorCodeLength(s);
			continue;
		}
		s++;
		len++;
	}
	G_FLOAT(OFS_RETURN) = len;
}

static void PF_strdecolorize(void)
{
	const char *s = G_STRING(OFS_PARM0);
	char *out = PR_GetTempString(), *d = out;

	while (*s && d < out + STRINGTEMP_LENGTH - 1)
	{
		if (PF_IsColorCode(s))
		{
			s += PF_ColorCodeLength(s);
			continue;
		}
		*d++ = *s++;
	}
	*d = 0;
	G_INT(OFS_RETURN) = PR_SetEngineString(out);
}

static void PF_strreplace_internal(qboolean insensitive)
{
	const char *search = G_STRING(OFS_PARM0);
	const char *replace = G_STRING(OFS_PARM1);
	const char *subject = G_STRING(OFS_PARM2);
	char *out = PR_GetTempString();
	size_t slen = strlen(search), rlen = strlen(replace), len = 0;

	if (!slen)
	{
		q_strlcpy(out, subject, STRINGTEMP_LENGTH);
		G_INT(OFS_RETURN) = PR_SetEngineString(out);
		return;
	}
	while (*subject && len < STRINGTEMP_LENGTH - 1)
	{
		qboolean match = insensitive ? !q_strncasecmp(subject, search, slen) : !strncmp(subject, search, slen);
		if (match)
		{
			size_t copy = q_min(rlen, (size_t)(STRINGTEMP_LENGTH - len - 1));
			memcpy(out + len, replace, copy);
			len += copy;
			subject += slen;
		}
		else
			out[len++] = *subject++;
	}
	out[len] = 0;
	G_INT(OFS_RETURN) = PR_SetEngineString(out);
}

static void PF_strreplace(void)
{
	PF_strreplace_internal(false);
}

static void PF_strireplace(void)
{
	PF_strreplace_internal(true);
}

static void PF_uri_escape(void)
{
	const char *s = G_STRING(OFS_PARM0);
	char *out = PR_GetTempString(), *d = out;
	static const char hex[] = "0123456789ABCDEF";

	while (*s && d < out + STRINGTEMP_LENGTH - 1)
	{
		unsigned char c = (unsigned char)*s++;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
			*d++ = c;
		else if (d < out + STRINGTEMP_LENGTH - 3)
		{
			*d++ = '%';
			*d++ = hex[c >> 4];
			*d++ = hex[c & 15];
		}
	}
	*d = 0;
	G_INT(OFS_RETURN) = PR_SetEngineString(out);
}

static int PF_HexValue(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static void PF_uri_unescape(void)
{
	const char *s = G_STRING(OFS_PARM0);
	char *out = PR_GetTempString(), *d = out;

	while (*s && d < out + STRINGTEMP_LENGTH - 1)
	{
		if (*s == '%' && PF_HexValue(s[1]) >= 0 && PF_HexValue(s[2]) >= 0)
		{
			*d++ = (PF_HexValue(s[1]) << 4) | PF_HexValue(s[2]);
			s += 3;
		}
		else if (*s == '+')
		{
			*d++ = ' ';
			s++;
		}
		else
			*d++ = *s++;
	}
	*d = 0;
	G_INT(OFS_RETURN) = PR_SetEngineString(out);
}

static void PF_crc16(void)
{
	qboolean insensitive = G_FLOAT(OFS_PARM0);
	const char *s = PF_VarString(1);
	unsigned short crc;

	CRC_Init(&crc);
	while (*s)
	{
		unsigned char c = (unsigned char)*s++;
		if (insensitive && c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
		CRC_ProcessByte(&crc, c);
	}
	G_FLOAT(OFS_RETURN) = CRC_Value(crc);
}

static void PF_cl_sendevent(void)
{
	const char *eventname = G_STRING(OFS_PARM0);
	const char *eventargs = G_STRING(OFS_PARM1);
	int a;

	if (cls.state != ca_connected)
		return;
	MSG_WriteByte(&cls.message, clcfte_qcrequest);
	for (a = 2; a < 8 && *eventargs; a++, eventargs++)
	{
		switch (*eventargs)
		{
		case 's':
			MSG_WriteByte(&cls.message, ev_string);
			MSG_WriteString(&cls.message, G_STRING(OFS_PARM0 + a * 3));
			break;
		case 'f':
			MSG_WriteByte(&cls.message, ev_float);
			MSG_WriteFloat(&cls.message, G_FLOAT(OFS_PARM0 + a * 3));
			break;
		case 'i':
			MSG_WriteByte(&cls.message, ev_ext_integer);
			MSG_WriteLong(&cls.message, G_INT(OFS_PARM0 + a * 3));
			break;
		case 'v':
			MSG_WriteByte(&cls.message, ev_vector);
			MSG_WriteFloat(&cls.message, G_FLOAT(OFS_PARM0 + a * 3 + 0));
			MSG_WriteFloat(&cls.message, G_FLOAT(OFS_PARM0 + a * 3 + 1));
			MSG_WriteFloat(&cls.message, G_FLOAT(OFS_PARM0 + a * 3 + 2));
			break;
		case 'e':
		{
			edict_t *ed = G_EDICT(OFS_PARM0 + a * 3);
			eval_t *entnum = GetEdictFieldValue(ed, ED_FindFieldOffset("entnum"));
			MSG_WriteByte(&cls.message, ev_entity);
			MSG_WriteEntity(&cls.message, entnum ? entnum->_float : NUM_FOR_EDICT(ed), cl.protocol_pext2);
			break;
		}
		default:
			break;
		}
	}
	MSG_WriteByte(&cls.message, 0);
	MSG_WriteString(&cls.message, eventname);
}

static void PF_checkcommand(void)
{
	const char *name = G_STRING(OFS_PARM0);

	if (Cmd_Exists(name))
		G_FLOAT(OFS_RETURN) = 1;
	else if (Cmd_AliasExists(name))
		G_FLOAT(OFS_RETURN) = 2;
	else if (Cvar_FindVar(name))
		G_FLOAT(OFS_RETURN) = 3;
	else
		G_FLOAT(OFS_RETURN) = 0;
}

//server/client stuff
static void PF_clientcommand(void)
{
	edict_t	*ed				= G_EDICT(OFS_PARM0);
	const char *str			= G_STRING(OFS_PARM1);
	unsigned int i			= NUM_FOR_EDICT(ed)-1;
	if (i < (unsigned int)svs.maxclients && svs.clients[i].active)
	{
		client_t *ohc = host_client;
		host_client = &svs.clients[i];
		Cmd_ExecuteString (str, src_client);
		host_client = ohc;
	}
	else
		Con_Printf("PF_clientcommand: not a client\n");
}


#define PF_BOTH(x)	x,x
#define PF_CSQC(x)	NULL,x
#define PF_SSQC(x)	x,NULL

builtindef_t pr_builtindefs[] =
{
	{"makevectors",				PF_SSQC(PF_makevectors),		1},		// void(entity e) makevectors		= #1
	{"setorigin",				PF_SSQC(PF_setorigin),			2},		// void(entity e, vector o) setorigin	= #2
	{"setmodel",				PF_SSQC(PF_setmodel),			3},		// void(entity e, string m) setmodel	= #3
	{"setsize",					PF_SSQC(PF_setsize),			4},		// void(entity e, vector min, vector max) setsize	= #4
	{"break",					PF_BOTH(PF_break),				6},		// void() break				= #6
	{"random",					PF_BOTH(PF_random),				7},		// float() random			= #7
	{"sound",					PF_SSQC(PF_sound),				8},		// void(entity e, float chan, string samp) sound	= #8
	{"normalize",				PF_BOTH(PF_normalize),			9},		// vector(vector v) normalize		= #9
	{"error",					PF_SSQC(PF_error),				10},	// void(string e) error			= #10
	{"objerror",				PF_SSQC(PF_objerror),			11},	// void(string e) objerror		= #11
	{"vlen",					PF_BOTH(PF_vlen),				12},	// float(vector v) vlen			= #12
	{"vectoyaw",				PF_BOTH(PF_vectoyaw),			13},	// float(vector v) vectoyaw		= #13
	{"spawn",					PF_SSQC(PF_Spawn),				14},	// entity() spawn			= #14
	{"remove",					PF_SSQC(PF_Remove),				15},	// void(entity e) remove		= #15
	{"traceline",				PF_SSQC(PF_traceline),			16},	// float(vector v1, vector v2, float tryents) traceline	= #16
	{"checkclient",				PF_SSQC(PF_checkclient),		17},	// entity() clientlist			= #17
	{"find",					PF_SSQC(PF_Find),				18},	// entity(entity start, .string fld, string match) find	= #18
	{"precache_sound",			PF_SSQC(PF_precache_sound),		19},	// void(string s) precache_sound	= #19
	{"precache_model",			PF_SSQC(PF_precache_model),		20},	// void(string s) precache_model	= #20
	{"stuffcmd",				PF_SSQC(PF_stuffcmd),			21},	// void(entity client, string s)stuffcmd	= #21
	{"findradius",				PF_SSQC(PF_findradius),			22},	// entity(vector org, float rad) findradius	= #22
	{"bprint",					PF_SSQC(PF_bprint),				23},	// void(string s) bprint		= #23
	{"sprint",					PF_SSQC(PF_sprint),				24},	// void(entity client, string s) sprint	= #24
	{"dprint",					PF_BOTH(PF_dprint),				25},	// void(string s) dprint		= #25
	{"ftos",					PF_BOTH(PF_ftos),				26},	// void(string s) ftos			= #26
	{"vtos",					PF_BOTH(PF_vtos),				27},	// void(string s) vtos			= #27
	{"coredump",				PF_SSQC(PF_coredump),			28},
	{"traceon",					PF_BOTH(PF_traceon),			29},
	{"traceoff",				PF_BOTH(PF_traceoff),			30},
	{"eprint",					PF_SSQC(PF_eprint),				31},	// void(entity e) debug print an entire entity
	{"walkmove",				PF_SSQC(PF_walkmove),			32},	// float(float yaw, float dist) walkmove
	{"droptofloor",				PF_SSQC(PF_droptofloor),		34},
	{"lightstyle",				PF_SSQC(PF_lightstyle),			35},
	{"rint",					PF_BOTH(PF_rint),				36},
	{"floor",					PF_BOTH(PF_floor),				37},
	{"ceil",					PF_BOTH(PF_ceil),				38},
	{"checkbottom",				PF_SSQC(PF_checkbottom),		40},
	{"pointcontents",			PF_SSQC(PF_pointcontents),		41},
	{"fabs",					PF_BOTH(PF_fabs),				43},
	{"aim",						PF_SSQC(PF_aim),				44},
	{"cvar",					PF_BOTH(PF_cvar),				45},
	{"localcmd",				PF_BOTH(PF_localcmd),			46},
	{"nextent",					PF_SSQC(PF_nextent),			47},
	{"particle",				PF_SSQC(PF_particle),			48},
	{"ChangeYaw",				PF_SSQC(PF_changeyaw),			49},
	{"vectoangles",				PF_BOTH(PF_vectoangles),		51},

	{"WriteByte",				PF_SSQC(PF_WriteByte),			52},
	{"WriteChar",				PF_SSQC(PF_WriteChar),			53},
	{"WriteShort",				PF_SSQC(PF_WriteShort),			54},
	{"WriteLong",				PF_SSQC(PF_WriteLong),			55},
	{"WriteCoord",				PF_SSQC(PF_WriteCoord),			56},
	{"WriteAngle",				PF_SSQC(PF_WriteAngle),			57},
	{"WriteString",				PF_SSQC(PF_WriteString),		58},
	{"WriteEntity",				PF_SSQC(PF_WriteEntity),		59},

	{"sin",						PF_BOTH(PF_Sin),				60},	// float(float angle)
	{"cos",						PF_BOTH(PF_Cos),				61},	// float(float angle)
	{"sqrt",					PF_BOTH(PF_Sqrt),				62},	// float(float value)

	{"etos",					PF_BOTH(PF_etos),				65},	// string(entity ent)

	{"movetogoal",				PF_SSQC(SV_MoveToGoal),			67},
	{"precache_file",			PF_SSQC(PF_precache_file),		68},
	{"makestatic",				PF_SSQC(PF_makestatic),			69},

	{"changelevel",				PF_SSQC(PF_changelevel),		70},

	{"cvar_set",				PF_BOTH(PF_cvar_set),			72},
	{"cvar_setlong",			PF_BOTH(PF_cvar_set),			72},
	{"centerprint",				PF_SSQC(PF_centerprint),		73},

	{"ambientsound",			PF_SSQC(PF_ambientsound),		74},

	{"precache_model2",			PF_SSQC(PF_precache_model),		75},
	{"precache_sound2",			PF_SSQC(PF_precache_sound),		76}, // precache_sound2 is different only for qcc
	{"precache_file2",			PF_SSQC(PF_precache_file),		77},

	{"setspawnparms",			PF_SSQC(PF_setspawnparms),		78},

	// 2021 re-release
	{"finaleFinished",			PF_SSQC(PF_finalefinished),		79},	// float() finaleFinished = #79
	{"localsound",				PF_SSQC(PF_localsound),			80},	// void localsound (entity client, string sample) = #80
	{"draw_point",				PF_SSQC(PF_Fixme),				81},	// void draw_point (vector point, float colormap, float lifetime, float depthtest) = #81
	{"stof",					PF_CSQC(PF_stof),				81},	// float(string) in CSQC/FTE_STRINGS-compatible hud code
	{"draw_line",				PF_SSQC(PF_Fixme),				82},	// void draw_line (vector start, vector end, float colormap, float lifetime, float depthtest) = #82
	{"draw_arrow",				PF_SSQC(PF_Fixme),				83},	// void draw_arrow (vector start, vector end, float colormap, float size, float lifetime, float depthtest) = #83
	{"draw_ray",				PF_SSQC(PF_Fixme),				84},	// void draw_ray (vector start, vector direction, float length, float colormap, float size, float lifetime, float depthtest) = #84
	{"draw_circle",				PF_SSQC(PF_Fixme),				85},	// void draw_circle (vector origin, float radius, float colormap, float lifetime, float depthtest) = #85
	{"draw_bounds",				PF_SSQC(PF_Fixme),				86},	// void draw_bounds (vector min, vector max, float colormap, float lifetime, float depthtest) = #86
	{"draw_worldtext",			PF_SSQC(PF_Fixme),				87},	// void draw_worldtext (string s, vector origin, float size, float lifetime, float depthtest) = #87
	{"draw_sphere",				PF_SSQC(PF_Fixme),				88},	// void draw_sphere (vector origin, float radius, float colormap, float lifetime, float depthtest) = #88
	{"draw_cylinder",			PF_SSQC(PF_Fixme),				89},	// void draw_cylinder (vector origin, float halfHeight, float radius, float colormap, float lifetime, float depthtest) = #89
	// 2021 re-release update 3
	{"ex_centerprint",			PF_SSQC(PF_centerprint)},				// void(entity client, string s, ...)
	{"ex_bprint",				PF_SSQC(PF_bprint)},					// void(string s, ...)
	{"ex_sprint",				PF_SSQC(PF_sprint)},					// void(entity client, string s, ...)
	{"ex_finalefinished",		PF_SSQC(PF_finalefinished)},			// float()
	{"ex_CheckPlayerEXFlags",	PF_SSQC(PF_CheckPlayerEXFlags),	90},	// float(entity playerEnt)
	{"ex_walkpathtogoal",		PF_SSQC(PF_walkpathtogoal),		91},	// float(float movedist, vector goal)
	{"ex_localsound",			PF_SSQC(PF_localsound)},				// void(entity client, string sample)

	{"registercvar",			PF_BOTH(PF_registercvar),		93},	// float(string cvarname, string defaultvalue)
	{"min",						PF_BOTH(PF_min),				94},	// float(float a, float b, ...)
	{"max",						PF_BOTH(PF_max),				95},	// float(float a, float b, ...)
	{"bound",					PF_BOTH(PF_bound),				96},	// float(float minimum, float val, float maximum)
	{"pow",						PF_BOTH(PF_pow),				97},	// float(float value, float exp)
	{"findfloat",				PF_BOTH(PF_findfloat),			98},	// entity(entity start, .__variant fld, __variant match)

	{"checkextension",			PF_BOTH(PF_checkextension),		99},	// float(string extname)
	{"builtin_find",			PF_BOTH(PF_builtin_find),		100},	// float(string name)
	{"checkbuiltin",			PF_BOTH(PF_checkbuiltin)},				// float(function builtinref)
	{"anglemod",				PF_BOTH(PF_anglemod),			102},	// float(float angle)

	{"fopen",					PF_BOTH(PF_fopen),				110},	// filestream(string filename, float mode)
	{"fclose",					PF_BOTH(PF_fclose),				111},	// void(filestream fhandle)
	{"fgets",					PF_BOTH(PF_fgets),				112},	// string(filestream fhandle)
	{"fputs",					PF_BOTH(PF_fputs),				113},	// void(filestream fhandle, string s, ...)

	{"strlen",					PF_BOTH(PF_strlen),				114},	// float(string s)
	{"strcat",					PF_BOTH(PF_strcat),				115},	// string(string s1, optional string s2, optional string s3, optional string s4, optional string s5, optional string s6, optional string s7, optional string s8)
	{"substring",				PF_BOTH(PF_substring),			116},	// string(string s, float start, float length)
	{"stov",					PF_BOTH(PF_stov),				117},	// vector(string s)
	{"strzone",					PF_BOTH(PF_strzone),			118},	// string(string s, ...)
	{"strunzone",				PF_BOTH(PF_strunzone),			119},	// void(string s)

	{"cvar_setf",				PF_BOTH(PF_cvar_setf),			176},	// void(string cvar, float val)

	{"str2chr",					PF_BOTH(PF_str2chr),			222},	// float(string str, float index)
	{"chr2str",					PF_BOTH(PF_chr2str),			223},	// string(float chr, ...)
	{"strconv",					PF_BOTH(PF_strconv),			224},	// string(float ccase, float redalpha, float redchars, string str, ...)
	{"strstrofs",				PF_BOTH(PF_strstrofs),			221},	// float(string s1, string sub, optional float startidx)
	{"strpad",					PF_BOTH(PF_strpad),				225},	// string(float pad, string str1, ...)
	{"bitshift",				PF_BOTH(PF_bitshift),			218},	// float(float bitmask, float shift)
	{"strncmp",					PF_BOTH(PF_strncmp),			228},	// float(string s1, string s2, optional float len, optional float s1ofs, optional float s2ofs)
	{"strcasecmp",				PF_BOTH(PF_strncasecmp),		229},	// float(string s1, string s2)
	{"strncasecmp",				PF_BOTH(PF_strncasecmp),		230},	// float(string s1, string s2, float len, optional float s1ofs, optional float s2ofs)

	{"clientstat",				PF_SSQC(PF_clientstat),			232},	// void(float num, float type, .__variant fld)
	{"globalstat",				PF_SSQC(PF_globalstat),			233},	// void(float num, float type, string name)
	{"isbackbuffered",			PF_SSQC(PF_isbackbuffered),		234},	// float(entity player)

	{"mod",						PF_BOTH(PF_mod),				245},	// float(float a, float n)
	{"strconv",					PF_BOTH(PF_strconv),			249},	// alternate FTE/QSS slot used by AD CSQC

	{"stoi",					PF_BOTH(PF_stoi),				259},	// int(string)
	{"itos",					PF_BOTH(PF_itos),				260},	// string(int)
	{"stoh",					PF_BOTH(PF_stoh),				261},	// int(string)
	{"htos",					PF_BOTH(PF_htos),				262},	// string(int)

	{"ftoi",					PF_BOTH(PF_ftoi)},						// int(float)
	{"itof",					PF_BOTH(PF_itof)},						// float(int)

	{"dprint",					PF_CSQC(PF_dprint),				277},	// Mjolnir CSQC debug-print alias

	{"checkcommand",			PF_BOTH(PF_checkcommand),		294},	// float(string name)

	{"drawline",				PF_CSQC(PF_cl_drawline),		315},	// void(float width, vector pos1, vector pos2, vector rgb, float alpha, optional float drawflag)
	{"iscachedpic",				PF_CSQC(PF_cl_iscachedpic),		316},	// float(string name)
	{"precache_pic",			PF_CSQC(PF_cl_precachepic),		317},	// string(string name, optional float flags)
	{"drawgetimagesize",		PF_CSQC(PF_cl_getimagesize),	318},	// #define draw_getimagesize drawgetimagesize\nvector(string picname)
	{"draw_getimagesize",		PF_CSQC(PF_cl_getimagesize),	318},	// vector(string picname)
	{"drawcharacter",			PF_CSQC(PF_cl_drawcharacter),	320},	// float(vector position, float character, vector size, vector rgb, float alpha, optional float drawflag)
	{"drawrawstring",			PF_CSQC(PF_cl_drawrawstring),	321},	// float(vector position, string text, vector size, vector rgb, float alpha, optional float drawflag)
	{"drawpic",					PF_CSQC(PF_cl_drawpic),			322},	// float(vector position, string pic, vector size, vector rgb, float alpha, optional float drawflag)
	{"drawfill",				PF_CSQC(PF_cl_drawfill),		323},	// float(vector position, vector size, vector rgb, float alpha, optional float drawflag)
	{"drawsetcliparea",			PF_CSQC(PF_cl_drawsetclip),		324},	// void(float x, float y, float width, float height)
	{"drawresetcliparea",		PF_CSQC(PF_cl_drawresetclip),	325},	// void(void)
	{"drawstring",				PF_CSQC(PF_cl_drawstring),		326},	// float(vector position, string text, vector size, vector rgb, float alpha, float drawflag)
	{"drawcolorcodedstring",	PF_CSQC(PF_cl_drawstring),		326},	// DP alias; best-effort with QSS/FTE drawstring signature
	{"drawcolorcodedstring2",	PF_CSQC(PF_cl_drawstring),		326},	// DP alias with explicit color
	{"stringwidth",				PF_CSQC(PF_cl_stringwidth),		327},	// float(string text, float usecolours, vector fontsize='8 8')
	{"drawsubpic",				PF_CSQC(PF_cl_drawsubpic),		328},	// void(vector pos, vector sz, string pic, vector srcpos, vector srcsz, vector rgb, float alpha, optional float drawflag)

	{"getstati",				PF_CSQC(PF_cl_getstat_int),		330},	// #define getstati_punf(stnum) (float)(__variant)getstati(stnum)\nint(float stnum)
	{"getstatf",				PF_CSQC(PF_cl_getstat_float),	331},	// #define getstatbits getstatf\nfloat(float stnum, optional float firstbit, optional float bitcount)
	{"getstats",				PF_CSQC(PF_cl_getstat_string),	332},	// string(float stnum)
	{"modelnameforindex",		PF_BOTH(PF_modelnameforindex),	334},	// string(float mdlindex)
	{"particleeffectnum",		PF_BOTH(PF_particleeffectnum),	335},	// float(string effectname)
	{"trailparticles",			PF_BOTH(PF_trailparticles),		336},	// void(float effectnum, entity ent, vector start, vector end)
	{"pointparticles",			PF_BOTH(PF_pointparticles),		337},	// void(float effectnum, vector origin, optional vector dir, optional float count)
	{"cprint",					PF_CSQC(PF_cl_cprint),			338},	// void(string s, ...)
	{"print",					PF_CSQC(PF_cl_print),			339},	// void(string s, ...)

	{"setcursormode",			PF_CSQC(PF_cl_setcursormode),	343},	// void(float usecursor, ...)
	{"runstandardplayerphysics",	PF_SSQC(PF_sv_pmove),			347},	// void(entity ent)
	{"getplayerkeyvalue",		PF_CSQC(PF_cl_playerkey_s),		348},	// string(float playernum, string keyname)
	{"getplayerkeyfloat",		PF_CSQC(PF_cl_playerkey_f)},			// float(float playernum, string keyname, optional float assumevalue)

	{"registercommand",			PF_CSQC(PF_cl_registercommand),	352},	// void(string cmdname)
	{"wasfreed",				PF_BOTH(PF_wasfreed),			353},	// float(entity ent)
	{"serverkey",				PF_BOTH(PF_serverkey_s),		354},	// string(string key)
	{"serverkeyfloat",			PF_BOTH(PF_serverkey_f)},				// float(string key, optional float assumevalue)
	{"loadfont",				PF_CSQC(PF_cl_loadfont),		357},	// float(string fontname, string fontmaps, string sizes, float slot, ...)
	{"sendevent",				PF_CSQC(PF_cl_sendevent),		359},	// void(string evname, string evargs, ...)
	{"readbyte",				PF_CSQC(PF_cl_readbyte),			360},	// float()
	{"readchar",				PF_CSQC(PF_cl_readchar),			361},	// float()
	{"readshort",				PF_CSQC(PF_cl_readshort),			362},	// float()
	{"readlong",				PF_CSQC(PF_cl_readlong),			363},	// float()
	{"readcoord",				PF_CSQC(PF_cl_readcoord),			364},	// float()
	{"readangle",				PF_CSQC(PF_cl_readangle),			365},	// float()
	{"readstring",				PF_CSQC(PF_cl_readstring),			366},	// string()
	{"readfloat",				PF_CSQC(PF_cl_readfloat),			367},	// float()
	{"readentitynum",			PF_CSQC(PF_cl_readentitynum),		368},	// float()
	{"copyentity",				PF_BOTH(PF_copyentity),			400},	// entity(entity from, optional entity to)
	{"setcolors",				PF_SSQC(PF_setcolors),		401},	// void(entity ent, float colors)
	{"findchain",				PF_BOTH(PF_findchain),			402},	// entity(.string fld, string match, optional .entity chainfield)
	{"findchainfloat",			PF_BOTH(PF_findchainfloat),		403},	// entity(.float fld, float match, optional .entity chainfield)
	{"te_particlerain",			PF_BOTH(PF_te_particlerain),	409},	// void(vector min, vector max, vector vel, float count, float color)
	{"te_particlesnow",			PF_BOTH(PF_te_particlesnow),	410},	// void(vector min, vector max, vector vel, float count, float color)

	{"ex_CheckPlayerEXFlags",	PF_SSQC(PF_CheckPlayerEXFlags),	430},	// rerelease sparse slot used by mod progs
	{"vectorvectors",			PF_BOTH(PF_vectorvectors),		432},	// void(vector dir)
	{"getsurfacenumpoints",		PF_BOTH(PF_getsurfacenumpoints),	434},	// float(entity e, float s)
	{"getsurfacepoint",			PF_BOTH(PF_getsurfacepoint),	435},	// vector(entity e, float s, float n)
	{"getsurfacenormal",		PF_BOTH(PF_getsurfacenormal),	436},	// vector(entity e, float s)
	{"getsurfacetexture",		PF_BOTH(PF_getsurfacetexture),	437},	// string(entity e, float s)
	{"getsurfacenearpoint",		PF_BOTH(PF_getsurfacenearpoint),	438},	// float(entity e, vector p)
	{"getsurfaceclippedpoint",	PF_BOTH(PF_getsurfaceclippedpoint),	439},	// vector(entity e, float s, vector p)

	{"clientcommand",			PF_SSQC(PF_clientcommand),		440},	// void(entity e, string s)
	{"tokenize",				PF_BOTH(PF_Tokenize),			441},	// float(string s)
	{"argv",					PF_BOTH(PF_ArgV),				442},	// string(float n)
	{"argc",					PF_BOTH(PF_ArgC)},						// float()
	{"search_begin",			PF_BOTH(PF_search_begin),		444},	// searchhandle(string pattern, float flags, float quiet, optional string filterpackage)
	{"search_end",				PF_BOTH(PF_search_end),			445},	// void(searchhandle handle)
	{"search_getsize",			PF_BOTH(PF_search_getsize),		446},	// float(searchhandle handle)
	{"search_getfilename",		PF_BOTH(PF_search_getfilename),	447},	// string(searchhandle handle, float num)
	{"cvar_string",				PF_BOTH(PF_cvar_string),		448},	// string(string cvarname)
	{"findflags",				PF_BOTH(PF_findflags),			449},	// entity(entity start, .float fld, float match)
	{"findchainflags",			PF_BOTH(PF_findchainflags),		450},	// entity(.float fld, float match, optional .entity chainfield)
	{"edict_num",				PF_BOTH(PF_edict_for_num),		459},	// entity(float entnum)
	{"buf_create",				PF_BOTH(PF_buf_create),			460},	// strbuf()
	{"buf_del",					PF_BOTH(PF_buf_del),			461},	// void(strbuf bufhandle)
	{"buf_getsize",				PF_BOTH(PF_buf_getsize),		462},	// float(strbuf bufhandle)
	{"buf_copy",				PF_BOTH(PF_buf_copy),			463},	// void(strbuf from, strbuf to)
	{"buf_sort",				PF_BOTH(PF_buf_sort),			464},	// void(strbuf bufhandle, float sortprefixlen, float backward)
	{"buf_implode",				PF_BOTH(PF_buf_implode),		465},	// string(strbuf bufhandle, string glue)
	{"bufstr_get",				PF_BOTH(PF_bufstr_get),			466},	// string(strbuf bufhandle, float string_index)
	{"bufstr_set",				PF_BOTH(PF_bufstr_set),			467},	// void(strbuf bufhandle, float string_index, string str)
	{"bufstr_add",				PF_BOTH(PF_bufstr_add),			468},	// float(strbuf bufhandle, string str, float order)
	{"bufstr_free",				PF_BOTH(PF_bufstr_free),		469},	// void(strbuf bufhandle, float string_index)

	{"asin",					PF_BOTH(PF_asin),				471},	// float(float s)
	{"acos",					PF_BOTH(PF_acos),				472},	// float(float c)
	{"atan",					PF_BOTH(PF_atan),				473},	// float(float t)
	{"atan2",					PF_BOTH(PF_atan2),				474},	// float(float c, float s)
	{"tan",						PF_BOTH(PF_tan),				475},	// float(float a)
	{"strlennocol",				PF_BOTH(PF_strlennocol),		476},	// float(string s)
	{"strdecolorize",			PF_BOTH(PF_strdecolorize),		477},	// string(string s)
	{"strftime",				PF_BOTH(PF_strftime),			478},	// string(float uselocaltime, string format, ...)
	{"tokenizebyseparator",		PF_BOTH(PF_tokenizebyseparator),	479},	// float(string s, string separator1, ...)
	{"strtolower",				PF_BOTH(PF_strtolower),			480},	// string(string s)
	{"strtoupper",				PF_BOTH(PF_strtoupper),			481},	// string(string s)
	{"cvar_defstring",			PF_BOTH(PF_cvar_defstring),		482},	// string(string cvarname)
	{"strreplace",				PF_BOTH(PF_strreplace),			484},	// string(string search, string replace, string subject)
	{"strireplace",				PF_BOTH(PF_strireplace),		485},	// string(string search, string replace, string subject)
	{"getsurfacepointattribute",	PF_BOTH(PF_getsurfacepointattribute),	486},	// vector(entity e, float s, float n, float a)
	{"crc16",					PF_BOTH(PF_crc16),				494},	// float(float caseinsensitive, string s, ...)
	{"cvar_type",				PF_BOTH(PF_cvar_type),			495},	// float(string cvarname)
	{"numentityfields",			PF_BOTH(PF_numentityfields),	496},	// float()
	{"findentityfield",			PF_BOTH(PF_findentityfield)},			// float(string fieldname)
	{"entityfieldref",			PF_BOTH(PF_entityfieldref)},			// .__variant(float fieldnum)
	{"entityfieldname",			PF_BOTH(PF_entityfieldname),	497},	// string(float fieldnum)
	{"entityfieldtype",			PF_BOTH(PF_entityfieldtype),	498},	// float(float fieldnum)
	{"getentityfieldstring",	PF_BOTH(PF_getentityfieldstring),	499},	// string(float fieldnum, entity ent)
	{"putentityfieldstring",	PF_BOTH(PF_putentityfieldstring),	500},	// float(float fieldnum, entity ent, string s)
	{"whichpack",				PF_BOTH(PF_whichpack),			503},	// string(string filename)
	{"uri_escape",				PF_BOTH(PF_uri_escape),			510},	// string(string in)
	{"uri_unescape",			PF_BOTH(PF_uri_unescape),		511},	// string(string in)
	{"num_for_edict",			PF_BOTH(PF_num_for_edict),		512},	// float(entity ent)

	{"tokenize_console",		PF_BOTH(PF_tokenize_console),	514},	// float(string str)
	{"argv_start_index",		PF_BOTH(PF_argv_start_index),	515},	// float(float idx)
	{"argv_end_index",			PF_BOTH(PF_argv_end_index),		516},	// float(float idx)
	{"buf_cvarlist",			PF_BOTH(PF_buf_cvarlist),		517},	// void(strbuf strbuf, string pattern, string antipattern)
	{"cvar_description",		PF_BOTH(PF_cvar_description),	518},	// string(string cvarname)
	{"gettime",					PF_BOTH(PF_gettime),			519},	// float(optional float timer)
	{"log",						PF_BOTH(PF_Logarithm),			532},	// float(float value, optional float base)
	{"buf_loadfile",			PF_BOTH(PF_buf_loadfile),		535},	// float(string filename, strbuf bufhandle)
	{"buf_writefile",			PF_BOTH(PF_buf_writefile),		536},	// float(filestream filehandle, strbuf bufhandle, optional float startpos, optional float numstrings)
	{"callfunction",			PF_BOTH(PF_callfunction),		605},	// void(..., string funcname)
	{"isfunction",				PF_BOTH(PF_isfunction),			607},	// float(string s)
	{"getresolution",			PF_CSQC(PF_cl_getresolution),	608},	// vector(optional float virtual)
	{"parseentitydata",			PF_BOTH(PF_parseentitydata),		613},	// float(entity e, string s, optional float offset)

	{"sprintf",					PF_BOTH(PF_sprintf),			627},	// string(string fmt, ...)
	{"getsurfacenumtriangles",	PF_BOTH(PF_getsurfacenumtriangles),	628},	// float(entity e, float s)
	{"getsurfacetriangle",		PF_BOTH(PF_getsurfacetriangle),	629},	// vector(entity e, float s, float n)
};
int pr_numbuiltindefs = countof (pr_builtindefs);

COMPILE_TIME_ASSERT (builtin_buffer_size, countof (pr_builtindefs) + 1 <= MAX_BUILTINS);
