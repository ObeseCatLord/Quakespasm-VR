/* Offline check of terrain_ice.c: cc -O1 -g -std=gnu11 -IQuake tests/ice_fixture.c Quake/terrain.c Quake/terrain_ice.c -lm */
#include "terrain.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static terrain_op_t Op (int kind, float x, float y, float z, float r)
{
	terrain_op_t op;
	memset (&op, 0, sizeof op);
	op.kind = kind; op.material = 3;
	op.radius = (uint16_t)(r * 2); // units -> SUB
	op.center[0] = (int)(x * 2); op.center[1] = (int)(y * 2); op.center[2] = (int)(z * 2);
	return op;
}
int main (void)
{
	int32_t org[3] = {0, 0, 0}, size[3] = {2, 2, 2};
	terrain_t *t = Terrain_Create (org, size, 1);
	terrain_op_t log[200]; int n = 0;
	log[n] = Op (TERRAIN_OP_DIG, 512, 512, 512, 300); Terrain_ApplyOp (t, &log[n++]); // room
	float ceil_z = 512 + 280;
	for (float dz = -24; dz <= 280; dz += 10) {
		float tt = dz / 280, r;
		if (tt < 0) r = 22; else if (tt < 0.1f) r = 16; else if (tt < 0.55f) r = 16 + 18 * sinf ((tt - 0.1f) / 0.45f * 1.5708f); else r = 34 - 28 * powf ((tt - 0.55f) / 0.45f, 1.2f);
		log[n] = Op (TERRAIN_OP_FILL, 512, 512, ceil_z - dz, r < 8 ? 8 : r); Terrain_ApplyOp (t, &log[n++]);
	}
	printf ("mid field %.1f\n", Terrain_SampleWorld (t, (float[3]){512, 512, ceil_z - 100}));
	uint64_t h0 = Terrain_Hash (t);
	log[n] = Op (TERRAIN_OP_DIG, 512, 512, ceil_z - 10, 40);
	printf ("dig changed %d\n", Terrain_ApplyOp (t, &log[n++]));
	terrain_op_t found[8];
	int nf = Terrain_IceFindIslands (t, &log[n - 1], found, 8);
	printf ("islands found %d\n", nf);
	for (int i = 0; i < nf; ++i) {
		printf ("seed %d %d %d\n", found[i].center[0], found[i].center[1], found[i].center[2]);
		int c = Terrain_ApplyOp (t, &found[i]);
		log[n++] = found[i];
		const terrain_brick_t *b = Terrain_Brick (t, Terrain_IceLastBrick (t));
		printf ("detach changed %d brick %d", c, Terrain_IceLastBrick (t));
		if (b) printf (" dim %d %d %d mass %.1f com %.1f %.1f %.1f r %.0f", b->dim[0], b->dim[1], b->dim[2], b->mass, b->com[0], b->com[1], b->com[2], b->radius);
		printf ("\nmid field after %.1f\n", Terrain_SampleWorld (t, (float[3]){512, 512, ceil_z - 100}));
	}
	// stamp back at the original pose
	const terrain_brick_t *b = Terrain_Brick (t, 0);
	if (b) {
		float q[4] = {1, 0, 0, 0};
		terrain_op_t s; memset (&s, 0, sizeof s); s.kind = TERRAIN_OP_STAMP; s.radius = 0; s.aux = Terrain_PackQuat (q);
		for (int a = 0; a < 3; ++a) s.center[a] = (int)floorf (((float)b->lat_min[a] * 16 + b->com[a]) * 2 + 0.5f);
		int c = Terrain_ApplyOp (t, &s); log[n++] = s;
		printf ("stamp changed %d mid %.1f hash same as before cut: %d\n", c, Terrain_SampleWorld (t, (float[3]){512, 512, ceil_z - 100}), Terrain_Hash (t) == h0);
		// a second copy lying tilted on the floor: a pose whose packed quaternion uses all its bits
		float q2[4] = {cosf (0.6f), sinf (0.6f) * 0.6f, sinf (0.6f) * 0.8f, 0.0f};
		terrain_op_t s2 = s; s2.aux = Terrain_PackQuat (q2);
		s2.center[0] += 2 * 120; s2.center[2] = 2 * (512 - 200);
		c = Terrain_ApplyOp (t, &s2); log[n++] = s2;
		printf ("tilted stamp changed %d aux %08x\n", c, s2.aux);
	}
	// replay
	terrain_t *r = Terrain_Create (org, size, 1);
	for (int i = 0; i < n; ++i) Terrain_ApplyOp (r, &log[i]);
	printf ("replay %s\n", Terrain_Hash (r) == Terrain_Hash (t) ? "MATCH" : "MISMATCH");
	// the same log through the netsync wire form (Terrain_OpPack / Terrain_OpUnpack), as a client replays it
	terrain_t *w = Terrain_Create (org, size, 1);
	int same = 1;
	for (int i = 0; i < n; ++i) {
		uint8_t packed[TERRAIN_OP_BYTES]; terrain_op_t back;
		Terrain_OpPack (&log[i], packed); Terrain_OpUnpack (packed, &back);
		same &= back.kind == log[i].kind && back.radius == log[i].radius && back.aux == (log[i].kind == TERRAIN_OP_STAMP ? log[i].aux : 0u) &&
				!memcmp (back.center, log[i].center, sizeof back.center);
		Terrain_ApplyOp (w, &back);
	}
	printf ("wire replay %s, ops round-trip %s, %d bricks\n", Terrain_Hash (w) == Terrain_Hash (t) ? "MATCH" : "MISMATCH", same ? "exact" : "CHANGED", Terrain_BrickCount (w));
	const int ok = Terrain_Hash (r) == Terrain_Hash (t) && Terrain_Hash (w) == Terrain_Hash (t) && same;
	puts (ok ? "ICE_FIXTURE_PASSED" : "ICE_FIXTURE_FAILED");
	return !ok;
}
