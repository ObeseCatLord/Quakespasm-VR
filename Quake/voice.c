/*
Copyright (C) 2026 Hiina <hiina@hiina.space>

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.
*/

#include "quakedef.h"
#include "voice.h"
#include "voice_jitter.h"
#include "voice_vad.h"

#include <opus/opus.h>
#include "SDL.h"

#define VOICE_OUTGOING_PACKETS 16
#define VOICE_PCM_RING_FRAMES 16384
#define VOICE_CAPTURE_BACKLOG_FRAMES 10

typedef struct voice_outgoing_s
{
	voice_packet_t packets[VOICE_OUTGOING_PACKETS];
	unsigned int read, write;
} voice_outgoing_t;

typedef struct voice_speaker_s
{
	OpusDecoder *decoder;
	voice_jitter_t jitter;
	unsigned int generation;
	int16_t pcm[VOICE_PCM_RING_FRAMES * 2];
	SDL_atomic_t pcm_read, pcm_write;
	float volume;
	qboolean muted;
	double talking_until;
} voice_speaker_t;

static cvar_t voice_receive = {"voice_receive", "1", CVAR_ARCHIVE};
/* These report local menu choices, not script-controlled permissions. Stuffed
 * commands can reach the ordinary command buffer through aliases and exec. */
static cvar_t voice_transmit = {"voice_transmit", "0", CVAR_ROM};
static cvar_t voice_mode = {"voice_mode", "0", CVAR_ROM}; /* 0 VAD, 1 PTT */
static cvar_t voice_input_device = {"voice_input_device", "", CVAR_ARCHIVE};
static cvar_t voice_input_gain = {"voice_input_gain", "1", CVAR_ARCHIVE};
static cvar_t voice_vad_sensitivity = {"voice_vad_sensitivity", "55", CVAR_ARCHIVE};
static cvar_t voice_volume = {"voice_volume", "1", CVAR_ARCHIVE};
static cvar_t voice_radio_volume = {"voice_radio_volume", "0.45", CVAR_ARCHIVE};
static cvar_t voice_spatial_distance = {"voice_spatial_distance", "768", CVAR_ARCHIVE};
static cvar_t voice_hud = {"voice_hud", "1", CVAR_ARCHIVE};

static SDL_AudioDeviceID voice_capture_device;
static OpusEncoder *voice_encoder;
static voice_vad_t voice_vad;
static voice_outgoing_t voice_outgoing;
static voice_speaker_t voice_speakers[MAX_SCOREBOARD];
static int16_t voice_preroll[VOICE_VAD_PREROLL_FRAMES][VOICE_FRAME_SAMPLES];
static unsigned int voice_preroll_write, voice_preroll_count;
static uint16_t voice_next_sequence;
static uint32_t voice_next_timestamp;
static uint8_t voice_talkspurt;
static qboolean voice_initialized, voice_ptt, voice_sending;
static qboolean voice_capture_consent;
static qboolean voice_ptt_keys[MAX_KEYS];
static qboolean voice_ptt_allowed[MAX_KEYS];
static float voice_input_meter;
static vec3_t voice_listener_origin, voice_listener_right;

static qboolean Voice_MultiplayerSessionActive(void)
{
	return cls.state == ca_connected && cl.maxclients > 1 && cl.voice_cap_sent;
}

static void Voice_CloseCapture(void)
{
	if (voice_capture_device)
	{
		SDL_CloseAudioDevice(voice_capture_device);
		voice_capture_device = 0;
	}
}

static qboolean Voice_OpenCapture(void)
{
	SDL_AudioSpec desired;
	const char *device = voice_input_device.string[0] ?
		voice_input_device.string : NULL;

	Voice_CloseCapture();
	if (!voice_capture_consent || !voice_transmit.value)
		return false;
	SDL_zero(desired);
	desired.freq = VOICE_SAMPLE_RATE;
	desired.format = AUDIO_S16SYS;
	desired.channels = 1;
	desired.samples = VOICE_FRAME_SAMPLES;
	desired.callback = NULL;
	voice_capture_device = SDL_OpenAudioDevice(device, SDL_TRUE, &desired,
		NULL, 0);
	if (!voice_capture_device)
	{
		Con_Printf("Voice: couldn't open capture device: %s\n", SDL_GetError());
		return false;
	}
	SDL_PauseAudioDevice(voice_capture_device, SDL_FALSE);
	Con_Printf("Voice: capture device %s, 48000 Hz mono\n",
		device ? device : "(default)");
	return true;
}

