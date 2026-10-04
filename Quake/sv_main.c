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

#include <errno.h>
#include "quakedef.h"
#include "skyroom_metadata.h"
#include "pmove.h"
#include "vr_weapon_calibration.h"
#include "vr_weapon_menu.h"
#include "player_avatar.h"

server_t		sv;
server_static_t svs;

/* Queue a slot's numeric projection and custom descriptor together. */
static qboolean SV_WriteAvatarSlot (client_t *client, int slot, int avatar_id,
	const char *custom_key, const char *custom_digest)
{
	char numeric_text[64];
	char custom_text[128];
	size_t required;

	if (!client || !client->avatar_capable ||
		!PlayerAvatar_BuildSlotCommand (true, numeric_text,
			sizeof (numeric_text), slot, avatar_id))
		return false;
	if (client->avatar_custom_capable &&
		!PlayerAvatar_BuildCustomSlotCommand (true, custom_text,
			sizeof (custom_text), slot, custom_key, custom_digest))
		return false;
	required = strlen (numeric_text) + 2; // svc_stufftext and NUL
	if (client->avatar_custom_capable)
		required += strlen (custom_text) + 2;
	if (client->message.overflowed || client->message.cursize < 0 ||
		client->message.maxsize <= 0 ||
		client->message.cursize > client->message.maxsize ||
		required > (size_t)(client->message.maxsize - client->message.cursize))
		return false;

	MSG_WriteByte (&client->message, svc_stufftext);
	MSG_WriteString (&client->message, numeric_text);
	if (client->avatar_custom_capable)
	{
		MSG_WriteByte (&client->message, svc_stufftext);
		MSG_WriteString (&client->message, custom_text);
	}
	return true;
}

static void SV_FlushAvatarSlots (client_t *client)
{
	int slot;

	if (!client || !client->active || !client->netconnection ||
		!client->avatar_capable)
		return;
	for (slot = 0; slot < svs.maxclients && slot < PLAYER_AVATAR_MAX_SLOTS; slot++)
	{
		unsigned short bit = (unsigned short)(1u << slot);
		client_t *source = &svs.clients[slot];
		int avatar_id;
		const char *custom_key = NULL;
		const char *custom_digest = NULL;

		if (!(client->avatar_dirty_slots & bit))
			continue;
		avatar_id = source->active && PlayerAvatar_IsValidId (source->avatar_id) ?
			source->avatar_id : PLAYER_AVATAR_RANGER;
		if (source->active && source->avatar_custom_key[0] &&
			PlayerAvatar_ValidCustomKey (source->avatar_custom_key) &&
			PlayerAvatar_ValidCustomDigest (source->avatar_custom_digest))
		{
			avatar_id = PLAYER_AVATAR_RANGER;
			custom_key = source->avatar_custom_key;
			custom_digest = source->avatar_custom_digest;
		}
		if (!SV_WriteAvatarSlot (client, slot, avatar_id, custom_key, custom_digest))
			return; // retain this and later slots for a reliable retry
		client->avatar_dirty_slots &= (unsigned short)~bit;
	}
}

void SV_SendAvatarTable (client_t *client)
{
	int slot;

	if (!client || !client->avatar_capable)
		return;
	for (slot = 0; slot < svs.maxclients && slot < PLAYER_AVATAR_MAX_SLOTS; slot++)
		client->avatar_dirty_slots |= (unsigned short)(1u << slot);
}

void SV_BroadcastAvatarSlot (int slot, int avatar_id)
{
	int recipient;

	if (slot < 0 || slot >= svs.maxclients || slot >= PLAYER_AVATAR_MAX_SLOTS ||
		!PlayerAvatar_IsValidId (avatar_id))
		return;
	for (recipient = 0; recipient < svs.maxclients; recipient++)
		if (svs.clients[recipient].active && svs.clients[recipient].avatar_capable)
			svs.clients[recipient].avatar_dirty_slots |= (unsigned short)(1u << slot);
}

static void SV_AppendAvatarOffers (client_t *client)
{
	static const char *offers[] = {
		"//avatar_protocol 1\n", "//avatar_custom_protocol 1\n"
	};
	size_t i;

	if (!client || !client->active || !client->netconnection ||
		client->message.overflowed || client->message.cursize < 0 ||
		client->message.maxsize <= 0 ||
		client->message.cursize > client->message.maxsize)
		return;
	for (i = 0; i < countof (offers); i++)
	{
		unsigned char bit = (unsigned char)(1u << i);
		if (!(client->avatar_offer_pending & bit) ||
			strlen (offers[i]) + 2 >
				(size_t)(client->message.maxsize - client->message.cursize))
			continue;
		MSG_WriteByte (&client->message, svc_stufftext);
		MSG_WriteString (&client->message, offers[i]);
		client->avatar_offer_pending &= (unsigned char)~bit;
	}
}

/* The donor's drop path leaves client storage intact. Retire any old avatar
 * on the next server send, before that slot can describe a new occupant. */
static void SV_ClearDroppedAvatarSlots (void)
{
	int slot;

	for (slot = 0; slot < svs.maxclients && slot < PLAYER_AVATAR_MAX_SLOTS; slot++)
	{
		client_t *client = &svs.clients[slot];
		if (client->active || (client->avatar_id == PLAYER_AVATAR_RANGER &&
			!client->avatar_custom_key[0] && !client->avatar_custom_digest[0]))
			continue;
		client->avatar_id = PLAYER_AVATAR_RANGER;
		client->avatar_custom_key[0] = 0;
		client->avatar_custom_digest[0] = 0;
		SV_BroadcastAvatarSlot (slot, PLAYER_AVATAR_RANGER);
	}
}

static char localmodels[MAX_MODELS][8]; // inline model names for precache

int sv_protocol = PROTOCOL_RMQ; // spike -- enough maps need this now that we can probably afford incompatibility with engines that still don't support 999
								// (vanilla was already broken) -- PROTOCOL_FITZQUAKE; //johnfitz
unsigned int sv_protocol_pext1 = PEXT1_SUPPORTED_SERVER; // spike
unsigned int sv_protocol_pext2 = PEXT2_SUPPORTED_SERVER; // spike

static cvar_t sv_netsort = {"sv_netsort", "1", CVAR_NONE};
/* Rendering skyrooms is deferred; do not expand entity visibility by default. */
static cvar_t sv_skyroom_pvs = {"sv_skyroom_pvs", "0", CVAR_NONE};
static cvar_t sv_smoothplatformlerps = {"sv_smoothplatformlerps", "1", CVAR_NONE};
static cvar_t sv_qsvr_private = {"sv_qsvr_private", "1", CVAR_NONE};
static cvar_t sv_private_pmove_walk = {"sv_private_pmove_walk", "1", CVAR_SERVERINFO};
extern cvar_t sv_gameplayfix_elevators;
static void SV_AddSkyRoomPVS (const vec3_t org, qmodel_t *worldmodel);

