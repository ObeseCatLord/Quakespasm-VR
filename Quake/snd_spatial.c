/* GPL-2.0-or-later. Game-thread adapter; rendering stays in snd_steamaudio.c. */
#include "quakedef.h"

#ifdef USE_STEAMAUDIO

#include "snd_spatial.h"
#include "vr_input.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SPATIAL_MUSIC_MAX_INPUT_FRAMES 32768
#define SPATIAL_MUSIC_CONVERTER_CAPACITY SA_BLOCK

#ifdef USE_SDL3
#include <SDL3/SDL.h>
#define SPATIAL_AUDIO_F32 SDL_AUDIO_F32
#define SPATIAL_AUDIO_S8 SDL_AUDIO_S8
#define SPATIAL_AUDIO_S16 SDL_AUDIO_S16
#define SPATIAL_AUDIO_U8 SDL_AUDIO_U8
#else
#include <SDL.h>
#define SPATIAL_AUDIO_F32 AUDIO_F32SYS
#define SPATIAL_AUDIO_S8 AUDIO_S8
#define SPATIAL_AUDIO_S16 AUDIO_S16SYS
#define SPATIAL_AUDIO_U8 AUDIO_U8
#endif

typedef struct spatial_sample_s {
	sfxcache_t *identity;
	sa_sample_t sample;
	struct spatial_sample_s *next;
} spatial_sample_t;

static sa_renderer_t *spatial_renderer;
static spatial_sample_t *spatial_samples;
/* The only adapter-side source state: one published snapshot per renderer slot. */
static sa_source_t spatial_sources[MAX_CHANNELS + MAX_SCOREBOARD];
static sa_settings_t spatial_settings = {
	.hrtf = 1,
	.radio_gain = 0.45f,
	.voice_distance = 768.0f,
	.radio_filter = 1.0f,
	.occlusion = 1.0f,
	.reverb = 0.25f,
	.voice_reverb = 0.12f,
	.room_mode = 2,
	.room_rays = 2048,
	.room_bounces = 16
};
static cvar_t snd_hrtf = {
	.name = "snd_hrtf",
	.string = "1",
	.flags = CVAR_ARCHIVE
};
static cvar_t snd_spatial_weapons = {"snd_spatial_weapons", "1", CVAR_ARCHIVE};
static SDL_AudioStream *music_converter;
static int music_rate, music_width, music_channels;
static float music_gain = 0.5f;
static int music_rejection_reported;
static void Spatial_PumpMusic(void);

static void Spatial_ReportMusicRejection(const char *reason)
{
	if (music_rejection_reported)
		return;
	Con_Printf("Steam Audio music input rejected (%s); check Spatial_MusicSpace before decoding\n", reason);
	music_rejection_reported = 1;
}

static SDL_AudioStream *Spatial_CreateStream(int src_format, int src_channels,
	int src_rate, int dst_format, int dst_channels, int dst_rate)
{
#ifdef USE_SDL3
	SDL_AudioSpec src = {0}, dst = {0};
	src.format = (SDL_AudioFormat)src_format;
	src.channels = src_channels;
	src.freq = src_rate;
	dst.format = (SDL_AudioFormat)dst_format;
	dst.channels = dst_channels;
	dst.freq = dst_rate;
	return SDL_CreateAudioStream(&src, &dst);
#else
	return SDL_NewAudioStream((Uint16)src_format, (Uint8)src_channels, src_rate,
		(Uint16)dst_format, (Uint8)dst_channels, dst_rate);
#endif
}

static void Spatial_FreeStream(SDL_AudioStream *stream)
{
	if (!stream)
		return;
#ifdef USE_SDL3
	SDL_DestroyAudioStream(stream);
#else
	SDL_FreeAudioStream(stream);
#endif
}

static void Spatial_ClearStream(SDL_AudioStream *stream)
{
	if (!stream)
		return;
#ifdef USE_SDL3
	SDL_ClearAudioStream(stream);
#else
	SDL_AudioStreamClear(stream);
#endif
}

static int Spatial_StreamPut(SDL_AudioStream *stream, const void *data, int bytes)
{
#ifdef USE_SDL3
	return SDL_PutAudioStreamData(stream, data, bytes);
#else
	return SDL_AudioStreamPut(stream, data, bytes) == 0;
#endif
}

static int Spatial_StreamFlush(SDL_AudioStream *stream)
{
#ifdef USE_SDL3
	return SDL_FlushAudioStream(stream);
#else
	return SDL_AudioStreamFlush(stream) == 0;
#endif
}

