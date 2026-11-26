/*
 * Falling ice: field-level half. Engine-independent (terrain.h, libc), so the offline fixture can
 * replay it. GPL-2.0-or-later, as the rest of the engine.
 *
 * DETACH: a bounded flood fill of solid samples from a seed. A component that does not reach the cap
 * or the edge of the fill box is an island, cut off from the cave. Its samples are copied into a
 * brick (a small field of its own), the island is set to air in the world field and the distances
 * around it are recomputed. STAMP is the reverse: the brick is written back at a quantized pose
 * (union with the field, like a fill). Both are plain integer-parameter ops, so the server's op log
 * replays them exactly; the brick table is rebuilt by replaying the DETACH ops in order.
 */
#include "terrain.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ICE_H_APPLY	 28 // half extent of the box a DETACH floods in; an island cannot be larger than 2*H+1 samples
#define ICE_H_DETECT 40 // box of the detection pass after a dig
#define ICE_BOX_MAX	 (2 * ICE_H_DETECT + 1)
#define ICE_CAP_MAX	 8192
#define ICE_MARGIN	 3	// air samples kept round the island in its brick
#define ICE_MIN_CELLS 3 // smaller islands just vanish
#define ICE_REDIST	 5	// samples beyond the island whose distances are recomputed

typedef struct
{
	terrain_brick_t **bricks;
	int				  num, cap, last;
} ice_state_t;

static ice_state_t *State (terrain_t *t, int create)
{
	if (!t->ice && create)
	{
		t->ice = calloc (1, sizeof (ice_state_t));
		((ice_state_t *)t->ice)->last = -1;
	}
	return (ice_state_t *)t->ice;
}

void Terrain_IceFree (terrain_t *t)
{
	ice_state_t *s = (ice_state_t *)t->ice;
	if (!s)
		return;
	for (int i = 0; i < s->num; ++i)
	{
		Terrain_Free (s->bricks[i]->field);
		free (s->bricks[i]);
	}
	free (s->bricks);
	free (s);
	t->ice = NULL;
}

const terrain_brick_t *Terrain_Brick (const terrain_t *t, int id)
{
	const ice_state_t *s = (const ice_state_t *)t->ice;
	return s && id >= 0 && id < s->num ? s->bricks[id] : NULL;
}

int Terrain_BrickCount (const terrain_t *t)
{
	const ice_state_t *s = (const ice_state_t *)t->ice;
	return s ? s->num : 0;
}

int Terrain_IceLastBrick (const terrain_t *t)
{
	const ice_state_t *s = (const ice_state_t *)t->ice;
	return s ? s->last : -1;
}

/* ---- quaternions ---- */

uint32_t Terrain_PackQuat (const float q[4])
{
	int	  big = 0;
	float a[4];
	for (int i = 0; i < 4; ++i)
	{
		a[i] = q[i];
		if (fabsf (q[i]) > fabsf (q[big]))
			big = i;
	}
	const float sign = q[big] < 0.0f ? -1.0f : 1.0f;
	uint32_t	out = (uint32_t)big << 30;
	int			slot = 0;
	for (int i = 0; i < 4; ++i)
	{
		if (i == big)
			continue;
		float v = a[i] * sign * (1.0f / 0.70710678f);
		v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
		const int n = (int)floorf (v * 511.0f + 0.5f) + 512; // 1..1023
		out |= (uint32_t)n << (10 * slot++);
	}
	return out;
}

void Terrain_UnpackQuat (uint32_t v, float q[4])
{
	const int big = (int)(v >> 30);
	float	  sum = 0.0f;
	int		  slot = 0;
	for (int i = 0; i < 4; ++i)
	{
		if (i == big)
			continue;
		const int n = (int)((v >> (10 * slot++)) & 1023u) - 512;
		q[i] = (float)n / 511.0f * 0.70710678f;
		sum += q[i] * q[i];
	}
	q[big] = sqrtf (sum < 1.0f ? 1.0f - sum : 0.0f);
	float len = 0.0f;
	for (int i = 0; i < 4; ++i)
		len += q[i] * q[i];
	len = 1.0f / sqrtf (len);
	for (int i = 0; i < 4; ++i)
		q[i] *= len;
}

