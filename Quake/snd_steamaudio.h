/* Callback-owned spatial renderer. GPL-2.0-or-later; Steam Audio is Apache-2.0. */
#ifndef SND_STEAMAUDIO_H
#define SND_STEAMAUDIO_H
#include <stdint.h>
#include <stddef.h>
#define SA_RATE 48000
#define SA_BLOCK 256
#define SA_STREAM_FRAMES 8192
typedef struct sa_renderer_s sa_renderer_t;
typedef struct {
    const float *pcm; /* immutable until reset with callback excluded */
    int frames, loop;
} sa_sample_t;
enum { SA_DRY, SA_POSITIONAL, SA_VOICE };
typedef struct {
    const sa_sample_t *sample;
    unsigned generation;
    int active, kind, offset, position_valid;
    float origin[3], gain, attenuation;
    float obstruction; /* 0 clear, 1 blocked; game-thread trace */
} sa_source_t;
typedef struct {
    float origin[3], forward[3], right[3], up[3];
    uint64_t timestamp;
} sa_listener_t;
typedef struct {
    int hrtf, pure_voice;
    float radio_gain, voice_distance;
    float radio_filter, radio_compression, radio_drive, occlusion;
} sa_settings_t;
/* Callback-published cursor for one non-stream source. */
typedef struct {
    unsigned generation;
    int position;
} sa_progress_t;
typedef struct {
    uint64_t blocks, clipped, nonfinite, snapshot_misses, underrun_frames, rt_allocations;
    uint64_t render_ticks, max_render_ticks, max_pose_age_ticks;
    float output_peak;
    int active, stream_frames, dropped_frames;
} sa_stats_t;
sa_renderer_t *SA_Create(int sources, int streams);
void SA_Destroy(sa_renderer_t *r); /* callback excluded */
void SA_Reset(sa_renderer_t *r); /* callback excluded; retains allocations */
void SA_ResetStream(sa_renderer_t *r, int stream); /* callback excluded */
void SA_SetSource(sa_renderer_t *r, int index, const sa_source_t *source);
void SA_SetListener(sa_renderer_t *r, const sa_listener_t *listener);
void SA_SetSettings(sa_renderer_t *r, const sa_settings_t *settings);
unsigned SA_Finished(sa_renderer_t *r, int index);
void SA_GetProgress(sa_renderer_t *r, int index, sa_progress_t *progress);
int SA_Clock(sa_renderer_t *r);
int SA_WriteVoice(sa_renderer_t *r, int stream, const int16_t *pcm, int frames);
/* Stereo music queue: main producer, callback consumer, no spatial processing. */
int SA_MusicSpace(sa_renderer_t *r);
int SA_WriteMusic(sa_renderer_t *r, const float *stereo, int frames);
void SA_ClearMusic(sa_renderer_t *r); /* callback excluded */
void SA_Render(sa_renderer_t *r, float *stereo, int frames);
void SA_GetStats(sa_renderer_t *r, sa_stats_t *stats); /* callback excluded */
void SA_ResetStats(sa_renderer_t *r); /* callback excluded */
#endif
