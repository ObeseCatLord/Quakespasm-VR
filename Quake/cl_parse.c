/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
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
// cl_parse.c  -- parse a message received from the server

#include "quakedef.h"
#include "bgmusic.h"
#include "vr.h"

const char *svc_strings[] =
{
	"svc_bad",
	"svc_nop",
	"svc_disconnect",
	"svc_updatestat",
	"svc_version",		// [long] server version
	"svc_setview",		// [short] entity number
	"svc_sound",			// <see code>
	"svc_time",			// [float] server time
	"svc_print",			// [string] null terminated string
	"svc_stufftext",		// [string] stuffed into client's console buffer
						// the string should be \n terminated
	"svc_setangle",		// [vec3] set the view angle to this absolute value

	"svc_serverinfo",		// [long] version
						// [string] signon string
						// [string]..[0]model cache [string]...[0]sounds cache
						// [string]..[0]item cache
	"svc_lightstyle",		// [byte] [string]
	"svc_updatename",		// [byte] [string]
	"svc_updatefrags",	// [byte] [short]
	"svc_clientdata",		// <shortbits + data>
	"svc_stopsound",		// <see code>
	"svc_updatecolors",	// [byte] [byte]
	"svc_particle",		// [vec3] <variable>
	"svc_damage",			// [byte] impact [byte] blood [vec3] from

	"svc_spawnstatic",
	"OBSOLETE svc_spawnbinary",
	"svc_spawnbaseline",

	"svc_temp_entity",		// <variable>
	"svc_setpause",
	"svc_signonnum",
	"svc_centerprint",
	"svc_killedmonster",
	"svc_foundsecret",
	"svc_spawnstaticsound",
	"svc_intermission",
	"svc_finale",			// [string] music [string] text
	"svc_cdtrack",			// [byte] track [byte] looptrack
	"svc_sellscreen",
	"svc_cutscene",
//johnfitz -- new server messages
	"",	// 35
	"",	// 36
	"svc_skybox", // 37					// [string] skyname
	"svc_botchat", // 38 (2021 RE-RELEASE)
	"", // 39
	"svc_bf", // 40						// no data
	"svc_fog", // 41					// [byte] density [byte] red [byte] green [byte] blue [float] time
	"svc_spawnbaseline2", //42			// support for large modelindex, large framenum, alpha, using flags
	"svc_spawnstatic2", // 43			// support for large modelindex, large framenum, alpha, using flags
	"svc_spawnstaticsound2", //	44		// [coord3] [short] samp [byte] vol [byte] aten
//johnfitz

// 2021 RE-RELEASE:
	"svc_setviews", // 45
	"svc_updateping", // 46
	"svc_updatesocial", // 47
	"svc_updateplinfo", // 48
	"svc_rawprint", // 49
	"svc_servervars", // 50
	"svc_seq", // 51
	"svc_achievement", // 52
	"svc_chat", // 53
	"svc_levelcompleted", // 54
	"svc_backtolobby", // 55
	"svc_localsound", // 56
	"svc_moveack", // 57
	"svc_snapshot" // 58
};

static const char *CL_LerpDebugModelName (qmodel_t *model)
{
	return (model && model->name[0]) ? model->name : "<none>";
}

static qmodel_t *CL_LerpDebugModelForIndex (int modelindex)
{
	if (modelindex > 0 && modelindex < MAX_MODELS)
		return cl.model_precache[modelindex];
	return NULL;
}

static qboolean CL_LerpDebugHasFilter (void)
{
	return cl_lerpdebug_models.string && cl_lerpdebug_models.string[0];
}

static qboolean CL_LerpDebugNameMatchesFilter (const char *modelname)
{
	const char *filter, *end;
	char token[128];
	size_t len;

	if (!CL_LerpDebugHasFilter ())
		return true;
	if (!modelname || !modelname[0])
		return false;

	filter = cl_lerpdebug_models.string;
	while (*filter)
	{
		while (*filter && (*filter == ',' || (unsigned char)*filter <= ' '))
			filter++;
		if (!*filter)
			break;
		end = filter;
		while (*end && *end != ',' && *end > ' ')
			end++;
		len = end - filter;
		if (len >= sizeof(token))
			len = sizeof(token) - 1;
		if (len > 0)
		{
			memcpy (token, filter, len);
			token[len] = 0;
			if (q_strcasestr (modelname, token))
				return true;
		}
		filter = end;
	}
	return false;
}

static qboolean CL_LerpDebugModelsMatchFilter (qmodel_t *oldmodel, qmodel_t *newmodel)
{
	return CL_LerpDebugNameMatchesFilter (CL_LerpDebugModelName(oldmodel))
		|| CL_LerpDebugNameMatchesFilter (CL_LerpDebugModelName(newmodel));
}

static qboolean CL_LerpDebugStatesMatchFilter (const entity_state_t *olds, const entity_state_t *news)
{
	qmodel_t *oldmodel = olds ? CL_LerpDebugModelForIndex (olds->modelindex) : NULL;
	qmodel_t *newmodel = news ? CL_LerpDebugModelForIndex (news->modelindex) : NULL;

	return CL_LerpDebugModelsMatchFilter (oldmodel, newmodel);
}

static void CL_LerpDebugResetAnim (int entnum, const char *path,
	const char *reason, float gap, qmodel_t *oldmodel, qmodel_t *newmodel,
	int oldframe, int newframe)
{
	if (!cl_lerpdebug.value)
		return;
	if (!CL_LerpDebugModelsMatchFilter (oldmodel, newmodel))
		return;
	if (!CL_LerpDebugHasFilter () && cl_lerpdebug.value < 2
		&& !q_strcasecmp (reason, "model change") && !oldmodel)
		return;

	Con_Printf ("cl_lerpdebug: resetanim path=%s ent=%d reason=%s gap=%.3f "
		"old=%s:%d new=%s:%d mtime=%.3f\n",
		path, entnum, reason, gap, CL_LerpDebugModelName(oldmodel), oldframe,
		CL_LerpDebugModelName(newmodel), newframe, cl.mtime[0]);
}

static void CL_LerpDebugDeltaReset (int entnum, unsigned int bits,
	const entity_state_t *olds, const entity_state_t *news)
{
	qmodel_t *oldmodel, *newmodel;

	if (!cl_lerpdebug.value || !(bits & UF_RESET))
		return;
	if (!CL_LerpDebugHasFilter () && cl_lerpdebug.value < 2)
		return;
	if (!CL_LerpDebugStatesMatchFilter (olds, news))
		return;

	oldmodel = olds ? CL_LerpDebugModelForIndex (olds->modelindex) : NULL;
	newmodel = news ? CL_LerpDebugModelForIndex (news->modelindex) : NULL;
	Con_Printf ("cl_lerpdebug: delta reset ent=%d bits=0x%x old=%s:%d new=%s:%d mtime=%.3f\n",
		entnum, bits, CL_LerpDebugModelName(oldmodel), olds ? olds->frame : 0,
		CL_LerpDebugModelName(newmodel), news ? news->frame : 0, cl.mtime[0]);
}

static void CL_LerpDebugEntityEvent (const char *event, int entnum, entity_t *ent)
{
	if (!cl_lerpdebug.value)
		return;
	if (ent && !CL_LerpDebugModelsMatchFilter (ent->model, CL_LerpDebugModelForIndex(ent->netstate.modelindex)))
		return;
	if (!CL_LerpDebugHasFilter () && cl_lerpdebug.value < 2 && entnum > 0)
		return;

	Con_Printf ("cl_lerpdebug: entity %s ent=%d model=%s:%d net=%s:%d mtime=%.3f\n",
		event, entnum,
		ent ? CL_LerpDebugModelName(ent->model) : "<none>", ent ? ent->frame : 0,
		ent ? CL_LerpDebugModelName(CL_LerpDebugModelForIndex(ent->netstate.modelindex)) : "<none>",
		ent ? ent->netstate.frame : 0, cl.mtime[0]);
}
#define	NUM_SVC_STRINGS	(sizeof(svc_strings) / sizeof(svc_strings[0]))

qboolean warn_about_nehahra_protocol; //johnfitz

extern vec3_t	v_punchangles[2]; //johnfitz

//=============================================================================

/*
===============
CL_EntityNum

This error checks and tracks the total number of entities
===============
*/
entity_t	*CL_EntityNum (int num)
{
	//johnfitz -- check minimum number too
	if (num < 0)
		Host_Error ("CL_EntityNum: %i is an invalid number",num);
	//john

	if (num >= cl.num_entities)
	{
		if (num >= cl.max_edicts) //johnfitz -- no more MAX_EDICTS
			Host_Error ("CL_EntityNum: %i is an invalid number",num);
		while (cl.num_entities<=num)
		{
			cl.entities[cl.num_entities].colormap = vid.colormap;
			cl.entities[cl.num_entities].lerpflags |= LERP_RESETMOVE|LERP_RESETANIM; //johnfitz
			cl.entities[cl.num_entities].baseline = nullentitystate;
			cl.entities[cl.num_entities].netstate = nullentitystate;
			cl.num_entities++;
		}
	}

	return &cl.entities[num];
}


/*
==================
CL_ParseStartSoundPacket
==================
*/
void CL_ParseStartSoundPacket(void)
{
	vec3_t	pos;
	int	channel, ent;
	int	sound_num;
	int	volume;
	int	field_mask;
	float	attenuation;
	int	i;

	field_mask = MSG_ReadByte();

	if (field_mask & SND_VOLUME)
		volume = MSG_ReadByte ();
	else
		volume = DEFAULT_SOUND_PACKET_VOLUME;

	if (field_mask & SND_ATTENUATION)
		attenuation = MSG_ReadByte () / 64.0;
	else
		attenuation = DEFAULT_SOUND_PACKET_ATTENUATION;

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (field_mask & SND_LARGEENTITY)
	{
		ent = (unsigned short) MSG_ReadShort ();
		channel = MSG_ReadByte ();
	}
	else
	{
		channel = (unsigned short) MSG_ReadShort ();
		ent = channel >> 3;
		channel &= 7;
	}

	if (field_mask & SND_LARGESOUND)
		sound_num = (unsigned short) MSG_ReadShort ();
	else
		sound_num = MSG_ReadByte ();
	//johnfitz

	//johnfitz -- check soundnum
	if (sound_num >= MAX_SOUNDS)
		Host_Error ("CL_ParseStartSoundPacket: %i > MAX_SOUNDS", sound_num);
	//johnfitz

	if (ent > cl.max_edicts) //johnfitz -- no more MAX_EDICTS
		Host_Error ("CL_ParseStartSoundPacket: ent = %i", ent);

	for (i = 0; i < 3; i++)
		pos[i] = MSG_ReadCoord (cl.protocolflags);

	if (vr_enabled.value && vr_haptic.value && ent == cl.viewentity && channel == 1)
		VR_TriggerHaptic (1, 0.0015f);

	S_StartSound (ent, channel, cl.sound_precache[sound_num], pos, volume/255.0, attenuation);
}

/*
==================
CL_ParseLocalSound - for 2021 rerelease
==================
*/
void CL_ParseLocalSound(void)
{
	int field_mask, sound_num;

	field_mask = MSG_ReadByte();
	sound_num = (field_mask&SND_LARGESOUND) ? MSG_ReadShort() : MSG_ReadByte();
	if (sound_num >= MAX_SOUNDS)
		Host_Error ("CL_ParseLocalSound: %i > MAX_SOUNDS", sound_num);

	S_LocalSound (cl.sound_precache[sound_num]->name);
}

