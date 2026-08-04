/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
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
// host.c -- coordinates spawning and killing of local servers

#include "quakedef.h"
#include "bgmusic.h"
#include <setjmp.h>
#include "vr.h"
#include "pmove.h"
#include "debug_log.h"
#include "addon_catalog.h"

/*

A server can allways be started, even if the system started out as a client
to a remote system.

A client can NOT be started if the system started as a dedicated server.

Memory is cleared / released when a server or client begins, not when they end.

*/

quakeparms_t *host_parms;

qboolean	host_initialized;		// true if into command execution

double		host_frametime;
float		host_netinterval = 1.0/72;
double		realtime;				// without any filtering or bounding
double		oldrealtime;			// last frame run

int		host_framecount;

int		host_hunklevel;

int		minimum_memory;

client_t	*host_client;			// current client

jmp_buf 	host_abortserver;

byte		*host_colormap;

cvar_t	host_framerate = {"host_framerate","0",CVAR_NONE};	// set for slow motion
cvar_t	host_speeds = {"host_speeds","0",CVAR_NONE};			// set for running times
cvar_t	host_maxfps = {"host_maxfps", "250", CVAR_ARCHIVE}; //johnfitz
cvar_t	host_timescale = {"host_timescale", "0", CVAR_NONE}; //johnfitz
cvar_t	cl_netfps = {"cl_netfps", "0", CVAR_NONE};	// retired alias; QSS-M-style pacing uses host_maxfps
cvar_t	max_edicts = {"max_edicts", "15000", CVAR_NONE}; // QSS-M default; removed CVAR_ARCHIVE
cvar_t	cl_nocsqc = {"cl_nocsqc", "0", CVAR_NONE};

cvar_t	sys_ticrate = {"sys_ticrate","0.05",CVAR_NOTIFY|CVAR_SERVERINFO}; // dedicated server -- match QSS-M
cvar_t	serverprofile = {"serverprofile","0",CVAR_NONE};

cvar_t	fraglimit = {"fraglimit","0",CVAR_NOTIFY|CVAR_SERVERINFO};
cvar_t	timelimit = {"timelimit","0",CVAR_NOTIFY|CVAR_SERVERINFO};
cvar_t	teamplay = {"teamplay","0",CVAR_NOTIFY|CVAR_SERVERINFO};
cvar_t	samelevel = {"samelevel","0",CVAR_NONE};
cvar_t	noexit = {"noexit","0",CVAR_NOTIFY|CVAR_SERVERINFO};
cvar_t	skill = {"skill","1",CVAR_NONE};			// 0 - 3
cvar_t	deathmatch = {"deathmatch","0",CVAR_NONE};	// 0, 1, or 2
cvar_t	coop = {"coop","0",CVAR_NONE};			// 0 or 1
cvar_t	sv_nofriendlyfire = {"sv_nofriendlyfire","0",CVAR_NOTIFY|CVAR_SERVERINFO};
cvar_t	sv_coop_noplayerclip = {"sv_coop_noplayerclip","1",CVAR_NOTIFY|CVAR_SERVERINFO};
cvar_t	sv_save_multiplayer = {"sv_save_multiplayer","1",CVAR_NONE};
cvar_t	sv_cmdfile = {"sv_cmdfile","",CVAR_NONE};

cvar_t	pausable = {"pausable","1",CVAR_NONE};

cvar_t	developer = {"developer","0",CVAR_NONE};

cvar_t	temp1 = {"temp1","0",CVAR_NONE};
extern cvar_t cl_portpingprobe_enable;
extern cvar_t net_lagdebug;
extern cvar_t net_sameip_stale_timeout;
extern cvar_t sv_inputtimeout;
extern cvar_t sv_gravity;
extern cvar_t sv_nqplayerphysics;
extern cvar_t sv_trustedmovement;
extern cvar_t sv_pmove_legacy_preserve_qc_velocity;
extern cvar_t sv_predict_nqmovement;
extern cvar_t sv_gameplayfix_spawnbeforethinks;
extern cvar_t sv_replacement_maxpackets;
extern cvar_t sv_maxpacketsize;
extern cvar_t sv_netsort;

cvar_t devstats = {"devstats","0",CVAR_NONE}; //johnfitz -- track developer statistics that vary every frame

cvar_t	campaign = {"campaign","0",CVAR_NONE}; // for the 2021 rerelease
cvar_t	horde = {"horde","0",CVAR_NONE}; // for the 2021 rerelease
cvar_t	sv_cheats = {"sv_cheats","0",CVAR_NONE}; // for the 2021 rerelease

devstats_t dev_stats, dev_peakstats;
overflowtimes_t dev_overflows; //this stores the last time overflow messages were displayed, not the last time overflows occured

/*
================
Max_Edicts_f -- johnfitz
================
*/
static void Max_Edicts_f (cvar_t *var)
{
	//TODO: clamp it here?
	if (cls.state == ca_connected || sv.active)
		Con_Printf ("Changes to max_edicts will not take effect until the next time a map is loaded.\n");
}

static qboolean Host_ValueMatchesOldDefault (float value, float old_default)
{
	const float epsilon = 0.0001f;

	return value > old_default - epsilon && value < old_default + epsilon;
}

static qboolean Host_CommandLineSetsCvar (const char *name)
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

