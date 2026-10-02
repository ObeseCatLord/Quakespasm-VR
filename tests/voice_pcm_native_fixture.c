/* Controlled PCM through the native voice codec, transport, relay and mixer.
 * Only SDL's dummy recording backend and a controlled DMA descriptor are used. */
#ifdef NDEBUG
#error "voice PCM fixture requires assertions for input and dummy-driver guards"
#endif

#define NEGOTIATION_FIXTURE_CUSTOM_CAN_SEND
#define MIXED_FIXTURE_CUSTOM_UNRELIABLE
#define MIXED_NATIVE_FIXTURE_ENTRY MixedFixtureMain
#include "mixed_native_fixture.c"
#undef MIXED_NATIVE_FIXTURE_ENTRY

#include "../Quake/voice.c"

static int voice_fixture_send_result = 1;
static qboolean voice_fixture_keep_first_capture;

int __wrap_NET_SendUnreliableMessage(qsocket_t *socket, sizebuf_t *message)
{
	assert(socket && message->cursize >= 0 &&
		message->cursize <= (int)sizeof(captured));
	captured_sends++;
	if (!voice_fixture_keep_first_capture || captured_sends == 1)
	{
		memcpy(captured, message->data, message->cursize);
		captured_length = message->cursize;
	}
	return voice_fixture_send_result;
}

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

typedef struct
{
	byte bytes[VOICE_SERVER_DATAGRAM_BUDGET];
	int length;
} voice_fixture_datagram_t;

static void CaptureVoiceRelay (client_t *receiver, voice_fixture_datagram_t *packet)
{
	captured_length = captured_sends = 0;
	assert (SV_SendPendingVoice (receiver));
	assert (captured_sends == 1 && captured_length >= VOICE_SVC_HEADER_BYTES &&
		captured_length <= (int)sizeof (packet->bytes));
	packet->length = captured_length;
	memcpy (packet->bytes, captured, packet->length);
}

static void ReceiveVoiceRelay (client_t *receiver, client_state_t *state,
	voice_fixture_datagram_t *packet)
{
	cl = *state;
	cls.netcon = receiver->netconnection;
	net_message.data = packet->bytes;
	net_message.cursize = packet->length;
	net_message.maxsize = sizeof (packet->bytes);
	CL_ParseServerMessage ();
	assert (msg_readcount == net_message.cursize);
	*state = cl;
}

static void RelayToReceiver (client_t *receiver, client_state_t *state)
{
	voice_fixture_datagram_t packet;
	CaptureVoiceRelay (receiver, &packet);
	ReceiveVoiceRelay (receiver, state, &packet);
	assert (voice_speakers[0].jitter.count > 0);
}

