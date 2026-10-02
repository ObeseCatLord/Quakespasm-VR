#ifndef QSVR_MUSIC_NATIVE_FIXTURE_H
#define QSVR_MUSIC_NATIVE_FIXTURE_H

#ifdef USE_STEAMAUDIO
#include "bgmusic.h"
#include "snd_codec.h"
#include "snd_spatial.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* spatial_callback_native_fixture.h owns this borrowed observer. */
void __real_S_CodecCloseStream(snd_stream_t *stream);
int __real_S_CodecReadStream(snd_stream_t *stream, int bytes, void *buffer);
int __real_S_CodecRewindStream(snd_stream_t *stream);

typedef struct
{
	uintptr_t stream_identity;
	snd_info_t info;
	uintptr_t close_identity;
	int identity_mismatch;
	int read_calls, positive_reads, eof_reads, failed_reads;
	int requested_bytes, returned_bytes, positive_bytes;
	int rewind_calls, rewind_failures, close_calls, positive_after_rewind;
} music_native_observer_t;

static music_native_observer_t music_native_observer;

static void Music_NativeRecordStream(const snd_stream_t *stream, const snd_info_t *info)
{
	if (!music_native_observer.stream_identity)
	{
		music_native_observer.stream_identity = (uintptr_t)stream;
		music_native_observer.info = *info;
	}
	else if (music_native_observer.stream_identity != (uintptr_t)stream)
		music_native_observer.identity_mismatch = 1;
}

int __wrap_S_CodecReadStream(snd_stream_t *stream, int bytes, void *buffer)
{
	int result = __real_S_CodecReadStream(stream, bytes, buffer);
	Music_NativeRecordStream(stream, &stream->info);
	music_native_observer.read_calls++;
	music_native_observer.requested_bytes = bytes;
	music_native_observer.returned_bytes = result;
	if (result > 0)
	{
		music_native_observer.positive_reads++;
		if (music_native_observer.rewind_calls)
			music_native_observer.positive_after_rewind++;
		music_native_observer.positive_bytes += result;
	}
	else if (result == 0)
		music_native_observer.eof_reads++;
	else
		music_native_observer.failed_reads++;
	return result;
}

int __wrap_S_CodecRewindStream(snd_stream_t *stream)
{
	int result = __real_S_CodecRewindStream(stream);
	Music_NativeRecordStream(stream, &stream->info);
	music_native_observer.rewind_calls++;
	if (result != 0)
		music_native_observer.rewind_failures++;
	return result;
}

void __wrap_S_CodecCloseStream(snd_stream_t *stream)
{
	/* Observe identity before the real owner ends the stream lifetime. */
	Music_NativeRecordStream(stream, &stream->info);
	music_native_observer.close_identity = (uintptr_t)stream;
	music_native_observer.close_calls++;
	__real_S_CodecCloseStream(stream);
}

static void Music_NativeResetObserver(void)
{
	memset(&music_native_observer, 0, sizeof(music_native_observer));
}

static void Music_NativeSetLoop(int enabled)
{
	assert(Cmd_ExecuteString(enabled ? "music_loop 1" : "music_loop 0", src_command));
}

static void Music_NativeAssertFormat(int rate, int width, int channels)
{
	assert(music_native_observer.stream_identity && !music_native_observer.identity_mismatch);
	assert(music_native_observer.info.rate == rate);
	assert(music_native_observer.info.width == width);
	assert(music_native_observer.info.bits == width * 8);
	assert(music_native_observer.info.channels == channels);
}

static void Music_NativeAssertNoOtherAudio(sa_renderer_t *renderer)
{
	sa_stats_t stats = {0};
	sa_room_stats_t room = {0};
	SA_GetStats(renderer, &stats);
	Spatial_RoomStats(&room);
	assert(stats.active == 0 && stats.stream_frames == 0 && stats.self_frames == 0);
	assert(!room.ready && !room.failed && room.triangles == 0);
}

static void Music_NativeAssertSilent(int blocks)
{
	float output[SA_BLOCK * 2];
	for (int block = 0; block < blocks; ++block)
	{
		Spatial_Render(output, SA_BLOCK);
		for (int i = 0; i < SA_BLOCK * 2; ++i)
			assert(isfinite(output[i]) && output[i] == 0.0f);
		Spatial_Update();
	}
}

static void Music_NativeRenderAndDrain(sa_renderer_t *renderer, int channels, int empty_space)
{
	float output[SA_BLOCK * 2];
	double energy[2] = {0.0, 0.0};
	int drained = 0;
	for (int block = 0; block < 64; ++block)
	{
		Spatial_Render(output, SA_BLOCK);
		for (int i = 0; i < SA_BLOCK; ++i)
		{
			float left = output[2 * i], right = output[2 * i + 1];
			assert(isfinite(left) && isfinite(right));
			energy[0] += (double)left * left;
			energy[1] += (double)right * right;
			if (channels == 1)
				assert(fabsf(left - right) < 0.000001f);
		}
		Spatial_Update();
		if (SA_MusicSpace(renderer) == empty_space)
		{
			drained = 1;
			break;
		}
	}
	assert(drained && energy[0] + energy[1] > 0.000001);
	if (channels == 2)
		assert(energy[0] > energy[1] && energy[1] > 0.000001);
	Music_NativeAssertSilent(2);
}

