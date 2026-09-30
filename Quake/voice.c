/*
 * Optional Opus voice transport integration for vkQuake's SDL sound owner.
 * Capture, packet handling, VAD, jitter playout, and spatialization run on the
 * game thread. SDL's existing playback callback is the only audio consumer.
 */

#include "quakedef.h"
#include "voice.h"
#include "voice_jitter.h"
#include "voice_vad.h"
#include "voice_settings.h"
#include "voice_capture.h"
#include "snd_spatial.h"
#include "cmd.h"
#include "client.h"
#ifdef USE_STEAMAUDIO
#include "glquake.h"
#endif

#include <opus/opus.h>
#ifdef USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include <stdlib.h>
#include <string.h>

#ifdef USE_SDL3
typedef SDL_AtomicInt voice_atomic_t;
#define Voice_AtomicGet SDL_GetAtomicInt
#define Voice_AtomicSet SDL_SetAtomicInt
#define VOICE_AUDIO_S16 SDL_AUDIO_S16
#else
typedef SDL_atomic_t voice_atomic_t;
#define Voice_AtomicGet SDL_AtomicGet
#define Voice_AtomicSet SDL_AtomicSet
#define VOICE_AUDIO_S16 AUDIO_S16SYS
#endif

#define VOICE_PCM_RING_FRAMES 16384
#define VOICE_CAPTURE_BACKLOG_FRAMES 10
#define VOICE_CAPTURE_FRAME_BYTES (VOICE_FRAME_SAMPLES * (int)sizeof(int16_t))
#define VOICE_PLAYBACK_GAIN 2.0f
#define VOICE_DEVICE_POLL_SECONDS 1.0
#define VOICE_CONFIRM_SECONDS 15.0

typedef struct voice_speaker_s
{
	OpusDecoder *decoder;
	voice_jitter_t jitter;
	uint32_t generation;
	qboolean have_generation;
	int16_t pcm[VOICE_PCM_RING_FRAMES * 2];
	voice_atomic_t pcm_read;
	voice_atomic_t pcm_write;
	voice_atomic_t muted_audio;
	voice_atomic_t talking;
	float volume;
	qboolean muted;
	double talking_until;
} voice_speaker_t;

typedef enum voice_pending_action_e
{
	VOICE_PENDING_NONE,
	VOICE_PENDING_CONSENT,
	VOICE_PENDING_DEVICE,
	VOICE_PENDING_MODE,
	VOICE_PENDING_SELF_REVERB
} voice_pending_action_t;

static cvar_t voice_receive = {"voice_receive", "1", CVAR_ARCHIVE};
static cvar_t voice_hud = {"voice_hud", "1", CVAR_ARCHIVE};
static cvar_t voice_input_gain = {"voice_input_gain", "1", CVAR_ARCHIVE};
static cvar_t voice_vad_sensitivity = {"voice_vad_sensitivity", "55", CVAR_ARCHIVE};
static cvar_t voice_volume = {"voice_volume", "1", CVAR_ARCHIVE};
static cvar_t voice_radio_volume = {"voice_radio_volume", "0.45", CVAR_ARCHIVE};
static cvar_t voice_spatial_distance = {"voice_spatial_distance", "768", CVAR_ARCHIVE};
static cvar_t voice_positional_only = {"voice_positional_only", "0", CVAR_ARCHIVE};
static cvar_t voice_self_reverb_volume = {"voice_self_reverb_volume", "1", CVAR_ARCHIVE};

static voice_speaker_t voice_speakers[MAX_SCOREBOARD];
static voice_vad_t voice_vad;
static voice_settings_t voice_settings;
static int16_t voice_preroll[VOICE_VAD_PREROLL_FRAMES][VOICE_FRAME_SAMPLES];
static int16_t voice_decode_frame[VOICE_FRAME_SAMPLES];
#ifndef USE_SDL3
static int16_t voice_raw_capture[16384];
#endif
static voice_atomic_t voice_audio_enabled;
static voice_atomic_t voice_receive_enabled;
static voice_atomic_t voice_input_level;
static voice_atomic_t voice_transmitting;
static voice_atomic_t voice_transmit_enabled;
static voice_atomic_t voice_vr_transmit_enabled;
static voice_atomic_t voice_capture_ready;
static voice_atomic_t voice_hud_visible;
static OpusEncoder *voice_encoder;
static SDL_AudioDeviceID voice_capture_device;
#ifdef USE_SDL3
static SDL_AudioStream *voice_capture_stream;
#else
static SDL_AudioSpec voice_capture_obtained;
static SDL_AudioStream *voice_capture_convert;
#endif
static unsigned int voice_preroll_write;
static unsigned int voice_preroll_count;
static unsigned int voice_next_sequence;
static uint32_t voice_next_timestamp;
static uint8_t voice_talkspurt;
static qboolean voice_initialized;
static qboolean voice_profile_vr;
static qboolean voice_sending;
static qboolean voice_ptt_keys[MAX_KEYS];
static qboolean voice_ptt;
static qboolean voice_receive_was_enabled = true;
static qboolean voice_last_session_active;
static qboolean voice_capture_wanted;
static double voice_next_device_check;
static voice_pending_action_t voice_pending_action;
static double voice_pending_deadline;
static int voice_pending_mode;
static char voice_pending_device[VOICE_SETTINGS_DEVICE_BYTES];
static char voice_settings_path[MAX_OSPATH];
static vec3_t voice_listener_origin;
static vec3_t voice_listener_right;
static voice_menu_state_t voice_menu_state;

static void Voice_PublishSpatialVoiceSource(int slot);
static void Voice_ResetSpatialStreams(void);
static void Voice_SyncProfile(void);

static void Voice_PublishMenuState(const voice_settings_profile_t *profile,
	qboolean device_available)
{
	if (!profile)
		return;
	voice_menu_state.available = voice_initialized;
	voice_menu_state.transmit = profile->transmit != 0;
	voice_menu_state.self_reverb = profile->self_reverb != 0;
	voice_menu_state.device_available = device_available;
	voice_menu_state.vr_profile = voice_profile_vr;
	voice_menu_state.mode = profile->mode ? 1 : 0;
	q_strlcpy(voice_menu_state.device, profile->device,
		sizeof(voice_menu_state.device));
	Voice_AtomicSet(&voice_transmit_enabled, profile->transmit ? 1 : 0);
}

static voice_settings_profile_t *Voice_Profile(void)
{
	Voice_SyncProfile();
	return voice_profile_vr ? &voice_settings.vr : &voice_settings.desktop;
}

static void Voice_LoadSettings(void)
{
	char *directory;
	int length, result = -1;

	VoiceSettings_Defaults(&voice_settings);
	directory = SDL_GetPrefPath("vkQuake", "vkQuake");
	if (directory)
	{
		length = q_snprintf(voice_settings_path, sizeof(voice_settings_path),
			"%svoice-settings.dat", directory);
		SDL_free(directory);
		if (length >= 0 && (size_t)length < sizeof(voice_settings_path))
			result = VoiceSettings_Load(voice_settings_path, &voice_settings);
	}
	if (result < 0)
	{
		VoiceSettings_Defaults(&voice_settings);
		voice_settings.desktop.transmit = 0;
		voice_settings.vr.transmit = 0;
		Con_Printf("Voice: microphone preferences couldn't be read; capture is disabled.\n");
	}
	Voice_AtomicSet(&voice_vr_transmit_enabled,
		voice_settings.vr.transmit ? 1 : 0);
}

