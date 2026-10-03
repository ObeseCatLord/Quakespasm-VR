/* D03/D04 native server parser qualification with stock map/QC plus appended
 * loaded SSQC handlers. Only the synthetic message source is wrapped; the real
 * SV_RunClients, SV_ReadClientMessage, VM, command dispatcher and builtins run. */
#include "native_engine_fixture.h"
#include "../Quake/net_sys.h"
#include "../Quake/net_defs.h"
#include <assert.h>

#define FIXTURE_CLIENTS 4
#define FIXTURE_PACKET_CAPACITY 4096

static struct qsocket_s synthetic_sockets[FIXTURE_CLIENTS];
static byte queued_bytes[FIXTURE_PACKET_CAPACITY];
static size_t queued_size;
static struct qsocket_s *queued_socket;
static qboolean queued_message;
static qboolean count_drops;
static int drop_calls;
static int disconnect_calls;
static int native_control_calls;
static int native_retire_calls;
static int native_tail_calls;

struct qsocket_s *__wrap_NET_GetServerMessage (void)
{
	if (!queued_message)
		return NULL;
	queued_message = false;
	net_message.data = queued_bytes;
	net_message.maxsize = sizeof (queued_bytes);
	net_message.cursize = (int)queued_size;
	net_message.allowoverflow = false;
	return queued_socket;
}

qboolean __wrap_NET_CanSendMessage (struct qsocket_s *socket)
{
	(void)socket;
	return false; /* This fixture never delivers packets or opens a connection. */
}

void __real_SV_DropClient (qboolean crash);
void __wrap_SV_DropClient (qboolean crash)
{
	if (count_drops)
		++drop_calls;
	__real_SV_DropClient (crash);
}

void __real_PR_ExecuteProgram (func_t function);
void __wrap_PR_ExecuteProgram (func_t function)
{
	if (count_drops && qcvm == &sv.qcvm &&
		function == pr_global_struct->ClientDisconnect)
		++disconnect_calls;
	__real_PR_ExecuteProgram (function);
}

static float FixtureFloat (const char *name)
{
	ddef_t *definition = ED_FindGlobal (name);
	assert (definition && (definition->type & ~DEF_SAVEGLOBAL) == ev_float);
	return G_FLOAT (definition->ofs);
}

static int FixtureInteger (const char *name)
{
	ddef_t *definition = ED_FindGlobal (name);
	assert (definition);
	return G_INT (definition->ofs);
}

static void SetFixtureFloat (const char *name, float value)
{
	ddef_t *definition = ED_FindGlobal (name);
	assert (definition && (definition->type & ~DEF_SAVEGLOBAL) == ev_float);
	G_FLOAT (definition->ofs) = value;
}

static void FixtureNativeControl (void)
{
	++native_control_calls;
}

static void FixtureNativeRetire (void)
{
	dfunction_t *function = ED_FindFunction ("fixture_server_request_native_replace");
	assert (function && function->first_statement > 0 && host_client && host_client->active);
	++native_retire_calls;
	pr_global_struct->self = EDICT_TO_PROG (host_client->edict);
	PR_ExecuteProgram (function - qcvm->functions);
}

static void FixtureNativeTail (void)
{
	++native_tail_calls;
}

static void RegisterNativeCommands (void)
{
	assert (Cmd_AddCommand_ClientCommand ("fixture_native_control", FixtureNativeControl));
	assert (Cmd_AddCommand_ClientCommand ("fixture_native_retire", FixtureNativeRetire));
	assert (Cmd_AddCommand_ClientCommand ("fixture_native_tail", FixtureNativeTail));
}

static void InitializeSyntheticSocket (struct qsocket_s *socket)
{
	memset (socket, 0, sizeof (*socket));
	socket->disconnected = true; /* NET_Close stays offline and has no driver call. */
	q_strlcpy (socket->trueaddress, "offline server request fixture",
		sizeof (socket->trueaddress));
	q_strlcpy (socket->maskedaddress, "offline server request fixture",
		sizeof (socket->maskedaddress));
}

