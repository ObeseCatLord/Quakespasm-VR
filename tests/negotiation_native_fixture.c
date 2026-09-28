/* Offline native engine negotiation. The real command offer/tokenizer, server
 * consumer, client initialization and serverinfo writer run with stock QC and
 * e1m1 loaded. Socket sends are held; the output header is inspected with real
 * MSG readers. This does not execute the client's serverinfo parser, signon,
 * transport, prediction or VR tracking. No SDL window or GPU is initialized.
 * Build/run: see negotiation_native.make and tests/README.md. */
#include "../Quake/sv_main.c"
#include "../Quake/cl_demo.c"
#include "../Quake/cl_parse.c"
#include "../Quake/net_sys.h"
#include "../Quake/net_defs.h"
#include "native_engine_fixture.h"
#include <assert.h>

qboolean __wrap_NET_CanSendMessage (qsocket_t *socket)
{
	assert (socket);
	return false; /* Keep the production writer's bytes for inspection. */
}

static char modern_offer[1024];

static void ClientOffer (unsigned legacy, qboolean no_extensions, char *out,
	size_t capacity)
{
	static byte bytes[2048];
	extern cvar_t cl_nopext;
	cls.state = ca_connected;
	cls.demoplayback = false;
	cls.legacy_qsvr = legacy;
	cls.offered_qsvr = 0;
	memset (&cls.message, 0, sizeof (cls.message));
	cls.message.data = bytes;
	cls.message.maxsize = sizeof (bytes);
	Cvar_SetQuick (&cl_nopext, no_extensions ? "1" : "0");
	Cmd_ExecuteString ("cmd pext", src_command);
	assert (!cls.message.overflowed && cls.message.cursize > 1);
	net_message = cls.message;
	MSG_BeginReading ();
	assert (MSG_ReadByte () == clc_stringcmd);
	q_strlcpy (out, MSG_ReadString (), capacity);
	assert (!msg_badread && msg_readcount == net_message.cursize);
	assert (cls.offered_qsvr == (legacy || no_extensions ? 0 : QSVR_PROTOCOL_PINNED));
}

static client_t *PrepareClient (int slot)
{
	client_t *client = &svs.clients[slot];
	if (!client->netconnection)
		client->netconnection = NET_NewQSocket ();
	assert (client->netconnection);
	/* Reinitialize the same synthetic endpoint between cases, rather than
	 * accumulating another active connection count for that one slot. */
	if (client->active)
		net_activeconnections--;
	host_client = client;
	SV_ConnectClient (slot);
	assert (client->active && !client->pextknown);
	assert (!client->protocol_qsvr && !client->private_pmove_walk_selected);
	/* The initial writer asked for extensions; don't carry those bytes into
	 * the post-offer serverinfo header inspected below. */
	SZ_Clear (&client->message);
	return client;
}

static void Header (client_t *client, unsigned expected)
{
	cl_server_protocol_t decoded, unchanged;
	const char *error;
	int prefix_start;
	assert (!client->message.overflowed && client->message.cursize);
	net_message = client->message;
	MSG_BeginReading ();
	assert (MSG_ReadByte () == svc_print);
	(void)MSG_ReadString ();
	assert (MSG_ReadByte () == svc_serverinfo);
	prefix_start = msg_readcount;
	error = CL_ReadServerProtocol (false, client->offered_qsvr, 0, &decoded);
	assert (!error && decoded.protocol == sv.protocol);
	assert (decoded.protocolflags == (sv.protocol == PROTOCOL_RMQ ? sv.protocolflags : 0));
	assert (decoded.protocol_pext2 == client->protocol_pext2 && decoded.protocol_qsvr == expected);
	assert (!msg_badread && client->protocol_qsvr == expected);
	assert (!client->private_pmove_walk_selected); /* Transport is not PMove. */
	/* The actual header is also self-describing offline, without an offer. */
	msg_readcount = prefix_start;
	assert (!CL_ReadServerProtocol (true, 0, 0, &decoded));
	assert (decoded.protocol_qsvr == expected);
	if (expected)
	{
		memset (&decoded, 0xa5, sizeof (decoded));
		unchanged = decoded;
		msg_readcount = prefix_start;
		assert (CL_ReadServerProtocol (false, 0, 0, &decoded));
		assert (!memcmp (&unchanged, &decoded, sizeof (decoded)));
	}
}

