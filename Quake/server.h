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

#include "vrik_codec.h"
#include "voice_protocol.h"

#define SERVER_INFO_STRING_SIZE 8192

typedef struct server_voice_packet_s
{
	uint64_t serial;
	double arrival_time;
	voice_packet_t packet;
} server_voice_packet_t;

typedef struct
{
	int				 maxclients;
	int				 maxclientslimit;
	struct client_s *clients;			 // [maxclients]
	int				 serverflags;		 // episode completion information
	qboolean		 changelevel_issued; // cleared when at SV_SpawnServer

	char serverinfo[SERVER_INFO_STRING_SIZE]; // \key\value infostring data.
} server_static_t;

//=============================================================================

typedef enum
{
	ss_loading,
	ss_active
} server_state_t;

#define NUM_BASIC_SPAWN_PARMS 16
#define NUM_TOTAL_SPAWN_PARMS 64

typedef struct
{
	qboolean active; // false if only a net client

	qboolean paused;
	qboolean loadgame;	 // handle connections specially
	qboolean loadgame_multiplayer; // inherited v6/v7 per-client restore state
	qboolean nomonsters; // server started with 'nomonsters' cvar active
	qboolean loadgame_client_saved[MAX_SCOREBOARD];
	qboolean loadgame_client_name_required[MAX_SCOREBOARD];
	char	 loadgame_client_names[MAX_SCOREBOARD][MAX_SCOREBOARDNAME];
	float	 loadgame_client_spawn_parms[MAX_SCOREBOARD][NUM_TOTAL_SPAWN_PARMS];
	int		 loadgame_client_colors[MAX_SCOREBOARD];
	int		 loadgame_client_old_frags[MAX_SCOREBOARD];
	byte	 loadgame_client_alpha[MAX_SCOREBOARD];
	byte	 *loadgame_client_edicts; // QC payload snapshots; excludes live edict metadata
	size_t	 loadgame_client_edict_size;

	char lastsave[128];

	qboolean coop_autosave_initialized;
	qboolean coop_autosave_mapstart_done;
	int		 coop_autosave_next_slot;
	double	 coop_autosave_last_realtime;
	double	 coop_autosave_retry_realtime;
	int		 coop_autosave_last_secrets;
	int		 coop_autosave_last_kill_bucket;
	int		 coop_autosave_last_serverflags;

	int	   lastcheck; // used by PF_checkclient
	double lastchecktime;

	qcvm_t qcvm; // Spike: entire qcvm state

	char			 name[64];					 // map name
	char			 modelname[64];				 // maps/<name>.bsp, for model_precache[0]
	const char		*model_precache[MAX_MODELS]; // NULL terminated
	struct qmodel_s *models[MAX_MODELS];
	const char		*sound_precache[MAX_SOUNDS]; // NULL terminated
	const char		*lightstyles[MAX_LIGHTSTYLES];
	server_state_t	 state; // some actions are only valid during load

	sizebuf_t datagram;
	byte	  datagram_buf[MAX_DATAGRAM];

	sizebuf_t reliable_datagram; // copied to all clients at end of frame
	byte	  reliable_datagram_buf[MAX_DATAGRAM];

	sizebuf_t signon;
	byte	  signon_buf[MAX_MSGLEN - 2]; // johnfitz -- was 8192, now uses MAX_MSGLEN

	unsigned protocol; // johnfitz
	unsigned protocolflags;

	sizebuf_t multicast; // selectively copied to clients by the multicast builtin
	byte	  multicast_buf[MAX_DATAGRAM];

	const char *particle_precache[MAX_PARTICLETYPES]; // NULL terminated

	entity_state_t *static_entities;
	int				num_statics;
	int				max_statics;

	struct ambientsound_s
	{
		vec3_t		 origin;
		unsigned int soundindex;
		float		 volume;
		float		 attenuation;
	}  *ambientsounds;
	int num_ambients;
	int max_ambients;

	struct svcustomstat_s
	{
		int		idx;
		int		type;
		int		fld;
		eval_t *ptr;
	} customstats[MAX_CL_STATS * 2]; // strings or numeric...
	size_t numcustomstats;

	int effectsmask; // only enable colored quad/penta dlights in 2021 release

	struct
	{
		qboolean active;
		int		 numwarnings;

		const char *changelevel;
		int			trigger_changelevel;
		int			valid_changelevel;
		int			intermission;
		int			skill_triggers;
		int			coop_spawns;
		int			dm_spawns;
		int			skill_ents[3];
	} mapchecks; // additional map checks for level designers
} server_t;

