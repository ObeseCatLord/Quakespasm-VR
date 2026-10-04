/* GPL-2.0-or-later. Callback-owned direct renderer and room DSP integration. */
#include "snd_steamaudio.h"
#include "snd_room.h"
#include <phonon.h>
#ifdef USE_SDL3
#include <SDL3/SDL.h>
typedef SDL_AtomicInt sa_atomic_t;
#define sa_atomic_get SDL_GetAtomicInt
#define sa_atomic_set SDL_SetAtomicInt
#define sa_atomic_add SDL_AddAtomicInt
#define sa_spin_lock SDL_LockSpinlock
#define sa_spin_try_lock SDL_TryLockSpinlock
#define sa_spin_unlock SDL_UnlockSpinlock
#else
#include <SDL.h>
typedef SDL_atomic_t sa_atomic_t;
#define sa_atomic_get SDL_AtomicGet
#define sa_atomic_set SDL_AtomicSet
#define sa_atomic_add SDL_AtomicAdd
#define sa_spin_lock SDL_AtomicLock
#define sa_spin_try_lock SDL_AtomicTryLock
#define sa_spin_unlock SDL_AtomicUnlock
#endif
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float pcm[SA_STREAM_FRAMES * 2];
    sa_atomic_t read, write, dropped;
} sa_ring_t;
typedef struct {
    IPLBinauralEffect effect;
    unsigned generation;
    int position, ended, tail;
    unsigned block_generation;
    int block_position;
    const sa_sample_t *block_sample;
    float left, right, spatial;
    float radio_gain, radio_mix, obstruction, envelope;
    float radio_hp[2], radio_lp[2], blocked_lp[2];
    sa_atomic_t finished;
} sa_playback_t;
struct sa_renderer_s {
    IPLContext context;
    IPLHRTF hrtf;
    sa_room_t *room;
    int count, streams, remainder;
    SDL_SpinLock control_lock;
    sa_source_t *control, *snapshot;
    sa_progress_t *progress;
    sa_listener_t listener, render_listener;
    sa_settings_t settings, render_settings;
    sa_playback_t *playback;
    sa_ring_t *voice, music, self;
    float self_gain, render_self_gain, last_self_gain;
    float underwater_accum[2];
    sa_atomic_t clock;
    float mono[SA_BLOCK], left[SA_BLOCK], right[SA_BLOCK];
    float mixed[SA_BLOCK * 2], radio_pcm[SA_BLOCK];
    float room_send[SA_BLOCK], voice_send[SA_BLOCK];
    sa_stats_t stats;
};

#ifdef _MSC_VER
static __declspec(thread) sa_renderer_t *rendering;
#else
static __thread sa_renderer_t *rendering;
#endif
/* Instrument SDK allocations, including calls originating inside libphonon. */
static void *IPLCALL sa_allocate(IPLsize size, IPLsize alignment)
{
    void *base;
    uintptr_t aligned;
    if (rendering) ++rendering->stats.rt_allocations;
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    if (size > SIZE_MAX - alignment - sizeof(void *)) return NULL;
    base = malloc(size + alignment + sizeof(void *));
    if (!base) return NULL;
    aligned = ((uintptr_t)base + sizeof(void *) + alignment - 1) & ~(uintptr_t)(alignment - 1);
    ((void **)aligned)[-1] = base;
    return (void *)aligned;
}
static void IPLCALL sa_free(void *ptr)
{
    if (rendering) ++rendering->stats.rt_allocations;
    if (ptr) free(((void **)ptr)[-1]);
}

static float clamp01(float v) { return !isfinite(v) || v < 0 ? 0 : v > 1 ? 1 : v; }
/* The SDK requires a unit direction, even for a dry source/HRTF-off block.
 * A startup/invalidation pose can project onto a zero listener basis. */
