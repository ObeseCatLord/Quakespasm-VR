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

#include "voice_jitter.h"

static unsigned int Voice_JitterClampTarget(unsigned int delay_ms)
{
	if (delay_ms < VOICE_JITTER_MIN_TARGET_DELAY_MS)
		return VOICE_JITTER_MIN_TARGET_DELAY_MS;
	if (delay_ms > VOICE_JITTER_MAX_TARGET_DELAY_MS)
		return VOICE_JITTER_MAX_TARGET_DELAY_MS;
	return delay_ms;
}

int Voice_JitterSequenceIsNewer(uint16_t sequence, uint16_t previous)
{
	uint16_t delta = (uint16_t)(sequence - previous);
	return delta != 0 && delta < 0x8000u;
}

static int Voice_JitterSequenceCompare(uint16_t a, uint16_t b)
{
	if (a == b)
		return 0;
	return Voice_JitterSequenceIsNewer(a, b) ? 1 : -1;
}

static void Voice_JitterFinalizeTalkspurt(voice_jitter_t *jitter)
{
	unsigned int next_target;

	if (!jitter->have_talkspurt)
		return;
	if (jitter->impairment_score == 0) {
		next_target = jitter->target_delay_ms > VOICE_JITTER_MIN_TARGET_DELAY_MS ?
			jitter->target_delay_ms - 20u : VOICE_JITTER_MIN_TARGET_DELAY_MS;
	} else {
		next_target = 60u + (unsigned int)jitter->impairment_score * 20u;
	}
	jitter->target_delay_ms = (uint8_t)Voice_JitterClampTarget(next_target);
}

void Voice_JitterEndTalkspurt(voice_jitter_t *jitter)
{
	unsigned int next_target;

	if (!jitter || !jitter->have_talkspurt)
		return;
	Voice_JitterFinalizeTalkspurt(jitter);
	next_target = jitter->target_delay_ms;
	Voice_JitterReset(jitter);
	jitter->target_delay_ms = (uint8_t)next_target;
}

void Voice_JitterReset(voice_jitter_t *jitter)
{
	if (!jitter)
		return;
	jitter->count = 0;
	jitter->expected_sequence = 0;
	jitter->highest_sequence = 0;
	jitter->next_deadline_ms = 0;
	jitter->last_activity_ms = 0;
	jitter->current_talkspurt = 0;
	jitter->impairment_score = 0;
	jitter->playout_started = 0;
	jitter->have_highest = 0;
	jitter->have_talkspurt = 0;
}

void Voice_JitterInit(voice_jitter_t *jitter, unsigned int capacity)
{
	if (!jitter)
		return;
	memset(jitter, 0, sizeof(*jitter));
	jitter->capacity = capacity == 0 || capacity > VOICE_JITTER_MAX_PACKETS ?
		VOICE_JITTER_MAX_PACKETS : capacity;
	jitter->target_delay_ms = VOICE_JITTER_MIN_TARGET_DELAY_MS;
}

static void Voice_JitterMaybeResetStale(voice_jitter_t *jitter, uint32_t now_ms)
{
	uint32_t idle;
	unsigned int next_target;

	if (!jitter->have_talkspurt)
		return;
	idle = now_ms - jitter->last_activity_ms;
	if (idle < 0x80000000u && idle >= VOICE_JITTER_STALE_MS) {
		Voice_JitterFinalizeTalkspurt(jitter);
		next_target = jitter->target_delay_ms;
		Voice_JitterReset(jitter);
		jitter->target_delay_ms = (uint8_t)next_target;
	}
}

static void Voice_JitterBeginTalkspurt(voice_jitter_t *jitter, uint8_t talkspurt)
{
	unsigned int next_target = jitter->target_delay_ms;

	if (jitter->have_talkspurt) {
		Voice_JitterFinalizeTalkspurt(jitter);
		next_target = jitter->target_delay_ms;
		Voice_JitterReset(jitter);
	}
	jitter->target_delay_ms = (uint8_t)Voice_JitterClampTarget(next_target);
	jitter->current_talkspurt = talkspurt;
	jitter->have_talkspurt = 1;
}

static void Voice_JitterMarkImpairment(voice_jitter_t *jitter)
{
	if (jitter->impairment_score < 4)
		++jitter->impairment_score;
}

