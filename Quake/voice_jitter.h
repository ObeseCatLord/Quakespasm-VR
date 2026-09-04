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

#ifndef VOICE_JITTER_H
#define VOICE_JITTER_H

#include "q_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VOICE_JITTER_MAX_PACKETS 16u
#define VOICE_JITTER_MAX_PAYLOAD 400u
#define VOICE_JITTER_FRAME_MS 20u
#define VOICE_JITTER_MIN_TARGET_DELAY_MS 60u
#define VOICE_JITTER_MAX_TARGET_DELAY_MS 140u
#define VOICE_JITTER_STALE_MS 400u

typedef enum voice_jitter_status_e {
	VOICE_JITTER_OK = 0,
	VOICE_JITTER_INVALID_ARGUMENT,
	VOICE_JITTER_DUPLICATE,
	VOICE_JITTER_TOO_OLD,
	VOICE_JITTER_FULL
} voice_jitter_status_t;

typedef enum voice_jitter_action_e {
	VOICE_JITTER_WAIT = 0,
	VOICE_JITTER_PACKET,
	VOICE_JITTER_PLC
} voice_jitter_action_t;

typedef struct voice_jitter_packet_s {
	uint16_t sequence;
	uint32_t timestamp;
	uint8_t talkspurt;
	uint8_t flags;
	const uint8_t *payload;
	size_t payload_size;
} voice_jitter_packet_t;

typedef struct voice_jitter_frame_s {
	voice_jitter_action_t action;
	uint16_t sequence;
	uint32_t timestamp;
	uint8_t talkspurt;
	uint8_t flags;
	uint32_t deadline_ms;
	unsigned int target_delay_ms;
	unsigned int queued_packets;
	size_t payload_size;
	uint8_t payload[VOICE_JITTER_MAX_PAYLOAD];
} voice_jitter_frame_t;

typedef struct voice_jitter_entry_s {
	uint16_t sequence;
	uint32_t timestamp;
	uint32_t arrival_ms;
	uint8_t talkspurt;
	uint8_t flags;
	uint16_t payload_size;
	uint8_t payload[VOICE_JITTER_MAX_PAYLOAD];
} voice_jitter_entry_t;

typedef struct voice_jitter_s {
	voice_jitter_entry_t entries[VOICE_JITTER_MAX_PACKETS];
	unsigned int capacity;
	unsigned int count;
	uint16_t expected_sequence;
	uint16_t highest_sequence;
	uint32_t next_deadline_ms;
	uint32_t last_activity_ms;
	uint8_t target_delay_ms;
	uint8_t current_talkspurt;
	uint8_t impairment_score;
	uint8_t playout_started;
	uint8_t have_highest;
	uint8_t have_talkspurt;
} voice_jitter_t;

void Voice_JitterInit(voice_jitter_t *jitter, unsigned int capacity);
void Voice_JitterReset(voice_jitter_t *jitter);
int Voice_JitterSequenceIsNewer(uint16_t sequence, uint16_t previous);
voice_jitter_status_t Voice_JitterInsert(voice_jitter_t *jitter,
	const voice_jitter_packet_t *packet, uint32_t now_ms);
voice_jitter_status_t Voice_JitterNextFrame(voice_jitter_t *jitter,
	uint32_t now_ms, voice_jitter_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_JITTER_H */
