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
#include <setjmp.h>

extern jmp_buf host_abortserver;

static qboolean hold_client_send;
static byte receive_bytes[NET_MAXMESSAGE];
static unsigned query_count, full_count, empty_full_count, increment_count;
static unsigned userinfo_count, signon2_count, signon3_count;
static unsigned userinfo_count_at_signon3;
static unsigned disconnect_count;
static unsigned skin_translation_calls, loading_end_calls, particle_clear_calls;
static unsigned graphical_map_reset_calls;
static qboolean control_pressure, name_pressure_armed, begin_pressure_armed;
static qboolean name_pressure_observed, begin_pressure_observed;
static qboolean limit_applied, limit_followup_applied, metadata_wait_observed;
static qboolean signon_capacity_restored;
static qboolean live_admission, live_capture_autocvar, live_capture_commands;
static unsigned live_autocvar_calls, live_native_command_count, live_failures;
static qboolean live_native_command_overflow;
static char live_native_commands[16][2048];
static int signon_restore_capacity;
static char signon_limit[16] = "none";

qboolean __real_NET_CanSendMessage (qsocket_t *socket);
qboolean __wrap_NET_CanSendMessage (qsocket_t *socket)
{
	if (hold_client_send && socket == cls.netcon)
		return false;
	return __real_NET_CanSendMessage (socket);
}

#ifdef METADATA_LIVE_ADMISSION_FIXTURE
void __real_PR_AutoCvarChanged (cvar_t *var);
void __wrap_PR_AutoCvarChanged (cvar_t *var)
{
	if (live_capture_autocvar)
		++live_autocvar_calls;
	__real_PR_AutoCvarChanged (var);
}

qboolean __real_Cmd_ExecuteString (const char *text, cmd_source_t src);
qboolean __wrap_Cmd_ExecuteString (const char *text, cmd_source_t src)
{
	if (live_capture_commands && src == src_client &&
		host_client == &svs.clients[0])
	{
		if (live_native_command_count < countof (live_native_commands))
		{
			if (q_strlcpy (live_native_commands[live_native_command_count], text,
				sizeof (live_native_commands[0])) >= sizeof (live_native_commands[0]))
				live_native_command_overflow = true;
		}
		else
			live_native_command_overflow = true;
		++live_native_command_count;
	}
	return __real_Cmd_ExecuteString (text, src);
}

#endif /* METADATA_LIVE_ADMISSION_FIXTURE */

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
		disconnect_count += net_message.cursize == 1 && net_message.data[0] == svc_disconnect;
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

typedef struct
{
	cvar_t *var;
	char string[256];
	char default_string[256];
	qboolean has_default;
	float value;
	cvarflags_t flags;
	cvarcallback_t callback;
} live_cvar_snapshot_t;

static void LiveCheck (qboolean condition, const char *name)
{
	if (condition)
		return;
	++live_failures;
	fprintf (stderr, "METADATA_LIVE_CHECK_FAILED %s\n", name);
}

static void LiveCvarSnapshot (live_cvar_snapshot_t *snapshot, cvar_t *var)
{
	assert (snapshot && var);
	memset (snapshot, 0, sizeof (*snapshot));
	snapshot->var = var;
	q_strlcpy (snapshot->string, var->string ? var->string : "",
		sizeof (snapshot->string));
	snapshot->has_default = var->default_string != NULL;
	if (snapshot->has_default)
		q_strlcpy (snapshot->default_string, var->default_string,
			sizeof (snapshot->default_string));
	snapshot->value = var->value;
	snapshot->flags = var->flags;
	snapshot->callback = var->callback;
}

static qboolean LiveCvarMatches (const live_cvar_snapshot_t *snapshot)
{
	const cvar_t *var = snapshot->var;
	return var && !strcmp (var->string ? var->string : "", snapshot->string) &&
		(!!var->default_string == snapshot->has_default) &&
		(!snapshot->has_default || !strcmp (var->default_string,
			snapshot->default_string)) && var->value == snapshot->value &&
		var->flags == snapshot->flags && var->callback == snapshot->callback;
}

static void LivePrintEscaped (const char *text)
{
	putchar ('"');
	for (; *text; ++text)
	{
		if (*text == '\n') fputs ("\\n", stdout);
		else if (*text == '\r') fputs ("\\r", stdout);
		else if (*text == '\\') fputs ("\\\\", stdout);
		else if (*text == '"') fputs ("\\\"", stdout);
		else putchar (*text);
	}
	putchar ('"');
}

