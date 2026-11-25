/*
 * Editable SDF terrain: field, integer edit ops and a surface-nets mesher.
 * See terrain.h. GPL-2.0-or-later, as the rest of the engine.
 */
#include "terrain.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

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

int Terrain_ApplyOp (terrain_t *t, const terrain_op_t *op)
{
	const int reach = (int)op->radius + TERRAIN_SDF_MAX;
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
				const int		 dist = (int)ISqrt64 ((uint64_t)(dx * dx + dy * dy + dz * dz));
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
				else
				{
					v = Clamp (dist - (int)op->radius, -TERRAIN_SDF_MAX, TERRAIN_SDF_MAX);
					if (v >= old)
						continue;
				}
				if (!c->sdf)
					Materialize (t, c, cx, cy, cz);
				c->sdf[i] = (int8_t)v;
				if (op->kind == TERRAIN_OP_FILL && old >= 0)
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
	terrain_op_t op = {TERRAIN_OP_DIG, 0, 7 * TERRAIN_SUB, {pos[0], pos[1], pos[2]}};
	Terrain_ApplyOp (t, &op); // start room at the region centre
	const int margin = 8 * TERRAIN_SUB;
	const int worms = 3 + t->size[0] * t->size[1] * t->size[2] / 8;
	for (int w = 0; w < worms; ++w)
	{
		int32_t p[3] = {pos[0], pos[1], pos[2]};
		int32_t dir[3] = {(int32_t)(XorShift (&rng) % 257) - 128, (int32_t)(XorShift (&rng) % 257) - 128, (int32_t)(XorShift (&rng) % 129) - 64};
		int		radius = 3 * TERRAIN_SUB;
		for (int step = 0; step < 48; ++step)
		{
			for (int a = 0; a < 3; ++a)
			{
				// wander: nudge the heading, keep vertical travel gentler than horizontal
				dir[a] = Clamp (dir[a] + (int32_t)(XorShift (&rng) % 97) - 48, a == 2 ? -80 : -128, a == 2 ? 80 : 128);
				p[a] += dir[a] * radius / 160;
				if (p[a] < margin || p[a] > extent[a] - margin)
				{
					dir[a] = -dir[a];
					p[a] = Clamp (p[a], margin, extent[a] - margin);
				}
			}
			radius = Clamp (radius + (int)(XorShift (&rng) % 49) - 24, 2 * TERRAIN_SUB, 5 * TERRAIN_SUB);
			if (XorShift (&rng) % 16 == 0)
				radius = 7 * TERRAIN_SUB; // occasional chamber
			op.radius = (uint16_t)radius;
			op.center[0] = p[0];
			op.center[1] = p[1];
			op.center[2] = p[2];
			Terrain_ApplyOp (t, &op);
		}
	}
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

	static const int corner[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
	static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

	for (int z = 0; z < CELLS; ++z)
		for (int y = 0; y < CELLS; ++y)
			for (int x = 0; x < CELLS; ++x)
			{
				int s[8], mask = 0;
				const int8_t *base = &pad[(z * PAD + y) * PAD + x];
				for (int k = 0; k < 8; ++k)
				{
					// 2s+1 keeps the sign rule (s < 0 is solid) but never lands a crossing exactly on a
					// lattice corner, which would collapse neighbouring cells' vertices into one point
					s[k] = 2 * base[(corner[k][2] * PAD + corner[k][1]) * PAD + corner[k][0]] + 1;
					mask |= (s[k] < 0) << k;
				}
				int32_t *slot = &cell_vert[(z * CELLS + y) * CELLS + x];
				if (mask == 0 || mask == 255)
				{
					*slot = -1;
					continue;
				}
				float p[3] = {0, 0, 0};
				int	  crossings = 0;
				for (int e = 0; e < 12; ++e)
				{
					const int a = edges[e][0], b = edges[e][1];
					if ((s[a] < 0) == (s[b] < 0))
						continue;
					const float f = (float)s[a] / (float)(s[a] - s[b]);
					for (int i = 0; i < 3; ++i)
						p[i] += (float)corner[a][i] + f * (float)(corner[b][i] - corner[a][i]);
					++crossings;
				}
				int solid = 0;
				for (int k = 1; k < 8; ++k)
					if (s[k] < s[solid])
						solid = k;
				*slot = mesh->num_verts;
				terrain_vertex_t *v = PushVert (mesh);
				const int		  cell[3] = {bx + x - 1, by + y - 1, bz + z - 1};
				for (int i = 0; i < 3; ++i)
					v->pos[i] = (float)t->origin[i] + ((float)cell[i] + p[i] / (float)crossings) * (float)TERRAIN_CELL;
				float n[3] = {
					(float)(s[1] - s[0] + s[3] - s[2] + s[5] - s[4] + s[7] - s[6]), (float)(s[2] - s[0] + s[3] - s[1] + s[6] - s[4] + s[7] - s[5]),
					(float)(s[4] - s[0] + s[5] - s[1] + s[6] - s[2] + s[7] - s[3])};
				const float len = sqrtf (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
				for (int i = 0; i < 3; ++i)
					v->normal[i] = len > 0.0f ? n[i] / len : (i == 2 ? 1.0f : 0.0f);
				Terrain_Sample (t, cell[0] + corner[solid][0], cell[1] + corner[solid][1], cell[2] + corner[solid][2], &v->material);
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
					// split along the shorter diagonal to keep facets from folding
					const float *p0 = mesh->verts[q[0]].pos, *p1 = mesh->verts[q[1]].pos, *p2 = mesh->verts[q[2]].pos, *p3 = mesh->verts[q[3]].pos;
					float		 d02 = 0, d13 = 0;
					for (int i = 0; i < 3; ++i)
					{
						d02 += (p0[i] - p2[i]) * (p0[i] - p2[i]);
						d13 += (p1[i] - p3[i]) * (p1[i] - p3[i]);
					}
					uint32_t *out = PushIndices (mesh, 6);
					int		  order[6];
					if (d02 <= d13)
						memcpy (order, (const int[6]){0, 1, 2, 0, 2, 3}, sizeof (order));
					else
						memcpy (order, (const int[6]){1, 2, 3, 1, 3, 0}, sizeof (order));
					// counter-clockwise seen from open space when the edge leaves solid along +axis
					for (int i = 0; i < 6; ++i)
						out[i] = q[order[solid ? i : 5 - i]];
				}
			}
}
