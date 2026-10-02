/* Native loopback endpoint ownership at the fatal server relay boundary. */
#include "../Quake/net_loop.h"

static void Voice_FatalSendNativeChecks(client_t **peers,
	client_state_t **states, const char *offer)
{
	client_state_t *retired = states[1];
	int original_connections = net_activeconnections;
	qsocket_t *paired_client, *paired_server;
	int saved_driver;
	int16_t pcm[VOICE_FRAME_SAMPLES];
	uint64_t source_serial;
	unsigned int source_generation;

	assert(voice_fixture_send_result == 1 && peers[0]->active &&
		peers[1]->active && !voice_sending && !voice_ptt);
	cl = *states[0];
	cls.netcon = peers[0]->netconnection;
	/* Retire only the pre-existing driverless test endpoint by its old pattern. */
	NET_FreeQSocket(peers[1]->netconnection);
	host_client = peers[1];
	SV_DropClient(false);
	assert(!peers[1]->active && !peers[1]->netconnection &&
		net_activeconnections == original_connections - 1);
	assert(net_numdrivers > 0 && net_drivers[0].initialized);
	saved_driver = net_driverlevel;
	net_driverlevel = 0;
	paired_client = Loop_Connect("local");
	net_driverlevel = saved_driver;
	assert(paired_client && !paired_client->disconnected &&
		paired_client->driver == 0 && paired_client->driverdata);
	paired_server = paired_client->driverdata;
	assert(paired_server->driver == 0 &&
		paired_server->driverdata == paired_client && !paired_server->disconnected);
	published_voice_length[1] = 0;
	SV_CheckForNewClients();
	assert(peers[1]->active && peers[1]->netconnection == paired_server &&
		net_activeconnections == original_connections);
	/* Reuse the existing native offer/header/QC spawn owners after real admission. */
	host_client = peers[1];
	SZ_Clear(&peers[1]->message);
	Cmd_ExecuteString(offer, src_client);
	assert(peers[1]->pextknown);
	Header(peers[1], 0);
	sv_player = peers[1]->edict;
	Cmd_ExecuteString("name public", src_client);
	Cmd_ExecuteString("spawn", src_client);
	Cmd_ExecuteString("begin", src_client);
	assert(peers[1]->spawned && peers[1]->knowntoqc &&
		peers[1]->edict->v.health > 0 && !peers[1]->edict->free);
	states[1] = CreateMixedPeerState(peers[1], 1);
	NegotiateVoice(peers[1], states[1]);
	WarmVoiceCommands(peers[1], states[1]);
	Mem_Free(retired->entities);
	Mem_Free(retired->scores);
	Mem_Free(retired);
	for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
		pcm[i] = (i / 96) & 1 ? 12000 : -12000;
	SendControlledFrame(peers[0], states[0], pcm);
	source_serial = peers[0]->voice_next_serial;
	source_generation = peers[0]->voice_generation;
	assert(peers[1]->voice_relay_serial[0] < source_serial);
	cl = *states[1];
	cls.netcon = paired_client;
	captured_length = captured_sends = 0;
	voice_fixture_send_result = -1;
	qboolean sent = SV_SendPendingVoice(peers[1]);
	voice_fixture_send_result = 1;
	*states[1] = cl;
	assert(!sent && captured_sends == 1 &&
		captured_length >= VOICE_SVC_HEADER_BYTES);
	assert(!peers[1]->active && !peers[1]->netconnection &&
		net_activeconnections == original_connections - 1 &&
		paired_server->disconnected && !paired_client->driverdata &&
		!paired_client->disconnected);
	assert(peers[0]->active && peers[0]->netconnection &&
		peers[0]->voice_generation == source_generation &&
		peers[0]->voice_next_serial == source_serial);
	cl = *states[0];
	cls.netcon = peers[0]->netconnection;
	NET_Close(paired_client);
	assert(paired_client->disconnected);

	/* Reuse the established driverless fixture for subsequent ordinary checks. */
	retired = states[1];
	published_voice_length[1] = 0;
	peers[1] = SpawnPeer(1, offer, 0);
	states[1] = CreateMixedPeerState(peers[1], 1);
	NegotiateVoice(peers[1], states[1]);
	WarmVoiceCommands(peers[1], states[1]);
	Mem_Free(retired->entities);
	Mem_Free(retired->scores);
	Mem_Free(retired);
	assert(net_activeconnections == original_connections &&
		peers[1]->voice_relay_generation[0] == source_generation &&
		peers[1]->voice_relay_serial[0] == source_serial);
	SendControlledFrame(peers[0], states[0], pcm);
	RelayToReceiver(peers[1], states[1]);
	PlayUntilBuffered();
	assert(UnreadSpeakerHasSignal());
	MixHasSignal(false);
	*states[1] = cl;
	cl = *states[0];
	cls.netcon = peers[0]->netconnection;
	assert(voice_fixture_send_result == 1 && !voice_sending && !voice_ptt &&
		!cl.voice_outgoing_count);
}