static void Voice_SaveSettings(void)
{
	Voice_AtomicSet(&voice_vr_transmit_enabled,
		voice_settings.vr.transmit ? 1 : 0);
	if (!voice_settings_path[0] || !VoiceSettings_Save(voice_settings_path,
		&voice_settings))
		Con_Printf("Voice: settings couldn't be saved; this choice lasts until exit.\n");
}

static qboolean Voice_MultiplayerSessionActive(void)
{
	return cls.state == ca_connected && cls.signon == SIGNONS &&
		!cls.demoplayback && cl.maxclients > 1 &&
		CL_VoiceTransportAvailable();
}

static qboolean Voice_HUDShouldDisplay(void)
{
	return voice_initialized && voice_hud.value != 0 &&
		!cls.demoplayback && cls.signon == SIGNONS &&
		Voice_MultiplayerSessionActive();
}

static void Voice_ClearNetworkQueue(void)
{
	cl.voice_outgoing_head = 0;
	cl.voice_outgoing_count = 0;
}

static qboolean Voice_QueuePacket(const int16_t *samples, unsigned int flags)
{
	voice_packet_t packet;
	int bytes;

	memset(&packet, 0, sizeof(packet));
	packet.sequence = (uint16_t)voice_next_sequence++;
	packet.timestamp = voice_next_timestamp;
	packet.talkspurt = voice_talkspurt;
	packet.flags = (uint8_t)flags;
	if (samples)
	{
		bytes = opus_encode(voice_encoder, samples, VOICE_FRAME_SAMPLES,
			packet.payload, sizeof(packet.payload));
		if (bytes < 0)
			return false;
		packet.payload_bytes = (uint16_t)bytes;
		voice_next_timestamp += VOICE_FRAME_SAMPLES;
	}
	return CL_QueueVoicePacket(&packet);
}

static void Voice_ClearPTT(void)
{
	voice_ptt = false;
	memset(voice_ptt_keys, 0, sizeof(voice_ptt_keys));
}

static void Voice_StopTransmit(void)
{
	qboolean was_sending = voice_sending;

	Voice_ClearNetworkQueue();
	voice_sending = false;
	Voice_AtomicSet(&voice_transmitting, 0);
	Voice_AtomicSet(&voice_input_level, 0);
	Voice_ClearPTT();
	voice_preroll_write = voice_preroll_count = 0;
	Voice_VADReset(&voice_vad);
	if (voice_encoder)
		opus_encoder_ctl(voice_encoder, OPUS_RESET_STATE);
	if (was_sending && Voice_MultiplayerSessionActive())
		Voice_QueuePacket(NULL, VOICE_FLAG_END);
}

#ifdef USE_SDL3
static SDL_AudioDeviceID Voice_ResolveCaptureDevice(const char *name)
{
	SDL_AudioDeviceID *devices;
	SDL_AudioDeviceID match = 0;
	int count = 0, matches = 0, i;
	devices = SDL_GetAudioRecordingDevices(&count);
	if (!devices)
		return 0;
	for (i = 0; i < count; ++i)
	{
		const char *candidate = SDL_GetAudioDeviceName(devices[i]);
		if (candidate && !strcmp(candidate, name))
		{
			match = devices[i];
			++matches;
		}
	}
	SDL_free(devices);
	return matches == 1 ? match : 0;
}

static qboolean Voice_CaptureStopped(void)
{
	return voice_capture_device && SDL_AudioDevicePaused(voice_capture_device);
}

static qboolean Voice_OpenCapture(SDL_AudioDeviceID device)
{
	SDL_AudioSpec spec;
	spec.format = SDL_AUDIO_S16;
	spec.channels = 1;
	spec.freq = VOICE_SAMPLE_RATE;
	voice_capture_stream = SDL_OpenAudioDeviceStream(device, &spec, NULL, NULL);
	if (!voice_capture_stream)
	{
		Con_Printf("Voice: couldn't open the selected microphone: %s\n", SDL_GetError());
		return false;
	}
	voice_capture_device = SDL_GetAudioStreamDevice(voice_capture_stream);
	if (!SDL_ResumeAudioStreamDevice(voice_capture_stream))
	{
		Con_Printf("Voice: couldn't start microphone capture: %s\n", SDL_GetError());
		SDL_DestroyAudioStream(voice_capture_stream);
		voice_capture_stream = NULL;
		voice_capture_device = 0;
		return false;
	}
	Voice_AtomicSet(&voice_capture_ready, 1);
	return true;
}

static void Voice_CloseCapture(void)
{
	if (voice_capture_stream)
		SDL_DestroyAudioStream(voice_capture_stream);
	Spatial_ResetSelf();
	voice_capture_stream = NULL;
	voice_capture_device = 0;
	Voice_AtomicSet(&voice_capture_ready, 0);
	Voice_AtomicSet(&voice_input_level, 0);
}

static int Voice_CaptureAvailable(void)
{
	return voice_capture_stream ? SDL_GetAudioStreamAvailable(voice_capture_stream) : 0;
}

static int Voice_CaptureRead(void *buffer, int bytes)
{
	return SDL_GetAudioStreamData(voice_capture_stream, buffer, bytes);
}

static void Voice_CaptureClear(void)
{
	if (voice_capture_stream)
		SDL_ClearAudioStream(voice_capture_stream);
}
#else
static int Voice_ResolveCaptureDevice(const char *name)
{
	int count = SDL_GetNumAudioDevices(SDL_TRUE), match = -1, matches = 0, i;
	for (i = 0; i < count; ++i)
	{
		const char *candidate = SDL_GetAudioDeviceName(i, SDL_TRUE);
		if (candidate && !strcmp(candidate, name))
		{
			match = i;
			++matches;
		}
	}
	return matches == 1 ? match : -1;
}

static qboolean Voice_CaptureStopped(void)
{
	return voice_capture_device &&
		SDL_GetAudioDeviceStatus(voice_capture_device) == SDL_AUDIO_STOPPED;
}

