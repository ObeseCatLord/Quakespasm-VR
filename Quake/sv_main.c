/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
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
// sv_main.c -- server main program

#include "quakedef.h"
#include "pmove.h"
#include "vr.h"

server_t	sv;
server_static_t	svs;

static char	localmodels[MAX_MODELS][8];	// inline model names for precache

int		sv_protocol = PROTOCOL_RMQ; //johnfitz

extern cvar_t nomonsters;
// Live cap for remote unreliable packets. Match QSS-M's DATAGRAM_MTU by
// default; lower this only when a specific network path needs more headroom.
cvar_t sv_maxpacketsize = {"sv_maxpacketsize", "1400", CVAR_NONE};
cvar_t sv_netdiag_interval = {"sv_netdiag_interval", "5", CVAR_NONE};
cvar_t sv_replacement_maxpackets = {"sv_replacement_maxpackets", "0", CVAR_NONE};
cvar_t sv_predict_nqmovement = {"sv_predict_nqmovement", "0", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_nopunchangle = {"sv_nopunchangle", "0", CVAR_NONE};
// When SV_WriteEntitiesToClient overflows the per-client datagram, the entity
// that gets evicted is whichever the loop reached last. With sv_netsort=1
// (ironwail's heuristic) entities are sorted by distance-to-player and PVS
// orientation first, so when packets get clipped it's distant or behind-camera
// entities that drop, not your weapon hand or the player next to you.
cvar_t sv_netsort = {"sv_netsort", "1", CVAR_NONE};
cvar_t sv_coop_classic = {"sv_coop_classic", "0", CVAR_ARCHIVE | CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_notelefrag = {"sv_coop_notelefrag", "-1", CVAR_ARCHIVE | CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_player_teleport_fallback = {"sv_coop_player_teleport_fallback", "-1", CVAR_ARCHIVE | CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_shared_pickups = {"sv_coop_shared_pickups", "-1", CVAR_ARCHIVE | CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_weapon_targetfix = {"sv_coop_weapon_targetfix", "-1", CVAR_ARCHIVE};
cvar_t sv_coop_pickup_targetlog = {"sv_coop_pickup_targetlog", "0", CVAR_NONE};
cvar_t sv_coop_pickup_targetfix = {"sv_coop_pickup_targetfix", "0", CVAR_NONE};
cvar_t sv_coop_pickup_targetfix_classes = {"sv_coop_pickup_targetfix_classes", "", CVAR_NONE};
cvar_t sv_coop_ammo_respawn = {"sv_coop_ammo_respawn", "-1", CVAR_ARCHIVE | CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_ammo_respawn_time = {"sv_coop_ammo_respawn_time", "30", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_progression_item_respawn = {"sv_coop_progression_item_respawn", "-1", CVAR_ARCHIVE | CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_progression_item_respawn_time = {"sv_coop_progression_item_respawn_time", "5", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_progression_item_respawn_classes = {"sv_coop_progression_item_respawn_classes", "item_jboots item_jboots_timed item_artifact_envirosuit item_artifact_wetsuit item_artifact_airtank item_artifact_divingsuit", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_revive = {"sv_coop_revive", "-1", CVAR_ARCHIVE | CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_revive_health = {"sv_coop_revive_health", "25", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_revive_range = {"sv_coop_revive_range", "96", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_respawn_near_player = {"sv_coop_respawn_near_player", "-1", CVAR_ARCHIVE | CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_respawn_delay = {"sv_coop_respawn_delay", "10", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_respawn_keep_weapons_ammo = {"sv_coop_respawn_keep_weapons_ammo", "-1", CVAR_ARCHIVE | CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_autosave = {"sv_coop_autosave", "-1", CVAR_ARCHIVE | CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_autosave_slots = {"sv_coop_autosave_slots", "4", CVAR_NONE};
cvar_t sv_coop_autosave_min_interval = {"sv_coop_autosave_min_interval", "30", CVAR_NONE};
cvar_t sv_coop_autosave_kill_interval = {"sv_coop_autosave_kill_interval", "10", CVAR_NONE};
cvar_t sv_coop_predictmove = {"sv_coop_predictmove", "0", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_triggerdebug = {"sv_triggerdebug", "0", CVAR_NONE};
cvar_t sv_vr_jump_velocity = {"sv_vr_jump_velocity", "297", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_skyroom_pvs = {"sv_skyroom_pvs", "1", CVAR_NONE};

//============================================================================

static void SVFTE_SetupFrames (client_t *client);
static qboolean SVFTE_SendClientDatagram (client_t *client, int maxsize);
static void SV_WriteMoveAckPayloadToMessage (client_t *client, sizebuf_t *msg);

#define VRIK_SVC_MESSAGE_BYTES (1 + 2 + 4 + VRIK_POSE_WIRE_BYTES)
#define VRIK_SERVER_MIN_INTERVAL 0.025
static unsigned int sv_vrik_next_generation;

static qboolean SV_VRIKPoseIsValid(const vrik_pose_t *pose)
{
	int tracker;
	int axis;

	if (pose->flags & ~VRIK_FLAG_KNOWN)
		return false;
	if ((pose->flags & VRIK_FLAG_ACTIVE) &&
		!(pose->flags & VRIK_FLAG_HEAD_TRACKED))
		return false;
	if (!isfinite(pose->body_yaw))
		return false;
	for (tracker = 0; tracker < VRIK_TRACKER_COUNT; tracker++)
	{
		for (axis = 0; axis < 3; axis++)
			if (!isfinite(pose->position[tracker][axis]) ||
				!isfinite(pose->orientation[tracker][axis]))
				return false;
		if (pose->position[tracker][0] * pose->position[tracker][0] +
				pose->position[tracker][1] * pose->position[tracker][1] +
				pose->position[tracker][2] * pose->position[tracker][2] >
			VRIK_MAX_ROOT_LOCAL_OFFSET * VRIK_MAX_ROOT_LOCAL_OFFSET)
			return false;
	}
	return true;
}

static qboolean SV_VRIKSequenceIsNewer(unsigned short sequence,
	unsigned short previous)
{
	return (short)(sequence - previous) > 0;
}

static void SV_WriteVRIKAngle16(sizebuf_t *msg, float angle)
{
	MSG_WriteShort(msg, (short)(Q_rint(angle * 65536.0 / 360.0) & 0xffff));
}

static void SV_WriteVRIKPose(sizebuf_t *msg, int entitynum,
	unsigned int generation, const vrik_pose_t *pose)
{
	int tracker;
	int axis;

	MSG_WriteByte(msg, svc_vrikpose);
	MSG_WriteShort(msg, entitynum);
	MSG_WriteLong(msg, (int)generation);
	MSG_WriteShort(msg, (short)pose->sequence);
	MSG_WriteByte(msg, pose->flags);
	SV_WriteVRIKAngle16(msg, pose->body_yaw);
	for (tracker = 0; tracker < VRIK_TRACKER_COUNT; tracker++)
		for (axis = 0; axis < 3; axis++)
			MSG_WriteShort(msg, Q_rint(pose->position[tracker][axis] * 8.0f));
	for (tracker = 0; tracker < VRIK_TRACKER_COUNT; tracker++)
		for (axis = 0; axis < 3; axis++)
			SV_WriteVRIKAngle16(msg, pose->orientation[tracker][axis]);
}

static void SV_RelayVRIKPose(client_t *source, const vrik_pose_t *pose)
{
	int source_num;
	int i;

	if (!source || !source->edict)
		return;
	source_num = NUM_FOR_EDICT(source->edict);
	for (i = 0; i < svs.maxclients; i++)
	{
		client_t *recipient = &svs.clients[i];

		if (recipient == source || !recipient->active || !recipient->spawned ||
			!recipient->vrik_capable)
			continue;
		if (recipient->datagram.cursize + VRIK_SVC_MESSAGE_BYTES >
			recipient->datagram.maxsize)
			continue;
		SV_WriteVRIKPose(&recipient->datagram, source_num,
			source->vrik_generation, pose);
	}
}

void SV_ReceiveVRIKPose(client_t *client, const vrik_pose_t *pose)
{
	if (!client || !pose || !client->active || !client->spawned ||
		!client->edict || !client->vrik_capable ||
		!SV_VRIKPoseIsValid(pose))
		return;
	if (client->vrik_sequence_valid &&
		!SV_VRIKSequenceIsNewer(pose->sequence, client->vrik_last_sequence))
		return;

	/* Client scheduling is not an authority boundary. Bound accepted and
	 * amplified pose traffic even if a peer sends one pose per datagram. */
	if (realtime < client->vrik_next_accept_time)
		return;
	if (!(pose->flags & VRIK_FLAG_ACTIVE) && client->vrik_inactive_sent)
		return;

	if (!client->vrik_generation)
	{
		client->vrik_generation = ++sv_vrik_next_generation;
		if (!client->vrik_generation)
			client->vrik_generation = ++sv_vrik_next_generation;
	}
	client->vrik_pose = *pose;
	client->vrik_last_sequence = pose->sequence;
	client->vrik_sequence_valid = true;
	client->vrik_pose_time = realtime;
	client->vrik_next_accept_time = realtime + VRIK_SERVER_MIN_INTERVAL;
	client->vrik_inactive_sent = !(pose->flags & VRIK_FLAG_ACTIVE);
	SV_RelayVRIKPose(client, pose);
}

void SV_ExpireVRIKPoses(void)
{
	int i;

	for (i = 0; i < svs.maxclients; i++)
	{
		client_t *client = &svs.clients[i];
		vrik_pose_t inactive;

		if (!client->active || !client->vrik_capable ||
			!client->vrik_sequence_valid || client->vrik_inactive_sent ||
			!(client->vrik_pose.flags & VRIK_FLAG_ACTIVE) ||
			realtime - client->vrik_pose_time <= VRIK_POSE_STALE_TIME)
			continue;

		Q_memset(&inactive, 0, sizeof(inactive));
		inactive.sequence = client->vrik_last_sequence + 1;
		client->vrik_pose = inactive;
		client->vrik_last_sequence = inactive.sequence;
		client->vrik_pose_time = realtime;
		client->vrik_inactive_sent = true;
		SV_RelayVRIKPose(client, &inactive);
	}
}

static qboolean SV_IsLocalClient (client_t *client)
{
	return client && client->netconnection &&
		Q_strcmp (NET_QSocketGetAddressString (client->netconnection), "LOCAL") == 0;
}

/*
 * Co-op compatibility switches use a tri-state value: -1 inherits the
 * selected profile, while 0 and 1 are explicit per-feature overrides.
 * This lets sv_coop_classic restore traditional behavior without taking
 * control away from an administrator's individual settings.
 */
qboolean SV_CoopFeatureEnabled(const cvar_t *feature,
                               qboolean modern_default)
{
	return SV_CoopFeatureLevel(feature, modern_default ? 1 : 0) != 0;
}

int SV_CoopFeatureLevel(const cvar_t *feature, int modern_default)
{
	if (feature && feature->value >= 0.0f)
		return (int)feature->value;
	return sv_coop_classic.value ? 0 : modern_default;
}

static int SV_RemoteUnreliableLimit (void)
{
	int maxsize;

	maxsize = (int)sv_maxpacketsize.value;
	if (maxsize <= 0)
		maxsize = DATAGRAM_MTU;
	return CLAMP (512, maxsize, DATAGRAM_MTU);
}

static void SV_SetClientLimits (client_t *client)
{
	unsigned int maxentities;

	if (!client)
		return;

	maxentities = qcvm && qcvm->max_edicts > 0 ?
		(unsigned int)qcvm->max_edicts : MAX_EDICTS;
	if (maxentities > MAX_EDICTS)
		maxentities = MAX_EDICTS;

	client->limit_reliable = NET_MAXMESSAGE;
	client->limit_unreliable = SV_IsLocalClient (client) ?
		NET_MAXMESSAGE : DATAGRAM_MTU;
	client->limit_entities = maxentities;
	client->limit_models = MAX_MODELS;
	client->limit_sounds = MAX_SOUNDS;

	if (client->message.data == client->msgbuf)
		client->message.maxsize = q_min ((int)sizeof(client->msgbuf),
			(int)client->limit_reliable);
	if (client->datagram.data == client->datagram_buf)
		client->datagram.maxsize = q_min ((int)sizeof(client->datagram_buf),
			(int)client->limit_unreliable);
}

static int SV_ClientUnreliableLimit (client_t *client)
{
	int maxsize;

	maxsize = client && client->limit_unreliable ?
		(int)client->limit_unreliable : DATAGRAM_MTU;
	if (!SV_IsLocalClient (client))
		maxsize = q_min (maxsize, SV_RemoteUnreliableLimit ());
	return CLAMP (512, maxsize, MAX_DATAGRAM);
}

static void SV_UpdateClientMSS (client_t *client)
{
	int maxsize;

	if (!client || !client->netconnection)
		return;

	maxsize = SV_ClientUnreliableLimit (client);
	NET_QSocketSetMSS (client->netconnection, maxsize);
	if (client->datagram.data == client->datagram_buf)
		client->datagram.maxsize = q_min ((int)sizeof(client->datagram_buf),
			maxsize);
}

static qboolean SV_AmmoCapacityValuesValid(const float values[4],
	const edict_t *ent)
{
	const float current[4] = {
		ent->v.ammo_shells, ent->v.ammo_nails,
		ent->v.ammo_rockets, ent->v.ammo_cells};
	int i;

	for (i = 0; i < 4; ++i)
		if (!isfinite(values[i]) || values[i] <= 0.0f ||
		    floorf(values[i]) != values[i] ||
		    values[i] > 1000000.0f || values[i] < current[i])
			return false;
	return true;
}

static qboolean SV_ReadAmmoCapacityFields(edict_t *ent, float values[4])
{
	static const char *names[4] = {
		"maxshells", "maxnails", "maxrockets", "maxcells"};
	int i;

	for (i = 0; i < 4; ++i)
	{
		ddef_t *def = ED_FindField(names[i]);
		eval_t *value;

		if (!def || (def->type & ~DEF_SAVEGLOBAL) != ev_float)
			return false;
		value = GetEdictFieldValue(ent, def->ofs);
		if (!value)
			return false;
		values[i] = value->_float;
	}
	return SV_AmmoCapacityValuesValid(values, ent);
}

static qboolean SV_ReadAmmoCapacityGlobals(edict_t *ent,
	const char *const names[4], qboolean require_saveglobal, float values[4])
{
	int i;

	for (i = 0; i < 4; ++i)
	{
		ddef_t *def = ED_FindGlobal(names[i]);

		if (!def || (def->type & ~DEF_SAVEGLOBAL) != ev_float ||
		    def->ofs >= qcvm->progs->numglobals ||
		    (require_saveglobal && !(def->type & DEF_SAVEGLOBAL)))
			return false;
		values[i] = G_FLOAT(def->ofs);
	}
	return SV_AmmoCapacityValuesValid(values, ent);
}

static void SV_WriteAmmoCapacityStats(edict_t *ent, int *statsi)
{
	static const char *const saveglobal_names[4] = {
		"ammo_shells_max", "ammo_nails_max", "ammo_rockets_max",
		"ammo_cells_max"};
	static const char *const max_ammo_names[4] = {
		"MAX_AMMO_SHELLS", "MAX_AMMO_NAILS", "MAX_AMMO_ROCKETS",
		"MAX_AMMO_CELLS"};
	static const char *const ammo_max_names[4] = {
		"AMMO_MAXSHELLS", "AMMO_MAXNAILS", "AMMO_MAXROCKETS",
		"AMMO_MAXCELLS"};
	static const int stats[4] = {
		STAT_VR_MAX_SHELLS, STAT_VR_MAX_NAILS,
		STAT_VR_MAX_ROCKETS, STAT_VR_MAX_CELLS};
	float values[4];
	int i;

	/* Prefer per-player fields, then coherent dynamic/static global families.
	 * Never combine names from different conventions. */
	if (!SV_ReadAmmoCapacityFields(ent, values) &&
	    !SV_ReadAmmoCapacityGlobals(ent, saveglobal_names, true, values) &&
	    !SV_ReadAmmoCapacityGlobals(ent, max_ammo_names, false, values) &&
	    !SV_ReadAmmoCapacityGlobals(ent, ammo_max_names, false, values))
		return;

	for (i = 0; i < 4; ++i)
		statsi[stats[i]] = (int)values[i];
}

void SV_CalcStats(client_t *client, int *statsi, float *statsf, const char **statss)
{
	size_t i;
	edict_t *ent = client->edict;
	const qboolean send_punchangle = !sv_nopunchangle.value;
	//FIXME: string stats!
	unsigned int items;
	eval_t *val = GetEdictFieldValue(ent, qcvm->extfields.items2);
	if (val)
		items = (unsigned int)ent->v.items | ((unsigned int)val->_float << 23);
	else
		items = (unsigned int)ent->v.items | ((unsigned int)pr_global_struct->serverflags << 28);

	memset(statsi, 0, sizeof(*statsi)*MAX_CL_STATS);
	memset(statsf, 0, sizeof(*statsf)*MAX_CL_STATS);
	memset((void*)statss, 0, sizeof(*statss)*MAX_CL_STATS);
	statsf[STAT_HEALTH] = ent->v.health;
//	statsf[STAT_FRAGS] = ent->v.frags;	//obsolete
	statsi[STAT_WEAPON] = SV_ModelIndex(PR_GetString(ent->v.weaponmodel));
	if ((unsigned int)statsi[STAT_WEAPON] >= client->limit_models)
		statsi[STAT_WEAPON] = 0;
	statsf[STAT_AMMO] = ent->v.currentammo;
	statsf[STAT_ARMOR] = ent->v.armorvalue;
	statsf[STAT_WEAPONFRAME] = ent->v.weaponframe;
	statsf[STAT_SHELLS] = ent->v.ammo_shells;
	statsf[STAT_NAILS] = ent->v.ammo_nails;
	statsf[STAT_ROCKETS] = ent->v.ammo_rockets;
	statsf[STAT_CELLS] = ent->v.ammo_cells;
	statsf[STAT_ACTIVEWEAPON] = ent->v.weapon;	//sent in a way that does NOT depend upon the current mod...
	if ((val = GetEdictFieldValue(ent, qcvm->extfields.viewzoom)) && val->_float)
	{
		statsf[STAT_VIEWZOOM] = val->_float * 255;
		if (statsf[STAT_VIEWZOOM] < 1)
			statsf[STAT_VIEWZOOM] = 1;
	}
	if (client->protocol_pext2 & PEXT2_PREDINFO)
	{
		statsi[STAT_ITEMS] = (int)items;
		statsf[STAT_VIEWHEIGHT] = ent->v.view_ofs[2];
		statsf[STAT_IDEALPITCH] = ent->v.idealpitch;
		if (send_punchangle)
		{
			statsf[STAT_PUNCHANGLE_X] = ent->v.punchangle[0];
			statsf[STAT_PUNCHANGLE_Y] = ent->v.punchangle[1];
			statsf[STAT_PUNCHANGLE_Z] = ent->v.punchangle[2];
		}
		PMSV_SetMoveStats(ent, statsf, statsi);
	}
	val = GetEdictFieldValueByName(ent, "weapons");
	if (val)
		statsi[STAT_VR_WEAPONS] = (int)val->_float;
	val = GetEdictFieldValueByName(ent, "items2");
	if (val)
		statsi[STAT_VR_ITEMS2] = (int)val->_float;
	val = GetEdictFieldValueByName(ent, "moditems");
	if (val)
		statsi[STAT_VR_MODITEMS] = (int)val->_float;
	else
	{
		val = GetEdictFieldValueByName(ent, "items_dwell");
		if (val)
			statsi[STAT_VR_MODITEMS] = (int)val->_float;
	}
	val = GetEdictFieldValueByName(ent, "weapon2");
	if (val)
		statsi[STAT_VR_WEAPON2] = (int)val->_float;
	val = GetEdictFieldValueByName(ent, "weapons2");
	if (val)
		statsi[STAT_VR_WEAPONS2] = (int)val->_float;

	SV_WriteAmmoCapacityStats(ent, statsi);
	if (coop.value && SV_CoopFeatureEnabled(&sv_coop_noplayerclip, true))
		statsi[STAT_VR_COOP_POLICY] |= VR_COOP_POLICY_NO_PLAYER_CLIP;

	for (i = 0; i < sv.numcustomstats; i++)
	{
		eval_t *eval = sv.customstats[i].ptr;
		if (!eval)
			eval = GetEdictFieldValue(ent, sv.customstats[i].fld);

		switch(sv.customstats[i].type)
		{
		case ev_ext_integer:
			statsi[sv.customstats[i].idx] = eval->_int;
			break;
		case ev_entity:
			statsi[sv.customstats[i].idx] = NUM_FOR_EDICT(PROG_TO_EDICT(eval->edict));
			break;
		case ev_float:
			statsf[sv.customstats[i].idx] = eval->_float;
			break;
		case ev_vector:
			statsf[sv.customstats[i].idx+0] = eval->vector[0];
			statsf[sv.customstats[i].idx+1] = eval->vector[1];
			statsf[sv.customstats[i].idx+2] = eval->vector[2];
			break;
		case ev_string:		//not supported in this build... send with svcfte_updatestatstring on change, which is annoying.
			statss[sv.customstats[i].idx] = PR_GetString(eval->string);
			break;
		case ev_void:		//nothing...
		case ev_field:		//panic! everyone panic!
		case ev_function:	//doesn't make much sense
		case ev_pointer:	//doesn't make sense
		default:
			break;
		}
	}
}

/*
===============
SV_Protocol_f
===============
*/
void SV_Protocol_f (void)
{
	int i;

	switch (Cmd_Argc())
	{
	case 1:
		Con_Printf ("\"sv_protocol\" is \"%i\"\n", sv_protocol);
		break;
	case 2:
		i = atoi(Cmd_Argv(1));
		if (i != PROTOCOL_RMQ)
		{
			Con_Printf ("sv_protocol is fixed at %i (RMQ) in this build.\n",
				PROTOCOL_RMQ);
			break;
		}
		sv_protocol = PROTOCOL_RMQ;
		break;
	default:
		Con_SafePrintf ("usage: sv_protocol <protocol>\n");
		break;
	}
}

/*
===============
SV_NetDiag_f
===============
*/
static qboolean SV_UsesReplacementDeltas (const client_t *client);
static int SV_ReplacementLastSentSequence (const client_t *client);
static int SV_ReplacementAckLag (const client_t *client, int sequence);

static void SV_NetDiag_f (void)
{
	int i;

	Con_Printf ("client netdiag: moves packets=%d cmds=%d generated_msec=%llu last_cmds=%d ack=%d moveacks=%d staleacks=%d\n",
		cl.net_move_packets_sent, cl.net_move_cmds_sent,
		cl.net_move_msec_generated, cl.net_move_last_packet_cmds, cl.ackedmovemessages,
		cl.net_move_acks, cl.net_move_stale_acks);
	Con_Printf ("client netdiag: snapshots seq=%d packets=%d drops=%d acks_sent=%d ack_overflows=%d pred=%d movetype=%d flags=%d vel=(%.1f %.1f %.1f)\n",
		cl.net_snapshot_sequence, cl.net_snapshot_packets,
		cl.net_snapshot_drops, cl.net_snapshot_acks_sent,
		cl.net_snapshot_ack_queue_overflows,
		cl.viewentity > 0 && cl.entities[cl.viewentity].netstate.pmovetype != 0,
		cl.viewentity > 0 ? cl.entities[cl.viewentity].netstate.pmovetype : 0,
		cl.viewentity > 0 ? cl.entities[cl.viewentity].netstate.pmovetype & 0xc0 : 0,
		cl.viewentity > 0 ? cl.entities[cl.viewentity].netstate.velocity[0] * (1.0f / 8.0f) : 0,
		cl.viewentity > 0 ? cl.entities[cl.viewentity].netstate.velocity[1] * (1.0f / 8.0f) : 0,
		cl.viewentity > 0 ? cl.entities[cl.viewentity].netstate.velocity[2] * (1.0f / 8.0f) : 0);
	Con_Printf ("client netdiag: prediction errors=%d last=%.2f max=%.2f\n",
		cl.net_prediction_errors, cl.net_prediction_error_last,
		cl.net_prediction_error_max);
	Con_Printf ("client netdiag: movement authority=%d prediction_allowed=%d mode_epoch=%u discontinuity_epoch=%u reason=%u\n",
		cl.move_ack_authority, cl.move_ack_prediction_allowed ? 1 : 0,
		cl.move_ack_mode_epoch, cl.move_ack_discontinuity_epoch,
		cl.move_ack_discontinuity_reason);

	if (!sv.active)
		return;

	for (i = 0; i < svs.maxclients; i++)
	{
		client_t *client = &svs.clients[i];
		qboolean replacement;
		int sequence;
		int ack;
		int acklag;

		if (!client->active)
			continue;
		replacement = SV_UsesReplacementDeltas (client);
		sequence = replacement ? SV_ReplacementLastSentSequence (client) :
			client->net_snapshot_sequence;
		ack = replacement ? (client->lastacksequence >= 0 ?
			client->lastacksequence : -1) : client->net_snapshot_ack;
		acklag = replacement ? SV_ReplacementAckLag (client, sequence) :
			client->net_snapshot_ack_lag_max;
		Con_Printf ("server netdiag: #%d %s moves packets=%d cmds=%d accepted=%d simulated=%d stale=%d last=%d pending=%d bundle=%d maxbundle=%d gap=%d lastdt=%.3f\n",
			i + 1, client->name, client->net_move_packets_received,
			client->net_move_cmds_received, client->net_move_cmds_accepted,
			client->net_move_cmds_simulated, client->net_move_cmds_stale,
			client->lastmovemessage, client->move_pending ? 1 : 0,
			client->net_move_last_bundle,
			client->net_move_bundle_max, client->net_move_last_gap,
			client->net_move_last_sim_seconds);
		Con_Printf ("server netdiag: #%d movement authority=%d prediction_allowed=%d quarantined=%d mode_epoch=%u discontinuity_epoch=%u reason=%u queue=%u accepted_seq=%d encoded_msec=%u servertime=%.3f\n",
			i + 1, client->move_authority,
			client->move_prediction_allowed ? 1 : 0,
			client->move_client_quarantined ? 1 : 0,
			client->move_mode_epoch, client->move_discontinuity_epoch,
			client->move_discontinuity_reason, client->move_queue_count,
			client->lastacceptedmovemessage, client->net_move_last_msec,
			client->net_move_last_servertime);
		Con_Printf ("server netdiag: #%d movement accepted_msec=%llu simulated_msec=%llu queue_overflows=%d roomscale_outliers=%d qc_pre=%d qc_post=%d qc_cmd=%d touches=%d dynamic_contacts=%d\n",
			i + 1, client->net_move_msec_accepted,
			client->net_move_msec_simulated, client->net_move_queue_overflows,
			client->net_move_roomscale_outliers,
			client->net_move_qc_prethinks, client->net_move_qc_postthinks,
			client->net_move_qc_commands, client->net_move_touches,
			client->net_move_dynamic_contacts);
		Con_Printf ("server netdiag: #%d snapshots replacement=%d seq=%d ack=%d packets=%d split_packets=%d last_packets=%d max_packets=%d last_bytes=%d max_bytes=%d acklag=%d loss=%d clipped_ents=%d\n",
			i + 1, replacement ? 1 : 0, sequence, ack,
			client->net_snapshot_packets_sent,
			client->net_snapshot_split_packets, client->net_snapshot_last_packets,
			client->net_snapshot_max_packets, client->net_snapshot_last_bytes,
			client->net_snapshot_max_bytes, acklag,
			client->netconnection ? NET_QSocketGetPacketLoss(client->netconnection) : 0,
			client->net_snapshot_unsent_entities);
	}
}

/*
===============
SV_Init
===============
*/
void SV_Init (void)
{
	int		i;
	const char	*p;
	extern	cvar_t	sv_maxvelocity;
	extern	cvar_t	sv_gravity;
	extern	cvar_t	sv_nostep;
	extern	cvar_t	vr_movement_instant_stop;
	extern	cvar_t	vr_movement_defaults_version;
	extern	cvar_t	sv_freezenonclients;
	extern	cvar_t	sv_friction;
	extern	cvar_t	sv_edgefriction;
	extern	cvar_t	sv_stopspeed;
	extern	cvar_t	sv_maxspeed;
	extern	cvar_t	sv_accelerate;
	extern	cvar_t	sv_idealpitchscale;
	extern	cvar_t	sv_pmove_legacy_preserve_qc_velocity;
	extern	cvar_t	sv_nqplayerphysics;
	extern	cvar_t	sv_trustedmovement;
	extern	cvar_t	sv_aim;
	extern	cvar_t	sv_altnoclip; //johnfitz
	extern	cvar_t	sv_gameplayfix_random;
	extern	cvar_t	sv_gameplayfix_spawnbeforethinks;
	extern	cvar_t	sv_gameplayfix_elevators;
	extern	cvar_t	sv_inputtimeout;

	Cvar_RegisterVariable (&sv_maxvelocity);
	Cvar_RegisterVariable (&sv_gravity);
	Cvar_RegisterVariable (&sv_friction);
	Cvar_SetCallback (&sv_gravity, Host_Callback_Notify);
	Cvar_SetCallback (&sv_friction, Host_Callback_Notify);
	Cvar_RegisterVariable (&sv_edgefriction);
	Cvar_RegisterVariable (&sv_stopspeed);
	Cvar_RegisterVariable (&sv_maxspeed);
	Cvar_SetCallback (&sv_maxspeed, Host_Callback_Notify);
	Cvar_RegisterVariable (&sv_accelerate);
	Cvar_RegisterVariable (&sv_idealpitchscale);
	Cvar_RegisterVariable (&sv_pmove_legacy_preserve_qc_velocity);
	Cvar_RegisterVariable (&sv_nqplayerphysics);
	Cvar_RegisterVariable (&sv_trustedmovement);
	Cvar_RegisterVariable (&sv_pmove_mode);
	Cvar_RegisterVariable (&sv_aim);
	Cvar_RegisterVariable (&sv_nostep);
	Cvar_RegisterVariable (&sv_freezenonclients);
	Cvar_RegisterVariable (&pr_checkextension);
	Cvar_RegisterVariable (&sv_altnoclip); //johnfitz
	Cvar_RegisterVariable (&sv_gameplayfix_spawnbeforethinks);
	Cvar_RegisterVariable (&sv_gameplayfix_elevators);
	Cvar_RegisterVariable (&sv_gameplayfix_random);
	Cvar_RegisterVariable (&sv_inputtimeout);
	Cvar_RegisterVariable (&sv_maxpacketsize);
	Cvar_RegisterVariable (&sv_netdiag_interval);
	Cvar_RegisterVariable (&sv_replacement_maxpackets);
	Cvar_RegisterVariable (&sv_predict_nqmovement);
	Cvar_RegisterVariable (&sv_nopunchangle);
	Cvar_RegisterVariable (&sv_coop_classic);
	Cvar_RegisterVariable (&sv_coop_notelefrag);
	Cvar_RegisterVariable (&sv_coop_player_teleport_fallback);
	Cvar_RegisterVariable (&sv_coop_shared_pickups);
	Cvar_RegisterVariable (&sv_coop_weapon_targetfix);
	Cvar_RegisterVariable (&sv_coop_pickup_targetlog);
	Cvar_RegisterVariable (&sv_coop_pickup_targetfix);
	Cvar_RegisterVariable (&sv_coop_pickup_targetfix_classes);
	Cvar_RegisterVariable (&sv_coop_ammo_respawn);
	Cvar_RegisterVariable (&sv_coop_ammo_respawn_time);
	Cvar_RegisterVariable (&sv_coop_progression_item_respawn);
	Cvar_RegisterVariable (&sv_coop_progression_item_respawn_time);
	Cvar_RegisterVariable (&sv_coop_progression_item_respawn_classes);
	Cvar_RegisterVariable (&sv_coop_revive);
	Cvar_RegisterVariable (&sv_coop_revive_health);
	Cvar_RegisterVariable (&sv_coop_revive_range);
	Cvar_RegisterVariable (&sv_coop_respawn_near_player);
	Cvar_RegisterVariable (&sv_coop_respawn_delay);
	Cvar_RegisterVariable (&sv_coop_respawn_keep_weapons_ammo);
	Cvar_RegisterVariable (&sv_coop_autosave);
	Cvar_RegisterVariable (&sv_coop_autosave_slots);
	Cvar_RegisterVariable (&sv_coop_autosave_min_interval);
	Cvar_RegisterVariable (&sv_coop_autosave_kill_interval);
	Cvar_RegisterVariable (&sv_coop_predictmove);
	Cvar_RegisterVariable (&sv_triggerdebug);
	Cvar_RegisterVariable (&sv_vr_jump_velocity);
	Cvar_RegisterVariable (&sv_skyroom_pvs);
	PM_Register ();
	Cvar_SetCallback (&sv_coop_ammo_respawn, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_classic, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_notelefrag, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_player_teleport_fallback, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_shared_pickups, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_ammo_respawn_time, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_progression_item_respawn, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_progression_item_respawn_time, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_progression_item_respawn_classes, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_revive, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_revive_health, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_revive_range, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_respawn_near_player, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_respawn_delay, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_respawn_keep_weapons_ammo, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_autosave, Host_Callback_Notify);
	Cvar_SetCallback (&sv_vr_jump_velocity, Host_Callback_Notify);
	Cvar_RegisterVariable (&vr_movement_instant_stop);
	Cvar_RegisterVariable (&vr_movement_defaults_version);
	Cmd_AddCommand ("vr_migrate_movement_defaults", VR_MigrateMovementDefaults_f);
	Cvar_RegisterVariable (&sv_netsort); // ironwail-style entity priority sorting

	Cmd_AddCommand ("netdiag", SV_NetDiag_f);
	Cmd_AddCommand ("sv_protocol", &SV_Protocol_f); //johnfitz

	for (i=0 ; i<MAX_MODELS ; i++)
		sprintf (localmodels[i], "*%i", i);

	i = COM_CheckParm ("-protocol");
	if (i && i < com_argc - 1)
	{
		if (atoi (com_argv[i + 1]) != PROTOCOL_RMQ)
			Con_Printf ("Ignoring -protocol %s; this build requires %i (RMQ).\n",
				com_argv[i + 1], PROTOCOL_RMQ);
		sv_protocol = PROTOCOL_RMQ;
	}
	switch (sv_protocol)
	{
	case PROTOCOL_RMQ:
		p = "RMQ";
		break;
	default:
		Con_Printf ("Bad protocol version request %i; forcing %i (RMQ).\n",
			sv_protocol, PROTOCOL_RMQ);
		sv_protocol = PROTOCOL_RMQ;
		p = "RMQ";
		break;
	}
	Sys_Printf ("Server using protocol %i (%s)\n", sv_protocol, p);
}

void SV_SetupSkyRoom (const char *value)
{
	char	*end;
	int	i;
	float	vals[4] = {0, 0, 0, 0};

	for (i = 0; i < 4; i++)
	{
		while (*value == ' ' || *value == '\t')
			value++;
		if (!*value)
			break;
		vals[i] = strtod (value, &end);
		if (end == value)
			break;
		value = end;
	}

	if (i < 3)
		return;

	sv.skyroom_pos_known = true;
	sv.skyroom_pos[0] = vals[0];
	sv.skyroom_pos[1] = vals[1];
	sv.skyroom_pos[2] = vals[2];
	sv.skyroom_pos[3] = (i >= 4) ? vals[3] : 0;
}

/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/

/*
==================
SV_StartParticle

Make sure the event gets sent to all clients
==================
*/
void SV_StartParticle (vec3_t org, vec3_t dir, int color, int count)
{
	int		i, v;

	if (sv.datagram.cursize > MAX_DATAGRAM-18)
		return;
	MSG_WriteByte (&sv.datagram, svc_particle);
	MSG_WriteCoord (&sv.datagram, org[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[2], sv.protocolflags);
	for (i=0 ; i<3 ; i++)
	{
		v = dir[i]*16;
		if (v > 127)
			v = 127;
		else if (v < -128)
			v = -128;
		MSG_WriteChar (&sv.datagram, v);
	}
	MSG_WriteByte (&sv.datagram, count);
	MSG_WriteByte (&sv.datagram, color);
}

static qboolean SV_WriteSoundToMessage (sizebuf_t *msg, edict_t *entity,
	int ent, int channel, int sound_num, int field_mask, int volume,
	float attenuation)
{
	int i, wire_channel;

	if (msg->overflowed)
		return false;
	if (msg->cursize > msg->maxsize - 21)
		return false;

	wire_channel = channel & 7;
	MSG_WriteByte (msg, svc_sound);
	MSG_WriteByte (msg, field_mask);
	if (field_mask & SND_VOLUME)
		MSG_WriteByte (msg, volume);
	if (field_mask & SND_ATTENUATION)
		MSG_WriteByte (msg, attenuation * 64);
	if (field_mask & SND_LARGEENTITY)
	{
		MSG_WriteShort (msg, ent);
		MSG_WriteByte (msg, wire_channel);
	}
	else
		MSG_WriteShort (msg, (ent << 3) | wire_channel);
	if (field_mask & SND_LARGESOUND)
		MSG_WriteShort (msg, sound_num);
	else
		MSG_WriteByte (msg, sound_num);
	for (i = 0; i < 3; i++)
		MSG_WriteCoord (msg, entity->v.origin[i] +
			0.5 * (entity->v.mins[i] + entity->v.maxs[i]), sv.protocolflags);

	return !msg->overflowed;
}

/*
==================
SV_StartSound

Each entity can have eight independant sound sources, like voice,
weapon, feet, etc.

Channel 0 is an auto-allocate channel, the others override anything
already running on that entity/channel pair.

An attenuation of 0 will play full volume everywhere in the level.
Larger attenuations will drop off.  (max 4 attenuation)

==================
*/
void SV_StartSound (edict_t *entity, int channel, const char *sample, int volume, float attenuation)
{
	int			sound_num, ent;
	int			i, field_mask;
	qboolean	unicast;
	client_t	*client;

	if (volume < 0)
		Host_Error ("SV_StartSound: volume = %i", volume);
	if (volume > 255)
	{
		Con_DPrintf ("SV_StartSound: volume = %i\n", volume);
		volume = 255;
	}

	if (attenuation < 0 || attenuation > 4)
		Host_Error ("SV_StartSound: attenuation = %f", attenuation);

	if (channel < 0 || channel > 255)
		Host_Error ("SV_StartSound: channel = %i", channel);
	if (channel > 15)
		Con_DPrintf ("SV_StartSound: unsupported channel flags %i\n", channel);

// find precache number for sound
	for (sound_num = 1; sound_num < MAX_SOUNDS && sv.sound_precache[sound_num]; sound_num++)
	{
		if (!strcmp(sample, sv.sound_precache[sound_num]))
			break;
	}

	if (sound_num == MAX_SOUNDS || !sv.sound_precache[sound_num])
	{
		Con_Printf ("SV_StartSound: %s not precached\n", sample);
		return;
	}

	ent = NUM_FOR_EDICT(entity);
	unicast = (channel & 8) && ent >= 1 && ent <= svs.maxclients;

	field_mask = 0;
	if (volume != DEFAULT_SOUND_PACKET_VOLUME)
		field_mask |= SND_VOLUME;
	if (attenuation != DEFAULT_SOUND_PACKET_ATTENUATION)
		field_mask |= SND_ATTENUATION;

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (ent >= 8192)
	{
		if (sv.protocol == PROTOCOL_NETQUAKE)
			return; //don't send any info protocol can't support
		field_mask |= SND_LARGEENTITY;
	}
	if (sound_num >= 256)
	{
		if (sv.protocol == PROTOCOL_NETQUAKE)
			return; //don't send any info protocol can't support
		field_mask |= SND_LARGESOUND;
	}
	//johnfitz

	if (unicast)
	{
		client = &svs.clients[ent - 1];
		if (!client->active || !client->spawned)
			return;
		if ((unsigned int)sound_num >= client->limit_sounds)
			return;
		SV_WriteSoundToMessage (&client->datagram, entity, ent, channel,
			sound_num, field_mask, volume, attenuation);
		return;
	}

	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		if (!client->active || !client->spawned)
			continue;
		if ((unsigned int)sound_num >= client->limit_sounds)
			continue;
		SV_WriteSoundToMessage (&client->datagram, entity, ent, channel,
			sound_num, field_mask, volume, attenuation);
	}
}

/*
==================
SV_LocalSound - for 2021 rerelease
==================
*/
void SV_LocalSound (client_t *client, const char *sample)
{
	int	sound_num, field_mask;

	for (sound_num = 1; sound_num < MAX_SOUNDS && sv.sound_precache[sound_num]; sound_num++)
	{
		if (!strcmp(sample, sv.sound_precache[sound_num]))
			break;
	}
	if (sound_num == MAX_SOUNDS || !sv.sound_precache[sound_num])
	{
		Con_Printf ("SV_LocalSound: %s not precached\n", sample);
		return;
	}

	field_mask = 0;
	if (sound_num >= 256)
	{
		if (sv.protocol == PROTOCOL_NETQUAKE)
			return;
		field_mask = SND_LARGESOUND;
	}

	if (client->message.cursize > client->message.maxsize-4)
		return;
	if ((unsigned int)sound_num >= client->limit_sounds)
		return;

	MSG_WriteByte (&client->message, svc_localsound);
	MSG_WriteByte (&client->message, field_mask);
	if (field_mask & SND_LARGESOUND)
		MSG_WriteShort (&client->message, sound_num);
	else
		MSG_WriteByte (&client->message, sound_num);
}

/*
==============================================================================

CLIENT SPAWNING

==============================================================================
*/

/*
================
SV_SendServerinfo

Sends the first message from the server to a connected client.
This will be sent on the initial connection and upon each server load.
================
*/
void SV_SendServerinfo (client_t *client)
{
	const char		**s;
	char			message[2048];
	int				i; //johnfitz

	client->knowntoqc = false;
	client->protocol_pext1 = 0;
	client->protocol_pext2 = PEXT2_REQUIRED_LATEST;
	client->pextknown = true;
	SV_SetClientLimits (client);
	SVFTE_SetupFrames (client);
	SV_UpdateClientMSS (client);

	MSG_WriteByte (&client->message, svc_print);
	sprintf (message, "%c\nFITZQUAKE %1.2f SERVER (%i CRC)\n", 2, FITZQUAKE_VERSION, qcvm->crc); //johnfitz -- include fitzquake version
	MSG_WriteString (&client->message,message);

	MSG_WriteByte (&client->message, svc_serverinfo);
	if (client->protocol_pext1)
	{
		MSG_WriteLong (&client->message, PROTOCOL_FTE_PEXT1);
		MSG_WriteLong (&client->message, client->protocol_pext1);
	}
	MSG_WriteLong (&client->message, PROTOCOL_FTE_PEXT2);
	MSG_WriteLong (&client->message, client->protocol_pext2);
	MSG_WriteLong (&client->message, sv.protocol); //johnfitz -- sv.protocol instead of PROTOCOL_VERSION
	
	// Latest-code servers always use RMQ, so protocol flags are mandatory.
	MSG_WriteLong (&client->message, sv.protocolflags);

	MSG_WriteString (&client->message, COM_GetGameNames(false));
	
	MSG_WriteByte (&client->message, svs.maxclients);

	if (!coop.value && deathmatch.value)
		MSG_WriteByte (&client->message, GAME_DEATHMATCH);
	else
		MSG_WriteByte (&client->message, GAME_COOP);

	MSG_WriteString (&client->message, PR_GetString(qcvm->edicts->v.message));

	//johnfitz -- only send the first 256 model and sound precaches if protocol is 15
	for (i = 1, s = sv.model_precache+1; *s && (unsigned int)i < client->limit_models; s++,i++)
		MSG_WriteString (&client->message, *s);
	MSG_WriteByte (&client->message, 0);
	client->signon_models = i;

	// Latest clients accept staged sound precaches. Keep these out of the
	// initial serverinfo message so large mods do not bloat signon packet 1.
	MSG_WriteByte (&client->message, 0);
	client->signon_sounds = 1;

// send music
	MSG_WriteByte (&client->message, svc_cdtrack);
	MSG_WriteByte (&client->message, qcvm->edicts->v.sounds);
	MSG_WriteByte (&client->message, qcvm->edicts->v.sounds);

	// set view
	MSG_WriteByte (&client->message, svc_setview);
	MSG_WriteShort (&client->message, NUM_FOR_EDICT(client->edict));

	/* Comment-prefixed stufftext is a safe capability handshake: older clients
	 * ignore the unknown command, while new clients may send body-relative VR
	 * muzzle coordinates without breaking older servers. */
	MSG_WriteByte (&client->message, svc_stufftext);
	MSG_WriteString (&client->message, "//vr_relative_muzzle 1\n");
	/* Comment-prefixed negotiation keeps pre-VRIK clients on their normal
	 * animation path and avoids spending a PEXT2 compatibility bit. */
	MSG_WriteByte (&client->message, svc_stufftext);
	MSG_WriteString (&client->message, "//vrik_protocol 1\n");

	MSG_WriteByte (&client->message, svc_signonnum);
	MSG_WriteByte (&client->message, 1);

	client->sendsignon = PRESPAWN_FLUSH;
	client->spawned = false;		// need prespawn, spawn, etc
}

/*
================
SV_ConnectClient

Initializes a client_t for a new net connection.  This will only be called
once for a player each game, not once for each level change.
================
*/
void SV_ConnectClient (int clientnum)
{
	edict_t			*ent;
	client_t		*client;
	int				edictnum;
	struct qsocket_s *netconnection;
	int				i;

	client = svs.clients + clientnum;
	if (clientnum >= 0 && clientnum < MAX_SCOREBOARD)
		svs.coop_initial_spawn_client[clientnum] = false;

	Con_DPrintf ("Client %s connected\n", NET_QSocketGetAddressString(client->netconnection));

	edictnum = clientnum+1;

	ent = EDICT_NUM(edictnum);

// set up the client_t
	netconnection = client->netconnection;

	SVFTE_DestroyFrames (client);
	memset (client, 0, sizeof(*client));
	client->netconnection = netconnection;
	SV_ResetClientMoveState (client);

	strcpy (client->name, "unconnected");
	client->active = true;
	client->spawned = false;
	client->edict = ent;
	client->message.data = client->msgbuf;
	client->message.maxsize = sizeof(client->msgbuf);
	client->message.allowoverflow = true;		// we can catch it
	client->datagram.data = client->datagram_buf;
	client->datagram.maxsize = q_min ((int)sizeof(client->datagram_buf),
		SV_ClientUnreliableLimit (client));
	client->datagram.cursize = 0;
	client->datagram.allowoverflow = true;

	/* A connection has no trustworthy player name yet.  Always initialize
	   defaults here; Host_Spawn_f applies a saved snapshot only after resolving
	   the signon name (or the unambiguous single-player slot). */
	PR_ExecuteProgram (pr_global_struct->SetNewParms);
	for (i=0 ; i<NUM_SPAWN_PARMS ; i++)
		client->spawn_parms[i] = (&pr_global_struct->parm1)[i];
	/* MG3 campaign upgrades are shared co-op progression.  Merge the durable
	 * union before signon so reconnecting and late-joining clients decode the
	 * same health/ammo caps in PutClientInServer. */
	SV_MG3UpgradeApplySpawnParms(client->spawn_parms);

	SV_SendServerinfo (client);
}


/*
===================
SV_CheckForNewClients

===================
*/
void SV_CheckForNewClients (void)
{
	struct qsocket_s	*ret;
	int				i;

//
// check for new connections
//
	while (1)
	{
		ret = NET_CheckNewConnections ();
		if (!ret)
			break;

	//
	// init a new client structure
	//
		for (i=0 ; i<svs.maxclients ; i++)
			if (!svs.clients[i].active)
				break;
		if (i == svs.maxclients)
			Sys_Error ("Host_CheckForNewClients: no free clients");

		svs.clients[i].netconnection = ret;
		SV_ConnectClient (i);

		net_activeconnections++;
	}
}


/*
===============================================================================

FRAME UPDATES

===============================================================================
*/

/*
==================
SV_ClearDatagram

==================
*/
void SV_ClearDatagram (void)
{
	SZ_Clear (&sv.datagram);
}

/*
=============================================================================

The PVS must include a small area around the client to allow head bobbing
or other small motion on the client side.  Otherwise, a bob might cause an
entity that should be visible to not show up, especially when the bob
crosses a waterline.

=============================================================================
*/

static int	fatbytes;
static byte	*fatpvs;
static int	fatpvs_capacity;

void SV_AddToFatPVS (vec3_t org, mnode_t *node, qmodel_t *worldmodel) //johnfitz -- added worldmodel as a parameter
{
	int		i;
	byte	*pvs;
	mplane_t	*plane;
	float	d;

	while (1)
	{
	// if this is a leaf, accumulate the pvs bits
		if (node->contents < 0)
		{
			if (node->contents != CONTENTS_SOLID)
			{
				pvs = Mod_LeafPVS ( (mleaf_t *)node, worldmodel); //johnfitz -- worldmodel as a parameter
				for (i=0 ; i<fatbytes ; i++)
					fatpvs[i] |= pvs[i];
			}
			return;
		}

		plane = node->plane;
		d = DotProduct (org, plane->normal) - plane->dist;
		if (d > 8)
			node = node->children[0];
		else if (d < -8)
			node = node->children[1];
		else
		{	// go down both
			SV_AddToFatPVS (org, node->children[0], worldmodel); //johnfitz -- worldmodel as a parameter
			node = node->children[1];
		}
	}
}

/*
=============
SV_FatPVS

Calculates a PVS that is the inclusive or of all leafs within 8 pixels of the
given point.
=============
*/
byte *SV_FatPVS (vec3_t org, qmodel_t *worldmodel) //johnfitz -- added worldmodel as a parameter
{
	fatbytes = (worldmodel->numleafs+7)>>3; // ericw -- was +31, assumed to be a bug/typo
	if (fatpvs == NULL || fatbytes > fatpvs_capacity)
	{
		fatpvs_capacity = fatbytes;
		fatpvs = (byte *) realloc (fatpvs, fatpvs_capacity);
		if (!fatpvs)
			Sys_Error ("SV_FatPVS: realloc() failed on %d bytes", fatpvs_capacity);
	}
	
	Q_memset (fatpvs, 0, fatbytes);
	SV_AddToFatPVS (org, worldmodel->nodes, worldmodel); //johnfitz -- worldmodel as a parameter
	return fatpvs;
}

/*
=============
SV_EdictInPVS
=============
*/
qboolean SV_EdictInPVS (edict_t *test, byte *pvs)
{
	int i;
	for (i = 0 ; i < test->num_leafs ; i++)
		if (pvs[test->leafnums[i] >> 3] & (1 << (test->leafnums[i] & 7)))
			return true;
	return false;
}

#define UF_REMOVE UF_16BIT
#define UF_MOVETYPE UF_EFFECTS2
#define UF_RESET2 UF_EXTEND1

static struct entity_num_state_s *snapshot_entstate;
static size_t snapshot_numents;
static size_t snapshot_maxents;

static void SVFTE_AppendSnapshotEntity (unsigned int num, const entity_state_t *state)
{
	if (snapshot_numents == snapshot_maxents)
	{
		size_t newmax = snapshot_maxents ? snapshot_maxents * 2 : 256;
		void *newents = realloc (snapshot_entstate, newmax * sizeof(*snapshot_entstate));
		if (!newents)
			Sys_Error ("SVFTE_AppendSnapshotEntity: realloc failed");
		snapshot_entstate = (struct entity_num_state_s *)newents;
		snapshot_maxents = newmax;
	}
	snapshot_entstate[snapshot_numents].num = num;
	snapshot_entstate[snapshot_numents].state = *state;
	snapshot_numents++;
}

static unsigned int SVFTE_DeltaPredCalcBits (entity_state_t *from, entity_state_t *to)
{
	unsigned int bits = 0;

	if (from && from->pmovetype != to->pmovetype)
		bits |= UFP_MOVETYPE;
	if (to->velocity[0] || to->velocity[1])
		bits |= UFP_VELOCITYXY;
	if (to->velocity[2])
		bits |= UFP_VELOCITYZ;
	return bits;
}

static unsigned int MSGFTE_DeltaCalcBits (entity_state_t *from, entity_state_t *to)
{
	unsigned int bits = 0;

	if (from->pmovetype != to->pmovetype)
		bits |= UF_PREDINFO | UF_MOVETYPE;
	if (SVFTE_DeltaPredCalcBits (from, to))
		bits |= UF_PREDINFO;
	if ((bits & UF_PREDINFO) && (from->velocity[0] || from->velocity[1] || from->velocity[2]))
		bits |= UF_ORIGINXY | UF_ORIGINZ;

	if (to->origin[0] != from->origin[0] || to->origin[1] != from->origin[1])
		bits |= UF_ORIGINXY;
	if (to->origin[2] != from->origin[2])
		bits |= UF_ORIGINZ;
	if (to->angles[0] != from->angles[0] || to->angles[2] != from->angles[2])
		bits |= UF_ANGLESXZ;
	if (to->angles[1] != from->angles[1])
		bits |= UF_ANGLESY;
	if (to->modelindex != from->modelindex)
		bits |= UF_MODEL;
	if (to->frame != from->frame)
		bits |= UF_FRAME;
	if (to->skin != from->skin)
		bits |= UF_SKIN;
	if (to->colormap != from->colormap)
		bits |= UF_COLORMAP;
	if (to->effects != from->effects)
		bits |= UF_EFFECTS;
	if (to->eflags != from->eflags)
		bits |= UF_FLAGS;
	if (to->solidsize != from->solidsize)
		bits |= UF_SOLID;
	if (to->scale != from->scale)
		bits |= UF_SCALE;
	if (to->alpha != from->alpha)
		bits |= UF_ALPHA;
	if (to->colormod[0] != from->colormod[0] ||
		to->colormod[1] != from->colormod[1] ||
		to->colormod[2] != from->colormod[2])
		bits |= UF_COLORMOD;
	if (to->tagentity != from->tagentity || to->tagindex != from->tagindex)
		bits |= UF_TAGINFO;
	if (to->traileffectnum != from->traileffectnum ||
		to->emiteffectnum != from->emiteffectnum)
		bits |= UF_TRAILEFFECT;

	return bits;
}

static void MSG_WriteSize16 (sizebuf_t *sb, int sz)
{
	if (sz == ES_SOLID_BSP)
		MSG_WriteShort (sb, ES_SOLID_BSP);
	else if (sz)
	{
		int x = sz & 255;
		int zd = (sz >> 8) & 255;
		int zu = ((sz >> 16) & 65535) - 32768;
		MSG_WriteShort (sb, ((x >> 3) << 0) |
			((zd >> 3) << 5) | (((zu + 32) >> 3) << 10));
	}
	else
		MSG_WriteShort (sb, 0);
}

static void MSGFTE_WriteEntityUpdate (unsigned int bits, entity_state_t *state,
	sizebuf_t *msg, unsigned int pext2, unsigned int protocolflags)
{
	unsigned int predbits = 0;

	pext2 |= PEXT2_REQUIRED_LATEST;

	if (bits & UF_MOVETYPE)
	{
		bits &= ~UF_MOVETYPE;
		predbits |= UFP_MOVETYPE;
	}
	if ((bits & UF_MODEL) && state->modelindex > 255)
		bits |= UF_16BIT;
	if ((bits & UF_FRAME) && state->frame > 255)
		bits |= UF_16BIT;
	if (bits & UF_EFFECTS)
	{
		if (state->effects & 0xffff0000)
			bits |= UF_EFFECTS | UF_EFFECTS2;
		else if (state->effects & 0x0000ff00)
			bits = (bits & ~UF_EFFECTS) | UF_EFFECTS2;
	}
	if (bits & 0xff000000)
		bits |= UF_EXTEND3;
	if (bits & 0x00ff0000)
		bits |= UF_EXTEND2;
	if (bits & 0x0000ff00)
		bits |= UF_EXTEND1;

	MSG_WriteByte (msg, (bits >> 0) & 0xff);
	if (bits & UF_EXTEND1)
		MSG_WriteByte (msg, (bits >> 8) & 0xff);
	if (bits & UF_EXTEND2)
		MSG_WriteByte (msg, (bits >> 16) & 0xff);
	if (bits & UF_EXTEND3)
		MSG_WriteByte (msg, (bits >> 24) & 0xff);

	if (bits & UF_FRAME)
	{
		if (bits & UF_16BIT)
			MSG_WriteShort (msg, state->frame);
		else
			MSG_WriteByte (msg, state->frame);
	}
	if (bits & UF_ORIGINXY)
	{
		MSG_WriteCoord (msg, state->origin[0], protocolflags);
		MSG_WriteCoord (msg, state->origin[1], protocolflags);
	}
	if (bits & UF_ORIGINZ)
		MSG_WriteCoord (msg, state->origin[2], protocolflags);
	if (bits & UF_ANGLESXZ)
	{
		MSG_WriteAngle (msg, state->angles[0], protocolflags);
		MSG_WriteAngle (msg, state->angles[2], protocolflags);
	}
	if (bits & UF_ANGLESY)
		MSG_WriteAngle (msg, state->angles[1], protocolflags);
	if ((bits & (UF_EFFECTS | UF_EFFECTS2)) == (UF_EFFECTS | UF_EFFECTS2))
		MSG_WriteLong (msg, state->effects);
	else if (bits & UF_EFFECTS2)
		MSG_WriteShort (msg, state->effects);
	else if (bits & UF_EFFECTS)
		MSG_WriteByte (msg, state->effects);
	if (bits & UF_PREDINFO)
	{
		predbits |= SVFTE_DeltaPredCalcBits (NULL, state);
		MSG_WriteByte (msg, predbits);
		if (predbits & UFP_MOVETYPE)
			MSG_WriteByte (msg, state->pmovetype);
		if (predbits & UFP_VELOCITYXY)
		{
			MSG_WriteShort (msg, state->velocity[0]);
			MSG_WriteShort (msg, state->velocity[1]);
		}
		if (predbits & UFP_VELOCITYZ)
			MSG_WriteShort (msg, state->velocity[2]);
	}
	if (bits & UF_MODEL)
	{
		if (bits & UF_16BIT)
			MSG_WriteShort (msg, state->modelindex);
		else
			MSG_WriteByte (msg, state->modelindex);
	}
	if (bits & UF_SKIN)
	{
		if (bits & UF_16BIT)
			MSG_WriteShort (msg, state->skin);
		else
			MSG_WriteByte (msg, state->skin);
	}
	if (bits & UF_COLORMAP)
		MSG_WriteByte (msg, state->colormap);
	if (bits & UF_SOLID)
	{
		if (pext2 & PEXT2_NEWSIZEENCODING)
		{
			if (!state->solidsize)
				MSG_WriteByte (msg, 0);
			else if (state->solidsize == ES_SOLID_BSP)
				MSG_WriteByte (msg, 1);
			else if (state->solidsize == ES_SOLID_HULL1)
				MSG_WriteByte (msg, 2);
			else if (state->solidsize == ES_SOLID_HULL2)
				MSG_WriteByte (msg, 3);
			else if (!ES_SOLID_HAS_EXTRA_BITS (state->solidsize))
			{
				MSG_WriteByte (msg, 16);
				MSG_WriteSize16 (msg, state->solidsize);
			}
			else
			{
				MSG_WriteByte (msg, 32);
				MSG_WriteLong (msg, state->solidsize);
			}
		}
		else
			MSG_WriteSize16 (msg, state->solidsize);
	}
	if (bits & UF_FLAGS)
		MSG_WriteByte (msg, state->eflags);
	if (bits & UF_ALPHA)
		MSG_WriteByte (msg, (state->alpha - 1) & 0xff);
	if (bits & UF_SCALE)
		MSG_WriteByte (msg, state->scale);
	if (bits & UF_TAGINFO)
	{
		MSG_WriteEntity (msg, state->tagentity, pext2);
		MSG_WriteByte (msg, state->tagindex);
	}
	if (bits & UF_TRAILEFFECT)
	{
		if (state->emiteffectnum)
		{
			MSG_WriteShort (msg, (state->traileffectnum & 0x3fff) | 0x8000);
			MSG_WriteShort (msg, state->emiteffectnum & 0x3fff);
		}
		else
			MSG_WriteShort (msg, state->traileffectnum & 0x3fff);
	}
	if (bits & UF_COLORMOD)
	{
		MSG_WriteByte (msg, state->colormod[0]);
		MSG_WriteByte (msg, state->colormod[1]);
		MSG_WriteByte (msg, state->colormod[2]);
	}
}

static void MSG_WriteStaticOrBaseLine (sizebuf_t *buf, int idx,
	entity_state_t *state, unsigned int protocol_pext2,
	unsigned int protocolflags)
{
	int i;
	int bits = 0;

	if (protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
	{
		if (idx >= 0)
		{
			MSG_WriteByte (buf, svcfte_spawnbaseline2);
			MSG_WriteEntity (buf, idx, protocol_pext2);
		}
		else
			MSG_WriteByte (buf, svcfte_spawnstatic2);
		MSGFTE_WriteEntityUpdate (MSGFTE_DeltaCalcBits (&nullentitystate,
			state), state, buf, protocol_pext2, protocolflags);
		return;
	}

	if (state->modelindex & 0xFF00)
		bits |= B_LARGEMODEL;
	if (state->frame & 0xFF00)
		bits |= B_LARGEFRAME;
	if (state->alpha != ENTALPHA_DEFAULT)
		bits |= B_ALPHA;

	if (idx >= 0)
	{
		MSG_WriteByte (buf, bits ? svc_spawnbaseline2 : svc_spawnbaseline);
		MSG_WriteShort (buf, idx);
	}
	else
		MSG_WriteByte (buf, bits ? svc_spawnstatic2 : svc_spawnstatic);

	if (bits)
		MSG_WriteByte (buf, bits);

	if (bits & B_LARGEMODEL)
		MSG_WriteShort (buf, state->modelindex);
	else
		MSG_WriteByte (buf, state->modelindex);

	if (bits & B_LARGEFRAME)
		MSG_WriteShort (buf, state->frame);
	else
		MSG_WriteByte (buf, state->frame);

	MSG_WriteByte (buf, state->colormap);
	MSG_WriteByte (buf, state->skin);
	for (i = 0; i < 3; i++)
	{
		MSG_WriteCoord (buf, state->origin[i], protocolflags);
		MSG_WriteAngle (buf, state->angles[i], protocolflags);
	}
	if (bits & B_ALPHA)
		MSG_WriteByte (buf, state->alpha);
}

void SVFTE_DestroyFrames (client_t *client)
{
	size_t i;

	for (i = 0; i < MAX_CL_STATS; i++)
	{
		free (client->oldstats_s[i]);
		client->oldstats_s[i] = NULL;
	}
	free (client->previousentities);
	client->previousentities = NULL;
	client->numpreviousentities = 0;
	client->maxpreviousentities = 0;
	free (client->pendingentities_bits);
	client->pendingentities_bits = NULL;
	client->numpendingentities = 0;
	free (client->pendingcsqcentities_bits);
	client->pendingcsqcentities_bits = NULL;
	client->numpendingcsqcentities = 0;
	if (client->frames)
	{
		while (client->numframes > 0)
		{
			client->numframes--;
			free (client->frames[client->numframes].ents);
		}
		free (client->frames);
	}
	client->frames = NULL;
	client->numframes = 0;
	client->lastacksequence = 0;
	client->net_snapshot_sequence = 0;
	client->snapshotresume = 0;
}

static void SVFTE_SetupFrames (client_t *client)
{
	size_t i;

	SVFTE_DestroyFrames (client);
	memset (client->oldstats_i, 0, sizeof(client->oldstats_i));
	memset (client->oldstats_f, 0, sizeof(client->oldstats_f));
	memset (client->resendstatsnum, 0, sizeof(client->resendstatsnum));
	memset (client->resendstatsstr, 0, sizeof(client->resendstatsstr));
	client->lastmovemessage = 0;

	client->numframes = 64;
	client->frames = (struct deltaframe_s *)calloc (client->numframes, sizeof(*client->frames));
	if (!client->frames)
		Sys_Error ("SVFTE_SetupFrames: calloc frames failed");
	client->net_snapshot_sequence = -1;
	client->lastacksequence = (int)0x80000000u;
	for (i = 0; i < client->numframes; i++)
		client->frames[i].sequence = client->lastacksequence;

	client->numpendingentities = q_max (1, qcvm->num_edicts + 64);
	client->pendingentities_bits =
		(unsigned int *)calloc (client->numpendingentities, sizeof(*client->pendingentities_bits));
	if (!client->pendingentities_bits)
		Sys_Error ("SVFTE_SetupFrames: calloc pendingentities failed");
	client->pendingentities_bits[0] = UF_REMOVE;

	client->numpendingcsqcentities = q_max (1, qcvm->num_edicts + 64);
	client->pendingcsqcentities_bits =
		(unsigned int *)calloc (client->numpendingcsqcentities, sizeof(*client->pendingcsqcentities_bits));
	if (!client->pendingcsqcentities_bits)
		Sys_Error ("SVFTE_SetupFrames: calloc pendingcsqcentities failed");
}

static void SVFTE_EnsurePendingEntityBits (client_t *client, size_t count)
{
	if (client->numpendingentities >= count)
		return;
	count += 64;
	client->pendingentities_bits = (unsigned int *)realloc (client->pendingentities_bits,
		count * sizeof(*client->pendingentities_bits));
	if (!client->pendingentities_bits)
		Sys_Error ("SVFTE_EnsurePendingEntityBits: realloc failed");
	memset (client->pendingentities_bits + client->numpendingentities, 0,
		(count - client->numpendingentities) * sizeof(*client->pendingentities_bits));
	client->numpendingentities = count;
}

static void SVFTE_EnsurePendingCSQCEntityBits (client_t *client, size_t count)
{
	if (client->numpendingcsqcentities >= count)
		return;
	count += 64;
	client->pendingcsqcentities_bits = (unsigned int *)realloc (client->pendingcsqcentities_bits,
		count * sizeof(*client->pendingcsqcentities_bits));
	if (!client->pendingcsqcentities_bits)
		Sys_Error ("SVFTE_EnsurePendingCSQCEntityBits: realloc failed");
	memset (client->pendingcsqcentities_bits + client->numpendingcsqcentities, 0,
		(count - client->numpendingcsqcentities) * sizeof(*client->pendingcsqcentities_bits));
	client->numpendingcsqcentities = count;
}

static void SVFTE_DroppedFrame (client_t *client, int sequence)
{
	int i;
	struct deltaframe_s *frame;

	if (!client->numframes)
		return;
	frame = &client->frames[sequence & (client->numframes - 1)];
	if (frame->sequence != sequence)
		return;
	frame->sequence = -1;
	for (i = 0; i < MAX_CL_STATS / 32; i++)
	{
		client->resendstatsnum[i] |= frame->resendstatsnum[i];
		client->resendstatsstr[i] |= frame->resendstatsstr[i];
	}
	for (i = 0; i < frame->numents; i++)
	{
		SVFTE_EnsurePendingEntityBits (client, frame->ents[i].num + 1);
		if (frame->ents[i].ebits)
			client->pendingentities_bits[frame->ents[i].num] |= frame->ents[i].ebits;
		if (frame->ents[i].csqcbits)
		{
			SVFTE_EnsurePendingCSQCEntityBits (client, frame->ents[i].num + 1);
			client->pendingcsqcentities_bits[frame->ents[i].num] |= frame->ents[i].csqcbits;
		}
	}
}

void SVFTE_Ack (client_t *client, int sequence)
{
	int dropseq;
	struct deltaframe_s *frame;

	if (!client->numframes)
		return;
	if (sequence == -1)
	{
		SVFTE_EnsurePendingEntityBits (client, 1);
		client->pendingentities_bits[0] |= UF_REMOVE;
	}
	if (sequence < client->lastacksequence)
		return;

	dropseq = client->lastacksequence + 1;
	if ((unsigned)(dropseq - sequence) >= client->numframes)
		dropseq = sequence - (int)client->numframes;
	while (dropseq < sequence)
		SVFTE_DroppedFrame (client, dropseq++);
	client->lastacksequence = sequence;
	client->net_snapshot_last_ack_time = realtime;

	frame = &client->frames[sequence & (client->numframes - 1)];
	if (frame->sequence == sequence)
	{
		frame->sequence = -1;
		client->ping_times[client->num_pings % NUM_PING_TIMES] =
			qcvm->time - frame->timestamp;
		client->num_pings++;
	}
}

static struct deltaframe_s *SVFTE_BeginFrame (client_t *client, int sequence)
{
	struct deltaframe_s *frame;

	frame = &client->frames[sequence & (client->numframes - 1)];
	if (frame->sequence != sequence)
	{
		if (frame->sequence > client->lastacksequence)
			SVFTE_DroppedFrame (client, frame->sequence);
		frame->sequence = sequence;
		frame->timestamp = qcvm->time;
		memset (frame->resendstatsnum, 0, sizeof(frame->resendstatsnum));
		memset (frame->resendstatsstr, 0, sizeof(frame->resendstatsstr));
		frame->numents = 0;
	}
	return frame;
}

static void SVFTE_BuildEntityState (client_t *client, edict_t *ent, entity_state_t *state)
{
	eval_t *val;

	*state = nullentitystate;
	VectorCopy (ent->v.origin, state->origin);
	VectorCopy (ent->v.angles, state->angles);
	state->modelindex = ent->v.modelindex;
	state->frame = ent->v.frame;
	state->colormap = ent->v.colormap;
	state->skin = ent->v.skin;
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.scale)))
		state->scale = ENTSCALE_ENCODE (val->_float);
	else
		state->scale = ENTSCALE_DEFAULT;
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.alpha)))
		state->alpha = ENTALPHA_ENCODE (val->_float);
	else
		state->alpha = ent->alpha;
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.colormod)) &&
		(val->vector[0] || val->vector[1] || val->vector[2]))
	{
		state->colormod[0] = CLAMP (0, Q_rint (val->vector[0] * 32), 255);
		state->colormod[1] = CLAMP (0, Q_rint (val->vector[1] * 32), 255);
		state->colormod[2] = CLAMP (0, Q_rint (val->vector[2] * 32), 255);
	}
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.traileffectnum)))
		state->traileffectnum = CLAMP (0, (int)val->_float, 0x3fff);
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.emiteffectnum)))
		state->emiteffectnum = CLAMP (0, (int)val->_float, 0x3fff);
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.tag_entity)) && val->edict)
		state->tagentity = NUM_FOR_EDICT(PROG_TO_EDICT(val->edict));
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.tag_index)))
		state->tagindex = CLAMP(0, (int)val->_float, 255);
	state->effects = (int)ent->v.effects & qcvm->effects_mask;
	if (ent->v.movetype == MOVETYPE_STEP)
		state->eflags |= EFLAGS_STEP;
	if (client && client->edict == ent && ((int)ent->v.flags & FL_ONGROUND))
		state->eflags |= EFLAGS_ONGROUND;
	if (client && client->edict == ent)
	{
		/*
		 * Match QSS-M by advertising prediction only when the server is
		 * actually running PMove/QC input movement. Predicting vanilla NQ
		 * movement with PMove is an explicit test mode because the physics
		 * models diverge enough to create visible correction.
		 */
		if (client->move_prediction_allowed || sv_predict_nqmovement.value)
		{
			state->pmovetype = (int)ent->v.movetype & 63;
			if ((int)ent->v.flags & FL_ONGROUND)
				state->pmovetype |= 0x80;
			val = GetEdictFieldValue (ent, qcvm->extfields.pmove_flags);
			if (val)
			{
				if ((int)val->_float & PMF_JUMP_HELD)
					state->pmovetype |= 0x40;
			}
			else if (!((int)ent->v.flags & FL_JUMPRELEASED))
				state->pmovetype |= 0x40;
		}
		state->velocity[0] = CLAMP (-32768, Q_rint (ent->v.velocity[0] * 8.0f), 32767);
		state->velocity[1] = CLAMP (-32768, Q_rint (ent->v.velocity[1] * 8.0f), 32767);
		state->velocity[2] = CLAMP (-32768, Q_rint (ent->v.velocity[2] * 8.0f), 32767);
	}

	if (client && client->edict && ent->v.owner == EDICT_TO_PROG (client->edict))
		state->solidsize = ES_SOLID_NOT;
	else if (ent->v.solid == SOLID_BSP || (ent->v.skin < 0 && ent->v.modelindex))
		state->solidsize = ES_SOLID_BSP;
	else if (ent->v.solid == SOLID_BBOX || ent->v.solid == SOLID_SLIDEBOX || ent->v.skin < 0)
	{
		state->solidsize = CLAMP (0, (int)-ent->v.mins[0], 255);
		state->solidsize |= CLAMP (0, (int)-ent->v.mins[2], 255) << 8;
		state->solidsize |= CLAMP (0, (int)(ent->v.maxs[2] + 32768), 65535) << 16;
		if (state->solidsize == 0x80000000u)
			state->solidsize = ES_SOLID_NOT;
	}
}