static void Host_MigrateNetworkDefaults_f (void)
{
	if (!Host_CommandLineSetsCvar ("cl_netfps") &&
		!Host_ValueMatchesOldDefault (cl_netfps.value, 0.0f))
		Cvar_SetQuick (&cl_netfps, "0");
	if (!Host_CommandLineSetsCvar ("host_maxfps") &&
		Host_ValueMatchesOldDefault (host_maxfps.value, 72.0f))
		Cvar_SetQuick (&host_maxfps, "250");
	if (!Host_CommandLineSetsCvar ("host_framerate") &&
		!Host_ValueMatchesOldDefault (host_framerate.value, 0.0f))
		Cvar_SetQuick (&host_framerate, "0");
	if (!Host_CommandLineSetsCvar ("host_timescale") &&
		!Host_ValueMatchesOldDefault (host_timescale.value, 0.0f))
		Cvar_SetQuick (&host_timescale, "0");
	if (!Host_CommandLineSetsCvar ("cl_portpingprobe_enable") &&
		!Host_ValueMatchesOldDefault (cl_portpingprobe_enable.value, 0.0f))
		Cvar_SetQuick (&cl_portpingprobe_enable, "0");
	if (!Host_CommandLineSetsCvar ("sv_nqplayerphysics") &&
		!Host_ValueMatchesOldDefault (sv_nqplayerphysics.value, 1.0f))
		Cvar_SetQuick (&sv_nqplayerphysics, "1");
	if (!Host_CommandLineSetsCvar ("sv_trustedmovement") &&
		!Host_ValueMatchesOldDefault (sv_trustedmovement.value, 0.0f))
		Cvar_SetQuick (&sv_trustedmovement, "0");
	if (!Host_CommandLineSetsCvar ("sv_predict_nqmovement") &&
		!Host_ValueMatchesOldDefault (sv_predict_nqmovement.value, 0.0f))
		Cvar_SetQuick (&sv_predict_nqmovement, "0");
	if (!Host_CommandLineSetsCvar ("sv_pmove_legacy_preserve_qc_velocity") &&
		!Host_ValueMatchesOldDefault (sv_pmove_legacy_preserve_qc_velocity.value, 1.0f))
		Cvar_SetQuick (&sv_pmove_legacy_preserve_qc_velocity, "1");
	if (!Host_CommandLineSetsCvar ("sv_gameplayfix_spawnbeforethinks") &&
		!Host_ValueMatchesOldDefault (sv_gameplayfix_spawnbeforethinks.value, 0.0f))
		Cvar_SetQuick (&sv_gameplayfix_spawnbeforethinks, "0");
	if (!Host_CommandLineSetsCvar ("sv_inputtimeout") &&
		!Host_ValueMatchesOldDefault (sv_inputtimeout.value, 0.0f))
		Cvar_SetQuick (&sv_inputtimeout, "0");
	if (!Host_CommandLineSetsCvar ("sv_replacement_maxpackets") &&
		!Host_ValueMatchesOldDefault (sv_replacement_maxpackets.value, 0.0f))
		Cvar_SetQuick (&sv_replacement_maxpackets, "0");
	if (!Host_CommandLineSetsCvar ("sv_maxpacketsize") &&
		!Host_ValueMatchesOldDefault (sv_maxpacketsize.value, 1400.0f))
		Cvar_SetQuick (&sv_maxpacketsize, "1400");
	if (!Host_CommandLineSetsCvar ("sv_netsort") &&
		!Host_ValueMatchesOldDefault (sv_netsort.value, 1.0f))
		Cvar_SetQuick (&sv_netsort, "1");
	if (!Host_CommandLineSetsCvar ("max_edicts") && max_edicts.value < 15000.0f)
		Cvar_SetQuick (&max_edicts, "15000");
	if (!Host_CommandLineSetsCvar ("net_lagdebug") &&
		!Host_ValueMatchesOldDefault (net_lagdebug.value, 0.0f))
		Cvar_SetQuick (&net_lagdebug, "0");
	if (!Host_CommandLineSetsCvar ("net_sameip_stale_timeout") &&
		!Host_ValueMatchesOldDefault (net_sameip_stale_timeout.value, 3.0f))
		Cvar_SetQuick (&net_sameip_stale_timeout, "3.0");
	if (!Host_CommandLineSetsCvar ("sv_gravity") &&
		!Host_ValueMatchesOldDefault (sv_gravity.value, 800.0f))
		Cvar_SetQuick (&sv_gravity, "800");
}

#define NETFPS_PROBE_DEFAULT_SECONDS	5.0
#define NETFPS_PROBE_MIN_SECONDS		1.0
#define NETFPS_PROBE_MAX_SECONDS		30.0
#define NETFPS_PROBE_DEFAULT_TARGET		72.0
#define NETFPS_PROBE_MIN_TARGET			10.0
#define NETFPS_PROBE_MAX_TARGET			150.0
#define NETFPS_PROBE_MAX_SAMPLES		32768

typedef struct
{
	qboolean	active;
	double		start_time;
	double		last_frame_time;
	double		duration;
	double		target_netfps;
	int			frames;
	int			intervals;
	double		sum_dt;
	double		min_dt;
	double		max_dt;
	int			sample_count;
	qboolean	sample_limit_hit;
	double		samples[NETFPS_PROBE_MAX_SAMPLES];
} netfps_probe_t;

typedef struct
{
	int			value;
	int			sends;
	double		effective_netfps;
	double		avg_send_dt;
	double		min_send_dt;
	double		max_send_dt;
	double		avg_late;
	double		max_late;
} netfps_probe_eval_t;

static netfps_probe_t netfps_probe;

/*
================
Max_Fps_f -- ericw
================
*/
static void Max_Fps_f (cvar_t *var)
{
	if (var->value < 0)
	{
		if (!host_netinterval)
			Con_Printf ("Using renderer/network isolation.\n");
		host_netinterval = 1/-var->value;
		if (host_netinterval > 1/10.f)
			host_netinterval = 1/10.f;
		if (host_netinterval < 1/150.f)
			host_netinterval = 1/150.f;
	}
	else if (var->value > 72 || var->value <= 0)
	{
		if (!host_netinterval)
			Con_Printf ("Using renderer/network isolation.\n");
		host_netinterval = 1.0/72;
	}
	else
	{
		if (host_netinterval)
			Con_Printf ("Disabling renderer/network isolation.\n");
		host_netinterval = 0;

		if (var->value > 72)
			Con_Warning ("host_maxfps above 72 breaks physics.\n");
	}
}

/*
================
Host_NetfpsProbe

Samples rendered host-frame cadence and simulates the renderer/network
isolation accumulator to suggest a negative host_maxfps value.
================
*/
static void Host_NetfpsProbe_Usage (void)
{
	Con_Printf ("usage: netfps_probe [seconds] [target_netfps]\n");
	Con_Printf ("       netfps_probe stop\n");
	Con_Printf ("Defaults: %.0f seconds, %.0f target netfps.\n",
				NETFPS_PROBE_DEFAULT_SECONDS, NETFPS_PROBE_DEFAULT_TARGET);
}

static int Host_NetfpsProbe_CompareDouble (const void *a, const void *b)
{
	double da = *(const double *)a;
	double db = *(const double *)b;

	if (da < db)
		return -1;
	if (da > db)
		return 1;
	return 0;
}

static void Host_NetfpsProbe_Evaluate (int value, netfps_probe_eval_t *eval)
{
	double interval = 1.0 / value;
	double accum = 0.0;
	double total = 0.0;
	double send_sum = 0.0;
	double late_sum = 0.0;
	int i;

	memset (eval, 0, sizeof(*eval));
	eval->value = value;

	for (i = 0; i < netfps_probe.sample_count; i++)
	{
		double dt = netfps_probe.samples[i];

		total += dt;
		accum += dt;
		if (accum >= interval)
		{
			double late = accum - interval;

			eval->sends++;
			send_sum += accum;
			late_sum += late;
			if (!eval->min_send_dt || accum < eval->min_send_dt)
				eval->min_send_dt = accum;
			if (accum > eval->max_send_dt)
				eval->max_send_dt = accum;
			if (late > eval->max_late)
				eval->max_late = late;
			accum = 0.0;
		}
	}

	if (total > 0.0)
		eval->effective_netfps = eval->sends / total;
	if (eval->sends > 0)
	{
		eval->avg_send_dt = send_sum / eval->sends;
		eval->avg_late = late_sum / eval->sends;
	}
}

static qboolean Host_NetfpsProbe_EvalBetter (const netfps_probe_eval_t *candidate, const netfps_probe_eval_t *best)
{
	if (!candidate->sends)
		return false;
	if (!best->sends)
		return true;
	if (candidate->effective_netfps > best->effective_netfps + 0.25)
		return true;
	if (candidate->effective_netfps < best->effective_netfps - 0.25)
		return false;
	if (candidate->max_late < best->max_late - 0.00025)
		return true;
	if (candidate->max_late > best->max_late + 0.00025)
		return false;
	return candidate->avg_late < best->avg_late;
}

static qboolean Host_NetfpsProbe_EvalCloser (const netfps_probe_eval_t *candidate, const netfps_probe_eval_t *best, double target)
{
	double candidate_diff, best_diff;

	if (!candidate->sends)
		return false;
	if (!best->sends)
		return true;

	candidate_diff = fabs (candidate->effective_netfps - target);
	best_diff = fabs (best->effective_netfps - target);

	if (candidate_diff < best_diff - 0.25)
		return true;
	if (candidate_diff > best_diff + 0.25)
		return false;
	if (candidate->max_late < best->max_late - 0.00025)
		return true;
	if (candidate->max_late > best->max_late + 0.00025)
		return false;
	return candidate->value < best->value;
}