static int Spatial_StreamAvailable(SDL_AudioStream *stream)
{
#ifdef USE_SDL3
	return SDL_GetAudioStreamAvailable(stream);
#else
	return SDL_AudioStreamAvailable(stream);
#endif
}

static int Spatial_StreamGet(SDL_AudioStream *stream, void *data, int bytes)
{
#ifdef USE_SDL3
	return SDL_GetAudioStreamData(stream, data, bytes);
#else
	return SDL_AudioStreamGet(stream, data, bytes);
#endif
}

static spatial_sample_t *Spatial_FindSample(sfxcache_t *cache)
{
	spatial_sample_t *entry;
	for (entry = spatial_samples; entry; entry = entry->next)
		if (entry->identity == cache)
			return entry;
	return NULL;
}

int Spatial_CacheSound(sfxcache_t *cache)
{
	SDL_AudioStream *converter;
	spatial_sample_t *entry;
	int format, bytes, got;
	if (!spatial_renderer || !cache || cache->length <= 0 || cache->speed <= 0 ||
		(cache->width != 1 && cache->width != 2) || cache->stereo != 0)
		return 0;
	if (Spatial_FindSample(cache))
		return 1;
	if ((size_t)cache->length > (size_t)INT_MAX / (size_t)cache->width)
		return 0;
	format = cache->width == 1 ? SPATIAL_AUDIO_S8 : SPATIAL_AUDIO_S16;
	converter = Spatial_CreateStream(format, 1, cache->speed, SPATIAL_AUDIO_F32, 1, SA_RATE);
	if (!converter)
		return 0;
	bytes = cache->length * cache->width;
	if (!Spatial_StreamPut(converter, cache->data, bytes) || !Spatial_StreamFlush(converter)) {
		Spatial_FreeStream(converter);
		return 0;
	}
	bytes = Spatial_StreamAvailable(converter);
	if (bytes <= 0 || bytes % (int)sizeof(float)) {
		Spatial_FreeStream(converter);
		return 0;
	}
	entry = calloc(1, sizeof(*entry));
	if (!entry) {
		Spatial_FreeStream(converter);
		return 0;
	}
	entry->sample.pcm = malloc((size_t)bytes);
	if (!entry->sample.pcm) {
		free(entry);
		Spatial_FreeStream(converter);
		return 0;
	}
	got = Spatial_StreamGet(converter, (float *)entry->sample.pcm, bytes);
	Spatial_FreeStream(converter);
	if (got != bytes) {
		free((void *)entry->sample.pcm);
		free(entry);
		return 0;
	}
	entry->identity = cache;
	entry->sample.frames = got / (int)sizeof(float);
	entry->sample.loop = cache->loopstart < 0 ? -1 :
		(int)((int64_t)cache->loopstart * SA_RATE / cache->speed);
	entry->next = spatial_samples;
	spatial_samples = entry;
	return 1;
}

static unsigned Spatial_NextGeneration(unsigned generation)
{
	++generation;
	return generation ? generation : 1;
}

void Spatial_Start(int channel, sfxcache_t *cache, int offset)
{
	spatial_sample_t *entry;
	sa_source_t *source;
	if (!spatial_renderer || channel < 0 || channel >= MAX_CHANNELS)
		return;
	source = &spatial_sources[channel];
	entry = cache && Spatial_CacheSound(cache) ? Spatial_FindSample(cache) : NULL;
	source->generation = Spatial_NextGeneration(source->generation);
	source->sample = entry ? &entry->sample : NULL;
	source->active = entry != NULL;
	source->kind = SA_POSITIONAL;
	source->offset = entry && offset > 0 ?
		(int)((int64_t)offset * SA_RATE / cache->speed) : 0;
	source->position_valid = 0;
	source->gain = 0.0f;
	source->room_send = 1.0f;
	SA_SetSource(spatial_renderer, channel, source);
}

void Spatial_Stop(int channel)
{
	sa_source_t *source;
	if (!spatial_renderer || channel < 0 || channel >= MAX_CHANNELS)
		return;
	source = &spatial_sources[channel];
	source->generation = Spatial_NextGeneration(source->generation);
	source->active = 0;
	source->sample = NULL;
	SA_SetSource(spatial_renderer, channel, source);
}

