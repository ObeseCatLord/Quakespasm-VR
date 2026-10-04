/*
	this is the quake-specific part of our ice implementation. the parts that interface cvars and bits.
	also handles interfacing with the broker (ftemaster).
	provides an alternative to quake's loopback/dgram layer which allows us a little more time to complete the ice/turn/dtls/sctp state before quake's silly dgram code times out.

	sctp supports reliables which would allow higher reliable bandwidth... but our sctp implementation is
	a) optional.
	b) lacks code for reliables.
	FIXME: we really should be using that and ignoring any acks.

	we (embarassingly) need to reimplement various chunks of quake's net_dgram.c, in part for compat with FTE's code which benefits from extra handshakes (as well as for our sctp implementation's missing reliables).
*/
/*
multiplexing...
  application: 191 < leadbyte < 256
  rtp: 127 < leadbyte < 192 (if you're using it, otherwise free...)
  turnchan: 64 < leadbyte < 79 (excluded by sender, so can be reused by application, we don't use it cos we're too lazy to track)
  dtls: 19 < leadbyte < 64
  stun: leadbyte < 4
So, |=0x80 to avoid ambiguity with lower layers, so long as any rtp is in-band. helps to use big-endian.
*/

#include "../quakedef.h"
#include "../mem.h"
#include "../arch_def.h"
#include "../net_sys.h"
#include "../net_defs.h"

#include "ice_private.h"
#include "ice_quake.h"

/* Native Datagram owns parsing and authorization of /udp broker controls. */
extern void _Datagram_BrokerPacket(byte *data, unsigned int length, sys_socket_t socket, struct qsockaddr *address);

#define DGRAM_PROTOCOL_NAME "QUAKE" //combined with NET_PROTOCOL_VERSION. not to be confused with 'com_protocolname'.

/* The target keeps these legacy ProQuake wire values private to net_dgrm.c. */
enum
{
	QICE_MOD_PROQUAKE = 1,
	QICE_PQF_CHEATFREE = 0x01,
	QICE_PQF_IGNOREPORT = 0x80
};

//broker protocol (over websockets).
#define PORT_ICEBROKER	27950	//same as q3's master.

#define ALLOW_UNSOLICITED_ICE	//allow the broker to connect over dtls to broker new connections without long-lasting tls connections..

enum icemsgtype_s
{	//shared by rtcpeers+broker
	ICEMSG_PEERLOST=0,	//other side dropped connection
	ICEMSG_GREETING=1,	//master telling us our unique game name
	ICEMSG_NEWPEER=2,	//relay established, send an offer now.
	ICEMSG_OFFER=3,		//peer->peer - peer's offer or answer details
	ICEMSG_CANDIDATE=4,	//peer->peer - candidate updates. may arrive late as new ones are discovered.
	ICEMSG_ACCEPT=5,	//go go go (response from offer)
	ICEMSG_SERVERINFO=6,//server->broker (for advertising the server properly)
	ICEMSG_SERVERUPDATE=7,//broker->browser (for querying available server lists)
	ICEMSG_NAMEINUSE=8,	//requested resource is unavailable.
  ICEMSG_SERVERDETAILS_REQUEST = 9,// Broker → server: request full server details (players, scores, rules) */
  ICEMSG_SERVERDETAILS_RESPONSE = 10, // Server → broker: response with full server details */
  ICEMSG_PROBE = 11,		// Broker → server: lightweight ICE probe for latency measurement (no DTLS/SCTP)
};

#define CVAR_NOTFROMSERVER 0 //FIXME
#define CVARFD(name,val,flags,desc) {name,val,flags}

static cvar_t net_ice_exchangeprivateips = CVARFD("net_ice_exchangeprivateips", "0", CVAR_NOTFROMSERVER, "Boolean. When set to 0, hides private IP addresses from your peers - only addresses determined from the other side of your router will be shared. You should only need to set this to 1 if mdns is unavailable.");
static cvar_t net_ice_allowstun = CVARFD("net_ice_allowstun", "1", CVAR_NOTFROMSERVER, "Boolean. When set to 0, prevents the use of stun to determine our public address (does not prevent connecting to our peer's server-reflexive candidates).");
static cvar_t net_ice_allowturn = CVARFD("net_ice_allowturn", "1", CVAR_NOTFROMSERVER, "Boolean. When set to 0, prevents registration of turn connections (does not prevent connecting to our peer's relay candidates).");
static cvar_t net_ice_allowmdns = CVARFD("net_ice_allowmdns", "1", CVAR_NOTFROMSERVER, "Boolean. When set to 0, prevents the use of multicast-dns to obtain candidates using random numbers instead of revealing private network info.");
static cvar_t net_ice_relayonly = CVARFD("net_ice_relayonly", "0", CVAR_NOTFROMSERVER, "Boolean. When set to 1, blocks reporting non-relay local candidates, does not attempt to connect to remote candidates other than via a relay.");
#ifdef HAVE_DTLS
static cvar_t net_ice_usewebrtc = CVARFD("net_ice_usewebrtc", "", CVAR_NOTFROMSERVER, "Use webrtc's extra overheads rather than simple ICE. This makes packets larger and is slower to connect, but is compatible with the web port.");
#endif
static cvar_t net_ice_servers = CVARFD("net_ice_servers", "", CVAR_NOTFROMSERVER, "A space-separated list of ICE servers, eg stun:host.example:3478 or turn:host.example:3478?user=foo?auth=blah");
static cvar_t net_ice_debug = CVARFD("net_ice_debug", "0", CVAR_NOTFROMSERVER, "0: Hide messy details.\n1: Show new candidates.\n2: Show each connectivity test.");
static cvar_t net_ice_broker = CVARFD("net_ice_broker", "master.frag-net.com:27950", CVAR_NOTFROMSERVER, "This is the default broker we attempt to connect through when using 'sv_port_rtc /foo' or 'connect /foo'.");
cvar_t sv_port_rtc = CVARFD("sv_port_rtc", "", CVAR_NOTFROMSERVER, "This is the '/roomname' for your server to register as. When '/' will request the master assign one. Empty disables registration.");
static cvar_t sv_addr_ws = CVARFD("sv_addr_ws", "", CVAR_NOTFROMSERVER, "WebSocket address for this server (e.g. wss://example.com:27950). Sent as *wsaddr in infostrings.");
/* The target's modern client password belongs in cls.userinfo.  This local
 * registration preserves the donor's legacy numeric ProQuake/server password
 * contract when no such userinfo field has been set. */
static cvar_t qice_password = CVARFD("password", "", CVAR_NOTFROMSERVER, "Legacy ICE/ProQuake password fallback; prefer setinfo password for client userinfo.");
extern cvar_t com_protocolname;
extern cvar_t net_messagetimeout;
extern cvar_t net_connecttimeout;

static char fingerprint[256];
static qboolean nqice_random_failed;

//this is the clientside part of our custom accountless broker protocol
//basically just keeps the broker processing, but doesn't send/receive actual game packets.
//inbound messages can change ice connection states.
//clients only handle one connection. servers need to handle multiple
typedef struct {
	struct icemodule_s icemodule;

	//config state
	char brokername[64];	//dns name:port
	int brokerport;
	char gamename[64];		//what we're trying to register as/for with the broker
	qboolean isserver;
	qboolean issecure;		//connecting over tls only.
	qboolean direct;		//direct peer owns its own handshake; no broker is involved.

	//broker connection state
	icestream_t *broker;
	netadr_t brokeraddr;
	qboolean brokeraddr_valid;
	double reconnecttimeout;
	double heartbeat;	//timestamp for when to send the next heartbeat (for server browsers).
	qboolean error;		//broker failed. may still have udp/tcp sockets listening for direct connections though.

	//client state...
	struct icestate_s *ice;
	qsocket_t *qsock;
	int serverid;
	double dohandshake;	//timestamp of next ccreq_connect request

	//server state...
	struct
	{
		struct icestate_s *ice;
		qboolean isnew;
	} *clients;
	size_t numclients;

	struct heartbeatctx_s *heartbeatctx;	//for non-broker heartbeats, now we're using this for dtls etc too.
	struct brokerlookupctx_s *brokerctx;	//threaded broker dns lookup before the non-blocking tcp connect.
} qice_connection_t;

static struct icestate_s *QICE_SocketIce(const qsocket_t *socket)
{
	return socket->driverdata2;
}

static void QICE_SetSocketIce(qsocket_t *socket, struct icestate_s *ice)
{
	socket->driverdata2 = ice;
}

#ifdef ALLOW_UNSOLICITED_ICE
struct qice_userstate_s
{	//this block is for our inbound udp broker reliability, ensuring we get candidate info to where its needed... gotta use ICE_GetUserPtr to access
	char *text;
	unsigned int inseq;
	unsigned int outseq;
};
#endif

static void QICE_FreeBrokerLookup(qice_connection_t *b);
static void QICE_DiscardQueuedServerMessages(qsocket_t *socket);
static void QICE_RequestServerRetirement(qsocket_t *socket, const char *reason);
static void QICE_ResetClientMessageQueue(qice_connection_t *owner);

static void QICE_Close(qice_connection_t *b)
{
	qsocket_t *s;
	int cl;
	QICE_ResetClientMessageQueue(b);
	if (b->broker)
	{	//kill the websocket connection
		b->broker->Close(b->broker);
		b->broker = NULL;
	}
	QICE_FreeBrokerLookup(b);

	for (cl = 0; cl < b->numclients; cl++)
		if (b->clients[cl].ice)
			iceapi.Close(b->clients[cl].ice, false);
	if (b->ice)
		iceapi.Close(b->ice, false);

	for (s = net_activeSockets; s; s = s->next)
		if (s->driverdata == b)
		{	//the icemodule is about to be destroyed. if there's any ice states still attached then make sure they're detatched.
			if (QICE_SocketIce(s))
				iceapi.Close(QICE_SocketIce(s), true);
			QICE_SetSocketIce(s, NULL);
			s->driverdata = NULL;
		}
	iceapi.CloseModule(&b->icemodule);


	Mem_Free(b->clients);
	Mem_Free(b);
}

static void QICE_SetBrokerStunServer(qice_connection_t *b, struct icestate_s *ice)
{
	if (b->brokeraddr_valid)
	{
		char brokeraddr[128];
		iceapi.Set(ice, "server", va("stun:%s", NET_AdrToString(brokeraddr, sizeof(brokeraddr), &b->brokeraddr)));
	}
	else
		iceapi.Set(ice, "server", va("stun:%s:%i", b->brokername, b->brokerport));
}

static int QICE_PrepareBrokerFrame(int icemsg, int cl, char *data)	//returns offset.
{
	data[0] = icemsg;
	data[1] = cl&0xff;
	data[2] = (cl>>8)&0xff;
	return 3;
}
static void QICE_SendBrokerFrame(qice_connection_t *b, const char *msg)
{	//call QICE_PrepareBrokerFrame first.
	size_t msgsize = 3 + strlen(msg+3);
	b->broker->WriteBytes(b->broker, msg, msgsize);
}

#define MAX_MASTERS 64
struct heartbeatctx_s
{	//thread context used to avoid stalls on dns lookups.
	qboolean working;	//don't really need a barrier, we'll use join to sync before reading the rest.
	void *thread;

	int nummasters;
	struct
	{
		int okay;
		char *name;
	} master[MAX_MASTERS];

	size_t numresults;
	struct
	{
		char *name;
		netadr_t addr;
	} result[MAX_MASTERS];
};

struct brokerlookupctx_s
{
	struct brokerlookupctx_s *next;
	qboolean working;
	qboolean okay;
	void *thread;
	char brokername[64];
	int brokerport;
	netadr_t addr;
};

static struct brokerlookupctx_s *orphanedbrokerlookups;

static int DNSLookupThread(void *vctx)
{
	struct heartbeatctx_s *ctx = vctx;
	size_t i, k, o, m;
	netadr_t res[MAX_MASTERS];
	ctx->numresults = 0;
	for (m = 0; m < ctx->nummasters; m++)
	{
		k = NET_StringToAdr(ctx->master[m].name, PORT_ICEBROKER, res, sizeof(res));
		ctx->master[m].okay = k>0;
		for (i = 0; i < k; i++)	//for each new result
		{
			for (o = 0; ; o++)	//for each prior result
			{
				if (o == ctx->numresults)
				{	//new resulting address.
					if (ctx->numresults < countof(ctx->result))
					{
						ctx->result[o].name = ctx->master[m].name;
						ctx->result[o].addr = res[i];
						ctx->numresults++;
					}
					break;
				}
				if (NET_CompareAdr(&res[i], &ctx->result[o].addr))
					break;	//already on the list
			}
		}
	}

	ctx->working = false;	//done.
	return true;
}

static int BrokerLookupThread(void *vctx)
{
	struct brokerlookupctx_s *ctx = vctx;

	ctx->okay = NET_StringToAdr(ctx->brokername, ctx->brokerport, &ctx->addr, 1) > 0;
	ctx->working = false;
	return true;
}

static void QICE_CleanupBrokerLookups(void)
{
	struct brokerlookupctx_s **link = &orphanedbrokerlookups;
	struct brokerlookupctx_s *ctx;

	while ((ctx = *link))
	{
		if (ctx->working)
		{
			link = &ctx->next;
			continue;
		}
		if (ctx->thread)
			SDL_WaitThread(ctx->thread, NULL);
		*link = ctx->next;
		Mem_Free(ctx);
	}
}

static void QICE_FreeBrokerLookup(qice_connection_t *b)
{
	struct brokerlookupctx_s *ctx = b->brokerctx;

	if (!ctx)
		return;

	b->brokerctx = NULL;
	if (ctx->thread)
	{
		if (ctx->working)
		{
			ctx->next = orphanedbrokerlookups;
			orphanedbrokerlookups = ctx;
			return;
		}
		SDL_WaitThread(ctx->thread, NULL);
	}
	Mem_Free(ctx);
}

extern void Datagram_GenerateGetInfoString(char *out, size_t outsize);
static void QICE_Heartbeat(qice_connection_t *b)
{
	char info[2048];
	struct heartbeatctx_s *ctx = b->heartbeatctx;

	extern cvar_t sv_public, sv_reportheartbeats, sv_heartbeat_interval, net_masters[];

	if (!b->isserver)
		return;	//don't ever heartbeat as a client.

	if (ctx && !ctx->working)
	{	//dns resolution finished.
		//only needs to do master stuff now

		//darkplaces here refers to the master server protocol, rather than the game protocol
		//(specifies that the server responds to infoRequest packets from the master/clients)
		static char *str = "\377\377\377\377heartbeat DarkPlaces\n";
		size_t k;
		SDL_WaitThread(ctx->thread, NULL);

		if (sv_public.value > 0)
		{
			if (sv_reportheartbeats.value)
				for (k = 0; k < ctx->nummasters; k++)
					if (!ctx->master[k].okay)
						Con_Warning("Unable to resolve master %s\n", ctx->master[k].name);
			for (k = 0; k < ctx->numresults; k++)
			{
				if (sv_reportheartbeats.value)
					Con_Printf("Sending heartbeat to %s (%s)\n", ctx->result[k].name, NET_AdrToString(info, sizeof(info), &ctx->result[k].addr));
				ICE_SendUDPPacket(&b->icemodule, &ctx->result[k].addr, (byte*)str, strlen(str));
			}
		}

		Mem_Free(ctx); //don't need it no more
		b->heartbeatctx = ctx = NULL;
	}

	if (realtime < b->heartbeat)
		return;	//not time yet.

	b->heartbeat = realtime+q_max(30,sv_heartbeat_interval.value);

	if (b->broker)
	{	//let the broker know the current serverinfo details, so its available via https://$net_ice_broker/raw/$com_protocolname
		char info[2048];
		int ofs = QICE_PrepareBrokerFrame(ICEMSG_SERVERINFO, -1, info);
		if (sv_public.value > 0)
			Datagram_GenerateGetInfoString(info+ofs, sizeof(info)-ofs);
		else
			info[ofs] = '\0';	//try to hide by sending an empty serverinfo payload.
		QICE_SendBrokerFrame(b, info);
	}

	if (!ctx && sv_public.value > 0)
	{
		size_t k, l = 0;

		for (k = 0; net_masters[k].string; k++)
			l += strlen(net_masters[k].string)+1;
		b->heartbeatctx = ctx = Mem_Alloc(sizeof(*ctx) + l);
		for (k = 0, l = 0; net_masters[k].string; k++)
		{
			if (*net_masters[k].string)
			{
				strcpy(    (ctx->master[ctx->nummasters].name = (char*)(ctx+1)+l), net_masters[k].string);	//copy the names over, just in case there's races
				l += strlen(ctx->master[ctx->nummasters].name)+1;
				ctx->nummasters++;
			}
		}
		ctx->working = true;
		ctx->thread = SDL_CreateThread(DNSLookupThread, "heartbeatdns", ctx);
		if (!ctx->thread)	//bum...
			ctx->working = false;	//just clean it up later.
	}
}

