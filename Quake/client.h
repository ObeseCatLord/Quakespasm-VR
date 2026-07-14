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

#ifndef _CLIENT_H_
#define _CLIENT_H_

// client.h

typedef struct
{
	int		length;
	char	map[MAX_STYLESTRING];
	char	average; //johnfitz
	char	peak; //johnfitz
} lightstyle_t;

typedef struct
{
	char	name[MAX_SCOREBOARDNAME];
	float	entertime;
	int		frags;
	int		colors;			// two 4 bit fields
	int		ping;
	byte	translations[VID_GRADES*256];
} scoreboard_t;

typedef struct
{
	int		destcolor[3];
	float	percent;		// 0-256
} cshift_t;

#define	CSHIFT_CONTENTS	0
#define	CSHIFT_DAMAGE	1
#define	CSHIFT_BONUS	2
#define	CSHIFT_POWERUP	3
#define	NUM_CSHIFTS		4

#define	NAME_LENGTH	64


//
// client_state_t should hold all pieces of the client state
//

#define	SIGNONS		4			// signon messages to receive before connected

#define	MAX_DLIGHTS		64 //johnfitz -- was 32
typedef struct
{
	vec3_t	origin;
	float	radius;
	float	die;				// stop lighting after this time
	float	decay;				// drop this each second
	float	minlight;			// don't add when contributing less
	int		key;
	vec3_t	color;				//johnfitz -- lit support via lordhavoc
} dlight_t;


#define	MAX_BEAMS	32 //johnfitz -- was 24
typedef struct
{
	int		entity;
	struct qmodel_s	*model;
	float	endtime;
	vec3_t	start, end;
#ifdef PSET_SCRIPT
	const char	*trailname;
	struct trailstate_s *trailstate;
#endif
} beam_t;

#define	MAX_MAPSTRING	2048
#define	MAX_DEMOS		8
#define	MAX_DEMONAME	16
#define	CL_MOVE_HISTORY	64

typedef enum {
ca_dedicated, 		// a dedicated server with no ability to start a client
ca_disconnected, 	// full screen console with no connection
ca_connected		// valid netcon, talking to a server
} cactive_t;

//
// the client_static_t structure is persistant through an arbitrary number
// of server connections
//
typedef struct
{
	cactive_t	state;

// personalization data sent to server
	char		spawnparms[MAX_MAPSTRING];	// to restart a level

// demo loop control
	qboolean	demoloop;
	int			demonum;		// -1 = don't play demos
	char		demos[MAX_DEMOS][MAX_DEMONAME];	// when not playing

// demo recording info must be here, because record is started before
// entering a map (and clearing client_state_t)
	qboolean	demorecording;
	qboolean	demoplayback;

// did the user pause demo playback? (separate from cl.paused because we don't
// want a svc_setpause inside the demo to actually pause demo playback).
	qboolean	demopaused;

	qboolean	timedemo;
	int		forcetrack;		// -1 = use normal cd track
	FILE		*demofile;
	int		td_lastframe;		// to meter out one message a frame
	int		td_startframe;		// host_framecount at start
	float		td_starttime;		// realtime at second frame of timedemo

// connection information
	int		signon;			// 0 to SIGNONS
	struct qsocket_s	*netcon;
	sizebuf_t	message;		// writing buffer to send to server

} client_static_t;

extern client_static_t	cls;

