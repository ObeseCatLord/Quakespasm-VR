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
#ifdef UPSTREAM_ACCEPTANCE_NATIVE_FIXTURE
static qboolean upstream_remote_signon, upstream_remote_can_send;
static int upstream_remote_send_result, upstream_remote_send_calls;
static int upstream_remote_sent_size;
static byte upstream_remote_sent[NET_MAXMESSAGE];
#endif
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
static qboolean metadata_lifecycle, metadata_downgrade;
static unsigned live_autocvar_calls, live_native_command_count, live_failures;
static qboolean live_native_command_overflow;
static char live_native_commands[16][2048];
static qboolean lifecycle_capture_publication;
static unsigned lifecycle_publication_packets, lifecycle_publication_commands;
#ifdef METADATA_LIVE_ADMISSION_FIXTURE
static qboolean lifecycle_capture_retirement;
static unsigned lifecycle_retirement_commands;
static char lifecycle_retirement_command[CLIENT_USER_INFO_STRING_SIZE + 128];
#endif
static float lifecycle_expected_autocvar;
static qboolean lifecycle_loaded_autocvar;
static int signon_restore_capacity;
static char signon_limit[16] = "none";

qboolean __real_NET_CanSendMessage (qsocket_t *socket);
qboolean __wrap_NET_CanSendMessage (qsocket_t *socket)
{
#ifdef UPSTREAM_ACCEPTANCE_NATIVE_FIXTURE
	if (upstream_remote_signon && socket == svs.clients[0].netconnection)
		return upstream_remote_can_send;
#endif
	if (hold_client_send && socket == cls.netcon)
		return false;
	return __real_NET_CanSendMessage (socket);
}

#ifdef UPSTREAM_ACCEPTANCE_NATIVE_FIXTURE
int __real_NET_SendMessage (qsocket_t *socket, sizebuf_t *message);
int __wrap_NET_SendMessage (qsocket_t *socket, sizebuf_t *message)
{
	if (!upstream_remote_signon || socket != svs.clients[0].netconnection)
		return __real_NET_SendMessage (socket, message);
	/* Only the transport result is controlled. The production sender owns
	 * chunk admission, reliable bytes, flush state, and its pending flag. */
	assert (upstream_remote_can_send && message->cursize <= 32000 && !message->overflowed);
	++upstream_remote_send_calls;
	upstream_remote_sent_size = message->cursize;
	memcpy (upstream_remote_sent, message->data, message->cursize);
	return upstream_remote_send_result;
}
#endif

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
	if (lifecycle_capture_retirement && src == src_server &&
		!strncmp (text, "fui 1 ", 6))
	{
		if (lifecycle_retirement_commands < 1)
			q_strlcpy (lifecycle_retirement_command, text,
				sizeof (lifecycle_retirement_command));
		++lifecycle_retirement_commands;
	}
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
		if (lifecycle_capture_publication)
		{
			unsigned commands = CountText (net_message.data, net_message.cursize, "//fui ");
			if (commands)
			{
				++lifecycle_publication_packets;
				lifecycle_publication_commands += commands;
			}
		}
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

#ifdef METADATA_LIVE_ADMISSION_FIXTURE
/* The lifecycle profile registers the native client userinfo cvars before
 * loading SSQC, so its generated AUTOCVAR binds to the real native cvar. */
static void Fixture_InitMetadataLifecycleEngine (int argc, char **argv,
	const char *map)
{
	static quakeparms_t parms;
	parms.basedir = ".";
	parms.argc = argc;
	parms.argv = argv;
	host_parms = &parms;
	COM_InitArgv (argc, argv);
	isDedicated = COM_CheckParm ("-dedicated") != 0;
	assert (isDedicated && COM_CheckParm ("-noudp"));
	assert (SDL_Init (0));
	Sys_Init ();
	Host_Init ();
	Cvar_SetQuick (&sv_coop_autosave, "0");
	CL_Init ();
	PR_SwitchQCVM (&sv.qcvm);
	SV_SpawnServer (map);
	assert (sv.active && qcvm == &sv.qcvm);
}
#endif

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

