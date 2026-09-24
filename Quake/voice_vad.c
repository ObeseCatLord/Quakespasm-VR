/*
Copyright (C) 2026 Hiina <hiina@hiina.space>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "voice_vad.h"

#define VOICE_VAD_Q8 256u

static int Voice_VADClampSensitivity(int sensitivity)
{
	if (sensitivity < 0)
		return 0;
	if (sensitivity > 100)
		return 100;
	return sensitivity;
}

static uint32_t Voice_VADMaxU32(uint32_t a, uint32_t b)
{
	return a > b ? a : b;
}

static uint32_t Voice_VADBlend(uint32_t current, uint32_t sample,
	uint32_t numerator, uint32_t denominator)
{
	return (current * numerator + sample) / denominator;
}

static uint32_t Voice_VADFrameEnergyQ8(const int16_t *samples, size_t count)
{
	uint64_t sum = 0;
	size_t i;

	for (i = 0; i < count; ++i) {
		int sample = samples[i];
		sum += (uint64_t)(sample < 0 ? -sample : sample);
	}

	return (uint32_t)((sum << 8) / (uint64_t)count);
}

static void Voice_VADThresholds(const voice_vad_t *vad, uint32_t *open_q8,
	uint32_t *close_q8)
{
	uint32_t noise_q8 = vad->noise_floor_q8;
	uint32_t sensitivity = (uint32_t)Voice_VADClampSensitivity(vad->config.sensitivity);
	uint32_t open_ratio_q10 = 2304u - sensitivity * 11u;
	uint32_t close_ratio_q10 = open_ratio_q10 > 256u ? open_ratio_q10 - 256u : 1024u;
	uint32_t open_margin = 960u > sensitivity * 7u ? 960u - sensitivity * 7u : 192u;
	uint32_t close_margin = open_margin / 2u + 48u;

	*open_q8 = Voice_VADMaxU32((noise_q8 * open_ratio_q10) / 1024u,
		noise_q8 + open_margin * VOICE_VAD_Q8);
	*close_q8 = Voice_VADMaxU32((noise_q8 * close_ratio_q10) / 1024u,
		noise_q8 + close_margin * VOICE_VAD_Q8);
}

void Voice_VADConfigDefault(voice_vad_config_t *config)
{
	if (!config)
		return;
	config->sensitivity = 50;
}

void Voice_VADReset(voice_vad_t *vad)
{
	if (!vad)
		return;
	vad->frame_count = 0;
	vad->meter_q8 = 0;
	vad->noise_floor_q8 = 0;
	vad->gate_open = 0;
	vad->hangover_frames = 0;
	vad->open_run = 0;
	vad->closed_history = 0;
}

void Voice_VADInit(voice_vad_t *vad, const voice_vad_config_t *config)
{
	if (!vad)
		return;

	Voice_VADConfigDefault(&vad->config);
	if (config)
		vad->config = *config;
	vad->config.sensitivity = Voice_VADClampSensitivity(vad->config.sensitivity);
	Voice_VADReset(vad);
}

void Voice_VADSetSensitivity(voice_vad_t *vad, int sensitivity)
{
	if (!vad)
		return;
	vad->config.sensitivity = Voice_VADClampSensitivity(sensitivity);
}

int Voice_VADProcessFrame(voice_vad_t *vad, const int16_t *samples,
	size_t sample_count, voice_vad_result_t *result)
{
	uint32_t energy_q8;
	uint32_t open_q8;
	uint32_t close_q8;
	int loud;
	int near_gate;
	int was_open;

	if (!vad || !samples || !result || sample_count != VOICE_VAD_FRAME_SAMPLES)
		return 0;

	energy_q8 = Voice_VADFrameEnergyQ8(samples, sample_count);
	if (vad->frame_count == 0)
		vad->noise_floor_q8 = energy_q8;
	Voice_VADThresholds(vad, &open_q8, &close_q8);
	was_open = vad->gate_open != 0;
	loud = energy_q8 >= (was_open ? close_q8 : open_q8);
	near_gate = energy_q8 >= close_q8;

	memset(result, 0, sizeof(*result));
	result->open_threshold = (uint16_t)((open_q8 + 128u) >> 8);
	result->close_threshold = (uint16_t)((close_q8 + 128u) >> 8);

	if (was_open) {
		if (loud) {
			vad->hangover_frames = VOICE_VAD_HANGOVER_FRAMES;
		} else if (vad->hangover_frames > 0) {
			--vad->hangover_frames;
		}
		if (!loud && vad->hangover_frames == 0) {
			vad->gate_open = 0;
			vad->open_run = 0;
			result->closed = 1;
		}
	} else {
		if (loud) {
			if (vad->open_run < 255)
				++vad->open_run;
		} else {
			vad->open_run = 0;
		}
		if (vad->open_run >= 2) {
			vad->gate_open = 1;
			vad->hangover_frames = VOICE_VAD_HANGOVER_FRAMES;
			result->opened = 1;
			result->preroll_frames = vad->closed_history;
			vad->closed_history = 0;
		}
	}

	if (!vad->gate_open) {
		if (!loud) {
			vad->noise_floor_q8 = vad->noise_floor_q8 == 0 ? energy_q8 :
				Voice_VADBlend(vad->noise_floor_q8, energy_q8, 15u, 16u);
		} else if (energy_q8 < open_q8) {
			vad->noise_floor_q8 = Voice_VADBlend(vad->noise_floor_q8, energy_q8, 31u, 32u);
		}
	} else if (energy_q8 <= close_q8) {
		vad->noise_floor_q8 = Voice_VADBlend(vad->noise_floor_q8, energy_q8, 63u, 64u);
	}

	vad->meter_q8 = vad->frame_count == 0 ? energy_q8 :
		Voice_VADBlend(vad->meter_q8, energy_q8, 3u, 4u);
	vad->frame_count++;
	result->active = vad->gate_open != 0;
	result->hangover_frames = vad->hangover_frames;
	result->meter = (uint16_t)((vad->meter_q8 + 128u) >> 8);
	result->noise_floor = (uint16_t)((vad->noise_floor_q8 + 128u) >> 8);

	if (vad->gate_open) {
		result->gate = near_gate ? VOICE_VAD_GATE_OPEN : VOICE_VAD_GATE_CLOSING;
	} else if (vad->open_run > 0) {
		result->gate = VOICE_VAD_GATE_OPENING;
	} else {
		result->gate = VOICE_VAD_GATE_CLOSED;
	}

	if (!vad->gate_open && !result->opened &&
		vad->closed_history < VOICE_VAD_PREROLL_FRAMES)
		++vad->closed_history;

	return 1;
}