static void SVFTE_BuildSnapshotForClient (client_t *client)
{
	int e, i;
	byte *pvs;
	vec3_t org;
	edict_t *clent;
	edict_t *ent;
	edict_t *parent;
	entity_state_t state;
	eval_t *val;
	qboolean cancsqc, iscsqc;
	unsigned char eflags;
	int emiteffect;
	int maxentities;
	int parent_depth;
	int pvs_flags;
	int proged;

	snapshot_numents = 0;
	clent = client->edict;
	proged = EDICT_TO_PROG (clent);
	maxentities = (int)client->limit_entities;
	if (maxentities <= 0 || maxentities > qcvm->num_edicts)
		maxentities = qcvm->num_edicts;
	VectorAdd (clent->v.origin, clent->v.view_ofs, org);
	pvs = SV_FatPVS (org, sv.worldmodel);
	cancsqc = GetEdictFieldValid (SendEntity) && GetEdictFieldValid (SendFlags) && client->csqcactive;
	if (cancsqc)
		SVFTE_EnsurePendingCSQCEntityBits (client, maxentities + 1);
	if (sv_skyroom_pvs.value && sv.skyroom_pos_known)
	{
		vec3_t skyorg;
		VectorMA (sv.skyroom_pos, sv.skyroom_pos[3], org, skyorg);
		SV_AddToFatPVS (skyorg, sv.worldmodel->nodes, sv.worldmodel);
	}

	ent = NEXT_EDICT(qcvm->edicts);
	for (e = 1; e < maxentities; e++, ent = NEXT_EDICT(ent))
	{
		pvs_flags = 0;
		if ((val = GetEdictFieldValue (ent, qcvm->extfields.pvsflags)))
			pvs_flags = (int)val->_float;
		if (ent->free)
			goto invisible;
		if ((val = GetEdictFieldValue (ent, qcvm->extfields.customizeentityforclient)) &&
			val->function)
		{
			pr_global_struct->self = EDICT_TO_PROG (ent);
			pr_global_struct->other = proged;
			PR_ExecuteProgram(val->function);
			if (!G_FLOAT(OFS_RETURN))
				goto invisible;
		}

		eflags = 0;
		iscsqc = false;
		if (cancsqc && (val = GetEdictFieldEval (ent, SendEntity)) && val->function)
			iscsqc = true;
		emiteffect = 0;
		if (GetEdictFieldValid (emiteffectnum) && (val = GetEdictFieldEval (ent, emiteffectnum)))
			emiteffect = (int)val->_float;
		if (ent != clent)
		{
			if ((!ent->v.modelindex || !PR_GetString(ent->v.model)[0]) && !emiteffect && !iscsqc)
				goto invisible;

			if ((val = GetEdictFieldValue (ent, qcvm->extfields.viewmodelforclient)) &&
				val->edict == proged)
				eflags |= EFLAGS_VIEWMODEL;
			else if (val && val->edict)
				goto invisible;
			else if (!(coop.value && e >= 1 && e <= svs.maxclients))
			{
				switch (pvs_flags & PVSF_MODE_MASK)
				{
				case PVSF_NOTRACECHECK:
				case PVSF_NORMALPVS:
					parent = ent;
					parent_depth = 0;
					while ((val = GetEdictFieldValue (parent, qcvm->extfields.tag_entity)) &&
						val->edict)
					{
						parent = PROG_TO_EDICT(val->edict);
						if (++parent_depth > maxentities)
							goto invisible;
					}
					if (parent->num_leafs < MAX_ENT_LEAFS)
					{
						for (i = 0; i < parent->num_leafs; i++)
							if (pvs[parent->leafnums[i] >> 3] &
								(1 << (parent->leafnums[i] & 7)))
								break;
						if (i == parent->num_leafs)
							goto invisible;
					}
					break;
				case PVSF_USEPHS:
				case PVSF_IGNOREPVS:
					break;
				}
			}
		}

		if ((val = GetEdictFieldValue (ent, qcvm->extfields.nodrawtoclient)) &&
			val->edict == proged)
			goto invisible;
		if ((val = GetEdictFieldValue (ent, qcvm->extfields.drawonlytoclient)) &&
			val->edict && val->edict != proged)
			goto invisible;

		if (iscsqc)
		{
			if (!(client->pendingcsqcentities_bits[e] & SENDFLAG_PRESENT))
				client->pendingcsqcentities_bits[e] |= SENDFLAG_USABLE;
			else
				client->pendingcsqcentities_bits[e] |= (int)GetEdictFieldEval (ent, SendFlags)->_float & SENDFLAG_USABLE;
			continue;
		}
		if (cancsqc && client->pendingcsqcentities_bits[e])
			client->pendingcsqcentities_bits[e] |= SENDFLAG_REMOVE;
		SVFTE_BuildEntityState (client, ent, &state);
		if ((unsigned int)state.modelindex >= client->limit_models)
			state.modelindex = 0;
		if (ent != clent && state.alpha == ENTALPHA_ZERO && !state.effects &&
			!state.traileffectnum && !state.emiteffectnum)
			continue;
		if ((val = GetEdictFieldValue (ent, qcvm->extfields.exteriormodeltoclient)) &&
			val->edict == proged)
			eflags |= EFLAGS_EXTERIORMODEL;
		state.eflags |= eflags;
		SVFTE_AppendSnapshotEntity (e, &state);
		continue;

	invisible:
		if (cancsqc && e < (int)client->numpendingcsqcentities &&
			client->pendingcsqcentities_bits[e] &&
			!(pvs_flags & PVSF_NOREMOVE))
			client->pendingcsqcentities_bits[e] |= SENDFLAG_REMOVE;
	}
}