static IPLVector3 unit_direction(IPLVector3 v)
{
    double length = sqrt((double)v.x * v.x + (double)v.y * v.y + (double)v.z * v.z);
    if (!isfinite(length) || length <= 0.000001) {
        v.x = v.y = 0; v.z = -1;
    } else {
        v.x = (float)(v.x / length);
        v.y = (float)(v.y / length);
        v.z = (float)(v.z / length);
    }
    return v;
}
static void reset_filters(sa_playback_t *p)
{
    p->radio_gain = p->radio_mix = p->obstruction = p->envelope = 0;
    memset(p->radio_hp, 0, sizeof(p->radio_hp));
    memset(p->radio_lp, 0, sizeof(p->radio_lp));
    memset(p->blocked_lp, 0, sizeof(p->blocked_lp));
}
/* Two non-resonant poles per edge, persistent across packet boundaries. */
static float radio_sample(sa_playback_t *p, float x, const sa_settings_t *settings)
{
    int j;
    float compression = clamp01(settings->radio_compression);
    float drive = isfinite(settings->radio_drive) ? fminf(4, fmaxf(0, settings->radio_drive)) : 0;
    for (j = 0; j < 2; ++j) {
        p->radio_hp[j] += 0.0385088f * (x - p->radio_hp[j]); /* 300 Hz */
        x -= p->radio_hp[j];
        p->radio_lp[j] += 0.359221f * (x - p->radio_lp[j]); /* 3400 Hz */
        x = p->radio_lp[j];
    }
    p->envelope += (fabsf(x) > p->envelope ? 0.002081f : 0.000139f) * (fabsf(x) - p->envelope);
    if (p->envelope > 0.12f)
        x *= 1 - compression + compression * sqrtf(0.12f / p->envelope);
    return x / (1 + drive * fabsf(x));
}
static int ring_count(sa_ring_t *q)
{
    return (sa_atomic_get(&q->write) - sa_atomic_get(&q->read) + SA_STREAM_FRAMES) % SA_STREAM_FRAMES;
}

sa_renderer_t *SA_Create(int sources, int streams)
{
    sa_renderer_t *r;
    IPLContextSettings context = {0};
    IPLAudioSettings audio = {SA_RATE, SA_BLOCK};
    IPLHRTFSettings hrtf = {0};
    IPLBinauralEffectSettings effect = {0};
    int i;
    if (sources < 1 || streams < 0 || streams > sources) return NULL;
    r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->count = sources; r->streams = streams;
    r->control = calloc(sources, sizeof(*r->control));
    r->snapshot = calloc(sources, sizeof(*r->snapshot));
    r->playback = calloc(sources, sizeof(*r->playback));
    r->progress = calloc(sources, sizeof(*r->progress));
    r->voice = calloc(streams ? streams : 1, sizeof(*r->voice));
    if (!r->control || !r->snapshot || !r->playback || !r->progress || !r->voice) goto fail;
    context.version = STEAMAUDIO_VERSION;
    context.allocateCallback = sa_allocate;
    context.freeCallback = sa_free;
    if (iplContextCreate(&context, &r->context) != IPL_STATUS_SUCCESS) goto fail;
    hrtf.type = IPL_HRTFTYPE_DEFAULT;
    hrtf.volume = 1.0f;
    if (iplHRTFCreate(r->context, &audio, &hrtf, &r->hrtf) != IPL_STATUS_SUCCESS) goto fail;
    effect.hrtf = r->hrtf;
    for (i = 0; i < sources; ++i)
        if (iplBinauralEffectCreate(r->context, &audio, &effect, &r->playback[i].effect) != IPL_STATUS_SUCCESS) goto fail;
    r->settings.hrtf = 1;
    r->settings.radio_gain = 0.45f;
    r->settings.voice_distance = 768;
    r->settings.radio_filter = r->settings.occlusion = 1;
    r->settings.underwater_alpha = 1.0f;
    r->render_settings.underwater_alpha = 1.0f;
    r->listener.forward[0] = 1;
    r->listener.right[1] = -1;
    r->listener.up[2] = 1;
    r->render_listener = r->listener;
    return r;
fail:
    SA_Destroy(r);
    return NULL;
}

