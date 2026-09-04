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
#include "player_avatar.h"
#include "bgmusic.h"
#include "vr.h"
#include "vrik_codec.h"

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
extern double	v_punchangles_times[2];
extern cvar_t	v_gunkick;

//=============================================================================

static void CL_UpdatePunchAnglesFromStats (double stattime)
{
	vec3_t punch;

	if (v_gunkick.value == 1)
	{
		punch[0] = cl.stats[STAT_PUNCHANGLE_X];
		punch[1] = cl.stats[STAT_PUNCHANGLE_Y];
		punch[2] = cl.stats[STAT_PUNCHANGLE_Z];
	}
	else
	{
		punch[0] = cl.statsf[STAT_PUNCHANGLE_X];
		punch[1] = cl.statsf[STAT_PUNCHANGLE_Y];
		punch[2] = cl.statsf[STAT_PUNCHANGLE_Z];
	}

	if (cl.punchangle[0] != punch[0] ||
		cl.punchangle[1] != punch[1] ||
		cl.punchangle[2] != punch[2])
	{
		VectorCopy (v_punchangles[0], v_punchangles[1]);
		v_punchangles_times[1] = v_punchangles_times[0];
		v_punchangles_times[0] = stattime;
		VectorCopy (punch, cl.punchangle);
		VectorCopy (cl.punchangle, v_punchangles[0]);
		cl.punchtime = cl.time;
	}
}

static void CL_UpdateItemsFromStats (void)
{
	int i;
	unsigned int items, olditems;

	items = (unsigned int)cl.stats[STAT_ITEMS];
	olditems = (unsigned int)cl.items;
	if (olditems == items)
		return;

	Sbar_Changed ();
	for (i = 0; i < 32; i++)
	{
		unsigned int mask = 1u << i;
		if ((items & mask) && !(olditems & mask))
			cl.item_gettime[i] = cl.time;
	}
	cl.items = cl.stats[STAT_ITEMS];
}

static void CL_ApplyStatSideEffects (int stat)
{
	switch (stat)
	{
	case STAT_VIEWHEIGHT:
		cl.viewheight = cl.statsf[stat];
		break;
	case STAT_IDEALPITCH:
		cl.idealpitch = cl.statsf[stat];
		break;
	case STAT_VIEWZOOM:
		vid.recalc_refdef = true;
		break;
	default:
		break;
	}
}

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
enum { CL_SOUND_CHANNEL_VOICE = 2 };

static qboolean CL_IsExcludedLocalHapticSample (int channel, const char *sample)
{
	const char	*basename;
	qboolean	player_sample;

	if (!sample || !sample[0])
		return false;
	basename = strrchr (sample, '/');
	basename = basename ? basename + 1 : sample;
	player_sample = q_strcasestr (sample, "player/") != NULL ||
		q_strcasestr (sample, "players/") != NULL;

	/* Sound channels are only conventions and mods routinely use AUTO, BODY,
	 * ITEM, or custom channels for valid interaction feedback.  Filter the two
	 * unwanted semantic families by sample name instead.  The basename checks
	 * cover common player/foot1 and player/walk1 conventions without treating a
	 * weapon sound such as bazooka/step1 as locomotion. */
	return q_strcasestr (sample, "footstep") != NULL ||
		q_strcasestr (sample, "waterstep") != NULL ||
		q_strcasestr (sample, "steps/") != NULL ||
		!q_strncasecmp (basename, "foot", 4) ||
		!q_strncasecmp (basename, "walk", 4) ||
		((channel == CL_SOUND_CHANNEL_VOICE || player_sample) &&
		 (q_strcasestr (basename, "pain") != NULL ||
		  q_strcasestr (basename, "drown") != NULL ||
		  q_strcasestr (basename, "burn") != NULL));
}