static void CL_ParseDPPrecache (void)
{
	unsigned short code = MSG_ReadShort ();
	unsigned int index = code & 0x3fff;
	const char *name = MSG_ReadString ();

	switch ((code >> 14) & 3)
	{
	case 1:
		if (index < MAX_PARTICLETYPES)
		{
#ifdef PSET_SCRIPT
			if (*name)
			{
				cl.particle_precache[index].name = strcpy (Hunk_AllocName (strlen(name) + 1, "particles"), name);
				cl.particle_precache[index].index = PScript_FindParticleType (cl.particle_precache[index].name);
			}
			else
			{
				cl.particle_precache[index].name = NULL;
				cl.particle_precache[index].index = P_INVALID;
			}
#else
			q_strlcpy (cl.particle_precache[index], name, sizeof(cl.particle_precache[index]));
#endif
		}
		break;
	default:
		Con_DPrintf ("CL_ParseDPPrecache: unsupported type %u for %s\n",
			(unsigned int)((code >> 14) & 3), name);
		break;
	}
}

#ifdef PSET_SCRIPT
int CL_GenerateRandomParticlePrecache(const char *pname);

static void CL_ForceProtocolParticles(void)
{
	cl.protocol_particles = true;
	PScript_FindParticleType ("effectinfo.");
	COM_Effectinfo_Enumerate (CL_GenerateRandomParticlePrecache);
	Con_DPrintf2 ("Received protocol particles before explicit particle extension setup\n");
}

void CL_RegisterParticles(void)
{
	int i;

	PScript_FindParticleType ("effectinfo.");
	for (i = 0; i < MAX_PARTICLETYPES; i++)
	{
		if (cl.particle_precache[i].name)
			cl.particle_precache[i].index = PScript_FindParticleType (cl.particle_precache[i].name);
		else
			cl.particle_precache[i].index = P_INVALID;

		if (cl.local_particle_precache[i].name)
			cl.local_particle_precache[i].index = PScript_FindParticleType (cl.local_particle_precache[i].name);
		else
			cl.local_particle_precache[i].index = P_INVALID;
	}
	Mod_ForEachModel (PScript_UpdateModelEffects);
}
#endif

static void CL_ParseDPPointParticles (qboolean compact)
{
	int effectnum = MSG_ReadShort ();
	int count = 1;
	vec3_t org, dir;

	org[0] = MSG_ReadCoord (cl.protocolflags);
	org[1] = MSG_ReadCoord (cl.protocolflags);
	org[2] = MSG_ReadCoord (cl.protocolflags);
	if (compact)
	{
		VectorCopy (vec3_origin, dir);
	}
	else
	{
		dir[0] = MSG_ReadCoord (cl.protocolflags);
		dir[1] = MSG_ReadCoord (cl.protocolflags);
		dir[2] = MSG_ReadCoord (cl.protocolflags);
		count = MSG_ReadShort ();
	}

#ifdef PSET_SCRIPT
	if (!cl.protocol_particles)
		CL_ForceProtocolParticles ();
	if (effectnum > 0 && effectnum < MAX_PARTICLETYPES && cl.particle_precache[effectnum].name)
	{
		PScript_RunParticleEffectState (org, dir, count, cl.particle_precache[effectnum].index, NULL);
		return;
	}
#endif
	CL_RunNamedParticleEffect (effectnum, org, dir, count);
}

static void CL_ParseDPTrailParticles (void)
{
	int entnum = MSG_ReadShort ();
	int effectnum = MSG_ReadShort ();
#ifdef PSET_SCRIPT
	entity_t *ent;
#endif
	vec3_t start, end, dir, org, step;
	float len;
	int samples, i;

	start[0] = MSG_ReadCoord (cl.protocolflags);
	start[1] = MSG_ReadCoord (cl.protocolflags);
	start[2] = MSG_ReadCoord (cl.protocolflags);
	end[0] = MSG_ReadCoord (cl.protocolflags);
	end[1] = MSG_ReadCoord (cl.protocolflags);
	end[2] = MSG_ReadCoord (cl.protocolflags);

#ifdef PSET_SCRIPT
	if (!cl.protocol_particles)
		CL_ForceProtocolParticles ();
	if (effectnum > 0 && effectnum < MAX_PARTICLETYPES && cl.particle_precache[effectnum].name)
	{
		ent = CL_EntityNum (entnum);
		PScript_ParticleTrail (start, end, cl.particle_precache[effectnum].index, 1, entnum, NULL, &ent->trailstate);
		return;
	}
#else
	(void)entnum;
#endif
	VectorSubtract (end, start, dir);
	len = VectorNormalize (dir);
	if (len <= 0)
		return;
	samples = CLAMP (1, (int)(len / 32.0f) + 1, 16);
	VectorScale (dir, len / samples, step);
	VectorCopy (start, org);
	for (i = 0; i < samples; i++)
	{
		CL_RunNamedParticleEffect (effectnum, org, dir, 1);
		VectorAdd (org, step, org);
	}
}

/*
==================
CL_KeepaliveMessage

When the client is taking a long time to load stuff, send keepalive messages
so the server doesn't disconnect.
==================
*/
static byte	net_olddata[NET_MAXMESSAGE];
void CL_KeepaliveMessage (void)
{
	float	time;
	static float lastmsg;
	int		ret;
	sizebuf_t	old;
	byte	*olddata;

	if (sv.active)
		return;		// no need if server is local
	if (cls.demoplayback)
		return;

// read messages from server, should just be nops
	olddata = net_olddata;
	old = net_message;
	memcpy (olddata, net_message.data, net_message.cursize);

	do
	{
		ret = CL_GetMessage ();
		switch (ret)
		{
		default:
			Host_Error ("CL_KeepaliveMessage: CL_GetMessage failed");
		case 0:
			break;	// nothing waiting
		case 1:
			Host_Error ("CL_KeepaliveMessage: received a message");
			break;
		case 2:
			if (MSG_ReadByte() != svc_nop)
				Host_Error ("CL_KeepaliveMessage: datagram wasn't a nop");
			break;
		}
	} while (ret);

	net_message = old;
	memcpy (net_message.data, olddata, net_message.cursize);

// check time
	time = Sys_DoubleTime ();
	if (time - lastmsg < 5)
		return;
	lastmsg = time;

// write out a nop
	Con_Printf ("--> client to server keepalive\n");

	MSG_WriteByte (&cls.message, clc_nop);
	NET_SendMessage (cls.netcon, &cls.message);
	SZ_Clear (&cls.message);
}

/*
==================
CL_ParseServerInfo
==================
*/
void CL_ParseServerInfo (void)
{
	const char	*str;
	int		i;
	int		nummodels, numsounds;
	char	model_precache[MAX_MODELS][MAX_QPATH];
	char	sound_precache[MAX_SOUNDS][MAX_QPATH];

	Con_DPrintf ("Serverinfo packet received.\n");

// ericw -- bring up loading plaque for map changes within a demo.
//          it will be hidden in CL_SignonReply.
	if (cls.demoplayback)
		SCR_BeginLoadingPlaque();

//
// wipe the client_state_t struct
//
	CL_ClearState ();

// parse protocol version number
	i = MSG_ReadLong ();
	//johnfitz -- support multiple protocols
	if (i != PROTOCOL_NETQUAKE && i != PROTOCOL_FITZQUAKE && i != PROTOCOL_RMQ) {
		Con_Printf ("\n"); //because there's no newline after serverinfo print
		Host_Error ("Server returned version %i, not %i or %i or %i", i, PROTOCOL_NETQUAKE, PROTOCOL_FITZQUAKE, PROTOCOL_RMQ);
	}
	cl.protocol = i;
	//johnfitz

	if (cl.protocol == PROTOCOL_RMQ)
	{
		const unsigned int supportedflags = (PRFL_SHORTANGLE | PRFL_FLOATANGLE | PRFL_24BITCOORD | PRFL_FLOATCOORD | PRFL_EDICTSCALE | PRFL_INT32COORD);

		// mh - read protocol flags from server so that we know what protocol features to expect
		cl.protocolflags = (unsigned int) MSG_ReadLong ();

		if (0 != (cl.protocolflags & (~supportedflags)))
		{
			Con_Warning("PROTOCOL_RMQ protocolflags %i contains unsupported flags\n", cl.protocolflags);
		}
	}
	else cl.protocolflags = 0;

	cl.protocol_pext1 = 0;
	cl.protocol_pext2 = PEXT2_REPLACEMENTDELTAS | PEXT2_PREDINFO | PEXT2_NEWSIZEENCODING;
	cl.requestresend = true;
	cl.ackframes_count = 1;
	cl.ackframes[0] = -1;
	cl.ackframes_history_count = 0;

// parse maxclients
	cl.maxclients = MSG_ReadByte ();
	if (cl.maxclients < 1 || cl.maxclients > MAX_SCOREBOARD)
	{
		Host_Error ("Bad maxclients (%u) from server", cl.maxclients);
	}
	cl.scores = (scoreboard_t *) Hunk_AllocName (cl.maxclients*sizeof(*cl.scores), "scores");

// parse gametype
	cl.gametype = MSG_ReadByte ();

// parse signon message
	str = MSG_ReadString ();
	q_strlcpy (cl.levelname, str, sizeof(cl.levelname));

// seperate the printfs so the server message can have a color
	Con_Printf ("\n%s\n", Con_Quakebar(40)); //johnfitz
	Con_Printf ("%c%s\n", 2, str);

//johnfitz -- tell user which protocol this is
	Con_Printf ("Using protocol %i\n", i);

// first we go through and touch all of the precache data that still
// happens to be in the cache, so precaching something else doesn't
// needlessly purge it

// precache models
	memset (cl.model_precache, 0, sizeof(cl.model_precache));
	for (nummodels = 1 ; ; nummodels++)
	{
		str = MSG_ReadString ();
		if (!str[0])
			break;
		if (nummodels == MAX_MODELS)
		{
			Host_Error ("Server sent too many model precaches");
		}
		q_strlcpy (model_precache[nummodels], str, MAX_QPATH);
		Mod_TouchModel (str);
	}

	//johnfitz -- check for excessive models
	if (nummodels >= 256)
		Con_DWarning ("%i models exceeds standard limit of 256 (max = %d).\n", nummodels, MAX_MODELS);
	//johnfitz

// precache sounds
	memset (cl.sound_precache, 0, sizeof(cl.sound_precache));
	for (numsounds = 1 ; ; numsounds++)
	{
		str = MSG_ReadString ();
		if (!str[0])
			break;
		if (numsounds == MAX_SOUNDS)
		{
			Host_Error ("Server sent too many sound precaches");
		}
		q_strlcpy (sound_precache[numsounds], str, MAX_QPATH);
		S_TouchSound (str);
	}

	//johnfitz -- check for excessive sounds
	if (numsounds >= 256)
		Con_DWarning ("%i sounds exceeds standard limit of 256 (max = %d).\n", numsounds, MAX_SOUNDS);
	//johnfitz

//
// now we try to load everything else until a cache allocation fails
//

	// copy the naked name of the map file to the cl structure -- O.S
	COM_StripExtension (COM_SkipPath(model_precache[1]), cl.mapname, sizeof(cl.mapname));

	for (i = 1; i < nummodels; i++)
	{
		cl.model_precache[i] = Mod_ForName (model_precache[i], false);
		if (cl.model_precache[i] == NULL)
		{
			Host_Error ("Model %s not found", model_precache[i]);
		}
		CL_KeepaliveMessage ();
	}

	S_BeginPrecaching ();
	for (i = 1; i < numsounds; i++)
	{
		cl.sound_precache[i] = S_PrecacheSound (sound_precache[i]);
		CL_KeepaliveMessage ();
	}
	S_EndPrecaching ();

// local state
	cl.entities[0].model = cl.worldmodel = cl.model_precache[1];

	R_NewMap ();

	//johnfitz -- clear out string; we don't consider identical
	//messages to be duplicates if the map has changed in between
	con_lastcenterstring[0] = 0;
	//johnfitz

	Hunk_Check ();		// make sure nothing is hurt

	noclip_anglehack = false;		// noclip is turned off at start

	warn_about_nehahra_protocol = true; //johnfitz -- warn about nehahra protocol hack once per server connection

//johnfitz -- reset developer stats
	memset(&dev_stats, 0, sizeof(dev_stats));
	memset(&dev_peakstats, 0, sizeof(dev_peakstats));
	memset(&dev_overflows, 0, sizeof(dev_overflows));
}

