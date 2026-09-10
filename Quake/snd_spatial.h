/* Engine boundary for the optional callback-owned renderer. GPL-2.0-or-later. */
#ifndef SND_SPATIAL_H
#define SND_SPATIAL_H
#ifdef USE_STEAMAUDIO
void Spatial_Register(void);
void Spatial_NewMap(void);
void Spatial_SelfEnable(qboolean enabled, float gain);
void Spatial_SelfPCM(const int16_t *pcm, int frames);
void Spatial_ClearWorld(void);
qboolean Spatial_Init(void);
qboolean Spatial_Active(void);
void Spatial_Shutdown(void);
void Spatial_Reset(void);
void Spatial_ClearMusic(void);
void Spatial_FinishMusic(void);
void Spatial_ClearCache(void);
void Spatial_CacheSound(sfx_t *sfx, const wavinfo_t *info, const byte *data);
void Spatial_Start(int channel, sfx_t *sfx, int offset);
void Spatial_Stop(int channel);
void Spatial_SyncClock(void);
void Spatial_Update(void);
void Spatial_Listener(const float *origin, const float *forward, const float *right, const float *up);
void Spatial_Render(unsigned char *stream, int bytes);
void Spatial_VoiceSettings(float radio, float distance, int pure);
void Spatial_VoiceSource(int slot, float gain, qboolean enabled);
void Spatial_VoicePCM(int slot, const int16_t *pcm, int frames);
void Spatial_ResetVoice(int slot); /* caller excludes SDL callback */
int Spatial_MusicSpace(void);
void Spatial_RawSamples(int samples, int rate, int width, int channels, byte *data, float volume);
#else
#define Spatial_SelfEnable(enabled, gain) ((void)0)
#define Spatial_SelfPCM(pcm, frames) ((void)0)
#define Spatial_NewMap() ((void)0)
#define Spatial_ClearWorld() ((void)0)
#define Spatial_Register() ((void)0)
#define Spatial_Init() false
#define Spatial_Active() false
#define Spatial_Shutdown() ((void)0)
#define Spatial_Reset() ((void)0)
#define Spatial_ClearMusic() ((void)0)
#define Spatial_FinishMusic() ((void)0)
#define Spatial_ClearCache() ((void)0)
#define Spatial_CacheSound(sfx, info, data) ((void)0)
#define Spatial_Start(channel, sfx, offset) ((void)0)
#define Spatial_Stop(channel) ((void)0)
#define Spatial_SyncClock() ((void)0)
#define Spatial_Update() ((void)0)
#define Spatial_Listener(origin, forward, right, up) ((void)0)
#define Spatial_Render(stream, bytes) ((void)0)
#define Spatial_VoiceSettings(radio, distance, pure) ((void)0)
#define Spatial_VoiceSource(slot, gain, enabled) ((void)0)
#define Spatial_VoicePCM(slot, pcm, frames) ((void)0)
#define Spatial_ResetVoice(slot) ((void)0)
#define Spatial_MusicSpace() 0
#define Spatial_RawSamples(samples, rate, width, channels, data, volume) ((void)0)
#endif
#endif