static void ProtocolFailures (void)
{
	byte bytes[128];
	sizebuf_t packet = {.data = bytes, .maxsize = sizeof (bytes)};
	cl_server_protocol_t decoded, unchanged;
	int length;
	MSG_WriteLong (&packet, PROTOCOL_QSVR_PROFILE);
	MSG_WriteLong (&packet, QSVR_PROTOCOL_PINNED);
	MSG_WriteLong (&packet, PROTOCOL_FTE_PEXT2);
	MSG_WriteLong (&packet, QSVR_PEXT2_REQUIRED);
	MSG_WriteLong (&packet, PROTOCOL_RMQ);
	MSG_WriteLong (&packet, PRFL_FLOATCOORD | PRFL_SHORTANGLE);
	length = packet.cursize;
	memset (&unchanged, 0xa5, sizeof (unchanged));
	for (int cut = 0; cut < length; cut++)
	{
		net_message = packet;
		net_message.cursize = cut;
		MSG_BeginReading ();
		decoded = unchanged;
		assert (CL_ReadServerProtocol (true, 0, 0, &decoded));
		assert (!memcmp (&unchanged, &decoded, sizeof (decoded)));
	}
	/* Alter one complete field at a time; keep every other byte unchanged. */
	const int field_offsets[] = {4, 12, 16, 20};
	const int bad_values[] = {QSVR_PROTOCOL_PINNED + 1, PEXT2_SUPPORTED_CLIENT,
		PROTOCOL_FITZQUAKE, PRFL_SHORTANGLE};
	for (unsigned i = 0; i < sizeof (field_offsets) / sizeof (field_offsets[0]); i++)
	{
		byte modified[128];
		memcpy (modified, bytes, length);
		for (int j = 0; j < 4; j++)
			modified[field_offsets[i] + j] = (unsigned)bad_values[i] >> (8 * j);
		net_message = packet;
		net_message.data = modified;
		MSG_BeginReading ();
		assert (CL_ReadServerProtocol (true, 0, 0, &decoded));
	}
	/* A duplicate marker and a marker after an extension with value zero
	 * are both misplaced, even though no nonzero extension state exists. */
	for (int duplicate = 0; duplicate < 2; duplicate++)
	{
		byte modified[128];
		sizebuf_t bad = {.data = modified, .maxsize = sizeof (modified)};
		MSG_WriteLong (&bad, duplicate ? PROTOCOL_QSVR_PROFILE : PROTOCOL_FTE_PEXT1);
		MSG_WriteLong (&bad, duplicate ? QSVR_PROTOCOL_PINNED : 0);
		SZ_Write (&bad, bytes, length);
		net_message = bad;
		MSG_BeginReading ();
		assert (CL_ReadServerProtocol (true, 0, 0, &decoded));
	}
	/* Colliding unmarked flags do not establish an offline private layout. */
	net_message = packet;
	net_message.data += 8;
	net_message.cursize -= 8;
	MSG_BeginReading ();
	assert (CL_ReadServerProtocol (true, 0, 0, &decoded));
	MSG_BeginReading ();
	assert (!CL_ReadServerProtocol (false, 0, QSVR_PROTOCOL_PINNED, &decoded));
	assert (decoded.protocol_qsvr == QSVR_PROTOCOL_PINNED);
}

