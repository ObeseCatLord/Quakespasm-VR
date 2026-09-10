/* GPL-2.0-or-later. Game-thread adapters; the callback only calls SA_Render. */
#include "quakedef.h"
#include "snd_spatial.h"
#include "snd_steamaudio.h"
#include "r_vrik.h"
#include "vr.h"
#include <SDL.h>

static sa_renderer_t *renderer;
static cvar_t snd_hrtf = {"snd_hrtf", "1", CVAR_ARCHIVE};
static cvar_t snd_spatial_weapons = {"snd_spatial_weapons", "1", CVAR_ARCHIVE};
static sa_source_t sources[MAX_CHANNELS];
static sfx_t *bound[MAX_CHANNELS];
static sa_settings_t settings = {1, 0, 0.45f, 768};
static struct { sfx_t *sfx; sa_sample_t sample; } samples[1024];
static int num_samples;
static SDL_AudioStream *music_converter;
static int music_rate, music_width, music_channels;
static float music_gain;
static void Spatial_PumpMusic(void);

static void Spatial_Probe_f(void)
{
    vec3_t origin;
    sfx_t *sfx;
    const char *name = Cmd_Argc() > 1 ? Cmd_Argv(1) : "ambience/fire1.wav";
    if (Cmd_Argc() != 1 && Cmd_Argc() != 2 && Cmd_Argc() != 5) {
        Con_Printf("snd_spatial_probe [sound.wav [x y z]]; stopsound clears probes\n"); return;
    }
    if (cls.signon != SIGNONS) { Con_Printf("Load a map before placing a spatial probe\n"); return; }
    VectorMA(listener_origin, 128, listener_forward, origin);
    if (Cmd_Argc() == 5) {
        origin[0] = Q_atof(Cmd_Argv(2)); origin[1] = Q_atof(Cmd_Argv(3)); origin[2] = Q_atof(Cmd_Argv(4));
    }
    sfx = S_PrecacheSound(name);
    S_StartSound(cl.max_edicts + 1, 0, sfx, origin, 1, 1);
    Con_Printf("Spatial probe %s at %.1f %.1f %.1f\n", name, origin[0], origin[1], origin[2]);
}

static void Spatial_Status_f(void)
{
    sa_stats_t s;
    double ms = 1000.0 / SDL_GetPerformanceFrequency();
    size_t bytes = 0;
    int i;
    if (!renderer) { Con_Printf("Spatial audio: legacy backend (restart without -sndlegacy in a Steam Audio build)\n"); return; }
    if (Cmd_Argc() == 2 && !Q_strcmp(Cmd_Argv(1), "reset")) {
        SDL_LockAudio(); SA_ResetStats(renderer); SDL_UnlockAudio();
    }
    SDL_LockAudio(); SA_GetStats(renderer, &s); SDL_UnlockAudio();
    for (i = 0; i < num_samples; ++i) bytes += samples[i].sample.frames * sizeof(float);
    Con_Printf("Spatial audio: Steam Audio, %d Hz / %d frames, HRTF %s\n", SA_RATE, SA_BLOCK, snd_hrtf.value ? "on" : "off (panning A/B)");
    Con_Printf("sources %d, samples %d (%.2f MiB), blocks %.0f, clock %d\n", s.active, num_samples, bytes / 1048576.0, (double)s.blocks, SA_Clock(renderer));
    Con_Printf("render mean %.3f ms, max %.3f ms, max pose age %.2f ms\n", s.blocks ? s.render_ticks * ms / s.blocks : 0, s.max_render_ticks * ms, s.max_pose_age_ticks * ms);
    Con_Printf("voice queued %d, dropped %d, partial-block missing %.0f, snapshot misses %.0f, clips %.0f, nonfinite %.0f\n", s.stream_frames, s.dropped_frames, (double)s.underrun_frames, (double)s.snapshot_misses, (double)s.clipped, (double)s.nonfinite);
    Con_Printf("SDK allocations/frees during rendering: %.0f\n", (double)s.rt_allocations);
    Con_Printf("Output peak %.4f (1.0 = full scale)\n", s.output_peak);
}
void Spatial_Register(void)
{
    Cvar_RegisterVariable(&snd_hrtf); Cvar_RegisterVariable(&snd_spatial_weapons);
    Cmd_AddCommand("snd_spatial_status", Spatial_Status_f);
    Cmd_AddCommand("snd_spatial_probe", Spatial_Probe_f);
}
qboolean Spatial_Init(void)
{
    if (COM_CheckParm("-sndlegacy")) return false;
    renderer = SA_Create(MAX_CHANNELS + MAX_SCOREBOARD, MAX_SCOREBOARD);
    if (!renderer) Con_Printf("Steam Audio initialization failed; using legacy sound\n");
    else Con_Printf("Steam Audio initialized: late mono-source rendering, %d Hz / %d frames\n", SA_RATE, SA_BLOCK);
    return renderer != NULL;
}
qboolean Spatial_Active(void) { return renderer != NULL; }
void Spatial_Shutdown(void)
{
    int i;
    SA_Destroy(renderer); renderer = NULL;
    for (i = 0; i < num_samples; ++i) free((void *)samples[i].sample.pcm);
    num_samples = 0;
    if (music_converter) SDL_FreeAudioStream(music_converter);
    music_converter = NULL;
    memset(sources, 0, sizeof(sources)); memset(bound, 0, sizeof(bound));
}
void Spatial_Reset(void)
{
    if (!renderer) return;
    SDL_LockAudio();
    SA_Reset(renderer);
    if (music_converter) SDL_AudioStreamClear(music_converter);
    memset(sources, 0, sizeof(sources)); memset(bound, 0, sizeof(bound));
    SDL_UnlockAudio();
    paintedtime = soundtime = s_rawend = 0;
}
void Spatial_ClearMusic(void)
{
    if (!renderer) return;
    SDL_LockAudio(); SA_ClearMusic(renderer); SDL_UnlockAudio();
    if (music_converter) SDL_AudioStreamClear(music_converter);
}
void Spatial_ClearCache(void)
{
    int i;
    if (!renderer) return;
    Spatial_Reset();
    for (i = 0; i < num_samples; ++i) free((void *)samples[i].sample.pcm);
    num_samples = 0;
}