void SA_Destroy(sa_renderer_t *r)
{
    int i;
    if (!r) return;
    if (r->playback)
        for (i = 0; i < r->count; ++i)
            if (r->playback[i].effect) iplBinauralEffectRelease(&r->playback[i].effect);
    SAR_Destroy(r->room);
    if (r->hrtf) iplHRTFRelease(&r->hrtf);
    if (r->context) iplContextRelease(&r->context);
    free(r->voice); free(r->progress); free(r->playback); free(r->snapshot); free(r->control); free(r);
}

void SA_ResetStream(sa_renderer_t *r, int stream)
{
    int index = r->count - r->streams + stream;
    if (stream < 0 || stream >= r->streams) return;
    SAR_Reset(r->room, 1);
    memset(&r->voice[stream], 0, sizeof(r->voice[stream]));
    iplBinauralEffectReset(r->playback[index].effect);
    r->playback[index].tail = 0;
    reset_filters(&r->playback[index]);
    r->playback[index].left = r->playback[index].right = 0;
    r->control[index].active = r->snapshot[index].active = 0;
    /* Discard already rendered mixed remainder on an explicit privacy/reset boundary. */
    r->remainder = 0;
}

/* At most one already rendered block remains; don't skip other sources' samples. */
void SA_ClearMusic(sa_renderer_t *r) { memset(&r->music, 0, sizeof(r->music)); }
void SA_Reset(sa_renderer_t *r)
{
    int i;
    SAR_Reset(r->room, 0);
    memset(&r->self, 0, sizeof(r->self));
    r->self_gain = r->render_self_gain = r->last_self_gain = 0;
    memset(r->control, 0, r->count * sizeof(*r->control));
    memset(r->snapshot, 0, r->count * sizeof(*r->snapshot));
    memset(r->progress, 0, r->count * sizeof(*r->progress));
    for (i = 0; i < r->count; ++i) {
        IPLBinauralEffect effect = r->playback[i].effect;
        iplBinauralEffectReset(effect);
        memset(&r->playback[i], 0, sizeof(r->playback[i]));
        r->playback[i].effect = effect;
    }
    memset(r->voice, 0, r->streams * sizeof(*r->voice));
    memset(r->underwater_accum, 0, sizeof(r->underwater_accum));
    SA_ClearMusic(r);
    r->remainder = 0;
    sa_atomic_set(&r->clock, 0);
}

void SA_ForgetSample(sa_renderer_t *r, const sa_sample_t *sample)
{
    int i;
    if (!r || !sample) return;
    /* The caller excludes SA_Render. Published control alone is insufficient:
     * an old snapshot and block-progress cursor can survive a missed publish. */
    sa_spin_lock(&r->control_lock);
    for (i = 0; i < r->count - r->streams; ++i) {
        sa_playback_t *p = &r->playback[i];
        if (r->control[i].sample == sample) {
            r->control[i].sample = NULL;
            r->control[i].active = 0;
            ++r->control[i].generation;
        }
        if (r->snapshot[i].sample == sample) {
            r->snapshot[i].sample = NULL;
            r->snapshot[i].active = 0;
            ++r->snapshot[i].generation;
        }
        if (p->block_sample == sample) {
            p->block_sample = NULL;
            p->block_generation = 0;
            r->progress[i].generation = 0;
            r->progress[i].position = 0;
        }
    }
    sa_spin_unlock(&r->control_lock);
}