#define NUM_PING_TIMES		  16
#define SV_PRIVATE_CMD_QUEUE_SIZE 32
#define SV_PRIVATE_CMD_QUEUE_MAX_MSEC 250 // bound pending command history to a quarter second

typedef struct client_s
{
	qboolean active;   // false = client is free
	qboolean spawned;  // false = don't send datagrams (set when client acked the first entities)
	qboolean spawn_parms_pending; // SetNewParms waits until inherited-load identity is known
	qboolean dropasap; // has been told to go to another level
	enum
	{
		PRESPAWN_DONE,
		PRESPAWN_FLUSH = 1,
		//		PRESPAWN_SERVERINFO,
		PRESPAWN_MODELS,
		PRESPAWN_SOUNDS,
		PRESPAWN_PARTICLES,
		PRESPAWN_BASELINES,
		PRESPAWN_STATICS,
		PRESPAWN_AMBIENTS,
		PRESPAWN_SIGNONMSG,
	} sendsignon; // only valid before spawned
	int			 signonidx;
	unsigned int signon_sounds; //
	unsigned int signon_models; //

	double last_message; // reliable messages must be sent
						 // periodically

	struct qsocket_s *netconnection; // communications handle

	usercmd_t cmd;	   // movement
	vec3_t	  wishdir; // intended motion calced from cmd

	sizebuf_t message; // can be added to at any time,
					   // copied and clear once per frame
	byte	  msgbuf[MAX_MSGLEN];
	edict_t	 *edict;	// EDICT_NUM(clientnum+1)
	char	  name[32]; // for printing to other people
	int		  colors;

	float ping_times[NUM_PING_TIMES];
	int	  num_pings; // ping_times[num_pings%NUM_PING_TIMES]

	// spawn parms are carried from level to level
	float spawn_parms[NUM_TOTAL_SPAWN_PARMS];

	// client known data for deltas
	int old_frags;

	sizebuf_t datagram;
	byte	  datagram_buf[MAX_DATAGRAM];

	unsigned int limit_entities;   // vanilla is 600
	unsigned int limit_unreliable; // max allowed size for unreliables
	unsigned int limit_reliable;   // max (total) size of a reliable message.
	unsigned int limit_models;	   //
	unsigned int limit_sounds;	   //
	qboolean	 pextknown;
	unsigned int offered_qsvr; // capability received under PROTOCOL_QSVR_PROFILE
	unsigned int offered_pext2; // original public FTE offer, retained across map sign-ons
	unsigned int protocol_qsvr; // selected private wire profile; zero is public
	unsigned int protocol_pext1;
	unsigned int protocol_pext2;
	int weapon_contact_last_mode; // last mode appended to the reliable stream; -1 means not queued
	/* Optional VRIK pose transport state; unrelated to QSVR movement admission. */
	qboolean vrik_capable;
	unsigned char vrik_protocol_version;
	qboolean vrik_sequence_valid;
	qboolean vrik_inactive_sent;
	unsigned short vrik_last_sequence;
	unsigned int vrik_generation;
	double vrik_pose_time;
	double vrik_next_accept_time;
	vrik_pose_t vrik_pose;
	vrik_codec_pose_t vrik_pose_v3;
	qboolean vrik_v2_body_valid;
	unsigned char vrik_v2_body[VRIK_V2_BODY_BYTES];
	qboolean vrik_relay_sequence_valid[MAX_SCOREBOARD];
	unsigned short vrik_relay_sequence[MAX_SCOREBOARD];
	unsigned int vrik_relay_generation[MAX_SCOREBOARD];
	/* Generation admission already appended to each recipient's reliable stream. */
	unsigned int vrik_admitted_generation[MAX_SCOREBOARD];
	/* Opaque voice packets are queued per source and relayed per recipient. */
	qboolean voice_protocol_offered;
	qboolean voice_capable;
	unsigned int voice_generation;
	server_voice_packet_t voice_packets[VOICE_SERVER_QUEUE_CAPACITY];
	uint64_t voice_next_serial;
	double voice_rate_window_start;
	unsigned int voice_rate_packets;
	unsigned int voice_rate_bytes;
	uint64_t voice_relay_serial[MAX_SCOREBOARD];
	unsigned int voice_relay_generation[MAX_SCOREBOARD];
	unsigned char voice_relay_next_source;
	unsigned int resendstatsnum[MAX_CL_STATS / 32]; // the stats which need to be resent.
	unsigned int resendstatsstr[MAX_CL_STATS / 32]; // the stats which need to be resent.
	int			 oldstats_i[MAX_CL_STATS];			// previous values of stats. if these differ from the current values, reflag resendstats.
	float		 oldstats_f[MAX_CL_STATS];			// previous values of stats. if these differ from the current values, reflag resendstats.
	char		*oldstats_s[MAX_CL_STATS];
	struct entity_num_state_s
	{
		unsigned int   num; // ascending order, there can be gaps.
		entity_state_t state;
	}			 *previousentities;
	size_t		  numpreviousentities;
	size_t		  maxpreviousentities;
	unsigned int  snapshotresume;
	unsigned int *pendingentities_bits; // UF_ flags for each entity
	size_t		  numpendingentities;	// realloc if too small
#define SENDFLAG_PRESENT 0x80000000u	// tracks that we previously sent one of these ents (resulting in a remove if the ent gets remove()d).
#define SENDFLAG_REMOVE	 0x40000000u	// for packetloss to signal that we need to resend a remove.
#define SENDFLAG_USABLE	 0x00ffffffu	// SendFlags bits that the qc is actually able to use (don't get confused if the mod uses SendFlags=-1).
	struct deltaframe_s
	{ // quick overview of how this stuff actually works:
		// when the server notices a gap in the ack sequence, we walk through the dropped frames and reflag everything that was dropped.
		// if the server isn't tracking enough frames, then we just treat those as dropped;
		// small note: when an entity is new, it re-flags itself as new for the next packet too, this reduces the immediate impact of packetloss on new
		// entities. reflagged state includes stats updates, entity updates, and entity removes.
		int			 sequence; // to see if its stale
		float		 timestamp;
		unsigned int resendstatsnum[MAX_CL_STATS / 32];
		unsigned int resendstatsstr[MAX_CL_STATS / 32];
		struct
		{
			unsigned int num;
			unsigned int ebits;
			unsigned int csqcbits;
		}  *ents;
		int numents; // doesn't contain an entry for every entity, just ones that were sent this frame. no 0 bits
		int maxents;
	}		*frames;
	size_t	 numframes; // preallocated power-of-two
	int		 lastacksequence;
	int		 lastmovemessage; // accepted/received cursor; private commands are also retained below
	int		 private_completed_move;
	int		 private_retired_move; // queued records retired through owner physics completion
	int		 private_discarded_move; // explicit cutoff for commands dropped without queue consumption
	qboolean	 private_pmove_walk_selected; // latched until command-queue/serverinfo reset
	double	 private_pmove_credit_msec; // fractional milliseconds; physics accrual/cap lives in sv_phys.c
	float	 private_pmove_jump_secs; // short PMove jump debounce across accepted commands
	usercmd_t	 private_pmove_last_cmd; // last command completed through its QC callbacks
	qboolean	 private_pmove_last_cmd_valid; // distinguishes no completion from a zero-input command
	usercmd_t private_cmd_queue[SV_PRIVATE_CMD_QUEUE_SIZE];
	unsigned int private_cmd_queue_head;
	unsigned int private_cmd_queue_count;
	unsigned int private_cmd_queue_msec;
	double	 lastmovetime;
	unsigned int private_latest_buttons;
	unsigned int private_latched_buttons;
	unsigned int private_latched_impulse;
	qboolean knowntoqc; // putclientinserver was called

	char userinfo[SERVER_INFO_STRING_SIZE]; // spike -- for csqc to (ab)use.
} client_t;