static void Host_NetfpsProbe_Report (void)
{
	double avg_dt, avgfps, minfps, maxfps, p95_dt = 0.0, p95fps = 0.0;
	double target;
	int target_value, safe_target, i;
	netfps_probe_eval_t requested, safe, closest, candidate;

	if (netfps_probe.intervals < 10 || netfps_probe.sum_dt <= 0.0 || netfps_probe.sample_count < 10)
	{
		Con_Printf ("netfps_probe: not enough frame samples; try again while the game is rendering normally.\n");
		return;
	}

	avg_dt = netfps_probe.sum_dt / netfps_probe.intervals;
	avgfps = netfps_probe.intervals / netfps_probe.sum_dt;
	minfps = (netfps_probe.max_dt > 0.0) ? 1.0 / netfps_probe.max_dt : 0.0;
	maxfps = (netfps_probe.min_dt > 0.0) ? 1.0 / netfps_probe.min_dt : 0.0;
	target = CLAMP (NETFPS_PROBE_MIN_TARGET, netfps_probe.target_netfps, NETFPS_PROBE_MAX_TARGET);
	target_value = CLAMP ((int)NETFPS_PROBE_MIN_TARGET, Q_rint(target), (int)NETFPS_PROBE_MAX_TARGET);
	safe_target = q_min (target_value, 72);

	Host_NetfpsProbe_Evaluate (target_value, &requested);
	memset (&safe, 0, sizeof(safe));
	for (i = (int)NETFPS_PROBE_MIN_TARGET; i <= safe_target; i++)
	{
		Host_NetfpsProbe_Evaluate (i, &candidate);
		if (Host_NetfpsProbe_EvalBetter (&candidate, &safe))
			safe = candidate;
	}
	memset (&closest, 0, sizeof(closest));
	for (i = (int)NETFPS_PROBE_MIN_TARGET; i <= (int)NETFPS_PROBE_MAX_TARGET; i++)
	{
		Host_NetfpsProbe_Evaluate (i, &candidate);
		if (Host_NetfpsProbe_EvalCloser (&candidate, &closest, target))
			closest = candidate;
	}

	qsort (netfps_probe.samples, netfps_probe.sample_count, sizeof(netfps_probe.samples[0]), Host_NetfpsProbe_CompareDouble);
	p95_dt = netfps_probe.samples[(int)(0.95 * (netfps_probe.sample_count - 1))];
	if (p95_dt > 0.0)
		p95fps = 1.0 / p95_dt;

	Con_Printf ("\nnetfps_probe result\n");
	Con_Printf ("netfps_probe: sampled %.1f seconds, %d frames.\n",
				netfps_probe.sum_dt, netfps_probe.frames);
	Con_Printf ("netfps_probe: render avg %.1f fps (%.2f ms).\n",
				avgfps, avg_dt * 1000.0);
	Con_Printf ("netfps_probe: render floor %.1f fps 95%%, worst %.1f fps, peak %.1f fps.\n",
				p95fps, minfps, maxfps);

	Con_Printf ("\nnetfps_probe: requested host_maxfps -%d estimates %.1f netfps.\n",
				target_value, requested.effective_netfps);
	Con_Printf ("netfps_probe: requested avg send %.2f ms, max late %.2f ms.\n",
				requested.avg_send_dt * 1000.0, requested.max_late * 1000.0);

	Con_Printf ("\n");
	Con_Printf ("netfps_probe: conservative command: host_maxfps -%d\n", safe.value);
	Con_Printf ("netfps_probe: conservative estimate: %.1f netfps.\n",
				safe.effective_netfps);
	Con_Printf ("netfps_probe: conservative avg send %.2f ms, max late %.2f ms.\n",
				safe.avg_send_dt * 1000.0, safe.max_late * 1000.0);
	if (closest.value != safe.value)
	{
		Con_Printf ("\n");
		Con_Printf ("netfps_probe: closest-to-target command: host_maxfps -%d\n", closest.value);
		Con_Printf ("netfps_probe: closest estimate: %.1f netfps.\n",
					closest.effective_netfps);
		Con_Printf ("netfps_probe: closest avg send %.2f ms, max late %.2f ms.\n",
					closest.avg_send_dt * 1000.0, closest.max_late * 1000.0);
		if (closest.value > 72)
			Con_Printf ("netfps_probe: note: closest-to-target is above 72 and can affect Quake physics; use conservative for normal play.\n");
	}
	if (safe.effective_netfps < target - 0.5)
	{
		Con_Printf ("\n");
		Con_Printf ("netfps_probe: note: values up to 72 cannot cleanly reach %.1f netfps with this render cadence.\n", target);
	}
	if (netfps_probe.sample_limit_hit)
		Con_Printf ("netfps_probe: note: sample buffer filled; result used the first %d intervals.\n", NETFPS_PROBE_MAX_SAMPLES);
	Con_Printf ("\nnetfps_probe: final recommendation: ^mhost_maxfps^m -^g%d^d\n", safe.value);
	Con_Printf ("\n");
}

static void Host_NetfpsProbe_Frame (void)
{
	double now, dt;

	if (!netfps_probe.active)
		return;

	now = realtime;
	if (netfps_probe.last_frame_time > 0.0)
	{
		dt = now - netfps_probe.last_frame_time;
		if (dt > 0.0 && dt < 1.0)
		{
			netfps_probe.intervals++;
			netfps_probe.sum_dt += dt;
			if (netfps_probe.sample_count < NETFPS_PROBE_MAX_SAMPLES)
				netfps_probe.samples[netfps_probe.sample_count++] = dt;
			else
				netfps_probe.sample_limit_hit = true;
			if (!netfps_probe.min_dt || dt < netfps_probe.min_dt)
				netfps_probe.min_dt = dt;
			if (dt > netfps_probe.max_dt)
				netfps_probe.max_dt = dt;
		}
	}
	netfps_probe.last_frame_time = now;
	netfps_probe.frames++;

	if (now - netfps_probe.start_time >= netfps_probe.duration)
	{
		netfps_probe.active = false;
		Host_NetfpsProbe_Report ();
	}
}