static void SVFTE_CalcEntityDeltas (client_t *client)
{
	struct entity_num_state_s *olds, *news, *oldstop, *newstop;
	size_t maxentities;

	maxentities = client->limit_entities ? client->limit_entities : qcvm->num_edicts;
	if (maxentities > (size_t)qcvm->num_edicts)
		maxentities = qcvm->num_edicts;
	SVFTE_EnsurePendingEntityBits (client, maxentities + 1);
	if (client->pendingentities_bits[0] & UF_REMOVE)
	{
		client->numpreviousentities = 0;
		client->pendingentities_bits[0] = UF_REMOVE;
	}

	news = snapshot_entstate;
	newstop = news + snapshot_numents;
	olds = client->previousentities;
	oldstop = olds + client->numpreviousentities;

	for (;;)
	{
		if (olds == oldstop && news == newstop)
			break;
		if (news == newstop || (olds != oldstop && olds->num < news->num))
		{
			SVFTE_EnsurePendingEntityBits (client, olds->num + 1);
			client->pendingentities_bits[olds->num] = UF_REMOVE;
			olds++;
		}
		else if (olds == oldstop || (news != newstop && news->num < olds->num))
		{
			SVFTE_EnsurePendingEntityBits (client, news->num + 1);
			client->pendingentities_bits[news->num] = UF_RESET;
			news++;
		}
		else
		{
			if (client->pendingentities_bits[news->num] & UF_REMOVE)
				client->pendingentities_bits[news->num] =
					(client->pendingentities_bits[news->num] & ~UF_REMOVE) | UF_RESET2;
			client->pendingentities_bits[news->num] |=
				MSGFTE_DeltaCalcBits (&olds->state, &news->state);
			news++;
			olds++;
		}
	}

	olds = client->previousentities;
	oldstop = olds + client->maxpreviousentities;
	client->previousentities = snapshot_entstate;
	client->numpreviousentities = snapshot_numents;
	client->maxpreviousentities = snapshot_maxents;
	snapshot_entstate = olds;
	snapshot_numents = 0;
	snapshot_maxents = oldstop - olds;
}

