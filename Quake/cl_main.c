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
#include "addon_catalog.h"
#include "snd_spatial_world.h"
#include "bgmusic.h"
#include "voice.h"
#include "pmove.h"
#include "vr_input.h"
#include "vr_weapon_menu.h"
#include "custom_avatar.h"
#include "r_vrik_render.h"

#ifdef QSVR_SHADOW_TRACE
#include <stdio.h>
#include <stdlib.h>
#endif

// we need to declare some mouse variables here, because the menu system
// references them even when on a unix system.

// these two are not intended to be set directly
cvar_t cl_name = {"_cl_name", "player", CVAR_ARCHIVE_GAME | CVAR_USERINFO};
cvar_t cl_avatar = {"cl_avatar", "ranger", CVAR_ARCHIVE_GAME};

cvar_t cl_topcolor = {"topcolor", "0", CVAR_ARCHIVE_GAME | CVAR_USERINFO};
cvar_t cl_bottomcolor = {"bottomcolor", "0", CVAR_ARCHIVE_GAME | CVAR_USERINFO};

cvar_t cl_shownet = {"cl_shownet", "0", CVAR_NONE}; // can be 0, 1, or 2
cvar_t cl_nolerp = {"cl_nolerp", "0", CVAR_NONE};
cvar_t cl_nopred = {"cl_nopred", "0", CVAR_ARCHIVE};
cvar_t cl_predict_smooth = {"cl_predict_smooth", "0", CVAR_NONE};
cvar_t cl_predict_smooth_time = {"cl_predict_smooth_time", "0.10", CVAR_NONE};
cvar_t cl_predict_smooth_min = {"cl_predict_smooth_min", "0.125", CVAR_NONE};
cvar_t cl_predict_smooth_max = {"cl_predict_smooth_max", "4", CVAR_NONE};

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

typedef enum
{
	cl_autoreconnect_idle,
	cl_autoreconnect_wait_config,
	cl_autoreconnect_connecting,
	cl_autoreconnect_wait_signon
} cl_autoreconnect_state_t;

/* Allow frame-synchronized configs and slow map/signon loads, but remain bounded. */
#define CL_AUTO_RECONNECT_CONFIG_TIMEOUT 90.0
#define CL_AUTO_RECONNECT_CONNECT_TIMEOUT 30.0
#define CL_AUTO_RECONNECT_SIGNON_TIMEOUT 90.0
#define CL_SERVERMOD_OPERATION_TIMEOUT 180.0

typedef struct
{
	cl_autoreconnect_state_t state;
	char endpoint[MAX_OSPATH];
	char modname[MAX_QPATH];
	unsigned int legacy_qsvr;
	double deadline;
	double next_attempt;
	double retry_interval;
	qboolean switch_pending;
} cl_autoreconnect_t;

static cl_autoreconnect_t cl_autoreconnect;
static char cl_last_connect_endpoint[MAX_OSPATH];
static unsigned int cl_last_connect_legacy_qsvr;
static qboolean cl_last_connect_valid;
static qboolean cl_prediction_replay_valid;
static int cl_prediction_replay_frame;
static qboolean cl_prediction_replay_frame_valid;

static void CL_TrySendAvatarSelection (void)
{
	int id;
	const custom_avatar_t *custom;
	char command[128];
	int length;

	if (!cl.avatar_set_pending || !cl.avatar_cap_sent ||
		cls.state != ca_connected || cls.demoplayback)
		return;
	id = CustomAvatar_IdForKey (cl_avatar.string);
	if (id < 0)
		return;
	custom = CustomAvatar_Get (id);
	if (custom && !cl.avatar_custom_cap_sent)
	{
		/* Keep the local custom choice queued for a possible later offer. */
		id = PLAYER_AVATAR_RANGER;
		custom = NULL;
	}
	if (custom)
		length = q_snprintf (command, sizeof (command), "avatar_custom_set %s %s",
			custom->key, custom->digest);
	else
		length = q_snprintf (command, sizeof (command), "avatar_set %d", id);
	if (length < 0 || length >= (int)sizeof (command) ||
		cls.message.cursize < 0 || cls.message.maxsize < 0 ||
		(size_t)cls.message.cursize + 1 + (size_t)length + 1 >
		(size_t)cls.message.maxsize)
		return;
	MSG_WriteByte (&cls.message, clc_stringcmd);
	MSG_WriteString (&cls.message, command);
	cl.avatar_set_pending = false;
}

static void CL_TrySendAvatarCapability (void)
{
	if (!cl.avatar_cap_pending || cl.avatar_cap_sent ||
		cls.state != ca_connected || cls.demoplayback ||
		cls.message.cursize < 0 || cls.message.maxsize < 0 ||
		(size_t)cls.message.cursize + 1 + sizeof ("avatar_cap 1") >
		(size_t)cls.message.maxsize)
		return;
	MSG_WriteByte (&cls.message, clc_stringcmd);
	MSG_WriteString (&cls.message, "avatar_cap 1");
	cl.avatar_cap_pending = false;
	cl.avatar_cap_sent = true;
	cl.avatar_set_pending = true;
	CL_TrySendAvatarSelection ();
}

static void CL_TrySendCustomAvatarCapability (void)
{
	if (!cl.avatar_custom_cap_pending || cl.avatar_custom_cap_sent ||
		!cl.avatar_cap_sent || cls.state != ca_connected || cls.demoplayback ||
		cls.message.cursize < 0 || cls.message.maxsize < 0 ||
		(size_t)cls.message.cursize + 1 + sizeof ("avatar_custom_cap 1") >
		(size_t)cls.message.maxsize)
		return;
	MSG_WriteByte (&cls.message, clc_stringcmd);
	MSG_WriteString (&cls.message, "avatar_custom_cap 1");
	cl.avatar_custom_cap_pending = false;
	cl.avatar_custom_cap_sent = true;
	cl.avatar_set_pending = true;
	CL_TrySendAvatarSelection ();
}

static void CL_AvatarChanged (cvar_t *var)
{
	if (CustomAvatar_IdForKey (var->string) < 0)
	{
		Con_Warning ("cl_avatar: unsupported avatar \"%s\"; using ranger\n",
			var->string);
		Cvar_SetQuick (var, CustomAvatar_KeyForId (PLAYER_AVATAR_RANGER));
		return;
	}
	cl.avatar_set_pending = true;
	CL_TrySendAvatarSelection ();
}

typedef struct
{
	qboolean active;
	qboolean refresh_attempted;
	unsigned int catalogue_operation_id;
	double deadline;
	char endpoint[MAX_OSPATH];
	unsigned int legacy_qsvr;
	cl_servermod_info_t info;
	addon_catalog_entry_t approved;
} cl_servermod_download_t;

static cl_servermod_download_t cl_servermod_download;

/* Implemented by the server-mod prompt UI, which is being added separately. */
void M_Menu_ServerModDownload_f (void);
void M_ServerModDownload_Close (void);

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
	R_VRIKRenderInvalidatePublication ();
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

void CL_ResetWeaponContactState (void)
{
	cl.vr_weapon_contact_mode = 0;
	cl.vr_weapon_contact_profile = VR_WEAPON_CONTACT_PROFILE_NONE;
}

// Pinned prediction presentation reset; epoch/replay state remains separately owned.
static void CL_ClearPredictionError (void)
{
	VectorClear (cl.prediction_error);
	cl.prediction_error_time = 0;
	cl.prediction_error_sequence = -1;
}

void CL_ResetPredictionSmoothing (void)
{
	CL_ClearPredictionError ();
	memset (cl.prediction_samples, 0, sizeof (cl.prediction_samples));
	cl.prediction_ack_sequence = 0;
	cl.prediction_ack_sequence_valid = false;
	memset (&cl.prediction_context, 0, sizeof (cl.prediction_context));
	cl.prediction_context_valid = false;
	cl_prediction_replay_valid = false;
	cl_prediction_replay_frame = -1;
	cl_prediction_replay_frame_valid = false;
}

static qboolean CL_PredictionSmoothingSettings (float *duration,
	float *minimum, float *maximum)
{
	float time = cl_predict_smooth_time.value;
	float min_error = cl_predict_smooth_min.value;
	float max_error = cl_predict_smooth_max.value;
	qboolean tracked = V_UseTrackedView () && V_TrackedSessionActive ();

	if (!isfinite (cl_predict_smooth.value) || !cl_predict_smooth.value ||
		!isfinite (time) || !isfinite (min_error) || !isfinite (max_error) ||
		time <= 0 || min_error < 0 || max_error < min_error)
		return false;

	*duration = fminf (time, 0.10f);
	*minimum = min_error;
	*maximum = fminf (max_error, 4.0f);
	if (tracked)
	{
		*duration = fminf (*duration, 0.060f);
		*maximum = fminf (*maximum, 1.0f);
	}
	return isfinite (*duration) && isfinite (*minimum) &&
		isfinite (*maximum) && *duration > 0 && *maximum >= *minimum;
}