static void PlayUntilBuffered (void)
{
	const unsigned ticks = 1 + (voice_speakers[0].jitter.target_delay_ms +
		voice_speakers[0].jitter.count * VOICE_FRAME_MILLISECONDS +
		VOICE_FRAME_MILLISECONDS - 1) / VOICE_FRAME_MILLISECONDS;
	for (unsigned frame = 0; frame < ticks; ++frame)
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

static void WarmVoiceCommands (client_t *peer, client_state_t *state)
{
	for (int frame = 0; frame < 3; ++frame)
	{
		usercmd_t command = {0};
		cl = *state;
		cls.netcon = peer->netconnection;
		command.servertime = cl.time = qcvm->time;
		captured_length = captured_sends = 0;
		CL_SendMove (&command);
		*state = cl;
		if (captured_length) GapDeliver (peer, captured, captured_length);
	}
}

static void VoiceGainChecks (client_t **peers, client_state_t **states)
{
	for (int gain = 0; gain <= 1; ++gain)
	{
		int16_t pcm[VOICE_FRAME_SAMPLES];
		cl = *states[1];
		Cmd_ExecuteString (gain ? "voice_player_volume 1 1" :
			"voice_player_volume 1 0", src_command);
		assert (voice_speakers[0].volume == gain);
		for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
			pcm[i] = (i / 96) & 1 ? 12000 : -12000;
		SendControlledFrame (peers[0], states[0], pcm);
		RelayToReceiver (peers[1], states[1]);
		PlayUntilBuffered ();
		qboolean decoded_signal = false;
		for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
			decoded_signal |= voice_decode_frame[i] != 0;
		assert (decoded_signal);
		MixHasSignal (!gain);
		*states[1] = cl;
	}
}

static void VoiceGenerationChecks (client_t **peers, client_state_t **states,
	const char *offer)
{
	int16_t pcm[VOICE_FRAME_SAMPLES];
	voice_fixture_datagram_t old_packet;
	for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
		pcm[i] = (i / 96) & 1 ? 12000 : -12000;
	SendControlledFrame (peers[0], states[0], pcm);
	RelayToReceiver (peers[1], states[1]);
	PlayUntilBuffered ();
	SendControlledFrame (peers[0], states[0], pcm);
	CaptureVoiceRelay (peers[1], &old_packet);
	ReceiveVoiceRelay (peers[1], states[1], &old_packet);
	assert (UnreadSpeakerHasSignal () && voice_speakers[0].jitter.count > 0);
	const uint32_t old_generation = voice_speakers[0].generation;
	client_state_t *retired = states[0];
	NET_FreeQSocket (peers[0]->netconnection);
	host_client = peers[0];
	SV_DropClient (false);
	published_voice_length[0] = 0;
	peers[0] = SpawnPeer (0, offer, 0);
	states[0] = CreateMixedPeerState (peers[0], 0);
	NegotiateVoice (peers[0], states[0]);
	WarmVoiceCommands (peers[0], states[0]);
	Mem_Free (retired->entities);
	Mem_Free (retired->scores);
	Mem_Free (retired);
	assert ((uint32_t)(peers[0]->voice_generation - old_generation) > 0 &&
		(uint32_t)(peers[0]->voice_generation - old_generation) < 0x80000000u);
	SendControlledFrame (peers[0], states[0], pcm);
	RelayToReceiver (peers[1], states[1]);
	assert (voice_speakers[0].generation == peers[0]->voice_generation &&
		Voice_AtomicGet (&voice_speakers[0].pcm_read) ==
		Voice_AtomicGet (&voice_speakers[0].pcm_write));
	assert (voice_speakers[0].jitter.entries[0].sequence ==
		SV_VoicePacketForSerial (peers[0], 1)->packet.sequence);
	voice_jitter_t retained;
	memcpy (&retained, &voice_speakers[0].jitter, sizeof (retained));
	ReceiveVoiceRelay (peers[1], states[1], &old_packet);
	assert (voice_speakers[0].generation == peers[0]->voice_generation &&
		!memcmp (&retained, &voice_speakers[0].jitter, sizeof (retained)));
	assert (Voice_AtomicGet (&voice_speakers[0].pcm_read) ==
		Voice_AtomicGet (&voice_speakers[0].pcm_write));
	PlayUntilBuffered ();
	MixHasSignal (false);
	*states[1] = cl;
}

static void VoiceLossReorderChecks (client_t **peers, client_state_t **states)
{
	for (int loss = 0; loss <= 1; ++loss)
	{
		voice_fixture_datagram_t packets[4];
		uint16_t sequences[4];
		int16_t pcm[VOICE_FRAME_SAMPLES];
		for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
			pcm[i] = (i / 96) & 1 ? 12000 : -12000;
		assert (!voice_speakers[0].jitter.count && !voice_sending);
		assert (Voice_AtomicGet (&voice_speakers[0].pcm_read) ==
			Voice_AtomicGet (&voice_speakers[0].pcm_write));
		for (int frame = 0; frame < 4; ++frame)
		{
			cl = *states[0];
			cls.netcon = peers[0]->netconnection;
			Voice_PTTKeyEvent (K_F12, frame != 3);
			Voice_EncodeCaptureFrame (pcm);
			assert (cl.voice_outgoing_count == 1);
			captured_length = captured_sends = 0;
			CL_SendMove (NULL);
			assert (captured_sends == 1 && !cl.voice_outgoing_count);
			*states[0] = cl;
			GapDeliver (peers[0], captured, captured_length);
			sequences[frame] = SV_VoicePacketForSerial (peers[0],
				peers[0]->voice_next_serial)->packet.sequence;
			if (frame) assert ((uint16_t)(sequences[frame] - sequences[frame - 1]) == 1);
			CaptureVoiceRelay (peers[1], &packets[frame]);
			realtime += VOICE_FRAME_MILLISECONDS / 1000.0;
		}
		ReceiveVoiceRelay (peers[1], states[1], &packets[2]);
		ReceiveVoiceRelay (peers[1], states[1], &packets[0]);
		if (!loss) ReceiveVoiceRelay (peers[1], states[1], &packets[1]);
		ReceiveVoiceRelay (peers[1], states[1], &packets[3]);
		assert (voice_speakers[0].jitter.count == (unsigned)(loss ? 3 : 4));
		assert (voice_speakers[0].jitter.entries[0].sequence == sequences[0] &&
			voice_speakers[0].jitter.entries[1].sequence == sequences[loss ? 2 : 1]);
		PlayUntilBuffered ();
		const int frames = (Voice_AtomicGet (&voice_speakers[0].pcm_write) -
			Voice_AtomicGet (&voice_speakers[0].pcm_read) + VOICE_PCM_RING_FRAMES) %
			VOICE_PCM_RING_FRAMES;
		assert (frames == 3 * VOICE_FRAME_SAMPLES && !voice_speakers[0].jitter.count);
		if (loss)
		{
			qboolean concealed_signal = false;
			const int start = Voice_AtomicGet (&voice_speakers[0].pcm_read);
			for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
			{
				const int index = (start + VOICE_FRAME_SAMPLES + i) % VOICE_PCM_RING_FRAMES;
				concealed_signal |= voice_speakers[0].pcm[index * 2] != 0 ||
					voice_speakers[0].pcm[index * 2 + 1] != 0;
			}
			assert (concealed_signal);
		}
		MixHasSignal (false);
		*states[1] = cl;
	}
}

#include "voice_queue_recovery_native_fixture.h"
#include "voice_routing_native_fixture.h"
#include "voice_vad_native_fixture.h"
#include "voice_budget_native_fixture.h"
#include "voice_fatal_send_native_fixture.h"
#include "voice_client_send_native_fixture.h"
#include "voice_fallback_native_fixture.h"
#include "spatial_callback_native_fixture.h"

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
	WarmVoiceCommands (peers[0], states[0]);
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
	if (COM_CheckParm ("-routing"))
		Voice_RoutingChecks ();
	voice_settings.desktop.transmit = 1; /* Prepared fixture preference only. */
	voice_settings.desktop.mode = 1;
	Voice_RefreshCapture (true);
	assert (voice_capture_device && Voice_CaptureReady ());
	if (COM_CheckParm ("-vad")) {
		Voice_VADNativeChecks (peers[0], states[0], peers[1], states[1]);
		puts ("VOICE_VAD_NATIVE_PASSED producer/preroll/decode/hangover/PTT/discontinuity/dummy recovery");
	}
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
	if (COM_CheckParm ("-recovery"))
	{
		Voice_QueueRecoveryChecks (peers[0], states[0], peers[1], states[1]);
		VoiceGainChecks (peers, states);
		VoiceLossReorderChecks (peers, states);
		VoiceGenerationChecks (peers, states, public_offer);
	}
	if (COM_CheckParm ("-budgets"))
	{
		Voice_BudgetNativeChecks (peers[0], states[0], peers[1], states[1]);
		puts ("VOICE_BUDGET_NATIVE_PASSED controlled parser replay/no auto-retransmit; nonfatal send0/send1 same bytes; independent decode/PCM");
	}
	if (COM_CheckParm ("-fatal-send"))
		Voice_FatalSendNativeChecks (peers, states, public_offer);
	if (COM_CheckParm ("-client-send"))
		Voice_ClientSendNativeChecks (peers[0], states[0], peers[1], states[1]);
	if (COM_CheckParm ("-spatial-fallback"))
		Voice_FallbackNativeChecks (peers[0], states[0], peers[1], states[1]);
#ifdef USE_STEAMAUDIO
	if (COM_CheckParm ("-spatial-callback"))
		Spatial_CallbackNativeChecks ();
#else
	assert (!COM_CheckParm ("-spatial-callback"));
#endif
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
		if (!COM_CheckParm ("-client-send"))
			Voice_PTTKeyEvent (K_F12, false);
	}
	assert (cl.voice_outgoing_count > 0 && CL_VoiceTransportAvailable ());
	if (COM_CheckParm ("-client-send"))
		Voice_ClientFatalDisconnect (peers, states);
	else
	{
		CL_ResetVoiceTransportState ();
		Voice_ResetConnection ();
		assert (!CL_VoiceTransportAvailable () && !cl.voice_outgoing_count);
		assert (!voice_speakers[0].have_generation &&
			!voice_speakers[0].jitter.count &&
			Voice_AtomicGet (&voice_speakers[0].pcm_read) ==
			Voice_AtomicGet (&voice_speakers[0].pcm_write));
	}
	Voice_Shutdown ();
	assert (!voice_initialized && !voice_encoder && !voice_capture_device);
	keybindings[K_F12] = saved_binding;
	shm = saved_shm;
	for (int slot = 0; slot < 2; ++slot)
	{
		if (peers[slot]->active)
		{
			/* Ordinary fixture endpoints have no driver owner. */
			NET_FreeQSocket (peers[slot]->netconnection);
			host_client = peers[slot];
			SV_DropClient (false);
		}
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
	if (COM_CheckParm ("-recovery"))
		puts ("VOICE_RECOVERY_NATIVE_PASSED generated stream/queue/gain/loss/reorder/generation; captured transport");
	if (COM_CheckParm ("-routing"))
		puts ("VOICE_ROUTING_NATIVE_PASSED stored VR defaults/opt-out; desktop dummy routes");
	if (COM_CheckParm ("-fatal-send"))
		puts ("VOICE_FATAL_SEND_NATIVE_PASSED native loop ctor/admission/close/QC drop/slot reuse; controlled fatal send");
	if (COM_CheckParm ("-client-send"))
		puts ("VOICE_CLIENT_SEND_NATIVE_PASSED public heartbeat queue preservation/consumption/decoded signal; native fatal close and hosted shutdown");
	if (COM_CheckParm ("-spatial-fallback"))
		puts ("VOICE_FALLBACK_NATIVE_PASSED six prepared position/listener cases; native decoded/mixed stereo direction and equal-channel radio");
#ifdef USE_STEAMAUDIO
	if (COM_CheckParm ("-spatial-callback"))
		puts ("SPATIAL_CALLBACK_NATIVE_PASSED actual SDK/publication/progress; prepared cached loops/one-shots/pause/menu/HRTF/panning/teardown");
#endif
	puts ("VOICE_PCM_NATIVE_PASSED native negotiated codec/relay/PCM/mute/reset; captured transport/dummy capture");
	return 0;
}