static void Host_NetfpsProbe_f (void)
{
	int argc = Cmd_Argc ();
	double duration = NETFPS_PROBE_DEFAULT_SECONDS;
	double target = NETFPS_PROBE_DEFAULT_TARGET;

	if (argc >= 2 &&
		(!q_strcasecmp (Cmd_Argv(1), "stop") ||
		 !q_strcasecmp (Cmd_Argv(1), "cancel")))
	{
		if (netfps_probe.active)
		{
			netfps_probe.active = false;
			Con_Printf ("netfps_probe: stopped.\n");
		}
		else
			Con_Printf ("netfps_probe: not running.\n");
		return;
	}

	if (argc > 3 ||
		(argc >= 2 && (!q_strcasecmp (Cmd_Argv(1), "?") ||
					   !q_strcasecmp (Cmd_Argv(1), "help"))))
	{
		Host_NetfpsProbe_Usage ();
		return;
	}

	if (cls.state != ca_connected || cls.signon != SIGNONS || cls.demoplayback || cls.timedemo)
	{
		Con_Printf ("netfps_probe: run this after connecting and entering a map.\n");
		if (cls.demoplayback || cls.timedemo)
			Con_Printf ("netfps_probe: demo playback/timedemo timing is not useful for netfps calibration.\n");
		return;
	}

	if (argc >= 2)
		duration = Q_atof (Cmd_Argv(1));
	if (argc >= 3)
		target = Q_atof (Cmd_Argv(2));

	if (duration <= 0.0 || target <= 0.0)
	{
		Host_NetfpsProbe_Usage ();
		return;
	}

	memset (&netfps_probe, 0, sizeof(netfps_probe));
	netfps_probe.active = true;
	netfps_probe.start_time = realtime;
	netfps_probe.duration = CLAMP (NETFPS_PROBE_MIN_SECONDS, duration, NETFPS_PROBE_MAX_SECONDS);
	netfps_probe.target_netfps = CLAMP (NETFPS_PROBE_MIN_TARGET, target, NETFPS_PROBE_MAX_TARGET);

	Con_Printf ("netfps_probe: sampling %.1f seconds for %.1f target netfps\n",
				netfps_probe.duration, netfps_probe.target_netfps);
	if (host_maxfps.value > 0)
		Con_Printf ("netfps_probe: host_maxfps is currently capping render fps; this will be included in the measurement.\n");
}

/*
================
Host_EndGame
================
*/
void Host_EndGame (const char *message, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr,message);
	q_vsnprintf (string, sizeof(string), message, argptr);
	va_end (argptr);
	Con_DPrintf ("Host_EndGame: %s\n",string);

	PR_SwitchQCVM(NULL);

	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.state == ca_dedicated)
		Sys_Error ("Host_EndGame: %s\n",string);	// dedicated servers exit

	if (cls.demonum != -1 && !cls.timedemo)
		CL_NextDemo ();
	else
		CL_Disconnect ();

	longjmp (host_abortserver, 1);
}

/*
================
Host_Error

This shuts down both the client and server
================
*/
void Host_Error (const char *error, ...)
{
	va_list		argptr;
	char		string[1024];
	static	qboolean inerror = false;

	if (inerror)
		Sys_Error ("Host_Error: recursively entered");
	inerror = true;

	PR_SwitchQCVM(NULL);

	SCR_EndLoadingPlaque ();		// reenable screen updates

	va_start (argptr,error);
	q_vsnprintf (string, sizeof(string), error, argptr);
	va_end (argptr);
	DebugLog("Host_Error: %s\n", string);
	Con_Printf ("Host_Error: %s\n",string);

	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.state == ca_dedicated)
		Sys_Error ("Host_Error: %s\n",string);	// dedicated servers exit

	CL_Disconnect ();
	cls.demonum = -1;
	cl.intermission = 0; //johnfitz -- for errors during intermissions (changelevel with no map found, etc.)

	inerror = false;

	longjmp (host_abortserver, 1);
}

/*
================
Host_FindMaxClients
================
*/
void	Host_FindMaxClients (void)
{
	int		i;

	svs.maxclients = 1;

	i = COM_CheckParm ("-dedicated");
	if (i)
	{
		cls.state = ca_dedicated;
		if (i != (com_argc - 1))
		{
			svs.maxclients = Q_atoi (com_argv[i+1]);
		}
		else
			svs.maxclients = 8;
	}
	else
		cls.state = ca_disconnected;

	i = COM_CheckParm ("-listen");
	if (i)
	{
		if (cls.state == ca_dedicated)
			Sys_Error ("Only one of -dedicated or -listen can be specified");
		if (i != (com_argc - 1))
			svs.maxclients = Q_atoi (com_argv[i+1]);
		else
			svs.maxclients = 8;
	}
	if (svs.maxclients < 1)
		svs.maxclients = 8;
	else if (svs.maxclients > MAX_SCOREBOARD)
		svs.maxclients = MAX_SCOREBOARD;

	svs.maxclientslimit = svs.maxclients;
	if (svs.maxclientslimit < 4)
		svs.maxclientslimit = 4;
	svs.clients = (struct client_s *) Hunk_AllocName (svs.maxclientslimit*sizeof(client_t), "clients");

	if (svs.maxclients > 1)
		Cvar_SetQuick (&deathmatch, "1");
	else
		Cvar_SetQuick (&deathmatch, "0");
}

void Host_Version_f (void)
{
	Con_Printf ("Quake Version %1.2f\n", VERSION);
	Con_Printf ("QuakeSpasm Version " QUAKESPASM_VER_STRING "\n");
	Con_Printf ("Exe: " __TIME__ " " __DATE__ "\n");
}

/* cvar callback functions : */
void Host_Callback_Notify (cvar_t *var)
{
	if (sv.active)
		SV_BroadcastPrintf ("\"%s\" changed to \"%s\"\n", var->name, var->string);
}

/*
=======================
Host_InitLocal
======================
*/
void Host_InitLocal (void)
{
	Cmd_AddCommand ("version", Host_Version_f);
	Cmd_AddCommand ("host_migrate_network_defaults", Host_MigrateNetworkDefaults_f);
	Cmd_AddCommand ("netfps_probe", Host_NetfpsProbe_f);

	Host_InitCommands ();

	Cvar_RegisterVariable (&host_framerate);
	Cvar_RegisterVariable (&host_speeds);
	Cvar_RegisterVariable (&host_maxfps); //johnfitz
	Cvar_SetCallback (&host_maxfps, Max_Fps_f);
	Cvar_RegisterVariable (&host_timescale); //johnfitz
	Cvar_RegisterVariable (&cl_netfps); // retired alias; host_maxfps controls QSS-M-style pacing

	Cvar_RegisterVariable (&cl_nocsqc);
	Cvar_RegisterVariable (&max_edicts); //johnfitz
	Cvar_SetCallback (&max_edicts, Max_Edicts_f);
	Cvar_RegisterVariable (&devstats); //johnfitz

	Cvar_RegisterVariable (&sys_ticrate);
	Cvar_RegisterVariable (&sys_throttle);
	Cvar_RegisterVariable (&serverprofile);

	Cvar_RegisterVariable (&fraglimit);
	Cvar_RegisterVariable (&timelimit);
	Cvar_RegisterVariable (&teamplay);
	Cvar_SetCallback (&fraglimit, Host_Callback_Notify);
	Cvar_SetCallback (&timelimit, Host_Callback_Notify);
	Cvar_SetCallback (&teamplay, Host_Callback_Notify);
	Cvar_RegisterVariable (&samelevel);
	Cvar_RegisterVariable (&noexit);
	Cvar_SetCallback (&noexit, Host_Callback_Notify);
	Cvar_RegisterVariable (&skill);
	Cvar_RegisterVariable (&developer);
	Cvar_RegisterVariable (&coop);
	Cvar_RegisterVariable (&sv_nofriendlyfire);
	Cvar_RegisterVariable (&sv_coop_noplayerclip);
	Cvar_RegisterVariable (&sv_save_multiplayer);
	Cvar_RegisterVariable (&sv_cmdfile);
	Cvar_RegisterVariable (&deathmatch);

	Cvar_RegisterVariable (&campaign);
	Cvar_RegisterVariable (&horde);
	Cvar_RegisterVariable (&sv_cheats);

	Cvar_RegisterVariable (&pausable);

	Cvar_RegisterVariable (&temp1);

	Host_FindMaxClients ();
}


