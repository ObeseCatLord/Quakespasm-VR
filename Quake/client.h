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

#include "voice_protocol.h"

// client.h

#define CLIENT_USER_INFO_STRING_SIZE 8192

typedef struct
{
	int	 length;
	char map[MAX_STYLESTRING];
	char average; // johnfitz
	char peak;	  // johnfitz
} lightstyle_t;

typedef struct
{
	char  name[MAX_SCOREBOARDNAME];
	float entertime;
	int	  frags;
	int	  colors; // two 4 bit fields
	int	  ping;
	byte  translations[VID_GRADES * 256];

	char userinfo[CLIENT_USER_INFO_STRING_SIZE];
} scoreboard_t;

typedef struct
{
	int	  destcolor[3];
	float percent; // 0-256
} cshift_t;

#define CSHIFT_CONTENTS 0
#define CSHIFT_DAMAGE	1
#define CSHIFT_BONUS	2
#define CSHIFT_POWERUP	3
#define NUM_CSHIFTS		4

#define NAME_LENGTH 64

//
// client_state_t should hold all pieces of the client state
//

#define SIGNONS 4 // signon messages to receive before connected

#define MAX_DLIGHTS 64 // johnfitz -- was 32
typedef struct
{
	vec3_t origin;
	float  radius;
	float  die;		 // stop lighting after this time
	float  decay;	 // drop this each second
	float  minlight; // don't add when contributing less
	int	   key;
	vec3_t color;		  // johnfitz -- lit support via lordhavoc
	vec3_t cone_dir;	  // spotlight direction
	float  cone_cos;	  // cos of the spotlight apex angle, <= -1: not a spotlight
	float  kex_intensity; // > 0: rerelease dynamiclight entity, lit with the KEX falloff (linear over radius, Lambert, linear cone)
} dlight_t;

#define MAX_BEAMS 32 // johnfitz -- was 24
typedef struct
{
	int					 entity;
	struct qmodel_s		*model;
	float				 endtime;
	vec3_t				 start, end;
	const char			*trailname;
	struct trailstate_s *trailstate;
} beam_t;

#define MAX_MAPSTRING 2048
#define MAX_DEMOS	  8
#define MAX_DEMONAME  16

typedef enum
{
	ca_dedicated,	 // a dedicated server with no ability to start a client
	ca_disconnected, // full screen console with no connection
	ca_connected	 // valid netcon, talking to a server
} cactive_t;

//
// the client_static_t structure is persistant through an arbitrary number
// of server connections
//
typedef struct
{
	cactive_t state;

	// personalization data sent to server
	char spawnparms[MAX_MAPSTRING]; // to restart a level

	// demo loop control
	int	 demonum;						 // -1 = don't play demos
	char demos[MAX_DEMOS][MAX_DEMONAME]; // when not playing

	// demo recording info must be here, because record is started before
	// entering a map (and clearing client_state_t)
	qboolean demorecording;
	qboolean demoplayback;

	// did the user pause demo playback? (separate from cl.paused because we don't
	// want a svc_setpause inside the demo to actually pause demo playback).
	qboolean demopaused;
	qboolean demoseeking;
	float	 seektime;
	float	 demospeed;

	// demo file position where the current level starts (after signon packets)
	qfileofs_t demo_prespawn_end;

	qboolean timedemo;
	int		 forcetrack; // -1 = use normal cd track
	FILE	*demofile;
	int		 td_lastframe;	// to meter out one message a frame
	int		 td_startframe; // host_framecount at start
	float	 td_starttime;	// realtime at second frame of timedemo

	// Explicit legacy layout selection survives map clears, not disconnects.
	unsigned int legacy_qsvr;
	unsigned int offered_qsvr; // profile offered on this connection; survives map signon clears

	// connection information
	int				  signon; // 0 to SIGNONS
	struct qsocket_s *netcon;
	sizebuf_t		  message; // writing buffer to send to server

	char userinfo[CLIENT_USER_INFO_STRING_SIZE];
} client_static_t;

extern client_static_t cls;

