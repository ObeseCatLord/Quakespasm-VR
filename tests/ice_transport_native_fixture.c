/*
 * Deferred CAND-NET-004 local transport fixture.
 *
 * This reuses ordinary_connect_native_fixture.c's production dedicated/UDP
 * bootstrap and negotiation_native.make's engine object graph.  It does not
 * implement ICE, a UDP protocol, or a synthetic NET driver: the client is
 * created by NET_ConnectSpecial("udp://..."), the local server is sv.active,
 * and every exchange uses NET_CanSendMessage, NET_SendMessage,
 * NET_SendUnreliableMessage, NET_GetServerMessage, and NET_GetMessage.
 *
 * The optional DTLS section uses the same shared-socket direct-server path as
 * production and performs the native reliable/unreliable exchange. It does
 * not exercise broker room signaling or NAT traversal.
 */
#include "../Quake/net_sys.h"

#define main OrdinaryConnectFixture_DeferredMain
#include "ordinary_connect_native_fixture.c"
#undef main

#include "../Quake/net_dgrm.h"
#include "../Quake/ice/ice_quake.h"
#if defined(USE_GNUTLS) || defined(USE_OPENSSL)
#include "../Quake/ice/ice_private.h"
#undef qboolean
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if defined(USE_GNUTLS) || defined(USE_OPENSSL)
static int fixture_broker_parser_calls;

void __real__Datagram_BrokerPacket (byte *data, unsigned int length,
	sys_socket_t socket, struct qsockaddr *address);
void __wrap__Datagram_BrokerPacket (byte *data, unsigned int length,
	sys_socket_t socket, struct qsockaddr *address)
{
	fixture_broker_parser_calls++;
	__real__Datagram_BrokerPacket (data, length, socket, address);
}
#endif

static int Fixture_ActiveSocketCount (void)
{
	int count = 0;
	qsocket_t *socket;
	for (socket = net_activeSockets; socket; socket = socket->next)
		count++;
	return count;
}

static void Fixture_AddressClassifier (void)
{
	static const char *const accepted[] = {
		"/room", "rtc://broker.example/room", "rtcs://broker.example/room",
		"ice://broker.example/room", "ices://broker.example/room",
		"tcp://broker.example/room", "tls://broker.example/room",
		"http://broker.example/room", "https://broker.example/room",
		"udp://203.0.113.8:27950", "dtls://203.0.113.8:27950?fp=abc",
		"ws://203.0.113.8:27950", "wss://203.0.113.8:27950"
	};
	static const char *const rejected[] = {
		"", "203.0.113.8:26000", "quake.example", "ftp://broker.example/room"
	};
	unsigned int i;

	assert (!NQICE_IsAddress (NULL));
	for (i = 0; i < sizeof (accepted) / sizeof (accepted[0]); i++)
		assert (NQICE_IsAddress (accepted[i]));
	for (i = 0; i < sizeof (rejected) / sizeof (rejected[0]); i++)
		assert (!NQICE_IsAddress (rejected[i]));
}

static int Fixture_ListenPort (void)
{
	int i;
	for (i = 0; i < net_numlandrivers; i++)
		if (net_landrivers[i].initialized &&
			net_landrivers[i].listeningSock != INVALID_SOCKET)
		{
			struct qsockaddr address;
			if (net_landrivers[i].GetSocketAddr (net_landrivers[i].listeningSock, &address) == 0)
				return net_landrivers[i].GetSocketPort (&address);
		}
	return 0;
}

static qsocket_t *Fixture_ServerConnection (void)
{
	int i;
	for (i = 0; i < svs.maxclients; i++)
		if (svs.clients[i].netconnection)
			return svs.clients[i].netconnection;
	return NULL;
}

static void Fixture_Pause (void)
{
	usleep (1000);
}

static qsocket_t *Fixture_ConnectUdp (char *endpoint, size_t endpoint_size)
{
	qboolean handled;
	qsocket_t *client;
	qsocket_t *server = NULL;
	int port = Fixture_ListenPort ();

	assert (port > 0);
	assert (q_snprintf (endpoint, endpoint_size, "udp://127.0.0.1:%d", port) > 0);
	client = NET_ConnectSpecial (endpoint, &handled);
	assert (handled && client && client->driverdata && client->driverdata2);
	assert (!strcmp (NET_QSocketGetConnectAddressString (client), endpoint));

	/* NQICE_CanSendMessage drives the production direct handshake. Datagram's
	 * own QGetAnyMessage accepts the control request and creates the server
	 * qsocket; no CL connection owner is involved. */
	for (int attempt = 0; attempt < 3000; attempt++)
	{
		(void) NET_CanSendMessage (client);
		(void) NET_GetServerMessage ();
		(void) NET_GetMessage (client);
		server = Fixture_ServerConnection ();
		if (server && NET_CanSendMessage (client))
			break;
		Fixture_Pause ();
	}
	assert (server && NET_CanSendMessage (client));
	assert (net_drivers[client->driver].Init == NQICE_Init);
	assert (net_drivers[server->driver].Init == Datagram_Init);
	return client;
}