static void CSQC_ClearCsEdictForSSQC (size_t entnum)
{
	edict_t *ed;

	if (entnum >= cl.ssqc_to_csqc_max)
		return;

	ed = cl.ssqc_to_csqc[entnum];
	if (!ed)
		return;

	cl.ssqc_to_csqc[entnum] = NULL;
	pr_global_struct->self = EDICT_TO_PROG(ed);
	if (qcvm->extfuncs.CSQC_Ent_Remove)
		PR_ExecuteProgram(qcvm->extfuncs.CSQC_Ent_Remove);
	else
		ED_Free(ed);
}

static void CSQC_UpdateCsEdictForSSQC (size_t entnum)
{
	edict_t *ed;
	eval_t *ev;
	qboolean isnew;

	if (entnum >= cl.ssqc_to_csqc_max)
	{
		size_t nc;
		void *nptr;

		if (entnum >= MAX_EDICTS)
			Host_Error ("CSQC_UpdateCsEdictForSSQC: entnum %zu >= MAX_EDICTS", entnum);
		nc = q_min((size_t)MAX_EDICTS, entnum + 64);
		nptr = realloc(cl.ssqc_to_csqc, nc * sizeof(*cl.ssqc_to_csqc));
		if (!nptr)
			Sys_Error ("CSQC_UpdateCsEdictForSSQC: realloc failed");
		cl.ssqc_to_csqc = nptr;
		memset(cl.ssqc_to_csqc + cl.ssqc_to_csqc_max, 0,
			(nc - cl.ssqc_to_csqc_max) * sizeof(*cl.ssqc_to_csqc));
		cl.ssqc_to_csqc_max = nc;
	}

	ed = cl.ssqc_to_csqc[entnum];
	if (!ed)
	{
		ed = cl.ssqc_to_csqc[entnum] = ED_Alloc();
		ev = GetEdictFieldValue(ed, qcvm->extfields.entnum);
		if (ev)
			ev->_float = entnum;
		isnew = true;
	}
	else
		isnew = false;

	G_FLOAT(OFS_PARM0) = isnew;
	pr_global_struct->self = EDICT_TO_PROG(ed);
	PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_Ent_Update);
}

static void CLFTE_ParseCSQCEntitiesUpdate (void)
{
	unsigned int entnum;
	qboolean removeflag;

	if (!qcvm->extfuncs.CSQC_Ent_Update)
		Host_Error ("Received svc_csqcentities but CSQC_Ent_Update is missing");

	for (;;)
	{
		entnum = (unsigned short)MSG_ReadShort();
		removeflag = !!(entnum & 0x8000);
		if (entnum & 0x4000)
			entnum = (entnum & 0x3fff) | (MSG_ReadByte() << 14);
		else
			entnum &= ~0x8000;

		if ((!entnum && !removeflag) || msg_badread)
			break;

		if (removeflag)
		{
			if (cl_shownet.value >= 3)
				Con_Printf ("%3i:     CSQC remove %u\n", msg_readcount, entnum);
			CSQC_ClearCsEdictForSSQC(entnum);
		}
		else
		{
			if (cl_shownet.value >= 3)
				Con_Printf ("%3i:     CSQC update %u\n", msg_readcount, entnum);
			CSQC_UpdateCsEdictForSSQC(entnum);
		}
	}
}

/*
==================
CL_ParseUpdate

Parse an entity update message from the server
If an entities model or origin changes from frame to frame, it must be
relinked.  Other attributes can change without relinking.
==================
*/
void CL_ParseUpdate (int bits)
{
	int		i;
	qmodel_t	*model;
	int		modnum;
	qboolean	forcelink;
	entity_t	*ent;
	int		num;
	int		skin;
	float		oldmsgtime;
	qmodel_t	*oldmodel;
	int		oldframe;

	if (cls.signon == SIGNONS - 1)
	{	// first update is the final signon stage
		cls.signon = SIGNONS;
		CL_SignonReply ();
	}

	if (bits & U_MOREBITS)
	{
		i = MSG_ReadByte ();
		bits |= (i<<8);
	}

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (cl.protocol == PROTOCOL_FITZQUAKE || cl.protocol == PROTOCOL_RMQ)
	{
		if (bits & U_EXTEND1)
			bits |= MSG_ReadByte() << 16;
		if (bits & U_EXTEND2)
			bits |= MSG_ReadByte() << 24;
	}
	//johnfitz

	if (bits & U_LONGENTITY)
		num = MSG_ReadShort ();
	else
		num = MSG_ReadByte ();

	ent = CL_EntityNum (num);
	oldmsgtime = ent->msgtime;
	oldmodel = ent->model;
	oldframe = ent->frame;

	if (ent->msgtime != cl.mtime[1])
		forcelink = true;	// no previous frame to lerp from
	else
		forcelink = false;

	//johnfitz -- lerping
	if (ent->msgtime + 0.2 < cl.mtime[0]) //more than 0.2 seconds since the last message (most entities think every 0.1 sec)
	{
		ent->lerpflags |= LERP_RESETANIM; //if we missed a think, we'd be lerping from the wrong frame
		CL_LerpDebugResetAnim (num, "legacy", "message gap",
			cl.mtime[0] - oldmsgtime, oldmodel, oldmodel, oldframe, oldframe);
	}
	//johnfitz

	ent->msgtime = cl.mtime[0];

	if (bits & U_MODEL)
	{
		modnum = MSG_ReadByte ();
		if (modnum >= MAX_MODELS)
			Host_Error ("CL_ParseModel: bad modnum");
	}
	else
		modnum = ent->baseline.modelindex;

	if (bits & U_FRAME)
		ent->frame = MSG_ReadByte ();
	else
		ent->frame = ent->baseline.frame;

	if (bits & U_COLORMAP)
		i = MSG_ReadByte();
	else
		i = ent->baseline.colormap;
	if (!i)
		ent->colormap = vid.colormap;
	else
	{
		if (i > cl.maxclients)
			Sys_Error ("i >= cl.maxclients");
		ent->colormap = cl.scores[i-1].translations;
	}
	if (bits & U_SKIN)
		skin = MSG_ReadByte();
	else
		skin = ent->baseline.skin;
	if (skin != ent->skinnum)
	{
		ent->skinnum = skin;
		if (num > 0 && num <= cl.maxclients)
			R_TranslateNewPlayerSkin (num - 1); //johnfitz -- was R_TranslatePlayerSkin
	}
	if (bits & U_EFFECTS)
		ent->effects = MSG_ReadByte();
	else
		ent->effects = ent->baseline.effects;

// shift the known values for interpolation
	VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
	VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);

	if (bits & U_ORIGIN1)
		ent->msg_origins[0][0] = MSG_ReadCoord (cl.protocolflags);
	else
		ent->msg_origins[0][0] = ent->baseline.origin[0];
	if (bits & U_ANGLE1)
		ent->msg_angles[0][0] = MSG_ReadAngle(cl.protocolflags);
	else
		ent->msg_angles[0][0] = ent->baseline.angles[0];

	if (bits & U_ORIGIN2)
		ent->msg_origins[0][1] = MSG_ReadCoord (cl.protocolflags);
	else
		ent->msg_origins[0][1] = ent->baseline.origin[1];
	if (bits & U_ANGLE2)
		ent->msg_angles[0][1] = MSG_ReadAngle(cl.protocolflags);
	else
		ent->msg_angles[0][1] = ent->baseline.angles[1];

	if (bits & U_ORIGIN3)
		ent->msg_origins[0][2] = MSG_ReadCoord (cl.protocolflags);
	else
		ent->msg_origins[0][2] = ent->baseline.origin[2];
	if (bits & U_ANGLE3)
		ent->msg_angles[0][2] = MSG_ReadAngle(cl.protocolflags);
	else
		ent->msg_angles[0][2] = ent->baseline.angles[2];

	//johnfitz -- lerping for movetype_step entities
	if (bits & U_STEP)
	{
		ent->lerpflags |= LERP_MOVESTEP;
		ent->forcelink = true;
	}
	else
		ent->lerpflags &= ~LERP_MOVESTEP;
	//johnfitz

	//johnfitz -- PROTOCOL_FITZQUAKE and PROTOCOL_NEHAHRA
	if (cl.protocol == PROTOCOL_FITZQUAKE || cl.protocol == PROTOCOL_RMQ)
	{
		if (bits & U_ALPHA)
			ent->alpha = MSG_ReadByte();
		else
			ent->alpha = ent->baseline.alpha;
		if (bits & U_SCALE)
			ent->scale = MSG_ReadByte();
		else
			ent->scale = ent->baseline.scale;
		if (bits & U_FRAME2)
			ent->frame = (ent->frame & 0x00FF) | (MSG_ReadByte() << 8);
		if (bits & U_MODEL2)
			modnum = (modnum & 0x00FF) | (MSG_ReadByte() << 8);
		if (bits & U_LERPFINISH)
		{
			ent->lerpfinish = ent->msgtime + ((float)(MSG_ReadByte()) / 255);
			ent->lerpflags |= LERP_FINISH;
		}
		else
			ent->lerpflags &= ~LERP_FINISH;
	}
	else if (cl.protocol == PROTOCOL_NETQUAKE)
	{
		//HACK: if this bit is set, assume this is PROTOCOL_NEHAHRA
		if (bits & U_TRANS)
		{
			float a, b;

			if (warn_about_nehahra_protocol)
			{
				Con_Warning ("nonstandard update bit, assuming Nehahra protocol\n");
				warn_about_nehahra_protocol = false;
			}

			a = MSG_ReadFloat();
			b = MSG_ReadFloat(); //alpha
			if (a == 2)
				MSG_ReadFloat(); //fullbright (not using this yet)
			ent->alpha = ENTALPHA_ENCODE(b);
		}
		else
			ent->alpha = ent->baseline.alpha;
		ent->scale = ent->baseline.scale;
	}
	//johnfitz

	//johnfitz -- moved here from above
	model = cl.model_precache[modnum];
	if (model != ent->model)
	{
		qmodel_t *previousmodel = ent->model;

		ent->model = model;
	// automatic animation (torches, etc) can be either all together
	// or randomized
		if (model)
		{
			if (model->synctype == ST_RAND)
				ent->syncbase = (float)(rand()&0x7fff) / 0x7fff;
			else
				ent->syncbase = 0.0;
		}
		else
			forcelink = true;	// hack to make null model players work
		if (num > 0 && num <= cl.maxclients)
			R_TranslateNewPlayerSkin (num - 1); //johnfitz -- was R_TranslatePlayerSkin

		ent->lerpflags |= LERP_RESETANIM; //johnfitz -- don't lerp animation across model changes
		CL_LerpDebugResetAnim (num, "legacy", "model change", -1.0f,
			previousmodel, model, oldframe, ent->frame);
	}
	//johnfitz

	if ( forcelink )
	{	// didn't have an update last message
		VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
		VectorCopy (ent->msg_origins[0], ent->origin);
		VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);
		VectorCopy (ent->msg_angles[0], ent->angles);
		ent->forcelink = true;
	}
}

/*
==================
CL_ParseBaseline
==================
*/
void CL_ParseBaseline (entity_t *ent, int version) //johnfitz -- added argument
{
	int	i;
	int bits; //johnfitz

	//johnfitz -- PROTOCOL_FITZQUAKE
	ent->baseline = nullentitystate;
	bits = (version == 2) ? MSG_ReadByte() : 0;
	ent->baseline.modelindex = (bits & B_LARGEMODEL) ? MSG_ReadShort() : MSG_ReadByte();
	ent->baseline.frame = (bits & B_LARGEFRAME) ? MSG_ReadShort() : MSG_ReadByte();
	//johnfitz

	ent->baseline.colormap = MSG_ReadByte();
	ent->baseline.skin = MSG_ReadByte();
	for (i = 0; i < 3; i++)
	{
		ent->baseline.origin[i] = MSG_ReadCoord (cl.protocolflags);
		ent->baseline.angles[i] = MSG_ReadAngle (cl.protocolflags);
	}

	ent->baseline.alpha = (bits & B_ALPHA) ? MSG_ReadByte() : ENTALPHA_DEFAULT; //johnfitz -- PROTOCOL_FITZQUAKE
	ent->baseline.scale = (bits & B_SCALE) ? MSG_ReadByte() : ENTSCALE_DEFAULT;
	ent->netstate = ent->baseline;
}