void Spatial_ForgetCache(sfxcache_t *cache)
{
	spatial_sample_t **link, *entry;
	int i, locked = 0;
	if (!cache)
		return;
	link = &spatial_samples;
	while (*link && (*link)->identity != cache)
		link = &(*link)->next;
	entry = *link;
	if (!entry)
		return;
	if (spatial_renderer) {
		SNDDMA_LockBuffer();
		locked = 1;
		/* SA_SetSource alone leaves retained render snapshots pointing at PCM. */
		SA_ForgetSample(spatial_renderer, &entry->sample);
	}
	for (i = 0; i < MAX_CHANNELS; ++i) {
		if (spatial_sources[i].sample == &entry->sample) {
			spatial_sources[i].generation = Spatial_NextGeneration(spatial_sources[i].generation);
			spatial_sources[i].active = 0;
			spatial_sources[i].sample = NULL;
			if (spatial_renderer)
				SA_SetSource(spatial_renderer, i, &spatial_sources[i]);
		}
	}
	*link = entry->next;
	if (locked)
		SNDDMA_Submit();
	free((void *)entry->sample.pcm);
	free(entry);
}

void Spatial_ClearCache(void)
{
	spatial_sample_t *entry, *next;
	if (spatial_renderer) {
		SNDDMA_LockBuffer();
		for (entry = spatial_samples; entry; entry = entry->next)
			SA_ForgetSample(spatial_renderer, &entry->sample);
	}
	for (int i = 0; i < MAX_CHANNELS; ++i) {
		if (spatial_sources[i].sample) {
			spatial_sources[i].generation = Spatial_NextGeneration(spatial_sources[i].generation);
			spatial_sources[i].active = 0;
			spatial_sources[i].sample = NULL;
			if (spatial_renderer)
				SA_SetSource(spatial_renderer, i, &spatial_sources[i]);
		}
	}
	if (spatial_renderer)
		SNDDMA_Submit();
	for (entry = spatial_samples; entry; entry = next) {
		next = entry->next;
		free((void *)entry->sample.pcm);
		free(entry);
	}
	spatial_samples = NULL;
}

void Spatial_Register(void)
{
	Cvar_RegisterVariable(&snd_hrtf);
	Cvar_RegisterVariable(&snd_spatial_weapons);
}

int Spatial_Init(void)
{
	if (spatial_renderer)
		return 1;
	if (COM_CheckParm("-sndlegacy"))
		return 0;
	spatial_renderer = SA_Create(MAX_CHANNELS + MAX_SCOREBOARD, MAX_SCOREBOARD);
	if (!spatial_renderer)
		return 0;
	spatial_settings.hrtf = snd_hrtf.value != 0;
	SA_SetSettings(spatial_renderer, &spatial_settings);
	return 1;
}

int Spatial_Active(void)
{
	return spatial_renderer != NULL;
}

void Spatial_Shutdown(void)
{
	spatial_sample_t *entry, *next;
	sa_room_t *room;
	if (spatial_renderer) {
		/* The owner must stop/quiesce the DMA callback before this function. */
		room = SA_DetachRoom(spatial_renderer);
		SA_DestroyRoom(room);
		SA_Destroy(spatial_renderer);
		spatial_renderer = NULL;
	}
	for (entry = spatial_samples; entry; entry = next) {
		next = entry->next;
		free((void *)entry->sample.pcm);
		free(entry);
	}
	spatial_samples = NULL;
	memset(spatial_sources, 0, sizeof(spatial_sources));
	Spatial_FreeStream(music_converter);
	music_converter = NULL;
	music_rate = music_width = music_channels = 0;
}

void Spatial_Reset(void)
{
	if (!spatial_renderer)
		return;
	SNDDMA_LockBuffer();
	SA_Reset(spatial_renderer);
	SNDDMA_Submit();
	memset(spatial_sources, 0, sizeof(spatial_sources));
	Spatial_ClearStream(music_converter);
}

void Spatial_SetSettings(const sa_settings_t *settings)
{
	if (!spatial_renderer || !settings)
		return;
	spatial_settings = *settings;
	spatial_settings.hrtf = snd_hrtf.value != 0;
	SA_SetSettings(spatial_renderer, &spatial_settings);
}

void Spatial_Listener(const float *origin, const float *forward,
	const float *right, const float *up)
{
	sa_listener_t listener;
	if (!spatial_renderer || !origin || !forward || !right || !up)
		return;
	memcpy(listener.origin, origin, sizeof(listener.origin));
	memcpy(listener.forward, forward, sizeof(listener.forward));
	memcpy(listener.right, right, sizeof(listener.right));
	memcpy(listener.up, up, sizeof(listener.up));
	listener.timestamp = SDL_GetPerformanceCounter();
	SA_SetListener(spatial_renderer, &listener);
}

