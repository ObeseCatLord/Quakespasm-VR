/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2026 Hiina

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#ifndef VOICE_PROTOCOL_H
#define VOICE_PROTOCOL_H

#include <stdint.h>

#define VOICE_PROTOCOL_VERSION 1

#define VOICE_SAMPLE_RATE 48000
#define VOICE_FRAME_MILLISECONDS 20
#define VOICE_FRAME_SAMPLES 960

/* A 20 ms Opus packet at the configured voice bitrate is normally much
 * smaller.  This limit leaves ample VBR headroom while keeping every queue
 * and parser allocation fixed-size. */
#define VOICE_MAX_PAYLOAD 400

#define VOICE_FLAG_START 0x01u
#define VOICE_FLAG_END 0x02u
/* Reserved for a future controller-near-head long-range radio gesture. */
#define VOICE_FLAG_RADIO 0x04u
#define VOICE_FLAG_KNOWN (VOICE_FLAG_START | VOICE_FLAG_END | VOICE_FLAG_RADIO)

typedef struct voice_packet_s
{
	uint16_t sequence;
	uint32_t timestamp;
	uint8_t talkspurt;
	uint8_t flags;
	uint16_t payload_bytes;
	uint8_t payload[VOICE_MAX_PAYLOAD];
} voice_packet_t;

#define VOICE_CLC_HEADER_BYTES (1 + 2 + 4 + 1 + 1 + 2)
#define VOICE_SVC_HEADER_BYTES (1 + 1 + 4 + 2 + 4 + 1 + 1 + 2)

#endif /* VOICE_PROTOCOL_H */