static void SVFTE_WriteStatsToClient (client_t *client, sizebuf_t *msg,
	struct deltaframe_s *frame)
{
	int			statsi[MAX_CL_STATS];
	float		statsf[MAX_CL_STATS];
	const char	*statss[MAX_CL_STATS];
	int			i, reserve;

	SV_CalcStats (client, statsi, statsf, statss);
	reserve = 9;

	for (i = 0; i < MAX_CL_STATS; i++)
	{
		int need;

		if (!statsi[i])
			statsi[i] = statsf[i];
		else
			statsf[i] = 0;

		if (statsi[i] != client->oldstats_i[i] ||
			statsf[i] != client->oldstats_f[i])
		{
			client->oldstats_i[i] = statsi[i];
			client->oldstats_f[i] = statsf[i];
			client->resendstatsnum[i / 32] |= 1u << (i & 31);
		}

		if (statss[i] || client->oldstats_s[i])
		{
			const char *os = client->oldstats_s[i] ? client->oldstats_s[i] : "";
			const char *ns = statss[i] ? statss[i] : "";
			if (strcmp(os, ns))
			{
				client->resendstatsstr[i / 32] |= 1u << (i & 31);
				free(client->oldstats_s[i]);
				client->oldstats_s[i] = strdup(ns);
			}
		}

		if (client->resendstatsnum[i / 32] & (1u << (i & 31)))
		{
			if ((double)statsi[i] != statsf[i] && statsf[i])
			{
				need = 6;
				if (msg->cursize + need + reserve > msg->maxsize)
					break;
				MSG_WriteByte (msg, svcfte_updatestatfloat);
				MSG_WriteByte (msg, i);
				MSG_WriteFloat (msg, statsf[i]);
			}
			else if (statsi[i] >= 0 && statsi[i] <= 255)
			{
				need = 3;
				if (msg->cursize + need + reserve > msg->maxsize)
					break;
				MSG_WriteByte (msg, svcdp_updatestatbyte);
				MSG_WriteByte (msg, i);
				MSG_WriteByte (msg, statsi[i]);
			}
			else
			{
				need = 6;
				if (msg->cursize + need + reserve > msg->maxsize)
					break;
				MSG_WriteByte (msg, svc_updatestat);
				MSG_WriteByte (msg, i);
				MSG_WriteLong (msg, statsi[i]);
			}
			client->resendstatsnum[i / 32] &= ~(1u << (i & 31));
			frame->resendstatsnum[i / 32] |= 1u << (i & 31);
		}

		if (client->resendstatsstr[i / 32] & (1u << (i & 31)))
		{
			const char *s = statss[i] ? statss[i] : "";

			need = 2 + strlen(s) + 1;
			if (msg->cursize + need + reserve > msg->maxsize)
				break;

			MSG_WriteByte (msg, svcfte_updatestatstring);
			MSG_WriteByte (msg, i);
			MSG_WriteString (msg, s);

			client->resendstatsstr[i / 32] &= ~(1u << (i & 31));
			frame->resendstatsstr[i / 32] |= 1u << (i & 31);
		}
	}
}

