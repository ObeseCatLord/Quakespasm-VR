/* Real local sender/parser/sign-on qualification for C02 metadata publication.
 * The dedicated bootstrap keeps graphics out of scope; client message parsing,
 * command execution, native sign-on replies, and server reliable sends are real. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "../Quake/sv_main.c"
#include "../Quake/cl_demo.c"
#include "../Quake/cl_parse.c"
#include "../Quake/net_sys.h"
#include "../Quake/net_defs.h"
#include "native_engine_fixture.h"

#include <assert.h>

static qboolean hold_client_send;
static byte receive_bytes[NET_MAXMESSAGE];
static unsigned query_count, full_count, empty_full_count, increment_count;
static unsigned userinfo_count, signon2_count, signon3_count;
static unsigned userinfo_count_at_signon3;
static unsigned skin_translation_calls, loading_end_calls, particle_clear_calls;
static unsigned graphical_map_reset_calls;
static qboolean control_pressure, name_pressure_armed, begin_pressure_armed;
static qboolean name_pressure_observed, begin_pressure_observed;
static qboolean limit_applied, limit_followup_applied, metadata_wait_observed;
static qboolean signon_capacity_restored;
static int signon_restore_capacity;
static char signon_limit[16] = "none";

qboolean __real_NET_CanSendMessage (qsocket_t *socket);
qboolean __wrap_NET_CanSendMessage (qsocket_t *socket)
{
	if (hold_client_send && socket == cls.netcon)
		return false;
	return __real_NET_CanSendMessage (socket);
}

void __real_CL_SignonReply (void);
void __wrap_CL_SignonReply (void)
{
	if (control_pressure && cls.signon == 1 && !name_pressure_armed)
	{
		__real_CL_SignonReply ();
		assert (cls.signon_reply_pending == CL_SIGNON_REPLY_NONE && cls.message.cursize > 0);
		while (cls.message.maxsize - cls.message.cursize > 5)
			MSG_WriteByte (&cls.message, clc_nop);
		name_pressure_armed = hold_client_send = true;
		return;
	}
	if (control_pressure && cls.signon == 3 && !begin_pressure_armed)
	{
		while (cls.message.maxsize - cls.message.cursize > 6)
			MSG_WriteByte (&cls.message, clc_nop);
		__real_CL_SignonReply ();
		assert (cls.signon_reply_pending == CL_SIGNON_REPLY_BEGIN);
		begin_pressure_armed = hold_client_send = true;
		return;
	}
	__real_CL_SignonReply ();
}

void __wrap_R_TranslateNewPlayerSkin (int playernum)
{
	assert (isDedicated && playernum >= 0 && playernum < MAX_SCOREBOARD);
	++skin_translation_calls; /* Parser callback only; no renderer is initialized. */
}

void __wrap_R_CheckEfrags (void) { assert (isDedicated); }
void __wrap_R_ClearParticles (void)
{
	assert (isDedicated);
	++particle_clear_calls; /* No renderer particle pool exists in this fixture. */
}
void __wrap_R_NewMap (void)
{
	assert (isDedicated);
	++graphical_map_reset_calls; /* Exclude Vulkan/render map-resource reset. */
}
void __wrap_PScript_ClearParticles (qboolean load)
{
	assert (isDedicated);
	(void)load; /* Particle reset belongs to the excluded renderer boundary. */
	++particle_clear_calls;
}
void __wrap_SCR_EndLoadingPlaque (void)
{
	assert (isDedicated);
	++loading_end_calls; /* Sign-on completion notification, not renderer proof. */
}

static qboolean Contains (const byte *bytes, int length, const char *needle)
{
	size_t n = strlen (needle);
	for (int i = 0; i + (int)n <= length; ++i)
		if (!memcmp (bytes + i, needle, n)) return true;
	return false;
}

static unsigned CountText (const byte *bytes, int length, const char *needle)
{
	unsigned count = 0;
	size_t n = strlen (needle);
	for (int i = 0; i + (int)n <= length; ++i)
		if (!memcmp (bytes + i, needle, n)) { ++count; i += (int)n - 1; }
	return count;
}