//escape a string so that COM_Parse will give the same string.
//maximum expansion is strlen(string)*2+4 (includes null terminator)
const char *JSON_QuotedString(const char *string, char *buf, int buflen, qboolean omitquotes)
{
	const char *result = buf;
	//strings of the form \"foo" can contain c-style escapes, including for newlines etc.
	//it might be fancy to ALWAYS escape non-ascii chars too, but mneh
	if (!omitquotes)
	{
		*buf++ = '\\';	//prefix so the reader knows its a quoted string.
		*buf++ = '\"';	//opening quote
		buflen -= 4;
	}
	else
		buflen -= 1;
	while(*string && buflen >= 2)
	{
		switch(*string)
		{
		case '\n':
			*buf++ = '\\';
			*buf++ = 'n';
			break;
		case '\r':
			*buf++ = '\\';
			*buf++ = 'r';
			break;
		case '\t':
			*buf++ = '\\';
			*buf++ = 't';
			break;
		case '\'':
			*buf++ = '\\';
			*buf++ = '\'';
			break;
		case '\"':
			*buf++ = '\\';
			*buf++ = '\"';
			break;
		case '\\':
			*buf++ = '\\';
			*buf++ = '\\';
			break;
		case '$':
			*buf++ = '\\';
			*buf++ = '$';
			break;
		default:
			*buf++ = *string++;
			buflen--;
			continue;
		}
		buflen -= 2;
		string++;
	}
	if (!omitquotes)
		*buf++ = '\"';	//closing quote
	*buf++ = 0;
	return result;
}
static void QICE_SendOffer(qice_connection_t *b, int cl, struct icestate_s *ice, const char *type)
{
	char buf[8192];
#if defined(HAVE_JSON) && defined(HAVE_DTLS)
	if (net_ice_usewebrtc.value || !*net_ice_usewebrtc.string)//if (ice->modeflags & ICEF_ALLOW_WEBRTC)
	{
		//okay, now send the sdp (encapsulated in json) to our peer.
		if (iceapi.Get(ice, type, buf, sizeof(buf)))
		{
			char json[8192+256];
			int ofs = QICE_PrepareBrokerFrame(ICEMSG_OFFER, cl, json);

			q_strlcpy(json+ofs, va("{\"type\":\"%s\",\"sdp\":\"", type+3), sizeof(json)-ofs);
			JSON_QuotedString(buf, json+ofs+strlen(json+ofs), sizeof(json)-ofs-strlen(json+ofs)-2, true);
			q_strlcat(json+ofs, "\"}", sizeof(json)-ofs);
			QICE_SendBrokerFrame(b, json);
		}
	}
	else
#endif
	{
		//okay, now send the sdp to our peer.
		int ofs = QICE_PrepareBrokerFrame(ICEMSG_OFFER, cl, buf);
		if (iceapi.Get(ice, type, buf+ofs, sizeof(buf)-ofs))
		{
			QICE_SendBrokerFrame(b, buf);
		}
	}
}
static void QICE_FoundPeer(qice_connection_t *b, const char *peeraddr, int cl, struct icestate_s **ret)
{	//sends offer
	struct icestate_s *ice;
	const char *s;
	unsigned int modeflags = 0;
	if (*ret)
	{
		/* Publish the old peer as absent before its close callback.  A broker
		 * replacement is a valid transition, not a terminal client failure. */
		ice = *ret;
		*ret = NULL;
		if (!b->isserver && b->qsock && QICE_SocketIce(b->qsock) == ice)
			QICE_SetSocketIce(b->qsock, NULL);
		iceapi.Close(ice, false);
	}
#ifdef HAVE_DTLS
	if (net_ice_usewebrtc.value)
		modeflags |= ICEF_ALLOW_WEBRTC;
	else if (!*net_ice_usewebrtc.string)
		modeflags |= ICEF_ALLOW_WEBRTC|ICEF_ALLOW_PLAIN;	//let the peer decide. this means we can use dtls, but not sctp.
	else
		modeflags |= ICEF_ALLOW_PLAIN;
#endif
	if (!b->isserver)
		modeflags |= ICEF_INITIATOR;
	modeflags |= ICEF_ALLOW_PROBE;	//yes, we want to allow probes. don't be ice-lite.
	if (net_ice_allowstun.value)
		modeflags |= ICEF_ALLOW_STUN;	//query stun servers. relatively cheap.
	if (net_ice_allowturn.value)
		modeflags |= ICEF_ALLOW_TURN;	//register with a TURN server if defined.
	if (net_ice_allowmdns.value)
		modeflags |= ICEF_ALLOW_MDNS;	//share lan addresses without sharing lan addesses (queries and responses)
	if (net_ice_relayonly.value)
		modeflags |= ICEF_RELAY_ONLY;	//laggier
	if (net_ice_exchangeprivateips.value)
		modeflags |= ICEF_SHARE_PRIVATE;	//bad
	if (net_ice_debug.value)
		modeflags |= ICEF_VERBOSE;
	if (net_ice_debug.value>=2)
		modeflags |= ICEF_VERBOSE_PROBE;
	ice = *ret = iceapi.Create(&b->icemodule, NULL, b->isserver?((peeraddr&&*peeraddr)?va("%s:%i", peeraddr,cl):NULL):va("/%s", b->gamename), modeflags, b->isserver?ICEP_SERVER:ICEP_CLIENT);
	if (!*ret)
		return;	//some kind of error?!?

	QICE_SetBrokerStunServer(b, ice);

	s = net_ice_servers.string;
	while((s=COM_Parse(s)))
		iceapi.Set(ice, "server", com_token);

	//we're meant to wait until we reach the end of ICE_GATHERING state, but we assume our peer also supports trickle ice so we can just skip straight past that.
	if (!b->isserver)
		QICE_SendOffer(b, cl, ice, "sdpoffer");
}
static void QICE_Refresh(qice_connection_t *b, int cl, struct icestate_s *ice)
{	//sends offer
	char buf[8192];

#if defined(HAVE_JSON) && defined(HAVE_DTLS)
	if (net_ice_usewebrtc.value || !*net_ice_usewebrtc.string)//if (ice->modeflags & ICEF_ALLOW_WEBRTC)
	{
		while (ice && iceapi.GetLCandidateSDP(ice, buf, sizeof(buf)))
		{
			char json[8192+256];
			int ofs = QICE_PrepareBrokerFrame(ICEMSG_CANDIDATE, cl, json);

			q_strlcpy(json+ofs, "{\"candidate\":\"", sizeof(json)-ofs);
			JSON_QuotedString(buf+2, json+ofs+strlen(json+ofs), sizeof(json)-ofs-strlen(json+ofs)-2, true);
			q_strlcat(json+ofs, "\",\"sdpMid\":\"0\",\"sdpMLineIndex\":0}", sizeof(json)-ofs);
			QICE_SendBrokerFrame(b, json);
		}
	}
	else
#endif
	{
		int ofs = QICE_PrepareBrokerFrame(ICEMSG_CANDIDATE, cl, buf);
		while (ice && iceapi.GetLCandidateSDP(ice, buf+ofs, sizeof(buf)-ofs))
		{
			QICE_SendBrokerFrame(b, buf);
		}
	}
}
static void Buf_ReadString(const char **data, const char *end, char *out, size_t outsize)
{
	const char *in = *data;
	char c;
	outsize--;	//count the null early.
	while (in < end)
	{
		c = *in++;
		if (!c)
			break;
		if (outsize)
		{
			outsize--;
			*out++ = c;
		}
	}
	*out = 0;
	*data = in;
}
static qboolean QICE_UpdateBroker(qice_connection_t *b)
{
#ifdef HAVE_JSON
	json_t *json;
#endif
	int len, cl;
	const char *data;
	char msgbuf[8192];
	qboolean result = false;
	netadr_t brokeraddr;
	struct brokerlookupctx_s *brokerctx;

	QICE_CleanupBrokerLookups();

	if (b->isserver && !*sv_port_rtc.string)
	{
		if (b->broker)
		{
			b->broker->Close(b->broker);
			b->broker = NULL;
		}
		b->error = false;
		*b->brokername = 0;
		b->brokerport = 0;
		b->brokeraddr_valid = false;
		QICE_FreeBrokerLookup(b);
		QICE_Heartbeat(b);
		return false;
	}

	if (!b->broker && !b->error)
	{
		const char *roomname = b->gamename;
		char *url;
		char *c;

		// Re-read the broker cvar in case it was changed by autoexec.cfg
		// after the initial QICE_Setup during NET_Init
		{
			const char *broker = net_ice_broker.string;
			if (!strncmp(broker, "ws://", 5))
				{ broker += 5; b->issecure = false; }
			else if (!strncmp(broker, "wss://", 6))
				{ broker += 6; b->issecure = true; }
			q_strlcpy(b->brokername, broker, sizeof(b->brokername));
			c = strchr(b->brokername, ':');
			if (c)
				{ b->brokerport = atoi(c+1); *c = 0; }
			else
				b->brokerport = PORT_ICEBROKER;
		}

		if (!*b->brokername)
		{
			QICE_FreeBrokerLookup(b);
			b->brokeraddr_valid = false;
			if (b->isserver)
				QICE_Heartbeat(b);
			return false;
		}

		if (b->reconnecttimeout > realtime)
		{
			if (b->isserver)
				QICE_Heartbeat(b);
			return false;
		}

		if (b->brokerctx)
		{
			brokerctx = b->brokerctx;
			if (strcmp(brokerctx->brokername, b->brokername) || brokerctx->brokerport != b->brokerport)
			{
				QICE_FreeBrokerLookup(b);
				b->brokeraddr_valid = false;
				if (b->isserver)
					QICE_Heartbeat(b);
				return false;
			}
			if (brokerctx->working)
			{
				if (b->isserver)
					QICE_Heartbeat(b);
				return false;
			}

			SDL_WaitThread(brokerctx->thread, NULL);
			b->brokerctx = NULL;
			if (!brokerctx->okay)
			{
				Mem_Free(brokerctx);
				b->brokeraddr_valid = false;
				b->reconnecttimeout = realtime + 30;
				Con_Printf("rtc broker resolve for %s failed%s\n", b->brokername, b->isserver?" (retry: 30 secs)":"");
				return false;
			}
			brokeraddr = brokerctx->addr;
			b->brokeraddr = brokeraddr;
			b->brokeraddr_valid = true;
			Mem_Free(brokerctx);
		}
		else
		{
			b->brokeraddr_valid = false;
			brokerctx = Mem_Alloc(sizeof(*brokerctx));
			q_strlcpy(brokerctx->brokername, b->brokername, sizeof(brokerctx->brokername));
			brokerctx->brokerport = b->brokerport;
			brokerctx->working = true;
			brokerctx->thread = SDL_CreateThread(BrokerLookupThread, "brokerdns", brokerctx);
			if (!brokerctx->thread)
			{
				Mem_Free(brokerctx);
				b->reconnecttimeout = realtime + 30;
				Con_Printf("rtc broker resolve thread for %s failed%s\n", b->brokername, b->isserver?" (retry: 30 secs)":"");
				return false;
			}
			b->brokerctx = brokerctx;
			if (b->isserver)
				QICE_Heartbeat(b);
			return false;
		}

		if (b->isserver && *sv_port_rtc.string && strcmp(sv_port_rtc.string,"/"))
		{
			roomname = sv_port_rtc.string;
			if (*roomname == '/')
				roomname++;
		}

		if (!b->isserver)
			Con_SafePrintf("Connecting to rtc%s://%s:%i/%s...\n",
				b->issecure?"s":"",	//secure or not.
				b->brokername,	//broker ip/name,
				b->brokerport,
				roomname);	//server name

		COM_Parse(com_protocolname.string);
		url = va("ws%s:%s://%s/%s/%s",
			b->issecure?"s":"",	//secure or not.
			b->isserver?"rtc_host":"rtc_client",	//whether we're hosting or connecting
			b->brokername,	//broker ip/name,
			com_token,		//protocol/game
			roomname);	//server name
		b->broker = ICE_OpenTCPResolved(url, b->brokerport, true, &brokeraddr);

		if (!b->broker)
		{
			b->brokeraddr_valid = false;
			b->reconnecttimeout = realtime + 30;
			Con_Printf("rtc broker connection to %s failed%s\n", b->brokername, b->isserver?" (retry: 30 secs)":"");
			return false;
		}


		b->heartbeat = realtime;
	}
	if (b->error)
	{
handleerror:
		if (b->broker)
			b->broker->Close(b->broker);
		b->broker = NULL;
		QICE_FreeBrokerLookup(b);
		b->brokeraddr_valid = false;
		b->reconnecttimeout = realtime + 30;

		/*for (cl = 0; cl < b->numclients; cl++)
		{
			if (b->clients[cl].ice)
				iceapi.Close(b->clients[cl].ice, false);
			if (b->clients[cl].qsock)
				QICE_SetSocketIce(b->clients[cl].qsock, NULL);	//remove that dead link too.
			b->clients[cl].ice = NULL;
		}
		if (b->ice)
			iceapi.Close(b->ice, false);
		if (b->qsock)
			QICE_SetSocketIce(b->qsock, NULL);	//remove that dead link too.
		b->ice = NULL;*/

		if (b->error != 1 || !b->isserver)
			return false;	//permanant error...
		b->error = false;
		return false;
	}

	//keep checking for new candidate info.
	if (b->isserver)
	{
		for (cl = 0; cl < b->numclients; cl++)
			if (b->clients[cl].ice)
				QICE_Refresh(b, cl, b->clients[cl].ice);
		QICE_Heartbeat(b);
	}
	else
	{
		if (b->ice)
			QICE_Refresh(b, b->serverid, b->ice);
	}

	len = b->broker->ReadBytes(b->broker, msgbuf, sizeof(msgbuf)-1);
	if (!len)
		return false;	//nothing new
	if (len < 0)
	{
//		if (!b->error)
			Con_Printf("rtc broker connection to %s failed%s\n", b->brokername, b->isserver?" (retry: 30 secs)":"");
		b->error = true;
		goto handleerror;
	}
	msgbuf[len] = 0;

	if (len < 3)
		Con_Printf("rtc runt (%s)\n", b->brokername);
	else
	{
		cl = (short)(msgbuf[1] | (msgbuf[2]<<8));
		data = msgbuf+3;

		switch(msgbuf[0])
		{
		case ICEMSG_PEERLOST:	//the broker lost its connection to our peer...
			if (cl == -1)
			{
				b->error = true;
				if (net_ice_debug.value)
					Con_Printf(S_COLOR_GRAY"[%s]: Broker host lost connection: %s\n", ICE_GetConnName(b->ice), *data?data:"<NO REASON>");
			}
			else if (cl >= 0 && cl < b->numclients)
			{
				if (net_ice_debug.value)
					Con_Printf(S_COLOR_GRAY"[%s]: Broker client lost connection: %s\n", ICE_GetConnName(b->clients[cl].ice), *data?data:"<NO REASON>");
				if (b->clients[cl].ice)
					iceapi.Close(b->clients[cl].ice, true);
				b->clients[cl].ice = NULL;
				b->clients[cl].isnew = false;	//just in case...
			}
			break;
		case ICEMSG_NAMEINUSE:
			Con_Printf("Unable to listen on /%s - name already taken\n", b->gamename);
			b->error = true;	//try again later.
			break;
		case ICEMSG_GREETING:	//reports the trailing url we're 'listening' on. anyone else using that url will connect to us.
			data = strchr(data, '/');
			if (data++)
				q_strlcpy(b->gamename, data, sizeof(b->gamename));
			Con_Printf("Publicly listening on /%s\n", b->gamename);
			break;
		case ICEMSG_NEWPEER:	//relay connection established with a new peer
			//note that the server ought to wait for an offer from the client before replying with any ice state, but it doesn't really matter for our use-case.
			{
				char peer[MAX_QPATH];
				char relay[MAX_QPATH];
				const char *s;
				Buf_ReadString(&data, msgbuf+len, peer, sizeof(peer));
				Buf_ReadString(&data, msgbuf+len, relay, sizeof(relay));

				if (b->isserver)
				{
//					Con_DPrintf("Client connecting: %s\n", data);
					if (cl < 1024 && cl >= b->numclients)
					{	//looks like a new one... but don't waste memory if too many slots were used...
						void *n = realloc(b->clients, sizeof(b->clients[0])*(cl+1));
						if (!n)
							break;
						b->clients = n;
						memset(b->clients+b->numclients, 0, sizeof(b->clients[0]) * ((cl+1) - b->numclients));
						b->numclients = cl+1;
					}
					if (cl >= 0 && cl < b->numclients)
					{
						if (b->clients[cl].ice)
						{	//close any existing state (stale connection or probe)
							iceapi.Close(b->clients[cl].ice, true);
							b->clients[cl].ice = NULL;
						}
						QICE_FoundPeer(b, *peer?peer:NULL, cl, &b->clients[cl].ice);
						b->clients[cl].isnew = true;
						for (s = relay; (s=COM_Parse(s)); )
							iceapi.Set(b->clients[cl].ice, "server", com_token);
						if (net_ice_debug.value)
							Con_Printf(S_COLOR_GRAY"[%s]: New client spotted...\n", ICE_GetConnName(b->clients[cl].ice));
					}
					else if (net_ice_debug.value)
						Con_Printf(S_COLOR_GRAY"[%s]: New client spotted, but index is unusable\n", ICE_GetConnName(NULL));
				}
				else
				{
					//Con_DPrintf("Server found: %s\n", data);
					QICE_FoundPeer(b, *peer?peer:NULL, cl, &b->ice);
					b->serverid = cl;
					for (s = relay; (s=COM_Parse(s)); )
						iceapi.Set(b->ice, "server", com_token);
					if (net_ice_debug.value)
						Con_Printf(S_COLOR_GRAY"[%s]: Server identified\n", ICE_GetConnName(b->ice));
				}
				result = true;
			}
			break;
		case ICEMSG_OFFER:	//we received an offer from a client
#ifdef HAVE_JSON
			json = JSON_Parse(data);
			if (json)
				//should probably also verify the type.
				data = JSON_GetString(json, "sdp", com_token,sizeof(com_token), NULL);
#endif
			if (b->isserver)
			{
				if (cl >= 0 && cl < b->numclients && b->clients[cl].ice)
				{
					if (net_ice_debug.value)
						Con_Printf(S_COLOR_GRAY"[%s]: Got offer:\n%s\n", ICE_GetConnName(b->clients[cl].ice), data);
					iceapi.Set(b->clients[cl].ice, "sdpoffer", data);
					iceapi.Set(b->clients[cl].ice, "state", STRINGIFY(ICE_CONNECTING));

					QICE_SendOffer(b, cl, b->clients[cl].ice, "sdpanswer");
				}
				else if (net_ice_debug.value)
					Con_Printf(S_COLOR_GRAY"[%s]: Got bad offer/answer:\n%s\n", ICE_GetConnName(b->clients[cl].ice), data);
			}
			else
			{
				Con_Printf ("Server contacted...\n");
				if (b->ice)
				{
					if (net_ice_debug.value)
						Con_Printf(S_COLOR_GRAY"[%s]: Got answer:\n%s\n", ICE_GetConnName(b->ice), data);
					iceapi.Set(b->ice, "sdpanswer", data);
					iceapi.Set(b->ice, "state", STRINGIFY(ICE_CONNECTING));
				}
				else if (net_ice_debug.value)
					Con_Printf(S_COLOR_GRAY"[%s]: Got bad offer/answer:\n%s\n", ICE_GetConnName(b->ice), data);
			}
#ifdef HAVE_JSON
			JSON_Destroy(json);
#endif
			break;
		case ICEMSG_CANDIDATE:
#ifdef HAVE_JSON
			json = JSON_Parse(data);
			if (json)
			{
				data = com_token;
				com_token[0]='a';
				com_token[1]='=';
				com_token[2]=0;
				JSON_GetString(json, "candidate", com_token+2,sizeof(com_token)-2, NULL);
			}
#endif
//			Con_Printf("Candidate update: %s\n", data);
			if (b->isserver)
			{
				if (cl >= 0 && cl < b->numclients && b->clients[cl].ice)
				{
					if (net_ice_debug.value)
						Con_Printf(S_COLOR_GRAY"[%s]: Got candidate:\n%s\n", ICE_GetConnName(b->clients[cl].ice), data);
					iceapi.Set(b->clients[cl].ice, "sdp", data);
				}
			}
			else
			{
				if (b->ice)
				{
					if (net_ice_debug.value)
						Con_Printf(S_COLOR_GRAY"[%s]: Got candidate:\n%s\n", ICE_GetConnName(b->ice), data);
					iceapi.Set(b->ice, "sdp", data);
				}
			}
#ifdef HAVE_JSON
			JSON_Destroy(json);
#endif
			break;
		case ICEMSG_SERVERDETAILS_REQUEST:
			if (b->isserver)
			{
				char buf[8192];
				int ofs = QICE_PrepareBrokerFrame(ICEMSG_SERVERDETAILS_RESPONSE, cl, buf);
				int i, j;

				//server info (same as getstatus infostring)
				Datagram_GenerateGetInfoString(buf+ofs, sizeof(buf)-ofs);
				ofs += strlen(buf+ofs);

				//player list: one line per player "\nfrags ping colors "name" address"
				for (i = 0; i < svs.maxclients; i++)
				{
					if (svs.clients[i].active)
					{
						float total = 0;
						const char *addr;

						for (j = 0; j < NUM_PING_TIMES; j++)
							total += svs.clients[i].ping_times[j];
						total /= NUM_PING_TIMES;
						total *= 1000;	//ms

						if (svs.clients[i].netconnection)
							addr = NET_QSocketGetMaskedAddressString(svs.clients[i].netconnection);
						else
							addr = "botclient";

						ofs += q_snprintf(buf+ofs, sizeof(buf)-ofs, "\n%i %i %i_%i \"%s\" %s",
							svs.clients[i].old_frags, (int)total,
							svs.clients[i].colors & 15, svs.clients[i].colors >> 4,
							svs.clients[i].name, addr);
					}
				}

				//rules: "\nrule=value" for each CVAR_SERVERINFO cvar
				{
					cvar_t *var;
					ofs += q_snprintf(buf+ofs, sizeof(buf)-ofs, "\n");
					for (var = Cvar_FindVarAfter("", CVAR_SERVERINFO); var; var = Cvar_FindVarAfter(var->name, CVAR_SERVERINFO))
					{
						ofs += q_snprintf(buf+ofs, sizeof(buf)-ofs, "\\%s\\%s", var->name, var->string);
						if (ofs >= (int)sizeof(buf) - 64)
							break;
					}
				}

				QICE_SendBrokerFrame(b, buf);
			}
			break;
		case ICEMSG_PROBE:
			//lightweight ICE probe for latency measurement — no DTLS/SCTP, short timeout
			if (b->isserver)
			{
				char peer[MAX_QPATH];
				char relay[MAX_QPATH];
				const char *s;
				struct icestate_s *probe;
				unsigned int modeflags = ICEF_ALLOW_PROBE | ICEF_ALLOW_STUN | ICEF_ALLOW_WEBRTC;

				Buf_ReadString(&data, msgbuf+len, peer, sizeof(peer));
				Buf_ReadString(&data, msgbuf+len, relay, sizeof(relay));

				if (net_ice_allowmdns.value)
					modeflags |= ICEF_ALLOW_MDNS;
				if (net_ice_debug.value)
					modeflags |= ICEF_VERBOSE;
				if (net_ice_debug.value >= 2)
					modeflags |= ICEF_VERBOSE_PROBE;

				if (cl < 1024 && cl >= b->numclients)
				{
					void *n = realloc(b->clients, sizeof(b->clients[0])*(cl+1));
					if (!n)
						break;
					b->clients = n;
					memset(b->clients+b->numclients, 0, sizeof(b->clients[0]) * ((cl+1) - b->numclients));
					b->numclients = cl+1;
				}
				if (cl >= 0 && cl < b->numclients)
				{
					if (b->clients[cl].ice)
					{
						iceapi.Close(b->clients[cl].ice, true);
						b->clients[cl].ice = NULL;
					}

					probe = iceapi.Create(&b->icemodule, NULL,
						(*peer) ? va("%s:%i", peer, cl) : NULL,
						modeflags, ICEP_SERVER);
					if (probe)
					{
						b->clients[cl].ice = probe;
						b->clients[cl].isnew = false;	//not a real connection

						//set to GATHERING so ICE_Tick doesn't destroy it before the offer arrives
						iceapi.Set(probe, "state", STRINGIFY(ICE_GATHERING));
						iceapi.Set(probe, "brokerless", "1");	//self-cleanup via icetimeout
						iceapi.Set(probe, "timeout", "5000");	//probes are short-lived

						//add STUN servers
						QICE_SetBrokerStunServer(b, probe);
						for (s = relay; (s=COM_Parse(s)); )
							iceapi.Set(probe, "server", com_token);
						s = net_ice_servers.string;
						while ((s=COM_Parse(s)))
							iceapi.Set(probe, "server", com_token);

						if (net_ice_debug.value)
							Con_Printf(S_COLOR_GRAY"[%s]: ICE probe from %s\n", ICE_GetConnName(probe), peer);
					}
				}
				result = true;
			}
			break;
		default:
			if (net_ice_debug.value)
				Con_Printf(S_COLOR_GRAY"[%s]: Broker send unknown packet: %i\n", ICE_GetConnName(b->ice), msgbuf[0]);
			break;
		}
	}

	b->broker->ReadBytes(b->broker, NULL, 0);	//should flush any pending outogoing data.
	return result;
}
static qboolean qice_listening;
static qice_connection_t *qice_hostcon;
static struct icestate_s *qice_broker_active;
static qboolean qice_broker_authenticated;