cvar_t sv_gorilla = {"sv_gorilla", "1", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_gorilla_trustclient = {"sv_gorilla_trustclient", "1", CVAR_NOTIFY | CVAR_SERVERINFO};
static cvar_t sv_weapon_collision = {"sv_weapon_collision", "-1", CVAR_NOTIFY | CVAR_SERVERINFO};
/* Keep the first stock-axe adapter explicitly enabled until physical swing
 * and QC outcome qualification is complete on the release targets. */
static cvar_t sv_immersive_melee = {"sv_immersive_melee", "0", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_voice = {"sv_voice", "1", CVAR_SERVERINFO};
cvar_t sv_coop_shared_pickups = {"sv_coop_shared_pickups", "-1", CVAR_ARCHIVE | CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_respawn_near_player = {"sv_coop_respawn_near_player", "-1", CVAR_ARCHIVE | CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_respawn_delay = {"sv_coop_respawn_delay", "10", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_respawn_keep_weapons_ammo = {"sv_coop_respawn_keep_weapons_ammo", "-1", CVAR_ARCHIVE | CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_coop_player_teleport_fallback = {"sv_coop_player_teleport_fallback", "-1", CVAR_ARCHIVE | CVAR_NOTIFY | CVAR_SERVERINFO};

static void SV_GorillaPolicyChanged (cvar_t *var)
{
	int i;

	Host_Callback_Notify (var);
	for (i = 0; svs.clients && i < svs.maxclients; i++)
		svs.clients[i].vr_gorilla_last_advertised = -1;
}

qboolean SV_VRWeaponCollisionEnabled (void)
{
	return sv_weapon_collision.value < 0.0f ?
		(cls.state != ca_dedicated && svs.maxclients == 1) :
		(sv_weapon_collision.value > 0.0f);
}

static qboolean SV_VRContactPolicyEnabled (const cvar_t *policy)
{
	return policy->value < 0.0f ?
		(cls.state != ca_dedicated && svs.maxclients == 1) :
		(policy->value > 0.0f);
}

qboolean SV_VRStockAxeMeleeEnabled (void)
{
	return SV_VRContactPolicyEnabled (&sv_immersive_melee) &&
		SV_VRStockAxeContactProfile () != VR_WEAPON_CONTACT_PROFILE_NONE;
}

qboolean SV_VRDwellBerserkMeleeEnabled (void)
{
	return SV_VRContactPolicyEnabled (&sv_immersive_melee) &&
		SV_DwellBerserkAkimboProgramLoaded ();
}

qboolean SV_VRQBJ3MeleeEnabled (void)
{
	return SV_VRContactPolicyEnabled (&sv_immersive_melee) &&
		SV_VRQBJ3MeleeContactProfile () == VR_WEAPON_CONTACT_PROFILE_QBJ3;
}

qboolean SV_VREnyoMeleeEnabled (void)
{
	return SV_VRContactPolicyEnabled (&sv_immersive_melee) &&
		SV_VREnyoMeleeContactProfile () == VR_WEAPON_CONTACT_PROFILE_ENYO;
}

extern cvar_t nomonsters;

#define VRIK_SVC_V2_MESSAGE_BYTES (1 + 2 + 4 + VRIK_POSE_WIRE_BYTES)
static unsigned int sv_vrik_next_generation;
static unsigned int sv_voice_next_generation;

static unsigned int SV_NextVoiceGeneration (void)
{
	if (!++sv_voice_next_generation)
		++sv_voice_next_generation;
	return sv_voice_next_generation;
}

static void SV_ResetVoiceMapState (void)
{
	int i;

	for (i = 0; i < svs.maxclients; ++i)
	{
		client_t *client = &svs.clients[i];

		client->voice_protocol_offered = false;
		client->voice_capable = false;
		client->voice_next_serial = 0;
		client->voice_rate_window_start = 0;
		client->voice_rate_packets = 0;
		client->voice_rate_bytes = 0;
		client->voice_relay_next_source = 0;
		memset (client->voice_packets, 0, sizeof (client->voice_packets));
		memset (client->voice_relay_serial, 0,
			sizeof (client->voice_relay_serial));
		memset (client->voice_relay_generation, 0,
			sizeof (client->voice_relay_generation));
		client->voice_generation = client->active ?
			SV_NextVoiceGeneration () : 0;
	}
}

static qboolean SV_VoicePacketIsDuplicate (const client_t *client,
	const voice_packet_t *packet)
{
	uint64_t first, serial, last;

	if (!client->voice_next_serial)
		return false;
	first = client->voice_next_serial >= VOICE_SERVER_QUEUE_CAPACITY ?
		client->voice_next_serial - VOICE_SERVER_QUEUE_CAPACITY + 1 : 1;
	last = client->voice_next_serial;
	for (serial = first; ; ++serial)
	{
		const server_voice_packet_t *queued =
			&client->voice_packets[(serial - 1) % VOICE_SERVER_QUEUE_CAPACITY];
		if (queued->serial == serial &&
			queued->packet.sequence == packet->sequence &&
			queued->packet.talkspurt == packet->talkspurt &&
			queued->packet.flags == packet->flags)
			return true;
		if (serial == last)
			break;
	}
	return false;
}

void SV_ReceiveVoicePacket (client_t *client, const voice_packet_t *packet)
{
	server_voice_packet_t *queued;
	unsigned int packet_bytes;

	if (!client || !packet || !sv_voice.value || !client->active ||
		!client->spawned || !client->voice_capable ||
		!Voice_PacketIsValid (packet))
		return;
	if (realtime < client->voice_rate_window_start ||
		realtime - client->voice_rate_window_start >= 1.0)
	{
		client->voice_rate_window_start = realtime;
		client->voice_rate_packets = 0;
		client->voice_rate_bytes = 0;
	}
	packet_bytes = VOICE_CLC_HEADER_BYTES + packet->payload_bytes;
	if (client->voice_rate_packets >= VOICE_SERVER_MAX_PACKETS_PER_SECOND ||
		client->voice_rate_bytes + packet_bytes >
		VOICE_SERVER_MAX_BYTES_PER_SECOND)
		return;
	client->voice_rate_packets++;
	client->voice_rate_bytes += packet_bytes;
	if (SV_VoicePacketIsDuplicate (client, packet))
		return;
	if (!client->voice_generation)
		client->voice_generation = SV_NextVoiceGeneration ();
	client->voice_next_serial++;
	if (!client->voice_next_serial)
	{
		memset (client->voice_packets, 0, sizeof (client->voice_packets));
		client->voice_next_serial = 1;
		client->voice_generation = SV_NextVoiceGeneration ();
	}
	queued = &client->voice_packets[(client->voice_next_serial - 1) %
		VOICE_SERVER_QUEUE_CAPACITY];
	queued->serial = client->voice_next_serial;
	queued->arrival_time = realtime;
	queued->packet = *packet;
}

void SV_AppendVRIKRetirement(client_t *client, int slot,
	unsigned int generation)
{
	char command[64];
	int command_length;
	size_t required;

	if (!client || !client->active || !client->netconnection ||
		client->vrik_protocol_version < VRIK_PROTOCOL_LEGACY_VERSION ||
		client->vrik_protocol_version >= VRIK_ADMISSION_PROTOCOL_VERSION ||
		slot < 0 || slot >= MAX_SCOREBOARD || !generation ||
		client->message.overflowed || client->message.cursize < 0 ||
		client->message.maxsize < 0 ||
		client->message.cursize > client->message.maxsize)
		return;
	command_length = q_snprintf(command, sizeof(command),
		"//vrik_retire %d %u\n", slot, generation);
	if (command_length <= 0 || (size_t)command_length >= sizeof(command))
		return;
	required = (size_t)command_length + 2; /* svc_stufftext and NUL */
	if (required > (size_t)(client->message.maxsize - client->message.cursize))
		return;

	/* Legacy peers get this only inline after required scoreboard updates. */
	MSG_WriteByte(&client->message, svc_stufftext);
	MSG_WriteString(&client->message, command);
}

static void SV_ResetVRIKClientState(client_t *client, qboolean keep_capability)
{
	qboolean capable;
	unsigned char version;

	if (!client)
		return;
	capable = keep_capability ? client->vrik_capable : false;
	version = keep_capability ? client->vrik_protocol_version : 0;
	client->vrik_capable = false;
	client->vrik_protocol_version = 0;
	client->vrik_sequence_valid = false;
	client->vrik_inactive_sent = false;
	client->vrik_last_sequence = 0;
	client->vrik_generation = 0;
	client->vrik_pose_time = 0;
	client->vrik_next_accept_time = 0;
	client->vrik_v2_body_valid = false;
	memset(&client->vrik_pose, 0, sizeof(client->vrik_pose));
	memset(&client->vrik_pose_v3, 0, sizeof(client->vrik_pose_v3));
	memset(client->vrik_v2_body, 0, sizeof(client->vrik_v2_body));
	memset(client->vrik_relay_sequence_valid, 0,
		sizeof(client->vrik_relay_sequence_valid));
	memset(client->vrik_relay_sequence, 0,
		sizeof(client->vrik_relay_sequence));
	memset(client->vrik_relay_generation, 0,
		sizeof(client->vrik_relay_generation));
	memset(client->vrik_admitted_generation, 0,
		sizeof(client->vrik_admitted_generation));
	client->vrik_capable = capable;
	client->vrik_protocol_version = version;
}

static void SV_ResetVRIKMapState(void)
{
	int i;

	for (i = 0; i < svs.maxclients; ++i)
	{
		client_t *client = &svs.clients[i];
		SV_ResetVRIKClientState(client, false);
		/* A map is a new pose stream. SV_SendServerinfo sends the new offer
		 * in the same reliable message as the new signon. */
	}
}

static qboolean SV_VRIKPoseV3IsValid(const vrik_codec_pose_t *pose)
{
	int target;
	const double max_squared = (double)VRIK_MAX_ROOT_LOCAL_OFFSET *
		(double)VRIK_MAX_ROOT_LOCAL_OFFSET;

	if (!pose)
		return false;
	for (target = 0; target < VRIK_TARGET_COUNT; ++target)
	{
		double x, y, z;
		if (!(pose->present_mask & VRIK_TARGET_BIT(target)))
			continue;
		x = pose->targets[target].position[0];
		y = pose->targets[target].position[1];
		z = pose->targets[target].position[2];
		if (x * x + y * y + z * z > max_squared)
			return false;
	}
	return true;
}

static void SV_VRIKV3ToLegacyPose(const vrik_codec_pose_t *source,
	vrik_pose_t *destination)
{
	int target;

	memset(destination, 0, sizeof(*destination));
	destination->sequence = source->sequence;
	if (!(source->flags & VRIK_V3_FLAG_ACTIVE) ||
		!(source->tracked_mask & VRIK_TARGET_BIT(VRIK_TARGET_HEAD)))
		return;

	destination->flags = VRIK_FLAG_ACTIVE | VRIK_FLAG_HEAD_TRACKED;
	if (source->flags & VRIK_V3_FLAG_DOMINANT_LEFT)
		destination->flags |= VRIK_FLAG_DOMINANT_LEFT;
	for (target = 0; target < VRIK_TRACKER_COUNT; ++target)
	{
		if (source->tracked_mask & VRIK_TARGET_BIT(target))
			destination->flags |= (unsigned char)(VRIK_FLAG_HEAD_TRACKED << target);
		if (source->present_mask & VRIK_TARGET_BIT(target))
		{
			VectorCopy(source->targets[target].position, destination->position[target]);
			VectorCopy(source->targets[target].orientation, destination->orientation[target]);
		}
	}
	destination->body_yaw = source->body_yaw;
	VectorCopy(source->aim_orientation, destination->aim_orientation);
}

static qboolean SV_LegacyPoseToV2(const vrik_pose_t *source,
	vrik_v2_pose_t *destination)
{
	int target;

	if (!source || !destination)
		return false;
	memset(destination, 0, sizeof(*destination));
	destination->sequence = source->sequence;
	destination->flags = source->flags;
	destination->body_yaw = source->body_yaw;
	for (target = 0; target < VRIK_TRACKER_COUNT; ++target)
	{
		VectorCopy(source->position[target], destination->targets[target].position);
		VectorCopy(source->orientation[target], destination->targets[target].orientation);
	}
	VectorCopy(source->aim_orientation, destination->aim_orientation);
	return vrik_v2_validate_legacy_pose(destination) == VRIK_CODEC_OK;
}

static qboolean SV_WriteVRIKPoseV2(sizebuf_t *msg, int entitynum,
	unsigned int generation, const vrik_codec_pose_t *pose,
	const unsigned char raw_body[VRIK_V2_BODY_BYTES])
{
	vrik_pose_t legacy;
	vrik_v2_pose_t encoded_pose;
	uint8_t body[VRIK_V2_BODY_BYTES];
	const uint8_t *wire_body = raw_body;
	size_t written;
	int i;

	if (!wire_body)
	{
		SV_VRIKV3ToLegacyPose(pose, &legacy);
		if (!SV_LegacyPoseToV2(&legacy, &encoded_pose) ||
			vrik_v2_encode(&encoded_pose, body, sizeof(body), &written) != VRIK_CODEC_OK ||
			written != VRIK_V2_BODY_BYTES)
			return false;
		wire_body = body;
	}
	MSG_WriteByte(msg, svc_vrikpose);
	MSG_WriteShort(msg, entitynum);
	MSG_WriteLong(msg, (int)generation);
	for (i = 0; i < (int)VRIK_V2_BODY_BYTES; ++i)
		MSG_WriteByte(msg, wire_body[i]);
	return true;
}

static qboolean SV_WriteVRIKPoseV3(sizebuf_t *msg, int entitynum,
	unsigned int generation, const vrik_codec_pose_t *pose)
{
	uint8_t body[VRIK_V3_MAX_BODY_BYTES];
	size_t written;
	int i;

	if (vrik_v3_encode(pose, body, sizeof(body), &written) != VRIK_CODEC_OK ||
		written > VRIK_V3_MAX_BODY_BYTES)
		return false;
	MSG_WriteByte(msg, svc_vrikpose);
	MSG_WriteShort(msg, entitynum);
	MSG_WriteLong(msg, (int)generation);
	MSG_WriteByte(msg, (int)written);
	for (i = 0; i < (int)written; ++i)
		MSG_WriteByte(msg, body[i]);
	return true;
}

static void SV_ReceiveVRIKPoseInternal(client_t *client,
	const vrik_codec_pose_t *pose,
	const unsigned char raw_v2_body[VRIK_V2_BODY_BYTES])
{
	if (!client || !pose || !client->active || !client->spawned ||
		!client->edict || !client->vrik_capable ||
		!SV_VRIKPoseV3IsValid(pose))
		return;
	if (client->vrik_sequence_valid &&
		!vrik_sequence_is_newer(pose->sequence, client->vrik_last_sequence))
		return;
	if (!(pose->flags & VRIK_V3_FLAG_ACTIVE) && client->vrik_inactive_sent)
		return;

	if (!client->vrik_generation)
	{
		client->vrik_generation = ++sv_vrik_next_generation;
		if (!client->vrik_generation)
			client->vrik_generation = ++sv_vrik_next_generation;
		Con_DPrintf("VRIK: accepted pose stream from %s generation %u\n",
			client->name, client->vrik_generation);
	}
	client->vrik_pose_v3 = *pose;
	client->vrik_v2_body_valid = raw_v2_body != NULL;
	if (raw_v2_body)
		memcpy(client->vrik_v2_body, raw_v2_body, VRIK_V2_BODY_BYTES);
	SV_VRIKV3ToLegacyPose(pose, &client->vrik_pose);
	client->vrik_last_sequence = pose->sequence;
	client->vrik_sequence_valid = true;
	client->vrik_pose_time = realtime;
	client->vrik_inactive_sent = !(pose->flags & VRIK_V3_FLAG_ACTIVE);
}

void SV_ReceiveVRIKPoseV2(client_t *client, const vrik_v2_pose_t *pose,
	const unsigned char body[VRIK_V2_BODY_BYTES])
{
	vrik_codec_pose_t normalized;

	if (!pose || !body ||
		vrik_v2_validate_legacy_pose(pose) != VRIK_CODEC_OK ||
		vrik_v2_to_normalized(pose, &normalized) != VRIK_CODEC_OK)
		return;
	SV_ReceiveVRIKPoseInternal(client, &normalized, body);
}

void SV_ReceiveVRIKPoseV3(client_t *client, const vrik_codec_pose_t *pose)
{
	SV_ReceiveVRIKPoseInternal(client, pose, NULL);
}

void SV_ExpireVRIKPoses(void)
{
	int i;

	for (i = 0; i < svs.maxclients; ++i)
	{
		client_t *client = &svs.clients[i];
		vrik_codec_pose_t inactive;

		/* Dropped slots cannot relay, but clear optional state before reuse. */
		if (!client->active)
		{
			if (client->vrik_capable || client->vrik_sequence_valid ||
				client->vrik_generation)
				SV_ResetVRIKClientState(client, false);
			continue;
		}

		if (!client->vrik_capable ||
			!client->vrik_sequence_valid || client->vrik_inactive_sent ||
			!(client->vrik_pose_v3.flags & VRIK_V3_FLAG_ACTIVE) ||
			realtime - client->vrik_pose_time <= VRIK_POSE_STALE_TIME)
			continue;

		memset(&inactive, 0, sizeof(inactive));
		inactive.sequence = client->vrik_last_sequence + 1;
		client->vrik_pose_v3 = inactive;
		client->vrik_v2_body_valid = false;
		SV_VRIKV3ToLegacyPose(&inactive, &client->vrik_pose);
		client->vrik_last_sequence = inactive.sequence;
		client->vrik_pose_time = realtime;
		client->vrik_inactive_sent = true;
	}
}

static qboolean SV_AppendPendingVRIK(client_t *recipient, sizebuf_t *msg)
{
	int i;

	if (!recipient->vrik_capable || !recipient->spawned)
		return true;
	for (i = 0; i < svs.maxclients && i < MAX_SCOREBOARD; ++i)
	{
		client_t *source = &svs.clients[i];
		int required_bytes;
		size_t v3_body_bytes;
		qboolean written;

		if (source == recipient || !source->active || !source->spawned ||
			!source->vrik_capable || !source->vrik_sequence_valid ||
			!source->vrik_generation)
			continue;
		if (recipient->vrik_relay_sequence_valid[i] &&
			recipient->vrik_relay_generation[i] == source->vrik_generation &&
			recipient->vrik_relay_sequence[i] == source->vrik_last_sequence)
			continue;
		if (recipient->vrik_protocol_version >= VRIK_ADMISSION_PROTOCOL_VERSION &&
			recipient->vrik_admitted_generation[i] != source->vrik_generation)
		{
			char command[64];
			int command_length = q_snprintf(command, sizeof(command),
				"//vrik_generation %d %u\n", i, source->vrik_generation);
			size_t required;

			if (!source->name[0] || command_length <= 0 ||
				(size_t)command_length >= sizeof(command) ||
				recipient->message.overflowed ||
				recipient->message.cursize < 0 ||
				recipient->message.maxsize < 0 ||
				recipient->message.cursize > recipient->message.maxsize)
				continue;
			required = (size_t)command_length + 2;
			if (required > (size_t)(recipient->message.maxsize -
				recipient->message.cursize))
				continue;
			MSG_WriteByte(&recipient->message, svc_stufftext);
			MSG_WriteString(&recipient->message, command);
			/* Record only an admission appended to the reliable stream. */
			recipient->vrik_admitted_generation[i] = source->vrik_generation;
		}

		if (recipient->vrik_protocol_version >= VRIK_PROTOCOL_VERSION)
		{
			if (vrik_v3_body_size(source->vrik_pose_v3.present_mask,
					&v3_body_bytes) != VRIK_CODEC_OK)
				continue;
			required_bytes = 1 + 2 + 4 + 1 + (int)v3_body_bytes;
		}
		else
			required_bytes = VRIK_SVC_V2_MESSAGE_BYTES;
		if (required_bytes > msg->maxsize)
			continue;

		if (msg->cursize + required_bytes > msg->maxsize)
		{
			if (msg->cursize &&
				NET_SendUnreliableMessage(recipient->netconnection, msg) == -1)
			{
				host_client = recipient;
				SV_DropClient(false);
				return false;
			}
			SZ_Clear(msg);
		}

		if (recipient->vrik_protocol_version >= VRIK_PROTOCOL_VERSION)
			written = SV_WriteVRIKPoseV3(msg, i + 1, source->vrik_generation,
				&source->vrik_pose_v3);
		else
			written = SV_WriteVRIKPoseV2(msg, i + 1, source->vrik_generation,
				&source->vrik_pose_v3,
				source->vrik_v2_body_valid ? source->vrik_v2_body : NULL);
		if (!written)
			continue;

		recipient->vrik_relay_sequence_valid[i] = true;
		recipient->vrik_relay_generation[i] = source->vrik_generation;
		recipient->vrik_relay_sequence[i] = source->vrik_last_sequence;
	}
	return true;
}

static server_voice_packet_t *SV_VoicePacketForSerial (client_t *source,
	uint64_t serial)
{
	uint64_t oldest;
	server_voice_packet_t *packet;

	if (!source->voice_next_serial || !serial ||
		serial > source->voice_next_serial)
		return NULL;
	oldest = source->voice_next_serial >= VOICE_SERVER_QUEUE_CAPACITY ?
		source->voice_next_serial - VOICE_SERVER_QUEUE_CAPACITY + 1 : 1;
	if (serial < oldest)
		return NULL;
	packet = &source->voice_packets[(serial - 1) % VOICE_SERVER_QUEUE_CAPACITY];
	return packet->serial == serial ? packet : NULL;
}

static void SV_WriteVoicePacket (sizebuf_t *msg, int source_slot,
	unsigned int generation, const voice_packet_t *packet)
{
	MSG_WriteByte (msg, svc_voice);
	MSG_WriteByte (msg, source_slot + 1);
	MSG_WriteLong (msg, (int)generation);
	MSG_WriteShort (msg, packet->sequence);
	MSG_WriteLong (msg, (int)packet->timestamp);
	MSG_WriteByte (msg, packet->talkspurt);
	MSG_WriteByte (msg, packet->flags);
	MSG_WriteShort (msg, packet->payload_bytes);
	if (packet->payload_bytes)
		SZ_Write (msg, packet->payload, packet->payload_bytes);
}

/* Voice gets its own small datagram after snapshots and VRIK have been sent. */
static qboolean SV_SendPendingVoice (client_t *recipient)
{
	byte data[VOICE_SERVER_DATAGRAM_BUDGET];
	sizebuf_t msg = {0};
	uint64_t serial_cursor[MAX_SCOREBOARD] = {0};
	uint64_t sent_serial[MAX_SCOREBOARD] = {0};
	qboolean has_sent_serial[MAX_SCOREBOARD] = {false};
	int source_count, start, round, offset;
	int send_result;
	qboolean full = false;

	if (!sv_voice.value || !recipient->active || !recipient->spawned ||
		!recipient->voice_capable)
		return true;
	source_count = q_min (svs.maxclients, MAX_SCOREBOARD);
	if (source_count <= 1 || recipient->limit_unreliable <= 0)
		return true;
	msg.data = data;
	msg.maxsize = q_min ((int)sizeof (data), (int)recipient->limit_unreliable);
	msg.allowoverflow = false;
	start = recipient->voice_relay_next_source % source_count;
	for (offset = 0; offset < source_count; ++offset)
	{
		client_t *source = &svs.clients[offset];
		uint64_t oldest;

		if (source == recipient || !source->active || !source->spawned ||
			!source->voice_capable ||
			!source->voice_generation || !source->voice_next_serial)
			continue;
		oldest = source->voice_next_serial >= VOICE_SERVER_QUEUE_CAPACITY ?
			source->voice_next_serial - VOICE_SERVER_QUEUE_CAPACITY + 1 : 1;
		if (recipient->voice_relay_generation[offset] != source->voice_generation)
		{
			recipient->voice_relay_generation[offset] = source->voice_generation;
			recipient->voice_relay_serial[offset] = oldest - 1;
		}
		if (recipient->voice_relay_serial[offset] < oldest - 1)
			recipient->voice_relay_serial[offset] = oldest - 1;
		serial_cursor[offset] = recipient->voice_relay_serial[offset];
	}

	for (round = 0; round < VOICE_SERVER_PACKETS_PER_SOURCE_TICK && !full;
		++round)
	{
		for (offset = 0; offset < source_count; ++offset)
		{
			int source_slot = (start + offset) % source_count;
			client_t *source = &svs.clients[source_slot];
			server_voice_packet_t *queued;
			uint64_t serial;
			int required;

			if (source == recipient || !source->active || !source->spawned ||
				!source->voice_capable || !source->voice_generation ||
				!source->voice_next_serial)
				continue;
			serial = serial_cursor[source_slot] + 1;
			queued = SV_VoicePacketForSerial (source, serial);
			while (queued && realtime - queued->arrival_time >
				VOICE_SERVER_MAX_PACKET_AGE)
			{
				serial_cursor[source_slot] = serial;
				recipient->voice_relay_serial[source_slot] = serial++;
				queued = SV_VoicePacketForSerial (source, serial);
			}
			if (!queued)
				continue;

			required = VOICE_SVC_HEADER_BYTES + queued->packet.payload_bytes;
			if (required > msg.maxsize - msg.cursize)
			{
				full = true;
				break;
			}
			SV_WriteVoicePacket (&msg, source_slot, source->voice_generation,
				&queued->packet);
			if (msg.overflowed)
				return true;
			serial_cursor[source_slot] = serial;
			sent_serial[source_slot] = serial;
			has_sent_serial[source_slot] = true;
		}
	}
	if (!msg.cursize)
		return true;
	send_result = NET_SendUnreliableMessage (recipient->netconnection, &msg);
	if (send_result < 0)
	{
		host_client = recipient;
		SV_DropClient (false);
		return false;
	}
	if (send_result == 1)
	{
		/* Live packets are retired only after the datagram entered the net queue. */
		for (offset = 0; offset < source_count; ++offset)
			if (has_sent_serial[offset])
				recipient->voice_relay_serial[offset] = sent_serial[offset];
		recipient->voice_relay_next_source =
			(unsigned char)((start + 1) % source_count);
	}
	return true;
}

qboolean SV_PrivateWalkTrialSelected (client_t *client)
{
	return client && client->private_pmove_walk_selected;
}

static qboolean SV_PrivateWalkStatsDisjoint (void);

qboolean SV_PrivateWalkTrialStockProgram (void)
{
	return qcvm == &sv.qcvm && qcvm->progssize == 340014 &&
		qcvm->progscrc == 0x0bf8 && qcvm->progshash == 0xcf69c3e2;
}

static const char *SV_PrivateWalkTrialAdmissionFailure (client_t *client)
{
	const qboolean q30 = SV_PrivateWalkTrialQ30Program ();

	if (!client || !client->active || !client->knowntoqc || !client->edict || client->edict->free)
		return "client is not a live spawned owner";
	if (!client->netconnection)
		return "requires a connected client";
	if (client->protocol_qsvr != QSVR_PROTOCOL_PINNED)
		return "requires the pinned private profile";
	if (q30 && !(client->offered_pmove_policies & QSVR_PMOVE_CAP_Q30_JUMP))
		return "requires the q30 ordinary jump policy";
	if (!SV_PrivateWalkStatsDisjoint ())
		return "custom stats overlap private movement stats";
	/* Selection is not permission to run PMove. Admitted owners retain their
	 * classified engine/cooperative/native movement and publication contracts. */
	if (SV_PrivateWalkTrialSelected (client))
		return SV_PrivateWalkTrialFrameStateError (client->edict, client, &client->cmd);
	/* A legacy mode cannot handle later selected platform contacts, even if
	 * the initial spawn is on static floor. Keep that session native. */
	if (sv_gameplayfix_elevators.value < 3.f)
		return "requires robust elevator physics";
	/* Session selection uses the same observational owner checks for stock
	 * and mods. Wet WALK and supported native starts may return to prediction. */
	const char *failure = SV_PrivateWalkTrialBeginStateError (client, &client->cmd);
	movevars_t vars;
	if (failure)
		return failure;
	/* Even native authority must publish this owner's movement stats. */
	if (!SV_PrivateWalkTrialBuildMoveVars (client, &vars))
		return "invalid initial movement settings";
	return NULL;
}

void SV_PrivateWalkTrialSelectAtBegin (client_t *client)
{
	const char *reason;

	if (!client || client->spawned || client->private_pmove_walk_selected ||
		!sv_private_pmove_walk.value)
		return;
	reason = SV_PrivateWalkTrialAdmissionFailure (client);
	if (reason)
	{
		Sys_Printf ("%s: using native movement: %s\n", client->name, reason);
		return;
	}

	SV_ResetPrivateCommandQueue (client);
	SV_ResetPrivateVRContactState (client);
	client->private_latest_buttons = 0;
	client->private_latched_buttons = 0;
	client->private_latched_impulse = 0;
	client->lastmovetime = 0;
	client->private_pmove_walk_selected = true;
	Sys_Printf ("%s: selected %s predictive movement\n", client->name,
		SV_PrivateWalkTrialStockProgram () ? "stock" :
		SV_PrivateWalkTrialQ30Program () ? "q30" : "shared QC");
}

/*
=============
SV_UsePredThinkPos
=============
*/
static qboolean SV_UsePredThinkPos (edict_t *ent)
{
	extern cvar_t r_lerpmove;
	if (!sv_smoothplatformlerps.value || (!isDedicated && !r_lerpmove.value))
		return false;
	if (ent->v.movetype != MOVETYPE_STEP)
		return false;
	if (!((int)ent->v.flags & FL_ONGROUND))
		return false;
	float elapsedtime = qcvm->time - ent->lastthink;
	if (elapsedtime < 0 || elapsedtime > 0.1)
		return false;
	return true;
}

//============================================================================

static qboolean SV_AmmoCapacityValuesValid (const float values[4],
	const edict_t *ent)
{
	const float current[4] = {
		ent->v.ammo_shells, ent->v.ammo_nails,
		ent->v.ammo_rockets, ent->v.ammo_cells
	};
	int i;

	for (i = 0; i < 4; ++i)
		if (!isfinite (values[i]) || values[i] <= 0.0f ||
		    floorf (values[i]) != values[i] || values[i] > 1000000.0f ||
		    values[i] < current[i])
			return false;
	return true;
}

static qboolean SV_ReadAmmoCapacityFields (edict_t *ent, float values[4])
{
	static const char *names[4] = {
		"maxshells", "maxnails", "maxrockets", "maxcells"
	};
	int i;

	for (i = 0; i < 4; ++i)
	{
		ddef_t *def = ED_FindField (names[i]);
		eval_t *value;

		if (!def || (def->type & ~DEF_SAVEGLOBAL) != ev_float)
			return false;
		value = GetEdictFieldValue (ent, def->ofs);
		if (!value)
			return false;
		values[i] = value->_float;
	}
	return SV_AmmoCapacityValuesValid (values, ent);
}

static qboolean SV_ReadAmmoCapacityGlobals (edict_t *ent,
	const char *const names[4], qboolean require_saveglobal, float values[4])
{
	int i;

	for (i = 0; i < 4; ++i)
	{
		ddef_t *def = ED_FindGlobal (names[i]);

		if (!def || (def->type & ~DEF_SAVEGLOBAL) != ev_float ||
		    def->ofs >= qcvm->progs->numglobals ||
		    (require_saveglobal && !(def->type & DEF_SAVEGLOBAL)))
			return false;
		values[i] = G_FLOAT (def->ofs);
	}
	return SV_AmmoCapacityValuesValid (values, ent);
}

static void SV_WriteAmmoCapacityStats (edict_t *ent, int *statsi)
{
	static const char *const saveglobal_names[4] = {
		"ammo_shells_max", "ammo_nails_max", "ammo_rockets_max",
		"ammo_cells_max"
	};
	static const char *const max_ammo_names[4] = {
		"MAX_AMMO_SHELLS", "MAX_AMMO_NAILS", "MAX_AMMO_ROCKETS",
		"MAX_AMMO_CELLS"
	};
	static const char *const ammo_max_names[4] = {
		"AMMO_MAXSHELLS", "AMMO_MAXNAILS", "AMMO_MAXROCKETS",
		"AMMO_MAXCELLS"
	};
	static const int stats[4] = {
		STAT_VR_MAX_SHELLS, STAT_VR_MAX_NAILS,
		STAT_VR_MAX_ROCKETS, STAT_VR_MAX_CELLS
	};
	float values[4];
	int i;

	/* Prefer per-player fields, then coherent dynamic/static global families.
	 * Never combine names from different conventions. */
	if (!SV_ReadAmmoCapacityFields (ent, values) &&
	    !SV_ReadAmmoCapacityGlobals (ent, saveglobal_names, true, values) &&
	    !SV_ReadAmmoCapacityGlobals (ent, max_ammo_names, false, values) &&
	    !SV_ReadAmmoCapacityGlobals (ent, ammo_max_names, false, values))
		return;

	for (i = 0; i < 4; ++i)
		statsi[stats[i]] = (int)values[i];
}

static qboolean SV_ReadOptionalInventoryField (edict_t *ent, const char *name, int *destination)
{
	ddef_t *def = ED_FindField (name);
	eval_t *value;
	float number;
	int64_t normalized;

	if (!def || (def->type & ~DEF_SAVEGLOBAL) != ev_float)
		return false;
	value = GetEdictFieldValue (ent, def->ofs);
	if (!value)
		return false;
	number = value->_float;
	if (!isfinite (number) || number < -2147483648.0 || number >= 4294967296.0)
		return false;

	/* Truncate the numeric QC value, then preserve its 32-bit transport bits. */
	normalized = (int64_t)number;
	if (normalized >= INT64_C (2147483648))
		normalized -= INT64_C (4294967296);
	*destination = (int)normalized;
	return true;
}

void SV_CalcStats (client_t *client, int *statsi, float *statsf, const char **statss)
{
	size_t	 i;
	edict_t *ent = client->edict;
	int		 items;
	int		 items2 = 0;
	qboolean items2_valid = SV_ReadOptionalInventoryField (ent, "items2", &items2);
	eval_t	*val;
	if (items2_valid)
		items = (int)((uint32_t)ent->v.items | ((uint32_t)items2 << 23));
	else
		items = (int)((uint32_t)ent->v.items | ((uint32_t)pr_global_struct->serverflags << 28));

	memset (statsi, 0, sizeof (*statsi) * MAX_CL_STATS);
	memset (statsf, 0, sizeof (*statsf) * MAX_CL_STATS);
	memset ((void *)statss, 0, sizeof (*statss) * MAX_CL_STATS);
	statsf[STAT_HEALTH] = ent->v.health;
	statsi[STAT_WEAPON] = SV_ModelIndex (PR_GetString (ent->v.weaponmodel));
	if ((unsigned int)statsi[STAT_WEAPON] >= client->limit_models)
		statsi[STAT_WEAPON] = 0;
	statsf[STAT_AMMO] = ent->v.currentammo;
	statsf[STAT_ARMOR] = ent->v.armorvalue;
	statsf[STAT_WEAPONFRAME] = ent->v.weaponframe;
	statsf[STAT_SHELLS] = ent->v.ammo_shells;
	statsf[STAT_NAILS] = ent->v.ammo_nails;
	statsf[STAT_ROCKETS] = ent->v.ammo_rockets;
	statsf[STAT_CELLS] = ent->v.ammo_cells;
	statsf[STAT_ACTIVEWEAPON] = ent->v.weapon; // sent in a way that does NOT depend upon the current mod...
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.viewzoom)) && val->_float)
	{
		statsf[STAT_VIEWZOOM] = val->_float * 255;
		if (statsf[STAT_VIEWZOOM] < 1)
			statsf[STAT_VIEWZOOM] = 1;
	}

	if (client->protocol_pext2 & PEXT2_PREDINFO)
	{ // predinfo also kills clc_clientdata
		statsi[STAT_ITEMS] = items;
		statsf[STAT_VIEWHEIGHT] = ent->v.view_ofs[2];
		statsf[STAT_IDEALPITCH] = ent->v.idealpitch;
		statsf[STAT_PUNCHANGLE_X] = ent->v.punchangle[0];
		statsf[STAT_PUNCHANGLE_Y] = ent->v.punchangle[1];
		statsf[STAT_PUNCHANGLE_Z] = ent->v.punchangle[2];
	}

	/* Preserve mod-owned inventory channels for the shared desktop/VR wheel.
	 * Absent or invalid optional QuakeC fields leave their stats zero. */
	SV_ReadOptionalInventoryField (ent, "weapons", &statsi[STAT_VR_WEAPONS]);
	statsi[STAT_VR_ITEMS2] = items2;
	if (!SV_ReadOptionalInventoryField (ent, "moditems", &statsi[STAT_VR_MODITEMS]) &&
		!SV_ReadOptionalInventoryField (ent, "items_dwell", &statsi[STAT_VR_MODITEMS]))
		SV_ReadOptionalInventoryField (ent, "items_snack", &statsi[STAT_VR_MODITEMS]);
	SV_ReadOptionalInventoryField (ent, "weapon2", &statsi[STAT_VR_WEAPON2]);
	SV_ReadOptionalInventoryField (ent, "weapons2", &statsi[STAT_VR_WEAPONS2]);
	SV_WriteAmmoCapacityStats (ent, statsi);

	for (i = 0; i < sv.numcustomstats; i++)
	{
		eval_t *eval = sv.customstats[i].ptr;
		if (!eval)
			eval = GetEdictFieldValue (ent, sv.customstats[i].fld);

		switch (sv.customstats[i].type)
		{
		case ev_ext_integer:
			statsi[sv.customstats[i].idx] = eval->_int;
			break;
		case ev_ext_uint32:
			statsi[sv.customstats[i].idx] = eval->_uint32;
			break;
		case ev_ext_sint64:
			statsi[sv.customstats[i].idx + 0] = eval->_sint64;
			statsi[sv.customstats[i].idx + 1] = eval->_sint64 >> 32;
			break;
		case ev_ext_uint64:
			statsi[sv.customstats[i].idx + 0] = eval->_uint64;
			statsi[sv.customstats[i].idx + 1] = eval->_uint64 >> 32;
			break;
		case ev_ext_double:
			statsf[sv.customstats[i].idx] = eval->_double; // FIXME: precision loss
			break;
		case ev_entity:
			statsi[sv.customstats[i].idx] = NUM_FOR_EDICT (PROG_TO_EDICT (eval->edict));
			break;
		case ev_float:
			statsf[sv.customstats[i].idx] = eval->_float;
			break;
		case ev_vector:
			statsf[sv.customstats[i].idx + 0] = eval->vector[0];
			statsf[sv.customstats[i].idx + 1] = eval->vector[1];
			statsf[sv.customstats[i].idx + 2] = eval->vector[2];
			break;
		case ev_string:
			statss[sv.customstats[i].idx] = PR_GetString (eval->string);
			break;
		case ev_void:	  // nothing...
		case ev_field:	  // panic! everyone panic!
		case ev_function: // doesn't make much sense
		case ev_pointer:  // doesn't make sense
		default:
			break;
		}
	}

	/* Keep the movement policy bit authoritative over mod custom stat 240. */
	if (coop.value && SV_CoopFeatureEnabled (&sv_coop_noplayerclip, true))
		statsi[STAT_VR_COOP_POLICY] |= VR_COOP_POLICY_NO_PLAYER_CLIP;

}

/*server-side-only flags that re-use encoding bits*/
#define UF_REMOVE		   UF_16BIT	   /*says we removed the entity in this frame*/
#define UF_MOVETYPE		   UF_EFFECTS2 /*this flag isn't present in the header itself*/
#define UF_RESET2		   UF_EXTEND1  /*so new ents are reset multiple times to avoid weird baselines*/
// #define UF_UNUSED		UF_EXTEND2	/**/
#define UF_WEAPONFRAME_OLD UF_EXTEND2
#define UF_VIEWANGLES	   UF_EXTEND3 /**/

static unsigned int SVFTE_DeltaPredCalcBits (entity_state_t *from, entity_state_t *to)
{
	unsigned int bits = 0;
	//	if (from && from->pmovetype != to->pmovetype)
	//		bits |= UFP_MOVETYPE;

	//	if (to->movement[0])
	//		bits |= UFP_FORWARD;
	//	if (to->movement[1])
	//		bits |= UFP_SIDE;
	//	if (to->movement[2])
	//		bits |= UFP_UP;
	if (to->velocity[0])
		bits |= UFP_VELOCITYXY;
	if (to->velocity[1])
		bits |= UFP_VELOCITYXY;
	if (to->velocity[2])
		bits |= UFP_VELOCITYZ;
	//	if (to->msec)
	//		bits |= UFP_MSEC;

	return bits;
}

static unsigned int MSGFTE_DeltaCalcBits (entity_state_t *from, entity_state_t *to, qboolean private_qsvr)
{
	unsigned int bits = 0;

	if (from->pmovetype != to->pmovetype)
		bits |= UF_PREDINFO | UF_MOVETYPE;
	{
		if (SVFTE_DeltaPredCalcBits (from, to))
			bits |= UF_PREDINFO;

		// moving players get extra data forced upon them which is not deltatracked
		if ((bits & UF_PREDINFO) && (from->velocity[0] || from->velocity[1] || from->velocity[2]))
		{
			// if we've got player movement then write the origin anyway, to cover packetloss
			bits |= UF_ORIGINXY | UF_ORIGINZ;
		}
	}

	if (to->origin[0] != from->origin[0])
		bits |= UF_ORIGINXY;
	if (to->origin[1] != from->origin[1])
		bits |= UF_ORIGINXY;
	if (to->origin[2] != from->origin[2])
		bits |= UF_ORIGINZ;

	if (to->angles[0] != from->angles[0])
		bits |= UF_ANGLESXZ;
	if (to->angles[1] != from->angles[1])
		bits |= UF_ANGLESY;
	if (to->angles[2] != from->angles[2])
		bits |= UF_ANGLESXZ;

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
	if (private_qsvr && to->solidsize != from->solidsize)
		bits |= UF_SOLID;
	if (to->scale != from->scale)
		bits |= UF_SCALE;
	if (to->alpha != from->alpha)
		bits |= UF_ALPHA;
	if (to->colormod[0] != from->colormod[0] || to->colormod[1] != from->colormod[1] || to->colormod[2] != from->colormod[2])
		bits |= UF_COLORMOD;
	if (to->tagentity != from->tagentity || to->tagindex != from->tagindex)
		bits |= UF_TAGINFO;
	if (to->traileffectnum != from->traileffectnum || to->emiteffectnum != from->emiteffectnum)
		bits |= UF_TRAILEFFECT;
#ifdef LERP_BANDAID
	if (to->lerp != from->lerp)
		bits |= UF_UNUSED2;
#endif

	return bits;
}

static void MSG_WriteSize16 (sizebuf_t *msg, unsigned int solid)
{
	if (solid == ES_SOLID_BSP)
		MSG_WriteShort (msg, ES_SOLID_BSP);
	else if (solid)
	{
		int x = solid & 255;
		int zd = (solid >> 8) & 255;
		int zu = ((solid >> 16) & 65535) - 32768;
		MSG_WriteShort (msg, ((x >> 3) << 0) | (zd >> 3) << 5 | (((zu + 32) >> 3) << 10));
	}
	else
		MSG_WriteShort (msg, 0);
}

static qboolean MSG_SolidSizeHasExtraBits (unsigned int solid)
{
	return (solid & 0x0707) || (((solid >> 16) - 32768 + 32) & 7);
}

static void MSGFTE_WriteEntityUpdate (unsigned int bits, entity_state_t *state, sizebuf_t *msg, unsigned int pext2, unsigned int protocolflags, qboolean private_qsvr)
{
	unsigned int predbits = 0;
	if (bits & UF_MOVETYPE)
	{
		bits &= ~UF_MOVETYPE;
		predbits |= UFP_MOVETYPE;
	}
	if (pext2 & PEXT2_PREDINFO)
	{
		if (bits & UF_VIEWANGLES)
		{
			bits &= ~UF_VIEWANGLES;
			bits |= UF_PREDINFO;
			predbits |= UFP_VIEWANGLE;
		}
	}
	else
	{
		if (bits & UF_VIEWANGLES)
		{
			bits &= ~UF_VIEWANGLES;
			bits |= UF_PREDINFO;
		}
		if (bits & UF_WEAPONFRAME_OLD)
		{
			bits &= ~UF_WEAPONFRAME_OLD;
			predbits |= UFP_WEAPONFRAME_OLD;
		}
	}

#ifdef LERP_BANDAID
	if (bits & UF_UNUSED2 && (cls.demorecording || strcmp (NET_QSocketGetTrueAddressString (host_client->netconnection), "LOCAL")))
		bits &= ~UF_UNUSED2;
#endif

	bits &= ~UF_BONEDATA;

	/*check if we need more precision for some things*/
	if ((bits & UF_MODEL) && state->modelindex > 255)
		bits |= UF_16BIT;
	if ((bits & UF_FRAME) && state->frame > 255)
		bits |= UF_16BIT;

	/*convert effects bits to higher lengths if needed*/
	if (bits & UF_EFFECTS)
	{
		if (state->effects & 0xffff0000) /*both*/
			bits |= UF_EFFECTS | UF_EFFECTS2;
		else if (state->effects & 0x0000ff00) /*2 only*/
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
	{ /*if we have pred info, (always) use more precise angles*/
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
		/*movetype is set above somewhere*/
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
		MSG_WriteByte (msg, state->colormap & 0xff);
	if (bits & UF_SOLID)
	{
		if (private_qsvr)
		{
			if (!state->solidsize)
				MSG_WriteByte (msg, 0);
			else if (state->solidsize == ES_SOLID_BSP)
				MSG_WriteByte (msg, 1);
			else if (state->solidsize == ES_SOLID_HULL1)
				MSG_WriteByte (msg, 2);
			else if (state->solidsize == ES_SOLID_HULL2)
				MSG_WriteByte (msg, 3);
			else if (!MSG_SolidSizeHasExtraBits (state->solidsize))
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
		{ // 3 spare bits. so that's nice (this is guarenteed to be 14 bits max due to precaches using the upper two bits).
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

#ifdef LERP_BANDAID
	if (bits & UF_UNUSED2)
		MSG_WriteShort (msg, state->lerp);
#endif
}

static struct entity_num_state_s *snapshot_entstate;
static size_t					  snapshot_numents;
static size_t					  snapshot_maxents;

void SVFTE_DestroyFrames (client_t *client)
{
	int i;
	for (i = 0; i < MAX_CL_STATS; i++)
	{
		if (!client->oldstats_s[i])
			continue;
		Mem_Free (client->oldstats_s[i]);
		client->oldstats_s[i] = 0;
	}
	if (client->previousentities)
		Mem_Free (client->previousentities);
	client->previousentities = NULL;
	client->numpreviousentities = 0;
	client->maxpreviousentities = 0;

	if (client->pendingentities_bits)
		Mem_Free (client->pendingentities_bits);
	client->pendingentities_bits = NULL;
	client->numpendingentities = 0;

	Mem_Free (client->csqcentities_remove_boundary);
	client->csqcentities_remove_boundary = NULL;
	Mem_Free (client->pendingcsqcentities_bits);
	client->pendingcsqcentities_bits = NULL;
	client->numpendingcsqcentities = 0;
	client->csqcsnapshotresume = 1;
	client->csqcactive = false;

	while (client->numframes > 0)
	{
		client->numframes--;
		Mem_Free (client->frames[client->numframes].ents);
	}
	if (client->frames)
		Mem_Free (client->frames);
	client->frames = NULL;

	client->lastacksequence = 0;
}
static void SVFTE_SetupFrames (client_t *client)
{
	size_t fr;
	// the client will clear out their stats on receipt of the svc_serverinfo packet.
	// we won't send any reliables until they receive it
	// so it should be enough to just clear these here, and they'll get their new stats with the first entity update once they're spawned
	memset (client->oldstats_i, 0, sizeof (client->oldstats_i));
	memset (client->oldstats_f, 0, sizeof (client->oldstats_f));
	client->lastmovemessage = 0; // it'll clear this too
	client->private_completed_move = 0;

	if (!client->protocol_pext2)
	{
		SVFTE_DestroyFrames (client);
		return;
	}

	client->numframes = 64; // must be power-of-two
	client->frames = Mem_Alloc (sizeof (*client->frames) * client->numframes);
	client->lastacksequence = (int)0x80000000;
	memset (client->frames, 0, sizeof (*client->frames) * client->numframes);
	for (fr = 0; fr < client->numframes; fr++)
		client->frames[fr].sequence = client->lastacksequence;

	client->numpendingentities = qcvm->num_edicts;
	client->pendingentities_bits = Mem_Alloc (client->numpendingentities * sizeof (*client->pendingentities_bits));

	client->pendingentities_bits[0] = UF_REMOVE;
	client->numpendingcsqcentities = qcvm->num_edicts;
	client->pendingcsqcentities_bits = Mem_Alloc (client->numpendingcsqcentities * sizeof (*client->pendingcsqcentities_bits));
	client->csqcentities_remove_boundary = Mem_Alloc (client->numpendingcsqcentities * sizeof (*client->csqcentities_remove_boundary));
	client->csqcsnapshotresume = 1;
}
static void SVFTE_DroppedFrame (client_t *client, int sequence)
{
	int					 i;
	struct deltaframe_s *frame = &client->frames[sequence & (client->numframes - 1)];
	if (frame->sequence != sequence)
		return; // this frame was stale... client is running too far behind. we'll probably be spamming resends as a result.
	frame->sequence = -1;
	// flag their stats for resend
	for (i = 0; i < MAX_CL_STATS / 32; i++)
	{
		client->resendstatsnum[i] |= frame->resendstatsnum[i];
		client->resendstatsstr[i] |= frame->resendstatsstr[i];
	}
	// flag their various entities as needing a resend too.
	for (i = 0; i < frame->numents; i++)
	{
		if (frame->ents[i].ebits)
			client->pendingentities_bits[frame->ents[i].num] |= frame->ents[i].ebits;
		if (frame->ents[i].csqcbits && frame->ents[i].num < client->numpendingcsqcentities)
			client->pendingcsqcentities_bits[frame->ents[i].num] |=
				frame->ents[i].csqcbits & (SENDFLAG_USABLE | SENDFLAG_REMOVE);
	}
}
// Reuse the primary packet owner, including packets without ordinary stat writes.
static struct deltaframe_s *SVFTE_BeginFrame (client_t *client)
{
	int sequence = NET_QSocketGetSequenceOut (client->netconnection);
	struct deltaframe_s *frame = &client->frames[sequence & (client->numframes - 1)];
	if (frame->sequence != sequence)
	{
		if (frame->sequence > client->lastacksequence)
			SVFTE_DroppedFrame (client, frame->sequence);
		frame->sequence = sequence;
		frame->timestamp = qcvm->time;
		memset (frame->resendstatsnum, 0, sizeof (frame->resendstatsnum));
		memset (frame->resendstatsstr, 0, sizeof (frame->resendstatsstr));
		frame->numents = 0;
	}
	return frame;
}

static qboolean SV_CSQCTransportAllowed (const client_t *client)
{
	return (sv_protocol_pext1 & PEXT1_CSQC) &&
		(client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS) &&
		(client->protocol_qsvr == QSVR_PROTOCOL_PINNED ||
		 (client->protocol_pext1 & PEXT1_CSQC));
}

void SV_SetCSQCActive (client_t *client, qboolean active)
{
	client->csqcactive = active && SV_CSQCTransportAllowed (client);
	if (client->csqcactive)
		for (size_t e = 1; e < client->numpendingcsqcentities; ++e)
			if (client->pendingcsqcentities_bits[e] & SENDFLAG_PRESENT)
				client->pendingcsqcentities_bits[e] |= SENDFLAG_USABLE;
}

static unsigned int SVFTE_RetireCSQCBits (unsigned int bits)
{
	if (bits & SENDFLAG_PRESENT)
		bits |= SENDFLAG_REMOVE | SENDFLAG_RETIRENEW;
	return bits & (SENDFLAG_PRESENT | SENDFLAG_REMOVE | SENDFLAG_REMOVEWAIT | SENDFLAG_RETIRENEW);
}

void SV_CSQCEntityFreed (edict_t *ed)
{
	if (qcvm != &sv.qcvm)
		return;
	unsigned int entnum = NUM_FOR_EDICT (ed);
	for (int i = 0; i < svs.maxclients; ++i)
	{
		client_t *client = &svs.clients[i];
		if (entnum >= client->numpendingcsqcentities)
			continue;
		unsigned int bits = client->pendingcsqcentities_bits[entnum];
		client->pendingcsqcentities_bits[entnum] = SVFTE_RetireCSQCBits (bits);
	}
}

static void SVFTE_ResetCSQCState (client_t *client)
{
	for (size_t e = 1; e < client->numpendingcsqcentities; ++e)
		if (client->pendingcsqcentities_bits[e] & SENDFLAG_CURRENT)
			client->pendingcsqcentities_bits[e] |= SENDFLAG_USABLE;
	client->csqcsnapshotresume = 1;
}

void SVFTE_Ack (client_t *client, int sequence)
{ // any gaps in the sequence need to considered dropped
	struct deltaframe_s *frame;
	int					 dropseq = client->lastacksequence + 1;
	if (!client->numframes)
		return; // client shouldn't be using this.
	if (sequence == -1)
		client->pendingentities_bits[0] |=
			UF_REMOVE; // client wants a full resend. which might happen from it just starting to record a demo, saving it from writing all the deltas out.
	if (sequence < client->lastacksequence)
	{
		//		else Con_SafePrintf("dupe or stale ack (%s, %i->%i)\n", client->name, client->lastacksequence, sequence);
		return; // panic
	}
	if ((unsigned)(dropseq - sequence) >= client->numframes)
		dropseq = sequence - client->numframes;
	while (dropseq < sequence)
		SVFTE_DroppedFrame (client, dropseq++);
	client->lastacksequence = sequence;

	frame = &client->frames[sequence & (client->numframes - 1)];
	if (frame->sequence == sequence)
	{
		for (int i = 0; i < frame->numents; ++i)
		{
			unsigned int e = frame->ents[i].num;
			if ((frame->ents[i].csqcbits & SENDFLAG_REMOVE) && e < client->numpendingcsqcentities &&
				(client->pendingcsqcentities_bits[e] & (SENDFLAG_REMOVEWAIT | SENDFLAG_RETIRENEW)) == SENDFLAG_REMOVEWAIT &&
				sequence >= client->csqcentities_remove_boundary[e])
				client->pendingcsqcentities_bits[e] &= ~SENDFLAG_REMOVEWAIT;
		}
		frame->sequence = -1;
		host_client->ping_times[host_client->num_pings % NUM_PING_TIMES] = qcvm->time - frame->timestamp;
		host_client->num_pings++;
	}
}

static const int sv_private_move_float_stats[] = {
	STAT_MOVEVARS_WATERSINKSPEED, STAT_MOVEVARS_FLYFRICTION,
	STAT_MOVEVARS_BUNNYSPEEDCAP, STAT_MOVEVARS_KTJUMP,
	STAT_MOVEVARS_FRICTION, STAT_MOVEVARS_WATERFRICTION,
	STAT_MOVEVARS_TIMESCALE, STAT_MOVEVARS_GRAVITY, STAT_MOVEVARS_STOPSPEED,
	STAT_MOVEVARS_MAXSPEED, STAT_MOVEVARS_SPECTATORMAXSPEED,
	STAT_MOVEVARS_ACCELERATE, STAT_MOVEVARS_AIRACCELERATE,
	STAT_MOVEVARS_WATERACCELERATE, STAT_MOVEVARS_ENTGRAVITY,
	STAT_MOVEVARS_JUMPVELOCITY, STAT_MOVEVARS_EDGEFRICTION,
	STAT_MOVEVARS_MAXAIRSPEED, STAT_MOVEVARS_STEPHEIGHT,
	STAT_PRIVATE_JUMP_SECS, STAT_PRIVATE_WATERJUMP_SECS};

static qboolean SV_IsPrivateMoveStat (int stat)
{
	return (stat == STAT_PRIVATE_QC_MAXVELOCITY && SV_PrivateWalkTrialQ30Program ()) ||
		stat == STAT_MOVEFLAGS ||
		stat == STAT_PRIVATE_JUMP_SECS ||
		stat == STAT_PRIVATE_WATERJUMP_SECS ||
		(stat >= STAT_MOVEVARS_WATERSINKSPEED && stat <= STAT_MOVEVARS_KTJUMP) ||
		(stat >= STAT_MOVEVARS_FRICTION && stat <= STAT_MOVEVARS_WATERFRICTION) ||
		(stat >= STAT_MOVEVARS_TIMESCALE && stat <= STAT_MOVEVARS_STEPHEIGHT);
}

/* The selected path sends movement slots on its own snapshot. A mod's other
 * custom stats remain available; only actual overlap would hide their data. */
static qboolean SV_PrivateWalkStatsDisjoint (void)
{
	for (size_t i = 0; i < sv.numcustomstats; ++i)
	{
		const int first = sv.customstats[i].idx;
		const int width = sv.customstats[i].type == ev_vector ? 3 :
			(sv.customstats[i].type == ev_ext_sint64 ||
			 sv.customstats[i].type == ev_ext_uint64 ? 2 : 1);
		if (first < 0 || first > MAX_CL_STATS - width)
			return false;
		for (int slot = first; slot < first + width; ++slot)
			if (SV_IsPrivateMoveStat (slot))
				return false;
	}
	return true;
}

qboolean SV_PrivateWalkTrialQ30Program (void)
{
	static const byte q30_sha256[32] = {
		0x5e, 0x69, 0xfe, 0xce, 0x92, 0xfb, 0x43, 0x23,
		0x60, 0x9c, 0x8e, 0x12, 0x09, 0xa3, 0x9e, 0xec,
		0xf4, 0xf7, 0x0c, 0x31, 0x61, 0xae, 0x17, 0xbe,
		0xb5, 0x30, 0x63, 0xfe, 0x3e, 0x06, 0xc3, 0x40
	};
	return qcvm == &sv.qcvm && qcvm->progssize == 2347206 &&
		!memcmp (qcvm->progssha256, q30_sha256, sizeof (q30_sha256));
}

/* One selected-policy producer for solver setup and complete owner snapshots.
 * Actual QC still owns the server impulse; this height also feeds its client
 * consumer. An older peer never gains the policy from program identity alone. */
qboolean SV_PrivateWalkTrialBuildMoveVars (client_t *client, movevars_t *out)
{
	extern cvar_t sv_maxvelocity;
	ddef_t *height;
	if (!client || !PMSV_BuildMoveVars (out, client->edict, sv.protocolflags))
		return false;
	if (SV_ClientInstantStopEnabled (client))
		out->flags |= MOVEFLAG_VR_INSTANT_STOP;
	if (!SV_PrivateWalkTrialQ30Program () ||
		!(client->offered_pmove_policies & QSVR_PMOVE_CAP_Q30_JUMP))
		return true;
	height = ED_FindGlobal ("map_jumpheight");
	if (!height || (height->type & ~DEF_SAVEGLOBAL) != ev_float ||
		height->ofs >= qcvm->progs->numglobals || !isfinite (G_FLOAT (height->ofs)) ||
		!isfinite (sv_maxvelocity.value) || sv_maxvelocity.value < 0)
		return false;
	out->jumpspeed = G_FLOAT (height->ofs);
	out->qc_maxvelocity = sv_maxvelocity.value;
	out->flags |= MOVEFLAG_QC_JUMP_ORDINARY;
	return true;
}

static qboolean SVFTE_WritePrivateMoveStats (client_t *client, sizebuf_t *msg)
{
	movevars_t private_movevars;
	int statsi[MAX_CL_STATS] = {0};
	float statsf[MAX_CL_STATS] = {0};
	size_t required = 1 + 1 + 4 + countof (sv_private_move_float_stats) * (1 + 1 + 4);
	size_t i;

	if (!isfinite (client->private_pmove_jump_secs) || client->private_pmove_jump_secs < 0.0f)
		return false;
	if (!isfinite (client->private_pmove_waterjump_secs) ||
		client->private_pmove_waterjump_secs < 0.0f ||
		client->private_pmove_waterjump_secs > 2.0f)
		return false;
	if (!SV_PrivateWalkStatsDisjoint ())
		return false;
	if (!SV_PrivateWalkTrialBuildMoveVars (client, &private_movevars))
		return false;
	if (private_movevars.flags & MOVEFLAG_QC_JUMP_ORDINARY)
		required += 1 + 1 + 4;
	if (!PMSV_ExportMoveStats (&private_movevars, statsf, statsi))
		return false;
	statsf[STAT_PRIVATE_JUMP_SECS] = client->private_pmove_jump_secs;
	statsf[STAT_PRIVATE_WATERJUMP_SECS] = client->private_pmove_waterjump_secs;
	if (msg->cursize < 0 || msg->maxsize < 0 || msg->cursize > msg->maxsize ||
		required > (size_t)(msg->maxsize - msg->cursize))
		return false;

	MSG_WriteByte (msg, svc_updatestat);
	MSG_WriteByte (msg, STAT_MOVEFLAGS);
	MSG_WriteLong (msg, statsi[STAT_MOVEFLAGS]);
	for (i = 0; i < countof (sv_private_move_float_stats); i++)
	{
		int stat = sv_private_move_float_stats[i];
		MSG_WriteByte (msg, svcfte_updatestatfloat);
		MSG_WriteByte (msg, stat);
		MSG_WriteFloat (msg, statsf[stat]);
	}
	if (private_movevars.flags & MOVEFLAG_QC_JUMP_ORDINARY)
	{
		MSG_WriteByte (msg, svcfte_updatestatfloat);
		MSG_WriteByte (msg, STAT_PRIVATE_QC_MAXVELOCITY);
		MSG_WriteFloat (msg, private_movevars.qc_maxvelocity);
	}
	return true;
}

static void SVFTE_WriteStats (client_t *client, sizebuf_t *msg, struct deltaframe_s *frame)
{
	int					 statsi[MAX_CL_STATS];
	float				 statsf[MAX_CL_STATS];
	const char			*statss[MAX_CL_STATS];
	int					 i;
	int					 maxstats;

	if (client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
		maxstats = MAX_CL_STATS;
	else
		maxstats = 32;

	// figure out the current values in a nice easy way (yay for copying to make arrays easier!)
	SV_CalcStats (client, statsi, statsf, statss);

	for (i = 0; i < maxstats; i++)
	{
		if (client->protocol_qsvr == QSVR_PROTOCOL_PINNED &&
			SV_PrivateWalkTrialSelected (client) && SV_IsPrivateMoveStat (i))
			continue;

		// small cleanup
		if (!statsi[i])
			statsi[i] = statsf[i];
		else
			statsf[i] = 0; // statsi[i];

		// if it changed flag for sending
		if (statsi[i] != client->oldstats_i[i] || statsf[i] != client->oldstats_f[i])
		{
			client->oldstats_i[i] = statsi[i];
			client->oldstats_f[i] = statsf[i];
			client->resendstatsnum[i / 32] |= 1u << (i & 31);
		}

		if (statss[i] || client->oldstats_s[i])
		{
			const char *os = client->oldstats_s[i];
			const char *ns = statss[i];
			if (!ns)
				ns = "";
			if (!os)
				os = "";
			if (strcmp (os, ns))
			{
				client->resendstatsstr[i / 32] |= 1u << (i & 31);
				Mem_Free (client->oldstats_s[i]);
				client->oldstats_s[i] = q_strdup (ns);
			}
		}

		// if its flagged then unflag it, log it, and send it
		if (client->resendstatsnum[i / 32] & (1u << (i & 31)))
		{
			client->resendstatsnum[i / 32] &= ~(1u << (i & 31));
			frame->resendstatsnum[i / 32] |= 1u << (i & 31);

			if ((double)statsi[i] != statsf[i] && statsf[i])
			{ // didn't round nicely, so send as a float
				MSG_WriteByte (msg, svcfte_updatestatfloat);
				MSG_WriteByte (msg, i);
				MSG_WriteFloat (msg, statsf[i]);
			}
			else
			{
				if (statsi[i] < 0 || statsi[i] > 255)
				{ // needs to be big
					MSG_WriteByte (msg, svc_updatestat);
					MSG_WriteByte (msg, i);
					MSG_WriteLong (msg, statsi[i]);
				}
				else
				{ // can be fairly small
					MSG_WriteByte (msg, svcdp_updatestatbyte);
					MSG_WriteByte (msg, i);
					MSG_WriteByte (msg, statsi[i]);
				}
			}
		}
		// if its flagged then unflag it, log it, and send it
		if (client->resendstatsstr[i / 32] & (1u << (i & 31)))
		{
			client->resendstatsstr[i / 32] &= ~(1u << (i & 31));
			frame->resendstatsstr[i / 32] |= 1u << (i & 31);

			MSG_WriteByte (msg, svcfte_updatestatstring);
			MSG_WriteByte (msg, i);
			if (statss[i])
				MSG_WriteString (msg, statss[i]);
			else
				MSG_WriteString (msg, NULL);
		}
	}
}
static void SVFTE_CalcEntityDeltas (client_t *client)
{
	struct entity_num_state_s *olds, *news, *oldstop, *newstop;

	if ((int)client->numpendingentities < qcvm->num_edicts)
	{
		int newmax = qcvm->num_edicts + 64;
		client->pendingentities_bits = Mem_Realloc (client->pendingentities_bits, sizeof (*client->pendingentities_bits) * newmax);
		memset (client->pendingentities_bits + client->numpendingentities, 0, sizeof (*client->pendingentities_bits) * (newmax - client->numpendingentities));
		client->numpendingentities = newmax;
	}

	// if we're clearing the list and starting from scratch, just wipe all lingering state
	if (client->pendingentities_bits[0] & UF_REMOVE)
	{
		client->numpreviousentities = 0;
		client->pendingentities_bits[0] = UF_REMOVE;
	}

	news = snapshot_entstate;
	newstop = news + snapshot_numents;
	olds = client->previousentities;
	oldstop = (olds != NULL) ? (olds + client->numpreviousentities) : NULL;

	// we have two sets of entity state, pvs culled etc already.
	// figure out which flags changed,
	for (;;)
	{
		if (olds == oldstop && news == newstop)
			break;
		if (news == newstop || (olds != oldstop && olds->num < news->num))
		{
			// old ent is no longer visible, so flag for removal.
			client->pendingentities_bits[olds->num] = UF_REMOVE;
			olds++;
		}
		else if (olds == oldstop || (news != newstop && news->num < olds->num))
		{
			// new ent is new this frame, so reset everything.
			client->pendingentities_bits[news->num] = UF_RESET;
			// don't need to calc the other bits here, resets are enough
			news++;
		}
		else
		{ // simple entity delta
			// its flagged for removing, that's weird... must be some killer packetloss. turn that back into a reset or something
			if (client->pendingentities_bits[news->num] & UF_REMOVE)
				client->pendingentities_bits[news->num] = (client->pendingentities_bits[news->num] & ~UF_REMOVE) | UF_RESET2;
			client->pendingentities_bits[news->num] |= MSGFTE_DeltaCalcBits (&olds->state, &news->state, client->protocol_qsvr == QSVR_PROTOCOL_PINNED);
			news++;
			olds++;
		}
	}

	// now we know what flags to apply, the client needs a copy of that state for the next frame too.
	// outgoing data can just read off these states too, instead of needing to hit the edicts memory (which may be spread over multiple allocations, yay cache).
	// to avoid a potentially large memcopy, I'm just going to swap these buffers.
	olds = client->previousentities;
	oldstop = (olds != NULL) ? (olds + client->maxpreviousentities) : NULL;

	client->previousentities = snapshot_entstate;
	client->numpreviousentities = snapshot_numents;
	client->maxpreviousentities = snapshot_maxents;

	snapshot_entstate = olds;
	snapshot_numents = 0;
	snapshot_maxents = (olds != NULL) ? (oldstop - olds) : 0;
}
static qboolean SV_GorillaAckStateIsFinite (const client_t *client)
{
	const vr_gorilla_state_t *state = &client->vr_gorilla_state;
	int hand, axis;

	if (state->initialized > 1 ||
		(state->touching & ~VR_GORILLA_HANDS) ||
		(state->recovering & ~VR_GORILLA_HANDS))
		return false;
	for (hand = 0; hand < 2; hand++)
	{
		if (state->surface[hand] < 0 || state->surface[hand] >= MAX_EDICTS ||
			state->surface_model[hand] >= QSVR_MODEL_LIMIT ||
			(!state->surface[hand] && state->surface_model[hand]) ||
			(state->surface[hand] && !state->surface_model[hand]))
			return false;
		for (axis = 0; axis < 3; axis++)
			if (!isfinite (state->anchor[hand][axis]) ||
				!isfinite (state->recovery_offset[hand][axis]))
				return false;
		if (DotProduct (state->recovery_offset[hand], state->recovery_offset[hand]) >
			VR_GORILLA_MAX_REACH * VR_GORILLA_MAX_REACH)
			return false;
	}
	for (axis = 0; axis < 3; axis++)
		if (!isfinite (state->velocity[axis]) || !isfinite (state->origin[axis]))
			return false;
	return true;
}

static qboolean SV_GorillaAckOriginMatchesOwner (const client_t *client)
{
	vec3_t delta;
	float distance;
	int axis;

	if (!client->edict || client->edict->free)
		return false;
	for (axis = 0; axis < 3; axis++)
		if (!isfinite (client->edict->v.origin[axis]))
			return false;
	VectorSubtract (client->edict->v.origin, client->vr_gorilla_state.origin, delta);
	distance = VectorLength (delta);
	return isfinite (distance) && distance <= .01f;
}

/* Frame interaction marks reset even when no new command is available. A
 * palm-only binding can outlive that mark without native body carry, so check
 * the existing accepted anchors before granting replay. This observes state;
 * it neither advances a hand solver nor creates another support lifetime. */
static qboolean SV_PrivateMoveHasPusherPalm (const client_t *client)
{
	const vr_gorilla_state_t *state = &client->vr_gorilla_state;
	if (!sv_gorilla.value || !client->vr_gorilla_capable || !state->initialized)
		return false;
	for (int hand = 0; hand < 2; ++hand)
	{
		int number = state->surface[hand];
		if (!(state->touching & (1 << hand)) || number <= 0 || number >= qcvm->num_edicts)
			continue;
		edict_t *surface = EDICT_NUM (number);
		if (!surface->free && surface->v.movetype == MOVETYPE_PUSH &&
			surface->v.solid == SOLID_BSP &&
			surface->v.modelindex == (float)state->surface_model[hand])
			return true;
	}
	return false;
}

static void SV_WriteGorillaAckState (client_t *client, sizebuf_t *msg)
{
	int hand, axis;

	MSG_WriteLong (msg, client->vr_gorilla_last_sequence);
	MSG_WriteByte (msg, client->vr_gorilla_state.initialized);
	MSG_WriteByte (msg, client->vr_gorilla_state.touching);
	MSG_WriteByte (msg, client->vr_gorilla_state.recovering);
	for (hand = 0; hand < 2; hand++)
		for (axis = 0; axis < 3; axis++)
			MSG_WriteFloat (msg, client->vr_gorilla_state.anchor[hand][axis]);
	for (hand = 0; hand < 2; hand++)
		for (axis = 0; axis < 3; axis++)
			MSG_WriteFloat (msg, client->vr_gorilla_state.recovery_offset[hand][axis]);
	for (axis = 0; axis < 3; axis++)
		MSG_WriteFloat (msg, client->vr_gorilla_state.velocity[axis]);
	for (axis = 0; axis < 3; axis++)
		MSG_WriteFloat (msg, client->vr_gorilla_state.origin[axis]);
	for (hand = 0; hand < 2; hand++)
		MSG_WriteLong (msg, client->vr_gorilla_state.surface[hand]);
	for (hand = 0; hand < 2; hand++)
		MSG_WriteLong (msg, client->vr_gorilla_state.surface_model[hand]);
}

static qboolean SVFTE_PredictionVelocityRepresentable (const vec3_t velocity)
{
	for (int i = 0; i < 3; ++i)
		if (!isfinite (velocity[i]) || velocity[i] < SHRT_MIN * .125f ||
			velocity[i] > SHRT_MAX * .125f)
			return false;
	return true;
}

static short SVFTE_EncodeVelocity (float velocity)
{
	/* This is a presentation seed, never a physics clamp. Private replay is
	 * withheld when the actual owner cannot fit the existing eighth-unit wire
	 * field. In-range conversion retains the existing truncation behavior. */
	if (!isfinite (velocity))
		return 0;
	const double encoded = (double)velocity * 8;
	if (encoded < SHRT_MIN)
		return SHRT_MIN;
	if (encoded > SHRT_MAX)
		return SHRT_MAX;
	return (short)encoded;
}

static qboolean SVFTE_WriteEntitiesToClient (client_t *client, sizebuf_t *msg,
	size_t overflowsize, qboolean continuation, struct deltaframe_s *frame)
{
	struct entity_num_state_s *state, *stateend;
	struct entity_num_state_s *ownerstate = NULL;
	unsigned int			   entbits, logbits, netbits;
	size_t					   entnum, ownernum = 0, i;
	qboolean				   selected = client->protocol_qsvr == QSVR_PROTOCOL_PINNED && SV_PrivateWalkTrialSelected (client);
	qboolean				   selected_engine = selected &&
		SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_WALK &&
		!client->private_move_native_frame;
	qboolean				   worldreset = false, wrote_optional = false;
	qboolean				   gorilla_ack = false;
	int					   ack_flags = 0;
	size_t					   origmaxsize = msg->maxsize;
	size_t					   rollbacksize; // I'm too lazy to figure out sizes (especially if someone updates this for bone states or whatever)

	msg->maxsize = overflowsize;

	state = client->previousentities;
	stateend = state + client->numpreviousentities;

	MSG_WriteByte (msg, svcfte_updateentities);

	if (client->protocol_qsvr == QSVR_PROTOCOL_PINNED)
	{
		move_authority_t authority = selected_engine ?
			(SV_PrivateWalkTrialQ30Program () &&
			 (client->offered_pmove_policies & QSVR_PMOVE_CAP_Q30_JUMP) ?
			 MOVE_AUTHORITY_PMOVE_QC_COMMAND : MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT) :
			MOVE_AUTHORITY_LEGACY_FRAME;
		if (selected)
			ack_flags |= MOVEACK_FLAG_SELECTED;
		if (selected && client->private_input_phase == PRIVATE_INPUT_AWAIT_MARKER)
			ack_flags |= MOVEACK_FLAG_RESUME_PENDING;
		if (selected && client->private_input_phase == PRIVATE_INPUT_RUNNING &&
			client->private_resume_first_sequence > 0 &&
			client->private_completed_move >= client->private_resume_first_sequence)
			ack_flags |= MOVEACK_FLAG_RESUME_COMPLETED;
		if (!client->private_move_published_authority_valid)
		{
			client->private_move_published_authority = authority;
			client->private_move_published_authority_valid = true;
		}
		else if (client->private_move_published_authority != authority)
		{
			client->private_move_mode_epoch++;
			client->private_move_published_authority = authority;
		}
		gorilla_ack = selected_engine && sv_gorilla.value && client->vr_gorilla_capable &&
			client->vr_gorilla_state.initialized &&
			client->vr_gorilla_cursor_valid && client->vr_gorilla_last_sequence >= 0 &&
			client->vr_gorilla_last_sequence == client->private_completed_move &&
			SV_GorillaAckStateIsFinite (client) &&
			SV_GorillaAckOriginMatchesOwner (client);
		if (selected_engine && !sv.paused &&
			client->private_input_phase == PRIVATE_INPUT_RUNNING)
		{
			ack_flags |= MOVEACK_FLAG_AUTHORITATIVE;
			/* Stock WALK replay includes the shared solver's liquid domain.
			 * A positive private timer owns a ledge jump; an unowned future
			 * deadline remains a QC teleport hold, never a replay seed. */
			if ((!SV_PrivateWalkTrialQ30Program () ||
				 (client->offered_pmove_policies & QSVR_PMOVE_CAP_Q30_JUMP)) &&
				client->edict && !client->edict->free &&
				SVFTE_PredictionVelocityRepresentable (client->edict->v.velocity) &&
				client->edict->v.health > 0 &&
				client->edict->v.deadflag == DEAD_NO &&
				client->edict->v.movetype == MOVETYPE_WALK &&
				client->edict->v.solid == SOLID_SLIDEBOX &&
				isfinite (client->edict->v.waterlevel) &&
				client->edict->v.waterlevel >= 0 && client->edict->v.waterlevel <= 3 &&
				isfinite (client->edict->v.flags) &&
				isfinite (client->edict->v.teleport_time) &&
				isfinite (client->private_pmove_waterjump_secs) &&
				client->private_pmove_waterjump_secs >= 0.0f &&
				client->private_pmove_waterjump_secs <= 2.0f &&
				!client->private_pmove_pusher_interaction &&
				!SV_PrivateMoveHasPusherPalm (client) &&
				((client->private_pmove_waterjump_secs > 0.0f &&
				  ((int)client->edict->v.flags & FL_WATERJUMP)) ||
				 (client->private_pmove_waterjump_secs == 0.0f &&
				  !((int)client->edict->v.flags & FL_WATERJUMP) &&
				  qcvm->time >= client->edict->v.teleport_time)))
				ack_flags |= MOVEACK_FLAG_PREDICTION_ALLOWED;
		}
		if (client->private_move_discontinuity_reason != MOVEACK_DISCONTINUITY_NONE)
			ack_flags |= MOVEACK_FLAG_DISCONTINUITY;
		if (gorilla_ack)
			ack_flags |= MOVEACK_FLAG_VR_GORILLA;
		MSG_WriteShort (msg, (client->private_completed_move & 0xffff));
		MSG_WriteByte (msg, ack_flags);
		MSG_WriteByte (msg, authority);
		MSG_WriteShort (msg, client->private_move_mode_epoch);
		MSG_WriteShort (msg, client->private_move_discontinuity_epoch);
		MSG_WriteByte (msg, client->private_move_discontinuity_reason);
		if (gorilla_ack)
			SV_WriteGorillaAckState (client, msg);
	}
	else if (client->protocol_pext2 & PEXT2_PREDINFO)
		MSG_WriteShort (msg, (client->lastmovemessage & 0xffff));
	MSG_WriteFloat (msg, frame->timestamp); // should be the time the last physics frame was run.
	if (selected)
	{
		/* The ACK, complete move stats (written immediately before this
		 * service), and a baseline-relative owner must survive as one datagram.
		 * Prioritize the owner over optional PVS entities on every continuation. */
		ownernum = NUM_FOR_EDICT (client->edict);
		for (i = 0; i < client->numpreviousentities; i++)
			if (client->previousentities[i].num == ownernum)
			{
				ownerstate = &client->previousentities[i];
				break;
			}
		if (!ownerstate || ownernum >= client->numpendingentities)
		{
			msg->maxsize = origmaxsize;
			return false;
		}
		// Replayed reset debt waits for the next snapshot, as in the ordinary cursor.
		worldreset = !continuation && (client->pendingentities_bits[0] & UF_REMOVE) != 0;
		if (worldreset)
			MSG_WriteShort (msg, 0x8000); // world removal precedes owner reset
		if (ownernum >= 0x4000)
		{
			MSG_WriteShort (msg, 0x4000 | (ownernum & 0x3fff));
			MSG_WriteByte (msg, (ownernum >> 14) & 0xff);
		}
		else
			MSG_WriteShort (msg, ownernum);
		netbits = UF_RESET | MSGFTE_DeltaCalcBits (&client->edict->baseline,
			&ownerstate->state, true);
		MSGFTE_WriteEntityUpdate (netbits, &ownerstate->state, msg,
			client->protocol_pext2, sv.protocolflags, true);
		if ((size_t)msg->cursize + 2 > origmaxsize)
		{
			msg->maxsize = origmaxsize;
			return false;
		}
		/* Owner resets are repeated rather than depending on frame resends.
		 * The world reset still needs the existing lost-frame bookkeeping. */
		client->pendingentities_bits[ownernum] = 0;
		if (worldreset)
		{
			SVFTE_ResetCSQCState (client);
			client->pendingentities_bits[0] = 0;
			if (frame->numents == frame->maxents)
			{
				frame->maxents += 64;
				frame->ents = Mem_Realloc (frame->ents, sizeof (*frame->ents) * frame->maxents);
			}
			frame->ents[frame->numents].num = 0;
			frame->ents[frame->numents].ebits = UF_REMOVE;
			frame->ents[frame->numents].csqcbits = 0;
			frame->numents++;
		}
	}
	for (entnum = client->snapshotresume; entnum < client->numpendingentities; entnum++)
	{
		if (selected && (entnum == ownernum || (entnum == 0 && worldreset)))
			continue;
		entbits = client->pendingentities_bits[entnum];
		if (!(entbits & ~UF_RESET2))
			continue; // nothing to send (if reset2 is still set, then leave it pending until there's more data

		rollbacksize = msg->cursize;
		client->pendingentities_bits[entnum] = 0;
		logbits = 0;
		if (entbits & UF_REMOVE)
		{
			if (entnum > 0x3fff)
			{
				MSG_WriteShort (msg, 0xc000 | (entnum & 0x3fff));
				MSG_WriteByte (msg, (entnum >> 14) & 0xff);
			}
			else
				MSG_WriteShort (msg, 0x8000 | entnum);
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
					/*if reset2, then this is the second packet sent to the client and should have a forced reset (but which isn't tracked)*/
					logbits = entbits & ~(UF_RESET | UF_RESET2);
					netbits = UF_RESET | MSGFTE_DeltaCalcBits (&EDICT_NUM (entnum)->baseline, &state->state, client->protocol_qsvr == QSVR_PROTOCOL_PINNED);
					//					Con_Printf("RESET2 %u @ %i\n", (int)entnum, sequence);
				}
				else if (entbits & UF_RESET)
				{
					/*flag the entity for the next packet, so we always get two resets when it appears, to reduce the effects of packetloss on seeing rockets
					 * etc*/
					client->pendingentities_bits[entnum] = UF_RESET2;
					netbits = UF_RESET | MSGFTE_DeltaCalcBits (&EDICT_NUM (entnum)->baseline, &state->state, client->protocol_qsvr == QSVR_PROTOCOL_PINNED);
					logbits = UF_RESET;
					//					Con_Printf("RESET %u @ %i\n", (int)entnum, sequence);
				}
				else
					logbits = netbits = entbits;

				if (entnum >= 0x4000)
				{
					MSG_WriteShort (msg, 0x4000 | (entnum & 0x3fff));
					MSG_WriteByte (msg, (entnum >> 14) & 0xff);
				}
				else
					MSG_WriteShort (msg, entnum);
				//				SV_EmitDeltaEntIndex(msg, j, false, true);
				MSGFTE_WriteEntityUpdate (netbits, &state->state, msg, client->protocol_pext2, sv.protocolflags, client->protocol_qsvr == QSVR_PROTOCOL_PINNED);
			}
		}

		if ((size_t)msg->cursize + 2 > origmaxsize)
		{
			msg->cursize = rollbacksize;					// roll back
			client->pendingentities_bits[entnum] = entbits; // make sure those bits get re-applied later.
			break;
		}
		if (entnum == 0 && (logbits & UF_REMOVE))
			SVFTE_ResetCSQCState (client);
		if (selected && msg->cursize > rollbacksize)
			wrote_optional = true;
		if (frame->numents == frame->maxents)
		{
			frame->maxents += 64;
			frame->ents = Mem_Realloc (frame->ents, sizeof (*frame->ents) * frame->maxents);
		}
		frame->ents[frame->numents].num = entnum;
		frame->ents[frame->numents].ebits = logbits;
		frame->ents[frame->numents].csqcbits = 0;
		frame->numents++;
	}
	msg->maxsize = origmaxsize;
	if (selected && continuation && entnum < client->numpendingentities && !wrote_optional)
		return false; // even a clean continuation cannot fit the optional entity
	MSG_WriteShort (msg, 0); // eom

	// remember how far we got, so we can keep things flushed, instead of only updating the first N entities.
	client->snapshotresume = entnum;

	if (msg->cursize > 1024 && dev_peakstats.packetsize <= 1024)
		Con_DWarning ("%i byte packet exceeds standard limit of 1024.\n", msg->cursize);
	dev_stats.packetsize = msg->cursize;
	dev_peakstats.packetsize = q_max (msg->cursize, dev_peakstats.packetsize);
	return true;
}

static void SVFTE_WriteCSQCEntityNum (sizebuf_t *msg, size_t entnum, qboolean remove)
{
	unsigned int flags = remove ? 0x8000 : 0;
	if (entnum >= 0x4000)
	{
		MSG_WriteShort (msg, flags | 0x4000 | (entnum & 0x3fff));
		MSG_WriteByte (msg, entnum >> 14);
	}
	else
		MSG_WriteShort (msg, flags | entnum);
}

// QC may retire a connection while leaving its recipient edict live.
static qboolean SV_ClientConnectionMatches (const client_t *client, const struct qsocket_s *socket)
{
	return client->active && client->netconnection == socket;
}

static qboolean SVFTE_WriteCSQCEntitiesToClient (client_t *client, sizebuf_t *msg,
	struct deltaframe_s *frame, qboolean continuation)
{
	struct qsocket_s *socket = client->netconnection;
	edict_t *clent = client->edict;
	byte entbuf[MAX_DATAGRAM];
	qboolean wroteheader = false;
	qboolean optional_native = false;
	size_t entnum;
	if (!client->csqcactive || !SV_CSQCTransportAllowed (client) ||
		!GetEdictFieldValid (SendEntity) || !GetEdictFieldValid (SendFlags))
	{
		client->csqcsnapshotresume = client->numpendingcsqcentities;
		return true;
	}
	for (int i = 0; i < frame->numents; ++i)
		if (frame->ents[i].ebits && frame->ents[i].num)
			optional_native = true;

	for (entnum = client->csqcsnapshotresume; entnum < client->numpendingcsqcentities; ++entnum)
	{
		// BeginFrame may have replayed dirty bits after visibility was collected.
		if (!(client->pendingcsqcentities_bits[entnum] & SENDFLAG_CURRENT))
			client->pendingcsqcentities_bits[entnum] &= ~SENDFLAG_USABLE;
		unsigned int bits = client->pendingcsqcentities_bits[entnum];
		unsigned int logbits = 0;
		qboolean update = false;
		qboolean remove = (bits & SENDFLAG_REMOVE) != 0 ||
			((bits & SENDFLAG_REMOVEWAIT) && (bits & SENDFLAG_USABLE));
		qboolean payload_overflow = false;
		sizebuf_t entmsg = {0};
		entmsg.data = entbuf;
		entmsg.maxsize = sizeof (entbuf);
		entmsg.allowoverflow = true;
		if (!(bits & (SENDFLAG_USABLE | SENDFLAG_REMOVE)))
			continue;

		SZ_Clear (&sv.multicast);
		if ((bits & SENDFLAG_CURRENT) && entnum < (size_t)qcvm->num_edicts)
		{
			edict_t *ed = EDICT_NUM (entnum);
			if (!ed->free && GetEdictFieldEval (ed, SendEntity)->function)
			{
				int oldself = pr_global_struct->self;
				int oldother = pr_global_struct->other;
				qboolean oldallowoverflow = sv.multicast.allowoverflow;
				ED_Retain (ed);
				ED_Retain (clent);
				sv.multicast.allowoverflow = true;
				pr_global_struct->self = EDICT_TO_PROG (ed);
				G_INT (OFS_PARM0) = EDICT_TO_PROG (clent);
				// Preserve primary's callback arguments; CURRENT is engine-only.
				G_FLOAT (OFS_PARM1 + 0) = remove ? SENDFLAG_USABLE : (bits & SENDFLAG_USABLE);
				G_FLOAT (OFS_PARM1 + 1) = (bits & SENDFLAG_PRESENT) >> 24;
				G_FLOAT (OFS_PARM1 + 2) = 0;
				PR_ExecuteProgram (GetEdictFieldEval (ed, SendEntity)->function);
				qboolean recipient_live = SV_ClientConnectionMatches (client, socket) && !clent->free;
				update = G_FLOAT (OFS_RETURN) && !ed->free &&
					GetEdictFieldEval (ed, SendEntity)->function;
				payload_overflow = sv.multicast.overflowed;
				sv.multicast.allowoverflow = oldallowoverflow;
				pr_global_struct->self = oldself;
				pr_global_struct->other = oldother;
				ED_Release (ed);
				ED_Release (clent);
				if (!recipient_live)
				{
					SZ_Clear (&sv.multicast);
					return false;
				}
			}
		}
		if (!update)
			remove |= (bits & SENDFLAG_PRESENT) != 0;

		if (!remove && !update)
		{
			client->pendingcsqcentities_bits[entnum] &= SENDFLAG_CURRENT | SENDFLAG_REMOVEWAIT | SENDFLAG_RETIRENEW;
			SZ_Clear (&sv.multicast);
			continue;
		}
		if (!wroteheader)
			MSG_WriteByte (&entmsg, svcdp_csqcentities);
		if (remove)
		{
			SVFTE_WriteCSQCEntityNum (&entmsg, entnum, true);
			logbits |= SENDFLAG_REMOVE;
		}
		if (update)
		{
			SVFTE_WriteCSQCEntityNum (&entmsg, entnum, false);
			SZ_Write (&entmsg, sv.multicast.data, sv.multicast.cursize);
			logbits |= remove ? SENDFLAG_USABLE : (bits & SENDFLAG_USABLE);
		}
		if ((update && payload_overflow) || entmsg.overflowed ||
			(msg->cursize + entmsg.cursize + 2 > msg->maxsize &&
			 continuation && !optional_native && !wroteheader))
		{
			Con_Printf ("%s: CSQC entity %zu payload cannot fit a valid datagram (record %d, used %d, max %d)\n",
				client->name, entnum, entmsg.cursize, msg->cursize, msg->maxsize);
			SZ_Clear (&sv.multicast);
			return false;
		}
		if (msg->cursize + entmsg.cursize + 2 > msg->maxsize)
		{
			// No delivery or log mutation until the complete candidate fits.
			SZ_Clear (&sv.multicast);
			break;
		}
		SZ_Write (msg, entmsg.data, entmsg.cursize);
		wroteheader = true;
		unsigned int pending = client->pendingcsqcentities_bits[entnum];
		if (remove && ((pending & SENDFLAG_RETIRENEW) || (!update && (bits & SENDFLAG_PRESENT))))
		{
			client->csqcentities_remove_boundary[entnum] = frame->sequence;
			pending |= SENDFLAG_REMOVEWAIT;
			pending &= ~SENDFLAG_RETIRENEW;
		}
		client->pendingcsqcentities_bits[entnum] =
			(pending & (SENDFLAG_CURRENT | SENDFLAG_REMOVEWAIT)) | (update ? SENDFLAG_PRESENT : 0);
		if (frame->numents == frame->maxents)
		{
			frame->maxents += 64;
			frame->ents = Mem_Realloc (frame->ents, frame->maxents * sizeof (*frame->ents));
		}
		frame->ents[frame->numents].num = entnum;
		frame->ents[frame->numents].ebits = 0;
		frame->ents[frame->numents].csqcbits = logbits;
		frame->numents++;
		SZ_Clear (&sv.multicast);
	}
	if (wroteheader)
		MSG_WriteShort (msg, 0);
	client->csqcsnapshotresume = entnum;
	return true;
}

// Raw QC references must be validated before conversion, including in client VM makestatic.
static edict_t *SV_LiveEntityForOffset (int offset)
{
	if (offset < 0 || qcvm->edict_size <= 0 || offset % qcvm->edict_size ||
		offset / qcvm->edict_size >= qcvm->num_edicts)
		return NULL;
	edict_t *ent = EDICT_NUM (offset / qcvm->edict_size);
	return ent->free ? NULL : ent;
}

static edict_t *SV_VisibilityParent (edict_t *ent)
{
	for (int depth = 0; depth < qcvm->num_edicts; ++depth)
	{
		eval_t *val = GetEdictFieldValue (ent, qcvm->extfields.tag_entity);
		if (!val || !val->edict)
			return ent;
		ent = SV_LiveEntityForOffset (val->edict);
		if (!ent)
			return NULL;
	}
	return NULL; // No acyclic chain can exceed the allocated entity count.
}

static qboolean SV_CustomizeEntityForClient (edict_t *ent, edict_t *recipient)
{
	eval_t *val = GetEdictFieldValue (ent, qcvm->extfields.customizeentityforclient);
	qboolean visible = true;
	if (val && val->function)
	{
		int oldself = pr_global_struct->self, oldother = pr_global_struct->other;
		ED_Retain (ent); // The caller retains the recipient for its entire pass.
		pr_global_struct->self = EDICT_TO_PROG (ent);
		pr_global_struct->other = EDICT_TO_PROG (recipient);
		PR_ExecuteProgram (val->function);
		visible = G_FLOAT (OFS_RETURN) && !ent->free;
		pr_global_struct->self = oldself;
		pr_global_struct->other = oldother;
		ED_Release (ent);
	}
	return visible; // QC field mutations intentionally persist across recipients.
}

static qboolean SV_VisibilityPVS (edict_t *parent, byte *pvs, int flags, qboolean modern)
{
	// PHS conservatively bypasses PVS; only modern snapshots bypass zero leaves.
	return (flags & PVSF_MODE_MASK) >= PVSF_USEPHS || (modern && !parent->num_leafs) ||
		parent->num_leafs >= MAX_ENT_LEAFS || SV_EdictInPVS (parent, pvs);
}

static qboolean SV_HasParticleEffect (edict_t *ent, int field)
{
	eval_t *val = GetEdictFieldValue (ent, field);
	// Bound the float before converting/indexing, also rejecting NaN and infinity.
	return val && val->_float >= 1 && val->_float < MAX_PARTICLETYPES &&
		sv.particle_precache[(int)val->_float] && sv.particle_precache[(int)val->_float][0];
}

/*
SV_BuildEntityState
copies edict state into a more compact entity_state_t with all the extension fields etc sorted out and neatened up for network precision.
note: ignores  like nodrawtoclient / drawonlytoclient and other client-specific stuff.
*/
void SV_BuildEntityState (edict_t *ent, entity_state_t *state)
{
	eval_t *val;
	edict_t *parent;
	state->eflags = 0;
	if (SV_UsePredThinkPos (ent))
		VectorCopy (ent->predthinkpos, state->origin);
	else
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
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.colormod)) && (val->vector[0] || val->vector[1] || val->vector[2]))
	{
		state->colormod[0] = val->vector[0] * 32;
		state->colormod[1] = val->vector[1] * 32;
		state->colormod[2] = val->vector[2] * 32;
	}
	else
		state->colormod[0] = state->colormod[1] = state->colormod[2] = 32;
	state->traileffectnum = qcvm->extfields.traileffectnum >= 0 ? GetEdictFieldValue (ent, qcvm->extfields.traileffectnum)->_float : 0;
	state->emiteffectnum = qcvm->extfields.emiteffectnum >= 0 ? GetEdictFieldValue (ent, qcvm->extfields.emiteffectnum)->_float : 0;
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.tag_entity)) && val->edict &&
		(parent = SV_LiveEntityForOffset (val->edict)))
		state->tagentity = NUM_FOR_EDICT (parent);
	else
		state->tagentity = 0;
	if (state->tagentity && (val = GetEdictFieldValue (ent, qcvm->extfields.tag_index)))
		state->tagindex = val->_float;
	else
		state->tagindex = 0;
	state->effects = (int)ent->v.effects & sv.effectsmask;
	state->solidsize = ES_SOLID_NOT;
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.modelflags)))
		state->effects |= ((unsigned int)val->_float) << 24;
	if (ent->v.movetype == MOVETYPE_STEP)
		state->eflags |= EFLAGS_STEP;

	state->pmovetype = 0;
	state->velocity[0] = state->velocity[1] = state->velocity[2] = 0;