static double SV_NetLagDebugFrameThreshold (void)
{
	double threshold, tick_threshold;

	threshold = net_lagdebug_frame_threshold.value;
	if (sys_ticrate.value > 0)
	{
		tick_threshold = sys_ticrate.value + 0.005;
		if (threshold < tick_threshold)
			threshold = tick_threshold;
	}
	return threshold;
}

static qboolean SV_UsesReplacementDeltas (const client_t *client)
{
	(void)client;
	return true;
}

static int SV_ReplacementLastSentSequence (const client_t *client)
{
	return client->net_snapshot_sequence;
}

static int SV_ReplacementAckLag (const client_t *client, int sequence)
{
	if (sequence < 0 || client->lastacksequence < 0)
		return 0;
	if (sequence <= client->lastacksequence)
		return 0;
	return sequence - client->lastacksequence;
}

static void SVFTE_WriteEntitiesToClient (client_t *client, sizebuf_t *msg,
	struct deltaframe_s *frame, int sequence, qboolean drop_oversized)
{
	struct entity_num_state_s *state, *stateend;
	unsigned int entbits, logbits, netbits;
	size_t entnum;
	int header_need;
	int payload_start;
	byte entbuf[MAX_DATAGRAM];
	sizebuf_t entmsg;

	state = client->previousentities;
	stateend = state + client->numpreviousentities;

	header_need = 1 + 2 + 4 + 2;
	if (client->protocol_pext2 & PEXT2_EXPLICITCMDMSEC)
		header_need += 7;
	if (msg->cursize + header_need > msg->maxsize)
		return;

	MSG_WriteByte (msg, svcfte_updateentities);
	SV_WriteMoveAckPayloadToMessage (client, msg);
	MSG_WriteFloat (msg, qcvm->time);
	payload_start = msg->cursize;

	for (entnum = client->snapshotresume; entnum < client->numpendingentities; entnum++)
	{
		entbits = client->pendingentities_bits[entnum];
		if (!(entbits & ~UF_RESET2))
			continue;

		entmsg.data = entbuf;
		entmsg.maxsize = sizeof(entbuf);
		entmsg.cursize = 0;
		entmsg.allowoverflow = true;
		entmsg.overflowed = false;

		logbits = 0;
		netbits = 0;
		if (entbits & UF_REMOVE)
		{
			if (entnum > 0x3fff)
			{
				MSG_WriteShort (&entmsg, 0xc000 | (entnum & 0x3fff));
				MSG_WriteByte (&entmsg, entnum >> 14);
			}
			else
				MSG_WriteShort (&entmsg, 0x8000 | entnum);
			logbits = UF_REMOVE;
		}
		else
		{
			while (state < stateend && state->num < entnum)
				state++;
			if (state < stateend && state->num == entnum)
			{
				if (entbits & UF_RESET2)
				{
					logbits = entbits & ~(UF_RESET | UF_RESET2);
					netbits = UF_RESET |
						MSGFTE_DeltaCalcBits (&EDICT_NUM(entnum)->baseline, &state->state);
				}
				else if (entbits & UF_RESET)
				{
					netbits = UF_RESET |
						MSGFTE_DeltaCalcBits (&EDICT_NUM(entnum)->baseline, &state->state);
					logbits = UF_RESET;
				}
				else
					logbits = netbits = entbits;

				if (entnum >= 0x4000)
				{
					MSG_WriteShort (&entmsg, 0x4000 | (entnum & 0x3fff));
					MSG_WriteByte (&entmsg, entnum >> 14);
				}
				else
					MSG_WriteShort (&entmsg, entnum);
				MSGFTE_WriteEntityUpdate (netbits, &state->state, &entmsg,
					client->protocol_pext2, sv.protocolflags);
			}
		}

		if (!entmsg.cursize && !logbits)
		{
			client->pendingentities_bits[entnum] = 0;
			continue;
		}

		if (entmsg.overflowed ||
			(drop_oversized &&
			 msg->cursize == payload_start &&
			 msg->cursize + entmsg.cursize + 2 > msg->maxsize))
		{
			if (net_lagdebug.value)
				Con_Printf ("net_lagdebug: dropping oversized replacement entity delta for %s ent=%zu bytes=%d packet=%d max=%d bits=0x%x\n",
					client->name, entnum, entmsg.cursize, msg->cursize,
					msg->maxsize, entbits);
			client->pendingentities_bits[entnum] =
				(entbits & UF_RESET) && !(entbits & UF_RESET2) ? UF_RESET2 : 0;
			continue;
		}

		if (msg->cursize + entmsg.cursize + 2 > msg->maxsize)
		{
			client->pendingentities_bits[entnum] = entbits;
			break;
		}

		client->pendingentities_bits[entnum] =
			(entbits & UF_RESET) && !(entbits & UF_RESET2) ? UF_RESET2 : 0;
		if (entmsg.cursize)
			SZ_Write (msg, entmsg.data, entmsg.cursize);

		if (!logbits)
			continue;
		if (frame->numents == frame->maxents)
		{
			frame->maxents += 64;
			frame->ents = (void *)realloc (frame->ents, sizeof(*frame->ents) * frame->maxents);
			if (!frame->ents)
				Sys_Error ("SVFTE_WriteEntitiesToClient: realloc frame ents failed");
		}
		frame->ents[frame->numents].num = entnum;
		frame->ents[frame->numents].ebits = logbits;
		frame->ents[frame->numents].csqcbits = 0;
		frame->numents++;
	}
	MSG_WriteShort (msg, 0);
	client->snapshotresume = entnum;
	dev_stats.packetsize = msg->cursize;
	dev_peakstats.packetsize = q_max (msg->cursize, dev_peakstats.packetsize);
}

static void SVFTE_WriteCSQCEntitiesToClient (client_t *client, sizebuf_t *msg,
	struct deltaframe_s *frame, qboolean drop_oversized)
{
	edict_t *ed;
	unsigned int bits, originalbits, logbits;
	size_t entnum;
	qboolean wroteheader = false;
	qboolean candidate_has_header;
	byte entbuf[MAX_DATAGRAM];
	sizebuf_t entmsg;

	if (!client->csqcactive || !GetEdictFieldValid(SendEntity) || !GetEdictFieldValid(SendFlags))
		return;
	if (!client->pendingcsqcentities_bits)
		return;

	for (entnum = 1; entnum < client->numpendingcsqcentities; entnum++)
	{
		int old_multicast_maxsize;
		int payload_maxsize;
		qboolean old_multicast_allowoverflow;
		qboolean multicast_overflowed;

		bits = client->pendingcsqcentities_bits[entnum];
		if (!(bits & ~SENDFLAG_PRESENT))
			continue;

		originalbits = bits;
		logbits = 0;
		candidate_has_header = false;
		payload_maxsize = msg->maxsize - 2;
		if (payload_maxsize < 16)
			payload_maxsize = 16;
		entmsg.data = entbuf;
		entmsg.maxsize = q_min((int)sizeof(entbuf), payload_maxsize);
		entmsg.cursize = 0;
		entmsg.allowoverflow = true;
		entmsg.overflowed = false;

		if (bits & SENDFLAG_REMOVE)
		{
	sendremove:
			if (!wroteheader)
			{
				MSG_WriteByte (&entmsg, svcdp_csqcentities);
				candidate_has_header = true;
			}
			if (entnum > 0x3fff)
			{
				MSG_WriteShort (&entmsg, 0xc000 | (entnum & 0x3fff));
				MSG_WriteByte (&entmsg, entnum >> 14);
			}
			else
				MSG_WriteShort (&entmsg, 0x8000 | entnum);
			logbits = SENDFLAG_REMOVE;
			bits = 0;
		}
		else
		{
			ed = EDICT_NUM(entnum);
			if (ed->free || !GetEdictFieldEval(ed, SendEntity)->function)
			{
				if (bits & SENDFLAG_PRESENT)
					goto sendremove;
				logbits = bits = 0;
			}
			else
			{
				SZ_Clear (&sv.multicast);
				old_multicast_maxsize = sv.multicast.maxsize;
				old_multicast_allowoverflow = sv.multicast.allowoverflow;
				sv.multicast.maxsize =
					q_min(old_multicast_maxsize, payload_maxsize);
				sv.multicast.allowoverflow = true;
				pr_global_struct->self = EDICT_TO_PROG(ed);
				G_INT(OFS_PARM0) = EDICT_TO_PROG(client->edict);
				G_FLOAT(OFS_PARM1 + 0) = (bits >> 0) & 0xffffff;
				G_FLOAT(OFS_PARM1 + 1) = (bits >> 24) & 0xffffff;
				G_FLOAT(OFS_PARM1 + 2) = 0;
				PR_ExecuteProgram(GetEdictFieldEval(ed, SendEntity)->function);
				multicast_overflowed = sv.multicast.overflowed;
				sv.multicast.maxsize = old_multicast_maxsize;
				sv.multicast.allowoverflow = old_multicast_allowoverflow;
				sv.multicast.overflowed = multicast_overflowed;

				if (G_FLOAT(OFS_RETURN))
				{
					logbits = bits;
					if (!sv.multicast.overflowed)
					{
						if (!wroteheader)
						{
							MSG_WriteByte (&entmsg, svcdp_csqcentities);
							candidate_has_header = true;
						}
						if (entnum >= 0x4000)
						{
							MSG_WriteShort (&entmsg, 0x4000 | (entnum & 0x3fff));
							MSG_WriteByte (&entmsg, entnum >> 14);
						}
						else
							MSG_WriteShort (&entmsg, entnum);

						SZ_Write (&entmsg, sv.multicast.data,
							sv.multicast.cursize);
						bits = SENDFLAG_PRESENT;
					}
				}
				else if (bits & SENDFLAG_PRESENT)
					goto sendremove;
				else
					logbits = bits = 0;
			}
		}

		if (!entmsg.cursize && !logbits)
		{
			sv.multicast.overflowed = false;
			client->pendingcsqcentities_bits[entnum] = bits;
			continue;
		}

		if (entmsg.overflowed || sv.multicast.overflowed ||
			(drop_oversized &&
			 msg->cursize + entmsg.cursize + 2 > msg->maxsize))
		{
			if (net_lagdebug.value)
				Con_Printf ("net_lagdebug: dropping oversized replacement CSQC delta for %s ent=%zu bytes=%d packet=%d max=%d bits=0x%x overflow=%d/%d\n",
					client->name, entnum, entmsg.cursize, msg->cursize,
					msg->maxsize, originalbits, entmsg.overflowed ? 1 : 0,
					sv.multicast.overflowed ? 1 : 0);
			sv.multicast.overflowed = false;
			client->pendingcsqcentities_bits[entnum] =
				(originalbits & SENDFLAG_REMOVE) ? 0 : SENDFLAG_PRESENT;
			continue;
		}

		if (msg->cursize + entmsg.cursize + 2 > msg->maxsize)
		{
			sv.multicast.overflowed = false;
			client->pendingcsqcentities_bits[entnum] = originalbits;
			break;
		}

		SZ_Write (msg, entmsg.data, entmsg.cursize);
		if (candidate_has_header)
			wroteheader = true;
		sv.multicast.overflowed = false;
		client->pendingcsqcentities_bits[entnum] = bits;

		if (logbits)
		{
			if (frame->numents == frame->maxents)
			{
				frame->maxents += 64;
				frame->ents = (void *)realloc (frame->ents, sizeof(*frame->ents) * frame->maxents);
				if (!frame->ents)
					Sys_Error ("SVFTE_WriteCSQCEntitiesToClient: realloc frame ents failed");
			}
			frame->ents[frame->numents].num = entnum;
			frame->ents[frame->numents].ebits = 0;
			frame->ents[frame->numents].csqcbits = logbits;
			frame->numents++;
		}
	}

	if (wroteheader)
		MSG_WriteShort (msg, 0);
	SZ_Clear (&sv.multicast);
	dev_stats.packetsize = msg->cursize;
	dev_peakstats.packetsize = q_max (msg->cursize, dev_peakstats.packetsize);
}

static size_t SVFTE_CountPendingCSQCEntities (client_t *client)
{
	size_t i, count;

	if (!client->pendingcsqcentities_bits)
		return 0;
	count = 0;
	for (i = 1; i < client->numpendingcsqcentities; i++)
		if (client->pendingcsqcentities_bits[i] & ~SENDFLAG_PRESENT)
			count++;
	return count;
}

static size_t SVFTE_CountPendingEntityDeltasFrom (client_t *client, size_t start)
{
	size_t i, count;

	if (!client->pendingentities_bits)
		return 0;
	if (start >= client->numpendingentities)
		return 0;
	count = 0;
	for (i = start; i < client->numpendingentities; i++)
		if (client->pendingentities_bits[i] & ~UF_RESET2)
			count++;
	return count;
}

static size_t SVFTE_CountPendingEntityDeltas (client_t *client)
{
	return SVFTE_CountPendingEntityDeltasFrom (client, 0);
}

static int SVFTE_ReplacementMaxPacketsPerFrame (void)
{
	int maxpackets;

	/*
	 * QSS-M drains replacement-delta snapshots in one server frame without a
	 * fixed packet cap. Keep that as the default for large-map parity; positive
	 * cvar values are an explicit manual bandwidth throttle.
	 */
	if (sv_replacement_maxpackets.value <= 0)
		return INT_MAX;
	if (sv_replacement_maxpackets.value >= (float)INT_MAX)
		return INT_MAX;

	maxpackets = (int)sv_replacement_maxpackets.value;
	return CLAMP (1, maxpackets, INT_MAX);
}

/*
=============
SV_VisibleToClient -- johnfitz

PVS test encapsulated in a nice function
=============
*/
qboolean SV_VisibleToClient (edict_t *client, edict_t *test, qmodel_t *worldmodel)
{
	byte	*pvs;
	vec3_t	org;

	VectorAdd (client->v.origin, client->v.view_ofs, org);
	pvs = SV_FatPVS (org, worldmodel);

	return SV_EdictInPVS (test, pvs);
}

//=============================================================================

#define MAX_NET_EDICTS 65536

static uint16_t net_edicts[MAX_NET_EDICTS];
static byte     net_edict_dists[MAX_NET_EDICTS];
static int      net_edict_bins[256];
static uint16_t net_edicts_sorted[MAX_NET_EDICTS];
static int      net_edict_write_start;
static int      net_edict_write_next;
static int      net_edict_write_total;

