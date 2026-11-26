/*
 * Particle lava for the editable terrain prototype. GPL-2.0-or-later, as the rest of the engine.
 *
 * A fixed pool of particles (structure of arrays) simulated on the server with the double-density
 * relaxation of Clavet, Beaudoin and Poulin (2005) on a uniform hash grid: gravity, pairwise
 * viscosity, predicted positions, density relaxation (pressure plus a sharper near pressure), then
 * collision with the terrain field and a velocity taken from the position change. Each particle
 * carries heat: it cools in the air and faster against terrain, gets thicker as it cools, and
 * freezes (is removed) when cold. Heat lost on contact becomes melt energy in a per-cell counter;
 * a full counter emits ONE dig op through SV_Terrain_Edit, limited by a per-stream token bucket.
 * Frozen particles can pile up into a fill op (dark rock).
 *
 * The core compiles on its own with -DLAVA_STANDALONE (tests/terrain_lava_fixture.c supplies
 * terrain_active and SV_Terrain_Edit); the engine glue (cvars, commands, emitters from the player,
 * damage) is below the core.
 */
#ifdef LAVA_STANDALONE
#define _POSIX_C_SOURCE 199309L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "terrain.h"
#define Con_Printf printf
static double LavaNow (void)
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}
#else
#include "quakedef.h"
#include "terrain.h"
static double LavaNow (void)
{
	return Sys_DoubleTime () * 1e3;
}
#endif

#define LMIN(a, b)		((a) < (b) ? (a) : (b))
#define LMAX(a, b)		((a) > (b) ? (a) : (b))
#define LCLAMP(l, v, h) ((v) < (l) ? (l) : (v) > (h) ? (h) : (v))

#define LAVA_MAX_CAP	16384
#define LAVA_GRID_BITS	15
#define LAVA_GRID_SIZE	(1 << LAVA_GRID_BITS)
#define LAVA_STREAMS	16
#define LAVA_EMITTERS	16
#define LAVA_CELLS		2048
#define LAVA_MAX_LIGHTS 8
#define LAVA_PAIRS_PER	40 // pair capacity per particle; denser clumps drop the extra pairs

typedef struct
{
	int	  cap;
	float dt, spacing, h, radius;
	float gravity, rest, stiff, near_stiff, cohesion;
	float visc, visc_quad, cold_visc;
	float friction, stick_time, adhesion;
	float cool_air, cool_contact, freeze;
	float melt, melt_threshold, dig_radius, ops_rate;
	float fill_count, fill_radius;
	float vmax;
} lava_params_t;

static lava_params_t P;

typedef struct
{
	int	  i, j;
	float q, u[3];
} pair_t;

typedef struct
{
	uint64_t key;
	double	 last;
	float	 e, fz;
	float	 pos[3], nrm[3];	// melt: weighted contact point and normal
	float	 fpos[3], fnrm[3]; // frozen particles
	int		 stream;
} cell_t;

typedef struct
{
	int	   active;
	float  pos[3], vel[3], spread;
	float  rate, left, acc;
	int	   stream;
	float  heat;
} emitter_t;

static struct
{
	int		cap, n;
	float  *x[3], *q[3], *v[3], *dq[3];
	float  *heat, *rho, *rhon, *wet;
	float  *sp[3], *sd, *gp[3], *gn[3]; // gp/gn: where the surface normal was last computed, and it // where the terrain was last sampled and the distance found there
	uint8_t *stream, *lost;
	uint64_t *ckey, *skey; // cell key per particle, and in grid order
	float  *sx[3];			// positions in grid order, for a sequential inner loop
	uint32_t *bucket;
	int	   *sorted, *cstart;
	pair_t *pairs;
	int		npairs, cap_pairs, pairs_dropped;
	cell_t	cells[LAVA_CELLS];
	float	tokens[LAVA_STREAMS];
	emitter_t em[LAVA_EMITTERS];
	int		next_stream;
	uint32_t rng;
	double	 sim_time, acc_time;
	// stats
	long	emitted, frozen, lost_count, dig_ops, fill_ops, blocked;
	int		last_touch, step_pairs;
	float	max_speed;
	double	t_grid, t_relax, t_terrain, t_total; // ms per step, smoothed
	int		steps;
	float	first_dig[3], first_before, first_after;
	int		have_first;
	// lights
	float	lights[LAVA_MAX_LIGHTS][4];
	int		num_lights;
	int		next_light_step;
	float	rho0;
	float	rho0_for_spacing, rho0_for_h;
} L;

static float Rand (void)
{
	L.rng ^= L.rng << 13;
	L.rng ^= L.rng >> 17;
	L.rng ^= L.rng << 5;
	return (L.rng & 0xffffff) / (float)0x1000000;
}

static void Lava_Defaults (void)
{
	P.cap = 4096;
	P.dt = 1.0f / 72.0f;
	P.spacing = 8.0f;
	P.h = 16.0f;
	P.radius = 4.0f;
	P.gravity = 800.0f;
	P.rest = 1.0f;
	P.stiff = 2500.0f;
	P.near_stiff = 7500.0f;
	P.cohesion = 1.0f;
	P.visc = 5.0f;
	P.visc_quad = 0.0f;
	P.cold_visc = 12.0f;
	P.friction = 12.0f;
	P.stick_time = 0.5f;
	P.adhesion = 1.0f;
	P.cool_air = 0.02f;
	P.cool_contact = 0.15f;
	P.freeze = 0.12f;
	P.melt = 1.0f;
	P.melt_threshold = 40.0f;
	P.dig_radius = 20.0f;
	P.ops_rate = 4.0f;
	P.fill_count = 10.0f;
	P.fill_radius = 11.0f;
	P.vmax = 1100.0f;
}

static void Lava_Free (void)
{
	for (int a = 0; a < 3; ++a)
	{
		free (L.x[a]);
		free (L.q[a]);
		free (L.v[a]);
		free (L.dq[a]);
		free (L.sp[a]);
		free (L.gp[a]);
		free (L.gn[a]);
		free (L.sx[a]);
		L.sx[a] = NULL;
		L.x[a] = L.q[a] = L.v[a] = L.dq[a] = L.sp[a] = L.gp[a] = L.gn[a] = NULL;
	}
	free (L.heat);
	free (L.rho);
	free (L.rhon);
	free (L.wet);
	free (L.sd);
	free (L.stream);
	free (L.lost);
	free (L.ckey);
	free (L.skey);
	L.skey = NULL;
	free (L.bucket);
	free (L.sorted);
	free (L.cstart);
	free (L.pairs);
	L.heat = L.rho = L.rhon = L.wet = L.sd = NULL;
	L.stream = L.lost = NULL;
	L.ckey = NULL;
	L.bucket = NULL;
	L.sorted = L.cstart = NULL;
	L.pairs = NULL;
	L.cap = L.n = 0;
}

