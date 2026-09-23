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
#include "pmove.h"
#include "vr_input.h"

#ifdef QSVR_SHADOW_TRACE
#include <stdio.h>
#include <stdlib.h>
#endif

// we need to declare some mouse variables here, because the menu system
// references them even when on a unix system.

// these two are not intended to be set directly
cvar_t cl_name = {"_cl_name", "player", CVAR_ARCHIVE_GAME | CVAR_USERINFO};

cvar_t cl_topcolor = {"topcolor", "0", CVAR_ARCHIVE_GAME | CVAR_USERINFO};
cvar_t cl_bottomcolor = {"bottomcolor", "0", CVAR_ARCHIVE_GAME | CVAR_USERINFO};

cvar_t cl_shownet = {"cl_shownet", "0", CVAR_NONE}; // can be 0, 1, or 2
cvar_t cl_nolerp = {"cl_nolerp", "0", CVAR_NONE};
cvar_t cl_nopred = {"cl_nopred", "0", CVAR_ARCHIVE};

cvar_t cfg_unbindall = {"cfg_unbindall", "1", CVAR_ARCHIVE_GAME};

cvar_t lookspring = {"lookspring", "0", CVAR_NONE};
cvar_t lookstrafe = {"lookstrafe", "0", CVAR_NONE};
cvar_t sensitivity = {"sensitivity", "3", CVAR_ARCHIVE_GAME};

cvar_t m_pitch = {"m_pitch", "0.022", CVAR_ARCHIVE_GAME};
cvar_t m_yaw = {"m_yaw", "0.022", CVAR_ARCHIVE_GAME};
cvar_t m_forward = {"m_forward", "1", CVAR_ARCHIVE_GAME};
cvar_t m_side = {"m_side", "0.8", CVAR_ARCHIVE_GAME};

cvar_t cl_maxpitch = {"cl_maxpitch", "90", CVAR_ARCHIVE_GAME};	// johnfitz -- variable pitch clamping
cvar_t cl_minpitch = {"cl_minpitch", "-90", CVAR_ARCHIVE_GAME}; // johnfitz -- variable pitch clamping

cvar_t cl_startdemos = {"cl_startdemos", "1", CVAR_ARCHIVE};
cvar_t cl_confirmquit = {"cl_confirmquit", "0", CVAR_ARCHIVE};

client_static_t cls;
client_state_t	cl;
// FIXME: put these on hunk?
lightstyle_t	cl_lightstyle[MAX_LIGHTSTYLES];
dlight_t		cl_dlights[MAX_DLIGHTS];

int		   cl_numvisedicts;
int		   cl_numvisedicts_alpha_overwater;
int		   cl_numvisedicts_alpha_underwater;
int		   cl_maxvisedicts;
entity_t **cl_visedicts;
entity_t **cl_visedicts_alpha;

extern cvar_t r_lerpmodels, r_lerpmove; // johnfitz
extern cvar_t r_lerpturn;				// Danni
extern float  host_netinterval;			// Spike

qboolean needs_relink;

void CL_ClearTrailStates (void)
{
	int i;
	for (i = 0; i < cl.num_statics; i++)
	{
		PScript_DelinkTrailstate (&(cl.static_entities[i]->trailstate));
		PScript_DelinkTrailstate (&(cl.static_entities[i]->emitstate));
	}
	for (i = 0; i < cl.max_edicts; i++)
	{
		PScript_DelinkTrailstate (&(cl.entities[i].trailstate));
		PScript_DelinkTrailstate (&(cl.entities[i].emitstate));
	}
	for (i = 0; i < MAX_BEAMS; i++)
	{
		PScript_DelinkTrailstate (&(cl_beams[i].trailstate));
	}
}

void CL_FreeState (void)
{
	int i;
	for (i = 0; i < MAX_CL_STATS; i++)
		Mem_Free (cl.statss[i]);
	PR_ClearProgs (&cl.qcvm);
	// Free entity BLASes before freeing entities
	if (cl.entities)
	{
		for (i = 0; i < cl.max_edicts; i++)
			R_FreeEntityBLAS (&cl.entities[i]);
	}
	Mem_Free (cl.entities);
	for (i = 0; i < cl.num_statics; i++)
		R_FreeEntityBLAS (cl.static_entities[i]);
	for (i = 0; i < cl.num_statics; i += 64)
		Mem_Free (cl.static_entities[i]);
	Mem_Free (cl.static_entities);
	Mem_Free (cl.scores);
	for (i = 0; i < MAX_PARTICLETYPES; ++i)
		Mem_Free (cl.particle_precache[i].name);
	for (i = 0; i < cl.num_efragallocs; ++i)
		Mem_Free (cl.efrag_allocs[i]);
	Mem_Free (cl.efrag_allocs);
	memset (&cl, 0, sizeof (cl));
	PMCL_ClearMoveVars ();
}

// Pinned prediction presentation reset; epoch/replay state remains separately owned.
void CL_ResetPredictionSmoothing (void)
{
	VectorCopy (vec3_origin, cl.prediction_error);
	cl.prediction_error_time = 0;
	cl.prediction_error_sequence = -1;
}

/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState (void)
{
	V_ResetTrackedAim ();
	if (!sv.active)
		Host_ClearMemory ();

	// wipe the entire cl structure
	CL_FreeState ();
	/* A new map renegotiates optional VRIK before its pose stream starts. */
	CL_ResetPredictionSmoothing ();
	cl.vr_gorilla_state_sequence = -1;

	SZ_Clear (&cls.message);

	// clear other arrays
	memset (cl_dlights, 0, sizeof (cl_dlights));
	memset (cl_lightstyle, 0, sizeof (cl_lightstyle));
	memset (cl_temp_entities, 0, sizeof (cl_temp_entities));
	memset (cl_beams, 0, sizeof (cl_beams));

	// johnfitz -- cl_entities is now dynamically allocated
	cl.max_edicts = CLAMP (MIN_EDICTS, (int)max_edicts.value, MAX_EDICTS);
	cl.entities = (entity_t *)Mem_Alloc (cl.max_edicts * sizeof (entity_t));
	// johnfitz

	cl.viewent.netstate = nullentitystate;
	// Spike -- this stuff needs to get reset to defaults.
	PScript_Shutdown ();
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
	CL_ResetVRIKState ();
	cls.legacy_qsvr = 0;
	cls.offered_qsvr = 0;
	cl.protocol_qsvr = 0;
	cl.move_snapshot_valid = false;
	V_ResetTrackedAim ();
	if (key_dest == key_message)
		Key_EndChat (); // don't get stuck in chat mode

	// stop sounds (especially looping!)
	S_StopAllSounds (true, false);
	BGM_Stop ();
	CDAudio_Stop ();

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
		cls.netcon = NULL;

		cls.state = ca_disconnected;
		if (sv.active)
			Host_ShutdownServer (false);
	}

	cls.demoplayback = cls.timedemo = false;
	cls.demopaused = false;
	cls.signon = 0;
	cls.netcon = NULL;
	cl.intermission = 0;
	cl.worldmodel = NULL;
	cl.sendprespawn = false;
	SCR_CenterPrintClear ();
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
void CL_EstablishConnection (const char *host, unsigned int legacy_qsvr)
{
	if (cls.state == ca_dedicated)
		return;

	if (cls.demoplayback)
		return;

	CL_Disconnect ();
	if (legacy_qsvr && legacy_qsvr != QSVR_PROTOCOL_PINNED)
		Host_Error ("Unsupported legacy Quakespasm VR layout %u", legacy_qsvr);
	cls.legacy_qsvr = legacy_qsvr;

	cls.netcon = NET_Connect (host);
	if (!cls.netcon)
		Host_Error ("CL_Connect: connect failed");
	Con_DPrintf ("CL_EstablishConnection: connected to %s\n", host);

	cls.demonum = -1; // not in the demo loop now
	cls.state = ca_connected;
	cls.signon = 0;						   // need all the signon messages before playing
	MSG_WriteByte (&cls.message, clc_nop); // NAT Fix from ProQuake
}

