#ifdef USE_STEAMAUDIO
#include "../Quake/snd_spatial.h"
#include "../Quake/snd_spatial_world.h"
#include "../Quake/voice_settings.h"
#include <assert.h>
#ifdef __linux__
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ROOM_MONITOR_QUEUE_FRAMES 1920
#define ROOM_MONITOR_TASK_LIMIT 64

static void Room_MonitorOffline(void)
{
	assert(cls.state == ca_disconnected && !cls.netcon);
	assert(!cl.voice_outgoing_count && !voice_sending && !Voice_IsTransmitting());
}

static void Room_MonitorPCM(int frames)
{
	int16_t pcm[VOICE_FRAME_SAMPLES];
	for (int f = 0; f < frames; ++f) {
		for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
			pcm[i] = ((i / 48 + f) & 1) ? 12000 : -12000;
		Voice_EncodeCaptureFrame(pcm);
	}
	Room_MonitorOffline();
}

static uint64_t room_monitor_pcm_signal;
static int room_monitor_last_pcm_abs;

static double Room_MonitorDraw(int frames)
{
	float output[SA_BLOCK * 2];
	double signal = 0.0;
	assert(frames > 0 && frames <= SA_BLOCK);
	Spatial_Render(output, frames);
	room_monitor_pcm_signal = 0;
	room_monitor_last_pcm_abs = 0;
	for (int i = 0; i < frames * 2; ++i) {
		assert(isfinite(output[i]));
		signal += fabs((double)output[i]);
		/* Match the native SDL3 S16 output boundary, not a float epsilon. */
		int16_t pcm = (int16_t)(CLAMP(-1.0f, output[i], 1.0f) * 32767.0f);
		room_monitor_pcm_signal += (unsigned int)abs((int)pcm);
		if (i >= frames * 2 - 2) room_monitor_last_pcm_abs += abs((int)pcm);
	}
	return signal;
}

static void Room_MonitorStats(sa_renderer_t *renderer, sa_stats_t *stats)
{
	assert(renderer && stats);
	SA_GetStats(renderer, stats);
	assert(!stats->nonfinite && !stats->rt_allocations);
}

static int Room_MonitorTimedOut(Uint32 start)
{
	return (Uint32)((Uint32)SDL_GetTicks() - start) >= 5000U;
}

static double Room_MonitorDrain(sa_renderer_t *renderer, int silent)
{
	const Uint32 start = (Uint32)SDL_GetTicks();
	sa_stats_t stats = {0};
	double signal = 0.0;
	int empty_blocks = 0;
	do {
		signal += Room_MonitorDraw(SA_BLOCK);
		Room_MonitorStats(renderer, &stats);
		empty_blocks = stats.self_frames ? 0 : empty_blocks + 1;
		if (empty_blocks >= 8) break;
		SDL_Delay(10);
	} while (!Room_MonitorTimedOut(start));
	Room_MonitorStats(renderer, &stats);
	assert(!stats.self_frames);
	if (silent) assert(signal == 0.0);
	else assert(signal > 0.0);
	return signal;
}

static void Room_MonitorPartial(sa_renderer_t *renderer)
{
	Spatial_ResetSelf();
	Spatial_SelfGain(1.0f);
	Room_MonitorPCM(2);
	const unsigned int clock = (unsigned int)SA_Clock(renderer);
	const Uint32 start = (Uint32)SDL_GetTicks();
	sa_stats_t stats = {0};
	do {
		double signal = Room_MonitorDraw(SA_BLOCK / 2);
		Room_MonitorStats(renderer, &stats);
		if (((unsigned int)SA_Clock(renderer) - clock) % SA_BLOCK == SA_BLOCK / 2 &&
			signal > 0.0 && room_monitor_pcm_signal > 0 && room_monitor_last_pcm_abs >= 8 && stats.self_frames > 0)
			return;
		SDL_Delay(10);
	} while (!Room_MonitorTimedOut(start));
	assert(0 && "nonzero first half and queued self PCM not observed");
}

static void Room_MonitorWaitReady(void)
{
	const Uint32 start = (Uint32)SDL_GetTicks();
	sa_room_stats_t room = {0};
	do {
		Spatial_RoomStats(&room);
		assert(!room.failed);
		if (room.ready && room.runs) break;
		SDL_Delay(10);
	} while (!Room_MonitorTimedOut(start));
	if (!room.ready) fprintf(stderr, "room readiness: ready=%d failed=%d runs=%llu triangles=%d bytes=%zu\n", room.ready, room.failed, (unsigned long long)room.runs, room.triangles, room.geometry_bytes);
	assert(room.ready && !room.failed && room.runs && room.triangles > 0 && room.geometry_bytes > 0);
	for (int i = 0; i < 3; ++i)
		assert(isfinite(room.rt60[i]) && room.rt60[i] > 0.0f);
}