void Terrain_QuatMatrix (const float q[4], float m[9])
{
	const float w = q[0], x = q[1], y = q[2], z = q[3];
	m[0] = 1 - 2 * (y * y + z * z);
	m[1] = 2 * (x * y - w * z);
	m[2] = 2 * (x * z + w * y);
	m[3] = 2 * (x * y + w * z);
	m[4] = 1 - 2 * (x * x + z * z);
	m[5] = 2 * (y * z - w * x);
	m[6] = 2 * (x * z - w * y);
	m[7] = 2 * (y * z + w * x);
	m[8] = 1 - 2 * (x * x + y * y);
}

/* ---- flood fill ---- */

typedef struct
{
	int16_t p[3];
} cell3;

static uint32_t flood_stamp[ICE_BOX_MAX * ICE_BOX_MAX * ICE_BOX_MAX];
static uint32_t flood_gen;
static cell3	flood_queue[ICE_CAP_MAX + 8];
static int		flood_box_dim, flood_box_lo[3];

static int BoxIndex (const int p[3])
{
	int q[3];
	for (int a = 0; a < 3; ++a)
	{
		q[a] = p[a] - flood_box_lo[a];
		if (q[a] < 0 || q[a] >= flood_box_dim)
			return -1;
	}
	return (q[2] * flood_box_dim + q[1]) * flood_box_dim + q[0];
}

static int Solid (const terrain_t *t, const int p[3])
{
	return Terrain_Sample (t, p[0], p[1], p[2], NULL) < 0;
}

