/* Actual public-heartbeat voice send and native disconnect ownership. */
#ifndef VOICE_CLIENT_SEND_NATIVE_FIXTURE_H
#define VOICE_CLIENT_SEND_NATIVE_FIXTURE_H
#include "../Quake/net_loop.h"

static void Voice_ClientReadAttempt(const voice_packet_t *expected)
{
	sizebuf_t saved_message = net_message;
	int saved_readcount = msg_readcount, payload_bytes;
	qboolean saved_badread = msg_badread;
	assert(captured_length >= VOICE_CLC_HEADER_BYTES &&
		captured_length <= VOICE_CLIENT_DATAGRAM_BUDGET);
	net_message.data = captured; net_message.maxsize = net_message.cursize = captured_length;
	MSG_BeginReading();
	assert(MSG_ReadByte() == clc_voice && (uint16_t)MSG_ReadShort() == expected->sequence);
	assert((uint32_t)MSG_ReadLong() == expected->timestamp &&
		MSG_ReadByte() == expected->talkspurt && MSG_ReadByte() == expected->flags);
	payload_bytes = MSG_ReadShort();
	assert(payload_bytes == expected->payload_bytes && payload_bytes >= 0);
	for (int i = 0; i < payload_bytes; ++i)
		assert(MSG_ReadByte() == expected->payload[i]);
	assert(!msg_badread && msg_readcount == net_message.cursize);
	net_message = saved_message; msg_readcount = saved_readcount; msg_badread = saved_badread;
}

static void Voice_ClientDeliver(client_t *source)
{
	sizebuf_t saved_message = net_message;
	int saved_readcount = msg_readcount;
	qboolean saved_badread = msg_badread;
	GapDeliver(source, captured, captured_length);
	net_message = saved_message; msg_readcount = saved_readcount; msg_badread = saved_badread;
}

static void Voice_ClientCheckServerPacket(client_t *source, uint64_t serial,
	const voice_packet_t *expected)
{
	server_voice_packet_t *actual = SV_VoicePacketForSerial(source, serial);
	assert(actual && actual->packet.sequence == expected->sequence &&
		actual->packet.timestamp == expected->timestamp && actual->packet.talkspurt == expected->talkspurt &&
		actual->packet.flags == expected->flags && actual->packet.payload_bytes == expected->payload_bytes &&
		!memcmp(actual->packet.payload, expected->payload, expected->payload_bytes));
}

static void Voice_ClientReceiveRelay(client_t *receiver, client_state_t *state,
	voice_fixture_datagram_t *packet)
{
	sizebuf_t saved_message = net_message;
	qsocket_t *saved_netcon = cls.netcon;
	int saved_readcount = msg_readcount;
	qboolean saved_badread = msg_badread;
	cl = *state; cls.netcon = receiver->netconnection;
	net_message.data = packet->bytes; net_message.cursize = packet->length;
	net_message.maxsize = sizeof(packet->bytes);
	CL_ParseServerMessage();
	/* The native parser reads the -1 EOF sentinel and leaves msg_badread set. */
	assert(msg_readcount == net_message.cursize);
	*state = cl; cls.netcon = saved_netcon;
	net_message = saved_message; msg_readcount = saved_readcount; msg_badread = saved_badread;
}

static void Voice_ClientSendNativeChecks(client_t *source, client_state_t *source_state,
	client_t *receiver, client_state_t *receiver_state)
{
	qsocket_t *saved_netcon = cls.netcon;
	int16_t pcm[VOICE_FRAME_SAMPLES];
	voice_packet_t active, end, queued[VOICE_CLIENT_QUEUE_CAPACITY];
	voice_fixture_datagram_t relay;
	uint64_t serial;
	unsigned int head, count;
	qboolean decoded = false;
	assert(source->active && receiver->active && source != receiver && source->voice_capable &&
		receiver->voice_capable && voice_capture_device && !source_state->voice_outgoing_count &&
		!voice_sending && !voice_ptt && !voice_speakers[0].jitter.count &&
		Voice_AtomicGet(&voice_speakers[0].pcm_read) == Voice_AtomicGet(&voice_speakers[0].pcm_write));
	for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i) pcm[i] = (i / 96) & 1 ? 12000 : -12000;
	cl = *source_state; cls.state = ca_connected; cls.signon = SIGNONS;
	cls.netcon = source->netconnection;
	assert(!cl.protocol_qsvr && CL_VoiceTransportAvailable() && voice_fixture_send_result == 1);
	Key_Event(K_F12, true); Voice_EncodeCaptureFrame(pcm);
	assert(voice_ptt && Voice_InputLevel() > 0 && cl.voice_outgoing_count == 1);
	active = cl.voice_outgoing[cl.voice_outgoing_head];
	assert(Voice_PacketIsValid(&active) && (active.flags & VOICE_FLAG_START));
	head = cl.voice_outgoing_head; count = cl.voice_outgoing_count;
	memcpy(queued, cl.voice_outgoing, sizeof(queued)); serial = source->voice_next_serial;
	captured_length = captured_sends = 0; voice_fixture_send_result = 0;
	CL_SendMove(NULL); voice_fixture_send_result = 1;
	assert(captured_sends == 1 && source->active && cls.state == ca_connected &&
		cls.netcon == source->netconnection && cl.voice_outgoing_head == head &&
		cl.voice_outgoing_count == count && !memcmp(queued, cl.voice_outgoing, sizeof(queued)));
	Voice_ClientReadAttempt(&active);
	captured_length = captured_sends = 0; CL_SendMove(NULL);
	assert(captured_sends == 1 && cl.voice_outgoing_count == 0 &&
		cl.voice_outgoing_head == (head + 1) % VOICE_CLIENT_QUEUE_CAPACITY);
	Voice_ClientReadAttempt(&active); *source_state = cl; Voice_ClientDeliver(source);
	assert(source->voice_next_serial == serial + 1);
	Voice_ClientCheckServerPacket(source, serial + 1, &active);
	Key_Event(K_F12, false); Voice_EncodeCaptureFrame(pcm);
	assert(!voice_ptt && !voice_sending && cl.voice_outgoing_count == 1);
	end = cl.voice_outgoing[cl.voice_outgoing_head];
	assert((end.flags & VOICE_FLAG_END) && !end.payload_bytes);
	serial = source->voice_next_serial; captured_length = captured_sends = 0;
	CL_SendMove(NULL);
	assert(captured_sends == 1 && !cl.voice_outgoing_count);
	Voice_ClientReadAttempt(&end); *source_state = cl; Voice_ClientDeliver(source);
	assert(source->voice_next_serial == serial + 1);
	Voice_ClientCheckServerPacket(source, serial + 1, &end);
	captured_length = captured_sends = 0; CaptureVoiceRelay(receiver, &relay);
	Voice_ClientReceiveRelay(receiver, receiver_state, &relay);
	assert(voice_speakers[0].jitter.count > 0);
	PlayUntilBuffered();
	for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i) decoded |= voice_decode_frame[i] != 0;
	assert(decoded && UnreadSpeakerHasSignal()); MixHasSignal(false);
	assert(!voice_speakers[0].jitter.count &&
		Voice_AtomicGet(&voice_speakers[0].pcm_read) == Voice_AtomicGet(&voice_speakers[0].pcm_write));
	*receiver_state = cl;
	cl = *source_state; cls.netcon = saved_netcon;
}