void SA_SetSource(sa_renderer_t *r, int index, const sa_source_t *source)
{
    if (index < 0 || index >= r->count) return;
    sa_spin_lock(&r->control_lock);
    r->control[index] = *source;
    sa_spin_unlock(&r->control_lock);
}
void SA_SetListener(sa_renderer_t *r, const sa_listener_t *listener)
{
    double forward_length2 = 0, right_length2 = 0, up_length2 = 0;
    int i;
    /* Retain the initialized/last valid pose, including its timestamp. This
     * owner also supplies room simulation and the callback's decode basis. */
    for (i = 0; i < 3; ++i) {
        if (!isfinite(listener->origin[i]) || !isfinite(listener->forward[i]) ||
            !isfinite(listener->right[i]) || !isfinite(listener->up[i])) return;
        forward_length2 += (double)listener->forward[i] * listener->forward[i];
        right_length2 += (double)listener->right[i] * listener->right[i];
        up_length2 += (double)listener->up[i] * listener->up[i];
    }
    if (forward_length2 <= 1e-12 || right_length2 <= 1e-12 || up_length2 <= 1e-12) return;
    sa_spin_lock(&r->control_lock); r->listener = *listener; sa_spin_unlock(&r->control_lock);
}
void SA_SetSettings(sa_renderer_t *r, const sa_settings_t *settings)
{
    sa_listener_t listener;
    sa_spin_lock(&r->control_lock);
    r->settings = *settings;
    listener = r->listener;
    sa_spin_unlock(&r->control_lock);
    SAR_Update(r->room, &listener, settings);
}
sa_room_t *SA_CreateRoom(sa_renderer_t *r, sa_geometry_t *geometry)
{
    if (!r || !geometry) return NULL;
    return SAR_Create(r->context, r->hrtf, geometry);
}
sa_room_t *SA_ReplaceRoom(sa_renderer_t *r, sa_room_t *replacement)
{
    sa_room_t *detached;
    detached = r->room;
    r->room = replacement;
    return detached;
}
sa_room_t *SA_DetachRoom(sa_renderer_t *r) { return SA_ReplaceRoom(r, NULL); }
void SA_DestroyRoom(sa_room_t *room) { SAR_Destroy(room); }
void SA_RoomStats(sa_renderer_t *r, sa_room_stats_t *stats) { SAR_Stats(r->room, stats); }
void SA_SetSelf(sa_renderer_t *r, float gain)
{
    sa_spin_lock(&r->control_lock); r->self_gain = isfinite(gain) ? fminf(2, fmaxf(0, gain)) : 0; sa_spin_unlock(&r->control_lock);
}
void SA_ResetSelf(sa_renderer_t *r)
{
    memset(&r->self, 0, sizeof(r->self));
    r->self_gain = r->render_self_gain = r->last_self_gain = 0;
    /* A revoked monitor must not survive in the downstream water filter. */
    memset(r->underwater_accum, 0, sizeof(r->underwater_accum));
    r->remainder = 0; SAR_Reset(r->room, 0);
}
int SA_WriteSelf(sa_renderer_t *r, const int16_t *pcm, int frames)
{
    sa_ring_t *q = &r->self;
    int i, write = sa_atomic_get(&q->write);
    /* At most two 20 ms capture frames. Drop new stale work rather than let
     * local monitoring grow to the much larger network-voice ring capacity. */
    if (frames < 0 || frames > 1920 || ring_count(q) + frames > 1920) {
        if (frames > 0) sa_atomic_add(&q->dropped, frames);
        return 0;
    }
    for (i = 0; i < frames; ++i) {
        q->pcm[write] = pcm[i] / 32768.0f; write = (write + 1) % SA_STREAM_FRAMES;
    }
    sa_atomic_set(&q->write, write); return frames;
}
unsigned SA_Finished(sa_renderer_t *r, int index) { return (unsigned)sa_atomic_get(&r->playback[index].finished); }
void SA_GetProgress(sa_renderer_t *r, int index, sa_progress_t *progress)
{
    if (!progress) return;
    progress->generation = 0;
    progress->position = 0;
    if (index < 0 || index >= r->count) return;
    sa_spin_lock(&r->control_lock);
    *progress = r->progress[index];
    sa_spin_unlock(&r->control_lock);
}
int SA_Clock(sa_renderer_t *r) { return sa_atomic_get(&r->clock); }

