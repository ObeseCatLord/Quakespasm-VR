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
cvar_t	cl_autoswitchgame = {"cl_autoswitchgame","1",CVAR_ARCHIVE};
// Retired compatibility cvars. Keep them registered so old configs and launch
// scripts do not warn, but do not archive them back out.
cvar_t	cl_extrapolate = {"cl_extrapolate","0",CVAR_NONE};
cvar_t	cl_extrapolate_adaptive = {"cl_extrapolate_adaptive","0",CVAR_NONE};
cvar_t	cl_extrapolate_adaptive_max = {"cl_extrapolate_adaptive_max","0.12",CVAR_NONE};
cvar_t	cl_extrapolate_adaptive_time = {"cl_extrapolate_adaptive_time","0.75",CVAR_NONE};
cvar_t	cl_net_lerpbuffer = {"cl_net_lerpbuffer","0",CVAR_NONE};
cvar_t	cl_net_lerpbuffer_adaptive = {"cl_net_lerpbuffer_adaptive","0",CVAR_NONE};
cvar_t	cl_net_lerpbuffer_adaptive_max = {"cl_net_lerpbuffer_adaptive_max","0.30",CVAR_NONE};
cvar_t	cl_net_lerpbuffer_adaptive_time = {"cl_net_lerpbuffer_adaptive_time","0.75",CVAR_NONE};
cvar_t	cl_predict_smooth = {"cl_predict_smooth","0",CVAR_NONE};
cvar_t	cl_predict_smooth_time = {"cl_predict_smooth_time","0.04",CVAR_NONE};
cvar_t	cl_predict_smooth_min = {"cl_predict_smooth_min","0.25",CVAR_NONE};
cvar_t	cl_predict_smooth_max = {"cl_predict_smooth_max","8",CVAR_NONE};
cvar_t	cl_predict_error_log = {"cl_predict_error_log","1",CVAR_NONE};

extern cvar_t host_maxfps;
extern cvar_t cl_netfps;

typedef struct cl_auto_reconnect_s
{
	qboolean	active;
	char		game[MAX_QPATH];
	char		server[NET_NAMELEN];
	double		start_time;
	double		next_attempt_time;
	double		retry_interval;
	double		timeout;
	double		settle_until;
	int		attempts;
	qboolean	switch_pending;
} cl_auto_reconnect_t;

static cl_auto_reconnect_t cl_auto_reconnect;
static qboolean cl_auto_reconnect_switch_command;
static char cl_connect_target[NET_NAMELEN];

