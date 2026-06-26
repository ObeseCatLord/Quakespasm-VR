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

#ifndef _QUAKE_SERVER_H
#define _QUAKE_SERVER_H

// server.h

typedef struct
{
	int			maxclients;
	int			maxclientslimit;
	struct client_s	*clients;		// [maxclients]
	int			serverflags;		// episode completion information
	qboolean	changelevel_issued;	// cleared when at SV_SpawnServer
	qboolean	coop_loadgame_late_join_spawns_near;
	qboolean	coop_initial_spawn_client[MAX_SCOREBOARD];
} server_static_t;

//=============================================================================

#define MAX_SIGNON_BUFFERS 256

typedef enum {ss_loading, ss_active} server_state_t;

typedef struct
{
	qboolean	active;				// false if only a net client

	qboolean	paused;
	qboolean	loadgame;			// handle connections specially
	qboolean	loadgame_resumed;
	qboolean	nomonsters;			// server started with 'nomonsters' cvar active

	qboolean	loadgame_client_saved[MAX_SCOREBOARD];
	byte		*loadgame_client_edicts;

	qboolean	coop_autosave_initialized;
	qboolean	coop_autosave_mapstart_done;
	int			coop_autosave_next_slot;
	double		coop_autosave_last_time;
	double		coop_autosave_last_realtime;
	int			coop_autosave_last_secrets;
	int			coop_autosave_last_kill_bucket;
	int			coop_autosave_last_serverflags;

	int			lastcheck;			// used by PF_checkclient
	double		lastchecktime;

	qcvm_t		qcvm;				// Spike: entire qcvm state

	char		name[64];			// map name
	char		modelname[64];		// maps/<name>.bsp, for model_precache[0]
	struct qmodel_s	*worldmodel;
	qboolean	skyroom_pos_known;
	vec4_t		skyroom_pos;
	const char	*model_precache[MAX_MODELS];	// NULL terminated
	struct qmodel_s	*models[MAX_MODELS];
	const char	*sound_precache[MAX_SOUNDS];	// NULL terminated
	const char	*particle_precache[MAX_PARTICLETYPES];
	const char	*lightstyles[MAX_LIGHTSTYLES];
	server_state_t	state;			// some actions are only valid during load

	sizebuf_t	datagram;
	byte		datagram_buf[MAX_DATAGRAM];

	sizebuf_t	multicast;			// temporary QC message buffer, used for CSQC entity payloads
	byte		multicast_buf[MAX_DATAGRAM];

	sizebuf_t	reliable_datagram;	// copied to all clients at end of frame
	byte		reliable_datagram_buf[MAX_DATAGRAM];

	sizebuf_t	*signon;
	int			num_signon_buffers;
	sizebuf_t	*signon_buffers[MAX_SIGNON_BUFFERS];

	unsigned	protocol; //johnfitz
	unsigned	protocolflags;

	struct svcustomstat_s
	{
		int idx;
		int type;
		int fld;
		eval_t *ptr;
	} customstats[MAX_CL_STATS*2];	//strings or numeric...
	size_t		numcustomstats;
} server_t;

void SV_SetupSkyRoom (const char *value);


#define	NUM_PING_TIMES		16
#define	NUM_SPAWN_PARMS		16

