/* Real Steam Audio renderer tests; no Quake assets or audio device required. */
#include "snd_steamaudio.h"
#include "voice_jitter.h"
#include <opus.h>
#include <SDL.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float tone[4096], output[8192], reference[8192];
static sa_sample_t sample = {tone, 4096, 0};
static sa_listener_t listener = {{0,0,0}, {1,0,0}, {0,-1,0}, {0,0,1}, 0};
static sa_source_t source = {0};
static double energy(const float *pcm, int frames, int channel)
{
    int i; double e = 0;
    for (i = 0; i < frames; ++i) { assert(isfinite(pcm[2*i+channel])); e += pcm[2*i+channel] * pcm[2*i+channel]; }
    return e;
}
static void setup(sa_renderer_t *r)
{
    SA_Reset(r);
    memset(&source, 0, sizeof(source));
    source.sample = &sample; source.active = 1; source.generation = 1;
    source.kind = SA_POSITIONAL; source.origin[1] = -64; source.gain = 0.3f;
    SA_SetListener(r, &listener); SA_SetSource(r, 0, &source);
}

static int stress_thread(void *ptr)
{
    sa_renderer_t *r = ptr;
    float out[1024];
    int i;
    for (i = 0; i < 2000; ++i) {
        SA_Render(r, out, 1 + (i * 37) % 512);
        if (i % 20 == 0) SDL_Delay(1);
    }
    return 0;
}