static qboolean CL_AutoReconnect_IsSafeGame(const char *game)
{
	const unsigned char *p;

	if (!game || !*game || strlen(game) >= MAX_QPATH ||
		!strcmp(game, ".") || strstr(game, ".."))
		return false;

	for (p = (const unsigned char *)game; *p; p++)
	{
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		      (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.'))
			return false;
	}

	return true;
}

static qboolean CL_AutoReconnect_IsSafeServer(const char *server)
{
	const unsigned char *p;

	if (!server || !*server || strlen(server) >= NET_NAMELEN)
		return false;

	for (p = (const unsigned char *)server; *p; p++)
	{
		if (*p <= 32 || *p == '"' || *p == '\'' || *p == '\\' || *p == ';')
			return false;
	}

	return true;
}

static qboolean CL_AutoReconnect_GameExists(const char *game)
{
	if (!q_strcasecmp(game, GAMENAME))
		return Sys_FileType(va("%s/%s", com_basedir, GAMENAME)) == FS_ENT_DIRECTORY;

	if (Sys_FileType(va("%s/%s", com_basedir, game)) == FS_ENT_DIRECTORY)
		return true;

	return host_parms->userdir != host_parms->basedir &&
	       Sys_FileType(va("%s/%s", host_parms->userdir, game)) == FS_ENT_DIRECTORY;
}

static qboolean CL_AutoReconnect_ResolveInstalledGame(const char *game,
	char *resolved, size_t resolvedsize)
{
	/*
	 * Serverinfo parsing is on the connection hot path.  Rebuilding the full
	 * Mods list here can synchronously walk every installed add-on (and its
	 * metadata) just as the old connection is being torn down.  The game
	 * command needs only a validated, installed directory, so use the same
	 * direct check as CL_AutoReconnect_Start instead.
	 */
	if (!CL_AutoReconnect_IsSafeGame(game) ||
		!CL_AutoReconnect_GameExists(game))
		return false;

	q_strlcpy(resolved, !q_strcasecmp(game, GAMENAME) ? GAMENAME : game,
		resolvedsize);
	return true;
}

qboolean CL_AutoReconnect_IsActive(void)
{
	return cl_auto_reconnect.active;
}

qboolean CL_AutoReconnect_IsSwitchCommand(void)
{
	return cl_auto_reconnect_switch_command;
}

void CL_AutoReconnect_Cancel(void)
{
	cl_auto_reconnect.active = false;
}

double CL_NetLagDebugFrameThreshold (void)
{
	double threshold;

	threshold = net_lagdebug_frame_threshold.value;
	if (threshold < 0.055)
		threshold = 0.055;
	return threshold;
}

static qboolean CL_ValueMatchesOldDefault (float value, float old_default)
{
	const float epsilon = 0.0001f;

	return value > old_default - epsilon && value < old_default + epsilon;
}

static qboolean CL_CommandLineSetsCvar (const char *name)
{
	int i;

	for (i = 1; i < com_argc; i++)
	{
		const char *arg = com_argv[i];

		if (!arg || arg[0] != '+')
			continue;
		if (!q_strcasecmp (arg + 1, name))
			return true;
		if ((!q_strcasecmp (arg + 1, "set") ||
			 !q_strcasecmp (arg + 1, "seta")) &&
			i + 1 < com_argc && com_argv[i + 1] &&
			!q_strcasecmp (com_argv[i + 1], name))
			return true;
	}

	return false;
}

static void CL_MigrateNetworkDefaults_f (void)
{
	if (cl_extrapolate.value != 0 || CL_ValueMatchesOldDefault (cl_extrapolate.value, 0.02f))
		Cvar_SetQuick (&cl_extrapolate, "0");
	if (cl_extrapolate_adaptive.value != 0)
		Cvar_SetQuick (&cl_extrapolate_adaptive, "0");
	if (cl_net_lerpbuffer.value != 0 || CL_ValueMatchesOldDefault (cl_net_lerpbuffer.value, 0.10f))
		Cvar_SetQuick (&cl_net_lerpbuffer, "0");
	if (cl_net_lerpbuffer_adaptive.value != 0)
		Cvar_SetQuick (&cl_net_lerpbuffer_adaptive, "0");
	if (cl_predict_smooth.value != 0)
		Cvar_SetQuick (&cl_predict_smooth, "0");
	if (!CL_CommandLineSetsCvar ("cl_netfps") &&
		!CL_ValueMatchesOldDefault (cl_netfps.value, 0.0f))
		Cvar_SetQuick (&cl_netfps, "0");
	if (!CL_CommandLineSetsCvar ("host_maxfps") &&
		CL_ValueMatchesOldDefault (host_maxfps.value, 72.0f))
		Cvar_SetQuick (&host_maxfps, "250");
	if (!CL_CommandLineSetsCvar ("cl_predictmove") &&
		!CL_ValueMatchesOldDefault (cl_predictmove.value, 1.0f))
		Cvar_SetQuick (&cl_predictmove, "1");
	if (!CL_CommandLineSetsCvar ("cl_nopred") &&
		!CL_ValueMatchesOldDefault (cl_nopred.value, 0.0f))
		Cvar_SetQuick (&cl_nopred, "0");
}

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
extern double	v_punchangles_times[2];

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
	memset (v_punchangles_times, 0, sizeof (v_punchangles_times));
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

void CL_Disconnect_f (void)
{
	CL_AutoReconnect_Cancel();
	CL_Disconnect ();
	if (sv.active)
		Host_ShutdownServer (false);
}


static qboolean CL_TryEstablishConnection (const char *host)
{
	if (cls.state == ca_dedicated)
		return false;

	if (cls.demoplayback)
		return false;

	if (!host || !*host ||
		q_strlcpy(cl_connect_target, host, sizeof(cl_connect_target)) >= sizeof(cl_connect_target))
		cl_connect_target[0] = 0;

	CL_MigrateNetworkDefaults_f ();
	CL_Disconnect ();

	cls.netcon = NET_Connect (host);
	if (!cls.netcon)
		return false;
	Con_DPrintf ("CL_EstablishConnection: connected to %s\n", host);

	cls.demonum = -1;			// not in the demo loop now
	cls.state = ca_connected;
	CL_ClearSignons ();			// need all the signon messages before playing
	MSG_WriteByte (&cls.message, clc_nop);	// NAT Fix from ProQuake
	return true;
}

/*
=====================
CL_EstablishConnection

Host should be either "local" or a net address to be passed on
=====================
*/
void CL_EstablishConnection (const char *host)
{
	/* Console connect commands historically do nothing in these states. */
	if (cls.state == ca_dedicated || cls.demoplayback)
		return;
	if (!CL_TryEstablishConnection(host))
		Host_Error ("CL_Connect: connect failed");
}

static qboolean CL_AutoReconnect_Start(const char *game, const char *server,
	double delay, double retry_interval, double timeout)
{
	if (!CL_AutoReconnect_IsSafeGame(game))
	{
		Con_Printf("Auto reconnect: invalid game directory \"%s\"\n", game);
		return false;
	}
	if (!CL_AutoReconnect_IsSafeServer(server))
	{
		Con_Printf("Auto reconnect: invalid server address \"%s\"\n", server);
		return false;
	}
	if (!CL_AutoReconnect_GameExists(game))
	{
		Con_Printf("Auto reconnect: missing game directory \"%s\"\n", game);
		return false;
	}

	delay = CLAMP(0.0, delay, 60.0);
	retry_interval = CLAMP(0.5, retry_interval, 15.0);
	timeout = CLAMP(delay + retry_interval, timeout, 300.0);

	memset(&cl_auto_reconnect, 0, sizeof(cl_auto_reconnect));
	cl_auto_reconnect.active = true;
	q_strlcpy(cl_auto_reconnect.game, game, sizeof(cl_auto_reconnect.game));
	q_strlcpy(cl_auto_reconnect.server, server, sizeof(cl_auto_reconnect.server));
	cl_auto_reconnect.start_time = realtime;
	cl_auto_reconnect.next_attempt_time = realtime + delay;
	cl_auto_reconnect.retry_interval = retry_interval;
	cl_auto_reconnect.timeout = timeout;
	cl_auto_reconnect.switch_pending = !COM_GameDirMatches(cl_auto_reconnect.game);

	Con_Printf("Switching to game \"%s\". Reconnecting to %s in %.1f seconds.\n",
	           cl_auto_reconnect.game, cl_auto_reconnect.server, delay);
	SCR_CenterPrint(va("Switching to %s\nReconnecting soon...", cl_auto_reconnect.game));
	cls.demonum = -1;
	CL_Disconnect();

	return true;
}

qboolean CL_MaybeSwitchServerGame(const char *serverdirs)
{
	char requested[MAX_QPATH];
	char resolved[MAX_QPATH];

	if (!serverdirs || COM_GameDirMatches(serverdirs))
		return false;

	Con_Warning("Server game \"%s\" does not match local game \"%s\".\n",
		serverdirs[0] ? serverdirs : GAMENAME, COM_GetGameNames(false));

	if (!cl_autoswitchgame.value || cls.demoplayback ||
		cls.state != ca_connected || !cls.netcon)
		return false;

	/* A second mismatch after switching is a server change or bad advert. */
	if (cl_auto_reconnect.active)
	{
		Con_Warning("Automatic game switch did not resolve the server mismatch; disconnecting.\n");
		cl_auto_reconnect.active = false;
		CL_Disconnect();
		return true;
	}

	if (!serverdirs[0])
		q_strlcpy(requested, GAMENAME, sizeof(requested));
	else
	{
		if (strlen(serverdirs) >= sizeof(requested) ||
			!CL_AutoReconnect_IsSafeGame(serverdirs))
		{
			Con_Warning("Cannot automatically switch compound or invalid server game \"%s\".\n",
				serverdirs);
			return false;
		}
		q_strlcpy(requested, serverdirs, sizeof(requested));
	}

	if (!CL_AutoReconnect_ResolveInstalledGame(requested, resolved, sizeof(resolved)))
	{
		Con_Warning("Server game \"%s\" is not installed locally.\n",
			requested);
		return false;
	}

	if (!cl_connect_target[0] || !CL_AutoReconnect_IsSafeServer(cl_connect_target))
	{
		Con_Warning("Cannot preserve the original server address for an automatic game switch.\n");
		return false;
	}

	return CL_AutoReconnect_Start(resolved, cl_connect_target, 0.0, 1.0, 30.0);
}

static void CL_AutoReconnectGame_f(void)
{
	const char *game;
	const char *server;
	double delay;
	double retry_interval;
	double timeout;

	if (Cmd_Argc() < 3)
	{
		Con_Printf("qs_reconnect_game <game> <server> [delay] [retry] [timeout]\n");
		return;
	}

	if (cls.state == ca_dedicated)
		return;

	game = Cmd_Argv(1);
	server = Cmd_Argv(2);
	delay = Cmd_Argc() > 3 ? Q_atof(Cmd_Argv(3)) : 8.0;
	retry_interval = Cmd_Argc() > 4 ? Q_atof(Cmd_Argv(4)) : 2.0;
	timeout = Cmd_Argc() > 5 ? Q_atof(Cmd_Argv(5)) : 120.0;

	CL_AutoReconnect_Start(game, server, delay, retry_interval, timeout);
}

void CL_AutoReconnect_Frame(void)
{
	if (!cl_auto_reconnect.active)
		return;

	if (realtime - cl_auto_reconnect.start_time > cl_auto_reconnect.timeout)
	{
		Con_Printf("Auto reconnect to %s timed out after %.1f seconds.\n",
		           cl_auto_reconnect.server, cl_auto_reconnect.timeout);
		cl_auto_reconnect.active = false;
		if (cls.state == ca_connected && cls.signon < SIGNONS)
			CL_Disconnect();
		return;
	}

	/* Run the filesystem switch at a frame boundary, and only while active. */
	if (cl_auto_reconnect.switch_pending)
	{
		cl_auto_reconnect.switch_pending = false;
		cl_auto_reconnect_switch_command = true;
		Cmd_ExecuteString(va("game %s\n", cl_auto_reconnect.game), src_command);
		cl_auto_reconnect_switch_command = false;
		/*
		 * COM_Game_f switches filesystems synchronously, then queues quake.rc,
		 * postcfg, and migration commands.  CL_AutoReconnect_Frame runs after
		 * Cbuf_Execute, so defer the network attempt long enough for at least
		 * the following command-buffer frame(s) to finish and for the renderer
		 * reset to settle.  Preserve a longer user-specified delay.
		 */
		cl_auto_reconnect.settle_until = realtime + 0.25;
		if (cl_auto_reconnect.next_attempt_time < cl_auto_reconnect.settle_until)
			cl_auto_reconnect.next_attempt_time = cl_auto_reconnect.settle_until;
		return;
	}

	if (cls.state == ca_connected)
	{
		if (cls.signon == SIGNONS)
		{
			Con_Printf("Auto reconnect complete: %s (%s).\n",
			           cl_auto_reconnect.server, COM_GetGameNames(false));
			cl_auto_reconnect.active = false;
		}
		return;
	}

	if (realtime < cl_auto_reconnect.next_attempt_time)
		return;

	/* Do not race quake.rc/config.cfg/postcfg chains which use `wait`. */
	if (!Cbuf_IsEmpty())
	{
		cl_auto_reconnect.next_attempt_time = realtime + 0.05;
		return;
	}

	if (!COM_GameDirMatches(cl_auto_reconnect.game))
	{
		Con_Printf("Auto reconnect waiting for game \"%s\" to load locally.\n",
		           cl_auto_reconnect.game);
		cl_auto_reconnect.next_attempt_time = realtime + 1.0;
		return;
	}

	cl_auto_reconnect.attempts++;
	Con_Printf("Auto reconnect attempt %d: connect %s\n",
	           cl_auto_reconnect.attempts, cl_auto_reconnect.server);
	if (CL_TryEstablishConnection(cl_auto_reconnect.server))
	{
		SCR_BeginLoadingPlaque();
		CL_ClearSignons();
		return;
	}

	cl_auto_reconnect.next_attempt_time = realtime + cl_auto_reconnect.retry_interval;
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
			if (cls.state != ca_connected)
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
	float	f, frac;
	static double	last_lerp_log;

	f = cl.mtime[0] - cl.mtime[1];

	if (!f || cls.timedemo || (sv.active && !host_netinterval))
	{
		if (!f && !cls.timedemo && !sv.active && net_lagdebug.value &&
			cls.state == ca_connected && cls.signon == SIGNONS &&
			cl.mtime[0] > 0 && realtime - last_lerp_log > 0.5)
		{
			Con_Printf ("net_lagdebug: client interpolation collapsed mtime=%.3f cl_time=%.3f lastmsg_age=%.3f\n",
				cl.mtime[0], cl.time, realtime - cl.last_received_message);
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
				f, cl.time, realtime - cl.last_received_message);
			last_lerp_log = realtime;
		}
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
		if (net_lagdebug.value && cls.state == ca_connected && cls.signon == SIGNONS &&
			cl.time - cl.mtime[0] > CL_NetLagDebugFrameThreshold () &&
			realtime - last_lerp_log > 0.5)
		{
			Con_Printf ("net_lagdebug: client interpolation overrun over=%.3f frac=%.3f mtime_delta=%.3f lastmsg_age=%.3f\n",
				cl.time - cl.mtime[0], frac, f,
				realtime - cl.last_received_message);
			last_lerp_log = realtime;
		}
		if (frac > 1.01)
			cl.time = cl.mtime[0];
		frac = 1;
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
===============
CL_ModelTrail

Use an effectinfo/FTE model trail when one is defined, otherwise retain the
classic trail. Scripted trails are not rate limited by the classic renderer,
so advance their origin every frame.
===============
*/
static void CL_ModelTrail (entity_t *ent, const char *effectname, int type)
{
	if (PScript_EntParticleTrail (ent->trailorg, ent, effectname))
		CL_RocketTrail (ent, type);
	else
		CL_ResetTrail (ent);
}

static qboolean CL_LocalSingleplayerActive (void)
{
	return sv.active && svs.maxclients <= 1;
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

static void CL_ClearPredictionHistory (void)
{
	int i;

	for (i = 0; i < CL_MOVE_HISTORY; i++)
		cl.predicted_move_sequence[i] = -1;
	cl.net_prediction_error_last_sequence = -1;
}

static void CL_CheckPredictionError (entity_t *ent)
{
	int		ack;
	int		index;
	float	err;
	vec3_t	delta;

	if (cl.ackedmovemessages < 2)
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

	if (err < 0.25f)
		return;

	cl.net_prediction_errors++;
	if (net_lagdebug.value && cl_predict_error_log.value)
		Con_Printf ("net_lagdebug: prediction error ack=%d err=%.2f server=(%.1f %.1f %.1f) predicted=(%.1f %.1f %.1f) vel=(%.1f %.1f %.1f)\n",
			ack, err,
			ent->msg_origins[0][0], ent->msg_origins[0][1],
			ent->msg_origins[0][2],
			cl.predicted_move_origin[index][0],
			cl.predicted_move_origin[index][1],
			cl.predicted_move_origin[index][2],
			cl.predicted_move_velocity[index][0],
			cl.predicted_move_velocity[index][1],
			cl.predicted_move_velocity[index][2]);
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
	unsigned int	solidsize;

	if (CL_LocalSingleplayerActive ())
		return false;

	if (!cl_predictmove.value || cl_nopred.value || cls.demoplayback ||
		cls.state != ca_connected || cls.signon != SIGNONS ||
		!cl.worldmodel || cl.viewentity <= 0)
		return false;
	if (cl.stats[STAT_HEALTH] <= 0 || cl.ackedmovemessages <= 0)
		return false;
	if (ent != &cl.entities[cl.viewentity])
		return false;
	if ((cl.protocol_pext2 & PEXT2_REQUIRED_LATEST) != PEXT2_REQUIRED_LATEST)
		Host_Error ("Server does not support this build's movement protocol");
	if (!ent->netstate.pmovetype)
		return false;

	PMCL_SetMoveVars ();
	memset (&pmove, 0, sizeof(pmove));
	VectorCopy (ent->msg_origins[0], pmove.origin);

	solidsize = ent->netstate.solidsize;
	if (solidsize && solidsize != ES_SOLID_BSP)
		CL_PredictDecodeSolidSize (solidsize, pmove.player_mins, pmove.player_maxs);
	for (i = 0; i < 3; i++)
	{
		pmove.velocity[i] = ent->netstate.velocity[i] * (1.0f / 8.0f);
		bounds[0][i] = pmove.origin[i] + pmove.player_mins[i] - 256;
		bounds[1][i] = pmove.origin[i] + pmove.player_maxs[i] + 256;
	}
	VectorClear (pmove.gravitydir);

	raw_pmovetype = ent->netstate.pmovetype;
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
	if (ent->forcelink || (ent->lerpflags & LERP_RESETMOVE))
		CL_ClearPredictionHistory ();
	else
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
		if (pmove.cmd.seconds > 0.5f)
			pmove.cmd.seconds = 0.5f;
		PM_PlayerMove (1);
		CL_RecordPredictedMove (seq, pmove.origin, pmove.velocity);
		propagate[(seq + 1) & (CL_MOVE_HISTORY - 1)].seq = seq + 1;
		propagate[(seq + 1) & (CL_MOVE_HISTORY - 1)].waterjumptime =
			pmove.waterjumptime;
		predicted = true;
	}

	pending = cl.pendingcmd;
	VectorCopy (cl.aimangles, pending.viewangles);
	VR_UpdateCommandViewAngles (&pending);
	pmove.cmd = pending;
	if (pmove.cmd.seconds > 0.5f)
		pmove.cmd.seconds = 0.5f;
	PM_PlayerMove (1);
	predicted = true;

	if (!predicted)
		return false;

	VectorCopy (pmove.origin, ent->origin);
	VectorCopy (pmove.velocity, cl.velocity);
	cl.onground = pmove.onground;
	cl.inwater = pmove.waterlevel >= 2;
	return true;
}

/*
===============
CL_LerpAttachmentParentPose

Return the interpolated pose used for an attachment parent without changing
the parent entity. An attachment can refer to an entity later in the entity
array, so relying on parent->origin here would make the result depend on
entity ordering. Keep this in lock-step with the presentation interpolation
in CL_RelinkEntities instead.
===============
*/
static void CL_LerpAttachmentParentPose (const entity_t *ent, float frac,
	vec3_t origin, vec3_t angles)
{
	float	f, d;
	vec3_t	delta;
	int		j;

	if (ent->forcelink)
	{
		VectorCopy (ent->msg_origins[0], origin);
		VectorCopy (ent->msg_angles[0], angles);
		return;
	}

	f = frac;
	for (j = 0; j < 3; j++)
	{
		delta[j] = ent->msg_origins[0][j] - ent->msg_origins[1][j];
		if (delta[j] > 100 || delta[j] < -100)
			f = 1; // teleport: use the newest transform
	}

	// Keep the attachment parent in the same presentation pose as normal
	// MOVESTEP entities. This is render-only and deliberately does not set
	// the parent's lerp flags or otherwise alter received state.
	if (r_lerpmove.value && (ent->lerpflags & LERP_MOVESTEP))
		f = 1;

	for (j = 0; j < 3; j++)
	{
		origin[j] = ent->msg_origins[1][j] + f * delta[j];

		d = ent->msg_angles[0][j] - ent->msg_angles[1][j];
		if (d > 180)
			d -= 360;
		else if (d < -180)
			d += 360;
		angles[j] = ent->msg_angles[1][j] + f * d;
	}
}

/*
===============
CL_TransformAttachmentVector

RotMatFromAngleVector stores the entity axes in the convention used by
R_RotateForEntity. Applying a local vector through those axes gives the
same transform as the renderer, including Quake's flipped right axis.
===============
*/
static void CL_TransformAttachmentVector (vec3_t basis[3], vec3_t local,
	vec3_t world)
{
	VectorScale (basis[0], local[0], world);
	VectorMA (world, local[1], basis[1], world);
	VectorMA (world, local[2], basis[2], world);
}

/*
===============
CL_AttachEntity

Apply FTE/QSS-M UF_TAGINFO as a presentation-only parent transform. We do
not expose MD3/MD5 per-frame tags here yet, so every tagindex uses the
well-defined model-origin fallback. That preserves useful simple entity
attachments while avoiding invented tag transforms. The traversal validates
the complete parent chain before touching ent, so stale, cyclic, and invalid
attachments retain the normal unparented presentation pose.

cl.viewent is intentionally never involved: it is the sole entity that gets
the VR held-weapon transform. Attached entities stay in cl.entities, and a
child of cl.viewentity is therefore rendered as a normal exterior model.
===============
*/
enum { CL_MAX_ATTACHMENT_DEPTH = 10 };
static qboolean CL_AttachEntity (entity_t *ent, float frac)
{
	unsigned int	chain[CL_MAX_ATTACHMENT_DEPTH];
	unsigned int	tagent;
	int			childnum;
	int			depth, i, j;
	entity_t		*parent;
	vec3_t		parentorigin, parentangles;
	vec3_t		parentaxis[3], childaxis[3], worldaxis[3];
	vec3_t		localorigin, localangles;

	if (!cl.entities || !ent)
		return false;

	childnum = (int)(ent - cl.entities);
	tagent = ent->netstate.tagentity;
	depth = 0;

	/*
	 * Gather and validate first. A failed attachment must not partially move
	 * the child, otherwise a malformed server update would create visual
	 * flicker and could contaminate trail presentation.
	 */
	while (tagent)
	{
		if (depth == CL_MAX_ATTACHMENT_DEPTH ||
			tagent >= (unsigned int)cl.num_entities ||
			tagent == (unsigned int)childnum)
			return false;
		for (i = 0; i < depth; i++)
			if (chain[i] == tagent)
				return false;

		parent = &cl.entities[tagent];
		if (!parent->model || parent->msgtime != cl.mtime[0])
			return false;

		chain[depth++] = tagent;
		tagent = parent->netstate.tagentity;
	}

	/*
	 * A non-zero tagindex requires a model-tag lookup that this renderer does
	 * not expose. Deliberately fall back to the parent model origin rather
	 * than guessing an MD3/MD5 tag pose. This value is presentation data only
	 * and is never written back to netstate.
	 */
	(void)ent->netstate.tagindex;

	/*
	 * The first entry is the direct parent. Transform outward through the
	 * chain so a child of an attached parent is correctly composed even when
	 * the parent appears later in the entity array.
	 */
	for (i = 0; i < depth; i++)
	{
		parent = &cl.entities[chain[i]];
		/*
		 * Earlier entities have already received their full presentation
		 * transform (including a valid parent chain and local prediction). Use
		 * that pose directly, then stop: it is already in world space. Later
		 * entities have not been relinked yet, so reconstruct their matching
		 * interpolated pose from immutable message state.
		 */
		if (chain[i] < (unsigned int)childnum)
		{
			VectorCopy (parent->origin, parentorigin);
			VectorCopy (parent->angles, parentangles);
		}
		else
			CL_LerpAttachmentParentPose (parent, frac, parentorigin, parentangles);

		/*
		 * Alias-family entities (including the current MD3/MD5 replacement
		 * path) use the historical Quake pitch convention. Match the existing
		 * vkQuake/QSS-M attachment convention before composing axes.
		 */
		if (parent->model->type == mod_alias)
			parentangles[PITCH] *= -1;
		VectorCopy (ent->angles, localangles);
		if (ent->model && ent->model->type == mod_alias)
			localangles[PITCH] *= -1;

		RotMatFromAngleVector (parentangles, parentaxis);
		RotMatFromAngleVector (localangles, childaxis);

		CL_TransformAttachmentVector (parentaxis, ent->origin, localorigin);
		VectorAdd (parentorigin, localorigin, ent->origin);

		for (j = 0; j < 3; j++)
			CL_TransformAttachmentVector (parentaxis, childaxis[j], worldaxis[j]);
		AngleVectorFromRotMat (worldaxis, ent->angles);
		if (ent->model && ent->model->type == mod_alias)
			ent->angles[PITCH] *= -1;

		if (chain[i] < (unsigned int)childnum)
			break;
	}

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
		if (ent->msgtime != cl.mtime[0])
		{
			ent->model = NULL;
			ent->lerpflags |= LERP_RESETMOVE | LERP_RESETANIM;
			InvalidateTraceLineCache ();
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

		// UF_TAGINFO is a presentation transform only. Invalid parent input
		// is not rendered, matching vkQuake/QSS-M rather than exposing a raw
		// local-space child in the world. It never changes network/prediction
		// state. Reset renderer-side movement smoothing so the composed pose is
		// not smoothed a second time after its parent transform.
		if (ent->netstate.tagentity)
		{
			if (!CL_AttachEntity (ent, frac))
				continue;
			ent->lerpflags |= LERP_RESETMOVE;
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
			CL_ModelTrail (ent, "TR_BLOOD", 2);
		else if (ent->model->flags & EF_ZOMGIB)
			CL_ModelTrail (ent, "TR_SLIGHTBLOOD", 4);
		else if (ent->model->flags & EF_TRACER)
			CL_ModelTrail (ent, "TR_WIZSPIKE", 3);
		else if (ent->model->flags & EF_TRACER2)
			CL_ModelTrail (ent, "TR_KNIGHTSPIKE", 5);
		else if (ent->model->flags & EF_ROCKET)
		{
			CL_ModelTrail (ent, "TR_ROCKET", 0);
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->radius = 200;
			dl->die = cl.time + 0.01;
		}
		else if (ent->model->flags & EF_GRENADE)
			CL_ModelTrail (ent, "TR_GRENADE", 1);
		else if (ent->model->flags & EF_TRACER3)
			CL_ModelTrail (ent, "TR_VORESPIKE", 6);
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

void CL_ClearPendingCmd (void)
{
	Q_memset(&cl.pendingcmd, 0, sizeof(cl.pendingcmd));
	VectorCopy(vec3_origin, cl.accummoves);
	VectorCopy(vec3_origin, cl.vr_roomscalemove_accum);
}

static void CL_AccumulateVRRoomScaleMove (void)
{
	if (vr_enabled.value && (int)vr_aimmode.value == VR_AIMMODE_CONTROLLER)
		VectorAdd (cl.vr_roomscalemove_accum, vr_room_scale_move,
			cl.vr_roomscalemove_accum);
}

void CL_AccumulateCmd (void)
{
	if (cls.state != ca_connected || cls.signon != SIGNONS)
	{
		CL_ClearPendingCmd ();
		cl.lastcmdtime = cl.mtime[0] > 0 ? cl.mtime[0] : cl.time;
		return;
	}

	CL_AdjustAngles ();
	CL_BaseMove (&cl.pendingcmd, false);
	IN_Move (&cl.pendingcmd);
	VR_Move (&cl.pendingcmd);
	CL_AccumulateVRRoomScaleMove ();
	CL_FinishMove (&cl.pendingcmd, false);
	VectorCopy (cl.aimangles, cl.pendingcmd.viewangles);
	VR_UpdateCommandViewAngles (&cl.pendingcmd);
}

/*
=================
CL_SendCmd
=================
*/
void CL_SendCmd (void)
{
	usercmd_t		cmd;

	if (cls.state != ca_connected)
		return;

	CL_BaseMove (&cmd, true);
	IN_Move (&cmd);
	VR_Move (&cmd);
	CL_FinishMove (&cmd, true);

	if (cl.qcvm.extfuncs.CSQC_Input_Frame)
	{
		PR_SwitchQCVM (&cl.qcvm);
		PR_GetSetInputs (&cmd, true);
		PR_ExecuteProgram (cl.qcvm.extfuncs.CSQC_Input_Frame);
		PR_GetSetInputs (&cmd, false);
		PR_SwitchQCVM (NULL);
	}

	if (cls.signon == SIGNONS)
	{
	// send the unreliable message
		CL_SendMove (&cmd);
		CL_ClearPendingCmd ();
	}
	else
	{
		CL_SendMove (NULL);
		cmd.seconds = 0;
	}
	cl.pendingcmd.seconds = 0;
	cl.lastcmdtime = cmd.servertime;

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
	Cvar_RegisterVariable (&cl_iDrive);
	Cvar_RegisterVariable (&cl_predictmove);
	Cvar_RegisterVariable (&cl_nopred);
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
	Cvar_RegisterVariable (&cl_autoswitchgame);
	Cvar_RegisterVariable (&cl_extrapolate);
	Cvar_RegisterVariable (&cl_extrapolate_adaptive);
	Cvar_RegisterVariable (&cl_extrapolate_adaptive_max);
	Cvar_RegisterVariable (&cl_extrapolate_adaptive_time);
	Cvar_RegisterVariable (&cl_net_lerpbuffer);
	Cvar_RegisterVariable (&cl_net_lerpbuffer_adaptive);
	Cvar_RegisterVariable (&cl_net_lerpbuffer_adaptive_max);
	Cvar_RegisterVariable (&cl_net_lerpbuffer_adaptive_time);
	Cvar_RegisterVariable (&cl_predict_smooth);
	Cvar_RegisterVariable (&cl_predict_smooth_time);
	Cvar_RegisterVariable (&cl_predict_smooth_min);
	Cvar_RegisterVariable (&cl_predict_smooth_max);
	Cmd_AddCommand ("cl_migrate_network_defaults", CL_MigrateNetworkDefaults_f);
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

	Cmd_AddCommand ("qs_reconnect_game", CL_AutoReconnectGame_f);
	Cmd_AddCommand_ServerCommand ("st", CL_SetStat_f);
	Cmd_AddCommand_ServerCommand ("sts", CL_SetStatString_f);
}