static const int nbr[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

/*
Floods the solid samples joined to seed inside the box [lo, lo + 2*half]. Returns the sample count
(queued in flood_queue, stamped `cur`) when the component is whole, or 0 when it reaches the cap, the
box edge or the region edge. A stamp equal to `conn` marks samples known to be joined to the world.
*/
static int FloodSolid (const terrain_t *t, const int seed[3], const int lo[3], int half, int cap, uint32_t cur, uint32_t conn)
{
	flood_box_dim = 2 * half + 1;
	for (int a = 0; a < 3; ++a)
		flood_box_lo[a] = lo[a];
	int si = BoxIndex (seed);
	if (si < 0 || !Solid (t, seed) || flood_stamp[si] == conn)
		return 0;
	int head = 0, tail = 0, escaped = 0;
	for (int a = 0; a < 3; ++a)
		flood_queue[0].p[a] = (int16_t)seed[a];
	tail = 1;
	flood_stamp[si] = cur;
	while (head < tail && !escaped)
	{
		int c[3] = {flood_queue[head].p[0], flood_queue[head].p[1], flood_queue[head].p[2]};
		++head;
		for (int k = 0; k < 6; ++k)
		{
			const int n[3] = {c[0] + nbr[k][0], c[1] + nbr[k][1], c[2] + nbr[k][2]};
			if (!Solid (t, n))
				continue;
			const int ni = BoxIndex (n);
			if (ni < 0 || flood_stamp[ni] == conn || tail >= cap)
			{
				escaped = 1;
				break;
			}
			if (flood_stamp[ni] == cur)
				continue;
			for (int a = 0; a < 3; ++a)
				flood_queue[tail].p[a] = (int16_t)n[a];
			++tail;
			flood_stamp[ni] = cur;
		}
	}
	if (escaped)
	{
		for (int i = 0; i < tail; ++i)
		{
			const int p[3] = {flood_queue[i].p[0], flood_queue[i].p[1], flood_queue[i].p[2]};
			flood_stamp[BoxIndex (p)] = conn;
		}
		return 0;
	}
	return tail;
}

/*
The ops a dig may have made necessary. Seeds are the solid samples that touch the dug sphere; each
is flooded once, and samples reached by an earlier flood are skipped.
*/
int Terrain_IceFindIslands (const terrain_t *t, const terrain_op_t *dig, terrain_op_t *out, int max_out)
{
	if (dig->kind != TERRAIN_OP_DIG)
		return 0;
	const float c[3] = {(float)dig->center[0] / TERRAIN_SUB, (float)dig->center[1] / TERRAIN_SUB, (float)dig->center[2] / TERRAIN_SUB};
	const float r = (float)dig->radius / TERRAIN_SUB;
	if (r > 10.0f)
		return 0;
	int lo[3], found = 0, work = 0;
	for (int a = 0; a < 3; ++a)
		lo[a] = (int)floorf (c[a] + 0.5f) - ICE_H_DETECT;
	flood_gen += 2;
	const uint32_t cur = flood_gen, conn = flood_gen + 1;
	flood_box_dim = ICE_BOX_MAX;
	const float outer = r + 2.2f;
	const int	reach = (int)ceilf (outer);
	int			ctr[3];
	for (int a = 0; a < 3; ++a)
		ctr[a] = (int)floorf (c[a] + 0.5f);
	for (int z = ctr[2] - reach; z <= ctr[2] + reach && found < max_out && work < 60000; ++z)
		for (int y = ctr[1] - reach; y <= ctr[1] + reach && found < max_out && work < 60000; ++y)
			for (int x = ctr[0] - reach; x <= ctr[0] + reach && found < max_out && work < 60000; ++x)
			{
				const float d[3] = {(float)x - c[0], (float)y - c[1], (float)z - c[2]};
				const float dist = sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
				if (dist < r - 0.5f || dist > outer)
					continue;
				const int p[3] = {x, y, z};
				if (!Solid (t, p))
					continue;
				int surface = 0;
				for (int k = 0; k < 6 && !surface; ++k)
				{
					const int n[3] = {x + nbr[k][0], y + nbr[k][1], z + nbr[k][2]};
					surface = !Solid (t, n);
				}
				if (!surface)
					continue;
				flood_box_dim = ICE_BOX_MAX;
				for (int a = 0; a < 3; ++a)
					flood_box_lo[a] = lo[a];
				const int bi = BoxIndex (p);
				if (bi >= 0 && (flood_stamp[bi] == cur || flood_stamp[bi] == conn))
					continue;
				const int n = FloodSolid (t, p, lo, ICE_H_DETECT, ICE_CAP_MAX < 4096 ? ICE_CAP_MAX : 4096, cur, conn);
				work += n ? n : 4096;
				if (n)
				{
					terrain_op_t *op = &out[found++];
					memset (op, 0, sizeof (*op));
					op->kind = TERRAIN_OP_DETACH;
					op->radius = 4096;
					for (int a = 0; a < 3; ++a)
						op->center[a] = p[a];
				}
			}
	return found;
}

/* ---- brick ---- */

static terrain_brick_t *BuildBrick (const terrain_t *t, int n_cells, const int lo[3], const int hi[3], uint32_t gen)
{
	terrain_brick_t *b = (terrain_brick_t *)calloc (1, sizeof (*b));
	int				 nc[3];
	for (int a = 0; a < 3; ++a)
	{
		b->lat_min[a] = lo[a] - ICE_MARGIN;
		b->dim[a] = hi[a] - lo[a] + 1 + 2 * ICE_MARGIN;
		nc[a] = (b->dim[a] + TERRAIN_CHUNK - 1) / TERRAIN_CHUNK;
	}
	const int32_t origin[3] = {0, 0, 0};
	b->field = Terrain_Create (origin, nc, 0);
	for (int i = 0; i < nc[0] * nc[1] * nc[2]; ++i)
	{
		terrain_chunk_t *c = &b->field->chunks[i];
		c->sdf = (int8_t *)malloc (TERRAIN_CHUNK_CELLS);
		c->mat = (uint8_t *)calloc (1, TERRAIN_CHUNK_CELLS);
		memset (c->sdf, TERRAIN_SDF_MAX, TERRAIN_CHUNK_CELLS);
	}
	double sum = 0, mom[3] = {0, 0, 0};
	float  bmin[3] = {1e9f, 1e9f, 1e9f}, bmax[3] = {-1e9f, -1e9f, -1e9f};
	for (int k = 0; k < b->dim[2]; ++k)
		for (int j = 0; j < b->dim[1]; ++j)
			for (int i = 0; i < b->dim[0]; ++i)
			{
				const int w[3] = {b->lat_min[0] + i, b->lat_min[1] + j, b->lat_min[2] + k};
				uint8_t	  mat;
				int		  s = Terrain_Sample (t, w[0], w[1], w[2], &mat);
				const int bi = BoxIndex (w);
				const int island = s < 0 && bi >= 0 && flood_stamp[bi] == gen;
				if (s < 0 && !island)
					s = TERRAIN_SUB / 2; // other rock that happens to be close: not part of this body
				terrain_chunk_t *c = Terrain_Chunk (b->field, i >> TERRAIN_CHUNK_SHIFT, j >> TERRAIN_CHUNK_SHIFT, k >> TERRAIN_CHUNK_SHIFT);
				const int		 ci = (((k & 31) << TERRAIN_CHUNK_SHIFT | (j & 31)) << TERRAIN_CHUNK_SHIFT) | (i & 31);
				c->sdf[ci] = (int8_t)s;
				c->mat[ci] = mat;
				if (s < TERRAIN_SUB / 2)
				{
					float wgt = (float)(TERRAIN_SUB / 2 - s) / (float)TERRAIN_SUB;
					wgt = wgt > 1.0f ? 1.0f : wgt;
					const float p[3] = {(float)i * TERRAIN_CELL, (float)j * TERRAIN_CELL, (float)k * TERRAIN_CELL};
					sum += wgt;
					for (int a = 0; a < 3; ++a)
						mom[a] += wgt * p[a];
				}
				if (island)
				{
					const float p[3] = {(float)i * TERRAIN_CELL, (float)j * TERRAIN_CELL, (float)k * TERRAIN_CELL};
					for (int a = 0; a < 3; ++a)
					{
						bmin[a] = p[a] < bmin[a] ? p[a] : bmin[a];
						bmax[a] = p[a] > bmax[a] ? p[a] : bmax[a];
					}
				}
			}
	b->cells = n_cells;
	b->mass = (float)sum;
	for (int a = 0; a < 3; ++a)
	{
		b->com[a] = (float)(mom[a] / sum);
		b->mins[a] = bmin[a] - TERRAIN_CELL;
		b->maxs[a] = bmax[a] + TERRAIN_CELL;
	}
	double I[9] = {0};
	float  far2 = 0.0f;
	for (int k = 0; k < b->dim[2]; ++k)
		for (int j = 0; j < b->dim[1]; ++j)
			for (int i = 0; i < b->dim[0]; ++i)
			{
				const int s = Terrain_Sample (b->field, i, j, k, NULL);
				if (s >= TERRAIN_SUB / 2)
					continue;
				float wgt = (float)(TERRAIN_SUB / 2 - s) / (float)TERRAIN_SUB;
				wgt = wgt > 1.0f ? 1.0f : wgt;
				const float r[3] = {(float)i * TERRAIN_CELL - b->com[0], (float)j * TERRAIN_CELL - b->com[1], (float)k * TERRAIN_CELL - b->com[2]};
				const float r2 = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
				if (s < 0 && r2 > far2)
					far2 = r2;
				for (int a = 0; a < 3; ++a)
					for (int c = 0; c < 3; ++c)
						I[a * 3 + c] += wgt * ((a == c ? r2 + (float)(TERRAIN_CELL * TERRAIN_CELL) / 6.0f : 0.0f) - r[a] * r[c]);
			}
	for (int i = 0; i < 9; ++i)
		b->inertia[i] = (float)I[i];
	b->radius = sqrtf (far2) + TERRAIN_CELL;
	return b;
}

/*
Recomputes distances round a removed island: a chamfer transform from the rock that is left. Samples
that touch remaining rock keep their value (they were measured against it); the others, and the
island itself, take the distance estimate when it is larger.
*/
static void Redistance (terrain_t *t, const int lo[3], const int hi[3], uint32_t gen)
{
	int blo[3], bhi[3], bd[3];
	for (int a = 0; a < 3; ++a)
	{
		const int n = t->size[a] * TERRAIN_CHUNK;
		blo[a] = lo[a] - ICE_REDIST;
		bhi[a] = hi[a] + ICE_REDIST;
		blo[a] = blo[a] < 1 ? 1 : blo[a];
		bhi[a] = bhi[a] > n - 2 ? n - 2 : bhi[a];
		bd[a] = bhi[a] - blo[a] + 1;
	}
	const size_t count = (size_t)bd[0] * bd[1] * bd[2];
	int32_t		*D = (int32_t *)malloc (count * sizeof (*D));
	uint8_t		*isl = (uint8_t *)calloc (count, 1);
	const int32_t INF = 1 << 20;
#define DI(x, y, z) ((((size_t)(z)*bd[1] + (y)) * bd[0]) + (x))
	for (int z = 0; z < bd[2]; ++z)
		for (int y = 0; y < bd[1]; ++y)
			for (int x = 0; x < bd[0]; ++x)
			{
				const int w[3] = {blo[0] + x, blo[1] + y, blo[2] + z};
				const int bi = BoxIndex (w);
				const int s = Terrain_Sample (t, w[0], w[1], w[2], NULL);
				const int ring = x == 0 || y == 0 || z == 0 || x == bd[0] - 1 || y == bd[1] - 1 || z == bd[2] - 1;
				isl[DI (x, y, z)] = bi >= 0 && flood_stamp[bi] == gen && s == TERRAIN_SDF_MAX; // cleared island sample
				if (isl[DI (x, y, z)])
				{
					D[DI (x, y, z)] = INF;
					continue;
				}
				D[DI (x, y, z)] = s < 0 ? 0 : (ring ? s : INF);
			}
	// two-pass chamfer: forward over the 13 earlier neighbours, then backward over the 13 later ones
	for (int pass = 0; pass < 2; ++pass)
		for (int zi = 0; zi < bd[2]; ++zi)
			for (int yi = 0; yi < bd[1]; ++yi)
				for (int xi = 0; xi < bd[0]; ++xi)
				{
					const int z = pass ? bd[2] - 1 - zi : zi, y = pass ? bd[1] - 1 - yi : yi, x = pass ? bd[0] - 1 - xi : xi;
					int32_t	  best = D[DI (x, y, z)];
					for (int dz = -1; dz <= 1; ++dz)
						for (int dy = -1; dy <= 1; ++dy)
							for (int dx = -1; dx <= 1; ++dx)
							{
								const int before = dz < 0 || (dz == 0 && (dy < 0 || (dy == 0 && dx < 0)));
								if ((dx == 0 && dy == 0 && dz == 0) || before == pass)
									continue;
								const int nx = x + dx, ny = y + dy, nz = z + dz;
								if (nx < 0 || ny < 0 || nz < 0 || nx >= bd[0] || ny >= bd[1] || nz >= bd[2])
									continue;
								const int	  axes = (dx != 0) + (dy != 0) + (dz != 0);
								const int32_t cand = D[DI (nx, ny, nz)] + (axes == 1 ? 32 : (axes == 2 ? 45 : 55));
								best = cand < best ? cand : best;
							}
					D[DI (x, y, z)] = best;
				}
	for (int z = 1; z < bd[2] - 1; ++z)
		for (int y = 1; y < bd[1] - 1; ++y)
			for (int x = 1; x < bd[0] - 1; ++x)
			{
				const int w[3] = {blo[0] + x, blo[1] + y, blo[2] + z};
				const int cur = Terrain_Sample (t, w[0], w[1], w[2], NULL);
				const int est = D[DI (x, y, z)] - TERRAIN_SUB / 2;
				int		  v;
				if (isl[DI (x, y, z)])
					v = est < 1 ? 1 : (est > TERRAIN_SDF_MAX ? TERRAIN_SDF_MAX : est);
				else if (cur < 0 || D[DI (x, y, z)] <= 55)
					continue;
				else
				{
					v = est > TERRAIN_SDF_MAX ? TERRAIN_SDF_MAX : est;
					if (v <= cur)
						continue;
				}
				if (v != cur)
					Terrain_SetSample (t, w[0], w[1], w[2], v, -1);
			}
#undef DI
	free (D);
	free (isl);
}

static int Detach (terrain_t *t, const terrain_op_t *op)
{
	const int seed[3] = {op->center[0], op->center[1], op->center[2]};
	const int cap = op->radius < 1 ? 1 : (op->radius > ICE_CAP_MAX ? ICE_CAP_MAX : op->radius);
	int		  lo[3];
	for (int a = 0; a < 3; ++a)
		lo[a] = seed[a] - ICE_H_APPLY;
	flood_gen += 2;
	const uint32_t gen = flood_gen;
	const int	   n = FloodSolid (t, seed, lo, ICE_H_APPLY, cap, gen, gen + 1);
	ice_state_t	  *s = State (t, 1);
	s->last = -1;
	if (!n)
		return 0;
	int ilo[3] = {1 << 20, 1 << 20, 1 << 20}, ihi[3] = {-(1 << 20), -(1 << 20), -(1 << 20)};
	for (int i = 0; i < n; ++i)
		for (int a = 0; a < 3; ++a)
		{
			ilo[a] = flood_queue[i].p[a] < ilo[a] ? flood_queue[i].p[a] : ilo[a];
			ihi[a] = flood_queue[i].p[a] > ihi[a] ? flood_queue[i].p[a] : ihi[a];
		}
	if (n >= ICE_MIN_CELLS)
	{
		terrain_brick_t *b = BuildBrick (t, n, ilo, ihi, gen);
		if (s->num == s->cap)
		{
			s->cap = s->cap ? s->cap * 2 : 16;
			s->bricks = (terrain_brick_t **)realloc (s->bricks, s->cap * sizeof (*s->bricks));
		}
		s->last = s->num;
		s->bricks[s->num++] = b;
	}
	for (int i = 0; i < n; ++i)
		Terrain_SetSample (t, flood_queue[i].p[0], flood_queue[i].p[1], flood_queue[i].p[2], TERRAIN_SDF_MAX, -1);
	Redistance (t, ilo, ihi, gen);
	return n;
}

/* ---- stamp ---- */

static void PoseOf (const terrain_t *t, const terrain_op_t *op, float pos[3], float R[9])
{
	float q[4];
	Terrain_UnpackQuat (op->aux, q);
	Terrain_QuatMatrix (q, R);
	for (int a = 0; a < 3; ++a)
		pos[a] = (float)t->origin[a] + (float)op->center[a] * ((float)TERRAIN_CELL / (float)TERRAIN_SUB);
}

static void StampBox (const terrain_t *t, const terrain_brick_t *b, const float pos[3], const float R[9], int lo[3], int hi[3])
{
	float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
	for (int k = 0; k < 8; ++k)
	{
		const float p[3] = {(k & 1 ? b->maxs[0] : b->mins[0]) - b->com[0], (k & 2 ? b->maxs[1] : b->mins[1]) - b->com[1], (k & 4 ? b->maxs[2] : b->mins[2]) - b->com[2]};
		for (int a = 0; a < 3; ++a)
		{
			const float w = pos[a] + R[a * 3] * p[0] + R[a * 3 + 1] * p[1] + R[a * 3 + 2] * p[2];
			mn[a] = w < mn[a] ? w : mn[a];
			mx[a] = w > mx[a] ? w : mx[a];
		}
	}
	for (int a = 0; a < 3; ++a)
	{
		const int n = t->size[a] * TERRAIN_CHUNK;
		lo[a] = (int)floorf ((mn[a] - (float)t->origin[a]) / TERRAIN_CELL) - 1;
		hi[a] = (int)ceilf ((mx[a] - (float)t->origin[a]) / TERRAIN_CELL) + 1;
		lo[a] = lo[a] < 1 ? 1 : lo[a];
		hi[a] = hi[a] > n - 2 ? n - 2 : hi[a];
	}
}

static int Stamp (terrain_t *t, const terrain_op_t *op)
{
	const terrain_brick_t *b = Terrain_Brick (t, op->radius);
	if (!b)
		return 0;
	float pos[3], R[9];
	int	  lo[3], hi[3], changed = 0;
	PoseOf (t, op, pos, R);
	StampBox (t, b, pos, R, lo, hi);
	const float lim[3] = {(float)(b->dim[0] - 1) * TERRAIN_CELL, (float)(b->dim[1] - 1) * TERRAIN_CELL, (float)(b->dim[2] - 1) * TERRAIN_CELL};
	for (int z = lo[2]; z <= hi[2]; ++z)
		for (int y = lo[1]; y <= hi[1]; ++y)
			for (int x = lo[0]; x <= hi[0]; ++x)
			{
				const float d[3] = {(float)(t->origin[0] + x * TERRAIN_CELL) - pos[0], (float)(t->origin[1] + y * TERRAIN_CELL) - pos[1], (float)(t->origin[2] + z * TERRAIN_CELL) - pos[2]};
				float		pb[3];
				for (int a = 0; a < 3; ++a)
					pb[a] = R[a] * d[0] + R[3 + a] * d[1] + R[6 + a] * d[2] + b->com[a]; // R^T d + com
				if (pb[0] < 0 || pb[1] < 0 || pb[2] < 0 || pb[0] > lim[0] || pb[1] > lim[1] || pb[2] > lim[2])
					continue;
				const float v = Terrain_SampleWorld (b->field, pb);
				int			s = (int)floorf (v * ((float)TERRAIN_SUB / (float)TERRAIN_CELL) + 0.5f);
				s = s < -TERRAIN_SDF_MAX ? -TERRAIN_SDF_MAX : (s > TERRAIN_SDF_MAX ? TERRAIN_SDF_MAX : s);
				const int old = Terrain_Sample (t, x, y, z, NULL);
				if (s >= old)
					continue;
				uint8_t mat = 0;
				Terrain_Sample (b->field, (int)floorf (pb[0] / TERRAIN_CELL + 0.5f), (int)floorf (pb[1] / TERRAIN_CELL + 0.5f), (int)floorf (pb[2] / TERRAIN_CELL + 0.5f), &mat);
				Terrain_SetSample (t, x, y, z, s, old >= 0 ? mat : -1);
				++changed;
			}
	return changed;
}

int Terrain_IceApplyOp (terrain_t *t, const terrain_op_t *op)
{
	if (op->kind == TERRAIN_OP_DETACH)
		return Detach (t, op);
	if (op->kind == TERRAIN_OP_STAMP)
		return Stamp (t, op);
	return 0;
}

void Terrain_IceOpBounds (const terrain_t *t, const terrain_op_t *op, int lo[3], int hi[3])
{
	if (op->kind == TERRAIN_OP_STAMP)
	{
		const terrain_brick_t *b = Terrain_Brick (t, op->radius);
		if (b)
		{
			float pos[3], R[9];
			PoseOf (t, op, pos, R);
			StampBox (t, b, pos, R, lo, hi);
			return;
		}
	}
	for (int a = 0; a < 3; ++a)
	{
		lo[a] = op->center[a] - ICE_H_APPLY - ICE_REDIST;
		hi[a] = op->center[a] + ICE_H_APPLY + ICE_REDIST;
		const int n = t->size[a] * TERRAIN_CHUNK;
		lo[a] = lo[a] < 0 ? 0 : lo[a];
		hi[a] = hi[a] > n - 1 ? n - 1 : hi[a];
	}
}