static void QICE_Closed(struct icemodule_s *module, struct icestate_s *ice)
{	//callback from ice to avoid hanging pointers.
	qice_connection_t *b = (qice_connection_t*)module;
	qsocket_t *s;
	int i;

	struct qice_userstate_s *u = ICE_GetUserPtr(ice);
	if (u)
	{
		ICE_SetUserPtr(ice, NULL);
		if (u->text)
			Mem_Free(u->text);
		Mem_Free(u);
	}


	for (s = net_activeSockets; s; s = s->next)
	{
		if (s->driverdata == b)
		if (QICE_SocketIce(s) == ice)
		{
			QICE_DiscardQueuedServerMessages(s);
			QICE_SetSocketIce(s, NULL);	//detach from ice state as its no longer valid.
			if (b == qice_hostcon)
				QICE_RequestServerRetirement(s, "ICE state closed");
			else
			{
				extern char m_return_reason[32];
				b->error = true;
				q_strlcpy(m_return_reason, "ICE connection closed", sizeof(m_return_reason));
				QICE_ResetClientMessageQueue(b);
			}
		}
	}

	//and any broker state.
	if (b->ice == ice)
		b->ice = NULL;
	if (qice_broker_active == ice)
	{
		qice_broker_active = NULL;
		qice_broker_authenticated = false;
	}
	for (i = 0; i < b->numclients; i++)
	{
		if (b->clients[i].ice == ice)
			b->clients[i].ice = NULL;
	}
}

static void QICE_SendInitial(struct icemodule_s *module, struct icestate_s *ice)
{
	qice_connection_t *b = (qice_connection_t*)module;

	b->dohandshake = Sys_DoubleTime();	//reset handshake timer so we try NOW.
}

#ifdef HAVE_DTLS
static void QICE_FreeLocalCred(struct dtlslocalcred_s *cred)
{
	if (cred->cert)
		free(cred->cert);
	if (cred->key)
		memset(cred->key, 0, cred->keysize);
	if (cred->key)
		free(cred->key);
	if (cred->rawcert)
		free(cred->rawcert);
	if (cred->rawkey)
		memset(cred->rawkey, 0, cred->rawkeysize);
	if (cred->rawkey)
		free(cred->rawkey);
	cred->cert = NULL;
	cred->certsize = 0;
	cred->key = NULL;
	cred->keysize = 0;
	cred->rawcert = NULL;
	cred->rawcertsize = 0;
	cred->rawkey = NULL;
	cred->rawkeysize = 0;
}

static qboolean QICE_ReadFileBuffer(const char *path, void **out, size_t *outsize)
{
	qfilesize_t sz;
	int fd;
	qbyte *buf;

	sz = Sys_FileOpenRead(path, &fd);
	if (sz < 0 || sz > INT_MAX)
		return false;

	buf = malloc((size_t)sz + 1);
	if (!buf)
	{
		Sys_FileClose(fd);
		return false;
	}

	if (Sys_FileRead(fd, buf, (int)sz) != sz)
	{
		Sys_FileClose(fd);
		free(buf);
		return false;
	}
	Sys_FileClose(fd);
	buf[sz] = 0;

	*out = buf;
	*outsize = sz;
	return true;
}

static qboolean QICE_CopyBuffer(void **out, size_t *outsize, const void *src, size_t srcsize)
{
	qbyte *buf = malloc(srcsize + 1);

	if (!buf)
		return false;

	memcpy(buf, src, srcsize);
	buf[srcsize] = 0;

	*out = buf;
	*outsize = srcsize;
	return true;
}

static qboolean QICE_LoadCerts(struct dtlslocalcred_s *cred, char *priv, char *cert)
{
	QICE_FreeLocalCred(cred);

	//private key is most likely to need special permissions to read, so fail on that one first.
	if (!QICE_ReadFileBuffer(priv, &cred->rawkey, &cred->rawkeysize))
		return false;

	if (!QICE_ReadFileBuffer(cert, &cred->rawcert, &cred->rawcertsize))
	{
		QICE_FreeLocalCred(cred);
		return false;
	}

	if (!QICE_CopyBuffer(&cred->key, &cred->keysize, cred->rawkey, cred->rawkeysize) ||
		!QICE_CopyBuffer(&cred->cert, &cred->certsize, cred->rawcert, cred->rawcertsize))
	{
		QICE_FreeLocalCred(cred);
		return false;
	}

	ICE_DePEM(cred);
	return true;
}

/* Server DTLS identity is either explicitly supplied, read from the system
 * certificate paths, or generated once for this process.  Never write it to
 * com_basedir: that may be a read-only install or a shared runtime archive. */
static struct dtlslocalcred_s qice_server_cred;
static qboolean qice_server_cred_cached;

static qboolean QICE_CopyLocalCred(struct dtlslocalcred_s *out, const struct dtlslocalcred_s *src)
{
	qboolean copied;
	QICE_FreeLocalCred(out);
	if (!src->cert || !src->key || !src->certsize || !src->keysize)
		return false;
	copied = QICE_CopyBuffer(&out->cert, &out->certsize, src->cert, src->certsize) &&
		QICE_CopyBuffer(&out->key, &out->keysize, src->key, src->keysize);
	if (!copied)
		QICE_FreeLocalCred(out);
	return copied;
}

static qboolean QICE_LoadServerCredentials(const dtlsfuncs_t *funcs, struct dtlslocalcred_s *cred)
{
	int keyarg, certarg;
	const char *hostname = "localhost";

	if (qice_server_cred_cached)
		return QICE_CopyLocalCred(cred, &qice_server_cred);

	keyarg = COM_CheckParm("-privkey") + 1;
	certarg = COM_CheckParm("-pubkey") + 1;
	if (keyarg > 1 && keyarg < com_argc && certarg > 1 && certarg < com_argc)
		QICE_LoadCerts(cred, com_argv[keyarg], com_argv[certarg]);
#if defined(__unix__) || defined(__POSIX__)
	if (!cred->cert && !QICE_LoadCerts(cred, "/etc/ssl/private/privkey.pem", "/etc/ssl/certs/fullchain.pem"))
		QICE_FreeLocalCred(cred);
#endif
	if (!cred->cert)
	{
		int hostarg = COM_CheckParm("-certhost") + 1;
		if (hostarg > 1 && hostarg < com_argc)
			hostname = com_argv[hostarg];
		if (!funcs || !funcs->GenTempCertificate || !funcs->GenTempCertificate(hostname, cred))
			return false;
	}
	if (!QICE_CopyLocalCred(&qice_server_cred, cred))
	{
		QICE_FreeLocalCred(cred);
		return false;
	}
	qice_server_cred_cached = true;
	return true;
}
#endif

static struct icesocket_s *shared_game_socket4;	//datagram driver's shared IPv4 UDP socket
static struct icesocket_s *shared_game_socket6;	//datagram driver's shared IPv6 UDP socket

