/* GPL-2.0-or-later. Only this file depends on the Steam Audio SDK. */
#include "snd_steamaudio.h"
#include <phonon.h>
#include <SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float pcm[SA_STREAM_FRAMES * 2];
    SDL_atomic_t read, write, dropped;
} sa_ring_t;
typedef struct {
    IPLBinauralEffect effect;
    unsigned generation;
    int position, ended, tail;
    float left, right, spatial;
    SDL_atomic_t finished;
} sa_playback_t;
struct sa_renderer_s {
    IPLContext context;
    IPLHRTF hrtf;
    int count, streams, remainder;
    SDL_SpinLock control_lock;
    sa_source_t *control, *snapshot;
    sa_listener_t listener, render_listener;
    sa_settings_t settings, render_settings;
    sa_playback_t *playback;
    sa_ring_t *voice, music;
    SDL_atomic_t clock;
    float mono[SA_BLOCK], left[SA_BLOCK], right[SA_BLOCK];
    float mixed[SA_BLOCK * 2];
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

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static int ring_count(sa_ring_t *q)
{
    return (SDL_AtomicGet(&q->write) - SDL_AtomicGet(&q->read) + SA_STREAM_FRAMES) % SA_STREAM_FRAMES;
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
    r->voice = calloc(streams ? streams : 1, sizeof(*r->voice));
    if (!r->control || !r->snapshot || !r->playback || !r->voice) goto fail;
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
    r->listener.forward[0] = 1;
    r->listener.right[1] = -1;
    r->listener.up[2] = 1;
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
    if (r->hrtf) iplHRTFRelease(&r->hrtf);
    if (r->context) iplContextRelease(&r->context);
    free(r->voice); free(r->playback); free(r->snapshot); free(r->control); free(r);
}

void SA_ResetStream(sa_renderer_t *r, int stream)
{
    int index = r->count - r->streams + stream;
    if (stream < 0 || stream >= r->streams) return;
    memset(&r->voice[stream], 0, sizeof(r->voice[stream]));
    iplBinauralEffectReset(r->playback[index].effect);
    r->playback[index].tail = 0;
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
    memset(r->control, 0, r->count * sizeof(*r->control));
    memset(r->snapshot, 0, r->count * sizeof(*r->snapshot));
    for (i = 0; i < r->count; ++i) {
        IPLBinauralEffect effect = r->playback[i].effect;
        iplBinauralEffectReset(effect);
        memset(&r->playback[i], 0, sizeof(r->playback[i]));
        r->playback[i].effect = effect;
    }
    memset(r->voice, 0, r->streams * sizeof(*r->voice));
    SA_ClearMusic(r);
    r->remainder = 0;
    SDL_AtomicSet(&r->clock, 0);
}

void SA_SetSource(sa_renderer_t *r, int index, const sa_source_t *source)
{
    if (index < 0 || index >= r->count) return;
    SDL_AtomicLock(&r->control_lock);
    r->control[index] = *source;
    SDL_AtomicUnlock(&r->control_lock);
}
void SA_SetListener(sa_renderer_t *r, const sa_listener_t *listener)
{
    SDL_AtomicLock(&r->control_lock); r->listener = *listener; SDL_AtomicUnlock(&r->control_lock);
}
void SA_SetSettings(sa_renderer_t *r, const sa_settings_t *settings)
{
    SDL_AtomicLock(&r->control_lock); r->settings = *settings; SDL_AtomicUnlock(&r->control_lock);
}
unsigned SA_Finished(sa_renderer_t *r, int index) { return (unsigned)SDL_AtomicGet(&r->playback[index].finished); }
int SA_Clock(sa_renderer_t *r) { return SDL_AtomicGet(&r->clock); }

int SA_WriteVoice(sa_renderer_t *r, int stream, const int16_t *pcm, int frames)
{
    sa_ring_t *q;
    int i, read, write;
    if (stream < 0 || stream >= r->streams) return 0;
    q = &r->voice[stream]; read = SDL_AtomicGet(&q->read); write = SDL_AtomicGet(&q->write);
    for (i = 0; i < frames && (write + 1) % SA_STREAM_FRAMES != read; ++i) {
        q->pcm[write] = pcm[i] / 32768.0f;
        write = (write + 1) % SA_STREAM_FRAMES;
    }
    SDL_AtomicSet(&q->write, write);
    SDL_AtomicAdd(&q->dropped, frames - i);
    return i;
}
int SA_MusicSpace(sa_renderer_t *r) { return SA_STREAM_FRAMES - 1 - ring_count(&r->music); }
int SA_WriteMusic(sa_renderer_t *r, const float *stereo, int frames)
{
    sa_ring_t *q = &r->music;
    int i, read = SDL_AtomicGet(&q->read), write = SDL_AtomicGet(&q->write);
    for (i = 0; i < frames && (write + 1) % SA_STREAM_FRAMES != read; ++i) {
        q->pcm[2 * write] = stereo[2 * i]; q->pcm[2 * write + 1] = stereo[2 * i + 1];
        write = (write + 1) % SA_STREAM_FRAMES;
    }
    SDL_AtomicSet(&q->write, write);
    return i;
}

static void render_block(sa_renderer_t *r)
{
    int s, i, active = 0;
    uint64_t start = SDL_GetPerformanceCounter();
    float *in_channels[] = {r->mono}, *out_channels[] = {r->left, r->right};
    IPLAudioBuffer in = {1, SA_BLOCK, in_channels}, out = {2, SA_BLOCK, out_channels};
    if (SDL_AtomicTryLock(&r->control_lock)) {
        memcpy(r->snapshot, r->control, r->count * sizeof(*r->snapshot));
        r->render_listener = r->listener; r->render_settings = r->settings;
        SDL_AtomicUnlock(&r->control_lock);
    } else ++r->stats.snapshot_misses; /* Never wait for the game thread. */
    memset(r->mixed, 0, sizeof(r->mixed));
    for (s = 0; s < r->count; ++s) {
        sa_source_t *c = &r->snapshot[s];
        sa_playback_t *p = &r->playback[s];
        int stream = s - (r->count - r->streams), supplied = 0;
        float delta[3], distance = 0, pan, gain, radio = 0, spatial = 0;
        float l, rr;
        IPLBinauralEffectParams params = {0};
        if (p->generation != c->generation) {
            iplBinauralEffectReset(p->effect);
            p->generation = c->generation; p->position = c->offset;
            p->ended = p->tail = 0; p->left = p->right = p->spatial = 0;
            SDL_AtomicSet(&p->finished, 0);
        }
        if (!c->active) {
            if (stream >= 0) SDL_AtomicSet(&r->voice[stream].read, SDL_AtomicGet(&r->voice[stream].write));
            if (p->tail) iplBinauralEffectReset(p->effect);
            p->tail = 0; p->left = p->right = 0;
            continue;
        }
        memset(r->mono, 0, sizeof(r->mono));
        if (stream >= 0) {
            sa_ring_t *q = &r->voice[stream];
            int read = SDL_AtomicGet(&q->read), write = SDL_AtomicGet(&q->write);
            for (i = 0; i < SA_BLOCK && read != write; ++i) {
                r->mono[i] = q->pcm[read]; read = (read + 1) % SA_STREAM_FRAMES;
            }
            supplied = i;
            SDL_AtomicSet(&q->read, read);
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
            if (stream < 0) SDL_AtomicSet(&p->finished, (int)p->generation);
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
        pan = params.direction.x;
        gain = c->gain;
        if (c->kind == SA_VOICE) {
            float blend = r->render_settings.pure_voice ? 0 : clamp01(distance / fmaxf(1, r->render_settings.voice_distance));
            if (!c->position_valid) blend = 1;
            radio = (r->render_settings.pure_voice ? 0 : blend * r->render_settings.radio_gain) * gain;
            gain *= (1 - blend) / (1 + distance / 512);
            spatial = c->position_valid != 0;
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
        params.hrtf = r->hrtf;
        params.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
        params.spatialBlend = 1;
        /* Maintain filter history even in A/B panning mode. */
        if (supplied) p->tail = iplBinauralEffectApply(p->effect, &params, &in, &out) == IPL_AUDIOEFFECTSTATE_TAILREMAINING;
        else p->tail = iplBinauralEffectGetTail(p->effect, &out) == IPL_AUDIOEFFECTSTATE_TAILREMAINING;
        l = gain; rr = gain;
        if (spatial && !r->render_settings.hrtf) {
            float separation = c->kind == SA_VOICE ? 0.5f : 1.0f;
            l *= 1 - separation * pan; rr *= 1 + separation * pan;
        }
        spatial *= r->render_settings.hrtf != 0;
        for (i = 0; i < SA_BLOCK; ++i) {
            float t = fminf(1, (i + 1) / 64.0f);
            float h = p->spatial + (spatial - p->spatial) * t;
            float gl = p->left + (l - p->left) * t, gr = p->right + (rr - p->right) * t;
            r->mixed[2 * i] += (r->left[i] * h + r->mono[i] * (1 - h)) * gl + r->mono[i] * radio;
            r->mixed[2 * i + 1] += (r->right[i] * h + r->mono[i] * (1 - h)) * gr + r->mono[i] * radio;
        }
        p->left = l; p->right = rr; p->spatial = spatial;
        ++active;
    }
    {
        sa_ring_t *q = &r->music;
        int read = SDL_AtomicGet(&q->read), write = SDL_AtomicGet(&q->write);
        for (i = 0; i < SA_BLOCK && read != write; ++i) {
            r->mixed[2 * i] += q->pcm[2 * read]; r->mixed[2 * i + 1] += q->pcm[2 * read + 1];
            read = (read + 1) % SA_STREAM_FRAMES;
        }
        SDL_AtomicSet(&q->read, read);
    }
    for (i = 0; i < SA_BLOCK * 2; ++i) {
        float v = r->mixed[i];
        if (!isfinite(v)) { ++r->stats.nonfinite; v = 0; }
        if (fabsf(v) > r->stats.output_peak) r->stats.output_peak = fabsf(v);
        if (v > 1 || v < -1) { ++r->stats.clipped; v = fmaxf(-1, fminf(1, v)); }
        r->mixed[i] = v;
    }
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

void SA_Render(sa_renderer_t *r, float *stereo, int frames)
{
    rendering = r;
    while (frames > 0) {
        int count;
        if (!r->remainder) { render_block(r); r->remainder = SA_BLOCK; }
        count = frames < r->remainder ? frames : r->remainder;
        memcpy(stereo, r->mixed + 2 * (SA_BLOCK - r->remainder), count * 2 * sizeof(float));
        r->remainder -= count; frames -= count; stereo += 2 * count;
        SDL_AtomicAdd(&r->clock, count);
    }
    rendering = NULL;
}
void SA_GetStats(sa_renderer_t *r, sa_stats_t *stats)
{
    int i;
    *stats = r->stats;
    for (i = 0; i < r->streams; ++i) {
        stats->stream_frames += ring_count(&r->voice[i]);
        stats->dropped_frames += SDL_AtomicGet(&r->voice[i].dropped);
    }
}
void SA_ResetStats(sa_renderer_t *r) { memset(&r->stats, 0, sizeof(r->stats)); }