void Spatial_Update(void)
{
	int i, count;
	if (!spatial_renderer)
		return;
	Spatial_PumpMusic();
	Spatial_Listener(listener_origin, listener_forward, listener_right, listener_up);
	spatial_settings.hrtf = snd_hrtf.value != 0;
	SA_SetSettings(spatial_renderer, &spatial_settings);
	count = total_channels;
	if (count < 0) count = 0;
	if (count > MAX_CHANNELS) count = MAX_CHANNELS;
	for (i = 0; i < MAX_CHANNELS; ++i) {
		sa_source_t *source = &spatial_sources[i];
		channel_t *channel = &snd_channels[i];
		sfxcache_t *cache = (i < count && channel->sfx) ? channel->sfx->cache : NULL;
		if (i < count && channel->sfx && !cache)
			cache = S_LoadSound(channel->sfx);
		spatial_sample_t *entry = cache ? Spatial_FindSample(cache) : NULL;
		if (cache && !entry && Spatial_CacheSound(cache))
			entry = Spatial_FindSample(cache);
		if (!cache || !entry) {
			if (source->active || source->sample) {
				source->generation = Spatial_NextGeneration(source->generation);
				source->active = 0;
				source->sample = NULL;
				SA_SetSource(spatial_renderer, i, source);
			}
			continue;
		}
		if (source->sample != &entry->sample) {
			source->generation = Spatial_NextGeneration(source->generation);
			source->offset = channel->pos > 0 ?
				(int)((int64_t)channel->pos * SA_RATE / cache->speed) : 0;
			source->sample = &entry->sample;
			source->position_valid = 0;
		}
		source->active = 1;
		source->kind = (i < NUM_AMBIENTS || channel->entchannel == -1 ||
			channel->entnum == cl.viewentity) ? SA_DRY : SA_POSITIONAL;
		source->gain = channel->master_vol * (sfxvolume.value / 510.0f);
		source->attenuation = channel->dist_mult;
		source->room_send = i >= NUM_AMBIENTS && channel->entchannel != -1 ? 1.0f : 0.0f;
		if (snd_spatial_weapons.value && channel->entnum == cl.viewentity &&
			channel->entchannel != -1 &&
			(channel->entchannel == 1 || !strncmp(channel->sfx->name, "weapons/", 8))) {
			vec3_t muzzle, forward;
			if ((!source->position_valid || entry->sample.loop >= 0) &&
				VR_InputCrosshairAimRay(muzzle, forward)) {
				VectorCopy(muzzle, source->origin);
				source->position_valid = 1;
			}
			if (source->position_valid)
				source->kind = SA_POSITIONAL;
		}
		if (source->kind != SA_POSITIONAL || !source->position_valid) {
			memcpy(source->origin, channel->origin, sizeof(source->origin));
			source->position_valid = source->kind == SA_POSITIONAL;
		}
		SA_SetSource(spatial_renderer, i, source);
	}
}

int Spatial_Clock(void)
{
	int rate = shm && shm->speed > 0 ? shm->speed : SA_RATE;
	return spatial_renderer ? (int)((int64_t)SA_Clock(spatial_renderer) * rate / SA_RATE) : 0;
}

unsigned Spatial_ChannelGeneration(int channel)
{
	if (!spatial_renderer || channel < 0 || channel >= MAX_CHANNELS)
		return 0;
	return spatial_sources[channel].generation;
}

unsigned Spatial_Finished(int channel)
{
	if (!spatial_renderer || channel < 0 || channel >= MAX_CHANNELS)
		return 0;
	return SA_Finished(spatial_renderer, channel);
}

void Spatial_GetProgress(int channel, sa_progress_t *progress)
{
	if (!progress)
		return;
	memset(progress, 0, sizeof(*progress));
	if (spatial_renderer && channel >= 0 && channel < MAX_CHANNELS) {
		SA_GetProgress(spatial_renderer, channel, progress);
		if (snd_channels[channel].sfx && snd_channels[channel].sfx->cache &&
			snd_channels[channel].sfx->cache->speed > 0)
			progress->position = (int)((int64_t)progress->position *
				snd_channels[channel].sfx->cache->speed / SA_RATE);
	}
}

void Spatial_Render(float *stereo, int frames)
{
	if (!stereo || frames <= 0)
		return;
	if (!spatial_renderer) {
		memset(stereo, 0, (size_t)frames * 2 * sizeof(*stereo));
		return;
	}
	SA_Render(spatial_renderer, stereo, frames);
}