static void ReceiveClientPackets (void)
{
	for (;;)
	{
		net_message.data = receive_bytes;
		net_message.maxsize = sizeof (receive_bytes);
		net_message.cursize = 0;
		int received = NET_GetMessage (cls.netcon);
		assert (received >= 0);
		if (!received) return;
		query_count += Contains (net_message.data, net_message.cursize, "cmd pext");
		full_count += CountText (net_message.data, net_message.cursize, "//fullserverinfo");
		empty_full_count += CountText (net_message.data, net_message.cursize, "//fullserverinfo \"\"");
		increment_count += CountText (net_message.data, net_message.cursize, "//svi ");
		userinfo_count += CountText (net_message.data, net_message.cursize, "//fui ");
		const int before = cls.signon;
		/* This first packet is the native cmd pext query. The test chooses a
		 * bounded offer directly through Cmd_ExecuteString below; later packets,
		 * including serverinfo and metadata, go through the real parser. */
		if (before == 0 && Contains (net_message.data, net_message.cursize, "cmd pext"))
			continue;
		CL_ParseServerMessage ();
		assert (msg_readcount == net_message.cursize && cls.state == ca_connected);
		if (before < 2 && cls.signon >= 2) ++signon2_count;
		if (before < 3 && cls.signon >= 3)
		{
			++signon3_count;
			userinfo_count_at_signon3 = userinfo_count;
		}
	}
}

static void SendSelectedOffer (const char *mode)
{
	char command[128];
	/* The server's real cmd pext query has arrived. Choose a bounded
	 * client capability offer through the real cmd/Cmd_ExecuteString path. */
	if (!strcmp (mode, "qsmi"))
		q_snprintf (command, sizeof (command), "cmd pext %#x 1",
			PROTOCOL_QSVR_METADATA);
	else
		q_snprintf (command, sizeof (command), "cmd pext %#x %#x",
			PROTOCOL_FTE_PEXT2, PEXT2_REPLACEMENTDELTAS | PEXT2_PREDINFO);
	Cmd_ExecuteString (command, src_command);
	assert (cls.message.cursize > 0 && cls.state == ca_connected);
	CL_SendCmd ();
	assert (!cls.message.cursize);
}

static void SetServerInfo (const char *mode)
{
	char value[760];
	memset (value, 'm', sizeof (value) - 1);
	value[sizeof (value) - 1] = 0;
	if (!strcmp (mode, "qsmi"))
		Info_SetKey (svs.serverinfo, sizeof (svs.serverinfo), "c02_blob", value);
	else
	{
		Info_SetKey (svs.serverinfo, sizeof (svs.serverinfo), "c02_first", value);
		Info_SetKey (svs.serverinfo, sizeof (svs.serverinfo), "c02_second", value);
	}
}

static void ServiceClient (void)
{
	if (cl.sendprespawn && cls.signon_reply_pending != CL_SIGNON_REPLY_NAME)
	{
		/* Match the host's one-shot prespawn obligation. CL_LoadCSProgs is a
		 * separate VM/rendering boundary and is explicitly not qualified here. */
		cl.sendprespawn = false;
		CL_RequestPrespawn ();
	}
	CL_SendCmd ();
}

static void RunServerFrame (void)
{
	PR_SwitchQCVM (&sv.qcvm);
	Host_ServerFrame ();
	PR_SwitchQCVM (NULL);
}

static void VerifyUserinfoPublication (client_t *peer)
{
	static char payload[7100];
	char value[CLIENT_USER_INFO_STRING_SIZE];
	memset (payload, 'p', sizeof (payload) - 1);
	payload[sizeof (payload) - 1] = 0;
	Info_SetKey (peer->userinfo, sizeof (peer->userinfo), "payload", payload);
	Info_SetKey (peer->userinfo, sizeof (peer->userinfo), "_private", "server-only");
	Info_SetKey (peer->userinfo, sizeof (peer->userinfo), "*star", "visible");
	Info_SetKey (peer->userinfo, sizeof (peer->userinfo), "quoted", "omit\"this");
	Info_SetKey (peer->userinfo, sizeof (peer->userinfo), "line", "first\nsecond");
	Info_SetKey (peer->userinfo, sizeof (peer->userinfo), "name", "stale-name");
	Info_SetKey (peer->userinfo, sizeof (peer->userinfo), "topcolor", "1");
	Info_SetKey (peer->userinfo, sizeof (peer->userinfo), "bottomcolor", "2");
	q_strlcpy (peer->name, "Native \"Quoted", sizeof (peer->name));
	peer->colors = 0x6d;
	SV_MetadataUserinfoChanged (0);
	host_frametime = 0.017f;
	realtime += host_frametime;
	RunServerFrame ();
	ReceiveClientPackets ();
	assert (cl.scores && !strcmp (cl.scores[0].name, peer->name) &&
		cl.scores[0].colors == peer->colors);
	Info_GetKey (cl.scores[0].userinfo, "payload", value, sizeof (value));
	assert (!strcmp (value, payload));
	Info_GetKey (cl.scores[0].userinfo, "*star", value, sizeof (value));
	assert (!strcmp (value, "visible"));
	Info_GetKey (cl.scores[0].userinfo, "line", value, sizeof (value));
	assert (!strcmp (value, "first\nsecond"));
	Info_GetKey (cl.scores[0].userinfo, "_private", value, sizeof (value));
	assert (!*value);
	Info_GetKey (cl.scores[0].userinfo, "quoted", value, sizeof (value));
	assert (!*value && skin_translation_calls > 0);
}