//=============================================================================
// clang-format off
// edict->movetype values
typedef enum
{
	MOVETYPE_NONE				= 0,	// never moves
	MOVETYPE_ANGLENOCLIP		= 1,
	MOVETYPE_ANGLECLIP			= 2,
	MOVETYPE_WALK				= 3,	// gravity
	MOVETYPE_STEP				= 4,	// gravity, special edge handling
	MOVETYPE_FLY				= 5,
	MOVETYPE_TOSS				= 6,	// gravity
	MOVETYPE_PUSH				= 7,	// no clip to world, push and crush
	MOVETYPE_NOCLIP				= 8,
	MOVETYPE_FLYMISSILE			= 9,	// extra size to monsters
	MOVETYPE_BOUNCE				= 10,
	MOVETYPE_GIB				= 11,	// 2021 rerelease gibs
} emovetype_t;

// edict->solid values
typedef enum
{
	SOLID_NOT					= 0,	// no interaction with other objects
	SOLID_TRIGGER				= 1,	// touch on edge, but not blocking
	SOLID_BBOX					= 2,	// touch on edge, block
	SOLID_SLIDEBOX				= 3,	// touch on edge, but not an onground
	SOLID_BSP					= 4,	// bsp clip, touch on edge, block
} esolid_t;

// edict->deadflag values
typedef enum
{
	DEAD_NO						= 0,
	DEAD_DYING					= 1,
	DEAD_DEAD					= 2,
	DEAD_RESPAWNABLE			= 3,
} edeadflag_t;

