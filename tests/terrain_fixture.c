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

	// 7. walking navigation on the pristine seed-1 cave
	{
		terrain_t *nt = Terrain_Create (origin, size, 1);
		Terrain_Generate (nt);
		terrain_nav_t nav;
		Terrain_NavInit (&nav, nt);
		double n0 = Now ();
		Terrain_NavRefreshAll (&nav, nt);
		const double refresh_ms = Now () - n0;
		int			 stand[TERRAIN_NAV_CLASSES] = {0};
		for (int i = 0; i < nav.count; ++i)
			for (int cls = 0; cls < TERRAIN_NAV_CLASSES; ++cls)
				stand[cls] += (nav.flags[i] & (TERRAIN_NAV_STAND << cls)) != 0;
		uint16_t   *dist = malloc ((size_t)nav.count * sizeof (uint16_t));
		uint16_t   *dist2 = malloc ((size_t)nav.count * sizeof (uint16_t));
		// the generator always opens a room at the centre; stand on its floor
		const float centre[3] = {
			(float)origin[0] + size[0] * TERRAIN_CHUNK_UNITS * 0.5f, (float)origin[1] + size[1] * TERRAIN_CHUNK_UNITS * 0.5f,
			(float)origin[2] + size[2] * TERRAIN_CHUNK_UNITS * 0.5f - 150.0f};
		const int seed = Terrain_NavFindStand (&nav, 0, centre, 2, 4);
		CHECK (seed >= 0, "no stand cell on the floor of the centre room");
		n0 = Now ();
		Terrain_NavFlood (&nav, 0, &seed, 1, dist);
		const double flood_ms = Now () - n0;
		int			 reached = 0, far_cell = -1;
		for (int i = 0; i < nav.count; ++i)
			if (dist[i] != TERRAIN_NAV_FAR)
			{
				++reached;
				if (far_cell < 0 || dist[i] > dist[far_cell])
					far_cell = i;
			}
		printf (
			"nav: %d x %d x %d cells, stand %d (wide %d), reachable from the centre %d, farthest costs %d\n", nav.dim[0], nav.dim[1], nav.dim[2], stand[0],
			stand[1], reached, far_cell >= 0 ? dist[far_cell] : -1);
		printf ("nav cost: full refresh %.2f ms, flood %.2f ms\n", refresh_ms, flood_ms);
		CHECK (reached > 400, "only %d stand cells reachable on foot from the centre", reached);
		CHECK (stand[1] > 0 && stand[1] < stand[0], "wide bodies should stand on fewer cells (%d of %d)", stand[1], stand[0]);

		// following the field from the farthest cell arrives in exactly its number of steps, and every
		// step has room for a player-sized box at body height
		int path[4096], path_len = 0, blocked_links = 0;
		for (int cell = far_cell; cell >= 0 && path_len < 4096; cell = Terrain_NavNext (&nav, 0, dist, cell))
			path[path_len++] = cell;
		CHECK (path_len > 1 && path[path_len - 1] == seed, "the route from the farthest cell misses the target");
		for (int k = 0; k + 1 < path_len; ++k)
		{
			CHECK (dist[path[k + 1]] + Terrain_NavStepCost (&nav, path[k], path[k + 1]) == dist[path[k]], "route step %d is not on a cheapest way", k);
			const float	  mins[3] = {-16, -16, -24}, maxs[3] = {16, 16, 32};
			float		  a[3], b[3];
			terrain_hit_t hit;
			Terrain_NavCellCenter (&nav, path[k], a);
			Terrain_NavCellCenter (&nav, path[k + 1], b);
			a[2] = b[2] = (a[2] > b[2] ? a[2] : b[2]) + 24.0f; // feet at the higher cell's centre
			blocked_links += Terrain_TraceBox (nt, a, mins, maxs, b, &hit) && (hit.startsolid || hit.fraction < 0.5f);
		}
		int drops = 0;
		for (int k = 0; k + 1 < path_len; ++k)
			drops += Terrain_NavIsDrop (&nav, path[k], path[k + 1]);
		printf ("nav route: %d steps (%d off a ledge), %d with a wall at body height\n", path_len - 1, drops, blocked_links);
		CHECK (blocked_links * 10 <= path_len, "%d of %d route steps run into a wall", blocked_links, path_len - 1);

		// jumps: a target up on the highest floor of the cave is out of reach of most of it on foot;
		// jumps of up to 3 levels bring more of the cave to it, and no route gets dearer
		{
			int high = -1;
			for (int i = 0; i < nav.count; ++i)
				if ((nav.flags[i] & TERRAIN_NAV_STAND) && dist[i] != TERRAIN_NAV_FAR &&
					(high < 0 || i / (nav.dim[0] * nav.dim[1]) > high / (nav.dim[0] * nav.dim[1])))
					high = i;
			int reach_walk = 0, reach_jump = 0, dearer = 0, climbs = 0;
			Terrain_NavFlood (&nav, 0, &high, 1, dist2);
			uint16_t *walk_only = malloc ((size_t)nav.count * sizeof (uint16_t));
			memcpy (walk_only, dist2, (size_t)nav.count * sizeof (uint16_t));
			nav.max_climb = 3;
			Terrain_NavFlood (&nav, 0, &high, 1, dist2);
			int sample = -1;
			for (int i = 0; i < nav.count; ++i)
			{
				reach_walk += walk_only[i] != TERRAIN_NAV_FAR;
				reach_jump += dist2[i] != TERRAIN_NAV_FAR;
				dearer += walk_only[i] != TERRAIN_NAV_FAR && dist2[i] > walk_only[i];
				if (sample < 0 && dist2[i] != TERRAIN_NAV_FAR && walk_only[i] == TERRAIN_NAV_FAR)
					sample = i;
			}
			for (int cell = sample, next; cell >= 0 && (next = Terrain_NavNext (&nav, 0, dist2, cell)) >= 0; cell = next)
				climbs += Terrain_NavIsClimb (&nav, cell, next);
			printf (
				"nav to the highest floor: %d cells reach it on foot, %d with jumps of up to 3 levels (%d jumps on one such route), %d dearer\n", reach_walk,
				reach_jump, climbs, dearer);
			CHECK (reach_jump > reach_walk, "jumps brought nothing new to the highest floor");
			CHECK (!dearer, "allowing jumps made %d routes dearer", dearer);
			CHECK (sample < 0 || climbs > 0, "a route only jumps make possible has no jump");
			free (walk_only);
			nav.max_climb = 0;
		}
		// block the route halfway, then dig it open again: the far cell's distance has to follow
		const int mid = path[path_len / 2];
		float	  mid_pos[3];
		Terrain_NavCellCenter (&nav, mid, mid_pos);
		terrain_op_t op = {TERRAIN_OP_FILL, TERRAIN_MAT_FILL, 7 * TERRAIN_SUB, {0, 0, 0}};
		for (int a = 0; a < 3; ++a)
			op.center[a] = (int32_t)((mid_pos[a] - (float)origin[a]) * TERRAIN_SUB / TERRAIN_CELL);
		Terrain_ApplyOp (nt, &op);
		Terrain_NavRefreshOp (&nav, nt, &op);
		CHECK (!(nav.flags[mid] & TERRAIN_NAV_STAND), "a filled cell is still somewhere to stand");
		Terrain_NavFlood (&nav, 0, &seed, 1, dist2);
		printf ("nav: after a fill on the route the far cell costs %d (was %d)\n", dist2[far_cell] == TERRAIN_NAV_FAR ? -1 : dist2[far_cell], dist[far_cell]);
		CHECK (dist2[far_cell] >= dist[far_cell], "filling the route shortened it");
		int through_fill = 0;
		for (int cell = far_cell; cell >= 0; cell = Terrain_NavNext (&nav, 0, dist2, cell))
		{
			float p[3], d2 = 0;
			Terrain_NavCellCenter (&nav, cell, p);
			for (int a = 0; a < 3; ++a)
				d2 += (p[a] - mid_pos[a]) * (p[a] - mid_pos[a]);
			through_fill += d2 < 96.0f * 96.0f;
		}
		CHECK (!through_fill, "the new route still goes through %d filled cells", through_fill);
		op.kind = TERRAIN_OP_DIG;
		op.radius = 9 * TERRAIN_SUB; // wider than the fill, so it reopens the passage on both sides
		Terrain_ApplyOp (nt, &op);
		Terrain_NavRefreshOp (&nav, nt, &op);
		Terrain_NavFlood (&nav, 0, &seed, 1, dist2);
		CHECK (
			dist2[far_cell] != TERRAIN_NAV_FAR && dist2[far_cell] <= dist[far_cell] + 4, "digging the route open again did not restore it (%d steps)",
			dist2[far_cell]);

		// a refresh around each op gives what a full refresh gives
		uint8_t *partial = malloc (nav.count);
		for (int i = 0; i < 40; ++i)
		{
			Terrain_ApplyOp (nt, &ops[i]);
			Terrain_NavRefreshOp (&nav, nt, &ops[i]);
		}
		memcpy (partial, nav.flags, nav.count);
		Terrain_NavRefreshAll (&nav, nt);
		CHECK (!memcmp (partial, nav.flags, nav.count), "refreshing around ops differs from a full refresh");
		free (partial);
		free (dist);
		free (dist2);
		Terrain_NavFree (&nav);
		Terrain_Free (nt);
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
