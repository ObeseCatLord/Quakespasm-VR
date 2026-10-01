/* Controlled PCM through the native voice codec, transport, relay and mixer.
 * Only SDL's dummy recording backend and a controlled DMA descriptor are used. */
#ifdef NDEBUG
#error "voice PCM fixture requires assertions for input and dummy-driver guards"
#endif

#define NEGOTIATION_FIXTURE_CUSTOM_CAN_SEND
#define MIXED_NATIVE_FIXTURE_ENTRY MixedFixtureMain
#include "mixed_native_fixture.c"
#undef MIXED_NATIVE_FIXTURE_ENTRY

#include "../Quake/voice.c"

static int VoiceOfferPacket (client_t *peer, byte *packet, size_t capacity)
{
	static const char offer[] = "//voice_protocol 1\n";
	const size_t length = 1 + sizeof (offer);
	const sizebuf_t *message = &peer->message;
	int found = 0, offset = -1;
	assert (message->data && message->cursize >= 0 &&
		(size_t)message->cursize <= (size_t)message->maxsize && length <= capacity);
	for (int i = 0; i <= message->cursize - (int)length; ++i)
		if (message->data[i] == svc_stufftext &&
			!memcmp (message->data + i + 1, offer, sizeof (offer)))
		{
			found++;
			offset = i;
		}
	if (!found) return 0;
	assert (found == 1);
	memcpy (packet, message->data + offset, length);
	return (int)length;
}

static byte published_voice_offer[2][64];
static int published_voice_length[2];

qboolean __wrap_NET_CanSendMessage (qsocket_t *socket)
{
	assert (socket);
	for (int slot = 0; slot < 2 && slot < svs.maxclients; ++slot)
	{
		client_t *peer = &svs.clients[slot];
		if (peer->netconnection == socket && peer->voice_protocol_offered)
		{
			int length = VoiceOfferPacket (peer, published_voice_offer[slot],
				sizeof (published_voice_offer[slot]));
			if (length) published_voice_length[slot] = length;
		}
	}
	return false; /* Existing held reliable-send boundary. */
}

static void NegotiateVoice (client_t *peer, client_state_t *state)
{
	static byte capability[128];
	const int slot = (int)(peer - svs.clients);
	assert (slot >= 0 && slot < 2 && published_voice_length[slot] > 0);
	byte *offer = published_voice_offer[slot];
	const int offer_length = published_voice_length[slot];
	cl = *state;
	cls.state = ca_connected;
	cls.signon = SIGNONS;
	cls.demoplayback = false;
	cls.netcon = peer->netconnection;
	cls.message.data = capability;
	cls.message.maxsize = sizeof (capability);
	SZ_Clear (&cls.message);
	net_message.data = offer;
	net_message.cursize = offer_length;
	net_message.maxsize = sizeof (published_voice_offer[slot]);
	CL_ParseServerMessage ();
	assert (msg_readcount == net_message.cursize && cl.voice_cap_sent &&
		cl.voice_protocol_offered && cls.message.cursize > 0);
	*state = cl;
	GapDeliver (peer, cls.message.data, cls.message.cursize);
	assert (peer->voice_capable);
}

static void OfferBothPeers (client_t **peers, client_state_t **states)
{
	for (int slot = 0; slot < 2; ++slot)
		NegotiateVoice (peers[slot], states[slot]);
}

static void SendControlledFrame (client_t *source, client_state_t *state,
	int16_t *pcm)
{
	usercmd_t command = {0};
	const uint64_t serial_before = source->voice_next_serial;
	cl = *state;
	cls.state = ca_connected;
	cls.signon = SIGNONS;
	cls.netcon = source->netconnection;
	cl.time = command.servertime = qcvm->time;
	VectorCopy (cl.viewangles, command.viewangles);
	Voice_PTTKeyEvent (K_F12, true);
	Voice_EncodeCaptureFrame (pcm);
	assert (Voice_InputLevel () > 0 && cl.voice_outgoing_count > 0);
	Voice_PTTKeyEvent (K_F12, false);
	captured_length = captured_sends = 0;
	CL_SendMove (&command);
	assert (captured_length > 0 && cl.voice_outgoing_count == 0);
	*state = cl;
	GapDeliver (source, captured, captured_length);
	assert (source->voice_next_serial > serial_before && source->voice_generation != 0);
	/* Complete the controlled burst through the real released-PTT producer. */
	Voice_EncodeCaptureFrame (pcm);
	assert (cl.voice_outgoing_count == 1 && !voice_sending);
	captured_length = captured_sends = 0;
	CL_SendMove (NULL);
	assert (captured_length > 0 && !cl.voice_outgoing_count);
	*state = cl;
	GapDeliver (source, captured, captured_length);
	assert (source->voice_next_serial >= serial_before + 2);
}