static void CL_ObservePredictionContext (void)
{
	cl_prediction_context_t context;
	entity_t *owner = NULL;

	memset (&context, 0, sizeof (context));
	context.world = cl.worldmodel;
	context.viewentity = cl.viewentity;
	context.protocol = cl.protocol;
	context.private_protocol = cl.protocol_qsvr;
	context.protocolflags = cl.protocolflags;
	context.protocol_extensions = cl.protocol_pext2;
	if (cl.entities && cl.viewentity > 0 && cl.viewentity < cl.num_entities)
		owner = &cl.entities[cl.viewentity];
	context.movement_mode = owner ? owner->netstate.pmovetype : 0;
	context.authority = cl.move_ack_authority;
	context.prediction_allowed = cl.move_ack_prediction_allowed;
	context.mode_epoch = cl.move_ack_mode_epoch;
	context.discontinuity_epoch = cl.move_ack_discontinuity_epoch;
	context.discontinuity_reason = cl.move_ack_discontinuity_reason;
	context.tracked_view = V_UseTrackedView ();
	context.tracked_session = V_TrackedSessionActive ();
	context.tracked_aim_mode = V_TrackedAimMode ();
	context.chase_camera = chase_active.value != 0;
	context.paused = cl.paused;
	context.demo_playback = cls.demoplayback;
	context.intermission = cl.intermission;
	context.connection_state = cls.state;

	if (!cl.prediction_context_valid ||
		memcmp (&cl.prediction_context, &context, sizeof (context)))
	{
		CL_ResetPredictionSmoothing ();
		cl.prediction_context = context;
		cl.prediction_context_valid = true;
	}
}

qboolean CL_EvaluatePredictionViewOffset (vec3_t offset, qboolean camera_eligible)
{
	float duration, minimum, maximum, scale, error_length;
	double elapsed;
	int axis;

	VectorClear (offset);
	CL_ObservePredictionContext ();
	if (!camera_eligible || !cl_prediction_replay_valid ||
		!cl_prediction_replay_frame_valid ||
		cl_prediction_replay_frame != host_framecount)
	{
		CL_ResetPredictionSmoothing ();
		return false;
	}
	if (!CL_PredictionSmoothingSettings (&duration, &minimum, &maximum))
	{
		CL_ResetPredictionSmoothing ();
		return false;
	}
	if (cl.prediction_error_sequence < 0)
		return false;
	for (axis = 0; axis < 3; axis++)
		if (!isfinite (cl.prediction_error[axis]))
		{
			CL_ClearPredictionError ();
			return false;
		}
	error_length = VectorLength (cl.prediction_error);
	if (!isfinite (error_length) || error_length < minimum ||
		error_length > maximum)
	{
		CL_ClearPredictionError ();
		return false;
	}

	elapsed = realtime - cl.prediction_error_time;
	if (!isfinite (elapsed) || elapsed < 0)
	{
		CL_ResetPredictionSmoothing ();
		return false;
	}
	if (elapsed >= duration)
	{
		CL_ClearPredictionError ();
		return false;
	}

	scale = 1.0f - (float)(elapsed / duration);
	for (axis = 0; axis < 3; axis++)
	{
		offset[axis] = scale * cl.prediction_error[axis];
		if (!isfinite (offset[axis]))
		{
			VectorClear (offset);
			CL_ClearPredictionError ();
			return false;
		}
	}
	return true;
}