static void Music_NativePlayToEOF(sa_renderer_t *renderer, const char *name,
	int rate, int width, int channels, int pause_first)
{
	int empty_space;
	Music_NativeSetLoop(0);
	BGM_Stop();
	Music_NativeResetObserver();
	empty_space = SA_MusicSpace(renderer);
	assert(empty_space == SA_STREAM_FRAMES - 1 && !music_native_observer.stream_identity);
	BGM_Play(name);
	if (pause_first)
	{
		BGM_Pause();
		BGM_Update();
		assert(!music_native_observer.read_calls && !music_native_observer.close_calls);
		assert(SA_MusicSpace(renderer) == empty_space);
		Music_NativeAssertSilent(2);
		BGM_Resume();
	}
	BGM_Update();
	Music_NativeAssertFormat(rate, width, channels);
	assert(music_native_observer.read_calls > 0 && music_native_observer.positive_reads > 0);
	assert(music_native_observer.positive_bytes > 0 && music_native_observer.eof_reads > 0);
	assert(!music_native_observer.failed_reads && !music_native_observer.rewind_calls);
	assert(music_native_observer.close_calls == 1);
	assert(music_native_observer.close_identity == music_native_observer.stream_identity);
	assert(music_native_observer.requested_bytes > 0 &&
		music_native_observer.returned_bytes == 0);
	assert(SA_MusicSpace(renderer) < empty_space);
	Music_NativeRenderAndDrain(renderer, channels, empty_space);
}

static void Music_NativeLoopStop(sa_renderer_t *renderer, int empty_space)
{
	float output[SA_BLOCK * 2];
	double signal = 0.0;
	Music_NativeSetLoop(1);
	BGM_Stop();
	assert(SA_MusicSpace(renderer) == empty_space);
	Music_NativeResetObserver();
	BGM_Play("native-u8.wav");
	BGM_Update();
	Music_NativeAssertFormat(11025, 1, 1);
	assert(music_native_observer.positive_reads > 0 &&
		music_native_observer.positive_bytes > 0);
	assert(music_native_observer.eof_reads > 0 &&
		music_native_observer.rewind_calls > 0 &&
		music_native_observer.rewind_failures == 0 &&
		music_native_observer.positive_after_rewind > 0);
	assert(music_native_observer.rewind_calls < 16 && !music_native_observer.close_calls);
	assert(SA_MusicSpace(renderer) < empty_space);
	Spatial_Render(output, SA_BLOCK);
	for (int i = 0; i < SA_BLOCK * 2; ++i)
	{
		assert(isfinite(output[i]));
		signal += fabs(output[i]);
	}
	assert(signal > 0.000001);
	BGM_Stop();
	assert(music_native_observer.close_calls == 1 &&
		music_native_observer.close_identity == music_native_observer.stream_identity);
	assert(SA_MusicSpace(renderer) == empty_space);
	Music_NativeAssertSilent(2);
}

static void Music_NativeChecks(void)
{
	static const struct
	{
		const char *name;
		int rate, width, channels;
	} cases[] = {
		{"native-u8.wav", 11025, 1, 1},
		{"native-mono.wav", 22050, 2, 1},
		{"native-stereo.wav", 48000, 2, 2},
		{"native-flac.flac", 44100, 2, 2},
		{"native-vorbis.ogg", 44100, 2, 2},
		{"native-mp3.mp3", 48000, 2, 2},
		{"native-opus.opus", 48000, 2, 2}
	};
	static const unsigned required_codecs[] = {
		CODECTYPE_WAV, CODECTYPE_FLAC, CODECTYPE_VORBIS,
		CODECTYPE_MP3, CODECTYPE_OPUS
	};
	char saved_bgmvolume[64];
	cvar_t *volume;
	sa_renderer_t *renderer;
	int empty_space, restore_volume;
	assert(COM_CheckParm("-nosound") && COM_CheckParm("-dedicated"));
	assert(shm && shm->speed == SA_RATE && shm->channels == 2 && shm->samplebits == 16);
	assert(!Spatial_Active() && !spatial_callback_renderer);
	assert(total_channels == 0);
	for (int i = 0; i < MAX_CHANNELS; ++i)
		assert(!snd_channels[i].sfx);

	/* Reuse the complete native registration owner even with this option alone. */
	if (!Cvar_FindVar("snd_hrtf"))
		S_Init();
	assert(Cvar_FindVar("snd_hrtf"));
	volume = Cvar_FindVar("bgmvolume");
	assert(volume && volume->string);
	snprintf(saved_bgmvolume, sizeof(saved_bgmvolume), "%s", volume->string);
	restore_volume = volume->value <= 0.0f;
	if (restore_volume)
		Cvar_SetQuick(volume, "1");

	S_CodecInit();
	for (unsigned i = 0; i < sizeof(required_codecs) / sizeof(required_codecs[0]); ++i)
		assert(S_CodecIsAvailable(required_codecs[i]) == 1);
	assert(BGM_Init());
	assert(Spatial_Init() && Spatial_Active());
	Spatial_Update();
	renderer = spatial_callback_renderer;
	assert(renderer);
	empty_space = SA_MusicSpace(renderer);
	assert(empty_space == SA_STREAM_FRAMES - 1);
	Music_NativeAssertNoOtherAudio(renderer);

	Music_NativeLoopStop(renderer, empty_space);
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
		Music_NativePlayToEOF(renderer, cases[i].name, cases[i].rate,
			cases[i].width, cases[i].channels, !strcmp(cases[i].name, "native-mono.wav"));

	Music_NativeSetLoop(1);
	BGM_Shutdown();
	S_CodecShutdown();
	Spatial_Shutdown();
	spatial_callback_renderer = NULL;
	assert(!Spatial_Active());
	if (restore_volume)
		Cvar_SetQuick(volume, saved_bgmvolume);
}
#endif
#endif