static void Voice_ClientFatalDisconnect(client_t **peers, client_state_t **states)
{
	int original_connections = net_activeconnections, saved_driver = net_driverlevel;
	qsocket_t *replaced, *paired_client, *paired_server;
	voice_packet_t expected;
	assert(peers[0]->active && peers[1]->active && sv.active && UnreadSpeakerHasSignal() &&
		voice_speakers[0].jitter.count > 0 && cl.voice_outgoing_count > 0 &&
		CL_VoiceTransportAvailable() && voice_ptt && voice_ptt_keys[K_F12] &&
		voice_sending && Voice_IsTransmitting() && Voice_InputLevel() > 0);
	/* Retire the other driverless peer; preserve this prepared sender. */
	NET_FreeQSocket(peers[1]->netconnection); host_client = peers[1]; SV_DropClient(false);
	assert(!peers[1]->active && !peers[1]->netconnection &&
		net_activeconnections == original_connections - 1);
	replaced = peers[0]->netconnection; NET_FreeQSocket(replaced);
	assert(replaced->disconnected);
	/* Prepared transport association, not a new server admission. */
	net_driverlevel = 0; paired_client = Loop_Connect("local");
	paired_server = Loop_CheckNewConnections(); net_driverlevel = saved_driver;
	assert(paired_client && paired_server && paired_client->driver == 0 && paired_server->driver == 0 &&
		paired_client->driverdata == paired_server && paired_server->driverdata == paired_client);
	peers[0]->netconnection = paired_server;
	assert(peers[0]->active && net_activeconnections == original_connections - 1);
	cls.state = ca_connected; cls.signon = SIGNONS; cls.netcon = paired_client;
	assert(!cl.protocol_qsvr && CL_VoiceTransportAvailable());
	expected = cl.voice_outgoing[cl.voice_outgoing_head];
	assert(Voice_PacketIsValid(&expected));
	captured_length = captured_sends = 0; voice_fixture_keep_first_capture = true;
	/* Native client heartbeat runs outside either QC VM, unlike server helpers. */
	PR_SwitchQCVM(NULL);
	voice_fixture_send_result = -1; CL_SendMove(NULL);
	voice_fixture_send_result = 1; voice_fixture_keep_first_capture = false; *states[0] = cl;
	assert(captured_sends == 2 && cls.state == ca_disconnected && !cls.netcon && !sv.active &&
		net_activeconnections == 0 && !peers[0]->active && !peers[0]->netconnection &&
		!peers[1]->active && !peers[1]->netconnection);
	Voice_ClientReadAttempt(&expected);
	assert(paired_client->disconnected && paired_server->disconnected &&
		!paired_client->driverdata && !paired_server->driverdata);
	assert(!CL_VoiceTransportAvailable() && !cl.voice_outgoing_count && !cl.voice_outgoing_head &&
		!voice_ptt && !voice_ptt_keys[K_F12] && !voice_sending && !voice_capture_wanted && !voice_capture_device &&
		!Voice_CaptureReady() && !Voice_IsTransmitting() && Voice_InputLevel() == 0 &&
		!Voice_AtomicGet(&voice_input_level) && !Voice_AtomicGet(&voice_transmitting) &&
		!voice_preroll_count && !voice_vad.frame_count && voice_settings.desktop.transmit &&
		voice_settings.desktop.mode == 1);
	assert(!voice_speakers[0].have_generation && !voice_speakers[0].jitter.count &&
		Voice_AtomicGet(&voice_speakers[0].pcm_read) == Voice_AtomicGet(&voice_speakers[0].pcm_write));
}
#endif
