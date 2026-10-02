/* Optional native VAD producer qualification; production owners remain untouched. */
static int VoiceVADMeanAbs(const int16_t *samples)
{
	uint64_t sum = 0;
	for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
	{
		int sample = samples[i];
		sum += (unsigned)(sample < 0 ? -sample : sample);
	}
	return (int)(sum / VOICE_FRAME_SAMPLES);

}
static voice_packet_t VoiceVADSendOne(client_t *source, client_state_t *state)
{
	voice_packet_t packet;
	uint64_t serial = source->voice_next_serial;
	cl = *state;
	cls.netcon = source->netconnection;
	assert(cl.voice_outgoing_count);
	packet = cl.voice_outgoing[cl.voice_outgoing_head];
	captured_length = captured_sends = 0;
	CL_SendMove(NULL);
	assert(captured_sends == 1 && captured_length > 0 &&
		cl.voice_outgoing_count + 1 == state->voice_outgoing_count);
	*state = cl;
	GapDeliver(source, captured, captured_length);
	assert(source->voice_next_serial == serial + 1);
	const server_voice_packet_t *received = SV_VoicePacketForSerial(source, serial + 1);
	assert(received && received->packet.sequence == packet.sequence &&
		received->packet.timestamp == packet.timestamp &&
		received->packet.talkspurt == packet.talkspurt &&
		received->packet.flags == packet.flags &&
		received->packet.payload_bytes == packet.payload_bytes &&
		!memcmp(received->packet.payload, packet.payload, packet.payload_bytes));
	return packet;

}
static void VoiceVADRetireRelay(client_t *source, client_state_t *state, client_t *receiver, client_state_t *receiver_state)
{
	int slot = (int)(source - svs.clients);
	assert(slot >= 0 && slot < MAX_SCOREBOARD);
	while (receiver->voice_relay_serial[slot] < source->voice_next_serial)
	{
		voice_fixture_datagram_t packet;
		CaptureVoiceRelay(receiver, &packet);
		ReceiveVoiceRelay(receiver, receiver_state, &packet);
	}
	assert(receiver->voice_relay_serial[slot] == source->voice_next_serial);
	PlayUntilBuffered();
	assert(!voice_speakers[0].jitter.count);
	MixHasSignal(false);
	*receiver_state = cl;
	cl = *state;
	cls.netcon = source->netconnection;

}
static void Voice_VADNativeChecks(client_t *source, client_state_t *state, client_t *receiver, client_state_t *receiver_state)
{
	int16_t quiet[VOICE_FRAME_SAMPLES] = {0}, loud[VOICE_FRAME_SAMPLES];
	voice_packet_t onset[VOICE_VAD_PREROLL_FRAMES + 1];
	int old_mode = voice_settings.desktop.mode;
	qboolean old_transmit = voice_settings.desktop.transmit;
	float old_gain = voice_input_gain.value, old_sensitivity = voice_vad_sensitivity.value;
	keydest_t old_dest = key_dest;
	char *old_binding = keybindings[K_F12] ? q_strdup(keybindings[K_F12]) : NULL;
	unsigned int sequence = voice_next_sequence;
	uint32_t timestamp = voice_next_timestamp;
	uint64_t serial;
	float active_level, settled_level;
	int error, starts = 0;
	OpusDecoder *decoder;
	int16_t decoded[VOICE_FRAME_SAMPLES];
	assert(Voice_CaptureReady() && !cl.voice_outgoing_count && !voice_sending &&
		!voice_ptt && !voice_speakers[0].jitter.count);
	for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
		loud[i] = (i / 96) & 1 ? 10000 : -10000;
	cl = *state;
	Voice_SetMode(0);
	key_dest = key_game;
	Cvar_SetValueQuick(&voice_input_gain, 1.25f);
	Cvar_SetValueQuick(&voice_vad_sensitivity, 65.0f);
	for (int frame = 0; frame < 4; ++frame)
	{
		cl = *state;
		Voice_EncodeCaptureFrame(quiet);
		*state = cl;
		assert(!cl.voice_outgoing_count && !voice_sending);
	}
	cl = *state;
	Voice_EncodeCaptureFrame(loud);
	*state = cl;
	assert(!cl.voice_outgoing_count && !Voice_IsTransmitting());
	cl = *state;
	Voice_EncodeCaptureFrame(loud);
	*state = cl;
	assert(cl.voice_outgoing_count == VOICE_CLIENT_QUEUE_CAPACITY &&
		Voice_IsTransmitting());
	active_level = Voice_InputLevel();
	assert(active_level > 0.0f);
	for (int i = 0; i < VOICE_VAD_PREROLL_FRAMES + 1; ++i)
		onset[i] = VoiceVADSendOne(source, state);
	for (int i = 0; i < VOICE_VAD_PREROLL_FRAMES + 1; ++i)
	{
		assert(onset[i].payload_bytes && onset[i].sequence == (uint16_t)(sequence + i) &&
			onset[i].timestamp == timestamp + i * VOICE_FRAME_SAMPLES &&
			onset[i].talkspurt == onset[0].talkspurt);
		starts += !!(onset[i].flags & VOICE_FLAG_START);
		assert(!(onset[i].flags & VOICE_FLAG_END));
	}
	assert(starts == 1 && (onset[0].flags & VOICE_FLAG_START));
	decoder = opus_decoder_create(VOICE_SAMPLE_RATE, 1, &error);
	assert(decoder && error == OPUS_OK);
	assert(opus_decode(decoder, onset[0].payload, onset[0].payload_bytes, decoded, VOICE_FRAME_SAMPLES, 0) == VOICE_FRAME_SAMPLES); int quiet0 = VoiceVADMeanAbs(decoded);
	assert(opus_decode(decoder, onset[1].payload, onset[1].payload_bytes, decoded, VOICE_FRAME_SAMPLES, 0) == VOICE_FRAME_SAMPLES); int quiet1 = VoiceVADMeanAbs(decoded);
	assert(opus_decode(decoder, onset[2].payload, onset[2].payload_bytes, decoded, VOICE_FRAME_SAMPLES, 0) == VOICE_FRAME_SAMPLES); int voice0 = VoiceVADMeanAbs(decoded);
	assert(opus_decode(decoder, onset[3].payload, onset[3].payload_bytes, decoded, VOICE_FRAME_SAMPLES, 0) == VOICE_FRAME_SAMPLES); int voice1 = VoiceVADMeanAbs(decoded);
	opus_decoder_destroy(decoder);
	assert(quiet0 < 500 && quiet1 < 500 && voice0 > 1000 && voice1 > 1000);
	serial = source->voice_next_serial;
	for (int frame = 0; frame < VOICE_VAD_HANGOVER_FRAMES; ++frame)
	{
		cl = *state;
		Voice_EncodeCaptureFrame(quiet);
		*state = cl;
		assert(cl.voice_outgoing_count == 1);
		voice_packet_t packet = VoiceVADSendOne(source, state);
		if (frame + 1 < VOICE_VAD_HANGOVER_FRAMES)
			assert(packet.payload_bytes && !(packet.flags & VOICE_FLAG_END) &&
				Voice_IsTransmitting());
		else
			assert(!packet.payload_bytes && (packet.flags & VOICE_FLAG_END) &&
				!Voice_IsTransmitting());
	}
	assert(source->voice_next_serial == serial + VOICE_VAD_HANGOVER_FRAMES);
	settled_level = Voice_InputLevel();
	assert(settled_level > 0.0f && settled_level < active_level);
	for (int frame = 0; frame < 3; ++frame)
	{
		cl = *state;
		Voice_EncodeCaptureFrame(quiet);
		*state = cl;
		assert(!cl.voice_outgoing_count && !voice_sending);
	}
	assert(Voice_InputLevel() < settled_level);
	VoiceVADRetireRelay(source, state, receiver, receiver_state);

	Voice_SetMode(1);
	Key_SetBinding(K_F12, "+voicerecord");
	key_dest = key_console;
	Voice_PTTKeyEvent(K_F12, true);
	assert(!voice_ptt);
	key_dest = key_game;
	Voice_PTTKeyEvent(K_F12, true);
	assert(voice_ptt && voice_ptt_keys[K_F12]);
	cl = *state;
	Voice_EncodeCaptureFrame(loud);
	*state = cl;
	assert(cl.voice_outgoing_count == 1 && Voice_IsTransmitting());
	voice_packet_t packet = VoiceVADSendOne(source, state);
	assert(packet.payload_bytes && (packet.flags & VOICE_FLAG_START));
	Voice_PTTKeyEvent(K_F12, false);
	cl = *state;
	Voice_EncodeCaptureFrame(quiet);
	*state = cl;
	packet = VoiceVADSendOne(source, state);
	assert(!packet.payload_bytes && (packet.flags & VOICE_FLAG_END));
	VoiceVADRetireRelay(source, state, receiver, receiver_state);

	Voice_PTTKeyEvent(K_F12, true);
	for (int frame = 0; frame < 3; ++frame)
	{
		cl = *state;
		Voice_EncodeCaptureFrame(loud);
		*state = cl;
	}
	assert(cl.voice_outgoing_count == 3 && voice_sending);
	Voice_CaptureDiscontinuity(false);
	*state = cl;
	assert(voice_capture_device && Voice_CaptureReady() && voice_ptt &&
		voice_ptt_keys[K_F12] && !voice_sending && cl.voice_outgoing_count == 1 &&
		!cl.voice_outgoing[cl.voice_outgoing_head].payload_bytes &&
		(cl.voice_outgoing[cl.voice_outgoing_head].flags & VOICE_FLAG_END) &&
		!Voice_InputLevel() && !voice_preroll_count && !voice_vad.frame_count);
	packet = VoiceVADSendOne(source, state);
	assert(!packet.payload_bytes && (packet.flags & VOICE_FLAG_END));
	for (int frame = 0; frame < 2; ++frame)
	{
		cl = *state;
		Voice_EncodeCaptureFrame(loud);
		*state = cl;
	}
	double retry = realtime + 10.0;
	Voice_CaptureDiscontinuity(true);
	*state = cl;
	assert(!voice_capture_device && !Voice_CaptureReady() && voice_capture_wanted &&
		voice_next_device_check == retry && voice_ptt && voice_ptt_keys[K_F12] &&
		!voice_sending && cl.voice_outgoing_count == 1 &&
		!cl.voice_outgoing[cl.voice_outgoing_head].payload_bytes &&
		(cl.voice_outgoing[cl.voice_outgoing_head].flags & VOICE_FLAG_END) &&
		!Voice_InputLevel() && !voice_preroll_count && !voice_vad.frame_count);
	packet = VoiceVADSendOne(source, state);
	assert(!packet.payload_bytes && (packet.flags & VOICE_FLAG_END));
	Voice_RefreshCapture(true);
	assert(voice_capture_device && Voice_CaptureReady() &&
		SDL_GetCurrentAudioDriver() && !strcmp(SDL_GetCurrentAudioDriver(), "dummy"));
	cl = *state;
	Voice_EncodeCaptureFrame(loud);
	*state = cl;
	packet = VoiceVADSendOne(source, state);
	assert(packet.payload_bytes && (packet.flags & VOICE_FLAG_START));
	Voice_PTTKeyEvent(K_F12, false);
	cl = *state;
	Voice_EncodeCaptureFrame(quiet);
	*state = cl;
	packet = VoiceVADSendOne(source, state);
	assert(!packet.payload_bytes && (packet.flags & VOICE_FLAG_END));
	VoiceVADRetireRelay(source, state, receiver, receiver_state);
	Voice_PTTKeyEvent(K_F12, false);
	cl = *state;
	Voice_StopTransmit();
	Voice_SetMode(old_mode);
	Voice_SetTransmitEnabled(old_transmit);
	Cvar_SetValueQuick(&voice_input_gain, old_gain);
	Cvar_SetValueQuick(&voice_vad_sensitivity, old_sensitivity);
	key_dest = old_dest;
	Key_SetBinding(K_F12, old_binding);
	if (old_binding) Mem_Free(old_binding);
	*state = cl;
	assert(voice_capture_device && Voice_CaptureReady() && !voice_ptt &&
		!voice_sending && !cl.voice_outgoing_count &&
		voice_settings.desktop.mode == old_mode &&
		voice_settings.desktop.transmit == old_transmit);
}