static void Lava_Alloc (int cap)
{
	Lava_Free ();
	cap = LCLAMP (64, cap, LAVA_MAX_CAP);
	L.cap = cap;
	for (int a = 0; a < 3; ++a)
	{
		L.x[a] = (float *)calloc (cap, sizeof (float));
		L.q[a] = (float *)calloc (cap, sizeof (float));
		L.v[a] = (float *)calloc (cap, sizeof (float));
		L.dq[a] = (float *)calloc (cap, sizeof (float));
		L.sp[a] = (float *)calloc (cap, sizeof (float));
		L.gp[a] = (float *)calloc (cap, sizeof (float));
		L.gn[a] = (float *)calloc (cap, sizeof (float));
		L.sx[a] = (float *)calloc (cap, sizeof (float));
	}
	L.heat = (float *)calloc (cap, sizeof (float));
	L.rho = (float *)calloc (cap, sizeof (float));
	L.rhon = (float *)calloc (cap, sizeof (float));
	L.wet = (float *)calloc (cap, sizeof (float));
	L.sd = (float *)calloc (cap, sizeof (float));
	L.stream = (uint8_t *)calloc (cap, 1);
	L.lost = (uint8_t *)calloc (cap, 1);
	L.ckey = (uint64_t *)calloc (cap, sizeof (uint64_t));
	L.skey = (uint64_t *)calloc (cap, sizeof (uint64_t));
	L.bucket = (uint32_t *)calloc (cap, sizeof (uint32_t));
	L.sorted = (int *)calloc (cap, sizeof (int));
	L.cstart = (int *)calloc (LAVA_GRID_SIZE + 1, sizeof (int));
	L.cap_pairs = cap * LAVA_PAIRS_PER;
	L.pairs = (pair_t *)calloc (L.cap_pairs, sizeof (pair_t));
}

// Lattice sum of the density kernel at the rest spacing: what a settled puddle should read.
static float Lava_RestDensity (void)
{
	const float s = P.spacing, h = P.h;
	const int	m = (int)ceilf (h / s);
	float		sum = 0.0f;
	for (int a = -m; a <= m; ++a)
		for (int b = -m; b <= m; ++b)
			for (int c = -m; c <= m; ++c)
			{
				if (!a && !b && !c)
					continue;
				const float r = s * sqrtf ((float)(a * a + b * b + c * c));
				if (r >= h)
					continue;
				const float w = 1.0f - r / h;
				sum += w * w;
			}
	return sum;
}

static void Lava_ClearAll (void)
{
	L.n = 0;
	memset (L.em, 0, sizeof (L.em));
	memset (L.cells, 0, sizeof (L.cells));
	for (int i = 0; i < LAVA_STREAMS; ++i)
		L.tokens[i] = 1.0f;
	L.emitted = L.frozen = L.lost_count = L.dig_ops = L.fill_ops = L.blocked = 0;
	L.max_speed = 0;
	L.have_first = 0;
	L.num_lights = 0;
	L.acc_time = 0;
	L.sim_time = 0;
	L.pairs_dropped = 0;
}

/* ---- terrain helpers ---- */

static float Sample (const float p[3])
{
	return Terrain_SampleWorld (terrain_active, p);
}

// Gradient by forward differences from the value d already known at p.
static int Gradient (const float p[3], float d, float g[3])
{
	const float e = 3.0f;
	for (int a = 0; a < 3; ++a)
	{
		float s[3] = {p[0], p[1], p[2]};
		s[a] += e;
		g[a] = (Sample (s) - d) / e;
	}
	const float len = sqrtf (g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
	if (len < 0.2f)
		return 0;
	g[0] /= len;
	g[1] /= len;
	g[2] /= len;
	return 1;
}

/* ---- spawning ---- */

static int Spawn (const float pos[3], const float vel[3], float heat, int stream)
{
	if (L.n >= L.cap)
		return 0;
	if (Sample (pos) < P.radius)
	{
		++L.blocked;
		return 0;
	}
	const int i = L.n++;
	for (int a = 0; a < 3; ++a)
	{
		L.x[a][i] = L.q[a][i] = L.sp[a][i] = L.gp[a][i] = pos[a];
		L.gn[a][i] = 0.0f;
		L.v[a][i] = vel[a];
		L.dq[a][i] = 0;
	}
	L.heat[i] = heat;
	L.wet[i] = 0;
	L.sd[i] = 1000.0f;
	L.sp[0][i] = pos[0] + 100000.0f; // forces a first sample
	L.gp[0][i] = pos[0] + 100000.0f;
	L.stream[i] = (uint8_t)stream;
	L.lost[i] = 0;
	++L.emitted;
	return 1;
}

static void Lava_Emitters (float dt)
{
	for (int e = 0; e < LAVA_EMITTERS; ++e)
	{
		emitter_t *em = &L.em[e];
		if (!em->active)
			continue;
		if (em->left >= 0.0f)
		{
			em->left -= dt;
			if (em->left <= 0.0f)
			{
				em->active = 0;
				continue;
			}
		}
		em->acc += em->rate * dt;
		const float speed = sqrtf (em->vel[0] * em->vel[0] + em->vel[1] * em->vel[1] + em->vel[2] * em->vel[2]);
		while (em->acc >= 1.0f)
		{
			em->acc -= 1.0f;
			float p[3], v[3];
			const float t = Rand ();
			for (int a = 0; a < 3; ++a)
			{
				p[a] = em->pos[a] + em->vel[a] * dt * t + (Rand () - 0.5f) * 3.0f;
				v[a] = em->vel[a] * (0.93f + 0.14f * Rand ()) + (Rand () - 0.5f) * 2.0f * em->spread * speed;
			}
			if (!Spawn (p, v, em->heat, em->stream))
				++L.blocked;
		}
	}
}

/* ---- melt and fill counters ---- */

static cell_t *Cell (const float p[3], int create)
{
	const uint64_t kx = (uint64_t)(int)floorf (p[0] / TERRAIN_CELL) & 0xfffff, ky = (uint64_t)(int)floorf (p[1] / TERRAIN_CELL) & 0xfffff,
				   kz = (uint64_t)(int)floorf (p[2] / TERRAIN_CELL) & 0xfffff;
	const uint64_t key = (kx << 40) | (ky << 20) | kz | (1ull << 62);
	uint32_t	   h = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 40) & (LAVA_CELLS - 1);
	cell_t		  *free_slot = NULL;
	for (int probe = 0; probe < 16; ++probe, h = (h + 1) & (LAVA_CELLS - 1))
	{
		cell_t *c = &L.cells[h];
		if (c->key == key)
			return c;
		if (!c->key || L.sim_time - c->last > 8.0)
		{
			if (!free_slot)
				free_slot = c;
			if (!c->key)
				break;
		}
	}
	if (!create)
		return NULL;
	if (!free_slot)
		free_slot = &L.cells[h]; // all busy: evict the one we ended on
	memset (free_slot, 0, sizeof (*free_slot));
	free_slot->key = key;
	free_slot->last = L.sim_time;
	return free_slot;
}

static int TakeToken (int stream)
{
	if (L.tokens[stream] < 1.0f)
		return 0;
	L.tokens[stream] -= 1.0f;
	return 1;
}

#ifdef LAVA_STANDALONE
static int LavaFillBlocked (const float c[3], float r)
{
	(void)c;
	(void)r;
	return 0;
}
#else
static int LavaFillBlocked (const float c[3], float r);
#endif