/*
=============
SV_WriteEntitiesToClient

=============
*/
void SV_WriteEntitiesToClient (edict_t	*clent, sizebuf_t *msg)
{
	int		e, i, j, numents;
	int		bits;
	byte	*pvs;
	vec3_t	org, forward, right, up;
	float	miss, dist, size;
	eval_t	*val;
	edict_t	*ent;

// find the client's PVS
	VectorAdd (clent->v.origin, clent->v.view_ofs, org);
	pvs = SV_FatPVS (org, sv.worldmodel);
	if (sv_skyroom_pvs.value && sv.skyroom_pos_known)
	{
		vec3_t skyorg;

		VectorMA (sv.skyroom_pos, sv.skyroom_pos[3], org, skyorg);
		SV_AddToFatPVS (skyorg, sv.worldmodel->nodes, sv.worldmodel);
	}

// find the client's orientation (for "behind camera" sort key)
	AngleVectors (clent->v.v_angle, forward, right, up);

// reset sorting bins
	memset (net_edict_bins, 0, sizeof (net_edict_bins));

// add clent first - always sent and gets the highest priority bin
	if (sv_netsort.value)
	{
		net_edicts[0] = NUM_FOR_EDICT (clent);
		net_edict_dists[0] = 0;
		net_edict_bins[0] = 1;
	}
	else
	{
		net_edicts_sorted[0] = NUM_FOR_EDICT (clent);
	}
	numents = 1;

// add all other entities that touch the pvs (or that the coop hack forces in)
	ent = NEXT_EDICT(qcvm->edicts);
	for (e=1 ; e<qcvm->num_edicts ; e++, ent = NEXT_EDICT(ent))
	{
		if (ent == clent)
			continue;	// already added before the loop

		// ignore ents without visible models
		if (!ent->v.modelindex || !PR_GetString(ent->v.model)[0])
			continue;

		//johnfitz -- don't send model>255 entities if protocol is 15
		if (sv.protocol == PROTOCOL_NETQUAKE && (int)ent->v.modelindex & 0xFF00)
			continue;

		// in co-op, always send other players regardless of PVS
		// so VR outline feature can show them through walls
		if (coop.value && e >= 1 && e <= svs.maxclients)
			goto skip_pvs_cull;

		// ignore if not touching a PV leaf
		for (i=0 ; i < ent->num_leafs ; i++)
			if (pvs[ent->leafnums[i] >> 3] & (1 << (ent->leafnums[i]&7) ))
				break;

		// ericw -- added ent->num_leafs < MAX_ENT_LEAFS condition.
		//
		// if ent->num_leafs == MAX_ENT_LEAFS, the ent is visible from too many leafs
		// for us to say whether it's in the PVS, so don't try to vis cull it.
		// this commonly happens with rotators, because they often have huge bboxes
		// spanning the entire map, or really tall lifts, etc.
		if (i == ent->num_leafs && ent->num_leafs < MAX_ENT_LEAFS)
			continue;		// not visible

skip_pvs_cull:
		if (sv_netsort.value)
		{
			// distance from view origin to closest point on ent's bbox,
			// scaled by ent size; sqrt-of-sqrt keeps bins evenly populated
			// across an entire level rather than clustered at low distances.
			dist = size = 0.f;
			for (i=0 ; i<3 ; i++)
			{
				float delta = CLAMP (ent->v.absmin[i], org[i], ent->v.absmax[i]) - org[i];
				dist += delta * delta;
				delta = ent->v.absmax[i] - ent->v.absmin[i];
				size += delta * delta;
			}
			size = q_max (1.f, size);

			dist = 8.f * sqrt (sqrt (dist/size));
			net_edict_dists[numents] = (int) q_min (dist, 255.f);
			net_edicts[numents] = e;

			// if the entire bbox is behind the eye, set the high bit so
			// the entity sorts after everything in front (bins 128..255).
			dist = 0.f;
			for (i=0 ; i<3 ; i++)
				dist += ((forward[i] < 0.f ? ent->v.absmin[i] : ent->v.absmax[i]) - org[i]) * forward[i];
			if (dist < 0.f)
				net_edict_dists[numents] |= 128;

			net_edict_bins[net_edict_dists[numents]]++;
		}
		else
		{
			net_edicts_sorted[numents] = e;
		}

		if (++numents == MAX_NET_EDICTS)
			break;
	}

	if (sv_netsort.value)
	{
		// prefix sum bins -> insertion offsets
		e = 0;
		for (i=0 ; i<256 ; i++)
		{
			int tmp = net_edict_bins[i];
			net_edict_bins[i] = e;
			e += tmp;
		}

		// place each edict into its sorted slot
		for (e=0 ; e<numents ; e++)
			net_edicts_sorted[net_edict_bins[net_edict_dists[e]]++] = net_edicts[e];
	}

	net_edict_write_total = numents;
	net_edict_write_next = numents;

// send entities, closest/most-relevant first
	for (j=net_edict_write_start ; j<numents ; j++)
	{
		e = net_edicts_sorted[j];
		ent = EDICT_NUM (e);

		// johnfitz -- max size for protocol 15 is 18 bytes, not 16 as originally
		// assumed here.  And, for protocol 85 the max size is actually 24 bytes.
		// For float coords and angles the limit is 40.
		// FIXME: Use tighter limit according to protocol flags and send bits.
		if (msg->cursize + 40 > msg->maxsize)
		{
			net_edict_write_next = j;
			goto stats;
		}

// send an update
		bits = 0;

		for (i=0 ; i<3 ; i++)
		{
			miss = ent->v.origin[i] - ent->baseline.origin[i];
			if ( miss < -0.1 || miss > 0.1 )
				bits |= U_ORIGIN1<<i;
		}

		if ( ent->v.angles[0] != ent->baseline.angles[0] )
			bits |= U_ANGLE1;

		if ( ent->v.angles[1] != ent->baseline.angles[1] )
			bits |= U_ANGLE2;

		if ( ent->v.angles[2] != ent->baseline.angles[2] )
			bits |= U_ANGLE3;

		if (ent->v.movetype == MOVETYPE_STEP)
			bits |= U_STEP;	// don't mess up the step animation

		if (ent->baseline.colormap != ent->v.colormap)
			bits |= U_COLORMAP;

		if (ent->baseline.skin != ent->v.skin)
			bits |= U_SKIN;

		if (ent->baseline.frame != ent->v.frame)
			bits |= U_FRAME;

		if ((ent->baseline.effects ^ (int)ent->v.effects) & qcvm->effects_mask)
			bits |= U_EFFECTS;

		if (ent->baseline.modelindex != ent->v.modelindex)
			bits |= U_MODEL;

		//johnfitz -- alpha
		// TODO: find a cleaner place to put this code
		val = GetEdictFieldValueByName(ent, "alpha");
		if (val)
			ent->alpha = ENTALPHA_ENCODE(val->_float);

		//don't send invisible entities unless they have effects
		if (ent->alpha == ENTALPHA_ZERO && !((int)ent->v.effects & qcvm->effects_mask))
			continue;
		//johnfitz

		val = GetEdictFieldValueByName(ent, "scale");
		if (val)
			ent->scale = ENTSCALE_ENCODE(val->_float);
		else
			ent->scale = ENTSCALE_DEFAULT;

		//johnfitz -- PROTOCOL_FITZQUAKE
		if (sv.protocol != PROTOCOL_NETQUAKE)
		{

			if (ent->baseline.alpha != ent->alpha) bits |= U_ALPHA;
			if (ent->baseline.scale != ent->scale) bits |= U_SCALE;
			if (bits & U_FRAME && (int)ent->v.frame & 0xFF00) bits |= U_FRAME2;
			if (bits & U_MODEL && (int)ent->v.modelindex & 0xFF00) bits |= U_MODEL2;
			if (ent->sendinterval) bits |= U_LERPFINISH;
			if (bits >= 65536) bits |= U_EXTEND1;
			if (bits >= 16777216) bits |= U_EXTEND2;
		}
		//johnfitz

		if (e >= 256)
			bits |= U_LONGENTITY;

		if (bits >= 256)
			bits |= U_MOREBITS;

	//
	// write the message
	//
		MSG_WriteByte (msg, bits | U_SIGNAL);

		if (bits & U_MOREBITS)
			MSG_WriteByte (msg, bits>>8);

		//johnfitz -- PROTOCOL_FITZQUAKE
		if (bits & U_EXTEND1)
			MSG_WriteByte(msg, bits>>16);
		if (bits & U_EXTEND2)
			MSG_WriteByte(msg, bits>>24);
		//johnfitz

		if (bits & U_LONGENTITY)
			MSG_WriteShort (msg,e);
		else
			MSG_WriteByte (msg,e);

		if (bits & U_MODEL)
			MSG_WriteByte (msg,	ent->v.modelindex);
		if (bits & U_FRAME)
			MSG_WriteByte (msg, ent->v.frame);
		if (bits & U_COLORMAP)
			MSG_WriteByte (msg, ent->v.colormap);
		if (bits & U_SKIN)
			MSG_WriteByte (msg, ent->v.skin);
		if (bits & U_EFFECTS)
			MSG_WriteByte (msg, (int)ent->v.effects & qcvm->effects_mask);
		if (bits & U_ORIGIN1)
			MSG_WriteCoord (msg, ent->v.origin[0], sv.protocolflags);
		if (bits & U_ANGLE1)
			MSG_WriteAngle(msg, ent->v.angles[0], sv.protocolflags);
		if (bits & U_ORIGIN2)
			MSG_WriteCoord (msg, ent->v.origin[1], sv.protocolflags);
		if (bits & U_ANGLE2)
			MSG_WriteAngle(msg, ent->v.angles[1], sv.protocolflags);
		if (bits & U_ORIGIN3)
			MSG_WriteCoord (msg, ent->v.origin[2], sv.protocolflags);
		if (bits & U_ANGLE3)
			MSG_WriteAngle(msg, ent->v.angles[2], sv.protocolflags);

		//johnfitz -- PROTOCOL_FITZQUAKE
		if (bits & U_ALPHA)
			MSG_WriteByte(msg, ent->alpha);
		if (bits & U_SCALE)
			MSG_WriteByte(msg, ent->scale);
		if (bits & U_FRAME2)
			MSG_WriteByte(msg, (int)ent->v.frame >> 8);
		if (bits & U_MODEL2)
			MSG_WriteByte(msg, (int)ent->v.modelindex >> 8);
		if (bits & U_LERPFINISH)
			MSG_WriteByte(msg, (byte)(Q_rint((ent->v.nextthink-qcvm->time)*255)));
		//johnfitz
	}

	//johnfitz -- devstats
stats:
	if (msg->cursize > 1024 && dev_peakstats.packetsize <= 1024)
		Con_DWarning ("%i byte packet exceeds standard limit of 1024 (max = %d).\n", msg->cursize, msg->maxsize);
	dev_stats.packetsize = msg->cursize;
	dev_peakstats.packetsize = q_max(msg->cursize, dev_peakstats.packetsize);
	//johnfitz
}

/*
=============
SV_CleanupEnts

=============
*/
void SV_CleanupEnts (void)
{
	int		e;
	edict_t	*ent;

	ent = NEXT_EDICT(qcvm->edicts);
	for (e=1 ; e<qcvm->num_edicts ; e++, ent = NEXT_EDICT(ent))
	{
		ent->v.effects = (int)ent->v.effects & ~EF_MUZZLEFLASH;
	}
}

/*
==================
SV_WriteDamageToMessage

==================
*/
static void SV_WriteDamageToMessage (edict_t *ent, sizebuf_t *msg)
{
	edict_t	*other;
	int		i;

	if (!ent->v.dmg_take && !ent->v.dmg_save)
		return;

	other = PROG_TO_EDICT(ent->v.dmg_inflictor);
	MSG_WriteByte (msg, svc_damage);
	MSG_WriteByte (msg, ent->v.dmg_save);
	MSG_WriteByte (msg, ent->v.dmg_take);
	for (i=0 ; i<3 ; i++)
		MSG_WriteCoord (msg, other->v.origin[i] + 0.5*(other->v.mins[i] + other->v.maxs[i]), sv.protocolflags );

	ent->v.dmg_take = 0;
	ent->v.dmg_save = 0;
}

/*
==================
SV_WriteClientdataToMessage

==================
*/
static void SV_WriteSetAngleToMessage (edict_t *ent, sizebuf_t *msg)
{
	int		i;

	if (!ent->v.fixangle)
		return;

	MSG_WriteByte (msg, svc_setangle);
	for (i=0 ; i < 3 ; i++)
		MSG_WriteAngle (msg, ent->v.angles[i], sv.protocolflags );
	ent->v.fixangle = 0;
}

void SV_WriteClientdataToMessage (edict_t *ent, sizebuf_t *msg)
{
	int		bits;
	int		i;
	unsigned int	items;
	unsigned int	weaponmodellimit;
	unsigned int	weaponmodelindex;
	eval_t	*val;
	const qboolean send_punchangle = !sv_nopunchangle.value;

	weaponmodellimit = host_client && host_client->limit_models ?
		host_client->limit_models : MAX_MODELS;
	weaponmodelindex = SV_ModelIndex(PR_GetString(ent->v.weaponmodel));
	if (weaponmodelindex >= weaponmodellimit)
		weaponmodelindex = 0;

//
// send a damage message
//
	SV_WriteDamageToMessage (ent, msg);

//
// send the current viewpos offset from the view entity
//
	SV_SetIdealPitch ();		// how much to look up / down ideally

// a fixangle might get lost in a dropped packet.  Oh well.
	SV_WriteSetAngleToMessage (ent, msg);

	bits = 0;

	if (ent->v.view_ofs[2] != DEFAULT_VIEWHEIGHT)
		bits |= SU_VIEWHEIGHT;

	if (ent->v.idealpitch)
		bits |= SU_IDEALPITCH;

// stuff the sigil bits into the high bits of items for sbar, or else
// mix in items2
	val = GetEdictFieldValueByName(ent, "items2");

	if (val)
		items = (unsigned int)ent->v.items | ((unsigned int)val->_float << 23);
	else
		items = (unsigned int)ent->v.items | ((unsigned int)pr_global_struct->serverflags << 28);

	bits |= SU_ITEMS;

	if ( (int)ent->v.flags & FL_ONGROUND)
		bits |= SU_ONGROUND;

	if ( ent->v.waterlevel >= 2)
		bits |= SU_INWATER;

	for (i=0 ; i<3 ; i++)
	{
		if (send_punchangle && ent->v.punchangle[i])
			bits |= (SU_PUNCH1<<i);
		if (ent->v.velocity[i])
			bits |= (SU_VELOCITY1<<i);
	}

	if (ent->v.weaponframe)
		bits |= SU_WEAPONFRAME;

	if (ent->v.armorvalue)
		bits |= SU_ARMOR;

//	if (ent->v.weapon)
	  bits |= SU_WEAPON;

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (sv.protocol != PROTOCOL_NETQUAKE)
	{
		if (bits & SU_WEAPON && weaponmodelindex & 0xFF00) bits |= SU_WEAPON2;
		if ((int)ent->v.armorvalue & 0xFF00) bits |= SU_ARMOR2;
		if ((int)ent->v.currentammo & 0xFF00) bits |= SU_AMMO2;
		if ((int)ent->v.ammo_shells & 0xFF00) bits |= SU_SHELLS2;
		if ((int)ent->v.ammo_nails & 0xFF00) bits |= SU_NAILS2;
		if ((int)ent->v.ammo_rockets & 0xFF00) bits |= SU_ROCKETS2;
		if ((int)ent->v.ammo_cells & 0xFF00) bits |= SU_CELLS2;
		if (bits & SU_WEAPONFRAME && (int)ent->v.weaponframe & 0xFF00) bits |= SU_WEAPONFRAME2;
		if (bits & SU_WEAPON && ent->alpha != ENTALPHA_DEFAULT) bits |= SU_WEAPONALPHA; //for now, weaponalpha = client entity alpha
		if (bits >= 65536) bits |= SU_EXTEND1;
		if (bits >= 16777216) bits |= SU_EXTEND2;
	}
	//johnfitz

// send the data

	MSG_WriteByte (msg, svc_clientdata);
	MSG_WriteShort (msg, bits);

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (bits & SU_EXTEND1) MSG_WriteByte(msg, bits>>16);
	if (bits & SU_EXTEND2) MSG_WriteByte(msg, bits>>24);
	//johnfitz

	if (bits & SU_VIEWHEIGHT)
		MSG_WriteChar (msg, ent->v.view_ofs[2]);

	if (bits & SU_IDEALPITCH)
		MSG_WriteChar (msg, ent->v.idealpitch);

	for (i=0 ; i<3 ; i++)
	{
		if (bits & (SU_PUNCH1<<i))
			MSG_WriteChar (msg, ent->v.punchangle[i]);
		if (bits & (SU_VELOCITY1<<i))
			MSG_WriteChar (msg, ent->v.velocity[i]/16);
	}

// [always sent]	if (bits & SU_ITEMS)
	MSG_WriteLong (msg, items);

	if (bits & SU_WEAPONFRAME)
		MSG_WriteByte (msg, ent->v.weaponframe);
	if (bits & SU_ARMOR)
		MSG_WriteByte (msg, ent->v.armorvalue);
	if (bits & SU_WEAPON)
		MSG_WriteByte (msg, weaponmodelindex);

	MSG_WriteShort (msg, ent->v.health);
	MSG_WriteByte (msg, ent->v.currentammo);
	MSG_WriteByte (msg, ent->v.ammo_shells);
	MSG_WriteByte (msg, ent->v.ammo_nails);
	MSG_WriteByte (msg, ent->v.ammo_rockets);
	MSG_WriteByte (msg, ent->v.ammo_cells);

	if (standard_quake)
	{
		MSG_WriteByte (msg, ent->v.weapon);
	}
	else
	{
		int weapon = 0;
		for(i=0;i<32;i++)
		{
			if ( ((int)ent->v.weapon) & (1<<i) )
			{
				weapon = i;
				break;
			}
		}
		MSG_WriteByte (msg, weapon);
	}

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (bits & SU_WEAPON2)
		MSG_WriteByte (msg, weaponmodelindex >> 8);
	if (bits & SU_ARMOR2)
		MSG_WriteByte (msg, (int)ent->v.armorvalue >> 8);
	if (bits & SU_AMMO2)
		MSG_WriteByte (msg, (int)ent->v.currentammo >> 8);
	if (bits & SU_SHELLS2)
		MSG_WriteByte (msg, (int)ent->v.ammo_shells >> 8);
	if (bits & SU_NAILS2)
		MSG_WriteByte (msg, (int)ent->v.ammo_nails >> 8);
	if (bits & SU_ROCKETS2)
		MSG_WriteByte (msg, (int)ent->v.ammo_rockets >> 8);
	if (bits & SU_CELLS2)
		MSG_WriteByte (msg, (int)ent->v.ammo_cells >> 8);
	if (bits & SU_WEAPONFRAME2)
		MSG_WriteByte (msg, (int)ent->v.weaponframe >> 8);
	if (bits & SU_WEAPONALPHA)
		MSG_WriteByte (msg, ent->alpha); //for now, weaponalpha = client entity alpha
	//johnfitz
}

/*
=======================
SV_SendClientDatagram
=======================
*/
static void SV_WriteMoveAckPayloadToMessage(client_t *client, sizebuf_t *msg)
{
	int flags = 0;

	MSG_WriteShort (msg, client->lastmovemessage & 0xffff);
	if (!(client->protocol_pext2 & PEXT2_EXPLICITCMDMSEC))
		return;
	if (client->move_authority != MOVE_AUTHORITY_UNKNOWN)
		flags |= MOVEACK_FLAG_AUTHORITATIVE;
	if (client->move_prediction_allowed)
		flags |= MOVEACK_FLAG_PREDICTION_ALLOWED;
	if (client->move_discontinuity_reason != MOVEACK_DISCONTINUITY_NONE)
		flags |= MOVEACK_FLAG_DISCONTINUITY;
	MSG_WriteByte (msg, flags);
	MSG_WriteByte (msg, client->move_authority);
	MSG_WriteShort (msg, client->move_mode_epoch);
	MSG_WriteShort (msg, client->move_discontinuity_epoch);
	MSG_WriteByte (msg, client->move_discontinuity_reason);
}

static void SV_MaybePrintSnapshotSummary (client_t *client, int client_index)
{
	double interval;
	int avg_packets;
	qboolean replacement;
	int sequence;
	int ack;
	int acklag;
	double ackage;

	if (!net_lagdebug.value || client_index < 0)
		return;
	interval = sv_netdiag_interval.value;
	if (interval <= 0 || realtime - client->net_snapshot_last_summary_time < interval)
		return;

	replacement = SV_UsesReplacementDeltas (client);
	sequence = replacement ? SV_ReplacementLastSentSequence (client) :
		client->net_snapshot_sequence;
	ack = replacement ? (client->lastacksequence >= 0 ?
		client->lastacksequence : -1) : client->net_snapshot_ack;
	acklag = replacement ? SV_ReplacementAckLag (client, sequence) :
		client->net_snapshot_ack_lag_max;
	ackage = client->net_snapshot_last_ack_time > 0 ?
		realtime - client->net_snapshot_last_ack_time : -1;
	if (ackage > client->net_snapshot_ack_age_max)
		client->net_snapshot_ack_age_max = ackage;
	avg_packets = client->net_snapshot_updates_sent ?
		(client->net_snapshot_split_packets + client->net_snapshot_updates_sent) /
			client->net_snapshot_updates_sent : 0;
	Con_Printf ("net_lagdebug: server summary to %s (%s): replacement=%d updates=%d last_packets=%d avg_packets=%d max_packets=%d last_bytes=%d max_bytes=%d seq=%d ack=%d acklag=%d ackage=%.3f max_ackage=%.3f clipped=%d authority=%d prediction_allowed=%d mode_epoch=%u discontinuity_epoch=%u queue=%u accepted_msec=%llu simulated_msec=%llu overflows=%d outliers=%d\n",
		client->name, NET_QSocketGetAddressString(client->netconnection),
		replacement ? 1 : 0,
		client->net_snapshot_updates_sent, client->net_snapshot_last_packets,
		avg_packets, client->net_snapshot_max_packets,
		client->net_snapshot_last_bytes, client->net_snapshot_max_bytes,
		sequence, ack, acklag, ackage, client->net_snapshot_ack_age_max,
		client->net_snapshot_unsent_entities, client->move_authority,
		client->move_prediction_allowed ? 1 : 0, client->move_mode_epoch,
		client->move_discontinuity_epoch, client->move_queue_count,
		client->net_move_msec_accepted, client->net_move_msec_simulated,
		client->net_move_queue_overflows, client->net_move_roomscale_outliers);
	client->net_snapshot_last_summary_time = realtime;
}

static int SV_ProtocolCoordSize (void);

static int SV_ParticleSize (const byte *buf)
{
	int coord_size;

	if (buf[0] != svc_particle)
		return 0;
	coord_size = SV_ProtocolCoordSize ();
	return 6 + 3 * coord_size;
}

static int SV_ProtocolCoordSize (void)
{
	if (sv.protocolflags & PRFL_24BITCOORD)
		return 3;
	if (sv.protocolflags & (PRFL_FLOATCOORD | PRFL_INT32COORD))
		return 4;
	return 2;
}

static void SVFTE_ClearDatagramMessage (sizebuf_t *msg)
{
	SZ_Clear (msg);
	msg->overflowed = false;
}

static qboolean SVFTE_SendBufferedDatagram (client_t *client, sizebuf_t *msg,
	int *packet_count, int *total_bytes, int *max_packet_bytes)
{
	if (!msg->cursize)
		return true;
	if (NET_SendUnreliableMessage (client->netconnection, msg) == -1)
	{
		SV_DropClient (true);
		return false;
	}
	(*packet_count)++;
	*total_bytes += msg->cursize;
	if (msg->cursize > *max_packet_bytes)
		*max_packet_bytes = msg->cursize;
	client->net_snapshot_packets_sent++;
	SVFTE_ClearDatagramMessage (msg);
	return true;
}

static int SVFTE_AppendPrivateDatagram (client_t *client, sizebuf_t *msg,
	int *packet_count, int *total_bytes, int *max_packet_bytes)
{
	int written;

	if (!client->datagram.cursize)
		return 0;
	if (client->datagram.overflowed)
	{
		SZ_Clear (&client->datagram);
		return 0;
	}
	if (client->datagram.cursize >= msg->maxsize)
	{
		Con_DPrintf ("SVFTE_AppendPrivateDatagram: dropping oversized private datagram for %s (%d bytes, max %d)\n",
			client->name, client->datagram.cursize, msg->maxsize);
		SZ_Clear (&client->datagram);
		return 0;
	}
	if (msg->cursize + client->datagram.cursize >= msg->maxsize)
	{
		if (!SVFTE_SendBufferedDatagram (client, msg, packet_count,
				total_bytes, max_packet_bytes))
			return -1;
	}

	written = client->datagram.cursize;
	SZ_Write (msg, client->datagram.data, client->datagram.cursize);
	SZ_Clear (&client->datagram);
	return written;
}