void CL_SendInitialUserinfo (void *ctx, const char *key, const char *val)
{
	if (*key == '*')
		return; // servers don't like that sort of userinfo key
	if (!strcmp (key, "name"))
		return; // already unconditionally sent earlier.
	MSG_WriteByte (&cls.message, clc_stringcmd);
	MSG_WriteString (&cls.message, va ("setinfo \"%s\" \"%s\"\n", key, val));
}
/*
=====================
CL_SignonReply

An svc_signonnum has been received, perform a client side setup
=====================
*/
void CL_SignonReply (void)
{
	char str[8192];

	Con_DPrintf ("CL_SignonReply: %i\n", cls.signon);

	switch (cls.signon)
	{
	case 1:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va ("name \"%s\"\n", cl_name.string));

		cl.sendprespawn = true;
		break;

	case 2:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va ("color %i %i\n", (int)cl_topcolor.value, (int)cl_bottomcolor.value));

		if (*cl.serverinfo)
			Info_Enumerate (cls.userinfo, CL_SendInitialUserinfo, NULL);

		MSG_WriteByte (&cls.message, clc_stringcmd);
		q_snprintf (str, sizeof (str), "spawn %s", cls.spawnparms);
		MSG_WriteString (&cls.message, str);
		break;

	case 3:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "begin");
		break;

	case 4:
		SCR_EndLoadingPlaque (); // allow normal screen updates
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
	char str[1024];

	if (cls.demonum == -1)
		return; // don't play demos

	if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS)
	{
		cls.demonum = 0;
		if (!cls.demos[cls.demonum][0])
		{
			Con_Printf ("No demos listed with startdemos\n");
			cls.demonum = -1;
			CL_Disconnect ();
			return;
		}
	}

	SCR_BeginLoadingPlaque ();

	q_snprintf (str, sizeof (str), "playdemo %s\n", cls.demos[cls.demonum]);
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
	entity_t *ent;
	int		  i;

	if (cls.state != ca_connected)
		return;

	for (i = 0, ent = cl.entities; i < cl.num_entities; i++, ent++)
	{
		Con_Printf ("%3i:", i);
		if (!ent->model)
		{
			Con_Printf ("EMPTY\n");
			continue;
		}
		Con_Printf (
			"%s:%2i  (%5.1f,%5.1f,%5.1f) [%5.1f %5.1f %5.1f]\n", ent->model->name, ent->frame, ent->origin[0], ent->origin[1], ent->origin[2], ent->angles[0],
			ent->angles[1], ent->angles[2]);
	}
}

/*
===============
CL_AllocDlight

===============
*/
dlight_t *CL_AllocDlight (int key)
{
	int		  i;
	dlight_t *dl;

	// first look for an exact key match
	if (key)
	{
		dl = cl_dlights;
		for (i = 0; i < MAX_DLIGHTS; i++, dl++)
		{
			if (dl->key == key)
			{
				memset (dl, 0, sizeof (*dl));
				dl->key = key;
				dl->color[0] = dl->color[1] = dl->color[2] = 1; // johnfitz -- lit support via lordhavoc
				dl->cone_cos = -2.0f;
				dl->kex_intensity = 0.0f;
				return dl;
			}
		}
	}

	// then look for anything else
	dl = cl_dlights;
	for (i = 0; i < MAX_DLIGHTS; i++, dl++)
	{
		if (dl->die < cl.time)
		{
			memset (dl, 0, sizeof (*dl));
			dl->key = key;
			dl->color[0] = dl->color[1] = dl->color[2] = 1; // johnfitz -- lit support via lordhavoc
			dl->cone_cos = -2.0f;
			dl->kex_intensity = 0.0f;
			return dl;
		}
	}

	dl = &cl_dlights[0];
	memset (dl, 0, sizeof (*dl));
	dl->key = key;
	dl->color[0] = dl->color[1] = dl->color[2] = 1; // johnfitz -- lit support via lordhavoc
	dl->cone_cos = -2.0f;
	return dl;
}