static void Spatial_PumpMusic(void)
{
	float buffer[1024 * 2];
	int available, space, frames, got, i;
	if (!spatial_renderer || !music_converter)
		return;
	for (;;) {
		available = Spatial_StreamAvailable(music_converter);
		space = SA_MusicSpace(spatial_renderer);
		frames = available / (2 * (int)sizeof(float));
		if (frames <= 0 || space <= 0)
			return;
		if (frames > space) frames = space;
		if (frames > 1024) frames = 1024;
		got = Spatial_StreamGet(music_converter, buffer, frames * 2 * (int)sizeof(float));
		if (got <= 0)
			return;
		frames = got / (2 * (int)sizeof(float));
		for (i = 0; i < frames * 2; ++i)
			buffer[i] *= music_gain;
		if (SA_WriteMusic(spatial_renderer, buffer, frames) != frames)
			return;
	}
}

int Spatial_MusicSpace(int rate, int width, int channels)
{
	int output_space, pending, pending_bytes, source_space;
	int64_t outstanding, engine_space, source_by_clock;
	if (!spatial_renderer || rate <= 0 || rate > 384000 ||
		(width != 1 && width != 2) || (channels != 1 && channels != 2) ||
		!shm || shm->speed <= 0)
		return 0;
	Spatial_PumpMusic();
	if (music_converter && (music_rate != rate || music_width != width || music_channels != channels))
		return 0; /* Caller must clear the prior track before changing its format. */
	pending_bytes = music_converter ? Spatial_StreamAvailable(music_converter) : 0;
	if (pending_bytes < 0)
		return 0;
	pending = pending_bytes / (2 * (int)sizeof(float));
	/* A bounded 256-frame SDL tail plus the 8191-frame SPSC ring exceeds
	 * BGM's current 8192-frame decode window at the adapter's 48 kHz clock. */
	output_space = SA_MusicSpace(spatial_renderer) +
		SPATIAL_MUSIC_CONVERTER_CAPACITY - pending - 2;
	if (output_space <= 0)
		return 0;
	source_space = (int)((int64_t)output_space * rate / SA_RATE);
	if (source_space > SPATIAL_MUSIC_MAX_INPUT_FRAMES)
		source_space = SPATIAL_MUSIC_MAX_INPUT_FRAMES;
	/* Preserve BGM_UpdateStream's s_rawend/paintedtime rolling-window bound. */
	outstanding = (int64_t)(s_rawend > paintedtime ? s_rawend : paintedtime) - paintedtime;
	engine_space = (int64_t)MAX_RAW_SAMPLES - outstanding;
	if (engine_space <= 0)
		return 0;
	source_by_clock = engine_space * rate / shm->speed;
	if (source_space > source_by_clock)
		source_space = (int)source_by_clock;
	return source_space;
}

int Spatial_RawSamples(int samples, int rate, int width, int channels,
	const byte *data, float volume)
{
	int format, space, bytes;
	int64_t engine_frames, new_rawend;
	if (!spatial_renderer || samples <= 0 || rate <= 0 || !data ||
		(width != 1 && width != 2) || (channels != 1 && channels != 2) || !shm || shm->speed <= 0) {
		if (spatial_renderer)
			Spatial_ReportMusicRejection("invalid input or output clock");
		return 0;
	}
	Spatial_PumpMusic();
	space = Spatial_MusicSpace(rate, width, channels);
	if (samples > space || (size_t)samples > (size_t)INT_MAX / (size_t)(width * channels)) {
		Spatial_ReportMusicRejection("bounded queue has insufficient space");
		return 0;
	}
	engine_frames = ((int64_t)samples * shm->speed + rate - 1) / rate;
	new_rawend = s_rawend < paintedtime ? paintedtime : s_rawend;
	if (engine_frames <= 0 || engine_frames > INT_MAX || new_rawend > INT_MAX - engine_frames) {
		Spatial_ReportMusicRejection("sample clock overflow");
		return 0;
	}
	if (!music_converter || music_rate != rate || music_width != width || music_channels != channels) {
		if (music_converter) {
			Spatial_ReportMusicRejection("format changed without clearing the prior track");
			return 0;
		}
		Spatial_FreeStream(music_converter);
		music_converter = NULL;
		music_rate = music_width = music_channels = 0;
		format = width == 1 ? SPATIAL_AUDIO_U8 : SPATIAL_AUDIO_S16;
		music_converter = Spatial_CreateStream(format, channels, rate,
			SPATIAL_AUDIO_F32, 2, SA_RATE);
		if (!music_converter) {
			Spatial_ReportMusicRejection("could not create SDL converter");
			return 0;
		}
		music_rate = rate;
		music_width = width;
		music_channels = channels;
	}
	bytes = samples * width * channels;
	if (!Spatial_StreamPut(music_converter, data, bytes)) {
		Spatial_ReportMusicRejection("SDL converter rejected the complete input block");
		return 0;
	}
	music_gain = isfinite(volume) ? volume * 0.5f : 0.0f;
	s_rawend = (int)(new_rawend + engine_frames);
	Spatial_PumpMusic();
	return 1;
}