/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState (void)
{
	CL_ResetPendingImpulse ();
	/* The wheel borrows precached client models; drop those references before
	 * serverinfo tears down the old client state, including same-map restarts. */
	VR_WeaponMenu_ClientReset ();
	SpatialWorld_Clear ();
	Voice_ResetConnection ();
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
	CL_ResetPendingImpulse ();
	VR_WeaponMenu_ClientReset ();
	NET_DatagramConnectCancel ();
	SpatialWorld_Clear ();
	CL_ResetVRIKState ();
	cl.avatar_protocol_offered = false;
	cl.avatar_cap_sent = false;
	cl.avatar_cap_pending = false;
	cl.avatar_set_pending = false;
	memset (cl.avatar_ids, PLAYER_AVATAR_RANGER, sizeof (cl.avatar_ids));
	cl.avatar_custom_protocol_offered = false;
	cl.avatar_custom_cap_sent = false;
	cl.avatar_custom_cap_pending = false;
	memset (cl.avatar_custom_keys, 0, sizeof (cl.avatar_custom_keys));
	memset (cl.avatar_custom_digests, 0, sizeof (cl.avatar_custom_digests));
	CL_ResetWeaponContactState ();
	CL_ResetVoiceTransportState ();
	Voice_ResetConnection ();
	cls.legacy_qsvr = 0;
	cls.offered_qsvr = 0;
	cl.protocol_qsvr = 0;
	cl.vr_instant_stop_supported = false;
	cl.vr_instant_stop_cap_sent = false;
	cl.vr_instant_stop_policy_seen = false;
	cl.vr_instant_stop_policy = false;
	cl.vr_instant_stop_resume_ack = 0;
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

qboolean CL_VoiceTransportAvailable (void)
{
	return cl.voice_protocol_offered && cl.voice_cap_sent &&
		cl.voice_protocol_version == VOICE_PROTOCOL_VERSION;
}

void CL_ResetVoiceTransportState (void)
{
	cl.voice_protocol_offered = false;
	cl.voice_cap_sent = false;
	cl.voice_protocol_version = 0;
	cl.voice_cap_pending = false;
	memset (cl.voice_outgoing, 0, sizeof (cl.voice_outgoing));
	cl.voice_outgoing_head = 0;
	cl.voice_outgoing_count = 0;
}

qboolean CL_QueueVoicePacket (const voice_packet_t *packet)
{
	unsigned int tail;

	if (!CL_VoiceTransportAvailable () || cls.state != ca_connected ||
		cls.demoplayback || cls.signon != SIGNONS ||
		!Voice_PacketIsValid (packet))
		return false;
	if (cl.voice_outgoing_count >= VOICE_CLIENT_QUEUE_CAPACITY)
	{
		/* Capture can outrun a blocked network briefly; retain the freshest audio. */
		cl.voice_outgoing_head = (cl.voice_outgoing_head + 1) %
			VOICE_CLIENT_QUEUE_CAPACITY;
		cl.voice_outgoing_count--;
	}
	tail = (cl.voice_outgoing_head + cl.voice_outgoing_count) %
		VOICE_CLIENT_QUEUE_CAPACITY;
	cl.voice_outgoing[tail] = *packet;
	cl.voice_outgoing_count++;
	return true;
}

void CL_Disconnect_f (void)
{
	CL_CancelAutoReconnect ();
	CL_Disconnect ();
	if (sv.active)
		Host_ShutdownServer (false);
}

void CL_CancelAutoReconnect (void)
{
	NET_DatagramConnectCancel ();
	CL_ServerModDownload_Cancel ();
	cl_autoreconnect.state = cl_autoreconnect_idle;
	cl_autoreconnect.next_attempt = 0.0;
	cl_autoreconnect.retry_interval = 0.0;
	cl_autoreconnect.switch_pending = false;
}

static void CL_AutoReconnectFinish (qboolean failed)
{
	if (failed)
	{
		SCR_EndStartupLoadingPlaque ();
		SCR_EndLoadingPlaque ();
		if (cls.state != ca_connected)
			cls.legacy_qsvr = 0;
	}
	CL_CancelAutoReconnect ();
}

static void CL_AttachConnection (const char *host, unsigned int legacy_qsvr,
	struct qsocket_s *netcon)
{
	const char *endpoint;

	cls.legacy_qsvr = legacy_qsvr;
	cls.netcon = netcon;
	Con_DPrintf ("CL_EstablishConnection: connected to %s\n", host);
	cls.demonum = -1;
	cls.state = ca_connected;
	cls.signon = 0;
	SZ_Clear (&cls.message);
	MSG_WriteByte (&cls.message, clc_nop); // NAT Fix from ProQuake

	cl_last_connect_valid = false;
	cl_last_connect_legacy_qsvr = legacy_qsvr;
	/* Keep the numeric control endpoint, including its port. Reverse DNS is
	 * only a display name, and the accepted game socket may use another port. */
	endpoint = !q_strcasecmp (host, "local") ? "local" :
		NET_QSocketGetConnectAddressString (netcon);
	if ((!endpoint || !*endpoint) && host && strlen (host) < sizeof (cl_last_connect_endpoint))
		endpoint = host;
	if (endpoint && *endpoint && strlen (endpoint) < sizeof (cl_last_connect_endpoint))
	{
		q_strlcpy (cl_last_connect_endpoint, endpoint, sizeof (cl_last_connect_endpoint));
		cl_last_connect_valid = true;
	}
}

static qboolean CL_TryEstablishConnection (const char *host, unsigned int legacy_qsvr)
{
	struct qsocket_s *netcon;

	if (cls.state == ca_dedicated || cls.demoplayback ||
		(legacy_qsvr && legacy_qsvr != QSVR_PROTOCOL_PINNED))
		return false;

	CL_Disconnect ();
	cls.legacy_qsvr = legacy_qsvr;
	netcon = NET_Connect (host);
	if (!netcon)
	{
		cls.legacy_qsvr = 0;
		return false;
	}
	CL_AttachConnection (host, legacy_qsvr, netcon);
	return true;
}

static qboolean CL_AutoReconnectTimed (void)
{
	return cl_autoreconnect.retry_interval > 0.0;
}

static void CL_AutoReconnectRetryTimed (void)
{
	NET_DatagramConnectCancel ();
	if (cls.state == ca_connected && cls.signon != SIGNONS)
		CL_Disconnect ();
	cl_autoreconnect.state = cl_autoreconnect_wait_config;
	cl_autoreconnect.next_attempt = realtime + cl_autoreconnect.retry_interval;
}

static void CL_AutoReconnectTimedOut (void)
{
	NET_DatagramConnectCancel ();
	if (cls.state == ca_connected && cls.signon != SIGNONS)
		CL_Disconnect ();
	Con_Warning ("Reconnect to %s for game %s timed out.\n",
		cl_autoreconnect.endpoint, cl_autoreconnect.modname);
	CL_AutoReconnectFinish (true);
}

void CL_AutoReconnectFrame (void)
{
	qboolean timed;

	if (cl_autoreconnect.state == cl_autoreconnect_idle)
		return;
	if (cl_autoreconnect.state == cl_autoreconnect_wait_signon &&
		cls.state == ca_connected && cls.signon == SIGNONS)
	{
		CL_AutoReconnectFinish (false);
		return;
	}
	timed = CL_AutoReconnectTimed ();
	if (timed && realtime >= cl_autoreconnect.deadline)
	{
		CL_AutoReconnectTimedOut ();
		return;
	}
	if (cl_autoreconnect.state == cl_autoreconnect_wait_config)
	{
		if (!timed && realtime >= cl_autoreconnect.deadline)
		{
			Con_Warning ("Server gamedir switch to %s timed out before configuration settled.\n", cl_autoreconnect.modname);
			CL_AutoReconnectFinish (true);
			return;
		}
		if (timed && cl_autoreconnect.switch_pending)
		{
			char paths[MAX_QPATH + sizeof (GAMENAME) + 2];

			cl_autoreconnect.switch_pending = false;
			if (!q_strcasecmp (cl_autoreconnect.modname, GAMENAME))
				q_strlcpy (paths, GAMENAME, sizeof (paths));
			else
				q_snprintf (paths, sizeof (paths), "%s;%s", GAMENAME, cl_autoreconnect.modname);
			COM_SwitchGame (paths);
			cl_autoreconnect.next_attempt = q_max (cl_autoreconnect.next_attempt,
				realtime + 0.25);
			return;
		}
		if (cmd_text.cursize)
			return;
		if (timed && realtime < cl_autoreconnect.next_attempt)
			return;
		if (timed && !COM_GameDirMatches (cl_autoreconnect.modname))
		{
			Con_Warning ("Reconnect stopped because game %s is no longer active.\n",
				cl_autoreconnect.modname);
			CL_AutoReconnectFinish (true);
			return;
		}

		if (!q_strcasecmp (cl_autoreconnect.endpoint, "local"))
		{
			/* The game config can start a demo and freeze loading again. */
			CL_Disconnect ();
			SCR_EndLoadingPlaque ();
			if (!CL_TryEstablishConnection ("local", cl_autoreconnect.legacy_qsvr))
			{
				Con_Warning ("Server gamedir switched to %s, but local reconnect failed.\n",
					cl_autoreconnect.modname);
				if (timed)
					CL_AutoReconnectRetryTimed ();
				else
					CL_AutoReconnectFinish (true);
				return;
			}
			cl_autoreconnect.state = cl_autoreconnect_wait_signon;
			if (!timed)
				cl_autoreconnect.deadline = realtime + CL_AUTO_RECONNECT_SIGNON_TIMEOUT;
			return;
		}
		CL_Disconnect ();
		SCR_EndLoadingPlaque ();
		cls.legacy_qsvr = cl_autoreconnect.legacy_qsvr;
		if (!NET_DatagramConnectStart (cl_autoreconnect.endpoint))
		{
			Con_Warning ("Could not start reconnect to %s.\n",
				cl_autoreconnect.endpoint);
			if (timed)
				CL_AutoReconnectRetryTimed ();
			else
				CL_AutoReconnectFinish (true);
			return;
		}
		cl_autoreconnect.state = cl_autoreconnect_connecting;
		if (!timed)
			cl_autoreconnect.deadline = realtime + CL_AUTO_RECONNECT_CONNECT_TIMEOUT;
		return;
	}
	if (cl_autoreconnect.state == cl_autoreconnect_connecting)
	{
		net_connect_result_t result;
		struct qsocket_s *netcon = NULL;
		const char *reason = NULL;

		if (!timed && realtime >= cl_autoreconnect.deadline)
		{
			Con_Warning ("Reconnect to %s timed out.\n", cl_autoreconnect.endpoint);
			CL_AutoReconnectFinish (true);
			return;
		}
		result = NET_DatagramConnectFrame (&netcon, &reason);
		if (result == NET_CONNECT_PENDING)
			return;
		if (result != NET_CONNECT_COMPLETE || !netcon)
		{
			Con_Warning ("Reconnect to %s failed: %s.\n",
				cl_autoreconnect.endpoint, reason && *reason ? reason : "no response");
			if (timed)
				CL_AutoReconnectRetryTimed ();
			else
				CL_AutoReconnectFinish (true);
			return;
		}
		CL_AttachConnection (cl_autoreconnect.endpoint,
			cl_autoreconnect.legacy_qsvr, netcon);
		cl_autoreconnect.state = cl_autoreconnect_wait_signon;
		if (!timed)
			cl_autoreconnect.deadline = realtime + CL_AUTO_RECONNECT_SIGNON_TIMEOUT;
		return;
	}
	if (cl_autoreconnect.state != cl_autoreconnect_wait_signon)
		return;
	if (timed)
	{
		if (cls.state == ca_connected)
			return;
		Con_Warning ("Reconnect to %s did not complete signon for %s; retrying.\n",
			cl_autoreconnect.endpoint, cl_autoreconnect.modname);
		CL_AutoReconnectRetryTimed ();
		return;
	}
	if (cls.state == ca_connected && realtime < cl_autoreconnect.deadline)
		return;

	if (cls.state == ca_connected)
		CL_Disconnect ();
	Con_Warning ("Reconnect to %s did not complete signon for %s.\n",
		cl_autoreconnect.endpoint, cl_autoreconnect.modname);
	CL_AutoReconnectFinish (true);
}

static qboolean CL_StartAutoReconnect (const char *modname,
	const char *endpoint, unsigned int legacy_qsvr)
{
	char paths[MAX_QPATH + sizeof (GAMENAME) + 2];

	if (!modname || !*modname || !endpoint || !*endpoint ||
		(legacy_qsvr && legacy_qsvr != QSVR_PROTOCOL_PINNED) ||
		cl_autoreconnect.state != cl_autoreconnect_idle)
		return false;

	cl_autoreconnect.state = cl_autoreconnect_wait_config;
	q_strlcpy (cl_autoreconnect.endpoint, endpoint, sizeof (cl_autoreconnect.endpoint));
	q_strlcpy (cl_autoreconnect.modname, modname, sizeof (cl_autoreconnect.modname));
	cl_autoreconnect.legacy_qsvr = legacy_qsvr;
	cl_autoreconnect.deadline = realtime + CL_AUTO_RECONNECT_CONFIG_TIMEOUT;
	cl_autoreconnect.next_attempt = 0.0;
	cl_autoreconnect.retry_interval = 0.0;
	cl_autoreconnect.switch_pending = false;

	if (!q_strcasecmp (modname, GAMENAME))
		q_strlcpy (paths, GAMENAME, sizeof (paths));
	else
		q_snprintf (paths, sizeof (paths), "%s;%s", GAMENAME, modname);
	Con_Printf ("Server requires game %s; switching and reconnecting to %s.\n",
		modname, cl_autoreconnect.endpoint);
	/* The original connect command may have frozen screen updates for loading.
	 * A frame-driven reconnect must keep rendering and input responsive. */
	SCR_EndLoadingPlaque ();
	COM_SwitchGame (paths);
	return true;
}

static void CL_AutoReconnectGame_f (void)
{
	const char *game, *server;
	double delay, retry_interval, timeout;
	unsigned int legacy_qsvr = 0;
	qboolean same_game;

	if (cmd_source != src_command)
		return;
	if (Cmd_Argc () < 3)
	{
		Con_Printf ("qs_reconnect_game <game> <server> [delay] [retry] [timeout]\n");
		return;
	}
	if (cls.state == ca_dedicated)
		return;

	game = Cmd_Argv (1);
	server = Cmd_Argv (2);
	if (!COM_IsSafeGameDirName (game))
	{
		Con_Printf ("Auto reconnect: invalid game directory \"%s\"\n", game);
		return;
	}
	if (!COM_IsSafeServerAddress (server))
	{
		Con_Printf ("Auto reconnect: invalid server address \"%s\"\n", server);
		return;
	}
	if (!COM_GameDirExists (game))
	{
		Con_Printf ("Auto reconnect: missing game directory \"%s\"\n", game);
		return;
	}

	delay = Cmd_Argc () > 3 ? atof (Cmd_Argv (3)) : 8.0;
	retry_interval = Cmd_Argc () > 4 ? atof (Cmd_Argv (4)) : 2.0;
	timeout = Cmd_Argc () > 5 ? atof (Cmd_Argv (5)) : 120.0;
	if (!isfinite (delay) || !isfinite (retry_interval) || !isfinite (timeout))
	{
		Con_Printf ("Auto reconnect: timing values must be finite\n");
		return;
	}
	delay = CLAMP (0.0, delay, 60.0);
	retry_interval = CLAMP (0.5, retry_interval, 15.0);
	timeout = CLAMP (delay + retry_interval, timeout, 300.0);

	same_game = COM_GameDirMatches (game);
	if (!same_game && !registered.value)
	{
		Con_Printf ("Auto reconnect: registered Quake data is required to change game directories\n");
		return;
	}
	if (cl_last_connect_valid && !q_strcasecmp (server, cl_last_connect_endpoint))
		legacy_qsvr = cl_last_connect_legacy_qsvr;

	CL_CancelAutoReconnect ();
	cl_autoreconnect.state = cl_autoreconnect_wait_config;
	q_strlcpy (cl_autoreconnect.endpoint, server, sizeof (cl_autoreconnect.endpoint));
	q_strlcpy (cl_autoreconnect.modname, game, sizeof (cl_autoreconnect.modname));
	cl_autoreconnect.legacy_qsvr = legacy_qsvr;
	cl_autoreconnect.deadline = realtime + timeout;
	cl_autoreconnect.next_attempt = realtime + delay;
	cl_autoreconnect.retry_interval = retry_interval;
	cl_autoreconnect.switch_pending = !same_game;
	cls.demonum = -1;
	CL_Disconnect ();
	SCR_EndLoadingPlaque ();
	Con_Printf ("Switching to game %s; reconnecting to %s in %.1f seconds.\n",
		game, server, delay);
}

qboolean CL_MaybeSwitchServerGame (const char *modname)
{
	if (cl_autoreconnect.state != cl_autoreconnect_idle)
	{
		Con_Warning ("Server gamedir changed again during reconnect; stopping.\n");
		CL_Disconnect ();
		CL_AutoReconnectFinish (true);
		return true;
	}
	if (!modname)
		return false;
	if (cls.state != ca_connected || cls.demoplayback || !cl_last_connect_valid)
		return false;
	return CL_StartAutoReconnect (modname, cl_last_connect_endpoint,
		cl_last_connect_legacy_qsvr);
}

static void CL_ServerModDownload_Error (const char *message)
{
	SCR_EndLoadingPlaque ();
	cl_servermod_download.info.phase = CL_SERVERMOD_ERROR;
	q_strlcpy (cl_servermod_download.info.message,
		message && *message ? message : "Add-on operation failed",
		sizeof (cl_servermod_download.info.message));
	cl_servermod_download.catalogue_operation_id = 0;
	Con_Warning ("Server add-on: %s\n", cl_servermod_download.info.message);
}

static void CL_ServerModDownload_Resume (const char *game)
{
	Modlist_Init ();
	if (!CL_FindInstalledServerGame (game))
	{
		CL_ServerModDownload_Error ("The downloaded add-on was not installed correctly");
		return;
	}
	if (!CL_StartAutoReconnect (game, cl_servermod_download.endpoint,
		cl_servermod_download.legacy_qsvr))
	{
		CL_ServerModDownload_Error ("Could not resume the connection to the server");
		return;
	}
	cl_servermod_download.active = false;
	cl_servermod_download.catalogue_operation_id = 0;
	M_ServerModDownload_Close ();
}

qboolean CL_ServerModDownload_Begin (const char *gamedir)
{
	const char *installed;

	if (!gamedir || !*gamedir || strlen (gamedir) >= MAX_QPATH ||
		COM_ModForbiddenChars (gamedir))
		return false;
	if (cl_servermod_download.active)
	{
		CL_Disconnect ();
		return true;
	}
	if (cl_autoreconnect.state != cl_autoreconnect_idle)
	{
		Con_Warning ("Server gamedir changed again during reconnect; stopping.\n");
		CL_Disconnect ();
		CL_AutoReconnectFinish (true);
		return true;
	}
	if (cls.state != ca_connected || cls.demoplayback || !cl_last_connect_valid)
	{
		CL_Disconnect ();
		SCR_EndLoadingPlaque ();
		Con_Warning ("Cannot check the missing server add-on without a saved connection endpoint.\n");
		return true;
	}

	/* Modlist includes pak and loose-file mods; keep it the installation authority. */
	Modlist_Init ();
	installed = CL_FindInstalledServerGame (gamedir);
	if (installed)
	{
		if (!CL_StartAutoReconnect (installed, cl_last_connect_endpoint,
			cl_last_connect_legacy_qsvr))
			CL_Disconnect ();
		return true;
	}

	memset (&cl_servermod_download, 0, sizeof (cl_servermod_download));
	cl_servermod_download.active = true;
	cl_servermod_download.info.phase = CL_SERVERMOD_CHECKING;
	q_strlcpy (cl_servermod_download.info.game, gamedir,
		sizeof (cl_servermod_download.info.game));
	q_strlcpy (cl_servermod_download.info.message,
		"Checking the add-on catalogue...",
		sizeof (cl_servermod_download.info.message));
	q_strlcpy (cl_servermod_download.endpoint, cl_last_connect_endpoint,
		sizeof (cl_servermod_download.endpoint));
	cl_servermod_download.legacy_qsvr = cl_last_connect_legacy_qsvr;
	cl_servermod_download.deadline = realtime + CL_SERVERMOD_OPERATION_TIMEOUT;
	CL_Disconnect ();
	SCR_EndLoadingPlaque ();
	M_Menu_ServerModDownload_f ();
	return true;
}

qboolean CL_ServerModDownload_GetInfo (cl_servermod_info_t *info)
{
	if (!cl_servermod_download.active)
		return false;
	if (info)
		*info = cl_servermod_download.info;
	return true;
}

void CL_ServerModDownload_Cancel (void)
{
	if (!cl_servermod_download.active)
		return;
	if (cl_servermod_download.catalogue_operation_id)
		AddonCatalog_CancelOperation (cl_servermod_download.catalogue_operation_id);
	cl_servermod_download.active = false;
	cl_servermod_download.catalogue_operation_id = 0;
	M_ServerModDownload_Close ();
}

void CL_ServerModDownload_Accept (void)
{
	addon_catalog_entry_t entry;
	const char *installed;
	int index;

	if (!cl_servermod_download.active ||
		cl_servermod_download.info.phase != CL_SERVERMOD_PROMPT)
		return;
	if (AddonCatalog_State () != ADDON_CATALOG_READY)
	{
		CL_ServerModDownload_Error ("The add-on catalogue is being updated; review the new details before installing");
		return;
	}
	index = AddonCatalog_FindGameDir (cl_servermod_download.approved.gamedir,
		&entry);
	if (index < 0 || !AddonCatalog_EntryMatchesApproved (&entry,
		&cl_servermod_download.approved))
	{
		CL_ServerModDownload_Error ("The approved add-on details have changed");
		return;
	}

	Modlist_Init ();
	installed = CL_FindInstalledServerGame (entry.gamedir);
	if (installed || entry.installed)
	{
		q_strlcpy (cl_servermod_download.info.game,
			installed ? installed : entry.gamedir,
			sizeof (cl_servermod_download.info.game));
		CL_ServerModDownload_Resume (cl_servermod_download.info.game);
		return;
	}
	if (!AddonCatalog_StartInstallApproved (index,
		&cl_servermod_download.approved, true))
	{
		CL_ServerModDownload_Error (AddonCatalog_Message ());
		return;
	}
	cl_servermod_download.info.phase = CL_SERVERMOD_INSTALLING;
	cl_servermod_download.catalogue_operation_id = AddonCatalog_OperationId ();
	q_strlcpy (cl_servermod_download.info.message, "Downloading add-on...",
		sizeof (cl_servermod_download.info.message));
}

void CL_ServerModDownload_Frame (void)
{
	addon_catalog_entry_t entry;
	addon_catalog_state_t state;
	const char *installed;
	int index;

	if (!cl_servermod_download.active)
		return;
	AddonCatalog_Poll ();
	state = AddonCatalog_State ();

	if (cl_servermod_download.info.phase == CL_SERVERMOD_INSTALLING)
	{
		if (state == ADDON_CATALOG_INSTALLING)
		{
			q_strlcpy (cl_servermod_download.info.message, AddonCatalog_Message (),
				sizeof (cl_servermod_download.info.message));
			return;
		}
		cl_servermod_download.catalogue_operation_id = 0;
		Modlist_Init ();
		installed = CL_FindInstalledServerGame (
			cl_servermod_download.approved.gamedir);
		if (!installed)
		{
			CL_ServerModDownload_Error (state == ADDON_CATALOG_ERROR ?
				AddonCatalog_Message () :
				"The downloaded add-on was not installed correctly");
			return;
		}
		/* The installed directory is authoritative even if another catalogue
		 * refresh began after this operation completed. */
		q_strlcpy (cl_servermod_download.info.game, installed,
			sizeof (cl_servermod_download.info.game));
		CL_ServerModDownload_Resume (cl_servermod_download.info.game);
		return;
	}

	if (cl_servermod_download.info.phase != CL_SERVERMOD_CHECKING)
		return;
	if (realtime >= cl_servermod_download.deadline)
	{
		if (cl_servermod_download.catalogue_operation_id)
			AddonCatalog_CancelOperation (cl_servermod_download.catalogue_operation_id);
		CL_ServerModDownload_Error ("Add-on catalogue lookup timed out");
		return;
	}
	if (state == ADDON_CATALOG_UNAVAILABLE)
	{
		CL_ServerModDownload_Error (AddonCatalog_Message ());
		return;
	}
	if (state == ADDON_CATALOG_REFRESHING || state == ADDON_CATALOG_INSTALLING)
	{
		q_strlcpy (cl_servermod_download.info.message, AddonCatalog_Message (),
			sizeof (cl_servermod_download.info.message));
		return;
	}
	if (state == ADDON_CATALOG_IDLE || state == ADDON_CATALOG_ERROR)
	{
		if (cl_servermod_download.refresh_attempted)
		{
			CL_ServerModDownload_Error (AddonCatalog_Message ());
			return;
		}
		cl_servermod_download.refresh_attempted = true;
		AddonCatalog_Refresh ();
		cl_servermod_download.catalogue_operation_id =
			AddonCatalog_State () == ADDON_CATALOG_REFRESHING ?
			AddonCatalog_OperationId () : 0;
		q_strlcpy (cl_servermod_download.info.message, AddonCatalog_Message (),
			sizeof (cl_servermod_download.info.message));
		return;
	}
	if (state != ADDON_CATALOG_READY)
		return;

	cl_servermod_download.catalogue_operation_id = 0;
	Modlist_Init ();
	installed = CL_FindInstalledServerGame (cl_servermod_download.info.game);
	if (installed)
	{
		q_strlcpy (cl_servermod_download.info.game, installed,
			sizeof (cl_servermod_download.info.game));
		CL_ServerModDownload_Resume (installed);
		return;
	}
	index = AddonCatalog_FindGameDir (cl_servermod_download.info.game, &entry);
	if (index < 0)
	{
		if (!cl_servermod_download.refresh_attempted)
		{
			cl_servermod_download.refresh_attempted = true;
			AddonCatalog_Refresh ();
			cl_servermod_download.catalogue_operation_id =
				AddonCatalog_State () == ADDON_CATALOG_REFRESHING ?
				AddonCatalog_OperationId () : 0;
			q_strlcpy (cl_servermod_download.info.message, AddonCatalog_Message (),
				sizeof (cl_servermod_download.info.message));
			return;
		}
		CL_ServerModDownload_Error ("This server add-on is not in the catalogue");
		return;
	}
	if (entry.installed)
	{
		q_strlcpy (cl_servermod_download.info.game, entry.gamedir,
			sizeof (cl_servermod_download.info.game));
		CL_ServerModDownload_Resume (entry.gamedir);
		return;
	}
	cl_servermod_download.approved = entry;
	q_strlcpy (cl_servermod_download.info.game, entry.gamedir,
		sizeof (cl_servermod_download.info.game));
	q_strlcpy (cl_servermod_download.info.name, entry.name,
		sizeof (cl_servermod_download.info.name));
	q_strlcpy (cl_servermod_download.info.author, entry.author,
		sizeof (cl_servermod_download.info.author));
	q_strlcpy (cl_servermod_download.info.description, entry.description,
		sizeof (cl_servermod_download.info.description));
	cl_servermod_download.info.size = entry.size;
	cl_servermod_download.info.verified = entry.verified;
	cl_servermod_download.info.message[0] = '\0';
	cl_servermod_download.info.phase = CL_SERVERMOD_PROMPT;
	Con_Printf ("Server add-on \"%s\" (%s) is available in the catalogue.\n",
		entry.name, entry.gamedir);
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

	CL_CancelAutoReconnect ();
	if (legacy_qsvr && legacy_qsvr != QSVR_PROTOCOL_PINNED)
		Host_Error ("Unsupported legacy Quakespasm VR layout %u", legacy_qsvr);
	cl_last_connect_valid = false;
	if (!CL_TryEstablishConnection (host, legacy_qsvr))
		Host_Error ("CL_Connect: connect failed");
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

static qboolean CL_RestoreReplayGorillaSnapshot (void)
{
	int hand;

	if (!cl.vr_gorilla_state_valid ||
		cl.vr_gorilla_state_sequence != cl.ackedmovemessages)
		return false;
	pmove.gorilla = cl.vr_gorilla_state;
	pmove.gorilla_allowed = true;
	for (hand = 0; hand < 2; hand++)
	{
		int surface = pmove.gorilla.surface[hand];
		if (surface > 0 && (surface >= cl.num_entities ||
			cl.entities[surface].forcelink ||
			cl.entities[surface].netstate.solidsize != ES_SOLID_BSP ||
			cl.entities[surface].netstate.modelindex != pmove.gorilla.surface_model[hand]))
		{
			memset (&pmove.gorilla, 0, sizeof (pmove.gorilla));
			break;
		}
	}
	return true;
}

static qboolean CL_SetupReplayGorilla (int startseq)
{
	const usercmd_t *cmd;
	qboolean raw_replay = false;
	qboolean fresh_reset = false;
	int seq;

	if (CL_ReplayCanTrustGorilla ())
	{
		pmove.gorilla_allowed = true;
		return true;
	}
	if (!cl.vr_gorilla_supported || !cl.vr_gorilla_allowed)
		return true;
	/* The pinned client additionally gates raw-state restoration on
	 * VR_GorillaActive().  That tracked producer/activity owner is not present
	 * in this slice. A journaled raw hand sample is sufficient provenance for
	 * journal replay; protocol permission alone is deliberately not. */
	for (seq = startseq; seq < cl.movemessages; seq++)
	{
		cmd = &cl.movecmds[seq & MOVECMDS_MASK];
		if (cmd->vr_gorilla.flags)
		{
			raw_replay = true;
			fresh_reset = (cmd->vr_gorilla.flags & VR_GORILLA_RESET) != 0;
			break;
		}
	}
	if (!raw_replay)
		return true;
	/* A RESET on the first unacknowledged raw sample is self-seeding. This
	 * permits initial activation and OFF->ON replay after a zero-state ACK
	 * without a 95-byte empty solver snapshot on every inactive packet. */
	if ((!cl.vr_gorilla_state_valid ||
		cl.vr_gorilla_state_sequence != cl.ackedmovemessages) &&
		fresh_reset && cl.move_ack_prediction_allowed &&
		(cl.move_ack_authority == MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT ||
		 (cl.move_ack_authority == MOVE_AUTHORITY_PMOVE_QC_COMMAND &&
		  (movevars.flags & MOVEFLAG_QC_JUMP_ORDINARY))))
	{
		memset (&pmove.gorilla, 0, sizeof (pmove.gorilla));
		pmove.gorilla_allowed = true;
		return true;
	}
	if (!cl.vr_gorilla_state_valid)
		return false;
	if (cl.vr_gorilla_state.initialized &&
		cl.vr_gorilla_state_sequence != cl.ackedmovemessages)
		return false;
	return CL_RestoreReplayGorillaSnapshot ();
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

static qboolean CL_PredictionVectorFinite (const vec3_t vector)
{
	return isfinite (vector[0]) && isfinite (vector[1]) && isfinite (vector[2]);
}

static qboolean CL_PredictionContactEligible (qboolean private_replay)
{
	int i;

	if (!private_replay)
		return true;
	if (pmove.onground && (pmove.groundent < 0 ||
		pmove.groundent >= pmove.numphysent ||
		pmove.physents[pmove.groundent].info != 0))
		return false;
	for (i = 0; i < pmove.numtouch; i++)
	{
		int physent = pmove.touchindex[i];
		if (physent < 0 || physent >= pmove.numphysent ||
			pmove.physents[physent].info != 0)
			return false;
	}
	return true;
}

typedef struct
{
	vec3_t origin;
	vec3_t velocity;
	qboolean onground;
	qboolean inwater;
	int target_sequence;
	float jump_secs;
	cl_prediction_sample_t prediction_samples[CL_PREDICTION_SAMPLE_COUNT];
	int prediction_sample_count;
	qboolean smoothing_enabled;
	qboolean ack_evaluation_valid, ack_sample_valid, ack_sample_eligible;
	unsigned int ack_sequence;
	vec3_t ack_error;
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

static qboolean CL_ReplayEnsureCommandPhysents (vec3_t bounds[2],
	const usercmd_t *command, qboolean private_replay)
{
	vec3_t needed[2];
	qboolean refresh = false;
	int axis;
	if (!private_replay || !pmove.gorilla_allowed)
		return true;
	for (axis = 0; axis < 3; axis++)
	{
		float low = pmove.player_mins[axis];
		float high = pmove.player_maxs[axis];
		if (private_replay && pmove.gorilla_allowed &&
			command->vr_gorilla.flags)
		{
			const vr_gorilla_input_t *raw = &command->vr_gorilla;
			if (!isfinite (raw->head[axis]) ||
				!isfinite (raw->hand[0][axis]) ||
				!isfinite (raw->hand[1][axis]) ||
				fabsf (raw->head[axis]) > 160.0f ||
				fabsf (raw->hand[0][axis]) > 272.0f ||
				fabsf (raw->hand[1][axis]) > 272.0f)
				return false;
			low = fminf (low, fminf (raw->head[axis],
				fminf (raw->hand[0][axis], raw->hand[1][axis])));
			high = fmaxf (high, fmaxf (raw->head[axis],
				fmaxf (raw->hand[0][axis], raw->hand[1][axis])));
		}
		needed[0][axis] = pmove.origin[axis] + low - 256.0f;
		needed[1][axis] = pmove.origin[axis] + high + 256.0f;
		if (!isfinite (needed[0][axis]) || !isfinite (needed[1][axis]))
			return false;
		if (needed[0][axis] < bounds[0][axis] ||
			needed[1][axis] > bounds[1][axis])
			refresh = true;
	}
	if (refresh)
	{
		VectorCopy (needed[0], bounds[0]);
		VectorCopy (needed[1], bounds[1]);
		PMCL_AddEntities (bounds);
	}
	return true;
}

static qboolean CL_ComputeReplayPlayerMovement (entity_t *ent, cl_replay_result_t *result,
	qboolean shadow, int target_sequence)
{
	qboolean private_replay;
	qboolean prediction_samples_enabled = false;
	playermove_t saved_pmove;
	movevars_t saved_movevars;
	usercmd_t preview;
	vec3_t bounds[2];
	vec3_t baseline_origin;
	unsigned int solidsize;
	int i, seq, startseq, endseq;
	int pm_type;

	memset (result, 0, sizeof (*result));
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
			(cl.move_ack_authority != MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT &&
			 cl.move_ack_authority != MOVE_AUTHORITY_PMOVE_QC_COMMAND) ||
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
	/* Authority alone is not a jump-policy consumer. An accepted older QC
	 * owner must never fall through to generic jumping with a missing policy. */
	if (private_replay &&
		!!(movevars.flags & MOVEFLAG_QC_JUMP_ORDINARY) !=
		(cl.move_ack_authority == MOVE_AUTHORITY_PMOVE_QC_COMMAND))
	{
		if (shadow)
			goto shadow_failed;
		return false;
	}

	memset (&pmove, 0, sizeof(pmove));
	pmove.qc_jump_owner = (movevars.flags & MOVEFLAG_QC_JUMP_ORDINARY) != 0;
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
	if (!shadow)
	{
		float duration, minimum, maximum;
		if (!CL_PredictionSmoothingSettings (&duration, &minimum, &maximum))
			CL_ResetPredictionSmoothing ();
		else
		{
			CL_ObservePredictionContext ();
			if (cl.intermission || chase_active.value != 0)
				CL_ResetPredictionSmoothing ();
			else
				prediction_samples_enabled = true;
		}
	}
	result->smoothing_enabled = prediction_samples_enabled;
	if (prediction_samples_enabled)
	{
		const unsigned int ack_sequence = (unsigned int)cl.ackedmovemessages;
		cl_prediction_sample_t *sample;

		if (cl.prediction_ack_sequence_valid &&
			ack_sequence < cl.prediction_ack_sequence)
		{
			CL_ResetPredictionSmoothing ();
			CL_ObservePredictionContext ();
		}
		if (!cl.prediction_ack_sequence_valid ||
			ack_sequence > cl.prediction_ack_sequence)
		{
			result->ack_evaluation_valid = true;
			result->ack_sequence = ack_sequence;
			sample = &cl.prediction_samples[ack_sequence &
				(CL_PREDICTION_SAMPLE_COUNT - 1)];
			if (sample->valid && sample->sequence == ack_sequence)
			{
				result->ack_sample_valid = true;
				result->ack_sample_eligible = sample->eligible &&
					CL_PredictionVectorFinite (sample->origin);
				if (result->ack_sample_eligible)
				{
					VectorSubtract (sample->origin, baseline_origin, result->ack_error);
					result->ack_sample_eligible =
						CL_PredictionVectorFinite (result->ack_error);
				}
			}
		}
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
	pmove.waterjumptime = private_replay ?
		cl.statsf[STAT_PRIVATE_WATERJUMP_SECS] : 0;
	pmove.jump_held = (ent->netstate.pmovetype & 0x40) != 0;
	pmove.onladder = false;
	pmove.jump_secs = private_replay ? cl.statsf[STAT_PRIVATE_JUMP_SECS] : 0;
	pmove.onground = (ent->netstate.pmovetype & 0x80) != 0;
	pmove.skipent = -cl.viewentity;
	if (!shadow && private_replay && !CL_SetupReplayGorilla (startseq))
		return false;
	PMCL_AddEntities (bounds);

	/* Selected-private snapshots include the authoritative waterjump timer for
	 * their completed ACK. Never replace that seed with an earlier local
	 * replay result, even if the journal sequence happens to match. Public
	 * PREDINFO has no timer stat and still needs the local propagation. */
	if (!shadow && !private_replay &&
		cl.move_replay_propagate_sequence[startseq & MOVECMDS_MASK] == startseq)
		pmove.waterjumptime =
			cl.move_replay_propagate_waterjumptime[startseq & MOVECMDS_MASK];

	for (seq = startseq; seq < endseq; seq++)
	{
		const usercmd_t *histcmd = &cl.movecmds[seq & MOVECMDS_MASK];
		CL_PrepareReplayCommand (&pmove.cmd, histcmd, private_replay);
		/* Replay may cross several command-sized regions before the next
		 * authoritative snapshot. Gorilla also point-traces from the head
		 * through both palms, beyond the body hull. Refresh only when that
		 * command's conservative local envelope leaves the collected box. */
		if (!CL_ReplayEnsureCommandPhysents (bounds, &pmove.cmd,
			private_replay))
		{
			if (shadow)
				goto shadow_failed;
			CL_ResetReplayPropagation ();
			return false;
		}
		if (movevars.flags & MOVEFLAG_QC_JUMP_ORDINARY)
		{
			if (!PM_PlayerMoveQCReplay (1))
			{
				if (shadow)
					goto shadow_failed;
				CL_ResetReplayPropagation ();
				return false;
			}
		}
		else
			PM_PlayerMove (1);
		if (!shadow && prediction_samples_enabled &&
			result->prediction_sample_count < CL_PREDICTION_SAMPLE_COUNT &&
			CL_PredictionVectorFinite (pmove.origin))
		{
			cl_prediction_sample_t *sample =
				&result->prediction_samples[result->prediction_sample_count++];
			sample->sequence = histcmd->sequence;
			VectorCopy (pmove.origin, sample->origin);
			sample->eligible = CL_PredictionContactEligible (private_replay);
			sample->valid = true;
		}
		if (!shadow && !private_replay)
		{
			cl.move_replay_propagate_sequence[(seq + 1) & MOVECMDS_MASK] = seq + 1;
			cl.move_replay_propagate_waterjumptime[(seq + 1) & MOVECMDS_MASK] =
				pmove.waterjumptime;
		}
	}

	/* CSQC_Input_Frame can be stateful and is executed on each actual outgoing
	 * command. The unsent preview has no corresponding filtered command, so
	 * replay only committed command history for that mod instead of predicting
	 * raw buttons/movement which its QC may suppress. */
	if (!shadow && !cl.qcvm.extfuncs.CSQC_Input_Frame)
	{
		CL_PrepareReplayPreview (&preview, private_replay);
		/* Preserve the established preview/input ordering. If the journal
		 * contained no raw sample, the disposable preview can still seed the
		 * existing solver after an ACK or a fresh RESET. */
		if (private_replay && !pmove.gorilla_allowed &&
			preview.vr_gorilla.flags && cl.vr_gorilla_supported &&
			cl.vr_gorilla_allowed)
		{
			if (cl.move_ack_authority != MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT &&
				!(cl.move_ack_authority == MOVE_AUTHORITY_PMOVE_QC_COMMAND &&
				  (movevars.flags & MOVEFLAG_QC_JUMP_ORDINARY)))
			{
				CL_ResetReplayPropagation ();
				return false;
			}
			if (preview.vr_gorilla.flags & VR_GORILLA_RESET)
			{
				memset (&pmove.gorilla, 0, sizeof (pmove.gorilla));
				pmove.gorilla_allowed = true;
			}
			else if (startseq < endseq)
			{
				/* Earlier OFF commands reset the solver during replay.
				 * Keep that state instead of resurrecting the ACK's anchors. */
				pmove.gorilla_allowed = true;
			}
			else if (!CL_RestoreReplayGorillaSnapshot ())
			{
				CL_ResetReplayPropagation ();
				return false;
			}
		}
		CL_PrepareReplayCommand (&pmove.cmd, &preview, private_replay);
		if (!CL_ReplayEnsureCommandPhysents (bounds, &pmove.cmd,
			private_replay))
		{
			CL_ResetReplayPropagation ();
			return false;
		}
		if (movevars.flags & MOVEFLAG_QC_JUMP_ORDINARY)
		{
			if (!PM_PlayerMoveQCReplay (1))
			{
				CL_ResetReplayPropagation ();
				return false;
			}
		}
		else
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

static void CL_CommitPredictionSmoothing (const cl_replay_result_t *result)
{
	float duration, minimum, maximum, error_length;
	int i;

	if (!result->smoothing_enabled)
		return;
	for (i = 0; i < result->prediction_sample_count; i++)
	{
		const cl_prediction_sample_t *sample = &result->prediction_samples[i];
		cl.prediction_samples[sample->sequence &
			(CL_PREDICTION_SAMPLE_COUNT - 1)] = *sample;
	}
	if (!result->ack_evaluation_valid)
		return;

	cl.prediction_ack_sequence = result->ack_sequence;
	cl.prediction_ack_sequence_valid = true;
	if (!result->ack_sample_valid || !result->ack_sample_eligible ||
		!CL_PredictionSmoothingSettings (&duration, &minimum, &maximum))
	{
		CL_ClearPredictionError ();
		return;
	}
	error_length = VectorLength (result->ack_error);
	if (!isfinite (error_length) || error_length < minimum ||
		error_length > maximum || !CL_PredictionVectorFinite (result->ack_error))
	{
		CL_ClearPredictionError ();
		return;
	}
	VectorCopy (result->ack_error, cl.prediction_error);
	cl.prediction_error_time = realtime;
	cl.prediction_error_sequence = (int)result->ack_sequence;
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
			(cl.move_ack_authority == MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT ||
			 (cl.move_ack_authority == MOVE_AUTHORITY_PMOVE_QC_COMMAND &&
			  (cl.stats[STAT_MOVEFLAGS] & MOVEFLAG_QC_JUMP_ORDINARY))) &&
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
	{
		CL_ResetPredictionSmoothing ();
		cl_prediction_replay_valid = false;
		return false;
	}

	CL_CommitPredictionSmoothing (&result);
	cl_prediction_replay_valid = true;
	cl_prediction_replay_frame = host_framecount;
	cl_prediction_replay_frame_valid = true;
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
	qboolean discontinuity;

	memset (frame, 0, sizeof(*frame));
	if (!cl.entities || cl.viewentity <= 0 || cl.viewentity >= cl.num_entities)
		return;
	viewent = &cl.entities[cl.viewentity];
	if (!viewent->model || viewent->msgtime != cl.mtime[0])
		return;

	frame->viewpose_teleported =
		CL_LerpEntity (viewent, frame->vieworigin, frame->viewangles, frac);
	frame->viewpose_valid = true;
	discontinuity = frame->viewpose_teleported || viewent->forcelink;
	if (discontinuity)
		CL_ResetPredictionSmoothing ();
	if (!CL_ReplayPlayerMovement (viewent, frame->vieworigin) || discontinuity)
	{
		cl_prediction_replay_valid = false;
		cl_prediction_replay_frame_valid = false;
	}
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

	cl_prediction_replay_valid = false;
	cl_prediction_replay_frame_valid = false;
	if (cl.time < cl.oldtime)
		CL_ResetPredictionSmoothing ();
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
		if (i <= cl.maxclients && i <= MAX_SCOREBOARD &&
			(ent->forcelink || cl.time < cl.oldtime))
			R_VRIKRenderInvalidateMuzzle (ent);

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

		if (teleported && i <= cl.maxclients && i <= MAX_SCOREBOARD)
			R_VRIKRenderInvalidateMuzzle (ent);

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
			if (!R_VRIKRenderGetMuzzleOrigin (ent, ent->forcelink || teleported || cl.time < cl.oldtime, dl->origin))
			{
				VectorCopy (ent->origin, dl->origin);
				dl->origin[2] += 16;
				AngleVectors (ent->angles, fv, rv, uv);
				VectorMA (dl->origin, 18, fv, dl->origin);
			}
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
		else if (modelflags & EF_GIB)
		{
			if (PScript_EntParticleTrail (oldorg, ent, "TR_BLOOD"))
				CL_RocketTrail (ent, 2);
		}
		else if (modelflags & EF_ZOMGIB)
		{
			if (PScript_EntParticleTrail (oldorg, ent, "TR_SLIGHTBLOOD"))
				CL_RocketTrail (ent, 4);
		}
		else if (modelflags & EF_TRACER)
		{
			if (PScript_EntParticleTrail (oldorg, ent, "TR_WIZSPIKE"))
				CL_RocketTrail (ent, 3);
		}
		else if (modelflags & EF_TRACER2)
		{
			if (PScript_EntParticleTrail (oldorg, ent, "TR_KNIGHTSPIKE"))
				CL_RocketTrail (ent, 5);
		}
		else if (modelflags & EF_ROCKET)
		{
			if (PScript_EntParticleTrail (oldorg, ent, "TR_ROCKET"))
				CL_RocketTrail (ent, 0);
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->radius = 200;
			dl->die = cl.time + 0.01;
		}
		else if (modelflags & EF_GRENADE)
		{
			if (PScript_EntParticleTrail (oldorg, ent, "TR_GRENADE"))
				CL_RocketTrail (ent, 1);
		}
		else if (modelflags & EF_TRACER3)
		{
			if (PScript_EntParticleTrail (oldorg, ent, "TR_VORESPIKE"))
				CL_RocketTrail (ent, 6);
		}

		ent->forcelink = false;

		if (ent->netstate.emiteffectnum > 0 && ent->netstate.emiteffectnum < MAX_PARTICLETYPES &&
			cl.particle_precache[ent->netstate.emiteffectnum].name)
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

/* The loader arms this once after Init. Retry only the bounded reliable write. */
void CL_TryEnableCSQCEntities (void)
{
	if (!cl.csqc_enable_pending || cls.state != ca_connected || cls.demoplayback ||
		!cl.qcvm.progs || !cl.qcvm.edicts || !cl.qcvm.extfuncs.CSQC_Ent_Update ||
		!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS) ||
		!(cl.protocol_qsvr == QSVR_PROTOCOL_PINNED ||
		  (!cl.protocol_qsvr && (cl.protocol_pext1 & PEXT1_CSQC))))
		return;

	/* Account for the string command opcode and terminating NUL as well. */
	if (cls.message.cursize < 0 || cls.message.maxsize < 0 ||
		(size_t)cls.message.cursize + 1 + sizeof ("enablecsqc") >
		(size_t)cls.message.maxsize)
		return;

	MSG_WriteByte (&cls.message, clc_stringcmd);
	MSG_WriteString (&cls.message, "enablecsqc");
	cl.csqc_enable_pending = false;
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
	CL_TrySendAvatarCapability ();
	CL_TrySendCustomAvatarCapability ();
	CL_TrySendAvatarSelection ();
	CL_TrySendVRIKCapability ();
	CL_TrySendVoiceCapability ();

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

	/* A catch-up network tick is a distinct command: each outgoing command
	 * needs its own QC filter, including held-button suppression. Stereo eyes
	 * never call this host-side command path. */
	if (cl.qcvm.extfuncs.CSQC_Input_Frame)
	{
		PR_SwitchQCVM (&cl.qcvm);
		PR_GetSetInputs (&cmd, true);
		PR_ExecuteProgram (cl.qcvm.extfuncs.CSQC_Input_Frame);
		PR_GetSetInputs (&cmd, false);
		PR_SwitchQCVM (NULL);
		/* QC may have set attack after CL_FinishMove checked calibration. */
		if (VR_InputSuppressUncalibratedAttack (&cmd))
			cmd.buttons &= ~1u;
	}

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
	CL_TryEnableCSQCEntities ();
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

/* The native tokenizer accepts a quoted token ending at NUL. Metadata must
 * instead be a complete command before any shared store or scoreboard changes. */
static qboolean CL_MetadataCommandComplete (void)
{
	const char *text = Cmd_Args ();
	char token[SERVER_INFO_STRING_SIZE];
	for (int argument = 1; argument < Cmd_Argc (); ++argument)
	{
		const char *start, *end;
		qboolean parse_error;
		end = COM_ParseExBufferSpan (text, CPE_NOTRUNC, token, sizeof (token),
			&parse_error, &start);
		if (!end || parse_error || !start || strcmp (token, Cmd_Argv (argument)) ||
			(*start == '"' && (end - start < 2 || end[-1] != '"')))
			return false;
		text = end;
	}
	while (*text && (unsigned char)*text <= ' ')
		++text;
	return !*text;
}

static void CL_ServerExtension_FullServerinfo_f (void)
{
	if (Cmd_Argc () != 2 || !CL_MetadataCommandComplete ())
		return;
	const char *newserverinfo = Cmd_Argv (1);
	q_strlcpy (cl.serverinfo, newserverinfo, sizeof (cl.serverinfo));
	PMCL_ServerinfoUpdated ();
}
static void CL_ServerExtension_ServerinfoUpdate_f (void)
{
	if (Cmd_Argc () != 3 || !CL_MetadataCommandComplete ())
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
static qboolean CL_ParseBoundedDecimal (const char *text, unsigned int maximum,
	unsigned int *value);

static void CL_ServerExtension_FullUserinfo_f (void)
{
	unsigned int slot;
	scoreboard_t *sb;
	const char *newuserinfo;
	if (Cmd_Argc () != 3 || !CL_MetadataCommandComplete () || !cl.scores || cl.maxclients <= 0 ||
		cl.maxclients > MAX_SCOREBOARD ||
		!CL_ParseBoundedDecimal (Cmd_Argv (1), (unsigned int)(cl.maxclients - 1), &slot))
		return;
	newuserinfo = Cmd_Argv (2);
	sb = &cl.scores[slot];
	if (strlen (newuserinfo) >= sizeof (sb->userinfo))
		return;
	q_strlcpy (sb->userinfo, newuserinfo, sizeof (sb->userinfo));
	CL_UserinfoChanged (sb);
}
static void CL_ServerExtension_UserinfoUpdate_f (void)
{
	unsigned int slot;
	scoreboard_t *sb;
	if (Cmd_Argc () != 4 || !CL_MetadataCommandComplete () || !cl.scores || cl.maxclients <= 0 ||
		cl.maxclients > MAX_SCOREBOARD ||
		!CL_ParseBoundedDecimal (Cmd_Argv (1), (unsigned int)(cl.maxclients - 1), &slot))
		return;
	sb = &cl.scores[slot];
	Info_SetKey (sb->userinfo, sizeof (sb->userinfo), Cmd_Argv (2), Cmd_Argv (3));
	CL_UserinfoChanged (sb);
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
	char prestr[64];

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
		q_snprintf (prestr, sizeof (prestr), "//ui %i", edict);
		pre = prestr;
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

static qboolean CL_ParseBoundedDecimal (const char *text, unsigned int maximum,
	unsigned int *value)
{
	unsigned int parsed = 0;
	const unsigned char *cursor = (const unsigned char *)text;

	if (!cursor || !*cursor || !value)
		return false;
	for (; *cursor; ++cursor)
	{
		unsigned int digit;
		if (*cursor < '0' || *cursor > '9')
			return false;
		digit = (unsigned int)(*cursor - '0');
		if (digit > maximum || parsed > (maximum - digit) / 10)
			return false;
		parsed = parsed * 10 + digit;
	}
	*value = parsed;
	return true;
}

static qboolean CL_ServerNumericOfferSyntaxValid (const char *raw, int fields)
{
	const unsigned char *cursor = (const unsigned char *)raw;

	/* Cmd_Argv has already tokenized this text. Check the original spelling
	 * too: the shared tokenizer accepts incomplete quotes and comments. */
	for (int field = 0; field < fields; ++field)
	{
		while (*cursor == ' ' || *cursor == '\t')
			cursor++;
		if (*cursor < '0' || *cursor > '9')
			return false;
		do
			cursor++;
		while (*cursor >= '0' && *cursor <= '9');
		if (field + 1 < fields && *cursor != ' ' && *cursor != '\t')
			return false;
	}
	while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
		*cursor == '\n')
		cursor++;
	return *cursor == 0;
}

void CL_QueueGorillaCapability (void)
{
	const size_t required = 1 + sizeof ("vr_gorilla_cap 1");

	if (!cl.vr_gorilla_supported || cl.vr_gorilla_cap_sent ||
		cl.protocol_qsvr != QSVR_PROTOCOL_PINNED || cls.state != ca_connected ||
		cls.demoplayback ||
		cls.message.overflowed || cls.message.cursize < 0 ||
		cls.message.maxsize < 0 ||
		cls.message.cursize > cls.message.maxsize ||
		required > (size_t)(cls.message.maxsize - cls.message.cursize))
		return;
	MSG_WriteByte (&cls.message, clc_stringcmd);
	MSG_WriteString (&cls.message, "vr_gorilla_cap 1");
	cl.vr_gorilla_cap_sent = true;
}

void CL_QueueInstantStopCapability (void)
{
	const size_t required = 1 + sizeof ("vr_instant_stop_cap 1");

	if (!cl.vr_instant_stop_supported || cl.vr_instant_stop_cap_sent ||
		cl.protocol_qsvr != QSVR_PROTOCOL_PINNED || cls.state != ca_connected ||
		cls.demoplayback || cls.message.overflowed ||
		cls.message.cursize < 0 || cls.message.maxsize < 0 ||
		cls.message.cursize > cls.message.maxsize ||
		required > (size_t)(cls.message.maxsize - cls.message.cursize))
		return;
	MSG_WriteByte (&cls.message, clc_stringcmd);
	MSG_WriteString (&cls.message, "vr_instant_stop_cap 1");
	cl.vr_instant_stop_cap_sent = true;
}

static void CL_ServerExtension_InstantStopProtocol_f (void)
{
	unsigned int version;

	if (cmd_source != src_server)
		return;
	/* Replacement offers revoke a prior negotiation unless they are valid. */
	cl.vr_instant_stop_supported = false;
	cl.vr_instant_stop_cap_sent = false;
	if (cl.protocol_qsvr != QSVR_PROTOCOL_PINNED || Cmd_Argc () != 2 ||
		!CL_ServerNumericOfferSyntaxValid (Cmd_Args (), 1) ||
		!CL_ParseBoundedDecimal (Cmd_Argv (1), 1, &version) || version != 1)
	{
		Con_DPrintf2 ("Ignoring malformed VR instant-stop capability offer.\n");
		return;
	}
	cl.vr_instant_stop_supported = true;
	CL_QueueInstantStopCapability ();
}

static void CL_ServerExtension_AkimboProtocol_f (void)
{
	unsigned int qbj3_akimbo, qbj3_berserk_akimbo, enyo_akimbo,
		dwell_berserk_akimbo;

	if (cmd_source != src_server)
		return;

	/* A malformed or unsupported replacement offer revokes every prior flag. */
	cl.vr_qbj3_akimbo_supported = false;
	cl.vr_qbj3_berserk_akimbo_supported = false;
	cl.vr_enyo_akimbo_supported = false;
	cl.vr_dwell_berserk_akimbo_supported = false;
	if (cl.protocol_qsvr != QSVR_PROTOCOL_PINNED || Cmd_Argc () != 5 ||
		!CL_ServerNumericOfferSyntaxValid (Cmd_Args (), 4) ||
		!CL_ParseBoundedDecimal (Cmd_Argv (1), 1, &qbj3_akimbo) ||
		!CL_ParseBoundedDecimal (Cmd_Argv (2), 1, &qbj3_berserk_akimbo) ||
		!CL_ParseBoundedDecimal (Cmd_Argv (3), 1, &enyo_akimbo) ||
		!CL_ParseBoundedDecimal (Cmd_Argv (4), 1, &dwell_berserk_akimbo))
	{
		Con_DPrintf2 ("Ignoring malformed akimbo capability offer.\n");
		return;
	}

	cl.vr_qbj3_akimbo_supported = (qboolean)qbj3_akimbo;
	cl.vr_qbj3_berserk_akimbo_supported = (qboolean)qbj3_berserk_akimbo;
	cl.vr_enyo_akimbo_supported = (qboolean)enyo_akimbo;
	cl.vr_dwell_berserk_akimbo_supported = (qboolean)dwell_berserk_akimbo;
}

static void CL_ServerExtension_GorillaProtocol_f (void)
{
	unsigned int version, allowed;

	if (cmd_source != src_server)
		return;
	/* A malformed replacement offer cannot leave prior permission active. */
	if (Cmd_Argc () != 3 ||
		!CL_ServerNumericOfferSyntaxValid (Cmd_Args (), 2) ||
		!CL_ParseBoundedDecimal (Cmd_Argv (1), 1, &version) || version != 1 ||
		!CL_ParseBoundedDecimal (Cmd_Argv (2), 1, &allowed) ||
		cl.protocol_qsvr != QSVR_PROTOCOL_PINNED)
	{
		cl.vr_gorilla_supported = false;
		cl.vr_gorilla_allowed = false;
		cl.vr_gorilla_cap_sent = false;
		cl.vr_gorilla_trusted_supported = false;
		cl.vr_gorilla_trusted_cap_sent = false;
		cl.vr_gorilla_motion_generation_valid = false;
		cl.vr_gorilla_state_valid = false;
		cl.vr_gorilla_state_sequence = -1;
		memset (&cl.vr_gorilla_state, 0, sizeof (cl.vr_gorilla_state));
		Con_DPrintf2 ("Ignoring malformed Gorilla capability offer.\n");
		return;
	}
	if (!cl.vr_gorilla_supported || cl.vr_gorilla_allowed != (qboolean)allowed)
	{
		cl.vr_gorilla_state_valid = false;
		cl.vr_gorilla_state_sequence = -1;
		cl.vr_gorilla_motion_generation_valid = false;
		memset (&cl.vr_gorilla_state, 0, sizeof (cl.vr_gorilla_state));
	}
	cl.vr_gorilla_supported = true;
	cl.vr_gorilla_allowed = (qboolean)allowed;
	CL_QueueGorillaCapability ();
}

static void CL_ServerExtension_WeaponContactProtocol_f (void)
{
	unsigned int version, mode, profile;

	if (cmd_source != src_server)
		return;

	/* Every server update is authoritative, including a malformed revocation. */
	CL_ResetWeaponContactState ();
	if (Cmd_Argc () != 4 || !CL_ServerNumericOfferSyntaxValid (Cmd_Args (), 3) ||
		!CL_ParseBoundedDecimal (Cmd_Argv (1), VR_WEAPON_CONTACT_PROTOCOL_VERSION,
			&version) || version != VR_WEAPON_CONTACT_PROTOCOL_VERSION ||
		!CL_ParseBoundedDecimal (Cmd_Argv (2), VR_WEAPON_CONTACT_CAP_KNOWN,
			&mode) || (mode & ~VR_WEAPON_CONTACT_CAP_KNOWN) ||
		!CL_ParseBoundedDecimal (Cmd_Argv (3), VR_WEAPON_CONTACT_PROFILE_COUNT - 1,
			&profile))
	{
		Con_DPrintf2 ("Ignoring malformed weapon-contact capability offer.\n");
		return;
	}
	cl.vr_weapon_contact_mode = mode;
	cl.vr_weapon_contact_profile = profile;
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
	CustomAvatar_Init ();

	cmd_function_t *cmd;

	Cvar_RegisterVariable (&cl_name);
	Cvar_RegisterVariable (&cl_avatar);
	Cvar_SetCallback (&cl_avatar, CL_AvatarChanged);
	CL_AvatarChanged (&cl_avatar);
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
	Cvar_RegisterVariable (&cl_predict_smooth);
	Cvar_RegisterVariable (&cl_predict_smooth_time);
	Cvar_RegisterVariable (&cl_predict_smooth_min);
	Cvar_RegisterVariable (&cl_predict_smooth_max);
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
	Cmd_AddCommand ("qs_reconnect_game", CL_AutoReconnectGame_f);
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
	Cmd_AddCommand_ServerCommand ("vr_qbj3_akimbo_protocol",
		CL_ServerExtension_AkimboProtocol_f);
	Cmd_AddCommand_ServerCommand ("vr_weapon_contact_protocol",
		CL_ServerExtension_WeaponContactProtocol_f);
	Cmd_AddCommand_ServerCommand ("vr_gorilla_protocol",
		CL_ServerExtension_GorillaProtocol_f);
	Cmd_AddCommand_ServerCommand ("vr_instant_stop_protocol",
		CL_ServerExtension_InstantStopProtocol_f);

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