static qsocket_t *Fixture_WaitServerMessage (void)
{
	qsocket_t *socket;
	for (int attempt = 0; attempt < 3000; attempt++)
	{
		socket = NET_GetServerMessage ();
		if (socket)
			return socket;
		Fixture_Pause ();
	}
	assert (!"timed out waiting for native server message");
	return NULL;
}

static int Fixture_WaitClientMessage (qsocket_t *client)
{
	int result;
	for (int attempt = 0; attempt < 3000; attempt++)
	{
		result = NET_GetMessage (client);
		if (result)
			return result;
		Fixture_Pause ();
	}
	assert (!"timed out waiting for ICE client message");
	return -1;
}

static void Fixture_ExpectMessage (const char *expected)
{
	assert ((size_t) net_message.cursize == strlen (expected));
	assert (!memcmp (net_message.data, expected, net_message.cursize));
}

static void Fixture_Exchange (qsocket_t *client)
{
	qsocket_t *server = Fixture_ServerConnection ();
	byte client_reliable[] = "ice-client-reliable";
	byte client_unreliable[] = "ice-client-unreliable";
	byte server_reliable[] = "native-server-reliable";
	byte server_unreliable[] = "native-server-unreliable";
	sizebuf_t message;

	assert (server);
	message = (sizebuf_t) {.data = client_reliable, .cursize = sizeof (client_reliable) - 1,
		.maxsize = sizeof (client_reliable) - 1};
	assert (NET_SendMessage (client, &message) == 1);
	assert (Fixture_WaitServerMessage () == server);
	Fixture_ExpectMessage ((char *)client_reliable);

	message = (sizebuf_t) {.data = client_unreliable, .cursize = sizeof (client_unreliable) - 1,
		.maxsize = sizeof (client_unreliable) - 1};
	assert (NET_SendUnreliableMessage (client, &message) == 1);
	assert (Fixture_WaitServerMessage () == server);
	Fixture_ExpectMessage ((char *)client_unreliable);

	message = (sizebuf_t) {.data = server_reliable, .cursize = sizeof (server_reliable) - 1,
		.maxsize = sizeof (server_reliable) - 1};
	assert (NET_SendMessage (server, &message) == 1);
	assert (Fixture_WaitClientMessage (client) == 1);
	Fixture_ExpectMessage ((char *)server_reliable);

	message = (sizebuf_t) {.data = server_unreliable, .cursize = sizeof (server_unreliable) - 1,
		.maxsize = sizeof (server_unreliable) - 1};
	assert (NET_SendUnreliableMessage (server, &message) == 1);
	assert (Fixture_WaitClientMessage (client) == 2);
	Fixture_ExpectMessage ((char *)server_unreliable);
}

static void Fixture_ClosePair (qsocket_t *client)
{
	int i;
	qsocket_t *server = Fixture_ServerConnection ();

	assert (client && server);
	NET_Close (client);
	for (i = 0; i < svs.maxclients; i++)
		if (svs.clients[i].netconnection == server)
		{
			host_client = &svs.clients[i];
			SV_DropClient (true);
			break;
		}
	assert (i < svs.maxclients);
	assert (Fixture_ActiveSocketCount () == 0);
}

static void Fixture_CancelAndRelisten (void)
{
	char endpoint[MAX_OSPATH];
	qboolean handled;
	qsocket_t *client;

	assert (q_snprintf (endpoint, sizeof (endpoint), "udp://127.0.0.1:%d",
		Fixture_ListenPort ()) > 0);
	client = NET_ConnectSpecial (endpoint, &handled);
	assert (handled && client && !strcmp (client->connectaddress, endpoint));
	NET_Close (client);
	assert (Fixture_ActiveSocketCount () == 0);

	/* This is the same ownership order as NET_Listen_f: Datagram unshares the
	 * borrowed descriptors before the ICE host module is closed. A second real
	 * exchange after the rebind catches a stale wrapper targeting an old fd. */
	Datagram_Listen (false);
	NQICE_Listen (false);
	Datagram_Listen (true);
	NQICE_Listen (true);
	assert (NQICE_IsListening ());
}

#if defined(USE_GNUTLS) || defined(USE_OPENSSL)
static qsocket_t *Fixture_ConnectDtls (char *endpoint, size_t endpoint_size)
{
	const char *fingerprint = NQICE_GetFingerprint ();
	qboolean handled;
	qsocket_t *client;
	qsocket_t *server = NULL;

	assert (*fingerprint && Fixture_ListenPort () > 0);
	assert (q_snprintf (endpoint, endpoint_size, "dtls://127.0.0.1:%d?fp=%s",
		Fixture_ListenPort (), fingerprint) > 0);
	client = NET_ConnectSpecial (endpoint, &handled);
	assert (handled && client && !strcmp (client->connectaddress, endpoint));
	for (int attempt = 0; attempt < 6000; attempt++)
	{
		(void) NET_CanSendMessage (client);
		(void) NET_GetServerMessage ();
		(void) NET_GetMessage (client);
		server = Fixture_ServerConnection ();
		if (server && NET_CanSendMessage (client))
			break;
		Fixture_Pause ();
	}
	assert (server && NET_CanSendMessage (client));
	assert (net_drivers[client->driver].Init == NQICE_Init);
	assert (net_drivers[server->driver].Init == NQICE_Init);
	return client;
}