static client_t *PrepareRequester (void)
{
	client_t *client = &svs.clients[0];
	InitializeSyntheticSocket (&synthetic_sockets[0]);
	client->netconnection = &synthetic_sockets[0];
	host_client = client;
	SV_ConnectClient (0);
	client = &svs.clients[0];
	assert (client->active && client->netconnection == &synthetic_sockets[0]);
	assert (client->edict && !client->edict->free);
	client->spawned = true; /* Exercise the real loaded ClientDisconnect callback. */
	client->knowntoqc = true;
	client->edict->v.movetype = MOVETYPE_NONE;
	q_strlcpy (client->name, "request-fixture", sizeof (client->name));
	return client;
}

static void PrepareBotSlots (void)
{
	dfunction_t *fill = ED_FindFunction ("fixture_server_request_fill_bots");
	assert (fill && fill->first_statement > 0 && svs.maxclients == FIXTURE_CLIENTS);
	pr_global_struct->self = EDICT_TO_PROG (qcvm->edicts);
	PR_ExecuteProgram (fill - qcvm->functions);
	for (int slot = 1; slot < FIXTURE_CLIENTS; ++slot)
	{
		assert (svs.clients[slot].active && svs.clients[slot].spawned &&
			!svs.clients[slot].netconnection);
		svs.clients[slot].edict->v.movetype = MOVETYPE_NONE;
	}
	assert (net_activeconnections == FIXTURE_CLIENTS - 1);
}

static void PacketStart (sizebuf_t *packet, byte *bytes)
{
	memset (packet, 0, sizeof (*packet));
	packet->data = bytes;
	packet->maxsize = FIXTURE_PACKET_CAPACITY;
}

static void QueuePacket (client_t *client, const sizebuf_t *packet)
{
	assert (client && client->active && client->netconnection && !queued_message);
	assert (packet->cursize > 0 && (size_t)packet->cursize <= sizeof (queued_bytes));
	memcpy (queued_bytes, packet->data, packet->cursize);
	queued_size = (size_t)packet->cursize;
	queued_socket = client->netconnection;
	queued_message = true;
	host_client = client;
	SV_RunClients (); /* Actual network receive loop and per-frame client owner. */
	assert (!queued_message);
}

static void WriteStringCommand (sizebuf_t *packet, const char *command)
{
	MSG_WriteByte (packet, clc_stringcmd);
	MSG_WriteString (packet, command);
}

static int StringCommandBytes (const char *command)
{
	return 1 + (int)strlen (command) + 1;
}

static void WriteEntityRequest (sizebuf_t *packet, unsigned int entity,
	const char *event)
{
	MSG_WriteByte (packet, clcfte_qcrequest);
	MSG_WriteByte (packet, ev_entity);
	MSG_WriteEntity (packet, entity, 0);
	MSG_WriteByte (packet, ev_void);
	MSG_WriteString (packet, event);
}

static void CheckRequestAlignment (client_t *client, const char *event,
	unsigned int entity, qboolean expected_handler, int expected_offset)
{
	byte bytes[FIXTURE_PACKET_CAPACITY];
	sizebuf_t packet;
	const float before_events = FixtureFloat ("fixture_server_request_event_calls");
	const float before_hooks = FixtureFloat ("fixture_server_request_hook_calls");
	PacketStart (&packet, bytes);
	WriteEntityRequest (&packet, entity, event);
	WriteStringCommand (&packet, "fixture_qc_control");
	QueuePacket (client, &packet);
	assert (msg_badread && msg_readcount == packet.cursize);
	assert (FixtureFloat ("fixture_server_request_event_calls") ==
		before_events + (expected_handler ? 1 : 0));
	assert (FixtureFloat ("fixture_server_request_hook_calls") == before_hooks + 1);
	if (expected_handler)
		assert (FixtureInteger ("fixture_server_request_event_entity") == expected_offset);
}