#ifdef __linux__
static int Room_MonitorTasks(pid_t ids[ROOM_MONITOR_TASK_LIMIT])
{
	DIR *directory = opendir("/proc/self/task");
	struct dirent *entry;
	int count = 0;
	assert(directory);
	while ((entry = readdir(directory))) {
		char path[128], name[64], *end;
		long id;
		FILE *file;
		if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
		errno = 0;
		id = strtol(entry->d_name, &end, 10);
		if (errno || *end || id <= 0) continue;
		snprintf(path, sizeof(path), "/proc/self/task/%ld/comm", id);
		file = fopen(path, "r");
		if (!file) continue;
		if (!fgets(name, sizeof(name), file)) {
			fclose(file);
			continue;
		}
		fclose(file);
		name[strcspn(name, "\r\n")] = '\0';
		if (strcmp(name, "room-acoustics")) continue;
		assert(count < ROOM_MONITOR_TASK_LIMIT);
		ids[count++] = (pid_t)id;
	}
	closedir(directory);
	return count;
}

static int Room_MonitorHasTask(const pid_t *ids, int count, pid_t id)
{
	for (int i = 0; i < count; ++i)
		if (ids[i] == id) return 1;
	return 0;
}

static void Room_MonitorWaitRetired(const pid_t *retired, int retired_count)
{
	const Uint32 start = (Uint32)SDL_GetTicks();
	pid_t current[ROOM_MONITOR_TASK_LIMIT];
	do {
		int count = Room_MonitorTasks(current), found = 0;
		for (int i = 0; i < retired_count; ++i)
			found |= Room_MonitorHasTask(current, count, retired[i]);
		if (!found) return;
		SDL_Delay(10);
	} while (!Room_MonitorTimedOut(start));
	assert(0 && "owned room-acoustics task did not retire");
}

static void Room_MonitorWaitTaskSet(const pid_t *expected, int expected_count)
{
	const Uint32 start = (Uint32)SDL_GetTicks();
	pid_t current[ROOM_MONITOR_TASK_LIMIT];
	do {
		int count = Room_MonitorTasks(current), same = count == expected_count;
		for (int i = 0; same && i < count; ++i)
			same = Room_MonitorHasTask(expected, expected_count, current[i]);
		if (same) return;
		SDL_Delay(10);
	} while (!Room_MonitorTimedOut(start));
	assert(0 && "room-acoustics task set did not return to baseline");
}
#endif

