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

#ifndef VOICE_VAD_H
#define VOICE_VAD_H

#include "q_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VOICE_VAD_SAMPLE_RATE 48000
#define VOICE_VAD_FRAME_SAMPLES 960
#define VOICE_VAD_PREROLL_FRAMES 3u
#define VOICE_VAD_HANGOVER_FRAMES 12u

typedef enum voice_vad_gate_e {
	VOICE_VAD_GATE_CLOSED = 0,
	VOICE_VAD_GATE_OPENING,
	VOICE_VAD_GATE_OPEN,
	VOICE_VAD_GATE_CLOSING
} voice_vad_gate_t;

typedef struct voice_vad_config_s {
	int sensitivity; /* 0..100, where higher opens more readily. */
} voice_vad_config_t;

typedef struct voice_vad_s {
	voice_vad_config_t config;
	uint32_t frame_count;
	uint32_t meter_q8;
	uint32_t noise_floor_q8;
	uint8_t gate_open;
	uint8_t hangover_frames;
	uint8_t open_run;
	uint8_t closed_history;
} voice_vad_t;

typedef struct voice_vad_result_s {
	voice_vad_gate_t gate;
	int active;
	int opened;
	int closed;
	unsigned int preroll_frames;
	unsigned int hangover_frames;
	uint16_t meter;
	uint16_t noise_floor;
	uint16_t open_threshold;
	uint16_t close_threshold;
} voice_vad_result_t;

void Voice_VADConfigDefault(voice_vad_config_t *config);
void Voice_VADInit(voice_vad_t *vad, const voice_vad_config_t *config);
void Voice_VADReset(voice_vad_t *vad);
void Voice_VADSetSensitivity(voice_vad_t *vad, int sensitivity);
int Voice_VADProcessFrame(voice_vad_t *vad, const int16_t *samples,
	size_t sample_count, voice_vad_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_VAD_H */