static qice_connection_t *QICE_Setup(const char *address, qboolean isserver)
{	//[rtc://][broker[:port]]/[roomname]
	qice_connection_t *newcon;
	const char *path;
	char *c;
	int i;

	struct
	{
		const char *name;
		qboolean secure;
	} schemes[] =
	{
		{"ws://", false},	//brokers are connected to via websockets so these two are technically more correct... but ambiguous
		{"wss://", true},
		{"ice://", false},	//individual servers might use these schemes. generally implies to skip dtls...
		{"ices://", true},
		{"rtc://", false},	//supposedly implies using dtls... but with no wss. dumb.
		{"rtcs://", true},
		{"tcp://", false},	//weirdness... lowest common denominator...
		{"tls://", true},
		{"http://", false},	//we can query the broker for servers over http... prolly shouldn't be used.
		{"https://", true},

#ifdef HAVE_TLS
		{"", true},	//otherwise assume wss.
#else
		{"", false},	//otherwise assume unencrypted. FIXME!
#endif
	};

	newcon = Mem_Alloc(sizeof(*newcon));

	if (address)
	{
		if (*address=='/' || !*address)
		{	//had a leading slash. use the default broker for it.
			q_strlcpy(newcon->brokername, "", sizeof(newcon->brokername));
			newcon->issecure = true;

			if (*address == '/')
				address++;
			q_strlcpy(newcon->gamename, address, sizeof(newcon->gamename));	//so we know what to tell the broker.
		}
		else
		{	//explicit broker was specified.
			for (i = 0; i < countof(schemes)-1; i++)
			{
				if (!strncmp(address, schemes[i].name, strlen(schemes[i].name)))
					break;
			}

			address += strlen(schemes[i].name);
			q_strlcpy(newcon->brokername, address, sizeof(newcon->brokername));
			newcon->issecure = schemes[i].secure;

			path = strchr(address, '/');
			if (path && path-address < sizeof(newcon->brokername))
			{	//truncate room from broker (broker may end up 0-bytes long).
				newcon->brokername[path-address] = 0;
				q_strlcpy(newcon->gamename, path+1, sizeof(newcon->gamename));	//so we know what to tell the broker.
			}
			else
				*newcon->gamename = 0;
		}

		if (!*newcon->brokername)
		{	//broker name was omitted. use the default.
			q_strlcpy(newcon->brokername, net_ice_broker.string, sizeof(newcon->brokername));	//fallback.
			for (i = 0; i < countof(schemes)-1; i++)
			{
				if (!strncmp(net_ice_broker.string, schemes[i].name, strlen(schemes[i].name)))
					break;
			}
			q_strlcpy(newcon->brokername, net_ice_broker.string+strlen(schemes[i].name), sizeof(newcon->brokername));
			newcon->issecure = schemes[i].secure;
		}

		c = strchr(newcon->brokername, ':');
		if (c)
		{
			newcon->brokerport = atoi(c+1);
			*c = 0;
		}
		else
			newcon->brokerport = PORT_ICEBROKER;
	}
	else
	{
		//not doing the broker thing... just directly using ice.
		*newcon->brokername = 0;
		newcon->brokerport = 0;
		*newcon->gamename = 0;
	}

	newcon->broker = NULL;
	newcon->reconnecttimeout = realtime;
	newcon->heartbeat = realtime;
	newcon->isserver = isserver;
	newcon->dohandshake = Sys_DoubleTime();
	newcon->icemodule.SendInitial = QICE_SendInitial;
	newcon->icemodule.ClosedState = QICE_Closed;

	if (isserver && (shared_game_socket4 || shared_game_socket6))
	{	//use send-only wrappers for the shared game sockets; Datagram owns recv and forwards ICE packets.
		if (shared_game_socket4)
		{
			struct icesocket_s *wrap = ICE_WrapExistingSocketSendOnly(shared_game_socket4->sock, shared_game_socket4->af);
			if (wrap)
				newcon->icemodule.conn[0] = wrap;
		}
		if (shared_game_socket6)
		{
			struct icesocket_s *wrap = ICE_WrapExistingSocketSendOnly(shared_game_socket6->sock, shared_game_socket6->af);
			if (wrap)
				newcon->icemodule.conn[1] = wrap;
		}
		newcon->icemodule.srflx_port = net_hostport;	// use game port for srflx candidates (NAT may remap)
		ICE_SetupModule(&newcon->icemodule,
			0,						// shared sockets handle UDP; 0 = don't open another
			net_hostport);			// TCP/WebSocket on game port
	}
	else
	{
		if (isserver)
			newcon->icemodule.srflx_port = net_hostport;	// use game port for srflx candidates (NAT may remap)
		ICE_SetupModule(&newcon->icemodule,
			0,						// ephemeral — Datagram shares its socket via NQICE_ShareGameSocket
			isserver ? net_hostport : 0);		// TCP/WebSocket on game port
	}

#ifdef HAVE_DTLS
	if (isserver)
	{
		qbyte digest[DIGEST_MAXSIZE];
		struct dtlslocalcred_s cred = {NULL,0,NULL,0};
		newcon->icemodule.dtlsfuncs = ICE_DTLS_InitServer();	//we want to support clients using dtls...

		if (newcon->icemodule.dtlsfuncs)
		{
			if (QICE_LoadServerCredentials(newcon->icemodule.dtlsfuncs, &cred))
			{
				/* `dtls://host:port?fp=foo` uses URL-safe base64. */
				Base64_EncodeBlockURI(digest, CalcHash(&hash_sha2_256, digest, sizeof(digest), cred.cert, cred.certsize), fingerprint, sizeof(fingerprint));
				if (!newcon->icemodule.dtlsfuncs->SetCredentials(&cred))
					Con_Printf(CON_WARNING"Unable to install DTLS server credentials.\n");
				QICE_FreeLocalCred(&cred);
			}
			else
				Con_Printf(CON_WARNING"Unable to obtain ephemeral DTLS server credentials.\n");
		}
		else
			Con_Printf(CON_WARNING"DTLS unavailable - GnuTLS library failed to load.\n");
	}
#endif

	return newcon;
}


//public functions, to make qss happy.
#include "../net_defs.h"

/* Broker control shares the existing server ICE/DTLS peer.  The authentication
 * window is set only while ICE_Main is delivering successfully decrypted data,
 * so no second DTLS listener or borrowed-socket reference survives Unshare. */

bool BrokerDTLS_IsAuthenticated(void)
{
	return qice_broker_authenticated && qice_broker_active != NULL;
}

int BrokerDTLS_Send(const void *data, int len)
{
	if (!BrokerDTLS_IsAuthenticated() || len < 0)
		return -1;
	return iceapi.SendPacket(qice_broker_active, data, (size_t)len) == NETERR_SENT ? 0 : -1;
}

#ifdef SUPPORT_ICE
//Adapted for QSS-M: handles ice_offer/ice_ccand connectionless packets from a broker on the game UDP port.
//The broker sends these so browsers can establish WebRTC DataChannels directly to the server.

static void Z_StrCat(char **dest, const char *src)
{	//append src to a Z_Malloc'd string, reallocating as needed
	size_t oldlen = *dest ? strlen(*dest) : 0;
	size_t addlen = strlen(src);
	char *n = (char *)Mem_Alloc(oldlen + addlen + 1);
	if (*dest)
	{
		memcpy(n, *dest, oldlen);
		Mem_Free(*dest);
	}
	memcpy(n + oldlen, src, addlen + 1);
	*dest = n;
}

//per-peer candidate trickle state (keyed by broker id), since icestate_s.u is not available
#define MAX_ICE_PEERS 16
static struct
{
	char brokerid[64];
	char *text;
	unsigned int inseq;
	unsigned int outseq;
	double timeout;
} ice_peer_state[MAX_ICE_PEERS];

static int ICE_FindOrAllocPeer(const char *brokerid, qboolean alloc)
{
	int i, oldest = 0;
	for (i = 0; i < MAX_ICE_PEERS; i++)
	{
		if (!strcmp(ice_peer_state[i].brokerid, brokerid))
			return i;
		if (ice_peer_state[i].timeout < ice_peer_state[oldest].timeout)
			oldest = i;
	}
	if (!alloc)
		return -1;
	//reuse oldest slot
	i = oldest;
	if (ice_peer_state[i].text)
	Mem_Free(ice_peer_state[i].text);
	memset(&ice_peer_state[i], 0, sizeof(ice_peer_state[i]));
	q_strlcpy(ice_peer_state[i].brokerid, brokerid, sizeof(ice_peer_state[i].brokerid));
	ice_peer_state[i].timeout = Sys_DoubleTime() + 30;
	return i;
}

#ifdef ALLOW_UNSOLICITED_ICE

unsigned int ICE_GetICEFlags(qboolean isinitiator)
{
	unsigned int modeflags = 0;

#ifdef HAVE_DTLS
	if (net_ice_usewebrtc.value)
		modeflags |= ICEF_ALLOW_WEBRTC;
	else if (!*net_ice_usewebrtc.string)
		modeflags |= ICEF_ALLOW_WEBRTC|ICEF_ALLOW_PLAIN;	//let the peer decide. this means we can use dtls, but not sctp.
	else
		modeflags |= ICEF_ALLOW_PLAIN;
#endif
	if (isinitiator)
		modeflags |= ICEF_INITIATOR;
	modeflags |= ICEF_ALLOW_PROBE;	//yes, we want to allow probes. don't be ice-lite.
	if (net_ice_allowstun.value)
		modeflags |= ICEF_ALLOW_STUN;	//query stun servers. relatively cheap.
	if (net_ice_allowturn.value)
		modeflags |= ICEF_ALLOW_TURN;	//register with a TURN server if defined.
	if (net_ice_allowmdns.value)
		modeflags |= ICEF_ALLOW_MDNS;	//share lan addresses without sharing lan addesses (queries and responses)
	if (net_ice_relayonly.value)
		modeflags |= ICEF_RELAY_ONLY;	//laggier
	if (net_ice_exchangeprivateips.value)
		modeflags |= ICEF_SHARE_PRIVATE;	//bad
	if (net_ice_debug.value)
		modeflags |= ICEF_VERBOSE;
	if (net_ice_debug.value>=2)
		modeflags |= ICEF_VERBOSE_PROBE;
	return modeflags;
}
#endif

void SVC_ICE_Offer(const char *clientaddr, const char *brokerid, const char *sdpdata, const char *brokeraddr, ice_udp_send_t sendpacket)
{	//handles an 'ice_offer' udp message from a broker
	struct icestate_s *ice;
	const char *sdp, *s;
	char buf[1400];
	int sz, pi;
#ifdef HAVE_JSON
	json_t *json;
#endif
	Con_DPrintf("ICE: ice_offer from %s broker_id=%s dtls=%i\n", clientaddr, brokerid, BrokerDTLS_IsAuthenticated());
#ifdef HAVE_DTLS
	if (!BrokerDTLS_IsAuthenticated())
	{
		Con_DPrintf("ICE: ice_offer rejected - not DTLS authenticated\n");
		return;
	}
#endif
	if (!qice_hostcon)
	{
		Con_DPrintf("ICE: no host connection (sv_port_rtc not set?)\n");
		return;
	}

	pi = ICE_FindOrAllocPeer(brokerid, true);
	if (pi < 0)
		return;
	ice_peer_state[pi].timeout = Sys_DoubleTime() + 30;

	//close any stale ICE state for this broker_id (e.g. from a previous offer/retry)
	ice = iceapi.Find(&qice_hostcon->icemodule, brokerid);
	if (ice)
		iceapi.Close(ice, true);

	ice = iceapi.Find(&qice_hostcon->icemodule, brokerid);
	Con_DPrintf(CON_WARNING"ICE: %soffer [%s] for %s\n", ice?"dupe ":"", brokerid, clientaddr);
	if (ice)
		return;

	ice = iceapi.Create(&qice_hostcon->icemodule, brokerid, clientaddr, ICE_GetICEFlags(false), ICEP_SERVER);
	if (!ice)
	{
		Con_Printf("ICE: iceapi.Create failed for broker_id=%s\n", brokerid);
		return;
	}
	iceapi.Set(ice, "brokerless", "1");	//not managed by WebSocket broker — clean up on failure/timeout
	iceapi.Set(ice, "timeout", "5000");	//short timeout — game connections survive via data flow extending it

	//use the broker as a STUN server to discover our server-reflexive address.
	//Use the broker's known IP (from the DTLS source) with the main broker port
	//(from net_ice_broker cvar). The DTLS session may arrive from an ephemeral alt
	//socket, so we can't use its source port directly. And we can't resolve the
	//broker hostname — inside Docker it may resolve to an unreachable address.
	if (brokeraddr && *brokeraddr)
	{
		char stunaddr[128];
		const char *brokerhost = net_ice_broker.string;
		int brokerport = PORT_ICEBROKER;
		const char *broker_colon;
		char *colon;

		//extract just the port from the broker cvar
		if (!strncmp(brokerhost, "ws://", 5)) brokerhost += 5;
		else if (!strncmp(brokerhost, "wss://", 6)) brokerhost += 6;
		broker_colon = strrchr(brokerhost, ':');
		if (broker_colon)
			brokerport = atoi(broker_colon + 1);

		//extract just the IP from brokeraddr (strip its port)
		q_strlcpy(stunaddr, brokeraddr, sizeof(stunaddr));
		colon = strrchr(stunaddr, ':');
		if (colon)
			*colon = 0;

		iceapi.Set(ice, "server", va("stun:%s:%d", stunaddr, brokerport));
	}

	s = net_ice_servers.string;
	while((s=COM_Parse(s)))
		iceapi.Set(ice, "server", com_token);

	sdp = sdpdata;
#ifdef HAVE_JSON
	json = JSON_Parse(sdp);
	if (json)
		sdp = JSON_GetString(json, "sdp", buf, sizeof(buf), "");
#endif
	if (iceapi.Set(ice, "sdpoffer", sdp))
	{
		iceapi.Set(ice, "state", STRINGIFY(ICE_CONNECTING));

		q_snprintf(buf, sizeof(buf), "\xff\xff\xff\xff""ice_answer %s\n", brokerid);
		sz = strlen(buf);
		if (iceapi.Get(ice, "sdpanswer", buf+sz, sizeof(buf)-sz))
		{
			sz += strlen(buf+sz);
			sendpacket(buf, sz);
		}
		else
			Con_DPrintf("ICE: sdpanswer generation failed for broker_id=%s\n", brokerid);
	}
	else
		Con_DPrintf("ICE: sdpoffer parse failed for broker_id=%s\n", brokerid);
#ifdef HAVE_JSON
	JSON_Destroy(json);
#endif
}

void SVC_ICE_Candidate(const char *brokerid, const char *seq_s, const char *ack_s, const char *canddata, ice_udp_send_t sendpacket)
{	//handles an 'ice_ccand' udp message from a broker
	struct icestate_s *ice;
#ifdef HAVE_JSON
	json_t *json;
#endif
	const char *sdp, *line;
	char buf[1400];
	int pi;
	unsigned int seq = atoi(seq_s);
	unsigned int ack = atoi(ack_s);
	Con_DPrintf("ICE: ice_ccand broker_id=%s seq=%u ack=%u\n", brokerid, seq, ack);
#ifdef HAVE_DTLS
	if (!BrokerDTLS_IsAuthenticated())
		return;
#endif
	if (!qice_hostcon)
		return;

	ice = iceapi.Find(&qice_hostcon->icemodule, brokerid);
	if (!ice)
	{
		Con_DPrintf("ICE: ice_ccand - no ICE state for broker_id=%s\n", brokerid);
		return;
	}

	pi = ICE_FindOrAllocPeer(brokerid, false);
	if (pi < 0)
		return;
	ice_peer_state[pi].timeout = Sys_DoubleTime() + 30;

	//parse the inbound candidates (newline-separated in canddata)
	line = canddata;
	while (line && *line)
	{
		const char *nl = strchr(line, '\n');
		size_t len = nl ? (size_t)(nl - line) : strlen(line);
		char linebuf[512];
		if (len >= sizeof(linebuf)) len = sizeof(linebuf)-1;
		memcpy(linebuf, line, len);
		linebuf[len] = 0;

		if (seq++ >= ice_peer_state[pi].inseq && *linebuf)
		{
			ice_peer_state[pi].inseq++;
			sdp = linebuf;
#ifdef HAVE_JSON
			json = JSON_Parse(sdp);
			if (json)
			{
				sdp = buf;
				buf[0]='a'; buf[1]='='; buf[2]=0;
				JSON_GetString(json, "candidate", buf+2, sizeof(buf)-2, NULL);
			}
#endif
			iceapi.Set(ice, "sdp", sdp);
#ifdef HAVE_JSON
			JSON_Destroy(json);
#endif
		}
		line = nl ? nl+1 : NULL;
	}

	while (ack > ice_peer_state[pi].outseq)
	{
		char *nl = ice_peer_state[pi].text ? strchr(ice_peer_state[pi].text, '\n') : NULL;
		if (nl)
		{
			nl++;
			memmove(ice_peer_state[pi].text, nl, strlen(nl)+1);
			ice_peer_state[pi].outseq++;
			continue;
		}
		if (ack > ice_peer_state[pi].outseq)
			ice_peer_state[pi].outseq = ack;
		break;
	}

	while (iceapi.GetLCandidateSDP(ice, buf, sizeof(buf)))
	{
		Z_StrCat(&ice_peer_state[pi].text, buf);
		Z_StrCat(&ice_peer_state[pi].text, "\n");
	}

	q_snprintf(buf, sizeof(buf), "\xff\xff\xff\xff""ice_scand %s %u %u\n%s",
		brokerid, ice_peer_state[pi].outseq, ice_peer_state[pi].inseq,
		ice_peer_state[pi].text ? ice_peer_state[pi].text : "");
	sendpacket(buf, strlen(buf));
}
#endif