static void MeltDeposit (const float contact[3], const float n[3], float energy, int stream)
{
	cell_t *c = Cell (contact, 1);
	const float keep = expf (-0.15f * (float)(L.sim_time - c->last));
	c->e *= keep;
	for (int a = 0; a < 3; ++a)
	{
		c->pos[a] *= keep;
		c->nrm[a] *= keep;
	}
	c->last = L.sim_time;
	c->stream = stream;
	for (int a = 0; a < 3; ++a)
	{
		c->pos[a] += contact[a] * energy;
		c->nrm[a] += n[a] * energy;
	}
	c->e += energy;
	if (c->e < P.melt_threshold || !TakeToken (stream))
		return;
	const float inv = 1.0f / c->e;
	float		nl = sqrtf (c->nrm[0] * c->nrm[0] + c->nrm[1] * c->nrm[1] + c->nrm[2] * c->nrm[2]);
	nl = nl > 1e-4f ? 1.0f / nl : 0.0f;
	float center[3];
	for (int a = 0; a < 3; ++a)
		center[a] = c->pos[a] * inv - c->nrm[a] * nl * (P.dig_radius * 0.35f); // into the surface
	const float before = Sample (center);
	const int	changed = SV_Terrain_Edit (TERRAIN_OP_DIG, center, P.dig_radius);
	if (changed > 0)
	{
		++L.dig_ops;
		if (!L.have_first)
		{
			L.have_first = 1;
			L.first_before = before;
			L.first_after = Sample (center);
			memcpy (L.first_dig, center, sizeof (center));
		}
	}
	c->e = 0.0f;
	memset (c->pos, 0, sizeof (c->pos));
	memset (c->nrm, 0, sizeof (c->nrm));
	// the terrain moved: every particle's cached clearance is stale
	for (int i = 0; i < L.n; ++i)
		L.sp[0][i] += 100000.0f;
}

static void FreezeDeposit (const float p[3], const float n[3])
{
	if (P.fill_count <= 0.0f)
		return;
	cell_t *c = Cell (p, 1);
	c->last = L.sim_time;
	for (int a = 0; a < 3; ++a)
	{
		c->fpos[a] += p[a];
		c->fnrm[a] += n[a];
	}
	c->fz += 1.0f;
	if (c->fz < P.fill_count)
		return;
	const float inv = 1.0f / c->fz;
	float		nl = sqrtf (c->fnrm[0] * c->fnrm[0] + c->fnrm[1] * c->fnrm[1] + c->fnrm[2] * c->fnrm[2]);
	nl = nl > 1e-4f ? 1.0f / nl : 0.0f;
	float center[3];
	for (int a = 0; a < 3; ++a)
		center[a] = c->fpos[a] * inv - c->fnrm[a] * nl * 3.0f;
	if (LavaFillBlocked (center, P.fill_radius))
	{
		c->fz = P.fill_count - 1.0f; // try again after one more
		return;
	}
	if (SV_Terrain_Edit (TERRAIN_OP_FILL, center, P.fill_radius) > 0)
		++L.fill_ops;
	c->fz = 0.0f;
	memset (c->fpos, 0, sizeof (c->fpos));
	memset (c->fnrm, 0, sizeof (c->fnrm));
	for (int i = 0; i < L.n; ++i)
		L.sp[0][i] += 100000.0f;
}

/* ---- neighbour search ---- */

static inline uint32_t BucketOf (uint64_t key)
{
	return (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> (64 - LAVA_GRID_BITS));
}

static inline uint64_t CellKey (int cx, int cy, int cz)
{
	return ((uint64_t)(cx & 0xfffff) << 40) | ((uint64_t)(cy & 0xfffff) << 20) | (uint64_t)(cz & 0xfffff);
}

static void BuildPairs (void)
{
	const int	n = L.n;
	const float inv_h = 1.0f / P.h, h2 = P.h * P.h;
	memset (L.cstart, 0, (LAVA_GRID_SIZE + 1) * sizeof (int));
	for (int i = 0; i < n; ++i)
	{
		const int cx = (int)floorf (L.q[0][i] * inv_h), cy = (int)floorf (L.q[1][i] * inv_h), cz = (int)floorf (L.q[2][i] * inv_h);
		const uint64_t k = CellKey (cx, cy, cz);
		L.ckey[i] = k;
		const uint32_t b = BucketOf (k);
		L.bucket[i] = b;
		++L.cstart[b + 1];
	}
	for (int b = 0; b < LAVA_GRID_SIZE; ++b)
		L.cstart[b + 1] += L.cstart[b];
	// scatter into grid order using the start offsets as running cursors, then restore them
	for (int i = 0; i < n; ++i)
	{
		const int s = L.cstart[L.bucket[i]]++;
		L.sorted[s] = i;
		L.skey[s] = L.ckey[i];
		L.sx[0][s] = L.q[0][i];
		L.sx[1][s] = L.q[1][i];
		L.sx[2][s] = L.q[2][i];
	}
	for (int b = LAVA_GRID_SIZE; b > 0; --b)
		L.cstart[b] = L.cstart[b - 1];
	L.cstart[0] = 0;

	static const int stencil[13][3] = {{1, 0, 0},  {-1, 1, 0}, {0, 1, 0},	{1, 1, 0}, {-1, -1, 1}, {0, -1, 1}, {1, -1, 1},
									   {-1, 0, 1}, {0, 0, 1},  {1, 0, 1}, {-1, 1, 1}, {0, 1, 1},	{1, 1, 1}};
	int				  np = 0;
	const int		  cap = L.cap_pairs;
	uint64_t		  last_key = ~0ull;
	uint64_t		  skey[14];
	int				  slo[14], shi[14];
	for (int s = 0; s < n; ++s)
	{
		const int	i = L.sorted[s];
		const float xi = L.sx[0][s], yi = L.sx[1][s], zi = L.sx[2][s];
		const uint64_t ki = L.skey[s];
		if (ki != last_key)
		{
			// the cell's own bucket and its 13 forward neighbours: one lookup per cell run, not per particle
			last_key = ki;
			const int cx = (int)((ki >> 40) & 0xfffff), cy = (int)((ki >> 20) & 0xfffff), cz = (int)(ki & 0xfffff);
			skey[0] = ki;
			const uint32_t b0 = BucketOf (ki);
			slo[0] = 0;
			shi[0] = L.cstart[b0 + 1];
			for (int t = 0; t < 13; ++t)
			{
				// signed wrap of the 20-bit coordinates is harmless: keys only need to match their owners
				skey[t + 1] = CellKey (cx + stencil[t][0], cy + stencil[t][1], cz + stencil[t][2]);
				const uint32_t b = BucketOf (skey[t + 1]);
				slo[t + 1] = L.cstart[b];
				shi[t + 1] = L.cstart[b + 1];
			}
		}
		for (int t = 0; t < 14; ++t)
		{
			const uint64_t key = skey[t];
			const int	   lo = t ? slo[t] : s + 1, hi = shi[t];
			for (int m = lo; m < hi; ++m)
			{
				if (L.skey[m] != key)
					continue;
				const float dx = L.sx[0][m] - xi, dy = L.sx[1][m] - yi, dz = L.sx[2][m] - zi;
				const float r2 = dx * dx + dy * dy + dz * dz;
				if (r2 >= h2)
					continue;
				if (np >= cap)
				{
					++L.pairs_dropped;
					continue;
				}
				pair_t *p = &L.pairs[np++];
				p->i = i;
				p->j = L.sorted[m];
				if (r2 < 1e-6f)
				{
					p->q = 0.0f;
					p->u[0] = Rand () - 0.5f;
					p->u[1] = Rand () - 0.5f;
					p->u[2] = Rand () - 0.5f;
					const float l = 1.0f / sqrtf (p->u[0] * p->u[0] + p->u[1] * p->u[1] + p->u[2] * p->u[2] + 1e-9f);
					p->u[0] *= l;
					p->u[1] *= l;
					p->u[2] *= l;
				}
				else
				{
					const float r = sqrtf (r2), inv = 1.0f / r;
					p->q = r * inv_h;
					p->u[0] = dx * inv;
					p->u[1] = dy * inv;
					p->u[2] = dz * inv;
				}
			}
		}
	}
	L.npairs = np;
}

