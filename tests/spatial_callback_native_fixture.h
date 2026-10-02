#ifdef USE_STEAMAUDIO
#include "snd_spatial.h"
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void __real_SA_SetSource(sa_renderer_t *renderer, int channel, const sa_source_t *source);
static const int spatial_callback_channel[2] = {NUM_AMBIENTS, NUM_AMBIENTS + 1};
static sa_source_t spatial_callback_published[2];
static qboolean spatial_callback_seen[2];
static sa_renderer_t *spatial_callback_renderer;

void __wrap_SA_SetSource(sa_renderer_t *renderer, int channel, const sa_source_t *source)
{
	spatial_callback_renderer = renderer;
	for (int i = 0; i < 2; ++i)
		if (channel == spatial_callback_channel[i]) {
			spatial_callback_published[i] = *source;
			spatial_callback_seen[i] = true;
		}
	__real_SA_SetSource(renderer, channel, source);
}

static sfxcache_t *Spatial_CallbackCache(int loopstart)
{
	const int frames = 16384;
	const size_t bytes = offsetof(sfxcache_t, data) + (size_t)frames * sizeof(int16_t);
	sfxcache_t *cache = calloc(1, bytes);
	assert(cache);
	cache->length = frames;
	cache->loopstart = loopstart;
	cache->speed = SA_RATE;
	cache->width = 2;
	for (int i = 0; i < frames; ++i)
		((int16_t *)cache->data)[i] = (i & 63) < 32 ? 12000 : -12000;
	return cache;
}
static void Spatial_CallbackSetChannel(channel_t *channel, sfx_t *sfx, int pos)
{
	memset(channel, 0, sizeof(*channel));
	channel->sfx = sfx;
	channel->pos = pos;
	channel->end = sfx->cache->length;
	channel->looping = sfx->cache->loopstart;
	channel->entnum = cl.viewentity;
	channel->entchannel = 0;
	channel->master_vol = 255;
	channel->dist_mult = 0;
	VectorCopy(listener_origin, channel->origin);
}


static void Spatial_CallbackUpdate(void)
{
	memset(spatial_callback_seen, 0, sizeof(spatial_callback_seen));
	Spatial_Update();
}
static void Spatial_CallbackStable(int slot, const sa_sample_t *sample, unsigned generation,
	int offset, float gain, int active)
{
	const sa_source_t *source = &spatial_callback_published[slot];
	assert(spatial_callback_seen[slot] && source->active == active);
	assert(source->sample == sample && source->generation == generation);
	assert(source->offset == offset && fabsf(source->gain - gain) < 0.00001f);
}
static int Spatial_CallbackProgress(int channel, unsigned generation)
{
	sa_progress_t progress = {0};
	Spatial_GetProgress(channel, &progress);
	assert(progress.generation == generation);
	return progress.position;
}
static void Spatial_CallbackRender(int frames, double energy[2], double *signal)
{
	float output[SA_BLOCK * 2];
	assert(frames > 0 && frames <= SA_BLOCK);
	Spatial_Render(output, frames);
	for (int i = 0; i < frames; ++i) {
		assert(isfinite(output[2 * i]) && isfinite(output[2 * i + 1]));
		if (energy) {
			energy[0] += (double)output[2 * i] * output[2 * i];
			energy[1] += (double)output[2 * i + 1] * output[2 * i + 1];
		}
		if (signal)
			*signal += fabs(output[2 * i]) + fabs(output[2 * i + 1]);
	}
}

static void Spatial_CallbackDirection(sfx_t *sfx, int side, int hrtf)
{
	const int channel = spatial_callback_channel[0];
	double energy[2] = {0, 0};
	float old_hrtf = (float)Cvar_VariableValue("snd_hrtf");
	Spatial_Reset();
	Cvar_SetValue("snd_hrtf", (float)hrtf);
	snd_channels[spatial_callback_channel[1]].sfx = NULL;
	Spatial_CallbackSetChannel(&snd_channels[channel], sfx, 0);
	snd_channels[channel].entnum = cl.viewentity == 1 ? 2 : 1;
	for (int axis = 0; axis < 3; ++axis)
		snd_channels[channel].origin[axis] = listener_origin[axis] +
			side * 128.0f * listener_right[axis];
	total_channels = channel + 1;
	Spatial_CallbackUpdate();
	assert(spatial_callback_seen[0]);
	assert(spatial_callback_published[0].active && spatial_callback_published[0].kind == SA_POSITIONAL &&
		spatial_callback_published[0].position_valid);
	for (int block = 0; block < 8; ++block)
		Spatial_CallbackRender(SA_BLOCK, block >= 2 ? energy : NULL, NULL);
	assert(energy[0] + energy[1] > 0);
	if (hrtf) assert(energy[0] > 0 && energy[1] > 0);
	else assert(energy[side > 0 ? 0 : 1] == 0.0);
	assert(side > 0 ? energy[1] > energy[0] : energy[0] > energy[1]);
	Cvar_SetValue("snd_hrtf", old_hrtf);
}