static void ICE_Show_f(void)
{
	const char *findname = Cmd_Argv(1);
	qsocket_t *s;

	if (!*findname)
	{	//passing null will report all of them. even when they're not attached to a qsocket.
		ICE_Debug(NULL);
		return;
	}

	for (s = net_activeSockets; s; s = s->next)
	{
		if (net_drivers[s->driver].Init != NQICE_Init)
			continue;	//uninteresting.
		if (*findname && q_strcasecmp(findname, s->trueaddress+1))
			continue;
		if (QICE_SocketIce(s))
			ICE_Debug(QICE_SocketIce(s));
	}
}
int NQICE_Init (void)
{
	qbyte random_probe[32];
	nqice_random_failed = false;
	if (safemode || COM_CheckParm("-noice"))
		return -1;
	if (!Sys_RandomBytes(random_probe, sizeof(random_probe)))
	{
		Con_Printf("ICE disabled: operating-system cryptographic randomness is unavailable.\n");
		return -1;
	}

//	if (!COM_CheckParm("-useice"))
//		return -1;	//disable by default for now.

	//ICE state
	Cvar_RegisterVariable(&net_ice_exchangeprivateips);
	Cvar_RegisterVariable(&net_ice_allowstun);
	Cvar_RegisterVariable(&net_ice_allowturn);
	Cvar_RegisterVariable(&net_ice_allowmdns);
	Cvar_RegisterVariable(&net_ice_relayonly);
#ifdef HAVE_DTLS
	Cvar_RegisterVariable(&net_ice_usewebrtc);
#endif
	Cvar_RegisterVariable(&net_ice_servers);
	Cvar_RegisterVariable(&net_ice_debug);

	//broker context.
	Cvar_RegisterVariable(&net_ice_broker);
	Cvar_RegisterVariable(&sv_port_rtc);
	Cvar_RegisterVariable(&sv_addr_ws);
	Cvar_RegisterVariable(&qice_password);

	Cmd_AddCommand("net_ice_show", ICE_Show_f);

	return 0;	//its okay. we'll create actual sockets later.
}
int NQICE_QueryAddresses(qhostaddr_t *addresses, int maxaddresses)	//server state
{	//returns the server's address, if known.
	int i = 0, j, k, l;
	netadr_t adrs[16];
	if (qice_hostcon)
	{	//include broker url? too lazy.
		if (i < maxaddresses)
		{
			addresses[i][0] = '/';
			q_strlcpy(addresses[i]+1, qice_hostcon->gamename, sizeof(addresses[i])-1);
			i++;
		}

		//FIXME: add stun results.

		for (j = 0; j < countof(qice_hostcon->icemodule.conn); j++)
			if (qice_hostcon->icemodule.conn[j] && qice_hostcon->icemodule.conn[j]->EnumerateAddresses)
			{
				k = qice_hostcon->icemodule.conn[j]->EnumerateAddresses(qice_hostcon->icemodule.conn[j], adrs, countof(adrs));
				for (l = 0; l < k && i < maxaddresses; l++)
				{
					if (NET_ClassifyAddress(&adrs[l], NULL) < ASCOPE_LINK)
						continue;
					NET_AdrToString(addresses[i],sizeof(addresses[i]), &adrs[l]);
					i++;
				}
			}
	}
	return i;
}
void Datagram_AddHostCacheInfo(struct qsockaddr *readaddr, const char *cname, const char *info);
bool NQICE_SearchForHosts (bool xmit)
{
	static char buf[8192], cname[128];
	static int ofs = 0, sz = 0;
	static int header;
	static icestream_t *lst;
	char *e;
	const char *l;
	int r;

	if (!COM_CheckParm("-useice"))
	{
		if (lst)
		{
			lst->Close(lst);
			lst = NULL;
		}
		ofs = sz = 0;
		header = 0;
		return false;
	}

	if (slist_scope != SLIST_INTERNET)
	{
		if (lst)
		{
			lst->Close(lst);
			lst = NULL;
		}
		ofs = sz = 0;
		header = 0;
		return false;
	}

	if (xmit && !lst)
	{
		const char *broker = net_ice_broker.string;
		qboolean sec = true;

#ifndef HAVE_TLS
		sec = false;	//assume no tls if we can't do tls.
#endif

		//clean up the broker name a bit... it was dumb to have so many possibilities.
		if (!strncmp(broker, "ws://", 5))
			broker += 5, sec = false;
		else if (!strncmp(broker, "tcp://", 6) || !strncmp(broker, "ice://", 6) || !strncmp(broker, "rtc://", 6))
			broker += 6, sec = false;
		else if (!strncmp(broker, "tls://", 6) || !strncmp(broker, "wss://", 6))
			broker += 6, sec = true;
		else if (!strncmp(broker, "ices://", 7) || !strncmp(broker, "rtcs://", 7))
			broker += 7, sec = true;
		else if (strstr(broker, "://"))
			broker = NULL;	//something weird.

		ofs = sz = 0;
		header = 2;
		lst = broker?ICE_OpenTCP(broker, PORT_ICEBROKER, sec):NULL;
		if (lst)
		{
			COM_ParseOut(com_protocolname.string, com_token,sizeof(com_token));
			q_snprintf(buf, sizeof(buf),
				"GET /raw/%s HTTP/1.1\r\n"
				"Host: %s\r\n"
				"Connection: close\r\n"
				"User-Agent: "ENGINE_NAME_AND_VER"\r\n"
				"\r\n", com_token, broker);
			sz = strlen(buf);
		}
	}

	if (lst)
	{
		if (header == 2)
		{
			r = lst->WriteBytes(lst, buf+ofs, sz-ofs);
			if (r < 0)
			{	//EOF? error? w/e
				lst->Close(lst);
				lst = NULL;
				return false;	//give up.
			}
			ofs += r;
			if (ofs < sz)
				return true;	//waiting for it to flush (probably delayed due to tls handshakes)

			header = 1;
			ofs = 0; sz = 0;
		}
		r = lst->ReadBytes(lst, buf+sz, sizeof(buf)-1 - sz);
		if (r > 0)
			sz += r;
		else if (r < 0)
		{
			lst->Close(lst);
			lst = NULL; //EOF? error? w/e
		}
		buf[sz] = 0;

		if (header)
		{
			if (strncmp(buf, "HTTP/1.1 200 ", (sz>13)?13:sz))
			{	//not http... or a weird error code that we can't handle. too lazy to do redirects.
				if (lst)	//may have already been closed by a read error above
				{
					lst->Close(lst);
					lst = NULL;
				}
				return true;
			}
			if (sz < 13)
				return true;	//still waiting...
			e = strstr(buf, "\r\n\r\n");
			if (!e)
				return true;	//headers not complete
			e += 4;
			ofs = e-buf;
			//okay, headers skipped over.
			header = 0;
			//consume it.
			memmove(buf, buf+ofs, sz-ofs+1);
			sz -= ofs;
			ofs = 0;
		}

		for(;;)
		{
			e = strchr(buf+ofs, '\n');
			if (!e)
				break;

			if (e > buf+ofs && e[-1] == '\r')
				e[-1] = 0;
			else
				*e = 0;
			l = buf+ofs;
			ofs = e+1-buf;

			while (*l == ' ' || *l == '\t')
				l++;
			if (*l == '#' || !*l)
				continue;
			l=COM_ParseOut(l, cname, sizeof(cname));
			while (*l == ' ' || *l == '\t')
				l++;
			Datagram_AddHostCacheInfo(NULL, cname, l);
		}

		if (ofs > 0)
		{
			memmove(buf, buf+ofs, sz-ofs+1);
			sz -= ofs;
			ofs = 0;
		}

		return true;
	}

	return false;	//no broadcasts here. we can't implement this.
}
bool NQICE_IsAddress(const char *host)
{
	static const char *const schemes[] = {
		"ws://", "wss://", "ice://", "ices://", "rtc://", "rtcs://",
		"tcp://", "tls://", "http://", "https://", "udp://", "dtls://"
	};
	int i;

	if (!host || !*host)
		return false;
	if (*host == '/')
		return true;
	for (i = 0; i < countof(schemes); i++)
		if (!strncmp(host, schemes[i], strlen(schemes[i])))
			return true;
	return false;
}

qsocket_t *NQICE_Connect (const char *host)		//used by client (enables websocket connection). fails when not ice, otherwise succeeds pending async broker failure.
{
	qice_connection_t *b;
	qsocket_t *dest;
	qboolean direct = true;

	//Only accept the room and URL forms parsed by QICE_Setup.
	if (nqice_random_failed || !NQICE_IsAddress(host))
		return NULL;
	else if (*host == '/')
		direct = false;
	else if (!strncmp(host, "rtc://", 6) || !strncmp(host, "rtcs://", 7) ||
		!strncmp(host, "ice://", 6) || !strncmp(host, "ices://", 7) ||
		!strncmp(host, "tcp://", 6) || !strncmp(host, "tls://", 6) ||
		!strncmp(host, "http://", 7) || !strncmp(host, "https://", 8))
		direct = false;
	else if (!strncmp(host, "udp://", 6))
		direct = true;	//plain text...
	else if (!strncmp(host, "dtls://", 7))
		direct = true;	//direct dtls connection (hopefully with `?fp=b64` on the end
	else if (!strncmp(host, "ws://", 5) || !strncmp(host, "wss://", 6))
		direct = true;	//direct websocket address (fixme: add #fp=? )

#ifndef HAVE_TLS
	if (!strncmp(host, "wss://", 6) || !strncmp(host, "rtcs://", 7) ||
		!strncmp(host, "ices://", 7) || !strncmp(host, "tls://", 6) ||
		!strncmp(host, "https://", 8) || !strncmp(host, "dtls://", 7))
	{
		Con_Printf("ICE: %s requires a TLS-enabled ICE build; browser WebRTC is unavailable.\n", host);
		return NULL;
	}
#endif

	//create a new ice connection
	dest = NET_NewQSocket();
	if (dest)
	{
		q_strlcpy(dest->connectaddress, host, sizeof(dest->connectaddress));
		if (direct)
		{
			b = QICE_Setup(NULL, false);
			b->direct = true;
			b->ice = iceapi.Create(&b->icemodule, NULL, NULL, ICEF_INITIATOR, ICEP_CLIENT);
			iceapi.Set(b->ice, "peer", host);
		}
		else
		{	//just broker. ice state will be set up once the broker tells us we have a peer.
			b = QICE_Setup(host, false);
		}

		dest->proquake_angle_hack = true;

		dest->driverdata = b;
		QICE_SetSocketIce(dest, b->ice);
		b->qsock = dest;
	}
	return dest;
}

static int QICE_ResendReliable (qsocket_t *sock)
{
	neterr_t err;
	struct icestate_s *ice = QICE_SocketIce(sock);
	int length;
	struct
	{
		unsigned int length;
		unsigned int sequence;
		qbyte data[1];
	} *pkt;

	if (!sock->sendMessageLength)
		return NETERR_SENT;	//nothing to actually send.
	if (!ice)	//still waiting for it to become available? don't crash.
		return NETERR_CLOGGED;

	sock->sendNext = false;

	length = sock->max_datagram;
	if (length >= sock->sendMessageLength)
		length = sock->sendMessageLength;

	pkt = alloca(NET_HEADERSIZE + length);
	pkt->length = BigLong((NET_HEADERSIZE+length) | NETFLAG_DATA | ((length==sock->sendMessageLength)?NETFLAG_EOM:0));
	pkt->sequence = BigLong(sock->sendSequence);
	memcpy(pkt->data, sock->sendMessage, length);

	err = iceapi.SendPacket(ice, pkt, NET_HEADERSIZE+length);
	if (err == NETERR_CLOGGED)
	{
		sock->sendNext = true;	//didn't actually send. retry now.
		return 0;
	}
	else if (err == NETERR_NQIO)	//netquake.io is unable to handle fragmentation
	{	//mtu too small...
		sock->max_datagram = 65536; //boost it. hopefully it'll start to get through after.
		sock->sendNext = true;	//didn't actually send. retry now.
		return 0;
	}
	else if (err == NETERR_MTU)
	{	//mtu too big...
		if (sock->max_datagram > 1200)
		{	//should still be on the first segment on the reliable if it didn't get through yet.
			Con_Printf("MTU error, dropping to %i\n", sock->max_datagram);
			sock->max_datagram -= 64;
			return 0;
		}
		Con_Printf("MTU error\n");
		return -1;
	}
	sock->lastSendTime = net_time;
	if (err == NETERR_SENT)
		return 1; //yay
	return -1;	//fatal
}

typedef enum
{
	QICE_MESSAGE_IGNORED,
	QICE_MESSAGE_READY,
	QICE_MESSAGE_FATAL
} qice_message_result_t;

static qice_message_result_t QICE_ProcessMessage(unsigned int length, struct icestate_s *ice, qsocket_t *sock, const void *data, size_t datasize, sizebuf_t *newmsg, qboolean log_server_drops)
{
	unsigned int sequence;

	if (datasize < 8)
		return QICE_MESSAGE_FATAL;	//runt...

	if ((length & 0xffff) != datasize)
		return QICE_MESSAGE_FATAL;	//err... something weird.

	if (length & NETFLAG_CTL)
	{	//no sequence info here.
		return QICE_MESSAGE_IGNORED;
	}
	sequence = BigLong(((const int*)data)[1]);
	data = &((const int*)data)[2];
	datasize -= NET_HEADERSIZE;

	if (length & NETFLAG_UNRELIABLE)
	{
		unsigned int count = 0;

		if (sequence < sock->unreliableReceiveSequence)
		{
			Con_DPrintf("Got a stale datagram\n");
			return QICE_MESSAGE_IGNORED;
		}
		if (sequence != sock->unreliableReceiveSequence)
		{
			count = sequence - sock->unreliableReceiveSequence;
		}
		if (count)
		{
			if (log_server_drops)
				Con_DPrintf("Dropped %u ICE datagram(s)\n", count);
			else
				Con_DPrintf("Dropped %u datagram(s)\n", count);
		}
		if (datasize > newmsg->maxsize)
		{
			Con_DPrintf("ICE unreliable message exceeds target message buffer\n");
			return QICE_MESSAGE_FATAL;
		}
		sock->unreliableReceiveSequence = sequence + 1;

		SZ_Clear(newmsg);
		SZ_Write(newmsg, data, datasize);
		return QICE_MESSAGE_READY;
	}
	else if (length & NETFLAG_ACK)
	{
		if (sequence != sock->sendSequence)
		{
			Con_DPrintf("Stale ACK received\n");
			return QICE_MESSAGE_IGNORED;
		}
		sock->sendSequence++;
		sock->sendMessageLength -= sock->max_datagram;
		if (sock->sendMessageLength > 0)
		{
			memmove (sock->sendMessage, sock->sendMessage + sock->max_datagram, sock->sendMessageLength);
			sock->sendNext = true;
		}
		else
		{
			sock->sendMessageLength = 0;
			sock->canSend = true;
		}
		return QICE_MESSAGE_IGNORED;
	}
	else if (length & NETFLAG_DATA)
	{
		if (sequence != sock->receiveSequence)
		{
			/* An accepted fragment may be retransmitted after its ACK was lost.
			 * Re-ACK only the immediately preceding sequence; do not append it
			 * again or acknowledge a future fragment. */
			if (sock->receiveSequence != 0 && sequence == sock->receiveSequence - 1)
			{
				int ack[2];
				ack[0] = BigLong(NET_HEADERSIZE | NETFLAG_ACK);
				ack[1] = BigLong(sequence);
				iceapi.SendPacket(ice, ack, sizeof(ack));
			}
			return QICE_MESSAGE_IGNORED;
		}
		if (datasize > sizeof(sock->receiveMessage) - sock->receiveMessageLength)
		{
			Con_DPrintf("ICE reliable receive buffer overflow\n");
			return QICE_MESSAGE_FATAL;
		}

		if (length & NETFLAG_EOM)
		{
			if (sock->receiveMessageLength + datasize > newmsg->maxsize)
			{
				Con_DPrintf("ICE reliable message exceeds target message buffer\n");
				return QICE_MESSAGE_FATAL;
			}
			/* Capacity and sequence are valid before acknowledging.  A peer must
			 * retransmit rather than losing an acknowledged message prefix. */
			{
				int ack[2];
				ack[0] = BigLong(NET_HEADERSIZE | NETFLAG_ACK);
				ack[1] = BigLong(sequence);
				iceapi.SendPacket(ice, ack, sizeof(ack));
			}
			sock->receiveSequence++;
			SZ_Clear(newmsg);
			SZ_Write(newmsg, sock->receiveMessage, sock->receiveMessageLength);
			SZ_Write(newmsg, data, datasize);
			sock->receiveMessageLength = 0;
			return QICE_MESSAGE_READY;
		}
		else
		{
			int ack[2];
			ack[0] = BigLong(NET_HEADERSIZE | NETFLAG_ACK);
			ack[1] = BigLong(sequence);
			iceapi.SendPacket(ice, ack, sizeof(ack));
			sock->receiveSequence++;
			memcpy(sock->receiveMessage+sock->receiveMessageLength, data, datasize);
			sock->receiveMessageLength += datasize;
			return QICE_MESSAGE_IGNORED;
		}
	}
	return QICE_MESSAGE_FATAL;
}

static const char *QICE_GetClientPassword(char *out, size_t outsize)
{
	Info_GetKey(cls.userinfo, "password", out, outsize);
	return *out ? out : qice_password.string;
}

static int QICE_PasswordValue(const char *text)
{
	char *e;
	int pwd;

	if (!*text || !strcmp(text, "none"))
		return 0;
	pwd = strtol(text, &e, 0);
	if (*e)
		pwd = Com_BlockChecksum((void *)text, (int)strlen(text));
	return pwd;
}

static qboolean QICE_SV_CheckPassword(int check)
{
	if (*qice_password.string && strcmp(qice_password.string, "none"))
	{
		if (check != QICE_PasswordValue(qice_password.string))
			return false;	//you chose poorly.
	}
	return true; //okay!
}