/*
===============
Host_WriteConfiguration

Writes key bindings and archived cvars to config.cfg
===============
*/
void Host_WriteConfiguration (void)
{
	FILE	*f;

// dedicated servers initialize the host but don't parse and set the
// config.cfg cvars
	if (host_initialized && !isDedicated && !host_parms->errstate)
	{
		f = fopen (va("%s/config.cfg", com_gamedir), "w");
		if (!f)
		{
			Con_Printf ("Couldn't write config.cfg.\n");
			return;
		}

		//VID_SyncCvars (); //johnfitz -- write actual current mode to config file, in case cvars were messed with

		Key_WriteBindings (f);
		Cvar_WriteVariables (f);

		//johnfitz -- extra commands to preserve state
		fprintf (f, "vid_restart\n");
		if (in_mlook.state & 1) fprintf (f, "+mlook\n");
		//johnfitz

		fclose (f);
	}
}


/*
=================
SV_ClientPrintf

Sends text across to be displayed
FIXME: make this just a stuffed echo?
=================
*/
void SV_ClientPrintf (const char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr,fmt);
	q_vsnprintf (string, sizeof(string), fmt,argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_print);
	MSG_WriteString (&host_client->message, string);
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients
=================
*/
void SV_BroadcastPrintf (const char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];
	int			i;

	va_start (argptr,fmt);
	q_vsnprintf (string, sizeof(string), fmt, argptr);
	va_end (argptr);

	for (i = 0; i < svs.maxclients; i++)
	{
		if (svs.clients[i].active && svs.clients[i].spawned)
		{
			MSG_WriteByte (&svs.clients[i].message, svc_print);
			MSG_WriteString (&svs.clients[i].message, string);
		}
	}
}

/*
=================
Host_ClientCommands

Send text over to the client to be executed
=================
*/
void Host_ClientCommands (const char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr,fmt);
	q_vsnprintf (string, sizeof(string), fmt, argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_stufftext);
	MSG_WriteString (&host_client->message, string);
}

/*
=====================
SV_DropClient

Called when the player is getting totally kicked off the host
if (crash = true), don't bother sending signofs
=====================
*/
void SV_DropClient (qboolean crash)
{
	int		saveSelf;
	int		i;
	int		dropped_slot;
	client_t *client;

	dropped_slot = host_client - svs.clients;

	if (!crash)
	{
		// send any final messages (don't check for errors)
		if (NET_CanSendMessage (host_client->netconnection))
		{
			MSG_WriteByte (&host_client->message, svc_disconnect);
			NET_SendMessage (host_client->netconnection, &host_client->message);
		}

		if (host_client->edict && host_client->spawned)
		{
		// call the prog function for removing a client
		// this will set the body to a dead frame, among other things
			saveSelf = pr_global_struct->self;
			pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
			PR_ExecuteProgram (pr_global_struct->ClientDisconnect);
			pr_global_struct->self = saveSelf;
		}

		Sys_Printf ("Client %s removed\n",host_client->name);
	}

// break the net connection
	NET_Close (host_client->netconnection);
	host_client->netconnection = NULL;
	SVFTE_DestroyFrames (host_client);

// free the client (the body stays around)
	host_client->active = false;
	host_client->spawned = false;
	host_client->knowntoqc = false;
	host_client->name[0] = 0;
	host_client->old_frags = -999999;
	net_activeconnections--;
	SV_ResetTransientClientSlot(dropped_slot);
	SV_CoopSharedResetClientSlot(dropped_slot);

// send notification to all clients
	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		if (!client->knowntoqc)
			continue;
		MSG_WriteByte (&client->message, svc_updatename);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteString (&client->message, "");
		MSG_WriteByte (&client->message, svc_updatefrags);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteShort (&client->message, 0);
		MSG_WriteByte (&client->message, svc_updatecolors);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteByte (&client->message, 0);
	}
}