int main(int argc, char **argv)
{
    sa_renderer_t *r;
    sa_stats_t stats;
    sa_settings_t settings = {1, 0, 0.45f, 768};
    int16_t voice[960];
    int i, j;
    double right, left;
    SDL_Thread *thread;
    assert(SDL_Init(SDL_INIT_TIMER) == 0);
    for (i = 0; i < 4096; ++i) tone[i] = 0.4f * sinf(i * 0.073f) + 0.2f * sinf(i * 0.337f);
    for (i = 0; i < 960; ++i) voice[i] = (int16_t)(10000 * sinf(i * 0.11f));
    r = SA_Create(5, 1); assert(r);
    setup(r); SA_Render(r, reference, 4096);
    left = energy(reference + 512, 3840, 0); right = energy(reference + 512, 3840, 1);
    assert(right > left * 1.1); /* Quake -Y is the listener's right. */

    /* Arbitrary device callback partitions must preserve the sample sequence. */
    setup(r);
    for (i = 0; i < 4096; i += j) {
        j = 1 + (i * 31) % 511; if (j > 4096 - i) j = 4096 - i;
        SA_Render(r, output + 2*i, j);
    }
    for (i = 0; i < 8192; ++i) assert(fabsf(output[i] - reference[i]) < 0.000001f);
    assert(SA_Clock(r) == 4096);

    /* Turning the head changes a playing source without replacing/requeuing it. */
    {
        sa_listener_t turned = listener;
        turned.forward[0] = -1; turned.right[1] = 1;
        SA_SetListener(r, &turned); SA_Render(r, output, 4096);
        assert(energy(output + 512, 3840, 0) > energy(output + 512, 3840, 1) * 1.1);
    }

    /* Shared geometry translation is acoustically invariant. */
    setup(r);
    {
        sa_listener_t shifted = listener;
        shifted.origin[0] = source.origin[0] = 200;
        shifted.origin[2] = source.origin[2] = -100;
        SA_SetSource(r, 0, &source); SA_SetListener(r, &shifted);
        SA_Render(r, output, 4096);
        for (i = 0; i < 8192; ++i) assert(fabsf(output[i] - reference[i]) < 0.000001f);
    }

    /* One-shot completion includes the filter tail, replacement starts anew. */
    setup(r);
    {
        sa_sample_t short_sample = {tone, 100, -1};
        source.sample = &short_sample; SA_SetSource(r, 0, &source);
        SA_Render(r, output, 4096); assert(SA_Finished(r, 0) == 1);
        source.generation = 2; source.sample = &sample; SA_SetSource(r, 0, &source);
        SA_Render(r, output, 1024); assert(energy(output, 1024, 1) > 0.1);
        assert(SA_Finished(r, 0) != 2);
        source.active = 0; SA_SetSource(r, 0, &source);
        SA_Render(r, output, 512); assert(energy(output, 512, 0) == 0);
    }

    /* Voice stays mono in the queue: the pose set AFTER enqueue controls it. */
    SA_Reset(r); source.sample = NULL; source.kind = SA_VOICE;
    source.active = source.position_valid = 1; source.generation = 1;
    source.gain = 0.5f; source.origin[0] = source.origin[2] = 0; source.origin[1] = -64;
    settings.pure_voice = 1; SA_SetSettings(r, &settings);
    SA_SetSource(r, 4, &source); assert(SA_WriteVoice(r, 0, voice, 960) == 960);
    SA_SetListener(r, &listener); SA_Render(r, reference, 2048);
    SA_ResetStream(r, 0);
    {
        sa_listener_t turned = listener;
        turned.forward[0] = -1; turned.right[1] = 1;
        SA_SetSource(r, 4, &source); SA_WriteVoice(r, 0, voice, 960);
        SA_SetListener(r, &turned); SA_Render(r, output, 2048);
        assert(energy(reference, 1024, 1) > energy(reference, 1024, 0));
        assert(energy(output, 1024, 0) > energy(output, 1024, 1));
    }
    SA_Render(r, output, 4096); assert(energy(output, 4096, 0) < 1e-10);
    SA_WriteVoice(r, 0, voice, 960); SA_Render(r, output, 1024);
    assert(energy(output, 1024, 0) > 0.1); /* recovery after starvation */
    SA_ResetStream(r, 0); SA_Render(r, output, 1024);
    assert(energy(output, 1024, 0) == 0);

    /* The production jitter buffer and real Opus decode feed the same mono
     * boundary, including one missing packet followed by decoder recovery. */
    {
        voice_jitter_t jitter;
        voice_jitter_frame_t frame;
        voice_jitter_packet_t packet = {0};
        unsigned char encoded[3][400];
        int lengths[3], error;
        int16_t decoded[960];
        OpusEncoder *encoder = opus_encoder_create(SA_RATE, 1, OPUS_APPLICATION_VOIP, &error);
        OpusDecoder *decoder = opus_decoder_create(SA_RATE, 1, &error);
        assert(encoder && decoder);
        opus_encoder_ctl(encoder, OPUS_SET_BITRATE(24000));
        Voice_JitterInit(&jitter, 4);
        for (i = 0; i < 3; ++i) {
            lengths[i] = opus_encode(encoder, voice, 960, encoded[i], 400); assert(lengths[i] > 0);
            if (i == 1) continue;
            packet.sequence = (uint16_t)i; packet.timestamp = i * 960;
            packet.talkspurt = 1; packet.payload = encoded[i]; packet.payload_size = lengths[i];
            assert(Voice_JitterInsert(&jitter, &packet, i * 20) == VOICE_JITTER_OK);
        }
        SA_ResetStream(r, 0); SA_SetSource(r, 4, &source);
        for (i = 0; i < 3; ++i) {
            assert(Voice_JitterNextFrame(&jitter, 60 + i * 20, &frame) == VOICE_JITTER_OK);
            assert(frame.action == (i == 1 ? VOICE_JITTER_PLC : VOICE_JITTER_PACKET));
            assert(opus_decode(decoder, i == 1 ? NULL : frame.payload,
                i == 1 ? 0 : (opus_int32)frame.payload_size, decoded, 960, 0) == 960);
            SA_WriteVoice(r, 0, decoded, 960); SA_Render(r, output, 960);
            assert(energy(output, 960, 0) > 0.001);
        }
        Voice_JitterEndTalkspurt(&jitter);
        SA_Render(r, output, 4096); SA_Render(r, output, 4096);
        assert(energy(output, 4096, 0) < 1e-10);
        opus_decoder_destroy(decoder); opus_encoder_destroy(encoder);
    }

    /* Missing position is centered radio normally, silent in pure mode. */
    SA_ResetStream(r, 0);
    source.position_valid = 0; settings.pure_voice = 0;
    SA_SetSettings(r, &settings); SA_SetSource(r, 4, &source);
    SA_WriteVoice(r, 0, voice, 960); SA_Render(r, output, 1024);
    for (i = 0; i < 1024; ++i) assert(fabsf(output[2*i] - output[2*i+1]) < 1e-6);
    assert(energy(output, 1024, 0) > 0.1);
    SA_ResetStream(r, 0); settings.pure_voice = 1;
    SA_SetSettings(r, &settings); SA_SetSource(r, 4, &source);
    SA_WriteVoice(r, 0, voice, 960); SA_Render(r, output, 1024);
    assert(energy(output, 1024, 0) == 0);

    /* Music ring wrap and reset, without spatial processing. */
    SA_Reset(r);
    for (i = 0; i < 8192; ++i) reference[i] = (i & 1) ? -0.125f : 0.25f;
    for (j = 0; j < 4; ++j) {
        assert(SA_WriteMusic(r, reference, 4096) == 4096);
        SA_Render(r, output, 4096);
        assert(memcmp(reference, output, sizeof(output)) == 0);
    }
    SA_ClearMusic(r); SA_Render(r, output, 1024); assert(energy(output, 1024, 0) == 0);

    /* One producer and callback run concurrently through ring wrap/slot reuse. */
    setup(r); thread = SDL_CreateThread(stress_thread, "spatial-fixture", r); assert(thread);
    for (i = 0; i < 1000; ++i) {
        sa_listener_t moving = listener;
        moving.origin[0] = (float)(i % 30);
        SA_SetListener(r, &moving);
        source.generation++; source.active = (i % 7 != 0); SA_SetSource(r, 0, &source);
        SA_WriteVoice(r, 0, voice, 960);
        if (i % 10 == 0) SDL_Delay(1);
    }
    SDL_WaitThread(thread, NULL);
    SA_GetStats(r, &stats);
    assert(stats.nonfinite == 0); assert(stats.rt_allocations == 0);
    printf("PASS: HRTF direction, late pose, block partitioning, translation, loops, completion, replacement, Opus/jitter/PLC, voice recovery/radio/reset, music wrap, concurrent publication; SDK realtime allocations = %llu\n", (unsigned long long)stats.rt_allocations);
    SA_Destroy(r);

    /* Optional representative source-count benchmark (includes real HRIR filters). */
    if (argc > 1 && !strcmp(argv[1], "--benchmark")) {
        const int counts[] = {8, 32, 128, 512};
        for (j = 0; j < 4; ++j) {
            uint64_t start, elapsed;
            r = SA_Create(counts[j], 0); assert(r); setup(r);
            for (i = 0; i < counts[j]; ++i) {
                source.origin[0] = 100 * sinf((float)i); source.origin[1] = 100 * cosf((float)i);
                source.gain = 0.02f; SA_SetSource(r, i, &source);
            }
            SA_Render(r, output, 4096); start = SDL_GetPerformanceCounter();
            for (i = 0; i < 40; ++i) SA_Render(r, output, 4096);
            elapsed = SDL_GetPerformanceCounter() - start;
            SA_GetStats(r, &stats);
            printf("%d sources: %.3f ms/block average; %.3f ms worst (block budget %.3f ms)\n", counts[j], elapsed * 1000.0 / SDL_GetPerformanceFrequency() / (40 * 16), stats.max_render_ticks * 1000.0 / SDL_GetPerformanceFrequency(), 1000.0 * SA_BLOCK / SA_RATE);
            assert(stats.rt_allocations == 0); SA_Destroy(r);
        }
    }
    SDL_Quit(); return 0;
}