static qice_connection_t *qice_clientcon;	//evil global pointer passing.
static sizebuf_t qice_msgqueue[16];	//dumb fifo. this is what you get when you process stuff in bulk. sctp can bundle multiple packets too so we can't exactly avoid the whole bulk thing.
static int qice_msgqueuetype[countof(qice_msgqueue)];
static int qice_msgqueuesize;	//number used.
static int qice_msgqueueofs;	//position.
static qice_connection_t *qice_msgqueue_owner;

static void QICE_ResetClientMessageQueue(qice_connection_t *owner)
{
	int i;
	if (owner && qice_msgqueue_owner != owner)
		return;
	for (i = 0; i < countof(qice_msgqueue); i++)
		if (qice_msgqueue[i].data)
			SZ_Clear(&qice_msgqueue[i]);
	for (i = 0; i < countof(qice_msgqueuetype); i++)
		qice_msgqueuetype[i] = 0;
	qice_msgqueuesize = 0;
	qice_msgqueueofs = 0;
	qice_msgqueue_owner = NULL;
}

static void QICE_SetClientError(qice_connection_t *b, const char *reason)
{
	extern char m_return_reason[32];
	if (!b)
		return;
	b->error = true;
	q_strlcpy(m_return_reason, reason, sizeof(m_return_reason));
	QICE_ResetClientMessageQueue(b);
}

static qboolean QICE_GotS2CMessage (struct icestate_s *ice, const void *data, size_t datasize)
{
	unsigned int header;
	qice_connection_t *b = qice_clientcon;
	qsocket_t *sock;

	if (!b || !b->qsock)
		return false;
	sock = b->qsock;

	if (ice != b->ice)
	{
		Con_Printf("Packet from wrong ice state...\n");
		return false;
	}
	if (datasize < 4)
		return false;	//truncated somehow. don't try.

	header = BigLong(((const int*)data)[0]);
	if (header == -1)
	{	//qw-style out-of-band.
		const char *s;
		SZ_Clear(&net_message);
		SZ_Write(&net_message, data, datasize);
		MSG_BeginReading();
		MSG_ReadLong();
		s = MSG_ReadString();

		if (!strncmp(s, "challenge ", 10))
		{	//either a q2 or dp server... more likely to be dp.
			char password[CLIENT_USER_INFO_STRING_SIZE];
			const char *password_text = QICE_GetClientPassword(password, sizeof(password));
			s+=10;
			if (*password_text && !strchr(password_text, '\\') && !strchr(password_text, '\n'))
				s = va("%s\\password\\%s", s, password_text);
			s = va("%c%c%c%cconnect\\protocol\\darkplaces 3\\protocols\\RMQ FITZ QUAKE\\challenge\\%s", 255, 255, 255, 255, s);

			iceapi.SendPacket(ice, s, strlen(s));
		}
		else if (!strcmp(s, "accept"))
		{
			sock->proquake_angle_hack = false;
			b->dohandshake = 0;	//we're connected now. yay...
		}
		else if (!strncmp(s, "reject ", 7))
		{	//single line message, mostly.
			s += 7;
			Con_Printf("%s\n", s);
			QICE_SetClientError(b, s);
		}
		else if (!strncmp(s, "print\n", 6))
		{	//qw rejections. just in case.
			s += 6;
			Con_Printf("%s\n", s);
			QICE_SetClientError(b, s);
		}
		else
		{
			COM_Parse(s);
			Con_Printf("Unknown oob packet: %s\n", com_token);
		}

		SZ_Clear(&net_message);
		return true;
	}
	if (header == (datasize | NETFLAG_CTL))
	{
		int req;

		SZ_Clear(&net_message);
		SZ_Write(&net_message, data, datasize);
		MSG_BeginReading();
		MSG_ReadLong();

		req = MSG_ReadByte();
		if (!b->isserver)
		{
			if (req == CCREP_ACCEPT)
			{
#ifdef HAVE_DTLS
				char enc[32];
#endif
				int mod, flags;
				b->dohandshake = 0;

				/*port =*/MSG_ReadLong();	//don't care, should have been 0 anyway. doesn't make sense over ice.
				mod = MSG_ReadByte();
				/*ver =*/ MSG_ReadByte();
				flags = MSG_ReadByte();

				if (mod == QICE_MOD_PROQUAKE)
				{
					if (flags & QICE_PQF_CHEATFREE)
					{
						const char *reason = "Server is incompatible";
						Con_Printf("%s\n", reason);
						QICE_SetClientError(b, reason);
						return false;
					}
//					if (flags & PQF_IGNOREPORT)
//						port = 0; //don't switch it, for non-identity port forwarding.
					sock->proquake_angle_hack = true;
				}
				else
					sock->proquake_angle_hack = false;

#ifdef HAVE_DTLS
				if (iceapi.Get(ice, "encrypted", enc,sizeof(enc)) && atoi(enc) && b->issecure)
					Con_Printf ("Connection accepted ("S_COLOR_GREEN"encrypted"CON_DEFAULT")\n");
				else
					Con_Printf ("Connection accepted ("S_COLOR_RED"plaintext"CON_DEFAULT")\n");
#else
				Con_Printf ("Connection accepted\n");	//don't classify it as encrypted nor plain, so as to not let people think they can fix it by tweaking settings. they can't.
#endif
			}
			else if (req == CCREP_REJECT)
			{
				const char *reason = MSG_ReadString();
				Con_Printf(S_COLOR_RED"%s\n", reason);
				QICE_SetClientError(b, reason);
			}
		}
		SZ_Clear(&net_message);
		return true;
	}

	if (qice_msgqueuesize == countof(qice_msgqueue))
	{
		QICE_SetClientError(b, "ICE client receive queue overflow");
		return true;
	}
	if (!qice_msgqueue[qice_msgqueuesize].data)
	{
		qice_msgqueue[qice_msgqueuesize].maxsize = NET_MAXMESSAGE;
		qice_msgqueue[qice_msgqueuesize].data = Mem_Alloc(qice_msgqueue[qice_msgqueuesize].maxsize);
		SZ_Clear(&qice_msgqueue[qice_msgqueuesize]);	//paranoia
	}
	if (QICE_ProcessMessage(header, ice, b->qsock, data, datasize,
		&qice_msgqueue[qice_msgqueuesize], false) == QICE_MESSAGE_FATAL)
	{
		QICE_SetClientError(b, "invalid ICE client message");
		return true;
	}
	if (qice_msgqueue[qice_msgqueuesize].cursize)
	{
		qice_msgqueuetype[qice_msgqueuesize] =
			(header & NETFLAG_UNRELIABLE) ? 2 : 1;
		qice_msgqueuesize++;
	}
	return true;
}
int NQICE_GetMessage (qsocket_t *sock)
{	//ICE_GetAnyMessage... but for the client. client may logically only have one connection active at a time.
	qice_connection_t *b = sock->driverdata;
	if (!b)
		return -1;
	if (qice_msgqueue_owner && qice_msgqueue_owner != b)
		QICE_ResetClientMessageQueue(NULL);
	qice_msgqueue_owner = b;
	if (b->error)
	{
		QICE_ResetClientMessageQueue(b);
		return -1;
	}
	/* A room/broker connection has no ICE peer until QICE_UpdateBroker receives
	 * ICEMSG_NEWPEER.  Keep polling that valid pending state; the assignment to
	 * driverdata2 below is the existing publication seam. */
	if (QICE_SocketIce(sock) && QICE_SocketIce(sock) != b->ice)
	{
		QICE_ResetClientMessageQueue(b);
		return -1;
	}

	if (qice_msgqueueofs < qice_msgqueuesize)
	{
		SZ_Clear(&net_message);
		SZ_Write(&net_message, qice_msgqueue[qice_msgqueueofs].data, qice_msgqueue[qice_msgqueueofs].cursize);
		return qice_msgqueuetype[qice_msgqueueofs++];
	}
	qice_msgqueueofs = 0;
	while(qice_msgqueuesize)
		SZ_Clear(&qice_msgqueue[--qice_msgqueuesize]);

	if (!b->direct)
		QICE_UpdateBroker(b);	//keep the broker going.
	if (b->error)
	{
		QICE_ResetClientMessageQueue(b);
		return -1;
	}

	qice_clientcon = b;
	net_message.cursize = 0;
	b->icemodule.ReadGamePacket = QICE_GotS2CMessage;
	QICE_SetSocketIce(sock, b->ice);
	iceapi.ProcessModule(&b->icemodule);
	qice_clientcon = NULL;
	if (b->error)
	{
		QICE_ResetClientMessageQueue(b);
		return -1;
	}

	if (qice_msgqueuesize)
	{
		//we got one? multiple? only first matters here.
		SZ_Clear(&net_message);
		SZ_Write(&net_message, qice_msgqueue[0].data, qice_msgqueue[0].cursize);
		qice_msgqueueofs = 1;
		return qice_msgqueuetype[0];
	}

	//handle nq's reliable resends
	if ((net_time - sock->lastSendTime) > 1.0)
		sock->sendNext = true;
	if (sock->sendNext)
		if (QICE_ResendReliable(sock) < 0)
			return -1;	//erk...

	if (b && !b->error)
		return 0;	//not doable yet
	return -1;
}

qsocket_t *NQICE_CheckNewConnections (void)
{	//poll the server's websocket.
	qice_connection_t *b = qice_hostcon;	//stoopid globals.

	QICE_CleanupBrokerLookups();

	if (b)
		QICE_UpdateBroker(b);
	return NULL;
}

static void QCICE_SV_ReadUnsolicitedSPacket (struct icestate_s *ice, const void *data, size_t datasize, struct icestate_s*(*Accept)(struct icestate_s*temp))
{
	unsigned int header;
	const char *cmd;
	header = BigLong(((const int*)data)[0]);
	if (header == 0xffffffff)
	{
		extern cvar_t sv_public;
		if (sv_public.value > 0)
		{
			((char*)data)[datasize] = 0;
			Cmd_TokenizeString((char*)data+4);
			cmd = Cmd_Argv(0);
			if (!strcmp(cmd, "getinfo") || !strcmp(cmd, "getstatus"))
			{	//master, as well as other clients, may send us one of these two packets to get our serverinfo data
				//masters only really need gamename and player counts. actual clients might want player names too.
				qboolean full = !strcmp(cmd, "getstatus");
				char cookie[128], *nl;
				const char *s = Cmd_Args();
				int i;
				size_t j;
				if (!s) s = "";
				q_strlcpy(cookie, s, sizeof(cookie));
				nl = strchr(cookie, '\n'); if (nl) *nl = 0;

				SZ_Clear(&net_message);
				MSG_WriteLong(&net_message, -1);
				MSG_WriteString(&net_message, full?"statusResponse\n":"infoResponse\n");net_message.cursize--;

				//kinda evil, but oh well, just write it directly.
				Datagram_GenerateGetInfoString((char*)net_message.data+net_message.cursize, net_message.maxsize - net_message.cursize);
				net_message.cursize += strlen((char*)net_message.data+net_message.cursize);

				if (*cookie)
					{MSG_WriteString(&net_message, va("\\challenge\\%s", cookie));net_message.cursize--;}
				if (*fingerprint)
					{MSG_WriteString(&net_message, va("\\*fp\\%s", fingerprint));net_message.cursize--;}

				if (full)
				{
					for (i = 0; i < svs.maxclients; i++)
					{
						if (svs.clients[i].active)
						{
							float total = 0;
							for (j = 0; j < NUM_PING_TIMES; j++)
								total+=svs.clients[i].ping_times[j];
							total /= NUM_PING_TIMES;
							total *= 1000;	//put it in ms

							MSG_WriteString(&net_message, va("\n%i %i %i_%i \"%s\"",
								svs.clients[i].old_frags, (int)total, svs.clients[i].colors&15, svs.clients[i].colors>>4, svs.clients[i].name
							));net_message.cursize--;
						}
					}
				}

				iceapi.SendPacket(ice, net_message.data, net_message.cursize);
				SZ_Clear(&net_message);
			}
			/* Encrypted ice_offer/ice_ccand controls are intercepted by
			 * QICE_GotC2SMessage before this generic query handler and use
			 * native Datagram parsing under its temporary DTLS gate. */
			else if (!strcmp(cmd, "getchallenge"))
			{	//this might be a dp or qw getchallenge.
				qboolean dpresp;
				int i;
gotgetchallenge:
				Con_DPrintf("%s: getchallenge (unsupported)\n", ICE_GetConnName(ice));

				//clchallenge = Cmd_Argv(1);	//used by ioq3 but not us
				//contentnames = Cmd_Argv(2);	//'FTE-Quake' or 'FTE-QuakeRerelease' or something to describe datasets the client is willing to accept (space seperated).

				dpresp = true;
				for (i = 3; i < Cmd_Argc(); i++)
				{
					cmd = Cmd_Argv(i);
					if (!strncmp(cmd, "qw=", 3))
						/*we don't support this, don't bother with a reject because of it*/;
					else if (!strncmp(cmd, "nq=", 3))
						dpresp = atoi(cmd+3);
				}

				if (dpresp)
				{	//give it a dp-style challenge response instead
					int challenge = 0;
					cmd = va("\xff\xff\xff\xff" "challenge %iQSS", challenge);	//we still need to add a postfix to prevent it from being interpreted as a Q2 server
					iceapi.SendPacket(ice, cmd, strlen(cmd));
				}
			}
			else if (!strncmp(cmd, "connect\\", 8))
			{	//dp style connect.
			}
			else
				Con_DPrintf("%s: Unknown qw-style query\n", ICE_GetConnName(ice));
			return;
		}
		return;	//quakeworld connectionless packets.
	}
	if (header == (datasize | NETFLAG_CTL))
	{
		int req;
		SZ_Clear(&net_message);
		SZ_Write(&net_message, data, datasize);
		MSG_BeginReading();
		MSG_ReadLong();
		req = MSG_ReadByte();
		if (req == CCREQ_CONNECT)
		{
			qice_connection_t *b = qice_hostcon;
			const char *error = NULL;
			const char *game = MSG_ReadString();
			int ver = MSG_ReadByte(), mod, mod_passwd = 0; //proquakeisms
			int plnum = -1;
			qsocket_t *s = NULL;

			mod = MSG_ReadByte();
			if (mod==1)
			{
				/*modver =*/ MSG_ReadByte();
				/*modflags =*/ MSG_ReadByte();
				mod_passwd = MSG_ReadLong();
				if (msg_badread) mod_passwd = 0;
			}

			if (strcmp(game, DGRAM_PROTOCOL_NAME))
				return;	//wut? huh? did that just happen?!?

			if (ver != NET_PROTOCOL_VERSION)
				error = "Incompatible version.\n";
			else
			{
				const char *str = MSG_ReadString();	//yes, must have the proquake junk too (report a ver of 0 or something if need be).
				Cmd_TokenizeString(str);
				if (!strcmp(Cmd_Argv(0), "getchallenge"))
					goto gotgetchallenge;
				else if (!QICE_SV_CheckPassword(mod_passwd))
					error = "bad/missing password.\n";
			}

			SZ_Clear(&net_message);
			MSG_WriteLong(&net_message, 0);	//ctl header...
			if (error)
			{
				MSG_WriteByte(&net_message, CCREP_REJECT);
				MSG_WriteString(&net_message, error);
			}
			else
			{	//this sucks!
				for (s = net_activeSockets; s; s = s->next)
					if (QICE_SocketIce(s) == ice)
						break;

				if (!s)
				{	//new client... connect them if we can.
					char *dot, *dot2;

					//find a free player slot
					for (plnum=0 ; plnum<svs.maxclients ; plnum++)
						if (!svs.clients[plnum].active)
							break;
					if (plnum < svs.maxclients)
					{
						if (Accept)
							ice = Accept(ice);	//it was unsolicited and we need to explicitly accept it. Note: DTLS already created a connection so won't reach here.
						s = NET_NewQSocket();
					}
					if (s)
					{	//free slot AND qsocket, we are blessed!
						int i;
						for (i = 0; i < b->numclients; i++)
							if (b->clients[i].ice == ice)
								b->clients[i].isnew = false;	//has one allocated now

						s->proquake_angle_hack = (mod == 1);
						s->driverdata = b;
						QICE_SetSocketIce(s, ice);
						s->socket = -1;
						s->landriver = -1;

						//there is no truth here. the broker reported and masked their IP.
						//we could report where we're sending our packets, but that's likely a TURN relay rather than the actual target machine.
						*s->trueaddress = '@'; iceapi.Get(ice, "name", s->trueaddress+1, sizeof(s->trueaddress)-1);
						*s->maskedaddress = '@'; iceapi.Get(ice, "name", s->maskedaddress+1, sizeof(s->maskedaddress)-1); //should already be masked.

						dot = strchr(s->maskedaddress, '.');
						if (dot)
						{
							dot2 = strchr(dot+1, '.');
							if (dot2)
								q_strlcpy(dot2, ".x.x", sizeof(s->maskedaddress) - (dot2-s->maskedaddress));
						}
					}
					else
						error = "Server is full.\n";
				}

				if (error)
				{
					MSG_WriteByte(&net_message, CCREP_REJECT);
					MSG_WriteString(&net_message, error);
				}
				else
				{
					MSG_WriteByte(&net_message, CCREP_ACCEPT);

					MSG_WriteLong(&net_message, 0);	//never write the port on ICE
					s->proquake_angle_hack = (mod==1);
					if (s->proquake_angle_hack)
					{
						MSG_WriteByte(&net_message, 1);	//proquake
						MSG_WriteByte(&net_message, 30);//ver 30 should be safe. 34 screws with our single-server-socket stuff.
						MSG_WriteByte(&net_message, QICE_PQF_IGNOREPORT);	//flags: 0x80==ignore port

						//FTE adds this tp let the server know to ignore the ccreq and instead reply as a qw server would (avoiding race[packetloss] issues).
						//MSG_WriteString(&net_message, va("getchallenge %i %s\n", connectinfo.clchallenge, COM_QuotedString(com_protocolname.string, tmp, sizeof(tmp), false)));
					}
				}
			}

			*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
			iceapi.SendPacket(ice, net_message.data, net_message.cursize);
			SZ_Clear(&net_message);

			if (plnum>=0 && s)
			{
				//spawn the client.
				//FIXME: come up with some challenge mechanism so that we don't go to the expense of spamming serverinfos+modellists+etc until we know that its an actual connection attempt.
				svs.clients[plnum].netconnection = s;
				SV_ConnectClient (plnum);
			}
		}
		else if (req == CCREQ_SERVER_INFO)
		{	//use Q3's \xff\xff\xff\xffgetinfo request instead.
			qhostaddr_t adr;
			if (strcmp(MSG_ReadString(), DGRAM_PROTOCOL_NAME) != 0)
				return;

			SZ_Clear(&net_message);
			// save space for the header, filled in later
			MSG_WriteLong(&net_message, 0);
			MSG_WriteByte(&net_message, CCREP_SERVER_INFO);
			if (NQICE_QueryAddresses(&adr, 1) < 1)
				adr[0] = 0;	//unknown
			MSG_WriteString(&net_message, adr);	//cannonical name
			MSG_WriteString(&net_message, hostname.string);	//host name
			MSG_WriteString(&net_message, sv.name);	//map name
			MSG_WriteByte(&net_message, net_activeconnections);
			MSG_WriteByte(&net_message, svs.maxclients);
			MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
			*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
			iceapi.SendPacket(ice, net_message.data, net_message.cursize);
			SZ_Clear(&net_message);
		}
		else if (req == CCREQ_PLAYER_INFO)
		{	//use Q3's \xff\xff\xff\xffgetstatus request instead.
			int			playerNumber;
			int			activeNumber;
			int			clientNumber;
			client_t	*client;

			playerNumber = MSG_ReadByte();
			activeNumber = -1;

			for (clientNumber = 0, client = svs.clients; clientNumber < svs.maxclients; clientNumber++, client++)
			{
				if (client->active)
				{
					activeNumber++;
					if (activeNumber == playerNumber)
						break;
				}
			}

			if (clientNumber == svs.maxclients)
				return;

			SZ_Clear(&net_message);
			// save space for the header, filled in later
			MSG_WriteLong(&net_message, 0);
			MSG_WriteByte(&net_message, CCREP_PLAYER_INFO);
			MSG_WriteByte(&net_message, playerNumber);
			MSG_WriteString(&net_message, client->name);
			MSG_WriteLong(&net_message, client->colors);
			MSG_WriteLong(&net_message, (int)client->edict->v.frags);
			if (!client->netconnection)
			{
				MSG_WriteLong(&net_message, 0);
				MSG_WriteString(&net_message, "Bot");
			}
			else
			{
				MSG_WriteLong(&net_message, (int)(net_time - client->netconnection->connecttime));
				MSG_WriteString(&net_message, NET_QSocketGetMaskedAddressString(client->netconnection));
			}
			*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
			iceapi.SendPacket(ice, net_message.data, net_message.cursize);
			SZ_Clear(&net_message);
		}
		else if (req == CCREQ_RULE_INFO)
		{
			const char	*prevKey;
			char		info[2048], key[64], val[256];

			prevKey = MSG_ReadString();

			// build the full info string and iterate it — includes both
			// CVAR_SERVERINFO cvars and svs.serverinfo keys (set by QC mods)
			Datagram_GenerateGetInfoString(info, sizeof(info));

			// send the response
			SZ_Clear(&net_message);
			// save space for the header, filled in later
			MSG_WriteLong(&net_message, 0);
			MSG_WriteByte(&net_message, CCREP_RULE_INFO);
			if (Info_FindNextKey(info, prevKey, key, sizeof(key), val, sizeof(val)))
			{
				MSG_WriteString(&net_message, key);
				MSG_WriteString(&net_message, val);
			}
			*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
			iceapi.SendPacket(ice, net_message.data, net_message.cursize);
			SZ_Clear(&net_message);
		}
		else if (req == CCREQ_RCON)
			;
	}
}