/* ---- relaxation ---- */

static void Relax (float dt)
{
	const int	n = L.n, np = L.npairs;
	const pair_t *pr = L.pairs;

	// viscosity: impulses on approaching pairs (linear and quadratic) plus shear smoothing of the
	// whole relative velocity, thicker as the pair cools
	for (int k = 0; k < np; ++k)
	{
		const int	i = pr[k].i, j = pr[k].j;
		const float w = 1.0f - pr[k].q;
		const float cold = 1.0f - 0.5f * (L.heat[i] + L.heat[j]);
		const float mult = 1.0f + P.cold_visc * cold * cold;
		const float dvx = L.v[0][j] - L.v[0][i], dvy = L.v[1][j] - L.v[1][i], dvz = L.v[2][j] - L.v[2][i];
		const float *u = pr[k].u;
		const float un = -(dvx * u[0] + dvy * u[1] + dvz * u[2]); // approach speed
		float		s = LMIN (0.5f, P.visc * mult * dt * w);
		float		ix = dvx * s, iy = dvy * s, iz = dvz * s;
		if (un > 0.0f)
		{
			const float imp = LMIN (0.5f * un, 0.5f * dt * w * mult * (P.visc * un + P.visc_quad * un * un));
			ix -= imp * u[0];
			iy -= imp * u[1];
			iz -= imp * u[2];
		}
		L.v[0][i] += ix;
		L.v[1][i] += iy;
		L.v[2][i] += iz;
		L.v[0][j] -= ix;
		L.v[1][j] -= iy;
		L.v[2][j] -= iz;
	}
	for (int i = 0; i < n; ++i)
	{
		L.q[0][i] = L.x[0][i] + dt * L.v[0][i];
		L.q[1][i] = L.x[1][i] + dt * L.v[1][i];
		L.q[2][i] = L.x[2][i] + dt * L.v[2][i];
		L.rho[i] = L.rhon[i] = 0.0f;
		L.dq[0][i] = L.dq[1][i] = L.dq[2][i] = 0.0f;
	}
	for (int k = 0; k < np; ++k)
	{
		const float w = 1.0f - pr[k].q, w2 = w * w, w3 = w2 * w;
		L.rho[pr[k].i] += w2;
		L.rho[pr[k].j] += w2;
		L.rhon[pr[k].i] += w3;
		L.rhon[pr[k].j] += w3;
	}
	const float rho0 = L.rho0 * P.rest, dt2 = dt * dt, h = P.h;
	for (int k = 0; k < np; ++k)
	{
		const int	i = pr[k].i, j = pr[k].j;
		const float w = 1.0f - pr[k].q;
		float		pi = P.stiff * (L.rho[i] - rho0), pj = P.stiff * (L.rho[j] - rho0);
		if (pi < 0.0f)
			pi *= P.cohesion;
		if (pj < 0.0f)
			pj *= P.cohesion;
		const float pni = P.near_stiff * L.rhon[i], pnj = P.near_stiff * L.rhon[j];
		float		m = 0.5f * dt2 * ((pi + pj) * w + (pni + pnj) * w * w);
		const float r = pr[k].q * h;
		m = LCLAMP (-0.25f * r, m, 0.2f * h);
		const float *u = pr[k].u;
		L.dq[0][i] -= m * u[0];
		L.dq[1][i] -= m * u[1];
		L.dq[2][i] -= m * u[2];
		L.dq[0][j] += m * u[0];
		L.dq[1][j] += m * u[1];
		L.dq[2][j] += m * u[2];
	}
	const float maxd = 0.45f * P.h;
	for (int i = 0; i < n; ++i)
	{
		float dx = L.dq[0][i], dy = L.dq[1][i], dz = L.dq[2][i];
		const float l2 = dx * dx + dy * dy + dz * dz;
		if (l2 > maxd * maxd)
		{
			const float s = maxd / sqrtf (l2);
			dx *= s;
			dy *= s;
			dz *= s;
		}
		L.q[0][i] += dx;
		L.q[1][i] += dy;
		L.q[2][i] += dz;
	}
}

/* ---- terrain collision, heat, contact response ---- */