static int MSG_ReadSize16 (void)
{
	unsigned short ssolid = MSG_ReadShort ();
	if (ssolid == ES_SOLID_BSP)
		return ssolid;
	else
	{
		int solid = (((ssolid >> 7) & 0x1f8) - 32 + 32768) << 16;
		solid |= ((ssolid & 0x1f) << 3);
		solid |= ((ssolid & 0x3e0) << 6);
		return solid;
	}
}

static unsigned int CLFTE_ReadDelta (unsigned int entnum, entity_state_t *news,
	const entity_state_t *olds, const entity_state_t *baseline)
{
	unsigned int bits;
	unsigned int predbits = 0;

	bits = MSG_ReadByte ();
	if (bits & UF_EXTEND1)
		bits |= MSG_ReadByte () << 8;
	if (bits & UF_EXTEND2)
		bits |= MSG_ReadByte () << 16;
	if (bits & UF_EXTEND3)
		bits |= MSG_ReadByte () << 24;

	if (cl_shownet.value >= 3)
		Con_SafePrintf ("%3i:     FTE update %4u 0x%x\n", msg_readcount, entnum, bits);

	if (bits & UF_RESET)
		*news = *baseline;
	else if (olds)
		*news = *olds;
	else
		*news = *baseline;

	if (bits & UF_FRAME)
		news->frame = (bits & UF_16BIT) ? MSG_ReadShort () : MSG_ReadByte ();
	if (bits & UF_ORIGINXY)
	{
		news->origin[0] = MSG_ReadCoord (cl.protocolflags);
		news->origin[1] = MSG_ReadCoord (cl.protocolflags);
	}
	if (bits & UF_ORIGINZ)
		news->origin[2] = MSG_ReadCoord (cl.protocolflags);
	if ((bits & UF_PREDINFO) && !(cl.protocol_pext2 & PEXT2_PREDINFO))
	{
		if (bits & UF_ANGLESXZ)
		{
			news->angles[0] = MSG_ReadAngle16 (cl.protocolflags);
			news->angles[2] = MSG_ReadAngle16 (cl.protocolflags);
		}
		if (bits & UF_ANGLESY)
			news->angles[1] = MSG_ReadAngle16 (cl.protocolflags);
	}
	else
	{
		if (bits & UF_ANGLESXZ)
		{
			news->angles[0] = MSG_ReadAngle (cl.protocolflags);
			news->angles[2] = MSG_ReadAngle (cl.protocolflags);
		}
		if (bits & UF_ANGLESY)
			news->angles[1] = MSG_ReadAngle (cl.protocolflags);
	}
	if ((bits & (UF_EFFECTS | UF_EFFECTS2)) == (UF_EFFECTS | UF_EFFECTS2))
		news->effects = MSG_ReadLong ();
	else if (bits & UF_EFFECTS2)
		news->effects = (unsigned short)MSG_ReadShort ();
	else if (bits & UF_EFFECTS)
		news->effects = MSG_ReadByte ();

	news->velocity[0] = news->velocity[1] = news->velocity[2] = 0;
	if (bits & UF_PREDINFO)
	{
		predbits = MSG_ReadByte ();
		if (predbits & UFP_FORWARD)
			MSG_ReadShort ();
		if (predbits & UFP_SIDE)
			MSG_ReadShort ();
		if (predbits & UFP_UP)
			MSG_ReadShort ();
		if (predbits & UFP_MOVETYPE)
			news->pmovetype = MSG_ReadByte ();
		if (predbits & UFP_VELOCITYXY)
		{
			news->velocity[0] = MSG_ReadShort ();
			news->velocity[1] = MSG_ReadShort ();
		}
		if (predbits & UFP_VELOCITYZ)
			news->velocity[2] = MSG_ReadShort ();
		if (predbits & UFP_MSEC)
			MSG_ReadByte ();
		if ((cl.protocol_pext2 & PEXT2_PREDINFO) && (predbits & UFP_VIEWANGLE))
		{
			if (bits & UF_ANGLESXZ)
			{
				MSG_ReadShort ();
				MSG_ReadShort ();
			}
			if (bits & UF_ANGLESY)
				MSG_ReadShort ();
		}
	}

	if (bits & UF_MODEL)
		news->modelindex = (bits & UF_16BIT) ? MSG_ReadShort () : MSG_ReadByte ();
	if (bits & UF_SKIN)
		news->skin = (bits & UF_16BIT) ? MSG_ReadShort () : MSG_ReadByte ();
	if (bits & UF_COLORMAP)
		news->colormap = MSG_ReadByte ();
	if (bits & UF_SOLID)
	{
		if (cl.protocol_pext2 & PEXT2_NEWSIZEENCODING)
		{
			byte enc = MSG_ReadByte ();
			if (enc == 0)
				news->solidsize = ES_SOLID_NOT;
			else if (enc == 1)
				news->solidsize = ES_SOLID_BSP;
			else if (enc == 2)
				news->solidsize = ES_SOLID_HULL1;
			else if (enc == 3)
				news->solidsize = ES_SOLID_HULL2;
			else if (enc == 16)
				news->solidsize = MSG_ReadSize16 ();
			else if (enc == 32)
				news->solidsize = MSG_ReadLong ();
			else
				Host_Error ("CLFTE_ReadDelta: unknown solid encoding %u", enc);
		}
		else
			news->solidsize = MSG_ReadSize16 ();
	}
	if (bits & UF_FLAGS)
		news->eflags = MSG_ReadByte ();
	if (bits & UF_ALPHA)
		news->alpha = (MSG_ReadByte () + 1) & 0xff;
	if (bits & UF_SCALE)
		news->scale = MSG_ReadByte ();
	if (bits & UF_BONEDATA)
	{
		unsigned char fl = MSG_ReadByte ();
		if (fl & 0x80)
		{
			int i, bonecount = MSG_ReadByte ();
			for (i = 0; i < bonecount * 7; i++)
				MSG_ReadShort ();
		}
		if (fl & 0x40)
		{
			MSG_ReadByte ();
			MSG_ReadShort ();
		}
		if (fl & 0x3f)
			Host_Error ("CLFTE_ReadDelta: unsupported bone delta");
	}
	if (bits & UF_DRAWFLAGS)
		news->drawflags = MSG_ReadByte ();
	if (bits & UF_TAGINFO)
	{
		news->tagentity = MSG_ReadEntity (cl.protocol_pext2);
		news->tagindex = MSG_ReadByte ();
	}
	if (bits & UF_LIGHT)
	{
		MSG_ReadShort (); MSG_ReadShort (); MSG_ReadShort (); MSG_ReadShort ();
		MSG_ReadByte (); MSG_ReadByte ();
	}
	if (bits & UF_TRAILEFFECT)
	{
		unsigned short v = MSG_ReadShort ();
		news->emiteffectnum = 0;
		news->traileffectnum = v & 0x3fff;
		if (v & 0x8000)
			news->emiteffectnum = MSG_ReadShort () & 0x3fff;
	}
	if (bits & UF_COLORMOD)
	{
		news->colormod[0] = MSG_ReadByte ();
		news->colormod[1] = MSG_ReadByte ();
		news->colormod[2] = MSG_ReadByte ();
	}
	if (bits & UF_GLOW)
	{
		MSG_ReadByte ();
		MSG_ReadByte ();
		news->glowmod[0] = MSG_ReadByte ();
		news->glowmod[1] = MSG_ReadByte ();
		news->glowmod[2] = MSG_ReadByte ();
	}
	if (bits & UF_FATNESS)
		MSG_ReadByte ();
	if (bits & UF_MODELINDEX2)
	{
		if (bits & UF_16BIT)
			MSG_ReadShort ();
		else
			MSG_ReadByte ();
	}
	if (bits & UF_GRAVITYDIR)
	{
		MSG_ReadByte ();
		MSG_ReadByte ();
	}
	if (bits & (UF_UNUSED1 | UF_UNUSED2))
		Host_Error ("CLFTE_ReadDelta: unsupported entity delta bits");
	CL_LerpDebugDeltaReset (entnum, bits, olds, news);
	return bits;
}

static void CLFTE_EntitiesDeltaed (void)
{
	int newnum;
	entity_t *ent;
	qmodel_t *model;
	qboolean forcelink;

	for (newnum = 1; newnum < cl.num_entities; newnum++)
	{
		float oldmsgtime;
		qmodel_t *oldmodel;
		int oldframe;
		int oldskin;

		ent = CL_EntityNum (newnum);
		if (!ent->update_type)
			continue;
		oldmsgtime = ent->msgtime;
		oldmodel = ent->model;
		oldframe = ent->frame;
		oldskin = ent->skinnum;

		if (ent->msgtime == cl.mtime[0])
			forcelink = false;
		else
		{
			forcelink = ent->msgtime != cl.mtime[1];
			if (ent->msgtime + 0.2 < cl.mtime[0])
			{
				ent->lerpflags |= LERP_RESETANIM;
				CL_LerpDebugResetAnim (newnum, "replacement-delta",
					"message gap", cl.mtime[0] - oldmsgtime,
					oldmodel, oldmodel, oldframe, oldframe);
			}
			ent->msgtime = cl.mtime[0];
			VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
			VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);
			VectorCopy (ent->netstate.origin, ent->msg_origins[0]);
			VectorCopy (ent->netstate.angles, ent->msg_angles[0]);
		}

		ent->skinnum = ent->netstate.skin;
		ent->effects = ent->netstate.effects;
		if (ent->netstate.eflags & EFLAGS_STEP)
		{
			ent->lerpflags |= LERP_MOVESTEP;
			ent->forcelink = true;
		}
		else
			ent->lerpflags &= ~LERP_MOVESTEP;
		ent->alpha = ent->netstate.alpha;
		ent->scale = ent->netstate.scale;

		if (!ent->netstate.colormap)
			ent->colormap = vid.colormap;
		else if (ent->netstate.colormap <= cl.maxclients)
			ent->colormap = cl.scores[ent->netstate.colormap - 1].translations;
		else
			ent->colormap = vid.colormap;

		model = cl.model_precache[ent->netstate.modelindex];
		if (model != ent->model)
		{
			qmodel_t *previousmodel = ent->model;

			ent->model = model;
			if (model)
			{
				if (model->synctype == ST_RAND)
					ent->syncbase = (float)(rand() & 0x7fff) / 0x7fff;
				else
					ent->syncbase = 0.0;
			}
			else
				forcelink = true;
			ent->lerpflags |= LERP_RESETANIM;
			CL_LerpDebugResetAnim (newnum, "replacement-delta",
				"model change", -1.0f, previousmodel, model,
				oldframe, ent->netstate.frame);
		}
		ent->frame = ent->netstate.frame;
		if (newnum <= cl.maxclients && (oldmodel != ent->model ||
			oldskin != ent->skinnum))
			R_TranslateNewPlayerSkin (newnum - 1);

		if (forcelink)
		{
			VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
			VectorCopy (ent->msg_origins[0], ent->origin);
			VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);
			VectorCopy (ent->msg_angles[0], ent->angles);
			ent->forcelink = true;
		}
	}
}

static int CL_ExpandMoveAck16 (int ack16)
{
	int ack;

	if (ack16 == 0xffff && cl.ackedmovemessages < 0)
		return -1;
	if (cl.ackedmovemessages < 0)
		return ack16;

	ack = (cl.ackedmovemessages & ~0xffff) | (ack16 & 0xffff);
	if (ack <= cl.ackedmovemessages - 0x8000)
		ack += 0x10000;
	else if (ack > cl.ackedmovemessages + 0x8000)
		ack -= 0x10000;
	return ack;
}