static void LiveCompareCommands (const char *stage,
	const char *const *expected, unsigned expected_count)
{
	qboolean matches = !live_native_command_overflow &&
		live_native_command_count == expected_count;
	fprintf (stdout, "METADATA_LIVE_NATIVE_COMMANDS stage=%s count=%u",
		stage, live_native_command_count);
	for (unsigned i = 0; i < live_native_command_count &&
		i < countof (live_native_commands); ++i)
	{
		fputs (" command=", stdout);
		LivePrintEscaped (live_native_commands[i]);
		if (i >= expected_count || strcmp (live_native_commands[i], expected[i]))
			matches = false;
	}
	putchar ('\n');
	if (live_native_command_overflow)
		matches = false;
	LiveCheck (matches, va ("%s emitted exact native command sequence", stage));
}

static void LiveRunNativeFrame (void)
{
	live_native_command_count = 0;
	live_native_command_overflow = false;
	live_capture_commands = true;
	host_frametime = 0.017f;
	realtime += host_frametime;
	RunServerFrame ();
	live_capture_commands = false;
	ReceiveClientPackets ();
}

static void LiveCheckDeniedState (client_t *peer,
	const live_cvar_snapshot_t *name, const live_cvar_snapshot_t *top,
	const live_cvar_snapshot_t *bottom, const char *client_info,
	const char *server_info, const char *peer_name, const char *peer_info,
	int peer_colors, const byte *queued, int queued_size, int queued_maxsize,
	unsigned autocvar_calls)
{
	LiveCheck (LiveCvarMatches (name), "refusal preserved name cvar string/default/flags/value/callback");
	LiveCheck (LiveCvarMatches (top), "refusal preserved topcolor cvar string/default/flags/value/callback");
	LiveCheck (LiveCvarMatches (bottom), "refusal preserved bottomcolor cvar string/default/flags/value/callback");
	LiveCheck (!strcmp (cls.userinfo, client_info), "refusal preserved local userinfo store");
	LiveCheck (!strcmp (svs.serverinfo, server_info), "refusal preserved serverinfo store");
	LiveCheck (!strcmp (peer->name, peer_name) && peer->colors == peer_colors &&
		!strcmp (peer->userinfo, peer_info), "refusal preserved native peer state");
	LiveCheck (cls.message.cursize == queued_size &&
		cls.message.maxsize == queued_maxsize && !cls.message.overflowed &&
		!memcmp (cls.message.data, queued, queued_size),
		"refusal preserved every queued reliable byte");
	LiveCheck (live_autocvar_calls == autocvar_calls,
		"refusal produced no QC autocvar update");
}

static void LiveAttemptRefusal (const char *label, const char *command,
	client_t *peer, const live_cvar_snapshot_t *name,
	const live_cvar_snapshot_t *top, const live_cvar_snapshot_t *bottom,
	const char *client_info, const char *server_info, const char *peer_name,
	const char *peer_info, int peer_colors, const byte *queued, int queued_size,
	int queued_maxsize, unsigned autocvar_calls)
{
	qboolean handled;
	live_capture_autocvar = true;
	handled = Cmd_ExecuteString (command, src_command);
	live_capture_autocvar = false;
	LiveCheck (handled, va ("%s reached native console command owner", label));
	LiveCheckDeniedState (peer, name, top, bottom, client_info, server_info,
		peer_name, peer_info, peer_colors, queued, queued_size,
		queued_maxsize, autocvar_calls);
	fprintf (stdout, "METADATA_LIVE_REFUSAL stage=%s owner_reached=%d queued_unchanged=%d\n",
		label, handled, cls.message.cursize == queued_size &&
		!memcmp (cls.message.data, queued, queued_size));
}

static void LiveRetry (const char *stage, const char *command,
	const char *const *expected, unsigned expected_count)
{
	qboolean handled = Cmd_ExecuteString (command, src_command);
	LiveCheck (handled, va ("%s retry reached native console command owner", stage));
	LiveCheck (cls.message.cursize > 0,
		va ("%s retry queued native reliable command bytes", stage));
	CL_SendCmd ();
	LiveCheck (!cls.message.cursize,
		va ("%s retry reached local reliable transport", stage));
	LiveRunNativeFrame ();
	LiveCompareCommands (stage, expected, expected_count);
}

