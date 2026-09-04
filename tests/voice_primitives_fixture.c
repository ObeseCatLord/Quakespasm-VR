#include "../Quake/voice_jitter.h"
#include "../Quake/voice_vad.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void Expect(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "voice_primitives_fixture: %s\n", message);
		++failures;
	}
}

static void ExpectStatus(voice_jitter_status_t actual,
	voice_jitter_status_t expected, const char *message)
{
	if (actual != expected) {
		fprintf(stderr, "voice_primitives_fixture: %s (got %d expected %d)\n",
			message, (int)actual, (int)expected);
		++failures;
	}
}

static void FillFrame(int16_t *frame, int level)
{
	size_t i;
	for (i = 0; i < VOICE_VAD_FRAME_SAMPLES; ++i)
		frame[i] = (int16_t)level;
}

static void TestVADGatePrerollAndHangover(void)
{
	voice_vad_t vad;
	voice_vad_config_t config;
	voice_vad_result_t result;
	int16_t quiet[VOICE_VAD_FRAME_SAMPLES];
	int16_t speech[VOICE_VAD_FRAME_SAMPLES];
	unsigned int i;

	Voice_VADConfigDefault(&config);
	config.sensitivity = 80;
	Voice_VADInit(&vad, &config);
	FillFrame(quiet, 20);
	FillFrame(speech, 1600);

	for (i = 0; i < 3; ++i) {
		assert(Voice_VADProcessFrame(&vad, quiet, VOICE_VAD_FRAME_SAMPLES, &result));
		Expect(!result.active, "quiet frames must stay closed");
	}

	assert(Voice_VADProcessFrame(&vad, speech, VOICE_VAD_FRAME_SAMPLES, &result));
	Expect(result.gate == VOICE_VAD_GATE_OPENING,
		"first speech frame should enter opening hysteresis");
	Expect(!result.active, "gate should still be closed during first loud frame");

	assert(Voice_VADProcessFrame(&vad, speech, VOICE_VAD_FRAME_SAMPLES, &result));
	Expect(result.active && result.opened, "second loud frame should open gate");
	Expect(result.preroll_frames == 3u, "open must request full 60 ms preroll");
	Expect(result.hangover_frames == VOICE_VAD_HANGOVER_FRAMES,
		"open should refresh hangover");
	Expect(result.meter > result.noise_floor,
		"speech meter should rise above the tracked floor");

	for (i = 0; i < VOICE_VAD_HANGOVER_FRAMES - 1; ++i) {
		assert(Voice_VADProcessFrame(&vad, quiet, VOICE_VAD_FRAME_SAMPLES, &result));
		Expect(result.active, "hangover should keep gate active through quiet tail");
	}

	assert(Voice_VADProcessFrame(&vad, quiet, VOICE_VAD_FRAME_SAMPLES, &result));
	Expect(!result.active && result.closed, "gate must close after hangover expires");
}

static voice_jitter_packet_t Packet(uint16_t sequence, uint32_t timestamp,
	uint8_t talkspurt, uint8_t value)
{
	static uint8_t payloads[32][4];
	static unsigned int index;
	voice_jitter_packet_t packet;

	index = (index + 1u) % 32u;
	payloads[index][0] = value;
	payloads[index][1] = (uint8_t)(value + 1u);
	payloads[index][2] = (uint8_t)(value + 2u);
	payloads[index][3] = (uint8_t)(value + 3u);
	packet.sequence = sequence;
	packet.timestamp = timestamp;
	packet.talkspurt = talkspurt;
	packet.flags = (uint8_t)(value ^ 0x5a);
	packet.payload = payloads[index];
	packet.payload_size = sizeof(payloads[index]);
	return packet;
}

static void InsertPacket(voice_jitter_t *jitter, uint32_t now_ms,
	uint16_t sequence, uint32_t timestamp, uint8_t talkspurt, uint8_t value,
	voice_jitter_status_t expected, const char *message)
{
	voice_jitter_packet_t packet = Packet(sequence, timestamp, talkspurt, value);
	ExpectStatus(Voice_JitterInsert(jitter, &packet, now_ms), expected, message);
}

static void PullPacket(voice_jitter_t *jitter, uint32_t now_ms,
	uint16_t sequence, uint8_t value)
{
	voice_jitter_frame_t frame;
	ExpectStatus(Voice_JitterNextFrame(jitter, now_ms, &frame), VOICE_JITTER_OK,
		"next frame should succeed");
	Expect(frame.action == VOICE_JITTER_PACKET, "expected buffered packet");
	Expect(frame.sequence == sequence, "packet sequence mismatch");
	Expect(frame.payload_size == 4u && frame.payload[0] == value,
		"packet payload mismatch");
}