static qboolean Voice_OpenCapture(int device_index)
{
	SDL_AudioSpec desired;
	const char *name = device_index >= 0 ?
		SDL_GetAudioDeviceName(device_index, SDL_TRUE) : NULL;
	if (device_index >= 0 && !name)
		return false; /* A removed explicit device must not open the default. */
	SDL_zero(desired);
	desired.freq = VOICE_SAMPLE_RATE;
	desired.format = AUDIO_S16SYS;
	desired.channels = 1;
	desired.samples = VOICE_FRAME_SAMPLES;
	desired.callback = NULL;
	voice_capture_device = SDL_OpenAudioDevice(name, SDL_TRUE, &desired,
		&voice_capture_obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
		SDL_AUDIO_ALLOW_FORMAT_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
	if (!voice_capture_device)
	{
		Con_Printf("Voice: couldn't open the selected microphone: %s\n", SDL_GetError());
		return false;
	}
	voice_capture_convert = SDL_NewAudioStream(voice_capture_obtained.format,
		voice_capture_obtained.channels, voice_capture_obtained.freq,
		AUDIO_S16SYS, 1, VOICE_SAMPLE_RATE);
	if (!voice_capture_convert)
	{
		Con_Printf("Voice: couldn't prepare microphone format conversion: %s\n", SDL_GetError());
		SDL_CloseAudioDevice(voice_capture_device);
		voice_capture_device = 0;
		return false;
	}
	SDL_PauseAudioDevice(voice_capture_device, 0);
	Voice_AtomicSet(&voice_capture_ready, 1);
	return true;
}

static void Voice_CloseCapture(void)
{
	if (voice_capture_device)
	{
		SDL_PauseAudioDevice(voice_capture_device, 1);
		SDL_CloseAudioDevice(voice_capture_device);
	}
	Spatial_ResetSelf();
	voice_capture_device = 0;
	Voice_AtomicSet(&voice_capture_ready, 0);
	if (voice_capture_convert)
		SDL_FreeAudioStream(voice_capture_convert);
	voice_capture_convert = NULL;
	Voice_AtomicSet(&voice_input_level, 0);
}

static int Voice_CaptureAvailable(void)
{
	Uint32 queued;
	int frame_bytes, max_backlog;
	if (!voice_capture_device || !voice_capture_convert)
		return 0;
	queued = SDL_GetQueuedAudioSize(voice_capture_device);
	frame_bytes = (SDL_AUDIO_BITSIZE(voice_capture_obtained.format) / 8) *
		voice_capture_obtained.channels;
	max_backlog = voice_capture_obtained.freq * frame_bytes / 5;
	if ((int)queued > max_backlog)
	{
		SDL_ClearQueuedAudio(voice_capture_device);
		SDL_AudioStreamClear(voice_capture_convert);
		return 0;
	}
	while (queued > 0)
	{
		Uint32 amount = q_min((Uint32)sizeof(voice_raw_capture), queued);
		Uint32 got;
		if (frame_bytes <= 0)
			break;
		amount -= amount % (Uint32)frame_bytes;
		if (!amount)
			break;
		got = SDL_DequeueAudio(voice_capture_device, voice_raw_capture, amount);
		if (!got || SDL_AudioStreamPut(voice_capture_convert,
			voice_raw_capture, (int)got) < 0)
		{
			SDL_ClearQueuedAudio(voice_capture_device);
			SDL_AudioStreamClear(voice_capture_convert);
			return 0;
		}
		queued -= got;
	}
	return SDL_AudioStreamAvailable(voice_capture_convert);
}

static int Voice_CaptureRead(void *buffer, int bytes)
{
	return SDL_AudioStreamGet(voice_capture_convert, buffer, bytes);
}

static void Voice_CaptureClear(void)
{
	if (voice_capture_device)
		SDL_ClearQueuedAudio(voice_capture_device);
	if (voice_capture_convert)
		SDL_AudioStreamClear(voice_capture_convert);
}
#endif

static void Voice_SyncProfile(void)
{
	qboolean vr_active = V_TrackedSessionActive();

	if (vr_active == voice_profile_vr)
		return;
	if (voice_initialized)
	{
		voice_pending_action = VOICE_PENDING_NONE;
		Voice_StopTransmit();
		Voice_CloseCapture();
		voice_capture_wanted = false;
		voice_last_session_active = false;
		voice_next_device_check = 0;
	}
	voice_profile_vr = vr_active;
}

static void Voice_RefreshCapture(qboolean force)
{
	voice_settings_profile_t *profile = Voice_Profile();
	qboolean session = Voice_MultiplayerSessionActive();
	qboolean stopped = Voice_CaptureStopped();
	qboolean device_available = true;
	voice_capture_route_t route;
#ifdef USE_SDL3
	SDL_AudioDeviceID resolved_device = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
#else
	int resolved_device = -1;
#endif

	if (!voice_initialized)
		return;
	if (!force && !stopped && session == voice_last_session_active &&
		realtime < voice_next_device_check)
		return;
	voice_last_session_active = session;
	voice_next_device_check = realtime + VOICE_DEVICE_POLL_SECONDS;
	if (profile->device[0])
	{
		resolved_device = Voice_ResolveCaptureDevice(profile->device);
#ifdef USE_SDL3
		device_available = resolved_device != 0;
#else
		device_available = resolved_device >= 0;
#endif
	}
	Voice_PublishMenuState(profile, device_available);
	route = Voice_CaptureRoute(device_available, profile->transmit, session);
	/* Wet-only local monitoring has its own locally confirmed permission and
	 * does not require a network session or authorize transmission. */
	route.capture |= device_available && profile->self_reverb && Spatial_Active();
	if (force || stopped || route.capture != voice_capture_wanted)
	{
		if (voice_sending || stopped || route.capture != voice_capture_wanted)
			Voice_StopTransmit();
		if (!route.capture || stopped)
			Voice_CloseCapture();
		voice_capture_wanted = route.capture;
	}
	if (route.capture && !voice_capture_device)
	{
		if (!Voice_OpenCapture(resolved_device))
			voice_next_device_check = realtime + 10.0;
	}
}

static qboolean Voice_DeviceIsUnique(const char *name)
{
#ifdef USE_SDL3
	return Voice_ResolveCaptureDevice(name) != 0;
#else
	return Voice_ResolveCaptureDevice(name) >= 0;
#endif
}

static qboolean Voice_DevicePreferenceAvailable(const char *name)
{
	return !name[0] || Voice_DeviceIsUnique(name);
}

static void Voice_FinalizeMenuAction(void)
{
	voice_pending_action = VOICE_PENDING_NONE;
	Voice_StopTransmit();
	Voice_CloseCapture();
	voice_capture_wanted = false;
	Voice_SaveSettings();
	Voice_RefreshCapture(true);
}

void Voice_SetTransmitEnabled(qboolean enabled)
{
	voice_settings_profile_t *profile;
	if (!voice_initialized)
		return;
	profile = Voice_Profile();
	voice_pending_action = VOICE_PENDING_NONE;
	if (enabled && !Voice_DevicePreferenceAvailable(profile->device))
	{
		Con_Printf("Voice: selected recording device is missing or ambiguous.\n");
		return;
	}
	profile->transmit = enabled ? 1 : 0;
	Voice_FinalizeMenuAction();
}

void Voice_SetMode(int mode)
{
	voice_settings_profile_t *profile;
	if (!voice_initialized)
		return;
	profile = Voice_Profile();
	profile->mode = mode ? 1 : 0;
	Voice_FinalizeMenuAction();
}

void Voice_SetSelfReverb(qboolean enabled)
{
	voice_settings_profile_t *profile;
	if (!voice_initialized)
		return;
	profile = Voice_Profile();
	voice_pending_action = VOICE_PENDING_NONE;
	if (enabled && !Spatial_Active())
	{
		Con_Printf("Voice: local reflections require Steam Audio.\n");
		return;
	}
	if (enabled && !Voice_DevicePreferenceAvailable(profile->device))
	{
		Con_Printf("Voice: selected recording device is missing or ambiguous.\n");
		return;
	}
	profile->self_reverb = enabled ? 1 : 0;
	Voice_FinalizeMenuAction();
}

void Voice_CycleInputDevice(int direction)
{
	voice_settings_profile_t *profile;
	int count = 0, current = -1, eligible = 0, next, wanted, i, j;
#ifdef USE_SDL3
	SDL_AudioDeviceID *devices;
#endif

	if (!voice_initialized || !direction)
		return;
	profile = Voice_Profile();
#ifdef USE_SDL3
	devices = SDL_GetAudioRecordingDevices(&count);
	if (!devices)
	{
		Con_Printf("Voice: couldn't enumerate recording devices: %s\n", SDL_GetError());
		return;
	}
#else
	count = SDL_GetNumAudioDevices(SDL_TRUE);
	if (count < 0)
	{
		Con_Printf("Voice: couldn't enumerate recording devices: %s\n", SDL_GetError());
		return;
	}
#endif

	for (i = 0; i < count; ++i)
	{
#ifdef USE_SDL3
		const char *name = SDL_GetAudioDeviceName(devices[i]);
#else
		const char *name = SDL_GetAudioDeviceName(i, SDL_TRUE);
#endif
		int matches = 0;
		if (!name || !name[0] || strlen(name) >= sizeof(profile->device))
			continue;
		for (j = 0; j < count; ++j)
		{
#ifdef USE_SDL3
			const char *other = SDL_GetAudioDeviceName(devices[j]);
#else
			const char *other = SDL_GetAudioDeviceName(j, SDL_TRUE);
#endif
			if (other && !strcmp(name, other))
				++matches;
		}
		if (matches != 1)
			continue;
		if (profile->device[0] && !strcmp(profile->device, name))
			current = eligible + 1;
		++eligible;
	}
	if (!profile->device[0])
		current = 0;

	if (current < 0)
		next = direction < 0 ? eligible : (eligible ? 1 : 0);
	else
		next = (current + (direction < 0 ? -1 : 1) + eligible + 1) %
			(eligible + 1);

	if (!next)
		profile->device[0] = '\0';
	else
	{
		wanted = next - 1;
		eligible = 0;
		for (i = 0; i < count; ++i)
		{
#ifdef USE_SDL3
			const char *name = SDL_GetAudioDeviceName(devices[i]);
#else
			const char *name = SDL_GetAudioDeviceName(i, SDL_TRUE);
#endif
			int matches = 0;
			if (!name || !name[0] || strlen(name) >= sizeof(profile->device))
				continue;
			for (j = 0; j < count; ++j)
			{
#ifdef USE_SDL3
				const char *other = SDL_GetAudioDeviceName(devices[j]);
#else
				const char *other = SDL_GetAudioDeviceName(j, SDL_TRUE);
#endif
				if (other && !strcmp(name, other))
					++matches;
			}
			if (matches != 1)
				continue;
			if (eligible++ == wanted)
			{
				q_strlcpy(profile->device, name, sizeof(profile->device));
				break;
			}
		}
	}
#ifdef USE_SDL3
	SDL_free(devices);
#endif
	Voice_FinalizeMenuAction();
}

static void Voice_ListDevices_f(void)
{
#ifdef USE_SDL3
	SDL_AudioDeviceID *devices;
	int count = 0, i;
	devices = SDL_GetAudioRecordingDevices(&count);
	Con_Printf("Voice recording devices (%d):\n", q_max(count, 0));
	for (i = 0; devices && i < count; ++i)
		Con_Printf("  %d: %s\n", i + 1, SDL_GetAudioDeviceName(devices[i]));
	SDL_free(devices);
#else
	int count = SDL_GetNumAudioDevices(SDL_TRUE), i;
	Con_Printf("Voice capture devices (%d):\n", q_max(count, 0));
	for (i = 0; i < count; ++i)
		Con_Printf("  %d: %s\n", i + 1, SDL_GetAudioDeviceName(i, SDL_TRUE));
#endif
	Con_Printf("Use the system default: voice_select_device default\n");
	Con_Printf("Select a device by its exact name: voice_select_device \"name\"\n");
}

static void Voice_Status_f(void)
{
	voice_settings_profile_t *profile = Voice_Profile();
	Con_Printf("Voice receive %s; microphone transmission %s; local reflections %s; capture %s; mode %s.\n",
		voice_receive.value ? "on" : "off",
		profile->transmit ? "on" : "off",
		profile->self_reverb ? "on" : "off",
		voice_capture_device ? "active" : "inactive",
		profile->mode ? "push-to-talk" : "VAD");
	Con_Printf("Voice input: %s\n", profile->device[0] ? profile->device : "system default");
	if (voice_pending_action != VOICE_PENDING_NONE)
		Con_Printf("A local voice change is waiting for physical Y confirmation.\n");
}

static void Voice_SelectDevice_f(void)
{
	voice_settings_profile_t *profile;
	const char *name;
	if (Cmd_Argc() != 2)
	{
		Con_Printf("usage: voice_select_device default | \"exact device name\" | none\n");
		return;
	}
	profile = Voice_Profile();
	name = Cmd_Argv(1);
	if (!q_strcasecmp(name, "none"))
	{
		voice_pending_action = VOICE_PENDING_NONE;
		Voice_StopTransmit();
		Voice_CloseCapture();
		profile->device[0] = 0;
		profile->transmit = 0;
		profile->self_reverb = 0;
		voice_capture_wanted = false;
		Voice_SaveSettings();
		Con_Printf("Voice: microphone capture disabled; device preference reset to system default.\n");
		return;
	}
	if (!q_strcasecmp(name, "default"))
	{
		voice_pending_device[0] = 0;
		voice_pending_action = VOICE_PENDING_DEVICE;
		voice_pending_deadline = realtime + VOICE_CONFIRM_SECONDS;
		Con_Printf("Voice: press physical Y in the console within 15 seconds to use the system default microphone.\n");
		return;
	}
	if (strlen(name) >= sizeof(voice_pending_device) || !Voice_DeviceIsUnique(name))
	{
		Con_Printf("Voice: device name is missing, ambiguous, or too long; run voice_devices.\n");
		return;
	}
	q_strlcpy(voice_pending_device, name, sizeof(voice_pending_device));
	voice_pending_action = VOICE_PENDING_DEVICE;
	voice_pending_deadline = realtime + VOICE_CONFIRM_SECONDS;
	Con_Printf("Voice: press physical Y in the console within 15 seconds to select %s.\n", name);
}

static void Voice_Consent_f(void)
{
	voice_settings_profile_t *profile = Voice_Profile();
	if (!Voice_DevicePreferenceAvailable(profile->device))
	{
		Con_Printf("Voice: selected recording device is missing or ambiguous; run voice_devices.\n");
		return;
	}
	voice_pending_action = VOICE_PENDING_CONSENT;
	voice_pending_deadline = realtime + VOICE_CONFIRM_SECONDS;
	Con_Printf("Voice: press physical Y in the console within 15 seconds to authorize microphone capture.\n");
}

static void Voice_Revoke_f(void)
{
	voice_settings_profile_t *profile = Voice_Profile();
	voice_pending_action = VOICE_PENDING_NONE;
	profile->transmit = 0;
	profile->self_reverb = 0;
	Voice_StopTransmit();
	Voice_CloseCapture();
	voice_capture_wanted = false;
	Voice_SaveSettings();
	Con_Printf("Voice: microphone consent revoked; capture stopped.\n");
}

static void Voice_SelfReverb_f(void)
{
	voice_settings_profile_t *profile = Voice_Profile();
	const char *value;
	if (Cmd_Argc() != 2)
	{
		Con_Printf("usage: voice_self_reverb on|off\n");
		return;
	}
	value = Cmd_Argv(1);
	if (!q_strcasecmp(value, "off"))
	{
		voice_pending_action = VOICE_PENDING_NONE;
		profile->self_reverb = 0;
		Spatial_ResetSelf();
		Voice_SaveSettings();
		Voice_RefreshCapture(true);
		Con_Printf("Voice: local microphone reflections off.\n");
		return;
	}
	if (q_strcasecmp(value, "on"))
	{
		Con_Printf("usage: voice_self_reverb on|off\n");
		return;
	}
	if (!Spatial_Active())
	{
		Con_Printf("Voice: local reflections require the active Steam Audio renderer.\n");
		return;
	}
	if (!Voice_DevicePreferenceAvailable(profile->device))
	{
		Con_Printf("Voice: selected recording device is missing or ambiguous; run voice_devices.\n");
		return;
	}
	voice_pending_action = VOICE_PENDING_SELF_REVERB;
	voice_pending_deadline = realtime + VOICE_CONFIRM_SECONDS;
	Con_Printf("Voice: press physical Y in the console within 15 seconds to authorize local wet-only microphone reflections.\n");
}

static void Voice_Mode_f(void)
{
	const char *mode;
	(void)Voice_Profile();
	if (Cmd_Argc() != 2)
	{
		Con_Printf("usage: voice_mode vad|ptt\n");
		return;
	}
	mode = Cmd_Argv(1);
	if (!q_strcasecmp(mode, "ptt"))
		voice_pending_mode = 1;
	else if (!q_strcasecmp(mode, "vad"))
		voice_pending_mode = 0;
	else
	{
		Con_Printf("usage: voice_mode vad|ptt\n");
		return;
	}
	voice_pending_action = VOICE_PENDING_MODE;
	voice_pending_deadline = realtime + VOICE_CONFIRM_SECONDS;
	Con_Printf("Voice: press physical Y in the console within 15 seconds to set %s mode.\n", mode);
}

static int Voice_FindSpeaker(const char *value)
{
	char *end;
	long slot = strtol(value, &end, 10);
	int i;
	if (*value && !*end && slot >= 1 && slot <= cl.maxclients)
		return (int)slot - 1;
	for (i = 0; i < cl.maxclients && i < MAX_SCOREBOARD; ++i)
		if (!q_strcasecmp(value, cl.scores[i].name))
			return i;
	return -1;
}

static void Voice_Mute_f(void)
{
	int slot;
	if (Cmd_Argc() != 2 || (slot = Voice_FindSpeaker(Cmd_Argv(1))) < 0)
	{
		Con_Printf("usage: voice_mute <player name or slot>\n");
		return;
	}
	voice_speakers[slot].muted = !voice_speakers[slot].muted;
	Voice_AtomicSet(&voice_speakers[slot].muted_audio,
		voice_speakers[slot].muted ? 1 : 0);
	Voice_AtomicSet(&voice_speakers[slot].talking,
		!voice_speakers[slot].muted && realtime < voice_speakers[slot].talking_until);
	Spatial_ResetVoice(slot);
	Voice_PublishSpatialVoiceSource(slot);
	Con_Printf("Voice: %s %s.\n", cl.scores[slot].name,
		voice_speakers[slot].muted ? "muted" : "unmuted");
}

static void Voice_PlayerVolume_f(void)
{
	int slot;
	if (Cmd_Argc() != 3 || (slot = Voice_FindSpeaker(Cmd_Argv(1))) < 0)
	{
		Con_Printf("usage: voice_player_volume <player name or slot> <0..2>\n");
		return;
	}
	voice_speakers[slot].volume = CLAMP(0.0f,
		strtof(Cmd_Argv(2), NULL), 2.0f);
	Voice_PublishSpatialVoiceSource(slot);
	Con_Printf("Voice: %s volume %.2f.\n", cl.scores[slot].name,
		voice_speakers[slot].volume);
}

static void Voice_PTTCommand_f(void)
{
	/* Key_Event records physical PTT state directly; command text cannot press it. */
}

static void Voice_RefreshSpeakerRing(voice_speaker_t *speaker)
{
	Voice_AtomicSet(&speaker->pcm_read, 0);
	Voice_AtomicSet(&speaker->pcm_write, 0);
}

qboolean Voice_ConfirmKeyEvent(int key, qboolean down)
{
	voice_settings_profile_t *profile;

	profile = Voice_Profile();
	if (!voice_initialized || !down || key_dest != key_console ||
		(key != 'y' && key != 'Y') || voice_pending_action == VOICE_PENDING_NONE)
		return false;
	if (realtime > voice_pending_deadline)
	{
		voice_pending_action = VOICE_PENDING_NONE;
		Con_Printf("Voice: confirmation expired.\n");
		return true;
	}
	if (voice_pending_action == VOICE_PENDING_CONSENT)
	{
		if (!Voice_DevicePreferenceAvailable(profile->device))
		{
			voice_pending_action = VOICE_PENDING_NONE;
			Con_Printf("Voice: selected device is no longer available uniquely.\n");
			return true;
		}
		profile->transmit = 1;
		Con_Printf("Voice: microphone consent saved. Capture starts only in a negotiated multiplayer session.\n");
	}
	else if (voice_pending_action == VOICE_PENDING_DEVICE)
	{
		if (voice_pending_device[0] && !Voice_DeviceIsUnique(voice_pending_device))
		{
			voice_pending_action = VOICE_PENDING_NONE;
			Con_Printf("Voice: device is no longer uniquely available.\n");
			return true;
		}
		Voice_StopTransmit();
		Voice_CloseCapture();
		q_strlcpy(profile->device, voice_pending_device, sizeof(profile->device));
		Con_Printf("Voice: selected %s.\n",
			profile->device[0] ? profile->device : "system default microphone");
	}
	else if (voice_pending_action == VOICE_PENDING_MODE)
	{
		Voice_StopTransmit();
		profile->mode = (unsigned char)voice_pending_mode;
		Con_Printf("Voice: mode set to %s.\n", profile->mode ? "push-to-talk" : "VAD");
	}
	else if (voice_pending_action == VOICE_PENDING_SELF_REVERB)
	{
		if (!Spatial_Active() || !Voice_DevicePreferenceAvailable(profile->device))
		{
			voice_pending_action = VOICE_PENDING_NONE;
			Con_Printf("Voice: local reflections need Steam Audio and an available microphone.\n");
			return true;
		}
		profile->self_reverb = 1;
		Con_Printf("Voice: local wet-only microphone reflections enabled.\n");
	}
	voice_pending_action = VOICE_PENDING_NONE;
	Voice_SaveSettings();
	Voice_RefreshCapture(true);
	return true;
}

void Voice_PTTKeyEvent(int key, qboolean down)
{
	int i;
	if (key < 0 || key >= MAX_KEYS)
		return;
	voice_ptt_keys[key] = down && key_dest == key_game &&
		Voice_Profile()->transmit && Voice_Profile()->mode == 1 &&
		keybindings[key] && !q_strcasecmp(keybindings[key], "+voicerecord");
	voice_ptt = false;
	for (i = 0; i < MAX_KEYS; ++i)
		voice_ptt |= voice_ptt_keys[i];
}

static void Voice_EncodeCaptureFrame(int16_t *samples)
{
	voice_vad_result_t result;
	qboolean gate;
	unsigned int i;
	float gain = CLAMP(0.0f, voice_input_gain.value, 4.0f);

	for (i = 0; i < VOICE_FRAME_SAMPLES; ++i)
	{
		int sample = (int)(samples[i] * gain);
		samples[i] = (int16_t)CLAMP(-32768, sample, 32767);
	}
	if (Voice_Profile()->self_reverb)
		(void)Spatial_SelfPCM(samples, VOICE_FRAME_SAMPLES);
	Voice_VADSetSensitivity(&voice_vad,
		(int)CLAMP(0.0f, voice_vad_sensitivity.value, 100.0f));
	Voice_VADProcessFrame(&voice_vad, samples, VOICE_FRAME_SAMPLES, &result);
	/* Voice_Frame runs this capture/VAD work on the game/audio owner. The HUD
	 * only reads this atomic snapshot, including during OpenXR stereo drawing. */
	Voice_AtomicSet(&voice_input_level, CLAMP(0, result.meter, 32768));
	gate = Voice_MultiplayerSessionActive() && Voice_Profile()->transmit &&
		voice_capture_device && key_dest == key_game &&
		(Voice_Profile()->mode ? voice_ptt : result.active);

	if (gate && !voice_sending)
	{
		unsigned int count = Voice_Profile()->mode ? 0 :
			q_min(voice_preroll_count, result.preroll_frames);
		voice_talkspurt++;
		if (!voice_talkspurt)
			voice_talkspurt++;
		for (i = count; i > 0; --i)
		{
			unsigned int index = (voice_preroll_write +
				VOICE_VAD_PREROLL_FRAMES - i) % VOICE_VAD_PREROLL_FRAMES;
			Voice_QueuePacket(voice_preroll[index], i == count ? VOICE_FLAG_START : 0);
		}
		voice_sending = Voice_QueuePacket(samples, count ? 0 : VOICE_FLAG_START);
		Voice_AtomicSet(&voice_transmitting, voice_sending ? 1 : 0);
	}
	else if (gate)
	{
		voice_sending = Voice_QueuePacket(samples, 0);
		Voice_AtomicSet(&voice_transmitting, voice_sending ? 1 : 0);
	}
	else if (voice_sending)
	{
		Voice_QueuePacket(NULL, VOICE_FLAG_END);
		voice_sending = false;
		Voice_AtomicSet(&voice_transmitting, 0);
	}

	memcpy(voice_preroll[voice_preroll_write], samples, sizeof(voice_preroll[0]));
	voice_preroll_write = (voice_preroll_write + 1) % VOICE_VAD_PREROLL_FRAMES;
	if (voice_preroll_count < VOICE_VAD_PREROLL_FRAMES)
		voice_preroll_count++;
}

static void Voice_ProcessCapture(void)
{
	int available, processed = 0;
	int16_t frame[VOICE_FRAME_SAMPLES];
	int backlog_limit = VOICE_CAPTURE_FRAME_BYTES * VOICE_CAPTURE_BACKLOG_FRAMES;
	if (!voice_capture_device)
		return;
	available = Voice_CaptureAvailable();
	if (available < 0)
	{
		Voice_CaptureClear();
		Voice_AtomicSet(&voice_input_level, 0);
		return;
	}
	if (available > backlog_limit)
	{
		Voice_CaptureClear();
		Voice_AtomicSet(&voice_input_level, 0);
		return;
	}
	while (available >= VOICE_CAPTURE_FRAME_BYTES &&
		processed < VOICE_CAPTURE_BACKLOG_FRAMES)
	{
		int got = Voice_CaptureRead(frame, VOICE_CAPTURE_FRAME_BYTES);
		if (got != VOICE_CAPTURE_FRAME_BYTES)
			break;
		Voice_EncodeCaptureFrame(frame);
		available -= got;
		processed++;
	}
}

static void Voice_WriteSpeakerPCM(voice_speaker_t *speaker,
	const int16_t *mono, int frames, int slot)
{
	int read, write, outframes;
	float left, right;
	int i;
	if (speaker->muted)
		return;
	if (Spatial_Active())
	{
		(void)Spatial_VoicePCM(slot, mono, frames);
		return;
	}
	read = Voice_AtomicGet(&speaker->pcm_read);
	write = Voice_AtomicGet(&speaker->pcm_write);
	outframes = frames * shm->speed / VOICE_SAMPLE_RATE;
	left = right = voice_radio_volume.value;
	if (slot >= 0 && slot + 1 < cl.num_entities &&
		cl.entities[slot + 1].model && cl.entities[slot + 1].msgtime == cl.mtime[0])
	{
		vec3_t delta;
		float distance, blend, pan, positional;
		VectorSubtract(cl.entities[slot + 1].origin, voice_listener_origin, delta);
		distance = VectorLength(delta);
		pan = distance > 1.0f ? DotProduct(delta, voice_listener_right) / distance : 0;
		pan = CLAMP(-1.0f, pan, 1.0f);
		blend = CLAMP(0.0f, distance / q_max(1.0f, voice_spatial_distance.value), 1.0f);
		positional = 1.0f / (1.0f + distance / 512.0f);
		left = (1.0f - blend) * positional * (1.0f - 0.5f * pan) +
			blend * voice_radio_volume.value;
		right = (1.0f - blend) * positional * (1.0f + 0.5f * pan) +
			blend * voice_radio_volume.value;
	}
	left *= VOICE_PLAYBACK_GAIN * speaker->volume * voice_volume.value;
	right *= VOICE_PLAYBACK_GAIN * speaker->volume * voice_volume.value;
	for (i = 0; i < outframes; ++i)
	{
		int next = (write + 1) % VOICE_PCM_RING_FRAMES;
		int source = (int)((int64_t)i * frames / q_max(outframes, 1));
		if (next == read)
			break;
		speaker->pcm[write * 2] = (int16_t)CLAMP(-32768,
			(int)(mono[source] * left), 32767);
		speaker->pcm[write * 2 + 1] = (int16_t)CLAMP(-32768,
			(int)(mono[source] * right), 32767);
		write = next;
	}
	Voice_AtomicSet(&speaker->pcm_write, write);
}

static void Voice_PublishSpatialVoiceSource(int slot)
{
#ifdef USE_STEAMAUDIO
	voice_speaker_t *speaker;
	entity_t *entity = NULL;
	vrik_pose_t pose;
	vec3_t origin;
	qboolean active, position_valid = false;
	float gain;

	if (slot < 0 || slot >= MAX_SCOREBOARD)
		return;
	speaker = &voice_speakers[slot];
	active = voice_initialized && voice_receive.value != 0 &&
		slot < cl.maxclients && cl.scores && cl.scores[slot].name[0] &&
		speaker->have_generation && !speaker->muted;
	gain = VOICE_PLAYBACK_GAIN * speaker->volume * voice_volume.value;

	if (cl.entities && slot + 1 < cl.num_entities)
	{
		entity = &cl.entities[slot + 1];
		if (entity->model && entity->msgtime == cl.mtime[0])
		{
			VectorCopy(entity->origin, origin);
			origin[2] += 18.0f;
			position_valid = true;
			if (R_VRIKSampleEntityPose(entity, &pose))
			{
				vec3_t forward, right, up, mouth;
				float yaw = DEG2RAD(pose.body_yaw);
				float cy = cosf(yaw), sy = sinf(yaw);

				AngleVectors(pose.orientation[VRIK_TRACKER_HEAD],
					forward, right, up);
				VectorMA(pose.position[VRIK_TRACKER_HEAD], 2.0f, forward, mouth);
				VectorMA(mouth, -2.0f, up, mouth);
				origin[0] = entity->origin[0] + cy * mouth[0] - sy * mouth[1];
				origin[1] = entity->origin[1] + sy * mouth[0] + cy * mouth[1];
				origin[2] = entity->origin[2] + mouth[2];
			}
		}
	}
	Spatial_VoiceSource(slot, active, position_valid ? origin : NULL,
		position_valid, gain, 0.0f);
#else
	(void)slot;
#endif
}

static void Voice_ResetSpatialStreams(void)
{
	int slot;
	for (slot = 0; slot < MAX_SCOREBOARD; ++slot)
	{
		Spatial_ResetVoice(slot);
		Voice_PublishSpatialVoiceSource(slot);
	}
}

static void Voice_ReceiveCallback(int source_slot, uint32_t generation,
	const voice_packet_t *packet, void *opaque)
{
	(void)opaque;
	Voice_ReceivePacket(source_slot, generation, packet);
}

void Voice_Init(void)
{
	voice_vad_config_t config;
	int error, i;
	Cvar_RegisterVariable(&voice_receive);
	Cvar_RegisterVariable(&voice_hud);
	Cvar_RegisterVariable(&voice_input_gain);
	Cvar_RegisterVariable(&voice_vad_sensitivity);
	Cvar_RegisterVariable(&voice_volume);
	Cvar_RegisterVariable(&voice_radio_volume);
	Cvar_RegisterVariable(&voice_spatial_distance);
	Cvar_RegisterVariable(&voice_positional_only);
	Cvar_RegisterVariable(&voice_self_reverb_volume);
	Voice_LoadSettings();
	Cmd_AddCommand("voice_devices", Voice_ListDevices_f);
	Cmd_AddCommand("voice_select_device", Voice_SelectDevice_f);
	Cmd_AddCommand("voice_consent", Voice_Consent_f);
	Cmd_AddCommand("voice_revoke", Voice_Revoke_f);
	Cmd_AddCommand("voice_self_reverb", Voice_SelfReverb_f);
	Cmd_AddCommand("voice_mode", Voice_Mode_f);
	Cmd_AddCommand("voice_status", Voice_Status_f);
	Cmd_AddCommand("voice_mute", Voice_Mute_f);
	Cmd_AddCommand("voice_player_volume", Voice_PlayerVolume_f);
	Cmd_AddCommand("+voicerecord", Voice_PTTCommand_f);
	Cmd_AddCommand("-voicerecord", Voice_PTTCommand_f);

	voice_encoder = opus_encoder_create(VOICE_SAMPLE_RATE, 1,
		OPUS_APPLICATION_VOIP, &error);
	if (!voice_encoder || error != OPUS_OK)
	{
		Con_Printf("Voice unavailable: couldn't create Opus encoder: %s\n",
			opus_strerror(error));
		return;
	}
	opus_encoder_ctl(voice_encoder, OPUS_SET_BITRATE(24000));
	opus_encoder_ctl(voice_encoder, OPUS_SET_VBR(1));
	opus_encoder_ctl(voice_encoder, OPUS_SET_DTX(1));
	Voice_VADConfigDefault(&config);
	Voice_VADInit(&voice_vad, &config);
	for (i = 0; i < MAX_SCOREBOARD; ++i)
	{
		voice_speakers[i].decoder = opus_decoder_create(VOICE_SAMPLE_RATE, 1, &error);
		if (!voice_speakers[i].decoder || error != OPUS_OK)
		{
			Con_Printf("Voice unavailable: couldn't create Opus decoder: %s\n",
				opus_strerror(error));
			Voice_Shutdown();
			return;
		}
		Voice_JitterInit(&voice_speakers[i].jitter, VOICE_JITTER_MAX_PACKETS);
		voice_speakers[i].volume = 1.0f;
		voice_speakers[i].muted = false;
		Voice_AtomicSet(&voice_speakers[i].muted_audio, 0);
		Voice_AtomicSet(&voice_speakers[i].talking, 0);
		Voice_RefreshSpeakerRing(&voice_speakers[i]);
	}
	voice_initialized = true;
	Voice_AtomicSet(&voice_input_level, 0);
	Voice_AtomicSet(&voice_transmitting, 0);
	Voice_AtomicSet(&voice_vr_transmit_enabled,
		voice_settings.vr.transmit ? 1 : 0);
	Voice_AtomicSet(&voice_transmit_enabled, Voice_Profile()->transmit ? 1 : 0);
	Voice_AtomicSet(&voice_capture_ready, voice_capture_device ? 1 : 0);
	Voice_AtomicSet(&voice_hud_visible, Voice_HUDShouldDisplay() ? 1 : 0);
	Voice_AtomicSet(&voice_receive_enabled, voice_receive.value != 0);
	CL_SetVoiceReceiveCallback(Voice_ReceiveCallback, NULL);
	if (shm)
		SNDDMA_LockBuffer();
	Voice_AtomicSet(&voice_audio_enabled, 1);
	if (shm)
		SNDDMA_Submit();
	Voice_ResetSpatialStreams();
	Voice_RefreshCapture(true);
	if (voice_profile_vr)
		Con_Printf("Voice ready. VR microphone transmission is %s; toggle Microphone in VR Options or use voice_revoke to turn it off.\n",
			Voice_Profile()->transmit ? "on" : "off");
	else
		Con_Printf("Voice ready. Microphone transmission is %s; use voice_consent to opt in, or voice_select_device for another microphone.\n",
			Voice_Profile()->transmit ? "on" : "off");
}

void Voice_Shutdown(void)
{
	int i;
	if (!voice_initialized && !voice_encoder)
		return;
	CL_SetVoiceReceiveCallback(NULL, NULL);
	Voice_StopTransmit();
	Voice_ClearNetworkQueue();
	Voice_CloseCapture();
	if (shm)
		SNDDMA_LockBuffer();
	Voice_AtomicSet(&voice_audio_enabled, 0);
	voice_initialized = false;
	voice_menu_state.available = false;
	Voice_AtomicSet(&voice_hud_visible, 0);
	Voice_AtomicSet(&voice_transmit_enabled, 0);
	Voice_AtomicSet(&voice_capture_ready, 0);
	if (shm)
		SNDDMA_Submit();
	Voice_ResetSpatialStreams();
	voice_capture_wanted = false;
	voice_pending_action = VOICE_PENDING_NONE;
	for (i = 0; i < MAX_SCOREBOARD; ++i)
	{
		if (voice_speakers[i].decoder)
			opus_decoder_destroy(voice_speakers[i].decoder);
		voice_speakers[i].decoder = NULL;
	}
	if (voice_encoder)
		opus_encoder_destroy(voice_encoder);
	voice_encoder = NULL;
}

void Voice_ResetConnection(void)
{
	int i;
	voice_pending_action = VOICE_PENDING_NONE;
	Voice_StopTransmit();
	Voice_CloseCapture();
	voice_capture_wanted = false;
	voice_last_session_active = false;
	voice_next_sequence = 0;
	voice_next_timestamp = 0;
	voice_talkspurt = 0;
	if (shm)
		SNDDMA_LockBuffer();
	for (i = 0; i < MAX_SCOREBOARD; ++i)
	{
		Voice_JitterReset(&voice_speakers[i].jitter);
		voice_speakers[i].have_generation = false;
		voice_speakers[i].generation = 0;
		voice_speakers[i].talking_until = 0;
		Voice_AtomicSet(&voice_speakers[i].talking, 0);
		voice_speakers[i].muted = false;
		Voice_AtomicSet(&voice_speakers[i].muted_audio, 0);
		voice_speakers[i].volume = 1.0f;
		Voice_RefreshSpeakerRing(&voice_speakers[i]);
		if (voice_speakers[i].decoder)
			opus_decoder_ctl(voice_speakers[i].decoder, OPUS_RESET_STATE);
	}
	if (shm)
		SNDDMA_Submit();
	Voice_ResetSpatialStreams();
}

void Voice_Frame(void)
{
	unsigned int now = (unsigned int)(realtime * 1000.0);
	int slot;
	if (!voice_initialized)
		return;
	Voice_AtomicSet(&voice_receive_enabled, voice_receive.value != 0);
	Voice_AtomicSet(&voice_vr_transmit_enabled,
		voice_settings.vr.transmit ? 1 : 0);
	Voice_RefreshCapture(false);
	Spatial_SelfGain(Voice_Profile()->self_reverb && voice_capture_device ?
		CLAMP(0.0f, voice_self_reverb_volume.value, 2.0f) : 0.0f);
	Voice_ProcessCapture();
	Voice_AtomicSet(&voice_transmit_enabled, Voice_Profile()->transmit ? 1 : 0);
	Voice_AtomicSet(&voice_hud_visible, Voice_HUDShouldDisplay() ? 1 : 0);
	Spatial_VoiceSettings(voice_radio_volume.value,
		q_max(1.0f, voice_spatial_distance.value),
		voice_positional_only.value != 0);
	for (slot = 0; slot < MAX_SCOREBOARD; ++slot)
		Voice_PublishSpatialVoiceSource(slot);
	if (!voice_receive.value)
	{
		if (voice_receive_was_enabled)
		{
			if (shm)
				SNDDMA_LockBuffer();
			for (slot = 0; slot < MAX_SCOREBOARD; ++slot)
			{
				Voice_JitterReset(&voice_speakers[slot].jitter);
				voice_speakers[slot].talking_until = 0;
				Voice_AtomicSet(&voice_speakers[slot].talking, 0);
				voice_speakers[slot].have_generation = false;
				Voice_RefreshSpeakerRing(&voice_speakers[slot]);
			}
			if (shm)
				SNDDMA_Submit();
			Voice_ResetSpatialStreams();
		}
		voice_receive_was_enabled = false;
		return;
	}
	voice_receive_was_enabled = true;
	for (slot = 0; slot < cl.maxclients && slot < MAX_SCOREBOARD; ++slot)
	{
		voice_speaker_t *speaker = &voice_speakers[slot];
		voice_jitter_frame_t frame;
		int frames;
		if (realtime >= speaker->talking_until)
			Voice_AtomicSet(&speaker->talking, 0);
		while (Voice_JitterNextFrame(&speaker->jitter, now, &frame) == VOICE_JITTER_OK &&
			frame.action != VOICE_JITTER_WAIT)
		{
			if (frame.flags & VOICE_FLAG_END)
			{
				speaker->talking_until = 0;
				Voice_AtomicSet(&speaker->talking, 0);
				Voice_JitterEndTalkspurt(&speaker->jitter);
				continue;
			}
			frames = opus_decode(speaker->decoder,
				frame.action == VOICE_JITTER_PACKET ? frame.payload : NULL,
				frame.action == VOICE_JITTER_PACKET ? (opus_int32)frame.payload_size : 0,
				voice_decode_frame, VOICE_FRAME_SAMPLES, 0);
			if (frames > 0 && shm)
			{
				Voice_WriteSpeakerPCM(speaker, voice_decode_frame, frames, slot);
				speaker->talking_until = realtime + 0.15;
				Voice_AtomicSet(&speaker->talking, speaker->muted ? 0 : 1);
			}
		}
	}
}

void Voice_UpdateSpatialization(const float *origin, const float *right)
{
	if (!origin || !right)
		return;
	VectorCopy(origin, voice_listener_origin);
	VectorCopy(right, voice_listener_right);
}

void Voice_MixAudio(unsigned char *stream, int bytes, int samplebits,
	int channels, int rate, qboolean signed8)
{
	int frames, slot;
	(void)rate;
	if (!Voice_AtomicGet(&voice_audio_enabled) ||
		!Voice_AtomicGet(&voice_receive_enabled) || channels != 2 ||
		(samplebits != 8 && samplebits != 16))
		return;
	if (Spatial_Active())
	{
		for (slot = 0; slot < MAX_SCOREBOARD; ++slot)
		{
			voice_speaker_t *speaker = &voice_speakers[slot];
			Voice_AtomicSet(&speaker->pcm_read,
				Voice_AtomicGet(&speaker->pcm_write));
		}
		return;
	}
	frames = bytes / (channels * (samplebits / 8));
	for (slot = 0; slot < MAX_SCOREBOARD; ++slot)
	{
		voice_speaker_t *speaker = &voice_speakers[slot];
		int read = Voice_AtomicGet(&speaker->pcm_read);
		int write = Voice_AtomicGet(&speaker->pcm_write);
		qboolean muted = Voice_AtomicGet(&speaker->muted_audio) != 0;
		int i;
		for (i = 0; i < frames; ++i)
		{
			int ch;
			if (read == write)
				break;
			if (!muted)
			{
				for (ch = 0; ch < 2; ++ch)
				{
					int voice = speaker->pcm[read * 2 + ch];
					if (samplebits == 16)
					{
						int16_t *output = (int16_t *)stream;
						output[i * 2 + ch] = (int16_t)CLAMP(-32768,
							(int)output[i * 2 + ch] + voice, 32767);
					}
					else
					{
						int base = signed8 ? (int)(int8_t)stream[i * 2 + ch] :
							(int)stream[i * 2 + ch] - 128;
						int mixed = CLAMP(-128, base + voice / 256, 127);
						stream[i * 2 + ch] = signed8 ? (uint8_t)(int8_t)mixed :
							(uint8_t)(mixed + 128);
					}
				}
			}
			read = (read + 1) % VOICE_PCM_RING_FRAMES;
		}
		Voice_AtomicSet(&speaker->pcm_read, read);
	}
}

void Voice_ReceivePacket(int source_slot, uint32_t generation,
	const voice_packet_t *packet)
{
	voice_speaker_t *speaker;
	voice_jitter_packet_t incoming;
	if (!voice_initialized || !voice_receive.value || !packet ||
		source_slot < 0 || source_slot >= MAX_SCOREBOARD ||
		!Voice_PacketIsValid(packet))
		return;
	speaker = &voice_speakers[source_slot];
	/* Server generations increase monotonically (skipping zero). Ignore a
	 * delayed packet from the retired stream instead of resetting back to it. */
	if (speaker->have_generation && generation != speaker->generation &&
		(uint32_t)(generation - speaker->generation) >= 0x80000000u)
		return;
	if (!speaker->have_generation || speaker->generation != generation)
	{
		if (shm)
			SNDDMA_LockBuffer();
		Voice_JitterReset(&speaker->jitter);
		Voice_RefreshSpeakerRing(speaker);
		opus_decoder_ctl(speaker->decoder, OPUS_RESET_STATE);
		speaker->generation = generation;
		speaker->have_generation = true;
		speaker->talking_until = 0;
		Voice_AtomicSet(&speaker->talking, 0);
		if (shm)
			SNDDMA_Submit();
		Spatial_ResetVoice(source_slot);
	}
	incoming.sequence = packet->sequence;
	incoming.timestamp = packet->timestamp;
	incoming.talkspurt = packet->talkspurt;
	incoming.flags = packet->flags;
	incoming.payload = packet->payload;
	incoming.payload_size = packet->payload_bytes;
	Voice_JitterInsert(&speaker->jitter, &incoming,
		(unsigned int)(realtime * 1000.0));
}

qboolean Voice_SpeakerTalking(int source_slot)
{
	return source_slot >= 0 && source_slot < MAX_SCOREBOARD &&
		Voice_AtomicGet(&voice_speakers[source_slot].talking) != 0;
}

qboolean Voice_TransmitEnabled(void)
{
	return Voice_AtomicGet(&voice_transmit_enabled) != 0;
}

qboolean Voice_VRTransmitEnabled(void)
{
	return Voice_AtomicGet(&voice_vr_transmit_enabled) != 0;
}

void Voice_SetVRTransmitEnabled(qboolean enabled)
{
	Voice_SyncProfile();

	voice_settings.vr.transmit = enabled ? 1 : 0;
	if (!enabled)
		voice_settings.vr.self_reverb = 0;
	Voice_AtomicSet(&voice_vr_transmit_enabled, enabled ? 1 : 0);
	if (!voice_profile_vr)
	{
		Voice_SaveSettings();
		return;
	}
	Voice_FinalizeMenuAction();
}

void Voice_GetMenuState(voice_menu_state_t *state)
{
	if (state)
	{
		*state = voice_menu_state;
		state->capture_failed = voice_capture_wanted && !voice_capture_device;
	}
}

qboolean Voice_CaptureReady(void)
{
	return Voice_AtomicGet(&voice_capture_ready) != 0;
}

qboolean Voice_IsTransmitting(void)
{
	return Voice_AtomicGet(&voice_transmitting) != 0;
}

float Voice_InputLevel(void)
{
	return Voice_AtomicGet(&voice_input_level) / 32768.0f;
}

qboolean Voice_HUDEnabled(void)
{
	return Voice_AtomicGet(&voice_hud_visible) != 0;
}