static void RunMalformedPrefix (client_t *client, const char *prefix,
	qboolean disable_qc_hook, qboolean check_avatar)
{
	byte bytes[FIXTURE_PACKET_CAPACITY];
	sizebuf_t packet;
	const float before_hooks = FixtureFloat ("fixture_server_request_hook_calls");
	const int before_native = native_control_calls;
	PacketStart (&packet, bytes);
	MSG_WriteByte (&packet, clc_stringcmd);
	SZ_Write (&packet, prefix, (int)strlen (prefix)); /* Deliberately no NUL. */
	assert (!packet.overflowed);
	const func_t saved_hook = qcvm->extfuncs.SV_ParseClientCommand;
	if (disable_qc_hook)
		qcvm->extfuncs.SV_ParseClientCommand = 0;
	QueuePacket (client, &packet);
	qcvm->extfuncs.SV_ParseClientCommand = saved_hook;
	assert (msg_badread && !client->active);
	assert (FixtureFloat ("fixture_server_request_hook_calls") == before_hooks);
	assert (native_control_calls == before_native);
	if (check_avatar)
		assert (!client->avatar_capable);
}

static void CheckSingleDrop (client_t *client, const char *command,
	qboolean native_dispatch, qboolean replace, sizebuf_t *packet)
{
	const int before_drops = drop_calls;
	const int before_disconnects = disconnect_calls;
	const int before_connections = net_activeconnections;
	const float before_tail = FixtureFloat ("fixture_server_request_tail_calls");
	const int before_native_tail = native_tail_calls;
	const func_t saved_hook = qcvm->extfuncs.SV_ParseClientCommand;
	if (native_dispatch)
		qcvm->extfuncs.SV_ParseClientCommand = 0;
	count_drops = true;
	QueuePacket (client, packet);
	count_drops = false;
	qcvm->extfuncs.SV_ParseClientCommand = saved_hook;
	assert (drop_calls - before_drops == 1);
	assert (disconnect_calls - before_disconnects == 1);
	if (replace)
	{
		assert (client->active && !client->netconnection && client->spawned);
		assert (net_activeconnections == before_connections);
		assert (msg_readcount == StringCommandBytes (native_dispatch ?
			"fixture_native_retire" : "fixture_qc_replace"));
		if (native_dispatch)
		{
			assert (native_retire_calls == 1 && native_tail_calls == before_native_tail);
			assert (FixtureFloat ("fixture_server_request_native_replacement_calls") == 1);
		}
		else
			assert (FixtureFloat ("fixture_server_request_tail_calls") == before_tail);
	}
	else
	{
		assert (!client->active && net_activeconnections == before_connections - 1);
		assert (msg_readcount == StringCommandBytes ("fixture_qc_drop"));
		assert (FixtureFloat ("fixture_server_request_tail_calls") == before_tail);
	}
}

static void ScenarioReport (const char *name)
{
	printf ("SERVER_REQUEST_CASE_PASSED %s\n", name);
}