void __real_SZ_Write (sizebuf_t *buf, const void *data, int length);
void __wrap_SZ_Write (sizebuf_t *buf, const void *data, int length)
{
	client_t *peer = &svs.clients[0];
	if (!limit_applied && strcmp (signon_limit, "none") &&
		buf == &peer->message && peer->active &&
		peer->sendsignon == PRESPAWN_SIGNONMSG &&
		peer->metadata_serverinfo_pending && cls.signon == 1)
	{
		assert (length > 0);
		signon_restore_capacity = buf->maxsize;
		buf->maxsize = buf->cursize + length + sv.signon.cursize +
			(!strcmp (signon_limit, "exact") ? 2 : 1);
		assert (buf->maxsize <= NET_MAXMESSAGE);
		limit_applied = true;
	}
	__real_SZ_Write (buf, data, length);
}

void __real_MSG_WriteByte (sizebuf_t *buf, int value);
void __wrap_MSG_WriteByte (sizebuf_t *buf, int value)
{
	if (limit_applied && !signon_capacity_restored &&
		buf == &svs.clients[0].message && value == svc_signonnum)
	{
		assert (buf->maxsize - buf->cursize == 2);
		__real_MSG_WriteByte (buf, value);
		buf->maxsize = signon_restore_capacity;
		signon_capacity_restored = true;
		return;
	}
	__real_MSG_WriteByte (buf, value);
}

void __real_SV_SendClientMessages (void);
void __wrap_SV_SendClientMessages (void)
{
	client_t *peer = &svs.clients[0];
	qboolean pressure_queued = !strcmp (signon_limit, "pressure") &&
		limit_followup_applied && peer->message.cursize == 1;
	__real_SV_SendClientMessages ();
	if (pressure_queued && peer->active &&
		peer->sendsignon == PRESPAWN_SIGNONMSG && !peer->metadata_serverinfo_pending &&
		cls.signon < 2)
	{
		assert (peer->message.cursize == 0); /* Real reliable send drained the queued svc_nop. */
		metadata_wait_observed = true;
	}
	if (!strcmp (signon_limit, "pressure") && limit_applied &&
		!limit_followup_applied && peer->active &&
		peer->sendsignon == PRESPAWN_SIGNONMSG && !peer->metadata_serverinfo_pending &&
		peer->message.cursize == 0 && cls.signon < 2)
	{
		peer->message.maxsize = sv.signon.cursize + 2;
		MSG_WriteByte (&peer->message, svc_nop);
		assert (peer->message.cursize == 1);
		limit_followup_applied = true;
	}
}

static void ReleaseClientPressure (void)
{
	if (!hold_client_send) return;
	if (name_pressure_armed && !name_pressure_observed)
	{
		assert (cls.signon_reply_pending == CL_SIGNON_REPLY_PRESPAWN &&
			cls.message.maxsize - cls.message.cursize == 5);
		name_pressure_observed = true;
	}
	if (begin_pressure_armed && !begin_pressure_observed)
	{
		assert (cls.signon_reply_pending == CL_SIGNON_REPLY_BEGIN &&
			cls.message.maxsize - cls.message.cursize == 6);
		begin_pressure_observed = true;
	}
	hold_client_send = false;
}