// edict->takedamage
typedef enum
{
	DAMAGE_NO					= 0,
	DAMAGE_YES					= 1,
	DAMAGE_AIM					= 2,
} etakedamage_t;

// edict->flags
typedef enum
{
	FL_FLY						= 1,
	FL_SWIM						= 2,
//	FL_GLIMPSE					= 4,
	FL_CONVEYOR					= 4,
	FL_CLIENT					= 8,
	FL_INWATER					= 16,
	FL_MONSTER					= 32,
	FL_GODMODE					= 64,
	FL_NOTARGET					= 128,
	FL_ITEM						= 256,
	FL_ONGROUND					= 512,
	FL_PARTIALGROUND			= 1024,	// not all corners are valid
	FL_WATERJUMP				= 2048,	// player jumping out of water
	FL_JUMPRELEASED				= 4096,	// for jump debouncing
} eflags_t;

// entity effects
typedef enum
{
	EF_BRIGHTFIELD				= 1,
	EF_MUZZLEFLASH 				= 2,
	EF_BRIGHTLIGHT 				= 4,
	EF_DIMLIGHT 				= 8,
	EF_QEX_QUADLIGHT			= 16,	// 2021 rerelease
	EF_QEX_PENTALIGHT			= 32,	// 2021 rerelease
	EF_QEX_CANDLELIGHT			= 64,	// 2021 rerelease
} efx_t;

// spawnflags
typedef enum
{
	SPAWNFLAG_NOT_EASY			= 256,
	SPAWNFLAG_NOT_MEDIUM		= 512,
	SPAWNFLAG_NOT_HARD			= 1024,
	SPAWNFLAG_NOT_DEATHMATCH	= 2048,
} spawnflags_t;

#define MSG_BROADCAST	  0 // unreliable to all
#define MSG_ONE			  1 // reliable to one (msg_entity)
#define MSG_ALL			  2 // reliable to all
#define MSG_INIT		  3 // write to the init string
#define MSG_EXT_MULTICAST 4 // temporary buffer that can be splurged more reliably / with more control.
#define MSG_EXT_ENTITY	  5 // for csqc networking. we don't actually support this. I'm just defining it for completeness.

// clang-format on

//============================================================================

extern cvar_t teamplay;
extern cvar_t skill;
extern cvar_t deathmatch;
extern cvar_t coop;
extern cvar_t sv_save_multiplayer;
extern cvar_t sv_coop_autosave;
extern cvar_t sv_coop_autosave_slots;
extern cvar_t sv_coop_autosave_min_interval;
extern cvar_t sv_coop_autosave_kill_interval;
extern cvar_t sv_coop_classic;
extern cvar_t sv_coop_noplayerclip;
extern cvar_t sv_coop_notelefrag;
extern cvar_t sv_coop_shared_pickups;
extern cvar_t sv_coop_respawn_keep_weapons_ammo;
extern cvar_t fraglimit;
extern cvar_t timelimit;

qboolean SV_CoopFeatureEnabled (const cvar_t *feature, qboolean modern_default);

extern server_static_t svs; // persistant server info
extern server_t		   sv;	// local server

extern client_t *host_client;

extern edict_t *sv_player;

//===========================================================

void SV_Init (void);
void Host_CoopAutosaveFrame (void);

void SV_StartParticle (vec3_t org, vec3_t dir, int color, int count);
void SV_StartSound (edict_t *entity, float *origin, int channel, const char *sample, int volume, float attenuation);
void SV_LocalSound (client_t *client, const char *sample); // for 2021 rerelease