void Spatial_CacheSound(sfx_t *sfx, const wavinfo_t *info, const byte *data)
{
    SDL_AudioStream *converter;
    float *pcm;
    int i, bytes, got;
    if (!renderer) return;
    for (i = 0; i < num_samples; ++i) if (samples[i].sfx == sfx) return;
    if (num_samples == 1024) return;
    converter = SDL_NewAudioStream(info->width == 1 ? AUDIO_U8 : AUDIO_S16LSB, 1, info->rate, AUDIO_F32SYS, 1, SA_RATE);
    if (!converter) return;
    if (SDL_AudioStreamPut(converter, data, info->samples * info->width) < 0 || SDL_AudioStreamFlush(converter) < 0) { SDL_FreeAudioStream(converter); return; }
    bytes = SDL_AudioStreamAvailable(converter);
    pcm = bytes > 0 ? malloc(bytes) : NULL;
    got = pcm ? SDL_AudioStreamGet(converter, pcm, bytes) : 0;
    SDL_FreeAudioStream(converter);
    if (got <= 0) { free(pcm); return; }
    samples[num_samples].sfx = sfx;
    samples[num_samples].sample.pcm = pcm;
    samples[num_samples].sample.frames = got / sizeof(float);
    samples[num_samples].sample.loop = info->loopstart < 0 ? -1 : (int)((int64_t)info->loopstart * SA_RATE / info->rate);
    ++num_samples;
}
void Spatial_Start(int channel, sfx_t *sfx, int offset)
{
    int i;
    sa_source_t *c;
    if (!renderer || channel < 0 || channel >= MAX_CHANNELS) return;
    c = &sources[channel];
    c->active = 0; c->sample = NULL; c->position_valid = 0;
    ++c->generation; if (!c->generation) ++c->generation;
    bound[channel] = sfx;
    for (i = 0; i < num_samples; ++i) if (samples[i].sfx == sfx) {
        c->sample = &samples[i].sample; c->active = 1; break;
    }
    c->offset = offset;
    /* Position/gain are published together by Spatial_Update. */
}
void Spatial_Stop(int channel)
{
    if (!renderer || channel < 0 || channel >= MAX_CHANNELS) return;
    sources[channel].active = 0; bound[channel] = NULL;
    SA_SetSource(renderer, channel, &sources[channel]);
}
void Spatial_SyncClock(void)
{
    int i;
    if (!renderer) return;
    paintedtime = soundtime = SA_Clock(renderer);
    if (paintedtime > 0x40000000) { S_StopAllSounds(true); return; }
    for (i = NUM_AMBIENTS; i < total_channels; ++i)
        if (sources[i].active && sources[i].generation && SA_Finished(renderer, i) == sources[i].generation) {
            snd_channels[i].sfx = NULL; sources[i].active = 0; bound[i] = NULL;
        }
}
void Spatial_Listener(const float *origin, const float *forward, const float *right, const float *up)
{
    sa_listener_t l;
    if (!renderer) return;
    VectorCopy(origin, l.origin); VectorCopy(forward, l.forward);
    VectorCopy(right, l.right); VectorCopy(up, l.up);
    l.timestamp = SDL_GetPerformanceCounter(); SA_SetListener(renderer, &l);
}
void Spatial_Update(void)
{
    int i;
    if (!renderer) return;
    Spatial_PumpMusic();
    settings.hrtf = snd_hrtf.value != 0;
    SA_SetSettings(renderer, &settings);
    for (i = 0; i < total_channels; ++i) {
        channel_t *ch = &snd_channels[i];
        sa_source_t *c = &sources[i];
        if (!ch->sfx) { if (c->active) Spatial_Stop(i); continue; }
        if (bound[i] != ch->sfx) { S_LoadSound(ch->sfx); Spatial_Start(i, ch->sfx, ch->pos); }
        c->gain = ch->master_vol / 255.0f * sfxvolume.value * 0.5f;
        c->attenuation = ch->dist_mult;
        c->kind = (i < NUM_AMBIENTS || ch->entnum == cl.viewentity) ? SA_DRY : SA_POSITIONAL;
        VectorCopy(ch->origin, c->origin);
        if (vr_enabled.value && snd_spatial_weapons.value && ch->entnum == cl.viewentity && ch->entchannel != -1 &&
            (ch->entchannel == 1 || !Q_strncmp(ch->sfx->name, "weapons/", 8))) {
            c->kind = SA_POSITIONAL;
            /* One-shots retain their emission position; loops follow the weapon. */
            if (!c->position_valid || (c->sample && c->sample->loop >= 0)) VR_GetMuzzleAdjustedHandPos(c->origin);
            VectorCopy(c->origin, ch->origin);
            c->position_valid = 1;
        }
        SA_SetSource(renderer, i, c);
    }
}
void Spatial_Render(unsigned char *stream, int bytes)
{
    float out[SA_BLOCK * 2];
    int frames = bytes / 4, count, i;
    int16_t *dst = (int16_t *)stream;
    while (frames > 0) {
        count = frames < SA_BLOCK ? frames : SA_BLOCK;
        SA_Render(renderer, out, count);
        for (i = 0; i < count * 2; ++i) dst[i] = (int16_t)(out[i] * 32767.0f);
        dst += count * 2; frames -= count;
    }
}
void Spatial_VoiceSettings(float radio, float distance, int pure)
{
    settings.radio_gain = radio; settings.voice_distance = distance; settings.pure_voice = pure;
}
void Spatial_VoiceSource(int slot, float gain, qboolean enabled)
{
    sa_source_t c = {0};
    entity_t *ent;
    vrik_pose_t pose;
    if (!renderer || slot < 0 || slot >= MAX_SCOREBOARD) return;
    c.kind = SA_VOICE; c.generation = 1; c.active = enabled; c.gain = gain;
    if (slot + 1 < cl.num_entities && (ent = &cl.entities[slot + 1])->model && ent->msgtime == cl.mtime[0]) {
        c.position_valid = 1;
        VectorCopy(ent->origin, c.origin); c.origin[2] += 18;
        if (R_VRIKSampleEntityPose(ent, &pose)) {
            vec3_t f, right, up, mouth;
            float yaw = DEG2RAD(pose.body_yaw), cy = cosf(yaw), sy = sinf(yaw);
            AngleVectors(pose.orientation[VRIK_TRACKER_HEAD], f, right, up);
            VectorMA(pose.position[VRIK_TRACKER_HEAD], 2, f, mouth);
            VectorMA(mouth, -2, up, mouth);
            c.origin[0] = ent->origin[0] + cy * mouth[0] - sy * mouth[1];
            c.origin[1] = ent->origin[1] + sy * mouth[0] + cy * mouth[1];
            c.origin[2] = ent->origin[2] + mouth[2];
        }
    }
    SA_SetSource(renderer, MAX_CHANNELS + slot, &c);
}
void Spatial_VoicePCM(int slot, const int16_t *pcm, int frames) { if (renderer) SA_WriteVoice(renderer, slot, pcm, frames); }
void Spatial_ResetVoice(int slot) { if (renderer) SA_ResetStream(renderer, slot); }
int Spatial_MusicSpace(void) { return renderer ? SA_MusicSpace(renderer) : 0; }
void Spatial_RawSamples(int count, int rate, int width, int channels, byte *data, float volume)
{
    if (!renderer) return;
    if (!music_converter || music_rate != rate || music_width != width || music_channels != channels) {
        if (music_converter) SDL_FreeAudioStream(music_converter);
        music_converter = SDL_NewAudioStream(width == 1 ? AUDIO_U8 : AUDIO_S16SYS, channels, rate, AUDIO_F32SYS, 2, SA_RATE);
        music_rate = rate; music_width = width; music_channels = channels;
    }
    if (!music_converter) return;
    music_gain = volume * 0.5f;
    SDL_AudioStreamPut(music_converter, data, count * width * channels);
    Spatial_PumpMusic();
    s_rawend += (int)((int64_t)count * SA_RATE / rate);
}
static void Spatial_PumpMusic(void)
{
    float out[1024];
    int got, i, space;
    if (!renderer || !music_converter) return;
    while ((space = SA_MusicSpace(renderer)) > 0 && SDL_AudioStreamAvailable(music_converter) > 0) {
        if (space > 512) space = 512;
        got = SDL_AudioStreamGet(music_converter, out, space * 2 * sizeof(float));
        if (got <= 0) break;
        for (i = 0; i < got / (int)sizeof(float); ++i) out[i] *= music_gain;
        SA_WriteMusic(renderer, out, got / (2 * sizeof(float)));
    }
}
void Spatial_FinishMusic(void)
{
    if (!music_converter) return;
    SDL_AudioStreamFlush(music_converter);
    Spatial_PumpMusic();
}