static qboolean CL_IsLocalPlayerHapticSound (int ent, int channel,
	const char *sample)
{
	/*
	 * The packet entity is the stable network boundary: allow feedback for every
	 * local-player interaction except the explicit locomotion/damage sounds
	 * above, while excluding all world, monster, and remote-player sounds.
	 */
	return ent == cl.viewentity &&
		!CL_IsExcludedLocalHapticSample (channel, sample);
}

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

	if (vr_enabled.value && vr_haptic.value &&
		CL_IsLocalPlayerHapticSound (ent, channel,
			cl.sound_precache[sound_num] ? cl.sound_precache[sound_num]->name : NULL))
		VR_TriggerHaptic (1, 0.005f);

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
	case 0:
		if (index < MAX_MODELS)
		{
			cl.model_precache[index] = Mod_ForName (name, index == 1);
			if (index == 1)
				cl.entities[0].model = cl.worldmodel = cl.model_precache[1];
		}
		break;
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
	case 2:
		if (index < MAX_SOUNDS)
			cl.sound_precache[index] = S_PrecacheSound (name);
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
qboolean CL_ParseServerInfo (void)
{
	const char	*str;
	int		i;
	int		nummodels, numsounds;
	char	model_precache[MAX_MODELS][MAX_QPATH];
	char	sound_precache[MAX_SOUNDS][MAX_QPATH];
	unsigned int required_pext2;

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
	for (;;)
	{
		i = MSG_ReadLong ();
		if (i == PROTOCOL_FTE_PEXT1)
		{
			cl.protocol_pext1 = MSG_ReadLong ();
			if (cl.protocol_pext1 & ~PEXT1_ACCEPTED_CLIENT)
				Host_Error ("Server returned unsupported FTE1 protocol extensions %#x",
					cl.protocol_pext1 & ~PEXT1_ACCEPTED_CLIENT);
			continue;
		}
		if (i == PROTOCOL_FTE_PEXT2)
		{
			cl.protocol_pext2 = MSG_ReadLong ();
			if (cl.protocol_pext2 & ~PEXT2_ACCEPTED_CLIENT)
				Host_Error ("Server returned unsupported FTE2 protocol extensions %#x",
					cl.protocol_pext2 & ~PEXT2_ACCEPTED_CLIENT);
			continue;
		}
		break;
	}
	if (i != PROTOCOL_RMQ) {
		Con_Printf ("\n"); //because there's no newline after serverinfo print
		Host_Error ("Server returned version %i, expected %i (RMQ)", i,
			PROTOCOL_RMQ);
	}
	cl.protocol = i;

	{
		const unsigned int supportedflags = (PRFL_SHORTANGLE | PRFL_FLOATANGLE | PRFL_24BITCOORD | PRFL_FLOATCOORD | PRFL_EDICTSCALE | PRFL_INT32COORD);

		// Latest-code servers always use RMQ and always send protocol flags.
		cl.protocolflags = (unsigned int) MSG_ReadLong ();

		if (0 != (cl.protocolflags & (~supportedflags)))
		{
			Con_Warning("PROTOCOL_RMQ protocolflags %i contains unsupported flags\n", cl.protocolflags);
		}
	}

	required_pext2 = PEXT2_REQUIRED_LATEST;
	if ((cl.protocol_pext2 & required_pext2) != required_pext2)
		Host_Error ("Server is missing required FTE2 protocol extensions %#x",
			required_pext2 & ~cl.protocol_pext2);
	cl.requestresend = true;
	cl.ackframes_count = 1;
	cl.ackframes[0] = -1;

	q_strlcpy (cl.server_gamedir, MSG_ReadString (), sizeof(cl.server_gamedir));
	if (CL_MaybeSwitchServerGame(cl.server_gamedir))
		return false;

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
			/* A server-directed game switch can expose an incomplete or stale
			 * local add-on.  Treat that as a failed connection, not a fatal host
			 * error: leaving auto-reconnect armed otherwise retries the same bad
			 * precache indefinitely and looks like a client crash. */
			Con_Warning ("Cannot join server: model %s was not found in game %s.\n",
				model_precache[i], COM_GetGameNames(false));
			CL_AutoReconnect_Cancel ();
			CL_Disconnect ();
			SCR_EndLoadingPlaque ();
			M_Menu_Main_f ();
			return false;
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

	return true;
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
		InvalidateTraceLineCache ();
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
static unsigned int CLFTE_ReadDelta (unsigned int entnum, entity_state_t *news,
	const entity_state_t *olds, const entity_state_t *baseline);

void CL_ParseBaseline (entity_t *ent, int version) //johnfitz -- added argument
{
	int	i;
	int bits; //johnfitz

	if (version == 6)
	{
		CLFTE_ReadDelta (0, &ent->baseline, &nullentitystate, &nullentitystate);
		ent->netstate = ent->baseline;
		return;
	}

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
	if (bits & UF_ANGLESXZ)
	{
		news->angles[0] = MSG_ReadAngle (cl.protocolflags);
		news->angles[2] = MSG_ReadAngle (cl.protocolflags);
	}
	if (bits & UF_ANGLESY)
		news->angles[1] = MSG_ReadAngle (cl.protocolflags);
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
		if (predbits & UFP_VIEWANGLE)
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
			InvalidateTraceLineCache ();
			if (model)
			{
				if (model->synctype == ST_FRAMETIME)
					ent->syncbase = -cl.time;
				else if (model->synctype == ST_RAND)
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
		else if (model && model->synctype == ST_FRAMETIME &&
			ent->frame != ent->netstate.frame)
			ent->syncbase = -cl.time;
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

	ack = (cl.movemessages & ~0xffff) | (ack16 & 0xffff);
	if (ack > cl.movemessages)
		ack -= 0x10000;
	return ack;
}

static qboolean CL_UpdateMoveAck (int ack)
{
	if (ack < cl.ackedmovemessages)
	{
		cl.net_move_stale_acks++;
		return false;
	}
	if (ack == cl.ackedmovemessages)
		return true;

	if (ack > cl.ackedmovemessages)
		cl.net_move_acks++;

	cl.ackedmovemessages = ack;
	if (cl.qcvm.extglobals.servercommandframe)
		*cl.qcvm.extglobals.servercommandframe = cl.ackedmovemessages;
	return true;
}

static qboolean CL_ParseMoveAckPayload (void);

static void CLFTE_QueueAckFrame (int sequence)
{
	if (!cls.netcon)
		return;
	if (sequence < 0)
		return;
	if (cl.ackframes_count > 0 &&
		cl.ackframes[cl.ackframes_count - 1] == sequence)
		return;
	if (cl.ackframes_count < sizeof(cl.ackframes) / sizeof(cl.ackframes[0]))
	{
		cl.ackframes[cl.ackframes_count++] = sequence;
		if (cl.ackframes_count >= CL_ACKFRAME_FLUSH_THRESHOLD)
			CL_FlushAckFrames ();
		return;
	}

	// Keep the queued acks contiguous. Replacing the tail with a newer frame
	// makes the server infer artificial packet loss and resend a large range.
	cl.net_snapshot_ack_queue_overflows++;
	if (net_lagdebug.value && cl.net_snapshot_ack_queue_overflows <= 4)
		Con_Printf ("net_lagdebug: replacement ack queue full; preserving %u queued contiguous acks, dropping ack %d\n",
			cl.ackframes_count, sequence);
}

static void CLFTE_ParseEntitiesUpdate (void)
{
	int newnum;
	int i;
	qboolean removeflag;
	entity_t *ent;
	entity_t *viewent;
	float newtime;
	int frame_sequence;

	frame_sequence = cls.netcon ? NET_QSocketGetSequenceIn (cls.netcon) : -1;
	CLFTE_QueueAckFrame (frame_sequence);
	if (frame_sequence >= 0 && cl.net_snapshot_have &&
		frame_sequence > cl.net_snapshot_sequence + 1)
	{
		cl.net_snapshot_drops += frame_sequence - cl.net_snapshot_sequence - 1;
		if (net_lagdebug.value)
			Con_DPrintf ("net_lagdebug: replacement frame gap old=%d new=%d missing=%d\n",
				cl.net_snapshot_sequence, frame_sequence,
				frame_sequence - cl.net_snapshot_sequence - 1);
	}
	cl.net_snapshot_have = true;
	if (frame_sequence >= 0)
		cl.net_snapshot_sequence = frame_sequence;
	cl.net_snapshot_packets++;

	if (!CL_ParseMoveAckPayload ())
		return;

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
				InvalidateTraceLineCache ();
				cl.requestresend = false;
				continue;
			}
			CL_LerpDebugEntityEvent ("remove", newnum, ent);
			ent->update_type = false;
			ent->netstate = nullentitystate;
			ent->model = NULL;
			ent->lerpflags |= LERP_RESETMOVE | LERP_RESETANIM;
			InvalidateTraceLineCache ();
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
	if ((cl.protocol_pext2 & PEXT2_PREDINFO) && cl.viewentity > 0)
	{
		VectorCopy (cl.mvelocity[0], cl.mvelocity[1]);
		viewent = CL_EntityNum (cl.viewentity);
		for (i = 0; i < 3; i++)
			cl.mvelocity[0][i] = viewent->netstate.velocity[i] * (1.0f / 8.0f);
		cl.onground = (viewent->netstate.eflags & EFLAGS_ONGROUND) ? true : false;
		CL_UpdatePunchAnglesFromStats (newtime);
	}
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
	int		statval;

	bits = (unsigned short)MSG_ReadShort (); //johnfitz -- read bits here isntead of in CL_ParseServerMessage()

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (bits & SU_EXTEND1)
		bits |= (MSG_ReadByte() << 16);
	if (bits & SU_EXTEND2)
		bits |= (MSG_ReadByte() << 24);
	//johnfitz

	if (bits & SU_VIEWHEIGHT)
		statval = MSG_ReadChar ();
	else
		statval = DEFAULT_VIEWHEIGHT;
	cl.viewheight = statval;
	cl.stats[STAT_VIEWHEIGHT] = statval;
	cl.statsf[STAT_VIEWHEIGHT] = statval;

	if (bits & SU_IDEALPITCH)
		statval = MSG_ReadChar ();
	else
		statval = 0;
	cl.idealpitch = statval;
	cl.stats[STAT_IDEALPITCH] = statval;
	cl.statsf[STAT_IDEALPITCH] = statval;

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
		v_punchangles_times[1] = v_punchangles_times[0];
		v_punchangles_times[0] = cl.mtime[0];
		VectorCopy (cl.punchangle, v_punchangles[0]);
		cl.punchtime = cl.time;
	}
	//johnfitz

// [always sent]	if (bits & SU_ITEMS)
	i = MSG_ReadLong ();
	cl.stats[STAT_ITEMS] = i;
	cl.statsf[STAT_ITEMS] = i;
	CL_UpdateItemsFromStats ();

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
	InvalidateTraceLineCache ();
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

static qboolean CL_OfferVRIKProtocol(const char *command)
{
	const char *version;
	int offered_version;

	if (Q_strncmp(command, "vrik_protocol", 13) ||
		(command[13] != ' ' && command[13] != '\t'))
		return false;
	version = command + 13;
	while (*version == ' ' || *version == '\t')
		version++;
	if (*version == '3')
		offered_version = 3;
	else if (*version == '2')
		offered_version = 2;
	else
		return true;
	version++;
	/* CL_ParseStuffText calls extension handlers before removing the line
	 * terminator, so the server's canonical "//vrik_protocol 2\n" offer still
	 * has its newline here. */
	while (*version == ' ' || *version == '\t' || *version == '\r' ||
		*version == '\n')
		version++;
	if (*version || cl.vrik_cap_sent)
		return true;
	if (cls.message.cursize + 1 + (int)sizeof("vrik_cap 3") >
		cls.message.maxsize)
		return true;

	if (vrik_latch_protocol_version((uint8_t)offered_version,
		&cl.vrik_cap_sent, &cl.vrik_protocol_version) != VRIK_CODEC_OK)
		return true;
	cl.vrik_protocol_offered = true;
	MSG_WriteByte(&cls.message, clc_stringcmd);
	MSG_WriteString(&cls.message,
		offered_version >= VRIK_PROTOCOL_VERSION ? "vrik_cap 3" : "vrik_cap 2");
	Con_DPrintf("VRIK: negotiated protocol %d with server\n", offered_version);
	return true;
}

static qboolean CL_OfferAvatarProtocol(const char *command)
{
	if (Q_strncmp(command, "avatar_protocol", 15) ||
		(command[15] != ' ' && command[15] != '\t'))
		return false;
	/* Treat malformed offers as consumed extension traffic rather than
	 * executing a server-supplied console command. */
	if (!PlayerAvatar_LatchProtocolOffer(command,
		&cl.avatar_protocol_offered, &cl.avatar_cap_pending,
		cl.avatar_cap_sent))
		return true;
	cl.avatar_protocol_version = PLAYER_AVATAR_PROTOCOL_VERSION;
	Con_DPrintf("Avatar: negotiated protocol %d with server\n",
		PLAYER_AVATAR_PROTOCOL_VERSION);
	return true;
}

static qboolean CL_ParseAvatarSlot(const char *command)
{
	int slot;
	int id;

	if (Q_strncmp(command, "avatar_slot", 11) ||
		(command[11] != ' ' && command[11] != '\t'))
		return false;
	if (!cl.avatar_protocol_offered ||
		cl.avatar_protocol_version != PLAYER_AVATAR_PROTOCOL_VERSION)
		return true;
	if (PlayerAvatar_ParseSlotCommand(command, &slot, &id))
		cl.avatar_ids[slot] = (unsigned char)id;
	return true;
}

static qboolean CL_VRIKPoseWithinRootLocalLimit(const vrik_codec_pose_t *pose)
{
	int target;
	double max_squared = (double)VRIK_MAX_ROOT_LOCAL_OFFSET *
		(double)VRIK_MAX_ROOT_LOCAL_OFFSET;

	if (!pose)
		return false;
	for (target = 0; target < VRIK_TARGET_COUNT; ++target)
		if (pose->present_mask & VRIK_TARGET_BIT(target))
		{
			double x = pose->targets[target].position[0];
			double y = pose->targets[target].position[1];
			double z = pose->targets[target].position[2];
			if (x * x + y * y + z * z > max_squared)
				return false;
		}
	return true;
}

static void CL_VRIKV2ToLegacyPose(const vrik_v2_pose_t *source,
	vrik_pose_t *destination)
{
	int target;

	Q_memset(destination, 0, sizeof(*destination));
	destination->sequence = source->sequence;
	destination->flags = source->flags;
	destination->body_yaw = source->body_yaw;
	for (target = 0; target < VRIK_TRACKER_COUNT; ++target)
	{
		VectorCopy(source->targets[target].position,
			destination->position[target]);
		VectorCopy(source->targets[target].orientation,
			destination->orientation[target]);
	}
	VectorCopy(source->aim_orientation, destination->aim_orientation);
}

static void CL_VRIKV3ToLegacyPose(const vrik_codec_pose_t *source,
	vrik_pose_t *destination)
{
	int target;

	Q_memset(destination, 0, sizeof(*destination));
	destination->sequence = source->sequence;
	if (!(source->flags & VRIK_V3_FLAG_ACTIVE) ||
		!(source->tracked_mask & VRIK_TARGET_BIT(VRIK_TARGET_HEAD)))
		return;
	destination->flags = VRIK_FLAG_ACTIVE | VRIK_FLAG_HEAD_TRACKED;
	if (source->flags & VRIK_V3_FLAG_DOMINANT_LEFT)
		destination->flags |= VRIK_FLAG_DOMINANT_LEFT;
	for (target = 0; target < VRIK_TRACKER_COUNT; ++target)
	{
		if (source->tracked_mask & VRIK_TARGET_BIT(target))
			destination->flags |= (unsigned char)(VRIK_FLAG_HEAD_TRACKED << target);
		VectorCopy(source->targets[target].position,
			destination->position[target]);
		VectorCopy(source->targets[target].orientation,
			destination->orientation[target]);
	}
	destination->body_yaw = source->body_yaw;
	VectorCopy(source->aim_orientation, destination->aim_orientation);
}

static qboolean CL_ParseVRIKPose(void)
{
	vrik_pose_t pose;
	vrik_v2_pose_t pose_v2;
	vrik_codec_pose_t pose_v3;
	entity_t *ent;
	qboolean newstream;
	int entitynum;
	unsigned int generation;
	int body_bytes;
	size_t consumed;
	vrik_codec_status_t status;

	if (net_message.cursize - msg_readcount < 2 + 4)
	{
		msg_badread = true;
		return false;
	}

	entitynum = (unsigned short)MSG_ReadShort();
	generation = (unsigned int)MSG_ReadLong();
	if (msg_badread || !cl.vrik_protocol_offered ||
		!generation || entitynum < 1 || entitynum > cl.maxclients || entitynum >= cl.max_edicts ||
		entitynum >= cl.num_entities)
	{
		msg_badread = true;
		return false;
	}
	Q_memset(&pose_v3, 0, sizeof(pose_v3));
	if (cl.vrik_protocol_version >= VRIK_PROTOCOL_VERSION)
	{
		body_bytes = MSG_ReadByte();
		if (msg_badread || body_bytes < 0 || body_bytes > VRIK_V3_MAX_BODY_BYTES ||
			net_message.cursize - msg_readcount < body_bytes)
		{
			msg_badread = true;
			return false;
		}
		status = vrik_v3_decode(net_message.data + msg_readcount,
			(size_t)body_bytes, &pose_v3, &consumed);
		/* The declared length is authoritative for framing.  Consume it even
		 * when this sample is rejected, preserving later commands. */
		msg_readcount += body_bytes;
		if (status != VRIK_CODEC_OK || consumed != (size_t)body_bytes ||
			!CL_VRIKPoseWithinRootLocalLimit(&pose_v3))
			return true;
		CL_VRIKV3ToLegacyPose(&pose_v3, &pose);
	}
	else
	{
		if (net_message.cursize - msg_readcount < VRIK_POSE_WIRE_BYTES)
		{
			msg_badread = true;
			return false;
		}
		status = vrik_v2_decode(net_message.data + msg_readcount,
			VRIK_POSE_WIRE_BYTES, &pose_v2, &consumed);
		msg_readcount += VRIK_POSE_WIRE_BYTES;
		if (status != VRIK_CODEC_OK || consumed != VRIK_POSE_WIRE_BYTES ||
			vrik_v2_validate_legacy_pose(&pose_v2) != VRIK_CODEC_OK)
			return true;
		if (vrik_v2_to_normalized(&pose_v2, &pose_v3) != VRIK_CODEC_OK)
			return true;
		CL_VRIKV2ToLegacyPose(&pose_v2, &pose);
	}

	ent = &cl.entities[entitynum];
	newstream = !ent->vrik_sequence_valid || ent->vrik_generation != generation;
	if (newstream)
	{
		ent->vrik_pose_count = 0;
		ent->vrik_sequence_valid = false;
		ent->vrik_generation = generation;
	}
	else if (ent->vrik_sequence_valid &&
		!vrik_sequence_is_newer(pose.sequence, ent->vrik_last_sequence))
		return true;
	if (ent->vrik_pose_count > 0)
	{
		ent->vrik_poses[1] = ent->vrik_poses[0];
		ent->vrik_v3_poses[1] = ent->vrik_v3_poses[0];
		ent->vrik_pose_times[1] = ent->vrik_pose_times[0];
	}
	ent->vrik_poses[0] = pose;
	ent->vrik_v3_poses[0] = pose_v3;
	ent->vrik_pose_times[0] = realtime;
	ent->vrik_last_sequence = pose.sequence;
	ent->vrik_sequence_valid = true;
	if (ent->vrik_pose_count < 2)
		ent->vrik_pose_count++;
	if (newstream && (pose.flags & VRIK_FLAG_ACTIVE))
		Con_DPrintf("VRIK: receiving active pose for entity %d generation %u\n",
			entitynum, generation);
	return true;
}

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
			handled = CL_OfferAvatarProtocol(cl.stuffcmdbuf + 2) ||
				CL_ParseAvatarSlot(cl.stuffcmdbuf + 2) ||
				CL_OfferVRIKProtocol(cl.stuffcmdbuf + 2) ||
				Cmd_ExecuteString(cl.stuffcmdbuf+2, src_server);
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
static void CL_ApplyMoveAck (int ack16)
{
	int ack;

	ack = CL_ExpandMoveAck16 (ack16);
	CL_UpdateMoveAck (ack);
}

static qboolean CL_ParseMoveAckPayload (void)
{
	int ack16;
	int flags;
	int authority;
	int mode_epoch;
	int discontinuity_epoch;
	int reason;

	if (net_message.cursize - msg_readcount < 2)
	{
		msg_badread = true;
		return false;
	}

	ack16 = MSG_ReadShort () & 0xffff;
	CL_ApplyMoveAck (ack16);
	if (!(cl.protocol_pext2 & PEXT2_EXPLICITCMDMSEC))
		return !msg_badread;

	if (net_message.cursize - msg_readcount < 7)
	{
		msg_badread = true;
		return false;
	}

	flags = MSG_ReadByte ();
	authority = MSG_ReadByte ();
	mode_epoch = MSG_ReadShort () & 0xffff;
	discontinuity_epoch = MSG_ReadShort () & 0xffff;
	reason = MSG_ReadByte ();
	if (flags & ~(MOVEACK_FLAG_AUTHORITATIVE | MOVEACK_FLAG_PREDICTION_ALLOWED |
		MOVEACK_FLAG_DISCONTINUITY) ||
		authority < MOVE_AUTHORITY_UNKNOWN ||
		authority > MOVE_AUTHORITY_PMOVE_QC_COMMAND)
	{
		msg_badread = true;
		return false;
	}

	cl.move_ack_authority = (move_authority_t)authority;
	cl.move_ack_prediction_allowed =
		(flags & MOVEACK_FLAG_PREDICTION_ALLOWED) != 0;
	cl.move_ack_mode_epoch = (unsigned short)mode_epoch;
	cl.move_ack_discontinuity_epoch = (unsigned short)discontinuity_epoch;
	cl.move_ack_discontinuity_reason = (unsigned char)reason;
	return !msg_badread;
}

static void CL_ParseMoveAck (void)
{
	CL_ParseMoveAckPayload ();
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

			CL_UpdateItemsFromStats ();

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
			cl.fixangle = false;
			break;

		case svc_moveack:
			CL_ParseMoveAck ();
			break;

		case svc_vrikpose:
			SHOWNET("svc_vrikpose");
			if (!CL_ParseVRIKPose())
				Host_Error("CL_ParseServerMessage: malformed VRIK pose");
			break;

		case svcdp_csqcentities:
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
			if (i != PROTOCOL_RMQ)
				Host_Error ("Server returned version %i, expected %i (RMQ)", i,
					PROTOCOL_RMQ);
			cl.protocol = i;
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
			if (!CL_ParseServerInfo ())
				return;
			vid.recalc_refdef = true;	// leave intermission full screen
			break;

		case svc_setangle:
			for (i=0 ; i<3 ; i++)
				cl.viewangles[i] = MSG_ReadAngle (cl.protocolflags);
			cl.fixangle = true;
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
			CL_ApplyStatSideEffects (i);
			Sbar_Changed ();
			break;

		case svcdp_updatestatbyte:
			i = MSG_ReadByte ();
			if (i < 0 || i >= MAX_CL_STATS)
				Sys_Error ("svcdp_updatestatbyte: %i is invalid", i);
			cl.stats[i] = MSG_ReadByte ();
			cl.statsf[i] = cl.stats[i];
			CL_ApplyStatSideEffects (i);
			Sbar_Changed ();
			break;

		case svcfte_updatestatfloat:
			i = MSG_ReadByte ();
			if (i < 0 || i >= MAX_CL_STATS)
				Sys_Error ("svcfte_updatestatfloat: %i is invalid", i);
			cl.statsf[i] = MSG_ReadFloat ();
			cl.stats[i] = cl.statsf[i];
			CL_ApplyStatSideEffects (i);
			Sbar_Changed ();
			break;

		case svcfte_updatestatstring:
			i = MSG_ReadByte ();
			if (i < 0 || i >= MAX_CL_STATS)
				Sys_Error ("svcfte_updatestatstring: %i is invalid", i);
			free (cl.statss[i]);
			cl.statss[i] = strdup (MSG_ReadString ());
			Sbar_Changed ();
			break;

		case svcfte_updateentities:
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

		case svcfte_spawnstatic2:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_spawnstatic2 but replacement deltas are not active");
			CL_ParseStatic (6);
			break;

		case svcfte_spawnbaseline2:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_spawnbaseline2 but replacement deltas are not active");
			i = MSG_ReadEntity (cl.protocol_pext2);
			CL_ParseBaseline (CL_EntityNum (i), 6);
			break;

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

		/* Some negotiated extension opcodes live above svc_strings[].  Keep
		 * malformed-packet diagnostics from indexing that table out of bounds. */
		lastcmd = cmd < (int)NUM_SVC_STRINGS ? cmd : svc_bad; //johnfitz
	}
}