int SA_WriteVoice(sa_renderer_t *r, int stream, const int16_t *pcm, int frames)
{
    sa_ring_t *q;
    int i, read, write;
    if (stream < 0 || stream >= r->streams) return 0;
    q = &r->voice[stream]; read = sa_atomic_get(&q->read); write = sa_atomic_get(&q->write);
    for (i = 0; i < frames && (write + 1) % SA_STREAM_FRAMES != read; ++i) {
        q->pcm[write] = pcm[i] / 32768.0f;
        write = (write + 1) % SA_STREAM_FRAMES;
    }
    sa_atomic_set(&q->write, write);
    sa_atomic_add(&q->dropped, frames - i);
    return i;
}
int SA_MusicSpace(sa_renderer_t *r) { return SA_STREAM_FRAMES - 1 - ring_count(&r->music); }
int SA_WriteMusic(sa_renderer_t *r, const float *stereo, int frames)
{
    sa_ring_t *q = &r->music;
    int i, read = sa_atomic_get(&q->read), write = sa_atomic_get(&q->write);
    for (i = 0; i < frames && (write + 1) % SA_STREAM_FRAMES != read; ++i) {
        q->pcm[2 * write] = stereo[2 * i]; q->pcm[2 * write + 1] = stereo[2 * i + 1];
        write = (write + 1) % SA_STREAM_FRAMES;
    }
    sa_atomic_set(&q->write, write);
    return i;
}

/* Commit a whole source only if gain, blend and addition stay finite. */
static int mix_source(sa_renderer_t *r, const sa_playback_t *p, float l, float rr, float spatial)
{
    float candidate[SA_BLOCK * 2];
    int i;
    for (i = 0; i < SA_BLOCK; ++i) {
        float t = fminf(1, (i + 1) / 64.0f);
        float h = p->spatial + (spatial - p->spatial) * t;
        float gl = p->left + (l - p->left) * t, gr = p->right + (rr - p->right) * t;
        candidate[2*i] = r->mixed[2*i] +
            (r->left[i] * h + r->mono[i] * (1 - h)) * gl + r->radio_pcm[i];
        candidate[2*i+1] = r->mixed[2*i+1] +
            (r->right[i] * h + r->mono[i] * (1 - h)) * gr + r->radio_pcm[i];
        if (!isfinite(candidate[2*i]) || !isfinite(candidate[2*i+1])) return 0;
    }
    memcpy(r->mixed, candidate, sizeof(candidate));
    return 1;
}