#ifdef LERP_BANDAID
	state->lerp = (ent->sendinterval || ent->sendinterval_default) ? Q_rint ((ent->v.nextthink - qcvm->time) * 1000) + 1 : 0;
#endif
}

static qboolean SVFTE_BuildSnapshotForClient (client_t *client)
{
	struct qsocket_s *socket = client->netconnection;
	unsigned int  e;
	byte		 *pvs;
	vec3_t		  org;
	edict_t		 *ent, *parent;
	unsigned int  maxentities = client->limit_entities;
	edict_t		 *clent = client->edict;
	eval_t		 *val;
	unsigned char eflags;
	qboolean cancsqc = client->csqcactive && SV_CSQCTransportAllowed (client) &&
		GetEdictFieldValid (SendEntity) && GetEdictFieldValid (SendFlags);
	qboolean iscsqc, visible;
	int pvs_flags;
	int			  proged = EDICT_TO_PROG (clent);

	struct entity_num_state_s *ents = snapshot_entstate;
	size_t					   numents = 0;
	size_t					   maxents = snapshot_maxents;

	ED_Retain (clent);
	// find the client's PVS
	VectorAdd (clent->v.origin, clent->v.view_ofs, org);
	pvs = SV_FatPVS (org, qcvm->worldmodel);
	SV_AddSkyRoomPVS (org, qcvm->worldmodel);

	if (maxentities > (unsigned int)qcvm->num_edicts)
		maxentities = (unsigned int)qcvm->num_edicts;

	if (cancsqc && client->numpendingcsqcentities < maxentities)
	{
		size_t oldmax = client->numpendingcsqcentities;
		size_t newmax = maxentities + 64;
		client->pendingcsqcentities_bits = Mem_Realloc (client->pendingcsqcentities_bits,
			newmax * sizeof (*client->pendingcsqcentities_bits));
		memset (client->pendingcsqcentities_bits + oldmax, 0,
			(newmax - oldmax) * sizeof (*client->pendingcsqcentities_bits));
		client->csqcentities_remove_boundary = Mem_Realloc (client->csqcentities_remove_boundary,
			newmax * sizeof (*client->csqcentities_remove_boundary));
		memset (client->csqcentities_remove_boundary + oldmax, 0,
			(newmax - oldmax) * sizeof (*client->csqcentities_remove_boundary));
		client->numpendingcsqcentities = newmax;
	}

	// send over all entities (excpet the client) that touch the pvs
	ent = NEXT_EDICT (qcvm->edicts);
	for (e = 1; e < maxentities; e++, ent = NEXT_EDICT (ent))
	{
		iscsqc = false;
		pvs_flags = 0;
		if (ent->free)
			goto invisible;
		visible = SV_CustomizeEntityForClient (ent, clent);
		if (!SV_ClientConnectionMatches (client, socket) || clent->free)
			break;
		if (ent->free)
			goto invisible;
		eflags = 0;
		// The recipient player always retains the native movement snapshot.
		iscsqc = cancsqc && ent != clent && GetEdictFieldEval (ent, SendEntity)->function;
		val = GetEdictFieldValue (ent, qcvm->extfields.pvsflags);
		pvs_flags = val ? (int)val->_float : PVSF_NORMALPVS;
		parent = SV_VisibilityParent (ent); // Validate even when PVS is bypassed.
		if (!parent && ent != clent)
			goto invisible;
		val = GetEdictFieldValue (ent, qcvm->extfields.viewmodelforclient);
		if (val && val->edict == proged)
			eflags |= EFLAGS_VIEWMODEL;
		else if (val && val->edict)
			visible = false;
		if (ent != clent) // clent is ALLWAYS sent
		{
			// ignore ents without visible models
			if (!visible || ((!ent->v.modelindex || !PR_GetString (ent->v.model)[0]) &&
				!iscsqc && !SV_HasParticleEffect (ent, qcvm->extfields.emiteffectnum)))
				goto invisible;
			if (!(eflags & EFLAGS_VIEWMODEL) && !SV_VisibilityPVS (parent, pvs, pvs_flags, true))
				goto invisible;
		}

		val = GetEdictFieldValue (ent, qcvm->extfields.nodrawtoclient);
		if (val && val->edict == proged)
			visible = false;
		val = GetEdictFieldValue (ent, qcvm->extfields.drawonlytoclient);
		if (val && val->edict && val->edict != proged)
			visible = false;
		if (!visible)
		{
			if (ent != clent)
				goto invisible;
			// Hide ordinary owner geometry without discarding its prediction seed.
			eflags |= EFLAGS_EXTERIORMODEL;
		}
		val = GetEdictFieldValue (ent, qcvm->extfields.exteriormodeltoclient);
		if (val && val->edict == proged)
			eflags |= EFLAGS_EXTERIORMODEL;

		if (cancsqc)
		{
			unsigned int *bits = &client->pendingcsqcentities_bits[e];
			if (iscsqc)
			{
				if (!(*bits & SENDFLAG_CURRENT) || !(*bits & SENDFLAG_PRESENT) || (*bits & SENDFLAG_REMOVE))
					*bits |= SENDFLAG_USABLE;
				else
					*bits |= (int)GetEdictFieldEval (ent, SendFlags)->_float & SENDFLAG_USABLE;
				*bits |= SENDFLAG_CURRENT;
				continue;
			}
			*bits = SVFTE_RetireCSQCBits (*bits);
		}

		// okay, we care about this entity.
		if (numents == maxents)
		{
			maxents += 64;
			ents = Mem_Realloc (ents, maxents * sizeof (*ents));
		}

		ents[numents].num = e;
		SV_BuildEntityState (ent, &ents[numents].state);
		if (!parent) // The mandatory owner survives an invalid attachment without mutating QC.
			ents[numents].state.tagentity = ents[numents].state.tagindex = 0;
		if (client->protocol_qsvr == QSVR_PROTOCOL_PINNED)
		{
			if (client->edict && ent->v.owner == EDICT_TO_PROG (client->edict))
				ents[numents].state.solidsize = ES_SOLID_NOT;
			/* Match the server area tree: SOLID_NOT and triggers are not
			 * movement colliders even when a negative skin encodes contents. */
			else if (ent->v.solid == SOLID_NOT || ent->v.solid == SOLID_TRIGGER)
				ents[numents].state.solidsize = ES_SOLID_NOT;
			else if (ent->v.solid == SOLID_BSP)
				ents[numents].state.solidsize = ES_SOLID_BSP;
			else if (ent->v.solid == SOLID_BBOX || ent->v.solid == SOLID_SLIDEBOX)
			{
				ents[numents].state.solidsize = CLAMP (0, (int)-ent->v.mins[0], 255);
				ents[numents].state.solidsize |= CLAMP (0, (int)-ent->v.mins[2], 255) << 8;
				ents[numents].state.solidsize |= (uint32_t)CLAMP (0, (int)(ent->v.maxs[2] + 32768), 65535) << 16;
				if (ents[numents].state.solidsize == 0x80000000u)
					ents[numents].state.solidsize = ES_SOLID_NOT;
			}
		}
		if ((unsigned int)ents[numents].state.modelindex >= client->limit_models)
			ents[numents].state.modelindex = 0;
		if (ent == clent) // add velocity, but we only care for the local player (should add prediction for other entities some time too).
		{
			/* The selected PMove owner reports its movement seed, but the ACK
			 * permission bit still prevents client prediction. */
			ents[numents].state.pmovetype = 0;
			if (client->protocol_qsvr == QSVR_PROTOCOL_PINNED &&
				SV_PrivateWalkTrialSelected (client) &&
				(int)ent->v.movetype == MOVETYPE_WALK)
			{
				ents[numents].state.pmovetype = MOVETYPE_WALK;
				if ((int)ent->v.flags & FL_ONGROUND)
					ents[numents].state.pmovetype |= 0x80;
				if (!((int)ent->v.flags & FL_JUMPRELEASED))
					ents[numents].state.pmovetype |= 0x40;
			}
			if ((int)ent->v.flags & FL_ONGROUND)
				eflags |= EFLAGS_ONGROUND;
			for (int axis = 0; axis < 3; ++axis)
				ents[numents].state.velocity[axis] = SVFTE_EncodeVelocity (ent->v.velocity[axis]);
		}
		else if (ents[numents].state.alpha == ENTALPHA_ZERO && !ent->v.effects &&
			!SV_HasParticleEffect (ent, qcvm->extfields.traileffectnum) &&
			!SV_HasParticleEffect (ent, qcvm->extfields.emiteffectnum))
			continue;
		// EFLAGS_VIEWMODEL was handled above
		ents[numents].state.eflags |= eflags;

		numents++;
		continue;

	invisible:
		if (cancsqc)
		{
			unsigned int *bits = &client->pendingcsqcentities_bits[e];
			if (iscsqc && !ent->free && (pvs_flags & PVSF_NOREMOVE))
				*bits &= ~(SENDFLAG_CURRENT | SENDFLAG_USABLE);
			else
				*bits = SVFTE_RetireCSQCBits (*bits);
		}
	}

	qboolean recipient_live = SV_ClientConnectionMatches (client, socket) && !clent->free;
	snapshot_entstate = ents;
	snapshot_numents = recipient_live ? numents : 0;
	snapshot_maxents = maxents;
	ED_Release (clent);
	return recipient_live;
}