//
// the client_state_t structure is wiped completely at every
// server signon
//
typedef struct
{
	int		  movemessages;		 // since connecting to this server
								 // throw out the first couple, so the player
								 // doesn't accidentally do something the
								 // first frame
	int		  ackedmovemessages; // echo of movemessages from the server.
	// Pinned private movement state; public protocol still uses its donor path.
	move_authority_t move_ack_authority;
	qboolean move_ack_prediction_allowed;
	unsigned short move_ack_mode_epoch, move_ack_discontinuity_epoch;
	unsigned char move_ack_discontinuity_reason;
	// Coherent owner/ACK association only; replay must also check ACK policy.
	// The authoritative owner remains in entities[owner].netstate.
	qboolean move_snapshot_valid;
	int move_snapshot_ack, move_snapshot_owner;
	double move_msec_sample_time, move_msec_fractional_carry;
	qboolean move_msec_sample_valid;
	vec3_t prediction_error;
	double prediction_error_time;
	int prediction_error_sequence;
	int net_move_acks, net_move_stale_acks;
	int net_move_packets_sent, net_move_cmds_sent, net_move_last_packet_cmds;
	unsigned long long net_move_msec_generated;
	int net_snapshot_sequence, net_snapshot_packets, net_snapshot_drops;
	int net_snapshot_acks_sent, net_snapshot_ack_queue_overflows;
	qboolean net_snapshot_have;
	qboolean vr_gorilla_supported, vr_gorilla_allowed, vr_gorilla_cap_sent;
	qboolean vr_gorilla_trusted_supported, vr_gorilla_trusted_cap_sent;
	qboolean vr_gorilla_state_valid;
	int vr_gorilla_state_sequence;
	vr_gorilla_state_t vr_gorilla_state;
	qboolean vr_gorilla_motion_generation_valid;
	unsigned int vr_gorilla_motion_generation;
	usercmd_t movecmds[64];		 // ringbuffer of previous movement commands (journal for prediction)
#define MOVECMDS_MASK (countof (cl.movecmds) - 1)
	/* QSS-M replay propagation belongs to the command journal lifetime. */
	int move_replay_propagate_sequence[64];
	float move_replay_propagate_waterjumptime[64];
	qboolean move_replay_private_metadata_valid;
	qboolean move_replay_private_prediction_allowed;
	move_authority_t move_replay_private_authority;
	unsigned short move_replay_private_mode_epoch, move_replay_private_discontinuity_epoch;
	usercmd_t cmd; // last private command sent, with sampled duration and tracking
	usercmd_t pendingcmd; // accumulated state from mice+joysticks.

	// information for local display
	int	  stats[MAX_CL_STATS]; // health, etc
	float statsf[MAX_CL_STATS];
	char *statss[MAX_CL_STATS];
	int	  items;			// inventory bit flags
	float item_gettime[32]; // cl.time of aquiring item, for blinking
	float faceanimtime;		// use anim frame if cl.time < this

	float v_dmg_time, v_dmg_roll, v_dmg_pitch;

	cshift_t cshift_empty;				// can be modified by V_cshift_f ()
	cshift_t cshifts[NUM_CSHIFTS];		// color shifts for damage, powerups
	cshift_t prev_cshifts[NUM_CSHIFTS]; // and content types

	// the client maintains its own idea of view angles, which are
	// sent to the server each frame.  The server sets punchangle when
	// the view is temporarliy offset, and an angle reset commands at the start
	// of each level and after teleporting.
	vec3_t mviewangles[2]; // during demo playback viewangles is lerped
						   // between these
	vec3_t viewangles;

	vec3_t mvelocity[2]; // update by server, used for lean+bob
						 // (0 is newest)
	vec3_t velocity;	 // lerped between mvelocity[0] and [1]

	vec3_t punchangle; // temporary offset

	// pitch drifting vars
	float	 idealpitch;
	float	 pitchvel;
	qboolean nodrift;
	float	 driftmove;
	double	 laststop;

	float viewheight;
	float crouch; // local amount for smoothing stepups

	qboolean paused; // send over by server
	qboolean onground;
	qboolean inwater;
	double	 fixangle_time; // timestamp of last svc_setangle message

	int intermission;	// don't change view angle, full screen, etc
	int completed_time; // latched at intermission start

	double mtime[2]; // the timestamp of last two messages
	double time;	 // clients view of time, should be between
					 // servertime and oldservertime to generate
					 // a lerp point for other data
	double oldtime;	 // previous cl.time, time-oldtime is used
					 // to decay light values and smooth step ups

	float last_received_message; // (realtime) for net trouble icon

	//
	// information that is static for the entire time connected to a server
	//
	struct qmodel_s *model_precache[MAX_MODELS];
	struct sfx_s	*sound_precache[MAX_SOUNDS];

	char mapname[128];
	char levelname[128]; // for display on solo scoreboard //johnfitz -- was 40.
	int	 viewentity;	 // cl_entitites[cl.viewentity] = player
	int	 maxclients;
	int	 gametype;

	// refresh related state
	struct qmodel_s *worldmodel; // cl_entitites[0].model
	struct efrag_s	*free_efrags;
	int				 num_efrags;
	struct efrag_s **efrag_allocs;
	int				 num_efragallocs;
	entity_t		 viewent; // the gun model

	entity_t *entities; // spike -- moved into here
	int		  max_edicts;
	int		  num_entities;

	entity_t **static_entities; // spike -- was static
	int		   max_static_entities;
	int		   num_statics;

	int cdtrack, looptrack; // cd audio

	// frag scoreboard
	scoreboard_t *scores; // [cl.maxclients]

	unsigned protocol; // johnfitz
	unsigned protocolflags;
	unsigned protocol_pext1; // spike -- flag of fte protocol extensions
	unsigned protocol_pext2; // spike -- flag of fte protocol extensions
	unsigned protocol_qsvr; // selected private layout; zero until explicit admission, never inferred from FTE bits
	/* Optional inherited VRIK receive capability, independent of the QSVR
	 * movement profile and local VR/OpenXR initialization. */
	qboolean vrik_protocol_offered;
	qboolean vrik_cap_sent;
	unsigned char vrik_protocol_version;
	/* Optional opaque Opus transport, independent of PEXT and VRIK. */
	qboolean voice_protocol_offered;
	qboolean voice_cap_sent;
	unsigned char voice_protocol_version;
	voice_packet_t voice_outgoing[VOICE_CLIENT_QUEUE_CAPACITY];
	unsigned int voice_outgoing_head;
	unsigned int voice_outgoing_count;
	/* Newest-pose sender state shares the current client-state lifetime. */
	unsigned short vrik_next_sequence;
	double vrik_next_send_time;
	qboolean vrik_last_sent_active;

	qboolean protocol_particles;
	struct
	{
		const char *name;
		int			index;
	} particle_precache[MAX_PARTICLETYPES];
	struct
	{
		const char *name;
		int			index;
	} local_particle_precache[MAX_PARTICLETYPES];
#define CL_ACKFRAME_HISTORY 128
#define CL_ACKFRAME_FLUSH_THRESHOLD 8
	int ackframes[CL_ACKFRAME_HISTORY]; // private split-snapshot bursts; public admission remains eight
	unsigned int ackframes_count;
	qboolean	 requestresend;
	qboolean	 sendprespawn;

	qcvm_t qcvm; // for csqc.

	float zoom;
	float zoomdir;

	char serverinfo[SERVER_INFO_STRING_SIZE]; // \key\value infostring data.
} client_state_t;

