/* Offline native engine negotiation. The real command offer/tokenizer, server
 * consumer, client initialization and serverinfo writer run with stock QC and
 * e1m1 loaded. Socket sends are held; the output header is inspected with real
 * MSG readers. This does not execute the client's serverinfo parser, signon,
 * transport, prediction or VR tracking. No SDL window or GPU is initialized.
 * Build/run: see negotiation_native.make and tests/README.md. */
#include "../Quake/sv_main.c"
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
	int tag;
	assert (!client->message.overflowed && client->message.cursize);
	net_message = client->message;
	MSG_BeginReading ();
	assert (MSG_ReadByte () == svc_print);
	(void)MSG_ReadString ();
	assert (MSG_ReadByte () == svc_serverinfo);
	tag = MSG_ReadLong ();
	if (expected)
	{
		assert (tag == PROTOCOL_QSVR_PROFILE);
		assert (MSG_ReadLong () == QSVR_PROTOCOL_PINNED);
		tag = MSG_ReadLong ();
	}
	else
		assert (tag != PROTOCOL_QSVR_PROFILE);
	if (client->protocol_pext2)
	{
		assert (tag == PROTOCOL_FTE_PEXT2);
		assert ((unsigned)MSG_ReadLong () == client->protocol_pext2);
		tag = MSG_ReadLong ();
	}
	assert (tag == sv.protocol);
	if (tag == PROTOCOL_RMQ)
		assert ((unsigned)MSG_ReadLong () == sv.protocolflags);
	assert (!msg_badread && client->protocol_qsvr == expected);
	assert (!client->private_pmove_walk_selected); /* Transport is not PMove. */
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
	puts ("NEGOTIATION_NATIVE_PASSED offer/consumer/header/per-peer isolation; no signon/replay claim");
	return 0;
}