static qboolean CL_UpdateMoveAck (int ack)
{
	if (ack > cl.ackedmovemessages)
	{
		cl.ackedmovemessages = ack;
		if (cl.qcvm.extglobals.servercommandframe)
			*cl.qcvm.extglobals.servercommandframe = cl.ackedmovemessages;
		cl.net_move_acks++;
		return true;
	}
	if (ack == cl.ackedmovemessages)
		return true;

	cl.net_move_stale_acks++;
	return false;
}

static void CLFTE_QueueAckFrame (int sequence)
{
	unsigned int i;

	if (!cls.netcon)
		return;
	if (sequence < 0)
		return;
	if (cl.ackframes_count < sizeof(cl.ackframes) / sizeof(cl.ackframes[0]))
		cl.ackframes[cl.ackframes_count++] = sequence;
	else
		cl.ackframes[countof(cl.ackframes) - 1] = sequence;

	if (cl.ackframes_history_count &&
		cl.ackframes_history[cl.ackframes_history_count - 1] == sequence)
		return;
	for (i = 0; i < cl.ackframes_history_count; i++)
		if (cl.ackframes_history[i] == sequence)
			return;
	if (cl.ackframes_history_count < countof(cl.ackframes_history))
		cl.ackframes_history[cl.ackframes_history_count++] = sequence;
	else
	{
		memmove(cl.ackframes_history, cl.ackframes_history + 1,
			(countof(cl.ackframes_history) - 1) * sizeof(cl.ackframes_history[0]));
		cl.ackframes_history[countof(cl.ackframes_history) - 1] = sequence;
	}
}

static void CLFTE_ParseEntitiesUpdate (void)
{
	int newnum;
	qboolean removeflag;
	entity_t *ent;
	float newtime;
	int frame_sequence;

	frame_sequence = MSG_ReadLong ();
	CLFTE_QueueAckFrame (frame_sequence);
	if (cl.net_snapshot_have && frame_sequence > cl.net_snapshot_sequence + 1)
	{
		cl.net_snapshot_drops += frame_sequence - cl.net_snapshot_sequence - 1;
		if (net_lagdebug.value)
			Con_DPrintf ("net_lagdebug: replacement frame gap old=%d new=%d missing=%d\n",
				cl.net_snapshot_sequence, frame_sequence,
				frame_sequence - cl.net_snapshot_sequence - 1);
	}
	cl.net_snapshot_have = true;
	cl.net_snapshot_sequence = frame_sequence;
	cl.net_snapshot_packets++;

	if (cl.protocol_pext2 & PEXT2_PREDINFO)
	{
		int seq = CL_ExpandMoveAck16 (MSG_ReadShort () & 0xffff);
		if (!CL_UpdateMoveAck (seq) && net_lagdebug.value)
			Con_DPrintf ("net_lagdebug: ignored stale replacement move ack=%d current=%d\n",
				seq, cl.ackedmovemessages);
	}

	newtime = MSG_ReadFloat ();
	if (newtime != cl.mtime[0])
	{
		cl.mtime[1] = cl.mtime[0];
		cl.mtime[0] = newtime;
	}

	for (;;)
	{
		newnum = (unsigned short)(short)MSG_ReadShort ();
		removeflag = !!(newnum & 0x8000);
		if (newnum & 0x4000)
			newnum = (newnum & 0x3fff) | (MSG_ReadByte () << 14);
		else
			newnum &= ~0x8000;

		if ((!newnum && !removeflag) || msg_badread)
			break;

		ent = CL_EntityNum (newnum);
		if (removeflag)
		{
			if (!newnum)
			{
				CL_LerpDebugEntityEvent ("full-remove-reset", 0, NULL);
				for (newnum = 1; newnum < cl.num_entities; newnum++)
				{
					cl.entities[newnum].update_type = false;
					cl.entities[newnum].netstate = nullentitystate;
					cl.entities[newnum].model = NULL;
				}
				cl.requestresend = false;
				continue;
			}
			CL_LerpDebugEntityEvent ("remove", newnum, ent);
			ent->update_type = false;
			ent->netstate = nullentitystate;
			ent->model = NULL;
			ent->lerpflags |= LERP_RESETMOVE | LERP_RESETANIM;
			continue;
		}
		if (ent->update_type)
			CLFTE_ReadDelta (newnum, &ent->netstate, &ent->netstate, &ent->baseline);
		else
		{
			ent->update_type = true;
			CLFTE_ReadDelta (newnum, &ent->netstate, NULL, &ent->baseline);
			ent->lerpflags |= LERP_RESETMOVE | LERP_RESETANIM;
			CL_LerpDebugEntityEvent ("first-update", newnum, ent);
		}
	}

	CLFTE_EntitiesDeltaed ();
	if (!cl.requestresend && cls.signon == SIGNONS - 1)
	{
		cls.signon = SIGNONS;
		CL_SignonReply ();
	}
}

#define CL_SetHudStat(stat) cl.statsf[stat] = cl.stats[stat]

/*
==================
CL_ParseClientdata

Server information pertaining to this client only
==================
*/
void CL_ParseClientdata (void)
{
	int		i, j;
	int		bits; //johnfitz

	bits = (unsigned short)MSG_ReadShort (); //johnfitz -- read bits here isntead of in CL_ParseServerMessage()

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (bits & SU_EXTEND1)
		bits |= (MSG_ReadByte() << 16);
	if (bits & SU_EXTEND2)
		bits |= (MSG_ReadByte() << 24);
	//johnfitz

	if (bits & SU_VIEWHEIGHT)
		cl.viewheight = MSG_ReadChar ();
	else
		cl.viewheight = DEFAULT_VIEWHEIGHT;

	if (bits & SU_IDEALPITCH)
		cl.idealpitch = MSG_ReadChar ();
	else
		cl.idealpitch = 0;

	VectorCopy (cl.mvelocity[0], cl.mvelocity[1]);
	for (i = 0; i < 3; i++)
	{
		if (bits & (SU_PUNCH1<<i) )
			cl.punchangle[i] = MSG_ReadChar();
		else
			cl.punchangle[i] = 0;

		if (bits & (SU_VELOCITY1<<i) )
			cl.mvelocity[0][i] = MSG_ReadChar()*16;
		else
			cl.mvelocity[0][i] = 0;
	}

	//johnfitz -- update v_punchangles
	if (v_punchangles[0][0] != cl.punchangle[0] || v_punchangles[0][1] != cl.punchangle[1] || v_punchangles[0][2] != cl.punchangle[2])
	{
		VectorCopy (v_punchangles[0], v_punchangles[1]);
		VectorCopy (cl.punchangle, v_punchangles[0]);
		cl.punchtime = cl.time;
	}
	//johnfitz

// [always sent]	if (bits & SU_ITEMS)
		i = MSG_ReadLong ();

	if (cl.items != i)
	{	// set flash times
		Sbar_Changed ();
		for (j = 0; j < 32; j++)
			if ( (i & (1<<j)) && !(cl.items & (1<<j)))
				cl.item_gettime[j] = cl.time;
		cl.items = i;
		cl.stats[STAT_ITEMS] = i;
	}

	cl.onground = (bits & SU_ONGROUND) != 0;
	cl.inwater = (bits & SU_INWATER) != 0;

	if (bits & SU_WEAPONFRAME)
		cl.stats[STAT_WEAPONFRAME] = MSG_ReadByte ();
	else
		cl.stats[STAT_WEAPONFRAME] = 0;

	if (bits & SU_ARMOR)
		i = MSG_ReadByte ();
	else
		i = 0;
	if (cl.stats[STAT_ARMOR] != i)
	{
		cl.stats[STAT_ARMOR] = i;
		Sbar_Changed ();
	}

	if (bits & SU_WEAPON)
		i = MSG_ReadByte ();
	else
		i = 0;
	if (cl.stats[STAT_WEAPON] != i)
	{
		cl.stats[STAT_WEAPON] = i;
		Sbar_Changed ();
	}

	i = MSG_ReadShort ();
	if (cl.stats[STAT_HEALTH] != i)
	{
		cl.stats[STAT_HEALTH] = i;
		Sbar_Changed ();
	}

	i = MSG_ReadByte ();
	if (cl.stats[STAT_AMMO] != i)
	{
		cl.stats[STAT_AMMO] = i;
		Sbar_Changed ();
	}

	for (i = 0; i < 4; i++)
	{
		j = MSG_ReadByte ();
		if (cl.stats[STAT_SHELLS+i] != j)
		{
			cl.stats[STAT_SHELLS+i] = j;
			Sbar_Changed ();
		}
	}

	i = MSG_ReadByte ();

	if (standard_quake)
	{
		if (cl.stats[STAT_ACTIVEWEAPON] != i)
		{
			cl.stats[STAT_ACTIVEWEAPON] = i;
			Sbar_Changed ();
		}
	}
	else
	{
		if (cl.stats[STAT_ACTIVEWEAPON] != (1<<i))
		{
			cl.stats[STAT_ACTIVEWEAPON] = (1<<i);
			Sbar_Changed ();
		}
	}

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (bits & SU_WEAPON2)
		cl.stats[STAT_WEAPON] |= (MSG_ReadByte() << 8);
	if (bits & SU_ARMOR2)
		cl.stats[STAT_ARMOR] |= (MSG_ReadByte() << 8);
	if (bits & SU_AMMO2)
		cl.stats[STAT_AMMO] |= (MSG_ReadByte() << 8);
	if (bits & SU_SHELLS2)
		cl.stats[STAT_SHELLS] |= (MSG_ReadByte() << 8);
	if (bits & SU_NAILS2)
		cl.stats[STAT_NAILS] |= (MSG_ReadByte() << 8);
	if (bits & SU_ROCKETS2)
		cl.stats[STAT_ROCKETS] |= (MSG_ReadByte() << 8);
	if (bits & SU_CELLS2)
		cl.stats[STAT_CELLS] |= (MSG_ReadByte() << 8);
	if (bits & SU_WEAPONFRAME2)
		cl.stats[STAT_WEAPONFRAME] |= (MSG_ReadByte() << 8);
	if (bits & SU_WEAPONALPHA)
		cl.viewent.alpha = MSG_ReadByte();
	else
		cl.viewent.alpha = ENTALPHA_DEFAULT;
	//johnfitz

	CL_SetHudStat (STAT_ITEMS);
	CL_SetHudStat (STAT_WEAPONFRAME);
	CL_SetHudStat (STAT_ARMOR);
	CL_SetHudStat (STAT_WEAPON);
	/* SV_WriteStats carries the untruncated weapon bitfield for CSQC HUDs. */
	CL_SetHudStat (STAT_HEALTH);
	CL_SetHudStat (STAT_AMMO);
	CL_SetHudStat (STAT_SHELLS);
	CL_SetHudStat (STAT_NAILS);
	CL_SetHudStat (STAT_ROCKETS);
	CL_SetHudStat (STAT_CELLS);

	//johnfitz -- lerping
	//ericw -- this was done before the upper 8 bits of cl.stats[STAT_WEAPON] were filled in, breaking on large maps like zendar.bsp
	if (cl.viewent.model != cl.model_precache[cl.stats[STAT_WEAPON]])
	{
		cl.viewent.lerpflags |= LERP_RESETANIM; //don't lerp animation across model changes
	}
	//johnfitz
}

/*
=====================
CL_NewTranslation
=====================
*/
void CL_NewTranslation (int slot)
{
	int		i, j;
	int		top, bottom;
	byte	*dest, *source;

	if (slot > cl.maxclients)
		Sys_Error ("CL_NewTranslation: slot > cl.maxclients");
	dest = cl.scores[slot].translations;
	source = vid.colormap;
	memcpy (dest, vid.colormap, sizeof(cl.scores[slot].translations));
	top = cl.scores[slot].colors & 0xf0;
	bottom = (cl.scores[slot].colors &15)<<4;
	R_TranslatePlayerSkin (slot);

	for (i = 0; i < VID_GRADES; i++, dest += 256, source+=256)
	{
		if (top < 128)	// the artists made some backwards ranges.  sigh.
			memcpy (dest + TOP_RANGE, source + top, 16);
		else
		{
			for (j = 0; j < 16; j++)
				dest[TOP_RANGE+j] = source[top+15-j];
		}

		if (bottom < 128)
			memcpy (dest + BOTTOM_RANGE, source + bottom, 16);
		else
		{
			for (j = 0; j < 16; j++)
				dest[BOTTOM_RANGE+j] = source[bottom+15-j];
		}
	}
}