static void Collide (float dt)
{
	const float r = P.radius, touch = r + 1.5f;
	int			touching = 0;
	for (int i = 0; i < L.n; ++i)
	{
		float p[3] = {L.q[0][i], L.q[1][i], L.q[2][i]};
		const float mx = p[0] - L.sp[0][i], my = p[1] - L.sp[1][i], mz = p[2] - L.sp[2][i];
		const float moved = sqrtf (mx * mx + my * my + mz * mz);
		float		d = L.sd[i] - moved; // lower bound while the cached sample is valid
		float		n[3] = {0, 0, 0};
		int			contact = 0;
		if (d < touch + 2.0f)
		{
			d = Sample (p);
			L.sp[0][i] = p[0];
			L.sp[1][i] = p[1];
			L.sp[2][i] = p[2];
			L.sd[i] = d;
			if (d < touch)
			{
				contact = 1;
				// the normal is reused while the particle stays within a few units of where it was taken
				const float gx = p[0] - L.gp[0][i], gy = p[1] - L.gp[1][i], gz = p[2] - L.gp[2][i];
				if (gx * gx + gy * gy + gz * gz > 9.0f || d < -2.0f)
				{
					if (!Gradient (p, d, n))
						contact = 0;
					else
					{
						for (int a = 0; a < 3; ++a)
						{
							L.gn[a][i] = n[a];
							L.gp[a][i] = p[a];
						}
					}
				}
				else
					for (int a = 0; a < 3; ++a)
						n[a] = L.gn[a][i];
				if (contact && d < r)
				{
					if (d > -30.0f)
					{
						const float push = r - d;
						for (int a = 0; a < 3; ++a)
							p[a] += n[a] * push;
						L.sd[i] = r;
						for (int a = 0; a < 3; ++a)
							L.sp[a][i] = p[a];
					}
					else
					{
						// deep in rock: go back to where we were
						p[0] = L.x[0][i];
						p[1] = L.x[1][i];
						p[2] = L.x[2][i];
						L.v[0][i] = L.v[1][i] = L.v[2][i] = 0.0f;
						if (++L.lost[i] > 8)
						{
							L.heat[i] = -1.0f;
							++L.lost_count;
						}
						L.sd[i] = 1000.0f;
						L.sp[0][i] = p[0] + 100000.0f;
						L.q[0][i] = p[0];
						L.q[1][i] = p[1];
						L.q[2][i] = p[2];
						continue;
					}
				}
			}
		}
		L.q[0][i] = p[0];
		L.q[1][i] = p[1];
		L.q[2][i] = p[2];

		// velocity from position change
		float v[3] = {(p[0] - L.x[0][i]) / dt, (p[1] - L.x[1][i]) / dt, (p[2] - L.x[2][i]) / dt};
		const float cold = 1.0f - L.heat[i];
		L.heat[i] -= P.cool_air * dt;
		if (contact)
		{
			++touching;
			const float vn = v[0] * n[0] + v[1] * n[1] + v[2] * n[2];
			if (vn < 0.0f)
				for (int a = 0; a < 3; ++a)
					v[a] -= vn * n[a];
			const float fr = LMIN (1.0f, P.friction * (1.0f + 4.0f * cold) * dt);
			const float vn2 = v[0] * n[0] + v[1] * n[1] + v[2] * n[2];
			for (int a = 0; a < 3; ++a)
				v[a] = n[a] * vn2 + (v[a] - n[a] * vn2) * (1.0f - fr);
			// cling: pull into the surface while the contact is young, so it can hang and then drip
			const float hold = LCLAMP (0.0f, 1.0f - L.wet[i] / (P.stick_time * (1.0f + 3.0f * cold)), 1.0f);
			for (int a = 0; a < 3; ++a)
				v[a] -= n[a] * P.adhesion * hold * 0.6f * P.gravity * dt;
			L.wet[i] += dt;
			const float lost = P.cool_contact * (0.4f + cold) * dt;
			L.heat[i] -= lost;
			const float dd = d < r ? r : d;
			const float contact_pt[3] = {p[0] - n[0] * dd, p[1] - n[1] * dd, p[2] - n[2] * dd};
			const float e = L.heat[i] * P.melt * 10.0f * dt; // hot lava melts at a rate set by its heat
			MeltDeposit (contact_pt, n, e, L.stream[i]);
		}
		else
			L.wet[i] = LMAX (0.0f, L.wet[i] - 2.0f * dt);
		if (L.heat[i] < P.freeze && L.heat[i] >= 0.0f)
		{
			float nn[3] = {n[0], n[1], n[2]};
			if (!contact)
				Gradient (p, LMAX (L.sd[i], 1.0f), nn);
			FreezeDeposit (p, nn);
			L.heat[i] = -1.0f;
			++L.frozen;
		}
		const float sp2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
		if (sp2 > P.vmax * P.vmax)
		{
			const float s = P.vmax / sqrtf (sp2);
			v[0] *= s;
			v[1] *= s;
			v[2] *= s;
		}
		L.v[0][i] = v[0];
		L.v[1][i] = v[1];
		L.v[2][i] = v[2];
		L.x[0][i] = p[0];
		L.x[1][i] = p[1];
		L.x[2][i] = p[2];
	}
	L.last_touch = touching;
}

static void Compact (void)
{
	for (int i = 0; i < L.n;)
	{
		if (L.heat[i] >= 0.0f)
		{
			++i;
			continue;
		}
		const int last = --L.n;
		if (i != last)
		{
			for (int a = 0; a < 3; ++a)
			{
				L.x[a][i] = L.x[a][last];
				L.v[a][i] = L.v[a][last];
				L.sp[a][i] = L.sp[a][last];
				L.gp[a][i] = L.gp[a][last];
				L.gn[a][i] = L.gn[a][last];
			}
			L.heat[i] = L.heat[last];
			L.wet[i] = L.wet[last];
			L.sd[i] = L.sd[last];
			L.stream[i] = L.stream[last];
			L.lost[i] = L.lost[last];
		}
	}
}

/* ---- lights ---- */

static void UpdateLights (void)
{
	typedef struct
	{
		int32_t k[3];
		float	w, p[3];
	} bin_t;
	enum { NB = 256 };
	static bin_t bins[NB];
	memset (bins, 0, sizeof (bins));
	const float cell = 80.0f;
	for (int i = 0; i < L.n; ++i)
	{
		if (L.heat[i] < 0.35f)
			continue;
		const int32_t kx = (int32_t)floorf (L.x[0][i] / cell), ky = (int32_t)floorf (L.x[1][i] / cell), kz = (int32_t)floorf (L.x[2][i] / cell);
		uint32_t h = ((uint32_t)kx * 73856093u ^ (uint32_t)ky * 19349663u ^ (uint32_t)kz * 83492791u) & (NB - 1);
		for (int probe = 0; probe < NB; ++probe, h = (h + 1) & (NB - 1))
		{
			bin_t *b = &bins[h];
			if (!b->w || (b->k[0] == kx && b->k[1] == ky && b->k[2] == kz))
			{
				if (!b->w)
				{
					b->k[0] = kx;
					b->k[1] = ky;
					b->k[2] = kz;
				}
				const float w = L.heat[i];
				b->w += w;
				for (int a = 0; a < 3; ++a)
					b->p[a] += L.x[a][i] * w;
				break;
			}
		}
	}
	L.num_lights = 0;
	for (int pick = 0; pick < LAVA_MAX_LIGHTS; ++pick)
	{
		int best = -1;
		for (int b = 0; b < NB; ++b)
			if (bins[b].w > 1.5f && (best < 0 || bins[b].w > bins[best].w))
				best = b;
		if (best < 0)
			break;
		float *o = L.lights[L.num_lights++];
		for (int a = 0; a < 3; ++a)
			o[a] = bins[best].p[a] / bins[best].w;
		o[3] = LMIN (320.0f, 110.0f + 16.0f * sqrtf (bins[best].w));
		bins[best].w = 0.0f;
	}
}

/* ---- the step ---- */

static void Lava_Step (void)
{
	const float dt = P.dt;
	const double t0 = LavaNow ();
	L.sim_time += dt;
	for (int s = 0; s < LAVA_STREAMS; ++s)
		L.tokens[s] = LMIN (2.0f, L.tokens[s] + P.ops_rate * dt);
	Lava_Emitters (dt);
	if (!L.n)
	{
		L.num_lights = 0;
		return;
	}
	// gravity and the prediction
	float maxv2 = 0.0f;
	for (int i = 0; i < L.n; ++i)
	{
		L.v[2][i] -= P.gravity * dt;
		L.q[0][i] = L.x[0][i] + dt * L.v[0][i];
		L.q[1][i] = L.x[1][i] + dt * L.v[1][i];
		L.q[2][i] = L.x[2][i] + dt * L.v[2][i];
	}
	const double t1 = LavaNow ();
	BuildPairs ();
	const double t2 = LavaNow ();
	Relax (dt);
	const double t3 = LavaNow ();
	Collide (dt);
	Compact ();
	const double t4 = LavaNow ();
	for (int i = 0; i < L.n; ++i)
	{
		const float s2 = L.v[0][i] * L.v[0][i] + L.v[1][i] * L.v[1][i] + L.v[2][i] * L.v[2][i];
		maxv2 = LMAX (maxv2, s2);
	}
	L.max_speed = sqrtf (maxv2);
	if (L.steps >= L.next_light_step)
	{
		UpdateLights ();
		L.next_light_step = L.steps + 4;
	}
	const double a = 0.1;
	L.t_grid += a * ((t2 - t1) - L.t_grid);
	L.t_relax += a * ((t3 - t2) - L.t_relax);
	L.t_terrain += a * ((t4 - t3) - L.t_terrain);
	L.t_total += a * ((t4 - t0) - L.t_total);
	L.step_pairs = L.npairs;
	++L.steps;
}