int main (int argc, char **argv)
{
	byte bytes[FIXTURE_PACKET_CAPACITY];
	sizebuf_t packet;
	Fixture_InitNativeEngine (argc, argv, "e1m1", true);
	Cvar_SetQuick (&pr_checkextension, "1");
	PR_EnableExtensions (qcvm->globaldefs);
	assert (qcvm->extfuncs.SV_ParseClientCommand);
	assert (ED_FindFunction ("CSEv_fixture_probe_e"));
	assert (ED_FindFunction ("fixture_server_request_native_replace"));
	RegisterNativeCommands ();
	PrepareBotSlots ();
	client_t *client = PrepareRequester ();
	SetFixtureFloat ("fixture_server_request_event_calls", 0);
	SetFixtureFloat ("fixture_server_request_hook_calls", 0);
	SetFixtureFloat ("fixture_server_request_tail_calls", 0);
	SetFixtureFloat ("fixture_server_request_native_replacement_calls", 0);

	edict_t *stale = ED_Alloc ();
	const int stale_number = NUM_FOR_EDICT (stale);
	assert (stale_number > svs.maxclients && stale_number < qcvm->num_edicts);
	ED_Free (stale);
	assert (stale->free);
	CheckRequestAlignment (client, "fixture_probe", (unsigned)stale_number,
		true, (int)((byte *)stale - (byte *)qcvm->edicts));
	assert (stale->free);
	ScenarioReport ("bounded_freed_entity_present_handler");

	CheckRequestAlignment (client, "fixture_absent", (unsigned)stale_number,
		false, 0);
	assert (stale->free);
	ScenarioReport ("bounded_freed_entity_absent_handler");

	CheckRequestAlignment (client, "fixture_probe", (unsigned)qcvm->num_edicts,
		true, 0);
	ScenarioReport ("out_of_range_entity_world_fallback");

	float before_hooks = FixtureFloat ("fixture_server_request_hook_calls");
	PacketStart (&packet, bytes);
	WriteStringCommand (&packet, "fixture_qc_control");
	QueuePacket (client, &packet);
	assert (msg_badread && msg_readcount == packet.cursize);
	assert (FixtureFloat ("fixture_server_request_hook_calls") == before_hooks + 1);
	ScenarioReport ("terminated_qc_control");

	func_t saved_hook = qcvm->extfuncs.SV_ParseClientCommand;
	qcvm->extfuncs.SV_ParseClientCommand = 0;
	PacketStart (&packet, bytes);
	WriteStringCommand (&packet, "fixture_native_control");
	QueuePacket (client, &packet);
	qcvm->extfuncs.SV_ParseClientCommand = saved_hook;
	assert (msg_badread && msg_readcount == packet.cursize && native_control_calls == 1);
	ScenarioReport ("terminated_native_control");

	RunMalformedPrefix (client, "avatar_cap 1", false, true);
	assert (client->active == false);
	ScenarioReport ("eof_capability_prefix_has_no_effect");
	client = PrepareRequester ();

	RunMalformedPrefix (client, "fixture_qc_control", false, false);
	ScenarioReport ("eof_qc_prefix_has_no_effect");
	client = PrepareRequester ();

	RunMalformedPrefix (client, "fixture_native_control", true, false);
	assert (native_control_calls == 1);
	ScenarioReport ("eof_native_prefix_has_no_effect");
	client = PrepareRequester ();

	PacketStart (&packet, bytes);
	WriteStringCommand (&packet, "fixture_qc_drop");
	WriteStringCommand (&packet, "fixture_qc_tail");
	CheckSingleDrop (client, "fixture_qc_drop", false, false, &packet);
	ScenarioReport ("qc_self_drop_exact_once_and_tail_retired");
	client = PrepareRequester ();

	PacketStart (&packet, bytes);
	WriteStringCommand (&packet, "fixture_qc_replace");
	WriteStringCommand (&packet, "fixture_qc_tail");
	CheckSingleDrop (client, "fixture_qc_replace", false, true, &packet);
	ScenarioReport ("qc_immediate_slot_replacement_retains_no_tail");

	/* Retire the bot used as the replacement, then restore a synthetic original
	 * requester so the ordinary Cmd_ExecuteString branch can be tested too. */
	host_client = client;
	SV_DropClient (true);
	client = PrepareRequester ();
	PacketStart (&packet, bytes);
	WriteStringCommand (&packet, "fixture_native_control");
	qcvm->extfuncs.SV_ParseClientCommand = 0;
	QueuePacket (client, &packet);
	assert (msg_badread && msg_readcount == packet.cursize && native_control_calls == 2);
	ScenarioReport ("native_dispatch_control");

	PacketStart (&packet, bytes);
	WriteStringCommand (&packet, "fixture_native_retire");
	WriteStringCommand (&packet, "fixture_native_tail");
	CheckSingleDrop (client, "fixture_native_retire", true, true, &packet);
	qcvm->extfuncs.SV_ParseClientCommand = saved_hook;
	ScenarioReport ("native_dispatch_immediate_replacement_retains_no_tail");

	puts ("SERVER_REQUEST_NATIVE_PASSED actual SV_RunClients/SV_ReadClientMessage, loaded QC, assertion-enabled bounded entity decoding, native dropclient/spawnclient");
	return 0;
}