/*
===============
CL_DecayLights

===============
*/
void CL_DecayLights (void)
{
	int		  i;
	dlight_t *dl;
	float	  time;

	time = cl.time - cl.oldtime;
	if (time < 0)
		return;

	dl = cl_dlights;
	for (i = 0; i < MAX_DLIGHTS; i++, dl++)
	{
		if (dl->die < cl.time || !dl->radius)
			continue;

		dl->radius -= time * dl->decay;
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
float CL_LerpPoint (void)
{
	float f, frac;

	f = cl.mtime[0] - cl.mtime[1];

	if (!f || cls.timedemo || (sv.active && !host_netinterval))
	{
		cl.time = cl.mtime[0];
		return 1;
	}

	if (f > 0.1) // dropped packet, or start of demo
	{
		cl.mtime[1] = cl.mtime[0] - 0.1;
		f = 0.1;
	}

	frac = (cl.time - cl.mtime[1]) / f;

	if (frac < 0)
	{
		if (frac < -0.01)
			cl.time = cl.mtime[1];
		frac = 0;
	}
	else if (frac > 1)
	{
		if (frac > 1.01)
			cl.time = cl.mtime[0];
		frac = 1;
	}

	// johnfitz -- better nolerp behavior
	if (cl_nolerp.value)
		return 1;
	// johnfitz

	return frac;
}

/*
===============
CL_ReplayPlayerMovement

QSS-M 03a498aa client replay, adapted at the existing vkQuake owners.
Complete commands come from the inherited command journal; collision, movevar
selection and simulation stay with PMCL_AddEntities/PMCL_SetMoveVars/PM_PlayerMove.
After the journal, a disposable input preview supplies QSS-M's partial command
without consuming device state or advancing command clocks.
===============
*/
static void CL_ResetReplayPropagation (void)
{
	memset (cl.move_replay_propagate_sequence, 0, sizeof(cl.move_replay_propagate_sequence));
	memset (cl.move_replay_propagate_waterjumptime, 0, sizeof(cl.move_replay_propagate_waterjumptime));
}

static void CL_ObservePrivateReplayMetadata (void)
{
	if (!cl.move_replay_private_metadata_valid ||
		cl.move_replay_private_prediction_allowed != cl.move_ack_prediction_allowed ||
		cl.move_replay_private_authority != cl.move_ack_authority ||
		cl.move_replay_private_mode_epoch != cl.move_ack_mode_epoch ||
		cl.move_replay_private_discontinuity_epoch != cl.move_ack_discontinuity_epoch)
	{
		CL_ResetReplayPropagation ();
		cl.move_replay_private_metadata_valid = true;
		cl.move_replay_private_prediction_allowed = cl.move_ack_prediction_allowed;
		cl.move_replay_private_authority = cl.move_ack_authority;
		cl.move_replay_private_mode_epoch = cl.move_ack_mode_epoch;
		cl.move_replay_private_discontinuity_epoch = cl.move_ack_discontinuity_epoch;
	}
}

static int CL_ReplayPMoveType (int movetype)
{
	switch (movetype & 63)
	{
	case MOVETYPE_WALK:
		return PM_NORMAL;
	case MOVETYPE_TOSS:
	case MOVETYPE_BOUNCE:
	case MOVETYPE_GIB:
		/* QSS-M's bare MOVETYPE_TOSS break can retain a stale pm_type.
		 * The pinned VR server treats these gravity/dead-body modes as dead. */
		return PM_DEAD;
	case MOVETYPE_FLY:
		return PM_FLY;
	case MOVETYPE_NOCLIP:
		return PM_SPECTATOR;
	default:
		return PM_NONE;
	}
}

static qboolean CL_ReplayHistoryAvailable (int *startseq, int endseq)
{
	int seq;
	int first = cl.ackedmovemessages + 1;
	int oldest = cl.movemessages - countof(cl.movecmds);

	if (first < 2)
		first = 2;
	if (oldest < 2)
		oldest = 2;
	if (cl.protocol_qsvr == QSVR_PROTOCOL_PINNED && first < oldest)
		return false;
	/* Preserve QSS-M's public "lost is lost" policy: resume at the oldest
	 * retained command rather than reading an overwritten slot. Private replay
	 * must retain every requested command after its authoritative baseline ACK. */
	if (first < oldest)
		first = oldest;
	if (first > cl.movemessages)
		return false;

	for (seq = first; seq < endseq; seq++)
		if (cl.movecmds[seq & MOVECMDS_MASK].sequence != (unsigned int)seq)
			return false;

	*startseq = first;
	return true;
}

static qboolean CL_ReplayCanTrustGorilla (void)
{
	return cl.protocol_qsvr == QSVR_PROTOCOL_PINNED &&
		cl.vr_gorilla_motion_generation_valid && cl.vr_gorilla_trusted_cap_sent &&
		cl.vr_gorilla_allowed && cl.move_ack_prediction_allowed &&
		cl.move_ack_authority == MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT;
}

static qboolean CL_SetupReplayGorilla (int startseq)
{
	const usercmd_t *cmd;
	qboolean raw_replay = false;
	int seq;
	int hand;

	if (CL_ReplayCanTrustGorilla ())
	{
		pmove.gorilla_allowed = true;
		return true;
	}
	if (!cl.vr_gorilla_supported || !cl.vr_gorilla_allowed || !cl.vr_gorilla_state_valid)
		return true;
	if (!cl.vr_gorilla_state.initialized)
		return true;
	/* The pinned client additionally gates raw-state restoration on
	 * VR_GorillaActive().  That tracked producer/activity owner is not present
	 * in this slice.  A journaled raw Gorilla command is sufficient provenance
	 * for replay; protocol permission alone is deliberately not. */
	for (seq = startseq; seq < cl.movemessages; seq++)
	{
		cmd = &cl.movecmds[seq & MOVECMDS_MASK];
		if (cmd->vr_active && cmd->vr_handpos_relative && cmd->vr_gorilla.flags)
		{
			raw_replay = true;
			break;
		}
	}
	if (!raw_replay)
		return true;
	if (cl.vr_gorilla_state.initialized &&
		cl.vr_gorilla_state_sequence != cl.ackedmovemessages)
		return false;
	if (cl.vr_gorilla_state_sequence != cl.ackedmovemessages)
		return true;

	pmove.gorilla = cl.vr_gorilla_state;
	pmove.gorilla_allowed = cl.vr_gorilla_state.initialized != 0;
	for (hand = 0; hand < 2; hand++)
	{
		int surface = pmove.gorilla.surface[hand];
		if (surface > 0 && (surface >= cl.num_entities ||
			cl.entities[surface].forcelink ||
			cl.entities[surface].netstate.solidsize != ES_SOLID_BSP ||
			cl.entities[surface].netstate.modelindex != pmove.gorilla.surface_model[hand]))
		{
			memset (&pmove.gorilla, 0, sizeof(pmove.gorilla));
			break;
		}
	}
	return true;
}

static void CL_PrepareReplayCommand (usercmd_t *dst, const usercmd_t *src, qboolean private_replay)
{
	*dst = *src;
	if (!private_replay)
	{
		/* Public PREDINFO does not serialize these private movement fields. */
		dst->msec = 0;
		dst->vr_active = false;
		VectorClear (dst->vr_roomscalemove);
		memset (&dst->vr_gorilla, 0, sizeof(dst->vr_gorilla));
		memset (&dst->vr_gorilla_motion, 0, sizeof(dst->vr_gorilla_motion));
		return;
	}

	if (dst->vr_gorilla_motion.flags &&
		(!CL_ReplayCanTrustGorilla () ||
		 dst->vr_gorilla_motion.generation != cl.vr_gorilla_motion_generation))
	{
		/* Authored motion is generation-bound; stale authored motion must not
		 * silently become a raw-palm fallback. */
		memset (&dst->vr_gorilla_motion, 0, sizeof(dst->vr_gorilla_motion));
		memset (&dst->vr_gorilla, 0, sizeof(dst->vr_gorilla));
	}
}

static void CL_PrepareReplayPreview (usercmd_t *cmd, qboolean private_replay)
{
	double elapsed;

	CL_PreviewMove (cmd);
	cmd->seconds = 0;
	cmd->msec = 0;

	if (private_replay)
	{
		if (!cl.move_msec_sample_valid)
			return;
		elapsed = realtime - cl.move_msec_sample_time;
		if (!isfinite (elapsed) || elapsed < 0)
			elapsed = 0;
		if (elapsed > 0.125)
			elapsed = 0.125;
		cmd->seconds = (float)elapsed;
		if (elapsed > 0)
			cmd->msec = (unsigned char)CLAMP (1, (int)(elapsed * 1000.0 + 0.5), 125);
		return;
	}

	// Sending clears this duration together with device input. Recomputing it
	// after server-time advancement would replay cleared axes for positive time.
	elapsed = cl.pendingcmd.seconds;
	if (!isfinite (elapsed) || elapsed < 0)
		elapsed = 0;
	if (elapsed > 0.5)
		elapsed = 0.5;
	cmd->seconds = (float)elapsed;
}

typedef struct
{
	vec3_t origin;
	vec3_t velocity;
	qboolean onground;
	qboolean inwater;
	int target_sequence;
	float jump_secs;
} cl_replay_result_t;

#ifdef QSVR_SHADOW_TRACE
#define CL_SHADOW_TRACE_MAX_RECORDS 4096
#define CL_SHADOW_TRACE_MAX_ROOMSCALE 16.0f

static int cl_shadow_trace_last_target = -1;
static unsigned int cl_shadow_trace_records;
static unsigned int cl_shadow_trace_failures;
static qboolean cl_shadow_trace_io_failed;

static qboolean CL_ShadowTraceEnabled (const char **path)
{
	*path = getenv ("QSVR_SHADOW_TRACE_FILE");
	return *path && **path;
}

static qboolean CL_ShadowTraceCommandEligible (const usercmd_t *cmd)
{
	float horizontal;
	int axis;

	if (cmd->vr_gorilla.flags || cmd->vr_gorilla_motion.flags)
		return false;
	if (!cmd->vr_active)
		return true;
	for (axis = 0; axis < 3; axis++)
		if (!isfinite (cmd->vr_roomscalemove[axis]))
			return false;
	horizontal = sqrtf (cmd->vr_roomscalemove[0] * cmd->vr_roomscalemove[0] +
		cmd->vr_roomscalemove[1] * cmd->vr_roomscalemove[1]);
	return isfinite (horizontal) && horizontal <= CL_SHADOW_TRACE_MAX_ROOMSCALE &&
		fabsf (cmd->vr_roomscalemove[2]) <= CL_SHADOW_TRACE_MAX_ROOMSCALE;
}

static qboolean CL_ShadowTraceClaimTarget (int target)
{
	if (target < cl_shadow_trace_last_target)
		cl_shadow_trace_last_target = -1;
	if (target == cl_shadow_trace_last_target ||
		cl_shadow_trace_records >= CL_SHADOW_TRACE_MAX_RECORDS)
		return false;
	cl_shadow_trace_last_target = target;
	cl_shadow_trace_records++;
	return true;
}

static void CL_ShadowTraceWriteFloat (FILE *file, float value)
{
	if (isfinite (value))
		fprintf (file, "%.7g", value);
	else
		fputs ("null", file);
}

static void CL_ShadowTraceWriteVector (FILE *file, const vec3_t value)
{
	fputc ('[', file);
	CL_ShadowTraceWriteFloat (file, value[0]);
	fputc (',', file);
	CL_ShadowTraceWriteFloat (file, value[1]);
	fputc (',', file);
	CL_ShadowTraceWriteFloat (file, value[2]);
	fputc (']', file);
}

static void CL_ShadowTraceAppend (const char *path, int target,
	const vec3_t baseline_origin, const vec3_t baseline_velocity,
	const usercmd_t *command, const cl_replay_result_t *result, qboolean success)
{
	FILE *file;

	if (cl_shadow_trace_io_failed)
		return;
	file = fopen (path, "a");
	if (!file)
	{
		cl_shadow_trace_io_failed = true;
		Con_DPrintf ("QSVR shadow trace: cannot append to configured file\n");
		return;
	}

	if (!success)
		cl_shadow_trace_failures++;
	fprintf (file, "{\"ack\":%d,\"target\":%d,\"base_o\":",
		cl.ackedmovemessages, target);
	CL_ShadowTraceWriteVector (file, baseline_origin);
	fputs (",\"base_v\":", file);
	CL_ShadowTraceWriteVector (file, baseline_velocity);
	if (success)
	{
		fputs (",\"result_o\":", file);
		CL_ShadowTraceWriteVector (file, result->origin);
		fputs (",\"result_v\":", file);
		CL_ShadowTraceWriteVector (file, result->velocity);
		fputs (",\"jump_secs\":", file);
		CL_ShadowTraceWriteFloat (file, result->jump_secs);
		fprintf (file, ",\"onground\":%d", result->onground ? 1 : 0);
	}
	else
		fprintf (file, ",\"failure\":\"compute_rejected\",\"failures\":%u",
			cl_shadow_trace_failures);
	if (command && command->sequence == (unsigned int)target)
	{
		fputs (",\"cmd_seconds\":", file);
		CL_ShadowTraceWriteFloat (file, command->seconds);
		fprintf (file, ",\"cmd_msec\":%u", command->msec);
	}
	else
		fputs (",\"cmd_seconds\":null,\"cmd_msec\":null", file);
	fputs (success ? ",\"ok\":true}\n" : ",\"ok\":false}\n", file);
	if (fclose (file) != 0)
	{
		cl_shadow_trace_io_failed = true;
		Con_DPrintf ("QSVR shadow trace: append failed; tracing disabled\n");
	}
}
#endif

static qboolean CL_ComputeReplayPlayerMovement (entity_t *ent, cl_replay_result_t *result,
	qboolean shadow, int target_sequence)
{
	qboolean private_replay;
	playermove_t saved_pmove;
	movevars_t saved_movevars;
	usercmd_t preview;
	vec3_t bounds[2];
	vec3_t baseline_origin;
	unsigned int solidsize;
	int i, seq, startseq, endseq;
	int pm_type;

	if ((!shadow && cl_nopred.value) || cls.state != ca_connected || cls.signon != SIGNONS || cls.demoplayback ||
		cl.paused || !cl.worldmodel || !cl.entities ||
		cl.viewentity <= 0 || cl.viewentity >= cl.num_entities ||
		ent != &cl.entities[cl.viewentity] || cl.stats[STAT_HEALTH] <= 0 ||
		cl.ackedmovemessages <= 0 || !ent->netstate.pmovetype)
		return false;

	private_replay = cl.protocol_qsvr == QSVR_PROTOCOL_PINNED;
	if (cl.protocol_qsvr && !private_replay)
		return false;
	if (!private_replay && !(cl.protocol_pext2 & PEXT2_PREDINFO))
		return false;
	if (shadow)
	{
		/* A shadow is only an explicit diagnostic of a coherent selected owner.
		 * Prediction permission remains an independent live-policy decision. */
		if (!private_replay || !cl.move_snapshot_valid ||
			cl.move_snapshot_ack != cl.ackedmovemessages ||
			cl.move_snapshot_owner != cl.viewentity ||
			cl.move_ack_authority != MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT ||
			(ent->netstate.pmovetype & 63) != MOVETYPE_WALK)
			return false;
	}
	else if (private_replay)
	{
		/* The owner snapshot establishes ACK/state coherence only.  Permission,
		 * authority and epochs remain independent accepted metadata. */
		if (!cl.move_snapshot_valid ||
			cl.move_snapshot_ack != cl.ackedmovemessages ||
			cl.move_snapshot_owner != cl.viewentity)
			return false;
		CL_ObservePrivateReplayMetadata ();
		if (!cl.move_ack_prediction_allowed ||
			(cl.move_ack_authority != MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT &&
			 cl.move_ack_authority != MOVE_AUTHORITY_PMOVE_QC_COMMAND))
			return false;
	}
	else if (cl.move_replay_private_metadata_valid)
	{
		CL_ResetReplayPropagation ();
		cl.move_replay_private_metadata_valid = false;
	}

	if (shadow && target_sequence >= cl.movemessages)
		return false;
	pm_type = CL_ReplayPMoveType (ent->netstate.pmovetype);
	if (pm_type == PM_NONE || !CL_ReplayHistoryAvailable (&startseq,
		shadow ? target_sequence + 1 : cl.movemessages))
		return false;
	if (shadow)
	{
		if (target_sequence < startseq || target_sequence >= cl.movemessages)
			return false;
		for (seq = startseq; seq <= target_sequence; seq++)
		{
			const usercmd_t *cmd = &cl.movecmds[seq & MOVECMDS_MASK];
#ifdef QSVR_SHADOW_TRACE
			if (!CL_ShadowTraceCommandEligible (cmd))
				return false;
#else
			if (cmd->vr_active)
				return false;
#endif
		}
		endseq = target_sequence + 1;
		saved_pmove = pmove;
		saved_movevars = movevars;
	}
	else
		endseq = cl.movemessages;

	/* Select the current incremental stat/serverinfo accumulator exactly once
	 * for this pass.  A rejected numeric/dialect selection suppresses replay
	 * only; networking owners continue independently. */
	if (!PMCL_SetMoveVars ())
	{
		if (shadow)
			goto shadow_failed;
		return false;
	}

	memset (&pmove, 0, sizeof(pmove));
	if (private_replay)
		VectorCopy (ent->netstate.origin, baseline_origin);
	else
		VectorCopy (ent->msg_origins[0], baseline_origin);
	for (i = 0; i < 3; i++)
		if (!isfinite (baseline_origin[i]))
		{
			if (shadow)
				goto shadow_failed;
			return false;
		}
	VectorCopy (baseline_origin, pmove.origin);

	solidsize = ent->netstate.solidsize;
	if (solidsize && solidsize != ES_SOLID_BSP)
	{
		pmove.player_maxs[0] = pmove.player_maxs[1] = solidsize & 255;
		pmove.player_mins[0] = pmove.player_mins[1] = -pmove.player_maxs[0];
		pmove.player_mins[2] = -(int)((solidsize >> 8) & 255);
		pmove.player_maxs[2] = (int)((solidsize >> 16) & 65535) - 32768;
	}
	for (i = 0; i < 3; i++)
	{
		pmove.velocity[i] = ent->netstate.velocity[i] * (1.0f / 8.0f);
		bounds[0][i] = pmove.origin[i] + pmove.player_mins[i] - 256;
		bounds[1][i] = pmove.origin[i] + pmove.player_maxs[i] + 256;
	}
	VectorClear (pmove.gravitydir);
	pmove.pm_type = pm_type;
	pmove.safeorigin_known = false;
	pmove.waterjumptime = 0;
	pmove.jump_held = (ent->netstate.pmovetype & 0x40) != 0;
	pmove.onladder = false;
	pmove.jump_secs = private_replay ? cl.statsf[STAT_PRIVATE_JUMP_SECS] : 0;
	pmove.onground = (ent->netstate.pmovetype & 0x80) != 0;
	pmove.skipent = -cl.viewentity;
	if (!shadow && private_replay && !CL_SetupReplayGorilla (startseq))
		return false;
	PMCL_AddEntities (bounds);

	if (!shadow && cl.move_replay_propagate_sequence[startseq & MOVECMDS_MASK] == startseq)
		pmove.waterjumptime =
			cl.move_replay_propagate_waterjumptime[startseq & MOVECMDS_MASK];

	for (seq = startseq; seq < endseq; seq++)
	{
		const usercmd_t *histcmd = &cl.movecmds[seq & MOVECMDS_MASK];
		CL_PrepareReplayCommand (&pmove.cmd, histcmd, private_replay);
		PM_PlayerMove (1);
		if (!shadow)
		{
			cl.move_replay_propagate_sequence[(seq + 1) & MOVECMDS_MASK] = seq + 1;
			cl.move_replay_propagate_waterjumptime[(seq + 1) & MOVECMDS_MASK] =
				pmove.waterjumptime;
		}
	}

	if (!shadow)
	{
		CL_PrepareReplayPreview (&preview, private_replay);
		CL_PrepareReplayCommand (&pmove.cmd, &preview, private_replay);
		PM_PlayerMove (1);
	}

	VectorCopy (pmove.origin, result->origin);
	VectorCopy (pmove.velocity, result->velocity);
	result->onground = pmove.onground;
	result->inwater = pmove.waterlevel >= 2;
	result->target_sequence = target_sequence;
	result->jump_secs = pmove.jump_secs;
	if (shadow)
	{
		pmove = saved_pmove;
		movevars = saved_movevars;
	}
	return true;

shadow_failed:
	pmove = saved_pmove;
	movevars = saved_movevars;
	return false;
}

qboolean CL_ReplayPlayerMovement (entity_t *ent, vec3_t origin)
{
	cl_replay_result_t result;

#ifdef QSVR_SHADOW_TRACE
	{
		const char *trace_path;
		int target = cl.movemessages - 1;
		if (CL_ShadowTraceEnabled (&trace_path) && cl.ackedmovemessages > 0 &&
			target >= 0 && target > cl.ackedmovemessages &&
			cl.protocol_qsvr == QSVR_PROTOCOL_PINNED && cl.move_snapshot_valid &&
			cl.move_snapshot_ack == cl.ackedmovemessages &&
			cl.move_snapshot_owner == cl.viewentity &&
			cl.move_ack_authority == MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT &&
			cl.viewentity > 0 && cl.viewentity < cl.num_entities && cl.entities &&
			ent == &cl.entities[cl.viewentity] &&
			(ent->netstate.pmovetype & 63) == MOVETYPE_WALK &&
			CL_ShadowTraceClaimTarget (target))
		{
			cl_replay_result_t shadow_result;
			vec3_t baseline_origin, baseline_velocity;
			const usercmd_t *command = &cl.movecmds[target & MOVECMDS_MASK];
			qboolean shadow_ok;
			int axis;

			VectorCopy (ent->netstate.origin, baseline_origin);
			for (axis = 0; axis < 3; axis++)
				baseline_velocity[axis] = ent->netstate.velocity[axis] * (1.0f / 8.0f);
			shadow_ok = CL_ComputeReplayPlayerMovement (ent, &shadow_result, true, target);
			CL_ShadowTraceAppend (trace_path, target, baseline_origin, baseline_velocity,
				command, &shadow_result, shadow_ok);
		}
	}
#endif

	if (!CL_ComputeReplayPlayerMovement (ent, &result, false, -1))
		return false;

	VectorCopy (result.origin, origin);
	VectorCopy (result.velocity, cl.velocity);
	cl.onground = result.onground;
	cl.inwater = result.inwater;
	return true;
}

static qboolean CL_LerpEntity (entity_t *ent, vec3_t org, vec3_t ang, float frac)
{
	float	 f, d, a;
	int		 j;
	vec3_t	 delta;
	qboolean teleported = false;
	// figure out the pos+angles of the parent
	if (ent->forcelink)
	{ // the entity was not updated in the last message
		// so move to the final spot
		VectorCopy (ent->msg_origins[0], org);
		VectorCopy (ent->msg_angles[0], ang);
	}
	else
	{ // if the delta is large, assume a teleport and don't lerp
		f = frac;
		for (j = 0; j < 3; j++)
		{
			delta[j] = ent->msg_origins[0][j] - ent->msg_origins[1][j];
			if (delta[j] > 100 || delta[j] < -100)
			{
				f = 1;			   // assume a teleportation, not a motion
				teleported = true; // johnfitz -- don't lerp teleports
			}
		}

		a = f;

		// johnfitz -- don't cl_lerp entities that will be r_lerped
		if (r_lerpmove.value && ent->lerp.movestep)
		{
			f = 1;

			// same but for angles
			if (r_lerpturn.value)
				a = 1;
		}
		// johnfitz

		// interpolate the origin and angles
		for (j = 0; j < 3; j++)
		{
			org[j] = ent->msg_origins[1][j] + f * delta[j];

			d = ent->msg_angles[0][j] - ent->msg_angles[1][j];
			if (d > 180)
				d -= 360;
			else if (d < -180)
				d += 360;
			ang[j] = ent->msg_angles[1][j] + a * d;
		}
	}
	return teleported;
}

typedef struct
{
	qboolean viewpose_valid;
	qboolean viewpose_teleported;
	vec3_t vieworigin;
	vec3_t viewangles;
} cl_relink_frame_t;

static void CL_PrepareRelinkViewPose (cl_relink_frame_t *frame, float frac)
{
	entity_t *viewent;

	memset (frame, 0, sizeof(*frame));
	if (!cl.entities || cl.viewentity <= 0 || cl.viewentity >= cl.num_entities)
		return;
	viewent = &cl.entities[cl.viewentity];
	if (!viewent->model || viewent->msgtime != cl.mtime[0])
		return;

	frame->viewpose_teleported =
		CL_LerpEntity (viewent, frame->vieworigin, frame->viewangles, frac);
	frame->viewpose_valid = true;
	CL_ReplayPlayerMovement (viewent, frame->vieworigin);
}

static qboolean CL_AttachEntity (entity_t *ent, float frac,
	const cl_relink_frame_t *frame)
{
	entity_t	*parent;
	vec3_t		 porg, pang;
	vec3_t		 paxis[3];
	vec3_t		 tmp, fwd, up;
	unsigned int tagent = ent->netstate.tagentity;
	int			 runaway = 0;

	while (1)
	{
		if (!tagent)
			return true; // nothing to do.
		if (runaway++ == 10 || tagent >= (unsigned int)cl.num_entities)
			return false; // parent isn't valid
		parent = &cl.entities[tagent];

		if (tagent == cl.viewentity)
			ent->eflags |= EFLAGS_EXTERIORMODEL;

		if (!parent->model)
			return false;
		if (frame && tagent == (unsigned int)cl.viewentity && frame->viewpose_valid)
		{
			tagent = parent->netstate.tagentity;
			VectorCopy (frame->vieworigin, porg);
			VectorCopy (frame->viewangles, pang);
		}
		else
		{
			tagent = parent->netstate.tagentity;
			CL_LerpEntity (parent, porg, pang, frac);
		}

		// FIXME: this code needs to know the exact lerp info of the underlaying model.
		// however for some idiotic reason, someone decided to figure out what should be displayed somewhere far removed from the code that deals with timing
		// so we have absolutely no way to get a reliable origin
		// in the meantime, r_lerpmove 0; r_lerpmodels 0
		// you might be able to work around it by setting the attached entity to movetype_step to match the attachee, and to avoid EF_MUZZLEFLASH.
		// personally I'm just going to call it a quakespasm bug that I cba to fix.

		// FIXME: update porg+pang according to the tag index (we don't support md3s/iqms, so we don't need to do anything here yet)

		if (parent->model && parent->model->type == mod_alias)
			pang[0] *= -1;
		AngleVectors (pang, paxis[0], paxis[1], paxis[2]);

		if (ent->model && ent->model->type == mod_alias)
			ent->angles[0] *= -1;
		AngleVectors (ent->angles, fwd, tmp, up);

		// transform the origin
		VectorMA (porg, ent->origin[0], paxis[0], tmp);
		VectorMA (tmp, -ent->origin[1], paxis[1], tmp);
		VectorMA (tmp, ent->origin[2], paxis[2], ent->origin);

		// transform the forward vector
		VectorMA (vec3_origin, fwd[0], paxis[0], tmp);
		VectorMA (tmp, -fwd[1], paxis[1], tmp);
		VectorMA (tmp, fwd[2], paxis[2], fwd);
		// transform the up vector
		VectorMA (vec3_origin, up[0], paxis[0], tmp);
		VectorMA (tmp, -up[1], paxis[1], tmp);
		VectorMA (tmp, up[2], paxis[2], up);
		// regenerate the new angles.
		VectorAngles (fwd, up, ent->angles);
		if (ent->model && ent->model->type == mod_alias)
			ent->angles[0] *= -1;

		ent->eflags |= parent->netstate.eflags & (EFLAGS_VIEWMODEL | EFLAGS_EXTERIORMODEL);
	}
}

/*
===============
CL_ResetTrail
===============
*/
static void CL_ResetTrail (entity_t *ent)
{
	ent->traildelay = 1.f / MAX_PHYSICS_FREQ;
	VectorCopy (ent->origin, ent->trailorg);
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

	ent->traildelay = q_max (0.f, ent->traildelay + 1.f / MAX_PHYSICS_FREQ);
	VectorCopy (ent->origin, ent->trailorg);
}

/*
===============
CL_RelinkEntities
===============
*/
void CL_RelinkEntities (void)
{
	entity_t *ent;
	int		  i, j;
	float	  frac, d;
	float	  bobjrotate;
	vec3_t	  oldorg;
	dlight_t *dl;
	float	  frametime;
	int		  modelflags;
	qboolean  teleported;
	cl_relink_frame_t frame;

	CL_ExpireStaleVRIKPoses ();

	// determine partial update time
	frac = CL_LerpPoint ();

	frametime = cl.time - cl.oldtime;
	if (frametime < 0)
		frametime = 0;
	if (frametime > 0.1)
		frametime = 0.1;

	if (cl_numvisedicts + 256 > cl_maxvisedicts)
	{
		cl_maxvisedicts += cl_maxvisedicts ? 256 : 4096;
		cl_visedicts = Mem_Realloc (cl_visedicts, sizeof (*cl_visedicts) * cl_maxvisedicts);
		cl_visedicts_alpha = Mem_Realloc (cl_visedicts_alpha, sizeof (*cl_visedicts_alpha) * cl_maxvisedicts);
	}
	cl_numvisedicts = 0;

	//
	// interpolate player info
	//
	for (i = 0; i < 3; i++)
		cl.velocity[i] = cl.mvelocity[1][i] + frac * (cl.mvelocity[0][i] - cl.mvelocity[1][i]);

	SCR_UpdateZoom ();

	if (cls.demoplayback)
	{
		// interpolate the angles
		for (j = 0; j < 3; j++)
		{
			d = cl.mviewangles[0][j] - cl.mviewangles[1][j];
			if (d > 180)
				d -= 360;
			else if (d < -180)
				d += 360;
			cl.viewangles[j] = cl.mviewangles[1][j] + frac * d;
		}
	}

	bobjrotate = anglemod (100 * cl.time);
	CL_PrepareRelinkViewPose (&frame, frac);

	// start on the entity after the world
	ent = (cl.entities != NULL) ? (cl.entities + 1) : NULL;
	for (i = 1; i < cl.num_entities; i++, ent++)
	{
		if (!ent->model)
		{ // empty slot, ish.

			// ericw -- efrags are only used for static entities in GLQuake
			// ent can't be static, so this is a no-op.
			// if (ent->forcelink)
			//	R_RemoveEfrags (ent);	// just became empty
			continue;
		}
		ent->eflags = ent->netstate.eflags;

		// if the object wasn't included in the last packet, remove it
		if (ent->msgtime != cl.mtime[0])
		{
			ent->model = NULL;
			R_FreeEntityBLAS (ent);
			InvalidateTraceLineCache ();
			continue;
		}

		VectorCopy (ent->origin, oldorg);

		if (i == cl.viewentity && frame.viewpose_valid)
		{
			VectorCopy (frame.vieworigin, ent->origin);
			VectorCopy (frame.viewangles, ent->angles);
			teleported = frame.viewpose_teleported;
		}
		else
			teleported = CL_LerpEntity (ent, ent->origin, ent->angles, frac);

		if (cl.time < cl.oldtime)
		{
			// time ran backwards (demo jump): show current state without lerping
			ent->lerp.prev_frame = ent->frame;
			ent->lerp.frame_change_time = 0;
			ent->lerp.snap_frames = 0;
			VectorCopy (ent->msg_origins[0], ent->lerp.prev_origin);
			VectorCopy (ent->msg_angles[0], ent->lerp.prev_angles);
			ent->lerp.move_change_time = 0;
		}

		if (ent->netstate.tagentity)
			if (!CL_AttachEntity (ent, frac, &frame))
			{
				// can't draw it if we don't know where its parent is.
				continue;
			}

		modelflags = (ent->effects >> 24) & 0xff;
		modelflags |= ent->model->flags;

		if (ent->forcelink || teleported)
			CL_ResetTrail (ent);

		// rotate binary objects locally
		if (modelflags & EF_ROTATE)
			ent->angles[1] = bobjrotate;

		if (ent->effects & EF_BRIGHTFIELD)
			R_EntityParticles (ent);

		if (ent->effects & EF_MUZZLEFLASH)
		{
			vec3_t fv, rv, uv;

			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->origin[2] += 16;
			AngleVectors (ent->angles, fv, rv, uv);

			VectorMA (dl->origin, 18, fv, dl->origin);
			dl->radius = 200 + (COM_Rand () & 31);
			dl->minlight = 32;
			dl->die = cl.time + 0.1;

			// johnfitz -- assume muzzle flash accompanied by muzzle flare, which looks bad when lerped:
			// snap the transition into the flash frame and the one out of it
			if (r_lerpmodels.value != 2)
			{
				if (ent == &cl.entities[cl.viewentity])
				{
					// viewent frame changes are detected later in V_CalcRefdef, so the
					// transitions into and out of the flash frame both consume the counter.
					// effects keeps the flash bit until the next update overwrites it and
					// relink runs per render frame, so arm only once per server update
					if (cl.viewent.lerp.snap_msgtime != ent->msgtime)
					{
						cl.viewent.lerp.prev_frame = cl.viewent.frame;
						cl.viewent.lerp.frame_change_time = 0;
						cl.viewent.lerp.snap_frames = 2;
						cl.viewent.lerp.snap_msgtime = ent->msgtime;
					}
				}
				else
				{
					// the flash frame arrived in the same packet and was already recorded
					// at parse; snap it retroactively, the counter covers the change out
					ent->lerp.prev_frame = ent->frame;
					ent->lerp.frame_change_time = 0;
					ent->lerp.snap_frames = 1;
				}
			}
			// johnfitz
		}
		if (ent->effects & EF_BRIGHTLIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->origin[2] += 16;
			dl->radius = 400 + (COM_Rand () & 31);
			dl->die = cl.time + 0.001;
		}
		if (ent->effects & EF_DIMLIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->radius = 200 + (COM_Rand () & 31);
			dl->die = cl.time + 0.001;
		}
		if (ent->effects & EF_QEX_QUADLIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->radius = 200 + (COM_Rand () & 31);
			dl->die = cl.time + 0.001;
			dl->color[0] = 0.25f;
			dl->color[1] = 0.25f;
			dl->color[2] = 1.0f;
		}
		if (ent->effects & EF_QEX_PENTALIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->radius = 200 + (COM_Rand () & 31);
			dl->die = cl.time + 0.001;
			dl->color[0] = 1.0f;
			dl->color[1] = 0.25f;
			dl->color[2] = 0.25f;
		}

		if (cl.paused)
			;
		else if (ent->netstate.traileffectnum > 0 && ent->netstate.traileffectnum < MAX_PARTICLETYPES)
		{
			vec3_t axis[3];
			AngleVectors (ent->angles, axis[0], axis[1], axis[2]);
			PScript_ParticleTrail (oldorg, ent->origin, cl.particle_precache[ent->netstate.traileffectnum].index, frametime, i, axis, &ent->trailstate);
		}
		else if (ent->model->traileffect >= 0)
		{
			vec3_t axis[3];
			AngleVectors (ent->angles, axis[0], axis[1], axis[2]);
			PScript_ParticleTrail (oldorg, ent->origin, ent->model->traileffect, frametime, i, axis, &ent->trailstate);
		}
		else if (ent->model->flags & EF_GIB)
		{
			if (PScript_EntParticleTrail (oldorg, ent, "TR_BLOOD"))
				CL_RocketTrail (ent, 2);
		}
		else if (ent->model->flags & EF_ZOMGIB)
		{
			if (PScript_EntParticleTrail (oldorg, ent, "TR_SLIGHTBLOOD"))
				CL_RocketTrail (ent, 4);
		}
		else if (ent->model->flags & EF_TRACER)
		{
			if (PScript_EntParticleTrail (oldorg, ent, "TR_WIZSPIKE"))
				CL_RocketTrail (ent, 3);
		}
		else if (ent->model->flags & EF_TRACER2)
		{
			if (PScript_EntParticleTrail (oldorg, ent, "TR_KNIGHTSPIKE"))
				CL_RocketTrail (ent, 5);
		}
		else if (ent->model->flags & EF_ROCKET)
		{
			if (PScript_EntParticleTrail (oldorg, ent, "TR_ROCKET"))
				CL_RocketTrail (ent, 0);
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->radius = 200;
			dl->die = cl.time + 0.01;
		}
		else if (ent->model->flags & EF_GRENADE)
		{
			if (PScript_EntParticleTrail (oldorg, ent, "TR_GRENADE"))
				CL_RocketTrail (ent, 1);
		}
		else if (ent->model->flags & EF_TRACER3)
		{
			if (PScript_EntParticleTrail (oldorg, ent, "TR_VORESPIKE"))
				CL_RocketTrail (ent, 6);
		}

		ent->forcelink = false;

		if (ent->netstate.emiteffectnum > 0)
		{
			vec3_t axis[3];
			AngleVectors (ent->angles, axis[0], axis[1], axis[2]);
			if (ent->model->type == mod_alias)
				axis[0][2] *= -1; // stupid vanilla bug
			PScript_RunParticleEffectState (ent->origin, axis[0], frametime, cl.particle_precache[ent->netstate.emiteffectnum].index, &ent->emitstate);
		}
		else if (ent->model->emiteffect >= 0)
		{
			vec3_t axis[3];
			AngleVectors (ent->angles, axis[0], axis[1], axis[2]);
			if (ent->model->flags & MOD_EMITFORWARDS)
			{
				if (ent->model->type == mod_alias)
					axis[0][2] *= -1; // stupid vanilla bug
			}
			else
				VectorScale (axis[2], -1, axis[0]);
			PScript_RunParticleEffectState (ent->origin, axis[0], frametime, ent->model->emiteffect, &ent->emitstate);
			if (ent->model->flags & MOD_EMITREPLACE)
				continue;
		}

		if (i == cl.viewentity && !chase_active.value)
			continue;

		if (cl_numvisedicts < cl_maxvisedicts)
		{
			R_AllocateEntityBLAS (ent);
			cl_visedicts[cl_numvisedicts] = ent;
			cl_numvisedicts++;
		}
	}

	R_UpdateEntityDlights (); // 2021 rerelease shadow casting light entities
}

int CL_GenerateRandomParticlePrecache (const char *pname)
{ // for dpp7 compat
	size_t i;
	pname = va ("%s", pname);
	for (i = 1; i < MAX_PARTICLETYPES; i++)
	{
		if (!cl.particle_precache[i].name)
		{
			cl.particle_precache[i].name = q_strdup (pname);
			cl.particle_precache[i].index = PScript_FindParticleType (cl.particle_precache[i].name);
			return i;
		}
		if (!strcmp (cl.particle_precache[i].name, pname))
			return i;
	}
	return 0;
}

/*
===============
CL_ReadFromServer

Read all incoming data from the server
===============
*/
int CL_ReadFromServer (void)
{
	int		   ret;
	extern int num_temp_entities; // johnfitz
	int		   num_beams = 0;	  // johnfitz
	int		   num_dlights = 0;	  // johnfitz
	beam_t	  *b;				  // johnfitz
	dlight_t  *l;				  // johnfitz
	int		   i;				  // johnfitz

	cl.oldtime = cl.time;
	cl.time += host_frametime;

	needs_relink = true;
	do
	{
		ret = CL_GetMessage ();
		if (ret == -1)
			Host_Error ("CL_ReadFromServer: lost server connection");
		if (!ret)
			break;

		cl.last_received_message = realtime;
		CL_ParseServerMessage ();
	} while (ret && cls.state == ca_connected);

	if (cl_shownet.value)
		Con_Printf ("\n");

	CL_RelinkEntities ();
	needs_relink = false;
	CL_UpdateTEnts ();

	// johnfitz -- devstats

	// visedicts
	if (cl_numvisedicts > 256 && dev_peakstats.visedicts <= 256)
		Con_DWarning ("%i visedicts exceeds standard limit of 256.\n", cl_numvisedicts);
	dev_stats.visedicts = cl_numvisedicts;
	dev_peakstats.visedicts = q_max (cl_numvisedicts, dev_peakstats.visedicts);

	// temp entities
	if (num_temp_entities > 64 && dev_peakstats.tempents <= 64)
		Con_DWarning ("%i tempentities exceeds standard limit of 64 (max = %d).\n", num_temp_entities, MAX_TEMP_ENTITIES);
	dev_stats.tempents = num_temp_entities;
	dev_peakstats.tempents = q_max (num_temp_entities, dev_peakstats.tempents);

	// beams
	for (i = 0, b = cl_beams; i < MAX_BEAMS; i++, b++)
		if (b->model && b->endtime >= cl.time)
			num_beams++;
	if (num_beams > 24 && dev_peakstats.beams <= 24)
		Con_DWarning ("%i beams exceeded standard limit of 24 (max = %d).\n", num_beams, MAX_BEAMS);
	dev_stats.beams = num_beams;
	dev_peakstats.beams = q_max (num_beams, dev_peakstats.beams);

	// dlights
	for (i = 0, l = cl_dlights; i < MAX_DLIGHTS; i++, l++)
		if (l->die >= cl.time && l->radius)
			num_dlights++;
	if (num_dlights > 32 && dev_peakstats.dlights <= 32)
		Con_DWarning ("%i dlights exceeded standard limit of 32 (max = %d).\n", num_dlights, MAX_DLIGHTS);
	dev_stats.dlights = num_dlights;
	dev_peakstats.dlights = q_max (num_dlights, dev_peakstats.dlights);

	// johnfitz

	//
	// bring the links up to date
	//
	return 0;
}

/*
=================
CL_UpdateViewAngles

Spike: split from CL_SendCmd, to do clientside viewangle changes separately from outgoing packets.
=================
*/
void CL_AccumulateCmd (void)
{
	if (cls.signon == SIGNONS)
	{
		// basic keyboard looking
		CL_AdjustAngles ();

		// accumulate movement from other devices
		IN_Move (&cl.pendingcmd);
		VR_InputMove (&cl.pendingcmd);
	}

	cl.pendingcmd.seconds = cl.time - cl.pendingcmd.servertime;
}

/*
=================
CL_SendCmd
=================
*/
void CL_SendCmd (void)
{
	usercmd_t cmd;

	if (cls.state != ca_connected)
		return;

	// get basic movement from keyboard
	CL_BaseMove (&cmd);

	// allow mice or other external controllers to add to the move
	cmd.forwardmove += cl.pendingcmd.forwardmove + cl.pendingcmd.forwardmove_accumulator;
	cmd.sidemove += cl.pendingcmd.sidemove + cl.pendingcmd.sidemove_accumulator;
	cmd.upmove += cl.pendingcmd.upmove + cl.pendingcmd.upmove_accumulator;
	VR_InputApplyPending (&cmd);
	cmd.sequence = cl.movemessages;
	cmd.servertime = cl.time;
	cmd.seconds = cmd.servertime - cl.pendingcmd.servertime;

	CL_FinishMove (&cmd);

	if (cls.signon == SIGNONS)
		CL_SendMove (&cmd); // send the unreliable message
	else
		CL_SendMove (NULL);
	// Native consumption clears the sampled VR velocity along with other
	// pending movement. Same-frame catch-up retains its prepared angle basis.
	vec3_t pending_vr_angles;
	VectorCopy (cl.pendingcmd.vr_pending_angles, pending_vr_angles);
	qboolean pending_vr_angles_valid = cl.pendingcmd.vr_pending_angles_valid;
	memset (&cl.pendingcmd, 0, sizeof (cl.pendingcmd));
	VectorCopy (pending_vr_angles, cl.pendingcmd.vr_pending_angles);
	cl.pendingcmd.vr_pending_angles_valid = pending_vr_angles_valid;
	cl.pendingcmd.servertime = cmd.servertime;

	if (cls.demoplayback)
	{
		SZ_Clear (&cls.message);
		return;
	}

	// send the reliable message
	if (!cls.message.cursize)
		return; // no message at all

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
	vec3_t v, w;

	if (cls.state != ca_connected)
		return;

	VectorMA (r_refdef.vieworg, 8192.0, vpn, v);
	TraceLine (r_refdef.vieworg, v, w);

	if (VectorLength (w) == 0)
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
	if (cls.state != ca_connected)
		return;
#if 0
	//camera position
	Con_Printf ("Viewpos: (%i %i %i) %i %i %i\n",
		(int)r_refdef.vieworg[0],
		(int)r_refdef.vieworg[1],
		(int)r_refdef.vieworg[2],
		(int)r_refdef.viewangles[PITCH],
		(int)r_refdef.viewangles[YAW],
		(int)r_refdef.viewangles[ROLL]);
#else
	char buf[256];
	// player position
	q_snprintf (
		buf, sizeof (buf), "(%i %i %i) %i %i %i", (int)cl.entities[cl.viewentity].origin[0], (int)cl.entities[cl.viewentity].origin[1],
		(int)cl.entities[cl.viewentity].origin[2], (int)cl.viewangles[PITCH], (int)cl.viewangles[YAW], (int)cl.viewangles[ROLL]);

	// player position
	Con_SafePrintf ("Viewpos: %s\n", buf);

	if (Cmd_Argc () >= 2 && !q_strcasecmp (Cmd_Argv (1), "copy"))
	{
		SDL_SetClipboardText (buf);
	}
#endif
}

/*
===============
CL_Viewpos_Completion_f -- tab completion for the viewpos command
===============
*/
static void CL_Viewpos_Completion_f (const char *partial)
{
	if (Cmd_Argc () != 2)
		return;
	Con_AddToTabList ("copy", partial, NULL);
}

static void CL_ServerExtension_FullServerinfo_f (void)
{
	if (Cmd_Argc () != 2)
		return;
	const char *newserverinfo = Cmd_Argv (1);
	q_strlcpy (cl.serverinfo, newserverinfo, sizeof (cl.serverinfo));
	PMCL_ServerinfoUpdated ();
}
static void CL_ServerExtension_ServerinfoUpdate_f (void)
{
	if (Cmd_Argc () != 3)
		return;
	const char *newserverkey = Cmd_Argv (1);
	const char *newservervalue = Cmd_Argv (2);
	Info_SetKey (cl.serverinfo, sizeof (cl.serverinfo), newserverkey, newservervalue);
	PMCL_ServerinfoUpdated ();
}

static void CL_UserinfoChanged (scoreboard_t *sb)
{
	char tmp[64];
	int	 colors;
	Info_GetKey (sb->userinfo, "name", sb->name, sizeof (sb->name));

	Info_GetKey (sb->userinfo, "topcolor", tmp, sizeof (tmp));
	colors = (strtoul (tmp, NULL, 0) & 0xf) << 4;
	Info_GetKey (sb->userinfo, "bottomcolor", tmp, sizeof (tmp));
	colors |= strtoul (tmp, NULL, 0) & 0xf;

	if (colors != sb->colors)
	{
		sb->colors = colors;
		R_TranslateNewPlayerSkin (sb - cl.scores);
	}
}
static void CL_ServerExtension_FullUserinfo_f (void)
{
	int			slot = atoi (Cmd_Argv (1));
	const char *newserverinfo = Cmd_Argv (2);
	if (slot < cl.maxclients)
	{
		scoreboard_t *sb = &cl.scores[slot];
		strncpy (sb->userinfo, newserverinfo, sizeof (sb->userinfo) - 1); // just replace it
		CL_UserinfoChanged (sb);
	}
}
static void CL_ServerExtension_UserinfoUpdate_f (void)
{
	int			slot = atoi (Cmd_Argv (1));
	const char *newserverkey = Cmd_Argv (2);
	const char *newservervalue = Cmd_Argv (3);
	if (slot < cl.maxclients)
	{
		scoreboard_t *sb = &cl.scores[slot];
		Info_SetKey (sb->userinfo, sizeof (sb->userinfo), newserverkey, newservervalue);
		CL_UserinfoChanged (sb);
	}
}

static void SV_DecodeUserInfo (client_t *client)
{
	char tmp[64];
	int	 top, bot;

	// figure out the player's colours
	Info_GetKey (client->userinfo, "topcolor", tmp, sizeof (tmp));
	top = atoi (tmp) & 15;
	if (top > 13)
		top = 13;
	Info_GetKey (client->userinfo, "bottomcolor", tmp, sizeof (tmp));
	bot = atoi (tmp) & 15;
	if (bot > 13)
		bot = 13;
	// update their entity
	client->edict->v.team = bot + 1;
	client->colors = (top << 4) | bot;

	// pick out a name and try to clean it up a little.
	Info_GetKey (client->userinfo, "name", tmp, sizeof (tmp));

	if (!*tmp)
		q_strlcpy (tmp, "unnamed", sizeof (tmp));

	if (strcmp (client->name, tmp) != 0)
	{ // name changed.
		if (client->name[0] && strcmp (client->name, "unconnected"))
			Con_DPrintf ("\"%s\" renamed to \"%s\"\n", client->name, tmp);
		q_strlcpy (client->name, tmp, sizeof (client->name));

		client->edict->v.netname = PR_SetEngineString (client->name);
	}
}
void SV_UpdateInfo (int edict, const char *keyname, const char *value)
{
	char oldvalue[1024];

	char	   *info;
	size_t		infosize;
	const char *pre;
	client_t   *infoplayer = NULL;

	if (!edict)
	{
		cvar_t *var = Cvar_FindVar (keyname);
		if (var && var->flags & CVAR_SERVERINFO)
		{
			Cvar_Set (var->name, value);
			return;
		}
		info = svs.serverinfo;
		infosize = sizeof (svs.serverinfo);
		pre = "//svi ";
	}
	else if (edict <= svs.maxclients)
	{
		edict -= 1;
		infoplayer = &svs.clients[edict];
		info = infoplayer->userinfo;
		infosize = sizeof (infoplayer->userinfo);
		pre = va ("//ui %i", edict);
	}
	else
		return;

	Info_GetKey (info, keyname, oldvalue, sizeof (oldvalue));

	if (strcmp (value, oldvalue))
	{
		// its changed. actually broadcast it.
		Info_SetKey (info, infosize, keyname, value);

		if (infoplayer)
			SV_DecodeUserInfo (infoplayer);

		if (*keyname == '_' || !sv.active)
			return; // underscore means private (user) keys. these are not networked to clients.

		Info_GetKey (info, keyname, oldvalue, sizeof (oldvalue));
		value = oldvalue;

		for (client_t *current_client = svs.clients; current_client < svs.clients + svs.maxclients; current_client++)
		{
			if (current_client->active)
			{
				if (current_client->protocol_pext2 & PEXT2_PREDINFO)
				{
					MSG_WriteByte (&current_client->message, svc_stufftext);
					MSG_WriteString (&current_client->message, va ("%s \"%s\" \"%s\"\n", pre, keyname, value));
				}
				else if (infoplayer && !strcmp (keyname, "name"))
				{
					MSG_WriteByte (&current_client->message, svc_updatename);
					MSG_WriteByte (&current_client->message, edict);
					MSG_WriteString (&current_client->message, value);
				}
				else if (infoplayer && (!strcmp (keyname, "topcolor") || !strcmp (keyname, "bottomcolor")))
				{
					MSG_WriteByte (&current_client->message, svc_updatecolors);
					MSG_WriteByte (&current_client->message, edict);
					MSG_WriteByte (&current_client->message, infoplayer->colors);
				}
			}
		}
	}
}

static void CL_ServerExtension_Ignore_f (void)
{
	Con_DPrintf2 ("Ignoring stufftext: %s\n", Cmd_Argv (0));
}

static void CL_LegacyColor_f (void)
{
	// spike -- code to handle the legacy _cl_color cvar (we now use separate qw-style topcolor/bottomcolor userinfo cvars)
	int col = atoi (Cmd_Argv (1));
	Cvar_SetValue ("topcolor", (col >> 4) & 0xf);
	Cvar_SetValue ("bottomcolor", (col >> 0) & 0xf);
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

	cmd_function_t *cmd;

	Cvar_RegisterVariable (&cl_name);
	Cvar_RegisterVariable (&cl_topcolor);
	Cvar_RegisterVariable (&cl_bottomcolor);
	Cmd_AddCommand ("_cl_color", CL_LegacyColor_f); // for loading vanilla configs (we have separate qw-style topcolor/bottomcolor userinfo cvars instead)
	Cvar_RegisterVariable (&cl_upspeed);
	Cvar_RegisterVariable (&cl_forwardspeed);
	Cvar_RegisterVariable (&cl_desktop_vanilla_run);
	Cvar_RegisterVariable (&cl_backspeed);
	Cvar_RegisterVariable (&cl_sidespeed);
	Cvar_RegisterVariable (&cl_movespeedkey);
	Cvar_RegisterVariable (&cl_yawspeed);
	Cvar_RegisterVariable (&cl_pitchspeed);
	Cvar_RegisterVariable (&cl_anglespeedkey);
	Cvar_RegisterVariable (&cl_shownet);
	Cvar_RegisterVariable (&cl_nolerp);
	Cvar_RegisterVariable (&cl_nopred);
	Cvar_RegisterVariable (&lookspring);
	Cvar_RegisterVariable (&lookstrafe);
	Cvar_RegisterVariable (&sensitivity);

	Cvar_RegisterVariable (&cl_alwaysrun);

	Cvar_RegisterVariable (&m_pitch);
	Cvar_RegisterVariable (&m_yaw);
	Cvar_RegisterVariable (&m_forward);
	Cvar_RegisterVariable (&m_side);

	Cvar_RegisterVariable (&cfg_unbindall);

	Cvar_RegisterVariable (&cl_maxpitch); // johnfitz -- variable pitch clamping
	Cvar_RegisterVariable (&cl_minpitch); // johnfitz -- variable pitch clamping

	Cvar_RegisterVariable (&cl_startdemos);
	Cvar_RegisterVariable (&cl_confirmquit);

	Cmd_AddCommand ("entities", CL_PrintEntities_f);
	Cmd_AddCommand ("disconnect", CL_Disconnect_f);
	Cmd_AddCommand ("record", CL_Record_f);
	Cmd_AddCommand ("stop", CL_Stop_f);
	Cmd_AddCommand ("playdemo", CL_PlayDemo_f);
	Cmd_AddCommand ("timedemo", CL_TimeDemo_f);
	Cmd_AddCommand ("seek", CL_Seek_f);

	Cmd_AddCommand ("tracepos", CL_Tracepos_f);		// johnfitz
	cmd = Cmd_AddCommand ("viewpos", CL_Viewpos_f); // johnfitz
	if (cmd)
		cmd->completion = CL_Viewpos_Completion_f;

	// spike -- serverinfo stuff
	Cmd_AddCommand_ServerCommand ("fullserverinfo", CL_ServerExtension_FullServerinfo_f);
	Cmd_AddCommand_ServerCommand ("svi", CL_ServerExtension_ServerinfoUpdate_f);

	// spike -- userinfo stuff
	Cmd_AddCommand_ServerCommand ("fui", CL_ServerExtension_FullUserinfo_f);
	Cmd_AddCommand_ServerCommand ("ui", CL_ServerExtension_UserinfoUpdate_f);

	Cmd_AddCommand_ServerCommand ("paknames", CL_ServerExtension_Ignore_f);		 // package names in use by the server (including gamedir+extension)
	Cmd_AddCommand_ServerCommand ("paks", CL_ServerExtension_Ignore_f);			 // provides hashes to go with the paknames list
	Cmd_AddCommand_ServerCommand ("wps", CL_ServerExtension_Ignore_f);			 // ktx/cspree weapon stats
	Cmd_AddCommand_ServerCommand ("it", CL_ServerExtension_Ignore_f);			 // cspree item timers
	Cmd_AddCommand_ServerCommand ("tinfo", CL_ServerExtension_Ignore_f);		 // ktx team info
	Cmd_AddCommand_ServerCommand ("exectrigger", CL_ServerExtension_Ignore_f);	 // spike
	Cmd_AddCommand_ServerCommand ("csqc_progname", CL_ServerExtension_Ignore_f); // spike
	Cmd_AddCommand_ServerCommand ("csqc_progsize", CL_ServerExtension_Ignore_f); // spike
	Cmd_AddCommand_ServerCommand ("csqc_progcrc", CL_ServerExtension_Ignore_f);	 // spike
	Cmd_AddCommand_ServerCommand ("cl_fullpitch", CL_ServerExtension_Ignore_f);	 // spike
	Cmd_AddCommand_ServerCommand ("pq_fullpitch", CL_ServerExtension_Ignore_f);	 // spike

	Cmd_AddCommand_ServerCommand ("cl_serverextension_download", CL_ServerExtension_Ignore_f); // spike
	Cmd_AddCommand_ServerCommand ("cl_downloadbegin", CL_ServerExtension_Ignore_f);			   // spike
	Cmd_AddCommand_ServerCommand ("cl_downloadfinished", CL_ServerExtension_Ignore_f);		   // spike
}
