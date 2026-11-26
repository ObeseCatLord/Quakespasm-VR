/*
 * Offline gate for the lava particle fluid: runs the simulation core against the real generated
 * cave with no engine. Checks a pool settles, a stream runs downhill, melt is rate limited and
 * stops with the heat budget, and prints the step cost at 2k, 4k and 8k particles.
 *   cc -O2 -std=c11 -IQuake tests/terrain_lava_fixture.c Quake/terrain.c -lm -o terrain_lava_fixture
 *   ./terrain_lava_fixture
 */
#define LAVA_STANDALONE 1
#include "../Quake/sv_terrain_lava.c"

static int edit_log_count;

int SV_Terrain_Edit (int kind, const float pos[3], float radius)
{
	terrain_op_t op;
	op.kind = (uint8_t)kind;
	op.material = TERRAIN_MAT_FILL;
	op.radius = (uint16_t)(LCLAMP (8.0f, radius, 512.0f) * TERRAIN_SUB / TERRAIN_CELL);
	for (int i = 0; i < 3; ++i)
		op.center[i] = (int32_t)floorf ((pos[i] - (float)terrain_active->origin[i]) * TERRAIN_SUB / TERRAIN_CELL + 0.5f);
	const int changed = Terrain_ApplyOp (terrain_active, &op);
	if (changed)
		++edit_log_count;
	return changed;
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

static void Tune (void)
{
#define T(name) \
	if (getenv ("LAVA_" #name)) \
		P.name = (float)atof (getenv ("LAVA_" #name));
	T (rest) T (stiff) T (near_stiff) T (cohesion) T (visc) T (visc_quad) T (cold_visc) T (friction) T (stick_time) T (adhesion) T (cool_air) T (cool_contact) T (freeze) T (melt) T (melt_threshold) T (dig_radius)
	T (ops_rate) T (gravity) T (spacing) T (vmax)
#undef T
}

static void Reset (int cap)
{
	Lava_Defaults ();
	Tune ();
	P.cap = cap;
	Lava_Alloc (cap);
	L.rng = 12345;
	Lava_ClearAll ();
	L.rho0 = Lava_RestDensity ();
}

static void Run (double seconds)
{
	const int steps = (int)(seconds / P.dt);
	for (int i = 0; i < steps; ++i)
		Lava_Step ();
}

static void Op (int kind, float x, float y, float z, float radius)
{
	terrain_op_t op;
	op.kind = (uint8_t)kind;
	op.material = TERRAIN_MAT_FILL;
	op.radius = (uint16_t)(radius * TERRAIN_SUB / TERRAIN_CELL);
	const float c[3] = {x, y, z};
	for (int i = 0; i < 3; ++i)
		op.center[i] = (int32_t)floorf ((c[i] - (float)terrain_active->origin[i]) * TERRAIN_SUB / TERRAIN_CELL + 0.5f);
	Terrain_ApplyOp (terrain_active, &op);
}

// A closed solid region with one spherical room; a big fill sphere puts a (nearly) flat floor at z = -100,
// or tilts it. kind 0: flat floor, 1: 45 degree slope falling toward +x, 2: wall at x = 300 facing -x
static void Arena (int kind)
{
	if (terrain_active)
		Terrain_Free (terrain_active);
	const int32_t origin[3] = {-1024, -1024, -512};
	const int32_t size[3] = {4, 4, 2};
	terrain_active = Terrain_Create (origin, size, 1);
	Op (TERRAIN_OP_DIG, 0, 0, 0, 480);
	if (kind == 0)
		Op (TERRAIN_OP_FILL, 0, 0, -3100, 3000);
	else if (kind == 1)
		Op (TERRAIN_OP_FILL, -2200, 0, -2200 - 111, 3000);
	else if (kind == 2)
		Op (TERRAIN_OP_FILL, 3300, 0, 0, 3000);
}

static void Block (int n, const float base[3])
{
	const float s = P.spacing;
	const int	side = (int)ceilf (cbrtf ((float)n)) + 1;
	int			made = 0;
	const float zero[3] = {0, 0, 0};
	for (int k = 0; made < n; ++k)
		for (int j = 0; j < side && made < n; ++j)
			for (int i = 0; i < side && made < n; ++i)
			{
				const float p[3] = {base[0] + (i - side * 0.5f) * s + (Rand () - 0.5f), base[1] + (j - side * 0.5f) * s + (Rand () - 0.5f), base[2] + 10.0f + k * s};
				made += Spawn (p, zero, 1.0f, 15);
			}
}

int main (void)
{
	setvbuf (stdout, NULL, _IOLBF, 0);
	float	 floor_pt[3] = {0, 0, -100};

	// (a) pool on a flat floor
	Arena (0);
	printf ("floor at z %.1f at origin\n", Sample ((float[3]){0, 0, 0}) - 0.0f);
	Reset (4096);
	P.cool_contact = 0.0f;
	P.cool_air = 0.0f;
	P.melt = 0.0f;
	Tune ();
	Block (1500, floor_pt);
	for (int sec = 1; sec <= 8; ++sec)
	{
		Run (1.0);
		lava_stats_t s;
		Lava_Stats (&s);
		printf ("pool t=%d live %d maxspeed %.1f centroid %.1f %.1f %.1f neigh %.1f inside %.4f step %.2fms\n", sec, s.live, s.maxspeed, s.centroid[0], s.centroid[1], s.centroid[2], s.mean_neigh,
				s.inside_frac, L.t_total);
	}
	{
		lava_stats_t s;
		Lava_Stats (&s);
		float zmin = 1e9f, zmax = -1e9f, r2max = 0;
		for (int i = 0; i < L.n; ++i)
		{
			zmin = LMIN (zmin, L.x[2][i]);
			zmax = LMAX (zmax, L.x[2][i]);
			r2max = LMAX (r2max, L.x[0][i] * L.x[0][i] + L.x[1][i] * L.x[1][i]);
		}
		printf ("pool z range %.1f .. %.1f, radius %.1f\n", zmin, zmax, sqrtf (r2max));
		CHECK (s.live == 1500, "pool lost particles: %d", s.live);
		CHECK (s.maxspeed < 40.0f, "pool still moving: %.1f", s.maxspeed);
		CHECK (s.inside_frac < 0.01f, "pool inside rock: %.4f", s.inside_frac);
		CHECK (s.mean_neigh > 8.0f && s.mean_neigh < 45.0f, "neighbour count %.1f", s.mean_neigh);
	}

	// (b) slope: a stream lands on a 45 degree slope falling toward +x; the centroid must move downhill
	Arena (1);
	Reset (4096);
	{
		float top[3] = {-120, 0, 300};
		while (Sample (top) > 1.0f)
			top[2] -= 1.0f;
		const float vel[3] = {0, 0, -80};
		float		start[3] = {top[0], top[1], top[2] + 50.0f};
		Lava_AddEmitter (start, vel, 120.0f, 2.0f, 0.02f, 1.0f);
		float cx0 = 0, cx1 = 0;
		for (int sec = 1; sec <= 8; ++sec)
		{
			Run (1.0);
			lava_stats_t s;
			Lava_Stats (&s);
			printf ("slope t=%d live %d frozen %ld centroid %.1f %.1f %.1f maxspeed %.0f neigh %.1f\n", sec, s.live, L.frozen, s.centroid[0], s.centroid[1], s.centroid[2], s.maxspeed, s.mean_neigh);
			if (sec == 2)
				cx0 = s.centroid[0];
			cx1 = s.centroid[0];
		}
		printf ("slope: emitted at x %.0f; centroid x moved %.1f downhill\n", top[0], cx1 - cx0);
		CHECK (cx1 - cx0 > 40.0f, "slope: centroid moved only %.1f", cx1 - cx0);
	}

	// (c) melt: a stream held on a wall
	Arena (2);
	Reset (4096);
	{
		const float vel[3] = {500, 0, 0}, start[3] = {0, 0, 0};
		Lava_AddEmitter (start, vel, 200.0f, 3.0f, 0.03f, 1.0f);
		const float probe[3] = {300 + 5, 0, 0};
		float		before = Sample ((float[3]){305, 0, 0});
		long		ops_at[64] = {0};
		int			sec = 0, dead_at = -1;
		for (sec = 1; sec <= 60; ++sec)
		{
			Run (1.0);
			ops_at[sec] = L.dig_ops;
			if (sec <= 14 || sec % 5 == 0)
				printf ("melt t=%d live %d frozen %ld dig_ops %ld fill_ops %ld\n", sec, L.n, L.frozen, L.dig_ops, L.fill_ops);
			if (!L.n && dead_at < 0)
			{
				dead_at = sec;
				break;
			}
		}
		(void)probe;
		printf ("melt: first dig %.0f %.0f %.0f was %.1f after %.1f now %.1f; wall sample before %.1f now %.1f\n", L.first_dig[0], L.first_dig[1], L.first_dig[2], L.first_before, L.first_after,
				Sample (L.first_dig), before, Sample ((float[3]){305, 0, 0}));
		long worst = 0;
		for (int k = 1; k <= sec; ++k)
			worst = LMAX (worst, ops_at[k] - ops_at[k - 1]);
		printf ("melt: %ld dig ops, at most %ld in one second, all particles gone at t=%d\n", L.dig_ops, worst, dead_at);
		CHECK (L.dig_ops > 0, "melt: no ops");
		CHECK (worst <= 6, "melt: %ld ops in one second", worst);
		CHECK (L.dig_ops <= 4 * (long)sec + 2, "melt: %ld ops in %d s", L.dig_ops, sec);
		CHECK (L.have_first && L.first_before < 0.0f && L.first_after > 0.0f, "melt: rock did not turn to air");
		CHECK (dead_at > 0, "melt: particles never ran out of heat");
		const long final_ops = L.dig_ops;
		Run (5.0);
		CHECK (L.dig_ops == final_ops, "melt: dug %ld more after the heat was spent", L.dig_ops - final_ops);
	}

	// (d) cost
	for (int count = 2048; count <= 8192; count *= 2)
	{
		Arena (0);
		Reset (count);
		P.cool_contact = 0.0f;
		P.cool_air = 0.0f;
	P.melt = 0.0f;
		Tune ();
		Block (count, floor_pt);
		Run (4.0);
		double t0 = LavaNow ();
		Run (2.0);
		const double ms = (LavaNow () - t0) / (2.0 / P.dt);
		lava_stats_t s;
		Lava_Stats (&s);
		printf ("cost: %d live: %.3f ms per step (neighbour %.3f relax %.3f terrain %.3f) pairs %d neigh %.1f maxspeed %.1f\n", s.live, ms, L.t_grid, L.t_relax, L.t_terrain, L.npairs, s.mean_neigh,
				s.maxspeed);
	}
	printf ("%s\n", failures ? "TERRAIN_LAVA_FIXTURE_FAILED" : "TERRAIN_LAVA_FIXTURE_PASSED");
	return failures != 0;
}