static void Fixture_SendEncryptedBrokerControl (qsocket_t *client)
{
	/* This is intentionally not a synthetic broker: SendPacket encrypts this
	 * connectionless /udp control frame on the just-signed-on direct DTLS peer.
	 * The candidate names no live broker peer, so native control parsing has no
	 * externally observable signaling side effect. */
	static const byte control[] = {
		0xff, 0xff, 0xff, 0xff,
		'i', 'c', 'e', '_', 'c', 'c', 'a', 'n', 'd', ' ',
		'f', 'i', 'x', 't', 'u', 'r', 'e', ' ', '0', ' ', '0', '\n',
		'i', 'g', 'n', 'o', 'r', 'e', 'd'
	};
	struct icestate_s *ice = (struct icestate_s *) client->driverdata2;
	int parser_calls = fixture_broker_parser_calls;

	assert (ice);
	assert (iceapi.SendPacket (ice, control, sizeof (control)) == NETERR_SENT);
	for (int attempt = 0; attempt < 300; attempt++)
	{
		(void) NET_GetServerMessage ();
		(void) NET_GetMessage (client);
		Fixture_Pause ();
	}
	assert (fixture_broker_parser_calls == parser_calls + 1);
	/* The adapter deliberately exposes authentication only during the
	 * decrypted callback, never as a sticky peer property. */
	assert (!BrokerDTLS_IsAuthenticated ());
}

static void Fixture_ServerQueueOverflow (qsocket_t *client)
{
	byte payload[] = "ice-server-queue-overflow";
	sizebuf_t message = {
		.data = payload, .cursize = sizeof (payload) - 1,
		.maxsize = sizeof (payload) - 1
	};

	/* Datagram drains its shared socket before the ICE driver's singular
	 * callback runs. More completed packets than the bounded server FIFO must
	 * retire the peer through the native lifecycle instead of pre-marking the
	 * qsocket disconnected or losing an acknowledged reliable prefix. */
	for (int i = 0; i < 65; i++)
		assert (NET_SendUnreliableMessage (client, &message) == 1);
	for (int attempt = 0; attempt < 3000 && Fixture_ServerConnection (); attempt++)
	{
		(void) NET_GetServerMessage ();
		Fixture_Pause ();
	}
	assert (!Fixture_ServerConnection ());
	NET_Close (client);
	assert (Fixture_ActiveSocketCount () == 0);
}

static void Fixture_ClientQueueOverflow (char *endpoint, size_t endpoint_size)
{
	qsocket_t *client = Fixture_ConnectDtls (endpoint, endpoint_size);
	qsocket_t *server = Fixture_ServerConnection ();
	byte payload[] = "ice-client-queue-overflow";
	sizebuf_t message = {
		.data = payload, .cursize = sizeof (payload) - 1,
		.maxsize = sizeof (payload) - 1
	};

	assert (server);
	for (int i = 0; i < 65; i++)
		assert (NET_SendUnreliableMessage (server, &message) == 1);
	/* One native module poll must reject excess completed packets instead of
	 * returning an earlier successful FIFO entry after setting the error. */
	usleep (100000);
	assert (NET_GetMessage (client) == -1);
	Fixture_ClosePair (client);
}

#endif

int main (int argc, char **argv)
{
	char endpoint[MAX_OSPATH];
	qsocket_t *client;

	Fixture_AddressClassifier ();
	Fixture_InitClientOwners (argc, argv);
	PR_SwitchQCVM (&sv.qcvm);
	SV_SpawnServer ("start");
	assert (sv.active);
	Datagram_Listen (true);
	NQICE_Listen (true);
	assert (NQICE_IsListening ());

	client = Fixture_ConnectUdp (endpoint, sizeof (endpoint));
	Fixture_Exchange (client);
	Fixture_ClosePair (client);
	Fixture_CancelAndRelisten ();

	client = Fixture_ConnectUdp (endpoint, sizeof (endpoint));
	Fixture_Exchange (client);
	Fixture_ClosePair (client);
#if defined(USE_GNUTLS) || defined(USE_OPENSSL)
	client = Fixture_ConnectDtls (endpoint, sizeof (endpoint));
	Fixture_Exchange (client);
	Fixture_SendEncryptedBrokerControl (client);
	Fixture_ClosePair (client);
	client = Fixture_ConnectDtls (endpoint, sizeof (endpoint));
	Fixture_Exchange (client);
	Fixture_ServerQueueOverflow (client);
	Fixture_ClientQueueOverflow (endpoint, sizeof (endpoint));
#endif
	puts ("ICE_TRANSPORT_NATIVE_PASSED udp-direct/native-datagram reliable-unreliable close-relisten");
	return 0;
}