void SV_DropClient (qboolean crash);
void SV_AppendVRIKRetirement (client_t *client, int slot, unsigned int generation);

void SV_CoopRespawnRefreshClientInventory (edict_t *ent);
void SV_CoopRespawnSaveClientEdict (edict_t *ent, edict_t *snapshot);
void SV_CoopRespawnRestoreSavedInventory (edict_t *ent, edict_t *snapshot);
void SV_CoopRespawnInventoryResetClientSlot (int slot);
void SV_CoopRespawnInventoryResetState (void);
void SV_CoopRespawnSyncSharedKeys (edict_t *source);

#define SV_COOP_GIVEKEYS_SILVER 1
#define SV_COOP_GIVEKEYS_GOLD 2
#define SV_COOP_GIVEKEYS_CUSTOM 4
#define SV_COOP_GIVEKEYS_ALL \
	(SV_COOP_GIVEKEYS_SILVER | SV_COOP_GIVEKEYS_GOLD | \
	 SV_COOP_GIVEKEYS_CUSTOM)
qboolean SV_CoopGiveKeys (edict_t *player, int key_flags);
int SV_DeclaredWeaponBits (void);
qboolean SV_CoopUsesCountedKeys (void);
void SV_CoopSharedApplyToJoiningClient (edict_t *player);
void SV_CoopSharedMergeRestoredClient (edict_t *source);
void SV_CoopSharedRebuildGrantedKeys (edict_t *source);
void SV_CoopSharedReconcileClientDeath (edict_t *player);
qboolean SV_CoopSharedBeginClientTouch (edict_t *client);
void SV_CoopSharedEndClientTouch (edict_t *client);
void SV_CoopSharedResetState (void);
void SV_CoopSharedResetClientSlot (int slot);

void SVFTE_Ack (client_t *client, int sequence);
void SVFTE_DestroyFrames (client_t *client);
void SV_BuildEntityState (edict_t *ent, entity_state_t *state);
void SV_SendClientMessages (void);
void SV_ClearDatagram (void);

int SV_ModelIndex (const char *name);

void SV_SetIdealPitch (void);

void SV_AddUpdates (void);

void SV_ClientThink (void);
void SV_AddClientToServer (struct qsocket_s *ret);

void SV_ClientPrintf (const char *fmt, ...) FUNC_PRINTF (1, 2);
void SV_BroadcastPrintf (const char *fmt, ...) FUNC_PRINTF (1, 2);

void SV_Physics (void);

qboolean SV_CheckBottom (edict_t *ent);
qboolean SV_movestep (edict_t *ent, vec3_t move, qboolean relink);

void SV_WriteClientdataToMessage (client_t *client, sizebuf_t *msg);

void SV_MoveToGoal (void);

void SV_ConnectClient (int clientnum); // called from the netcode to add new clients. also called from pr_ext to spawn new botclients.
void SV_CheckForNewClients (void);
void SV_RunClients (void);
void SV_ResetPrivateCommandQueue (client_t *client);
void SV_ReceiveVRIKPoseV2 (client_t *client, const vrik_v2_pose_t *pose,
	const unsigned char body[VRIK_V2_BODY_BYTES]);
void SV_ReceiveVRIKPoseV3 (client_t *client, const vrik_codec_pose_t *pose);
void SV_ExpireVRIKPoses (void);
void SV_ReceiveVoicePacket (client_t *client, const voice_packet_t *packet);
extern cvar_t sv_voice;
void SV_FinishPrivateUsercmds (void);
qboolean SV_PrivateWalkTrialSelected (client_t *client);
void SV_PrivateWalkTrialSelectAtBegin (client_t *client);
const char *SV_PrivateWalkTrialStateError (edict_t *ent, client_t *client, const usercmd_t *cmd);
void SV_ClientUpdateAnglesForClient (client_t *client);
void SV_ClearVRWeaponPoseScope (void);
void SV_VRWeaponPoseSetOrigin (edict_t *ent);
void SV_VRWeaponPoseLinked (edict_t *ent);
void SV_SaveSpawnparms ();
void SV_SpawnServer (const char *server);

// Body codec only; sequence admission and receipt time remain queue-owned.
qboolean SV_ReadPrivateUsercmd (usercmd_t *cmd, unsigned int sequence, unsigned int protocolflags, unsigned int capabilities);

#endif /* _QUAKE_SERVER_H */
