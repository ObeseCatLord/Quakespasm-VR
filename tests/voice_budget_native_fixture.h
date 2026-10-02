/* Bounded native qualification for the frozen F08 voice byte/send budgets. */
#ifndef VOICE_BUDGET_NATIVE_FIXTURE_H
#define VOICE_BUDGET_NATIVE_FIXTURE_H
static void Voice_BudgetFillNoise(int frame, int16_t *pcm)
{
	uint32_t state = 0x91e10da5u ^ (uint32_t)frame;
	for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
	{
		state = state * 1664525u + 1013904223u;
		pcm[i] = (int16_t)((int32_t)(state >> 16) - 32768);
	}
}
static int Voice_BudgetProduceAndSend(client_t *source, client_state_t *state,
	int frame, voice_packet_t *encoded)
{
	usercmd_t command = {0};
	int16_t pcm[VOICE_FRAME_SAMPLES];
	cl = *state;
	cls.netcon = source->netconnection;
	command.servertime = cl.time = qcvm->time;
	VectorCopy(cl.viewangles, command.viewangles);
	Voice_BudgetFillNoise(frame, pcm);
	Voice_EncodeCaptureFrame(pcm);
	assert(Voice_InputLevel() > 0 && cl.voice_outgoing_count == 1);
	*encoded = cl.voice_outgoing[cl.voice_outgoing_head];
	assert(Voice_PacketIsValid(encoded) && encoded->payload_bytes >= 300);
	captured_length = captured_sends = 0;
	CL_SendMove(&command);
	assert(captured_sends == 1 && captured_length > 0 &&
		captured_length <= (int)sizeof(captured) && !cl.voice_outgoing_count);
	*state = cl;
	return captured_length;
}
static void Voice_BudgetNativeChecks(client_t *source, client_state_t *source_state,
	client_t *receiver, client_state_t *receiver_state)
{
	int saved_bitrate, saved_vbr, saved_dtx, error;
	int16_t decoded[VOICE_FRAME_SAMPLES];
	byte rejected[NET_MAXMESSAGE], send0[VOICE_SERVER_DATAGRAM_BUDGET];
	voice_packet_t witness = {0}, rejected_packet = {0};
	OpusDecoder *decoder;
	qsocket_t *saved_netcon = cls.netcon;
	uint64_t initial_serial, refused_serial = 0;
	unsigned int witness_generation = 0, accepted = 0, accepted_bytes = 0;
	unsigned char source_slot = (unsigned char)(source - svs.clients);
	unsigned int refused_length = 0;
	int rejected_length = 0, send0_length = 0, have_previous = 0;
	uint16_t previous_sequence = 0;
	qboolean refused = false;
	assert(source != receiver && source->voice_capable && receiver->voice_capable &&
		source->active && receiver->active && source->netconnection &&
		receiver->netconnection && voice_encoder && voice_capture_device &&
		!voice_sending && source_slot < MAX_SCOREBOARD);
	assert(voice_speakers[0].jitter.count == 0 &&
		Voice_AtomicGet(&voice_speakers[0].pcm_read) ==
		Voice_AtomicGet(&voice_speakers[0].pcm_write));
	assert(opus_encoder_ctl(voice_encoder, OPUS_GET_BITRATE(&saved_bitrate)) == OPUS_OK);
	assert(opus_encoder_ctl(voice_encoder, OPUS_GET_VBR(&saved_vbr)) == OPUS_OK);
	assert(opus_encoder_ctl(voice_encoder, OPUS_GET_DTX(&saved_dtx)) == OPUS_OK);
	assert(opus_encoder_ctl(voice_encoder, OPUS_SET_BITRATE(160000)) == OPUS_OK);
	assert(opus_encoder_ctl(voice_encoder, OPUS_SET_VBR(0)) == OPUS_OK);
	assert(opus_encoder_ctl(voice_encoder, OPUS_SET_DTX(0)) == OPUS_OK);
	assert(opus_encoder_ctl(voice_encoder, OPUS_GET_BITRATE(&error)) == OPUS_OK &&
		error == 160000);
	assert(opus_encoder_ctl(voice_encoder, OPUS_GET_VBR(&error)) == OPUS_OK && !error);
	assert(opus_encoder_ctl(voice_encoder, OPUS_GET_DTX(&error)) == OPUS_OK && !error);
	initial_serial = source->voice_next_serial; realtime += 1.01;
	assert(realtime >= source->voice_rate_window_start &&
		realtime - source->voice_rate_window_start >= 1.0);
	Voice_PTTKeyEvent(K_F12, true);
	for (int frame = 0; frame < (int)VOICE_SERVER_MAX_PACKETS_PER_SECOND; ++frame)
	{
		server_voice_packet_t queue_before[VOICE_SERVER_QUEUE_CAPACITY];
		uint64_t cursor_before;
		unsigned int relay_generation_before, generation_before;
		unsigned char relay_next_before;
		uint64_t serial_before;
		unsigned int packets_before, bytes_before, packet_bytes;
		double window_before;
		voice_packet_t encoded;
		int wire_length = Voice_BudgetProduceAndSend(source, source_state,
			frame, &encoded);
		if (have_previous)
			assert((uint16_t)(encoded.sequence - previous_sequence) == 1);
		previous_sequence = encoded.sequence; have_previous = 1;
		serial_before = source->voice_next_serial;
		packets_before = source->voice_rate_packets;
		bytes_before = source->voice_rate_bytes;
		window_before = source->voice_rate_window_start;
		generation_before = source->voice_generation;
		cursor_before = receiver->voice_relay_serial[source_slot];
		relay_generation_before = receiver->voice_relay_generation[source_slot];
		relay_next_before = receiver->voice_relay_next_source;
		memcpy(queue_before, source->voice_packets, sizeof(queue_before));
		packet_bytes = VOICE_CLC_HEADER_BYTES + encoded.payload_bytes;
		GapDeliver(source, captured, wire_length);
		if (source->voice_next_serial == serial_before)
		{
			refused = true; refused_serial = serial_before;
			refused_length = packet_bytes; rejected_packet = encoded;
			rejected_length = wire_length;
			memcpy(rejected, captured, (size_t)rejected_length);
			assert(source->voice_rate_window_start == window_before &&
				source->voice_rate_packets == packets_before &&
				source->voice_rate_bytes == bytes_before &&
				source->voice_generation == generation_before &&
				!memcmp(queue_before, source->voice_packets, sizeof(queue_before)));
			assert(receiver->voice_relay_serial[source_slot] == cursor_before &&
				receiver->voice_relay_generation[source_slot] ==
					relay_generation_before &&
				receiver->voice_relay_next_source == relay_next_before);
			assert(source->voice_rate_packets < VOICE_SERVER_MAX_PACKETS_PER_SECOND &&
				source->voice_rate_bytes + packet_bytes >
				VOICE_SERVER_MAX_BYTES_PER_SECOND && source->active &&
				source->netconnection);
			break;
		}
		assert(source->voice_next_serial == serial_before + 1 &&
			source->voice_rate_window_start == realtime &&
			source->voice_rate_packets == accepted + 1 &&
			source->voice_rate_bytes == accepted_bytes + packet_bytes);
		server_voice_packet_t *queued =
			SV_VoicePacketForSerial(source, source->voice_next_serial);
		assert(queued && queued->packet.sequence == encoded.sequence &&
			queued->packet.payload_bytes == encoded.payload_bytes &&
			!memcmp(queued->packet.payload, encoded.payload,
				encoded.payload_bytes));
		if (!accepted)
		{
			witness = queued->packet;
			witness_generation = source->voice_generation;
		}
		assert(source->voice_generation == witness_generation &&
			Voice_PacketIsValid(&queued->packet));
		accepted++; accepted_bytes += packet_bytes;
	}
	assert(refused && rejected_length > 0 && refused_length ==
		VOICE_CLC_HEADER_BYTES + rejected_packet.payload_bytes &&
		accepted > 0 && accepted < VOICE_SERVER_MAX_PACKETS_PER_SECOND &&
		source->voice_next_serial == initial_serial + accepted &&
		source->voice_next_serial == refused_serial);
	decoder = opus_decoder_create(VOICE_SAMPLE_RATE, 1, &error);
	assert(decoder && error == OPUS_OK);
	assert(opus_decode(decoder, witness.payload, witness.payload_bytes, decoded,
		VOICE_FRAME_SAMPLES, 0) == VOICE_FRAME_SAMPLES);
	qboolean decoded_signal = false;
	for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
		decoded_signal |= decoded[i] != 0;
	assert(decoded_signal);
	opus_decoder_destroy(decoder);
	realtime += 1.01;
	assert(realtime - source->voice_rate_window_start >= 1.0);
	GapDeliver(source, rejected, rejected_length);
	assert(source->voice_next_serial == refused_serial + 1 &&
		source->voice_generation == witness_generation &&
		source->voice_rate_packets == 1 && source->voice_rate_bytes == refused_length &&
		source->voice_rate_window_start == realtime);
	server_voice_packet_t *replayed =
		SV_VoicePacketForSerial(source, source->voice_next_serial);
	assert(replayed && replayed->packet.sequence == rejected_packet.sequence &&
		replayed->packet.payload_bytes == rejected_packet.payload_bytes &&
		!memcmp(replayed->packet.payload, rejected_packet.payload,
			rejected_packet.payload_bytes));
	cl = *source_state; cls.netcon = source->netconnection;
	Voice_PTTKeyEvent(K_F12, false);
	int16_t end_pcm[VOICE_FRAME_SAMPLES];
	Voice_BudgetFillNoise(0x7f, end_pcm);
	Voice_EncodeCaptureFrame(end_pcm);
	assert(!voice_sending && cl.voice_outgoing_count == 1);
	voice_packet_t end_packet = cl.voice_outgoing[cl.voice_outgoing_head];
	assert(!end_packet.payload_bytes && (end_packet.flags & VOICE_FLAG_END));
	captured_length = captured_sends = 0;
	CL_SendMove(NULL);
	assert(captured_sends == 1 && captured_length > 0 && !cl.voice_outgoing_count);
	*source_state = cl; uint64_t end_serial = source->voice_next_serial;
	GapDeliver(source, captured, captured_length);
	assert(source->voice_next_serial == end_serial + 1);
	server_voice_packet_t *queued_end =
		SV_VoicePacketForSerial(source, source->voice_next_serial);
	assert(queued_end && (queued_end->packet.flags & VOICE_FLAG_END));
	assert(opus_encoder_ctl(voice_encoder,
			OPUS_SET_BITRATE(saved_bitrate)) == OPUS_OK);
	assert(opus_encoder_ctl(voice_encoder, OPUS_SET_VBR(saved_vbr)) == OPUS_OK);
	assert(opus_encoder_ctl(voice_encoder, OPUS_SET_DTX(saved_dtx)) == OPUS_OK);
	assert(opus_encoder_ctl(voice_encoder, OPUS_RESET_STATE) == OPUS_OK);
	realtime += VOICE_SERVER_MAX_PACKET_AGE + 0.01;
	captured_length = captured_sends = 0;
	assert(SV_SendPendingVoice(receiver) && captured_sends == 0);
	assert(receiver->voice_relay_generation[source_slot] == source->voice_generation &&
		receiver->voice_relay_serial[source_slot] == source->voice_next_serial);
	int16_t speech_pcm[VOICE_FRAME_SAMPLES];
	for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
		speech_pcm[i] = (i / 96) & 1 ? 12000 : -12000;
	SendControlledFrame(source, source_state, speech_pcm);
	assert(receiver->voice_relay_serial[source_slot] == source->voice_next_serial - 2 &&
		receiver->voice_relay_generation[source_slot] == source->voice_generation);
	uint64_t source_serial = source->voice_next_serial;
	unsigned int source_generation = source->voice_generation;
	uint64_t relay_cursor = receiver->voice_relay_serial[source_slot];
	unsigned int relay_generation = receiver->voice_relay_generation[source_slot];
	unsigned char relay_next = receiver->voice_relay_next_source;
	qsocket_t *receiver_socket = receiver->netconnection;
	voice_fixture_send_result = 0;
	captured_length = captured_sends = 0;
	assert(SV_SendPendingVoice(receiver));
	voice_fixture_send_result = 1;
	assert(captured_sends == 1 && captured_length > 0 &&
		captured_length <= (int)sizeof(send0));
	send0_length = captured_length;
	memcpy(send0, captured, (size_t)send0_length);
	assert(receiver->active && receiver->netconnection == receiver_socket &&
		source->voice_next_serial == source_serial &&
		source->voice_generation == source_generation &&
		receiver->voice_relay_serial[source_slot] == relay_cursor &&
		receiver->voice_relay_generation[source_slot] == relay_generation &&
		receiver->voice_relay_next_source == relay_next);
	captured_length = captured_sends = 0;
	assert(SV_SendPendingVoice(receiver));
	assert(captured_sends == 1 && captured_length == send0_length &&
		!memcmp(send0, captured, (size_t)send0_length));
	assert(receiver->active && receiver->netconnection == receiver_socket &&
		receiver->voice_relay_serial[source_slot] == source_serial &&
		receiver->voice_relay_generation[source_slot] == source_generation &&
		receiver->voice_relay_next_source ==
			(unsigned char)((relay_next + 1) % q_min(svs.maxclients, MAX_SCOREBOARD)));
	voice_fixture_datagram_t relay;
	relay.length = captured_length;
	memcpy(relay.bytes, captured, (size_t)relay.length);
	ReceiveVoiceRelay(receiver, receiver_state, &relay);
	assert(voice_speakers[0].jitter.count > 0);
	PlayUntilBuffered();
	assert(UnreadSpeakerHasSignal());
	MixHasSignal(false);
	*receiver_state = cl;
	voice_fixture_send_result = 1; cls.netcon = saved_netcon; cl = *receiver_state;
}
#endif
