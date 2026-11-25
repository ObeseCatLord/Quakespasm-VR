/*
 * Offline gate for the terrain core: op determinism, closed surface across
 * chunk seams, dig locality, and chunk remesh time.
 *   cc -O2 -std=c11 -IQuake tests/terrain_fixture.c Quake/terrain.c -lm -o terrain_fixture
 *   ./terrain_fixture [out.obj]
 */
#define _POSIX_C_SOURCE 199309L
#include "terrain.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef TERRAIN_FIXTURE_GOLDEN
#define TERRAIN_FIXTURE_GOLDEN 0x6a1673dea18502c6ull // seed 1; same under gcc -O0/-O2 and clang -O3 -ffast-math
#endif

static double Now (void)
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}

static int failures;
#define CHECK(cond, ...) \
	do \
	{ \
		if (!(cond)) \
		{ \
			++failures; \
			printf ("FAIL: " __VA_ARGS__); \
			printf ("\n"); \
		} \
	} while (0)

typedef struct
{
	int32_t a[3], b[3];
	int		count;
} edge_t;

static int EdgeCmp (const void *l, const void *r)
{
	return memcmp (l, r, 6 * sizeof (int32_t));
}

int main (int argc, char **argv)
{
	const int32_t origin[3] = {-1024, -1024, -768};
	const int32_t size[3] = {4, 4, 3};
	const int	  chunk_count = size[0] * size[1] * size[2];

	// 1. determinism: same seed -> same bytes; a different seed differs
	terrain_t *t = Terrain_Create (origin, size, 1);
	Terrain_Generate (t);
	terrain_t *t2 = Terrain_Create (origin, size, 1);
	Terrain_Generate (t2);
	terrain_t *t3 = Terrain_Create (origin, size, 2);
	Terrain_Generate (t3);
	const uint64_t h = Terrain_Hash (t);
	CHECK (h == Terrain_Hash (t2), "same seed gave different fields");
	CHECK (h != Terrain_Hash (t3), "different seeds gave the same field");
	printf ("field hash seed 1: %016llx\n", (unsigned long long)h);
	// Golden value: any compiler, platform or float mode must reproduce it.
	CHECK (h == TERRAIN_FIXTURE_GOLDEN || TERRAIN_FIXTURE_GOLDEN == 0, "field hash differs from the recorded golden value");

	// 2. mesh every chunk; time the ones that have surface
	terrain_mesh_t *meshes = calloc (chunk_count, sizeof (*meshes));
	double			total = 0, worst = 0;
	int				surface_chunks = 0, tris = 0, materialized = 0;
	for (int rep = 0; rep < 5; ++rep)
		for (int i = 0; i < chunk_count; ++i)
		{
			const int	 cx = i % size[0], cy = i / size[0] % size[1], cz = i / (size[0] * size[1]);
			const double start = Now ();
			Terrain_MeshChunk (t, cx, cy, cz, &meshes[i]);
			const double ms = Now () - start;
			if (rep == 0)
				continue; // warm-up
			if (meshes[i].num_indices)
			{
				total += ms;
				if (ms > worst)
					worst = ms;
				++surface_chunks;
			}
		}
	surface_chunks /= 4;
	for (int i = 0; i < chunk_count; ++i)
	{
		tris += meshes[i].num_indices / 3;
		materialized += t->chunks[i].sdf != NULL;
	}
	const double mean = total / (surface_chunks * 4);
	printf ("chunks %d, stored %d (%.1f MB), with surface %d, triangles %d\n", chunk_count, materialized, materialized * 2.0 * TERRAIN_CHUNK_CELLS / 1048576.0,
			surface_chunks, tris);
	printf ("remesh per surface chunk: mean %.3f ms, worst %.3f ms (target < 1 ms)\n", mean, worst);
	CHECK (surface_chunks > 4, "cave touches too few chunks to be a useful test");
	CHECK (mean < 1.0, "mean chunk remesh over the 1 ms target");

	// 3. closed surface: over the whole region every edge is used once in each direction
	edge_t *edges = malloc ((size_t)tris * 3 * sizeof (*edges));
	int		num_edges = 0;
	for (int i = 0; i < chunk_count; ++i)
		for (int k = 0; k < meshes[i].num_indices; k += 3)
			for (int e = 0; e < 3; ++e)
			{
				const float *a = meshes[i].verts[meshes[i].indices[k + e]].pos;
				const float *b = meshes[i].verts[meshes[i].indices[k + (e + 1) % 3]].pos;
				int32_t		 qa[3], qb[3];
				for (int j = 0; j < 3; ++j)
				{
					qa[j] = (int32_t)(a[j] * 64.0f);
					qb[j] = (int32_t)(b[j] * 64.0f);
				}
				const int flip = memcmp (qa, qb, sizeof (qa)) > 0;
				edge_t	 *ed = &edges[num_edges++];
				memcpy (ed->a, flip ? qb : qa, sizeof (qa));
				memcpy (ed->b, flip ? qa : qb, sizeof (qa));
				ed->count = flip ? -1 : 1;
			}
	qsort (edges, num_edges, sizeof (*edges), EdgeCmp);
	int open_edges = 0, pinched = 0;
	for (int i = 0; i < num_edges;)
	{
		int j = i, sum = 0;
		while (j < num_edges && !EdgeCmp (&edges[i], &edges[j]))
			sum += edges[j++].count;
		if (sum != 0 || !memcmp (edges[i].a, edges[i].b, sizeof (edges[i].a)))
			++open_edges;
		else if (j - i != 2)
			++pinched; // surface nets joins two sheets that pass through one cell: closed, but not manifold
		i = j;
	}
	printf ("edges %d, pinched (non-manifold but closed) %d\n", num_edges / 2, pinched);
	CHECK (open_edges == 0, "%d edges are degenerate or not matched by an opposite use (holes, seams or flipped faces)", open_edges);

	// 4. winding agrees with the field gradient
	int backwards = 0;
	for (int i = 0; i < chunk_count; ++i)
		for (int k = 0; k < meshes[i].num_indices; k += 3)
		{
			const terrain_vertex_t *a = &meshes[i].verts[meshes[i].indices[k]], *b = &meshes[i].verts[meshes[i].indices[k + 1]],
								   *c = &meshes[i].verts[meshes[i].indices[k + 2]];
			float u[3], v[3];
			for (int j = 0; j < 3; ++j)
			{
				u[j] = b->pos[j] - a->pos[j];
				v[j] = c->pos[j] - a->pos[j];
			}
			const float n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
			const float g[3] = {a->normal[0] + b->normal[0] + c->normal[0], a->normal[1] + b->normal[1] + c->normal[1], a->normal[2] + b->normal[2] + c->normal[2]};
			backwards += n[0] * g[0] + n[1] * g[1] + n[2] * g[2] < 0;
		}
	CHECK (backwards * 200 < tris, "%d of %d triangles face against the field gradient", backwards, tris);

	// 5. a dig is local: only nearby chunks go dirty, and replay order across peers matches
	for (int i = 0; i < chunk_count; ++i)
		t->chunks[i].dirty = t2->chunks[i].dirty = 0;
	uint32_t	 rng = 12345;
	terrain_op_t ops[200];
	for (int i = 0; i < 200; ++i)
	{
		for (int a = 0; a < 3; ++a)
		{
			rng = rng * 1664525u + 1013904223u;
			ops[i].center[a] = (int32_t)(rng >> 8) % (size[a] * TERRAIN_CHUNK * TERRAIN_SUB);
		}
		rng = rng * 1664525u + 1013904223u;
		ops[i].kind = (rng >> 20) % 4 == 0 ? TERRAIN_OP_FILL : TERRAIN_OP_DIG;
		ops[i].material = TERRAIN_MAT_FILL;
		ops[i].radius = (uint16_t)(TERRAIN_SUB + (rng >> 9) % (4 * TERRAIN_SUB));
	}
	Terrain_ApplyOp (t, &ops[0]);
	int dirty = 0;
	for (int i = 0; i < chunk_count; ++i)
		dirty += t->chunks[i].dirty;
	CHECK (dirty <= 8, "one small op dirtied %d chunks", dirty);
	const double op_start = Now ();
	for (int i = 1; i < 200; ++i)
		Terrain_ApplyOp (t, &ops[i]);
	printf ("edit op: mean %.3f ms over 199 dig/fill ops (radius 1-5 cells)\n", (Now () - op_start) / 199);
	for (int i = 0; i < 200; ++i)
		Terrain_ApplyOp (t2, &ops[i]);
	CHECK (Terrain_Hash (t) == Terrain_Hash (t2), "two peers replaying the same ops diverged");
	printf ("field hash after 200 ops: %016llx\n", (unsigned long long)Terrain_Hash (t));

	// 6. collision: swept boxes through the pristine seed-2 cave
	{
		static const float boxes[2][2][3] = {{{-16, -16, -24}, {16, 16, 32}}, {{0, 0, 0}, {0, 0, 0}}};
		uint32_t		   r = 777;
		int				   traces = 0, hits = 0, end_overlaps = 0, tunnelled = 0, phantom = 0;
		double			   box_ms = 0, ray_ms = 0;
		int				   box_n = 0, ray_n = 0;
#define RND() ((r = r * 1664525u + 1013904223u) >> 8)
		while (traces < 4000)
		{
			float start[3], end[3], dir[3], len2 = 0;
			for (int a = 0; a < 3; ++a)
			{
				start[a] = (float)origin[a] + (float)(RND () % (uint32_t)(size[a] * TERRAIN_CHUNK_UNITS));
				dir[a] = (float)(RND () % 2001) / 1000.0f - 1.0f;
				len2 += dir[a] * dir[a];
			}
			if (len2 < 0.01f || Terrain_SampleWorld (t3, start) < 45.0f)
				continue;
			const int	kind = traces & 1;
			const float length = kind ? 200.0f + (float)(RND () % 1800) : 8.0f + (float)(RND () % 300);
			for (int a = 0; a < 3; ++a)
				end[a] = start[a] + dir[a] * length / sqrtf (len2);
			terrain_hit_t hit;
			const double  t0 = Now ();
			Terrain_TraceBox (t3, start, boxes[kind][0], boxes[kind][1], end, &hit);
			const double ms = Now () - t0;
			if (kind)
				ray_ms += ms, ++ray_n;
			else
				box_ms += ms, ++box_n;
			++traces;
			CHECK (!hit.startsolid, "trace %d started solid from open space", traces);
			float pos[3];
			for (int a = 0; a < 3; ++a)
				pos[a] = start[a] + (end[a] - start[a]) * hit.fraction;
			// A ray has no volume to overlap with, so rays are judged against the field instead. The drawn
			// surface and the field's zero level differ by a fraction of a cell, hence the tolerances.
			if (kind)
				end_overlaps += Terrain_SampleWorld (t3, pos) < -6.0f;
			else
				end_overlaps += Terrain_BoxOverlaps (t3, pos, boxes[0][0], boxes[0][1]);
			// brute force: no position on the way there may be inside either
			const int steps = (int)(length * hit.fraction);
			for (int k = 0; k < steps; ++k)
			{
				float p[3];
				for (int a = 0; a < 3; ++a)
					p[a] = start[a] + (end[a] - start[a]) * ((float)k / length);
				if (kind ? Terrain_SampleWorld (t3, p) < -10.0f : Terrain_BoxOverlaps (t3, p, boxes[0][0], boxes[0][1]))
				{
					++tunnelled;
					break;
				}
			}
			if (hit.fraction < 1.0f)
			{
				// the stop is real: a ray ends near the zero level; a box nudged a quarter unit into the
				// reported plane overlaps
				++hits;
				float p[3];
				for (int a = 0; a < 3; ++a)
					p[a] = pos[a] - hit.normal[a] * 0.25f;
				phantom += kind ? Terrain_SampleWorld (t3, pos) > 8.0f : !Terrain_BoxOverlaps (t3, p, boxes[0][0], boxes[0][1]);
			}
		}
		printf ("traces %d (hits %d): end overlaps %d, tunnelled %d, phantom stops %d\n", traces, hits, end_overlaps, tunnelled, phantom);
		printf ("trace cost: player box (8-308 units) %.4f ms, ray (200-2000 units) %.4f ms\n", box_ms / box_n, ray_ms / ray_n);
		CHECK (hits > traces / 4, "too few traces hit anything");
		CHECK (end_overlaps == 0, "%d traces ended overlapping the surface", end_overlaps);
		CHECK (tunnelled == 0, "%d traces passed through the surface", tunnelled);
		CHECK (phantom * 100 <= hits, "%d of %d stops had nothing behind them", phantom, hits);
	}

	if (argc > 1)
	{
		// pristine seed-1 cave as OBJ, for looking at the mesh outside the engine
		FILE *f = fopen (argv[1], "w");
		int	  base = 1;
		for (int i = 0; f && i < chunk_count; ++i)
		{
			const int cx = i % size[0], cy = i / size[0] % size[1], cz = i / (size[0] * size[1]);
			Terrain_MeshChunk (t3, cx, cy, cz, &meshes[i]);
			(void)cx;
		}
		for (int i = 0; f && i < chunk_count; ++i)
		{
			for (int k = 0; k < meshes[i].num_verts; ++k)
				fprintf (f, "v %.3f %.3f %.3f\n", meshes[i].verts[k].pos[0], meshes[i].verts[k].pos[1], meshes[i].verts[k].pos[2]);
			for (int k = 0; k < meshes[i].num_indices; k += 3)
				fprintf (f, "f %d %d %d\n", base + (int)meshes[i].indices[k], base + (int)meshes[i].indices[k + 1], base + (int)meshes[i].indices[k + 2]);
			base += meshes[i].num_verts;
		}
		if (f)
			fclose (f);
	}

	printf (failures ? "TERRAIN_FIXTURE_FAILED (%d)\n" : "TERRAIN_FIXTURE_PASSED\n", failures);
	return failures != 0;
}