/*
=====================
CL_ParseStatic
=====================
*/
void CL_ParseStatic (int version) //johnfitz -- added a parameter
{
	entity_t *ent;
	int		i;

	i = cl.num_statics;
	if (i >= MAX_STATIC_ENTITIES)
		Host_Error ("Too many static entities");

	ent = &cl_static_entities[i];
	cl.num_statics++;
	CL_ParseBaseline (ent, version); //johnfitz -- added second parameter

// copy it to the current state

	ent->model = cl.model_precache[ent->baseline.modelindex];
	ent->lerpflags |= LERP_RESETANIM; //johnfitz -- lerping
	ent->frame = ent->baseline.frame;

	ent->colormap = vid.colormap;
	ent->skinnum = ent->baseline.skin;
	ent->effects = ent->baseline.effects;
	ent->alpha = ent->baseline.alpha; //johnfitz -- alpha
	ent->scale = ent->baseline.scale;

	VectorCopy (ent->baseline.origin, ent->origin);
	VectorCopy (ent->baseline.angles, ent->angles);
	R_AddEfrags (ent);
}

/*
===================
CL_ParseStaticSound
===================
*/
void CL_ParseStaticSound (int version) //johnfitz -- added argument
{
	vec3_t		org;
	int			sound_num, vol, atten;
	int			i;

	for (i = 0; i < 3; i++)
		org[i] = MSG_ReadCoord (cl.protocolflags);

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (version == 2)
		sound_num = MSG_ReadShort ();
	else
		sound_num = MSG_ReadByte ();
	//johnfitz

	vol = MSG_ReadByte ();
	atten = MSG_ReadByte ();

	S_StaticSound (cl.sound_precache[sound_num], org, vol, atten);
}


#if 0	/* for debugging. from fteqw. */
static void CL_DumpPacket (void)
{
	int			i, pos;
	unsigned char	*packet = net_message.data;

	Con_Printf("CL_DumpPacket, BEGIN:\n");
	pos = 0;
	while (pos < net_message.cursize)
	{
		Con_Printf("%5i ", pos);
		for (i = 0; i < 16; i++)
		{
			if (pos >= net_message.cursize)
				Con_Printf(" X ");
			else	Con_Printf("%2x ", packet[pos]);
			pos++;
		}
		pos -= 16;
		for (i = 0; i < 16; i++)
		{
			if (pos >= net_message.cursize)
				Con_Printf("X");
			else if (packet[pos] == 0)
				Con_Printf(".");
			else	Con_Printf("%c", packet[pos]);
			pos++;
		}
		Con_Printf("\n");
	}

	Con_Printf("CL_DumpPacket, --- END ---\n");
}
#endif	/* CL_DumpPacket */

#define SHOWNET(x) if(cl_shownet.value==2)Con_Printf ("%3i:%s\n", msg_readcount-1, x);

//mods and servers might not send the \n instantly.
//some mods bug out and omit the \n entirely, this function helps prevent the damage from spreading too much.
//some servers or mods use //prefixed commands as extensions to avoid spam about unrecognised commands.
//proquake has its own extension coding thing.
static void CL_ParseStuffText(const char *msg)
{
	const char *str;
	q_strlcat(cl.stuffcmdbuf, msg, sizeof(cl.stuffcmdbuf));
	for (; (str = strchr(cl.stuffcmdbuf, '\n')); memmove(cl.stuffcmdbuf, str, Q_strlen(str)+1))
	{
		qboolean handled = false;

		str++;//skip past the \n

		if (*cl.stuffcmdbuf == 0x01 && cl.protocol == PROTOCOL_NETQUAKE) //proquake message, just strip this and try again (doesn't necessarily have a trailing \n straight away)
		{
			for (str = cl.stuffcmdbuf+1; *str >= 0x01 && *str <= 0x1f; str++)
				;//FIXME: parse properly
			continue;
		}

		//handle special commands
		if (cl.stuffcmdbuf[0] == '/' && cl.stuffcmdbuf[1] == '/')
		{
			handled = Cmd_ExecuteString(cl.stuffcmdbuf+2, src_server);
			if (!handled)
				Con_DPrintf("Server sent unknown command %s\n", Cmd_Argv(0));
		}
		else
			handled = Cmd_ExecuteString(cl.stuffcmdbuf, src_server);

		//let the server exec general user commands (massive security hole)
		if (!handled)
			Cbuf_AddTextLen(cl.stuffcmdbuf, str-cl.stuffcmdbuf);
	}
}

/*
=====================
CL_ParseMoveAck
=====================
*/
static float CL_ReadPayloadFloat (const byte *payload, int *offset)
{
	union
	{
		byte	b[4];
		float	f;
		int	l;
	} dat;

	dat.b[0] = payload[*offset + 0];
	dat.b[1] = payload[*offset + 1];
	dat.b[2] = payload[*offset + 2];
	dat.b[3] = payload[*offset + 3];
	*offset += 4;
	dat.l = LittleLong (dat.l);
	return dat.f;
}

static void CL_ApplyMoveAck (int ack16, int flags, int movetype, float servertime,
	const vec3_t origin, const vec3_t velocity, const vec3_t mins,
	const vec3_t maxs)
{
	int ack;
	int i;
	qboolean use_predstate;

	ack = CL_ExpandMoveAck16 (ack16);
	use_predstate = CL_UpdateMoveAck (ack);

	if (!use_predstate)
	{
		if (net_lagdebug.value)
			Con_DPrintf ("net_lagdebug: ignored stale prediction state ack=%d current=%d\n",
				ack, cl.ackedmovemessages);
		return;
	}

	cl.predstate_flags = flags;
	cl.predstate_valid = (flags & PREDINFO_VALID) != 0;
	cl.predstate_has_origin = cl.predstate_valid;
	cl.predstate_sequence = ack;
	cl.predstate_movetype = movetype;
	cl.predstate_time = servertime;
	for (i = 0; i < 3; i++)
		cl.predstate_origin[i] = origin[i];
	for (i = 0; i < 3; i++)
		cl.predstate_velocity[i] = velocity[i];
	for (i = 0; i < 3; i++)
		cl.predstate_mins[i] = mins[i];
	for (i = 0; i < 3; i++)
		cl.predstate_maxs[i] = maxs[i];
}

static void CL_ParseMoveAck (void)
{
	int ack16;
	int flags;
	int movetype;
	int i;
	float servertime;
	vec3_t origin;
	vec3_t velocity;
	vec3_t mins;
	vec3_t maxs;

	if (net_message.cursize - msg_readcount < 32)
	{
		msg_badread = true;
		return;
	}

	ack16 = MSG_ReadShort () & 0xffff;
	flags = MSG_ReadByte ();
	movetype = MSG_ReadByte ();
	servertime = MSG_ReadFloat ();
	for (i = 0; i < 3; i++)
		origin[i] = MSG_ReadFloat ();
	for (i = 0; i < 3; i++)
		velocity[i] = MSG_ReadShort () * (1.0f / 8.0f);
	for (i = 0; i < 3; i++)
		mins[i] = MSG_ReadChar ();
	for (i = 0; i < 3; i++)
		maxs[i] = MSG_ReadChar ();

	CL_ApplyMoveAck (ack16, flags, movetype, servertime, origin, velocity,
		mins, maxs);
}

static void CL_ParseSnapshotPartMoveAck (const byte *payload, int payload_size)
{
	int offset;
	int ack16;
	int flags;
	int movetype;
	int i;
	int value;
	float servertime;
	vec3_t origin;
	vec3_t velocity;
	vec3_t mins;
	vec3_t maxs;

	if (payload_size < 5 || payload[0] != svc_time)
		return;

	offset = 5;
	if (payload_size - offset < 33 || payload[offset] != svc_moveack)
		return;

	offset++;
	ack16 = (payload[offset] | (payload[offset + 1] << 8)) & 0xffff;
	offset += 2;
	flags = payload[offset++];
	movetype = payload[offset++];
	servertime = CL_ReadPayloadFloat (payload, &offset);
	for (i = 0; i < 3; i++)
		origin[i] = CL_ReadPayloadFloat (payload, &offset);
	for (i = 0; i < 3; i++)
	{
		value = (short)(payload[offset] | (payload[offset + 1] << 8));
		velocity[i] = value * (1.0f / 8.0f);
		offset += 2;
	}
	for (i = 0; i < 3; i++)
		mins[i] = (signed char)payload[offset++];
	for (i = 0; i < 3; i++)
		maxs[i] = (signed char)payload[offset++];

	CL_ApplyMoveAck (ack16, flags, movetype, servertime, origin, velocity,
		mins, maxs);
}

/*
=====================
CL_ParseSnapshotHeader
=====================
*/
typedef struct
{
	qboolean	active;
	struct qsocket_s	*netcon;
	int		seq;
	int		totalents;
	int		cursize;
	int		last_part;
	qboolean	have_first;
	qboolean	have_last;
	qboolean	received[SNAPSHOT_MAX_PARTS];
	int		offsets[SNAPSHOT_MAX_PARTS];
	int		lengths[SNAPSHOT_MAX_PARTS];
	unsigned int	mask[SNAPSHOT_ACK_MASK_WORDS];
	byte		data[MAX_DATAGRAM];
} cl_snapshot_assembly_t;

static cl_snapshot_assembly_t cl_snapshot_assembly;

static int CL_ExpandSnapshotSequence (int seq16)
{
	int seq;
	int base;

	if (cl_snapshot_assembly.active)
		base = cl_snapshot_assembly.seq;
	else if (cl.net_snapshot_have)
		base = cl.net_snapshot_sequence;
	else
		return seq16;

	seq = (base & ~0xffff) | seq16;
	if (seq <= base - 0x8000)
		seq += 0x10000;
	else if (seq > base + 0x8000)
		seq -= 0x10000;
	return seq;
}

static void CL_ResetSnapshotAssembly (void)
{
	memset (&cl_snapshot_assembly, 0, sizeof(cl_snapshot_assembly));
	cl_snapshot_assembly.last_part = -1;
	cl.net_snapshot_partial_active = false;
	cl.net_snapshot_partial_sequence = 0;
	cl.net_snapshot_partial_last_part = -1;
	memset (cl.net_snapshot_partial_mask, 0, sizeof(cl.net_snapshot_partial_mask));
}

static void CL_UpdateSnapshotPartialAck (void)
{
	int i;

	if (!cl_snapshot_assembly.active)
	{
		cl.net_snapshot_partial_active = false;
		return;
	}

	cl.net_snapshot_partial_active = true;
	cl.net_snapshot_partial_sequence = cl_snapshot_assembly.seq;
	cl.net_snapshot_partial_last_part = cl_snapshot_assembly.have_last ?
		cl_snapshot_assembly.last_part : -1;
	for (i = 0; i < SNAPSHOT_ACK_MASK_WORDS; i++)
		cl.net_snapshot_partial_mask[i] = cl_snapshot_assembly.mask[i];
}

static qboolean CL_SnapshotPartMaskTest (int part)
{
	if (part < 0 || part >= SNAPSHOT_MAX_PARTS)
		return false;
	return (cl_snapshot_assembly.mask[part >> 5] & (1u << (part & 31))) != 0;
}

static void CL_SnapshotPartMaskSet (int part)
{
	if (part < 0 || part >= SNAPSHOT_MAX_PARTS)
		return;
	cl_snapshot_assembly.mask[part >> 5] |= 1u << (part & 31);
}

