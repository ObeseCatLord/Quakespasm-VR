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

server_t	sv;
server_static_t	svs;

static char	localmodels[MAX_MODELS][8];	// inline model names for precache

int		sv_protocol = PROTOCOL_RMQ; //johnfitz

extern cvar_t nomonsters;
cvar_t sv_maxpacketsize = {"sv_maxpacketsize", "1200", CVAR_NONE}; // conservative UDP payload size, avoids common MTU fragmentation
cvar_t sv_snapshot_splits = {"sv_snapshot_splits", "0", CVAR_ARCHIVE};
cvar_t sv_snapshot_packetdup = {"sv_snapshot_packetdup", "0", CVAR_NONE};
cvar_t sv_snapshot_partresend = {"sv_snapshot_partresend", "1", CVAR_NONE};
cvar_t sv_snapshot_partresend_interval = {"sv_snapshot_partresend_interval", "0.04", CVAR_NONE};
cvar_t sv_netdiag_interval = {"sv_netdiag_interval", "5", CVAR_NONE};
// When SV_WriteEntitiesToClient overflows the per-client datagram, the entity
// that gets evicted is whichever the loop reached last. With sv_netsort=1
// (ironwail's heuristic) entities are sorted by distance-to-player and PVS
// orientation first, so when packets get clipped it's distant or behind-camera
// entities that drop, not your weapon hand or the player next to you.
cvar_t sv_netsort = {"sv_netsort", "1", CVAR_NONE};
cvar_t sv_coop_weapon_targetfix = {"sv_coop_weapon_targetfix", "1", CVAR_NONE};
cvar_t sv_coop_pickup_targetlog = {"sv_coop_pickup_targetlog", "0", CVAR_NONE};
cvar_t sv_coop_pickup_targetfix = {"sv_coop_pickup_targetfix", "0", CVAR_NONE};
cvar_t sv_coop_pickup_targetfix_classes = {"sv_coop_pickup_targetfix_classes", "", CVAR_NONE};
cvar_t sv_coop_ammo_respawn = {"sv_coop_ammo_respawn", "0", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_ammo_respawn_time = {"sv_coop_ammo_respawn_time", "30", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_progression_item_respawn = {"sv_coop_progression_item_respawn", "1", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_progression_item_respawn_classes = {"sv_coop_progression_item_respawn_classes", "item_jboots item_jboots_timed", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_revive = {"sv_coop_revive", "1", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_revive_health = {"sv_coop_revive_health", "25", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_revive_range = {"sv_coop_revive_range", "96", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_respawn_near_player = {"sv_coop_respawn_near_player", "1", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_respawn_keep_weapons_ammo = {"sv_coop_respawn_keep_weapons_ammo", "1", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_autosave = {"sv_coop_autosave", "1", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_autosave_slots = {"sv_coop_autosave_slots", "4", CVAR_NONE};
cvar_t sv_coop_autosave_min_interval = {"sv_coop_autosave_min_interval", "30", CVAR_NONE};
cvar_t sv_coop_autosave_kill_interval = {"sv_coop_autosave_kill_interval", "10", CVAR_NONE};
cvar_t sv_coop_trusted_clientmove = {"sv_coop_trusted_clientmove", "1", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_trusted_clientmove_maxdelta = {"sv_coop_trusted_clientmove_maxdelta", "96", CVAR_NONE};
cvar_t sv_coop_predictmove = {"sv_coop_predictmove", "1", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_vr_jump_velocity = {"sv_vr_jump_velocity", "300", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_skyroom_pvs = {"sv_skyroom_pvs", "1", CVAR_NONE};

//============================================================================

static void SVFTE_SetupFrames (client_t *client);
static qboolean SVFTE_SendClientDatagram (client_t *client, int maxsize);

static int SV_ClientMaxPacketSize (client_t *client)
{
	int maxsize;

	maxsize = MAX_DATAGRAM;
	if (client && client->netconnection &&
		Q_strcmp(NET_QSocketGetAddressString(client->netconnection), "LOCAL") != 0)
		maxsize = (int)sv_maxpacketsize.value;
	return CLAMP(512, maxsize, MAX_DATAGRAM);
}

static void SV_UpdateClientMSS (client_t *client)
{
	if (client && client->netconnection)
		NET_QSocketSetMSS (client->netconnection, SV_ClientMaxPacketSize(client));
}

void SV_CalcStats(client_t *client, int *statsi, float *statsf, const char **statss)
{
	size_t i;
	edict_t *ent = client->edict;
	//FIXME: string stats!
	int items;
	eval_t *val = GetEdictFieldValue(ent, qcvm->extfields.items2);
	if (val)
		items = (int)ent->v.items | ((int)val->_float << 23);
	else
		items = (int)ent->v.items | ((int)pr_global_struct->serverflags << 28);

	memset(statsi, 0, sizeof(*statsi)*MAX_CL_STATS);
	memset(statsf, 0, sizeof(*statsf)*MAX_CL_STATS);
	memset((void*)statss, 0, sizeof(*statss)*MAX_CL_STATS);
	statsf[STAT_HEALTH] = ent->v.health;
//	statsf[STAT_FRAGS] = ent->v.frags;	//obsolete
	statsi[STAT_WEAPON] = SV_ModelIndex(PR_GetString(ent->v.weaponmodel));
	//if ((unsigned int)statsi[STAT_WEAPON] >= client->limit_models)
	//	statsi[STAT_WEAPON] = 0;
	statsf[STAT_AMMO] = ent->v.currentammo;
	statsf[STAT_ARMOR] = ent->v.armorvalue;
	statsf[STAT_WEAPONFRAME] = ent->v.weaponframe;
	statsf[STAT_SHELLS] = ent->v.ammo_shells;
	statsf[STAT_NAILS] = ent->v.ammo_nails;
	statsf[STAT_ROCKETS] = ent->v.ammo_rockets;
	statsf[STAT_CELLS] = ent->v.ammo_cells;
	statsf[STAT_ACTIVEWEAPON] = ent->v.weapon;	//sent in a way that does NOT depend upon the current mod...
	statsi[STAT_ITEMS] = items;
	if (client->protocol_pext2 & PEXT2_PREDINFO)
	{
		statsf[STAT_VIEWHEIGHT] = ent->v.view_ofs[2];
		if (client->usingpmove)
			PMSV_SetMoveStats(ent, statsf, statsi);
	}

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
		if (i != PROTOCOL_NETQUAKE && i != PROTOCOL_FITZQUAKE && i != PROTOCOL_RMQ)
			Con_Printf ("sv_protocol must be %i or %i or %i\n", PROTOCOL_NETQUAKE, PROTOCOL_FITZQUAKE, PROTOCOL_RMQ);
		else
		{
			sv_protocol = i;
			if (sv.active)
				Con_Printf ("changes will not take effect until the next level load.\n");
		}
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
static void SV_NetDiag_f (void)
{
	int i;

	Con_Printf ("client netdiag: moves packets=%d cmds=%d dup_packets=%d last_cmds=%d ack=%d moveacks=%d staleacks=%d\n",
		cl.net_move_packets_sent, cl.net_move_cmds_sent,
		cl.net_move_dup_packets_sent, cl.net_move_last_packet_cmds,
		cl.ackedmovemessages, cl.net_move_acks, cl.net_move_stale_acks);
	Con_Printf ("client netdiag: snapshots seq=%d packets=%d drops=%d acks_sent=%d pred=%d movetype=%d flags=%d vel=(%.1f %.1f %.1f)\n",
		cl.net_snapshot_sequence, cl.net_snapshot_packets,
		cl.net_snapshot_drops, cl.net_snapshot_acks_sent,
		cl.predstate_valid, cl.predstate_movetype, cl.predstate_flags,
		cl.predstate_velocity[0], cl.predstate_velocity[1],
		cl.predstate_velocity[2]);
	Con_Printf ("client netdiag: snapshot parts dup=%d jumps=%d incomplete=%d reassembled=%d overruns=%d\n",
		cl.net_snapshot_duplicate_parts, cl.net_snapshot_part_jumps,
		cl.net_snapshot_incomplete, cl.net_snapshot_reassembled,
		cl.net_snapshot_interpolation_overruns);
	Con_Printf ("client netdiag: snapshot partial active=%d seq=%d last=%d mask=%08x/%08x/%08x/%08x partacks=%d out_of_order=%d\n",
		cl.net_snapshot_partial_active, cl.net_snapshot_partial_sequence,
		cl.net_snapshot_partial_last_part,
		cl.net_snapshot_partial_mask[0], cl.net_snapshot_partial_mask[1],
		cl.net_snapshot_partial_mask[2], cl.net_snapshot_partial_mask[3],
		cl.net_snapshot_part_resend_acks_sent,
		cl.net_snapshot_out_of_order_parts);
	Con_Printf ("client netdiag: prediction errors=%d last=%.2f max=%.2f smooth_seq=%d smooth_left=%.3f\n",
		cl.net_prediction_errors, cl.net_prediction_error_last,
		cl.net_prediction_error_max, cl.prediction_error_sequence,
		q_max (0.0, cl.prediction_error_time - realtime));

	if (!sv.active)
		return;

	for (i = 0; i < svs.maxclients; i++)
	{
		client_t *client = &svs.clients[i];
		if (!client->active)
			continue;
		Con_Printf ("server netdiag: #%d %s moves packets=%d cmds=%d accepted=%d simulated=%d stale=%d last=%d queued=%d maxqueue=%d bundle=%d maxbundle=%d gap=%d lastdt=%.3f\n",
			i + 1, client->name, client->net_move_packets_received,
			client->net_move_cmds_received, client->net_move_cmds_accepted,
			client->net_move_cmds_simulated, client->net_move_cmds_stale,
			client->lastmovemessage, client->move_queue_count,
			client->net_move_queue_max, client->net_move_last_bundle,
			client->net_move_bundle_max, client->net_move_last_gap,
			client->net_move_last_sim_seconds);
		Con_Printf ("server netdiag: #%d snapshots seq=%d ack=%d packets=%d split_packets=%d last_packets=%d max_packets=%d last_bytes=%d max_bytes=%d acklag_max=%d clipped_ents=%d part_resends=%d partial_seq=%d partial_last=%d\n",
			i + 1, client->net_snapshot_sequence, client->net_snapshot_ack,
			client->net_snapshot_packets_sent,
			client->net_snapshot_split_packets, client->net_snapshot_last_packets,
			client->net_snapshot_max_packets, client->net_snapshot_last_bytes,
			client->net_snapshot_max_bytes, client->net_snapshot_ack_lag_max,
			client->net_snapshot_unsent_entities,
			client->net_snapshot_part_resends,
			client->net_snapshot_partial_ack_seq,
			client->net_snapshot_partial_ack_last_part);
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
	extern	cvar_t	sv_freezenonclients;
	extern	cvar_t	sv_friction;
	extern	cvar_t	sv_edgefriction;
	extern	cvar_t	sv_stopspeed;
	extern	cvar_t	sv_maxspeed;
	extern	cvar_t	sv_accelerate;
	extern	cvar_t	sv_idealpitchscale;
	extern	cvar_t	sv_pmove;
	extern	cvar_t	sv_pmove_legacy;
	extern	cvar_t	sv_aim;
	extern	cvar_t	sv_altnoclip; //johnfitz
	extern	cvar_t	sv_gameplayfix_random;
	extern	cvar_t	sv_gameplayfix_elevators;
	extern	cvar_t	sv_inputtimeout;
	extern	cvar_t	sv_move_timeclamp;

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
	Cvar_RegisterVariable (&sv_pmove);
	Cvar_RegisterVariable (&sv_pmove_legacy);
	Cvar_RegisterVariable (&sv_aim);
	Cvar_RegisterVariable (&sv_nostep);
	Cvar_RegisterVariable (&sv_freezenonclients);
	Cvar_RegisterVariable (&pr_checkextension);
	Cvar_RegisterVariable (&sv_altnoclip); //johnfitz
	Cvar_RegisterVariable (&sv_gameplayfix_elevators);
	Cvar_RegisterVariable (&sv_gameplayfix_random);
	Cvar_RegisterVariable (&sv_inputtimeout);
	Cvar_RegisterVariable (&sv_move_timeclamp);
	Cvar_RegisterVariable (&sv_maxpacketsize);
	Cvar_RegisterVariable (&sv_snapshot_splits);
	Cvar_RegisterVariable (&sv_snapshot_packetdup);
	Cvar_RegisterVariable (&sv_snapshot_partresend);
	Cvar_RegisterVariable (&sv_snapshot_partresend_interval);
	Cvar_RegisterVariable (&sv_netdiag_interval);
	Cvar_RegisterVariable (&sv_coop_weapon_targetfix);
	Cvar_RegisterVariable (&sv_coop_pickup_targetlog);
	Cvar_RegisterVariable (&sv_coop_pickup_targetfix);
	Cvar_RegisterVariable (&sv_coop_pickup_targetfix_classes);
	Cvar_RegisterVariable (&sv_coop_ammo_respawn);
	Cvar_RegisterVariable (&sv_coop_ammo_respawn_time);
	Cvar_RegisterVariable (&sv_coop_progression_item_respawn);
	Cvar_RegisterVariable (&sv_coop_progression_item_respawn_classes);
	Cvar_RegisterVariable (&sv_coop_revive);
	Cvar_RegisterVariable (&sv_coop_revive_health);
	Cvar_RegisterVariable (&sv_coop_revive_range);
	Cvar_RegisterVariable (&sv_coop_respawn_near_player);
	Cvar_RegisterVariable (&sv_coop_respawn_keep_weapons_ammo);
	Cvar_RegisterVariable (&sv_coop_autosave);
	Cvar_RegisterVariable (&sv_coop_autosave_slots);
	Cvar_RegisterVariable (&sv_coop_autosave_min_interval);
	Cvar_RegisterVariable (&sv_coop_autosave_kill_interval);
	Cvar_RegisterVariable (&sv_coop_trusted_clientmove);
	Cvar_RegisterVariable (&sv_coop_trusted_clientmove_maxdelta);
	Cvar_RegisterVariable (&sv_coop_predictmove);
	Cvar_RegisterVariable (&sv_vr_jump_velocity);
	Cvar_RegisterVariable (&sv_skyroom_pvs);
	PM_Register ();
	Cvar_SetCallback (&sv_coop_ammo_respawn, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_ammo_respawn_time, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_progression_item_respawn, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_progression_item_respawn_classes, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_revive, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_revive_health, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_revive_range, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_respawn_near_player, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_respawn_keep_weapons_ammo, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_autosave, Host_Callback_Notify);
	Cvar_SetCallback (&sv_vr_jump_velocity, Host_Callback_Notify);
	Cvar_RegisterVariable (&vr_movement_instant_stop);
	Cvar_RegisterVariable (&sv_netsort); // ironwail-style entity priority sorting

	Cmd_AddCommand ("netdiag", SV_NetDiag_f);
	Cmd_AddCommand ("sv_protocol", &SV_Protocol_f); //johnfitz

	for (i=0 ; i<MAX_MODELS ; i++)
		sprintf (localmodels[i], "*%i", i);

	i = COM_CheckParm ("-protocol");
	if (i && i < com_argc - 1)
		sv_protocol = atoi (com_argv[i + 1]);
	switch (sv_protocol)
	{
	case PROTOCOL_NETQUAKE:
		p = "NetQuake";
		break;
	case PROTOCOL_FITZQUAKE:
		p = "FitzQuake";
		break;
	case PROTOCOL_RMQ:
		p = "RMQ";
		break;
	default:
		Sys_Error ("Bad protocol version request %i. Accepted values: %i, %i, %i.",
				sv_protocol, PROTOCOL_NETQUAKE, PROTOCOL_FITZQUAKE, PROTOCOL_RMQ);
		return; /* silence compiler */
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

	if (volume < 0 || volume > 255)
		Host_Error ("SV_StartSound: volume = %i", volume);

	if (attenuation < 0 || attenuation > 4)
		Host_Error ("SV_StartSound: attenuation = %f", attenuation);

	if (channel < 0 || channel > 7)
		Host_Error ("SV_StartSound: channel = %i", channel);

	if (sv.datagram.cursize > MAX_DATAGRAM-21)
		return;

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
	if (sound_num >= 256 || channel >= 8)
	{
		if (sv.protocol == PROTOCOL_NETQUAKE)
			return; //don't send any info protocol can't support
		field_mask |= SND_LARGESOUND;
	}
	//johnfitz

	if ((channel & 8) && ent >= 1 && ent <= svs.maxclients)
	{
		client_t *target = &svs.clients[ent - 1];
		if (!target->active || !target->spawned)
			return;
		if (target->datagram.cursize > target->datagram.maxsize - 21)
			return;

		MSG_WriteByte (&target->datagram, svc_sound);
		MSG_WriteByte (&target->datagram, field_mask);
		if (field_mask & SND_VOLUME)
			MSG_WriteByte (&target->datagram, volume);
		if (field_mask & SND_ATTENUATION)
			MSG_WriteByte (&target->datagram, attenuation*64);
		if (field_mask & SND_LARGEENTITY)
		{
			MSG_WriteShort (&target->datagram, ent);
			MSG_WriteByte (&target->datagram, channel & 7);
		}
		else
			MSG_WriteShort (&target->datagram, (ent<<3) | (channel & 7));
		if (field_mask & SND_LARGESOUND)
			MSG_WriteShort (&target->datagram, sound_num);
		else
			MSG_WriteByte (&target->datagram, sound_num);
		for (i = 0; i < 3; i++)
			MSG_WriteCoord (&target->datagram, entity->v.origin[i]+0.5*(entity->v.mins[i]+entity->v.maxs[i]), sv.protocolflags);
		return;
	}

	if (sv.datagram.cursize > MAX_DATAGRAM-21)
		return;

	MSG_WriteByte (&sv.datagram, svc_sound);
	MSG_WriteByte (&sv.datagram, field_mask);
	if (field_mask & SND_VOLUME)
		MSG_WriteByte (&sv.datagram, volume);
	if (field_mask & SND_ATTENUATION)
		MSG_WriteByte (&sv.datagram, attenuation*64);

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (field_mask & SND_LARGEENTITY)
	{
		MSG_WriteShort (&sv.datagram, ent);
		MSG_WriteByte (&sv.datagram, channel);
	}
	else
		MSG_WriteShort (&sv.datagram, (ent<<3) | channel);
	if (field_mask & SND_LARGESOUND)
		MSG_WriteShort (&sv.datagram, sound_num);
	else
		MSG_WriteByte (&sv.datagram, sound_num);
	//johnfitz

	for (i = 0; i < 3; i++)
		MSG_WriteCoord (&sv.datagram, entity->v.origin[i]+0.5*(entity->v.mins[i]+entity->v.maxs[i]), sv.protocolflags);
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

static qboolean SV_IsLocalClient (client_t *client)
{
	return Q_strcmp (NET_QSocketGetAddressString (client->netconnection), "LOCAL") == 0;
}

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

	client->protocol_pext1 = 0;
	client->protocol_pext2 = PEXT2_REPLACEMENTDELTAS | PEXT2_PREDINFO | PEXT2_NEWSIZEENCODING;
	SVFTE_SetupFrames (client);
	SV_UpdateClientMSS (client);

	MSG_WriteByte (&client->message, svc_print);
	sprintf (message, "%c\nFITZQUAKE %1.2f SERVER (%i CRC)\n", 2, FITZQUAKE_VERSION, qcvm->crc); //johnfitz -- include fitzquake version
	MSG_WriteString (&client->message,message);

	MSG_WriteByte (&client->message, svc_serverinfo);
	MSG_WriteLong (&client->message, sv.protocol); //johnfitz -- sv.protocol instead of PROTOCOL_VERSION
	
	if (sv.protocol == PROTOCOL_RMQ)
	{
		// mh - now send protocol flags so that the client knows the protocol features to expect
		MSG_WriteLong (&client->message, sv.protocolflags);
	}
	
	MSG_WriteByte (&client->message, svs.maxclients);

	if (!coop.value && deathmatch.value)
		MSG_WriteByte (&client->message, GAME_DEATHMATCH);
	else
		MSG_WriteByte (&client->message, GAME_COOP);

	MSG_WriteString (&client->message, PR_GetString(qcvm->edicts->v.message));

	//johnfitz -- only send the first 256 model and sound precaches if protocol is 15
	for (i = 1, s = sv.model_precache+1; *s; s++,i++)
		if (sv.protocol != PROTOCOL_NETQUAKE || i < 256)
			MSG_WriteString (&client->message, *s);
	MSG_WriteByte (&client->message, 0);

	for (i = 1, s = sv.sound_precache+1; *s; s++, i++)
		if (sv.protocol != PROTOCOL_NETQUAKE || i < 256)
			MSG_WriteString (&client->message, *s);
	MSG_WriteByte (&client->message, 0);
	//johnfitz

	for (i = 1, s = sv.particle_precache+1; i < MAX_PARTICLETYPES && *s; s++, i++)
	{
		MSG_WriteByte (&client->message, svcdp_precache);
		MSG_WriteShort (&client->message, 0x4000 | i);
		MSG_WriteString (&client->message, *s);
	}

// send music
	MSG_WriteByte (&client->message, svc_cdtrack);
	MSG_WriteByte (&client->message, qcvm->edicts->v.sounds);
	MSG_WriteByte (&client->message, qcvm->edicts->v.sounds);

	// set view
	MSG_WriteByte (&client->message, svc_setview);
	MSG_WriteShort (&client->message, NUM_FOR_EDICT(client->edict));

	if (coop.value && sv_coop_trusted_clientmove.value)
	{
		MSG_WriteByte (&client->message, svc_stufftext);
		MSG_WriteString (&client->message, "//cl_trustedmove_ack\n");
	}
	if (coop.value && sv_coop_predictmove.value)
	{
		MSG_WriteByte (&client->message, svc_stufftext);
		MSG_WriteString (&client->message, "//cl_moveext_ack\n");
	}
	MSG_WriteByte (&client->message, svc_stufftext);
	MSG_WriteString (&client->message, "//cl_moveext_ack\n");

	MSG_WriteByte (&client->message, svc_signonnum);
	MSG_WriteByte (&client->message, 1);

	client->sendsignon = true;
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
	float			spawn_parms[NUM_SPAWN_PARMS];
	qboolean		loaded_client;
	int				old_frags;
	int				colors;

	client = svs.clients + clientnum;
	if (clientnum >= 0 && clientnum < MAX_SCOREBOARD)
		svs.coop_initial_spawn_client[clientnum] = false;

	Con_DPrintf ("Client %s connected\n", NET_QSocketGetAddressString(client->netconnection));

	edictnum = clientnum+1;

	ent = EDICT_NUM(edictnum);

// set up the client_t
	netconnection = client->netconnection;

	loaded_client = sv.loadgame && !sv.loadgame_resumed &&
	    clientnum >= 0 && clientnum < MAX_SCOREBOARD &&
	    sv.loadgame_client_saved[clientnum] && sv.loadgame_client_edicts;
	if (loaded_client)
	{
		memcpy (spawn_parms, client->spawn_parms, sizeof(spawn_parms));
		old_frags = client->old_frags;
		colors = client->colors;
	}
	else
	{
		old_frags = 0;
		colors = 0;
	}
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
	client->datagram.maxsize = sizeof(client->datagram_buf);
	client->datagram.cursize = 0;
	client->datagram.allowoverflow = true;

	if (loaded_client)
	{
		memcpy (client->spawn_parms, spawn_parms, sizeof(spawn_parms));
		client->old_frags = old_frags;
		client->colors = colors;
	}
	else
	{
	// call the progs to get default spawn parms for the new client
		PR_ExecuteProgram (pr_global_struct->SetNewParms);
		for (i=0 ; i<NUM_SPAWN_PARMS ; i++)
			client->spawn_parms[i] = (&pr_global_struct->parm1)[i];
	}

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

	if (from ? from->pmovetype != to->pmovetype : to->pmovetype != 0)
		bits |= UFP_MOVETYPE;
	if (from ? (from->velocity[0] != to->velocity[0] ||
		from->velocity[1] != to->velocity[1]) :
		(to->velocity[0] || to->velocity[1]))
		bits |= UFP_VELOCITYXY;
	if (from ? from->velocity[2] != to->velocity[2] : to->velocity[2])
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
	if ((bits & UF_PREDINFO) && !(pext2 & PEXT2_PREDINFO))
	{
		if (bits & UF_ANGLESXZ)
		{
			MSG_WriteAngle16 (msg, state->angles[0], protocolflags);
			MSG_WriteAngle16 (msg, state->angles[2], protocolflags);
		}
		if (bits & UF_ANGLESY)
			MSG_WriteAngle16 (msg, state->angles[1], protocolflags);
	}
	else
	{
		if (bits & UF_ANGLESXZ)
		{
			MSG_WriteAngle (msg, state->angles[0], protocolflags);
			MSG_WriteAngle (msg, state->angles[2], protocolflags);
		}
		if (bits & UF_ANGLESY)
			MSG_WriteAngle (msg, state->angles[1], protocolflags);
	}
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
	client->snapshotresume = 0;
	client->snapshotnextdelta = 0;
	client->csqcsnapshotnextdelta = 0;
}

static void SVFTE_SetupFrames (client_t *client)
{
	size_t i;

	SVFTE_DestroyFrames (client);
	memset (client->oldstats_i, 0, sizeof(client->oldstats_i));
	memset (client->oldstats_f, 0, sizeof(client->oldstats_f));
	memset (client->resendstatsnum, 0, sizeof(client->resendstatsnum));
	memset (client->resendstatsstr, 0, sizeof(client->resendstatsstr));

	if (!(client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
		return;

	client->numframes = 64;
	client->frames = (struct deltaframe_s *)calloc (client->numframes, sizeof(*client->frames));
	if (!client->frames)
		Sys_Error ("SVFTE_SetupFrames: calloc frames failed");
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

	if (!client->numframes || !(client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
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
	state->effects = (int)ent->v.effects & qcvm->effects_mask;
	if (ent->v.movetype == MOVETYPE_STEP)
		state->eflags |= EFLAGS_STEP;
	if (client && client->edict == ent && client->usingpmove)
	{
		state->pmovetype = (int)ent->v.movetype & 63;
		if ((int)ent->v.flags & FL_ONGROUND)
			state->pmovetype |= 0x80;
		if (!((int)ent->v.flags & FL_JUMPRELEASED))
			state->pmovetype |= 0x40;
	}
	state->velocity[0] = CLAMP (-32768, Q_rint (ent->v.velocity[0] * 8.0f), 32767);
	state->velocity[1] = CLAMP (-32768, Q_rint (ent->v.velocity[1] * 8.0f), 32767);
	state->velocity[2] = CLAMP (-32768, Q_rint (ent->v.velocity[2] * 8.0f), 32767);

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
	entity_state_t state;
	eval_t *val;
	qboolean cancsqc, iscsqc;
	int emiteffect;

	snapshot_numents = 0;
	clent = client->edict;
	VectorAdd (clent->v.origin, clent->v.view_ofs, org);
	pvs = SV_FatPVS (org, sv.worldmodel);
	cancsqc = GetEdictFieldValid(SendEntity) && GetEdictFieldValid(SendFlags) && client->csqcactive;
	if (cancsqc)
		SVFTE_EnsurePendingCSQCEntityBits (client, qcvm->num_edicts + 1);
	if (sv_skyroom_pvs.value && sv.skyroom_pos_known)
	{
		vec3_t skyorg;
		VectorMA (sv.skyroom_pos, sv.skyroom_pos[3], org, skyorg);
		SV_AddToFatPVS (skyorg, sv.worldmodel->nodes, sv.worldmodel);
	}

	ent = NEXT_EDICT(qcvm->edicts);
	for (e = 1; e < qcvm->num_edicts; e++, ent = NEXT_EDICT(ent))
	{
		if (ent->free)
			goto invisible;
		iscsqc = false;
		if (cancsqc && (val = GetEdictFieldEval(ent, SendEntity)) && val->function)
			iscsqc = true;
		emiteffect = 0;
		if (GetEdictFieldValid(emiteffectnum) && (val = GetEdictFieldEval(ent, emiteffectnum)))
			emiteffect = (int)val->_float;
		if (ent != clent)
		{
			if ((!ent->v.modelindex || !PR_GetString(ent->v.model)[0]) && !emiteffect && !iscsqc)
				goto invisible;
			if (coop.value && e >= 1 && e <= svs.maxclients)
				goto visible;
			for (i = 0; i < ent->num_leafs; i++)
				if (pvs[ent->leafnums[i] >> 3] & (1 << (ent->leafnums[i] & 7)))
					break;
			if (i == ent->num_leafs && ent->num_leafs < MAX_ENT_LEAFS)
				goto invisible;
		}
	visible:
		if (iscsqc)
		{
			if (!(client->pendingcsqcentities_bits[e] & SENDFLAG_PRESENT))
				client->pendingcsqcentities_bits[e] |= SENDFLAG_USABLE;
			else
				client->pendingcsqcentities_bits[e] |= (int)GetEdictFieldEval(ent, SendFlags)->_float & SENDFLAG_USABLE;
			continue;
		}
		if (cancsqc && client->pendingcsqcentities_bits[e])
			client->pendingcsqcentities_bits[e] |= SENDFLAG_REMOVE;
		SVFTE_BuildEntityState (client, ent, &state);
		if (ent != clent && state.alpha == ENTALPHA_ZERO && !state.effects)
			continue;
		SVFTE_AppendSnapshotEntity (e, &state);
		continue;

	invisible:
		if (cancsqc && e < (int)client->numpendingcsqcentities &&
			client->pendingcsqcentities_bits[e])
			client->pendingcsqcentities_bits[e] |= SENDFLAG_REMOVE;
	}
}

static void SVFTE_CalcEntityDeltas (client_t *client)
{
	struct entity_num_state_s *olds, *news, *oldstop, *newstop;

	SVFTE_EnsurePendingEntityBits (client, qcvm->num_edicts + 1);
	if (client->pendingentities_bits[0] & UF_REMOVE)
	{
		client->numpreviousentities = 0;
		client->pendingentities_bits[0] = UF_REMOVE;
		client->snapshotnextdelta = 0;
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
	reserve = (client->protocol_pext2 & PEXT2_PREDINFO) ? 9 : 7;

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

static void SVFTE_WriteEntitiesToClient (client_t *client, sizebuf_t *msg)
{
	struct entity_num_state_s *state, *stateend;
	struct deltaframe_s *frame;
	unsigned int entbits, logbits, netbits;
	size_t entnum;
	size_t nextdelta;
	int sequence;
	int header_need;
	byte entbuf[MAX_DATAGRAM];
	sizebuf_t entmsg;

	sequence = NET_QSocketGetSequenceOut (client->netconnection);
	frame = SVFTE_BeginFrame (client, sequence);
	state = client->previousentities;
	stateend = state + client->numpreviousentities;
	nextdelta = client->snapshotnextdelta;
	if (nextdelta >= client->numpendingentities)
		nextdelta = 0;

	SVFTE_WriteStatsToClient (client, msg, frame);

	header_need = 1 + 4 + 2;
	if (client->protocol_pext2 & PEXT2_PREDINFO)
		header_need += 2;
	if (msg->cursize + header_need > msg->maxsize)
		return;

	MSG_WriteByte (msg, svcfte_updateentities);
	if (client->protocol_pext2 & PEXT2_PREDINFO)
		MSG_WriteShort (msg, client->lastmovemessage & 0xffff);
	MSG_WriteFloat (msg, qcvm->time);

	for (entnum = client->snapshotresume; entnum < client->numpendingentities; entnum++)
	{
		entbits = client->pendingentities_bits[entnum];
		if (!(entbits & ~UF_RESET2))
			continue;
		if (!client->snapshotresume && nextdelta &&
			entnum < nextdelta && entnum > (size_t)svs.maxclients)
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
					logbits = entbits & ~UF_RESET2;
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

		if (entmsg.overflowed || msg->cursize + entmsg.cursize + 2 > msg->maxsize)
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
	if (entnum >= client->numpendingentities)
		client->snapshotnextdelta = 0;
	else if (entnum > (size_t)svs.maxclients)
		client->snapshotnextdelta = entnum;
	dev_stats.packetsize = msg->cursize;
	dev_peakstats.packetsize = q_max (msg->cursize, dev_peakstats.packetsize);
}

static void SVFTE_WriteCSQCEntitiesToClient (client_t *client, sizebuf_t *msg)
{
	edict_t *ed;
	struct deltaframe_s *frame;
	unsigned int bits, originalbits, logbits;
	size_t entnum;
	size_t nextdelta;
	int sequence;
	qboolean wroteheader = false;
	qboolean candidate_has_header;
	byte entbuf[MAX_DATAGRAM];
	sizebuf_t entmsg;

	if (!client->csqcactive || !GetEdictFieldValid(SendEntity) || !GetEdictFieldValid(SendFlags))
		return;
	if (!client->pendingcsqcentities_bits)
		return;

	sequence = NET_QSocketGetSequenceOut (client->netconnection);
	frame = SVFTE_BeginFrame (client, sequence);
	nextdelta = client->csqcsnapshotnextdelta;
	if (nextdelta >= client->numpendingcsqcentities)
		nextdelta = 0;

	for (entnum = 1; entnum < client->numpendingcsqcentities; entnum++)
	{
		bits = client->pendingcsqcentities_bits[entnum];
		if (!(bits & ~SENDFLAG_PRESENT))
			continue;
		if (nextdelta && entnum < nextdelta && entnum > (size_t)svs.maxclients)
			continue;

		originalbits = bits;
		logbits = 0;
		candidate_has_header = false;
		entmsg.data = entbuf;
		entmsg.maxsize = sizeof(entbuf);
		entmsg.cursize = 0;
		entmsg.allowoverflow = true;
		entmsg.overflowed = false;

		if (bits & SENDFLAG_REMOVE)
		{
	sendremove:
			if (!wroteheader)
			{
				MSG_WriteByte (&entmsg, svcfte_csqcentities);
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
				pr_global_struct->self = EDICT_TO_PROG(ed);
				G_INT(OFS_PARM0) = EDICT_TO_PROG(client->edict);
				G_FLOAT(OFS_PARM1) = bits & SENDFLAG_USABLE;
				PR_ExecuteProgram(GetEdictFieldEval(ed, SendEntity)->function);

					if (G_FLOAT(OFS_RETURN))
					{
						if (!wroteheader)
						{
							MSG_WriteByte (&entmsg, svcfte_csqcentities);
							candidate_has_header = true;
						}
						if (entnum >= 0x4000)
						{
							MSG_WriteShort (&entmsg, 0x4000 | (entnum & 0x3fff));
							MSG_WriteByte (&entmsg, entnum >> 14);
						}
						else
							MSG_WriteShort (&entmsg, entnum);

						SZ_Write (&entmsg, sv.multicast.data, sv.multicast.cursize);
						logbits = bits;
						bits = SENDFLAG_PRESENT;
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
			msg->cursize + entmsg.cursize + 2 > msg->maxsize)
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
	if (entnum >= client->numpendingcsqcentities)
		client->csqcsnapshotnextdelta = 0;
	else if (entnum > (size_t)svs.maxclients)
		client->csqcsnapshotnextdelta = entnum;
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
SV_WriteClientdataToMessage

==================
*/
void SV_WriteClientdataToMessage (edict_t *ent, sizebuf_t *msg)
{
	int		bits;
	int		i;
	edict_t	*other;
	int		items;
	eval_t	*val;

//
// send a damage message
//
	if (ent->v.dmg_take || ent->v.dmg_save)
	{
		other = PROG_TO_EDICT(ent->v.dmg_inflictor);
		MSG_WriteByte (msg, svc_damage);
		MSG_WriteByte (msg, ent->v.dmg_save);
		MSG_WriteByte (msg, ent->v.dmg_take);
		for (i=0 ; i<3 ; i++)
			MSG_WriteCoord (msg, other->v.origin[i] + 0.5*(other->v.mins[i] + other->v.maxs[i]), sv.protocolflags );

		ent->v.dmg_take = 0;
		ent->v.dmg_save = 0;
	}

//
// send the current viewpos offset from the view entity
//
	SV_SetIdealPitch ();		// how much to look up / down ideally

// a fixangle might get lost in a dropped packet.  Oh well.
	if ( ent->v.fixangle )
	{
		MSG_WriteByte (msg, svc_setangle);
		for (i=0 ; i < 3 ; i++)
			MSG_WriteAngle (msg, ent->v.angles[i], sv.protocolflags );
		ent->v.fixangle = 0;
	}

	bits = 0;

	if (ent->v.view_ofs[2] != DEFAULT_VIEWHEIGHT)
		bits |= SU_VIEWHEIGHT;

	if (ent->v.idealpitch)
		bits |= SU_IDEALPITCH;

// stuff the sigil bits into the high bits of items for sbar, or else
// mix in items2
	val = GetEdictFieldValueByName(ent, "items2");

	if (val)
		items = (int)ent->v.items | ((int)val->_float << 23);
	else
		items = (int)ent->v.items | ((int)pr_global_struct->serverflags << 28);

	bits |= SU_ITEMS;

	if ( (int)ent->v.flags & FL_ONGROUND)
		bits |= SU_ONGROUND;

	if ( ent->v.waterlevel >= 2)
		bits |= SU_INWATER;

	for (i=0 ; i<3 ; i++)
	{
		if (ent->v.punchangle[i])
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
		if (bits & SU_WEAPON && SV_ModelIndex(PR_GetString(ent->v.weaponmodel)) & 0xFF00) bits |= SU_WEAPON2;
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
		MSG_WriteByte (msg, SV_ModelIndex(PR_GetString(ent->v.weaponmodel)));

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
		MSG_WriteByte (msg, SV_ModelIndex(PR_GetString(ent->v.weaponmodel)) >> 8);
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
static void SV_WriteMoveAckToMessage(client_t *client, sizebuf_t *msg)
{
	edict_t	*ent;
	int	i;
	int	flags;
	int	ival;

	MSG_WriteByte (msg, svc_moveack);
	MSG_WriteShort (msg, client->lastmovemessage & 0xffff);

	ent = client->edict;
	flags = 0;
	if (ent && !ent->free && client->usingpmove)
	{
		flags |= PREDINFO_VALID;
		if ((int)ent->v.flags & FL_ONGROUND)
			flags |= PREDINFO_ONGROUND;
		if ((int)ent->v.flags & FL_INWATER)
			flags |= PREDINFO_INWATER;
		if ((int)ent->v.flags & FL_WATERJUMP)
			flags |= PREDINFO_WATERJUMP;
		if ((int)ent->v.flags & FL_JUMPRELEASED)
			flags |= PREDINFO_JUMPRELEASED;
	}

	MSG_WriteByte (msg, flags);
	MSG_WriteByte (msg, (ent && client->usingpmove) ?
		(int)ent->v.movetype : MOVETYPE_NONE);
	for (i = 0; i < 3; i++)
	{
		ival = Q_rint (((ent && client->usingpmove) ?
			ent->v.velocity[i] : 0) * 8.0f);
		ival = CLAMP (-32768, ival, 32767);
		MSG_WriteShort (msg, ival);
	}
	for (i = 0; i < 3; i++)
	{
		ival = Q_rint (ent ? ent->v.mins[i] : 0);
		ival = CLAMP (-128, ival, 127);
		MSG_WriteChar (msg, ival);
	}
	for (i = 0; i < 3; i++)
	{
		ival = Q_rint (ent ? ent->v.maxs[i] : 0);
		ival = CLAMP (-128, ival, 127);
		MSG_WriteChar (msg, ival);
	}
}

static void SV_MaybePrintSnapshotSummary (client_t *client, int client_index)
{
	double interval;
	int avg_packets;

	if (!net_lagdebug.value || client_index < 0)
		return;
	interval = sv_netdiag_interval.value;
	if (interval <= 0 || realtime - client->net_snapshot_last_summary_time < interval)
		return;

	avg_packets = client->net_snapshot_updates_sent ?
		(client->net_snapshot_split_packets + client->net_snapshot_updates_sent) /
			client->net_snapshot_updates_sent : 0;
	Con_Printf ("net_lagdebug: server summary to %s (%s): updates=%d last_packets=%d avg_packets=%d max_packets=%d last_bytes=%d max_bytes=%d snap=%d ack=%d max_acklag=%d clipped=%d\n",
		client->name, NET_QSocketGetAddressString(client->netconnection),
		client->net_snapshot_updates_sent, client->net_snapshot_last_packets,
		avg_packets, client->net_snapshot_max_packets,
		client->net_snapshot_last_bytes, client->net_snapshot_max_bytes,
		client->net_snapshot_sequence, client->net_snapshot_ack,
		client->net_snapshot_ack_lag_max,
		client->net_snapshot_unsent_entities);
	client->net_snapshot_last_summary_time = realtime;
}

static qboolean SV_SnapshotAckMaskHasPart (client_t *client, int part)
{
	if (part < 0 || part >= SNAPSHOT_MAX_PARTS)
		return false;
	return (client->net_snapshot_partial_ack_mask[part >> 5] &
		(1u << (part & 31))) != 0;
}

static void SV_StoreSnapshotPart (client_t *client, int sequence, int part, sizebuf_t *msg)
{
	if (part == 0 || client->net_snapshot_resend_sequence != sequence)
	{
		client->net_snapshot_resend_sequence = sequence;
		client->net_snapshot_resend_parts = 0;
		Q_memset (client->net_snapshot_resend_part_len, 0,
			sizeof(client->net_snapshot_resend_part_len));
	}

	if (part < 0 || part >= SNAPSHOT_RESEND_MAX_PARTS ||
		msg->cursize > SNAPSHOT_RESEND_MAX_PACKET)
		return;

	client->net_snapshot_resend_part_len[part] = msg->cursize;
	Q_memcpy (client->net_snapshot_resend_part_data[part], msg->data,
		msg->cursize);
	if (part + 1 > client->net_snapshot_resend_parts)
		client->net_snapshot_resend_parts = part + 1;
}

static qboolean SV_MaybeResendSnapshotParts (client_t *client)
{
	sizebuf_t msg;
	double interval;
	int part;
	int parts;
	int sent;

	if (!sv_snapshot_partresend.value ||
		client->net_snapshot_partial_ack_seq != client->net_snapshot_resend_sequence ||
		client->net_snapshot_resend_parts <= 0)
		return true;

	if (client->net_snapshot_partial_ack_seq <= client->net_snapshot_ack)
	{
		client->net_snapshot_partial_ack_seq = -1;
		return true;
	}

	interval = q_max (0.0, sv_snapshot_partresend_interval.value);
	if (interval > 0 && realtime - client->net_snapshot_last_part_resend_time < interval)
		return true;

	parts = client->net_snapshot_resend_parts;
	if (client->net_snapshot_partial_ack_last_part != SNAPSHOT_PART_UNKNOWN &&
		client->net_snapshot_partial_ack_last_part + 1 < parts)
		parts = client->net_snapshot_partial_ack_last_part + 1;

	sent = 0;
	for (part = 0; part < parts; part++)
	{
		if (SV_SnapshotAckMaskHasPart (client, part) ||
			client->net_snapshot_resend_part_len[part] <= 0)
			continue;

		msg.data = client->net_snapshot_resend_part_data[part];
		msg.maxsize = client->net_snapshot_resend_part_len[part];
		msg.cursize = client->net_snapshot_resend_part_len[part];
		msg.allowoverflow = false;
		msg.overflowed = false;

		if (NET_SendUnreliableMessage (client->netconnection, &msg) == -1)
		{
			SV_DropClient (true);
			return false;
		}
		client->net_snapshot_packets_sent++;
		client->net_snapshot_part_resends++;
		sent++;
	}

	if (sent > 0)
	{
		client->net_snapshot_last_part_resend_time = realtime;
		if (net_lagdebug.value)
			Con_DPrintf ("net_lagdebug: resent %d snapshot parts to %s seq=%d mask=%08x/%08x/%08x/%08x last=%d\n",
				sent, client->name, client->net_snapshot_partial_ack_seq,
				client->net_snapshot_partial_ack_mask[0],
				client->net_snapshot_partial_ack_mask[1],
				client->net_snapshot_partial_ack_mask[2],
				client->net_snapshot_partial_ack_mask[3],
				client->net_snapshot_partial_ack_last_part);
	}

	return true;
}

static int SV_ParticleSize (const byte *buf)
{
	int coord_size = 2;

	if (buf[0] != svc_particle)
		return 0;
	if (sv.protocolflags & PRFL_24BITCOORD)
		coord_size = 3;
	else if (sv.protocolflags & (PRFL_FLOATCOORD | PRFL_INT32COORD))
		coord_size = 4;
	return 6 + 3 * coord_size;
}

static qboolean SVFTE_WriteDatagramToMessage (sizebuf_t *msg, int *offset)
{
	int position, size, remaining;
	qboolean wrote = false;

	position = *offset;
	while (position < sv.datagram.cursize &&
		(size = SV_ParticleSize (&sv.datagram.data[position])) != 0)
	{
		if (msg->cursize + size >= msg->maxsize)
			break;
		SZ_Write (msg, &sv.datagram.data[position], size);
		position += size;
		wrote = true;
	}
	*offset = position;

	remaining = sv.datagram.cursize - *offset;
	if (remaining <= 0)
		return wrote;

	if (msg->cursize + remaining < msg->maxsize)
	{
		SZ_Write (msg, &sv.datagram.data[*offset], remaining);
		*offset = sv.datagram.cursize;
		return true;
	}

	if (remaining >= msg->maxsize)
	{
		Con_DPrintf ("SVFTE_WriteDatagramToMessage: dropping oversized server datagram tail (%d bytes, max %d)\n",
			remaining, msg->maxsize);
		*offset = sv.datagram.cursize;
	}
	return wrote;
}

static qboolean SV_WritePrivateDatagramToMessage (client_t *client, sizebuf_t *msg)
{
	if (!client->datagram.cursize)
		return false;
	if (client->datagram.overflowed)
	{
		SZ_Clear (&client->datagram);
		return false;
	}
	if (client->datagram.cursize >= msg->maxsize)
	{
		Con_DPrintf ("SV_WritePrivateDatagramToMessage: dropping oversized private datagram for %s (%d bytes, max %d)\n",
			client->name, client->datagram.cursize, msg->maxsize);
		SZ_Clear (&client->datagram);
		return false;
	}
	if (msg->cursize + client->datagram.cursize >= msg->maxsize)
		return false;
	SZ_Write (msg, client->datagram.data, client->datagram.cursize);
	SZ_Clear (&client->datagram);
	return true;
}

static qboolean SVFTE_SendClientDatagram (client_t *client, int maxsize)
{
	byte		buf[MAX_DATAGRAM];
	sizebuf_t	msg;
	int			packet_count;
	int			total_bytes;
	int			max_packet_bytes;
	int			datagram_offset;
	int			client_index;
	size_t		prev_resume;
	int			prev_datagram_offset;
	double		update_gap;
	size_t		prev_csqc_pending;
	size_t		csqc_pending;
	static double	last_gap_log[MAX_SCOREBOARD];
	static double	last_update_sent[MAX_SCOREBOARD];
	static double	last_update_log[MAX_SCOREBOARD];
	static struct qsocket_s	*last_update_socket[MAX_SCOREBOARD];

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
	}
	if (net_lagdebug.value && client_index >= 0 && last_update_sent[client_index] > 0)
	{
		update_gap = realtime - last_update_sent[client_index];
		if (update_gap > net_lagdebug_frame_threshold.value &&
			realtime - last_gap_log[client_index] > 0.5)
		{
			Con_Printf ("net_lagdebug: server replacement-delta update gap to %s (%s): %.3f sec host_dt=%.3f sv_time=%.3f\n",
				client->name, NET_QSocketGetAddressString(client->netconnection),
				update_gap, host_frametime, qcvm->time);
			last_gap_log[client_index] = realtime;
		}
	}

	SVFTE_BuildSnapshotForClient (client);
	SVFTE_CalcEntityDeltas (client);
	client->snapshotresume = 0;

	packet_count = 0;
	total_bytes = 0;
	max_packet_bytes = 0;
	datagram_offset = 0;

	do
	{
			msg.data = buf;
			msg.maxsize = maxsize;
			msg.cursize = 0;
			msg.allowoverflow = false;
			msg.overflowed = false;

			if (packet_count == 0)
			{
				MSG_WriteByte (&msg, svc_time);
				MSG_WriteFloat (&msg, qcvm->time);
				if (client->lastmovemessage >= 0)
					SV_WriteMoveAckToMessage (client, &msg);
				SV_WriteClientdataToMessage (client->edict, &msg);
			}
			SV_WritePrivateDatagramToMessage (client, &msg);
			if (!msg.overflowed && packet_count == 0 &&
				datagram_offset < sv.datagram.cursize)
				SVFTE_WriteDatagramToMessage (&msg, &datagram_offset);

			prev_resume = client->snapshotresume;
			prev_datagram_offset = datagram_offset;
			prev_csqc_pending = SVFTE_CountPendingCSQCEntities (client);
		SVFTE_WriteEntitiesToClient (client, &msg);
		SVFTE_WriteCSQCEntitiesToClient (client, &msg);
		if (!msg.overflowed && datagram_offset < sv.datagram.cursize)
			SVFTE_WriteDatagramToMessage (&msg, &datagram_offset);
		if (msg.overflowed)
		{
			Con_Printf ("SVFTE_SendClientDatagram: packet overflow for %s\n", client->name);
			return true;
		}

		if (NET_SendUnreliableMessage (client->netconnection, &msg) == -1)
		{
			SV_DropClient (true);
			return false;
		}

		packet_count++;
		total_bytes += msg.cursize;
		if (msg.cursize > max_packet_bytes)
			max_packet_bytes = msg.cursize;
		client->net_snapshot_packets_sent++;

		if (client->snapshotresume == prev_resume &&
			client->snapshotresume < client->numpendingentities)
		{
			Con_Printf ("SVFTE_SendClientDatagram: entity update could not fit in %d byte packet\n",
				msg.maxsize);
			break;
		}
			csqc_pending = SVFTE_CountPendingCSQCEntities (client);
			if (client->snapshotresume == prev_resume &&
				datagram_offset == prev_datagram_offset &&
				csqc_pending == prev_csqc_pending &&
				(client->snapshotresume < client->numpendingentities ||
				 csqc_pending ||
				 datagram_offset < sv.datagram.cursize ||
				 client->datagram.cursize))
		{
			Con_Printf ("SVFTE_SendClientDatagram: replacement packet made no progress (%d byte packet)\n",
				msg.maxsize);
			break;
		}
		if (packet_count >= 128)
		{
			Con_Printf ("SVFTE_SendClientDatagram: too many replacement-delta packets\n");
			break;
		}
		}
		while (client->snapshotresume < client->numpendingentities ||
			csqc_pending ||
			datagram_offset < sv.datagram.cursize ||
			client->datagram.cursize);

	if (packet_count > 1)
		client->net_snapshot_split_packets += packet_count - 1;
	client->net_snapshot_updates_sent++;
	client->net_snapshot_last_packets = packet_count;
	client->net_snapshot_last_bytes = total_bytes;
	if (packet_count > client->net_snapshot_max_packets)
		client->net_snapshot_max_packets = packet_count;
	if (total_bytes > client->net_snapshot_max_bytes)
		client->net_snapshot_max_bytes = total_bytes;
	if (client_index >= 0)
		last_update_sent[client_index] = realtime;

	if (net_lagdebug.value &&
		(packet_count > 1 || max_packet_bytes > (maxsize * 9) / 10) &&
		(client_index < 0 || realtime - last_update_log[client_index] > 1.0))
	{
		Con_Printf ("net_lagdebug: server replacement update to %s (%s): packets=%d bytes=%d max=%d ents=%zu/%zu maxpacket=%d ack=%d\n",
			client->name, NET_QSocketGetAddressString(client->netconnection),
			packet_count, total_bytes, max_packet_bytes,
			(size_t)client->snapshotresume, client->numpendingentities, maxsize,
			client->lastacksequence);
		if (client_index >= 0)
			last_update_log[client_index] = realtime;
	}
	SV_MaybePrintSnapshotSummary (client, client_index);
	return true;
}

qboolean SV_SendClientDatagram (client_t *client)
{
	byte		buf[MAX_DATAGRAM];
	sizebuf_t	msg;
	int		maxsize;
	int		i;
	int		entity_start;
	int		prev_entity_start;
	int		datagram_offset;
	int		packet_count;
	int		snapshot_sequence;
	int		snapshot_dup;
	qboolean	first_packet;
	int		total_bytes;
	int		max_packet_bytes;
	int		last_packet_bytes;
	int		client_index;
	double		update_gap;
	int		ack_lag;
	static double	last_gap_log[MAX_SCOREBOARD];
	static double	last_update_sent[MAX_SCOREBOARD];
	static double	last_update_log[MAX_SCOREBOARD];
	static int	last_update_snapshot[MAX_SCOREBOARD];
	static struct qsocket_s	*last_update_socket[MAX_SCOREBOARD];

	msg.data = buf;
	maxsize = SV_ClientMaxPacketSize (client);
	SV_UpdateClientMSS (client);

	if (client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
		return SVFTE_SendClientDatagram (client, maxsize);

	if (!SV_MaybeResendSnapshotParts (client))
		return false;

	entity_start = 0;
	datagram_offset = 0;
	packet_count = 0;
	snapshot_sequence = ++client->net_snapshot_sequence;
	first_packet = true;
	total_bytes = 0;
	max_packet_bytes = 0;
	last_packet_bytes = 0;
	client_index = (int)(client - svs.clients);
	if (client_index < 0 || client_index >= MAX_SCOREBOARD)
		client_index = -1;
	snapshot_dup = CLAMP(0, (int)sv_snapshot_packetdup.value, 3);
	ack_lag = client->net_snapshot_ack >= 0 ?
		snapshot_sequence - client->net_snapshot_ack : 0;
	if (ack_lag > client->net_snapshot_ack_lag_max)
		client->net_snapshot_ack_lag_max = ack_lag;

	if (client_index >= 0 &&
		(last_update_socket[client_index] != client->netconnection ||
		 snapshot_sequence <= last_update_snapshot[client_index]))
	{
		last_update_socket[client_index] = client->netconnection;
		last_update_sent[client_index] = 0;
		last_update_log[client_index] = 0;
		last_gap_log[client_index] = 0;
	}

	if (net_lagdebug.value && client_index >= 0 && last_update_sent[client_index] > 0)
	{
		update_gap = realtime - last_update_sent[client_index];
		if (update_gap > net_lagdebug_frame_threshold.value &&
			realtime - last_gap_log[client_index] > 0.5)
		{
			Con_Printf ("net_lagdebug: server unreliable update gap to %s (%s): %.3f sec host_dt=%.3f sv_time=%.3f\n",
				client->name, NET_QSocketGetAddressString(client->netconnection),
				update_gap, host_frametime, qcvm->time);
			last_gap_log[client_index] = realtime;
		}
	}

	do
	{
		int		snapshot_flags_offset;

		msg.maxsize = maxsize;
		msg.cursize = 0;
		msg.allowoverflow = false;
		msg.overflowed = false;

		MSG_WriteByte (&msg, svc_snapshot);
		MSG_WriteShort (&msg, snapshot_sequence & 0xffff);
		MSG_WriteByte (&msg, packet_count & 0xff);
		snapshot_flags_offset = msg.cursize;
		MSG_WriteByte (&msg, first_packet ? SNAPSHOT_FIRST : 0);
		MSG_WriteShort (&msg, CLAMP (0, entity_start, 32767));
		MSG_WriteShort (&msg, CLAMP (0, qcvm->num_edicts, 32767));

		if (first_packet)
		{
			MSG_WriteByte (&msg, svc_time);
			MSG_WriteFloat (&msg, qcvm->time);
			if (client->lastmovemessage >= 0)
				SV_WriteMoveAckToMessage (client, &msg);
		}

// add the client specific data to the datagram
		if (first_packet)
		{
			SV_WriteClientdataToMessage (client->edict, &msg);
			SV_WritePrivateDatagramToMessage (client, &msg);
		}
		else
			SV_WritePrivateDatagramToMessage (client, &msg);

// With split snapshots disabled, send frame events before entity data so
// important sounds/temp entities are not crowded out by a large PVS.
		if (!sv_snapshot_splits.value && first_packet &&
			datagram_offset < sv.datagram.cursize)
		{
			int remaining = sv.datagram.cursize - datagram_offset;
			int space = msg.maxsize - msg.cursize;

			if (remaining <= space)
			{
				SZ_Write (&msg, sv.datagram.data + datagram_offset, remaining);
				datagram_offset = sv.datagram.cursize;
			}
			else if (net_lagdebug.value &&
				(client_index < 0 || realtime - last_update_log[client_index] > 1.0))
			{
				Con_Printf ("net_lagdebug: dropping oversized server datagram before snapshot for %s (%s): datagram=%d space=%d maxpacket=%d\n",
					client->name, NET_QSocketGetAddressString(client->netconnection),
					remaining, space, maxsize);
				if (client_index >= 0)
					last_update_log[client_index] = realtime;
				datagram_offset = sv.datagram.cursize;
			}
		}

		prev_entity_start = entity_start;
		net_edict_write_start = entity_start;
		SV_WriteEntitiesToClient (client->edict, &msg);
		entity_start = net_edict_write_next;

// copy the server datagram if there is space
		if (entity_start >= net_edict_write_total && datagram_offset < sv.datagram.cursize)
		{
			int remaining = sv.datagram.cursize - datagram_offset;
			int space = msg.maxsize - msg.cursize;

			if (remaining <= space)
			{
				SZ_Write (&msg, sv.datagram.data + datagram_offset, remaining);
				datagram_offset = sv.datagram.cursize;
			}
			else if (remaining > msg.maxsize - 5)
			{
				if (!dev_overflows.packetsize || dev_overflows.packetsize + CONSOLE_RESPAM_TIME < realtime )
				{
					Con_Printf ("Server datagram too large to split safely (%d bytes, max packet %d)\n",
						remaining, msg.maxsize);
					dev_overflows.packetsize = realtime;
				}
				datagram_offset = sv.datagram.cursize;
			}
		}

		if (!sv_snapshot_splits.value ||
			(entity_start >= net_edict_write_total &&
			 datagram_offset >= sv.datagram.cursize &&
			 !client->datagram.cursize) ||
			(entity_start == prev_entity_start &&
			 entity_start < net_edict_write_total) ||
			packet_count >= 127)
		{
			msg.data[snapshot_flags_offset] |= SNAPSHOT_LAST;
		}

	// send the datagram
		SV_StoreSnapshotPart (client, snapshot_sequence, packet_count, &msg);
		for (i = 0; i <= snapshot_dup; i++)
		{
			if (NET_SendUnreliableMessage (client->netconnection, &msg) == -1)
			{
				SV_DropClient (true);// if the message couldn't send, kick off
				return false;
			}
		}

		total_bytes += msg.cursize * (snapshot_dup + 1);
		if (msg.cursize > max_packet_bytes)
			max_packet_bytes = msg.cursize;
		last_packet_bytes = msg.cursize;
		packet_count++;
		client->net_snapshot_packets_sent += snapshot_dup + 1;
		first_packet = false;

		if (!sv_snapshot_splits.value &&
			(entity_start < net_edict_write_total ||
			 datagram_offset < sv.datagram.cursize ||
			 client->datagram.cursize))
		{
			client->net_snapshot_unsent_entities += net_edict_write_total - entity_start;
			if (net_lagdebug.value &&
				(client_index < 0 || realtime - last_update_log[client_index] > 1.0))
			{
				Con_Printf ("net_lagdebug: clipped oversized snapshot for %s (%s): sent_ents=%d/%d sv_datagram=%d/%d bytes=%d maxpacket=%d ack=%d\n",
					client->name, NET_QSocketGetAddressString(client->netconnection),
					entity_start, net_edict_write_total,
					datagram_offset, sv.datagram.cursize, msg.cursize, maxsize,
					client->net_snapshot_ack);
				if (client_index >= 0)
					last_update_log[client_index] = realtime;
			}
			break;
		}

		if (entity_start == prev_entity_start && entity_start < net_edict_write_total)
		{
			Con_Printf ("SV_SendClientDatagram: entity update could not fit in %d byte packet\n", msg.maxsize);
			break;
		}
		if (packet_count >= 128)
		{
			Con_Printf ("SV_SendClientDatagram: too many split packets\n");
			break;
		}
	}
	while (entity_start < net_edict_write_total ||
		datagram_offset < sv.datagram.cursize ||
		client->datagram.cursize);

	if (packet_count > 1)
		client->net_snapshot_split_packets += packet_count - 1;
	client->net_snapshot_updates_sent++;
	client->net_snapshot_last_packets = packet_count;
	client->net_snapshot_last_bytes = total_bytes;
	if (packet_count > client->net_snapshot_max_packets)
		client->net_snapshot_max_packets = packet_count;
	if (total_bytes > client->net_snapshot_max_bytes)
		client->net_snapshot_max_bytes = total_bytes;

	if (net_lagdebug.value &&
		(packet_count > 1 || max_packet_bytes > (maxsize * 9) / 10) &&
		(client_index < 0 || realtime - last_update_log[client_index] > 1.0))
	{
		Con_Printf ("net_lagdebug: server update to %s (%s): packets=%d dup=%d bytes=%d max=%d last=%d ents=%d/%d sv_datagram=%d/%d maxpacket=%d snap=%d ack=%d splits=%d clipped=%d\n",
			client->name, NET_QSocketGetAddressString(client->netconnection),
			packet_count, snapshot_dup, total_bytes, max_packet_bytes, last_packet_bytes,
			entity_start, net_edict_write_total,
			datagram_offset, sv.datagram.cursize, maxsize,
			snapshot_sequence, client->net_snapshot_ack,
			client->net_snapshot_split_packets,
			client->net_snapshot_unsent_entities);
		if (client_index >= 0)
			last_update_log[client_index] = realtime;
	}
	SV_MaybePrintSnapshotSummary (client, client_index);

	if (client_index >= 0)
	{
		last_update_sent[client_index] = realtime;
		last_update_snapshot[client_index] = snapshot_sequence;
	}

	return true;
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
				if (!client->active)
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
		if (!client->active)
			continue;
		if (!(client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
			SV_WriteStats (client);
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

// build individual updates
	for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
	{
		if (!host_client->active)
			continue;

		if (host_client->spawned)
		{
			if (!SV_SendClientDatagram (host_client))
				continue;
		}
		else
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
				if (host_client->signonidx == sv.num_signon_buffers)
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
			SV_DropClient (true);
			host_client->message.overflowed = false;
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
					SV_DropClient (true);	// if the message couldn't send, kick off
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
	int			i;
	edict_t		*svent;
	int			entnum;
	int			bits; //johnfitz -- PROTOCOL_FITZQUAKE

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

		//johnfitz -- PROTOCOL_FITZQUAKE
		bits = 0;
		if (sv.protocol == PROTOCOL_NETQUAKE) //still want to send baseline in PROTOCOL_NETQUAKE, so reset these values
		{
			if (svent->baseline.modelindex & 0xFF00)
				svent->baseline.modelindex = 0;
			if (svent->baseline.frame & 0xFF00)
				svent->baseline.frame = 0;
			svent->baseline.alpha = ENTALPHA_DEFAULT;
		}
		else //decide which extra data needs to be sent
		{
			if (svent->baseline.modelindex & 0xFF00)
				bits |= B_LARGEMODEL;
			if (svent->baseline.frame & 0xFF00)
				bits |= B_LARGEFRAME;
			if (svent->baseline.alpha != ENTALPHA_DEFAULT)
				bits |= B_ALPHA;
			// Baseline scale is not signon-encoded; dynamic U_SCALE still works.
		}
		//johnfitz

	//
	// add to the message
	//
		SV_ReserveSignonSpace (35);

		//johnfitz -- PROTOCOL_FITZQUAKE
		if (bits)
			MSG_WriteByte (sv.signon, svc_spawnbaseline2);
		else
			MSG_WriteByte (sv.signon, svc_spawnbaseline);
		//johnfitz

		MSG_WriteShort (sv.signon,entnum);

		//johnfitz -- PROTOCOL_FITZQUAKE
		if (bits)
			MSG_WriteByte (sv.signon, bits);

		if (bits & B_LARGEMODEL)
			MSG_WriteShort (sv.signon, svent->baseline.modelindex);
		else
			MSG_WriteByte (sv.signon, svent->baseline.modelindex);

		if (bits & B_LARGEFRAME)
			MSG_WriteShort (sv.signon, svent->baseline.frame);
		else
			MSG_WriteByte (sv.signon, svent->baseline.frame);
		//johnfitz

		MSG_WriteByte (sv.signon, svent->baseline.colormap);
		MSG_WriteByte (sv.signon, svent->baseline.skin);
		for (i=0 ; i<3 ; i++)
		{
			MSG_WriteCoord(sv.signon, svent->baseline.origin[i], sv.protocolflags);
			MSG_WriteAngle(sv.signon, svent->baseline.angles[i], sv.protocolflags);
		}

		//johnfitz -- PROTOCOL_FITZQUAKE
		if (bits & B_ALPHA)
			MSG_WriteByte (sv.signon, svent->baseline.alpha);
		//johnfitz
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

	svs.serverflags = pr_global_struct->serverflags;

	for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
	{
		if (!host_client->active)
			continue;

	// call the progs to get default spawn parms for the new client
		pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
		PR_ExecuteProgram (pr_global_struct->SetChangeParms);
		for (j=0 ; j<NUM_SPAWN_PARMS ; j++)
			host_client->spawn_parms[j] = (&pr_global_struct->parm1)[j];
	}
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

	sv.protocol = sv_protocol; // johnfitz
	
	if (sv.protocol == PROTOCOL_RMQ)
	{
		// set up the protocol flags used by this server
		// (note - these could be cvar-ised so that server admins could choose the protocol features used by their servers)
		sv.protocolflags = PRFL_INT32COORD | PRFL_SHORTANGLE;
	}
	else sv.protocolflags = 0;

	PR_SwitchQCVM(vm);
// load progs to get entity field count
	PR_LoadProgs ("progs.dat", true);

// allocate server memory
	/* Host_ClearMemory() called above already cleared the whole sv structure */
	qcvm->max_edicts = CLAMP (MIN_EDICTS,(int)max_edicts.value,MAX_EDICTS); //johnfitz -- max_edicts cvar
	qcvm->edicts = (edict_t *) malloc (qcvm->max_edicts*qcvm->edict_size); // ericw -- sv.edicts switched to use malloc()
	ClearLink (&qcvm->free_edicts);

	sv.datagram.maxsize = sizeof(sv.datagram_buf);
	sv.datagram.cursize = 0;
	sv.datagram.data = sv.datagram_buf;

	sv.multicast.maxsize = sizeof(sv.multicast_buf);
	sv.multicast.cursize = 0;
	sv.multicast.data = sv.multicast_buf;
	sv.multicast.allowoverflow = true;

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
	SV_Physics ();
	SV_Physics ();

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
}
