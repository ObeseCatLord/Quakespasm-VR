#ifndef VOICE_FALLBACK_NATIVE_FIXTURE_H
#define VOICE_FALLBACK_NATIVE_FIXTURE_H

static void Voice_FallbackPacket(client_t *source, client_state_t *source_state,
	client_t *receiver, client_state_t *receiver_state, qmodel_t *input_model,
	const vec3_t position, const vec3_t listener, const vec3_t right,
	qboolean stale, int direction)
{
	int16_t pcm[VOICE_FRAME_SAMPLES], mixed[VOICE_PCM_RING_FRAMES * 2] = {0};
	entity_t *speaker_entity = &receiver_state->entities[1];
	qboolean decoded = false;
	uint64_t left_energy = 0, right_energy = 0;
	int frames, read, write;
	assert(!voice_sending && !voice_speakers[0].jitter.count);
	assert(Voice_AtomicGet(&voice_speakers[0].pcm_read) ==
		Voice_AtomicGet(&voice_speakers[0].pcm_write));
	VectorCopy(position, speaker_entity->origin);
	speaker_entity->model = input_model;
	speaker_entity->msgtime = receiver_state->mtime[0] - (stale ? 1.0 : 0.0);
	Voice_UpdateSpatialization(listener, right);
	for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
		pcm[i] = (i / 96) & 1 ? 12000 : -12000;
	SendControlledFrame(source, source_state, pcm);
	RelayToReceiver(receiver, receiver_state);
	cl = *receiver_state;
	PlayUntilBuffered();
	for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i)
		decoded |= voice_decode_frame[i] != 0;
	assert(decoded && UnreadSpeakerHasSignal());
	*receiver_state = cl;
	assert(receiver_state->entities[1].model == input_model &&
		receiver_state->num_entities > 1);
	read = Voice_AtomicGet(&voice_speakers[0].pcm_read);
	write = Voice_AtomicGet(&voice_speakers[0].pcm_write);
	frames = (write - read + VOICE_PCM_RING_FRAMES) % VOICE_PCM_RING_FRAMES;
	assert(frames > 0 && frames < VOICE_PCM_RING_FRAMES);
	Voice_MixAudio((unsigned char *)mixed, frames * 2 * sizeof(int16_t), 16, 2,
		VOICE_SAMPLE_RATE, false);
	for (int i = 0; i < frames; ++i)
	{
		int left = mixed[i * 2], right_sample = mixed[i * 2 + 1];
		left_energy += left < 0 ? (uint64_t)-left : (uint64_t)left;
		right_energy += right_sample < 0 ? (uint64_t)-right_sample :
			(uint64_t)right_sample;
		if (!direction) assert(left == right_sample);
	}
	assert(left_energy && right_energy);
	if (direction > 0) assert(right_energy > left_energy);
	if (direction < 0) assert(left_energy > right_energy);
	assert(Voice_AtomicGet(&voice_speakers[0].pcm_read) ==
		Voice_AtomicGet(&voice_speakers[0].pcm_write));
}

void Voice_FallbackNativeChecks(client_t *source, client_state_t *source_state,
	client_t *receiver, client_state_t *receiver_state)
{
	entity_t *speaker_entity;
	qmodel_t *player_model;
	qsocket_t *source_netcon, *receiver_netcon, *saved_netcon;
	client_state_t saved_cl = cl;
	vec3_t saved_origin, saved_listener, saved_right, listener = {0, 0, 0};
	vec3_t right = {1, 0, 0}, reversed = {-1, 0, 0}, position = {256, 0, 0};
	qmodel_t *saved_model;
	double saved_msgtime;
	int saved_num_entities, modelindex;
	float far_distance, saved_voice_volume, saved_radio_volume, saved_distance;
	float saved_speaker_volume;
	qboolean saved_muted;
	assert(source && source_state && receiver && receiver_state && source->edict);
	assert(receiver_state->entities && receiver_state->num_entities > 1);
	assert(!Spatial_Active() && voice_radio_volume.value > 0 &&
		voice_volume.value > 0 && voice_speakers[0].volume > 0);
	modelindex = (int)source->edict->v.modelindex;
	assert(modelindex > 0 && modelindex < MAX_MODELS);
	player_model = sv.models[modelindex];
	assert(player_model);
	source_netcon = source->netconnection;
	receiver_netcon = receiver->netconnection;
	saved_netcon = cls.netcon;
	saved_num_entities = receiver_state->num_entities;
	speaker_entity = &receiver_state->entities[1];
	VectorCopy(speaker_entity->origin, saved_origin);
	saved_model = speaker_entity->model;
	saved_msgtime = speaker_entity->msgtime;
	saved_voice_volume = voice_volume.value;
	saved_radio_volume = voice_radio_volume.value;
	saved_distance = voice_spatial_distance.value;
	saved_speaker_volume = voice_speakers[0].volume;
	saved_muted = voice_speakers[0].muted;
	VectorCopy(voice_listener_origin, saved_listener);
	VectorCopy(voice_listener_right, saved_right);
	Voice_FallbackPacket(source, source_state, receiver, receiver_state,
		player_model, position, listener, right, false, 1);
	Voice_FallbackPacket(source, source_state, receiver, receiver_state,
		player_model, position, listener, reversed, false, -1);
	VectorSet(listener, 512, 0, 0);
	Voice_FallbackPacket(source, source_state, receiver, receiver_state,
		player_model, position, listener, right, false, -1);
	VectorClear(listener);
	far_distance = q_max(1.0f, voice_spatial_distance.value) + 256.0f;
	VectorSet(position, far_distance, 0, 0);
	Voice_FallbackPacket(source, source_state, receiver, receiver_state,
		player_model, position, listener, right, false, 0);
	VectorSet(position, 256, 0, 0);
	Voice_FallbackPacket(source, source_state, receiver, receiver_state,
		NULL, position, listener, right, false, 0);
	Voice_FallbackPacket(source, source_state, receiver, receiver_state,
		player_model, position, listener, right, true, 0);
	VectorCopy(saved_origin, speaker_entity->origin);
	speaker_entity->model = saved_model;
	speaker_entity->msgtime = saved_msgtime;
	assert(receiver_state->num_entities == saved_num_entities &&
		receiver_state->entities[1].model == saved_model &&
		receiver_state->entities[1].origin[0] == saved_origin[0] &&
		receiver_state->entities[1].origin[1] == saved_origin[1] &&
		receiver_state->entities[1].origin[2] == saved_origin[2] &&
		receiver_state->entities[1].msgtime == saved_msgtime);
	Voice_UpdateSpatialization(saved_listener, saved_right);
	assert(!memcmp(voice_listener_origin, saved_listener, sizeof(saved_listener)) &&
		!memcmp(voice_listener_right, saved_right, sizeof(saved_right)));
	assert(voice_volume.value == saved_voice_volume &&
		voice_radio_volume.value == saved_radio_volume &&
		voice_spatial_distance.value == saved_distance &&
		voice_speakers[0].volume == saved_speaker_volume &&
		voice_speakers[0].muted == saved_muted);
	assert(source->netconnection == source_netcon &&
		receiver->netconnection == receiver_netcon);
	cls.netcon = saved_netcon;
	cl = saved_cl;
}

#endif