static void render_block(sa_renderer_t *r)
{
    int s, i, active = 0, binaural_nonfinite = 0;
    uint64_t start = SDL_GetPerformanceCounter();
    float *in_channels[] = {r->mono}, *out_channels[] = {r->left, r->right};
    IPLAudioBuffer in = {1, SA_BLOCK, in_channels}, out = {2, SA_BLOCK, out_channels};
    if (sa_spin_try_lock(&r->control_lock)) {
        memcpy(r->snapshot, r->control, r->count * sizeof(*r->snapshot));
        r->render_listener = r->listener; r->render_settings = r->settings;
        r->render_self_gain = r->self_gain;
        sa_spin_unlock(&r->control_lock);
    } else ++r->stats.snapshot_misses; /* Never wait for the game thread. */
    memset(r->mixed, 0, sizeof(r->mixed));
    memset(r->room_send, 0, sizeof(r->room_send));
    memset(r->voice_send, 0, sizeof(r->voice_send));
    for (s = 0; s < r->count; ++s) {
        sa_source_t *c = &r->snapshot[s];
        sa_playback_t *p = &r->playback[s];
        int stream = s - (r->count - r->streams), supplied = 0;
        float delta[3], distance = 0, pan, gain, radio = 0, spatial = 0;
        float l, rr;
        IPLBinauralEffectParams params = {0};
        p->block_sample = NULL;
        if (p->generation != c->generation) {
            iplBinauralEffectReset(p->effect);
            p->generation = c->generation; p->position = c->offset;
            p->ended = p->tail = 0; p->left = p->right = p->spatial = 0;
            reset_filters(p);
            sa_atomic_set(&p->finished, 0);
        }
        if (!c->active) {
            if (stream >= 0) sa_atomic_set(&r->voice[stream].read, sa_atomic_get(&r->voice[stream].write));
            if (p->tail) iplBinauralEffectReset(p->effect);
            p->tail = 0; p->left = p->right = 0;
            reset_filters(p);
            continue;
        }
        if (stream < 0 && c->sample) {
            p->block_generation = p->generation;
            p->block_position = p->position;
            p->block_sample = c->sample;
        }
        memset(r->mono, 0, sizeof(r->mono));
        if (stream >= 0) {
            sa_ring_t *q = &r->voice[stream];
            int read = sa_atomic_get(&q->read), write = sa_atomic_get(&q->write);
            for (i = 0; i < SA_BLOCK && read != write; ++i) {
                r->mono[i] = q->pcm[read]; read = (read + 1) % SA_STREAM_FRAMES;
            }
            supplied = i;
            sa_atomic_set(&q->read, read);
            if (i) r->stats.underrun_frames += SA_BLOCK - i;
        } else if (c->sample && !p->ended) {
            const sa_sample_t *sample = c->sample;
            for (i = 0; i < SA_BLOCK; ++i) {
                if (p->position >= sample->frames) {
                    if (sample->loop >= 0 && sample->loop < sample->frames) p->position = sample->loop;
                    else { p->ended = 1; break; }
                }
                r->mono[i] = sample->pcm[p->position++];
            }
            supplied = i;
        }
        if (!supplied && !p->tail) {
            if (stream < 0) sa_atomic_set(&p->finished, (int)p->generation);
            continue;
        }
        for (i = 0; i < 3; ++i) { delta[i] = c->origin[i] - r->render_listener.origin[i]; distance += delta[i] * delta[i]; }
        distance = sqrtf(distance);
        params.direction.x = params.direction.y = params.direction.z = 0;
        if (distance > 0.001f) for (i = 0; i < 3; ++i) {
            params.direction.x += delta[i] * r->render_listener.right[i] / distance;
            params.direction.y += delta[i] * r->render_listener.up[i] / distance;
            params.direction.z -= delta[i] * r->render_listener.forward[i] / distance;
        } else params.direction.z = -1;
        params.direction = unit_direction(params.direction);
        pan = params.direction.x;
        gain = c->gain;
        if (c->kind == SA_VOICE) {
            float blend = r->render_settings.pure_voice ? 0 : clamp01(distance / fmaxf(1, r->render_settings.voice_distance));
            if (!c->position_valid) blend = 1;
            radio = (r->render_settings.pure_voice ? 0 : blend * r->render_settings.radio_gain) * gain;
            gain *= (1 - blend) / (1 + distance / 512);
            spatial = c->position_valid ? 1.0f : 0.0f;
        } else if (c->kind == SA_POSITIONAL) {
            gain *= clamp01(1 - distance * c->attenuation);
            spatial = 1;
        }
        /* Virtualize inaudible sources without merging their positions or stopping
         * their playback clocks. Reset once so no old filter tail returns later. */
        if (gain == 0 && radio == 0 && p->left == 0 && p->right == 0) {
            if (p->tail) iplBinauralEffectReset(p->effect);
            p->tail = 0;
            continue;
        }
        for (i = 0; i < SA_BLOCK; ++i) {
            float raw = r->mono[i], filtered = raw;
            float target = spatial ? clamp01(c->obstruction) * clamp01(r->render_settings.occlusion) : 0;
            float alpha = c->kind == SA_VOICE ? 0.279095f : 0.178275f; /* 2.5/1.5 kHz */
            int j;
            if (c->kind == SA_VOICE)
                r->voice_send[i] += raw * gain * clamp01(r->render_settings.voice_reverb) * (1 - target * .3f);
            else
                r->room_send[i] += raw * gain * clamp01(c->room_send) * (1 - target * .3f);
            p->obstruction += 0.000278f * (target - p->obstruction); /* 75 ms */
            for (j = 0; j < 2; ++j) {
                p->blocked_lp[j] += alpha * (filtered - p->blocked_lp[j]);
                filtered = p->blocked_lp[j];
            }
            r->mono[i] = (raw + p->obstruction * (filtered - raw)) *
                (1 - p->obstruction * (c->kind == SA_VOICE ? 0.4f : 0.65f));
            r->radio_pcm[i] = 0;
            if (c->kind == SA_VOICE) {
                float colored = radio_sample(p, raw, &r->render_settings);
                p->radio_mix += 0.002081f * (clamp01(r->render_settings.radio_filter) - p->radio_mix);
                p->radio_gain += 0.002081f * (radio - p->radio_gain);
                r->radio_pcm[i] = (raw + p->radio_mix * (colored - raw)) * p->radio_gain;
            }
        }
        params.hrtf = r->hrtf;
        params.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
        params.spatialBlend = 1;
        /* Maintain filter history even in A/B panning mode. */
        if (supplied) p->tail = iplBinauralEffectApply(p->effect, &params, &in, &out) == IPL_AUDIOEFFECTSTATE_TAILREMAINING;
        else p->tail = iplBinauralEffectGetTail(p->effect, &out) == IPL_AUDIOEFFECTSTATE_TAILREMAINING;
        for (i = 0; i < SA_BLOCK; ++i)
            if (!isfinite(r->left[i]) || !isfinite(r->right[i])) break;
        l = gain; rr = gain;
        if (spatial && !r->render_settings.hrtf) {
            float separation = c->kind == SA_VOICE ? 0.5f : 1.0f;
            l *= 1 - separation * pan; rr *= 1 + separation * pan;
        }
        spatial *= r->render_settings.hrtf != 0;
        if (i < SA_BLOCK || !mix_source(r, p, l, rr, spatial)) {
            binaural_nonfinite = 1;
            iplBinauralEffectReset(p->effect); p->tail = 0;
            /* Replace the whole source block before blending: NaN * 0 is
             * still NaN when HRTF or spatial blend is disabled. The mono
             * side of that blend must also be finite on this fallback path. */
            for (i = 0; i < SA_BLOCK; ++i) {
                float dry = isfinite(r->mono[i]) ? r->mono[i] : 0;
                r->mono[i] = r->left[i] = r->right[i] = dry;
            }
            /* If upstream mono/controls also overflow, retain healthy mix. */
            (void)mix_source(r, p, l, rr, spatial);
        }
        p->left = l; p->right = rr; p->spatial = spatial;
        ++active;
    }
    {
        sa_ring_t *q = &r->self;
        int read = sa_atomic_get(&q->read), write = sa_atomic_get(&q->write);
        if (r->render_self_gain <= 0) {
            read = write;
        } else {
            for (i = 0; i < SA_BLOCK && read != write; ++i) {
                r->room_send[i] += q->pcm[read] * r->render_self_gain;
                read = (read + 1) % SA_STREAM_FRAMES;
            }
        }
        r->last_self_gain = r->render_self_gain;
        sa_atomic_set(&q->read, read);
    }
    /* World geometry and simulation are owned by the room worker. The audio
     * callback consumes only its latest bounded effect parameters. Music is
     * added afterward so it remains dry, matching the source mixer. */
    if (r->room && SAR_Render(r->room, r->room_send, r->voice_send, r->mixed,
            &r->render_listener, &r->render_settings))
        ++r->stats.room_nonfinite_blocks;
    {
        sa_ring_t *q = &r->music;
        int read = sa_atomic_get(&q->read), write = sa_atomic_get(&q->write);
        for (i = 0; i < SA_BLOCK && read != write; ++i) {
            r->mixed[2 * i] += q->pcm[2 * read]; r->mixed[2 * i + 1] += q->pcm[2 * read + 1];
            read = (read + 1) % SA_STREAM_FRAMES;
        }
        sa_atomic_set(&q->read, read);
    }
    for (i = 0; i < SA_BLOCK * 2; ++i) {
        float v = r->mixed[i];
        if (!isfinite(v)) { ++r->stats.nonfinite; v = 0; }
        if (fabsf(v) > r->stats.output_peak) r->stats.output_peak = fabsf(v);
        if (v > 1 || v < -1) { ++r->stats.clipped; v = fmaxf(-1, fminf(1, v)); }
        r->mixed[i] = v;
    }
    if (binaural_nonfinite) ++r->stats.binaural_nonfinite_blocks;
    r->stats.active = active; ++r->stats.blocks;
    {
        uint64_t ticks = SDL_GetPerformanceCounter() - start;
        r->stats.render_ticks += ticks;
        if (ticks > r->stats.max_render_ticks) r->stats.max_render_ticks = ticks;
        if (r->render_listener.timestamp && start >= r->render_listener.timestamp) {
            ticks = start - r->render_listener.timestamp;
            if (ticks > r->stats.max_pose_age_ticks) r->stats.max_pose_age_ticks = ticks;
        }
    }
}