//
// the client_state_t structure is wiped completely at every
// server signon
//
typedef struct
{
	int			movemessages;	// since connecting to this server
								// throw out the first couple, so the player
								// doesn't accidentally do something the
								// first frame
	usercmd_t	cmd;			// last command sent to the server
	usercmd_t	pendingcmd;		// current unsent state from mice+joysticks.
	vec3_t		accummoves;		// accumulated mouse movement for paced sends
	vec3_t		vr_roomscalemove_accum; // accumulated room-scale movement for paced sends
	float		lastcmdtime;	// server time of last sent move command
		int			ackedmovemessages;	// last sequenced move accepted by server
		usercmd_t	movecmds[CL_MOVE_HISTORY];
		int			predicted_move_sequence[CL_MOVE_HISTORY];
		vec3_t		predicted_move_origin[CL_MOVE_HISTORY];
		vec3_t		predicted_move_velocity[CL_MOVE_HISTORY];
		vec3_t		prediction_error;
		double		prediction_error_time;
		int			prediction_error_sequence;
		int			net_prediction_errors;
		float		net_prediction_error_last;
		float		net_prediction_error_max;
		int			net_prediction_error_last_sequence;
		int			net_move_packets_sent;
	int			net_move_cmds_sent;
	int			net_move_last_packet_cmds;
	int			net_move_acks;
	int			net_move_stale_acks;
	int			net_snapshot_sequence;
	int			net_snapshot_packets;
	int			net_snapshot_drops;
	int			net_snapshot_acks_sent;
	int			net_snapshot_ack_queue_overflows;
	qboolean	net_snapshot_have;
	// Replacement-delta maps can arrive as large split bursts. Keep the queue
	// large enough for those bursts, and keep queued acks contiguous if full.
#define	CL_ACKFRAME_HISTORY	128
#define	CL_ACKFRAME_FLUSH_THRESHOLD	8
	int			ackframes[CL_ACKFRAME_HISTORY];
	unsigned int	ackframes_count;
	qboolean	requestresend;
	double		net_last_diag_time;

// information for local display
	int			stats[MAX_CL_STATS];	// health, etc
	float		statsf[MAX_CL_STATS];
	char		*statss[MAX_CL_STATS];
	int			items;			// inventory bit flags
	float	item_gettime[32];	// cl.time of aquiring item, for blinking
	float		faceanimtime;	// use anim frame if cl.time < this

	cshift_t	cshifts[NUM_CSHIFTS];	// color shifts for damage, powerups
	cshift_t	prev_cshifts[NUM_CSHIFTS];	// and content types

// the client maintains its own idea of view angles, which are
// sent to the server each frame.  The server sets punchangle when
// the view is temporarliy offset, and an angle reset commands at the start
// of each level and after teleporting.
	vec3_t		mviewangles[2];	// during demo playback viewangles is lerped
								// between these
	vec3_t		viewangles;
	vec3_t		aimangles;
	vec3_t		vmeshoffset;
	vec3_t		handpos[2];
	vec3_t		handrot[2];

	vec3_t		mvelocity[2];	// update by server, used for lean+bob
								// (0 is newest)
	vec3_t		velocity;		// lerped between mvelocity[0] and [1]

	vec3_t		punchangle;		// temporary offset
	double		punchtime;

// pitch drifting vars
	float		idealpitch;
	float		pitchvel;
	qboolean	nodrift;
	float		driftmove;
	double		laststop;

	float		wheel_pitch;	// for looking up/down using the mouse wheel

	float		viewheight;
	float		crouch;			// local amount for smoothing stepups

	qboolean	paused;			// send over by server
	qboolean	onground;
	qboolean	inwater;

	int			intermission;	// don't change view angle, full screen, etc
	int			completed_time;	// latched at intermission start

	double		mtime[2];		// the timestamp of last two messages
	double		time;			// clients view of time, should be between
								// servertime and oldservertime to generate
								// a lerp point for other data
	double		oldtime;		// previous cl.time, time-oldtime is used
								// to decay light values and smooth step ups


	float		last_received_message;	// (realtime) for net trouble icon

//
// information that is static for the entire time connected to a server
//
	struct qmodel_s		*model_precache[MAX_MODELS];
	struct sfx_s		*sound_precache[MAX_SOUNDS];
#ifdef PSET_SCRIPT
	qboolean	protocol_particles;
	struct
	{
		const char	*name;
		int			index;
	} particle_precache[MAX_PARTICLETYPES];
	struct
	{
		const char	*name;
		int			index;
	} local_particle_precache[MAX_PARTICLETYPES];
#else
	char		particle_precache[MAX_PARTICLETYPES][MAX_QPATH];
#endif

	char		mapname[128];
	char		levelname[128];	// for display on solo scoreboard //johnfitz -- was 40.
	char		server_gamedir[MAX_OSPATH];
	int			viewentity;		// cl_entitites[cl.viewentity] = player
	int			maxclients;
	int			gametype;

// refresh related state
	struct qmodel_s	*worldmodel;	// cl_entitites[0].model
	struct efrag_s	*free_efrags;
	int			num_efrags;
	entity_t	*entities;		// johnfitz -- was a static array, now on hunk
	int			max_edicts;		// only changes when new map loads
	int			num_entities;	// held in cl.entities array
	int			num_statics;	// held in cl_staticentities array
	entity_t	viewent;			// the gun model
	qboolean	in_vr_weaponmenu;

	int			cdtrack, looptrack;	// cd audio

// frag scoreboard
	scoreboard_t	*scores;		// [cl.maxclients]

	unsigned	protocol; //johnfitz
	unsigned	protocolflags;
	unsigned	protocol_pext1;
	unsigned	protocol_pext2;
	qboolean	vr_relative_muzzle_supported;

	qboolean	sendprespawn;

	char		stuffcmdbuf[1024];	//comment-extensions are a thing with certain servers, make sure we can handle them properly without further hacks/breakages. there's also some server->client only console commands that we might as well try to handle a bit better, like reconnect

	qcvm_t		qcvm;	//for csqc.
	size_t		ssqc_to_csqc_max;
	edict_t		**ssqc_to_csqc;	// maps server entity numbers to client-side CSQC edicts
} client_state_t;