typedef struct client_s
{
	qboolean		active;				// false = client is free
	qboolean		spawned;			// false = don't send datagrams
	qboolean		dropasap;			// has been told to go to another level
	enum
	{
		PRESPAWN_DONE,
		PRESPAWN_FLUSH=1,
		PRESPAWN_SIGNONBUFS,
		PRESPAWN_SIGNONMSG,
	}				sendsignon;			// only valid before spawned
	int				signonidx;

	double			last_message;		// reliable messages must be sent
										// periodically

	struct qsocket_s *netconnection;	// communications handle

	usercmd_t		cmd;				// movement
	vec3_t			wishdir;			// intended motion calced from cmd
	double			last_move_time;
	double			lastmovetime;
	qboolean		input_stale;
	qboolean		moveext;
	int				lastmovemessage;
	int				pendingmovemessage;
	qboolean		move_pending;
	int				net_move_packets_received;
	int				net_move_cmds_received;
	int				net_move_cmds_accepted;
	int				net_move_cmds_stale;
	int				net_move_cmds_simulated;
	int				net_move_stale_log_suppressed;
	int				net_move_bundle_max;
	int				net_move_last_bundle;
	int				net_move_last_gap;
	double			net_move_stale_log_time;
	double			net_move_input_log_time;
	float			net_move_last_sim_seconds;
	int				net_snapshot_sequence;
	int				net_snapshot_ack;
	int				net_snapshot_packets_sent;
	int				net_snapshot_split_packets;
	int				net_snapshot_unsent_entities;
	int				net_snapshot_updates_sent;
	int				net_snapshot_last_packets;
	int				net_snapshot_last_bytes;
	int				net_snapshot_max_bytes;
	int				net_snapshot_max_packets;
	int				net_snapshot_ack_lag_max;
	double			net_snapshot_last_ack_time;
	double			net_snapshot_ack_age_max;
	double			net_snapshot_last_summary_time;

		sizebuf_t		message;			// can be added to at any time,
										// copied and clear once per frame
	byte			msgbuf[MAX_MSGLEN];
	sizebuf_t		datagram;			// private unreliable data for this client
	byte			datagram_buf[MAX_DATAGRAM];
	edict_t			*edict;				// EDICT_NUM(clientnum+1)
	char			name[32];			// for printing to other people
	int				colors;

	float			ping_times[NUM_PING_TIMES];
	int				num_pings;			// ping_times[num_pings%NUM_PING_TIMES]

// spawn parms are carried from level to level
	float			spawn_parms[NUM_SPAWN_PARMS];

// client known data for deltas
	int				old_frags;

	int				oldstats_i[MAX_CL_STATS];		//previous values of stats. if these differ from the current values, reflag resendstats.
	float			oldstats_f[MAX_CL_STATS];		//previous values of stats. if these differ from the current values, reflag resendstats.
	char			*oldstats_s[MAX_CL_STATS];

	qboolean		pextknown;
	unsigned int	protocol_pext1;
	unsigned int	protocol_pext2;
	unsigned int	resendstatsnum[MAX_CL_STATS / 32];
	unsigned int	resendstatsstr[MAX_CL_STATS / 32];
	struct entity_num_state_s
	{
		unsigned int	num;
		entity_state_t state;
	}				*previousentities;
	size_t			numpreviousentities;
	size_t			maxpreviousentities;
	unsigned int	snapshotresume;
	unsigned int	*pendingentities_bits;
	size_t			numpendingentities;
	unsigned int	*pendingcsqcentities_bits;
#define	SENDFLAG_PRESENT	0x80000000u
#define	SENDFLAG_REMOVE		0x40000000u
#define	SENDFLAG_USABLE		0x00ffffffu
	size_t			numpendingcsqcentities;
	struct deltaframe_s
	{
		int			sequence;
		float		timestamp;
		unsigned int	resendstatsnum[MAX_CL_STATS / 32];
		unsigned int	resendstatsstr[MAX_CL_STATS / 32];
		struct
			{
				unsigned int num;
				unsigned int ebits;
				unsigned int csqcbits;
			}			*ents;
			int			numents;
			int			maxents;
	}				*frames;
	size_t			numframes;
	int				lastacksequence;
	qboolean		csqcactive;
	qboolean		usingpmove;

	// VR Data
	qboolean		is_vr_client;
	vec3_t			vr_handpos;
	vec3_t			vr_handrot;
	vec3_t			vr_roomscalemove;
	vec3_t			vr_roomscale_accum;
} client_t;

void SVFTE_Ack (client_t *client, int sequence);
void SVFTE_DestroyFrames (client_t *client);

//=============================================================================

// edict->movetype values
#define	MOVETYPE_NONE			0		// never moves
#define	MOVETYPE_ANGLENOCLIP	1
#define	MOVETYPE_ANGLECLIP		2
#define	MOVETYPE_WALK			3		// gravity
#define	MOVETYPE_STEP			4		// gravity, special edge handling
#define	MOVETYPE_FLY			5
#define	MOVETYPE_TOSS			6		// gravity
#define	MOVETYPE_PUSH			7		// no clip to world, push and crush
#define	MOVETYPE_NOCLIP			8
#define	MOVETYPE_FLYMISSILE		9		// extra size to monsters
#define	MOVETYPE_BOUNCE			10
#define	MOVETYPE_GIB			11		// 2021 rerelease gibs

// edict->solid values
#define	SOLID_NOT				0		// no interaction with other objects
#define	SOLID_TRIGGER			1		// touch on edge, but not blocking
#define	SOLID_BBOX				2		// touch on edge, block
#define	SOLID_SLIDEBOX			3		// touch on edge, but not an onground
#define	SOLID_BSP				4		// bsp clip, touch on edge, block

// edict->deadflag values
#define	DEAD_NO					0
#define	DEAD_DYING				1
#define	DEAD_DEAD				2

#define	DAMAGE_NO				0
#define	DAMAGE_YES				1
#define	DAMAGE_AIM				2