static void DemoServerdata (qboolean private_layout)
{
	static byte bytes[NET_MAXMESSAGE];
	cl_server_protocol_t decoded;
	FILE *file = tmpfile ();
	assert (file);
	cls.demofile = file;
	cls.demopaused = false;
	cls.signon = 0;
	cl.protocol = PROTOCOL_RMQ;
	cl.protocolflags = PRFL_FLOATCOORD | PRFL_SHORTANGLE;
	cl.protocol_pext2 = private_layout ? QSVR_PEXT2_REQUIRED : PEXT2_SUPPORTED_CLIENT;
	cl.protocol_qsvr = private_layout ? QSVR_PROTOCOL_PINNED : 0;
	cl.maxclients = svs.maxclients;
	cl.gametype = GAME_COOP;
	q_strlcpy (cl.levelname, "offline e1m1", sizeof (cl.levelname));
	cl.model_precache[1] = sv.qcvm.worldmodel;
	cl.model_precache[2] = NULL;
	cl.sound_precache[1] = NULL;
	cl.viewangles[0] = 11;
	cl.viewangles[1] = 23;
	cl.viewangles[2] = -7;
	net_message.data = bytes;
	net_message.maxsize = sizeof (bytes);
	SZ_Clear (&net_message);
	CL_Record_Serverdata (); /* Actual synthetic writer and file envelope. */
	rewind (file);
	cls.demoplayback = true;
	cls.offered_qsvr = cls.legacy_qsvr = 0;
	assert (CL_GetDemoMessage () == 1); /* Actual file reader. */
	MSG_BeginReading ();
	assert (MSG_ReadByte () == svc_serverinfo);
	assert (!CL_ReadServerProtocol (true, 0, 0, &decoded));
	assert (decoded.protocol_qsvr == (private_layout ? QSVR_PROTOCOL_PINNED : 0));
	assert (decoded.protocol_pext2 == cl.protocol_pext2);
	(void)MSG_ReadString (); /* Gamedir. */
	assert (MSG_ReadByte () == svs.maxclients && MSG_ReadByte () == GAME_COOP);
	assert (!strcmp (MSG_ReadString (), "offline e1m1"));
	assert (!strcmp (MSG_ReadString (), sv.qcvm.worldmodel->name));
	assert (MSG_ReadByte () == 0 && MSG_ReadByte () == 0);
	assert (MSG_ReadByte () == svc_signonnum && MSG_ReadByte () == 1);
	assert (!msg_badread && msg_readcount == net_message.cursize);
	assert (cl.mviewangles[0][0] == 11 && cl.mviewangles[0][1] == 23 && cl.mviewangles[0][2] == -7);
	fclose (file);
	cls.demofile = NULL;
	cls.demoplayback = false;
}

