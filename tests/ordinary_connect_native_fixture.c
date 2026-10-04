/*
 * Deferred CAND-NET-003 native connection fixture.
 *
 * This starts the production engine as a dedicated process solely to avoid a
 * window/GPU bootstrap, then exposes the already-initialized network/client
 * owners to the fixture.  UDP remains enabled.  The small loopback UDP peer
 * below supplies protocol control replies; it is not a second connection
 * implementation.  CL_EstablishConnection, CL_AutoReconnectFrame,
 * NET_CachedConnectHost, NET_Slist_f and NET_DatagramConnect* execute from
 * their production objects.
 *
 * The accepted remote cases prove transport acceptance only.  They do not
 * feed a serverinfo/signon stream, so they intentionally do not claim a
 * connected-game result.  See run_ordinary_connect_native.py for the final
 * deferred build/run command.
 */
#include "../Quake/quakedef.h"
#include "../Quake/sv_main.c"
#include "../Quake/cl_demo.c"
#include "../Quake/cl_parse.c"
#include "../Quake/net_sys.h"
#include "../Quake/net_defs.h"
#include "native_engine_fixture.h"

#include <arpa/inet.h>
#include <assert.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

extern qboolean m_return_onerror;
extern char m_return_reason[32];

/* The shared native Make harness wraps this symbol. This transport fixture
 * deliberately forwards to the real owner instead of bypassing readiness. */
qboolean __real_NET_CanSendMessage (qsocket_t *socket);
qboolean __wrap_NET_CanSendMessage (qsocket_t *socket)
{
	return __real_NET_CanSendMessage (socket);
}

typedef struct
{
	int fd;
	char endpoint[64];
	struct sockaddr_storage peer;
	socklen_t peer_size;
} fixture_udp_peer_t;

static void Fixture_InitClientOwners (int argc, char **argv)
{
	static quakeparms_t parms;

	parms.basedir = ".";
	parms.argc = argc;
	parms.argv = argv;
	host_parms = &parms;
	COM_InitArgv (argc, argv);
	isDedicated = COM_CheckParm ("-dedicated") != 0;
	/* A dedicated process avoids VID/VR setup; UDP must stay enabled here. */
	assert (isDedicated && !COM_CheckParm ("-noudp"));
	assert (SDL_Init (0));
	Sys_Init ();
	Host_Init ();

	/* Host_Init deliberately skips CL_Init for a dedicated process.  The
	 * exercised connection owners only require this production message buffer;
	 * no renderer, sound device, menu draw, or client parser is initialized. */
	cls.state = ca_disconnected;
	SZ_Alloc (&cls.message, NET_MAXMESSAGE);
	cls.message.maxsize = 1024;
	assert (net_activeSockets == NULL && net_numsockets >= 2);
}