static void(*qice_servercb)(qsocket_t *);	// temporary adapter callback while an ICE module is polled

/* The target net_driver_t returns one qsocket per NET_GetServerMessage call.
 * ICE can deliver several complete packets in one ProcessModule pass, so keep
 * a bounded FIFO.  On overflow the affected ICE socket is retired instead of
 * silently acknowledging and losing a reliable NetQuake message. */
#define QICE_SERVER_MESSAGE_QUEUE 64
typedef struct qice_server_message_s
{
	qsocket_t *socket;
	sizebuf_t data;
} qice_server_message_t;
static qice_server_message_t qice_server_messages[QICE_SERVER_MESSAGE_QUEUE];
static unsigned int qice_server_message_head;
static unsigned int qice_server_message_count;

/* A server can have at most one active qsocket per scoreboard slot.  Queue
 * retirement requests while ICE is inside ProcessModule/ProcessPacket, then
 * use the native server lifecycle after that call returns. */
#define QICE_SERVER_RETIRE_QUEUE MAX_SCOREBOARD
typedef struct qice_server_retire_s
{
	qsocket_t *socket;
	const char *reason;
} qice_server_retire_t;
static qice_server_retire_t qice_server_retires[QICE_SERVER_RETIRE_QUEUE];
static unsigned int qice_server_retire_count;

static qboolean QICE_ServerRetirePending(qsocket_t *socket)
{
	unsigned int i;
	for (i = 0; i < qice_server_retire_count; i++)
		if (qice_server_retires[i].socket == socket)
			return true;
	return false;
}

static void QICE_RequestServerRetirement(qsocket_t *socket, const char *reason)
{
	if (!socket || QICE_ServerRetirePending(socket))
		return;
	if (qice_server_retire_count == countof(qice_server_retires))
	{
		/* This cannot occur while server sockets remain bounded by
		 * MAX_SCOREBOARD, but never close a module from its callback. */
		Con_Printf("ICE: deferred retirement queue exhausted; refusing packet\n");
		return;
	}
	qice_server_retires[qice_server_retire_count].socket = socket;
	qice_server_retires[qice_server_retire_count].reason = reason;
	qice_server_retire_count++;
}

static void QICE_FlushServerRetirements(qice_connection_t *b)
{
	unsigned int i;
	client_t *saved_host_client = host_client;

	for (i = 0; i < qice_server_retire_count; i++)
	{
		qsocket_t *socket = qice_server_retires[i].socket;
		const char *reason = qice_server_retires[i].reason;
		qsocket_t *active;
		int clientnum;

		qice_server_retires[i].socket = NULL;
		/* Do not dereference a stale/free-list socket.  A native close can
		 * rewrite next, and the driverdata match prevents address reuse. */
		for (active = net_activeSockets; active; active = active->next)
			if (active == socket && active->driverdata == b)
				break;
		if (!active)
			continue;

		Con_Printf("ICE: retiring peer: %s\n", reason);
		QICE_DiscardQueuedServerMessages(socket);
		for (clientnum = 0; clientnum < svs.maxclients; clientnum++)
		{
			if (svs.clients[clientnum].netconnection != socket)
				continue;
			host_client = &svs.clients[clientnum];
			SV_DropClient(false);
			break;
		}
		if (clientnum == svs.maxclients)
			NET_Close(socket);
	}
	qice_server_retire_count = 0;
	host_client = saved_host_client;
}

static void QICE_QueueServerMessage(qsocket_t *socket)
{
	qice_server_message_t *message;
	unsigned int index;

	if (qice_server_message_count == countof(qice_server_messages))
	{
		QICE_RequestServerRetirement(socket, "server receive queue overflow");
		return;
	}
	index = (qice_server_message_head + qice_server_message_count) % countof(qice_server_messages);
	message = &qice_server_messages[index];
	if (!message->data.data)
	{
		message->data.maxsize = NET_MAXMESSAGE;
		message->data.data = Mem_Alloc(message->data.maxsize);
	}
	if (!message->data.data || net_message.cursize > message->data.maxsize)
	{
		QICE_RequestServerRetirement(socket, "unable to queue server message");
		return;
	}
	SZ_Clear(&message->data);
	SZ_Write(&message->data, net_message.data, net_message.cursize);
	message->socket = socket;
	qice_server_message_count++;
}

static void QICE_DiscardQueuedServerMessages(qsocket_t *socket)
{
	unsigned int i;
	for (i = 0; i < qice_server_message_count; i++)
	{
		qice_server_message_t *message = &qice_server_messages[
			(qice_server_message_head + i) % countof(qice_server_messages)];
		if (message->socket == socket)
			message->socket = NULL;
	}
}

static qsocket_t *QICE_DequeueServerMessage(void)
{
	qice_server_message_t *message;
	qsocket_t *socket;

	while (qice_server_message_count)
	{
		message = &qice_server_messages[qice_server_message_head];
		socket = message->socket;
		qice_server_message_head = (qice_server_message_head + 1) % countof(qice_server_messages);
		qice_server_message_count--;
		message->socket = NULL;
		if (!socket || socket->disconnected || QICE_ServerRetirePending(socket))
		{
			SZ_Clear(&message->data);
			continue;
		}
		SZ_Clear(&net_message);
		SZ_Write(&net_message, message->data.data, message->data.cursize);
		SZ_Clear(&message->data);
		return socket;
	}
	return NULL;
}

/* The native Datagram broker parser remains the authority for the control
 * format.  It may only be reached while ice_main is synchronously delivering
 * a successfully DTLS-decrypted packet for this exact ICE peer. */
static qboolean QICE_IsBrokerControlPacket(const void *data, size_t datasize)
{
	const char *text;

	if (datasize < 4 + 10)
		return false;
	text = (const char *)data + 4;
	return ((!memcmp(text, "ice_offer", 9) || !memcmp(text, "ice_ccand", 9)) &&
		(text[9] == ' ' || text[9] == '\n'));
}

static qboolean QICE_DispatchBrokerControl(struct icestate_s *ice, const void *data, size_t datasize)
{
	struct qsockaddr address;
	netadr_t peer;
	struct icestate_s *saved_active;
	qboolean saved_authenticated;

	if (!ICE_IsDecryptedCallback(ice) || !QICE_IsBrokerControlPacket(data, datasize) ||
		!ICE_GetPeerAddress(ice, &peer))
		return false;

	memset(&address, 0, sizeof(address));
	if (peer.type == NA_IP)
		memcpy(&address, &peer.in, sizeof(peer.in));
	else if (peer.type == NA_IPV6)
		memcpy(&address, &peer.in6, sizeof(peer.in6));
	else
		return false;

	/* BrokerDTLS_Send is valid only during this call.  It feeds replies back
	 * through the already-authenticated ICE/DTLS state; no second listener or
	 * borrowed socket wrapper owns the shared UDP descriptor. */
	saved_active = qice_broker_active;
	saved_authenticated = qice_broker_authenticated;
	qice_broker_active = ice;
	qice_broker_authenticated = true;
	_Datagram_BrokerPacket((byte *)data, (unsigned int)datasize, INVALID_SOCKET, &address);
	qice_broker_active = saved_active;
	qice_broker_authenticated = saved_authenticated;
	return true;
}

static qboolean QICE_GotC2SMessage (struct icestate_s *ice, const void *data, size_t datasize)
{	//returns false when the ice state got destroyed...
	unsigned int header;
	qsocket_t *s = NULL;

	if (datasize < sizeof(int))
		return false;
	header = BigLong(((const int*)data)[0]);
	if (header == 0xffffffff)
	{
		if (QICE_DispatchBrokerControl(ice, data, datasize))
			return true;
		QCICE_SV_ReadUnsolicitedSPacket(ice, data, datasize, NULL);
		return true;
	}
	if (header == (datasize | NETFLAG_CTL))	//netquake connectionless packets.
	{
		QCICE_SV_ReadUnsolicitedSPacket(ice, data, datasize, NULL);
		return true;
	}

	//this sucks!
	for (s = net_activeSockets; s; s = s->next)
		if (QICE_SocketIce(s) == ice)
			break;
	if (!s)
		return false;	//no idea who this is... did they try sneaking past the password checks?

	s->lastMessageTime = net_time;

	SZ_Clear(&net_message);
	if (QICE_ProcessMessage(header, ice, s, data, datasize, &net_message, true) == QICE_MESSAGE_FATAL)
	{
		QICE_RequestServerRetirement(s, "invalid ICE server message");
		return true;
	}
	if (net_message.cursize && qice_servercb)
		qice_servercb(s);
	return true;
}
static void NQICE_PollServer(void(*callback)(qsocket_t *))
{	//poll the server's sockets, figure out what connection they're from. decode dgram header to handle reliable/unreliables/ccrep
	//should already have called ICE_CheckNewConnections, so no need to poll brokers.

	qice_connection_t *b = qice_hostcon;
	qsocket_t *s;

	if (!b)
		return;	//not listening.

	net_message.cursize = 0;
	qice_servercb = callback;
	iceapi.ProcessModule(&b->icemodule);
	qice_servercb = NULL;
	QICE_FlushServerRetirements(b);

	{
		qsocket_t *next;
		for (s = net_activeSockets; s; s = next)
		{
			next = s->next;
			if (s->driverdata != b)
				continue;
			//handle nq's reliable resends
			if ((net_time - s->lastSendTime) > 1.0)
				s->sendNext = true;
			if (s->sendNext)
				QICE_ResendReliable(s);

			//kick clients that stopped responding — mirrors net_dgrm.c timeout logic
			if (net_time - s->lastMessageTime > ((!s->ackSequence)?net_connecttimeout.value:net_messagetimeout.value))
			{
				int i;
				for (i = 0; i < svs.maxclients; i++)
				{
					if (svs.clients[i].netconnection == s)
					{
						host_client = &svs.clients[i];
						SV_DropClient(false);
						break;
					}
				}
			}
		}
	}
}

qsocket_t *NQICE_GetAnyMessage(void)
{
	qsocket_t *socket;
	if (nqice_random_failed)
		return NULL;

	socket = QICE_DequeueServerMessage();
	if (socket)
		return socket;

	NQICE_PollServer(QICE_QueueServerMessage);
	return QICE_DequeueServerMessage();
}

bool NQICE_IsListening (void)
{
	return qice_listening && qice_hostcon != NULL;
}

const char *NQICE_GetWsAddr (void)
{
	return sv_addr_ws.string;
}

static char nqice_fp_cache[128];	//cached base64 fingerprint, computed once

const char *NQICE_GetFingerprint (void)
{
#ifdef HAVE_DTLS
	struct dtlslocalcred_s cred = {NULL,0,NULL,0};
	qbyte digest[DIGEST_MAXSIZE];
	const dtlsfuncs_t *funcs;

	if (*nqice_fp_cache)
		return nqice_fp_cache;

	funcs = ICE_DTLS_InitServer();
	if (!QICE_LoadServerCredentials(funcs, &cred))
		return "";

	if (!cred.certsize)
	{
		QICE_FreeLocalCred(&cred);
		return "";
	}

	/* Match the server identity installed by QICE_Setup and the URL parser used
	 * by direct dtls:// connections. */
	Base64_EncodeBlockURI(digest, CalcHash(&hash_sha2_256, digest, sizeof(digest), cred.cert, cred.certsize), nqice_fp_cache, sizeof(nqice_fp_cache));
	QICE_FreeLocalCred(&cred);
	return nqice_fp_cache;
#else
	return "";
#endif
}


void NQICE_UnshareGameSockets (void)
{	//invalidate shared sockets — call before closing the datagram listening sockets
	/* These conn slots are send-only wrappers made by NQICE_ShareGameSocket.
	 * ICEUDP_NoClose releases only the wrapper, leaving Datagram to close the
	 * descriptor. Clear them before that descriptor can be rebound to a new
	 * UDP socket with the same numeric handle. */
	if (qice_hostcon && shared_game_socket4 && qice_hostcon->icemodule.conn[0])
	{
		qice_hostcon->icemodule.conn[0]->CloseSocket(qice_hostcon->icemodule.conn[0]);
		qice_hostcon->icemodule.conn[0] = NULL;
	}
	if (qice_hostcon && shared_game_socket6 && qice_hostcon->icemodule.conn[1])
	{
		qice_hostcon->icemodule.conn[1]->CloseSocket(qice_hostcon->icemodule.conn[1]);
		qice_hostcon->icemodule.conn[1] = NULL;
	}
	if (shared_game_socket4)
	{
		free(shared_game_socket4);
		shared_game_socket4 = NULL;
	}
	if (shared_game_socket6)
	{
		free(shared_game_socket6);
		shared_game_socket6 = NULL;
	}
}