static void DemoEntityPackets (qboolean selected)
{
	static byte bytes[NET_MAXMESSAGE];
	client_t *client = &svs.clients[0];
	FILE *file = tmpfile ();
	assert (file && client->protocol_qsvr == QSVR_PROTOCOL_PINNED);
	host_client = client;
	client->spawned = client->knowntoqc = true;
	client->private_pmove_walk_selected = selected; /* Body producer, not admission proof. */
	client->private_input_phase = PRIVATE_INPUT_RUNNING;
	client->private_completed_move = 19;
	client->edict->v.health = 100;
	client->edict->v.deadflag = DEAD_NO;
	client->edict->v.movetype = MOVETYPE_WALK;
	client->edict->v.solid = SOLID_SLIDEBOX;
	VectorSet (client->edict->v.mins, -16, -16, -24);
	VectorSet (client->edict->v.maxs, 16, 16, 32);
	client->edict->v.waterlevel = 0;
	client->edict->v.teleport_time = 0;
	client->vr_gorilla_capable = true;
	client->vr_gorilla_cursor_valid = true;
	client->vr_gorilla_last_sequence = 19;
	memset (&client->vr_gorilla_state, 0, sizeof (client->vr_gorilla_state));
	client->vr_gorilla_state.initialized = true;
	VectorCopy (client->edict->v.origin, client->vr_gorilla_state.origin);
	cls.demofile = file;
	cls.netcon = NULL;
	cls.signon = 0;
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	cl.protocol_pext2 = QSVR_PEXT2_REQUIRED;
	cl.protocolflags = sv.protocolflags;
	cl.viewentity = 1;
	cl.vr_gorilla_supported = cl.vr_gorilla_trusted_cap_sent = false;
	cl.movemessages = 0;
	cl.ackedmovemessages = -1;
	cl.num_entities = 1;
	cl.max_edicts = qcvm->num_edicts + 1;
	cl.entities = Mem_Alloc (cl.max_edicts * sizeof (*cl.entities));
	for (int i = 0; i < MAX_MODELS; i++) cl.model_precache[i] = sv.models[i];
	net_message.data = bytes;
	net_message.maxsize = sizeof (bytes);
	for (int frame = 0; frame < 2; frame++)
	{
		client->edict->v.origin[0] = frame ? 16 : 8;
		VectorCopy (client->edict->v.origin, client->vr_gorilla_state.origin);
		SV_PresendClientDatagram (client);
		SZ_Clear (&net_message);
		assert (SVFTE_WriteEntitiesToClient (client, &net_message, sizeof (bytes), false));
		/* Verify this current writer really emitted the optional body. */
		MSG_BeginReading ();
		assert (MSG_ReadByte () == svcfte_updateentities);
		(void)MSG_ReadShort ();
		int flags = MSG_ReadByte ();
		assert (!!(flags & MOVEACK_FLAG_VR_GORILLA) == selected);
		assert (!(flags & MOVEACK_FLAG_GORILLA_TRUSTED));
		MSG_WriteByte (&net_message, svc_nop);
		CL_WriteDemoMessage ();
	}
	rewind (file);
	cls.demoplayback = true;
	for (int frame = 0; frame < 2; frame++)
	{
		assert (CL_GetDemoMessage () == 1);
		MSG_BeginReading ();
		assert (MSG_ReadByte () == svcfte_updateentities);
		CLFTE_ParseEntitiesUpdate ();
		assert (!msg_badread && cl.entities[1].netstate.origin[0] == (frame ? 16 : 8));
		assert (MSG_ReadByte () == svc_nop && msg_readcount == net_message.cursize);
		assert (!cl.movemessages && cl.ackedmovemessages == -1 &&
			!cl.move_ack_prediction_allowed && !cl.vr_gorilla_state_valid &&
			!cl.vr_gorilla_trusted_cap_sent && !cl.net_move_stale_acks);
	}
	fclose (file);
	cls.demofile = NULL;
	cls.demoplayback = false;
	Mem_Free (cl.entities);
	cl.entities = NULL;
	cl.num_entities = cl.max_edicts = 0;
	client->private_pmove_walk_selected = false;
}

static client_t *Negotiate (int slot, const char *offer, unsigned expected)
{
	client_t *client = PrepareClient (slot);
	Cmd_ExecuteString (offer, src_client);
	assert (client->pextknown);
	Header (client, expected);
	return client;
}

