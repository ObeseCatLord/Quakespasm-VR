/*
 * Editable SDF terrain: field, integer edit ops and a surface-nets mesher.
 * See terrain.h. GPL-2.0-or-later, as the rest of the engine.
 */
#include "terrain.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

terrain_t *terrain_active;
terrain_t *terrain_client;
unsigned   terrain_client_epoch;
terrain_t *terrain_pmove;

float Terrain_PmoveWalkNormal (void)
{
	return terrain_pmove ? terrain_pmove->walk_normal : 0.7f;
}

void Terrain_OpPack (const terrain_op_t *op, uint8_t out[TERRAIN_OP_BYTES])
{
	out[0] = op->kind;
	out[1] = op->material;
	out[2] = (uint8_t)(op->radius & 0xff);
	out[3] = (uint8_t)(op->radius >> 8);
	for (int a = 0; a < 3; ++a)
		for (int b = 0; b < 4; ++b)
			out[4 + a * 4 + b] = (uint8_t)((uint32_t)op->center[a] >> (8 * b));
	if (op->kind == TERRAIN_OP_STAMP)
		for (int b = 0; b < 4; ++b)
			out[16 + b] = (uint8_t)(op->aux >> (8 * b));
	else
	{
		for (int a = 0; a < 3; ++a)
			out[16 + a] = (uint8_t)op->normal[a];
		out[19] = op->thickness;
	}
}

void Terrain_OpUnpack (const uint8_t in[TERRAIN_OP_BYTES], terrain_op_t *op)
{
	memset (op, 0, sizeof (*op));
	op->kind = in[0];
	op->material = in[1];
	op->radius = (uint16_t)(in[2] | in[3] << 8);
	for (int a = 0; a < 3; ++a)
	{
		uint32_t v = 0;
		for (int b = 0; b < 4; ++b)
			v |= (uint32_t)in[4 + a * 4 + b] << (8 * b);
		op->center[a] = (int32_t)v;
	}
	if (op->kind == TERRAIN_OP_STAMP)
	{
		for (int b = 0; b < 4; ++b)
			op->aux |= (uint32_t)in[16 + b] << (8 * b);
		return;
	}
	for (int a = 0; a < 3; ++a)
		op->normal[a] = (int8_t)in[16 + a];
	op->thickness = in[19];
}

#define PAD	  (TERRAIN_CHUNK + 2) // samples -1 .. CHUNK
#define CELLS (TERRAIN_CHUNK + 1) // cells -1 .. CHUNK-1