void Room_MonitorNativeChecks(void)
{
	static const char *room_cvar_names[] = {
		"snd_spatial_room_mode", "snd_spatial_room_rays",
		"snd_spatial_room_bounces", "snd_spatial_reverb"
	};
	char saved_room_cvars[4][32], saved_input_gain[32];
	cvar_t *room_cvars[4];
	float saved_listener_origin[3], saved_listener_forward[3];
	float saved_listener_right[3], saved_listener_up[3];
	client_state_t saved_cl;
	client_static_t saved_cls;
	voice_settings_t saved_voice_settings;
	qboolean saved_profile_vr, saved_capture_ready;
	qboolean saved_paused, saved_server;
	qmodel_t *saved_worldmodel;
	float saved_voice_input_gain;
	size_t i;
#ifdef __linux__
	pid_t baseline[ROOM_MONITOR_TASK_LIMIT], active[ROOM_MONITOR_TASK_LIMIT];
	pid_t retired[ROOM_MONITOR_TASK_LIMIT];
	int baseline_count, active_count;
#endif
	assert(COM_CheckParm("-nosound") && COM_CheckParm("-dedicated"));
	assert(SDL_GetCurrentAudioDriver() && !strcmp(SDL_GetCurrentAudioDriver(), "dummy"));
	assert(shm && shm->speed == SA_RATE && shm->channels == 2 && shm->samplebits == 16);
	assert(!V_TrackedSessionActive() && !voice_profile_vr);
	assert(voice_initialized && !voice_sending && !cl.voice_outgoing_count);
	assert(VOICE_FRAME_SAMPLES == 960);
	assert(!Spatial_Active() && !spatial_callback_renderer);
	assert(total_channels == 0);
	for (i = 0; i < MAX_CHANNELS; ++i) assert(!snd_channels[i].sfx);
	if (!Cvar_FindVar("snd_hrtf")) S_Init();
	assert(Cvar_FindVar("snd_hrtf"));
	for (i = 0; i < 4; ++i) {
		room_cvars[i] = Cvar_FindVar(room_cvar_names[i]);
		assert(room_cvars[i] && room_cvars[i]->string);
		snprintf(saved_room_cvars[i], sizeof(saved_room_cvars[i]), "%s",
			room_cvars[i]->string);
	}
	snprintf(saved_input_gain, sizeof(saved_input_gain), "%s", voice_input_gain.string);
	saved_voice_input_gain = voice_input_gain.value;
	if (saved_voice_input_gain <= 0.0f) Cvar_SetValue("voice_input_gain", 1.0f);
	assert(voice_input_gain.value > 0.0f);
	saved_cl = cl;
	saved_cls = cls;
	saved_worldmodel = cl.worldmodel;
	saved_profile_vr = voice_profile_vr;
	saved_voice_settings = voice_settings;
	saved_capture_ready = Voice_CaptureReady();
	saved_paused = cl.paused;
	saved_server = sv.active;
	memcpy(saved_listener_origin, listener_origin, sizeof(saved_listener_origin));
	memcpy(saved_listener_forward, listener_forward, sizeof(saved_listener_forward));
	memcpy(saved_listener_right, listener_right, sizeof(saved_listener_right));
	memcpy(saved_listener_up, listener_up, sizeof(saved_listener_up));
	for (i = 0; i < 3; ++i) {
		assert(isfinite(listener_origin[i]) && isfinite(listener_forward[i]) &&
			isfinite(listener_right[i]) && isfinite(listener_up[i]));
	}
	assert(Spatial_Init() && Spatial_Active());
	Cvar_SetValue("snd_spatial_room_mode", 2);
	Cvar_SetValue("snd_spatial_room_rays", 256);
	Cvar_SetValue("snd_spatial_room_bounces", 2);
	Cvar_SetValue("snd_spatial_reverb", 0.5f);
	Spatial_Update();
	sa_renderer_t *renderer = spatial_callback_renderer;
	sa_stats_t stats = {0};
	sa_room_stats_t room = {0};
	assert(renderer);
	Room_MonitorStats(renderer, &stats);
	assert(!stats.active && !stats.stream_frames && !stats.self_frames);
	Spatial_RoomStats(&room);
	assert(!room.ready && !room.failed && !room.triangles);
	cls.state = ca_disconnected;
	cls.netcon = NULL;
	cls.signon = 0;
	cls.demoplayback = false;
	cl.paused = false;
	sv.active = false;
	Voice_SetTransmitEnabled(false);
	Voice_SetSelfReverb(true);
	assert(voice_settings.desktop.self_reverb && !voice_settings.desktop.transmit);
	Voice_RefreshCapture(true);
	assert(voice_capture_wanted && Voice_CaptureReady() && !voice_sending);
	Voice_Frame();
	Room_MonitorOffline();
	{
		Uint32 start = (Uint32)SDL_GetTicks();
		Room_MonitorStats(renderer, &stats);
		while (!stats.self_frames && !Room_MonitorTimedOut(start)) {
			SDL_Delay(10);
			Voice_Frame();
			Room_MonitorStats(renderer, &stats);
		}
		assert(stats.self_frames > 0);
	}
	Room_MonitorDrain(renderer, 1);
	Spatial_ResetSelf();
	Spatial_SelfGain(1.0f);
	Room_MonitorPCM(2);
	Room_MonitorStats(renderer, &stats);
	assert(stats.self_frames == 2 * VOICE_FRAME_SAMPLES);
	Room_MonitorDrain(renderer, 1);
	Spatial_ResetSelf();
	Spatial_SelfGain(1.0f);
	Room_MonitorStats(renderer, &stats);
	uint64_t dropped_before = stats.self_dropped;
	Room_MonitorPCM(3);
	Room_MonitorStats(renderer, &stats);
	assert(stats.self_frames == ROOM_MONITOR_QUEUE_FRAMES &&
		stats.self_dropped > dropped_before);
	Room_MonitorOffline();
	Spatial_ResetSelf();
	Room_MonitorStats(renderer, &stats);
	assert(!stats.self_frames);
	for (i = 0; i < 8; ++i) assert(Room_MonitorDraw(SA_BLOCK) == 0.0);
	assert(sv.models[1] && svs.clients && svs.clients[0].active &&
		svs.clients[0].edict && strstr(sv.models[1]->name, "e1m1"));
	cl.worldmodel = sv.models[1];
	VectorCopy(svs.clients[0].edict->v.origin, listener_origin);
	listener_origin[2] += 22.0f;
	VectorClear(listener_forward);
	VectorClear(listener_right);
	VectorClear(listener_up);
	listener_forward[0] = 1.0f;
	listener_right[1] = -1.0f;
	listener_up[2] = 1.0f;
	Spatial_Listener(listener_origin, listener_forward, listener_right, listener_up);
	Spatial_Update();
	renderer = spatial_callback_renderer;
	assert(renderer);
#ifdef __linux__
	baseline_count = Room_MonitorTasks(baseline);
	assert(!baseline_count);
#endif
	SpatialWorld_NewMap();
	Spatial_Update();
	Room_MonitorWaitReady();
#ifdef __linux__
	active_count = Room_MonitorTasks(active);
	assert(active_count > 0);
#else
	assert(0 && "room monitor task retirement check requires Linux");
#endif
	for (int mode = 1; mode <= 2; ++mode) {
		Cvar_SetValue("snd_spatial_room_mode", (float)mode);
		Spatial_Update();
		assert(spatial_callback_renderer == renderer);
		Spatial_ResetSelf();
		Spatial_SelfGain(1.0f);
		Room_MonitorPCM(2);
		Room_MonitorStats(renderer, &stats);
		assert(stats.self_frames == 2 * VOICE_FRAME_SAMPLES);
		Room_MonitorDrain(renderer, 0);
		Room_MonitorOffline();
	}
	Spatial_SetUnderwaterAlpha(0.5f);
	Spatial_Update();
	Room_MonitorPartial(renderer);
	Room_MonitorDraw(SA_BLOCK / 2);
	assert(room_monitor_pcm_signal > 0); /* actual converted continuation control */
	Room_MonitorPartial(renderer);
	Spatial_ResetSelf();
	Room_MonitorStats(renderer, &stats);
	assert(!stats.self_frames);
	for (i = 0; i < 16; ++i) {
		Room_MonitorDraw(SA_BLOCK);
		assert(!room_monitor_pcm_signal);
	}
	Room_MonitorPartial(renderer);
	Voice_SetSelfReverb(false); /* revoke directly from live filtered/queued state */
	assert(!voice_settings.desktop.self_reverb && !voice_capture_wanted &&
		!Voice_CaptureReady());
	Room_MonitorStats(renderer, &stats);
	assert(!stats.self_frames);
	for (i = 0; i < 16; ++i) {
		Room_MonitorDraw(SA_BLOCK);
		Room_MonitorStats(renderer, &stats);
		assert(!room_monitor_pcm_signal);
	}
	Spatial_SetUnderwaterAlpha(1.0f);
	Spatial_Update();
	Spatial_SelfGain(1.0f);
	uint64_t rejected_drops = stats.self_dropped;
	Room_MonitorPCM(1);
	Room_MonitorStats(renderer, &stats);
	assert(!stats.self_frames && stats.self_dropped == rejected_drops);
	Room_MonitorOffline();
#ifdef __linux__
	memcpy(retired, active, (size_t)active_count * sizeof(*retired));
	SpatialWorld_NewMap();
	Spatial_Update();
	Room_MonitorWaitReady();
	Room_MonitorWaitRetired(retired, active_count);
	active_count = Room_MonitorTasks(active);
	assert(active_count > 0);
	memcpy(retired, active, (size_t)active_count * sizeof(*retired));
	SpatialWorld_Clear();
	Room_MonitorWaitRetired(retired, active_count);
	Spatial_RoomStats(&room);
	assert(!room.ready && !room.triangles);
	Room_MonitorWaitTaskSet(baseline, baseline_count);
	SpatialWorld_NewMap();
	Spatial_Update();
	Room_MonitorWaitReady();
	active_count = Room_MonitorTasks(active);
	assert(active_count > 0);
	memcpy(retired, active, (size_t)active_count * sizeof(*retired));
#endif
	Spatial_Shutdown();
	spatial_callback_renderer = NULL;
	assert(!Spatial_Active());
#ifdef __linux__
	Room_MonitorWaitRetired(retired, active_count);
	Room_MonitorWaitTaskSet(baseline, baseline_count);
#endif
	cl = saved_cl;
	cls = saved_cls;
	cl.worldmodel = saved_worldmodel;
	cl.paused = saved_paused;
	sv.active = saved_server;
	memcpy(listener_origin, saved_listener_origin, sizeof(saved_listener_origin));
	memcpy(listener_forward, saved_listener_forward, sizeof(saved_listener_forward));
	memcpy(listener_right, saved_listener_right, sizeof(saved_listener_right));
	memcpy(listener_up, saved_listener_up, sizeof(saved_listener_up));
	voice_settings = saved_voice_settings;
	voice_profile_vr = saved_profile_vr;
	Cvar_SetQuick(&voice_input_gain, saved_input_gain);
	for (i = 0; i < 4; ++i) Cvar_SetQuick(room_cvars[i], saved_room_cvars[i]);
	Voice_SaveSettings();
	Voice_RefreshCapture(true);
	assert(Voice_CaptureReady() == saved_capture_ready);
	assert(!voice_sending && !cl.voice_outgoing_count);
}
#endif