//
// cvars
//
extern	cvar_t	cl_name;
extern	cvar_t	cl_color;

extern	cvar_t	cl_upspeed;
extern	cvar_t	cl_forwardspeed;
extern	cvar_t	cl_backspeed;
extern	cvar_t	cl_sidespeed;
extern	cvar_t	cl_desktop_vanilla_run;
extern	cvar_t	cl_predictmove;
extern	cvar_t	cl_nopred;
extern	cvar_t	cl_predict_error_log;

extern	cvar_t	cl_movespeedkey;

extern	cvar_t	cl_yawspeed;
extern	cvar_t	cl_pitchspeed;

extern	cvar_t	cl_anglespeedkey;

extern	cvar_t	cl_alwaysrun; // QuakeSpasm

extern	cvar_t	cl_autofire;

extern	cvar_t	cl_shownet;
extern	cvar_t	cl_nolerp;
extern	cvar_t	cl_lerpdebug;
extern	cvar_t	cl_lerpdebug_models;
extern	cvar_t	cl_extrapolate;
extern	cvar_t	cl_extrapolate_adaptive;
extern	cvar_t	cl_extrapolate_adaptive_max;
extern	cvar_t	cl_extrapolate_adaptive_time;
extern	cvar_t	cl_net_lerpbuffer;
extern	cvar_t	cl_net_lerpbuffer_adaptive;
extern	cvar_t	cl_net_lerpbuffer_adaptive_max;
extern	cvar_t	cl_net_lerpbuffer_adaptive_time;

double CL_NetLagDebugFrameThreshold (void);

extern	cvar_t	cfg_unbindall;

extern	cvar_t	cl_pitchdriftspeed;
extern	cvar_t	freelook;
extern	cvar_t	lookspring;
extern	cvar_t	lookstrafe;
extern	cvar_t	sensitivity;

extern	cvar_t	m_pitch;
extern	cvar_t	m_yaw;
extern	cvar_t	m_forward;
extern	cvar_t	m_side;

extern	cvar_t	cl_startdemos;
extern	cvar_t	cl_confirmquit;