static void VerifyLiveAdmission (client_t *peer)
{
	live_cvar_snapshot_t name, top, bottom;
	char client_info[sizeof (cls.userinfo)];
	char server_info[sizeof (svs.serverinfo)];
	char peer_name[sizeof (peer->name)];
	char peer_info[sizeof (peer->userinfo)];
	byte queued[NET_MAXMESSAGE];
	int queued_size, queued_maxsize, peer_colors;
	unsigned autocvar_calls;
	const char *expected[3];
	char value[CLIENT_USER_INFO_STRING_SIZE];

	assert (cls.state == ca_connected && cls.netcon && cls.signon >= SIGNONS &&
		peer->active && peer->spawned);
	assert (!cls.message.cursize && !cls.message.overflowed);
	assert (cls.message.maxsize > 0 && cls.message.maxsize <= sizeof (queued));
	LiveCvarSnapshot (&name, &cl_name);
	LiveCvarSnapshot (&top, &cl_topcolor);
	LiveCvarSnapshot (&bottom, &cl_bottomcolor);
	q_strlcpy (client_info, cls.userinfo, sizeof (client_info));
	q_strlcpy (server_info, svs.serverinfo, sizeof (server_info));
	q_strlcpy (peer_name, peer->name, sizeof (peer_name));
	q_strlcpy (peer_info, peer->userinfo, sizeof (peer_info));
	peer_colors = peer->colors;
	autocvar_calls = live_autocvar_calls;

	/* The real reliable client buffer is held at its full native capacity.
	 * clc_nop bytes create pressure without inventing commands or server state. */
	hold_client_send = true;
	while (cls.message.cursize < cls.message.maxsize)
		MSG_WriteByte (&cls.message, clc_nop);
	assert (cls.message.cursize == cls.message.maxsize && !cls.message.overflowed);
	queued_size = cls.message.cursize;
	queued_maxsize = cls.message.maxsize;
	memcpy (queued, cls.message.data, queued_size);

	LiveAttemptRefusal ("set", "set topcolor 5", peer, &name, &top, &bottom,
		client_info, server_info, peer_name, peer_info, peer_colors, queued,
		queued_size, queued_maxsize, autocvar_calls);
	LiveAttemptRefusal ("seta", "seta topcolor 6", peer, &name, &top, &bottom,
		client_info, server_info, peer_name, peer_info, peer_colors, queued,
		queued_size, queued_maxsize, autocvar_calls);
	LiveCheck (!(cl_topcolor.flags & CVAR_SETA) &&
		cl_topcolor.flags == top.flags,
		"refused seta added no persistence flags");
	LiveAttemptRefusal ("name", "name refusal_name", peer, &name, &top, &bottom,
		client_info, server_info, peer_name, peer_info, peer_colors, queued,
		queued_size, queued_maxsize, autocvar_calls);
	LiveAttemptRefusal ("color", "color 3 4", peer, &name, &top, &bottom,
		client_info, server_info, peer_name, peer_info, peer_colors, queued,
		queued_size, queued_maxsize, autocvar_calls);
	LiveAttemptRefusal ("setinfo", "setinfo fixture_key refused", peer,
		&name, &top, &bottom, client_info, server_info, peer_name, peer_info,
		peer_colors, queued, queued_size, queued_maxsize, autocvar_calls);

	hold_client_send = false;
	CL_SendCmd ();
	LiveCheck (!cls.message.cursize,
		"held reliable pressure drained through native local transport");
	LiveRunNativeFrame ();
	LiveCheck (!live_native_command_count,
		"pressure drain contained no fabricated client commands");

	expected[0] = "color \"5\" \"0\"\n";
	LiveRetry ("set", "set topcolor 5", expected, 1);
	LiveCheck (!strcmp (cl_topcolor.string, "5") && peer->colors == 0x50,
		"set retry updated native peer color state");
	expected[0] = "color \"6\" \"0\"\n";
	LiveRetry ("seta", "seta topcolor 6", expected, 1);
	LiveCheck (!strcmp (cl_topcolor.string, "6") && peer->colors == 0x60 &&
		(cl_topcolor.flags & CVAR_SETA),
		"seta retry updated native peer and persistence flag");
	/* The frozen wrapper contract retains setter emissions before its tail:
	 * two complete name commands and three ordered color commands. */
	expected[0] = "name \"retry_name\"\n";
	expected[1] = "name \"retry_name\"\n";
	LiveRetry ("name", "name retry_name", expected, 2);
	LiveCheck (!strcmp (cl_name.string, "retry_name") &&
		!strcmp (peer->name, "retry_name"),
		"name retry updated native peer name");
	expected[0] = "color \"3\" \"0\"\n";
	expected[1] = "color \"3\" \"4\"\n";
	expected[2] = "color \"3\" \"4\"\n";
	LiveRetry ("color", "color 3 4", expected, 3);
	LiveCheck (!strcmp (cl_topcolor.string, "3") &&
		!strcmp (cl_bottomcolor.string, "4") && peer->colors == 0x34,
		"color retry updated both native peer color nibbles");
	expected[0] = "setinfo \"fixture_key\" \"retry_info\"\n";
	LiveRetry ("setinfo", "setinfo fixture_key retry_info", expected, 1);
	Info_GetKey (cls.userinfo, "fixture_key", value, sizeof (value));
	LiveCheck (!strcmp (value, "retry_info"),
		"setinfo retry updated local userinfo store");
	Info_GetKey (peer->userinfo, "fixture_key", value, sizeof (value));
	LiveCheck (!strcmp (value, "retry_info"),
		"setinfo retry updated native peer userinfo");

	if (!live_failures)
		puts ("METADATA_LIVE_ADMISSION_PASSED refusals=5 retries=5 reliable=local-native parser=native-commands");
	else
		printf ("METADATA_LIVE_ADMISSION_FAILED failures=%u refusals=5 retries=5 parser_commands=%u\n",
			live_failures, live_native_command_count);
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
	/* Select a permanent peer limit while the real metadata unit is being built,
	 * before SV_MetadataDrain checks admission. Do not replace its decision. */
	client_t *peer = &svs.clients[0];
	if (!strcmp (signon_limit, "permanent") && !limit_applied &&
		buf != &peer->message && value == svc_stufftext && peer->active &&
		peer->sendsignon == PRESPAWN_SIGNONMSG &&
		peer->metadata_serverinfo_pending && cls.signon == 1)
	{
		assert (buf->maxsize == NET_MAXMESSAGE && peer->message.maxsize > 1);
		peer->message.maxsize = 1;
		limit_applied = true;
	}
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
		else if (!strcmp (argv[i], "-live-admission")) live_admission = true;
	assert (mode);
#ifndef METADATA_LIVE_ADMISSION_FIXTURE
	/* Historical default links need no additional observers. Fail closed if
	 * the opt-in case is requested without its two native observer wrappers. */
	assert (!live_admission);
#endif
	assert (!strcmp (mode, "qsmi") || !strcmp (mode, "predinfo"));
	assert (!live_admission || (!strcmp (mode, "qsmi") &&
		!strcmp (signon_limit, "none") && !control_pressure));
	assert (!strcmp (signon_limit, "none") || !strcmp (signon_limit, "exact") ||
		!strcmp (signon_limit, "pressure") || !strcmp (signon_limit, "permanent"));
	/* Reuse the native _Host_Frame abort boundary. Only persistent/static engine
	 * state is inspected after a jump; changed automatic frame locals are not. */
	if (setjmp (host_abortserver))
	{
		assert (!strcmp (signon_limit, "permanent") && limit_applied &&
			disconnect_count == 1 && query_count && !signon2_count && !signon3_count);
		assert (cls.state == ca_disconnected && !cls.netcon && !cls.signon &&
			!sv.active && !svs.clients[0].active && !svs.clients[0].spawned);
		puts ("METADATA_NATIVE_PASSED offer=qsmi limit=permanent native-drop-disconnect-host-abort");
		return 0;
	}
	Fixture_InitNativeEngine (argc, argv, "e1m1", false);
	CL_Init (); /* Registers the native client metadata command handlers headlessly. */
	PR_SwitchQCVM (NULL);
	SetServerInfo (mode);
	cls.state = ca_connected;
	cls.demonum = -1;
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
	assert (strcmp (signon_limit, "permanent")); /* Permanent refusal must never spawn. */
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
	if (live_admission)
		VerifyLiveAdmission (peer);
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
	return live_admission && live_failures ? 4 : 0;
}
