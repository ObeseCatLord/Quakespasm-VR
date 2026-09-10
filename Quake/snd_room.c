/* GPL-2.0-or-later. The worker owns scene/simulation, audio owns effects.
 * Geometry is a heap copy; no Quake state crosses this boundary. */
#include "snd_room.h"
#include <SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ROOM_DURATION 1.5f
#define ROOM_SAMPLES 72000
#define ROOM_CHANNELS 4
struct sa_room_s {
    IPLContext context;
    IPLHRTF hrtf;
    IPLScene scene;
    IPLStaticMesh mesh;
    IPLSimulator simulator;
    IPLSource source;
    IPLReflectionEffect hybrid, parametric, voice_effect;
    IPLAmbisonicsDecodeEffect decode;
    SDL_Thread *thread;
    SDL_atomic_t quit;
    SDL_SpinLock lock;
    sa_geometry_t geometry;
    sa_listener_t listener;
    sa_settings_t settings;
    sa_room_stats_t stats;
    IPLReflectionEffectParams published, current;
    float amb[ROOM_CHANNELS][SA_BLOCK], left[SA_BLOCK], right[SA_BLOCK], voice[SA_BLOCK];
    float wet_gain, rt60[3];
    int ready, mode, sfx_tail, voice_tail;
    uint64_t consumed_run;
};
static float bounded(float x, float lo, float hi)
{
    return isfinite(x) ? fminf(hi, fmaxf(lo, x)) : lo;
}
static IPLVector3 direction(const float *p)
{
    IPLVector3 v = {-p[1], p[2], -p[0]}; return v;
}
static void free_geometry(sa_geometry_t *g)
{
    free(g->vertices); free(g->triangles); free(g->materials);
    memset(g, 0, sizeof(*g));
}
static int simulate(void *ptr)
{
    sa_room_t *r = ptr;
    IPLSceneSettings scene = {0};
    IPLStaticMeshSettings mesh = {0};
    IPLSimulationSettings sim = {0};
    IPLSourceSettings src = {0};
    /* Absorption, scattering, transmission. Deliberately damped hard surfaces. */
    IPLMaterial materials[] = {
        {{.12f,.18f,.28f}, .35f, {.02f,.01f,.005f}},
        {{.10f,.12f,.18f}, .20f, {.02f,.01f,.005f}},
        {{.16f,.12f,.10f}, .20f, {.02f,.01f,.005f}},
        {{.20f,.24f,.30f}, .45f, {.02f,.01f,.005f}}
    };
    uint64_t start = SDL_GetPerformanceCounter();
    scene.type = IPL_SCENETYPE_DEFAULT;
    if (iplSceneCreate(r->context, &scene, &r->scene) != IPL_STATUS_SUCCESS) goto fail;
    mesh.numVertices = r->geometry.num_vertices;
    mesh.numTriangles = r->geometry.num_triangles;
    mesh.numMaterials = 4;
    mesh.vertices = (IPLVector3 *)r->geometry.vertices;
    mesh.triangles = (IPLTriangle *)r->geometry.triangles;
    mesh.materialIndices = r->geometry.materials;
    mesh.materials = materials;
    if (iplStaticMeshCreate(r->scene, &mesh, &r->mesh) != IPL_STATUS_SUCCESS) goto fail;
    iplStaticMeshAdd(r->mesh, r->scene); iplSceneCommit(r->scene);
    free_geometry(&r->geometry);
    if (SDL_AtomicGet(&r->quit)) return 0;
    sim.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
    sim.sceneType = IPL_SCENETYPE_DEFAULT;
    sim.reflectionType = IPL_REFLECTIONEFFECTTYPE_HYBRID;
    sim.maxNumRays = 4096; sim.numDiffuseSamples = 64;
    sim.maxDuration = ROOM_DURATION; sim.maxOrder = 1; sim.maxNumSources = 1;
    sim.numThreads = 1; sim.rayBatchSize = 1;
    sim.samplingRate = SA_RATE; sim.frameSize = SA_BLOCK;
    if (iplSimulatorCreate(r->context, &sim, &r->simulator) != IPL_STATUS_SUCCESS) goto fail;
    iplSimulatorSetScene(r->simulator, r->scene);
    src.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
    if (iplSourceCreate(r->simulator, &src, &r->source) != IPL_STATUS_SUCCESS) goto fail;
    iplSourceAdd(r->source, r->simulator); iplSimulatorCommit(r->simulator);
    SDL_AtomicLock(&r->lock);
    r->stats.build_ticks = SDL_GetPerformanceCounter() - start;
    SDL_AtomicUnlock(&r->lock);
    while (!SDL_AtomicGet(&r->quit)) {
        sa_listener_t listener;
        sa_settings_t settings;
        IPLSimulationInputs input = {0};
        IPLSimulationSharedInputs shared = {0};
        IPLSimulationOutputs output = {0};
        IPLVector3 pos;
        uint64_t elapsed;
        int i;
        SDL_AtomicLock(&r->lock);
        listener = r->listener; settings = r->settings;
        SDL_AtomicUnlock(&r->lock);
        if (!listener.timestamp || settings.reverb <= 0 || settings.room_mode == 0) {
            SDL_Delay(10); continue;
        }
        pos = direction(listener.origin);
        pos.x *= SA_METERS_PER_UNIT; pos.y *= SA_METERS_PER_UNIT; pos.z *= SA_METERS_PER_UNIT;
        shared.listener.origin = pos;
        shared.listener.right.x = 1; shared.listener.up.y = 1; shared.listener.ahead.z = -1;
        shared.numRays = (int)bounded(settings.room_rays, 256, 4096);
        shared.numBounces = (int)bounded(settings.room_bounces, 2, 32);
        shared.duration = ROOM_DURATION; shared.order = 1;
        shared.irradianceMinDistance = 0.5f;
        input.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
        input.source = shared.listener;
        input.reverbScale[0] = input.reverbScale[1] = input.reverbScale[2] = 1;
        input.hybridReverbTransitionTime = 0.15f;
        input.hybridReverbOverlapPercent = 0.25f;
        iplSourceSetInputs(r->source, IPL_SIMULATIONFLAGS_REFLECTIONS, &input);
        iplSimulatorSetSharedInputs(r->simulator, IPL_SIMULATIONFLAGS_REFLECTIONS, &shared);
        start = SDL_GetPerformanceCounter();
        iplSimulatorRunReflections(r->simulator);
        iplSourceGetOutputs(r->source, IPL_SIMULATIONFLAGS_REFLECTIONS, &output);
        elapsed = SDL_GetPerformanceCounter() - start;
        output.reflections.type = IPL_REFLECTIONEFFECTTYPE_HYBRID;
        output.reflections.numChannels = ROOM_CHANNELS; output.reflections.irSize = ROOM_SAMPLES;
        for (i = 0; i < 3; ++i) {
            output.reflections.reverbTimes[i] = bounded(output.reflections.reverbTimes[i], .1f, 3);
            output.reflections.eq[i] = bounded(output.reflections.eq[i], 0, 4);
        }
        output.reflections.delay = (int)bounded(output.reflections.delay, 0, ROOM_SAMPLES);
        SDL_AtomicLock(&r->lock);
        r->published = output.reflections;
        r->stats.runs++; r->stats.ready = 1;
        r->stats.last_ticks = elapsed;
        if (elapsed > r->stats.max_ticks) r->stats.max_ticks = elapsed;
        r->stats.result_timestamp = SDL_GetPerformanceCounter();
        memcpy(r->stats.rt60, output.reflections.reverbTimes, sizeof(r->stats.rt60));
        SDL_AtomicUnlock(&r->lock);
        /* 10 Hz maximum; slow runs coalesce input, never grow a work queue. */
        for (i = 0; i < 10 && !SDL_AtomicGet(&r->quit); ++i) SDL_Delay(10);
    }
    return 0;
fail:
    SDL_AtomicLock(&r->lock); r->stats.failed = 1; SDL_AtomicUnlock(&r->lock);
    free_geometry(&r->geometry);
    return 1;
}
sa_room_t *SAR_Create(IPLContext context, IPLHRTF hrtf, sa_geometry_t *geometry)
{
    sa_room_t *r = calloc(1, sizeof(*r));
    IPLAudioSettings audio = {SA_RATE, SA_BLOCK};
    IPLReflectionEffectSettings effect = {IPL_REFLECTIONEFFECTTYPE_HYBRID, ROOM_SAMPLES, ROOM_CHANNELS};
    IPLAmbisonicsDecodeEffectSettings decode = {0};
    if (!r) { free_geometry(geometry); return NULL; }
    r->context = context; r->hrtf = hrtf; r->geometry = *geometry;
    memset(geometry, 0, sizeof(*geometry));
    r->stats.triangles = r->geometry.num_triangles;
    r->stats.geometry_bytes = (size_t)r->geometry.num_vertices * 3 * sizeof(float) +
        (size_t)r->geometry.num_triangles * 4 * sizeof(int);
    if (iplReflectionEffectCreate(context, &audio, &effect, &r->hybrid) != IPL_STATUS_SUCCESS) goto fail;
    effect.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
    if (iplReflectionEffectCreate(context, &audio, &effect, &r->parametric) != IPL_STATUS_SUCCESS) goto fail;
    if (iplReflectionEffectCreate(context, &audio, &effect, &r->voice_effect) != IPL_STATUS_SUCCESS) goto fail;
    decode.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;
    decode.hrtf = hrtf; decode.maxOrder = 1;
    if (iplAmbisonicsDecodeEffectCreate(context, &audio, &decode, &r->decode) != IPL_STATUS_SUCCESS) goto fail;
    r->thread = SDL_CreateThread(simulate, "room-acoustics", r);
    if (!r->thread) goto fail;
    return r;
fail:
    SAR_Destroy(r); return NULL;
}
void SAR_Destroy(sa_room_t *r)
{
    if (!r) return;
    SDL_AtomicSet(&r->quit, 1);
    if (r->thread) SDL_WaitThread(r->thread, NULL);
    if (r->source) iplSourceRelease(&r->source);
    if (r->simulator) iplSimulatorRelease(&r->simulator);
    if (r->mesh) iplStaticMeshRelease(&r->mesh);
    if (r->scene) iplSceneRelease(&r->scene);
    if (r->decode) iplAmbisonicsDecodeEffectRelease(&r->decode);
    if (r->hybrid) iplReflectionEffectRelease(&r->hybrid);
    if (r->parametric) iplReflectionEffectRelease(&r->parametric);
    if (r->voice_effect) iplReflectionEffectRelease(&r->voice_effect);
    free_geometry(&r->geometry); free(r);
}
void SAR_Update(sa_room_t *r, const sa_listener_t *listener, const sa_settings_t *settings)
{
    if (!r) return;
    SDL_AtomicLock(&r->lock); r->listener = *listener; r->settings = *settings; SDL_AtomicUnlock(&r->lock);
}
void SAR_Reset(sa_room_t *r, int voice_only)
{
    if (!r) return;
    iplReflectionEffectReset(r->voice_effect); r->voice_tail = 0;
    if (voice_only) return;
    iplReflectionEffectReset(r->hybrid); iplReflectionEffectReset(r->parametric);
    iplAmbisonicsDecodeEffectReset(r->decode); r->sfx_tail = 0; r->wet_gain = 0;
}
void SAR_Stats(sa_room_t *r, sa_room_stats_t *stats)
{
    memset(stats, 0, sizeof(*stats));
    if (!r) return;
    SDL_AtomicLock(&r->lock); *stats = r->stats; SDL_AtomicUnlock(&r->lock);
}
void SAR_Render(sa_room_t *r, const float *sfx, const float *voice, float *stereo,
    const sa_listener_t *listener, const sa_settings_t *settings)
{
    float *amb[] = {r->amb[0], r->amb[1], r->amb[2], r->amb[3]};
    float *lr[] = {r->left, r->right}, *v[] = {r->voice};
    IPLAudioBuffer input = {1, SA_BLOCK, NULL}, output = {4, SA_BLOCK, amb};
    IPLAudioBuffer decoded = {2, SA_BLOCK, lr}, voice_out = {1, SA_BLOCK, v};
    IPLReflectionEffectParams params;
    float silence[SA_BLOCK] = {0};
    float *input_channel;
    IPLAmbisonicsDecodeEffectParams decode = {0};
    int i, sfx_active = 0, voice_active = 0, new_result = 0, mode = settings->room_mode;
    if (SDL_AtomicTryLock(&r->lock)) {
        r->current = r->published; r->ready = r->stats.ready;
        new_result = r->consumed_run != r->stats.runs;
        r->consumed_run = r->stats.runs;
        SDL_AtomicUnlock(&r->lock);
    }
    if (!r->ready) return;
    if (mode != r->mode) { SAR_Reset(r, 0); r->mode = mode; }
    if (mode == 0 || settings->reverb <= 0) { SAR_Reset(r, 0); return; }
    for (i = 0; i < SA_BLOCK; ++i) {
        if (sfx[i] != 0) sfx_active = 1;
        if (voice[i] != 0) voice_active = 1;
    }
    params = r->current;
    for (i = 0; i < 3; ++i) {
        if (!r->rt60[i]) r->rt60[i] = params.reverbTimes[i];
        r->rt60[i] += .02f * (params.reverbTimes[i] - r->rt60[i]);
        params.reverbTimes[i] = r->rt60[i];
    }
    memset(r->amb, 0, sizeof(r->amb));
    memset(r->voice, 0, sizeof(r->voice));
    /* Consume each IR update even in silence/parametric mode. Otherwise the
     * single-producer SDK handoff would retain an arbitrarily old room. */
    input_channel = (float *)sfx; input.data = &input_channel;
    if (new_result && mode == 1) {
        input_channel = silence;
        iplReflectionEffectApply(r->hybrid, &params, &input, &output, NULL);
        memset(r->amb, 0, sizeof(r->amb));
        input_channel = (float *)sfx;
    }
    if (sfx_active || r->sfx_tail || new_result) {
        IPLReflectionEffect effect = mode == 1 ? r->parametric : r->hybrid;
        params.type = mode == 1 ? IPL_REFLECTIONEFFECTTYPE_PARAMETRIC : IPL_REFLECTIONEFFECTTYPE_HYBRID;
        r->sfx_tail = ((sfx_active || new_result) ? iplReflectionEffectApply(effect, &params, &input, &output, NULL) :
            iplReflectionEffectGetTail(effect, &output, NULL)) == IPL_AUDIOEFFECTSTATE_TAILREMAINING;
    }
    decode.order = 1; decode.hrtf = r->hrtf; decode.binaural = settings->hrtf ? IPL_TRUE : IPL_FALSE;
    decode.orientation.right = direction(listener->right);
    decode.orientation.up = direction(listener->up);
    decode.orientation.ahead = direction(listener->forward);
    iplAmbisonicsDecodeEffectApply(r->decode, &decode, &output, &decoded);
    input_channel = (float *)voice;
    params.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
    for (i = 0; i < 3; ++i) params.reverbTimes[i] = fminf(1.2f, r->rt60[i] * .65f);
    if (voice_active || r->voice_tail)
        r->voice_tail = (voice_active ? iplReflectionEffectApply(r->voice_effect, &params, &input, &voice_out, NULL) :
            iplReflectionEffectGetTail(r->voice_effect, &voice_out, NULL)) == IPL_AUDIOEFFECTSTATE_TAILREMAINING;
    for (i = 0; i < SA_BLOCK; ++i) {
        r->wet_gain += .000417f * (bounded(settings->reverb, 0, 1) - r->wet_gain);
        stereo[2*i] += r->wet_gain * (r->left[i] + r->voice[i]);
        stereo[2*i+1] += r->wet_gain * (r->right[i] + r->voice[i]);
    }
}
