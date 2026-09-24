/* Game-thread adapter for the optional Steam Audio renderer. */
#ifndef SND_SPATIAL_H
#define SND_SPATIAL_H

#ifdef USE_STEAMAUDIO

/* Include after quakedef.h, as with q_sound.h throughout Quake/. */
#include "snd_steamaudio.h"

void Spatial_Register(void);
int Spatial_Init(void);
int Spatial_Active(void);
/* Call after the DMA callback has been stopped/quiesced. */
void Spatial_Shutdown(void);
void Spatial_Reset(void);

/*
 * cache is the stable, live sfxcache_t allocation. Call ForgetCache(cache)
 * before freeing an individual cache; call ClearCache before bulk cache frees.
 */
int Spatial_CacheSound(sfxcache_t *cache);
void Spatial_ForgetCache(sfxcache_t *cache);
void Spatial_ClearCache(void);
void Spatial_Start(int channel, sfxcache_t *cache, int offset);
void Spatial_Stop(int channel);
/*
 * Publishes current snd_channels[] and listener_* state on the game thread.
 * Call outside snd_mutex: this lazily calls S_LoadSound for uncached channels,
 * and S_LoadSound takes snd_mutex itself.
 */
void Spatial_Update(void);
void Spatial_Listener(const float *origin, const float *forward,
	const float *right, const float *up);
void Spatial_SetSettings(const sa_settings_t *settings);
/* Publishes S_SetUnderwaterIntensity's already-smoothed filter coefficient. */
void Spatial_SetUnderwaterAlpha(float alpha);
int Spatial_Clock(void);
unsigned Spatial_ChannelGeneration(int channel);
/* Zero until finished; otherwise returns the finished source generation. */
unsigned Spatial_Finished(int channel);
/* progress.position is converted to the channel cache's sample rate. */
void Spatial_GetProgress(int channel, sa_progress_t *progress);

/* Native renderer format is interleaved float stereo at SA_RATE. */
void Spatial_Render(float *stereo, int frames);

/*
 * Music input is interleaved mono/stereo U8 or native-endian S16.
 * MusicSpace returns the maximum whole source frames accepted for this format,
 * bounded by the SA ring/converter tail and the remaining
 * MAX_RAW_SAMPLES-(s_rawend-paintedtime) clock window. It can be used directly
 * to size BGM_UpdateStream's next decode. The converter tail is limited to 256
 * frames beyond the 8191-frame SA ring. RawSamples accepts the whole chunk
 * or returns 0 without queueing any of it. On success it advances s_rawend in
 * shm->speed frames. Keep BGM's paintedtime/MAX_RAW_SAMPLES budget as an
 * additional limit; clear music before changing stream format or track. If a
 * chunk is rejected, treat that as a stream error; do not advance the decoder.
 */
int Spatial_MusicSpace(int rate, int width, int channels);
int Spatial_RawSamples(int samples, int rate, int width, int channels,
	const byte *data, float volume);
/* Flushes converter delay; Spatial_Update pumps any remaining tail as space opens. */
void Spatial_FinishMusic(void);
void Spatial_ClearMusic(void);

/* Optional voice integration hooks. PCM is mono signed 16-bit at SA_RATE. */
void Spatial_VoiceSettings(float radio_gain, float distance, int pure_voice);
void Spatial_VoiceSource(int slot, int active, const float *origin,
	int position_valid, float gain, float room_send);
int Spatial_VoicePCM(int slot, const int16_t *pcm, int frames);
void Spatial_ResetVoice(int slot);
/* Local microphone monitor; reset excludes the audio callback. */
void Spatial_SelfGain(float gain);
int Spatial_SelfPCM(const int16_t *pcm, int frames);
void Spatial_ResetSelf(void);

/* geometry ownership transfers to this call; simulation starts on its worker. */
int Spatial_ReplaceRoom(sa_geometry_t *geometry);
void Spatial_ClearWorld(void);
void Spatial_RoomStats(sa_room_stats_t *stats);

#else

#define Spatial_Register() ((void)0)
#define Spatial_Init() 0
#define Spatial_Active() 0
#define Spatial_Shutdown() ((void)0)
#define Spatial_Reset() ((void)0)
#define Spatial_CacheSound(cache) 0
#define Spatial_ForgetCache(cache) ((void)0)
#define Spatial_ClearCache() ((void)0)
#define Spatial_Start(channel, cache, offset) ((void)0)
#define Spatial_Stop(channel) ((void)0)
#define Spatial_Update() ((void)0)
#define Spatial_Listener(origin, forward, right, up) ((void)0)
#define Spatial_SetSettings(settings) ((void)0)
#define Spatial_SetUnderwaterAlpha(alpha) ((void)0)
#define Spatial_Clock() 0
#define Spatial_ChannelGeneration(channel) 0
#define Spatial_Finished(channel) 0
#define Spatial_GetProgress(channel, progress) ((void)0)
#define Spatial_Render(stereo, frames) ((void)0)
#define Spatial_MusicSpace(rate, width, channels) 0
#define Spatial_RawSamples(samples, rate, width, channels, data, volume) 0
#define Spatial_FinishMusic() ((void)0)
#define Spatial_ClearMusic() ((void)0)
#define Spatial_VoiceSettings(gain, distance, pure) ((void)0)
#define Spatial_VoiceSource(slot, active, origin, valid, gain, send) ((void)0)
#define Spatial_VoicePCM(slot, pcm, frames) 0
#define Spatial_ResetVoice(slot) ((void)0)
#define Spatial_SelfGain(gain) ((void)0)
#define Spatial_SelfPCM(pcm, frames) 0
#define Spatial_ResetSelf() ((void)0)
#define Spatial_ReplaceRoom(geometry) 0
#define Spatial_ClearWorld() ((void)0)
#define Spatial_RoomStats(stats) ((void)0)

#endif
#endif