voice_jitter_status_t Voice_JitterInsert(voice_jitter_t *jitter,
	const voice_jitter_packet_t *packet, uint32_t now_ms)
{
	unsigned int insert_at;
	unsigned int i;
	int new_talkspurt = 0;

	if (!jitter || !packet || (!packet->payload && packet->payload_size != 0) ||
		packet->payload_size > VOICE_JITTER_MAX_PAYLOAD || jitter->capacity == 0)
		return VOICE_JITTER_INVALID_ARGUMENT;

	Voice_JitterMaybeResetStale(jitter, now_ms);

	if (jitter->have_talkspurt) {
		if (packet->talkspurt != jitter->current_talkspurt &&
			(!jitter->have_highest ||
			Voice_JitterSequenceIsNewer(packet->sequence, jitter->highest_sequence)))
			new_talkspurt = 1;
		if (!new_talkspurt && jitter->playout_started &&
			Voice_JitterSequenceCompare(packet->sequence, jitter->expected_sequence) < 0)
			return VOICE_JITTER_TOO_OLD;
	} else {
		new_talkspurt = 1;
	}

	if (new_talkspurt)
		Voice_JitterBeginTalkspurt(jitter, packet->talkspurt);

	for (i = 0; i < jitter->count; ++i)
		if (jitter->entries[i].sequence == packet->sequence)
			return VOICE_JITTER_DUPLICATE;

	if (jitter->count >= jitter->capacity) {
		Voice_JitterMarkImpairment(jitter);
		return VOICE_JITTER_FULL;
	}

	insert_at = jitter->count;
	for (i = 0; i < jitter->count; ++i)
		if (Voice_JitterSequenceCompare(packet->sequence, jitter->entries[i].sequence) < 0) {
			insert_at = i;
			Voice_JitterMarkImpairment(jitter);
			break;
		}

	for (i = jitter->count; i > insert_at; --i)
		jitter->entries[i] = jitter->entries[i - 1];

	jitter->entries[insert_at].sequence = packet->sequence;
	jitter->entries[insert_at].timestamp = packet->timestamp;
	jitter->entries[insert_at].arrival_ms = now_ms;
	jitter->entries[insert_at].talkspurt = packet->talkspurt;
	jitter->entries[insert_at].flags = packet->flags;
	jitter->entries[insert_at].payload_size = (uint16_t)packet->payload_size;
	if (packet->payload_size != 0)
		memcpy(jitter->entries[insert_at].payload, packet->payload, packet->payload_size);
	jitter->count++;
	jitter->last_activity_ms = now_ms;

	if (!jitter->have_highest ||
		Voice_JitterSequenceIsNewer(packet->sequence, jitter->highest_sequence)) {
		jitter->have_highest = 1;
		jitter->highest_sequence = packet->sequence;
	}

	return VOICE_JITTER_OK;
}

voice_jitter_status_t Voice_JitterNextFrame(voice_jitter_t *jitter,
	uint32_t now_ms, voice_jitter_frame_t *frame)
{
	voice_jitter_entry_t entry;

	if (!jitter || !frame)
		return VOICE_JITTER_INVALID_ARGUMENT;

	memset(frame, 0, sizeof(*frame));
	frame->action = VOICE_JITTER_WAIT;
	frame->target_delay_ms = jitter->target_delay_ms;
	frame->queued_packets = jitter->count;

	Voice_JitterMaybeResetStale(jitter, now_ms);
	frame->target_delay_ms = jitter->target_delay_ms;
	frame->queued_packets = jitter->count;
	if (!jitter->have_talkspurt)
		return VOICE_JITTER_OK;

	if (!jitter->playout_started) {
		if (jitter->count == 0)
			return VOICE_JITTER_OK;
		jitter->expected_sequence = jitter->entries[0].sequence;
		jitter->next_deadline_ms = jitter->entries[0].arrival_ms + jitter->target_delay_ms;
	}

	frame->deadline_ms = jitter->next_deadline_ms;
	if ((uint32_t)(now_ms - jitter->next_deadline_ms) >= 0x80000000u)
		return VOICE_JITTER_OK;
	/* Keep startup reorderable until the first playout deadline is reached. */
	jitter->playout_started = 1;

	if (jitter->count != 0 &&
		jitter->entries[0].sequence == jitter->expected_sequence) {
		entry = jitter->entries[0];
		for (frame->queued_packets = 1; frame->queued_packets < jitter->count;
			++frame->queued_packets)
			jitter->entries[frame->queued_packets - 1] =
				jitter->entries[frame->queued_packets];
		jitter->count--;
		frame->queued_packets = jitter->count;
		frame->action = VOICE_JITTER_PACKET;
		frame->sequence = entry.sequence;
		frame->timestamp = entry.timestamp;
		frame->talkspurt = entry.talkspurt;
		frame->flags = entry.flags;
		frame->payload_size = entry.payload_size;
		if (entry.payload_size != 0)
			memcpy(frame->payload, entry.payload, entry.payload_size);
	} else {
		frame->action = VOICE_JITTER_PLC;
		frame->sequence = jitter->expected_sequence;
		frame->talkspurt = jitter->current_talkspurt;
		Voice_JitterMarkImpairment(jitter);
	}

	jitter->expected_sequence = (uint16_t)(jitter->expected_sequence + 1u);
	jitter->next_deadline_ms += VOICE_JITTER_FRAME_MS;
	return VOICE_JITTER_OK;
}
