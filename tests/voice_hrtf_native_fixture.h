#ifndef VOICE_HRTF_NATIVE_FIXTURE_H
#define VOICE_HRTF_NATIVE_FIXTURE_H

#ifdef USE_STEAMAUDIO
#include "../Quake/snd_spatial.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void Voice_HRTFNativeChecks(client_t *source, client_state_t *source_state,
	client_t *receiver, client_state_t *receiver_state)
{
	client_state_t saved_cl = cl;
	qsocket_t *saved_netcon = cls.netcon;
	qsocket_t *source_netcon, *receiver_netcon;
	entity_t saved_entity, *entity;
	qmodel_t *player_model;
	vec3_t saved_origin, saved_forward, saved_right, saved_up;
	vec3_t saved_voice_origin, saved_voice_right;
	vec3_t origin = {0, 0, 0}, forward = {1, 0, 0};
	vec3_t right = {0, -1, 0}, up = {0, 0, 1};
	char saved_score_name[MAX_SCOREBOARDNAME], saved_receive[16], saved_hrtf[16];
	int saved_num_entities, modelindex;
	float far_distance;
	int16_t pcm[VOICE_FRAME_SAMPLES];
	const int slot = 0;
	assert(source && source_state && receiver && receiver_state && source->active &&
		source->voice_capable && source->edict && source->name[0] &&
		source->netconnection && receiver->active && receiver->netconnection);
	assert(source == &svs.clients[slot] && receiver_state->scores &&
		receiver_state->entities && receiver_state->maxclients > slot &&
		receiver_state->num_entities > slot + 1);
	assert(COM_CheckParm("-nosound") && !Spatial_Active() &&
		!spatial_callback_renderer && Cvar_VariableValue("voice_receive") != 0);
	if (!Cvar_FindVar("snd_hrtf")) S_Init();
	assert(Cvar_FindVar("snd_hrtf") && !Spatial_Active());
	assert(strlen(source->name) < sizeof(receiver_state->scores[slot].name));
	source_netcon = source->netconnection;
	receiver_netcon = receiver->netconnection;
	saved_num_entities = receiver_state->num_entities;
	entity = &receiver_state->entities[slot + 1];
	saved_entity = *entity;
	memcpy(saved_score_name, receiver_state->scores[slot].name, sizeof(saved_score_name));
	memcpy(saved_origin, listener_origin, sizeof(saved_origin));
	memcpy(saved_forward, listener_forward, sizeof(saved_forward));
	memcpy(saved_right, listener_right, sizeof(saved_right));
	memcpy(saved_up, listener_up, sizeof(saved_up));
	memcpy(saved_voice_origin, voice_listener_origin, sizeof(saved_voice_origin));
	memcpy(saved_voice_right, voice_listener_right, sizeof(saved_voice_right));
	snprintf(saved_receive, sizeof(saved_receive), "%s", Cvar_VariableString("voice_receive"));
	snprintf(saved_hrtf, sizeof(saved_hrtf), "%s", Cvar_VariableString("snd_hrtf"));
	Cvar_Set("snd_hrtf", "1");
	assert(Spatial_Init() && Spatial_Active());
	for (int channel = 0; channel < MAX_CHANNELS; ++channel)
		assert(!snd_channels[channel].sfx);
	{
		sa_room_stats_t room = {0};
		Spatial_RoomStats(&room);
		assert(!room.triangles && !room.geometry_bytes && !room.runs);
	}
	modelindex = (int)source->edict->v.modelindex;
	assert(modelindex > 0 && modelindex < MAX_MODELS && sv.models[modelindex]);
	player_model = sv.models[modelindex];
	for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
		pcm[i] = (i / 96) & 1 ? 12000 : -12000;
	Voice_UpdateSpatialization(origin, right);
	Spatial_Listener(origin, forward, right, up);
	far_distance = q_max(1.0f, voice_spatial_distance.value) + 256.0f;
	for (int test_case = 0; test_case < 5; ++test_case)
	{
		sa_stats_t stats = {0};
		double left_energy = 0, right_energy = 0;
		int radio = test_case >= 2, side = test_case == 0 ? 1 : test_case == 1 ? -1 : 0;
		Cvar_Set("voice_receive", "0");
		cl = *receiver_state; Voice_Frame(); *receiver_state = cl;
		Cvar_Set("voice_receive", "1");
		cl = *receiver_state; Voice_Frame(); *receiver_state = cl;
		assert(!voice_speakers[slot].have_generation && !voice_speakers[slot].jitter.count);
		assert(Voice_AtomicGet(&voice_speakers[slot].pcm_read) ==
			Voice_AtomicGet(&voice_speakers[slot].pcm_write));
		assert(spatial_callback_renderer);
		SA_GetStats(spatial_callback_renderer, &stats);
		assert(stats.stream_frames == 0);
		(void)snprintf(receiver_state->scores[slot].name,
			sizeof(receiver_state->scores[slot].name), "%s", source->name);
		VectorClear(entity->origin);
		entity->model = test_case == 3 ? NULL : player_model;
		entity->msgtime = receiver_state->mtime[0] - (test_case == 4 ? 1.0 : 0.0);
		if (test_case < 2 || test_case == 3 || test_case == 4)
			entity->origin[1] = test_case == 1 ? 256.0f : -256.0f;
		else
			entity->origin[0] = far_distance;
		Voice_UpdateSpatialization(origin, right);
		Spatial_Listener(origin, forward, right, up);
		for (int burst = 0; burst < 3; ++burst)
		{
			SendControlledFrame(source, source_state, pcm);
			RelayToReceiver(receiver, receiver_state);
		}
		assert(voice_speakers[slot].jitter.count > 0);
		for (int tick = 0; tick < 32 && voice_speakers[slot].jitter.count; ++tick)
		{
			realtime += VOICE_FRAME_MILLISECONDS / 1000.0;
			cl = *receiver_state; Voice_Frame(); *receiver_state = cl;
		}
		assert(!voice_speakers[slot].jitter.count && voice_speakers[slot].have_generation);
		assert(Voice_AtomicGet(&voice_speakers[slot].pcm_read) ==
			Voice_AtomicGet(&voice_speakers[slot].pcm_write));
		SA_GetStats(spatial_callback_renderer, &stats);
		assert(stats.stream_frames > 0);
		for (int block = 0; block < 64; ++block)
		{
			float output[SA_BLOCK * 2];
			Spatial_Render(output, SA_BLOCK);
			for (int frame = 0; frame < SA_BLOCK; ++frame)
			{
				float left = output[frame * 2], right_sample = output[frame * 2 + 1];
				assert(isfinite(left) && isfinite(right_sample));
				if (radio) assert(left == right_sample);
				if (block >= 2)
				{
					left_energy += (double)left * left;
					right_energy += (double)right_sample * right_sample;
				}
			}
			SA_GetStats(spatial_callback_renderer, &stats);
			if (!stats.stream_frames) break;
		}
		assert(!stats.stream_frames && left_energy > 0 && right_energy > 0);
		if (side > 0) assert(right_energy > left_energy);
		if (side < 0) assert(left_energy > right_energy);
	}
	/* Retire the last case, then leave fresh decoded speech pending for receive-off. */
	Cvar_Set("voice_receive", "0");
	cl = *receiver_state; Voice_Frame(); *receiver_state = cl;
	Cvar_Set("voice_receive", "1");
	cl = *receiver_state; Voice_Frame(); *receiver_state = cl;
	assert(!voice_speakers[slot].have_generation && !voice_speakers[slot].jitter.count);
	{
		sa_stats_t stats = {0};
		SA_GetStats(spatial_callback_renderer, &stats);
		assert(!stats.stream_frames);
	}

	VectorSet(entity->origin, 0, -256, 0);
	entity->model = player_model;
	entity->msgtime = receiver_state->mtime[0];
	Voice_UpdateSpatialization(origin, right);
	Spatial_Listener(origin, forward, right, up);
	SendControlledFrame(source, source_state, pcm);
	RelayToReceiver(receiver, receiver_state);
	for (int tick = 0; tick < 32 && voice_speakers[slot].jitter.count; ++tick)
	{
		realtime += VOICE_FRAME_MILLISECONDS / 1000.0;
		cl = *receiver_state; Voice_Frame(); *receiver_state = cl;
	}
	assert(!voice_speakers[slot].jitter.count && voice_speakers[slot].have_generation);
	assert(Voice_AtomicGet(&voice_speakers[slot].pcm_read) ==
		Voice_AtomicGet(&voice_speakers[slot].pcm_write));
	{
		sa_stats_t stats = {0};
		SA_GetStats(spatial_callback_renderer, &stats);
		assert(stats.stream_frames > 0);
	}
	/* Prime native HRTF, then leave a partial rendered block plus queued speech. */
	{
		sa_stats_t stats = {0};
		double signal = 0;
		Spatial_CallbackRender(SA_BLOCK, NULL, NULL);
		Spatial_CallbackRender(17, NULL, &signal);
		SA_GetStats(spatial_callback_renderer, &stats);
		assert(signal > 0 && stats.stream_frames > 0);
	}
	Cvar_Set("voice_receive", "0");
	cl = *receiver_state; Voice_Frame(); *receiver_state = cl;
	assert(!voice_speakers[slot].have_generation && !voice_speakers[slot].jitter.count);
	{
		sa_stats_t stats = {0};
		SA_GetStats(spatial_callback_renderer, &stats);
		assert(!stats.stream_frames);
	}
	for (int block = 0; block < 8; ++block)
	{
		float output[SA_BLOCK * 2];
		Spatial_Render(output, SA_BLOCK);
		for (int sample = 0; sample < SA_BLOCK * 2; ++sample)
			assert(output[sample] == 0.0f);
	}
	Cvar_Set("voice_receive", "1");
	cl = *receiver_state; Voice_Frame(); *receiver_state = cl;
	*entity = saved_entity;
	memcpy(receiver_state->scores[slot].name, saved_score_name, sizeof(saved_score_name));
	receiver_state->num_entities = saved_num_entities;
	Voice_UpdateSpatialization(saved_voice_origin, saved_voice_right);
	Spatial_Listener(saved_origin, saved_forward, saved_right, saved_up);
	Cvar_Set("snd_hrtf", saved_hrtf);
	Cvar_Set("voice_receive", saved_receive);
	cl = *receiver_state; Voice_Frame(); *receiver_state = cl;
	assert(receiver_state->num_entities == saved_num_entities &&
		!memcmp(entity, &saved_entity, sizeof(saved_entity)) &&
		!memcmp(receiver_state->scores[slot].name, saved_score_name, sizeof(saved_score_name)));
	assert(source->netconnection == source_netcon && receiver->netconnection == receiver_netcon);
	assert(spatial_callback_renderer);
	{
		sa_stats_t stats = {0};
		SA_GetStats(spatial_callback_renderer, &stats);
		assert(!stats.stream_frames && !stats.nonfinite && !stats.rt_allocations);
	}
	Spatial_Shutdown();
	spatial_callback_renderer = NULL;
	cl = saved_cl;
	cls.netcon = saved_netcon;
}
#endif
#endif