static fixture_udp_peer_t Fixture_OpenPeer (void)
{
	fixture_udp_peer_t peer;
	struct sockaddr_in address;
	socklen_t address_size = sizeof (address);

	memset (&peer, 0, sizeof (peer));
	peer.fd = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	assert (peer.fd >= 0);
	memset (&address, 0, sizeof (address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	assert (bind (peer.fd, (struct sockaddr *)&address, sizeof (address)) == 0);
	assert (getsockname (peer.fd, (struct sockaddr *)&address, &address_size) == 0);
	assert (q_snprintf (peer.endpoint, sizeof (peer.endpoint), "127.0.0.1:%u",
		(unsigned int)ntohs (address.sin_port)) > 0);
	return peer;
}

static void Fixture_WaitForConnectRequest (fixture_udp_peer_t *peer)
{
	struct pollfd pollfd = {.fd = peer->fd, .events = POLLIN};
	byte request[NET_MAXMESSAGE];

	assert (poll (&pollfd, 1, 2000) == 1 && (pollfd.revents & POLLIN));
	peer->peer_size = sizeof (peer->peer);
	assert (recvfrom (peer->fd, request, sizeof (request), 0,
		(struct sockaddr *)&peer->peer, &peer->peer_size) > 0);
}

static void Fixture_SendAccept (fixture_udp_peer_t *peer)
{
	byte response[sizeof (int) + 1 + sizeof (int)];
	int control = BigLong (NETFLAG_CTL | (int)sizeof (response));
	int game_port = 0;

	memcpy (response, &control, sizeof (control));
	response[sizeof (control)] = CCREP_ACCEPT;
	memcpy (response + sizeof (control) + 1, &game_port, sizeof (game_port));
	assert (sendto (peer->fd, response, sizeof (response), 0,
		(const struct sockaddr *)&peer->peer, peer->peer_size) == (ssize_t)sizeof (response));
}

static void Fixture_ClosePeer (fixture_udp_peer_t *peer)
{
	assert (close (peer->fd) == 0);
	peer->fd = -1;
}

static void Fixture_StartAndReachDatagramWait (const char *endpoint,
	fixture_udp_peer_t *peer)
{
	CL_EstablishConnection (endpoint, 0);
	/* The public call starts work but cannot synchronously poll the UDP peer. */
	assert (CL_ConnectionPending () && cls.state == ca_disconnected);
	CL_AutoReconnectFrame ();
	assert (CL_ConnectionPending () && cls.state == ca_disconnected);
	Fixture_WaitForConnectRequest (peer);
}

static void Fixture_CompleteTransport (fixture_udp_peer_t *peer)
{
	Fixture_SendAccept (peer);
	for (int n = 0; n < 20 && cls.state != ca_connected; ++n)
	{
		struct pollfd pollfd = {.fd = peer->fd, .events = 0};
		(void)poll (&pollfd, 1, 10);
		CL_AutoReconnectFrame ();
	}
	assert (cls.state == ca_connected && cls.netcon && CL_ConnectionPending ());
}

static void Fixture_ClearRemoteClient (void)
{
	CL_CancelAutoReconnect ();
	if (cls.state == ca_connected)
		CL_Disconnect ();
	assert (cls.state == ca_disconnected && !cls.netcon && !CL_ConnectionPending ());
	assert (net_activeSockets == NULL);
}

static void Fixture_OrdinaryRemoteReturnsPending (void)
{
	fixture_udp_peer_t peer = Fixture_OpenPeer ();

	Fixture_StartAndReachDatagramWait (peer.endpoint, &peer);
	Fixture_CompleteTransport (&peer);
	Fixture_ClearRemoteClient ();
	Fixture_ClosePeer (&peer);
}

static void Fixture_CancelReleasesSocketAndIgnoresLateAccept (void)
{
	fixture_udp_peer_t peer = Fixture_OpenPeer ();

	Fixture_StartAndReachDatagramWait (peer.endpoint, &peer);
	assert (net_activeSockets != NULL);
	CL_CancelAutoReconnect ();
	assert (!CL_ConnectionPending () && cls.state == ca_disconnected && !cls.netcon);
	assert (net_activeSockets == NULL);
	/* The old peer can still emit its queued success, but no owner remains to
	 * attach it on a later frame. */
	Fixture_SendAccept (&peer);
	CL_AutoReconnectFrame ();
	assert (cls.state == ca_disconnected && !cls.netcon && net_activeSockets == NULL);
	Fixture_ClosePeer (&peer);
}

static void Fixture_FailedEndpointReturnsMenuError (void)
{
	m_return_state = m_lanconfig;
	m_return_onerror = true;
	m_return_reason[0] = '\0';
	key_dest = key_game;
	CL_EstablishConnection ("256.256.256.256", 0);
	assert (CL_ConnectionPending ());
	CL_AutoReconnectFrame ();
	assert (!CL_ConnectionPending () && cls.state == ca_disconnected);
	assert (key_dest == key_menu && m_state == m_lanconfig && !m_return_onerror);
	assert (m_return_reason[0]);
}

static void Fixture_CachedAliasAndDiscoveryDoNotBusyWait (void)
{
	fixture_udp_peer_t peer = Fixture_OpenPeer ();

	hostCacheCount = 1;
	q_strlcpy (hostcache[0].name, "fixture-alias", sizeof (hostcache[0].name));
	q_strlcpy (hostcache[0].cname, peer.endpoint, sizeof (hostcache[0].cname));
	CL_EstablishConnection ("fixture-alias", 0);
	assert (CL_ConnectionPending () && cls.state == ca_disconnected);
	CL_AutoReconnectFrame ();
	Fixture_WaitForConnectRequest (&peer);
	CL_CancelAutoReconnect ();

	hostCacheCount = 0;
	CL_EstablishConnection ("", 0);
	assert (CL_ConnectionPending () && slistInProgress && cls.state == ca_disconnected);
	/* Discovery remains frame-owned while the native poll procedure is pending. */
	CL_AutoReconnectFrame ();
	assert (CL_ConnectionPending () && cls.state == ca_disconnected);
	hostCacheCount = 1;
	q_strlcpy (hostcache[0].cname, peer.endpoint, sizeof (hostcache[0].cname));
	slistInProgress = false;
	CL_AutoReconnectFrame ();
	Fixture_WaitForConnectRequest (&peer);
	CL_CancelAutoReconnect ();
	Fixture_ClosePeer (&peer);
}

static void Fixture_LocalLoopbackStaysImmediate (void)
{
	PR_SwitchQCVM (&sv.qcvm);
	SV_SpawnServer ("start");
	assert (sv.active);
	CL_EstablishConnection ("local", 0);
	assert (cls.state == ca_connected && cls.netcon && !CL_ConnectionPending ());
}

static void Fixture_ServerGameReleasesDirectOwnerToModTransfer (void)
{
	fixture_udp_peer_t peer = Fixture_OpenPeer ();
	cl_servermod_info_t info;

	Fixture_StartAndReachDatagramWait (peer.endpoint, &peer);
	Fixture_CompleteTransport (&peer);
	assert (CL_ServerModDownload_Begin ("fixture_missing_mod"));
	assert (!CL_ConnectionPending () && CL_ServerModDownload_GetInfo (&info));
	assert (info.phase == CL_SERVERMOD_CHECKING && !strcmp (info.game, "fixture_missing_mod"));
	assert (cls.state == ca_disconnected && !cls.netcon);
	CL_ServerModDownload_Cancel ();
	Fixture_ClosePeer (&peer);
}

int main (int argc, char **argv)
{
	Fixture_InitClientOwners (argc, argv);
	Fixture_OrdinaryRemoteReturnsPending ();
	Fixture_CancelReleasesSocketAndIgnoresLateAccept ();
	Fixture_FailedEndpointReturnsMenuError ();
	Fixture_CachedAliasAndDiscoveryDoNotBusyWait ();
	Fixture_ServerGameReleasesDirectOwnerToModTransfer ();
	Fixture_LocalLoopbackStaysImmediate ();
	puts ("ORDINARY_CONNECT_NATIVE_PASSED production client/datagram/cache/discovery/loopback owners");
	return 0;
}