/*
==================
Host_ShutdownServer

This only happens at the end of a game, not between levels
==================
*/
void Host_ShutdownServer(qboolean crash)
{
	int		i;
	int		count;
	sizebuf_t	buf;
	byte		message[4];
	double	start;

	if (!sv.active)
		return;

	sv.active = false;

// stop all client sounds immediately
	if (cls.state == ca_connected)
		CL_Disconnect ();

// flush any pending messages - like the score!!!
	start = Sys_DoubleTime();
	do
	{
		count = 0;
		NET_GetServerMessages(NULL);	// process ACKs on shared server sockets before checking canSend
		for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
		{
			if (host_client->active && host_client->message.cursize && host_client->netconnection)
			{
				if (NET_CanSendMessage (host_client->netconnection))
				{
					NET_SendMessage(host_client->netconnection, &host_client->message);
					SZ_Clear (&host_client->message);
				}
				else
					count++;
			}
		}
		if ((Sys_DoubleTime() - start) > 3.0)
			break;
	}
	while (count);

// make sure all the clients know we're disconnecting
	buf.data = message;
	buf.maxsize = 4;
	buf.cursize = 0;
	MSG_WriteByte(&buf, svc_disconnect);
	count = NET_SendToAll(&buf, 5.0);
	if (count)
		Con_Printf("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);

	PR_SwitchQCVM(&sv.qcvm);
	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		if (host_client->active)
			SV_DropClient(crash);
	PR_SwitchQCVM(NULL);

//
// clear structures
//
//	memset (&sv, 0, sizeof(sv)); // ServerSpawn already do this by Host_ClearMemory
	memset (svs.clients, 0, svs.maxclientslimit*sizeof(client_t));
}


/*
================
Host_ClearMemory

This clears all the memory used by both the client and server, but does
not reinitialize anything.
================
*/
void Host_ClearMemory (void)
{
	if (cl.qcvm.extfuncs.CSQC_Shutdown)
	{
		PR_SwitchQCVM(&cl.qcvm);
		PR_ExecuteProgram(qcvm->extfuncs.CSQC_Shutdown);
		qcvm->extfuncs.CSQC_Shutdown = 0;
		PR_SwitchQCVM(NULL);
	}

	Con_DPrintf ("Clearing memory\n");
	D_FlushCaches ();
	Mod_ClearAll ();
	Sky_ClearAll();
	PR_ClearProgs(&sv.qcvm);
	CL_FreeState ();
/* host_hunklevel MUST be set at this point */
	free (sv.static_entities);
	free (sv.ambientsounds);
	Hunk_FreeToLowMark (host_hunklevel);
	cls.signon = 0; // not CL_ClearSignons()
	memset (&sv, 0, sizeof(sv));
}


//==============================================================================
//
// Host Frame
//
//==============================================================================

/*
===================
Host_FilterTime

Returns false if the time is too short to run a frame
===================
*/
qboolean Host_FilterTime (float time)
{
	float maxfps; //johnfitz

	realtime += time;

	//johnfitz -- max fps cvar
	if ((host_maxfps.value > 0 || cls.state == ca_disconnected) && !cls.timedemo)
	{
		if (cls.state == ca_disconnected)
		{
			maxfps = 60.f;
			if (host_maxfps.value > 0)
				maxfps = q_min (maxfps, host_maxfps.value);
			maxfps = CLAMP (10.f, maxfps, 5000.f);
		}
		else
		{
			maxfps = CLAMP (10.f, host_maxfps.value, 5000.f);
		}

		if (realtime - oldrealtime < 1.0/maxfps)
			return false; // framerate is too high
	}
	//johnfitz

	host_frametime = realtime - oldrealtime;
	oldrealtime = realtime;

	//johnfitz -- host_timescale is more intuitive than host_framerate
	if (host_timescale.value > 0)
		host_frametime *= host_timescale.value;
	//johnfitz
	else if (host_framerate.value > 0)
		host_frametime = host_framerate.value;
	else if (host_maxfps.value > 0) // don't allow really long or short frames
		host_frametime = CLAMP (0.0001, host_frametime, 0.1); //johnfitz -- use CLAMP

	return true;
}

/*
===================
Host_GetConsoleCommands

Add them exactly as if they had been typed at the console
===================
*/
void Host_GetConsoleCommands (void)
{
	const char	*cmd;
	FILE		*f;
	char		name[MAX_OSPATH];
	char		*text;
	long		len;
	size_t		readlen;

	if (!isDedicated)
		return;	// no stdin necessary in graphical mode

	while (1)
	{
		cmd = Sys_ConsoleInput ();
		if (!cmd)
			break;
		Cbuf_AddText (cmd);
		Cbuf_AddText ("\n");
	}

	if (!sv_cmdfile.string[0])
		return;

	if (sv_cmdfile.string[0] == '/' || sv_cmdfile.string[0] == '\\' ||
		strstr(sv_cmdfile.string, "..") || strchr(sv_cmdfile.string, ':'))
	{
		static char bad_cmdfile[MAX_OSPATH];

		if (strcmp(bad_cmdfile, sv_cmdfile.string))
		{
			Con_Printf ("sv_cmdfile must be a relative file inside the current game directory\n");
			q_strlcpy (bad_cmdfile, sv_cmdfile.string, sizeof(bad_cmdfile));
		}
		return;
	}

	q_snprintf (name, sizeof(name), "%s/%s", com_gamedir, sv_cmdfile.string);
	f = fopen (name, "rb");
	if (!f)
		return;

	if (fseek (f, 0, SEEK_END) != 0)
	{
		fclose (f);
		return;
	}
	len = ftell (f);
	if (len <= 0)
	{
		fclose (f);
		remove (name);
		return;
	}
	if (len > 16384)
	{
		Con_Printf ("sv_cmdfile: ignoring oversized command file %s\n", name);
		fclose (f);
		remove (name);
		return;
	}
	rewind (f);

	text = (char *) Z_Malloc (len + 2);
	readlen = fread (text, 1, len, f);
	fclose (f);
	remove (name);

	text[readlen] = '\n';
	text[readlen + 1] = 0;
	Con_Printf ("Executing server command file %s\n", name);
	Cbuf_AddText (text);
	Z_Free (text);
}

/*
==================
Host_ServerFrame
==================
*/
void Host_ServerFrame (void)
{
	int		i, active, clients_active; //johnfitz
	edict_t	*ent; //johnfitz
	qboolean	lagdebug_timing;
	double		frame_start, after_clear, after_accept, after_clients, after_physics, after_send;
	static double	last_server_frame_log;

// run the world state
	pr_global_struct->frametime = host_frametime;
	qcvm->frametime = host_frametime;
	lagdebug_timing = net_lagdebug.value ? true : false;
	frame_start = after_clear = after_accept = after_clients = after_physics = after_send = 0;
	if (lagdebug_timing)
		frame_start = Sys_DoubleTime ();

// set the time and clear the general datagram
	SV_ClearDatagram ();
	if (lagdebug_timing)
		after_clear = Sys_DoubleTime ();

// respond to cvar changes
	PMSV_UpdateMovevars ();

// check for new clients
	SV_CheckForNewClients ();
	if (lagdebug_timing)
		after_accept = Sys_DoubleTime ();

// read client messages
	SV_RunClients ();
	if (lagdebug_timing)
		after_clients = Sys_DoubleTime ();

// move things around and think
// always pause in single player if in console or menus
	if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game) )
	{
		SV_Physics (host_frametime);
		SV_FinishLatestUsercmds ();
		Host_CoopAutosaveFrame ();
	}
	if (lagdebug_timing)
		after_physics = Sys_DoubleTime ();

//johnfitz -- devstats
	if (cls.signon == SIGNONS)
	{
		for (i=0, active=0; i<qcvm->num_edicts; i++)
		{
			ent = EDICT_NUM(i);
			if (!ent->free)
				active++;
		}
		if (active > 600 && dev_peakstats.edicts <= 600)
			Con_DWarning ("%i edicts exceeds standard limit of 600 (max = %d).\n", active, qcvm->max_edicts);
		dev_stats.edicts = active;
		dev_peakstats.edicts = q_max(active, dev_peakstats.edicts);
	}
//johnfitz

// send all messages to the clients
	SV_SendClientMessages ();
	if (lagdebug_timing)
	{
		after_send = Sys_DoubleTime ();
		if (after_send - frame_start > net_lagdebug_frame_threshold.value &&
			realtime - last_server_frame_log > 0.5)
		{
			clients_active = 0;
			for (i = 0; i < svs.maxclients; i++)
			{
				if (svs.clients[i].active)
					clients_active++;
			}
			Con_Printf ("net_lagdebug: server frame spike total=%.3f host_dt=%.3f clear=%.3f accept=%.3f clients=%.3f physics=%.3f send=%.3f active_clients=%d map=%s\n",
				after_send - frame_start, host_frametime,
				after_clear - frame_start,
				after_accept - after_clear,
				after_clients - after_accept,
				after_physics - after_clients,
				after_send - after_physics,
				clients_active, sv.name);
			last_server_frame_log = realtime;
		}
	}
}

static void CL_LoadCSProgs (void)
{
	PR_ClearProgs (&cl.qcvm);
	if (!cl_nocsqc.value)
	{
		PR_SwitchQCVM (&cl.qcvm);

		if ((PR_LoadProgs ("csprogs.dat", false) &&
			 (qcvm->extfuncs.CSQC_DrawHud || qcvm->extfuncs.CSQC_DrawScores || qcvm->extfuncs.CSQC_Ent_Update)) ||
		    (PR_LoadProgs ("progs.dat", false) &&
			 (qcvm->extfuncs.CSQC_DrawHud || qcvm->extfuncs.CSQC_Ent_Update)))
		{
			qcvm->max_edicts = CLAMP (MIN_EDICTS, (int)max_edicts.value, MAX_EDICTS);
			qcvm->edicts = (edict_t *)malloc (qcvm->max_edicts * qcvm->edict_size);
			qcvm->num_edicts = qcvm->reserved_edicts = 1;
			memset (qcvm->edicts, 0, qcvm->num_edicts * qcvm->edict_size);

			if (qcvm->extglobals.maxclients)
				*qcvm->extglobals.maxclients = cl.maxclients;
			pr_global_struct->time = cl.time;
			pr_global_struct->mapname = PR_SetEngineString (cl.mapname);
			pr_global_struct->total_monsters = cl.stats[STAT_TOTALMONSTERS];
			pr_global_struct->total_secrets = cl.stats[STAT_TOTALSECRETS];
			pr_global_struct->deathmatch = cl.gametype;
			pr_global_struct->coop = (cl.gametype == GAME_COOP) && cl.maxclients != 1;
			if (qcvm->extglobals.player_localnum)
				*qcvm->extglobals.player_localnum = cl.viewentity - 1;

			qcvm->edicts->v.solid = SOLID_BSP;
			qcvm->edicts->v.modelindex = 1;
			qcvm->edicts->v.model = PR_SetEngineString (cl.worldmodel->name);
			VectorCopy (cl.worldmodel->mins, qcvm->edicts->v.mins);
			VectorCopy (cl.worldmodel->maxs, qcvm->edicts->v.maxs);
			qcvm->edicts->v.message = PR_SetEngineString (cl.levelname);

				if (qcvm->extfuncs.CSQC_Init)
				{
					G_FLOAT (OFS_PARM0) = false;
					G_INT (OFS_PARM1) = PR_SetEngineString ("quakespasm-openvr");
					G_FLOAT (OFS_PARM2) = QUAKESPASM_VERSION;
					PR_ExecuteProgram (qcvm->extfuncs.CSQC_Init);
				}
				if (qcvm->extfuncs.CSQC_Ent_Update && cls.state == ca_connected)
				{
					MSG_WriteByte (&cls.message, clc_stringcmd);
					MSG_WriteString (&cls.message, "enablecsqc");
				}
			}
		else
			PR_ClearProgs (qcvm);
		PR_SwitchQCVM (NULL);
	}
}