//
// cvars
//
extern cvar_t cl_name;

extern cvar_t cl_topcolor, cl_bottomcolor;

extern cvar_t cl_upspeed;
extern cvar_t cl_forwardspeed;
extern cvar_t cl_desktop_vanilla_run;
extern cvar_t cl_backspeed;
extern cvar_t cl_sidespeed;

extern cvar_t cl_movespeedkey;

extern cvar_t cl_yawspeed;
extern cvar_t cl_pitchspeed;

extern cvar_t cl_anglespeedkey;

extern cvar_t cl_alwaysrun; // QuakeSpasm

extern cvar_t cl_autofire;

extern cvar_t cl_shownet;
extern cvar_t cl_nolerp;

extern cvar_t cfg_unbindall;

extern cvar_t cl_pitchdriftspeed;
extern cvar_t lookspring;
extern cvar_t lookstrafe;
extern cvar_t sensitivity;

extern cvar_t m_pitch;
extern cvar_t m_yaw;
extern cvar_t m_forward;
extern cvar_t m_side;

extern cvar_t cl_startdemos;

#define MAX_TEMP_ENTITIES 256 // johnfitz -- was 64

extern client_state_t cl;

// FIXME, allocate dynamically
extern lightstyle_t cl_lightstyle[MAX_LIGHTSTYLES];
extern dlight_t		cl_dlights[MAX_DLIGHTS];
extern entity_t		cl_temp_entities[MAX_TEMP_ENTITIES];
extern beam_t		cl_beams[MAX_BEAMS];
extern entity_t	  **cl_visedicts;
extern entity_t	  **cl_visedicts_alpha;
extern int			cl_numvisedicts;
extern int			cl_numvisedicts_alpha_overwater;
extern int			cl_numvisedicts_alpha_underwater;
extern int			cl_maxvisedicts; // extended if we exceeded it the previous frame

//=============================================================================

//
// cl_main
//
dlight_t *CL_AllocDlight (int key);
void	  CL_DecayLights (void);

void CL_RelinkEntities (void);
qboolean CL_ReplayPlayerMovement (entity_t *ent, vec3_t origin);

void CL_Init (void);