static int Clamp (int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static uint32_t Hash3 (uint32_t seed, int x, int y, int z)
{
	uint32_t h = seed ^ ((uint32_t)x * 0x8da6b343u) ^ ((uint32_t)y * 0xd8163841u) ^ ((uint32_t)z * 0xcb1ab31fu);
	h ^= h >> 15;
	h *= 0x2c1b3c6du;
	h ^= h >> 12;
	h *= 0x297a2d39u;
	h ^= h >> 15;
	return h;
}

// Integer trilinear value noise, 0..255, on a lattice of 2^shift samples.
static int ValueNoise (uint32_t seed, int x, int y, int z, int shift)
{
	const int mask = (1 << shift) - 1;
	const int fx = x & mask, fy = y & mask, fz = z & mask;
	const int ix = x >> shift, iy = y >> shift, iz = z >> shift;
	int		  c[8];
	for (int i = 0; i < 8; ++i)
		c[i] = (int)(Hash3 (seed, ix + (i & 1), iy + ((i >> 1) & 1), iz + (i >> 2)) & 255);
#define LERP(a, b, f) ((a) + ((((b) - (a)) * (f)) >> shift))
	const int x00 = LERP (c[0], c[1], fx), x10 = LERP (c[2], c[3], fx);
	const int x01 = LERP (c[4], c[5], fx), x11 = LERP (c[6], c[7], fx);
	const int y0 = LERP (x00, x10, fy), y1 = LERP (x01, x11, fy);
	return LERP (y0, y1, fz);
#undef LERP
}

// Pristine material is a pure function of seed and position, so uniform chunks need not store it.
static uint8_t GenMaterial (uint32_t seed, int x, int y, int z)
{
	const int strata = ValueNoise (seed ^ 0x51u, x, y, z * 3, 4);
	const int vein = ValueNoise (seed ^ 0xa7u, x, y, z, 2);
	if (vein > 214)
		return TERRAIN_MAT_GOLD;
	return strata > 150 ? TERRAIN_MAT_DIRT : TERRAIN_MAT_ROCK;
}

static uint32_t ISqrt64 (uint64_t v)
{
	uint64_t r = (uint64_t)sqrt ((double)v);
	// the float estimate is only a seed; the correction makes the result exact on every platform
	while (r * r > v)
		--r;
	while ((r + 1) * (r + 1) <= v)
		++r;
	return (uint32_t)r;
}

terrain_chunk_t *Terrain_Chunk (const terrain_t *t, int cx, int cy, int cz)
{
	if (cx < 0 || cy < 0 || cz < 0 || cx >= t->size[0] || cy >= t->size[1] || cz >= t->size[2])
		return NULL;
	return &t->chunks[(cz * t->size[1] + cy) * t->size[0] + cx];
}

terrain_t *Terrain_Create (const int32_t origin[3], const int32_t size_chunks[3], uint32_t seed)
{
	terrain_t *t = (terrain_t *)calloc (1, sizeof (*t));
	for (int i = 0; i < 3; ++i)
	{
		t->origin[i] = origin[i];
		t->size[i] = size_chunks[i];
	}
	t->seed = seed;
	t->walk_normal = 0.7f;
	const int count = t->size[0] * t->size[1] * t->size[2];
	t->chunks = (terrain_chunk_t *)calloc (count, sizeof (*t->chunks));
	for (int i = 0; i < count; ++i)
	{
		t->chunks[i].uniform_sdf = -TERRAIN_SDF_MAX;
		t->chunks[i].dirty = 1;
	}
	return t;
}

void Terrain_Free (terrain_t *t)
{
	if (!t)
		return;
	Terrain_IceFree (t);
	const int count = t->size[0] * t->size[1] * t->size[2];
	for (int i = 0; i < count; ++i)
	{
		free (t->chunks[i].sdf);
		free (t->chunks[i].mat);
	}
	free (t->chunks);
	free (t);
}

static void Materialize (terrain_t *t, terrain_chunk_t *c, int cx, int cy, int cz)
{
	c->sdf = (int8_t *)malloc (TERRAIN_CHUNK_CELLS);
	c->mat = (uint8_t *)malloc (TERRAIN_CHUNK_CELLS);
	memset (c->sdf, c->uniform_sdf, TERRAIN_CHUNK_CELLS);
	uint8_t *m = c->mat;
	for (int z = 0; z < TERRAIN_CHUNK; ++z)
		for (int y = 0; y < TERRAIN_CHUNK; ++y)
			for (int x = 0; x < TERRAIN_CHUNK; ++x)
				*m++ = GenMaterial (t->seed, cx * TERRAIN_CHUNK + x, cy * TERRAIN_CHUNK + y, cz * TERRAIN_CHUNK + z);
}

/*
A test arena instead of a cave (debug maps): a flat floor at lattice height floor_z, walls 64 cells
(1024 units) out from the region's middle on each side and a ceiling 48 cells above the floor, all
terrain, so it looks and digs like the cave. Integer-only like the cave, so a client builds the
same field.
*/
void Terrain_GenerateFlat (terrain_t *t, int floor_z)
{
	const int mid[2] = {t->size[0] * TERRAIN_CHUNK / 2, t->size[1] * TERRAIN_CHUNK / 2};
	const int half = 64, ceiling = floor_z + 48;
	for (int cz = 0; cz < t->size[2]; ++cz)
		for (int cy = 0; cy < t->size[1]; ++cy)
			for (int cx = 0; cx < t->size[0]; ++cx)
			{
				terrain_chunk_t *c = Terrain_Chunk (t, cx, cy, cz);
				c->dirty = 1;
				Materialize (t, c, cx, cy, cz);
				int8_t *out = c->sdf;
				for (int z = cz * TERRAIN_CHUNK; z < (cz + 1) * TERRAIN_CHUNK; ++z)
					for (int y = cy * TERRAIN_CHUNK; y < (cy + 1) * TERRAIN_CHUNK; ++y)
						for (int x = cx * TERRAIN_CHUNK; x < (cx + 1) * TERRAIN_CHUNK; ++x)
						{
							// cells to the nearest face of the box; negative outside it
							int d = z - floor_z;
							d = d < ceiling - z ? d : ceiling - z;
							const int dx = half - (x > mid[0] ? x - mid[0] : mid[0] - x), dy = half - (y > mid[1] ? y - mid[1] : mid[1] - y);
							d = d < dx ? d : dx;
							d = d < dy ? d : dy;
							*out++ = (int8_t)Clamp (d * TERRAIN_SUB, -TERRAIN_SDF_MAX, TERRAIN_SDF_MAX);
						}
			}
}

int Terrain_Sample (const terrain_t *t, int x, int y, int z, uint8_t *mat)
{
	const terrain_chunk_t *c = Terrain_Chunk (t, x >> TERRAIN_CHUNK_SHIFT, y >> TERRAIN_CHUNK_SHIFT, z >> TERRAIN_CHUNK_SHIFT);
	if (!c)
	{
		if (mat)
			*mat = TERRAIN_MAT_ROCK;
		return -TERRAIN_SDF_MAX;
	}
	if (!c->sdf)
	{
		if (mat)
			*mat = GenMaterial (t->seed, x, y, z);
		return c->uniform_sdf;
	}
	const int i = (((z & 31) << TERRAIN_CHUNK_SHIFT | (y & 31)) << TERRAIN_CHUNK_SHIFT) | (x & 31);
	if (mat)
		*mat = c->mat[i];
	return c->sdf[i];
}

float Terrain_SampleWorld (const terrain_t *t, const float p[3])
{
	float f[3];
	int	  i[3];
	for (int a = 0; a < 3; ++a)
	{
		const float l = (p[a] - (float)t->origin[a]) / (float)TERRAIN_CELL;
		const float fl = floorf (l);
		i[a] = (int)fl;
		f[a] = l - fl;
	}
	float v = 0.0f;
	for (int k = 0; k < 8; ++k)
	{
		const int	dx = k & 1, dy = (k >> 1) & 1, dz = k >> 2;
		const float w = (dx ? f[0] : 1.0f - f[0]) * (dy ? f[1] : 1.0f - f[1]) * (dz ? f[2] : 1.0f - f[2]);
		v += w * (float)Terrain_Sample (t, i[0] + dx, i[1] + dy, i[2] + dz, NULL);
	}
	return v * ((float)TERRAIN_CELL / (float)TERRAIN_SUB);
}

void Terrain_SetSample (terrain_t *t, int x, int y, int z, int sdf, int mat)
{
	const int		 cx = x >> TERRAIN_CHUNK_SHIFT, cy = y >> TERRAIN_CHUNK_SHIFT, cz = z >> TERRAIN_CHUNK_SHIFT;
	terrain_chunk_t *c = Terrain_Chunk (t, cx, cy, cz);
	if (!c)
		return;
	if (!c->sdf)
		Materialize (t, c, cx, cy, cz);
	const int i = (((z & 31) << TERRAIN_CHUNK_SHIFT | (y & 31)) << TERRAIN_CHUNK_SHIFT) | (x & 31);
	c->sdf[i] = (int8_t)Clamp (sdf, -TERRAIN_SDF_MAX, TERRAIN_SDF_MAX);
	if (mat >= 0)
		c->mat[i] = (uint8_t)mat;
	for (int nz = (z - 1) >> TERRAIN_CHUNK_SHIFT; nz <= (z + 1) >> TERRAIN_CHUNK_SHIFT; ++nz)
		for (int ny = (y - 1) >> TERRAIN_CHUNK_SHIFT; ny <= (y + 1) >> TERRAIN_CHUNK_SHIFT; ++ny)
			for (int nx = (x - 1) >> TERRAIN_CHUNK_SHIFT; nx <= (x + 1) >> TERRAIN_CHUNK_SHIFT; ++nx)
			{
				terrain_chunk_t *n = Terrain_Chunk (t, nx, ny, nz);
				if (n)
					n->dirty = 1;
			}
}

int Terrain_ApplyOp (terrain_t *t, const terrain_op_t *op)
{
	if (TERRAIN_OP_IS_ICE (op->kind))
		return Terrain_IceApplyOp (t, op);
	if (op->kind >= TERRAIN_OP_KINDS)
		return 0;
	const int	  reach = (int)op->radius + (op->kind == TERRAIN_OP_DISK ? (int)op->thickness : 0) + TERRAIN_SDF_MAX;
	// a disk's axis, as given (integer, any length up to 127)
	const int64_t nx = op->normal[0], ny = op->normal[1], nz = op->normal[2];
	const int64_t nlen = op->kind == TERRAIN_OP_DISK ? (int64_t)ISqrt64 ((uint64_t)(nx * nx + ny * ny + nz * nz)) : 0;
	if (op->kind == TERRAIN_OP_DISK && !nlen)
		return 0;
	int		  lo[3], hi[3];
	for (int a = 0; a < 3; ++a)
	{
		// The outermost sample layer stays solid so the region's surface is always closed.
		const int n = t->size[a] * TERRAIN_CHUNK;
		lo[a] = Clamp ((op->center[a] - reach) / TERRAIN_SUB - 1, 1, n - 2);
		hi[a] = Clamp ((op->center[a] + reach) / TERRAIN_SUB + 1, 1, n - 2);
	}
	int changed = 0;
	int clo[3] = {1 << 30, 1 << 30, 1 << 30}, chi[3] = {-1, -1, -1};
	for (int z = lo[2]; z <= hi[2]; ++z)
		for (int y = lo[1]; y <= hi[1]; ++y)
		{
			const int64_t dy = (int64_t)y * TERRAIN_SUB - op->center[1], dz = (int64_t)z * TERRAIN_SUB - op->center[2];
			const int	  cy = y >> TERRAIN_CHUNK_SHIFT, cz = z >> TERRAIN_CHUNK_SHIFT;
			for (int x = lo[0]; x <= hi[0]; ++x)
			{
				const int64_t	 dx = (int64_t)x * TERRAIN_SUB - op->center[0];
				const int64_t	 d2 = dx * dx + dy * dy + dz * dz;
				const int		 dist = (int)ISqrt64 ((uint64_t)d2);
				const int		 cx = x >> TERRAIN_CHUNK_SHIFT;
				terrain_chunk_t *c = Terrain_Chunk (t, cx, cy, cz);
				const int		 i = (((z & 31) << TERRAIN_CHUNK_SHIFT | (y & 31)) << TERRAIN_CHUNK_SHIFT) | (x & 31);
				const int		 old = c->sdf ? c->sdf[i] : c->uniform_sdf;
				int				 v;
				if (op->kind == TERRAIN_OP_DIG)
				{
					v = Clamp ((int)op->radius - dist, -TERRAIN_SDF_MAX, TERRAIN_SDF_MAX);
					if (v <= old)
						continue;
				}
				else if (op->kind == TERRAIN_OP_DISK)
				{
					// distance to a flat cylinder: across the axis against the radius, along it against the
					// half thickness
					const int64_t axial = (dx * nx + dy * ny + dz * nz) / nlen;
					const int64_t r2 = d2 - axial * axial;
					const int64_t qx = (int64_t)ISqrt64 ((uint64_t)(r2 > 0 ? r2 : 0)) - (int64_t)op->radius;
					const int64_t qy = (axial < 0 ? -axial : axial) - (int64_t)op->thickness;
					const int64_t ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, in = qx > qy ? qx : qy;
					const int64_t sdf = (int64_t)ISqrt64 ((uint64_t)(ox * ox + oy * oy)) + (in < 0 ? in : 0);
					v = (int)(sdf < -TERRAIN_SDF_MAX ? -TERRAIN_SDF_MAX : sdf > TERRAIN_SDF_MAX ? TERRAIN_SDF_MAX : sdf);
					if (v >= old)
						continue;
				}
				else
				{
					v = Clamp (dist - (int)op->radius, -TERRAIN_SDF_MAX, TERRAIN_SDF_MAX);
					if (v >= old)
						continue;
				}
				if (!c->sdf)
					Materialize (t, c, cx, cy, cz);
				c->sdf[i] = (int8_t)v;
				if (op->kind != TERRAIN_OP_DIG && old >= 0)
					c->mat[i] = op->material;
				++changed;
				const int p[3] = {x, y, z};
				for (int a = 0; a < 3; ++a)
				{
					if (p[a] < clo[a])
						clo[a] = p[a];
					if (p[a] > chi[a])
						chi[a] = p[a];
				}
			}
		}
	if (changed)
	{
		// a chunk's mesh reads samples -1 .. CHUNK, so a changed border sample dirties the neighbour too
		for (int a = 0; a < 3; ++a)
		{
			clo[a] = (clo[a] - 1) >> TERRAIN_CHUNK_SHIFT;
			chi[a] = (chi[a] + 1) >> TERRAIN_CHUNK_SHIFT;
		}
		for (int cz = clo[2]; cz <= chi[2]; ++cz)
			for (int cy = clo[1]; cy <= chi[1]; ++cy)
				for (int cx = clo[0]; cx <= chi[0]; ++cx)
				{
					terrain_chunk_t *c = Terrain_Chunk (t, cx, cy, cz);
					if (c)
						c->dirty = 1;
				}
	}
	return changed;
}

static uint32_t XorShift (uint32_t *s)
{
	*s ^= *s << 13;
	*s ^= *s >> 17;
	*s ^= *s << 5;
	return *s;
}

void Terrain_Generate (terrain_t *t)
{
	uint32_t rng = t->seed * 2654435761u + 0x9e3779b9u;
	if (!rng)
		rng = 1;
	int32_t extent[3], pos[3];
	for (int a = 0; a < 3; ++a)
	{
		extent[a] = t->size[a] * TERRAIN_CHUNK * TERRAIN_SUB;
		pos[a] = extent[a] / 2;
	}
	terrain_op_t op = {TERRAIN_OP_DIG, 0, 11 * TERRAIN_SUB, {pos[0], pos[1], pos[2]}, {0, 0, 0}, 0};
	Terrain_ApplyOp (t, &op); // start room at the region centre
	// Sized for Quake: passages 130-260 units across (a player is 32 by 56), chambers about 450.
	// The margin keeps every dig inside the region, so the cave never opens onto the map shell.
	const int margin = 16 * TERRAIN_SUB;
	const int worms = 3 + t->size[0] * t->size[1] * t->size[2] / 10;
	for (int w = 0; w < worms; ++w)
	{
		int32_t p[3] = {pos[0], pos[1], pos[2]};
		int32_t dir[3] = {(int32_t)(XorShift (&rng) % 257) - 128, (int32_t)(XorShift (&rng) % 257) - 128, (int32_t)(XorShift (&rng) % 129) - 64};
		int		radius = 6 * TERRAIN_SUB;
		for (int step = 0; step < 40; ++step)
		{
			for (int a = 0; a < 3; ++a)
			{
				// wander: nudge the heading, keep vertical travel gentler than horizontal
				dir[a] = Clamp (dir[a] + (int32_t)(XorShift (&rng) % 97) - 48, a == 2 ? -56 : -128, a == 2 ? 56 : 128);
				p[a] += dir[a] * radius / 160;
				if (p[a] < margin || p[a] > extent[a] - margin)
				{
					dir[a] = -dir[a];
					p[a] = Clamp (p[a], margin, extent[a] - margin);
				}
			}
			radius = Clamp (radius + (int)(XorShift (&rng) % 65) - 32, 4 * TERRAIN_SUB, 8 * TERRAIN_SUB);
			if (XorShift (&rng) % 14 == 0)
				radius = 14 * TERRAIN_SUB; // occasional chamber
			op.radius = (uint16_t)radius;
			op.center[0] = p[0];
			op.center[1] = p[1];
			op.center[2] = p[2];
			Terrain_ApplyOp (t, &op);
		}
	}
}

// Lattice height of the first solid sample above (dir 1) or below (dir -1) z, or -1 within limit.
static int SolidAlong (const terrain_t *t, int x, int y, int z, int dir, int limit)
{
	for (int k = 1; k <= limit; ++k)
		if (Terrain_Sample (t, x, y, z + dir * k, NULL) < 0)
			return z + dir * k;
	return -1;
}

static void FillColumn (terrain_t *t, int x, int y, int z0, int z1, int r0, int r1, int bulge)
{
	// a stack of fill spheres from z0 to z1 (lattice), radius r0 at z0 to r1 at z1 (TERRAIN_SUB units),
	// plus `bulge` extra at both ends so columns flare into floor and ceiling
	terrain_op_t op = {TERRAIN_OP_FILL, TERRAIN_MAT_GOLD, 0, {x * TERRAIN_SUB + TERRAIN_SUB / 2, y * TERRAIN_SUB + TERRAIN_SUB / 2, 0}, {0, 0, 0}, 0};
	const int	 len = (z1 - z0) * TERRAIN_SUB;
	const int	 dir = len < 0 ? -1 : 1;
	for (int d = 0; d * dir <= len * dir;)
	{
		const int f = len ? d * 256 / len : 0; // 0..256 along the column
		const int edge = (f < 128 ? 128 - f : f - 128);
		int		  r = r0 + (r1 - r0) * f / 256 + bulge * edge * edge / (128 * 128);
		if (r < TERRAIN_SUB / 3)
			r = TERRAIN_SUB / 3;
		op.radius = (uint16_t)r;
		op.center[2] = z0 * TERRAIN_SUB + d;
		Terrain_ApplyOp (t, &op);
		d += dir * (r / 2 > 4 ? r / 2 : 4);
	}
}

int Terrain_GenerateIce (terrain_t *t)
{
	uint32_t rng = t->seed * 0x9e3779b9u + 0x1ce1ce1u;
	if (!rng)
		rng = 1;
	const int nx = t->size[0] * TERRAIN_CHUNK, ny = t->size[1] * TERRAIN_CHUNK, nz = t->size[2] * TERRAIN_CHUNK;
	const int want = 6 + t->size[0] * t->size[1] * t->size[2] / 2;
	int		  made = 0;
	for (int attempt = 0; attempt < want * 60 && made < want; ++attempt)
	{
		const int x = 4 + (int)(XorShift (&rng) % (uint32_t)(nx - 8)), y = 4 + (int)(XorShift (&rng) % (uint32_t)(ny - 8));
		const int z = 4 + (int)(XorShift (&rng) % (uint32_t)(nz - 8));
		const int dx = x - nx / 2, dy = y - ny / 2, dz = z - nz / 2;
		if (dx * dx + dy * dy + dz * dz < 16 * 16) // keep the start room clear
			continue;
		if (Terrain_Sample (t, x, y, z, NULL) < TERRAIN_SDF_MAX) // well clear of every wall
			continue;
		const int floor = SolidAlong (t, x, y, z, -1, 40), ceil = SolidAlong (t, x, y, z, 1, 40);
		if (floor < 0 || ceil < 0)
			continue;
		const int height = ceil - floor;
		if (height < 8)
			continue;
		const uint32_t kind = XorShift (&rng) % 8;
		if (kind < 3 && height <= 26)
		{
			// a full pillar, waisted in the middle
			const int r = (int)(TERRAIN_SUB * 3 / 2 + XorShift (&rng) % TERRAIN_SUB);
			FillColumn (t, x, y, floor, ceil, r, r, r);
		}
		else
		{
			// icicles hanging from the ceiling, sometimes with a stub growing up to meet one
			const int n = 1 + (int)(XorShift (&rng) % 3);
			for (int i = 0; i < n; ++i)
			{
				const int ix = x + (int)(XorShift (&rng) % 7) - 3, iy = y + (int)(XorShift (&rng) % 7) - 3;
				const int top = SolidAlong (t, ix, iy, z, 1, 40);
				if (top < 0)
					continue;
				const int length = height * (25 + (int)(XorShift (&rng) % 30)) / 100;
				const int r = (int)(TERRAIN_SUB + XorShift (&rng) % TERRAIN_SUB);
				FillColumn (t, ix, iy, top + 1, top - length, r, TERRAIN_SUB / 4, 0);
			}
			if (kind == 7)
				FillColumn (t, x, y, floor - 1, floor + height / 4, TERRAIN_SUB * 2, TERRAIN_SUB / 3, 0);
		}
		++made;
	}
	return made;
}

uint64_t Terrain_Hash (const terrain_t *t)
{
	uint64_t  h = 0xcbf29ce484222325ull;
	const int count = t->size[0] * t->size[1] * t->size[2];
	for (int i = 0; i < count; ++i)
	{
		const terrain_chunk_t *c = &t->chunks[i];
		if (!c->sdf)
		{
			h = (h ^ (uint8_t)c->uniform_sdf) * 0x100000001b3ull;
			continue;
		}
		for (int k = 0; k < TERRAIN_CHUNK_CELLS; ++k)
			h = (h ^ (uint8_t)c->sdf[k]) * 0x100000001b3ull;
		for (int k = 0; k < TERRAIN_CHUNK_CELLS; ++k)
			h = (h ^ c->mat[k]) * 0x100000001b3ull;
	}
	return h;
}

void Terrain_MeshFree (terrain_mesh_t *mesh)
{
	free (mesh->verts);
	free (mesh->indices);
	memset (mesh, 0, sizeof (*mesh));
}

static terrain_vertex_t *PushVert (terrain_mesh_t *mesh)
{
	if (mesh->num_verts == mesh->cap_verts)
	{
		mesh->cap_verts = mesh->cap_verts ? mesh->cap_verts * 2 : 1024;
		mesh->verts = (terrain_vertex_t *)realloc (mesh->verts, mesh->cap_verts * sizeof (*mesh->verts));
	}
	return &mesh->verts[mesh->num_verts++];
}

static uint32_t *PushIndices (terrain_mesh_t *mesh, int count)
{
	if (mesh->num_indices + count > mesh->cap_indices)
	{
		mesh->cap_indices = mesh->cap_indices ? mesh->cap_indices * 2 : 4096;
		mesh->indices = (uint32_t *)realloc (mesh->indices, mesh->cap_indices * sizeof (*mesh->indices));
	}
	mesh->num_indices += count;
	return &mesh->indices[mesh->num_indices - count];
}

static const int net_corner[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
static const int net_edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

// The mesher and the collision trace both place cell vertices and split quads through these two
// functions, so what the player collides with is the drawn surface, bit for bit.
// s[] are the cell's corner samples as 2*sdf+1: the sign rule (negative is solid) is unchanged, but a
// crossing can never land exactly on a lattice corner and collapse neighbouring vertices together.
static int NetCellVertex (const terrain_t *t, const int cell[3], const int s[8], float pos[3])
{
	int mask = 0;
	for (int k = 0; k < 8; ++k)
		mask |= (s[k] < 0) << k;
	if (mask == 0 || mask == 255)
		return 0;
	float p[3] = {0, 0, 0};
	int	  crossings = 0;
	for (int e = 0; e < 12; ++e)
	{
		const int a = net_edges[e][0], b = net_edges[e][1];
		if ((s[a] < 0) == (s[b] < 0))
			continue;
		const float f = (float)s[a] / (float)(s[a] - s[b]);
		for (int i = 0; i < 3; ++i)
			p[i] += (float)net_corner[a][i] + f * (float)(net_corner[b][i] - net_corner[a][i]);
		++crossings;
	}
	for (int i = 0; i < 3; ++i)
		pos[i] = (float)t->origin[i] + ((float)cell[i] + p[i] / (float)crossings) * (float)TERRAIN_CELL;
	return 1;
}

// q0..q3 go round the quad; order[] receives two triangles, counter-clockwise seen from open space
// when the lattice edge leaves solid along its positive axis (solid != 0).
static void NetQuadOrder (const float *p0, const float *p1, const float *p2, const float *p3, int solid, int order[6])
{
	static const int split02[6] = {0, 1, 2, 0, 2, 3}, split13[6] = {1, 2, 3, 1, 3, 0};
	float			 d02 = 0, d13 = 0;
	for (int i = 0; i < 3; ++i)
	{
		d02 += (p0[i] - p2[i]) * (p0[i] - p2[i]);
		d13 += (p1[i] - p3[i]) * (p1[i] - p3[i]);
	}
	// split along the shorter diagonal to keep facets from folding
	const int *base = d02 <= d13 ? split02 : split13;
	for (int i = 0; i < 6; ++i)
		order[i] = base[solid ? i : 5 - i];
}

/*
 * Naive surface nets: one vertex per cell the surface crosses, at the mean of
 * its edge crossings; one quad per sign-changing lattice edge. The chunk owns
 * the edges that start at its own samples, so neighbouring chunks emit each
 * quad exactly once and place shared vertices identically.
 */
void Terrain_MeshChunk (const terrain_t *t, int cx, int cy, int cz, terrain_mesh_t *mesh)
{
	static _Thread_local int8_t	 pad[PAD * PAD * PAD];
	static _Thread_local int32_t cell_vert[CELLS * CELLS * CELLS];
	const terrain_chunk_t		*c = Terrain_Chunk (t, cx, cy, cz);
	const int					 bx = cx * TERRAIN_CHUNK, by = cy * TERRAIN_CHUNK, bz = cz * TERRAIN_CHUNK;

	mesh->num_verts = mesh->num_indices = 0;
	if (!c)
		return;

	int any_solid = 0, any_open = 0;
	for (int z = 0; z < PAD; ++z)
		for (int y = 0; y < PAD; ++y)
		{
			int8_t	 *row = &pad[(z * PAD + y) * PAD];
			const int interior = z >= 1 && z <= TERRAIN_CHUNK && y >= 1 && y <= TERRAIN_CHUNK;
			if (interior)
			{
				if (c->sdf)
					memcpy (row + 1, &c->sdf[((z - 1) * TERRAIN_CHUNK + (y - 1)) * TERRAIN_CHUNK], TERRAIN_CHUNK);
				else
					memset (row + 1, c->uniform_sdf, TERRAIN_CHUNK);
				row[0] = (int8_t)Terrain_Sample (t, bx - 1, by + y - 1, bz + z - 1, NULL);
				row[PAD - 1] = (int8_t)Terrain_Sample (t, bx + TERRAIN_CHUNK, by + y - 1, bz + z - 1, NULL);
			}
			else
				for (int x = 0; x < PAD; ++x)
					row[x] = (int8_t)Terrain_Sample (t, bx + x - 1, by + y - 1, bz + z - 1, NULL);
			for (int x = 0; x < PAD; ++x)
			{
				any_solid |= row[x] < 0;
				any_open |= row[x] >= 0;
			}
		}
	if (!any_solid || !any_open)
		return;

	for (int z = 0; z < CELLS; ++z)
		for (int y = 0; y < CELLS; ++y)
			for (int x = 0; x < CELLS; ++x)
			{
				int			  s[8];
				const int8_t *base = &pad[(z * PAD + y) * PAD + x];
				for (int k = 0; k < 8; ++k)
					s[k] = 2 * base[(net_corner[k][2] * PAD + net_corner[k][1]) * PAD + net_corner[k][0]] + 1;
				int32_t	 *slot = &cell_vert[(z * CELLS + y) * CELLS + x];
				const int cell[3] = {bx + x - 1, by + y - 1, bz + z - 1};
				float	  pos[3];
				if (!NetCellVertex (t, cell, s, pos))
				{
					*slot = -1;
					continue;
				}
				int solid = 0;
				for (int k = 1; k < 8; ++k)
					if (s[k] < s[solid])
						solid = k;
				*slot = mesh->num_verts;
				terrain_vertex_t *v = PushVert (mesh);
				for (int i = 0; i < 3; ++i)
					v->pos[i] = pos[i];
				float n[3] = {
					(float)(s[1] - s[0] + s[3] - s[2] + s[5] - s[4] + s[7] - s[6]), (float)(s[2] - s[0] + s[3] - s[1] + s[6] - s[4] + s[7] - s[5]),
					(float)(s[4] - s[0] + s[5] - s[1] + s[6] - s[2] + s[7] - s[3])};
				const float len = sqrtf (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
				for (int i = 0; i < 3; ++i)
					v->normal[i] = len > 0.0f ? n[i] / len : (i == 2 ? 1.0f : 0.0f);
				Terrain_Sample (t, cell[0] + net_corner[solid][0], cell[1] + net_corner[solid][1], cell[2] + net_corner[solid][2], &v->material);
			}

	// Ambient occlusion: how much open space lies a short way along the normal.
	for (int i = 0; i < mesh->num_verts; ++i)
	{
		terrain_vertex_t *v = &mesh->verts[i];
		const float		  reach = 2.5f * TERRAIN_CELL;
		const float		  probe[3] = {v->pos[0] + v->normal[0] * reach, v->pos[1] + v->normal[1] * reach, v->pos[2] + v->normal[2] * reach};
		const float		  open = Terrain_SampleWorld (t, probe) / reach;
		v->ao = (uint8_t)(255.0f * (open < 0.0f ? 0.0f : (open > 1.0f ? 1.0f : open)));
	}

	static const int axis_step[3] = {1, PAD, PAD * PAD};
	static const int cell_step[3] = {1, CELLS, CELLS * CELLS};
	for (int z = 1; z <= TERRAIN_CHUNK; ++z)
		for (int y = 1; y <= TERRAIN_CHUNK; ++y)
			for (int x = 1; x <= TERRAIN_CHUNK; ++x)
			{
				const int si = (z * PAD + y) * PAD + x;
				const int ci = (z * CELLS + y) * CELLS + x;
				const int solid = pad[si] < 0;
				for (int a = 0; a < 3; ++a)
				{
					if ((pad[si + axis_step[a]] < 0) == solid)
						continue;
					const int	   u = cell_step[(a + 1) % 3], w = cell_step[(a + 2) % 3];
					const uint32_t q[4] = {
						(uint32_t)cell_vert[ci], (uint32_t)cell_vert[ci - u], (uint32_t)cell_vert[ci - u - w], (uint32_t)cell_vert[ci - w]};
					int		  order[6];
					NetQuadOrder (mesh->verts[q[0]].pos, mesh->verts[q[1]].pos, mesh->verts[q[2]].pos, mesh->verts[q[3]].pos, solid, order);
					uint32_t *out = PushIndices (mesh, 6);
					for (int i = 0; i < 6; ++i)
						out[i] = q[order[i]];
				}
			}
}

/*
 * Collision. A trace gathers the surface-nets triangles of the cells its swept box touches straight
 * from the field, so no collision mesh is stored and collision can never lag behind a remesh.
 * Triangles are one-sided: only faces the box moves against can stop it.
 */
#define TRACE_MAX_SAMPLES 8192
#define TRACE_SEGMENT	  48.0f			  // long moves are cut into pieces this long, in Quake units
#define TRACE_BACKOFF	  (1.0f / 32.0f)  // a stopped box keeps this gap, like Quake's DIST_EPSILON
#define TRACE_SKIN		  (1.0f / 64.0f)  // boxes closer than this do not count as overlapping
#define TRACE_GROW		  (1.0f / 256.0f) // sweeps see triangles this much larger, so rays cannot slip between neighbours

typedef struct
{
	const terrain_t *t;
	int				 lo[3], n[3];
	int8_t			 sdf[TRACE_MAX_SAMPLES];
	uint8_t			 state[TRACE_MAX_SAMPLES]; // per cell: 0 not built, 1 no vertex, 2 vertex
	float			 vert[TRACE_MAX_SAMPLES][3];
} trace_grid_t;

typedef int (*trace_tri_fn) (void *ctx, const float *a, const float *b, const float *c);

static int GridCellVertex (trace_grid_t *g, int x, int y, int z, const float **pos)
{
	const int i = (z * g->n[1] + y) * g->n[0] + x;
	if (!g->state[i])
	{
		int		  s[8];
		const int cell[3] = {g->lo[0] + x, g->lo[1] + y, g->lo[2] + z};
		for (int k = 0; k < 8; ++k)
			s[k] = 2 * g->sdf[((z + net_corner[k][2]) * g->n[1] + y + net_corner[k][1]) * g->n[0] + x + net_corner[k][0]] + 1;
		g->state[i] = NetCellVertex (g->t, cell, s, g->vert[i]) ? 2 : 1;
	}
	*pos = g->vert[i];
	return g->state[i] == 2;
}

// Calls fn for every surface triangle that can reach the world-space box; stops early when fn returns nonzero.
// Returns -1 when the box is too large to gather (callers treat that as solid), else fn's last result.
static int ForEachTriangle (const terrain_t *t, const float bmin[3], const float bmax[3], trace_grid_t *g, trace_tri_fn fn, void *ctx)
{
	int hi[3];
	g->t = t;
	for (int a = 0; a < 3; ++a)
	{
		g->lo[a] = (int)floorf ((bmin[a] - (float)t->origin[a]) / (float)TERRAIN_CELL) - 1;
		hi[a] = (int)ceilf ((bmax[a] - (float)t->origin[a]) / (float)TERRAIN_CELL) + 1;
		g->n[a] = hi[a] - g->lo[a] + 1;
	}
	const int total = g->n[0] * g->n[1] * g->n[2];
	if (total > TRACE_MAX_SAMPLES)
		return -1;
	int any_solid = 0, any_open = 0;
	for (int z = 0, i = 0; z < g->n[2]; ++z)
		for (int y = 0; y < g->n[1]; ++y)
			for (int x = 0; x < g->n[0]; ++x, ++i)
			{
				g->sdf[i] = (int8_t)Terrain_Sample (t, g->lo[0] + x, g->lo[1] + y, g->lo[2] + z, NULL);
				any_solid |= g->sdf[i] < 0;
				any_open |= g->sdf[i] >= 0;
			}
	if (!any_solid || !any_open)
		return 0;
	memset (g->state, 0, total);
	const int step[3] = {1, g->n[0], g->n[0] * g->n[1]};
	for (int z = 0; z < g->n[2] - 1; ++z)
		for (int y = 0; y < g->n[1] - 1; ++y)
			for (int x = 0; x < g->n[0] - 1; ++x)
			{
				const int p[3] = {x, y, z};
				const int si = (z * g->n[1] + y) * g->n[0] + x;
				const int solid = g->sdf[si] < 0;
				for (int a = 0; a < 3; ++a)
				{
					const int u = (a + 1) % 3, w = (a + 2) % 3;
					if (p[u] < 1 || p[w] < 1 || (g->sdf[si + step[a]] < 0) == solid)
						continue;
					int c[4][3];
					for (int k = 0; k < 4; ++k)
					{
						c[k][0] = x;
						c[k][1] = y;
						c[k][2] = z;
					}
					c[1][u] -= 1;
					c[2][u] -= 1;
					c[2][w] -= 1;
					c[3][w] -= 1;
					const float *q[4];
					int			 ok = 1;
					for (int k = 0; k < 4; ++k)
						ok &= GridCellVertex (g, c[k][0], c[k][1], c[k][2], &q[k]);
					if (!ok)
						continue; // cannot happen for a sign-changing edge; kept as a guard
					int order[6];
					NetQuadOrder (q[0], q[1], q[2], q[3], solid, order);
					for (int k = 0; k < 6; k += 3)
					{
						const int r = fn (ctx, q[order[k]], q[order[k + 1]], q[order[k + 2]]);
						if (r)
							return r;
					}
				}
			}
	return 0;
}

typedef struct
{
	float center[3], half[3], move[3];
	float best;		 // earliest entry time so far, 0..1
	float normal[3]; // separating axis at that entry, facing against the move
	int	  hit;
} sweep_t;

static float Dot3 (const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void Cross3 (const float *a, const float *b, float *out)
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

// Swept separating-axis test of a moving box against one triangle (13 axes).
static int SweepTriangle (void *ctx, const float *a, const float *b, const float *c)
{
	sweep_t	   *sw = (sweep_t *)ctx;
	const float e[3][3] = {{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, {c[0] - b[0], c[1] - b[1], c[2] - b[2]}, {a[0] - c[0], a[1] - c[1], a[2] - c[2]}};
	float		axes[13][3];
	float		face[3];
	Cross3 (e[0], e[1], face);
	const float face_len = sqrtf (Dot3 (face, face));
	if (face_len < 1e-6f)
		return 0;
	const float approach = Dot3 (sw->move, face) / face_len;
	if (approach >= 0.0f)
		return 0; // moving along or away from the face
	for (int i = 0; i < 3; ++i)
	{
		axes[0][i] = face[i] / face_len;
		axes[1][i] = i == 0;
		axes[2][i] = i == 1;
		axes[3][i] = i == 2;
	}
	for (int k = 0; k < 3; ++k)
		for (int i = 0; i < 3; ++i)
		{
			const float unit[3] = {i == 0, i == 1, i == 2};
			Cross3 (e[k], unit, axes[4 + k * 3 + i]);
		}
	float enter = -1e30f, leave = 1e30f;
	int	  enter_axis = 0;
	float enter_sign = 1.0f;
	for (int k = 0; k < 13; ++k)
	{
		float		len = Dot3 (axes[k], axes[k]);
		if (len < 1e-10f)
			continue;
		len = sqrtf (len);
		const float L[3] = {axes[k][0] / len, axes[k][1] / len, axes[k][2] / len};
		const float pa = Dot3 (a, L), pb = Dot3 (b, L), pc = Dot3 (c, L);
		const float tmin = (pa < pb ? (pa < pc ? pa : pc) : (pb < pc ? pb : pc)) - TRACE_GROW;
		const float tmax = (pa > pb ? (pa > pc ? pa : pc) : (pb > pc ? pb : pc)) + TRACE_GROW;
		const float r = sw->half[0] * fabsf (L[0]) + sw->half[1] * fabsf (L[1]) + sw->half[2] * fabsf (L[2]);
		const float cmid = Dot3 (sw->center, L);
		const float v = Dot3 (sw->move, L);
		if (fabsf (v) < 1e-7f)
		{
			if (cmid + r <= tmin || cmid - r >= tmax)
				return 0;
			continue;
		}
		float t0 = (tmin - (cmid + r)) / v, t1 = (tmax - (cmid - r)) / v;
		float sign = -1.0f; // entering from the low side: the plane faces -L
		if (t0 > t1)
		{
			const float swap = t0;
			t0 = t1;
			t1 = swap;
			sign = 1.0f;
		}
		if (t0 > enter)
		{
			enter = t0;
			enter_axis = k;
			enter_sign = sign;
		}
		if (t1 < leave)
			leave = t1;
		if (enter > leave)
			return 0;
	}
	if (enter > 1.0f || leave < 0.0f)
		return 0;
	float normal[3], time;
	if (enter <= 0.0f)
	{
		// already touching and pushing in: stop here against the face itself
		time = 0.0f;
		for (int i = 0; i < 3; ++i)
			normal[i] = axes[0][i];
	}
	else
	{
		const float len = sqrtf (Dot3 (axes[enter_axis], axes[enter_axis]));
		for (int i = 0; i < 3; ++i)
			normal[i] = enter_sign * axes[enter_axis][i] / len;
		const float speed = -Dot3 (sw->move, normal);
		time = enter - (speed > 1e-6f ? TRACE_BACKOFF / speed : 0.0f);
		if (time < 0.0f)
			time = 0.0f;
	}
	if (time < sw->best)
	{
		sw->best = time;
		sw->hit = 1;
		for (int i = 0; i < 3; ++i)
			sw->normal[i] = normal[i];
	}
	return 0;
}

static int OverlapTriangle (void *ctx, const float *a, const float *b, const float *c)
{
	const sweep_t *sw = (const sweep_t *)ctx;
	const float	   e[3][3] = {{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, {c[0] - b[0], c[1] - b[1], c[2] - b[2]}, {a[0] - c[0], a[1] - c[1], a[2] - c[2]}};
	float		   axes[13][3];
	Cross3 (e[0], e[1], axes[0]);
	for (int i = 0; i < 3; ++i)
	{
		axes[1][i] = i == 0;
		axes[2][i] = i == 1;
		axes[3][i] = i == 2;
	}
	for (int k = 0; k < 3; ++k)
		for (int i = 0; i < 3; ++i)
		{
			const float unit[3] = {i == 0, i == 1, i == 2};
			Cross3 (e[k], unit, axes[4 + k * 3 + i]);
		}
	for (int k = 0; k < 13; ++k)
	{
		float		len = Dot3 (axes[k], axes[k]);
		if (len < 1e-10f)
			continue;
		len = sqrtf (len);
		const float L[3] = {axes[k][0] / len, axes[k][1] / len, axes[k][2] / len};
		const float pa = Dot3 (a, L), pb = Dot3 (b, L), pc = Dot3 (c, L);
		const float tmin = (pa < pb ? (pa < pc ? pa : pc) : (pb < pc ? pb : pc)) + TRACE_SKIN;
		const float tmax = (pa > pb ? (pa > pc ? pa : pc) : (pb > pc ? pb : pc)) - TRACE_SKIN;
		const float r = sw->half[0] * fabsf (L[0]) + sw->half[1] * fabsf (L[1]) + sw->half[2] * fabsf (L[2]);
		const float cmid = Dot3 (sw->center, L);
		if (cmid + r <= tmin || cmid - r >= tmax)
			return 0;
	}
	return 1;
}

int Terrain_PointSolid (const terrain_t *t, const float p[3])
{
	return Terrain_SampleWorld (t, p) < 0.0f;
}

int Terrain_BoxOverlaps (const terrain_t *t, const float origin[3], const float mins[3], const float maxs[3])
{
	static _Thread_local trace_grid_t grid;
	sweep_t							  sw;
	float							  bmin[3], bmax[3];
	for (int i = 0; i < 3; ++i)
	{
		sw.half[i] = (maxs[i] - mins[i]) * 0.5f;
		sw.center[i] = origin[i] + (maxs[i] + mins[i]) * 0.5f;
		bmin[i] = sw.center[i] - sw.half[i];
		bmax[i] = sw.center[i] + sw.half[i];
	}
	if (Terrain_PointSolid (t, sw.center))
		return 1;
	return ForEachTriangle (t, bmin, bmax, &grid, OverlapTriangle, &sw) != 0;
}

int Terrain_TraceBox (const terrain_t *t, const float start[3], const float mins[3], const float maxs[3], const float end[3], terrain_hit_t *hit)
{
	static _Thread_local trace_grid_t grid;
	sweep_t							  sw;
	float							  delta[3], center[3];
	memset (hit, 0, sizeof (*hit));
	hit->fraction = 1.0f;
	for (int i = 0; i < 3; ++i)
	{
		sw.half[i] = (maxs[i] - mins[i]) * 0.5f;
		center[i] = start[i] + (maxs[i] + mins[i]) * 0.5f;
		delta[i] = end[i] - start[i];
	}
	if (Terrain_PointSolid (t, center))
	{
		hit->startsolid = 2;
		hit->fraction = 0.0f;
		return 1;
	}
	const float length = sqrtf (Dot3 (delta, delta));
	if (length == 0.0f)
	{
		float bmin[3], bmax[3];
		for (int i = 0; i < 3; ++i)
		{
			sw.center[i] = center[i];
			bmin[i] = center[i] - sw.half[i];
			bmax[i] = center[i] + sw.half[i];
		}
		if (ForEachTriangle (t, bmin, bmax, &grid, OverlapTriangle, &sw) != 0)
		{
			hit->startsolid = 1;
			hit->fraction = 0.0f;
			return 1;
		}
		return 0;
	}
	const int	segments = (int)ceilf (length / TRACE_SEGMENT);
	const float reach = sqrtf (Dot3 (sw.half, sw.half)) + length / (float)segments * 0.5f + 2.0f * TERRAIN_CELL;
	for (int seg = 0; seg < segments; ++seg)
	{
		float bmin[3], bmax[3], mid[3];
		for (int i = 0; i < 3; ++i)
		{
			sw.center[i] = center[i] + delta[i] * (float)seg / (float)segments;
			sw.move[i] = delta[i] / (float)segments;
			const float to = sw.center[i] + sw.move[i];
			bmin[i] = (sw.center[i] < to ? sw.center[i] : to) - sw.half[i] - 1.0f;
			bmax[i] = (sw.center[i] > to ? sw.center[i] : to) + sw.half[i] + 1.0f;
			mid[i] = sw.center[i] + sw.move[i] * 0.5f;
		}
		// the field never overstates distance, so a far-enough reading clears the whole segment
		if (Terrain_SampleWorld (t, mid) > reach)
			continue;
		sw.best = 2.0f;
		sw.hit = 0;
		if (ForEachTriangle (t, bmin, bmax, &grid, SweepTriangle, &sw) < 0)
		{
			sw.hit = 1; // box too large to resolve: stop it where the segment starts
			sw.best = 0.0f;
			for (int i = 0; i < 3; ++i)
				sw.normal[i] = -delta[i] / length;
		}
		if (sw.hit)
		{
			hit->fraction = ((float)seg + sw.best) / (float)segments;
			for (int i = 0; i < 3; ++i)
				hit->normal[i] = sw.normal[i];
			return 1;
		}
	}
	return 0;
}

/*
==============================================================================

WALKING NAVIGATION

A coarse grid over the region for monsters that walk. A cell is open when the field at its centre
is clear by a few units; a cell is somewhere to stand when it is open, the one under it is not, and
there is headroom above. Two neighbouring stand cells are joined when the climb or drop between them
is at most one cell (45 degrees at this cell size, inside the step monsters take on terrain) and the
body has room to make it; a ledge also leads, one way, to a floor up to TERRAIN_NAV_MAX_DROP cells
below it, and when the caller sets max_climb a floor up to that many cells above can be jumped onto.
The flood is weighted (diagonals, drops and jumps cost more than walking), so a route keeps to the
floor where it can. A flood from the target gives every connected stand cell its number of
steps to go; walking to a neighbour with a smaller number is the way there.

==============================================================================
*/

static int NavMax (int a, int b)
{
	return a > b ? a : b;
}

static int NavMin (int a, int b)
{
	return a < b ? a : b;
}

#ifndef NAV_OPEN_UNITS
#define NAV_OPEN_UNITS 14 // a player-wide box fits through a run of cells this open
#endif
#define NAV_OPEN_SUB (NAV_OPEN_UNITS * TERRAIN_SUB / TERRAIN_CELL) // clearance at a cell centre that counts as open

static int NavHeadroom (int cls)
{
	return cls ? 3 : 2; // cells of open space a standing body needs, its own included
}

static int NavIndex3 (const terrain_nav_t *nav, int x, int y, int z)
{
	if (x < 0 || y < 0 || z < 0 || x >= nav->dim[0] || y >= nav->dim[1] || z >= nav->dim[2])
		return -1;
	return (z * nav->dim[1] + y) * nav->dim[0] + x;
}

static int NavOpen (const terrain_nav_t *nav, int x, int y, int z)
{
	const int i = NavIndex3 (nav, x, y, z);
	return i >= 0 && (nav->flags[i] & TERRAIN_NAV_AIR);
}

static int NavCanStand (const terrain_nav_t *nav, int cls, int x, int y, int z)
{
	if (!NavOpen (nav, x, y, z) || NavOpen (nav, x, y, z - 1))
		return 0;
	for (int h = 1; h < NavHeadroom (cls); ++h)
		if (!NavOpen (nav, x, y, z + h))
			return 0;
	if (cls)
	{
		// a wide body also needs its shoulders clear on every side
		static const int side[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
		for (int s = 0; s < 4; ++s)
			if (!NavOpen (nav, x + side[s][0], y + side[s][1], z + 1))
				return 0;
	}
	return 1;
}

void Terrain_NavInit (terrain_nav_t *nav, const terrain_t *t)
{
	memset (nav, 0, sizeof (*nav));
	int count = 1;
	for (int a = 0; a < 3; ++a)
	{
		nav->origin[a] = t->origin[a];
		nav->dim[a] = t->size[a] * TERRAIN_CHUNK_UNITS / TERRAIN_NAV_CELL;
		count *= nav->dim[a];
	}
	nav->count = count;
	nav->flags = (uint8_t *)calloc (count, 1);
}

void Terrain_NavFree (terrain_nav_t *nav)
{
	free (nav->flags);
	free (nav->heap);
	memset (nav, 0, sizeof (*nav));
}

void Terrain_NavRefresh (terrain_nav_t *nav, const terrain_t *t, const int lo_in[3], const int hi_in[3])
{
	const int ratio = TERRAIN_NAV_CELL / TERRAIN_CELL;
	int		  lo[3], hi[3];
	for (int a = 0; a < 3; ++a)
	{
		lo[a] = Clamp (lo_in[a], 0, nav->dim[a] - 1);
		hi[a] = Clamp (hi_in[a], 0, nav->dim[a] - 1);
		if (lo[a] > hi[a])
			return;
	}
	for (int z = lo[2]; z <= hi[2]; ++z)
		for (int y = lo[1]; y <= hi[1]; ++y)
			for (int x = lo[0]; x <= hi[0]; ++x)
			{
				// a cell centre lands exactly on a lattice sample
				uint8_t	 *f = &nav->flags[NavIndex3 (nav, x, y, z)];
				const int open =
					!(*f & TERRAIN_NAV_BLOCKED) && Terrain_Sample (t, x * ratio + ratio / 2, y * ratio + ratio / 2, z * ratio + ratio / 2, NULL) > NAV_OPEN_SUB;
				*f = (uint8_t)((*f & ~TERRAIN_NAV_AIR) | (open ? TERRAIN_NAV_AIR : 0));
			}
	// standing depends on the cell below, headroom above and, for wide bodies, the sides
	const int reach_down = TERRAIN_NAV_CLASSES > 1 ? NavHeadroom (1) - 1 : 1;
	for (int z = NavMax (lo[2] - reach_down, 0); z <= NavMin (hi[2] + 1, nav->dim[2] - 1); ++z)
		for (int y = NavMax (lo[1] - 1, 0); y <= NavMin (hi[1] + 1, nav->dim[1] - 1); ++y)
			for (int x = NavMax (lo[0] - 1, 0); x <= NavMin (hi[0] + 1, nav->dim[0] - 1); ++x)
			{
				uint8_t *f = &nav->flags[NavIndex3 (nav, x, y, z)];
				for (int cls = 0; cls < TERRAIN_NAV_CLASSES; ++cls)
				{
					const uint8_t bit = (uint8_t)(TERRAIN_NAV_STAND << cls);
					*f = (uint8_t)((*f & ~bit) | (NavCanStand (nav, cls, x, y, z) ? bit : 0));
				}
			}
}

void Terrain_NavRefreshAll (terrain_nav_t *nav, const terrain_t *t)
{
	const int lo[3] = {0, 0, 0}, hi[3] = {nav->dim[0] - 1, nav->dim[1] - 1, nav->dim[2] - 1};
	Terrain_NavRefresh (nav, t, lo, hi);
}

void Terrain_NavOpBox (const terrain_nav_t *nav, const terrain_t *t, const terrain_op_t *op, int lo[3], int hi[3])
{
	if (TERRAIN_OP_IS_ICE (op->kind))
	{
		// a STAMP's box needs its brick, so call this after the op (and every DETACH before it) was applied
		Terrain_IceOpBounds (t, op, lo, hi);
		for (int a = 0; a < 3; ++a)
		{
			const int lattice_per_nav = TERRAIN_NAV_CELL / TERRAIN_CELL;
			lo[a] = (lo[a] - 1) / lattice_per_nav; // lattice sample -> nav cell
			hi[a] = (hi[a] + 1) / lattice_per_nav;
		}
		return;
	}
	// an op changes samples up to radius + TERRAIN_SDF_MAX (fixed point) from its centre
	const int reach = (int)op->radius + (op->kind == TERRAIN_OP_DISK ? (int)op->thickness : 0) + TERRAIN_SDF_MAX + TERRAIN_SUB;
	(void)nav;
	for (int a = 0; a < 3; ++a)
	{
		const int sub_per_nav = TERRAIN_SUB * TERRAIN_NAV_CELL / TERRAIN_CELL;
		lo[a] = (int)floorf ((float)(op->center[a] - reach) / (float)sub_per_nav);
		hi[a] = (int)floorf ((float)(op->center[a] + reach) / (float)sub_per_nav);
	}
}

void Terrain_NavRefreshOp (terrain_nav_t *nav, const terrain_t *t, const terrain_op_t *op)
{
	int lo[3], hi[3];
	Terrain_NavOpBox (nav, t, op, lo, hi);
	Terrain_NavRefresh (nav, t, lo, hi);
}

void Terrain_NavSetBlocked (terrain_nav_t *nav, int cell, int blocked)
{
	if (cell < 0 || cell >= nav->count)
		return;
	nav->flags[cell] = (uint8_t)((nav->flags[cell] & ~TERRAIN_NAV_BLOCKED) | (blocked ? TERRAIN_NAV_BLOCKED : 0));
}

void Terrain_NavCellCenter (const terrain_nav_t *nav, int cell, float out[3])
{
	const int c[3] = {cell % nav->dim[0], (cell / nav->dim[0]) % nav->dim[1], cell / (nav->dim[0] * nav->dim[1])};
	for (int a = 0; a < 3; ++a)
		out[a] = (float)nav->origin[a] + ((float)c[a] + 0.5f) * (float)TERRAIN_NAV_CELL;
}

// Whether a column is open from just under level top down to level bottom: nothing solid between
// a body and a floor it is looking for below.
static int NavOpenDown (const terrain_nav_t *nav, int x, int y, int top, int bottom)
{
	for (int z = top; z > bottom; --z)
		if (!NavOpen (nav, x, y, z))
			return 0;
	return 1;
}

int Terrain_NavFindStand (const terrain_nav_t *nav, int cls, const float feet[3], int up, int down)
{
	const uint8_t bit = (uint8_t)(TERRAIN_NAV_STAND << cls);
	int			  c[3];
	for (int a = 0; a < 3; ++a)
		c[a] = (int)floorf ((feet[a] - (float)nav->origin[a]) / (float)TERRAIN_NAV_CELL);
	// nearest level first; on each level the column itself, then its neighbours. Below the feet only
	// through open cells, so a floor on the far side of rock never counts.
	for (int dz = 0; dz <= NavMax (up, down); ++dz)
		for (int sign = 0; sign <= 1; ++sign)
		{
			if ((sign && (dz > down || !dz)) || (!sign && dz > up))
				continue;
			const int z = c[2] + (sign ? -dz : dz);
			for (int ring = 0; ring <= 1; ++ring)
				for (int dy = -ring; dy <= ring; ++dy)
					for (int dx = -ring; dx <= ring; ++dx)
					{
						if (ring && !dx && !dy)
							continue;
						const int i = NavIndex3 (nav, c[0] + dx, c[1] + dy, z);
						if (i < 0 || !(nav->flags[i] & bit))
							continue;
						if (sign && !NavOpenDown (nav, c[0] + dx, c[1] + dy, c[2], z))
							continue;
						return i;
					}
		}
	return -1;
}

// Whether a body of this class can walk directly between two stand cells in neighbouring columns.
static int NavLink (const terrain_nav_t *nav, int cls, const int a[3], const int b[3])
{
	const int dz = b[2] - a[2];
	if (dz < -1 || dz > 1)
		return 0;
	const int top = NavMax (a[2], b[2]);
	// climbing or dropping a level: the lower end needs room for the body at the upper level
	if (dz)
	{
		const int *low = dz > 0 ? a : b;
		if (!NavOpen (nav, low[0], low[1], top + NavHeadroom (cls) - 1))
			return 0;
	}
	// diagonal: no cutting through a corner at body height
	if (a[0] != b[0] && a[1] != b[1])
		if (!NavOpen (nav, b[0], a[1], top + 1) || !NavOpen (nav, a[0], b[1], top + 1))
			return 0;
	return 1;
}

// Whether a body can walk off the edge of stand cell a and land on b, two to four levels down in a
// neighbouring column. One way only: nothing climbs back.
static int NavDrop (const terrain_nav_t *nav, int cls, const int a[3], const int b[3])
{
	const int fall = a[2] - b[2];
	if (fall < 2 || fall > TERRAIN_NAV_MAX_DROP)
		return 0;
	// open from the landing up to body height above the edge, so the body clears the lip as it falls
	for (int z = b[2] + 1; z <= a[2] + NavHeadroom (cls) - 1; ++z)
		if (!NavOpen (nav, b[0], b[1], z))
			return 0;
	if (a[0] != b[0] && a[1] != b[1])
		if (!NavOpen (nav, b[0], a[1], a[2] + 1) || !NavOpen (nav, a[0], b[1], a[2] + 1))
			return 0;
	return 1;
}

// One step from a to b on foot: a walk or climb of a level either way, a drop off a ledge, or a
// jump up onto one no higher than the caller allows (the reverse of a drop).
static int NavStep (const terrain_nav_t *nav, int cls, const int a[3], const int b[3])
{
	const int dz = b[2] - a[2];
	if (dz >= -1 && dz <= 1)
		return NavLink (nav, cls, a, b);
	if (dz < -1)
		return NavDrop (nav, cls, a, b);
	return dz <= nav->max_climb && NavDrop (nav, cls, b, a);
}

// What a step costs: about two per 32 units across, three on a diagonal, more for leaving the floor.
static int NavCost (const int a[3], const int b[3])
{
	const int diag = a[0] != b[0] && a[1] != b[1], dz = b[2] - a[2];
	if (dz > 1)
		return 4 + 2 * dz; // a jump up
	if (dz < -1)
		return 3 + dz * -1 / 2;
	return diag ? 3 : 2;
}

// Level changes tried for each direction, best first on a tie: walks, then drops, then climbs.
static int NavLevels (const terrain_nav_t *nav, int levels[16])
{
	int n = 0;
	levels[n++] = 0;
	levels[n++] = 1;
	levels[n++] = -1;
	for (int dz = 2; dz <= TERRAIN_NAV_MAX_DROP; ++dz)
		levels[n++] = -dz;
	for (int dz = 2; dz <= nav->max_climb && dz <= TERRAIN_NAV_MAX_CLIMB; ++dz)
		levels[n++] = dz;
	return n;
}

static const int nav_dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

// Binary heap of (cost << 32 | cell) for the flood.
static void NavPush (terrain_nav_t *nav, uint64_t v)
{
	if (nav->heap_len == nav->heap_cap)
	{
		nav->heap_cap = nav->heap_cap ? nav->heap_cap * 2 : 4096;
		nav->heap = (uint64_t *)realloc (nav->heap, (size_t)nav->heap_cap * sizeof (uint64_t));
	}
	int i = nav->heap_len++;
	while (i > 0 && nav->heap[(i - 1) / 2] > v)
	{
		nav->heap[i] = nav->heap[(i - 1) / 2];
		i = (i - 1) / 2;
	}
	nav->heap[i] = v;
}

static uint64_t NavPop (terrain_nav_t *nav)
{
	const uint64_t top = nav->heap[0], last = nav->heap[--nav->heap_len];
	int			   i = 0;
	for (;;)
	{
		int c = i * 2 + 1;
		if (c >= nav->heap_len)
			break;
		if (c + 1 < nav->heap_len && nav->heap[c + 1] < nav->heap[c])
			++c;
		if (nav->heap[c] >= last)
			break;
		nav->heap[i] = nav->heap[c];
		i = c;
	}
	if (nav->heap_len)
		nav->heap[i] = last;
	return top;
}

// Dijkstra from the seeds over stand cells, settling cells no dearer than max_cost. When touched is
// given, every cell given a cost is recorded there (to be reset later without clearing the grid).
static void NavFloodCore (terrain_nav_t *nav, int cls, const int *seeds, int num_seeds, uint16_t *dist, int max_cost, terrain_navfield_t *touched)
{
	const uint8_t bit = (uint8_t)(TERRAIN_NAV_STAND << cls);
	int			  levels[16];
	const int	  num_levels = NavLevels (nav, levels);
	nav->heap_len = 0;
#define NAV_TOUCH(cell)                                                                                            \
	do                                                                                                             \
	{                                                                                                              \
		if (touched)                                                                                               \
		{                                                                                                          \
			if (touched->num_touched == touched->cap_touched)                                                      \
			{                                                                                                      \
				touched->cap_touched = touched->cap_touched ? touched->cap_touched * 2 : 4096;                     \
				touched->touched = (int *)realloc (touched->touched, (size_t)touched->cap_touched * sizeof (int)); \
			}                                                                                                      \
			touched->touched[touched->num_touched++] = (cell);                                                     \
		}                                                                                                          \
	} while (0)
	for (int s = 0; s < num_seeds; ++s)
		if (seeds[s] >= 0 && seeds[s] < nav->count && dist[seeds[s]] == TERRAIN_NAV_FAR && (nav->flags[seeds[s]] & bit))
		{
			dist[seeds[s]] = 0;
			NAV_TOUCH (seeds[s]);
			NavPush (nav, (uint64_t)seeds[s]);
		}
	while (nav->heap_len)
	{
		const uint64_t top = NavPop (nav);
		const int	   cell = (int)(top & 0xffffffffu), cost = (int)(top >> 32);
		if (cost != dist[cell])
			continue; // a cheaper way here was already settled
		const int c[3] = {cell % nav->dim[0], (cell / nav->dim[0]) % nav->dim[1], cell / (nav->dim[0] * nav->dim[1])};
		// the flood runs backwards: next is a cell from which one step reaches this one
		for (int d = 0; d < 8; ++d)
			for (int k = 0; k < num_levels; ++k)
			{
				const int n[3] = {c[0] + nav_dirs[d][0], c[1] + nav_dirs[d][1], c[2] - levels[k]};
				const int next = NavIndex3 (nav, n[0], n[1], n[2]);
				if (next < 0 || !(nav->flags[next] & bit))
					continue;
				const int total = cost + NavCost (n, c);
				if (total >= dist[next] || total > max_cost || total >= TERRAIN_NAV_FAR || !NavStep (nav, cls, n, c))
					continue;
				if (dist[next] == TERRAIN_NAV_FAR)
					NAV_TOUCH (next);
				dist[next] = (uint16_t)total;
				NavPush (nav, (uint64_t)total << 32 | (uint64_t)next);
			}
	}
#undef NAV_TOUCH
}

void Terrain_NavFlood (terrain_nav_t *nav, int cls, const int *seeds, int num_seeds, uint16_t *dist)
{
	memset (dist, 0xff, (size_t)nav->count * sizeof (uint16_t));
	NavFloodCore (nav, cls, seeds, num_seeds, dist, TERRAIN_NAV_FAR, NULL);
}

void Terrain_NavFieldInit (const terrain_nav_t *nav, terrain_navfield_t *field)
{
	memset (field, 0, sizeof (*field));
	field->dist = (uint16_t *)malloc ((size_t)nav->count * sizeof (uint16_t));
	memset (field->dist, 0xff, (size_t)nav->count * sizeof (uint16_t));
}

void Terrain_NavFieldFree (terrain_navfield_t *field)
{
	free (field->dist);
	free (field->touched);
	memset (field, 0, sizeof (*field));
}

void Terrain_NavFloodNear (terrain_nav_t *nav, int cls, const int *seeds, int num_seeds, terrain_navfield_t *field, int max_cost)
{
	// clear only what the last flood reached, so a small flood costs only its own cells
	for (int i = 0; i < field->num_touched; ++i)
		field->dist[field->touched[i]] = TERRAIN_NAV_FAR;
	field->num_touched = 0;
	NavFloodCore (nav, cls, seeds, num_seeds, field->dist, max_cost, field);
}

int Terrain_NavNext (const terrain_nav_t *nav, int cls, const uint16_t *dist, int cell)
{
	if (cell < 0 || cell >= nav->count || dist[cell] == TERRAIN_NAV_FAR || !dist[cell])
		return -1;
	const uint8_t bit = (uint8_t)(TERRAIN_NAV_STAND << cls);
	const int	  c[3] = {cell % nav->dim[0], (cell / nav->dim[0]) % nav->dim[1], cell / (nav->dim[0] * nav->dim[1])};
	int			  levels[16], best = -1, best_total = dist[cell] + 1;
	const int	  num_levels = NavLevels (nav, levels);
	// the step on a cheapest way; on a tie straight directions win over diagonals, and within a
	// direction a walk over a drop over a climb
	for (int d = 0; d < 8; ++d)
		for (int k = 0; k < num_levels; ++k)
		{
			const int n[3] = {c[0] + nav_dirs[d][0], c[1] + nav_dirs[d][1], c[2] + levels[k]};
			const int next = NavIndex3 (nav, n[0], n[1], n[2]);
			if (next < 0 || dist[next] == TERRAIN_NAV_FAR || !(nav->flags[next] & bit))
				continue;
			const int total = dist[next] + NavCost (c, n);
			if (total >= best_total || !NavStep (nav, cls, c, n))
				continue;
			best = next;
			best_total = total;
		}
	return best;
}

int Terrain_NavLevelChange (const terrain_nav_t *nav, int from, int to)
{
	return to / (nav->dim[0] * nav->dim[1]) - from / (nav->dim[0] * nav->dim[1]);
}

int Terrain_NavIsDrop (const terrain_nav_t *nav, int from, int to)
{
	return Terrain_NavLevelChange (nav, from, to) < -1;
}

int Terrain_NavIsClimb (const terrain_nav_t *nav, int from, int to)
{
	return Terrain_NavLevelChange (nav, from, to) > 1;
}

int Terrain_NavStepCost (const terrain_nav_t *nav, int from, int to)
{
	const int d0 = nav->dim[0], d01 = nav->dim[0] * nav->dim[1];
	const int a[3] = {from % d0, (from / d0) % nav->dim[1], from / d01}, b[3] = {to % d0, (to / d0) % nav->dim[1], to / d01};
	return NavCost (a, b);
}