static void PullPLC(voice_jitter_t *jitter, uint32_t now_ms, uint16_t sequence)
{
	voice_jitter_frame_t frame;
	ExpectStatus(Voice_JitterNextFrame(jitter, now_ms, &frame), VOICE_JITTER_OK,
		"PLC decision should succeed");
	Expect(frame.action == VOICE_JITTER_PLC, "expected PLC frame");
	Expect(frame.sequence == sequence, "PLC sequence mismatch");
}

static void TestJitterReorderDuplicateLossAndWrap(void)
{
	voice_jitter_t jitter;
	voice_jitter_frame_t frame;

	Voice_JitterInit(&jitter, 4);
	InsertPacket(&jitter, 0u, 101u, 1010u, 1u, 0x31, VOICE_JITTER_OK,
		"insert base packet");
	InsertPacket(&jitter, 5u, 100u, 1000u, 1u, 0x30, VOICE_JITTER_OK,
		"insert reordered older packet");
	InsertPacket(&jitter, 6u, 100u, 1000u, 1u, 0x30, VOICE_JITTER_DUPLICATE,
		"duplicate packet must be rejected");
	ExpectStatus(Voice_JitterNextFrame(&jitter, 59u, &frame), VOICE_JITTER_OK,
		"before deadline should wait");
	Expect(frame.action == VOICE_JITTER_WAIT && frame.deadline_ms == 65u,
		"first deadline must be arrival plus target delay");
	PullPacket(&jitter, 65u, 100u, 0x30);
	PullPacket(&jitter, 85u, 101u, 0x31);

	InsertPacket(&jitter, 90u, 103u, 1030u, 1u, 0x33, VOICE_JITTER_OK,
		"insert packet after a gap");
	PullPLC(&jitter, 105u, 102u);
	PullPacket(&jitter, 125u, 103u, 0x33);

	Voice_JitterInit(&jitter, 4);
	InsertPacket(&jitter, 0u, 65535u, 5000u, 9u, 0xa0, VOICE_JITTER_OK,
		"insert wrap packet");
	InsertPacket(&jitter, 1u, 0u, 5960u, 9u, 0xa1, VOICE_JITTER_OK,
		"insert wrapped successor");
	PullPacket(&jitter, 60u, 65535u, 0xa0);
	PullPacket(&jitter, 80u, 0u, 0xa1);
}

static void TestJitterTalkspurtOverflowAndStaleReset(void)
{
	voice_jitter_t jitter;
	voice_jitter_frame_t frame;

	Voice_JitterInit(&jitter, 2);
	InsertPacket(&jitter, 0u, 10u, 10u, 1u, 0x10, VOICE_JITTER_OK,
		"first packet should insert");
	InsertPacket(&jitter, 1u, 11u, 970u, 1u, 0x11, VOICE_JITTER_OK,
		"second packet should insert");
	InsertPacket(&jitter, 2u, 12u, 1930u, 1u, 0x12, VOICE_JITTER_FULL,
		"fixed-capacity queue should reject overflow");
	PullPacket(&jitter, 60u, 10u, 0x10);
	PullPacket(&jitter, 80u, 11u, 0x11);

	InsertPacket(&jitter, 100u, 20u, 20u, 2u, 0x20, VOICE_JITTER_OK,
		"new talkspurt should reset playout base");
	ExpectStatus(Voice_JitterNextFrame(&jitter, 159u, &frame), VOICE_JITTER_OK,
		"new talkspurt should wait for its own delay");
	Expect(frame.action == VOICE_JITTER_WAIT && frame.target_delay_ms == 80u,
		"impairment score should raise next talkspurt target delay");
	PullPacket(&jitter, 180u, 20u, 0x20);

	InsertPacket(&jitter, 600u, 30u, 30u, 2u, 0x30, VOICE_JITTER_OK,
		"stale idle state should accept fresh packet");
	ExpectStatus(Voice_JitterNextFrame(&jitter, 659u, &frame), VOICE_JITTER_OK,
		"fresh talkspurt should arm a new deadline");
	Expect(frame.action == VOICE_JITTER_WAIT && frame.deadline_ms == 660u,
		"stale reset should restart timing from fresh arrival");
	PullPacket(&jitter, 660u, 30u, 0x30);
}

int main(void)
{
	TestVADGatePrerollAndHangover();
	TestJitterReorderDuplicateLossAndWrap();
	TestJitterTalkspurtOverflowAndStaleReset();

	if (failures != 0) {
		fprintf(stderr, "voice_primitives_fixture: %d failure(s)\n", failures);
		return 1;
	}

	return 0;
}