void MSG_WriteStaticOrBaseLine (sizebuf_t *buf, int idx, entity_state_t *state, unsigned int protocol_pext2, unsigned int protocol, unsigned int protocolflags)
{
	int i;
	if (protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
	{
		if (idx >= 0)
		{
			MSG_WriteByte (buf, svcfte_spawnbaseline2);
			MSG_WriteShort (buf, idx);
		}
		else
			MSG_WriteByte (buf, svcfte_spawnstatic2);
		MSGFTE_WriteEntityUpdate (MSGFTE_DeltaCalcBits (&nullentitystate, state, false), state, buf, protocol_pext2, protocolflags, false);
	}
	else
	{
		int bits = 0;
		{
			if (protocol == PROTOCOL_FITZQUAKE || protocol == PROTOCOL_RMQ) // still want to send baseline in PROTOCOL_NETQUAKE, so reset these values
			{
				if (state->modelindex > 255)
					bits |= B_LARGEMODEL;
				if (state->frame > 255)
					bits |= B_LARGEFRAME;
				if (state->alpha != ENTALPHA_DEFAULT)
					bits |= B_ALPHA;
				if (state->scale != ENTSCALE_DEFAULT && protocol == PROTOCOL_RMQ)
					bits |= B_SCALE;
			}
			if (idx >= 0)
			{
				MSG_WriteByte (buf, bits ? svc_spawnbaseline2 : svc_spawnbaseline);
				MSG_WriteEntity (buf, idx, protocol_pext2);
			}
			else
				MSG_WriteByte (buf, bits ? svc_spawnstatic2 : svc_spawnstatic);

			if (bits)
				MSG_WriteByte (buf, bits);
		}

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
		if (bits & B_SCALE)
			MSG_WriteByte (buf, state->scale);
	}
}
static void SV_Pext_f (void);

/*
===============
SV_Protocol_f
===============
*/
static void SV_Protocol_f (void)
{
	int			i;
	const char *s;
	int			prot, pext1, pext2;

	prot = sv_protocol;
	pext1 = sv_protocol_pext1;
	pext2 = sv_protocol_pext2;

	switch (Cmd_Argc ())
	{
	case 1:
		//"FTE+15" or "15", just to be explicit about it
		Con_Printf ("\"sv_protocol\" is \"%s%i\"\n", sv_protocol_pext2 ? "fte" : "", sv_protocol);
		break;
	case 2:
		s = Cmd_Argv (1);
		if (!q_strncasecmp (s, "FTE", 3))
		{
			s += 3;
			if (*s == '+' || *s == '-')
				s++;
			pext1 = PEXT1_SUPPORTED_SERVER;
			pext2 = PEXT2_SUPPORTED_SERVER;
		}
		else if (!q_strncasecmp (s, "+", 3))
		{
			s += 1;
			pext1 = PEXT1_SUPPORTED_SERVER;
			pext2 = PEXT2_SUPPORTED_SERVER;
		}
		else if (!q_strncasecmp (s, "Base", 4))
		{
			s += 4;
			if (*s == '+' || *s == '-')
				s++;
			pext1 = 0;
			pext2 = 0;
		}
		else if (*s == '-')
		{
			s++;
			pext1 = 0;
			pext2 = 0;
		}

		i = strtol (s, (char **)&s, 0);
		if (*s == '-')
		{
			pext1 = 0;
			pext2 = 0;
		}
		else if (*s == '+')
		{
			pext1 = PEXT1_SUPPORTED_SERVER;
			pext2 = PEXT2_SUPPORTED_SERVER;
		}

		if (i != PROTOCOL_NETQUAKE && i != PROTOCOL_FITZQUAKE && i != PROTOCOL_RMQ)
			Con_Printf (
				"sv_protocol must be %i or %i or %i.\nProtocol may be prefixed with FTE+ or Base- to enable/disable FTE extensions.\n", PROTOCOL_NETQUAKE,
				PROTOCOL_FITZQUAKE, PROTOCOL_RMQ);
		else
		{
			sv_protocol = i;
			sv_protocol_pext1 = pext1;
			sv_protocol_pext2 = pext2;
			if (sv.active)
			{
				if (prot == sv_protocol && pext1 == sv_protocol_pext1 && pext2 == sv_protocol_pext2)
					Con_Printf ("specified protocol already active.\n");
				else
					Con_Printf ("changes will not take effect until the next level load.\n");
			}
		}
		break;
	default:
		Con_SafePrintf ("usage: sv_protocol <protocol>\n");
		break;
	}
}

static void SV_NetDiag_f (void)
{
	int i;
	const qboolean connected = cls.state == ca_connected && cls.netcon != NULL;

	if (connected)
		Con_Printf ("client netdiag: connected=1 signon=%d protocol=%u qsvr=%u\n",
			cls.signon, cl.protocol, cl.protocol_qsvr);
	else
		Con_Printf ("client netdiag: connected=0 signon=%d protocol=n/a qsvr=n/a\n",
			cls.signon);
	Con_Printf ("client netdiag: moves acked=%d acks=%d stale_acks=%d packets=%d cmds=%d generated_msec=%llu last_packet_cmds=%d\n",
		cl.ackedmovemessages, cl.net_move_acks, cl.net_move_stale_acks,
		cl.net_move_packets_sent, cl.net_move_cmds_sent, cl.net_move_msec_generated,
		cl.net_move_last_packet_cmds);
	Con_Printf ("client netdiag: snapshots seq=%d packets=%d drops=%d acks_sent=%d ack_overflows=%d have=%d\n",
		cl.net_snapshot_sequence, cl.net_snapshot_packets, cl.net_snapshot_drops,
		cl.net_snapshot_acks_sent, cl.net_snapshot_ack_queue_overflows,
		cl.net_snapshot_have ? 1 : 0);
	Con_Printf ("client netdiag: authority=%d prediction_allowed=%d selected_owner=%d mode_epoch=%u discontinuity_epoch=%u reason=%u snapshot_valid=%d snapshot_ack=%d snapshot_owner=%d resume_pending=%d resume_epoch_valid=%d resume_epoch=%u resume_first=%d\n",
		cl.move_ack_authority, cl.move_ack_prediction_allowed ? 1 : 0,
		cl.move_ack_selected_owner ? 1 : 0, (unsigned)cl.move_ack_mode_epoch,
		(unsigned)cl.move_ack_discontinuity_epoch, (unsigned)cl.move_ack_discontinuity_reason,
		cl.move_snapshot_valid ? 1 : 0, cl.move_snapshot_ack, cl.move_snapshot_owner,
		cl.move_ack_resume_pending ? 1 : 0, cl.move_resume_marker_epoch_valid ? 1 : 0,
		(unsigned)cl.move_resume_marker_epoch_sent, cl.move_resume_marker_first_sequence);
	Con_Printf ("client netdiag: retained_prediction_error=(%.2f %.2f %.2f) sequence=%d time=%.3f\n",
		cl.prediction_error[0], cl.prediction_error[1], cl.prediction_error[2],
		cl.prediction_error_sequence, cl.prediction_error_time);

	if (!sv.active)
	{
		Con_Printf ("server netdiag: inactive\n");
		return;
	}

	for (i = 0; i < svs.maxclients; ++i)
	{
		client_t *client = &svs.clients[i];

		if (!client->active)
			continue;
		Con_Printf ("server netdiag: #%d %s protocol=%u qsvr=%u pext2=0x%x selected=%d queue_head=%u queue_count=%u queue_msec=%u lastmove=%d completed=%d retired=%d discarded=%d\n",
			i + 1, client->name, sv.protocol, client->protocol_qsvr,
			client->protocol_pext2, client->private_pmove_walk_selected ? 1 : 0,
			client->private_cmd_queue_head, client->private_cmd_queue_count,
			client->private_cmd_queue_msec, client->lastmovemessage,
			client->private_completed_move, client->private_retired_move,
			client->private_discarded_move);
		Con_Printf ("server netdiag: #%d mode_epoch=%u discontinuity_epoch=%u reason=%u published_authority_valid=%d published_authority=%u input_phase=%d native_frame=%d resume_first=%d\n",
			i + 1, (unsigned)client->private_move_mode_epoch,
			(unsigned)client->private_move_discontinuity_epoch,
			(unsigned)client->private_move_discontinuity_reason,
			client->private_move_published_authority_valid ? 1 : 0,
			(unsigned)client->private_move_published_authority, client->private_input_phase,
			client->private_move_native_frame ? 1 : 0, client->private_resume_first_sequence);
		if (client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
			Con_Printf ("server netdiag: #%d replacement_ack=%d\n",
				i + 1, client->lastacksequence);
		if (client->netconnection)
			Con_Printf ("server netdiag: #%d socket sequence_in=%d sequence_out=%d\n",
				i + 1, NET_QSocketGetSequenceIn (client->netconnection),
				NET_QSocketGetSequenceOut (client->netconnection));
		else
			Con_Printf ("server netdiag: #%d socket=none\n", i + 1);
	}
}

/*
===============
SV_Init
===============
*/
void SV_Init (void)
{
	int			  i;
	const char	 *p;
	extern cvar_t sv_maxvelocity;
	extern cvar_t sv_gravity;
	extern cvar_t sv_nostep;
	extern cvar_t sv_freezenonclients;
	extern cvar_t sv_gameplayfix_spawnbeforethinks;
	extern cvar_t sv_gameplayfix_bouncedownslopes;
	extern cvar_t sv_gameplayfix_elevators;
	extern cvar_t sv_gameplayfix_setmodelrealbox;
	extern cvar_t sv_fastpushmove;
	extern cvar_t sv_analyticphysics;
	extern cvar_t sv_friction;
	extern cvar_t sv_edgefriction;
	extern cvar_t sv_stopspeed;
	extern cvar_t vr_movement_instant_stop;
	extern cvar_t sv_maxspeed;
	extern cvar_t sv_accelerate;
	extern cvar_t sv_idealpitchscale;
	extern cvar_t sv_aim;
	extern cvar_t sv_altnoclip; // johnfitz
	extern cvar_t sv_reportheartbeats;
	extern cvar_t sv_heartbeat_interval;
	extern cvar_t sv_public;
	extern cvar_t com_protocolname;
	extern cvar_t net_masters[];
	extern cvar_t rcon_password;

	// FTE optimized world geometry checks
	extern cvar_t sv_fte_recursivehullckeck;
	extern cvar_t sv_fte_createareanode;

	Cvar_RegisterVariable (&sv_reportheartbeats);
	Cvar_RegisterVariable (&sv_heartbeat_interval);
	Cvar_RegisterVariable (&sv_public);
	Cvar_RegisterVariable (&com_protocolname);
	for (i = 0; net_masters[i].name; i++)
		Cvar_RegisterVariable (&net_masters[i]);
	Cvar_RegisterVariable (&rcon_password);

	Cvar_RegisterVariable (&sv_maxvelocity);
	Cvar_RegisterVariable (&sv_gravity);
	Cvar_RegisterVariable (&sv_friction);
	Cvar_SetCallback (&sv_gravity, Host_Callback_Notify);
	Cvar_SetCallback (&sv_friction, Host_Callback_Notify);
	Cvar_RegisterVariable (&sv_edgefriction);
	Cvar_RegisterVariable (&sv_stopspeed);
	Cvar_RegisterVariable (&vr_movement_instant_stop);
	Cvar_RegisterVariable (&sv_maxspeed);
	Cvar_SetCallback (&sv_maxspeed, Host_Callback_Notify);
	Cvar_RegisterVariable (&sv_accelerate);
	Cvar_RegisterVariable (&sv_idealpitchscale);
	Cvar_RegisterVariable (&sv_aim);
	Cvar_RegisterVariable (&sv_nostep);
	Cvar_RegisterVariable (&sv_freezenonclients);
	Cvar_RegisterVariable (&sv_gameplayfix_spawnbeforethinks);
	Cvar_RegisterVariable (&sv_gameplayfix_bouncedownslopes);
	Cvar_RegisterVariable (&sv_gameplayfix_elevators);
	Cvar_RegisterVariable (&sv_gameplayfix_setmodelrealbox);
	Cvar_RegisterVariable (&sv_fastpushmove);
	Cvar_RegisterVariable (&sv_analyticphysics);
	Cvar_RegisterVariable (&pr_checkextension);
	Cvar_RegisterVariable (&sv_altnoclip); // johnfitz
	Cvar_RegisterVariable (&sv_netsort);
	Cvar_RegisterVariable (&sv_skyroom_pvs);
	Cvar_RegisterVariable (&sv_smoothplatformlerps);
	Cvar_RegisterVariable (&sv_qsvr_private);
	Cvar_RegisterVariable (&sv_private_pmove_walk);
	Cvar_RegisterVariable (&sv_gorilla);
	Cvar_RegisterVariable (&sv_gorilla_trustclient);
	Cvar_RegisterVariable (&sv_weapon_collision);
	Cvar_RegisterVariable (&sv_immersive_melee);
	Cvar_RegisterVariable (&sv_voice);
	Cvar_RegisterVariable (&sv_coop_shared_pickups);
	Cvar_RegisterVariable (&sv_coop_respawn_near_player);
	Cvar_RegisterVariable (&sv_coop_respawn_delay);
	Cvar_RegisterVariable (&sv_coop_respawn_keep_weapons_ammo);
	Cvar_RegisterVariable (&sv_coop_player_teleport_fallback);
	Cvar_SetCallback (&sv_coop_shared_pickups, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_respawn_near_player, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_respawn_delay, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_respawn_keep_weapons_ammo, Host_Callback_Notify);
	Cvar_SetCallback (&sv_coop_player_teleport_fallback, Host_Callback_Notify);
	Cvar_SetCallback (&sv_gorilla, SV_GorillaPolicyChanged);
	Cvar_SetCallback (&sv_gorilla_trustclient, SV_GorillaPolicyChanged);

	Cvar_RegisterVariable (&sv_fte_recursivehullckeck);
	Cvar_RegisterVariable (&sv_fte_createareanode);
	VR_WeaponCalibrationInit ();
	VR_WeaponCalibrationRegisterCommands ();
	if (!VR_WeaponCalibrationReloadGame ())
		Con_Warning ("VR: invalid weapon calibration schema for active game\n");
	VR_WeaponMenu_ReloadGame ();

	Cmd_AddCommand_ClientCommand ("pext", SV_Pext_f);
	Cmd_AddCommand ("sv_protocol", &SV_Protocol_f); // johnfitz
	Cmd_AddCommand ("netdiag", SV_NetDiag_f);

	for (i = 0; i < MAX_MODELS; i++)
		q_snprintf (localmodels[i], 8, "*%i", i);

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
		Sys_Error ("Bad protocol version request %i. Accepted values: %i, %i, %i.", sv_protocol, PROTOCOL_NETQUAKE, PROTOCOL_FITZQUAKE, PROTOCOL_RMQ);
		return; /* silence compiler */
	}
	Sys_Printf ("Server using protocol %i%s (%s%s)\n", sv_protocol, sv_protocol_pext2 ? "+" : "", sv_protocol_pext2 ? "FTE-" : "", p);
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
	int i, v;

	if (sv.datagram.cursize > sv.datagram.maxsize - 18)
		return;
	MSG_WriteByte (&sv.datagram, svc_particle);
	MSG_WriteCoord (&sv.datagram, org[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[2], sv.protocolflags);
	for (i = 0; i < 3; i++)
	{
		v = dir[i] * 16;
		if (v > 127)
			v = 127;
		else if (v < -128)
			v = -128;
		MSG_WriteChar (&sv.datagram, v);
	}
	// count is sent through 1 byte, clamp it to 255
	if (count > 255.0f)
		count = 255.0f;
	MSG_WriteByte (&sv.datagram, count);

	MSG_WriteByte (&sv.datagram, color);
}

/*
==================
SV_StartSound

Each entity can have eight independant sound sources, like voice,
weapon, feet, etc.

Channel 0 is an auto-allocate channel, the others override anything
allready running on that entity/channel pair.
Volume is in 0-255.
Attenuation is in 0-4
An attenuation of 0 will play full volume everywhere in the level.
Larger attenuations will drop off.  (max 4 attenuation)

==================
*/
void SV_StartSound (edict_t *entity, float *origin, int channel, const char *sample, int volume, float attenuation)
{
	unsigned int sound_num, ent;
	int			 i, field_mask;
	int			 p;
	client_t	*client;

	if (volume < 0)
		Host_Error ("SV_StartSound: volume = %i", volume);
	else if (volume > 255)
	{
		volume = 255;
		Con_Printf ("SV_StartSound: volume = %i\n", volume);
	}

	if (attenuation < 0 || attenuation > 4)
		Host_Error ("SV_StartSound: attenuation = %f", attenuation);

	if (channel < 0 || channel > 255)
		Host_Error ("SV_StartSound: channel = %i", channel);
	else if (channel > 7)
		Con_DPrintf ("SV_StartSound: channel = %i\n", channel);

	// find precache number for sound
	for (sound_num = 1; sound_num < MAX_SOUNDS && sv.sound_precache[sound_num]; sound_num++)
	{
		if (!strcmp (sample, sv.sound_precache[sound_num]))
			break;
	}

	if (sound_num == MAX_SOUNDS || !sv.sound_precache[sound_num])
	{
		Con_Printf ("SV_StartSound: %s not precacheed\n", sample);
		return;
	}

	ent = NUM_FOR_EDICT (entity);

	field_mask = 0;
	if (volume != DEFAULT_SOUND_PACKET_VOLUME)
		field_mask |= SND_VOLUME;
	if (attenuation != DEFAULT_SOUND_PACKET_ATTENUATION)
		field_mask |= SND_ATTENUATION;

	// johnfitz -- PROTOCOL_FITZQUAKE
	if (ent >= 8192 || channel >= 8)
		field_mask |= SND_LARGEENTITY;
	if (sound_num >= 256)
		field_mask |= SND_LARGESOUND;
	// johnfitz

	for (p = 0; p < svs.maxclients; p++)
	{
		client = &svs.clients[p];
		if (!client->active || !client->spawned)
			continue;

		if (ent >= client->limit_entities)
			continue;
		if (sound_num >= client->limit_sounds)
			continue;
		// PROTOCOL_NETQUAKE do not support more than 256 sounds and/or 8192 entities.
		if ((field_mask & (SND_LARGEENTITY | SND_LARGESOUND)) && (sv.protocol == PROTOCOL_NETQUAKE))
			continue;

		if (client->datagram.cursize > client->datagram.maxsize - 22)
			continue;

		// directed messages go only to the entity the are targeted on
		MSG_WriteByte (&client->datagram, svc_sound);
		MSG_WriteByte (&client->datagram, field_mask);
		if (field_mask & SND_VOLUME)
			MSG_WriteByte (&client->datagram, volume);
		if (field_mask & SND_ATTENUATION)
			MSG_WriteByte (&client->datagram, attenuation * 64);

		// johnfitz -- PROTOCOL_FITZQUAKE
		if (field_mask & SND_LARGEENTITY)
		{
			if ((client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS) && ent > 0x7fff)
			{
				MSG_WriteShort (&client->datagram, (ent >> 8) | 0x8000);
				MSG_WriteByte (&client->datagram, ent & 0xff);
			}
			else
				MSG_WriteShort (&client->datagram, ent);
			MSG_WriteByte (&client->datagram, channel);
		}
		else
			MSG_WriteShort (&client->datagram, (ent << 3) | channel);
		if (field_mask & SND_LARGESOUND)
			MSG_WriteShort (&client->datagram, sound_num);
		else
			MSG_WriteByte (&client->datagram, sound_num);
		// johnfitz

		for (i = 0; i < 3; i++)
		{
			if (origin)
				MSG_WriteCoord (&client->datagram, origin[i], sv.protocolflags);
			else
				MSG_WriteCoord (&client->datagram, entity->v.origin[i] + 0.5 * (entity->v.mins[i] + entity->v.maxs[i]), sv.protocolflags);
		}
	}
}

/*
==================
SV_LocalSound - for 2021 rerelease
==================
*/
void SV_LocalSound (client_t *client, const char *sample)
{
	int sound_num, field_mask;

	for (sound_num = 1; sound_num < MAX_SOUNDS && sv.sound_precache[sound_num]; sound_num++)
	{
		if (!strcmp (sample, sv.sound_precache[sound_num]))
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
	return strcmp (NET_QSocketGetTrueAddressString (client->netconnection), "LOCAL") == 0;
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
	static const char *vrik_offers[] = {
		"//vrik_protocol 4\n",
		"//vrik_protocol 3\n",
		"//vrik_protocol 2\n"
	};
	const char **s;
	char		 message[2048];
	unsigned int i; // johnfitz
	unsigned int previous_qsvr = client->protocol_qsvr;
	qboolean	 cantruncate;
	qboolean	 truncated = false;

	SV_ResetPrivateVRContactState (client);
	client->vr_gorilla_capable = false;
	client->vr_gorilla_last_advertised = -1;
	client->vr_instant_stop_offered = false;
	client->vr_instant_stop_capable = false;
	client->weapon_contact_last_mode = -1;
	client->weapon_contact_last_profile = -1;
	client->akimbo_last_advertised_mask = -1;
	client->spawned = false; // need prespawn, spawn, etc
	client->voice_protocol_offered = false;
	client->avatar_offer_pending = AVATAR_OFFER_NUMERIC | AVATAR_OFFER_CUSTOM;

	// assume some safe defaults if we early out.
	client->limit_unreliable = 1024;
	client->limit_reliable = 8192;
	client->limit_entities = 0;
	client->limit_models = 0;
	client->limit_sounds = 0;

	client->protocol_qsvr = 0;
	client->protocol_pext1 = 0;
	client->protocol_pext2 = 0;
	client->csqcactive = false;
	if (!sv_protocol_pext2)
	{ // server disabled pext completely, don't bother trying.
		// make sure we try reenabling it again on the next map though.
		client->pextknown = false;
		client->offered_qsvr = 0;
		client->offered_metadata = 0;
		client->offered_pmove_policies = 0;
		client->offered_pext1 = 0;
		client->offered_pext2 = 0;
	}
	else if (client->pextknown)
	{
		client->protocol_pext1 = client->offered_pext1 & sv_protocol_pext1;
		client->protocol_pext2 = client->offered_pext2 & sv_protocol_pext2;
		if (!(client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
			client->protocol_pext2 &= ~PEXT2_PREDINFO; // stats can't be deltaed if there's no deltas, so just pretend its not supported on its own.

		if (sv_qsvr_private.value && client->netconnection &&
			client->offered_qsvr == QSVR_PROTOCOL_PINNED &&
			(client->offered_pext2 & PEXT2_SUPPORTED_CLIENT) == PEXT2_SUPPORTED_CLIENT &&
			(sv_protocol_pext2 & PEXT2_SUPPORTED_SERVER) == PEXT2_SUPPORTED_SERVER &&
			sv.protocol == PROTOCOL_RMQ && sv.protocolflags == (PRFL_FLOATCOORD | PRFL_SHORTANGLE))
		{
			client->protocol_qsvr = QSVR_PROTOCOL_PINNED;
			client->protocol_pext1 = 0;
			client->protocol_pext2 = QSVR_PEXT2_REQUIRED;
		}
	}

	if (previous_qsvr == QSVR_PROTOCOL_PINNED || client->protocol_qsvr == QSVR_PROTOCOL_PINNED)
	{
		// A new serverinfo starts a new command sequence and input lifetime.
		SV_ResetPrivateCommandQueue (client);
		client->lastmovemessage = 0;
		client->private_completed_move = 0;
		client->lastmovetime = 0;
		client->private_latest_buttons = 0;
		client->private_latched_buttons = 0;
		client->private_latched_impulse = 0;
		memset (&client->cmd, 0, sizeof (client->cmd));
	}

	if (sv_protocol_pext2 && !client->pextknown)
	{
		MSG_WriteByte (&client->message, svc_stufftext);
		MSG_WriteString (&client->message, "cmd pext\n");
		client->sendsignon = PRESPAWN_FLUSH;
		return;
	}
	SV_MetadataRearmClient (client);

	// now we know their protocol, pick some real defaults that match the limits of the engine that most defines that protocol's limits.
	switch (client->protocol_pext2 ? PROTOCOL_FTE_PEXT2 : sv.protocol)
	{
	default: // eep
	case PROTOCOL_NETQUAKE:
		client->limit_unreliable = 1024;
		client->limit_reliable = 8192;
		if (sv_protocol_pext2 && NET_QSocketGetProQuakeAngleHack (client->netconnection))
			client->limit_entities = 2048; // proquake supports more so assume we can use that limit if angles are also available (but only if we're not
										   // being strict about protocols)
		else
			client->limit_entities = 600; // vanilla sucks.
		client->limit_models = 256;		  // single byte
		client->limit_sounds = 256;		  // single byte
		break;
	case PROTOCOL_FITZQUAKE: // fitzquake didn't get abused quite as much as later engines did.
		client->limit_unreliable = 32000;
		client->limit_reliable = 32000;
		client->limit_entities = 32000;
		client->limit_models = 2048;
		client->limit_sounds = 2048;
		break;
	case PROTOCOL_RMQ: // actually QS - a moving target, so use our server's limits.
		client->limit_unreliable = 32000;
		client->limit_reliable = 64000;
		client->limit_entities = 32000;
		client->limit_models = 2048;
		client->limit_sounds = 2048;
		break;
	case PROTOCOL_FTE_PEXT2:					   // not a real protocol in itself, used to indicate QSS's full limits. FTE will match or allow higher.
		client->limit_unreliable = NET_MAXMESSAGE; // some safe ethernet limit. these clients should accept pretty much anything, but any routers will not.
		client->limit_reliable = NET_MAXMESSAGE;   // adhere to fitzquake's limits if we're recording a demoquite large, ip allows 16 bits
		client->limit_entities = MAX_EDICTS;	   // we don't really know, 8k is probably a save guess but could be 32k, 65k, or even more...
		client->limit_models = MAX_MODELS;		   // not really sure, client's problem until >14bits
		client->limit_sounds = MAX_SOUNDS;		   // not really sure, client's problem until >14bits
		break;
	}

	if (!strcmp (NET_QSocketGetTrueAddressString (client->netconnection), "LOCAL"))
	{ // might as well super-size it. demo playback doesn't care. mostly only affects vanilla. we should trigger other warnings if this limit is exceeded so
	  // don't worry about testers.
		client->limit_unreliable = client->limit_reliable;
	}
	else
	{ // remote clients must not exceed ip MTUs.
		if (client->limit_unreliable > DATAGRAM_MTU)
			client->limit_unreliable = DATAGRAM_MTU;
	}
	if (client->limit_entities > 0x8000 && !(client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
		client->limit_entities =
			0x8000; // pext2 changes the encoding of entities to support 23 bits instead of dpp7's 15bits or vanilla's 16bits, but our writeentity is lazy.
	if (client->limit_entities > (unsigned int)qcvm->max_edicts)
		client->limit_entities = (unsigned int)qcvm->max_edicts;

	// unfortunately we can't split this up, so if its oversized, we'll just let the client complain instead of always kicking them
	client->message.maxsize = sizeof (client->msgbuf);
	if (client->message.maxsize > (int)client->limit_reliable)
		client->message.maxsize = client->limit_reliable;
	if (client->datagram.maxsize > (int)client->limit_unreliable)
		client->datagram.maxsize = client->limit_unreliable;

	NET_QSocketSetMSS (client->netconnection, client->limit_unreliable);

	if (client->message.cursize)
	{ // try and flush the reliable NOW, in case the qc is evil
		if (NET_CanSendMessage (host_client->netconnection))
		{
			if (NET_SendMessage (host_client->netconnection, &host_client->message) > 0)
			{
				SZ_Clear (&host_client->message);
				if (host_client->signon_chunk_pending)
					host_client->message.maxsize = host_client->signon_message_capacity;
				host_client->signon_chunk_pending = false;
				host_client->last_message = realtime;
			}
		}
	}

	cantruncate = client->message.cursize == 0;
retry:
	MSG_WriteByte (&client->message, svc_print);
	//	q_snprintf (message, "%c\nFITZQUAKE %1.2f SERVER (%i CRC)\n", 2, FITZQUAKE_VERSION, pr_crc); //johnfitz -- include fitzquake version
	q_snprintf (
		message, sizeof (message), "%c\n" ENGINE_NAME_AND_VER " Server (%i CRC)\n", 2,
		qcvm->progscrc); // spike -- quakespasm has moved on, and has its own server capabilities now. Advertising = good, right?
	MSG_WriteString (&client->message, message);

	MSG_WriteByte (&client->message, svc_serverinfo);

	if (client->protocol_pext1)
	{
		MSG_WriteLong (&client->message, PROTOCOL_FTE_PEXT1);
		MSG_WriteLong (&client->message, client->protocol_pext1);
	}
	if (client->protocol_qsvr == QSVR_PROTOCOL_PINNED)
	{
		MSG_WriteLong (&client->message, PROTOCOL_QSVR_PROFILE);
		MSG_WriteLong (&client->message, QSVR_PROTOCOL_PINNED);
	}
	if (client->protocol_pext2)
	{ // pext stuff takes the form of modifiers to an underlaying protocol
		MSG_WriteLong (&client->message, PROTOCOL_FTE_PEXT2);
		MSG_WriteLong (&client->message, client->protocol_pext2); // active extensions that the client needs to look out for
	}
	MSG_WriteLong (&client->message, sv.protocol); // johnfitz -- sv.protocol instead of PROTOCOL_VERSION

	if (sv.protocol == PROTOCOL_RMQ)
	{
		// mh - now send protocol flags so that the client knows the protocol features to expect
		MSG_WriteLong (&client->message, sv.protocolflags);
	}

	if (client->protocol_pext2 & PEXT2_PREDINFO)
	{
		// if multiple gamedirs were used, we should list all the active ones eg: "id1;hipnotic;rogue;quoth;mod".
		// fixme: engine-specific forced gamedirs like id1/ or qw/ or fte/ are redundant, so don't bother listing them
		// we don't really track that stuff, so I'm just going to report the last one
		MSG_WriteString (&client->message, COM_GetGameNames (false));
	}

	MSG_WriteByte (&client->message, svs.maxclients);

	if (!coop.value && deathmatch.value)
		MSG_WriteByte (&client->message, GAME_DEATHMATCH);
	else
		MSG_WriteByte (&client->message, GAME_COOP);

	MSG_WriteString (&client->message, PR_GetString (qcvm->edicts->v.message));

	// johnfitz -- only send the first 256 model and sound precaches if protocol is 15
	for (i = 1, s = sv.model_precache + 1; *s && i < client->limit_models; s++, i++)
		MSG_WriteString (&client->message, *s);
	MSG_WriteByte (&client->message, 0);
	client->signon_models = i;

	// Spike: if we have svc_precache then use it for sounds. this reduces the stress on the serverinfo message size.
	if (host_client->protocol_pext2 && truncated)
		i = 1; // we tried, it didn't fit.
	else
		for (i = 1, s = sv.sound_precache + 1; *s && i < client->limit_sounds; s++, i++)
			MSG_WriteString (&client->message, *s);
	MSG_WriteByte (&client->message, 0);
	client->signon_sounds = i;
	// johnfitz

	// send music
	MSG_WriteByte (&client->message, svc_cdtrack);
	MSG_WriteByte (&client->message, qcvm->edicts->v.sounds);
	MSG_WriteByte (&client->message, qcvm->edicts->v.sounds);

	// set view
	MSG_WriteByte (&client->message, svc_setview);
	MSG_WriteShort (&client->message, NUM_FOR_EDICT (client->edict));

	MSG_WriteByte (&client->message, svc_signonnum);
	MSG_WriteByte (&client->message, 1);

	client->sendsignon = PRESPAWN_FLUSH;

	SVFTE_SetupFrames (client);

	if (client->message.overflowed && client->limit_models > 64 && cantruncate)
	{
		if (!host_client->protocol_pext2 || truncated)
		{ // first time around we can just drop sounds completely, filling them in later.
			// theoretically we can do the same with models too, but we don't entirely trust clients to handle lightmaps properly when its external bmodels.
			if (client->limit_models > client->limit_sounds || host_client->protocol_pext2)
				client->limit_models /= 2;
			else
				client->limit_sounds /= 2;
		}
		SZ_Clear (&client->message);
		truncated = true;
		goto retry;
	}
	/* Keep each offer inside serverinfo and after all ordinary precaches. A
	 * fitting newer offer remains useful even when a later offer cannot fit. */
	if (client->netconnection && !client->message.overflowed &&
		client->message.cursize >= 0 && client->message.maxsize >= 0 &&
		client->message.cursize <= client->message.maxsize)
	{
		unsigned int offer_index;
		for (offer_index = 0; offer_index < countof (vrik_offers); ++offer_index)
		{
			size_t required = strlen (vrik_offers[offer_index]) + 2;
			if (required > (size_t)(client->message.maxsize -
				client->message.cursize))
				continue;
			MSG_WriteByte (&client->message, svc_stufftext);
			MSG_WriteString (&client->message, vrik_offers[offer_index]);
		}
		if (sv_voice.value)
		{
			static const char voice_offer[] = "//voice_protocol 1\n";
			size_t required = sizeof (voice_offer) + 1;
			if (required <= (size_t)(client->message.maxsize -
				client->message.cursize))
			{
				MSG_WriteByte (&client->message, svc_stufftext);
				MSG_WriteString (&client->message, voice_offer);
				client->voice_protocol_offered = true;
			}
		}
		SV_AppendAvatarOffers (client);
	}

	// try and flush the reliable NOW, in case the qc is evil
	if (NET_CanSendMessage (client->netconnection))
	{
		if (NET_SendMessage (client->netconnection, &client->message) != -1)
		{
			SZ_Clear (&client->message);
			client->last_message = realtime;
			client->sendsignon = PRESPAWN_DONE;
		}
	}

	if (truncated)
		Con_Printf ("Protocol limitation (serverinfo) for %s\n", NET_QSocketGetTrueAddressString (client->netconnection));
}

static unsigned int SV_PextMetadataVersion (void)
{
	const char *raw = Cmd_Args ();
	const char *start;
	char *end;
	unsigned long parsed;
	unsigned int key = 0;
	unsigned int metadata = 0;
	int saved_errno;
	int argc = Cmd_Argc ();
	int i;
	qboolean seen = false;

	if (argc < 3 || ((argc - 1) & 1))
		return 0;
	for (i = 1; i < argc; i++)
	{
		const char *arg = Cmd_Argv (i);
		size_t len = strlen (arg);

		while (*raw && (unsigned char)*raw <= ' ')
			raw++;
		start = raw;
		if (!len || strncmp (start, arg, len) ||
			(start[len] && (unsigned char)start[len] > ' ') ||
			arg[0] == '+' || arg[0] == '-')
			return 0;
		raw = start + len;
		saved_errno = errno;
		errno = 0;
		parsed = strtoul (arg, &end, 0);
		if (errno == ERANGE || end == arg || *end || parsed > UINT_MAX)
		{
			errno = saved_errno;
			return 0;
		}
		errno = saved_errno;
		if (i & 1)
			key = (unsigned int)parsed;
		else if (key == PROTOCOL_QSVR_METADATA)
		{
			if (seen || parsed != QSVR_METADATA_VERSION)
				return 0;
			seen = true;
			metadata = (unsigned int)parsed;
		}
	}
	while (*raw && (unsigned char)*raw <= ' ')
		raw++;
	return *raw || !seen ? 0 : metadata;
}

void SV_Pext_f (void)
{
	// this only makes sense on the server. the clientside part only takes the form of 'cmd pext', for compat with clients that don't support this.
	if (cmd_source != src_client)
	{
		if (!cls.state)
		{
			Con_Printf ("Not connected\n");
			return;
		}
		Con_Printf ("Current Protocols:\n");
		if (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
			Con_Printf ("  Replacement Entity Deltas\n");
		if (cl.protocol_pext2 & PEXT2_PREDINFO)
			Con_Printf ("  Replacement Stats ('predinfo')\n");
		if (cl.protocol == PROTOCOL_NETQUAKE)
			Con_Printf ("  vanilla(15)\n");
		else if (cl.protocol == PROTOCOL_FITZQUAKE)
			Con_Printf ("  fitzquake(666)\n");
		else if (cl.protocol == PROTOCOL_RMQ)
			Con_Printf ("  rmq(999)\n");
		else
			Con_Printf ("  unknown protocol(%i)\n", cl.protocol);
		return;
	}

	if (!host_client->pextknown && !host_client->spawned)
	{
		int i;
		int key;
		int value;
		unsigned int offered_metadata = SV_PextMetadataVersion ();

		for (i = 1; i < Cmd_Argc (); i += 2)
		{
			key = strtoul (Cmd_Argv (i), NULL, 0);
			value = strtoul (Cmd_Argv (i + 1), NULL, 0);

			if (key == PROTOCOL_FTE_PEXT1)
				host_client->offered_pext1 = value;
			else if (key == PROTOCOL_FTE_PEXT2)
				host_client->offered_pext2 = value;
			else if (key == PROTOCOL_QSVR_PROFILE && value == QSVR_PROTOCOL_PINNED)
				host_client->offered_qsvr = value;
			else if (key == PROTOCOL_QSVR_PMOVE_POLICIES)
				host_client->offered_pmove_policies = value & QSVR_PMOVE_CAP_SUPPORTED;
			// else some other extension that we don't know
		}

		host_client->offered_metadata = offered_metadata;
		host_client->pextknown = true;
		SV_SendServerinfo (host_client);
	}
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
	edict_t			 *ent;
	client_t		 *client;
	int				  edictnum;
	struct qsocket_s *netconnection;
	int				  i;
	float			  spawn_parms[NUM_TOTAL_SPAWN_PARMS];
	qboolean		  defer_spawn_parms;

	client = svs.clients + clientnum;
	if (clientnum >= 0 && clientnum < MAX_SCOREBOARD)
		svs.coop_initial_spawn_client[clientnum] = false;
	SV_CoopRespawnCancelBorrowedPolicy (client, client->edict);

	if (client->netconnection)
		Con_DPrintf ("Client %s connected\n", NET_QSocketGetTrueAddressString (client->netconnection));
	else
		Con_DPrintf ("Bot connected\n");

	edictnum = clientnum + 1;

	ent = EDICT_NUM (edictnum);

	// set up the client_t
	netconnection = client->netconnection;
	net_activeconnections++;
	defer_spawn_parms = sv.loadgame && sv.loadgame_multiplayer;

	if (sv.loadgame)
		memcpy (spawn_parms, client->spawn_parms, sizeof (spawn_parms));
	memset (client, 0, sizeof (*client));
	SV_ResetPrivateCommandQueue (client);
	SV_ResetPrivateVRContactState (client);
	client->netconnection = netconnection;
	client->avatar_id = PLAYER_AVATAR_RANGER;
	client->voice_generation = SV_NextVoiceGeneration ();

	strcpy (client->name, "unconnected");
	client->active = true;
	client->spawned = false;
	/* A reused slot must show Ranger to capable peers until its new owner
	 * selects an avatar. */
	SV_BroadcastAvatarSlot (clientnum, PLAYER_AVATAR_RANGER);
	client->spawn_parms_pending = defer_spawn_parms;
	client->edict = ent;
	client->message.data = client->msgbuf;
	client->message.maxsize = sizeof (client->msgbuf);
	client->message.allowoverflow = true; // we can catch it

	client->datagram.data = client->datagram_buf;
	client->datagram.maxsize = sizeof (client->datagram_buf);
	client->datagram.allowoverflow = true; // simply ignored on overflow

	client->pextknown = false;
	client->offered_qsvr = 0;
	client->offered_metadata = 0;
	client->offered_pmove_policies = 0;
	client->offered_pext2 = 0;
	client->protocol_qsvr = 0;
	client->protocol_pext2 = 0;
	client->lastmovemessage = 0;
	client->private_completed_move = 0;

	if (sv.loadgame)
		memcpy (client->spawn_parms, spawn_parms, sizeof (spawn_parms));
	else if (!defer_spawn_parms)
	{
		// call the progs to get default spawn parms for the new client
		PR_ExecuteProgram (pr_global_struct->SetNewParms);
		for (i = 0; i < NUM_BASIC_SPAWN_PARMS; i++)
			client->spawn_parms[i] = (&pr_global_struct->parm1)[i];
		for (; i < NUM_TOTAL_SPAWN_PARMS; i++)
		{
			ddef_t *g = ED_FindGlobal (va ("parm%i", i + 1));
			client->spawn_parms[i] = g ? qcvm->globals[g->ofs] : 0;
		}
	}

	SV_MetadataUserinfoChanged (clientnum);
	SV_SendServerinfo (client);
}

/*
===================
SV_CheckForNewClients

===================
*/
void SV_CheckForNewClients (void)
{
	struct qsocket_s *ret;
	int				  i;

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
		for (i = 0; i < svs.maxclients; i++)
			if (!svs.clients[i].active)
				break;
		if (i == svs.maxclients)
			Sys_Error ("Host_CheckForNewClients: no free clients");

		svs.clients[i].netconnection = ret;
		SV_ConnectClient (i);
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

static int		fatbytes;
static byte	   *fatpvs;
static int		fatpvs_capacity;
static qboolean fatpvs_any;

void SV_AddToFatPVS (vec3_t org, mnode_t *node, qmodel_t *worldmodel) // johnfitz -- added worldmodel as a parameter
{
	int		  i;
	byte	 *pvs;
	mplane_t *plane;
	float	  d;

	while (1)
	{
		// if this is a leaf, accumulate the pvs bits
		if (node->contents < 0)
		{
			if (node->contents != CONTENTS_SOLID)
			{
				fatpvs_any = true;
				pvs = Mod_LeafPVS ((mleaf_t *)node, worldmodel); // johnfitz -- worldmodel as a parameter
				for (i = 0; i < fatbytes - 3; i += 4)
					*(uint32_t *)&fatpvs[i] |= *(uint32_t *)&pvs[i];
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
		{														 // go down both
			SV_AddToFatPVS (org, node->children[0], worldmodel); // johnfitz -- worldmodel as a parameter
			node = node->children[1];
		}
	}
}

void SV_SetupSkyRoom (const char *value)
{
	float values[8];
	int count;
	sv.skyroom_pos_known = false;
	memset (sv.skyroom_pos, 0, sizeof (sv.skyroom_pos));
	if (!Skyroom_ParseMetadata (value, values, &count))
		return;
	VectorCopy (values, sv.skyroom_pos);
	sv.skyroom_pos[3] = count >= 4 ? values[3] : 0.0f;
	sv.skyroom_pos_known = true;
}

static void SV_AddSkyRoomPVS (const vec3_t org, qmodel_t *worldmodel)
{
	vec3_t skyorg;
	if (!sv_skyroom_pvs.value || !sv.skyroom_pos_known || !worldmodel || !worldmodel->nodes)
		return;
	if (!Skyroom_ViewOrigin (sv.skyroom_pos, org, skyorg))
		return;
	/* SV_FatPVS already cleared and populated the shared fatpvs buffer for
	 * this viewer. Extend that buffer instead of replacing the main PVS. */
	SV_AddToFatPVS (skyorg, worldmodel->nodes, worldmodel);
}

/*
=============
SV_FatPVS

Calculates a PVS that is the inclusive or of all leafs within 8 pixels of the
given point.
=============
*/
byte *SV_FatPVS (vec3_t org, qmodel_t *worldmodel) // johnfitz -- added worldmodel as a parameter
{
	fatbytes = (worldmodel->numleafs + 31) / 8;
	if (fatpvs == NULL || fatbytes > fatpvs_capacity)
	{
		fatpvs_capacity = fatbytes;
		fatpvs = (byte *)Mem_Realloc (fatpvs, fatpvs_capacity);
		if (!fatpvs)
			Sys_Error ("SV_FatPVS: realloc() failed on %d bytes", fatpvs_capacity);
	}

	memset (fatpvs, 0, fatbytes);
	fatpvs_any = false;
	SV_AddToFatPVS (org, worldmodel->nodes, worldmodel); // johnfitz -- worldmodel as a parameter
	if (fatpvs_any == false)
		memset (fatpvs, 0xff, fatbytes);
	return fatpvs;
}

/*
=============
SV_EdictInPVS
=============
*/
qboolean SV_EdictInPVS (edict_t *test, byte *pvs)
{
	unsigned int i;

	for (i = 0; i < test->num_leafs; i++)
		if (pvs[test->leafnums[i] >> 3] & (1 << (test->leafnums[i] & 7)))
			return true;

	return false;
}

/*
=============
SV_VisibleToClient -- johnfitz

PVS test encapsulated in a nice function
=============
*/
qboolean SV_VisibleToClient (edict_t *client, edict_t *test, qmodel_t *worldmodel)
{
	byte  *pvs;
	vec3_t org;

	VectorAdd (client->v.origin, client->v.view_ofs, org);
	pvs = SV_FatPVS (org, worldmodel);

	return SV_EdictInPVS (test, pvs);
}

//=============================================================================

static uint16_t net_edicts[MAX_EDICTS];
static byte		net_edict_dists[MAX_EDICTS];
static int		net_edict_bins[256];
static uint16_t net_edicts_sorted[MAX_EDICTS];

/*
=============
SV_WriteEntitiesToClient

=============
*/
static qboolean SV_WriteEntitiesToClient (client_t *client, sizebuf_t *msg, size_t overflowsize)
{
	struct qsocket_s *socket = client->netconnection;
	edict_t		*clent = client->edict;
	unsigned int e, i, maxedict = qcvm->num_edicts, j, numents;
	int			 bits;
	byte		*pvs;
	vec3_t		 org, forward, right, up;
	float		 miss, dist, size;
	edict_t		*ent, *parent;
	eval_t		*val;
	size_t		 rollbacksize, origmaxsize = msg->maxsize;
	qboolean	 sort = sv_netsort.value > 1;
	qboolean     recipient_live = true;
	float		 scale;
	const char	*model;

	// with sv_netsort = 1, sort only if (any client) overflowed in the last 10 seconds
	if (sv_netsort.value == 1 && dev_overflows.packetsize + 10 > realtime)
		sort = true;

	ED_Retain (clent); // Slot zero in the gathered list owns this retain exactly once.
	msg->maxsize = overflowsize;

	if (maxedict > client->limit_entities)
		maxedict = client->limit_entities;

	// find the client's PVS
	VectorAdd (clent->v.origin, clent->v.view_ofs, org);
	pvs = SV_FatPVS (org, qcvm->worldmodel);
	SV_AddSkyRoomPVS (org, qcvm->worldmodel);

	// find the client's orientation
	AngleVectors (clent->v.v_angle, forward, right, up);

	// reset sorting bins
	memset (net_edict_bins, 0, sizeof (net_edict_bins));

	// add clent
	if (sort)
	{
		net_edicts[0] = NUM_FOR_EDICT (clent);
		net_edict_dists[0] = 0;
		net_edict_bins[0] = 1;
	}
	else
		net_edicts_sorted[0] = NUM_FOR_EDICT (clent);
	numents = 1;

	// add all other entities that touch the pvs
	ent = NEXT_EDICT (qcvm->edicts);
	for (e = 1; e < maxedict; e++, ent = NEXT_EDICT (ent))
	{
		if (ent->free)
			continue;
		qboolean visible = SV_CustomizeEntityForClient (ent, clent);
		if (!SV_ClientConnectionMatches (client, socket) || clent->free)
		{
			recipient_live = false;
			goto cleanup;
		}
		if (ent->free || (ent != clent && !visible))
			continue;
		if (ent != clent) // clent already added before the loop
		{
			parent = SV_VisibilityParent (ent);
			if (!parent)
				continue;
			// ignore ents without visible models
			if (!ent->v.modelindex || !(model = PR_GetString (ent->v.model))[0])
				continue;

			// johnfitz -- don't send model>255 entities if protocol is 15
			if ((unsigned int)ent->v.modelindex >= client->limit_models)
				continue;

			val = GetEdictFieldValue (ent, qcvm->extfields.pvsflags);
			if (!SV_VisibilityPVS (parent, pvs, val ? (int)val->_float : PVSF_NORMALPVS, false))
				continue; // not visible

			if (sort)
			{
				// compute ent bbox size and distance from org to the closest point in ent's bbox
				dist = size = 0.f;
				for (i = 0; i < 3; i++)
				{
					float delta = CLAMP (ent->v.absmin[i], org[i], ent->v.absmax[i]) - org[i];
					dist += delta * delta;
					delta = ent->v.absmax[i] - ent->v.absmin[i];
					size += delta * delta;
				}
				size = q_max (1.f, size);

				// prioritize point-sized projectiles that do something on impact
				if (size < 50 && ent->v.touch)
				{
					if (ent->v.movetype == MOVETYPE_FLYMISSILE || ent->v.movetype == MOVETYPE_FLY)
					{
						vec3_t to_self;
						VectorSubtract (org, ent->v.origin, to_self);
						float direction = DotProduct (ent->v.velocity, to_self); // if >0, coming toward us; otherwise rockets always get priority
						size = (direction > 0 || strstr (model, "miss") || strstr (model, "rocket")) ? 3072 : 768; // set a 32-/16-sided cube's size
					}
					else if (ent->v.movetype == MOVETYPE_BOUNCE || ent->v.movetype == MOVETYPE_TOSS)
						// for gibs, set size to a 16-sided cube. for grenades / lavaballs, 32-sided cube
						size = (ent->v.nextthink > 0 && !strstr (model, "gib")) ? 3072 : 768;
				}

				// use scaled square root of (distance/size) as sort key
				dist = 8.f * sqrt (sqrt (dist / size));
				net_edict_dists[numents] = (int)q_min (dist, 255.f);
				net_edicts[numents] = e;

				// compute max distance along forward axis
				dist = 0.f;
				for (i = 0; i < 3; i++)
					dist += ((forward[i] < 0.f ? ent->v.absmin[i] : ent->v.absmax[i]) - org[i]) * forward[i];
				if (dist < 0.f)
					net_edict_dists[numents] |= 128; // deprioritize entities behind the client

				net_edict_bins[net_edict_dists[numents]]++;
			}
			else
				net_edicts_sorted[numents] = e;

			ED_Retain (ent); // Later customization may free an already gathered candidate.
			++numents;
		}
		else
			continue;
	}

	if (sort)
	{
		// compute bin offsets
		e = 0;
		for (i = 0; i < countof (net_edict_bins); i++)
		{
			int tmp = net_edict_bins[i];
			net_edict_bins[i] = e;
			e += tmp;
		}

		// generate sorted list
		for (e = 0; e < numents; e++)
			net_edicts_sorted[net_edict_bins[net_edict_dists[e]]++] = net_edicts[e];
	}

	// send entities (closest first)
	for (j = 0; j < numents; j++)
	{
		e = net_edicts_sorted[j];
		ent = EDICT_NUM (e);
		if (ent->free || (ent != clent && (!ent->v.modelindex ||
			!PR_GetString (ent->v.model)[0] || (unsigned int)ent->v.modelindex >= client->limit_models)))
			continue;

		rollbacksize = msg->cursize;

		// send an update
		bits = 0;

		vec3_t origin;
		if (SV_UsePredThinkPos (ent))
			VectorCopy (ent->predthinkpos, origin);
		else
			VectorCopy (ent->v.origin, origin);

		for (i = 0; i < 3; i++)
		{
			miss = origin[i] - ent->baseline.origin[i];
			if (miss < -0.1 || miss > 0.1)
				bits |= U_ORIGIN1 << i;
		}

		if (ent->v.angles[0] != ent->baseline.angles[0])
			bits |= U_ANGLE1;

		if (ent->v.angles[1] != ent->baseline.angles[1])
			bits |= U_ANGLE2;

		if (ent->v.angles[2] != ent->baseline.angles[2])
			bits |= U_ANGLE3;

		if (ent->v.movetype == MOVETYPE_STEP)
			bits |= U_STEP; // don't mess up the step animation

		if (ent->baseline.colormap != ent->v.colormap)
			bits |= U_COLORMAP;

		if (ent->baseline.skin != ent->v.skin)
			bits |= U_SKIN;

		if (ent->baseline.frame != ent->v.frame)
			bits |= U_FRAME;

		if ((ent->baseline.effects ^ (int)ent->v.effects) & sv.effectsmask)
			bits |= U_EFFECTS;

		if (ent->baseline.modelindex != ent->v.modelindex)
			bits |= U_MODEL;

		// hide if the current client is specified
		val = GetEdictFieldValue (ent, qcvm->extfields.nodrawtoclient);
		if (ent != clent && val && val->edict == EDICT_TO_PROG (clent))
			continue;

		// hide if the current client is not specified
		val = GetEdictFieldValue (ent, qcvm->extfields.drawonlytoclient);
		if (ent != clent && val && val->edict && val->edict != EDICT_TO_PROG (clent))
			continue;
		val = GetEdictFieldValue (ent, qcvm->extfields.viewmodelforclient);
		if (ent != clent && val && val->edict)
			continue;
		val = GetEdictFieldValue (ent, qcvm->extfields.exteriormodeltoclient);
		if (ent != clent && val && val->edict == EDICT_TO_PROG (clent))
			continue;

		// johnfitz -- alpha
		//  TODO: find a cleaner place to put this code
		val = GetEdictFieldValue (ent, qcvm->extfields.alpha);
		if (val)
			ent->alpha = ENTALPHA_ENCODE (val->_float);

		// don't send invisible entities unless they have effects
		if (ent != clent && ent->alpha == ENTALPHA_ZERO && !((int)ent->v.effects & sv.effectsmask) &&
			!SV_HasParticleEffect (ent, qcvm->extfields.traileffectnum) &&
			!SV_HasParticleEffect (ent, qcvm->extfields.emiteffectnum))
			continue;
		// johnfitz

		val = GetEdictFieldValue (ent, qcvm->extfields.scale);
		scale = val ? ENTSCALE_ENCODE (val->_float) : ENTSCALE_DEFAULT;

		// johnfitz -- PROTOCOL_FITZQUAKE
		if (sv.protocol != PROTOCOL_NETQUAKE)
		{
			if (ent->baseline.alpha != ent->alpha)
				bits |= U_ALPHA;
			if (sv.protocol == PROTOCOL_RMQ)
			{
				if (ent->baseline.scale != scale)
					bits |= U_SCALE;
			}
			else if (ENTSCALE_DEFAULT != scale) // for 666, we didn't send the scale in the baseline!
				bits |= U_SCALE;
			if (bits & U_FRAME && (int)ent->v.frame > 255)
				bits |= U_FRAME2;
			if (bits & U_MODEL && (int)ent->v.modelindex > 255)
				bits |= U_MODEL2;
			// nonstandard intervals are always sent; the default 0.1 the client assumes anyway is only worth
			// the extra bytes on clients that are not constrained by the DATAGRAM_MTU packet budget
			if (ent->sendinterval || (ent->sendinterval_default && client->limit_unreliable > DATAGRAM_MTU))
				bits |= U_LERPFINISH;
			if (bits >= 65536)
				bits |= U_EXTEND1;
			if (bits >= 16777216)
				bits |= U_EXTEND2;
		}
		// johnfitz

		if (e >= 256)
			bits |= U_LONGENTITY;

		if (bits >= 256)
			bits |= U_MOREBITS;

		//
		// write the message
		//
		MSG_WriteByte (msg, (bits | U_SIGNAL) & 0xff);

		if (bits & U_MOREBITS)
			MSG_WriteByte (msg, (bits >> 8) & 0xff);

		// johnfitz -- PROTOCOL_FITZQUAKE
		if (bits & U_EXTEND1)
			MSG_WriteByte (msg, (bits >> 16) & 0xff);
		if (bits & U_EXTEND2)
			MSG_WriteByte (msg, (bits >> 24) & 0xff);
		// johnfitz

		if (bits & U_LONGENTITY)
			MSG_WriteShort (msg, e);
		else
			MSG_WriteByte (msg, e);

		if (bits & U_MODEL)
			MSG_WriteByte (msg, (int)ent->v.modelindex & 0xff);
		if (bits & U_FRAME)
			MSG_WriteByte (msg, (int)ent->v.frame & 0xff);
		if (bits & U_COLORMAP)
			MSG_WriteByte (msg, ent->v.colormap);
		if (bits & U_SKIN)
			MSG_WriteByte (msg, ent->v.skin);
		if (bits & U_EFFECTS)
			MSG_WriteByte (msg, (int)ent->v.effects & sv.effectsmask);
		if (bits & U_ORIGIN1)
			MSG_WriteCoord (msg, origin[0], sv.protocolflags);
		if (bits & U_ANGLE1)
			MSG_WriteAngle (msg, ent->v.angles[0], sv.protocolflags);
		if (bits & U_ORIGIN2)
			MSG_WriteCoord (msg, origin[1], sv.protocolflags);
		if (bits & U_ANGLE2)
			MSG_WriteAngle (msg, ent->v.angles[1], sv.protocolflags);
		if (bits & U_ORIGIN3)
			MSG_WriteCoord (msg, origin[2], sv.protocolflags);
		if (bits & U_ANGLE3)
			MSG_WriteAngle (msg, ent->v.angles[2], sv.protocolflags);

		// johnfitz -- PROTOCOL_FITZQUAKE
		if (bits & U_ALPHA)
			MSG_WriteByte (msg, ent->alpha);
		if (bits & U_SCALE)
			MSG_WriteByte (msg, scale);
		if (bits & U_FRAME2)
			MSG_WriteByte (msg, (int)ent->v.frame >> 8);
		if (bits & U_MODEL2)
			MSG_WriteByte (msg, (int)ent->v.modelindex >> 8);
		if (bits & U_LERPFINISH)
			MSG_WriteByte (msg, (byte)CLAMP (0, Q_rint ((ent->v.nextthink - qcvm->time) * 255), 255)); // qcvm->time may have advanced past nextthink
		// johnfitz

		if ((size_t)msg->cursize > origmaxsize)
		{
			msg->cursize = rollbacksize; // roll back
			// johnfitz -- less spammy overflow message
			if (!dev_overflows.packetsize || dev_overflows.packetsize + CONSOLE_RESPAM_TIME < realtime)
			{
				Con_Printf ("Packet overflow!\n");
				dev_overflows.packetsize = realtime;
			}
			break; // we could keep searching for something else that fits, but ehh
		}
	}

	cleanup:
	// The gather list is valid even if customization aborted before sorting.
	for (j = 0; j < numents; ++j)
		ED_Release (EDICT_NUM ((sort ? net_edicts : net_edicts_sorted)[j]));
	msg->maxsize = origmaxsize;

	// johnfitz -- devstats
	if (msg->cursize > 1024 && dev_peakstats.packetsize <= 1024)
		Con_DWarning ("%i byte packet exceeds standard limit of 1024 (max = %d).\n", msg->cursize, msg->maxsize);
	dev_stats.packetsize = msg->cursize;
	dev_peakstats.packetsize = q_max (msg->cursize, dev_peakstats.packetsize);
	// johnfitz
	return recipient_live;
}

/*
=============
SV_CleanupEnts

=============
*/
void SV_CleanupEnts (void)
{
	int		 e;
	edict_t *ent;

	ent = NEXT_EDICT (qcvm->edicts);

	for (e = 1; e < qcvm->num_edicts; e++, ent = NEXT_EDICT (ent))
	{
		ent->v.effects = (int)ent->v.effects & ~EF_MUZZLEFLASH;
	}
}

/*
==================
SV_WriteDamageToMessage

==================
*/
void SV_WriteDamageToMessage (edict_t *ent, sizebuf_t *msg)
{
	edict_t *other;
	int		 i;

	//
	// send a damage message
	//
	if (ent->v.dmg_take || ent->v.dmg_save)
	{
		other = PROG_TO_EDICT (ent->v.dmg_inflictor);
		MSG_WriteByte (msg, svc_damage);
		// damages are only 1 byte, clamp their vavlue
		if (ent->v.dmg_save > 255.0f)
			ent->v.dmg_save = 255.0f;
		MSG_WriteByte (msg, ent->v.dmg_save);

		if (ent->v.dmg_take > 255.0f)
			ent->v.dmg_take = 255.0f;
		MSG_WriteByte (msg, ent->v.dmg_take);

		for (i = 0; i < 3; i++)
			MSG_WriteCoord (msg, other->v.origin[i] + 0.5 * (other->v.mins[i] + other->v.maxs[i]), sv.protocolflags);

		ent->v.dmg_take = 0;
		ent->v.dmg_save = 0;
	}

	//
	// send the current viewpos offset from the view entity
	//
	SV_SetIdealPitch (); // how much to look up / down ideally

	// a fixangle might get lost in a dropped packet.  Oh well.
	if (ent->v.fixangle)
	{
		MSG_WriteByte (msg, svc_setangle);
		for (i = 0; i < 3; i++)
			MSG_WriteAngle (msg, ent->v.angles[i], sv.protocolflags);
		ent->v.fixangle = 0;
	}
}

/*
==================
SV_WriteClientdataToMessage

==================
*/
void SV_WriteClientdataToMessage (client_t *client, sizebuf_t *msg)
{
	edict_t		*ent = client->edict;
	int			 bits;
	int			 i;
	int			 items;
	int			 items2;
	unsigned int weaponmodelindex = SV_ModelIndex (PR_GetString (ent->v.weaponmodel));

	if (weaponmodelindex >= client->limit_models)
		weaponmodelindex = 0;

	bits = 0;

	if (ent->v.view_ofs[2] != DEFAULT_VIEWHEIGHT)
		bits |= SU_VIEWHEIGHT;

	if (ent->v.idealpitch)
		bits |= SU_IDEALPITCH;

	// stuff the sigil bits into the high bits of items for sbar, or else
	// mix in items2
	if (SV_ReadOptionalInventoryField (ent, "items2", &items2))
		items = (int)ent->v.items | ((uint32_t)items2 << 23);
	else
		items = (int)ent->v.items | ((int)pr_global_struct->serverflags << 28);

	bits |= SU_ITEMS;

	if ((int)ent->v.flags & FL_ONGROUND)
		bits |= SU_ONGROUND;

	if (ent->v.waterlevel >= 2)
		bits |= SU_INWATER;

	for (i = 0; i < 3; i++)
	{
		if (ent->v.punchangle[i])
			bits |= (SU_PUNCH1 << i);
		if (ent->v.velocity[i])
			bits |= (SU_VELOCITY1 << i);
	}

	if (ent->v.weaponframe)
		bits |= SU_WEAPONFRAME;

	if (ent->v.armorvalue)
		bits |= SU_ARMOR;

	//	if (ent->v.weapon)
	bits |= SU_WEAPON;

	// johnfitz -- PROTOCOL_FITZQUAKE
	if (sv.protocol != PROTOCOL_NETQUAKE)
	{
		if (bits & SU_WEAPON && weaponmodelindex > 255)
			bits |= SU_WEAPON2;
		if ((int)ent->v.armorvalue > 255)
			bits |= SU_ARMOR2;
		if ((int)ent->v.currentammo > 255)
			bits |= SU_AMMO2;
		if ((int)ent->v.ammo_shells > 255)
			bits |= SU_SHELLS2;
		if ((int)ent->v.ammo_nails > 255)
			bits |= SU_NAILS2;
		if ((int)ent->v.ammo_rockets > 255)
			bits |= SU_ROCKETS2;
		if ((int)ent->v.ammo_cells > 255)
			bits |= SU_CELLS2;
		if (bits & SU_WEAPONFRAME && (int)ent->v.weaponframe > 255)
			bits |= SU_WEAPONFRAME2;
		if (bits & SU_WEAPON && ent->alpha != ENTALPHA_DEFAULT)
			bits |= SU_WEAPONALPHA; // for now, weaponalpha = client entity alpha
		if (bits >= 65536)
			bits |= SU_EXTEND1;
		if (bits >= 16777216)
			bits |= SU_EXTEND2;
	}
	// johnfitz

	// send the data

	MSG_WriteByte (msg, svc_clientdata);
	MSG_WriteShort (msg, bits);

	// johnfitz -- PROTOCOL_FITZQUAKE
	if (bits & SU_EXTEND1)
		MSG_WriteByte (msg, bits >> 16);
	if (bits & SU_EXTEND2)
		MSG_WriteByte (msg, bits >> 24);
	// johnfitz

	if (bits & SU_VIEWHEIGHT)
		MSG_WriteChar (msg, ent->v.view_ofs[2]);

	if (bits & SU_IDEALPITCH)
		MSG_WriteChar (msg, ent->v.idealpitch);

	for (i = 0; i < 3; i++)
	{
		if (bits & (SU_PUNCH1 << i))
			MSG_WriteChar (msg, ent->v.punchangle[i]);
		if (bits & (SU_VELOCITY1 << i))
			MSG_WriteChar (msg, ent->v.velocity[i] / 16);
	}

	// [always sent]	if (bits & SU_ITEMS)
	MSG_WriteLong (msg, items);

	if (bits & SU_WEAPONFRAME)
		MSG_WriteByte (msg, (int)ent->v.weaponframe & 0xff);
	if (bits & SU_ARMOR)
		MSG_WriteByte (msg, (int)ent->v.armorvalue & 0xff);
	if (bits & SU_WEAPON)
		MSG_WriteByte (msg, (int)weaponmodelindex & 0xff);

	MSG_WriteShort (msg, ent->v.health);
	MSG_WriteByte (msg, (int)ent->v.currentammo & 0xff);
	MSG_WriteByte (msg, (int)ent->v.ammo_shells & 0xff);
	MSG_WriteByte (msg, (int)ent->v.ammo_nails & 0xff);
	MSG_WriteByte (msg, (int)ent->v.ammo_rockets & 0xff);
	MSG_WriteByte (msg, (int)ent->v.ammo_cells & 0xff);

	if (standard_quake)
	{
		MSG_WriteByte (msg, (int)ent->v.weapon & 0xff);
	}
	else
	{
		int weapon = 0;
		for (i = 0; i < 32; i++)
		{
			if (((int)ent->v.weapon) & (1 << i))
			{
				weapon = i;
				break;
			}
		}
		MSG_WriteByte (msg, weapon);
	}

	// johnfitz -- PROTOCOL_FITZQUAKE
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
		MSG_WriteByte (msg, ent->alpha); // for now, weaponalpha = client entity alpha
										 // johnfitz
}

static qboolean SV_PresendClientDatagram (client_t *client)
{
	if (!client->netconnection)
		return true; // botclient
	if (!client->spawned)
		return true; // not ready yet.
	if (!client->edict || client->edict->free)
		return false;
	if (!(client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
		return true; // brute force networking.
	if (!SVFTE_BuildSnapshotForClient (client))
		return false;
	SVFTE_CalcEntityDeltas (client);
	client->snapshotresume = 0;
	client->csqcsnapshotresume = 1;
	return true;
}

/*
=======================
SV_ParticleSize

If the start of buf contains a svc_particle, returns its size. Otherwise returns 0.
=======================
*/
static int SV_ParticleSize (byte *buf)
{
	if (buf[0] == svc_particle)
	{
		int coord_size = 2;
		if (sv.protocolflags & PRFL_24BITCOORD)
			coord_size = 3;
		else if (sv.protocolflags & (PRFL_FLOATCOORD | PRFL_INT32COORD))
			coord_size = 4;
		return 6 + 3 * coord_size;
	}
	else
		return 0;
}

/*
=======================
SV_SendClientDatagram
=======================
*/
qboolean SV_SendClientDatagram (client_t *client)
{
	struct qsocket_s *socket = client->netconnection;
	// made static to prevent too big stack usage.
	// fine as a temporary because only called from the main thread.
	static byte buf[MAX_DATAGRAM + 1000];
	sizebuf_t	msg;
	const char *trial_failure;

	if (!client->netconnection)
	{
		// botclient, shouldn't be sent anything.
		SZ_Clear (&client->datagram);
		return true;
	}

	msg.allowoverflow = false;
	msg.data = buf;
	msg.maxsize = q_min (MAX_DATAGRAM, client->limit_unreliable);
	msg.cursize = 0;

	host_client = client;
	if (client->spawned)
	{
		if (!client->edict || client->edict->free)
		{
			SV_DropClient (true);
			return false;
		}
		sv_player = client->edict;
		if (SV_PrivateWalkTrialSelected (client))
		{
			/* Always validate admission first so the owner is live before any
			 * selected data is serialized. Paused frames send without physics;
			 * their ACK permission stays off until an active-frame state check. */
			trial_failure = SV_PrivateWalkTrialAdmissionFailure (client);
			/* q30 preserves cached water until native WALK refreshes it
			 * after PreThink. Admission already performed its observational
			 * frame validation; the extra categorizing check is stock-only. */
			if (!trial_failure && !sv.paused && SV_PrivateWalkTrialStockProgram () &&
				SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_WALK)
				trial_failure = SV_PrivateWalkTrialStateError (client->edict, client,
					&client->cmd);
			if (trial_failure)
			{
				Con_Printf ("%s: dropping selected private WALK client: eligibility changed before snapshot: %s\n",
					client->name, trial_failure);
				SV_DropClient (false);
				return false;
			}
		}

		if (client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
		{
			struct deltaframe_s *frame = SVFTE_BeginFrame (client);
			SV_WriteDamageToMessage (client->edict, &msg);
			if (!(client->protocol_pext2 & PEXT2_PREDINFO))
				SV_WriteClientdataToMessage (client, &msg);
			else
				SVFTE_WriteStats (client, &msg, frame);
			if (client->protocol_qsvr == QSVR_PROTOCOL_PINNED &&
				SV_PrivateWalkTrialSelected (client) &&
				!SVFTE_WritePrivateMoveStats (client, &msg))
			{
				Con_Printf ("%s: dropping selected private WALK client: movement stats could not be built or fit in the datagram\n", client->name);
				SV_DropClient (false);
				return false;
			}
			if (!SVFTE_WriteEntitiesToClient (client, &msg, sizeof (buf), false, frame))
			{
				Con_Printf ("%s: dropping selected private WALK client: mandatory owner snapshot cannot fit\n", client->name);
				SV_DropClient (false);
				return false;
			}

			if (!SVFTE_WriteCSQCEntitiesToClient (client, &msg, frame, false))
			{
				if (SV_ClientConnectionMatches (client, socket))
					SV_DropClient (client->edict->free); // Dead recipients cannot enter QC disconnect.
				return false;
			}

			// this delta protocol doesn't wipe old state just because there's a new packet.
			// the server isn't required to sync with the client frames either
			// so we can just spam multiple packets to keep our udp data under the MTU
			while (client->snapshotresume < client->numpendingentities ||
				client->csqcsnapshotresume < client->numpendingcsqcentities)
			{
				size_t oldnative = client->snapshotresume;
				size_t oldcustom = client->csqcsnapshotresume;
				if (NET_SendUnreliableMessage (client->netconnection, &msg) == -1)
				{
					SV_DropClient (false);
					return false;
				}
				SZ_Clear (&msg);
				frame = SVFTE_BeginFrame (client);
				if (client->protocol_qsvr == QSVR_PROTOCOL_PINNED &&
					SV_PrivateWalkTrialSelected (client) &&
					!SVFTE_WritePrivateMoveStats (client, &msg))
				{
					Con_Printf ("%s: dropping selected private WALK client: movement stats could not be built or fit in the datagram\n", client->name);
					SV_DropClient (false);
					return false;
				}
				if (!SVFTE_WriteEntitiesToClient (client, &msg, sizeof (buf), true, frame))
				{
					Con_Printf ("%s: dropping selected private WALK client: mandatory owner snapshot cannot fit or optional update cannot advance\n", client->name);
					SV_DropClient (false);
					return false;
				}
				if (!SVFTE_WriteCSQCEntitiesToClient (client, &msg, frame, true))
				{
					if (SV_ClientConnectionMatches (client, socket))
						SV_DropClient (client->edict->free);
					return false;
				}
				if (client->snapshotresume == oldnative && client->csqcsnapshotresume == oldcustom)
				{
					Con_Printf ("%s: entity continuation cannot advance within datagram budget\n", client->name);
					SV_DropClient (false);
					return false;
				}
			}
		}
		else
		{
			MSG_WriteByte (&msg, svc_time);
			MSG_WriteFloat (&msg, qcvm->time);
			if (client->protocol_qsvr != QSVR_PROTOCOL_PINNED &&
				(client->protocol_pext2 & PEXT2_PREDINFO))
				MSG_WriteShort (&msg, (client->lastmovemessage & 0xffff));

			if (!SV_WriteEntitiesToClient (client, &msg, sizeof (buf)))
			{
				if (SV_ClientConnectionMatches (client, socket))
					SV_DropClient (true);
				return false;
			}
		}

		// copy the private datagram if there is space
		if (client->datagram.cursize && !client->datagram.overflowed)
		{
			if (msg.cursize + client->datagram.cursize < msg.maxsize)
				SZ_Write (&msg, client->datagram.data, client->datagram.cursize);
			else if (client->datagram.cursize < msg.maxsize)
			{
				// send private datagram in another packet
				NET_SendUnreliableMessage (client->netconnection, &msg);
				SZ_Clear (&msg);
				SZ_Write (&msg, client->datagram.data, client->datagram.cursize);
			}
		}
		SZ_Clear (&client->datagram);

		// copy the server datagram if there is space
		if (msg.cursize + sv.datagram.cursize < msg.maxsize)
			SZ_Write (&msg, sv.datagram.data, sv.datagram.cursize);
		else if (sv.datagram.cursize)
		{
			// if the server datagram starts with particles, split them across multiple packets
			int position = 0;
			int size;
			while (sv.datagram.cursize > position && (size = SV_ParticleSize (&sv.datagram.data[position])))
			{
				if (msg.cursize + size < msg.maxsize)
				{
					SZ_Write (&msg, &sv.datagram.data[position], size);
					position += size;
				}
				else
				{
					NET_SendUnreliableMessage (client->netconnection, &msg);
					SZ_Clear (&msg);
				}
			}
			int remaining = sv.datagram.cursize - position;
			if (msg.cursize + remaining < msg.maxsize)
				SZ_Write (&msg, &sv.datagram.data[position], remaining);
			else if (remaining < msg.maxsize)
			{
				NET_SendUnreliableMessage (client->netconnection, &msg);
				SZ_Clear (&msg);
				SZ_Write (&msg, &sv.datagram.data[position], remaining);
			}
		}

		if (!(client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
		{
			// add the client specific data to the datagram last to play nice with clients which reset onground on every packet
			// (and to leave a few more bytes for entity updates)
			// cannibalize client->datagram (cleared above) to get an exact size
			SV_WriteDamageToMessage (client->edict, &client->datagram);
			SV_WriteClientdataToMessage (client, &client->datagram);
			if (msg.cursize + client->datagram.cursize > msg.maxsize)
			{
				NET_SendUnreliableMessage (client->netconnection, &msg);
				SZ_Clear (&msg);
			}
			SZ_Write (&msg, client->datagram.data, client->datagram.cursize);
			SZ_Clear (&client->datagram);
		}

		/* Keep the ordinary snapshot intact.  If it used the available packet
		 * budget, the VRIK relay starts in a separate unreliable packet. */
		if (client->spawned && !SV_AppendPendingVRIK (client, &msg))
			return false;
	}

	// send the datagram
	if (msg.cursize && NET_SendUnreliableMessage (client->netconnection, &msg) == -1)
	{
		SV_DropClient (false); // if the message couldn't send, kick off
		return false;
	}
	if (!SV_SendPendingVoice (client))
		return false;

	return true;
}

/*
=======================
SV_WriteUnderwaterOverride
=======================
*/
static void SV_WriteUnderwaterOverride (client_t *client)
{
	if (!client->edict->sendforcewater)
		return;
	client->edict->sendforcewater = false;
	MSG_WriteByte (&client->message, svc_stufftext);
	MSG_WriteString (&client->message, va ("//v_water %i\n", client->edict->forcewater));
}

/*
=======================
SV_UpdateToReliableMessages
=======================
*/
void SV_UpdateToReliableMessages (void)
{
	int		  i, j;
	client_t *client;

	// check for changes to be sent over the reliable streams
	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
	{
		if (host_client->old_frags != host_client->edict->v.frags)
		{
			for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
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

	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (!client->active)
			continue;
		SV_WriteUnderwaterOverride (client);
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
	sizebuf_t msg;
	byte	  buf[4];

	msg.data = buf;
	msg.maxsize = sizeof (buf);
	msg.cursize = 0;

	MSG_WriteChar (&msg, svc_nop);

	if (NET_SendUnreliableMessage (client->netconnection, &msg) == -1)
		SV_DropClient (false); // if the message couldn't send, kick off
	client->last_message = realtime;
}

qboolean SV_SendPrespawnModelPrecaches (void)
{
	return false;
}
qboolean SV_SendPrespawnSoundPrecaches (void)
{
	unsigned int idx = host_client->signon_sounds;
	size_t		 maxsize = host_client->message.maxsize; // we can go quite large
	if (!host_client->protocol_pext2)
		return false; // unsupported by this client...
	for (; idx < host_client->limit_sounds; idx++)
	{
		if (!sv.sound_precache[idx])
			continue;
		if ((size_t)host_client->message.cursize + 4 + strlen (sv.sound_precache[idx]) > maxsize)
			break;
		MSG_WriteByte (&host_client->message, svcdp_precache);
		MSG_WriteShort (&host_client->message, 0x8000 | idx);
		MSG_WriteString (&host_client->message, sv.sound_precache[idx]);
	}
	host_client->signon_sounds = idx;
	return idx < host_client->limit_sounds;
}
int SV_SendPrespawnParticlePrecaches (int idx)
{
	size_t maxsize = host_client->message.maxsize; // we can go quite large
	if (!host_client->protocol_pext2)
		return -1; // unsupported by this client.
	for (;; idx++)
	{
		if (idx == MAX_PARTICLETYPES)
			return -1;
		if (!sv.particle_precache[idx])
			continue;
		if (host_client->message.cursize + 4 + strlen (sv.particle_precache[idx]) > maxsize)
			break;
		MSG_WriteByte (&host_client->message, svcdp_precache);
		MSG_WriteShort (&host_client->message, 0x4000 | idx);
		MSG_WriteString (&host_client->message, sv.particle_precache[idx]);
	}
	return idx;
}
int SV_SendPrespawnStatics (int idx)
{
	entity_state_t *svent;
	int				maxsize = host_client->message.maxsize - 128; // we can go quite large

	while (1)
	{
		if (idx >= sv.num_statics)
			return -1;
		svent = &sv.static_entities[idx];

		if (host_client->message.cursize > maxsize)
			break;
		idx++;

		if (svent->modelindex >= host_client->limit_models)
			continue;
		if (memcmp (&nullentitystate, svent, sizeof (nullentitystate)))
			MSG_WriteStaticOrBaseLine (&host_client->message, -1, svent, host_client->protocol_pext2, sv.protocol, sv.protocolflags);
	}
	return idx;
}
int SV_SendAmbientSounds (int idx)
{
	struct ambientsound_s *snd;
	int					   maxsize = host_client->message.maxsize - 128; // we can go quite large
	qboolean			   large;
	size_t				   i;

	while (1)
	{
		if (idx >= sv.num_ambients)
			return -1;
		snd = &sv.ambientsounds[idx];

		if (host_client->message.cursize > maxsize)
			break;
		idx++;

		if (snd->soundindex >= host_client->limit_sounds)
			continue;

		large = (snd->soundindex > 255);
		if (large)
			MSG_WriteByte (&host_client->message, svc_spawnstaticsound2); // johnfitz -- PROTOCOL_FITZQUAKE
		else
			MSG_WriteByte (&host_client->message, svc_spawnstaticsound);
		for (i = 0; i < 3; i++)
			MSG_WriteCoord (&host_client->message, snd->origin[i], sv.protocolflags);
		if (large)
			MSG_WriteShort (&host_client->message, snd->soundindex);
		else
			MSG_WriteByte (&host_client->message, snd->soundindex);
		MSG_WriteByte (&host_client->message, (int)CLAMP (0.f, snd->volume * 255.f, 255.f));
		MSG_WriteByte (&host_client->message, (int)CLAMP (0.f, snd->attenuation * 64.f, 255.f));
	}
	return idx;
}
int SV_SendPrespawnBaselines (int idx)
{
	edict_t *svent;
	int		 maxsize = host_client->message.maxsize - 128; // we can go quite large

	while (1)
	{
		if (idx >= qcvm->num_edicts)
			return -1;
		svent = EDICT_NUM (idx);

		if (host_client->message.cursize > maxsize)
			break;

		if (memcmp (&nullentitystate, &svent->baseline, sizeof (nullentitystate)))
			MSG_WriteStaticOrBaseLine (&host_client->message, idx, &svent->baseline, host_client->protocol_pext2, sv.protocol, sv.protocolflags);

		idx++;
	}
	return idx;
}

/*
=======================
SV_SendClientMessages
=======================
*/
static void SV_AppendWeaponContactProtocol (client_t *client)
{
	unsigned int mode;
	unsigned int profile;
	char command[64];
	int command_length;
	size_t required;
	int previous_size;

	if (!client || !client->active || !client->netconnection ||
		!client->spawned || client->protocol_qsvr != QSVR_PROTOCOL_PINNED)
		return;

	mode = SV_VRWeaponCollisionEnabled () ?
		VR_WEAPON_CONTACT_CAP_COLLISION : 0u;
	profile = VR_WEAPON_CONTACT_PROFILE_NONE;
	if (SV_VRStockAxeMeleeEnabled ())
	{
		mode |= VR_WEAPON_CONTACT_CAP_MELEE;
		profile = SV_VRStockAxeContactProfile ();
	}
	else if (SV_VRDwellBerserkMeleeEnabled ())
	{
		mode |= VR_WEAPON_CONTACT_CAP_MELEE;
		profile = VR_WEAPON_CONTACT_PROFILE_DWELL;
	}
	else if (SV_VRQBJ3MeleeEnabled ())
	{
		mode |= VR_WEAPON_CONTACT_CAP_MELEE;
		profile = VR_WEAPON_CONTACT_PROFILE_QBJ3;
	}
	else if (SV_VREnyoMeleeEnabled ())
	{
		mode |= VR_WEAPON_CONTACT_CAP_MELEE;
		profile = VR_WEAPON_CONTACT_PROFILE_ENYO;
	}
	if ((client->weapon_contact_last_mode == (int)mode &&
		client->weapon_contact_last_profile == (int)profile) ||
		client->message.overflowed || client->message.cursize < 0 ||
		client->message.maxsize <= 0 ||
		client->message.cursize > client->message.maxsize)
		return;

	command_length = q_snprintf (command, sizeof (command),
		"//vr_weapon_contact_protocol %u %u %u\n",
		VR_WEAPON_CONTACT_PROTOCOL_VERSION, mode, profile);
	if (command_length <= 0 || (size_t)command_length >= sizeof (command))
		return;

	required = (size_t)command_length + 2; /* svc_stufftext and NUL */
	if (required > (size_t)(client->message.maxsize - client->message.cursize))
		return;

	previous_size = client->message.cursize;
	MSG_WriteByte (&client->message, svc_stufftext);
	MSG_WriteString (&client->message, command);
	if (!client->message.overflowed &&
		client->message.cursize == previous_size + (int)required)
	{
		client->weapon_contact_last_mode = (int)mode;
		client->weapon_contact_last_profile = (int)profile;
	}
}

/* Offer only paired recipes backed by exact loaded QC program adapters. */
static void SV_AppendAkimboProtocol (client_t *client)
{
	enum
	{
		AKIMBO_OFFER_TWIN = 1u << 0,
		AKIMBO_OFFER_QBJ3_BERSERK = 1u << 1,
		AKIMBO_OFFER_ENYO = 1u << 2,
		AKIMBO_OFFER_DWELL = 1u << 3,
		AKIMBO_OFFER_MASK = (1u << 4) - 1u
	};
	char command[64];
	int command_length, previous_size;
	unsigned int offer_mask;
	size_t required;

	if (!client || !client->active || !client->netconnection ||
		!client->spawned || client->protocol_qsvr != QSVR_PROTOCOL_PINNED)
		return;

	/* Bits follow the four client command arguments. QBJ3's native paired
	 * animation remains available even if immersive contact is disabled. */
	offer_mask = SV_QBJ3TwinNailgunProgramLoaded () ?
		AKIMBO_OFFER_TWIN : 0;
	if (SV_VRQBJ3MeleeContactProfile () == VR_WEAPON_CONTACT_PROFILE_QBJ3)
		offer_mask |= AKIMBO_OFFER_QBJ3_BERSERK;
	if (SV_EnyoAkimboProgramLoaded ())
		offer_mask |= AKIMBO_OFFER_ENYO;
	if (SV_VRDwellBerserkMeleeEnabled ())
		offer_mask |= AKIMBO_OFFER_DWELL;
	offer_mask &= AKIMBO_OFFER_MASK;
	if (client->akimbo_last_advertised_mask == (signed char)offer_mask ||
		client->message.overflowed || client->message.cursize < 0 ||
		client->message.maxsize <= 0 ||
		client->message.cursize > client->message.maxsize)
		return;

	command_length = q_snprintf (command, sizeof (command),
		"//vr_qbj3_akimbo_protocol %u %u %u %u\n",
		(offer_mask >> 0) & 1u, (offer_mask >> 1) & 1u,
		(offer_mask >> 2) & 1u, (offer_mask >> 3) & 1u);
	if (command_length <= 0 || (size_t)command_length >= sizeof (command))
		return;
	required = (size_t)command_length + 2;
	if (required > (size_t)(client->message.maxsize - client->message.cursize))
		return;

	previous_size = client->message.cursize;
	MSG_WriteByte (&client->message, svc_stufftext);
	MSG_WriteString (&client->message, command);
	if (!client->message.overflowed &&
		client->message.cursize == previous_size + (int)required)
		client->akimbo_last_advertised_mask = (signed char)offer_mask;
}

static void SV_AppendGorillaProtocol (client_t *client)
{
	int enabled;
	char command[64];
	int command_length;
	size_t required;
	int previous_size;

	if (!client || !client->active || !client->netconnection ||
		!client->spawned || client->protocol_qsvr != QSVR_PROTOCOL_PINNED)
		return;

	/* The default owner consumes raw hands in the native frame; the opt-in
	 * stock WALK trial consumes them through its command-time PMove owner. */
	enabled = sv_gorilla.value != 0.0f;
	if (client->vr_gorilla_last_advertised == enabled ||
		client->message.overflowed || client->message.cursize < 0 ||
		client->message.maxsize <= 0 ||
		client->message.cursize > client->message.maxsize)
		return;

	command_length = q_snprintf (command, sizeof (command),
		"//vr_gorilla_protocol 1 %d\n", enabled);
	if (command_length <= 0 || (size_t)command_length >= sizeof (command))
		return;

	required = (size_t)command_length + 2; /* svc_stufftext and NUL */
	if (required > (size_t)(client->message.maxsize - client->message.cursize))
		return;

	previous_size = client->message.cursize;
	MSG_WriteByte (&client->message, svc_stufftext);
	MSG_WriteString (&client->message, command);
	if (!client->message.overflowed &&
		client->message.cursize == previous_size + (int)required)
		client->vr_gorilla_last_advertised = enabled;
}

static void SV_AppendInstantStopProtocol (client_t *client)
{
	static const char command[] = "//vr_instant_stop_protocol 1\n";
	const size_t required = 1 + sizeof (command);
	int previous_size;

	if (!client || !client->active || !client->netconnection ||
		!client->spawned || client->protocol_qsvr != QSVR_PROTOCOL_PINNED ||
		!SV_PrivateWalkTrialSelected (client) || client->vr_instant_stop_offered ||
		client->message.overflowed || client->message.cursize < 0 ||
		client->message.maxsize <= 0 ||
		client->message.cursize > client->message.maxsize ||
		required > (size_t)(client->message.maxsize - client->message.cursize))
		return;

	previous_size = client->message.cursize;
	MSG_WriteByte (&client->message, svc_stufftext);
	MSG_WriteString (&client->message, command);
	if (!client->message.overflowed &&
		client->message.cursize == previous_size + (int)required)
		client->vr_instant_stop_offered = true;
}

typedef struct
{
	char *text;
	size_t size;
	qboolean userinfo, failed;
} sv_metadata_projection_t;

typedef struct
{
	sizebuf_t *unit;
	char *store;
	size_t store_size, token_limit, text_limit;
	int slot;
	qboolean userinfo, failed;
} sv_metadata_increment_t;

static qboolean SV_MetadataClient (const client_t *client)
{
	return client && client->active &&
		(client->offered_metadata == QSVR_METADATA_VERSION ||
		 (client->protocol_pext2 & PEXT2_PREDINFO) != 0);
}

static unsigned int SV_MetadataSlotMask (void)
{
	int count = q_min (svs.maxclients, MAX_SCOREBOARD);
	return count >= 32 ? ~0u : count > 0 ? (1u << count) - 1 : 0;
}

void SV_MetadataRearmClient (client_t *client)
{
	if (!client)
		return;
	client->metadata_serverinfo_pending = SV_MetadataClient (client);
	client->metadata_userinfo_dirty = client->metadata_serverinfo_pending ?
		SV_MetadataSlotMask () : 0;
}

void SV_MetadataServerinfoChanged (void)
{
	for (int i = 0; i < svs.maxclients; ++i)
		if (SV_MetadataClient (&svs.clients[i]))
			svs.clients[i].metadata_serverinfo_pending = true;
}

void SV_MetadataUserinfoChanged (int slot)
{
	if (slot < 0 || slot >= svs.maxclients || slot >= MAX_SCOREBOARD)
		return;
	for (int i = 0; i < svs.maxclients; ++i)
		if (SV_MetadataClient (&svs.clients[i]))
			svs.clients[i].metadata_userinfo_dirty |= 1u << slot;
}

void SV_MetadataRetireSlot (int slot)
{
	if (slot < 0 || slot >= svs.maxclients || slot >= MAX_SCOREBOARD)
		return;
	svs.clients[slot].userinfo[0] = 0;
	svs.clients[slot].name[0] = 0;
	svs.clients[slot].colors = 0;
	SV_MetadataUserinfoChanged (slot);
}

static void SV_MetadataProjectField (void *opaque, const char *key,
	const char *value)
{
	sv_metadata_projection_t *projection = opaque;
	size_t keylen = strlen (key), vallen = strlen (value);
	if (key[0] == '_' || strchr (key, '"') || strchr (value, '"') ||
		(projection->userinfo && (!strcmp (key, "name") ||
		 !strcmp (key, "topcolor") || !strcmp (key, "bottomcolor"))))
		return;
	if (keylen + vallen + 2 >= projection->size - strlen (projection->text))
	{
		projection->failed = true;
		return;
	}
	char *tail = projection->text + strlen (projection->text);
	*tail++ = '\\';
	memcpy (tail, key, keylen); tail += keylen;
	*tail++ = '\\';
	memcpy (tail, value, vallen + 1);
}

static qboolean SV_MetadataStoreKey (char *store, size_t store_size,
	const char *key, const char *value)
{
	char actual[SERVER_INFO_STRING_SIZE];
	Info_SetKey (store, store_size, key, value);
	Info_GetKey (store, key, actual, sizeof (actual));
	return !strcmp (actual, value);
}

static qboolean SV_MetadataOverlayUserinfo (char *store, const char *name,
	int colors)
{
	char top[4], bottom[4];
	q_snprintf (top, sizeof (top), "%u", ((unsigned int)colors >> 4) & 15);
	q_snprintf (bottom, sizeof (bottom), "%u", (unsigned int)colors & 15);
	return SV_MetadataStoreKey (store, CLIENT_USER_INFO_STRING_SIZE, "name", name) &&
		SV_MetadataStoreKey (store, CLIENT_USER_INFO_STRING_SIZE, "topcolor", top) &&
		SV_MetadataStoreKey (store, CLIENT_USER_INFO_STRING_SIZE, "bottomcolor", bottom);
}

static qboolean SV_MetadataWriteCommand (sizebuf_t *unit, const char *command,
	size_t text_limit)
{
	size_t length = strlen (command), required = length + 2;
	if (length > text_limit || unit->cursize < 0 || unit->maxsize < unit->cursize ||
		required > (size_t)(unit->maxsize - unit->cursize))
		return false;
	MSG_WriteByte (unit, svc_stufftext);
	MSG_WriteString (unit, command);
	return !unit->overflowed;
}

static qboolean SV_MetadataWriteUserinfoCompanions (sizebuf_t *unit, int slot,
	const char *name, int colors)
{
	size_t required = strlen (name) + 6;
	if (unit->cursize < 0 || unit->maxsize < unit->cursize ||
		required > (size_t)(unit->maxsize - unit->cursize))
		return false;
	MSG_WriteByte (unit, svc_updatename);
	MSG_WriteByte (unit, slot);
	SZ_Write (unit, name, (int)strlen (name) + 1);
	MSG_WriteByte (unit, svc_updatecolors);
	MSG_WriteByte (unit, slot);
	MSG_WriteByte (unit, colors);
	return !unit->overflowed;
}

static qboolean SV_MetadataWriteUserinfo (sizebuf_t *unit, int slot,
	const char *userinfo, const char *name, int colors, size_t text_limit)
{
	char command[NET_MAXMESSAGE];
	int length = q_snprintf (command, sizeof (command), "//fui %i \"%s\"\n",
		slot, userinfo);
	if (length < 0 || length >= (int)sizeof (command) ||
		(size_t)length > text_limit || unit->cursize < 0 ||
		unit->maxsize < unit->cursize ||
		(size_t)length + 2 + strlen (name) + 6 >
		(size_t)(unit->maxsize - unit->cursize))
		return false;
	return SV_MetadataWriteCommand (unit, command, text_limit) &&
		SV_MetadataWriteUserinfoCompanions (unit, slot, name, colors);
}

static void SV_MetadataIncrementField (void *opaque, const char *key,
	const char *value)
{
	sv_metadata_increment_t *increment = opaque;
	char command[NET_MAXMESSAGE];
	int length;
	if (increment->failed || key[0] == '_' || strchr (key, '"') ||
		strchr (value, '"') || strlen (key) > increment->token_limit ||
		strlen (value) > increment->token_limit)
	{
		if (key[0] != '_' && !strchr (key, '"') && !strchr (value, '"'))
			increment->failed = true;
		return;
	}
	length = increment->userinfo ?
		q_snprintf (command, sizeof (command), "//ui %i \"%s\" \"%s\"\n",
			increment->slot, key, value) :
		q_snprintf (command, sizeof (command), "//svi \"%s\" \"%s\"\n", key, value);
	if (length < 0 || length >= (int)sizeof (command) ||
		!SV_MetadataWriteCommand (increment->unit, command, increment->text_limit))
	{
		increment->failed = true;
		return;
	}
	if (!SV_MetadataStoreKey (increment->store, increment->store_size, key, value))
		increment->failed = true;
}

static int SV_MetadataEnvelope (const client_t *client, size_t *token,
	size_t *full_text, size_t *increment_text)
{
	qboolean known = client->offered_metadata == QSVR_METADATA_VERSION;
	*token = known ? SERVER_INFO_STRING_SIZE - 1 : 1023;
	*full_text = known ? 8211 : 2046;
	*increment_text = known ? 2047 : 2046;
	return known;
}

static qboolean SV_MetadataServerUnit (client_t *client, sizebuf_t *unit,
	char *reason, size_t reason_size)
{
	char projected[SERVER_INFO_STRING_SIZE], simulated[SERVER_INFO_STRING_SIZE];
	char command[NET_MAXMESSAGE];
	size_t token, full_text, increment_text;
	sv_metadata_projection_t projection = { projected, sizeof (projected), false, false };
	sv_metadata_increment_t increment;
	int length;
	SV_MetadataEnvelope (client, &token, &full_text, &increment_text);
	projected[0] = 0;
	Info_Enumerate (svs.serverinfo, SV_MetadataProjectField, &projection);
	unit->allowoverflow = false; unit->overflowed = false;
	length = q_snprintf (command, sizeof (command), "//fullserverinfo \"%s\"\n", projected);
	if (!projection.failed && strlen (projected) <= token &&
		q_strlcpy (simulated, projected, sizeof (simulated)) == strlen (projected) &&
		!strcmp (simulated, projected) && length >= 0 &&
		length < (int)sizeof (command) && (size_t)length <= full_text &&
		SV_MetadataWriteCommand (unit, command, full_text))
		return true;
	if (projection.failed)
		goto impossible;
	SZ_Clear (unit);
	if (!SV_MetadataWriteCommand (unit, "//fullserverinfo \"\"\n", full_text))
		goto impossible;
	memset (&increment, 0, sizeof (increment));
	increment.unit = unit; increment.store = simulated; simulated[0] = 0;
	increment.store_size = SERVER_INFO_STRING_SIZE; increment.token_limit = 1023;
	increment.text_limit = increment_text;
	Info_Enumerate (projected, SV_MetadataIncrementField, &increment);
	if (!increment.failed && !strcmp (simulated, projected))
		return true;
impossible:
	q_strlcpy (reason, "serverinfo cannot fit a complete metadata envelope", reason_size);
	return false;
}

static qboolean SV_MetadataUserinfoUnit (client_t *recipient, int slot,
	client_t *source, sizebuf_t *unit, char *reason, size_t reason_size)
{
	char projected[CLIENT_USER_INFO_STRING_SIZE], wire[CLIENT_USER_INFO_STRING_SIZE];
	char canonical[CLIENT_USER_INFO_STRING_SIZE];
	char expected[CLIENT_USER_INFO_STRING_SIZE];
	char command[NET_MAXMESSAGE];
	char *source_info = source && source->active ? source->userinfo : "";
	const char *name = source && source->active ? source->name : "";
	int colors = source && source->active ? source->colors : 0;
	size_t token, full_text, increment_text;
	sv_metadata_projection_t projection = { projected, sizeof (projected), true, false };
	sv_metadata_projection_t wire_projection = { wire, sizeof (wire), false, false };
	sv_metadata_increment_t increment;
	int length;
	qboolean expected_valid;
	SV_MetadataEnvelope (recipient, &token, &full_text, &increment_text);
	projected[0] = 0;
	Info_Enumerate (source_info, SV_MetadataProjectField, &projection);
	expected_valid = !projection.failed &&
		q_strlcpy (expected, projected, sizeof (expected)) == strlen (projected) &&
		SV_MetadataOverlayUserinfo (expected, name, colors);
	wire[0] = 0;
	if (expected_valid)
		Info_Enumerate (expected, SV_MetadataProjectField, &wire_projection);
	unit->allowoverflow = false; unit->overflowed = false;
	length = q_snprintf (command, sizeof (command), "//fui %i \"%s\"\n", slot, wire);
	if (!projection.failed && !wire_projection.failed && expected_valid &&
		strlen (wire) <= token && length >= 0 &&
		length < (int)sizeof (command) && (size_t)length <= full_text &&
		q_strlcpy (canonical, wire, sizeof (canonical)) == strlen (wire) &&
		!strcmp (canonical, wire) &&
		SV_MetadataOverlayUserinfo (canonical, name, colors) &&
		!strcmp (canonical, expected) &&
		SV_MetadataWriteUserinfo (unit, slot, wire, name, colors, full_text))
		return true;
	if (projection.failed || wire_projection.failed || !expected_valid)
		goto impossible;
	SZ_Clear (unit);
	length = q_snprintf (command, sizeof (command), "//fui %i \"\"\n", slot);
	if (length < 0 || length >= (int)sizeof (command) ||
		!SV_MetadataWriteCommand (unit, command, full_text))
		goto impossible;
	memset (&increment, 0, sizeof (increment));
	increment.unit = unit; increment.store = canonical; canonical[0] = 0;
	increment.store_size = sizeof (canonical); increment.token_limit = 1023;
	increment.text_limit = increment_text; increment.slot = slot; increment.userinfo = true;
	Info_Enumerate (wire, SV_MetadataIncrementField, &increment);
	if (increment.failed || !SV_MetadataOverlayUserinfo (canonical, name, colors) ||
		strcmp (canonical, expected))
		goto impossible;
	if (!SV_MetadataWriteUserinfoCompanions (unit, slot, name, colors))
		goto impossible;
	return true;
impossible:
	q_snprintf (reason, reason_size, "userinfo slot %i cannot fit a complete metadata envelope", slot);
	return false;
}

/* Returns 1 when drained, 0 under ordinary message pressure, -1 if impossible. */
static int SV_MetadataDrain (client_t *client, qboolean include_userinfo,
	char *reason, size_t reason_size)
{
	byte bytes[NET_MAXMESSAGE];
	sizebuf_t unit = { false, false, bytes, sizeof (bytes), 0 };
	if (!SV_MetadataClient (client))
		return 1;
	for (int kind = 0; kind <= (include_userinfo ? MAX_SCOREBOARD : 0); ++kind)
	{
		int slot = kind - 1, built, required;
		if (kind == 0 ? !client->metadata_serverinfo_pending :
			!(client->metadata_userinfo_dirty & (1u << slot)))
			continue;
		SZ_Clear (&unit);
		built = kind == 0 ? SV_MetadataServerUnit (client, &unit, reason, reason_size) :
			SV_MetadataUserinfoUnit (client, slot, &svs.clients[slot], &unit, reason, reason_size);
		if (!built)
			return -1;
		required = unit.cursize;
		if (client->message.overflowed || client->message.cursize < 0 ||
			client->message.maxsize < required ||
			client->message.cursize > client->message.maxsize)
		{
			q_strlcpy (reason, "recipient reliable limit is smaller than metadata unit", reason_size);
			return -1;
		}
		if (required > client->message.maxsize - client->message.cursize)
			return 0;
		SZ_Write (&client->message, unit.data, required);
		if (kind == 0)
			client->metadata_serverinfo_pending = false;
		else
			client->metadata_userinfo_dirty &= ~(1u << slot);
	}
	return 1;
}

/* One remote native chunk per reliable-message lifetime. Metadata shares the
 * receiver budget; an exact-fit chunk and the final marker are admitted separately. */
static int SV_StageSignonMessage (client_t *client, char *reason, size_t reason_size)
{
	const qboolean local = SV_IsLocalClient (client);
	const int capacity = client->message.maxsize;
	const int limit = local ? capacity : q_min (capacity, 32000);
	int result;
	client->message.maxsize = limit;
	result = SV_MetadataDrain (client, false, reason, reason_size);
	client->message.maxsize = capacity;
	if (result <= 0)
		return result;
	if (limit < 2 || client->message.cursize < 0 || client->message.cursize > limit)
		goto impossible;
	while (client->signonidx < sv.num_signon_buffers)
	{
		const sizebuf_t *chunk = sv.signon_buffers[client->signonidx];
		if (!chunk || chunk->cursize < 0 || chunk->cursize > limit)
			goto impossible;
		if ((!local && client->signon_chunk_pending) || chunk->cursize > limit - client->message.cursize)
			return 0;
		SZ_Write (&client->message, chunk->data, chunk->cursize);
		++client->signonidx;
		if (!local)
		{
			client->signon_message_capacity = capacity;
			client->signon_chunk_pending = true;
			// Every later writer, including blocked-frame reliable updates, shares this envelope.
			client->message.maxsize = limit;
			break;
		}
	}
	if (client->signonidx < sv.num_signon_buffers || limit - client->message.cursize < 2)
		return 0;
	MSG_WriteByte (&client->message, svc_signonnum);
	MSG_WriteByte (&client->message, 2);
	client->sendsignon = PRESPAWN_FLUSH;
	return 1;
impossible:
	q_strlcpy (reason, "native signon exceeds the recipient reliable envelope", reason_size);
	return -1;
}

void SV_SendClientMessages (void)
{
	int i;

	SV_ExpireVRIKPoses ();
	SV_ClearDroppedAvatarSlots ();

	// update frags, names, etc
	SV_UpdateToReliableMessages ();

	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
	{
		if (!host_client->active)
			continue;

		client_t *client = host_client;
		struct qsocket_s *socket = client->netconnection;
		if (!SV_PresendClientDatagram (client) && SV_ClientConnectionMatches (client, socket))
			SV_DropClient (true);
	}

	if (GetEdictFieldValid (SendFlags))
		for (int e = 1; e < qcvm->num_edicts; ++e)
			GetEdictFieldEval (EDICT_NUM (e), SendFlags)->_float = 0;

	// build individual updates
	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
	{
		if (!host_client->active)
			continue;

		if (!SV_SendClientDatagram (host_client))
			continue;
		SV_AppendWeaponContactProtocol (host_client);
		SV_AppendAkimboProtocol (host_client);
		SV_AppendGorillaProtocol (host_client);
		SV_AppendInstantStopProtocol (host_client);
		if (host_client->spawned)
		{
			char reason[128] = "metadata envelope cannot be published";
			if (SV_MetadataDrain (host_client, true, reason, sizeof (reason)) < 0)
			{
				Con_Warning ("Disconnecting %s: metadata publication failed: %s\n",
					host_client->name, reason);
				SZ_Clear (&host_client->message);
				SV_DropClient (false);
				continue;
			}
		}
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
				continue; // don't send out non-signon messages
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
			if (host_client->sendsignon == PRESPAWN_SIGNONMSG)
			{
				char reason[128] = "signon envelope cannot be published";
				if (SV_StageSignonMessage (host_client, reason, sizeof (reason)) < 0)
				{
					Con_Warning ("Disconnecting %s: signon publication failed: %s\n", host_client->name, reason);
					SZ_Clear (&host_client->message);
					SV_DropClient (false);
					continue;
				}
			}
			if (host_client->sendsignon == PRESPAWN_SPAWN_METADATA)
			{
				char reason[128] = "metadata envelope cannot be published";
				int drained = SV_MetadataDrain (host_client, true, reason, sizeof (reason));
				if (drained < 0)
				{
					Con_Warning ("Disconnecting %s: metadata publication failed: %s\n",
						host_client->name, reason);
					SZ_Clear (&host_client->message);
					SV_DropClient (false);
					continue;
				}
				if (drained > 0 && host_client->message.maxsize - host_client->message.cursize >= 2)
				{
					MSG_WriteByte (&host_client->message, svc_signonnum);
					MSG_WriteByte (&host_client->message, 3);
					host_client->sendsignon = PRESPAWN_FLUSH;
				}
			}
		}

		// check for an overflowed message.  Should only happen
		// on a very fucked up connection that backs up a lot, then
		// changes level
		if (host_client->message.overflowed)
		{
			if (host_client->signon_chunk_pending)
				Con_Warning ("Disconnecting %s: native signon exceeds the recipient reliable envelope\n", host_client->name);
			SZ_Clear (&host_client->message);
			SV_DropClient (false);
			continue;
		}
		SV_AppendAvatarOffers (host_client);
		SV_FlushAvatarSlots (host_client);

		// Check at the transport boundary even after PRESPAWN_FLUSH and blocked frames.
		if (host_client->signon_chunk_pending &&
			(host_client->message.overflowed || host_client->message.cursize < 0 ||
			 host_client->message.cursize > q_min (host_client->message.maxsize, 32000)))
		{
			Con_Warning ("Disconnecting %s: native signon exceeds the recipient reliable envelope\n", host_client->name);
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
				SV_DropClient (false); // went to another level
			else
			{
				const int sent = NET_SendMessage (host_client->netconnection, &host_client->message);
				if (sent < 0)
				{
					SV_DropClient (false);
					continue;
				}
				if (!sent)
					continue; // Keep the chunk flag and bytes until transport accepts them.
				SZ_Clear (&host_client->message);
				if (host_client->signon_chunk_pending)
					host_client->message.maxsize = host_client->signon_message_capacity;
				host_client->signon_chunk_pending = false;
				host_client->last_message = realtime;
				if (host_client->sendsignon == PRESPAWN_FLUSH)
					host_client->sendsignon = PRESPAWN_DONE;
			}
		}
	}

	// clear muzzle flashes
	SV_CleanupEnts ();
}

/*
==============================================================================

SERVER SPAWNING

==============================================================================
*/

#define SIGNON_SIZE 31500 // QS has a MAX_DATAGRAM of 32000, try to play nice

/*
================
SV_AddSignonBuffer
================
*/
static void SV_AddSignonBuffer (void)
{
	sizebuf_t *sb;

	if (sv.num_signon_buffers >= MAX_SIGNON_BUFFERS)
		Host_Error ("SV_AddSignonBuffer overflow");

	sb = (sizebuf_t *)Mem_Alloc (sizeof (sizebuf_t) + SIGNON_SIZE);
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
	if (numbytes < 0 || numbytes > SIGNON_SIZE)
		Host_Error ("Signon unit exceeds native chunk limit (%i bytes)", numbytes);
	if (!sv.signon || sv.signon->cursize + numbytes > sv.signon->maxsize)
		SV_AddSignonBuffer ();
}

/*
================
SV_ModelIndex

================
*/
int SV_ModelIndex (const char *name)
{
	int i;

	if (!name || !name[0])
		return 0;

	for (i = 0; i < MAX_MODELS && sv.model_precache[i]; i++)
		if (!strcmp (sv.model_precache[i], name))
			return i;
	if (i == MAX_MODELS || !sv.model_precache[i])
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
	edict_t *svent;
	int		 entnum;
	eval_t	*val;

	for (entnum = 0; entnum < qcvm->num_edicts; entnum++)
	{
		// get the current server version
		svent = EDICT_NUM (entnum);
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
			svent->baseline.modelindex = SV_ModelIndex ("progs/player.mdl");
		}
		else
		{
			svent->baseline.colormap = 0;
			svent->baseline.modelindex = SV_ModelIndex (PR_GetString (svent->v.model));
			val = GetEdictFieldValue (svent, qcvm->extfields.alpha);
			if (val)
				svent->baseline.alpha = ENTALPHA_ENCODE (val->_float);
			else
				svent->baseline.alpha = svent->alpha; // johnfitz -- alpha support
			if ((val = GetEdictFieldValue (svent, qcvm->extfields.scale)))
				svent->baseline.scale = ENTSCALE_ENCODE (val->_float);
		}

		// Spike -- baselines are now transmitted on a per-client basis.
		// FIXME: should merge the above with other edict->entity_state copies (updates, baselines, spawnstatics)
		// 1) this allows per-client extensions.
		// 2) this avoids pre-generating a single signon buffer, splitting it over multiple packets.
		//    thereby allowing more than 3k or so entities
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
	byte	  data[128];
	sizebuf_t msg;

	msg.data = data;
	msg.cursize = 0;
	msg.maxsize = sizeof (data);

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
	int i, j;

	svs.serverflags = pr_global_struct->serverflags;

	for (i = 0; i < svs.maxclients; i++)
	{
		client_t *client = &svs.clients[i];
		host_client = client;
		if (!client->active)
			continue;

		// call the progs to get default spawn parms for the new client
		if (!SV_CoopRespawnSetChangeParms (client))
			continue;
		for (j = 0; j < NUM_BASIC_SPAWN_PARMS; j++)
			client->spawn_parms[j] = (&pr_global_struct->parm1)[j];
		for (; j < NUM_TOTAL_SPAWN_PARMS; j++)
		{
			ddef_t *g = ED_FindGlobal (va ("parm%i", j + 1));
			client->spawn_parms[j] = g ? qcvm->globals[g->ofs] : 0;
		}
	}
}

typedef enum
{
	MAPCHECK_FAILED,
	MAPCHECK_PARTIAL,
	MAPCHECK_OK,
} mapcheck_t;

/*
================
SV_MapCheckThresh
================
*/
static mapcheck_t SV_MapCheckThresh (int current, int target)
{
	if (current <= 0)
		return MAPCHECK_FAILED;
	if (current >= target)
		return MAPCHECK_OK;
	return MAPCHECK_PARTIAL;
}

/*
================
SV_PrintMapCheck
================
*/
static void SV_PrintMapCheck (mapcheck_t status, const char *format, ...)
{
	char	str[1024];
	va_list argptr;

	va_start (argptr, format);
	q_vsnprintf (str, sizeof (str), format, argptr);
	va_end (argptr);

	if (status == MAPCHECK_OK)
		Con_SafePrintf ("[x] %s\n", str);
	else
	{
		Con_SafePrintf ("[%c] %s\n", status == MAPCHECK_PARTIAL ? '-' : ' ', str);
		sv.mapchecks.numwarnings++;
	}
}

/*
================
SV_PrintMapChecklist
================
*/
static void SV_PrintMapChecklist (void)
{
	const int MIN_DM_SPAWN_POINTS = 5;
	const int MIN_COOP_SPAWN_POINTS = 3;

	qboolean skill_levels;
	char	 buf[1024];
	int		 i, track, numskies, count;

	Con_SafePrintf ("\n");
	Con_SafePrintf ("=====================================\n");
	Con_SafePrintf ("\n");
	Con_SafePrintf ("Map checklist (%s):\n\n", COM_SkipPath (sv.modelname));

	SV_PrintMapCheck (qcvm->worldmodel->lightdata != NULL ? MAPCHECK_OK : MAPCHECK_FAILED, "lightmap data");

	if (!qcvm->worldmodel->visdata)
	{
		char pointfile[MAX_OSPATH];
		q_snprintf (pointfile, sizeof (pointfile), "maps/%s.pts", sv.name);
		if (COM_FileExists (pointfile, NULL))
			SV_PrintMapCheck (MAPCHECK_FAILED, "vis data (unsealed map?)");
		else
			SV_PrintMapCheck (MAPCHECK_FAILED, "vis data");
	}
	else
		SV_PrintMapCheck (MAPCHECK_OK, "vis data");

	if (!sv.mapchecks.trigger_changelevel)
		SV_PrintMapCheck (MAPCHECK_FAILED, "trigger_changelevel");
	else if (sv.mapchecks.trigger_changelevel == 1)
	{
		if (sv.mapchecks.valid_changelevel == sv.mapchecks.trigger_changelevel)
			SV_PrintMapCheck (MAPCHECK_OK, "trigger_changelevel (%s)", sv.mapchecks.changelevel);
		else
			SV_PrintMapCheck (MAPCHECK_PARTIAL, "trigger_changelevel (missing \"map\" key)");
	}
	else
	{
		if (sv.mapchecks.valid_changelevel == sv.mapchecks.trigger_changelevel)
			SV_PrintMapCheck (MAPCHECK_OK, "trigger_changelevel (%d)", sv.mapchecks.trigger_changelevel);
		else
			SV_PrintMapCheck (
				MAPCHECK_PARTIAL, "trigger_changelevel (%d/%d missing \"map\" key)", sv.mapchecks.trigger_changelevel - sv.mapchecks.valid_changelevel,
				sv.mapchecks.trigger_changelevel);
	}

	if (!sv.mapchecks.intermission)
		SV_PrintMapCheck (MAPCHECK_FAILED, "info_intermission");
	else
		SV_PrintMapCheck (MAPCHECK_OK, "info_intermission (%d)", sv.mapchecks.intermission);

	skill_levels = sv.mapchecks.skill_triggers > 0 ||
				   (sv.mapchecks.skill_ents[0] != sv.mapchecks.skill_ents[1] || sv.mapchecks.skill_ents[1] != sv.mapchecks.skill_ents[2]);
	SV_PrintMapCheck (skill_levels ? MAPCHECK_OK : MAPCHECK_FAILED, "skill spawnflags/triggers");

	SV_PrintMapCheck (
		SV_MapCheckThresh (sv.mapchecks.coop_spawns, MIN_COOP_SPAWN_POINTS), "info_player_coop (%d/%d+)", sv.mapchecks.coop_spawns, MIN_COOP_SPAWN_POINTS);

	SV_PrintMapCheck (
		SV_MapCheckThresh (sv.mapchecks.dm_spawns, MIN_DM_SPAWN_POINTS), "info_player_deathmatch (%d/%d+)", sv.mapchecks.dm_spawns, MIN_DM_SPAWN_POINTS);

	track = (int)qcvm->edicts->v.sounds;
	if (track == 0)
		SV_PrintMapCheck (MAPCHECK_FAILED, "music track (worldspawn \"sounds\" field)");
	else if (track < 2 || track > 255)
		SV_PrintMapCheck (MAPCHECK_FAILED, "music track (%d, should be between 2 and 255)", track);
	else
		SV_PrintMapCheck (MAPCHECK_OK, "music track (%d)", track);

	Mod_SanitizeMapDescription (buf, sizeof (buf), PR_GetString ((int)qcvm->edicts->v.message));
	if (buf[0])
		SV_PrintMapCheck (MAPCHECK_OK, "map title (%s)", buf);
	else
		SV_PrintMapCheck (MAPCHECK_FAILED, "map title (worldspawn \"message\" field)");

	numskies = qcvm->worldmodel->texofs[TEXTYPE_SKY + 1] - qcvm->worldmodel->texofs[TEXTYPE_SKY];
	count = 0;
	for (i = 0; i < numskies; i++)
	{
		texture_t *tex = qcvm->worldmodel->textures[qcvm->worldmodel->usedtextures[qcvm->worldmodel->texofs[TEXTYPE_SKY] + i]];
		if (tex->width != 256 || tex->height != 128)
			count++;
	}
	if (numskies > 1 || count > 0)
	{
		SV_PrintMapCheck (MAPCHECK_FAILED, "compat: single %ssky texture (%d found)", count > 0 ? "256 x 128 " : "", numskies);
		for (i = 0; i < numskies; i++)
		{
			texture_t *tex = qcvm->worldmodel->textures[qcvm->worldmodel->usedtextures[qcvm->worldmodel->texofs[TEXTYPE_SKY] + i]];
			if (tex->width != 256 || tex->height != 128)
				q_snprintf (buf, sizeof (buf), " (%d x %d)", tex->width, tex->height);
			else
				buf[0] = '\0';
			Con_SafePrintf ("\x02    %d. %s%s\n", i + 1, tex->name, buf);
		}
	}

	Con_SafePrintf ("\n");
	Con_SafePrintf ("=====================================\n");
	Con_SafePrintf ("\n");
}

// used for sv.qcvm.GetModel (so ssqc+csqc can share builtins)
qmodel_t *SV_ModelForIndex (int index)
{
	if (index < 0 || index >= MAX_MODELS)
		return NULL;
	return sv.models[index];
}

/*
================
SV_SpawnServer

This is called at the start of each level
================
*/
void SV_SpawnServer (const char *server)
{
	static char dummy[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	edict_t	   *ent;
	int			i, signonsize;
	qcvm_t	   *vm = qcvm;

	// let's not have any servers with no name
	if (hostname.string[0] == 0)
		Cvar_Set ("hostname", "UNNAMED");
	SCR_CenterPrintClear ();

	Con_DPrintf ("SpawnServer: %s\n", server);
	svs.changelevel_issued = false; // now safe to issue another

	PR_SwitchQCVM (NULL);

	//
	// tell all connected clients that we are going to a new level
	//
	if (sv.active)
		SV_SendReconnect ();
	/* A map starts fresh optional pose and voice capability generations. */
	SV_ResetVRIKMapState ();
	SV_ResetVoiceMapState ();
	SV_CoopSharedResetState ();
	SV_CoopRespawnInventoryResetState ();
	/* Commands from the previous level must never survive into its successor. */
	for (i = 0; i < svs.maxclients; i++)
	{
		SV_ResetPrivateCommandQueue (&svs.clients[i]);
		SV_ResetPrivateVRContactState (&svs.clients[i]);
		svs.clients[i].vr_gorilla_capable = false;
		svs.clients[i].vr_gorilla_last_advertised = -1;
		svs.clients[i].vr_instant_stop_offered = false;
		svs.clients[i].vr_instant_stop_capable = false;
		svs.clients[i].akimbo_last_advertised_mask = -1;
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
	// memset (&sv, 0, sizeof(sv));
	Host_ClearMemory ();

	q_strlcpy (sv.name, server, sizeof (sv.name));
	if (developer.value || map_checks.value)
		sv.mapchecks.active = true;

	sv.protocol = sv_protocol; // johnfitz

	if (sv.protocol == PROTOCOL_RMQ)
	{
		// set up the protocol flags used by this server
		// (note - these could be cvar-ised so that server admins could choose the protocol features used by their servers)
		if (sv_protocol_pext2) // spike: I don't really want to step on anyone's toes, but floats have the exact same precision as qc does.
			sv.protocolflags = PRFL_FLOATCOORD | PRFL_SHORTANGLE;
		else // spike: purists might want to preserve the inprecision and just extend the range though. This matches vanilla QS. should compress a bit better
			 // too.
			sv.protocolflags = PRFL_INT32COORD | PRFL_SHORTANGLE;
	}
	else
		sv.protocolflags = 0;

	PR_SwitchQCVM (vm);
	// load progs to get entity field count
	PR_LoadProgs ("progs.dat", true, PROGHEADER_CRC, pr_ssqcbuiltins, pr_ssqcnumbuiltins);

	// allocate server memory
	/* Host_ClearMemory() called above already cleared the whole sv structure */
	qcvm->max_edicts = CLAMP (MIN_EDICTS, (int)max_edicts.value, MAX_EDICTS);  // johnfitz -- max_edicts cvar
	qcvm->edicts = (edict_t *)Mem_Alloc (qcvm->max_edicts * qcvm->edict_size); // ericw -- sv.edicts switched to use malloc()

#if defined(DEBUG) || defined(_DEBUG)
	for (int j = 0; j < qcvm->max_edicts; j++)
	{
		// set debug fiels for all max_edicts
		edict_t *e = EDICT_NUM_NO_CHECK (j);
		e->qcvm_owner = qcvm;
		e->edict_ptr = e;
		e->edict_num = j;
	}
#endif

	sv.datagram.maxsize = sizeof (sv.datagram_buf);
	sv.datagram.cursize = 0;
	sv.datagram.data = sv.datagram_buf;

	sv.multicast.maxsize = sizeof (sv.multicast_buf);
	sv.multicast.cursize = 0;
	sv.multicast.data = sv.multicast_buf;

	sv.reliable_datagram.maxsize = sizeof (sv.reliable_datagram_buf);
	sv.reliable_datagram.cursize = 0;
	sv.reliable_datagram.data = sv.reliable_datagram_buf;

	SV_AddSignonBuffer ();

	// leave slots at start for clients only:
	qcvm->num_edicts = qcvm->reserved_edicts = svs.maxclients + 1;

	for (i = 0; i < svs.maxclients; i++)
	{
		// skip entity 0 = World, initialized below:
		ent = EDICT_NUM (i + 1);
		assert (!ent->free);
		svs.clients[i].edict = ent;
	}

	sv.state = ss_loading;
	sv.paused = false;
	sv.nomonsters = (nomonsters.value != 0.f);

	qcvm->time = 1.0;

	q_strlcpy (sv.name, server, sizeof (sv.name));
	q_snprintf (sv.modelname, sizeof (sv.modelname), "maps/%s.bsp", server);
	qcvm->worldmodel = Mod_ForName (sv.modelname, false);
	if (!qcvm->worldmodel || qcvm->worldmodel->type != mod_brush)
	{
		Con_Printf ("Couldn't spawn server %s\n", sv.modelname);
		sv.active = false;
		return;
	}
	sv.models[1] = qcvm->worldmodel;
	qcvm->GetModel = SV_ModelForIndex;

	//
	// clear world interaction links
	//
	SV_ClearWorld ();

	sv.sound_precache[0] = dummy;
	sv.model_precache[0] = dummy;
	sv.model_precache[1] = sv.modelname;
	if (qcvm->worldmodel->numsubmodels > MAX_MODELS)
	{
		Con_Printf ("too many inline models %s\n", sv.modelname);
		sv.active = false;
		return;
	}
	for (i = 1; i < qcvm->worldmodel->numsubmodels; i++)
	{
		sv.model_precache[1 + i] = localmodels[i];
		sv.models[i + 1] = Mod_ForName (localmodels[i], false);
	}

	//
	// load the rest of the entities:
	//

	// Initialize entity 0 = World
	ent = EDICT_NUM (0);
	memset (&ent->v, 0, qcvm->progs->entityfields * 4);
	ent->free = false;
	ent->v.model = PR_SetEngineString (qcvm->worldmodel->name);
	ent->v.modelindex = 1; // world model
	ent->v.solid = SOLID_BSP;
	ent->v.movetype = MOVETYPE_PUSH;

	if (coop.value)
		pr_global_struct->coop = coop.value;
	else
		pr_global_struct->deathmatch = deathmatch.value;

	pr_global_struct->mapname = PR_SetEngineString (sv.name);

	// serverflags are for cross level information (sigils)
	pr_global_struct->serverflags = svs.serverflags;

	ED_LoadFromFile (qcvm->worldmodel->entities);

	sv.active = true;

	SV_Precache_Model ("progs/player.mdl"); // Spike -- SV_CreateBaseline depends on this model.

	// all setup is completed, any further precache statements are errors
	sv.state = ss_active;

	// run two frames to allow everything to settle
	host_frametime = 0.1;
	SV_Physics ();
	SV_Physics ();

	// create a baseline for more efficient communications
	SV_CreateBaseline ();

	// johnfitz -- warn if signon buffer larger than standard server can handle
	for (i = 0, signonsize = 0; i < sv.num_signon_buffers; i++)
		signonsize += sv.signon_buffers[i]->cursize;
	if (signonsize > 64000 - 2)
		Con_DWarning ("%i byte signon buffer exceeds QS limit of 63998.\n", signonsize);
	else if (signonsize > 8000 - 2) // max size that will fit into 8000-sized client->message buffer with 2 extra bytes on the end
		Con_DWarning ("%i byte signon buffer exceeds standard limit of 7998.\n", signonsize);
	// johnfitz

	// send serverinfo to all connected clients
	memset (svs.coop_initial_spawn_client, 0,
		sizeof (svs.coop_initial_spawn_client));
	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
	{
		host_client->knowntoqc = false;
		if (host_client->active)
		{
			if (i < MAX_SCOREBOARD)
				svs.coop_initial_spawn_client[i] = true;
			SV_SendServerinfo (host_client);
		}
	}

	Con_DPrintf ("Server spawned.\n");

	if (sv.mapchecks.active)
		SV_PrintMapChecklist ();
}