void NQICE_ShareGameSocket (sys_socket_t sock)
{	//share the datagram driver's UDP socket with ICE instead of ICE opening its own
	struct sockaddr_storage sa;
	socklen_t salen = sizeof(sa);
	struct icesocket_s *wrap, **slot;
	int af;

	if (getsockname((SOCKET)sock, (struct sockaddr *)&sa, &salen) < 0)
		return;
	af = sa.ss_family;
	slot = (af == AF_INET6) ? &shared_game_socket6 : &shared_game_socket4;

	if (*slot)
	{
		if ((*slot)->sock == (SOCKET)sock)
			return;	//same socket, nothing to do
		free(*slot);
		*slot = NULL;
	}
	wrap = ICE_WrapExistingSocket(sock, af);
	if (wrap)
		*slot = wrap;

	//put a send-only wrapper in the host module so ICE responses (STUN binding
	//replies, connectivity checks) go out through the game port, not an ephemeral port.
	//send-only so ICE_ProcessModule won't steal packets from the datagram driver.
	if (wrap && qice_hostcon)
	{
		int idx = (af == AF_INET6) ? 1 : 0;
		if (qice_hostcon->icemodule.conn[idx])
			qice_hostcon->icemodule.conn[idx]->CloseSocket(qice_hostcon->icemodule.conn[idx]);
		qice_hostcon->icemodule.conn[idx] = ICE_WrapExistingSocketSendOnly(sock, af);
	}
}

bool NQICE_ProcessPacket (byte *data, int len, struct qsockaddr *addr)
{	//forward a non-quake UDP packet to the ICE module for STUN/DTLS/SCTP processing
	netadr_t from;
	struct icesocket_s *via;
	extern int net_driverlevel;
	int saved_driverlevel = net_driverlevel;
	void(*saved_servercb)(qsocket_t *) = qice_servercb;
	qboolean handled = false;
	int i;

	if (nqice_random_failed)
		return false;

	//convert qsockaddr to netadr_t
	memset(&from, 0, sizeof(from));
	{	struct sockaddr_storage ss;
		memset(&ss, 0, sizeof(ss));
		memcpy(&ss, addr, sizeof(*addr));
		SockadrToNetadr(&ss, sizeof(ss), &from);
	}

	//pick the shared socket matching the original qsockaddr family
	via = (addr->qsa_family == AF_INET6) ? shared_game_socket6 : shared_game_socket4;
	if (!via)
		via = shared_game_socket4 ? shared_game_socket4 : shared_game_socket6;
	if (!via)
		return false;

	//set connum so ICE sends responses through the correct socket
	//(matches ICE_ProcessModule numbering: conn[0]=IPv4=connum 1, conn[1]=IPv6=connum 2)
	from.connum = (via == shared_game_socket6) ? 2 : 1;

	//set net_driverlevel to the ICE driver so NET_NewQSocket sets the correct driver
	for (i = 0; i < net_numdrivers; i++)
	{
		if (net_drivers[i].QGetAnyMessage == NQICE_GetAnyMessage)
		{
			net_driverlevel = i;
			break;
		}
	}

	//Queue complete game packets for the target's singular driver callback.
	qice_servercb = QICE_QueueServerMessage;

	//try ICE first (handles STUN connectivity checks and WebRTC DTLS)
	if (qice_hostcon)
		handled = iceapi.ProcessPacket(&qice_hostcon->icemodule, via, &from, data, len);

#ifdef HAVE_DTLS
	/* Send-only shared sockets cannot be read by ICE_ProcessModule. Reuse its
	 * unclaimed-packet path after established ICE dispatch.  This is the only
	 * shared-port DTLS acceptor: encrypted broker controls are later routed by
	 * QICE_GotC2SMessage through the native Datagram authorization boundary. */
	if (!handled && qice_hostcon && len > 0 && data[0] >= 20 && data[0] < 64)
		handled = ICE_ProcessUnsolicitedPacket(&qice_hostcon->icemodule, &from, data, len);
#endif

	/* ProcessPacket may invoke ReadGamePacket synchronously.  Retire requests
	 * must reach native NET_Close only after that module callback returns. */
	if (qice_hostcon)
		QICE_FlushServerRetirements(qice_hostcon);
	qice_servercb = saved_servercb;
	net_driverlevel = saved_driverlevel;
	return handled;
}

bool NQICE_ProcessSharedPacket(byte *data, int len, struct qsockaddr *addr)
{
	return NQICE_ProcessPacket(data, len, addr);
}

void NQICE_Listen (bool state)	//used by server (enables websocket connection).
{
	//connect/disconnect the websocket connection etc.
	qice_listening = state;
	if (qice_listening)
	{
		QICE_CleanupBrokerLookups();
#ifdef HAVE_DTLS
		//pre-generate the shared DTLS identity now (slow RSA keygen) rather than
		//on the server frame when the first browser offer arrives.
		ICE_CacheTempCredential(ICE_DTLS_InitServer());
#endif
		if (!qice_hostcon)
		{
			qice_hostcon = QICE_Setup(*sv_port_rtc.string?sv_port_rtc.string:NULL, true);
			qice_hostcon->icemodule.ReadGamePacket = QICE_GotC2SMessage;
			qice_hostcon->icemodule.ReadUnsolicitedPacket = QCICE_SV_ReadUnsolicitedSPacket;
		}
	}
	else if (qice_hostcon)
	{
		qice_connection_t *b = qice_hostcon;
		qsocket_t *s;

		/* The host module owns server peers.  Retire their native qsocket
		 * lifecycle before freeing that module, rather than leaving a detached
		 * socket that NET_Close can no longer dispatch to its driver. */
		for (s = net_activeSockets; s; s = s->next)
			if (s->driverdata == b)
				QICE_RequestServerRetirement(s, "ICE listener stopped");
		QICE_FlushServerRetirements(b);
		QICE_Close(b);
		qice_hostcon = NULL;
		QICE_CleanupBrokerLookups();
	}
}



int NQICE_SendMessage (qsocket_t *sock, sizebuf_t *data)
{	//FIXME: add nq's datagram header... handle resends
	//-1 if fatal, 1 if success.

	qice_connection_t *b = sock->driverdata;

	if (!b)
		return -1;	//no broker? that's bad.
	else if (QICE_SocketIce(sock))
		;	//socket has ice state, allow it to continue to run even if the broker has issues.
	else if (b->error)
		return -1;	//its dying.

	if (!sock->canSend)
		return -1;	//err, you shouldn't be here.
	if (sock->sendMessageLength)
		return 0;	//gotta flush it still.
	if (sizeof(sock->sendMessage) < data->cursize)
		return -1;	//oversized.
	memcpy(sock->sendMessage, data->data, data->cursize);
	sock->sendMessageLength = data->cursize;
	sock->canSend = false;

	/*if (QICE_SocketIce(sock) && ICE_AutoFragment(QICE_SocketIce(sock)))
	{	//websockets(tcp) or sctp both provide their own fragmentation. we can use that instead of quake's 1024-bytes-per-round-trip mess.
		//server should still keep its datagrams below the actual mtu though.
		sock->max_datagram = 65536;
	}
	else*/
	{
		//can resize at the start of each reliable. stoopid acks.
		sock->max_datagram = sock->pending_max_datagram;

		//make a guess about dtls+sctp overhead
		sock->max_datagram -= 25+28;
	}

	return QICE_ResendReliable(sock);
}
int NQICE_SendUnreliableMessage (qsocket_t *sock, sizebuf_t *data)
{
	qice_connection_t *b = sock->driverdata;
	struct icestate_s *ice = QICE_SocketIce(sock);

	if (!b || b->error)
		return -1;
	if (b && !b->error && ice)
	{
		neterr_t err;
		struct
		{
			unsigned int length;
			unsigned int sequence;
			qbyte data[1];
		} *pkt;

		if (data->cursize > 0xffff)
			return NETERR_MTU;	//this is the limit of the nq netchan we're using.
		if (!ice)	//still waiting for it to become available? don't crash.
			return NETERR_CLOGGED;

		pkt = alloca(NET_HEADERSIZE + data->cursize);
		pkt->length = BigLong((NET_HEADERSIZE+data->cursize) | NETFLAG_UNRELIABLE);
		pkt->sequence = BigLong(sock->unreliableSendSequence++);
		memcpy(pkt->data, data->data, data->cursize);

		err = iceapi.SendPacket(ice, pkt, NET_HEADERSIZE+data->cursize);
		if (err == NETERR_SENT)
			return 1; //yay
		if (err == NETERR_DISCONNECTED)
			return -1;	//fatal
		return 0;	//CLOGGED, NOROUTE, etc - transient, don't kill connection for unreliable data
	}
	return 0;	//not doable yet
}
bool NQICE_CanSendMessage (qsocket_t *sock)
{
	qice_connection_t *b = sock->driverdata;
	struct icestate_s *s = QICE_SocketIce(sock);
	neterr_t e;

	if (nqice_random_failed)
		return false;
	if (!b)
		return true;	//error state...
	if (!s)
		return false;	//broker didn't find a server yet.

	if (b)
	{
		if (b->isserver)
		{
		}
		else if (b->dohandshake)
		{
			if (b->dohandshake > Sys_DoubleTime())
				return false;	//not yet time

			//send a DP/QW getchallenge packet to mimic DP. this must not have a \n to avoid confusing FTE.
			iceapi.SendPacket(s, "\xff\xff\xff\xff" "getchallenge", 16);

			SZ_Clear(&net_message);
			// save space for the header, filled in later
			MSG_WriteLong(&net_message, 0);
			MSG_WriteByte(&net_message, CCREQ_CONNECT);
			MSG_WriteString(&net_message, DGRAM_PROTOCOL_NAME);
			MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
			if (sock->proquake_angle_hack)
			{	/*Spike -- proquake compat. if both engines claim to be using mod==1 then 16bit client->server angles can be used. server->client angles remain 16bit*/
				char password[CLIENT_USER_INFO_STRING_SIZE];
				int pwd = QICE_PasswordValue(QICE_GetClientPassword(password, sizeof(password)));

				Con_DWarning("Attempting to use ProQuake angle hack\n");
				MSG_WriteByte(&net_message, 1); /*'mod', 1=proquake*/
				MSG_WriteByte(&net_message, 34); /*'mod' version*/
				MSG_WriteByte(&net_message, 0); /*flags*/
				MSG_WriteLong(&net_message, pwd); /*password*/

				//FTE adds a 'getchallenge' hint here for a challenge response instead of nq protocols. QW or DP would expect only a getchallenge.
				//by sending a getchallenge for fte servers, we can get the server to use dp-style handshakes, bypassing any serverside need for smurf pretection and thus the downsides of 'sv_listen_nq 1'
//				if (!strchr(com_protocolname.string, '\"'))
//					MSG_WriteString(&net_message, va("getchallenge %i \"%s\"\n", 0, com_protocolname.string));
			}
			*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
			e = iceapi.SendPacket(s, net_message.data, net_message.cursize);
			SZ_Clear(&net_message);

			if (e == NETERR_NOROUTE)
				return false;	//no connection yet...
			if (e == NETERR_CLOGGED)
				return false;	//didn't send for some reason (dtls/sctp still pending?). don't spam and try again soon.

			Con_Printf("still trying...\n");
			b->dohandshake = Sys_DoubleTime() + 3;	//try again in 3 secs. we're trying to avoid dupes.

			return false;	//wait for the server to ack it.
		}
	}

	return sock->canSend;
}
bool NQICE_CanSendUnreliableMessage (qsocket_t *sock)
{
	qice_connection_t *b = sock->driverdata;
	struct icestate_s *s = QICE_SocketIce(sock);
	if (nqice_random_failed || !b || !s)
		return false;	//broker didn't find a server yet.

	return true;	//find out later...
}
void NQICE_Close (qsocket_t *sock)
{
	qice_connection_t *b = sock->driverdata;
	struct icestate_s *ice = QICE_SocketIce(sock);
	qsocket_t *s;
	int i;

	QICE_DiscardQueuedServerMessages(sock);
	if (b && b != qice_hostcon)
		QICE_ResetClientMessageQueue(b);
	if (ice)
	{	//kill the connection.
		iceapi.Close(ice, true);

		for (s = net_activeSockets; s; s = s->next)
			if (QICE_SocketIce(s) == ice)
				break;
		if (s)
			QICE_SetSocketIce(s, NULL);	//detach it.

		//if it was a client then kill that reference (should only be one)
		if (b)
		{
			b->ice = NULL;
			b->qsock = NULL;
		}

		//if we're the server then forget any references to the state here too (and make sure we don't just create a new qsock).
		for (i = 0; b && i < b->numclients; i++)
		{
			if (ice == b->clients[i].ice)
				b->clients[i].ice = NULL;
		}
	}
	if (b && b != qice_hostcon)
		QICE_Close(b);	//close the broker too when its a client (server socket persists beyond a single client dropping)
	sock->driverdata = NULL;
	QICE_SetSocketIce(sock, NULL);
}
void NQICE_Shutdown (void)
{
	int i;
	/* Tear down borrowed send-only wrappers while the host module still owns
	 * their conn[] entries.  Datagram may subsequently close and reuse the
	 * underlying descriptor. */
	NQICE_UnshareGameSockets();
	NQICE_Listen(false);	//just in case.
	QICE_CleanupBrokerLookups();

	nqice_fp_cache[0] = 0;	//clear cached fingerprint

#ifdef HAVE_DTLS
	QICE_FreeLocalCred(&qice_server_cred);
	qice_server_cred_cached = false;
#endif

	for (i = 0; i < MAX_ICE_PEERS; i++)
	{
		if (ice_peer_state[i].text)
		{
			Mem_Free(ice_peer_state[i].text);
			ice_peer_state[i].text = NULL;
		}
	}

	QICE_ResetClientMessageQueue(NULL);
	for (i = 0; i < countof(qice_msgqueue); i++)
	{
		if (!qice_msgqueue[i].data)
			continue;
		qice_msgqueue[i].maxsize = 0;
		Mem_Free(qice_msgqueue[i].data);
		qice_msgqueue[i].data = NULL;
	}
	qice_server_message_head = 0;
	qice_server_message_count = 0;
	qice_server_retire_count = 0;
	for (i = 0; i < countof(qice_server_messages); i++)
	{
		if (!qice_server_messages[i].data.data)
			continue;
		qice_server_messages[i].data.maxsize = 0;
		Mem_Free(qice_server_messages[i].data.data);
		qice_server_messages[i].data.data = NULL;
		qice_server_messages[i].socket = NULL;
	}
}



#if defined(__linux__) || defined(__bsd__) || defined(__POSIX__)
#include <fcntl.h>
qboolean Sys_RandomBytes(unsigned char *out, int len)
{
	int fd;
	ssize_t result;
	int offset = 0;

	if (!out || len < 0)
		return false;
	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		goto failure;
	while (offset < len)
	{
		result = read(fd, out + offset, (size_t)(len - offset));
		if (result > 0)
		{
			offset += (int)result;
			continue;
		}
		if (result < 0 && errno == EINTR)
			continue;
		close(fd);
		goto failure;
	}
	close(fd);
	return true;

failure:
	if (out && len > 0)
		memset(out, 0, (size_t)len);
	nqice_random_failed = true;
	Con_Printf("ICE: operating-system cryptographic randomness failed; refusing ICE traffic.\n");
	return false;
}
#elif defined(_WIN32)
#include <wincrypt.h>
qboolean Sys_RandomBytes(unsigned char *out, int len)
{
	qboolean ret = false;
	HCRYPTPROV  prov;
	if (out && len >= 0 && CryptAcquireContext(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
	{
		ret = !!CryptGenRandom(prov, len, (BYTE *)out);
		CryptReleaseContext(prov, 0);
	}
	if (!ret)
	{
		if (out && len > 0)
			memset(out, 0, (size_t)len);
		nqice_random_failed = true;
		Con_Printf("ICE: Windows cryptographic randomness failed; refusing ICE traffic.\n");
	}
	return ret;
}
#else
qboolean Sys_RandomBytes(unsigned char *out, int len)
{
	if (out && len > 0)
		memset(out, 0, (size_t)len);
	nqice_random_failed = true;
	Con_Printf("ICE: this platform has no cryptographic-randomness provider; refusing ICE traffic.\n");
	return false;
}
#endif
unsigned int Sys_Milliseconds(void)
{
	return Sys_DoubleTime()*1000;
}
const char *COM_ParseOut(const char *str, char *outbuf, size_t outbuf_sz)
{
	str = COM_Parse(str);
	q_strlcpy(outbuf, com_token, outbuf_sz);
	return str;
}