static void Lava_Setup (void)
{
	if (L.cap != LCLAMP (64, P.cap, LAVA_MAX_CAP))
	{
		Lava_Alloc (P.cap);
		Lava_ClearAll ();
	}
	L.rho0 = Lava_RestDensity ();
	if (!L.rng)
		L.rng = 2463534242u;
}

static void Lava_Advance (double frametime)
{
	Lava_Setup ();
	L.acc_time += LCLAMP (0.0, frametime, 0.1);
	int steps = 0;
	while (L.acc_time >= P.dt)
	{
		L.acc_time -= P.dt;
		if (++steps > 3)
		{
			L.acc_time = 0.0; // fell behind: drop the time rather than spiral
			break;
		}
		Lava_Step ();
	}
}

/* ---- statistics, shared with the fixture ---- */

typedef struct
{
	int	  live;
	float centroid[3], maxspeed, meanspeed, mean_neigh, inside_frac, mean_heat;
} lava_stats_t;

static void Lava_Stats (lava_stats_t *s)
{
	memset (s, 0, sizeof (*s));
	s->live = L.n;
	if (!L.n)
		return;
	int inside = 0;
	for (int i = 0; i < L.n; ++i)
	{
		for (int a = 0; a < 3; ++a)
			s->centroid[a] += L.x[a][i];
		s->mean_heat += L.heat[i];
		s->meanspeed += sqrtf (L.v[0][i] * L.v[0][i] + L.v[1][i] * L.v[1][i] + L.v[2][i] * L.v[2][i]);
		float p[3] = {L.x[0][i], L.x[1][i], L.x[2][i]};
		inside += Sample (p) < -8.0f;
	}
	for (int a = 0; a < 3; ++a)
		s->centroid[a] /= (float)L.n;
	s->mean_heat /= (float)L.n;
	s->meanspeed /= (float)L.n;
	s->maxspeed = L.max_speed;
	s->mean_neigh = 2.0f * (float)L.npairs / (float)L.n;
	s->inside_frac = (float)inside / (float)L.n;
}

static int Lava_AddEmitter (const float pos[3], const float vel[3], float rate, float seconds, float spread, float heat)
{
	for (int e = 1; e < LAVA_EMITTERS; ++e) // slot 0 is the weapon
		if (!L.em[e].active)
		{
			emitter_t *em = &L.em[e];
			memset (em, 0, sizeof (*em));
			em->active = 1;
			memcpy (em->pos, pos, sizeof (em->pos));
			memcpy (em->vel, vel, sizeof (em->vel));
			em->rate = rate;
			em->left = seconds;
			em->spread = spread;
			em->heat = heat;
			em->stream = e;
			return e;
		}
	return 0;
}

#ifndef LAVA_STANDALONE
/* ====================== engine glue ====================== */

static cvar_t terrain_lava_cap = {"terrain_lava_cap", "4096", CVAR_NONE};
static cvar_t terrain_lava_density = {"terrain_lava_density", "1.0", CVAR_NONE};		// multiplier on the lattice rest density
static cvar_t terrain_lava_stiffness = {"terrain_lava_stiffness", "2500", CVAR_NONE};
static cvar_t terrain_lava_near_stiffness = {"terrain_lava_near_stiffness", "7500", CVAR_NONE};
static cvar_t terrain_lava_cohesion = {"terrain_lava_cohesion", "1", CVAR_NONE};
static cvar_t terrain_lava_viscosity = {"terrain_lava_viscosity", "5", CVAR_NONE};
static cvar_t terrain_lava_cold_viscosity = {"terrain_lava_cold_viscosity", "12", CVAR_NONE};
static cvar_t terrain_lava_friction = {"terrain_lava_friction", "12", CVAR_NONE};
static cvar_t terrain_lava_stick = {"terrain_lava_stick", "0.5", CVAR_NONE};			// seconds a fresh contact clings
static cvar_t terrain_lava_cooling = {"terrain_lava_cooling", "0.02", CVAR_NONE};		// heat per second in the air
static cvar_t terrain_lava_contact_cooling = {"terrain_lava_contact_cooling", "0.15", CVAR_NONE};
static cvar_t terrain_lava_freeze = {"terrain_lava_freeze", "0.12", CVAR_NONE};
static cvar_t terrain_lava_melt = {"terrain_lava_melt", "1", CVAR_NONE};				// melt energy per heat lost on contact; 0 is off
static cvar_t terrain_lava_melt_threshold = {"terrain_lava_melt_threshold", "40", CVAR_NONE};
static cvar_t terrain_lava_dig_radius = {"terrain_lava_dig_radius", "20", CVAR_NONE};
static cvar_t terrain_lava_ops_rate = {"terrain_lava_ops_rate", "4", CVAR_NONE};		// dig ops per second per stream
static cvar_t terrain_lava_fill = {"terrain_lava_fill", "10", CVAR_NONE};				// frozen particles in a cell that leave rock; 0 is off
static cvar_t terrain_lava_heat = {"terrain_lava_heat", "1", CVAR_NONE};				// heat a new particle starts with
static cvar_t terrain_lava_rate = {"terrain_lava_rate", "220", CVAR_NONE};				// weapon particles per second
static cvar_t terrain_lava_speed = {"terrain_lava_speed", "600", CVAR_NONE};
static cvar_t terrain_lava_spread = {"terrain_lava_spread", "0.05", CVAR_NONE};
static cvar_t terrain_lava_damage = {"terrain_lava_damage", "4", CVAR_NONE};			// per half second; 0 is off
static cvar_t terrain_lava_gravity = {"terrain_lava_gravity", "800", CVAR_NONE};
static cvar_t terrain_lava_draw = {"terrain_lava_draw", "1", CVAR_NONE};
static cvar_t terrain_lava_lights = {"terrain_lava_lights", "1", CVAR_NONE};

static int	 lava_held;
static double lava_next_damage;