static void RelayToReceiver (client_t *receiver, client_state_t *state)
{
	captured_length = captured_sends = 0;
	assert (SV_SendPendingVoice (receiver));
	assert (captured_sends == 1 && captured_length > VOICE_SVC_HEADER_BYTES);
	cl = *state;
	cls.state = ca_connected;
	cls.signon = SIGNONS;
	cls.netcon = receiver->netconnection;
	net_message.data = captured;
	net_message.cursize = captured_length;
	net_message.maxsize = sizeof (captured);
	CL_ParseServerMessage ();
	assert (msg_readcount == net_message.cursize && voice_speakers[0].jitter.count > 0);
	*state = cl;
}

static void PlayUntilBuffered (void)
{
	for (int frame = 0; frame < 6; ++frame)
	{
		realtime += VOICE_FRAME_MILLISECONDS / 1000.0;
		Voice_Frame ();
	}
	assert (Voice_AtomicGet (&voice_speakers[0].pcm_write) !=
		Voice_AtomicGet (&voice_speakers[0].pcm_read));
}

static qboolean UnreadSpeakerHasSignal (void)
{
	int read = Voice_AtomicGet (&voice_speakers[0].pcm_read);
	const int write = Voice_AtomicGet (&voice_speakers[0].pcm_write);
	while (read != write)
	{
		if (voice_speakers[0].pcm[read * 2] || voice_speakers[0].pcm[read * 2 + 1])
			return true;
		read = (read + 1) % VOICE_PCM_RING_FRAMES;
	}
	return false;
}

static qboolean MixHasSignal (qboolean muted)
{
	int16_t mixed[VOICE_PCM_RING_FRAMES * 2] = {0};
	const int frames = (Voice_AtomicGet (&voice_speakers[0].pcm_write) -
		Voice_AtomicGet (&voice_speakers[0].pcm_read) + VOICE_PCM_RING_FRAMES) %
		VOICE_PCM_RING_FRAMES;
	assert (frames > 0 && frames < VOICE_PCM_RING_FRAMES);
	qboolean nonzero = false;
	Voice_MixAudio ((unsigned char *)mixed, frames * 2 * sizeof (int16_t), 16, 2,
		VOICE_SAMPLE_RATE, false);
	for (size_t i = 0; i < (size_t)frames * 2; ++i)
		nonzero |= mixed[i] != 0;
	assert (nonzero != muted);
	assert (Voice_AtomicGet (&voice_speakers[0].pcm_read) ==
		Voice_AtomicGet (&voice_speakers[0].pcm_write));
	return nonzero;
}