#define	MAX_TEMP_ENTITIES	256		//johnfitz -- was 64
#define	MAX_STATIC_ENTITIES	4096	//ericw -- was 512	//johnfitz -- was 128
#define	MAX_VISEDICTS		16384	// larger, now we support BSP2

extern	client_state_t	cl;

// FIXME, allocate dynamically
extern	entity_t		cl_static_entities[MAX_STATIC_ENTITIES];
extern	lightstyle_t	cl_lightstyle[MAX_LIGHTSTYLES];
extern	dlight_t		cl_dlights[MAX_DLIGHTS];
extern	entity_t		cl_temp_entities[MAX_TEMP_ENTITIES];
extern	beam_t			cl_beams[MAX_BEAMS];
extern	entity_t		*cl_visedicts[MAX_VISEDICTS];
extern	int				cl_numvisedicts;

//=============================================================================

//
// cl_main
//
dlight_t *CL_AllocDlight (int key);
void	CL_DecayLights (void);
int		CL_ParticleEffectColor (int effectnum);
void	CL_RunNamedParticleEffect (int effectnum, vec3_t org, vec3_t dir, int count);
#ifdef PSET_SCRIPT
void	CL_ClearTrailStates (void);
void	CL_RegisterParticles (void);
#endif

void CL_Init (void);

void CL_EstablishConnection (const char *host);

void CL_Disconnect (void);
void CL_Disconnect_f (void);
void CL_NextDemo (void);

//
// cl_input
//
typedef struct
{
	int		down[2];		// key nums holding it down
	int		state;			// low bit is down state
	double	downtime;		// when KeyDown() last pressed this button
	double	uptime;			// when KeyUp() last released this button
} kbutton_t;

extern	kbutton_t	in_mlook, in_klook;
extern 	kbutton_t 	in_strafe;
extern 	kbutton_t 	in_speed;
extern 	kbutton_t 	in_attack, in_jump;
extern	cvar_t		cl_iDrive;

void CL_InitInput(void);
void CL_SendCmd(void);
void CL_AccumulateCmd(void);
void CL_ClearPendingCmd(void);
void CL_SendMove(const usercmd_t *cmd);
void CL_FlushAckFrames(void);
int CL_ReadFromServer(void);
void CL_AutoReconnect_Frame(void);
qboolean CL_AutoReconnect_IsActive(void);
qboolean CL_AutoReconnect_IsSwitchCommand(void);
void CL_AutoReconnect_Cancel(void);
qboolean CL_MaybeSwitchServerGame(const char *serverdirs);
void CL_AdjustAngles(void);
void CL_BaseMove(usercmd_t *cmd, qboolean isfinal);
void CL_FinishMove(usercmd_t *cmd, qboolean isfinal);

void CL_ParseTEnt (void);
void CL_UpdateTEnts (void);

void CL_FreeState(void);
void CL_ClearState (void);

//
// cl_demo.c
//
void CL_StopPlayback (void);
int CL_GetMessage (void);
void CL_ClearSignons (void);

void CL_Stop_f (void);
void CL_Record_f (void);
void CL_PlayDemo_f (void);
void CL_TimeDemo_f (void);

//
// cl_parse.c
//
void CL_ParseServerMessage (void);
void CL_NewTranslation (int slot);

//
// view
//
void V_StartPitchDrift (void);
void V_StopPitchDrift (void);

void V_RenderView (void);
void V_ParseDamage (void);
void V_SetContentsColor (int contents);

//
// cl_tent
//
void CL_InitTEnts (void);
void CL_SignonReply (void);

//
// chase
//
extern	cvar_t	chase_active;

void Chase_Init (void);
void TraceLine (vec3_t start, vec3_t end, vec3_t impact);
void Chase_UpdateForClient (void);	//johnfitz
void Chase_UpdateForDrawing (void);	//johnfitz

#endif	/* _CLIENT_H_ */