static int SVFTE_AppendServerDatagram (client_t *client, sizebuf_t *msg,
	int *packet_count, int *total_bytes, int *max_packet_bytes)
{
	int position, size, remaining, written;

	if (!sv.datagram.cursize)
		return 0;

	if (msg->cursize + sv.datagram.cursize < msg->maxsize)
	{
		SZ_Write (msg, sv.datagram.data, sv.datagram.cursize);
		return sv.datagram.cursize;
	}

	/*
	 * Match QSS-M: split only a leading svc_particle run across packets.
	 * The rest of the server datagram is kept whole or dropped for this
	 * unreliable frame instead of producing an unbounded burst.
	 */
	position = 0;
	written = 0;
	while (position < sv.datagram.cursize &&
		(size = SV_ParticleSize (&sv.datagram.data[position])) != 0)
	{
		if (msg->cursize + size < msg->maxsize)
		{
			SZ_Write (msg, &sv.datagram.data[position], size);
			position += size;
			written += size;
		}
		else
		{
			if (!SVFTE_SendBufferedDatagram (client, msg, packet_count,
					total_bytes, max_packet_bytes))
				return -1;
		}
	}

	remaining = sv.datagram.cursize - position;
	if (!remaining)
		return written;
	if (msg->cursize + remaining < msg->maxsize)
	{
		SZ_Write (msg, &sv.datagram.data[position], remaining);
		written += remaining;
	}
	else if (remaining < msg->maxsize)
	{
		if (!SVFTE_SendBufferedDatagram (client, msg, packet_count,
				total_bytes, max_packet_bytes))
			return -1;
		SZ_Write (msg, &sv.datagram.data[position], remaining);
		written += remaining;
	}
	else
	{
		Con_DPrintf ("SVFTE_AppendServerDatagram: dropping oversized server datagram tail (%d bytes, max %d)\n",
			remaining, msg->maxsize);
	}

	return written;
}

static qboolean SVFTE_SendClientDatagram (client_t *client, int maxsize)
{
	byte		buf[MAX_DATAGRAM];
	sizebuf_t	msg;
	int			packet_count;
	int			total_bytes;
	int			max_packet_bytes;
	int			client_index;
	size_t		prev_resume;
	int			private_datagram_initial;
	int			private_datagram_written;
	int			global_datagram_written;
	int			append_result;
	double		update_gap;
	size_t		prev_csqc_pending;
	size_t		csqc_pending;
	size_t		prev_entity_pending;
	size_t		entity_pending;
	size_t		entity_pending_before;
	size_t		entity_pending_after;
	int			max_replacement_packets;
	struct deltaframe_s	*replacement_frame;
	int			replacement_sequence;
	qboolean	made_progress;
	qboolean	replacement_packet_cap_hit;
	qboolean	no_progress_retry_used;
	static double	last_gap_log[MAX_SCOREBOARD];
	static double	last_update_sent[MAX_SCOREBOARD];
	static double	last_update_log[MAX_SCOREBOARD];
	static double	last_cap_log[MAX_SCOREBOARD];
	static struct qsocket_s	*last_update_socket[MAX_SCOREBOARD];

	if (!client->spawned)
		return true;

	if (!client->pendingentities_bits || !client->frames)
		SVFTE_SetupFrames (client);

	client_index = (int)(client - svs.clients);
	if (client_index < 0 || client_index >= MAX_SCOREBOARD)
		client_index = -1;
	if (client_index >= 0 && last_update_socket[client_index] != client->netconnection)
	{
		last_update_socket[client_index] = client->netconnection;
		last_update_sent[client_index] = 0;
		last_update_log[client_index] = 0;
		last_gap_log[client_index] = 0;
		last_cap_log[client_index] = 0;
	}
	if (net_lagdebug.value && client_index >= 0 && last_update_sent[client_index] > 0)
	{
		update_gap = realtime - last_update_sent[client_index];
		if (update_gap > SV_NetLagDebugFrameThreshold () &&
			realtime - last_gap_log[client_index] > 0.5)
		{
			Con_Printf ("net_lagdebug: server replacement-delta update gap to %s (%s): %.3f sec host_dt=%.3f sv_time=%.3f\n",
				client->name, NET_QSocketGetAddressString(client->netconnection),
				update_gap, host_frametime, qcvm->time);
			last_gap_log[client_index] = realtime;
		}
	}

	if (client->snapshotresume >= client->numpendingentities ||
		!SVFTE_CountPendingEntityDeltasFrom (client, client->snapshotresume))
		client->snapshotresume = 0;

	packet_count = 0;
	total_bytes = 0;
	max_packet_bytes = 0;
	entity_pending_before = SVFTE_CountPendingEntityDeltas (client);
	entity_pending = SVFTE_CountPendingEntityDeltasFrom (client, client->snapshotresume);
	csqc_pending = SVFTE_CountPendingCSQCEntities (client);
	private_datagram_initial = client->datagram.cursize;
	private_datagram_written = 0;
	global_datagram_written = 0;
	max_replacement_packets = SVFTE_ReplacementMaxPacketsPerFrame ();
	replacement_packet_cap_hit = false;
	no_progress_retry_used = false;

	msg.data = buf;
	msg.maxsize = maxsize;
	msg.cursize = 0;
	msg.allowoverflow = false;
	msg.overflowed = false;

	for (;;)
	{
		replacement_sequence = NET_QSocketGetSequenceOut (client->netconnection);
		client->net_snapshot_sequence = replacement_sequence;
		replacement_frame = SVFTE_BeginFrame (client,
			replacement_sequence);

		if (packet_count == 0)
		{
			SV_WriteDamageToMessage (client->edict, &msg);
			SV_WriteSetAngleToMessage (client->edict, &msg);
			SVFTE_WriteStatsToClient (client, &msg,
				replacement_frame);
		}

		prev_resume = client->snapshotresume;
		prev_csqc_pending = csqc_pending;
		prev_entity_pending = entity_pending;

		SVFTE_WriteEntitiesToClient (client, &msg,
			replacement_frame, replacement_sequence,
			no_progress_retry_used);
		SVFTE_WriteCSQCEntitiesToClient (client, &msg,
			replacement_frame, no_progress_retry_used);
		entity_pending = SVFTE_CountPendingEntityDeltasFrom (client, client->snapshotresume);
		csqc_pending = SVFTE_CountPendingCSQCEntities (client);

		if (msg.overflowed)
		{
			Con_Printf ("SVFTE_SendClientDatagram: packet overflow for %s\n", client->name);
			return true;
		}

		made_progress = client->snapshotresume != prev_resume ||
			entity_pending != prev_entity_pending ||
			csqc_pending != prev_csqc_pending;
		if (!entity_pending && !csqc_pending)
			break;

		if (!SVFTE_SendBufferedDatagram (client, &msg, &packet_count,
				&total_bytes, &max_packet_bytes))
			return false;

		if (!made_progress)
		{
			if (!no_progress_retry_used &&
				packet_count < max_replacement_packets)
			{
				no_progress_retry_used = true;
				continue;
			}
			Con_Printf ("SVFTE_SendClientDatagram: replacement packet made no progress for %s (%d byte packet, pending_ents=%zu pending_csqc=%zu)\n",
				client->name, msg.maxsize, entity_pending, csqc_pending);
			break;
		}
		if (packet_count >= max_replacement_packets)
		{
			replacement_packet_cap_hit = true;
			if (net_lagdebug.value &&
				(client_index < 0 ||
				 realtime - last_cap_log[client_index] > 1.0))
			{
				Con_Printf ("net_lagdebug: replacement packet cap hit for %s (%s): packets=%d pending_ents=%zu pending_csqc=%zu maxpacket=%d\n",
					client->name, NET_QSocketGetAddressString(client->netconnection),
					packet_count, entity_pending, csqc_pending,
					max_replacement_packets);
				if (client_index >= 0)
					last_cap_log[client_index] = realtime;
			}
			break;
		}
	}

	append_result = SVFTE_AppendPrivateDatagram (client, &msg, &packet_count,
		&total_bytes, &max_packet_bytes);
	if (append_result < 0)
		return false;
	private_datagram_written += append_result;

	append_result = SVFTE_AppendServerDatagram (client, &msg, &packet_count,
		&total_bytes, &max_packet_bytes);
	if (append_result < 0)
		return false;
	global_datagram_written += append_result;

	if (msg.overflowed)
	{
		Con_Printf ("SVFTE_SendClientDatagram: packet overflow for %s\n", client->name);
		return true;
	}

	if (!SVFTE_SendBufferedDatagram (client, &msg, &packet_count,
			&total_bytes, &max_packet_bytes))
		return false;

	if (packet_count > 1)
		client->net_snapshot_split_packets += packet_count - 1;
	client->net_snapshot_updates_sent++;
	client->net_snapshot_last_packets = packet_count;
	client->net_snapshot_last_bytes = total_bytes;
	if (packet_count > client->net_snapshot_max_packets)
		client->net_snapshot_max_packets = packet_count;
	if (total_bytes > client->net_snapshot_max_bytes)
		client->net_snapshot_max_bytes = total_bytes;
	client->net_snapshot_unsent_entities = (int)(entity_pending + csqc_pending);
	if (client_index >= 0)
		last_update_sent[client_index] = realtime;

	if (net_lagdebug.value &&
		(packet_count > 1 || max_packet_bytes > (maxsize * 9) / 10 ||
		 replacement_packet_cap_hit) &&
		(client_index < 0 || realtime - last_update_log[client_index] > 1.0))
	{
		int sequence = SV_ReplacementLastSentSequence (client);
		int ack = client->lastacksequence >= 0 ? client->lastacksequence : -1;
		entity_pending_after = SVFTE_CountPendingEntityDeltas (client);
		Con_Printf ("net_lagdebug: server replacement update to %s (%s): packets=%d bytes=%d max=%d ents=%zu/%zu pending=%zu->%zu svdg=%d/%d priv=%d/%d maxpacket=%d cap=%d seq=%d ack=%d acklag=%d\n",
			client->name, NET_QSocketGetAddressString(client->netconnection),
			packet_count, total_bytes, max_packet_bytes,
			(size_t)client->snapshotresume, client->numpendingentities,
			entity_pending_before, entity_pending_after,
			global_datagram_written, sv.datagram.cursize,
			private_datagram_written, private_datagram_initial, maxsize,
			max_replacement_packets,
			sequence, ack, SV_ReplacementAckLag (client, sequence));
		if (client_index >= 0)
			last_update_log[client_index] = realtime;
	}
	SV_MaybePrintSnapshotSummary (client, client_index);
	return true;
}

qboolean SV_SendClientDatagram (client_t *client)
{
	int		maxsize;

	if (!client || !client->netconnection)
	{
		if (client)
			SZ_Clear (&client->datagram);
		return true;
	}

	maxsize = SV_ClientUnreliableLimit (client);
	SV_UpdateClientMSS (client);
	return SVFTE_SendClientDatagram (client, maxsize);
}

/*
=======================
SV_WriteStats

TODO: group multiple stats in a single stuffcmd, the client already supports this
=======================
*/
void SV_WriteStats (client_t *client)
{
	int			statsi[MAX_CL_STATS];
	float		statsf[MAX_CL_STATS];
	const char	*statss[MAX_CL_STATS];
	int			i;

	SV_CalcStats (client, statsi, statsf, statss);

	for (i = 0; i < MAX_CL_STATS; i++)
	{
		//small cleanup
		if (!statsi[i])
			statsi[i] =	statsf[i];
		else
			statsf[i] =	0;//statsi[i];

		if (statsi[i] != client->oldstats_i[i] || statsf[i] != client->oldstats_f[i])
		{
			client->oldstats_i[i] = statsi[i];
			client->oldstats_f[i] = statsf[i];

			if ((double)statsi[i] != statsf[i] && statsf[i])
			{	//didn't round nicely, so send as a float
				MSG_WriteByte (&client->message, svc_stufftext);
				MSG_WriteString (&client->message, va ("//st %i %g\n", i, statsf[i]));
			}
			else
			{
				if (i < MAX_CL_BASE_STATS)
				{
					MSG_WriteByte (&client->message, svc_updatestat);
					MSG_WriteByte (&client->message, i);
					MSG_WriteLong (&client->message, statsi[i]);
				}
				else
				{
					MSG_WriteByte (&client->message, svc_stufftext);
					MSG_WriteString (&client->message, va ("//st %i %i\n", i, statsi[i]));
				}
			}
		}

		if (statss[i] || client->oldstats_s[i])
		{
			const char *os = client->oldstats_s[i];
			const char *ns = statss[i];
			if (!ns)	ns="";
			if (!os)	os="";
			if (strcmp(os,ns))
			{
				free(client->oldstats_s[i]);
				client->oldstats_s[i] = strdup(ns);

				MSG_WriteByte (&client->message, svc_stufftext);
				MSG_WriteString (&client->message, va ("//sts %i \"%s\"\n", i, ns));
			}
		}
	}
}

/*
=======================
SV_UpdateToReliableMessages
=======================
*/
void SV_UpdateToReliableMessages (void)
{
	int			i, j;
	client_t *client;

// check for changes to be sent over the reliable streams
	for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
	{
		if (host_client->old_frags != host_client->edict->v.frags)
		{
			for (j=0, client = svs.clients ; j<svs.maxclients ; j++, client++)
			{
				if (!client->knowntoqc)
					continue;
				MSG_WriteByte (&client->message, svc_updatefrags);
				MSG_WriteByte (&client->message, i);
				MSG_WriteShort (&client->message, host_client->edict->v.frags);
			}

			host_client->old_frags = host_client->edict->v.frags;
		}
	}

	for (j=0, client = svs.clients ; j<svs.maxclients ; j++, client++)
	{
		if (!client->active || !client->pextknown)
			continue;
		SZ_Write (&client->message, sv.reliable_datagram.data, sv.reliable_datagram.cursize);
	}

	SZ_Clear (&sv.reliable_datagram);
}


/*
=======================
SV_SendNop

Send a nop message without trashing or sending the accumulated client
message buffer
=======================
*/
void SV_SendNop (client_t *client)
{
	sizebuf_t	msg;
	byte		buf[4];

	msg.data = buf;
	msg.maxsize = sizeof(buf);
	msg.cursize = 0;

	MSG_WriteChar (&msg, svc_nop);

	if (NET_SendUnreliableMessage (client->netconnection, &msg) == -1)
		SV_DropClient (true);	// if the message couldn't send, kick off
	client->last_message = realtime;
}

static int SV_PrespawnWriteLimit (void)
{
	return q_max (0, host_client->message.maxsize - 128);
}

static qboolean SV_SendPrespawnModelPrecaches (void)
{
	// Model names stay in svc_serverinfo for now; QSS-M keeps this stage but
	// effectively disables it because the world model is needed during map load.
	return false;
}

static qboolean SV_SendPrespawnSoundPrecaches (void)
{
	unsigned int idx;
	int maxsize;

	idx = host_client->signon_sounds;
	maxsize = SV_PrespawnWriteLimit ();

	for (; idx < host_client->limit_sounds; idx++)
	{
		const char *name = sv.sound_precache[idx];
		if (!name)
			break;
		if (host_client->message.cursize + 4 + (int)strlen(name) > maxsize)
			break;
		MSG_WriteByte (&host_client->message, svcdp_precache);
		MSG_WriteShort (&host_client->message, 0x8000 | idx);
		MSG_WriteString (&host_client->message, name);
	}

	host_client->signon_sounds = idx;
	return idx < host_client->limit_sounds && sv.sound_precache[idx] != NULL;
}

static int SV_SendPrespawnParticlePrecaches (int idx)
{
	int maxsize;

	maxsize = SV_PrespawnWriteLimit ();

	for (;; idx++)
	{
		const char *name;
		if (idx >= MAX_PARTICLETYPES)
			return -1;
		name = sv.particle_precache[idx];
		if (!name)
			continue;
		if (host_client->message.cursize + 4 + (int)strlen(name) > maxsize)
			break;
		MSG_WriteByte (&host_client->message, svcdp_precache);
		MSG_WriteShort (&host_client->message, 0x4000 | idx);
		MSG_WriteString (&host_client->message, name);
	}

	return idx;
}

static int SV_SendPrespawnBaselines (int idx)
{
	edict_t *svent;
	int maxsize;

	maxsize = SV_PrespawnWriteLimit ();

	while (1)
	{
		if (idx >= qcvm->num_edicts)
			return -1;
		if (host_client->message.cursize > maxsize)
			break;

		svent = EDICT_NUM(idx);
		if (memcmp (&nullentitystate, &svent->baseline, sizeof(nullentitystate)))
			MSG_WriteStaticOrBaseLine (&host_client->message, idx,
				&svent->baseline, host_client->protocol_pext2,
				sv.protocolflags);
		idx++;
	}

	return idx;
}

static int SV_SendPrespawnStatics (int idx)
{
	entity_state_t *svent;
	int maxsize;

	maxsize = SV_PrespawnWriteLimit ();

	while (1)
	{
		if (idx >= sv.num_statics)
			return -1;
		if (host_client->message.cursize > maxsize)
			break;

		svent = &sv.static_entities[idx++];
		if (svent->modelindex >= host_client->limit_models)
			continue;
		if (memcmp (&nullentitystate, svent, sizeof(nullentitystate)))
			MSG_WriteStaticOrBaseLine (&host_client->message, -1, svent,
				host_client->protocol_pext2, sv.protocolflags);
	}

	return idx;
}

static int SV_SendAmbientSounds (int idx)
{
	struct ambientsound_s *snd;
	int maxsize;
	qboolean large;
	int i;

	maxsize = SV_PrespawnWriteLimit ();

	while (1)
	{
		if (idx >= sv.num_ambients)
			return -1;
		if (host_client->message.cursize > maxsize)
			break;

		snd = &sv.ambientsounds[idx++];
		if (snd->soundindex >= host_client->limit_sounds)
			continue;

		large = snd->soundindex > 255;
		MSG_WriteByte (&host_client->message,
			large ? svc_spawnstaticsound2 : svc_spawnstaticsound);
		for (i = 0; i < 3; i++)
			MSG_WriteCoord (&host_client->message, snd->origin[i], sv.protocolflags);
		if (large)
			MSG_WriteShort (&host_client->message, snd->soundindex);
		else
			MSG_WriteByte (&host_client->message, snd->soundindex);
		MSG_WriteByte (&host_client->message,
			(int)CLAMP (0.f, snd->volume * 255.f, 255.f));
		MSG_WriteByte (&host_client->message,
			(int)CLAMP (0.f, snd->attenuation * 64.f, 255.f));
	}

	return idx;
}

/*
=======================
SV_PresendClientDatagram

Build replacement-delta state for all clients before any client datagrams are
sent.  This matches QSS-M's send-frame ordering and keeps snapshots tied to one
server frame instead of each client's send side effects.
=======================
*/
static void SV_PresendClientDatagram (client_t *client)
{
	if (!client->netconnection)
		return;
	if (!client->spawned)
		return;
	if (!client->pendingentities_bits || !client->frames)
		SVFTE_SetupFrames (client);
	SVFTE_BuildSnapshotForClient (client);
	SVFTE_CalcEntityDeltas (client);
	client->snapshotresume = 0;
}