static void Voice_ListDevices_f(void)
{
	int count = SDL_GetNumAudioDevices(SDL_TRUE);
	int i;
	Con_Printf("Voice capture devices (%d):\n", q_max(count, 0));
	for (i = 0; i < count; ++i)
		Con_Printf("  %d: %s\n", i, SDL_GetAudioDeviceName(i, SDL_TRUE));
}

static void Voice_Restart_f(void)
{
	if (!voice_initialized)
		return;
	Voice_CloseCapture();
	if (voice_transmit.value)
		Voice_OpenCapture();
}

static void Voice_Status_f(void)
{
	Con_Printf("Voice: %s, receive %s, transmit %s, mode %s, level %.0f%%\n",
		voice_capture_device ? "capture ready" :
			(voice_transmit.value ? "capture unavailable" : "capture disabled"),
		voice_receive.value ? "on" : "off",
		voice_transmit.value ? "armed" : "off",
		(int)voice_mode.value == 1 ? "push-to-talk" : "VAD",
		voice_input_meter * 100.0f);
}

static int Voice_FindSpeaker(const char *value)
{
	int i;
	char *end;
	long slot = strtol(value, &end, 10);
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
	Con_Printf("Voice: %s %s\n", cl.scores[slot].name,
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
	voice_speakers[slot].volume = CLAMP(0.0f, Q_atof(Cmd_Argv(2)), 2.0f);
	Con_Printf("Voice: %s volume %.2f\n", cl.scores[slot].name,
		voice_speakers[slot].volume);
}

/* Bindings retain their command names for Controls, but only a physical input
 * event may press PTT. Server text/aliases can execute these commands too. */
static void Voice_PTTCommand_f(void) { }

void Voice_PTTKeyEvent(int key, qboolean down)
{
	int i;
	if (key < 0 || key >= MAX_KEYS)
		return;
	voice_ptt_keys[key] = down && voice_capture_consent && voice_ptt_allowed[key] &&
		keybindings[key] && !q_strcasecmp(keybindings[key], "+voicerecord");
	voice_ptt = false;
	for (i = 0; i < MAX_KEYS; ++i)
		voice_ptt |= voice_ptt_keys[i];
}

static void Voice_ClearPTT(void)
{
	voice_ptt = false;
	Q_memset(voice_ptt_keys, 0, sizeof(voice_ptt_keys));
}

static qboolean Voice_QueuePacket(const int16_t *samples, unsigned int flags)
{
	voice_packet_t *packet;
	unsigned int next = (voice_outgoing.write + 1) % VOICE_OUTGOING_PACKETS;
	int bytes;

	/* Prefer fresh speech over growing latency when the network cannot drain. */
	if (next == voice_outgoing.read)
		voice_outgoing.read = (voice_outgoing.read + 1) % VOICE_OUTGOING_PACKETS;
	packet = &voice_outgoing.packets[voice_outgoing.write];
	Q_memset(packet, 0, sizeof(*packet));
	packet->sequence = voice_next_sequence++;
	packet->timestamp = voice_next_timestamp;
	packet->talkspurt = voice_talkspurt;
	packet->flags = flags;
	if (samples)
	{
		bytes = opus_encode(voice_encoder, samples, VOICE_FRAME_SAMPLES,
			packet->payload, sizeof(packet->payload));
		if (bytes < 0)
			return false;
		packet->payload_bytes = (uint16_t)bytes;
		voice_next_timestamp += VOICE_FRAME_SAMPLES;
	}
	voice_outgoing.write = next;
	return true;
}

static void Voice_TransmitChanged(cvar_t *var)
{
	if (!voice_initialized)
		return;
	if (var->value && voice_capture_consent)
	{
		if (!voice_capture_device)
			Voice_OpenCapture();
		return;
	}

	/* Disarming transmission also releases the microphone immediately. */
	voice_outgoing.read = voice_outgoing.write = 0;
	if (voice_sending)
		Voice_QueuePacket(NULL, VOICE_FLAG_END);
	voice_sending = false;
	Voice_ClearPTT();
	voice_input_meter = 0.0f;
	voice_preroll_write = voice_preroll_count = 0;
	Voice_CloseCapture();
}

/* Called only from menu input, never registered as console commands. Permission
 * lasts for this process and cannot be restored by config or server text. */
void Voice_SetTransmitEnabled(qboolean enabled)
{
	if (!voice_initialized)
		return;
	voice_capture_consent = enabled;
	Cvar_SetValueROM("voice_transmit", enabled ? 1 : 0);
}

void Voice_SetPTTKeyAllowed(int key, qboolean allowed)
{
	/* Called only when a physical Controls-menu interaction binds a key. */
	if (key >= 0 && key < MAX_KEYS)
		voice_ptt_allowed[key] = allowed;
}

void Voice_SetMode(int mode)
{
	Cvar_SetValueROM("voice_mode", mode == 1 ? 1 : 0);
	Voice_ClearPTT();
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
	Voice_VADSetSensitivity(&voice_vad,
		(int)CLAMP(0.0f, voice_vad_sensitivity.value, 100.0f));
	Voice_VADProcessFrame(&voice_vad, samples, VOICE_FRAME_SAMPLES, &result);
	voice_input_meter = result.meter / 32768.0f;
	gate = Voice_MultiplayerSessionActive() && voice_capture_consent && voice_transmit.value &&
		((int)voice_mode.value == 1 ? voice_ptt : result.active);

	if (gate && !voice_sending)
	{
		unsigned int count = ((int)voice_mode.value == 1) ? 0 :
			q_min(voice_preroll_count, result.preroll_frames);
		voice_talkspurt++;
		if (!voice_talkspurt)
			voice_talkspurt++;
		for (i = count; i > 0; --i)
		{
			unsigned int index = (voice_preroll_write +
				VOICE_VAD_PREROLL_FRAMES - i) % VOICE_VAD_PREROLL_FRAMES;
			Voice_QueuePacket(voice_preroll[index],
				i == count ? VOICE_FLAG_START : 0);
		}
		voice_sending = true;
		Voice_QueuePacket(samples, count ? 0 : VOICE_FLAG_START);
	}
	else if (gate)
		Voice_QueuePacket(samples, 0);
	else if (voice_sending)
	{
		Voice_QueuePacket(NULL, VOICE_FLAG_END);
		voice_sending = false;
	}

	Q_memcpy(voice_preroll[voice_preroll_write], samples, sizeof(voice_preroll[0]));
	voice_preroll_write = (voice_preroll_write + 1) % VOICE_VAD_PREROLL_FRAMES;
	if (voice_preroll_count < VOICE_VAD_PREROLL_FRAMES)
		voice_preroll_count++;
}

static void Voice_WriteSpeakerPCM(voice_speaker_t *speaker,
	const int16_t *mono, int frames, int slot)
{
	int read = SDL_AtomicGet(&speaker->pcm_read);
	int write = SDL_AtomicGet(&speaker->pcm_write);
	int outframes = frames * shm->speed / VOICE_SAMPLE_RATE;
	int i;
	float left = voice_radio_volume.value, right = voice_radio_volume.value;
	vec3_t delta;
	float distance, blend, pan, positional;

	if (slot >= 0 && slot + 1 < cl.num_entities &&
		cl.entities[slot + 1].model && cl.entities[slot + 1].msgtime == cl.mtime[0])
	{
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
	left *= speaker->volume * voice_volume.value;
	right *= speaker->volume * voice_volume.value;
	for (i = 0; i < outframes; ++i)
	{
		int next = (write + 1) % VOICE_PCM_RING_FRAMES;
		int src = (int)((int64_t)i * frames / q_max(outframes, 1));
		if (next == read)
			break;
		speaker->pcm[write * 2] = (int16_t)CLAMP(-32768, (int)(mono[src] * left), 32767);
		speaker->pcm[write * 2 + 1] = (int16_t)CLAMP(-32768, (int)(mono[src] * right), 32767);
		write = next;
	}
	SDL_AtomicSet(&speaker->pcm_write, write);
}

void Voice_Init(void)
{
	voice_vad_config_t config;
	int error, i;
	cvar_t *existing;

	/* Fail closed if a pre-created variable would split the menu permission
	 * from the static state read by capture (e.g. after starting with no sound). */
	existing = Cvar_FindVar("voice_transmit");
	if (existing && existing != &voice_transmit)
	{
		Con_Printf("Voice unavailable: voice_transmit was defined before sound startup. Restart the game.\n");
		return;
	}
	existing = Cvar_FindVar("voice_mode");
	if (existing && existing != &voice_mode)
	{
		Con_Printf("Voice unavailable: voice_mode was defined before sound startup. Restart the game.\n");
		return;
	}

	Cvar_RegisterVariable(&voice_receive);
	Cvar_RegisterVariable(&voice_transmit);
	Cvar_RegisterVariable(&voice_mode);
	Cvar_RegisterVariable(&voice_input_device);
	Cvar_RegisterVariable(&voice_input_gain);
	Cvar_RegisterVariable(&voice_vad_sensitivity);
	Cvar_RegisterVariable(&voice_volume);
	Cvar_RegisterVariable(&voice_radio_volume);
	Cvar_RegisterVariable(&voice_spatial_distance);
	Cvar_RegisterVariable(&voice_hud);
	Cvar_SetCallback(&voice_transmit, Voice_TransmitChanged);
	Cmd_AddCommand("voice_list_devices", Voice_ListDevices_f);
	Cmd_AddCommand("voice_restart", Voice_Restart_f);
	Cmd_AddCommand("voice_status", Voice_Status_f);
	Cmd_AddCommand("voice_mute", Voice_Mute_f);
	Cmd_AddCommand("voice_player_volume", Voice_PlayerVolume_f);
	Cmd_AddCommand("+voicerecord", Voice_PTTCommand_f);
	Cmd_AddCommand("-voicerecord", Voice_PTTCommand_f);

	voice_encoder = opus_encoder_create(VOICE_SAMPLE_RATE, 1,
		OPUS_APPLICATION_VOIP, &error);
	if (!voice_encoder || error != OPUS_OK)
	{
		Con_Printf("Voice: couldn't create Opus encoder: %s\n", opus_strerror(error));
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
			Con_Printf("Voice: couldn't create Opus decoder: %s\n",
				opus_strerror(error));
			Voice_Shutdown();
			return;
		}
		Voice_JitterInit(&voice_speakers[i].jitter, VOICE_JITTER_MAX_PACKETS);
		voice_speakers[i].volume = 1.0f;
	}
	if (shm)
		SNDDMA_LockBuffer();
	voice_initialized = true;
	if (shm)
		SNDDMA_Submit();
	if (voice_transmit.value)
		Voice_OpenCapture();
}

void Voice_Shutdown(void)
{
	int i;
	Voice_CloseCapture();
	voice_capture_consent = false;
	Voice_ClearPTT();
	Q_memset(voice_ptt_allowed, 0, sizeof(voice_ptt_allowed));
	Cvar_SetValueROM("voice_transmit", 0);
	if (shm)
		SNDDMA_LockBuffer();
	voice_initialized = false;
	if (shm)
		SNDDMA_Submit();
	if (voice_encoder)
		opus_encoder_destroy(voice_encoder);
	voice_encoder = NULL;
	for (i = 0; i < MAX_SCOREBOARD; ++i)
	{
		if (voice_speakers[i].decoder)
			opus_decoder_destroy(voice_speakers[i].decoder);
		voice_speakers[i].decoder = NULL;
	}
}

void Voice_ResetConnection(void)
{
	int i;
	voice_outgoing.read = voice_outgoing.write = 0;
	voice_sending = false;
	Voice_ClearPTT();
	voice_preroll_write = voice_preroll_count = 0;
	if (voice_capture_device)
		SDL_ClearQueuedAudio(voice_capture_device);
	/* The callback owns pcm_read. Both indices must change while it is excluded,
	 * or an in-flight callback can republish its old read position. */
	if (shm)
		SNDDMA_LockBuffer();
	for (i = 0; i < MAX_SCOREBOARD; ++i)
	{
		Voice_JitterReset(&voice_speakers[i].jitter);
		voice_speakers[i].generation = 0;
		voice_speakers[i].talking_until = 0;
		voice_speakers[i].muted = false;
		voice_speakers[i].volume = 1.0f;
		SDL_AtomicSet(&voice_speakers[i].pcm_read, 0);
		SDL_AtomicSet(&voice_speakers[i].pcm_write, 0);
	}
	if (shm)
		SNDDMA_Submit();
}

void Voice_Frame(void)
{
	int16_t capture[VOICE_FRAME_SAMPLES], decoded[VOICE_FRAME_SAMPLES];
	unsigned int queued, now = (unsigned int)(realtime * 1000.0);
	int slot;

	if (!voice_initialized)
		return;
	if (voice_capture_device)
	{
		queued = SDL_GetQueuedAudioSize(voice_capture_device);
		if (queued > sizeof(capture) * VOICE_CAPTURE_BACKLOG_FRAMES)
			SDL_ClearQueuedAudio(voice_capture_device);
		while (SDL_GetQueuedAudioSize(voice_capture_device) >= sizeof(capture))
		{
			if (SDL_DequeueAudio(voice_capture_device, capture, sizeof(capture)) !=
				sizeof(capture))
				break;
			Voice_EncodeCaptureFrame(capture);
		}
	}
	if (!voice_receive.value || !shm)
		return;
	for (slot = 0; slot < cl.maxclients && slot < MAX_SCOREBOARD; ++slot)
	{
		voice_speaker_t *speaker = &voice_speakers[slot];
		voice_jitter_frame_t frame;
		int frames;
		while (Voice_JitterNextFrame(&speaker->jitter, now, &frame) == VOICE_JITTER_OK &&
			frame.action != VOICE_JITTER_WAIT)
		{
			if (frame.flags & VOICE_FLAG_END)
			{
				speaker->talking_until = 0;
				Voice_JitterEndTalkspurt(&speaker->jitter);
				continue;
			}
			frames = opus_decode(speaker->decoder,
				frame.action == VOICE_JITTER_PACKET ? frame.payload : NULL,
				frame.action == VOICE_JITTER_PACKET ? (opus_int32)frame.payload_size : 0,
				decoded, VOICE_FRAME_SAMPLES, 0);
			if (frames > 0 && !speaker->muted)
			{
				Voice_WriteSpeakerPCM(speaker, decoded, frames, slot);
				speaker->talking_until = realtime + 0.15;
			}
		}
	}
}

void Voice_UpdateSpatialization(const float *origin, const float *forward,
	const float *right, const float *up)
{
	(void)forward; (void)up;
	VectorCopy(origin, voice_listener_origin);
	VectorCopy(right, voice_listener_right);
}

void Voice_MixAudio(unsigned char *stream, int bytes, int samplebits,
	int channels, int rate, qboolean signed8)
{
	int slot, frames, i;
	(void)rate;
	if (!voice_initialized || !voice_receive.value || channels != 2)
		return;
	frames = bytes / (channels * (samplebits / 8));
	for (slot = 0; slot < MAX_SCOREBOARD; ++slot)
	{
		voice_speaker_t *speaker = &voice_speakers[slot];
		int read = SDL_AtomicGet(&speaker->pcm_read);
		int write = SDL_AtomicGet(&speaker->pcm_write);
		for (i = 0; i < frames && read != write; ++i)
		{
			int ch;
			for (ch = 0; ch < 2; ++ch)
			{
				int voice = speaker->pcm[read * 2 + ch];
				if (samplebits == 16)
				{
					int16_t *out = (int16_t *)stream;
					out[i * 2 + ch] = (int16_t)CLAMP(-32768,
						(int)out[i * 2 + ch] + voice, 32767);
				}
				else if (samplebits == 8)
				{
					int base = signed8 ? (int)(int8_t)stream[i * 2 + ch] :
						(int)stream[i * 2 + ch] - 128;
					int mixed = CLAMP(-128, base + voice / 256, 127);
					stream[i * 2 + ch] = signed8 ? (uint8_t)(int8_t)mixed :
						(uint8_t)(mixed + 128);
				}
			}
			read = (read + 1) % VOICE_PCM_RING_FRAMES;
		}
		SDL_AtomicSet(&speaker->pcm_read, read);
	}
}

void Voice_AppendOutgoing(sizebuf_t *msg)
{
	int packets = 0;
	while (voice_outgoing.read != voice_outgoing.write && packets < 4)
	{
		voice_packet_t *packet = &voice_outgoing.packets[voice_outgoing.read];
		int bytes = VOICE_CLC_HEADER_BYTES + packet->payload_bytes;
		if (!cl.voice_cap_sent || msg->cursize + bytes > msg->maxsize)
			return;
		MSG_WriteByte(msg, clc_voice);
		MSG_WriteShort(msg, packet->sequence);
		MSG_WriteLong(msg, (int)packet->timestamp);
		MSG_WriteByte(msg, packet->talkspurt);
		MSG_WriteByte(msg, packet->flags);
		MSG_WriteShort(msg, packet->payload_bytes);
		if (packet->payload_bytes)
			SZ_Write(msg, packet->payload, packet->payload_bytes);
		voice_outgoing.read = (voice_outgoing.read + 1) % VOICE_OUTGOING_PACKETS;
		packets++;
	}
}

void Voice_ReceivePacket(int speaker_slot, unsigned int generation,
	const voice_packet_t *packet)
{
	voice_speaker_t *speaker;
	voice_jitter_packet_t incoming;
	if (!voice_initialized || speaker_slot < 0 || speaker_slot >= MAX_SCOREBOARD)
		return;
	speaker = &voice_speakers[speaker_slot];
	if (speaker->generation != generation)
	{
		if (shm)
			SNDDMA_LockBuffer();
		SDL_AtomicSet(&speaker->pcm_read, 0);
		SDL_AtomicSet(&speaker->pcm_write, 0);
		if (shm)
			SNDDMA_Submit();
		Voice_JitterReset(&speaker->jitter);
		opus_decoder_ctl(speaker->decoder, OPUS_RESET_STATE);
		speaker->generation = generation;
	}
	incoming.sequence = packet->sequence;
	incoming.timestamp = packet->timestamp;
	incoming.talkspurt = packet->talkspurt;
	incoming.flags = packet->flags;
	incoming.payload = packet->payload;
	incoming.payload_size = packet->payload_bytes;
	Voice_JitterInsert(&speaker->jitter, &incoming,
		(uint32_t)(realtime * 1000.0));
}

qboolean Voice_Available(void) { return voice_initialized; }
const char *Voice_InputDeviceName(void)
{
	return voice_input_device.string[0] ? voice_input_device.string : "default";
}
void Voice_CycleInputDevice(int direction)
{
	int count = SDL_GetNumAudioDevices(SDL_TRUE);
	int current = -1;
	int i, next;

	if (!voice_initialized || count < 0)
		return;
	if (voice_input_device.string[0])
		for (i = 0; i < count; ++i)
			if (!q_strcasecmp(voice_input_device.string,
				SDL_GetAudioDeviceName(i, SDL_TRUE)))
			{
				current = i;
				break;
			}

	/* The default device is entry zero; SDL's named devices follow it. */
	next = (current + 1 + (direction < 0 ? count : 1)) % (count + 1);
	if (!next)
		Cvar_Set("voice_input_device", "");
	else
		Cvar_Set("voice_input_device", SDL_GetAudioDeviceName(next - 1, SDL_TRUE));
	if (voice_transmit.value)
		Voice_OpenCapture();
}
qboolean Voice_TransmitEnabled(void) { return voice_transmit.value != 0; }
qboolean Voice_IsTransmitting(void) { return voice_sending; }
float Voice_InputLevel(void) { return voice_input_meter; }
qboolean Voice_SpeakerTalking(int slot)
{
	return slot >= 0 && slot < MAX_SCOREBOARD &&
		voice_speakers[slot].talking_until > realtime;
}
qboolean Voice_HUDEnabled(void)
{
	return voice_hud.value != 0 && Voice_MultiplayerSessionActive();
}