int main (int argc, char **argv)
{
	const char *mode = NULL;
	for (int i = 1; i + 1 < argc; ++i)
	{
		if (!strcmp (argv[i], "-metadata-offer")) mode = argv[i + 1];
		if (!strcmp (argv[i], "-metadata-limit"))
			q_strlcpy (signon_limit, argv[i + 1], sizeof (signon_limit));
	}
	for (int i = 1; i < argc; ++i)
		if (!strcmp (argv[i], "-control-pressure")) control_pressure = true;
	assert (mode);
	assert (!strcmp (mode, "qsmi") || !strcmp (mode, "predinfo"));
	assert (!strcmp (signon_limit, "none") || !strcmp (signon_limit, "exact") ||
		!strcmp (signon_limit, "pressure"));
	Fixture_InitNativeEngine (argc, argv, "e1m1", false);
	CL_Init (); /* Registers the native client metadata command handlers headlessly. */
	PR_SwitchQCVM (NULL);
	SetServerInfo (mode);
	cls.state = ca_connected;
	cls.demoplayback = false;
	cls.offered_qsvr = 0;
	cls.legacy_qsvr = 0;
	cls.netcon = NET_Connect ("local");
	assert (cls.netcon);
	client_t *peer = &svs.clients[0];
	qboolean offer_sent = false;
	int frame;
	for (frame = 0; frame < 300 && !peer->spawned; ++frame)
	{
		host_frametime = 0.017f;
		realtime += host_frametime;
		ServiceClient ();
		ReleaseClientPressure ();
		RunServerFrame ();
		ReceiveClientPackets ();
		if (query_count && !offer_sent)
		{
			SendSelectedOffer (mode);
			offer_sent = true;
		}
		if (peer->pextknown && !offer_sent)
			assert (!"server accepted an offer before the test response");
		if (peer->pextknown && strcmp (mode, "qsmi") == 0)
		{
			if (peer->offered_metadata != QSVR_METADATA_VERSION ||
				(peer->protocol_pext2 & PEXT2_PREDINFO))
				fprintf (stderr, "METADATA_NEGOTIATION offered=%#x accepted=%#x pext1=%#x pext2=%#x\n",
					peer->offered_metadata, peer->offered_qsvr,
					peer->protocol_pext1, peer->protocol_pext2);
			assert (peer->offered_metadata == QSVR_METADATA_VERSION &&
				!(peer->protocol_pext2 & PEXT2_PREDINFO));
		}
		if (peer->pextknown && !strcmp (mode, "predinfo"))
			assert (!peer->offered_metadata &&
				(peer->protocol_pext2 & PEXT2_PREDINFO));
		if (cls.signon >= 2)
			assert (cl.serverinfo_received && signon2_count == 1);
		if (cls.signon >= 3)
			assert (userinfo_count_at_signon3 >= MAX_SCOREBOARD);
	}
	if (!peer->spawned)
	{
		fprintf (stderr,
			"METADATA_NATIVE_STALL mode=%s frames=%d query=%u offer=%d pextknown=%d signon=%d pending=%d sendprespawn=%d serverinfo=%d serverinfo_pending=%d dirty=%#x server_spawned=%d userinfo=%u\n",
			mode, frame, query_count, offer_sent, peer->pextknown, cls.signon,
			cls.signon_reply_pending, cl.sendprespawn, cl.serverinfo_received,
			peer->metadata_serverinfo_pending, peer->metadata_userinfo_dirty,
			peer->spawned, userinfo_count);
		return 3;
	}
	assert (query_count && offer_sent && peer->pextknown && cl.serverinfo_received &&
		cls.signon >= 3 && signon2_count == 1 && signon3_count == 1 &&
		userinfo_count_at_signon3 >= MAX_SCOREBOARD);
	if (!strcmp (signon_limit, "exact"))
		assert (limit_applied && signon_capacity_restored);
	if (!strcmp (signon_limit, "pressure"))
		assert (limit_applied && metadata_wait_observed && signon_capacity_restored);
	if (control_pressure)
		assert (name_pressure_observed && begin_pressure_observed);
	for (int slot = 0; slot < MAX_SCOREBOARD; ++slot)
		assert (!cl.scores[slot].colors || slot == 0);
	if (!strcmp (mode, "qsmi")) VerifyUserinfoPublication (peer);
	if (!strcmp (mode, "predinfo"))
	{
		char expected[760], actual[760];
		memset (expected, 'm', sizeof (expected) - 1);
		expected[sizeof (expected) - 1] = 0;
		Info_GetKey (cl.serverinfo, "c02_first", actual, sizeof (actual));
		assert (!strcmp (actual, expected));
		Info_GetKey (cl.serverinfo, "c02_second", actual, sizeof (actual));
		assert (!strcmp (actual, expected) && empty_full_count &&
			increment_count >= 2 && full_count >= 1);
	}
	printf ("METADATA_NATIVE_PASSED offer=%s limit=%s control=%d frames=%d query=%u pextknown=%d predinfo=%d metadata=%u full=%u empty=%u svi=%u fui=%u signon2=%u signon3=%u spawned=%d scoreboard=%s graphics=excluded\n",
		mode, signon_limit, control_pressure, frame, query_count, peer->pextknown,
		!!(peer->protocol_pext2 & PEXT2_PREDINFO), peer->offered_metadata,
		full_count, empty_full_count, increment_count, userinfo_count,
		signon2_count, signon3_count, peer->spawned,
		!strcmp (mode, "qsmi") ? "near-full-ordered-overlay" : "all-16-slots");
	return 0;
}
