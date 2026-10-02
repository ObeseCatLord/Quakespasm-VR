/* SDL3 capture API failure and controlled retry qualification. */
#ifndef VOICE_DEVICE_FAULT_NATIVE_FIXTURE_H
#define VOICE_DEVICE_FAULT_NATIVE_FIXTURE_H
#define VOICE_DEVICE_FAULT_AVAILABLE 1
#define VOICE_DEVICE_FAULT_READ 2

static SDL_AudioStream *voice_device_fault_stream;
static int voice_device_fault_mode;
static unsigned voice_device_fault_errors;
static unsigned voice_device_fault_available_errors;
static unsigned voice_device_fault_read_errors;
static qboolean voice_device_fault_active;
static qboolean voice_device_fault_fail_open;
static unsigned voice_device_fault_open_calls;

static qboolean Voice_DeviceFaultArmed(SDL_AudioStream *stream, int mode)
{
	return voice_device_fault_active && stream && stream == voice_device_fault_stream &&
		voice_device_fault_mode == mode;
}

static void Voice_DeviceFaultDisarm(void)
{
	voice_device_fault_mode = 0;
	voice_device_fault_stream = NULL;
}

extern int SDLCALL __real_SDL_GetAudioStreamAvailable(SDL_AudioStream *stream);
extern int SDLCALL __real_SDL_GetAudioStreamData(SDL_AudioStream *stream, void *buffer, int bytes);
extern SDL_AudioStream * SDLCALL __real_SDL_OpenAudioDeviceStream(SDL_AudioDeviceID device, const SDL_AudioSpec *spec, SDL_AudioStreamCallback callback, void *userdata);

int SDLCALL __wrap_SDL_GetAudioStreamAvailable(SDL_AudioStream *stream)
{
	if (Voice_DeviceFaultArmed(stream, VOICE_DEVICE_FAULT_AVAILABLE))
	{
		Voice_DeviceFaultDisarm();
		voice_device_fault_errors++; voice_device_fault_available_errors++;
		return -1;
	}
	if (Voice_DeviceFaultArmed(stream, VOICE_DEVICE_FAULT_READ))
		return VOICE_CAPTURE_FRAME_BYTES;
	return __real_SDL_GetAudioStreamAvailable(stream);
}


int SDLCALL __wrap_SDL_GetAudioStreamData(SDL_AudioStream *stream, void *buffer, int bytes)
{
	if (Voice_DeviceFaultArmed(stream, VOICE_DEVICE_FAULT_READ))
	{
		Voice_DeviceFaultDisarm();
		voice_device_fault_errors++; voice_device_fault_read_errors++;
		return -1;
	}
	return __real_SDL_GetAudioStreamData(stream, buffer, bytes);
}


SDL_AudioStream * SDLCALL __wrap_SDL_OpenAudioDeviceStream(SDL_AudioDeviceID device, const SDL_AudioSpec *spec, SDL_AudioStreamCallback callback, void *userdata)
{
	if (voice_device_fault_active)
	{
		voice_device_fault_open_calls++;
		if (voice_device_fault_fail_open)
		{
			voice_device_fault_fail_open = false;
			SDL_SetError("native voice fixture: one requested open failure");
			return NULL;
		}
	}
	return __real_SDL_OpenAudioDeviceStream(device, spec, callback, userdata);
}

static void Voice_DeviceFaultFrame(client_t *source, client_state_t *state)
{
	cl = *state; cls.netcon = source->netconnection;
	Voice_Frame();
	*state = cl;
}