static void Lava_ReadCvars (void)
{
	Lava_Defaults ();
	P.cap = (int)terrain_lava_cap.value;
	P.rest = terrain_lava_density.value;
	P.stiff = terrain_lava_stiffness.value;
	P.near_stiff = terrain_lava_near_stiffness.value;
	P.cohesion = terrain_lava_cohesion.value;
	P.visc = terrain_lava_viscosity.value;
	P.cold_visc = terrain_lava_cold_viscosity.value;
	P.friction = terrain_lava_friction.value;
	P.stick_time = terrain_lava_stick.value;
	P.cool_air = terrain_lava_cooling.value;
	P.cool_contact = terrain_lava_contact_cooling.value;
	P.freeze = terrain_lava_freeze.value;
	P.melt = terrain_lava_melt.value;
	P.melt_threshold = LMAX (1.0f, terrain_lava_melt_threshold.value);
	P.dig_radius = terrain_lava_dig_radius.value;
	P.ops_rate = terrain_lava_ops_rate.value;
	P.fill_count = terrain_lava_fill.value;
	P.gravity = terrain_lava_gravity.value;
}

void SV_Terrain_LavaReset (void)
{
	Lava_Defaults ();
	Lava_ClearAll ();
	lava_held = 0;
}

static int LavaFillBlocked (const float c[3], float r)
{
	if (!sv.active || !qcvm || !qcvm->edicts)
		return 0;
	for (int e = 1; e < qcvm->num_edicts; ++e)
	{
		edict_t *ed = EDICT_NUM (e);
		if (ed->free || !ed->v.takedamage)
			continue;
		float d2 = 0.0f;
		for (int a = 0; a < 3; ++a)
		{
			const float lo = ed->v.origin[a] + ed->v.mins[a] - r - 8.0f, hi = ed->v.origin[a] + ed->v.maxs[a] + r + 8.0f;
			const float q = c[a] < lo ? lo - c[a] : c[a] > hi ? c[a] - hi : 0.0f;
			d2 += q * q;
		}
		if (d2 <= 0.0f)
			return 1;
	}
	return 0;
}

static void LavaHurt (edict_t *victim, float dmg)
{
	dfunction_t *td = ED_FindFunction ("T_Damage");
	if (td)
	{
		const int self = pr_global_struct->self, other = pr_global_struct->other;
		edict_t *world = qcvm->edicts;
		pr_global_struct->self = EDICT_TO_PROG (world);
		pr_global_struct->other = EDICT_TO_PROG (world);
		pr_global_struct->time = qcvm->time;
		G_INT (OFS_PARM0) = EDICT_TO_PROG (victim);
		G_INT (OFS_PARM1) = EDICT_TO_PROG (world);
		G_INT (OFS_PARM2) = EDICT_TO_PROG (world);
		G_FLOAT (OFS_PARM3) = dmg;
		PR_ExecuteProgram (td - qcvm->functions);
		pr_global_struct->self = self;
		pr_global_struct->other = other;
	}
	else
		victim->v.health -= dmg;
}

// Every half second, anything with health whose box has hot particles in it takes a little.
static void LavaDamage (void)
{
	if (terrain_lava_damage.value <= 0.0f || !L.n || qcvm->time < lava_next_damage)
		return;
	lava_next_damage = qcvm->time + 0.5;
	for (int e = 1; e < qcvm->num_edicts; ++e)
	{
		edict_t *ed = EDICT_NUM (e);
		if (ed->free || !ed->v.takedamage || ed->v.health <= 0.0f)
			continue;
		const float pad = P.radius + 3.0f;
		float		lo[3], hi[3];
		for (int a = 0; a < 3; ++a)
		{
			lo[a] = ed->v.origin[a] + ed->v.mins[a] - pad;
			hi[a] = ed->v.origin[a] + ed->v.maxs[a] + pad;
		}
		// the grid is keyed on the predicted positions of the last step; close enough for 0.5 s ticks
		int hot = 0;
		for (int i = 0; i < L.n && hot < 6; ++i)
			if (L.heat[i] > 0.4f && L.x[0][i] >= lo[0] && L.x[0][i] <= hi[0] && L.x[1][i] >= lo[1] && L.x[1][i] <= hi[1] && L.x[2][i] >= lo[2] && L.x[2][i] <= hi[2])
				++hot;
		if (hot)
			LavaHurt (ed, terrain_lava_damage.value * (float)q_min (hot, 4));
	}
}

static void LavaWeapon (void)
{
	emitter_t *em = &L.em[0];
	if (!lava_held || !sv_player || sv_player->free)
	{
		em->active = 0;
		return;
	}
	edict_t *pl = sv_player;
	vec3_t	 forward, right, up;
	AngleVectors (pl->v.v_angle, forward, right, up);
	float eye[3];
	for (int a = 0; a < 3; ++a)
		eye[a] = pl->v.origin[a] + pl->v.view_ofs[a];
	// the muzzle backs off when the eye is against rock
	float out[3];
	int	  found = 0;
	for (float dist = 24.0f; dist >= 0.0f; dist -= 6.0f)
	{
		for (int a = 0; a < 3; ++a)
			out[a] = eye[a] + forward[a] * dist - up[a] * 6.0f;
		if (Sample (out) > P.radius + 2.0f)
		{
			found = 1;
			break;
		}
	}
	if (!found)
	{
		em->active = 0;
		return;
	}
	const float speed = terrain_lava_speed.value;
	em->active = 1;
	em->left = -1.0f;
	em->stream = 0;
	em->rate = terrain_lava_rate.value;
	em->spread = terrain_lava_spread.value;
	em->heat = terrain_lava_heat.value;
	memcpy (em->pos, out, sizeof (out));
	for (int a = 0; a < 3; ++a)
		em->vel[a] = forward[a] * speed + pl->v.velocity[a] * 0.5f;
}

void SV_Terrain_LavaFrame (void)
{
	if (!terrain_active || !sv.active)
	{
		if (L.n)
			SV_Terrain_LavaReset ();
		return;
	}
	if (!L.n && !lava_held)
	{
		int any = 0;
		for (int e = 0; e < LAVA_EMITTERS; ++e)
			any |= L.em[e].active;
		if (!any)
			return;
	}
	Lava_ReadCvars ();
	LavaWeapon ();
	Lava_Advance (host_frametime);
	LavaDamage ();
}

/* ---- client view ---- */

static terrain_lava_view_t lava_view;

const terrain_lava_view_t *SV_Terrain_LavaView (void)
{
	if (!terrain_lava_draw.value || !sv.active || !L.n)
		return NULL;
	lava_view.count = L.n;
	for (int a = 0; a < 3; ++a)
		lava_view.pos[a] = L.x[a];
	lava_view.heat = L.heat;
	lava_view.radius = P.spacing * 0.9f;
	lava_view.num_lights = terrain_lava_lights.value ? L.num_lights : 0;
	lava_view.lights = L.lights;
	return &lava_view;
}

/* ---- commands ---- */

static int Ready (void)
{
	if (!terrain_active || !sv.active)
	{
		Con_Printf ("terrain_lava: needs a terrain and a local server\n");
		return 0;
	}
	Lava_ReadCvars ();
	Lava_Setup ();
	return 1;
}

static void LavaOn_f (void)
{
	if (Ready ())
		lava_held = 1;
}

static void LavaOff_f (void)
{
	lava_held = 0;
}