static qboolean CL_SnapshotAssemblyComplete (void)
{
	int i;

	if (!cl_snapshot_assembly.active ||
		!cl_snapshot_assembly.have_first ||
		!cl_snapshot_assembly.have_last)
		return false;
	if (cl_snapshot_assembly.last_part < 0 ||
		cl_snapshot_assembly.last_part >= SNAPSHOT_MAX_PARTS)
		return false;
	for (i = 0; i <= cl_snapshot_assembly.last_part; i++)
		if (!cl_snapshot_assembly.received[i])
			return false;
	return true;
}

/*
=====================
CL_PrepareSnapshotMessage

Snapshot datagrams are split at the UDP layer on busy maps.  Buffer every
piece and expose only the fully assembled payload to the normal parser so
svc_time/clientdata/entity deltas are applied once as one coherent frame.
=====================
*/
static qboolean CL_PrepareSnapshotMessage (void)
{
	byte	*data;
	int	seq, part, flags, firstent, totalents;
	int	payload_offset, payload_size;

	if (net_message.cursize <= 0 || net_message.data[0] != svc_snapshot)
		return true;

	if (net_message.cursize < 9)
	{
		if (net_lagdebug.value)
			Con_Printf ("net_lagdebug: ignoring truncated snapshot header size=%d\n",
				net_message.cursize);
		return false;
	}

	if (cl_snapshot_assembly.active && cl_snapshot_assembly.netcon != cls.netcon)
		CL_ResetSnapshotAssembly ();

	data = net_message.data;
	seq = CL_ExpandSnapshotSequence ((data[1] | (data[2] << 8)) & 0xffff);
	part = data[3];
	flags = data[4];
	firstent = (short)(data[5] | (data[6] << 8));
	totalents = (short)(data[7] | (data[8] << 8));
	payload_offset = 9;
	payload_size = net_message.cursize - payload_offset;

	cl.net_snapshot_packets++;

	if (cl.net_snapshot_have && seq < cl.net_snapshot_sequence)
	{
		if (net_lagdebug.value)
			Con_DPrintf ("net_lagdebug: dropping stale snapshot seq=%d current=%d part=%d flags=%d\n",
				seq, cl.net_snapshot_sequence, part, flags);
		return false;
	}
	if (!cl_snapshot_assembly.active && cl.net_snapshot_have &&
		seq == cl.net_snapshot_sequence)
	{
		if (net_lagdebug.value)
			Con_DPrintf ("net_lagdebug: dropping duplicate complete snapshot seq=%d part=%d flags=%d\n",
				seq, part, flags);
		cl.net_snapshot_duplicate_parts++;
		return false;
	}

	if (!cl_snapshot_assembly.active || cl_snapshot_assembly.seq != seq)
	{
		if (cl_snapshot_assembly.active)
		{
			cl.net_snapshot_incomplete++;
			CL_NetSnapshotStartSmoothing ();
			if (net_lagdebug.value)
				Con_DPrintf ("net_lagdebug: discarding incomplete snapshot seq=%d mask=%08x/%08x/%08x/%08x last=%d for newer seq=%d\n",
					cl_snapshot_assembly.seq,
					cl_snapshot_assembly.mask[0], cl_snapshot_assembly.mask[1],
					cl_snapshot_assembly.mask[2], cl_snapshot_assembly.mask[3],
					cl_snapshot_assembly.last_part, seq);
		}
		CL_ResetSnapshotAssembly ();

		if (cl.net_snapshot_have && seq > cl.net_snapshot_sequence + 1)
		{
			cl.net_snapshot_drops += seq - cl.net_snapshot_sequence - 1;
			CL_NetSnapshotStartSmoothing ();
			if (net_lagdebug.value)
				Con_DPrintf ("net_lagdebug: client snapshot sequence gap old=%d new=%d missing=%d\n",
					cl.net_snapshot_sequence, seq,
					seq - cl.net_snapshot_sequence - 1);
		}

		cl_snapshot_assembly.active = true;
		cl_snapshot_assembly.netcon = cls.netcon;
		cl_snapshot_assembly.seq = seq;
		cl_snapshot_assembly.totalents = totalents;
		cl_snapshot_assembly.last_part = -1;
	}

	if (part < 0 || part >= SNAPSHOT_MAX_PARTS)
	{
		if (net_lagdebug.value)
			Con_Printf ("net_lagdebug: dropping snapshot with invalid part seq=%d part=%d flags=%d\n",
				seq, part, flags);
		cl.net_snapshot_part_jumps++;
		CL_ResetSnapshotAssembly ();
		return false;
	}

	if (CL_SnapshotPartMaskTest (part))
	{
		if (net_lagdebug.value)
			Con_DPrintf ("net_lagdebug: ignoring duplicate snapshot part seq=%d part=%d flags=%d firstent=%d total=%d\n",
				seq, part, flags,
				firstent, totalents);
		cl.net_snapshot_duplicate_parts++;
		CL_UpdateSnapshotPartialAck ();
		return false;
	}

	if ((flags & SNAPSHOT_FIRST) && part != 0)
	{
		cl.net_snapshot_part_jumps++;
		cl.net_snapshot_incomplete++;
		CL_NetSnapshotStartSmoothing ();
		if (net_lagdebug.value)
			Con_DPrintf ("net_lagdebug: snapshot FIRST flag on nonzero part seq=%d part=%d flags=%d firstent=%d total=%d\n",
				seq, part, flags,
				firstent, totalents);
		CL_ResetSnapshotAssembly ();
		return false;
	}
	if (part == 0)
		cl_snapshot_assembly.have_first = true;
	if (flags & SNAPSHOT_LAST)
	{
		if (cl_snapshot_assembly.have_last &&
			cl_snapshot_assembly.last_part != part)
		{
			if (net_lagdebug.value)
				Con_Printf ("net_lagdebug: snapshot changed last part seq=%d old=%d new=%d\n",
					seq, cl_snapshot_assembly.last_part, part);
			cl.net_snapshot_part_jumps++;
			CL_ResetSnapshotAssembly ();
			return false;
		}
		cl_snapshot_assembly.have_last = true;
		cl_snapshot_assembly.last_part = part;
	}
	if (cl_snapshot_assembly.have_last && part > cl_snapshot_assembly.last_part)
	{
		if (net_lagdebug.value)
			Con_Printf ("net_lagdebug: dropping snapshot part beyond LAST seq=%d part=%d last=%d\n",
				seq, part, cl_snapshot_assembly.last_part);
		cl.net_snapshot_part_jumps++;
		CL_ResetSnapshotAssembly ();
		return false;
	}

	if (cl_snapshot_assembly.cursize + payload_size > (int)sizeof(cl_snapshot_assembly.data) ||
		cl_snapshot_assembly.cursize + payload_size > net_message.maxsize)
	{
		if (net_lagdebug.value)
			Con_Printf ("net_lagdebug: dropping oversized assembled snapshot seq=%d size=%d add=%d max=%d\n",
				seq, cl_snapshot_assembly.cursize, payload_size,
				net_message.maxsize);
		cl.net_snapshot_incomplete++;
		CL_NetSnapshotStartSmoothing ();
		CL_ResetSnapshotAssembly ();
		return false;
	}

	if (part > 0 && !cl_snapshot_assembly.received[part - 1])
		cl.net_snapshot_out_of_order_parts++;
	cl_snapshot_assembly.offsets[part] = cl_snapshot_assembly.cursize;
	cl_snapshot_assembly.lengths[part] = payload_size;
	cl_snapshot_assembly.received[part] = true;
	CL_SnapshotPartMaskSet (part);
	memcpy (cl_snapshot_assembly.data + cl_snapshot_assembly.cursize,
		data + payload_offset, payload_size);
	cl_snapshot_assembly.cursize += payload_size;

	if (!CL_SnapshotAssemblyComplete ())
	{
		if (part == 0 && (flags & SNAPSHOT_FIRST))
			CL_ParseSnapshotPartMoveAck (data + payload_offset, payload_size);
		CL_UpdateSnapshotPartialAck ();
		return false;
	}

	net_message.cursize = 0;
	for (part = 0; part <= cl_snapshot_assembly.last_part; part++)
	{
		if (net_message.cursize + cl_snapshot_assembly.lengths[part] > net_message.maxsize)
		{
			if (net_lagdebug.value)
				Con_Printf ("net_lagdebug: assembled snapshot overflow seq=%d size=%d add=%d max=%d\n",
					seq, net_message.cursize,
					cl_snapshot_assembly.lengths[part], net_message.maxsize);
			cl.net_snapshot_incomplete++;
			CL_ResetSnapshotAssembly ();
			return false;
		}
		memcpy (net_message.data + net_message.cursize,
			cl_snapshot_assembly.data + cl_snapshot_assembly.offsets[part],
			cl_snapshot_assembly.lengths[part]);
		net_message.cursize += cl_snapshot_assembly.lengths[part];
	}

	cl.net_snapshot_have = true;
	cl.net_snapshot_sequence = seq;
	cl.net_snapshot_last_part = cl_snapshot_assembly.last_part;
	cl.net_snapshot_reassembled++;

	CL_ResetSnapshotAssembly ();
	return true;
}

static void CL_ParseSnapshotHeader (void)
{
	int seq16;
	int seq;
	int part;
	int flags;
	int firstent;
	int totalents;

	seq16 = MSG_ReadShort () & 0xffff;
	if (!cl.net_snapshot_have)
		seq = seq16;
	else
	{
		seq = (cl.net_snapshot_sequence & ~0xffff) | seq16;
		if (seq <= cl.net_snapshot_sequence - 0x8000)
			seq += 0x10000;
		else if (seq > cl.net_snapshot_sequence + 0x8000)
			seq -= 0x10000;
	}

	part = MSG_ReadByte ();
	flags = MSG_ReadByte ();
	firstent = MSG_ReadShort ();
	totalents = MSG_ReadShort ();

	if (cl.net_snapshot_have && seq > cl.net_snapshot_sequence + 1)
	{
		cl.net_snapshot_drops += seq - cl.net_snapshot_sequence - 1;
		CL_NetSnapshotStartSmoothing ();
		if (net_lagdebug.value)
			Con_DPrintf ("net_lagdebug: client snapshot sequence gap old=%d new=%d missing=%d\n",
				cl.net_snapshot_sequence, seq, seq - cl.net_snapshot_sequence - 1);
	}
	if (cl.net_snapshot_have && seq == cl.net_snapshot_sequence &&
		part != cl.net_snapshot_last_part + 1 && net_lagdebug.value)
	{
		Con_DPrintf ("net_lagdebug: client snapshot part jump seq=%d oldpart=%d newpart=%d flags=%d firstent=%d total=%d\n",
			seq, cl.net_snapshot_last_part, part, flags, firstent, totalents);
	}

	cl.net_snapshot_have = true;
	cl.net_snapshot_sequence = seq;
	cl.net_snapshot_last_part = part;
	cl.net_snapshot_packets++;
}