int main (int argc, char **argv)
{
	client_t *peers[2];
	client_state_t *states[2];
	byte pcm_buffer[VOICE_FRAME_SAMPLES * 2 * sizeof (int16_t)] = {0};
	dma_t controlled_dma = {0};
	volatile dma_t *saved_shm;
	char *saved_binding;
	char public_offer[1024];
	assert (getenv ("SDL_AUDIODRIVER") &&
		!strcmp (getenv ("SDL_AUDIODRIVER"), "dummy"));
	assert (getenv ("XDG_DATA_HOME") && getenv ("XDG_DATA_HOME")[0] == '/');
	Fixture_InitNativeEngine (argc, argv, "e1m1", true);
	assert (svs.maxclients >= 2 && sv_voice.value != 0);
	assert (SDL_InitSubSystem (SDL_INIT_AUDIO));
	assert (SDL_GetCurrentAudioDriver () &&
		!strcmp (SDL_GetCurrentAudioDriver (), "dummy"));
	ConfigurePrivateMovementFixture (false, false);
	ClientOffer (QSVR_PROTOCOL_PINNED, false, public_offer, sizeof (public_offer));
	peers[0] = SpawnPeer (0, public_offer, 0);
	peers[1] = SpawnPeer (1, public_offer, 0);
	cls.signon = SIGNONS;
	cls.state = ca_connected;
	for (int slot = 0; slot < 2; ++slot)
		states[slot] = CreateMixedPeerState (peers[slot], slot);
	OfferBothPeers (peers, states);
	/* Preserve the native command warmup instead of seeding its sequence. */
	for (int frame = 0; frame < 3; ++frame)
	{
		usercmd_t command = {0};
		cl = *states[0];
		cls.netcon = peers[0]->netconnection;
		command.servertime = cl.time = qcvm->time;
		captured_length = captured_sends = 0;
		CL_SendMove (&command);
		*states[0] = cl;
		if (captured_length) GapDeliver (peers[0], captured, captured_length);
	}
	controlled_dma.channels = 2;
	controlled_dma.samples = VOICE_FRAME_SAMPLES * 2;
	controlled_dma.samplebits = 16;
	controlled_dma.speed = VOICE_SAMPLE_RATE;
	controlled_dma.buffer = pcm_buffer;
	saved_shm = shm;
	assert (!saved_shm);
	shm = &controlled_dma;
	Voice_Init ();
	assert (voice_initialized && !voice_settings.desktop.transmit);
	voice_settings.desktop.transmit = 1; /* Prepared fixture preference only. */
	voice_settings.desktop.mode = 1;
Voice_RefreshCapture (true);
	assert (voice_capture_device && Voice_CaptureReady ());
	saved_binding = keybindings[K_F12];
	keybindings[K_F12] = "+voicerecord";
	key_dest = key_game;
	for (int pass = 0; pass < 2; ++pass)
	{
		int16_t pcm[VOICE_FRAME_SAMPLES];
		for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
			pcm[i] = (i / 96) & 1 ? 12000 : -12000;
		SendControlledFrame (peers[0], states[0], pcm);
		RelayToReceiver (peers[1], states[1]);
		cl = *states[1];
		PlayUntilBuffered ();
		if (!pass)
			MixHasSignal (false);
		else
		{
			assert (UnreadSpeakerHasSignal ());
			Cmd_ExecuteString ("voice_mute 1", src_command);
			assert (voice_speakers[0].muted);
			MixHasSignal (true);
		}
		*states[1] = cl;
	}
	Cmd_ExecuteString ("voice_mute 1", src_command);
	assert (!voice_speakers[0].muted);
	/* Leave real PCM buffered, then real jitter queued, without consuming either. */
	for (int burst = 0; burst < 2; ++burst)
	{
		int16_t pcm[VOICE_FRAME_SAMPLES];
		for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
			pcm[i] = (i / 96) & 1 ? 12000 : -12000;
		SendControlledFrame (peers[0], states[0], pcm);
		RelayToReceiver (peers[1], states[1]);
		if (!burst) PlayUntilBuffered ();
		*states[1] = cl;
	}
	assert (UnreadSpeakerHasSignal () && voice_speakers[0].jitter.count > 0 &&
		voice_speakers[0].have_generation);
	/* Independently leave a native producer packet queued at the sender. */
	cl = *states[0];
	cls.netcon = peers[0]->netconnection;
	{
		int16_t pcm[VOICE_FRAME_SAMPLES];
		for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
			pcm[i] = (i / 96) & 1 ? 12000 : -12000;
		Voice_PTTKeyEvent (K_F12, true);
		Voice_EncodeCaptureFrame (pcm);
		Voice_PTTKeyEvent (K_F12, false);
	}
	assert (cl.voice_outgoing_count > 0 && CL_VoiceTransportAvailable ());
	CL_ResetVoiceTransportState ();
	Voice_ResetConnection ();
	assert (!CL_VoiceTransportAvailable () && !cl.voice_outgoing_count);
	assert (!voice_speakers[0].have_generation &&
		!voice_speakers[0].jitter.count &&
		Voice_AtomicGet (&voice_speakers[0].pcm_read) ==
		Voice_AtomicGet (&voice_speakers[0].pcm_write));
	Voice_Shutdown ();
	assert (!voice_initialized && !voice_encoder && !voice_capture_device);
	keybindings[K_F12] = saved_binding;
	shm = saved_shm;
	for (int slot = 0; slot < 2; ++slot)
	{
		/* No driver owns these fixture-only endpoints (Loop_Init is held). */
		NET_FreeQSocket (peers[slot]->netconnection);
		host_client = peers[slot];
		SV_DropClient (false);
		assert (!peers[slot]->active && !peers[slot]->netconnection);
	}
	PR_SwitchQCVM (NULL);
	Host_ShutdownServer (false);
	Host_Shutdown ();
	/* Native disconnect has now completed every cache write through global cl. */
	assert (cls.state == ca_disconnected && !cls.netcon);
	cl.entities = NULL;
	cl.scores = NULL;
	for (int slot = 0; slot < 2; ++slot)
	{
		Mem_Free (states[slot]->entities);
		Mem_Free (states[slot]->scores);
		Mem_Free (states[slot]);
	}
	puts ("VOICE_PCM_NATIVE_PASSED native negotiated codec/relay/PCM/mute/reset; captured transport/dummy capture");
	return 0;
}