static void Spatial_CallbackNativeChecks(void)
{
	const int first = spatial_callback_channel[0], second = spatial_callback_channel[1];
	channel_t saved_channels[2] = {snd_channels[first], snd_channels[second]};
	const int saved_total = total_channels;
	vec3_t saved_origin, saved_forward, saved_right, saved_up;
	const qboolean saved_paused = cl.paused, saved_server = sv.active;
	const int saved_maxclients = svs.maxclients;
	const keydest_t saved_key_dest = key_dest;
	float saved_pause, saved_hrtf, saved_occlusion;
	sfxcache_t *loop_cache, *shot_cache;
	sfx_t loop_sfx = {0}, shot_sfx = {0};
	const sa_sample_t *loop_sample, *shot_sample;
	unsigned loop_generation, shot_generation;
	float loop_gain, shot_gain;
	int loop_position, shot_position;
	double signal = 0;
	assert(COM_CheckParm("-nosound") && !Spatial_Active());
	assert(!snd_channels[first].sfx && !snd_channels[second].sfx);
	memcpy(saved_origin, listener_origin, sizeof(saved_origin));
	memcpy(saved_forward, listener_forward, sizeof(saved_forward));
	memcpy(saved_right, listener_right, sizeof(saved_right));
	memcpy(saved_up, listener_up, sizeof(saved_up));
	S_Init();
	assert(Spatial_Init() && Spatial_Active());
	saved_pause = (float)Cvar_VariableValue("snd_pauselooping");
	saved_hrtf = (float)Cvar_VariableValue("snd_hrtf");
	saved_occlusion = (float)Cvar_VariableValue("snd_spatial_occlusion");
	loop_cache = Spatial_CallbackCache(0);
	shot_cache = Spatial_CallbackCache(-1);
	loop_sfx.cache = loop_cache;
	shot_sfx.cache = shot_cache;
	strcpy(loop_sfx.name, "fixture_loop");
	strcpy(shot_sfx.name, "fixture_shot");
	VectorClear(listener_origin);
	listener_forward[0] = 1; listener_forward[1] = listener_forward[2] = 0;
	listener_right[0] = listener_right[2] = 0; listener_right[1] = -1;
	listener_up[0] = listener_up[1] = 0; listener_up[2] = 1;
	cl.paused = false;
	sv.active = false;
	svs.maxclients = 1;
	key_dest = key_game;
	Cvar_SetValue("snd_pauselooping", 1);
	Cvar_SetValue("snd_spatial_occlusion", 0);
	Spatial_CallbackSetChannel(&snd_channels[first], &loop_sfx, 7);
	Spatial_CallbackSetChannel(&snd_channels[second], &shot_sfx, 11);
	total_channels = second + 1;
	Spatial_CallbackUpdate();
	assert(spatial_callback_seen[0] && spatial_callback_seen[1]);
	loop_sample = spatial_callback_published[0].sample;
	shot_sample = spatial_callback_published[1].sample;
	loop_generation = spatial_callback_published[0].generation;
	shot_generation = spatial_callback_published[1].generation;
	loop_gain = spatial_callback_published[0].gain;
	shot_gain = spatial_callback_published[1].gain;
	assert(loop_sample && shot_sample && loop_sample->loop == 0 && shot_sample->loop == -1 &&
		loop_generation && shot_generation);
	assert(fabsf(loop_gain - shot_gain) < 0.00001f);
	assert(fabsf(spatial_callback_published[0].room_send - 0.35f) < 0.00001f &&
		fabsf(spatial_callback_published[1].room_send - 1.0f) < 0.00001f);
	loop_position = spatial_callback_published[0].offset;
	shot_position = spatial_callback_published[1].offset;
	assert(loop_position == 7 && shot_position == 11);
	Spatial_CallbackRender(17, NULL, &signal);
	assert(Spatial_CallbackProgress(first, loop_generation) == loop_position + 17);
	assert(Spatial_CallbackProgress(second, shot_generation) == shot_position + 17);
	Spatial_CallbackRender(19, NULL, &signal);
	assert(Spatial_CallbackProgress(first, loop_generation) == loop_position + 36);
	assert(Spatial_CallbackProgress(second, shot_generation) == shot_position + 36);
	Spatial_CallbackRender(SA_BLOCK - 36, NULL, &signal);
	loop_position += SA_BLOCK; shot_position += SA_BLOCK;
	assert(Spatial_CallbackProgress(first, loop_generation) == loop_position);
	assert(Spatial_CallbackProgress(second, shot_generation) == shot_position);
	assert(signal > 0);
	cl.paused = true;
	Spatial_CallbackUpdate();
	Spatial_CallbackStable(0, loop_sample, loop_generation, 7, loop_gain, 0);
	Spatial_CallbackStable(1, shot_sample, shot_generation, 11, shot_gain, 1);
	assert(spatial_callback_published[0].offset == 7 &&
		spatial_callback_published[1].offset == 11);
	Spatial_CallbackRender(SA_BLOCK, NULL, NULL);
	assert(Spatial_CallbackProgress(first, loop_generation) == loop_position);
	shot_position += SA_BLOCK;
	assert(Spatial_CallbackProgress(second, shot_generation) == shot_position);
	cl.paused = false;
	Spatial_CallbackUpdate();
	Spatial_CallbackRender(SA_BLOCK, NULL, NULL);
	loop_position += SA_BLOCK; shot_position += SA_BLOCK;
	assert(Spatial_CallbackProgress(first, loop_generation) == loop_position &&
		Spatial_CallbackProgress(second, shot_generation) == shot_position);
	cl.paused = true;
	Cvar_SetValue("snd_pauselooping", 0);
	Spatial_CallbackUpdate();
	assert(spatial_callback_published[0].active);
	Spatial_CallbackRender(SA_BLOCK, NULL, NULL);
	loop_position += SA_BLOCK; shot_position += SA_BLOCK;
	assert(Spatial_CallbackProgress(first, loop_generation) == loop_position &&
		Spatial_CallbackProgress(second, shot_generation) == shot_position);
	Cvar_SetValue("snd_pauselooping", 1);
	cl.paused = false;
	sv.active = true;
	svs.maxclients = 1;
	key_dest = key_menu;
	Spatial_CallbackUpdate();
	assert(!spatial_callback_published[0].active && spatial_callback_published[1].active);
	Spatial_CallbackRender(SA_BLOCK, NULL, NULL);
	assert(Spatial_CallbackProgress(first, loop_generation) == loop_position);
	shot_position += SA_BLOCK;
	assert(Spatial_CallbackProgress(second, shot_generation) == shot_position);
	key_dest = key_game;
	Spatial_CallbackUpdate();
	Spatial_CallbackRender(SA_BLOCK, NULL, NULL);
	loop_position += SA_BLOCK; shot_position += SA_BLOCK;
	assert(Spatial_CallbackProgress(first, loop_generation) == loop_position &&
		Spatial_CallbackProgress(second, shot_generation) == shot_position);
	svs.maxclients = 2;
	key_dest = key_menu;
	Spatial_CallbackUpdate();
	assert(spatial_callback_published[0].active);
	Spatial_CallbackRender(SA_BLOCK, NULL, NULL);
	loop_position += SA_BLOCK; shot_position += SA_BLOCK;
	assert(Spatial_CallbackProgress(first, loop_generation) == loop_position &&
		Spatial_CallbackProgress(second, shot_generation) == shot_position);
	/* Remote solo client menu also leaves loops running. */
	sv.active = false;
	svs.maxclients = 1;
	Spatial_CallbackUpdate();
	assert(spatial_callback_published[0].active);
	Spatial_CallbackRender(SA_BLOCK, NULL, NULL);
	loop_position += SA_BLOCK; shot_position += SA_BLOCK;
	assert(Spatial_CallbackProgress(first, loop_generation) == loop_position &&
		Spatial_CallbackProgress(second, shot_generation) == shot_position);
	Spatial_CallbackDirection(&shot_sfx, 1, 1);
	Spatial_CallbackDirection(&shot_sfx, -1, 1);
	Spatial_CallbackDirection(&shot_sfx, 1, 0);
	Spatial_CallbackDirection(&shot_sfx, -1, 0);
	Cvar_SetValue("snd_pauselooping", saved_pause);
	Cvar_SetValue("snd_hrtf", saved_hrtf);
	Cvar_SetValue("snd_spatial_occlusion", saved_occlusion);
	sa_stats_t stats = {0};
	assert(spatial_callback_renderer);
	SA_GetStats(spatial_callback_renderer, &stats);
	assert(!stats.nonfinite && !stats.rt_allocations);
	Spatial_Shutdown();
	spatial_callback_renderer = NULL;
	snd_channels[first] = saved_channels[0];
	snd_channels[second] = saved_channels[1];
	total_channels = saved_total;
	memcpy(listener_origin, saved_origin, sizeof(saved_origin));
	memcpy(listener_forward, saved_forward, sizeof(saved_forward));
	memcpy(listener_right, saved_right, sizeof(saved_right));
	memcpy(listener_up, saved_up, sizeof(saved_up));
	cl.paused = saved_paused;
	key_dest = saved_key_dest;
	sv.active = saved_server;
	svs.maxclients = saved_maxclients;
	free(loop_cache);
	free(shot_cache);
}
#endif