/*
=======================
SV_SendClientMessages
=======================
*/
void SV_SendClientMessages (void)
{
	int			i;

// update frags, names, etc
	SV_UpdateToReliableMessages ();
	SV_ExpireVRIKPoses ();

	for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
	{
		if (!host_client->active)
			continue;
		SV_PresendClientDatagram (host_client);
	}

// build individual updates
	for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
	{
		if (!host_client->active)
			continue;

		if (!SV_SendClientDatagram (host_client))
			continue;
		if (!host_client->spawned)
		{
		// the player isn't totally in the game yet
		// send small keepalive messages if too much time has passed
		// send a full message when the next signon stage has been requested
		// some other message data (name changes, etc) may accumulate
		// between signon stages
			if (!host_client->sendsignon)
			{
				if (realtime - host_client->last_message > 5)
					SV_SendNop (host_client);
				continue;	// don't send out non-signon messages
			}
			if (host_client->sendsignon == PRESPAWN_MODELS)
			{
				if (!SV_SendPrespawnModelPrecaches ())
				{
					host_client->signonidx = 0;
					host_client->sendsignon++;
				}
			}
			if (host_client->sendsignon == PRESPAWN_SOUNDS)
			{
				if (!SV_SendPrespawnSoundPrecaches ())
				{
					host_client->signonidx = 0;
					host_client->sendsignon++;
				}
			}
			if (host_client->sendsignon == PRESPAWN_PARTICLES)
			{
				host_client->signonidx = SV_SendPrespawnParticlePrecaches (host_client->signonidx);
				if (host_client->signonidx < 0)
				{
					host_client->signonidx = 0;
					host_client->sendsignon++;
				}
			}
			if (host_client->sendsignon == PRESPAWN_BASELINES)
			{
				host_client->signonidx = SV_SendPrespawnBaselines (host_client->signonidx);
				if (host_client->signonidx < 0)
				{
					host_client->signonidx = 0;
					host_client->sendsignon++;
				}
			}
			if (host_client->sendsignon == PRESPAWN_STATICS)
			{
				host_client->signonidx = SV_SendPrespawnStatics (host_client->signonidx);
				if (host_client->signonidx < 0)
				{
					host_client->signonidx = 0;
					host_client->sendsignon++;
				}
			}
			if (host_client->sendsignon == PRESPAWN_AMBIENTS)
			{
				host_client->signonidx = SV_SendAmbientSounds (host_client->signonidx);
				if (host_client->signonidx < 0)
				{
					host_client->signonidx = 0;
					host_client->sendsignon++;
				}
			}
			if (host_client->sendsignon == PRESPAWN_SIGNONBUFS)
			{
				qboolean local = SV_IsLocalClient (host_client);
				while (host_client->signonidx < sv.num_signon_buffers)
				{
					sizebuf_t *signon = sv.signon_buffers[host_client->signonidx];
					if (host_client->message.cursize + signon->cursize > host_client->message.maxsize)
						break;
					SZ_Write (&host_client->message, signon->data, signon->cursize);
					host_client->signonidx++;
					// only send multiple buffers at once when playing locally,
					// otherwise we send one signon at a time to avoid overflowing
					// the datagram buffer for clients using a lower limit (e.g. 32000 in QS)
					if (!local)
						break;
				}
				if (host_client->signonidx >= sv.num_signon_buffers)
					host_client->sendsignon = PRESPAWN_SIGNONMSG;
			}
			if (host_client->sendsignon == PRESPAWN_SIGNONMSG)
			{
				if (host_client->message.cursize + 2 < host_client->message.maxsize)
				{
					MSG_WriteByte (&host_client->message, svc_signonnum);
					MSG_WriteByte (&host_client->message, 2);
					host_client->sendsignon = PRESPAWN_FLUSH;
				}
			}
		}

		// check for an overflowed message.  Should only happen
		// on a very fucked up connection that backs up a lot, then
		// changes level
		if (host_client->message.overflowed)
		{
			SZ_Clear (&host_client->message);
			SV_DropClient (false);
			continue;
		}

		if (host_client->message.cursize || host_client->dropasap)
		{
			if (!NET_CanSendMessage (host_client->netconnection))
			{
//				I_Printf ("can't write\n");
				continue;
			}

			if (host_client->dropasap)
				SV_DropClient (false);	// went to another level
			else
			{
				if (NET_SendMessage (host_client->netconnection
				, &host_client->message) == -1)
					SV_DropClient (false);	// if the message couldn't send, kick off
				SZ_Clear (&host_client->message);
				host_client->last_message = realtime;
				if (host_client->sendsignon == PRESPAWN_FLUSH)
					host_client->sendsignon = PRESPAWN_DONE;
			}
		}
	}


	if (GetEdictFieldValid(SendFlags))
	{
		edict_t *ent;

		for (i = 1, ent = NEXT_EDICT(qcvm->edicts); i < qcvm->num_edicts; i++, ent = NEXT_EDICT(ent))
			if (!ent->free)
				GetEdictFieldEval(ent, SendFlags)->_float = 0;
	}

// clear muzzle flashes
	SV_CleanupEnts ();
}


/*
==============================================================================

SERVER SPAWNING

==============================================================================
*/

#define SIGNON_SIZE		31500 // QS has a MAX_DATAGRAM of 32000, try to play nice

/*
================
SV_AddSignonBuffer
================
*/
static void SV_AddSignonBuffer (void)
{
	sizebuf_t *sb;
	if (sv.num_signon_buffers >= MAX_SIGNON_BUFFERS)
		Host_Error ("SV_AddSignonBuffer overflow\n");

	sb = (sizebuf_t *) Hunk_AllocName (sizeof (sizebuf_t) + SIGNON_SIZE, "signon");
	sb->data = (byte *)(sb + 1);
	sb->maxsize = SIGNON_SIZE;
	sv.signon_buffers[sv.num_signon_buffers++] = sb;
	sv.signon = sb;
}

/*
================
SV_ReserveSignonSpace
================
*/
void SV_ReserveSignonSpace (int numbytes)
{
	if (sv.signon->cursize + numbytes > sv.signon->maxsize)
		SV_AddSignonBuffer ();
}

/*
================
SV_ModelIndex

================
*/
int SV_ModelIndex (const char *name)
{
	int		i;

	if (!name || !name[0])
		return 0;

	for (i=0 ; i<MAX_MODELS && sv.model_precache[i] ; i++)
		if (!strcmp(sv.model_precache[i], name))
			return i;
	if (i==MAX_MODELS || !sv.model_precache[i])
		Sys_Error ("SV_ModelIndex: model %s not precached", name);
	return i;
}

/*
================
SV_CreateBaseline
================
*/
void SV_CreateBaseline (void)
{
	edict_t		*svent;
	int			entnum;

	for (entnum = 0; entnum < qcvm->num_edicts ; entnum++)
	{
	// get the current server version
		svent = EDICT_NUM(entnum);
		if (svent->free)
			continue;
		if (entnum > svs.maxclients && !svent->v.modelindex)
			continue;

	//
	// create entity baseline
	//
		svent->baseline = nullentitystate;
		VectorCopy (svent->v.origin, svent->baseline.origin);
		VectorCopy (svent->v.angles, svent->baseline.angles);
		svent->baseline.frame = svent->v.frame;
		svent->baseline.skin = svent->v.skin;
		if (entnum > 0 && entnum <= svs.maxclients)
		{
			svent->baseline.colormap = entnum;
			svent->baseline.modelindex = SV_ModelIndex("progs/player.mdl");
			svent->baseline.alpha = ENTALPHA_DEFAULT; //johnfitz -- alpha support
		}
		else
		{
			svent->baseline.colormap = 0;
			svent->baseline.modelindex = SV_ModelIndex(PR_GetString(svent->v.model));
			svent->baseline.alpha = svent->alpha; //johnfitz -- alpha support
		}
	}
}


/*
================
SV_SendReconnect

Tell all the clients that the server is changing levels
================
*/
void SV_SendReconnect (void)
{
	byte	data[128];
	sizebuf_t	msg;

	msg.data = data;
	msg.cursize = 0;
	msg.maxsize = sizeof(data);

	MSG_WriteChar (&msg, svc_stufftext);
	MSG_WriteString (&msg, "reconnect\n");
	NET_SendToAll (&msg, 5.0);

	if (!isDedicated)
		Cmd_ExecuteString ("reconnect\n", src_command);
}


/*
================
SV_SaveSpawnparms

Grabs the current state of each client for saving across the
transition to another level
================
*/
void SV_SaveSpawnparms (void)
{
	int		i, j;
	float		saved_health, saved_deadflag;
	qboolean	preserve_dead_inventory;

	svs.serverflags = pr_global_struct->serverflags;

	for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
	{
		if (!host_client->active)
			continue;

	// call the progs to get default spawn parms for the new client
		preserve_dead_inventory =
			SV_CoopRespawnPrepareChangelevel(host_client->edict);
		saved_health = host_client->edict->v.health;
		saved_deadflag = host_client->edict->v.deadflag;
		if (preserve_dead_inventory)
		{
			/* Most mods intentionally give dead players SetNewParms.  Present
			 * the cached inventory as alive only while SetChangeParms encodes
			 * it, so co-op map transitions do not strip that player's weapons. */
			host_client->edict->v.health = 1;
			host_client->edict->v.deadflag = DEAD_NO;
		}
		pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
		PR_ExecuteProgram (pr_global_struct->SetChangeParms);
		if (preserve_dead_inventory)
		{
			host_client->edict->v.health = saved_health;
			host_client->edict->v.deadflag = saved_deadflag;
		}
		for (j=0 ; j<NUM_SPAWN_PARMS ; j++)
			host_client->spawn_parms[j] = (&pr_global_struct->parm1)[j];
		SV_MG3UpgradeSyncSpawnParms(host_client->spawn_parms);
	}
}

typedef enum
{
	MAPCHECK_FAILED,
	MAPCHECK_PARTIAL,
	MAPCHECK_OK,
} mapcheck_t;

static mapcheck_t SV_MapCheckThresh (int current, int target)
{
	if (current <= 0)
		return MAPCHECK_FAILED;
	if (current >= target)
		return MAPCHECK_OK;
	return MAPCHECK_PARTIAL;
}

static void SV_PrintMapCheck (mapcheck_t status, const char *format, ...)
{
	char str[1024];
	va_list argptr;

	va_start (argptr, format);
	q_vsnprintf (str, sizeof (str), format, argptr);
	va_end (argptr);

	if (status == MAPCHECK_OK)
		Con_SafePrintf ("[x] %s\n", str);
	else
	{
		Con_SafePrintf ("[%c] %s\n", status == MAPCHECK_PARTIAL ? '-' : ' ', str);
		sv.mapchecks.warnings++;
	}
}

static void SV_PrintMapChecklist (void)
{
	const int min_dm_spawn_points = 5;
	const int min_coop_spawn_points = 3;
	const char *title;
	qboolean skill_levels;
	int i, track, numskies, nonstandard_skies;

	Con_SafePrintf ("\n=====================================\n\n");
	Con_SafePrintf ("Map checklist (%s):\n\n", COM_SkipPath (sv.modelname));

	SV_PrintMapCheck (sv.worldmodel->lightdata ? MAPCHECK_OK : MAPCHECK_FAILED,
		"lightmap data");
	if (!sv.worldmodel->visdata)
	{
		char pointfile[MAX_OSPATH];
		q_snprintf (pointfile, sizeof (pointfile), "maps/%s.pts", sv.name);
		SV_PrintMapCheck (MAPCHECK_FAILED, COM_FileExists (pointfile, NULL) ?
			"vis data (unsealed map?)" : "vis data");
	}
	else
		SV_PrintMapCheck (MAPCHECK_OK, "vis data");

	if (!sv.mapchecks.trigger_changelevel)
		SV_PrintMapCheck (MAPCHECK_FAILED, "trigger_changelevel");
	else if (sv.mapchecks.trigger_changelevel == 1)
	{
		if (sv.mapchecks.valid_changelevel == 1)
			SV_PrintMapCheck (MAPCHECK_OK, "trigger_changelevel (%s)", sv.mapchecks.changelevel);
		else
			SV_PrintMapCheck (MAPCHECK_PARTIAL, "trigger_changelevel (missing \"map\" key)");
	}
	else if (sv.mapchecks.valid_changelevel == sv.mapchecks.trigger_changelevel)
		SV_PrintMapCheck (MAPCHECK_OK, "trigger_changelevel (%d)", sv.mapchecks.trigger_changelevel);
	else
		SV_PrintMapCheck (MAPCHECK_PARTIAL, "trigger_changelevel (%d/%d missing \"map\" key)",
			sv.mapchecks.trigger_changelevel - sv.mapchecks.valid_changelevel,
			sv.mapchecks.trigger_changelevel);

	if (sv.mapchecks.intermission)
		SV_PrintMapCheck (MAPCHECK_OK, "info_intermission (%d)", sv.mapchecks.intermission);
	else
		SV_PrintMapCheck (MAPCHECK_FAILED, "info_intermission");

	skill_levels = sv.mapchecks.skill_triggers > 0 ||
		(sv.mapchecks.skill_ents[0] != sv.mapchecks.skill_ents[1] ||
		 sv.mapchecks.skill_ents[1] != sv.mapchecks.skill_ents[2]);
	SV_PrintMapCheck (skill_levels ? MAPCHECK_OK : MAPCHECK_FAILED,
		"skill spawnflags/triggers");
	SV_PrintMapCheck (SV_MapCheckThresh (sv.mapchecks.coop_spawns, min_coop_spawn_points),
		"info_player_coop (%d/%d+)", sv.mapchecks.coop_spawns, min_coop_spawn_points);
	SV_PrintMapCheck (SV_MapCheckThresh (sv.mapchecks.dm_spawns, min_dm_spawn_points),
		"info_player_deathmatch (%d/%d+)", sv.mapchecks.dm_spawns, min_dm_spawn_points);

	track = (int)qcvm->edicts->v.sounds;
	if (!track)
		SV_PrintMapCheck (MAPCHECK_FAILED, "music track (worldspawn \"sounds\" field)");
	else if (track < 2 || track > 255)
		SV_PrintMapCheck (MAPCHECK_FAILED, "music track (%d, should be between 2 and 255)", track);
	else
		SV_PrintMapCheck (MAPCHECK_OK, "music track (%d)", track);

	title = COM_SkipSpace (PR_GetString ((int)qcvm->edicts->v.message));
	if (*title)
		SV_PrintMapCheck (MAPCHECK_OK, "map title (%s)", title);
	else
		SV_PrintMapCheck (MAPCHECK_FAILED, "map title (worldspawn \"message\" field)");

	numskies = nonstandard_skies = 0;
	for (i = 0; i < sv.worldmodel->numtextures; i++)
	{
		texture_t *tex = sv.worldmodel->textures[i];
		if (!tex || q_strncasecmp (tex->name, "sky", 3))
			continue;
		numskies++;
		if (tex->width != 256 || tex->height != 128)
			nonstandard_skies++;
	}
	if (numskies > 1 || nonstandard_skies)
	{
		SV_PrintMapCheck (MAPCHECK_FAILED, "compat: single %ssky texture (%d found)",
			nonstandard_skies ? "256 x 128 " : "", numskies);
		for (i = 0; i < sv.worldmodel->numtextures; i++)
		{
			texture_t *tex = sv.worldmodel->textures[i];
			if (!tex || q_strncasecmp (tex->name, "sky", 3))
				continue;
			Con_SafePrintf ("  %s (%d x %d)\n", tex->name, tex->width, tex->height);
		}
	}

	Con_SafePrintf ("\n=====================================\n\n");
}


/*
================
SV_SpawnServer

This is called at the start of each level
================
*/
extern float		scr_centertime_off;
void SV_SpawnServer (const char *server)
{
	static char	dummy[8] = { 0,0,0,0,0,0,0,0 };
	edict_t		*ent;
	int			i, signonsize;
	qcvm_t		*vm = qcvm;

	// let's not have any servers with no name
	if (hostname.string[0] == 0)
		Cvar_Set ("hostname", "UNNAMED");
	scr_centertime_off = 0;

	Con_DPrintf ("SpawnServer: %s\n",server);
	svs.changelevel_issued = false;		// now safe to issue another

	PR_SwitchQCVM(NULL);

//
// tell all connected clients that we are going to a new level
//
	if (sv.active)
	{
		SV_SendReconnect ();
	}

	/* File-static coop/VR state must never outlive the edicts for this map. */
	SV_ResetTransientClientState();
	SV_CoopSharedResetState();

//
// make cvars consistant
//
	if (coop.value)
		Cvar_Set ("deathmatch", "0");
	current_skill = (int)(skill.value + 0.5);
	if (current_skill < 0)
		current_skill = 0;
	if (current_skill > 3)
		current_skill = 3;

	Cvar_SetValue ("skill", (float)current_skill);

//
// set up the new server
//
	//memset (&sv, 0, sizeof(sv));
	Host_ClearMemory ();

	q_strlcpy (sv.name, server, sizeof(sv.name));
	sv.mapchecks.active = map_checks.value != 0.f;

	sv.protocol = sv_protocol; // johnfitz
	
	// Match QSS-M's active PEXT2/RMQ path: QC already uses float precision,
	// and float coords avoid fixed-point origin nudging during prediction.
	sv.protocolflags = PRFL_FLOATCOORD | PRFL_SHORTANGLE;

	PR_SwitchQCVM(vm);
// load progs to get entity field count
	PR_LoadProgs ("progs.dat", true);

// allocate server memory
	/* Host_ClearMemory() called above already cleared the whole sv structure */
	qcvm->max_edicts = CLAMP (MIN_EDICTS,(int)max_edicts.value,MAX_EDICTS); //johnfitz -- max_edicts cvar
	qcvm->edicts = (edict_t *) malloc (qcvm->max_edicts*qcvm->edict_size); // ericw -- sv.edicts switched to use malloc()
	if (!qcvm->edicts)
		Sys_Error ("SV_SpawnServer: out of memory (%d edicts x %d bytes)",
			qcvm->max_edicts, qcvm->edict_size);
	ClearLink (&qcvm->free_edicts);

	sv.datagram.maxsize = sizeof(sv.datagram_buf);
	sv.datagram.cursize = 0;
	sv.datagram.data = sv.datagram_buf;

	sv.multicast.maxsize = sizeof(sv.multicast_buf);
	sv.multicast.cursize = 0;
	sv.multicast.data = sv.multicast_buf;

	sv.reliable_datagram.maxsize = sizeof(sv.reliable_datagram_buf);
	sv.reliable_datagram.cursize = 0;
	sv.reliable_datagram.data = sv.reliable_datagram_buf;

	SV_AddSignonBuffer ();

// leave slots at start for clients only
	qcvm->num_edicts = svs.maxclients+1;
	memset(qcvm->edicts, 0, qcvm->num_edicts*qcvm->edict_size); // ericw -- sv.edicts switched to use malloc()
	for (i=0 ; i<svs.maxclients ; i++)
	{
		ent = EDICT_NUM(i+1);
		svs.clients[i].edict = ent;
		SV_ResetClientMoveState (&svs.clients[i]);
	}

	sv.state = ss_loading;
	sv.paused = false;
	sv.nomonsters = (nomonsters.value != 0.f);

	qcvm->time = 1.0;

	q_strlcpy (sv.name, server, sizeof(sv.name));
	q_snprintf (sv.modelname, sizeof(sv.modelname), "maps/%s.bsp", server);
	sv.worldmodel = Mod_ForName (sv.modelname, false);
	if (!sv.worldmodel)
	{
		Con_Printf ("Couldn't spawn server %s\n", sv.modelname);
		sv.active = false;
		return;
	}
	sv.models[1] = sv.worldmodel;

//
// clear world interaction links
//
	SV_ClearWorld ();

	sv.sound_precache[0] = dummy;
	sv.model_precache[0] = dummy;
	sv.model_precache[1] = sv.modelname;
	for (i=1 ; i<sv.worldmodel->numsubmodels ; i++)
	{
		sv.model_precache[1+i] = localmodels[i];
		sv.models[i+1] = Mod_ForName (localmodels[i], false);
	}

//
// load the rest of the entities
//
	ent = EDICT_NUM(0);
	memset (&ent->v, 0, qcvm->progs->entityfields * 4);
	ent->v.model = PR_SetEngineString(sv.worldmodel->name);
	ent->v.modelindex = 1;		// world model
	ent->v.solid = SOLID_BSP;
	ent->v.movetype = MOVETYPE_PUSH;

	if (coop.value)
		pr_global_struct->coop = coop.value;
	else
		pr_global_struct->deathmatch = deathmatch.value;

	pr_global_struct->mapname = PR_SetEngineString(sv.name);

// serverflags are for cross level information (sigils)
	pr_global_struct->serverflags = svs.serverflags;

	ED_LoadFromFile (sv.worldmodel->entities);

	sv.active = true;

// all setup is completed, any further precache statements are errors
	sv.state = ss_active;

// run two frames to allow everything to settle
	host_frametime = 0.1;
	SV_Physics (host_frametime);
	SV_Physics (host_frametime);

// create a baseline for more efficient communications
	SV_CreateBaseline ();

	//johnfitz -- warn if signon buffer larger than standard server can handle
	for (i = 0, signonsize = 0; i < sv.num_signon_buffers; i++)
		signonsize += sv.signon_buffers[i]->cursize;
	if (signonsize > 64000-2)
		Con_DWarning ("%i byte signon buffer exceeds QS limit of 63998.\n", signonsize);
	else if (signonsize > 8000-2) //max size that will fit into 8000-sized client->message buffer with 2 extra bytes on the end
		Con_DWarning ("%i byte signon buffer exceeds standard limit of 7998.\n", signonsize);
	//johnfitz

// send serverinfo to all connected clients
	for (i = 0; i < MAX_SCOREBOARD; i++)
		svs.coop_initial_spawn_client[i] = false;
	for (i=0,host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
		if (host_client->active)
		{
			if (i < MAX_SCOREBOARD)
				svs.coop_initial_spawn_client[i] = true;
			SV_SendServerinfo (host_client);
		}

	Con_DPrintf ("Server spawned.\n");

	if (sv.mapchecks.active)
		SV_PrintMapChecklist ();
}