/*
==================
Host_Frame

Runs all active servers
==================
*/
void _Host_Frame (double time)
{
	static double		accumtime = 0;
	static double		time1 = 0;
	static double		time2 = 0;
	static double		time3 = 0;
	int			pass1, pass2, pass3;
	qboolean		lagdebug_frame;
	double			lagdebug_start, lagdebug_after_events, lagdebug_after_commands;
	double			lagdebug_after_cbuf, lagdebug_after_netpoll, lagdebug_after_accumulate;
	double			lagdebug_after_send, lagdebug_after_server, lagdebug_after_remote_send;
	double			lagdebug_after_read;
	double			lagdebug_after_screen, lagdebug_after_audio;
	static double		last_client_frame_log;
	static double		last_host_gap_log;
	double			saved_host_frametime;
	double			host_gap_threshold;
	const char		*host_gap_map;

	if (setjmp (host_abortserver) )
		return;			// something bad happened, or the server disconnected

// keep the random time dependent
	rand ();

	accumtime += host_netinterval ? CLAMP (0, time, 0.2) : 0;

// decide the simulation time
	if (!Host_FilterTime (time))
		return;			// don't run too fast, or packets will flood out
	Host_NetfpsProbe_Frame ();
	if (net_lagdebug.value)
	{
		host_gap_threshold = net_lagdebug_threshold.value;
		if (host_gap_threshold <= 0)
			host_gap_threshold = 0.25;
		if (time > host_gap_threshold && realtime - last_host_gap_log > 0.5)
		{
			host_gap_map = sv.active ? sv.name : (cl.worldmodel ? cl.worldmodel->name : "");
			Con_Printf ("net_lagdebug: host frame gap total=%.3f host_dt=%.3f dedicated=%d sv_active=%d cls_state=%d signon=%d map=%s\n",
				time, host_frametime, isDedicated ? 1 : 0, sv.active ? 1 : 0,
				cls.state, cls.signon, host_gap_map);
			last_host_gap_log = realtime;
		}
	}
	lagdebug_frame = (net_lagdebug.value && !isDedicated) ? true : false;
	lagdebug_start = lagdebug_after_events = lagdebug_after_commands = 0;
	lagdebug_after_cbuf = lagdebug_after_netpoll = lagdebug_after_accumulate = 0;
	lagdebug_after_send = lagdebug_after_server = lagdebug_after_remote_send = 0;
	lagdebug_after_read = 0;
	lagdebug_after_screen = lagdebug_after_audio = 0;
	if (lagdebug_frame)
		lagdebug_start = Sys_DoubleTime ();

	if (!isDedicated)
	{
	// get new key events
		Key_UpdateForDest ();
		IN_UpdateInputMode ();
		Sys_SendKeyEvents ();
		if (lagdebug_frame)
			lagdebug_after_events = Sys_DoubleTime ();

	// allow mice or other external controllers to add commands
		IN_Commands ();
		if (lagdebug_frame)
			lagdebug_after_commands = Sys_DoubleTime ();
	}
	else if (lagdebug_frame)
	{
		lagdebug_after_events = Sys_DoubleTime ();
		lagdebug_after_commands = lagdebug_after_events;
	}

// check for commands typed to the host
	Host_GetConsoleCommands ();

// process console commands
	Cbuf_Execute ();
	if (lagdebug_frame)
		lagdebug_after_cbuf = Sys_DoubleTime ();

	CL_AutoReconnect_Frame ();

	NET_Poll();
	if (lagdebug_frame)
		lagdebug_after_netpoll = Sys_DoubleTime ();

// sample client input every rendered frame; CL_SendCmd drains the accumulated
// move at the paced network tick.
	CL_AccumulateCmd();
	if (lagdebug_frame)
		lagdebug_after_accumulate = Sys_DoubleTime ();

	if (cl.sendprespawn)
	{
		CL_LoadCSProgs();

		cl.sendprespawn = false;
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "prespawn");
		vid.recalc_refdef = true;
	}

	saved_host_frametime = host_frametime;
	if (accumtime >= host_netinterval)
	{
		if (host_netinterval)
		{
			host_frametime = q_max (accumtime, host_netinterval);
			accumtime -= host_frametime;
			if (host_timescale.value > 0)
				host_frametime *= host_timescale.value;
			else if (host_framerate.value > 0)
				host_frametime = host_framerate.value;
		}
		else
			accumtime -= host_netinterval;
		CL_SendCmd ();

		if (lagdebug_frame)
			lagdebug_after_send = Sys_DoubleTime ();

//-------------------
//
// server operations
//
//-------------------

		if (sv.active)
		{
			PR_SwitchQCVM(&sv.qcvm);
			Host_ServerFrame ();
			PR_SwitchQCVM(NULL);
		}
		if (lagdebug_frame)
			lagdebug_after_server = Sys_DoubleTime ();

//-------------------
//
// client operations
//
//-------------------

		host_frametime = saved_host_frametime;
		Cbuf_Waited ();
	}
	else if (lagdebug_frame)
	{
		lagdebug_after_send = Sys_DoubleTime ();
		lagdebug_after_server = lagdebug_after_send;
	}
	if (lagdebug_frame)
		lagdebug_after_remote_send = Sys_DoubleTime ();

	if (cl.qcvm.progs)
	{
		PR_SwitchQCVM(&cl.qcvm);
		SV_Physics(cl.time - qcvm->time);
		pr_global_struct->time = cl.time;
		PR_SwitchQCVM(NULL);
	}

// fetch results from server
	if (cls.state == ca_connected)
		CL_ReadFromServer ();
	if (lagdebug_frame)
		lagdebug_after_read = Sys_DoubleTime ();

// update video
	if (host_speeds.value)
		time1 = Sys_DoubleTime ();

	SCR_UpdateScreen ();

	CL_RunParticles (); //johnfitz -- seperated from rendering
	if (lagdebug_frame)
		lagdebug_after_screen = Sys_DoubleTime ();

	if (host_speeds.value)
		time2 = Sys_DoubleTime ();