// terrain_lava_emit x y z dx dy dz rate seconds [spread] : the velocity is the vector dx dy dz
static void Emit_f (void)
{
	if (Cmd_Argc () < 9)
	{
		Con_Printf ("usage: terrain_lava_emit x y z vx vy vz rate seconds [spread]\n");
		return;
	}
	if (!Ready ())
		return;
	float pos[3], vel[3];
	for (int a = 0; a < 3; ++a)
	{
		pos[a] = atof (Cmd_Argv (1 + a));
		vel[a] = atof (Cmd_Argv (4 + a));
	}
	const float spread = Cmd_Argc () > 9 ? atof (Cmd_Argv (9)) : 0.03f;
	const int	slot = Lava_AddEmitter (pos, vel, atof (Cmd_Argv (7)), atof (Cmd_Argv (8)), spread, terrain_lava_heat.value);
	if (!slot)
		Con_Printf ("terrain_lava_emit: no free emitter\n");
}

static void Clear_f (void)
{
	Lava_ClearAll ();
}

static void Stop_f (void)
{
	for (int e = 0; e < LAVA_EMITTERS; ++e)
		L.em[e].active = 0;
	lava_held = 0;
}

static void Info_f (void)
{
	if (!Ready ())
		return;
	lava_stats_t s;
	Lava_Stats (&s);
	int active = 0;
	for (int e = 0; e < LAVA_EMITTERS; ++e)
		active += L.em[e].active;
	Con_Printf ("terrain_lava_info: live %d frozen %ld emitted %ld lost %ld blocked %ld\n", s.live, L.frozen, L.emitted, L.lost_count, L.blocked);
	Con_Printf ("terrain_lava_info: ops %ld dig %ld fill %ld emitters %d t %.2f\n", L.dig_ops + L.fill_ops, L.dig_ops, L.fill_ops, active, L.sim_time);
	Con_Printf ("terrain_lava_info: speed max %.1f mean %.1f meanneigh %.1f inside %.4f\n", s.maxspeed, s.meanspeed, s.mean_neigh, s.inside_frac);
	Con_Printf ("terrain_lava_info: centroid %.1f %.1f %.1f meanheat %.2f lights %d\n", s.centroid[0], s.centroid[1], s.centroid[2], s.mean_heat, L.num_lights);
	Con_Printf ("terrain_lava_info: cost_ms %.3f neighbour %.3f relax %.3f terrain %.3f\n", L.t_total, L.t_grid, L.t_relax, L.t_terrain);
	Con_Printf ("terrain_lava_info: steps %d cap %d pairs %d dropped %d touching %d\n", L.steps, L.cap, L.npairs, L.pairs_dropped, L.last_touch);
	if (sv_player && !sv_player->free)
		Con_Printf ("terrain_lava_info: player health %.0f\n", sv_player->v.health);
	if (L.have_first)
		Con_Printf ("terrain_lava_info: first dig %.0f %.0f %.0f was %.1f after %.1f now %.1f\n", L.first_dig[0], L.first_dig[1], L.first_dig[2], L.first_before, L.first_after, Sample (L.first_dig));
}

// terrain_lava_fill n x y z vx vy vz : test helper, packs n particles into a 3D block at once
static void Block_f (void)
{
	if (Cmd_Argc () < 5 || !Ready ())
	{
		Con_Printf ("usage: terrain_lava_block n x y z  (n particles on a lattice above x y z)\n");
		return;
	}
	const int	n = atoi (Cmd_Argv (1));
	const float s = P.spacing;
	const float o[3] = {atof (Cmd_Argv (2)), atof (Cmd_Argv (3)), atof (Cmd_Argv (4))};
	const int	side = (int)ceilf (cbrtf ((float)n));
	int			made = 0;
	for (int k = 0; k < side && made < n; ++k)
		for (int j = 0; j < side && made < n; ++j)
			for (int i = 0; i < side && made < n; ++i)
			{
				const float p[3] = {o[0] + (i - side * 0.5f) * s + (Rand () - 0.5f), o[1] + (j - side * 0.5f) * s + (Rand () - 0.5f), o[2] + k * s};
				const float v[3] = {0, 0, 0};
				made += Spawn (p, v, terrain_lava_heat.value, 15);
			}
	Con_Printf ("terrain_lava_block: %d particles\n", made);
}

// terrain_lava_trench x y z dirx diry length radius : test helper, digs a channel along the surface
// starting at x y z, heading dirx diry (horizontal), following the surface height; every dig is an ordinary op
static void Trench_f (void)
{
	if (Cmd_Argc () < 8 || !Ready ())
	{
		Con_Printf ("usage: terrain_lava_trench x y z dirx diry length radius\n");
		return;
	}
	float p[3] = {atof (Cmd_Argv (1)), atof (Cmd_Argv (2)), atof (Cmd_Argv (3))};
	float dx = atof (Cmd_Argv (4)), dy = atof (Cmd_Argv (5));
	const float len = atof (Cmd_Argv (6)), radius = atof (Cmd_Argv (7)), dl = sqrtf (dx * dx + dy * dy);
	if (dl < 1e-4f)
		return;
	dx /= dl;
	dy /= dl;
	int ops = 0;
	for (float s = 0.0f; s <= len; s += radius * 0.6f)
	{
		float q[3] = {p[0] + dx * s, p[1] + dy * s, p[2] + 60.0f};
		int	  guard = 0;
		while (Sample (q) > 1.0f && guard++ < 200)
			q[2] -= LMAX (1.0f, Sample (q) * 0.5f);
		q[2] -= radius * 0.35f; // centre below the surface so the channel is a groove, not a scrape
		ops += SV_Terrain_Edit (TERRAIN_OP_DIG, q, radius) > 0;
	}
	Con_Printf ("terrain_lava_trench: %d dig ops\n", ops);
}

void SV_Terrain_LavaInit (void)
{
	cvar_t *all[] = {&terrain_lava_cap, &terrain_lava_density, &terrain_lava_stiffness, &terrain_lava_near_stiffness, &terrain_lava_cohesion, &terrain_lava_viscosity,
					 &terrain_lava_cold_viscosity, &terrain_lava_friction, &terrain_lava_stick, &terrain_lava_cooling, &terrain_lava_contact_cooling, &terrain_lava_freeze,
					 &terrain_lava_melt, &terrain_lava_melt_threshold, &terrain_lava_dig_radius, &terrain_lava_ops_rate, &terrain_lava_fill, &terrain_lava_heat, &terrain_lava_rate,
					 &terrain_lava_speed, &terrain_lava_spread, &terrain_lava_damage, &terrain_lava_gravity, &terrain_lava_draw, &terrain_lava_lights};
	for (size_t i = 0; i < sizeof (all) / sizeof (all[0]); ++i)
		Cvar_RegisterVariable (all[i]);
	Cmd_AddCommand ("+terrain_lava", LavaOn_f);
	Cmd_AddCommand ("-terrain_lava", LavaOff_f);
	Cmd_AddCommand ("terrain_lava_emit", Emit_f);
	Cmd_AddCommand ("terrain_lava_clear", Clear_f);
	Cmd_AddCommand ("terrain_lava_stop", Stop_f);
	Cmd_AddCommand ("terrain_lava_info", Info_f);
	Cmd_AddCommand ("terrain_lava_block", Block_f);
	Cmd_AddCommand ("terrain_lava_trench", Trench_f);
	Lava_Defaults ();
}
#endif