static float LiveBoundTopcolor (qboolean *found)
{
	qcvm_t *oldvm = qcvm;
	float value = 0;
	PR_SwitchQCVM (&sv.qcvm);
	ddef_t *global = ED_FindGlobal ("autocvar_topcolor");
	*found = global && (global->type & ~DEF_SAVEGLOBAL) == ev_float;
	if (*found)
		value = G_FLOAT (global->ofs);
	PR_SwitchQCVM (oldvm);
	return value;
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
	if (lifecycle_loaded_autocvar)
	{
		qboolean found = false;
		float value = LiveBoundTopcolor (&found);
		LiveCheck (found && value == lifecycle_expected_autocvar,
			"refusal preserved the real loaded SSQC AUTOCVAR value");
	}
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
	const char *const *expected, unsigned expected_count,
	qboolean expect_autocvar, float expected_bound_value)
{
	unsigned autocvar_calls = live_autocvar_calls;
	qboolean handled;
	live_capture_autocvar = true;
	handled = Cmd_ExecuteString (command, src_command);
	live_capture_autocvar = false;
	LiveCheck (handled, va ("%s retry reached native console command owner", stage));
	if (lifecycle_loaded_autocvar)
	{
		LiveCheck (live_autocvar_calls == autocvar_calls + (expect_autocvar ? 1 : 0),
			va ("%s retry reached real PR_AutoCvarChanged exactly once", stage));
		if (expect_autocvar)
		{
			qboolean found = false;
			float bound_value = LiveBoundTopcolor (&found);
			LiveCheck (found && bound_value == expected_bound_value,
				va ("%s retry updated the loaded SSQC AUTOCVAR", stage));
			lifecycle_expected_autocvar = expected_bound_value;
		}
	}
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
	qboolean bound_autocvar_found = false;
	const char *expected[3];
	char value[CLIENT_USER_INFO_STRING_SIZE];

	assert (cls.state == ca_connected && cls.netcon && cls.signon >= SIGNONS &&
		peer->active && peer->spawned);
	assert (!cls.message.cursize && !cls.message.overflowed);
	assert (cls.message.maxsize > 0 && cls.message.maxsize <= sizeof (queued));
	LiveCvarSnapshot (&name, &cl_name);
	LiveCvarSnapshot (&top, &cl_topcolor);
	LiveCvarSnapshot (&bottom, &cl_bottomcolor);
	if (metadata_lifecycle)
	{
		cvar_t *registered = Cvar_FindVar ("topcolor");
		lifecycle_expected_autocvar = LiveBoundTopcolor (&bound_autocvar_found);
		lifecycle_loaded_autocvar = bound_autocvar_found;
		LiveCheck (registered == &cl_topcolor && (registered->flags &
			(CVAR_REGISTERED | CVAR_USERINFO | CVAR_AUTOCVAR)) ==
			(CVAR_REGISTERED | CVAR_USERINFO | CVAR_AUTOCVAR),
			"loaded AUTOCVAR is bound to the registered native topcolor userinfo cvar");
		LiveCheck (bound_autocvar_found &&
			lifecycle_expected_autocvar == cl_topcolor.value,
			"loaded AUTOCVAR starts with the native topcolor value");
	}
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
	LiveRetry ("set", "set topcolor 5", expected, 1, true, 5);
	LiveCheck (!strcmp (cl_topcolor.string, "5") && peer->colors == 0x50,
		"set retry updated native peer color state");
	expected[0] = "color \"6\" \"0\"\n";
	LiveRetry ("seta", "seta topcolor 6", expected, 1, true, 6);
	LiveCheck (!strcmp (cl_topcolor.string, "6") && peer->colors == 0x60 &&
		(cl_topcolor.flags & CVAR_SETA),
		"seta retry updated native peer and persistence flag");
	/* The frozen wrapper contract retains setter emissions before its tail:
	 * two complete name commands and three ordered color commands. */
	expected[0] = "name \"retry_name\"\n";
	expected[1] = "name \"retry_name\"\n";
	LiveRetry ("name", "name retry_name", expected, 2, false, 0);
	LiveCheck (!strcmp (cl_name.string, "retry_name") &&
		!strcmp (peer->name, "retry_name"),
		"name retry updated native peer name");
	expected[0] = "color \"3\" \"0\"\n";
	expected[1] = "color \"3\" \"4\"\n";
	expected[2] = "color \"3\" \"4\"\n";
	LiveRetry ("color", "color 3 4", expected, 3, true, 3);
	LiveCheck (!strcmp (cl_topcolor.string, "3") &&
		!strcmp (cl_bottomcolor.string, "4") && peer->colors == 0x34,
		"color retry updated both native peer color nibbles");
	expected[0] = "setinfo \"fixture_key\" \"retry_info\"\n";
	LiveRetry ("setinfo", "setinfo fixture_key retry_info", expected, 1, false, 0);
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

#ifdef METADATA_LIVE_ADMISSION_FIXTURE
static void LifecycleDrainSlots (client_t *peer);

static void LifecycleServerinfoReset (client_t *peer)
{
	char saved[sizeof (svs.serverinfo)], saved_blob[SERVER_INFO_STRING_SIZE];
	char value[SERVER_INFO_STRING_SIZE];
	q_strlcpy (saved, svs.serverinfo, sizeof (saved));
	Info_GetKey (saved, "c02_blob", saved_blob, sizeof (saved_blob));
	assert (!peer->metadata_serverinfo_pending);
	svs.serverinfo[0] = 0;
	SV_MetadataServerinfoChanged ();
	LifecycleDrainSlots (peer);
	LiveCheck (!peer->metadata_serverinfo_pending && cl.serverinfo_received &&
		!cl.serverinfo[0], "empty full snapshot reset initialized serverinfo");

	Info_SetKey (svs.serverinfo, sizeof (svs.serverinfo), "lifecycle_reset", "updated");
	SV_MetadataServerinfoChanged ();
	LifecycleDrainSlots (peer);
	Info_GetKey (cl.serverinfo, "lifecycle_reset", value, sizeof (value));
	LiveCheck (!strcmp (value, "updated") && !peer->metadata_serverinfo_pending,
		"native metadata update after empty reset reached the client store");

	q_strlcpy (svs.serverinfo, saved, sizeof (svs.serverinfo));
	SV_MetadataServerinfoChanged ();
	LifecycleDrainSlots (peer);
	Info_GetKey (cl.serverinfo, "lifecycle_reset", value, sizeof (value));
	qboolean reset_key_removed = !*value;
	Info_GetKey (cl.serverinfo, "c02_blob", value, sizeof (value));
	LiveCheck (reset_key_removed && !strcmp (value, saved_blob),
		"restored public producer state replaced reset-era metadata without stale keys");
}

static int LifecycleSpawnQCSlot (client_t *recipient)
{
	qcvm_t *oldvm = qcvm;
	ddef_t *reference, *entity_global;
	func_t function;
	int entity_reference, entity_number, slot;
	host_client = recipient;
	PR_SwitchQCVM (&sv.qcvm);
	reference = ED_FindGlobal ("fixture_ref_lifecycle_spawn");
	entity_global = ED_FindGlobal ("fixture_lifecycle_bot");
	assert (reference && entity_global &&
		(reference->type & ~DEF_SAVEGLOBAL) == ev_function &&
		(entity_global->type & ~DEF_SAVEGLOBAL) == ev_entity);
	function = G_INT (reference->ofs);
	assert (function && function < (func_t)qcvm->progs->numfunctions);
	PR_ExecuteProgram (function);
	entity_reference = G_INT (entity_global->ofs);
	assert (entity_reference > 0 && entity_reference % qcvm->edict_size == 0);
	entity_number = entity_reference / qcvm->edict_size;
	assert (entity_number >= 1 && entity_number <= svs.maxclients);
	slot = entity_number - 1;
	PR_SwitchQCVM (oldvm);
	return slot;
}

static void LifecycleDropQCSlot (client_t *recipient)
{
	qcvm_t *oldvm = qcvm;
	ddef_t *reference;
	func_t function;
	host_client = recipient;
	PR_SwitchQCVM (&sv.qcvm);
	reference = ED_FindGlobal ("fixture_ref_lifecycle_drop");
	assert (reference && (reference->type & ~DEF_SAVEGLOBAL) == ev_function);
	function = G_INT (reference->ofs);
	assert (function && function < (func_t)qcvm->progs->numfunctions);
	PR_ExecuteProgram (function);
	PR_SwitchQCVM (oldvm);
}

static void LifecycleUpdateInfo (int edict, const char *key, const char *value)
{
	qcvm_t *oldvm = qcvm;
	PR_SwitchQCVM (&sv.qcvm);
	SV_UpdateInfo (edict, key, value);
	PR_SwitchQCVM (oldvm);
}

static void LifecycleSetSlot (int slot, const char *name, int colors,
	const char *payload, const char *private_value)
{
	client_t *source = &svs.clients[slot];
	char top[4], bottom[4], actual[CLIENT_USER_INFO_STRING_SIZE];
	assert (source->active && slot >= 0 && slot < MAX_SCOREBOARD);
	LifecycleUpdateInfo (slot + 1, "name", name);
	q_snprintf (top, sizeof (top), "%u", ((unsigned int)colors >> 4) & 15);
	q_snprintf (bottom, sizeof (bottom), "%u", (unsigned int)colors & 15);
	LifecycleUpdateInfo (slot + 1, "topcolor", top);
	LifecycleUpdateInfo (slot + 1, "bottomcolor", bottom);
	Info_SetKey (source->userinfo, sizeof (source->userinfo), "payload", payload);
	Info_SetKey (source->userinfo, sizeof (source->userinfo), "_private", private_value);
	Info_GetKey (source->userinfo, "payload", actual, sizeof (actual));
	assert (!strcmp (actual, payload));
	SV_MetadataUserinfoChanged (slot);
}

static void LifecycleDrainSlots (client_t *peer)
{
	for (int send = 0; send < 16; ++send)
	{
		char reason[128] = "metadata envelope cannot be published";
		int drained = SV_MetadataDrain (peer, true, reason, sizeof (reason));
		if (drained < 0)
		{
			fprintf (stderr, "METADATA_LIFECYCLE_DRAIN_REFUSED %s\n", reason);
			break;
		}
		if (peer->message.cursize)
		{
			if (!NET_CanSendMessage (peer->netconnection))
			{
				ReceiveClientPackets ();
				continue;
			}
			int sent = NET_SendMessage (peer->netconnection, &peer->message);
			if (sent == -1)
			{
				LiveCheck (false, "native local reliable transport admitted metadata output");
				break;
			}
			SZ_Clear (&peer->message);
			ReceiveClientPackets ();
		}
		if (drained > 0 && !peer->metadata_serverinfo_pending &&
			!peer->metadata_userinfo_dirty && !peer->message.cursize)
			break;
	}
	LiveCheck (!peer->metadata_serverinfo_pending && !peer->metadata_userinfo_dirty &&
		!peer->message.cursize && !peer->message.overflowed,
		"native sender drained all logical slot envelopes through reliable messages");
}

static void LifecycleCheckSlot (int slot, const char *name, int colors,
	const char *payload, const char *owned)
{
	char value[CLIENT_USER_INFO_STRING_SIZE], expected_top[4], expected_bottom[4];
	scoreboard_t *score = &cl.scores[slot];
	Info_GetKey (score->userinfo, "payload", value, sizeof (value));
	LiveCheck (!strcmp (value, payload), va ("slot %i retained its complete payload", slot));
	Info_GetKey (score->userinfo, "qc_owned", value, sizeof (value));
	LiveCheck (!strcmp (value, owned), va ("slot %i retained the QC-written value", slot));
	Info_GetKey (score->userinfo, "_private", value, sizeof (value));
	LiveCheck (!*value, va ("slot %i omitted its private userinfo", slot));
	Info_GetKey (score->userinfo, "name", value, sizeof (value));
	LiveCheck (!strcmp (score->name, name) && !strcmp (value, name),
		va ("slot %i native name companion matches its metadata store", slot));
	q_snprintf (expected_top, sizeof (expected_top), "%u",
		((unsigned int)colors >> 4) & 15);
	q_snprintf (expected_bottom, sizeof (expected_bottom), "%u",
		(unsigned int)colors & 15);
	Info_GetKey (score->userinfo, "topcolor", value, sizeof (value));
	LiveCheck (!strcmp (value, expected_top), va ("slot %i topcolor retained", slot));
	Info_GetKey (score->userinfo, "bottomcolor", value, sizeof (value));
	LiveCheck (!strcmp (value, expected_bottom) && score->colors == colors,
		va ("slot %i bottomcolor and native color companion retained", slot));
}

static void VerifyMetadataLifecycle (client_t *peer)
{
	static char payload[MAX_SCOREBOARD][4800];
	char name[MAX_SCOREBOARD][32];
	int colors[MAX_SCOREBOARD];
	unsigned before_commands, multi_packet_count;
	int reused_slot = 1;
	assert (cls.signon == SIGNONS && peer->spawned && !peer->message.cursize &&
		!peer->metadata_serverinfo_pending && !peer->metadata_userinfo_dirty);
	LifecycleServerinfoReset (peer);

	for (int slot = MAX_SCOREBOARD - 1; slot > 0; --slot)
	{
		int occupied = LifecycleSpawnQCSlot (peer);
		LiveCheck (occupied == slot && svs.clients[occupied].active &&
			svs.clients[occupied].spawned,
			"loaded QC spawnclient occupied the expected native slot");
	}

	for (int slot = 0; slot < MAX_SCOREBOARD; ++slot)
	{
		client_t *source = &svs.clients[slot];
		if (slot == 0)
		{
			q_strlcpy (name[slot], source->name, sizeof (name[slot]));
			colors[slot] = source->colors;
		}
		else
		{
			q_snprintf (name[slot], sizeof (name[slot]), "qcbot_%02i", slot);
			colors[slot] = ((slot % 14) << 4) | ((slot + 2) % 14);
			LifecycleUpdateInfo (slot + 1, "name", name[slot]);
			char top[4], bottom[4];
			q_snprintf (top, sizeof (top), "%i", (colors[slot] >> 4) & 15);
			q_snprintf (bottom, sizeof (bottom), "%i", colors[slot] & 15);
			LifecycleUpdateInfo (slot + 1, "topcolor", top);
			LifecycleUpdateInfo (slot + 1, "bottomcolor", bottom);
		}
		memset (payload[slot], 'A' + slot, sizeof (payload[slot]) - 1);
		payload[slot][sizeof (payload[slot]) - 1] = 0;
		Info_SetKey (source->userinfo, sizeof (source->userinfo), "payload", payload[slot]);
		Info_SetKey (source->userinfo, sizeof (source->userinfo), "_private", "never-published");
		if (slot == 0)
			Info_SetKey (source->userinfo, sizeof (source->userinfo), "qc_owned", "native-peer");
		SV_MetadataUserinfoChanged (slot);
	}

	lifecycle_publication_packets = lifecycle_publication_commands = 0;
	lifecycle_capture_publication = true;
	before_commands = userinfo_count;
	LifecycleDrainSlots (peer);
	lifecycle_capture_publication = false;
	LiveCheck (lifecycle_publication_packets >= 2 &&
		lifecycle_publication_commands == MAX_SCOREBOARD &&
		userinfo_count - before_commands == MAX_SCOREBOARD,
		"all 16 complete userinfo envelopes crossed at least two reliable packets");
	multi_packet_count = lifecycle_publication_packets;
	for (int slot = 0; slot < MAX_SCOREBOARD; ++slot)
		LifecycleCheckSlot (slot, name[slot], colors[slot], payload[slot],
			slot ? "occupied" : "native-peer");

	LifecycleDropQCSlot (peer);
	LiveCheck (!svs.clients[reused_slot].active &&
		(peer->metadata_userinfo_dirty & (1u << reused_slot)),
		"native QC drop retired the occupied slot and queued an empty envelope");
	lifecycle_publication_packets = lifecycle_publication_commands = 0;
	lifecycle_retirement_commands = 0;
	lifecycle_retirement_command[0] = 0;
	lifecycle_capture_publication = true;
	lifecycle_capture_retirement = true;
	LifecycleDrainSlots (peer);
	lifecycle_capture_retirement = false;
	lifecycle_capture_publication = false;
	char retired_value[CLIENT_USER_INFO_STRING_SIZE];
	Info_GetKey (cl.scores[reused_slot].userinfo, "payload", retired_value,
		sizeof (retired_value));
	qboolean payload_cleared = !*retired_value;
	Info_GetKey (cl.scores[reused_slot].userinfo, "qc_owned", retired_value,
		sizeof (retired_value));
	qboolean qc_key_cleared = !*retired_value;
	Info_GetKey (cl.scores[reused_slot].userinfo, "_private", retired_value,
		sizeof (retired_value));
	qboolean private_key_cleared = !*retired_value;
	Info_GetKey (cl.scores[reused_slot].userinfo, "name", retired_value,
		sizeof (retired_value));
	qboolean name_key_cleared = !*retired_value;
	Info_GetKey (cl.scores[reused_slot].userinfo, "topcolor", retired_value,
		sizeof (retired_value));
	qboolean zero_topcolor = !strcmp (retired_value, "0");
	Info_GetKey (cl.scores[reused_slot].userinfo, "bottomcolor", retired_value,
		sizeof (retired_value));
	qboolean zero_bottomcolor = !strcmp (retired_value, "0");
	LiveCheck (lifecycle_publication_commands == 1 &&
		!cl.scores[reused_slot].name[0] && !cl.scores[reused_slot].colors &&
		payload_cleared && qc_key_cleared && private_key_cleared &&
		name_key_cleared && zero_topcolor && zero_bottomcolor &&
		lifecycle_retirement_commands == 1 &&
		!strstr (lifecycle_retirement_command, "payload") &&
		!strstr (lifecycle_retirement_command, "qc_owned") &&
		!strstr (lifecycle_retirement_command, "never-published") &&
		strlen (lifecycle_retirement_command) < sizeof (retired_value),
		"retirement cleared the complete prior occupant from the native scoreboard");

	int new_slot = LifecycleSpawnQCSlot (peer);
	LiveCheck (new_slot == reused_slot && svs.clients[new_slot].active,
		"loaded QC reused the retired native slot");
	q_snprintf (name[new_slot], sizeof (name[new_slot]), "qc_reused_%02i", new_slot);
	colors[new_slot] = 0x35;
	LifecycleSetSlot (new_slot, name[new_slot], colors[new_slot], "replacement", "private-new");
	lifecycle_publication_packets = lifecycle_publication_commands = 0;
	lifecycle_capture_publication = true;
	LifecycleDrainSlots (peer);
	lifecycle_capture_publication = false;
	LifecycleCheckSlot (new_slot, name[new_slot], colors[new_slot], "replacement", "occupied");
	LiveCheck (lifecycle_publication_commands == 1,
		"reused slot published only the replacement occupant envelope");

	printf ("METADATA_LIFECYCLE_PASSED midsignon=1 slots=%i reliable_userinfo_packets=%u reset_empty=1 retire_reuse=1 loaded_autocvar=1\n",
		MAX_SCOREBOARD, multi_packet_count);
}
#endif /* METADATA_LIVE_ADMISSION_FIXTURE */

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
		buf->maxsize = buf->cursize + length + sv.signon->cursize +
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
		peer->message.maxsize = sv.signon->cursize + 2;
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

#ifdef UPSTREAM_ACCEPTANCE_NATIVE_FIXTURE
static void UpstreamRemoteRetained (client_t *peer, int index, const byte *bytes, int size)
{
	for (int frame = 0; frame < 8; ++frame)
	{
		SV_SendClientMessages ();
		assert (peer->active && peer->signonidx == index &&
			peer->message.cursize == size && !peer->message.overflowed &&
			!memcmp (peer->message.data, bytes, size));
	}
}

/* This is a sender/message-lifetime witness with a controlled remote socket
 * boundary. Complete remote two-process gameplay is qualified separately. */
static int UpstreamRemoteSignon (int argc, char **argv)
{
	byte chunk_bytes[31500], retained[NET_MAXMESSAGE], metadata_bytes[NET_MAXMESSAGE];
	sizebuf_t chunks[2] = {
		{.data = chunk_bytes, .maxsize = sizeof (chunk_bytes), .cursize = sizeof (chunk_bytes)},
		{.data = chunk_bytes, .maxsize = sizeof (chunk_bytes), .cursize = sizeof (chunk_bytes)}
	};
	sizebuf_t metadata = {.data = metadata_bytes, .maxsize = sizeof (metadata_bytes)};
	sizebuf_t *original[MAX_SIGNON_BUFFERS];
	char reason[128], blob[711];
	Fixture_InitNativeEngine (argc, argv, "e1m1", false);
	assert (svs.maxclients == 1);
	client_t *peer = &svs.clients[0];
	peer->netconnection = NET_NewQSocket ();
	assert (peer->netconnection && peer->edict);
	q_strlcpy (peer->netconnection->trueaddress, "REMOTE", sizeof (peer->netconnection->trueaddress));
	peer->active = true;
	peer->message.data = peer->msgbuf;
	peer->message.maxsize = sizeof (peer->msgbuf);
	peer->offered_metadata = QSVR_METADATA_VERSION;
	peer->sendsignon = PRESPAWN_SIGNONMSG;
	assert (peer->message.maxsize > 63000 && !SV_IsLocalClient (peer));
	const int original_count = sv.num_signon_buffers;
	memcpy (original, sv.signon_buffers, sizeof (original));
	memset (chunk_bytes, svc_nop, sizeof (chunk_bytes));
	sv.num_signon_buffers = 2;
	sv.signon_buffers[0] = &chunks[0]; sv.signon_buffers[1] = &chunks[1];
	SZ_Clear (&sv.reliable_datagram);
	svs.serverinfo[0] = 0;
	Info_SetKey (svs.serverinfo, sizeof (svs.serverinfo), "fixture", "small");
	peer->metadata_serverinfo_pending = true;
	upstream_remote_signon = true;
	SV_SendClientMessages ();
	const int size = peer->message.cursize;
	assert (peer->signonidx == 1 && peer->signon_chunk_pending &&
		peer->message.maxsize == 32000 &&
		!peer->metadata_serverinfo_pending && size > 31500 && size <= 32000 &&
		Contains (peer->message.data, size, "//fullserverinfo"));
	memcpy (retained, peer->message.data, size);
	UpstreamRemoteRetained (peer, 1, retained, size);
	assert (!upstream_remote_send_calls && peer->signon_chunk_pending);
	upstream_remote_can_send = true; /* Transport still rejects the send with zero. */
	UpstreamRemoteRetained (peer, 1, retained, size);
	assert (upstream_remote_send_calls == 8 && peer->signon_chunk_pending &&
		upstream_remote_sent_size == size && !memcmp (upstream_remote_sent, retained, size));
	upstream_remote_send_result = 1;
	SV_SendClientMessages ();
	assert (!peer->message.cursize && !peer->signon_chunk_pending && peer->signonidx == 1 &&
		peer->message.maxsize == sizeof (peer->msgbuf));
	/* No test code clears the pending flag: successful production sends do. */
	peer->message.maxsize = 31500;
	SV_SendClientMessages ();
	assert (peer->signonidx == 2 && peer->sendsignon == PRESPAWN_SIGNONMSG &&
		upstream_remote_sent_size == 31500 && !memcmp (upstream_remote_sent, chunk_bytes, 31500) &&
		!peer->message.cursize && !peer->signon_chunk_pending);
	peer->message.maxsize = 2;
	SV_SendClientMessages ();
	assert (upstream_remote_sent_size == 2 && upstream_remote_sent[0] == svc_signonnum &&
		upstream_remote_sent[1] == 2 && peer->sendsignon == PRESPAWN_DONE && !peer->message.cursize);
	puts ("UPSTREAM_REMOTE_SIGNON_RETAINED_PASSED cannot-send/zero-send frames exact-fit-chunk separate-two-byte-marker");

	/* Start another fixture signon. Metadata's real encoder determines the
	 * budget. An occupied envelope retries the same dirty unit without loss. */
	peer->sendsignon = PRESPAWN_SIGNONMSG;
	peer->signonidx = 0;
	memset (blob, 'm', sizeof (blob) - 1); blob[sizeof (blob) - 1] = 0;
	Info_SetKey (svs.serverinfo, sizeof (svs.serverinfo), "fixture", blob);
	peer->metadata_serverinfo_pending = true;
	assert (SV_MetadataServerUnit (peer, &metadata, reason, sizeof (reason)) && metadata.cursize > 500);
	peer->message.maxsize = metadata.cursize;
	MSG_WriteByte (&peer->message, svc_nop);
	upstream_remote_can_send = false;
	retained[0] = svc_nop;
	UpstreamRemoteRetained (peer, 0, retained, 1);
	assert (peer->metadata_serverinfo_pending && !peer->signon_chunk_pending);
	upstream_remote_can_send = true;
	SV_SendClientMessages ();
	assert (peer->metadata_serverinfo_pending && upstream_remote_sent_size == 1 && !peer->message.cursize);
	peer->message.maxsize = sizeof (peer->msgbuf);
	upstream_remote_can_send = false;
	SV_SendClientMessages ();
	assert (!peer->metadata_serverinfo_pending && !peer->signonidx && !peer->signon_chunk_pending &&
		peer->message.cursize == metadata.cursize && !memcmp (peer->message.data, metadata.data, metadata.cursize));
	memcpy (retained, peer->message.data, metadata.cursize);
	UpstreamRemoteRetained (peer, 0, retained, metadata.cursize);
	upstream_remote_can_send = true;
	SV_SendClientMessages ();
	assert (!peer->message.cursize && !peer->signonidx);
	SV_SendClientMessages ();
	assert (peer->signonidx == 1 && upstream_remote_sent_size == 31500 && !peer->signon_chunk_pending);
	SV_SendClientMessages ();
	assert (peer->signonidx == 2 && upstream_remote_sent_size == 31502 && peer->sendsignon == PRESPAWN_DONE);
	puts ("UPSTREAM_REMOTE_SIGNON_BUDGET_RETRY_PASSED metadata-dirty-retry metadata-consumes-remote-headroom successful-send-rearms");

	peer->sendsignon = PRESPAWN_SIGNONMSG;
	peer->signonidx = 0;
	peer->message.maxsize = 31499;
	assert (SV_StageSignonMessage (peer, reason, sizeof (reason)) == -1 &&
		!peer->signonidx && !peer->message.cursize && !peer->signon_chunk_pending &&
		strstr (reason, "reliable envelope"));
	peer->metadata_serverinfo_pending = true;
	peer->message.maxsize = metadata.cursize - 1;
	assert (SV_StageSignonMessage (peer, reason, sizeof (reason)) == -1 &&
		peer->metadata_serverinfo_pending && !peer->message.cursize && strstr (reason, "metadata unit"));
	memcpy (sv.signon_buffers, original, sizeof (original));
	sv.num_signon_buffers = original_count;
	puts ("UPSTREAM_REMOTE_SIGNON_NATIVE_PASSED real-sender/staging owners permanent-chunk/metadata-limits socket-boundary-controlled");
	return 0;
}
#endif

int main (int argc, char **argv)
{
#ifdef UPSTREAM_ACCEPTANCE_NATIVE_FIXTURE
	for (int i = 1; i < argc; ++i)
		if (!strcmp (argv[i], "-upstream-remote-signon")) return UpstreamRemoteSignon (argc, argv);
#endif
	const char *mode = NULL;
	qboolean mid_signon_changed = false;
	for (int i = 1; i + 1 < argc; ++i)
	{
		if (!strcmp (argv[i], "-metadata-offer")) mode = argv[i + 1];
		if (!strcmp (argv[i], "-metadata-limit"))
			q_strlcpy (signon_limit, argv[i + 1], sizeof (signon_limit));
	}
	for (int i = 1; i < argc; ++i)
		if (!strcmp (argv[i], "-control-pressure")) control_pressure = true;
		else if (!strcmp (argv[i], "-live-admission")) live_admission = true;
		else if (!strcmp (argv[i], "-metadata-lifecycle")) metadata_lifecycle = true;
		else if (!strcmp (argv[i], "-metadata-downgrade")) metadata_downgrade = true;
	if (metadata_lifecycle) live_admission = true;
	assert (mode);
#ifndef METADATA_LIVE_ADMISSION_FIXTURE
	/* Historical default links need no additional observers. Fail closed if
	 * the opt-in case is requested without its two native observer wrappers. */
	assert (!live_admission && !metadata_lifecycle && !metadata_downgrade);
#endif
	assert (!strcmp (mode, "qsmi") || !strcmp (mode, "predinfo"));
	assert (!live_admission || (!strcmp (mode, "qsmi") &&
		!strcmp (signon_limit, "none") && !control_pressure));
	assert (!metadata_lifecycle || (!strcmp (mode, "qsmi") &&
		!strcmp (signon_limit, "none") && !control_pressure));
	assert (!metadata_downgrade || (!strcmp (mode, "predinfo") &&
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
	if (metadata_lifecycle)
	{
#ifdef METADATA_LIVE_ADMISSION_FIXTURE
		Fixture_InitMetadataLifecycleEngine (argc, argv, "e1m1");
#else
		assert (!"metadata lifecycle fixture was not compiled");
#endif
	}
	else
	{
		Fixture_InitNativeEngine (argc, argv, "e1m1", false);
		CL_Init (); /* Registers native client metadata handlers headlessly. */
	}
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
		if (metadata_lifecycle && !mid_signon_changed && cls.signon == 2)
		{
			assert (!peer->metadata_serverinfo_pending);
			Info_SetKey (svs.serverinfo, sizeof (svs.serverinfo), "c02_blob",
				"mid-signon-latest");
			SV_MetadataServerinfoChanged ();
			assert (peer->metadata_serverinfo_pending);
			mid_signon_changed = true;
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
	if (metadata_lifecycle)
	{
		char value[SERVER_INFO_STRING_SIZE];
		assert (mid_signon_changed && cls.signon == SIGNONS &&
			!peer->metadata_serverinfo_pending);
		Info_GetKey (cl.serverinfo, "c02_blob", value, sizeof (value));
		LiveCheck (!strcmp (value, "mid-signon-latest") && full_count >= 2,
			"mid-signon producer change crossed the next complete native snapshot");
	LiveCheck (!strcmp (Info_GetKey (cl.serverinfo, "c02_blob", value, sizeof (value)),
		"mid-signon-latest"),
		"server and client expose the same mid-signon public value");
	}
	for (int slot = 0; slot < MAX_SCOREBOARD; ++slot)
		assert (!cl.scores[slot].colors || slot == 0);
	if (live_admission)
		VerifyLiveAdmission (peer);
	if (!strcmp (mode, "qsmi")) VerifyUserinfoPublication (peer);
#ifdef METADATA_LIVE_ADMISSION_FIXTURE
	if (metadata_lifecycle) VerifyMetadataLifecycle (peer);
#endif
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
		if (metadata_downgrade)
		{
			assert (cl.serverinfo_received);
			printf ("METADATA_DOWNGRADE_INIT_PASSED empty_full=%u updates=%u serverinfo_received=%d\n",
				empty_full_count, increment_count, cl.serverinfo_received);
		}
	}
	printf ("METADATA_NATIVE_PASSED offer=%s limit=%s control=%d frames=%d query=%u pextknown=%d predinfo=%d metadata=%u full=%u empty=%u svi=%u fui=%u signon2=%u signon3=%u spawned=%d scoreboard=%s graphics=excluded\n",
		mode, signon_limit, control_pressure, frame, query_count, peer->pextknown,
		!!(peer->protocol_pext2 & PEXT2_PREDINFO), peer->offered_metadata,
		full_count, empty_full_count, increment_count, userinfo_count,
		signon2_count, signon3_count, peer->spawned,
		!strcmp (mode, "qsmi") ? "near-full-ordered-overlay" : "all-16-slots");
	return live_admission && live_failures ? 4 : 0;
}