// edict->flags
#define	FL_FLY					1
#define	FL_SWIM					2
//#define	FL_GLIMPSE				4
#define	FL_CONVEYOR				4
#define	FL_CLIENT				8
#define	FL_INWATER				16
#define	FL_MONSTER				32
#define	FL_GODMODE				64
#define	FL_NOTARGET				128
#define	FL_ITEM					256
#define	FL_ONGROUND				512
#define	FL_PARTIALGROUND		1024	// not all corners are valid
#define	FL_WATERJUMP			2048	// player jumping out of water
#define	FL_JUMPRELEASED			4096	// for jump debouncing

// entity effects

#define	EF_BRIGHTFIELD			1
#define	EF_MUZZLEFLASH 			2
#define	EF_BRIGHTLIGHT 			4
#define	EF_DIMLIGHT 			8

#define	SPAWNFLAG_NOT_EASY			256
#define	SPAWNFLAG_NOT_MEDIUM		512
#define	SPAWNFLAG_NOT_HARD			1024
#define	SPAWNFLAG_NOT_DEATHMATCH	2048

//============================================================================

extern cvar_t teamplay;
extern cvar_t skill;
extern cvar_t deathmatch;
extern cvar_t coop;
extern cvar_t sv_nofriendlyfire;
extern cvar_t sv_coop_noplayerclip;
extern cvar_t sv_coop_weapon_targetfix;
extern cvar_t sv_coop_pickup_targetlog;
extern cvar_t sv_coop_pickup_targetfix;
extern cvar_t sv_coop_pickup_targetfix_classes;
extern cvar_t sv_coop_ammo_respawn;
extern cvar_t sv_coop_ammo_respawn_time;
extern cvar_t sv_coop_progression_item_respawn;
extern cvar_t sv_coop_progression_item_respawn_classes;
extern cvar_t sv_coop_revive;
extern cvar_t sv_coop_revive_health;
extern cvar_t sv_coop_revive_range;
extern cvar_t sv_coop_respawn_near_player;
extern cvar_t sv_coop_respawn_delay;
extern cvar_t sv_coop_respawn_keep_weapons_ammo;
extern cvar_t sv_coop_autosave;
extern cvar_t sv_coop_autosave_slots;
extern cvar_t sv_coop_autosave_min_interval;
extern cvar_t sv_coop_autosave_kill_interval;
extern cvar_t sv_coop_predictmove;
extern cvar_t sv_nqplayerphysics;
extern cvar_t sv_triggerdebug;
extern cvar_t sv_vr_jump_velocity;
qboolean SV_IsVRClientSlot(int num);
extern cvar_t sv_netdiag_interval;
extern cvar_t sv_save_multiplayer;
extern cvar_t sv_cmdfile;
extern cvar_t fraglimit;
extern cvar_t timelimit;

extern	server_static_t	svs;				// persistant server info
extern	server_t		sv;					// local server

extern	client_t	*host_client;

extern	edict_t		*sv_player;

//===========================================================

void SV_Init (void);

void SV_StartParticle (vec3_t org, vec3_t dir, int color, int count);
void SV_StartSound (edict_t *entity, int channel, const char *sample, int volume,
    float attenuation);
void SV_LocalSound (client_t *client, const char *sample); // for 2021 rerelease

void SV_DropClient (qboolean crash);

void SV_SendClientMessages (void);
void SV_ClearDatagram (void);
void SV_ReserveSignonSpace (int numbytes);

int SV_ModelIndex (const char *name);

void SV_SetIdealPitch (void);

void SV_AddUpdates (void);

void SV_ClientThink (void);
void SV_FinishPMoveUsercmd(client_t *client);
qboolean SV_RunClientPMoveCommand(client_t *client);
void SV_AddClientToServer (struct qsocket_s	*ret);

void SV_ClientPrintf (const char *fmt, ...) FUNC_PRINTF(1,2);
void SV_BroadcastPrintf (const char *fmt, ...) FUNC_PRINTF(1,2);

void SV_Physics (void);
void SV_CoopReviveBeginPostThink(edict_t *ent);
void SV_CoopReviveEndPostThink(void);
void SV_CoopReviveApplyPending(void);
void SV_CoopReviveFromTrace(vec3_t start, vec3_t end, edict_t *ent,
                            float trace_fraction);
qboolean SV_CoopRespawnPlaceNearPlayer(edict_t *ent);
qboolean SV_CoopRespawnTeleportToPlayer(edict_t *ent, edict_t *target);

qboolean SV_CheckBottom (edict_t *ent);
qboolean SV_movestep (edict_t *ent, vec3_t move, qboolean relink);

void SV_ResetClientMoveState (client_t *client);
void SV_WriteClientdataToMessage (edict_t *ent, sizebuf_t *msg);

void SV_MoveToGoal (void);

void SV_CheckForNewClients (void);
void SV_RunClients (void);
void SV_SaveSpawnparms ();
void SV_SpawnServer (const char *server);

#endif	/* _QUAKE_SERVER_H */