void Spatial_FinishMusic(void)
{
	if (!music_converter)
		return;
	Spatial_StreamFlush(music_converter);
	Spatial_PumpMusic();
}

void Spatial_ClearMusic(void)
{
	if (spatial_renderer) {
		SNDDMA_LockBuffer();
		SA_ClearMusic(spatial_renderer);
		SNDDMA_Submit();
	}
	Spatial_ClearStream(music_converter);
	s_rawend = paintedtime;
	music_rejection_reported = 0;
}

void Spatial_VoiceSettings(float radio_gain, float distance, int pure_voice)
{
	spatial_settings.radio_gain = radio_gain;
	spatial_settings.voice_distance = distance;
	spatial_settings.pure_voice = pure_voice != 0;
	if (spatial_renderer)
		SA_SetSettings(spatial_renderer, &spatial_settings);
}

void Spatial_VoiceSource(int slot, int active, const float *origin,
	int position_valid, float gain, float room_send)
{
	sa_source_t *source;
	if (!spatial_renderer || slot < 0 || slot >= MAX_SCOREBOARD)
		return;
	source = &spatial_sources[MAX_CHANNELS + slot];
	if ((source->active != 0) != (active != 0) || !source->generation)
		source->generation = Spatial_NextGeneration(source->generation);
	source->active = active != 0;
	source->kind = SA_VOICE;
	source->gain = gain;
	source->room_send = room_send;
	source->position_valid = position_valid && origin;
	if (source->position_valid)
		memcpy(source->origin, origin, sizeof(source->origin));
	SA_SetSource(spatial_renderer, MAX_CHANNELS + slot, source);
}

int Spatial_VoicePCM(int slot, const int16_t *pcm, int frames)
{
	return spatial_renderer && pcm ? SA_WriteVoice(spatial_renderer, slot, pcm, frames) : 0;
}

void Spatial_ResetVoice(int slot)
{
	if (spatial_renderer && slot >= 0 && slot < MAX_SCOREBOARD) {
		SNDDMA_LockBuffer();
		SA_ResetStream(spatial_renderer, slot);
		SNDDMA_Submit();
		spatial_sources[MAX_CHANNELS + slot].generation =
			Spatial_NextGeneration(spatial_sources[MAX_CHANNELS + slot].generation);
		spatial_sources[MAX_CHANNELS + slot].active = 0;
	}
}

int Spatial_ReplaceRoom(sa_geometry_t *geometry)
{
	sa_room_t *replacement, *detached;
	if (!geometry)
		return 0;
	if (!spatial_renderer) {
		free(geometry->vertices);
		free(geometry->triangles);
		free(geometry->materials);
		memset(geometry, 0, sizeof(*geometry));
		return 0;
	}
	/* SA_CreateRoom transfers geometry and starts the simulation worker here. */
	replacement = SA_CreateRoom(spatial_renderer, geometry);
	if (!replacement)
		return 0;
	SNDDMA_LockBuffer();
	detached = SA_ReplaceRoom(spatial_renderer, replacement);
	SNDDMA_Submit();
	/* Joining the detached worker while callback exclusion is held can stall audio. */
	SA_DestroyRoom(detached);
	return 1;
}

void Spatial_ClearWorld(void)
{
	sa_room_t *detached;
	if (!spatial_renderer)
		return;
	SNDDMA_LockBuffer();
	detached = SA_DetachRoom(spatial_renderer);
	SNDDMA_Submit();
	SA_DestroyRoom(detached);
}

void Spatial_RoomStats(sa_room_stats_t *stats)
{
	if (!stats)
		return;
	memset(stats, 0, sizeof(*stats));
	if (spatial_renderer)
		SA_RoomStats(spatial_renderer, stats);
}

#endif /* USE_STEAMAUDIO */