/*
=====================
CL_ParseServerMessage
=====================
*/
void CL_ParseServerMessage (void)
{
	int			cmd;
	int			i;
	const char		*str; //johnfitz
	int			total, j, lastcmd; //johnfitz
	float			newtime;
	static double		last_svctime_log;

	if (!CL_PrepareSnapshotMessage ())
		return;

//
// if recording demos, copy the message out
//
	if (cl_shownet.value == 1)
		Con_Printf ("%i ",net_message.cursize);
	else if (cl_shownet.value == 2)
		Con_Printf ("------------------\n");

//	cl.onground = false;	// unless the server says otherwise

//
// parse the message
//
	MSG_BeginReading ();

	lastcmd = 0;
	while (1)
	{
		if (msg_badread)
			Host_Error ("CL_ParseServerMessage: Bad server message");

		cmd = MSG_ReadByte ();

		if (cmd == -1)
		{
			SHOWNET("END OF MESSAGE");

			if (*cl.stuffcmdbuf && net_message.cursize < 512)
				CL_ParseStuffText("\n");	//there's a few mods that forget to write \ns, that then fuck up other things too. So make sure it gets flushed to the cbuf. the cursize check is to reduce backbuffer overflows that would give a false positive.

			return;		// end of message
		}

	// if the high bit of the command byte is set, it is a fast update
		if (cmd & U_SIGNAL) //johnfitz -- was 128, changed for clarity
		{
			SHOWNET("fast update");
			CL_ParseUpdate (cmd&127);
			continue;
		}

		if (cmd < (int)NUM_SVC_STRINGS) {
			SHOWNET(svc_strings[cmd]);
		}

	// other commands
		switch (cmd)
		{
		default:
		//	CL_DumpPacket ();
			Host_Error ("Illegible server message %d (previous was %s)", cmd, svc_strings[lastcmd]); //johnfitz -- added svc_strings[lastcmd]
			break;

		case svc_nop:
		//	Con_Printf ("svc_nop\n");
			break;

		case svc_time:
			newtime = MSG_ReadFloat ();
			if (net_lagdebug.value && cls.state == ca_connected && cls.signon == SIGNONS &&
				cl.mtime[0] > 0 && realtime - last_svctime_log > 0.5 &&
				(newtime <= cl.mtime[0] || newtime - cl.mtime[0] > CL_NetLagDebugFrameThreshold ()))
			{
				Con_Printf ("net_lagdebug: client svc_time %s old=%.3f new=%.3f delta=%.3f lastmsg_age=%.3f msgsize=%d\n",
					(newtime <= cl.mtime[0]) ? "non-advancing" : "gap",
					cl.mtime[0], newtime, newtime - cl.mtime[0],
					realtime - cl.last_received_message, net_message.cursize);
				last_svctime_log = realtime;
			}
			cl.mtime[1] = cl.mtime[0];
			cl.mtime[0] = newtime;
			break;

		case svc_moveack:
			CL_ParseMoveAck ();
			break;

		case svc_snapshot:
			CL_ParseSnapshotHeader ();
			break;

		case svcfte_csqcentities:
			PR_SwitchQCVM(&cl.qcvm);
			CLFTE_ParseCSQCEntitiesUpdate();
			PR_SwitchQCVM(NULL);
			break;

		case svc_clientdata:
			CL_ParseClientdata (); //johnfitz -- removed bits parameter, we will read this inside CL_ParseClientdata()
			break;

		case svc_version:
			i = MSG_ReadLong ();
			//johnfitz -- support multiple protocols
			if (i != PROTOCOL_NETQUAKE && i != PROTOCOL_FITZQUAKE && i != PROTOCOL_RMQ)
				Host_Error ("Server returned version %i, not %i or %i or %i", i, PROTOCOL_NETQUAKE, PROTOCOL_FITZQUAKE, PROTOCOL_RMQ);
			cl.protocol = i;
			//johnfitz
			break;

		case svc_disconnect:
			Host_EndGame ("Server disconnected\n");

		case svc_print:
			Con_Printf ("%s", MSG_ReadString ());
			break;

		case svc_centerprint:
			//johnfitz -- log centerprints to console
			str = MSG_ReadString ();
			SCR_CenterPrint (str);
			Con_LogCenterPrint (str);
			//johnfitz
			break;

		case svc_stufftext:
			CL_ParseStuffText (MSG_ReadString ());
			break;

		case svc_damage:
			V_ParseDamage ();
			break;

		case svc_serverinfo:
			CL_ParseServerInfo ();
			vid.recalc_refdef = true;	// leave intermission full screen
			break;

		case svc_setangle:
			for (i=0 ; i<3 ; i++)
				cl.viewangles[i] = MSG_ReadAngle (cl.protocolflags);
			VR_SetAngles (cl.viewangles);
			break;

		case svc_setview:
			cl.viewentity = MSG_ReadShort ();
			VR_PushYaw ();
			break;

		case svc_lightstyle:
			i = MSG_ReadByte ();
			if (i >= MAX_LIGHTSTYLES)
				Sys_Error ("svc_lightstyle > MAX_LIGHTSTYLES");
			q_strlcpy (cl_lightstyle[i].map, MSG_ReadString(), MAX_STYLESTRING);
			cl_lightstyle[i].length = Q_strlen(cl_lightstyle[i].map);
			//johnfitz -- save extra info
			if (cl_lightstyle[i].length)
			{
				total = 0;
				cl_lightstyle[i].peak = 'a';
				for (j=0; j<cl_lightstyle[i].length; j++)
				{
					total += cl_lightstyle[i].map[j] - 'a';
					cl_lightstyle[i].peak = q_max(cl_lightstyle[i].peak, cl_lightstyle[i].map[j]);
				}
				cl_lightstyle[i].average = total / cl_lightstyle[i].length + 'a';
			}
			else
				cl_lightstyle[i].average = cl_lightstyle[i].peak = 'm';
			//johnfitz
			break;

		case svc_sound:
			CL_ParseStartSoundPacket();
			break;

		case svc_stopsound:
			i = MSG_ReadShort();
			S_StopSound(i>>3, i&7);
			break;

		case svc_updatename:
			Sbar_Changed ();
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updatename > MAX_SCOREBOARD");
			q_strlcpy (cl.scores[i].name, MSG_ReadString(), MAX_SCOREBOARDNAME);
			break;

		case svc_updatefrags:
			Sbar_Changed ();
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updatefrags > MAX_SCOREBOARD");
			cl.scores[i].frags = MSG_ReadShort ();
			break;

		case svc_updatecolors:
			Sbar_Changed ();
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updatecolors > MAX_SCOREBOARD");
			cl.scores[i].colors = MSG_ReadByte ();
			CL_NewTranslation (i);
			break;

		case svc_updateping:
			Sbar_Changed ();
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updateping > MAX_SCOREBOARD");
			cl.scores[i].ping = MSG_ReadShort ();
			break;

		case svc_updateplinfo:
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updateplinfo > MAX_SCOREBOARD");
			MSG_ReadByte ();
			break;

		case svc_particle:
			R_ParseParticleEffect ();
			break;

		case svc_spawnbaseline:
			i = MSG_ReadShort ();
			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline (CL_EntityNum(i), 1); // johnfitz -- added second parameter
			break;

		case svc_spawnstatic:
			CL_ParseStatic (1); //johnfitz -- added parameter
			break;

		case svc_temp_entity:
			CL_ParseTEnt ();
			break;

		case svc_setpause:
			cl.paused = MSG_ReadByte ();
			if (cl.paused)
			{
				CDAudio_Pause ();
				BGM_Pause ();
			}
			else
			{
				CDAudio_Resume ();
				BGM_Resume ();
			}
			break;

		case svc_signonnum:
			i = MSG_ReadByte ();
			if (i <= cls.signon)
				Host_Error ("Received signon %i when at %i", i, cls.signon);
			cls.signon = i;
			//johnfitz -- if signonnum==2, signon packet has been fully parsed, so check for excessive static ents and efrags
			if (i == 2)
			{
				if (cl.num_statics > 128)
					Con_DWarning ("%i static entities exceeds standard limit of 128 (max = %d).\n", cl.num_statics, MAX_STATIC_ENTITIES);
				R_CheckEfrags ();
			}
			//johnfitz
			CL_SignonReply ();
			break;

		case svc_killedmonster:
			cl.stats[STAT_MONSTERS]++;
			cl.statsf[STAT_MONSTERS] = cl.stats[STAT_MONSTERS];
			break;

		case svc_foundsecret:
			cl.stats[STAT_SECRETS]++;
			cl.statsf[STAT_SECRETS] = cl.stats[STAT_SECRETS];
			break;

		case svc_updatestat:
			i = MSG_ReadByte ();
			if (i < 0 || i >= MAX_CL_STATS)
				Sys_Error ("svc_updatestat: %i is invalid", i);
			cl.stats[i] = MSG_ReadLong ();
			cl.statsf[i] = cl.stats[i];
			break;

		case svcdp_updatestatbyte:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("CL_ParseServerMessage: unexpected svc_seq/updatestatbyte");
			i = MSG_ReadByte ();
			if (i < 0 || i >= MAX_CL_STATS)
				Sys_Error ("svcdp_updatestatbyte: %i is invalid", i);
			cl.stats[i] = MSG_ReadByte ();
			cl.statsf[i] = cl.stats[i];
			Sbar_Changed ();
			break;

		case svcfte_updatestatfloat:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("CL_ParseServerMessage: unexpected svcfte_updatestatfloat");
			i = MSG_ReadByte ();
			if (i < 0 || i >= MAX_CL_STATS)
				Sys_Error ("svcfte_updatestatfloat: %i is invalid", i);
			cl.statsf[i] = MSG_ReadFloat ();
			cl.stats[i] = cl.statsf[i];
			Sbar_Changed ();
			break;

		case svcfte_updatestatstring:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("CL_ParseServerMessage: unexpected svcfte_updatestatstring");
			i = MSG_ReadByte ();
			if (i < 0 || i >= MAX_CL_STATS)
				Sys_Error ("svcfte_updatestatstring: %i is invalid", i);
			free (cl.statss[i]);
			cl.statss[i] = strdup (MSG_ReadString ());
			Sbar_Changed ();
			break;

		case svcfte_updateentities:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("CL_ParseServerMessage: unexpected svcfte_updateentities");
			CLFTE_ParseEntitiesUpdate ();
			break;

		case svc_spawnstaticsound:
			CL_ParseStaticSound (1); //johnfitz -- added parameter
			break;

		case svc_cdtrack:
			cl.cdtrack = MSG_ReadByte ();
			cl.looptrack = MSG_ReadByte ();
			if ( (cls.demoplayback || cls.demorecording) && (cls.forcetrack != -1) )
				BGM_PlayCDtrack ((byte)cls.forcetrack, true);
			else
				BGM_PlayCDtrack ((byte)cl.cdtrack, true);
			break;

		case svc_intermission:
			cl.intermission = 1;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true;	// go to full screen
			V_RestoreAngles ();
			break;

		case svc_finale:
			cl.intermission = 2;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true;	// go to full screen
			//johnfitz -- log centerprints to console
			str = MSG_ReadString ();
			SCR_CenterPrint (str);
			Con_LogCenterPrint (str);
			//johnfitz
			V_RestoreAngles ();
			break;

		case svc_cutscene:
			cl.intermission = 3;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true;	// go to full screen
			//johnfitz -- log centerprints to console
			str = MSG_ReadString ();
			SCR_CenterPrint (str);
			Con_LogCenterPrint (str);
			//johnfitz
			V_RestoreAngles ();
			break;

		case svc_sellscreen:
			Cmd_ExecuteString ("help", src_command);
			break;

		//johnfitz -- new svc types
		case svc_skybox:
			Sky_LoadSkyBox (MSG_ReadString());
			break;

		case svc_bf:
			Cmd_ExecuteString ("bf", src_command);
			break;

		case svc_fog:
			Fog_ParseServerMessage ();
			break;

		case svc_spawnbaseline2: //PROTOCOL_FITZQUAKE
			i = MSG_ReadShort ();
			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline (CL_EntityNum(i), 2);
			break;

		case svc_spawnstatic2: //PROTOCOL_FITZQUAKE
			CL_ParseStatic (2);
			break;

		case svc_spawnstaticsound2: //PROTOCOL_FITZQUAKE
			CL_ParseStaticSound (2);
			break;
		//johnfitz

		case svcdp_precache:
			CL_ParseDPPrecache ();
			break;

		case svcdp_trailparticles:
			CL_ParseDPTrailParticles ();
			break;

		case svcdp_pointparticles:
			CL_ParseDPPointParticles (false);
			break;

		case svcdp_pointparticles1:
			CL_ParseDPPointParticles (true);
			break;

		//used by the 2021 rerelease
		case svc_achievement:
			str = MSG_ReadString();
			Con_DPrintf("Ignoring svc_achievement (%s)\n", str);
			break;
		case svc_localsound:
			CL_ParseLocalSound();
			break;
		}

		lastcmd = cmd; //johnfitz
	}
}
