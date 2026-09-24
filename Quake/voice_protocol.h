/*
 * Optional Opus voice wire format shared by the client and server.
 * Audio encoding and playback are supplied by a separate worker.
 */
#ifndef VOICE_PROTOCOL_H
#define VOICE_PROTOCOL_H

#include <stdint.h>

#define VOICE_PROTOCOL_VERSION 1
#define VOICE_SAMPLE_RATE 48000
#define VOICE_FRAME_MILLISECONDS 20
#define VOICE_FRAME_SAMPLES 960
#define VOICE_MAX_PAYLOAD 400
#define VOICE_CLIENT_QUEUE_CAPACITY 4
#define VOICE_CLIENT_DATAGRAM_BUDGET 512
#define VOICE_CLIENT_MAX_PACKETS_PER_DATAGRAM 16
#define VOICE_SERVER_QUEUE_CAPACITY 32
#define VOICE_SERVER_DATAGRAM_BUDGET 900
#define VOICE_SERVER_MAX_PACKETS_PER_SECOND 75u
#define VOICE_SERVER_MAX_BYTES_PER_SECOND 16000u
#define VOICE_SERVER_MAX_PACKET_AGE 0.25
#define VOICE_SERVER_PACKETS_PER_SOURCE_TICK 3
#define VOICE_SERVER_MAX_PACKETS_PER_DATAGRAM 4

#define VOICE_FLAG_START 0x01u
#define VOICE_FLAG_END 0x02u
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

/* source_slot is zero-based; packet storage is valid only during the call. */
typedef void (*voice_receive_callback_t)(int source_slot,
	uint32_t generation, const voice_packet_t *packet, void *opaque);
/* Receive callbacks run synchronously on the parser thread; copy queued data. */

#define VOICE_CLC_HEADER_BYTES (1 + 2 + 4 + 1 + 1 + 2)
#define VOICE_SVC_HEADER_BYTES (1 + 1 + 4 + 2 + 4 + 1 + 1 + 2)

static inline int Voice_PacketIsValid(const voice_packet_t *packet)
{
	return packet && packet->payload_bytes <= VOICE_MAX_PAYLOAD &&
		!(packet->flags & ~VOICE_FLAG_KNOWN) &&
		(packet->payload_bytes || (packet->flags & VOICE_FLAG_END));
}

#endif /* VOICE_PROTOCOL_H */
