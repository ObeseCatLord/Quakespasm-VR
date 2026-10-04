/* Existing entity writer and delta decoder, without an engine window or
 * invented wire reader. Link using a prepared assertion-enabled native graph. */
#define main ImportedSolidNegotiationMain
#include "negotiation_native_fixture.c"
#undef main

static unsigned RoundTrip (unsigned solid, unsigned layout, int *tag)
{
	byte bytes[128];
	sizebuf_t message = {0};
	entity_state_t written = {0}, old = {0}, baseline = {0}, read = {0};
	message.data = bytes;
	message.maxsize = sizeof (bytes);
	written.solidsize = solid;
	MSGFTE_WriteEntityUpdate (UF_SOLID, &written, &message,
		PEXT2_REPLACEMENTDELTAS, 0, layout != 0);
	MSG_WriteByte (&message, 0x5a);
	cl.protocol_qsvr = layout;
	cl.protocol_pext2 = PEXT2_REPLACEMENTDELTAS;
	net_message = message;
	MSG_BeginReading ();
	CLFTE_ReadDelta (1, &read, &old, &baseline);
	assert (!msg_badread && MSG_ReadByte () == 0x5a &&
		msg_readcount == message.cursize);
	if (tag) *tag = bytes[2];
	return read.solidsize;
}

int main (void)
{
	const int heights[] = {-40, -32, 472, 480, 1832};
	for (unsigned i = 0; i < countof (heights); ++i)
	{
		unsigned solid = 24 | (24 << 8) | ((unsigned)(heights[i] + 32768) << 16);
		int tag;
		assert (RoundTrip (solid, QSVR_PROTOCOL_PINNED, &tag) == solid);
		assert (tag == (heights[i] < -32 || heights[i] > 472 ? 32 : 16));
		unsigned compact = 24 | (24 << 8) |
			((unsigned)(CLAMP (-32, heights[i], 472) + 32768) << 16);
		assert (RoundTrip (solid, 0, NULL) == compact);
	}
	const unsigned exact[] = {ES_SOLID_NOT, ES_SOLID_BSP, ES_SOLID_HULL1,
		ES_SOLID_HULL2, 0x80211911, 0x81d8f8f8, 0x8020ffff};
	for (unsigned i = 0; i < countof (exact); ++i)
		assert (RoundTrip (exact[i], QSVR_PROTOCOL_PINNED, NULL) == exact[i]);
	assert (RoundTrip (ES_SOLID_NOT, 0, NULL) == ES_SOLID_NOT);
	assert (RoundTrip (ES_SOLID_BSP, 0, NULL) == ES_SOLID_BSP);
	assert (RoundTrip (ES_SOLID_HULL1, 0, NULL) == ES_SOLID_HULL1);
	assert (RoundTrip (ES_SOLID_HULL2, 0, NULL) == ES_SOLID_HULL2);
	puts ("SOLID_SIZE_ROUNDTRIP_PASSED actual writer/decoder; compact limits, private extended dimensions, unaligned bytes and sentinels");
	return 0;
}