void CL_EstablishConnection (const char *host, unsigned int legacy_qsvr);
qboolean CL_MaybeSwitchServerGame (const char *modname);
const char *CL_FindInstalledServerGame (const char *gamedir);
void CL_AutoReconnectFrame (void);
void CL_CancelAutoReconnect (void);

typedef enum cl_servermod_phase_e
{
	CL_SERVERMOD_CHECKING,
	CL_SERVERMOD_PROMPT,
	CL_SERVERMOD_INSTALLING,
	CL_SERVERMOD_ERROR
} cl_servermod_phase_t;

typedef struct cl_servermod_info_s
{
	cl_servermod_phase_t phase;
	char game[MAX_QPATH];
	char name[64];
	char author[64];
	char description[160];
	char message[160];
	int size;
	qboolean verified;
} cl_servermod_info_t;

qboolean CL_ServerModDownload_Begin (const char *gamedir);
qboolean CL_ServerModDownload_GetInfo (cl_servermod_info_t *info);
void CL_ServerModDownload_Accept (void);
void CL_ServerModDownload_Cancel (void);
void CL_ServerModDownload_Frame (void);

void CL_Signon1 (void);
void CL_Signon2 (void);
void CL_Signon3 (void);
void CL_Signon4 (void);

void CL_Disconnect (void);
void CL_Disconnect_f (void);
void CL_ResetVRIKPoseCaches (void);
void CL_ResetVRIKState (void);
void CL_ExpireStaleVRIKPoses (void);
void CL_NextDemo (void);

void SV_UpdateInfo (int edict, const char *keyname, const char *value);

//
// cl_input
//
typedef struct
{
	int down[2]; // key nums holding it down
	int state;	 // low bit is down state
} kbutton_t;

extern kbutton_t in_mlook, in_klook;
extern kbutton_t in_strafe;
extern kbutton_t in_speed;

void	 CL_InitInput (void);
void	 CL_AccumulateCmd (void);
void	 CL_SendCmd (void);
void	 CL_SendMove (const usercmd_t *cmd);
int		 CL_ReadFromServer (void);
qboolean CL_AngleLocked (void);
void	 CL_AdjustAngles (void);
void	 CL_BaseMove (usercmd_t *cmd);
void	 CL_FinishMove (usercmd_t *cmd);
// Disposable keyboard/device preview; never consumes input or command clocks.
void	 CL_PreviewMove (usercmd_t *cmd);

void CL_UpdateBeam (struct qmodel_s *m, const char *trailname, const char *impactname, int ent, float *start, float *end);
void CL_ParseTEnt (void);
void CL_UpdateTEnts (void);

void CL_FreeState (void);
void CL_ClearState (void);
void CL_ClearTrailStates (void);

//
// cl_demo.c
//
void CL_StopPlayback (void);
int	 CL_GetMessage (void);
void CL_Seek_f (void);

void CL_Stop_f (void);
void CL_Record_f (void);
void CL_PlayDemo_f (void);
void CL_TimeDemo_f (void);
void CL_Resume_Record (qboolean recordsignons);

//
// cl_parse.c
//
void CL_ParseServerMessage (void);
void CL_RegisterParticles (void);
void CL_NewTranslation (int slot);

//
// view
//
void V_StartPitchDrift (void);
void V_StopPitchDrift (void);

void V_ParseDamage (void);
void V_SetContentsColor (int contents);

//
// cl_tent
//
void  CL_InitTEnts (void);
void  CL_SignonReply (void);
float CL_TraceLine (vec3_t start, vec3_t end, vec3_t impact, vec3_t normal, int *ent);
/* Read-only world hull trace for game-thread work that must not prepare the
 * particle system's shared brush-entity list. */
float CL_TraceWorldLine (vec3_t start, vec3_t end, vec3_t impact, vec3_t normal);

//
// chase
//
extern cvar_t chase_active;

void Chase_Init (void);
void TraceLine (vec3_t start, vec3_t end, vec3_t impact);
void Chase_UpdateForClient (void);	// johnfitz
void Chase_UpdateForDrawing (void); // johnfitz

void CL_ResetPredictionSmoothing (void);
void CL_FlushAckFrames (void);

/* Audio worker boundary. Queueing and callbacks run on the client main thread. */
qboolean CL_VoiceTransportAvailable (void);
qboolean CL_QueueVoicePacket (const voice_packet_t *packet);
void CL_SetVoiceReceiveCallback (voice_receive_callback_t callback, void *opaque);
void CL_ResetVoiceTransportState (void);

// Body codec only: caller must admit the private dialect before using this.
void CL_WritePrivateUsercmd (sizebuf_t *buf, const usercmd_t *cmd, unsigned int protocolflags, unsigned int capabilities);

#endif /* _CLIENT_H_ */