static void Voice_DeviceFaultPrime(void)
{
	int16_t pcm[VOICE_FRAME_SAMPLES];
	assert(voice_ptt && voice_ptt_keys[K_F12]);
	for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
		pcm[i] = (i / 96) & 1 ? 10000 : -10000;
	Voice_EncodeCaptureFrame(pcm);
	assert(voice_sending && Voice_IsTransmitting() && Voice_InputLevel() > 0.0f &&
		voice_vad.frame_count > 0 && voice_preroll_count > 0 &&
		cl.voice_outgoing_count == 1 && cl.voice_outgoing[cl.voice_outgoing_head].payload_bytes > 0 &&
		cl.voice_outgoing[cl.voice_outgoing_head].flags == VOICE_FLAG_START);
}

static void Voice_DeviceFaultNativeChecks(client_t *source, client_state_t *state)
{
	const double old_realtime = realtime;
	const int old_mode = voice_settings.desktop.mode, old_signon = cls.signon;
	const qboolean old_transmit = voice_settings.desktop.transmit, old_wanted = voice_capture_wanted;
	const keydest_t old_dest = key_dest;
	char *old_binding = keybindings[K_F12];
	qsocket_t *old_netcon = cls.netcon; cactive_t old_state = cls.state;
	qboolean old_demoplayback = cls.demoplayback;
	assert(source && state && source->netconnection);
	cl = *state;
	assert(!cl.voice_outgoing_count && !voice_ptt && !voice_sending &&
		!voice_ptt_keys[K_F12] && Voice_CaptureReady());
	assert(old_mode == 1 && old_transmit && !voice_settings.desktop.device[0] && old_wanted && voice_capture_device && voice_capture_stream);
	voice_device_fault_active = true;
	voice_device_fault_open_calls = 0;
	voice_device_fault_fail_open = false;
	cls.netcon = source->netconnection;
	cls.state = ca_connected;
	cls.signon = SIGNONS;
	cls.demoplayback = false;
	keybindings[K_F12] = "+voicerecord";
	key_dest = key_game;
	Key_Event(K_F12, false);
	assert(!voice_ptt && !voice_ptt_keys[K_F12]);
	assert(Voice_MultiplayerSessionActive());

	for (int mode = VOICE_DEVICE_FAULT_AVAILABLE;
		mode <= VOICE_DEVICE_FAULT_READ; ++mode)
	{
		const unsigned opens_before_setup = voice_device_fault_open_calls, errors_before = voice_device_fault_errors, available_before = voice_device_fault_available_errors, read_before = voice_device_fault_read_errors;
		unsigned opens_before_retry;
		double fault_time, retry;
		Voice_RefreshCapture(true);
		assert(voice_capture_device && voice_capture_stream && Voice_CaptureReady() &&
			SDL_GetCurrentAudioDriver() && !strcmp(SDL_GetCurrentAudioDriver(), "dummy") &&
			voice_device_fault_open_calls == opens_before_setup && !cl.voice_outgoing_count && !voice_ptt && !voice_sending);

		Key_Event(K_F12, true);
		assert(voice_ptt && voice_ptt_keys[K_F12]);
		Voice_DeviceFaultPrime();
		*state = cl;
		voice_device_fault_stream = voice_capture_stream;
		voice_device_fault_mode = mode;
		fault_time = realtime;
		Voice_DeviceFaultFrame(source, state);
		assert(voice_device_fault_errors == errors_before + 1 &&
			((mode == VOICE_DEVICE_FAULT_AVAILABLE && voice_device_fault_available_errors == available_before + 1 && voice_device_fault_read_errors == read_before) || (mode == VOICE_DEVICE_FAULT_READ && voice_device_fault_read_errors == read_before + 1 && voice_device_fault_available_errors == available_before)));
		Voice_DeviceFaultDisarm();
		assert(!voice_capture_device && !voice_capture_stream && !Voice_CaptureReady() &&
			voice_settings.desktop.transmit == old_transmit && voice_capture_wanted == old_wanted &&
			voice_ptt && voice_ptt_keys[K_F12] &&
			!voice_sending && !Voice_IsTransmitting() && Voice_InputLevel() == 0.0f && !voice_preroll_count && !voice_vad.frame_count &&
			cl.voice_outgoing_count == 1 && !cl.voice_outgoing[cl.voice_outgoing_head].payload_bytes &&
			cl.voice_outgoing[cl.voice_outgoing_head].flags == VOICE_FLAG_END);
		retry = fault_time + 10.0;
		assert(voice_next_device_check == retry);

		Voice_ClearNetworkQueue(false);
		assert(!cl.voice_outgoing_count);
		*state = cl;
		realtime = retry - 0.001;
		Voice_DeviceFaultFrame(source, state);
		assert(voice_device_fault_open_calls == opens_before_setup && !voice_capture_device &&
			!Voice_CaptureReady() && voice_next_device_check == retry);
		realtime = retry;
		opens_before_retry = voice_device_fault_open_calls;
		voice_device_fault_fail_open = true;
		Voice_DeviceFaultFrame(source, state);
		assert(!voice_device_fault_fail_open && voice_device_fault_open_calls == opens_before_retry + 1 &&
			!voice_capture_device && !voice_capture_stream && !Voice_CaptureReady() &&
			voice_settings.desktop.transmit == old_transmit && voice_capture_wanted == old_wanted &&
			voice_ptt && voice_ptt_keys[K_F12] && !cl.voice_outgoing_count);
		retry = realtime + 10.0;
		assert(voice_next_device_check == retry);
		Key_Event(K_F12, false);
		assert(!voice_ptt && !voice_ptt_keys[K_F12]);
		realtime = retry - 0.001;
		Voice_DeviceFaultFrame(source, state);
		assert(voice_device_fault_open_calls == opens_before_retry + 1 && !voice_capture_device &&
			!Voice_CaptureReady() && voice_next_device_check == retry &&
			!voice_ptt && !voice_ptt_keys[K_F12]);
		realtime = retry;
		Voice_DeviceFaultFrame(source, state);
		assert(voice_device_fault_open_calls == opens_before_retry + 2 &&
			voice_capture_device && voice_capture_stream && Voice_CaptureReady() &&
			SDL_GetCurrentAudioDriver() && !strcmp(SDL_GetCurrentAudioDriver(), "dummy") &&
			voice_settings.desktop.transmit == old_transmit && voice_capture_wanted == old_wanted &&
			!voice_ptt && !voice_ptt_keys[K_F12]);
		Key_Event(K_F12, true);
		assert(voice_ptt && voice_ptt_keys[K_F12]);
		Voice_DeviceFaultPrime();
		*state = cl;
		Key_Event(K_F12, false);
		assert(!voice_ptt && !voice_ptt_keys[K_F12]);
		Voice_StopTransmit();
		Voice_ClearNetworkQueue(false);
		*state = cl;
		assert(!voice_ptt && !voice_sending && !Voice_IsTransmitting() &&
			!Voice_InputLevel() && !cl.voice_outgoing_count);
	}

	Voice_DeviceFaultDisarm();
	voice_device_fault_fail_open = false;
	Key_Event(K_F12, false);
	Voice_StopTransmit();
	Voice_ClearNetworkQueue(false);

	keybindings[K_F12] = old_binding;
	key_dest = old_dest;
	assert(voice_settings.desktop.mode == old_mode);
	realtime = old_realtime;
	Voice_RefreshCapture(true);
	assert(voice_capture_device && voice_capture_stream && Voice_CaptureReady() &&
		voice_settings.desktop.mode == old_mode && voice_settings.desktop.transmit == old_transmit &&
		voice_capture_wanted == old_wanted && !voice_ptt &&
		!voice_sending && !cl.voice_outgoing_count && !voice_device_fault_fail_open);

	cls.netcon = old_netcon;
	cls.state = old_state;
	cls.signon = old_signon;
	cls.demoplayback = old_demoplayback;
	cl = *state;
	voice_device_fault_active = false;
}

#endif /* VOICE_DEVICE_FAULT_NATIVE_FIXTURE_H */