// update audio
	BGM_Update();	// adds music raw samples and/or advances midi driver
	if (cls.signon == SIGNONS)
	{
		S_Update (r_origin, vpn, vright, vup);
		CL_DecayLights ();
	}
	else
		S_Update (vec3_origin, vec3_origin, vec3_origin, vec3_origin);

	CDAudio_Update();
	if (lagdebug_frame)
	{
		lagdebug_after_audio = Sys_DoubleTime ();
		if ((lagdebug_after_audio - lagdebug_start > net_lagdebug_frame_threshold.value ||
			lagdebug_after_screen - lagdebug_after_read > net_lagdebug_frame_threshold.value ||
			lagdebug_after_remote_send - lagdebug_after_server > net_lagdebug_frame_threshold.value ||
			lagdebug_after_read - lagdebug_after_remote_send > net_lagdebug_frame_threshold.value) &&
			realtime - last_client_frame_log > 0.5)
		{
			Con_Printf ("net_lagdebug: client frame spike total=%.3f host_dt=%.3f events=%.3f cmds=%.3f cbuf=%.3f netpoll=%.3f accum=%.3f localsend=%.3f server=%.3f remotesend=%.3f netread=%.3f gfx=%.3f snd=%.3f state=%d signon=%d lastmsg_age=%.3f\n",
				lagdebug_after_audio - lagdebug_start, host_frametime,
				lagdebug_after_events - lagdebug_start,
				lagdebug_after_commands - lagdebug_after_events,
				lagdebug_after_cbuf - lagdebug_after_commands,
				lagdebug_after_netpoll - lagdebug_after_cbuf,
				lagdebug_after_accumulate - lagdebug_after_netpoll,
				lagdebug_after_send - lagdebug_after_accumulate,
				lagdebug_after_server - lagdebug_after_send,
				lagdebug_after_remote_send - lagdebug_after_server,
				lagdebug_after_read - lagdebug_after_remote_send,
				lagdebug_after_screen - lagdebug_after_read,
				lagdebug_after_audio - lagdebug_after_screen,
				cls.state, cls.signon, realtime - cl.last_received_message);
			last_client_frame_log = realtime;
		}
	}

	if (host_speeds.value)
	{
		pass1 = (time1 - time3)*1000;
		time3 = Sys_DoubleTime ();
		pass2 = (time2 - time1)*1000;
		pass3 = (time3 - time2)*1000;
		Con_Printf ("%3i tot %3i server %3i gfx %3i snd\n",
					pass1+pass2+pass3, pass1, pass2, pass3);
	}
	host_framecount++;

}

void Host_Frame (double time)
{
	double	time1, time2;
	static double	timetotal;
	static int		timecount;
	int		i, c, m;

	if (cls.state != ca_dedicated)
		AddonCatalog_Poll ();

	if (!serverprofile.value)
	{
		_Host_Frame (time);
		return;
	}

	time1 = Sys_DoubleTime ();
	_Host_Frame (time);
	time2 = Sys_DoubleTime ();

	timetotal += time2 - time1;
	timecount++;

	if (timecount < 1000)
		return;

	m = timetotal*1000/timecount;
	timecount = 0;
	timetotal = 0;
	c = 0;
	for (i = 0; i < svs.maxclients; i++)
	{
		if (svs.clients[i].active)
			c++;
	}

	Con_Printf ("serverprofile: %2i clients %2i msec\n",  c,  m);
}

/*
====================
Host_Init
====================
*/
void Host_Init (void)
{
	if (standard_quake)
		minimum_memory = MINIMUM_MEMORY;
	else	minimum_memory = MINIMUM_MEMORY_LEVELPAK;

	if (COM_CheckParm ("-minmemory"))
		host_parms->memsize = minimum_memory;

	if (host_parms->memsize < minimum_memory)
		Sys_Error ("Only %4.1f megs of memory available, can't execute game", host_parms->memsize / (float)0x100000);

	com_argc = host_parms->argc;
	com_argv = host_parms->argv;

	Memory_Init (host_parms->membase, host_parms->memsize);
	Cbuf_Init ();
	Cmd_Init ();
	LOG_Init (host_parms);
	Cvar_Init (); //johnfitz
	COM_Init ();
	COM_InitFilesystem ();
	Host_InitLocal ();
	W_LoadWadFile (); //johnfitz -- filename is now hard-coded for honesty
	if (cls.state != ca_dedicated)
	{
		Key_Init ();
		Con_Init ();
	}
	PR_Init ();
	Mod_Init ();
	NET_Init ();
	SV_Init ();

	Con_Printf ("Exe: " __TIME__ " " __DATE__ "\n");
	Con_Printf ("%4.1f megabyte heap\n", host_parms->memsize/ (1024*1024.0));

	if (cls.state != ca_dedicated)
	{
		host_colormap = (byte *)COM_LoadHunkFile ("gfx/colormap.lmp", NULL);
		if (!host_colormap)
			Sys_Error ("Couldn't load gfx/colormap.lmp");

		V_Init ();
		Chase_Init ();
		M_Init ();
		ExtraMaps_Init (); //johnfitz
		Modlist_Init (); //johnfitz
		AddonCatalog_Init ();
		DemoList_Init (); //ericw
		VID_Init ();
		IN_Init ();
		TexMgr_Init (); //johnfitz
		Draw_Init ();
		SCR_Init ();
		R_Init ();
		S_Init ();
		CDAudio_Init ();
		BGM_Init();
		Sbar_Init ();
		CL_Init ();
	}

	VR_InitGame (); // per-game weapon offsets + projectile tuning
	LOC_Init (); // for 2021 rerelease support.

	Hunk_AllocName (0, "-HOST_HUNKLEVEL-");
	host_hunklevel = Hunk_LowMark ();

	host_initialized = true;
	Con_Printf ("\n========= Quake Initialized =========\n\n");

	if (cls.state != ca_dedicated)
	{
		Cbuf_InsertText ("exec quake.rc\n");
		// johnfitz -- in case the vid mode was locked during vid_init, we can unlock it now.
		// note: two leading newlines because the command buffer swallows one of them.
		Cbuf_AddText ("\n\nvid_unlock\n");
		Cbuf_AddText ("mod_migrate_enhancedmodels\n");
		Cmd_QueuePostConfig ();
		Cbuf_AddText ("host_migrate_network_defaults\n");
		Cbuf_AddText ("cl_migrate_network_defaults\n");
		Cbuf_AddText ("vr_migrate_movement_defaults\n");
	}

	if (cls.state == ca_dedicated)
	{
		Cbuf_AddText ("exec autoexec.cfg\n");
		Cbuf_AddText ("stuffcmds\n");
		Cmd_QueuePostConfig ();
		Cbuf_AddText ("host_migrate_network_defaults\n");
		Cbuf_AddText ("vr_migrate_movement_defaults\n");
		Cbuf_Execute ();
		if (!sv.active)
		{
			Cbuf_AddText ("map start\n");
			Cbuf_Execute ();
		}
	}
}


/*
===============
Host_Shutdown

FIXME: this is a callback from Sys_Quit and Sys_Error.  It would be better
to run quit through here before the final handoff to the sys code.
===============
*/
void Host_Shutdown(void)
{
	static qboolean isdown = false;

	if (isdown)
	{
		printf ("recursive shutdown\n");
		return;
	}
	isdown = true;

// keep Con_Printf from trying to update the screen
	scr_disabled_for_loading = true;

	Host_WriteConfiguration ();

	NET_Shutdown ();

	if (cls.state != ca_dedicated)
	{
		AddonCatalog_Shutdown ();
		if (con_initialized)
			History_Shutdown ();
		BGM_Shutdown();
		CDAudio_Shutdown ();
		S_Shutdown ();
		IN_Shutdown ();
        VID_VR_Shutdown();
		VID_Shutdown();
	}

	LOG_Close ();

	LOC_Shutdown ();
}