static int advance_position(const sa_sample_t *sample, int position, int frames)
{
    if (!sample || position < 0) return 0;
    if (position >= sample->frames) {
        if (sample->loop < 0 || sample->loop >= sample->frames) return sample->frames;
        position = sample->loop;
    }
    while (frames > 0 && position < sample->frames) {
        int count = sample->frames - position;
        if (frames < count) return position + frames;
        frames -= count;
        if (sample->loop < 0 || sample->loop >= sample->frames) return sample->frames;
        position = sample->loop;
    }
    return position;
}

/* The callback never waits for the game thread; a missed publication is harmless. */
static void publish_progress(sa_renderer_t *r)
{
    int s, consumed = SA_BLOCK - r->remainder;
    if (!sa_spin_try_lock(&r->control_lock)) return;
    for (s = 0; s < r->count - r->streams; ++s) {
        sa_playback_t *p = &r->playback[s];
        if (!p->block_sample || !p->block_generation) continue;
        r->progress[s].generation = p->block_generation;
        r->progress[s].position = advance_position(p->block_sample, p->block_position, consumed);
    }
    sa_spin_unlock(&r->control_lock);
}

void SA_Render(sa_renderer_t *r, float *stereo, int frames)
{
    rendering = r;
    while (frames > 0) {
        int count, i;
        float alpha;
        if (!r->remainder) { render_block(r); r->remainder = SA_BLOCK; }
        count = frames < r->remainder ? frames : r->remainder;
        memcpy(stereo, r->mixed + 2 * (SA_BLOCK - r->remainder), count * 2 * sizeof(float));
        alpha = clamp01(r->render_settings.underwater_alpha);
        if (alpha >= 1.0f) {
            r->underwater_accum[0] = stereo[2 * (count - 1)];
            r->underwater_accum[1] = stereo[2 * (count - 1) + 1];
        } else {
            for (i = 0; i < count; ++i) {
                r->underwater_accum[0] += alpha * (stereo[2 * i] - r->underwater_accum[0]);
                r->underwater_accum[1] += alpha * (stereo[2 * i + 1] - r->underwater_accum[1]);
                stereo[2 * i] = r->underwater_accum[0];
                stereo[2 * i + 1] = r->underwater_accum[1];
            }
        }
        r->remainder -= count; frames -= count; stereo += 2 * count;
        sa_atomic_add(&r->clock, count);
    }
    publish_progress(r);
    rendering = NULL;
}
void SA_GetStats(sa_renderer_t *r, sa_stats_t *stats)
{
    int i;
    *stats = r->stats;
    stats->self_frames = ring_count(&r->self);
    stats->self_dropped = sa_atomic_get(&r->self.dropped);
    for (i = 0; i < r->streams; ++i) {
        stats->stream_frames += ring_count(&r->voice[i]);
        stats->dropped_frames += sa_atomic_get(&r->voice[i].dropped);
    }
}
void SA_ResetStats(sa_renderer_t *r) { memset(&r->stats, 0, sizeof(r->stats)); }