static void Cases (void)
{
	char legacy_offer[1024], plain_offer[1024], wrong_offer[1024], partial_offer[1024];
	client_t *private_peer, *public_peer;
	int saved_protocol = sv.protocol;
	unsigned saved_flags = sv.protocolflags;

	ClientOffer (0, false, modern_offer, sizeof (modern_offer));
	/* Before any server override: ordinary modern peers must reach the
	 * production private transport, without selecting movement prediction. */
	assert (!strcmp (sv_qsvr_private.default_string, "1") && sv_qsvr_private.value == 1);
	assert (!strcmp (sv_private_pmove_walk.default_string, "0") && !sv_private_pmove_walk.value);
	Negotiate (0, modern_offer, QSVR_PROTOCOL_PINNED);
	puts ("NEGOTIATION_NATIVE_DEFAULT_PASSED untouched private=1 pmove=0");
	ClientOffer (QSVR_PROTOCOL_PINNED, false, legacy_offer, sizeof (legacy_offer));
	ClientOffer (0, true, plain_offer, sizeof (plain_offer));
	assert (!strcmp (plain_offer, "pext"));
	assert (strcmp (modern_offer, legacy_offer));
	ClientOffer (0, false, modern_offer, sizeof (modern_offer));

	q_snprintf (wrong_offer, sizeof (wrong_offer), "pext %#x %#x %#x %#x",
		PROTOCOL_FTE_PEXT2, PEXT2_SUPPORTED_CLIENT, PROTOCOL_QSVR_PROFILE,
		QSVR_PROTOCOL_PINNED + 1);
	q_snprintf (partial_offer, sizeof (partial_offer), "pext %#x %#x %#x %#x",
		PROTOCOL_FTE_PEXT2, PEXT2_REPLACEMENTDELTAS, PROTOCOL_QSVR_PROFILE,
		QSVR_PROTOCOL_PINNED);

	Cvar_SetQuick (&sv_qsvr_private, "0");
	Negotiate (0, modern_offer, 0);
	Cvar_SetQuick (&sv_qsvr_private, "1");
	private_peer = Negotiate (0, modern_offer, QSVR_PROTOCOL_PINNED);
	public_peer = Negotiate (1, legacy_offer, 0);
	assert (private_peer->protocol_qsvr == QSVR_PROTOCOL_PINNED &&
		public_peer->protocol_qsvr == 0);
	assert (private_peer->protocol_pext2 == QSVR_PEXT2_REQUIRED &&
		public_peer->protocol_pext2 == (PEXT2_SUPPORTED_CLIENT & sv_protocol_pext2));
	Negotiate (1, plain_offer, 0);
	Negotiate (1, wrong_offer, 0);
	Negotiate (1, partial_offer, 0);
	assert (private_peer->protocol_qsvr == QSVR_PROTOCOL_PINNED);

	/* Real serverinfo refresh on the same owner must drop stale selection
	 * after a server disable; its client's capability still survives. */
	Cvar_SetQuick (&sv_qsvr_private, "0");
	host_client = private_peer;
	SZ_Clear (&private_peer->message);
	SV_SendServerinfo (private_peer);
	Header (private_peer, 0);
	assert (private_peer->offered_qsvr == QSVR_PROTOCOL_PINNED);
	Cvar_SetQuick (&sv_qsvr_private, "1");
	SZ_Clear (&private_peer->message);
	SV_SendServerinfo (private_peer);
	Header (private_peer, QSVR_PROTOCOL_PINNED);

	sv.protocolflags = PRFL_SHORTANGLE; /* Float coordinates are required. */
	Negotiate (1, modern_offer, 0);
	sv.protocolflags = saved_flags;
	sv.protocol = PROTOCOL_FITZQUAKE;
	Negotiate (1, modern_offer, 0);
	sv.protocol = saved_protocol;
	Negotiate (1, modern_offer, QSVR_PROTOCOL_PINNED);
	assert (!private_peer->private_pmove_walk_selected &&
		!public_peer->private_pmove_walk_selected);
}

int main (int argc, char **argv)
{
	Fixture_InitNativeEngine (argc, argv, "e1m1", true);
	assert (svs.maxclients >= 2);
	printf ("NEGOTIATION_NATIVE_DEFAULT private=%s pmove=%s\n",
		sv_qsvr_private.default_string, sv_private_pmove_walk.default_string);
	Cases ();
	ProtocolFailures ();
	DemoServerdata (false);
	DemoServerdata (true);
	DemoEntityPackets (false);
	DemoEntityPackets (true);
	puts ("NEGOTIATION_NATIVE_PASSED offer/consumer/header/per-peer isolation; no signon/replay claim");
	puts ("DEMO_SERVERDATA_NATIVE_PASSED writer/file-reader/protocol-prefix; no full playback claim");
	puts ("DEMO_ENTITY_NATIVE_PASSED writer/file-reader/entity-decoder/movement; selected admission staged");
	return 0;
}
