/* Native producer pressure, replay charging, END recovery and relay expiry. */
static void Voice_QueueRecoveryChecks(client_t *source,
	client_state_t *state, client_t *receiver, client_state_t *receiver_state)
{
	int16_t pcm[VOICE_FRAME_SAMPLES];
	unsigned int initial_sequence, i;
	uint16_t oldest_sequence, last_sequence, replay_sequence;
	byte replay_wire[NET_MAXMESSAGE], end_wire[NET_MAXMESSAGE];
	int replay_length, end_length;
	uint64_t serial_before;
	unsigned int rate_before;
	qboolean rate_window_resets;
	int source_slot;

	assert(source && state && receiver && receiver_state && source != receiver);
	source_slot = (int)(source - svs.clients);
	assert(source_slot >= 0 && source_slot < svs.maxclients);
	cl = *state;
	cls.netcon = source->netconnection;
	assert(!cl.voice_outgoing_count && !voice_ptt && !voice_sending);
	for (i = 0; i < VOICE_FRAME_SAMPLES; ++i)
		pcm[i] = (i / 96) & 1 ? 12000 : -12000;

	/* Let native capture overrun the bounded client queue while sends are held. */
	initial_sequence = voice_next_sequence;
	Voice_PTTKeyEvent(K_F12, true);
	*state = cl;
	assert(voice_ptt);
	for (i = 0; i < VOICE_CLIENT_QUEUE_CAPACITY + 3; ++i)
	{
		Voice_EncodeCaptureFrame(pcm);
		*state = cl;
	}
	assert(cl.voice_outgoing_count == VOICE_CLIENT_QUEUE_CAPACITY && voice_sending);
	oldest_sequence = cl.voice_outgoing[cl.voice_outgoing_head].sequence;
	last_sequence = cl.voice_outgoing[(cl.voice_outgoing_head +
		cl.voice_outgoing_count - 1) % VOICE_CLIENT_QUEUE_CAPACITY].sequence;
	assert(oldest_sequence == (uint16_t)(initial_sequence + 3));
	assert(last_sequence == (uint16_t)(initial_sequence +
		VOICE_CLIENT_QUEUE_CAPACITY + 2));
	for (i = 0; i < cl.voice_outgoing_count; ++i)
		assert(cl.voice_outgoing[(cl.voice_outgoing_head + i) %
			VOICE_CLIENT_QUEUE_CAPACITY].sequence ==
			(uint16_t)(oldest_sequence + i));

	/* Native stop drops queued pressure and leaves its actual END for the sender. */
	Voice_StopTransmit();
	*state = cl;
	assert(!voice_ptt && !voice_sending && cl.voice_outgoing_count == 1);
	assert(!cl.voice_outgoing[cl.voice_outgoing_head].payload_bytes &&
		(cl.voice_outgoing[cl.voice_outgoing_head].flags & VOICE_FLAG_END));
	if (source->voice_rate_packets >= VOICE_SERVER_MAX_PACKETS_PER_SECOND &&
		realtime >= source->voice_rate_window_start &&
		realtime - source->voice_rate_window_start < 1.0)
		realtime += 1.1;
	serial_before = source->voice_next_serial;
	rate_before = source->voice_rate_packets;
	rate_window_resets = realtime < source->voice_rate_window_start ||
		realtime - source->voice_rate_window_start >= 1.0;
	captured_length = captured_sends = 0;
	CL_SendMove(NULL);
	*state = cl;
	assert(captured_length > 0 && captured_sends == 1 && !cl.voice_outgoing_count);
	GapDeliver(source, captured, captured_length);
	assert(source->voice_next_serial == serial_before + 1 &&
		source->voice_rate_packets == (rate_window_resets ? 1 : rate_before + 1));

	/* Replay captured native bytes to fill the rate window; duplicates never advance serials. */
	realtime += 1.1;
	cl = *state;
	cls.netcon = source->netconnection;
	serial_before = source->voice_next_serial;
	rate_before = source->voice_rate_packets;
	Voice_PTTKeyEvent(K_F12, true);
	*state = cl;
	Voice_EncodeCaptureFrame(pcm);
	*state = cl;
	assert(cl.voice_outgoing_count == 1);
	replay_sequence = cl.voice_outgoing[cl.voice_outgoing_head].sequence;
	captured_length = captured_sends = 0;
	CL_SendMove(NULL);
	*state = cl;
	assert(captured_length > 0 && captured_length <= (int)sizeof(replay_wire));
	replay_length = captured_length;
	memcpy(replay_wire, captured, (size_t)replay_length);
	GapDeliver(source, replay_wire, replay_length);
	assert(source->voice_next_serial == serial_before + 1 &&
		source->voice_rate_window_start == realtime &&
		source->voice_rate_packets == 1 && rate_before <= VOICE_SERVER_MAX_PACKETS_PER_SECOND);
	while (source->voice_rate_packets < VOICE_SERVER_MAX_PACKETS_PER_SECOND)
	{
		serial_before = source->voice_next_serial;
		rate_before = source->voice_rate_packets;
		GapDeliver(source, replay_wire, replay_length);
		assert(source->voice_rate_packets == rate_before + 1 &&
			source->voice_next_serial == serial_before);
	}

	/* Refuse the native END at the full limit, then accept it after the real rate window rolls. */
	Voice_StopTransmit();
	*state = cl;
	assert(!voice_ptt && !voice_sending && cl.voice_outgoing_count == 1);
	assert(!cl.voice_outgoing[cl.voice_outgoing_head].payload_bytes &&
		(cl.voice_outgoing[cl.voice_outgoing_head].flags & VOICE_FLAG_END) &&
		cl.voice_outgoing[cl.voice_outgoing_head].sequence != replay_sequence);
	captured_length = captured_sends = 0;
	CL_SendMove(NULL);
	*state = cl;
	assert(captured_length > 0 && captured_length <= (int)sizeof(end_wire) &&
		captured_sends == 1 && !cl.voice_outgoing_count);
	end_length = captured_length;
	memcpy(end_wire, captured, (size_t)end_length);
	serial_before = source->voice_next_serial;
	GapDeliver(source, end_wire, end_length);
	assert(source->voice_next_serial == serial_before &&
		source->voice_rate_packets == VOICE_SERVER_MAX_PACKETS_PER_SECOND);
	realtime += 1.1;
	GapDeliver(source, end_wire, end_length);
	assert(source->voice_next_serial == serial_before + 1 &&
		source->voice_rate_window_start == realtime && source->voice_rate_packets == 1);

	/* Expire queued relay packets; the native sender emits nothing and advances the receiver cursor. */
	{
		uint64_t receiver_cursor = receiver->voice_relay_serial[source_slot];
		realtime += VOICE_SERVER_MAX_PACKET_AGE + 0.02;
		cl = *receiver_state;
		cls.netcon = receiver->netconnection;
		captured_length = captured_sends = 0;
		assert(SV_SendPendingVoice(receiver));
		*receiver_state = cl;
		assert(captured_sends == 0 && captured_length == 0);
		assert(receiver_cursor < source->voice_next_serial &&
			receiver->voice_relay_serial[source_slot] == source->voice_next_serial);
	}

	cl = *state;
	cls.netcon = source->netconnection;
	assert(!cl.voice_outgoing_count && !voice_ptt && !voice_sending);
	*state = cl;
}
