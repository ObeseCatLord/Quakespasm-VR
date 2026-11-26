/*
 * Server side of the editable terrain prototype: owns the region, the log of edits, and the
 * player hooks that edit it (axe digging) or help cross it (jetpack).
 * GPL-2.0-or-later, as the rest of the engine.
 *
 * The whole terrain state is (seed, size, origin, ordered op log). Every edit goes through
 * SV_Terrain_Edit, so replicating that log is all a remote client will need; today the listen
 * server's own client simply reads terrain_active.
 */
#include "quakedef.h"
#include "terrain.h"

unsigned terrain_epoch; // bumped whenever terrain_active is replaced or freed

static terrain_op_t *sv_terrain_ops;
static qboolean		 sv_terrain_ice_used; // whether the region was generated with ice; clients regenerate it the same way
static int			 sv_terrain_num_ops, sv_terrain_cap_ops;
static double		 sv_terrain_next_dig[MAX_SCOREBOARD + 1];

static cvar_t terrain_seed = {"terrain_seed", "1", CVAR_NONE};			   // cave seed for drg* maps
static cvar_t terrain_axedig = {"terrain_axedig", "1", CVAR_NONE};		   // the axe digs
static cvar_t terrain_jetpack = {"terrain_jetpack", "1", CVAR_NONE};	   // hold jump in the air to climb
static cvar_t terrain_jetpack_thrust = {"terrain_jetpack_thrust", "1500", CVAR_NONE}; // units/s^2, against sv_gravity
static cvar_t terrain_blast = {"terrain_blast", "64", CVAR_NONE};				   // radius an explosion digs; 0 turns it off
static cvar_t terrain_slope = {"terrain_slope", "0.55", CVAR_NONE};					   // least walkable floor normal z on terrain (BSP: 0.7)
static cvar_t terrain_vault = {"terrain_vault", "1", CVAR_NONE};					   // hold jump at a ledge to climb onto it
static cvar_t terrain_vault_height = {"terrain_vault_height", "76", CVAR_NONE};		   // highest ledge above the feet
static cvar_t terrain_crawlers = {"terrain_crawlers", "4", CVAR_NONE};				   // surface-crawling fiends added to a drg* map
// Which monsters crawl: a comma list of classname[:scale[:health[:speed[:spawnflags]]]], used in
// turn. The classname must be a spawn function of the running progs.dat.
static cvar_t terrain_crawler_types = {"terrain_crawler_types", "monster_demon1", CVAR_NONE};
static cvar_t terrain_crawler_speed = {"terrain_crawler_speed", "210", CVAR_NONE};
static cvar_t terrain_crawler_sense = {"terrain_crawler_sense", "900", CVAR_NONE}; // crawlers wake for a player this close, seen or not
static cvar_t terrain_monsterstep = {"terrain_monsterstep", "1", CVAR_NONE};		   // slope-tolerant monster steps on terrain
static cvar_t terrain_jetpack_speed = {"terrain_jetpack_speed", "260", CVAR_NONE};	  // fastest climb
static cvar_t terrain_monsterclimb = {"terrain_monsterclimb", "3", CVAR_NONE};		  // levels (32 units each) a walking monster may jump up onto a ledge
static cvar_t terrain_pathfind = {"terrain_pathfind", "1", CVAR_NONE};				  // walking monsters follow a route through the cave
static cvar_t terrain_ice = {"terrain_ice", "1", CVAR_NONE}; // grow ice pillars and icicles in new caves; 0 is the plain worm cave

#define FLARE_MODEL "progs/s_light.spr"
#define MAX_FLARES	8
static edict_t *sv_terrain_flares[MAX_FLARES];

static struct
{
	double until, next;
	float  top_z, dir[2];
} sv_terrain_vaults[MAX_SCOREBOARD + 1];

#define MAX_CRAWLERS   32
typedef struct
{
	edict_t *ent;
	int		 attached;
	float	 normal[3], forward[3];
	double	 air_since;
	float	 scale, speed, want_health;
	float	 feet, half_width, height; // from the monster's own box, times scale, once QuakeC has set it
	int		 sized;
} terrain_crawler_t;
static terrain_crawler_t sv_terrain_crawlers[MAX_CRAWLERS];
static int				 sv_terrain_num_crawlers;
static int				 nav_ops_seen = -1; // op count the crawler flow field was built for
static int		sv_terrain_next_flare;
static int		sv_terrain_sniff_state, sv_terrain_sniff_count;
static float	sv_terrain_sniff_pos[3];

#define DRG_SIZE_X 6
#define DRG_SIZE_Y 6
#define DRG_SIZE_Z 3
#define DRG_SIZE_MAX 12 // chunks per axis at most (6144 units): keeps the walking grid and its fields a few MB

static void WalkReset (void);

void SV_Terrain_Clear (void)
{
	if (!terrain_active)
		return;
	Terrain_Free (terrain_active);
	terrain_active = NULL;
	sv_terrain_num_ops = 0;
	++terrain_epoch;
	WalkReset ();
}

void SV_Terrain_SpawnAt (const float center[3], uint32_t seed, const int32_t size[3])
{
	int32_t origin[3];
	SV_Terrain_Clear ();
	for (int i = 0; i < 3; ++i)
		origin[i] = (int)floorf (center[i] / TERRAIN_CELL + 0.5f) * TERRAIN_CELL - size[i] * TERRAIN_CHUNK_UNITS / 2;
	terrain_t	*t = Terrain_Create (origin, size, seed);
	const double start = Sys_DoubleTime ();
	Terrain_Generate (t);
	const int ice = terrain_ice.value ? Terrain_GenerateIce (t) : 0;
	t->walk_normal = CLAMP (0.0f, terrain_slope.value, 1.0f);
	sv_terrain_ice_used = terrain_ice.value != 0;
	terrain_active = t;
	++terrain_epoch;
	memset (sv_terrain_next_dig, 0, sizeof (sv_terrain_next_dig));
	memset (sv_terrain_flares, 0, sizeof (sv_terrain_flares));
	memset (sv_terrain_vaults, 0, sizeof (sv_terrain_vaults));
	sv_terrain_num_crawlers = 0;
	nav_ops_seen = -1;
	memset (sv_terrain_step_stats, 0, sizeof (sv_terrain_step_stats));
	sv_terrain_sniff_state = 0;
	Con_Printf (
		"terrain: seed %u, %dx%dx%d chunks, %d ice features, generate %.1f ms, hash %016llx\n", seed, size[0], size[1], size[2], ice, (Sys_DoubleTime () - start) * 1000.0,
		(unsigned long long)Terrain_Hash (t));
}

// Called while a map loads, before its entities spawn. Maps named drg* are shells for a cave that
// fills the middle of the map.
void SV_Terrain_NewMap (void)
{
	SV_Terrain_Clear ();
	if (strncmp (sv.name, "drg", 3))
		return;
	// The region covers the whole map, so its outer layer (which always stays solid to keep the
	// surface closed) lies behind the map's own walls: digging stops at the map, not at a flat
	// wall in the middle of the cave.
	vec3_t	center;
	int32_t size[3];
	for (int i = 0; i < 3; ++i)
	{
		const float extent = qcvm->worldmodel->maxs[i] - qcvm->worldmodel->mins[i];
		const int	least = i == 2 ? DRG_SIZE_Z : DRG_SIZE_X;
		size[i] = CLAMP (least, (int)ceilf ((extent + 2 * TERRAIN_CELL) / TERRAIN_CHUNK_UNITS), DRG_SIZE_MAX);
		center[i] = (qcvm->worldmodel->mins[i] + qcvm->worldmodel->maxs[i]) * 0.5f;
	}
	SV_Terrain_SpawnAt (center, (uint32_t)terrain_seed.value, size);
}

/*
The one way the terrain changes after generation. The op is quantized here and logged; the
quantized op is what every peer would replay. Radius and position are in Quake units.
*/
int SV_Terrain_Edit (int kind, const float pos[3], float radius)
{
	terrain_t *t = terrain_active;
	if (!t)
		return 0;
	terrain_op_t op;
	op.kind = (uint8_t)kind;
	op.material = TERRAIN_MAT_FILL;
	op.radius = (uint16_t)(CLAMP (8.0f, radius, 512.0f) * TERRAIN_SUB / TERRAIN_CELL);
	for (int i = 0; i < 3; ++i)
		op.center[i] = (int32_t)floorf ((pos[i] - (float)t->origin[i]) * TERRAIN_SUB / TERRAIN_CELL + 0.5f);
	const int changed = Terrain_ApplyOp (t, &op);
	if (!changed)
		return 0;
	if (sv_terrain_num_ops == sv_terrain_cap_ops)
	{
		sv_terrain_cap_ops = sv_terrain_cap_ops ? sv_terrain_cap_ops * 2 : 256;
		sv_terrain_ops = (terrain_op_t *)realloc (sv_terrain_ops, sv_terrain_cap_ops * sizeof (*sv_terrain_ops));
	}
	sv_terrain_ops[sv_terrain_num_ops++] = op;
	return changed;
}

int SV_Terrain_OpLog (const terrain_op_t **ops)
{
	*ops = sv_terrain_ops;
	return sv_terrain_num_ops;
}

static uint32_t PlaceRandom (uint32_t *s)
{
	*s ^= *s << 13;
	*s ^= *s >> 17;
	*s ^= *s << 5;
	return *s;
}

// Finds a spot where a box of this size rests on a walkable part of the cave floor.
static qboolean PlaceFindFloor (uint32_t *rng, const float mins[3], const float maxs[3], const float near[3], float min_dist, float max_dist, float out[3])
{
	const terrain_t *t = terrain_active;
	for (int attempt = 0; attempt < 3000; ++attempt)
	{
		float p[3], end[3];
		for (int a = 0; a < 3; ++a)
		{
			const float extent = (float)(t->size[a] * TERRAIN_CHUNK_UNITS);
			p[a] = (float)t->origin[a] + 64.0f + (float)(PlaceRandom (rng) % 4096) / 4096.0f * (extent - 128.0f);
		}
		const float d[3] = {p[0] - near[0], p[1] - near[1], p[2] - near[2]};
		const float dist = sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		if (dist < min_dist || dist > max_dist || Terrain_BoxOverlaps (t, p, mins, maxs))
			continue;
		terrain_hit_t hit;
		end[0] = p[0];
		end[1] = p[1];
		end[2] = p[2] - 768.0f;
		if (!Terrain_TraceBox (t, p, mins, maxs, end, &hit) || hit.startsolid || hit.normal[2] < 0.85f)
			continue;
		out[0] = p[0];
		out[1] = p[1];
		out[2] = p[2] - 768.0f * hit.fraction + 1.0f;
		if (Terrain_BoxOverlaps (t, out, mins, maxs))
			continue;
		return true;
	}
	return false;
}

int SV_Terrain_RelaxedSteps (void)
{
	return terrain_active && terrain_monsterstep.value;
}

float SV_Terrain_WalkNormal (void)
{
	return terrain_active ? terrain_active->walk_normal : 0.7f; // the slope cvar applies to the next cave
}

/*
Surface crawler prototype. The edict is an ordinary QuakeC fiend (its think, pain, death and melee
all still run), but its position is owned here: it is glued to the zero surface of the terrain
field, on floors, walls and ceilings alike, and walks at its enemy along that surface, through the
cave by the flow field below. A leap (QuakeC clears FL_ONGROUND and sets a velocity) lets go of the
surface until it lands or a second passes.
*/
static terrain_crawler_t *CrawlerFind (edict_t *ent)
{
	for (int i = 0; i < sv_terrain_num_crawlers; ++i)
		if (sv_terrain_crawlers[i].ent == ent)
			return &sv_terrain_crawlers[i];
	return NULL;
}

int SV_Terrain_IsCrawler (edict_t *ent)
{
	terrain_crawler_t *c = sv_terrain_num_crawlers ? CrawlerFind (ent) : NULL;
	return c && c->attached;
}

/*
Walking routes for ordinary monsters. terrain.c keeps a grid of cells a body can stand in and how
they join up (no climbs steeper than the monsters' terrain step); here it learns which cells the
map's own brushes fill, follows edits as they are made, and floods a distance field from whatever a
monster is chasing. SV_MoveToGoal asks for a heading from it before trying its stock chase, so the
QuakeC (ai_run and movetogoal) is unchanged. Fields are kept per player and body size, a few at once.
*/
#define WALK_SLOTS 8
static terrain_nav_t walk_nav;
static uint8_t		*walk_bsp_tested; // per walking cell: the map's brushes were asked about it
static unsigned		 walk_nav_epoch;
static int			 walk_nav_ops = -1, walk_nav_version;
// Each chased player has two fields per body size. The near one covers about WALK_NEAR_COST of
// walking around them and is flooded again whenever they reach another cell or the cave changes,
// in the same frame, so the monsters close by react to a new tunnel or platform at once; it costs
// only the cells it covers. The far one covers the whole cave for the monsters beyond that and is
// brought up to date every couple of seconds.
#define WALK_NEAR_COST	   120 // about 1900 units of walking
#define WALK_NEAR_COST_TOP 180 // for the most chased player
#define WALK_FAR_EVERY	   2.0
#define WALK_FAR_EVERY_TOP 1.0
static struct
{
	edict_t			  *goal;
	int				   cls, used_once;
	double			   used;
	int				   asks, demand; // monsters' requests this second, and over the last full second
	double			   demand_since;
	terrain_navfield_t near;
	int				   near_seed, near_version;
	double			   near_built;
	uint16_t		  *far;
	int				   far_seed, far_version;
	double			   far_built;
} walk_fields[WALK_SLOTS];

static double walk_far_frame = -1.0; // server time of the last whole-cave flood: one a frame at most

/*
Priority: a field's demand is how many monster thinks asked for it over the last second, i.e. how
hard that player is being chased. The most wanted field gets the larger near area and the faster
whole-cave refresh, takes the frame's one whole-cave flood first, and fields in demand are never
given up for another target. A player who outruns the pack keeps a fresh field behind them, so the
monsters follow their trail instead of losing them.
*/
static int WalkTopDemand (void)
{
	int top = 0;
	for (int i = 0; i < WALK_SLOTS; ++i)
		if (walk_fields[i].used_once && walk_fields[i].demand > top)
			top = walk_fields[i].demand;
	return top;
}

// Per monster: the best route cost it has reached and when, to notice one that has stopped getting
// anywhere.
typedef struct
{
	int	   best, seed;
	double since, stock_until;
} walk_progress_t;
static walk_progress_t *walk_progress;
static int				walk_progress_cap;

static void WalkReset (void)
{
	free (walk_progress);
	walk_progress = NULL;
	walk_far_frame = -1.0;
	free (walk_bsp_tested);
	walk_bsp_tested = NULL;
	walk_progress_cap = 0;
	Terrain_NavFree (&walk_nav);
	walk_nav_ops = -1;
	for (int i = 0; i < WALK_SLOTS; ++i)
	{
		Terrain_NavFieldFree (&walk_fields[i].near);
		free (walk_fields[i].far);
	}
	memset (walk_fields, 0, sizeof (walk_fields)); // qcvm->time restarts with the map, so the stamps go too
}

// Asks the map whether a cell's centre is inside a brush, once per cell; 1 when it is.
static int WalkNavTestCell (int cell)
{
	if (walk_bsp_tested[cell])
		return 0;
	walk_bsp_tested[cell] = 1;
	vec3_t p;
	Terrain_NavCellCenter (&walk_nav, cell, p);
	const int contents = SV_HullPointContents (&qcvm->worldmodel->hulls[0], 0, p);
	if (contents != CONTENTS_SOLID && contents != CONTENTS_SKY)
		return 0;
	Terrain_NavSetBlocked (&walk_nav, cell, 1);
	return 1;
}

// Brings the walking grid up to date with the terrain and its op log.
static qboolean WalkNavUpdate (void)
{
	const terrain_t *t = terrain_active;
	if (!t)
		return false;
	if (!walk_nav.flags || walk_nav_epoch != terrain_epoch)
	{
		WalkReset ();
		Terrain_NavInit (&walk_nav, t);
		walk_nav_epoch = terrain_epoch;
		// cells inside the map's brushes are closed whatever the field says; only open cells need
		// asking, and a dig that opens more asks about those as it comes (WalkNavTestCell)
		Terrain_NavRefreshAll (&walk_nav, t);
		free (walk_bsp_tested);
		walk_bsp_tested = (uint8_t *)calloc (walk_nav.count, 1);
		int blocked = 0;
		for (int i = 0; i < walk_nav.count; ++i)
			if (walk_nav.flags[i] & TERRAIN_NAV_AIR)
				blocked += WalkNavTestCell (i);
		Terrain_NavRefreshAll (&walk_nav, t);
		walk_nav_ops = sv_terrain_num_ops;
		++walk_nav_version;
		Con_DPrintf ("terrain: walking grid %dx%dx%d, %d cells inside brushes\n", walk_nav.dim[0], walk_nav.dim[1], walk_nav.dim[2], blocked);
	}
	const int climb = CLAMP (0, (int)terrain_monsterclimb.value, TERRAIN_NAV_MAX_CLIMB);
	if (walk_nav.max_climb != climb)
	{
		walk_nav.max_climb = climb;
		++walk_nav_version;
	}
	if (walk_nav_ops != sv_terrain_num_ops)
	{
		for (int i = walk_nav_ops; i < sv_terrain_num_ops; ++i)
		{
			int lo[3], hi[3];
			Terrain_NavOpBox (&walk_nav, &sv_terrain_ops[i], lo, hi);
			for (int z = q_max (lo[2], 0); z <= q_min (hi[2], walk_nav.dim[2] - 1); ++z)
				for (int y = q_max (lo[1], 0); y <= q_min (hi[1], walk_nav.dim[1] - 1); ++y)
					for (int x = q_max (lo[0], 0); x <= q_min (hi[0], walk_nav.dim[0] - 1); ++x)
						WalkNavTestCell ((z * walk_nav.dim[1] + y) * walk_nav.dim[0] + x);
			Terrain_NavRefreshOp (&walk_nav, t, &sv_terrain_ops[i]);
		}
		walk_nav_ops = sv_terrain_num_ops;
		++walk_nav_version;
	}
	return true;
}

static int WalkClass (const edict_t *ent)
{
	return ent->v.maxs[0] - ent->v.mins[0] > 40.0f || ent->v.maxs[2] - ent->v.mins[2] > 72.0f;
}

static int WalkStandCell (const edict_t *ent, int cls, int down)
{
	const float feet[3] = {ent->v.origin[0], ent->v.origin[1], ent->v.origin[2] + ent->v.mins[2] + 8.0f};
	return Terrain_NavFindStand (&walk_nav, cls, feet, 1, down);
}

// The distance field toward goal for bodies of this class, or NULL when the goal has no floor.
/*
The field toward goal for bodies of this class that covers cell (the monster's): the near one when
the monster is inside it, else the far one. NULL when there is none yet or the goal has no floor.
*/
static const uint16_t *WalkField (edict_t *goal, int cls, int cell)
{
	// a target in the air (jetpack, a jump) is chased to the floor under it
	const int seed = WalkStandCell (goal, cls, 8);
	int		  slot = -1;
	for (int i = 0; i < WALK_SLOTS && slot < 0; ++i)
		if (walk_fields[i].goal == goal && walk_fields[i].cls == cls && walk_fields[i].used_once)
			slot = i;
	if (slot < 0)
	{
		if (seed < 0)
			return NULL;
		// an empty slot, else the least wanted (then the one unused longest); a field still in use is
		// not given up
		slot = 0;
		for (int i = 1; i < WALK_SLOTS; ++i)
		{
			const qboolean empty = !walk_fields[i].used_once, slot_empty = !walk_fields[slot].used_once;
			if (slot_empty)
				break;
			if (empty || walk_fields[i].demand < walk_fields[slot].demand ||
				(walk_fields[i].demand == walk_fields[slot].demand && walk_fields[i].used < walk_fields[slot].used))
				slot = i;
		}
		if (walk_fields[slot].used_once && qcvm->time >= walk_fields[slot].used && qcvm->time < walk_fields[slot].used + 1.0)
			return NULL;
		__typeof__ (walk_fields[0]) *f = &walk_fields[slot];
		if (!f->near.dist)
			Terrain_NavFieldInit (&walk_nav, &f->near);
		if (!f->far)
			f->far = (uint16_t *)malloc ((size_t)walk_nav.count * sizeof (uint16_t));
		f->goal = goal;
		f->cls = cls;
		f->used_once = 1;
		f->near_seed = f->far_seed = -1;
		f->near_built = f->far_built = -1.0;
		f->asks = f->demand = 0;
		f->demand_since = qcvm->time;
	}
	__typeof__ (walk_fields[0]) *f = &walk_fields[slot];
	f->used = qcvm->time;
	++f->asks;
	if (qcvm->time < f->demand_since || qcvm->time >= f->demand_since + 1.0)
	{
		f->demand = f->asks;
		f->asks = 0;
		f->demand_since = qcvm->time;
	}
	const qboolean top = f->demand > 0 && f->demand >= WalkTopDemand ();
	if (seed >= 0)
	{
		// near: redone the frame anything it depends on changes
		if ((seed != f->near_seed || f->near_version != walk_nav_version) && f->near_built != qcvm->time)
		{
			Terrain_NavFloodNear (&walk_nav, cls, &seed, 1, &f->near, top ? WALK_NEAR_COST_TOP : WALK_NEAR_COST);
			f->near_seed = seed;
			f->near_version = walk_nav_version;
			f->near_built = qcvm->time;
		}
		// far: the whole cave, every couple of seconds at most
		// far: the whole cave, every second or two, one such flood a frame, the most chased first
		// (a field never built yet goes first of all: until then its monsters have nothing)
		const qboolean far_stale = f->far_seed < 0 || seed != f->far_seed || f->far_version != walk_nav_version;
		const double   every = top ? WALK_FAR_EVERY_TOP : WALK_FAR_EVERY;
		const qboolean frame_free = walk_far_frame != qcvm->time || f->far_built < 0.0;
		if (far_stale && frame_free && (f->far_built < 0.0 || qcvm->time < f->far_built || qcvm->time >= f->far_built + every))
		{
			walk_far_frame = qcvm->time;
			Terrain_NavFlood (&walk_nav, cls, &seed, 1, f->far);
			f->far_seed = seed;
			f->far_version = walk_nav_version;
			f->far_built = qcvm->time;
		}
	}
	if (cell >= 0 && f->near_seed >= 0 && f->near.dist[cell] != TERRAIN_NAV_FAR)
		return f->near.dist;
	return f->far_seed >= 0 ? f->far : NULL;
}

static int WalkSteps (edict_t *ent, edict_t *goal)
{
	if (!WalkNavUpdate () || goal == qcvm->edicts || goal->free)
		return -1;
	const int		cls = WalkClass (ent);
	const int		cell = WalkStandCell (ent, cls, 2);
	const uint16_t *dist = WalkField (goal, cls, cell);
	if (cell < 0)
		return -2; // not standing anywhere the grid knows
	return dist && dist[cell] != TERRAIN_NAV_FAR ? dist[cell] : -1;
}

// Turns toward yaw and steps, but keeps the turn only when a step that way is possible: turning
// toward one heading and then another in the same think would leave the monster facing neither.
static qboolean WalkTry (edict_t *ent, float yaw, float dist)
{
	const float angle = ent->v.angles[YAW], ideal = ent->v.ideal_yaw;
	if (SV_StepDirection (ent, anglemod (yaw), dist))
		return true;
	ent->v.angles[YAW] = angle; // a refused heading leaves the monster facing as it was
	ent->v.ideal_yaw = ideal;
	return false;
}

// True when the way ahead is clear and there is no floor within a step of the feet: the monster is
// at a ledge, not held back by something in its way.
static qboolean WalkAtLip (edict_t *ent, float yaw)
{
	vec3_t		ahead, below;
	const float reach = (ent->v.maxs[0] - ent->v.mins[0]) * 0.5f + 16.0f;
	ahead[0] = ent->v.origin[0] + cosf (yaw * M_PI / 180.0f) * reach;
	ahead[1] = ent->v.origin[1] + sinf (yaw * M_PI / 180.0f) * reach;
	ahead[2] = ent->v.origin[2];
	trace_t way = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, ahead, MOVE_NORMAL, ent);
	if (way.startsolid || way.allsolid || way.fraction < 1.0f)
		return false;
	VectorCopy (ahead, below);
	below[2] -= 42.0f; // the deepest step SV_movestep takes on terrain
	trace_t floor = SV_Move (ahead, ent->v.mins, ent->v.maxs, below, MOVE_NOMONSTERS, ent);
	return !floor.startsolid && floor.fraction == 1.0f;
}

/*
Called by SV_MoveToGoal for a monster on the ground. Steps the monster along its route toward goal
and returns true; returns false when the stock chase should decide (no terrain, a flyer or crawler,
a target that is not a player, no route, or the target close by).
*/
void PF_changeyaw (void);
extern cvar_t sv_gravity;

// A monster whose box ended up inside the terrain (a fill, an explosion's dig next to it, a bad
// landing) can take no step at all. Push it out along the field's gradient, a little at a time.
static float CrawlerGradient (const terrain_t *t, const float p[3], float g[3]);

int SV_Terrain_BoxInBrushes (const float origin[3], const float mins[3], const float maxs[3])
{
	for (int k = 0; k < 9; ++k)
	{
		vec3_t p;
		for (int a = 0; a < 3; ++a)
			p[a] = origin[a] + (k == 8 ? (mins[a] + maxs[a]) * 0.5f : ((k >> a) & 1) ? maxs[a] - 1.0f : mins[a] + 1.0f);
		if (SV_HullPointContents (&qcvm->worldmodel->hulls[0], 0, p) == CONTENTS_SOLID)
			return 1;
	}
	return 0;
}

static void WalkUnstick (edict_t *ent)
{
	const terrain_t *t = terrain_active;
	for (int i = 0; i < 4 && Terrain_BoxOverlaps (t, ent->v.origin, ent->v.mins, ent->v.maxs); ++i)
	{
		float centre[3], g[3];
		for (int a = 0; a < 3; ++a)
			centre[a] = ent->v.origin[a] + (ent->v.mins[a] + ent->v.maxs[a]) * 0.5f;
		if (CrawlerGradient (t, centre, g) < 0.05f)
			break;
		vec3_t to;
		VectorMA (ent->v.origin, 4.0f, g, to);
		if (SV_Terrain_BoxInBrushes (to, ent->v.mins, ent->v.maxs))
			break; // out of the cave but into the map: stay put
		VectorCopy (to, ent->v.origin);
	}
	SV_LinkEdict (ent, false);
}

// Jumps from the foot of a ledge onto it, the way a drop goes off one: just enough to clear the lip.
static void WalkLeap (edict_t *ent, int target, float yaw_deg)
{
	vec3_t top;
	Terrain_NavCellCenter (&walk_nav, target, top);
	const float rise = q_max (top[2] - TERRAIN_NAV_CELL * 0.5f - (ent->v.origin[2] + ent->v.mins[2]), 16.0f) + 24.0f;
	const float g = q_max (sv_gravity.value, 100.0f), up = sqrtf (2.0f * g * rise);
	const float yaw = yaw_deg * M_PI / 180.0f;
	ent->v.velocity[0] = cosf (yaw) * 110.0f;
	ent->v.velocity[1] = sinf (yaw) * 110.0f;
	ent->v.velocity[2] = up;
	ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
	ent->v.groundentity = 0;
}

// Turns toward yaw and reports whether it already faces it closely enough to jump or drop.
static qboolean WalkFace (edict_t *ent, float yaw)
{
	ent->v.ideal_yaw = anglemod (yaw);
	PF_changeyaw ();
	return fabsf (anglemod (ent->v.angles[YAW] - ent->v.ideal_yaw + 180.0f) - 180.0f) < 30.0f;
}

int SV_Terrain_MoveToGoal (edict_t *ent, edict_t *goal, float dist)
{
	if (!terrain_active || !terrain_pathfind.value || ((int)ent->v.flags & (FL_FLY | FL_SWIM)) || SV_Terrain_IsCrawler (ent))
		return false;
	// players only: monsters fighting each other are usually close, and a field per monster would
	// churn the few kept
	if (goal == qcvm->edicts || goal->free || !((int)goal->v.flags & FL_CLIENT) || !WalkNavUpdate ())
		return false;
	WalkUnstick (ent);
	const int		cls = WalkClass (ent);
	const int		cell = WalkStandCell (ent, cls, 2);
	const uint16_t *field = cell >= 0 ? WalkField (goal, cls, cell) : NULL;
	if (cell < 0 || !field || field[cell] == TERRAIN_NAV_FAR || field[cell] <= 3)
		return false;

	// A monster that has not got closer in a few seconds is stuck on something the grid cannot see
	// (another monster, a step the slope rules refuse): give it a second of the stock chase, whose
	// random turns usually shake it loose.
	const int num = NUM_FOR_EDICT (ent);
	if (num >= walk_progress_cap)
	{
		const int cap = q_max (num + 1, qcvm->num_edicts);
		walk_progress = (walk_progress_t *)realloc (walk_progress, cap * sizeof (*walk_progress));
		memset (walk_progress + walk_progress_cap, 0, (cap - walk_progress_cap) * sizeof (*walk_progress));
		walk_progress_cap = cap;
	}
	walk_progress_t *wp = &walk_progress[num];
	const int		 target = WalkStandCell (goal, cls, 8);
	if (qcvm->time < wp->since || !wp->best || field[cell] < wp->best || target != wp->seed)
	{
		wp->seed = target; // a player on the move resets it: their route changing is not being stuck
		wp->best = field[cell];
		wp->since = qcvm->time;
	}
	if (qcvm->time < wp->stock_until)
		return false;
	if (qcvm->time > wp->since + 3.0)
	{
		wp->stock_until = qcvm->time + 1.0;
		wp->best = field[cell];
		wp->since = qcvm->time + 1.0;
		return false;
	}

	// look a few cells down the route; aiming past the next cell cuts the grid's corners. Not past a
	// ledge, up or down: that is taken square on.
	int route[4], n = 0;
	for (int c = cell, next; n < 4 && (next = Terrain_NavNext (&walk_nav, cls, field, c)) >= 0; c = next)
	{
		route[n++] = next;
		if (Terrain_NavIsDrop (&walk_nav, c, next) || Terrain_NavIsClimb (&walk_nav, c, next))
			break;
	}
	if (!n)
		return false;
	float yaws[2];
	for (int k = 0; k < 2; ++k)
	{
		vec3_t p;
		Terrain_NavCellCenter (&walk_nav, route[k ? 0 : n - 1], p);
		yaws[k] = atan2f (p[1] - ent->v.origin[1], p[0] - ent->v.origin[0]) * 180.0f / M_PI;
	}
	if (WalkTry (ent, yaws[0], dist) || (n > 1 && WalkTry (ent, yaws[1], dist)))
		return true;
	if (Terrain_NavIsDrop (&walk_nav, cell, route[0]) && WalkAtLip (ent, yaws[1]))
	{
		// At the lip, where a step is refused for walking off an edge: once it faces the drop, it
		// steps off and falls as ordinary step physics, just fast enough to clear the lip and land
		// in the next cell.
		if (WalkFace (ent, yaws[1]))
		{
			vec3_t		landing;
			const float yaw = yaws[1] * M_PI / 180.0f;
			Terrain_NavCellCenter (&walk_nav, route[0], landing);
			const float fall = q_max (ent->v.origin[2] + ent->v.mins[2] - (landing[2] - TERRAIN_NAV_CELL * 0.5f), 16.0f);
			const float airtime = sqrtf (2.0f * fall / q_max (sv_gravity.value, 100.0f));
			const float speed = CLAMP (60.0f, 48.0f / airtime, 160.0f);
			ent->v.velocity[0] = cosf (yaw) * speed;
			ent->v.velocity[1] = sinf (yaw) * speed;
			ent->v.velocity[2] = 0;
			ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
			ent->v.groundentity = 0;
		}
		return true;
	}
	if (Terrain_NavIsClimb (&walk_nav, cell, route[0]))
	{
		// At the foot of a ledge on the route: once it faces it, it jumps up.
		if (WalkFace (ent, yaws[1]))
			WalkLeap (ent, route[0], yaws[1]);
		return true;
	}
	static const float around[4] = {45, -45, 90, -90};
	for (int k = 0; k < 4; ++k)
		if (WalkTry (ent, yaws[1] + around[k], dist))
			return true;
	return false; // boxed in: the stock chase shuffles it loose
}

/*
Flow field for the crawlers: a coarse grid over the region, each open cell holding its walking
distance (through open cells) to the nearest living player. Stepping to the neighbour with the
smallest number is the way through the cave, whatever shape digging has left it in. The open-cell
mask is rebuilt at most once a second after an edit, the distances four times a second.
*/
#define NAV_CELL 32.0f
#define NAV_FAR	 0xffff
static uint8_t	*nav_air;
static uint16_t *nav_dist;
static int		*nav_queue;
static int		 nav_dim[3];
static double	 nav_air_time, nav_dist_time;

static int NavIndex (const terrain_t *t, const float p[3])
{
	int c[3];
	for (int a = 0; a < 3; ++a)
	{
		c[a] = (int)floorf ((p[a] - (float)t->origin[a]) / NAV_CELL);
		if (c[a] < 0 || c[a] >= nav_dim[a])
			return -1;
	}
	return (c[2] * nav_dim[1] + c[1]) * nav_dim[0] + c[0];
}

static void NavUpdate (const terrain_t *t)
{
	int dim[3];
	for (int a = 0; a < 3; ++a)
		dim[a] = (int)((float)(t->size[a] * TERRAIN_CHUNK_UNITS) / NAV_CELL);
	const int count = dim[0] * dim[1] * dim[2];
	if (!nav_air || dim[0] != nav_dim[0] || dim[1] != nav_dim[1] || dim[2] != nav_dim[2])
	{
		free (nav_air);
		free (nav_dist);
		free (nav_queue);
		nav_air = (uint8_t *)malloc (count);
		nav_dist = (uint16_t *)malloc (count * sizeof (uint16_t));
		nav_queue = (int *)malloc (count * sizeof (int));
		memcpy (nav_dim, dim, sizeof (dim));
		nav_ops_seen = -1;
	}
	if (nav_ops_seen < 0 || (nav_ops_seen != sv_terrain_num_ops && qcvm->time > nav_air_time + 1.0))
	{
		WalkNavUpdate (); // for the map's own solid cells
		int i = 0;
		for (int z = 0; z < dim[2]; ++z)
			for (int y = 0; y < dim[1]; ++y)
				for (int x = 0; x < dim[0]; ++x, ++i)
				{
					const float p[3] = {
						(float)t->origin[0] + ((float)x + 0.5f) * NAV_CELL, (float)t->origin[1] + ((float)y + 0.5f) * NAV_CELL,
						(float)t->origin[2] + ((float)z + 0.5f) * NAV_CELL};
					nav_air[i] = Terrain_SampleWorld (t, p) > 6.0f && !(walk_nav.flags && (walk_nav.flags[i] & TERRAIN_NAV_BLOCKED));
				}
		nav_ops_seen = sv_terrain_num_ops;
		nav_air_time = qcvm->time;
		nav_dist_time = -1.0;
	}
	if (nav_dist_time >= 0.0 && qcvm->time < nav_dist_time + 0.25 && qcvm->time >= nav_dist_time)
		return;
	nav_dist_time = qcvm->time;
	memset (nav_dist, 0xff, count * sizeof (uint16_t));
	int head = 0, tail = 0;
	for (int i = 1; i <= svs.maxclients; ++i)
	{
		edict_t *pl = EDICT_NUM (i);
		if (!svs.clients[i - 1].active || pl->v.health <= 0)
			continue;
		const int cell = NavIndex (t, pl->v.origin);
		if (cell >= 0 && nav_dist[cell] == NAV_FAR)
		{
			nav_dist[cell] = 0; // seeded even when the cell reads solid: the player is in it
			nav_queue[tail++] = cell;
		}
	}
	const int step[6] = {1, -1, dim[0], -dim[0], dim[0] * dim[1], -dim[0] * dim[1]};
	while (head < tail)
	{
		const int cell = nav_queue[head++];
		const int x = cell % dim[0], y = (cell / dim[0]) % dim[1], z = cell / (dim[0] * dim[1]);
		const int ok[6] = {x + 1 < dim[0], x > 0, y + 1 < dim[1], y > 0, z + 1 < dim[2], z > 0};
		for (int n = 0; n < 6; ++n)
		{
			const int next = cell + step[n];
			if (!ok[n] || !nav_air[next] || nav_dist[next] != NAV_FAR || nav_dist[cell] >= NAV_FAR - 1)
				continue;
			nav_dist[next] = nav_dist[cell] + 1;
			nav_queue[tail++] = next;
		}
	}
}

// Direction from p toward the neighbouring cell nearest a player; false when p is cut off.
static qboolean NavDirection (const terrain_t *t, const float p[3], float dir[3], int *cells_away)
{
	int base[3], best = NAV_FAR, best_off[3] = {0, 0, 0};
	for (int a = 0; a < 3; ++a)
		base[a] = (int)floorf ((p[a] - (float)t->origin[a]) / NAV_CELL);
	for (int dz = -1; dz <= 1; ++dz)
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int x = base[0] + dx, y = base[1] + dy, z = base[2] + dz;
				if (x < 0 || y < 0 || z < 0 || x >= nav_dim[0] || y >= nav_dim[1] || z >= nav_dim[2])
					continue;
				const int d = nav_dist[(z * nav_dim[1] + y) * nav_dim[0] + x];
				if (d < best)
				{
					best = d;
					best_off[0] = dx;
					best_off[1] = dy;
					best_off[2] = dz;
				}
			}
	if (best == NAV_FAR)
		return false;
	*cells_away = best;
	for (int a = 0; a < 3; ++a)
		dir[a] = (float)t->origin[a] + ((float)(base[a] + best_off[a]) + 0.5f) * NAV_CELL - p[a];
	return true;
}

static float CrawlerGradient (const terrain_t *t, const float p[3], float g[3])
{
	const float h = 8.0f;
	for (int a = 0; a < 3; ++a)
	{
		float hi[3] = {p[0], p[1], p[2]}, lo[3] = {p[0], p[1], p[2]};
		hi[a] += h;
		lo[a] -= h;
		g[a] = Terrain_SampleWorld (t, hi) - Terrain_SampleWorld (t, lo);
	}
	const float len = sqrtf (DotProduct (g, g));
	if (len > 1e-4f)
		VectorScale (g, 1.0f / len, g);
	return len;
}

// Pulls p onto the surface; false when the field is flat here (too far from any surface).
static qboolean CrawlerProject (const terrain_t *t, float p[3], float normal[3])
{
	for (int i = 0; i < 4; ++i)
	{
		float g[3];
		if (CrawlerGradient (t, p, g) < 0.05f)
			return false;
		const float d = Terrain_SampleWorld (t, p);
		VectorMA (p, -d, g, p);
	}
	return CrawlerGradient (t, p, normal) >= 0.05f;
}

static void CrawlerPose (terrain_crawler_t *c, const float feet[3])
{
	edict_t *ent = c->ent;
	float	*n = c->normal, *f = c->forward;
	// forward stays in the surface
	VectorMA (f, -DotProduct (f, n), n, f);
	if (VectorNormalize (f) < 0.01f)
	{
		const float any[3] = {n[2], n[0], n[1]};
		CrossProduct (n, any, f);
		VectorNormalize (f);
	}
	VectorMA (feet, c->feet, n, ent->v.origin);
	// model angles: positive pitch looks up; roll brings the model's up onto the surface normal
	const float yaw = atan2f (f[1], f[0]), pitch = atan2f (f[2], sqrtf (f[0] * f[0] + f[1] * f[1]));
	vec3_t		view = {-pitch * 180.0f / M_PI, yaw * 180.0f / M_PI, 0}, fw, right0, up0;
	AngleVectors (view, fw, right0, up0);
	ent->v.angles[0] = pitch * 180.0f / M_PI;
	ent->v.angles[1] = yaw * 180.0f / M_PI;
	ent->v.angles[2] = atan2f (DotProduct (n, right0), DotProduct (n, up0)) * 180.0f / M_PI;
	ent->v.ideal_yaw = ent->v.angles[1];
	// the hit box follows the body, which hangs off the surface along the normal
	for (int a = 0; c->sized && a < 3; ++a)
	{
		const float centre = (c->height * 0.5f - c->feet) * n[a];
		const float half = a == 2 ? q_max (c->height * 0.42f, c->half_width) : c->half_width;
		ent->v.mins[a] = centre - half;
		ent->v.maxs[a] = centre + half;
		ent->v.size[a] = half * 2.0f;
	}
	eval_t *scale = GetEdictFieldValue (ent, qcvm->extfields.scale);
	if (scale && c->scale != 1.0f)
		scale->_float = c->scale;
	ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
	ent->v.groundentity = EDICT_TO_PROG (qcvm->edicts);
	ent->v.velocity[0] = ent->v.velocity[1] = ent->v.velocity[2] = 0;
	SV_LinkEdict (ent, true);
}

/*
Whether a crawler may move its feet from feet to next (on a surface with normal n). The body must
stay out of the map's brushes, and must not push into another monster or a player: a move that
brings two bodies closer than they are wide is refused, one that separates them is allowed, so a
pile can always untangle.
*/
static qboolean CrawlerRoom (const terrain_crawler_t *c, const float feet[3], const float next[3], const float n[3])
{
	float body[3], was[3];
	VectorMA (next, c->height * 0.5f, n, body);
	VectorMA (feet, c->height * 0.5f, c->normal, was);
	const int contents = SV_PointContents (body);
	if (contents == CONTENTS_SOLID || contents == CONTENTS_SKY)
		return false;
	for (int e = 1; e < qcvm->num_edicts; ++e)
	{
		edict_t *other = EDICT_NUM (e);
		if (other == c->ent || other->free || other->v.solid == SOLID_NOT || other->v.health <= 0 || !((int)other->v.flags & (FL_MONSTER | FL_CLIENT)))
			continue;
		float centre[3], d_new[3], d_old[3];
		for (int a = 0; a < 3; ++a)
			centre[a] = other->v.origin[a] + (other->v.mins[a] + other->v.maxs[a]) * 0.5f;
		VectorSubtract (body, centre, d_new);
		VectorSubtract (was, centre, d_old);
		const float room = c->half_width + q_max (other->v.maxs[0] - other->v.mins[0], other->v.maxs[2] - other->v.mins[2]) * 0.35f;
		const float near2 = DotProduct (d_new, d_new);
		if (near2 < room * room && near2 < DotProduct (d_old, d_old))
			return false; // closing on a player stops at arm's length, which is within its melee reach
	}
	return true;
}

int SV_Terrain_CrawlerFrame (edict_t *ent)
{
	const terrain_t *t = terrain_active;
	if (!t || !sv_terrain_num_crawlers)
		return 0;
	terrain_crawler_t *c = CrawlerFind (ent);
	if (!c)
		return 0;
	if (ent->free || (ent->v.health <= 0 && c->sized))
	{
		// dead: let go of the wall and fall like any corpse
		ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
		ent->v.angles[0] = ent->v.angles[2] = 0;
		*c = sv_terrain_crawlers[--sv_terrain_num_crawlers];
		return 0;
	}
	if (!c->sized && ent->v.takedamage && ent->v.maxs[2] - ent->v.mins[2] > 1.0f)
	{
		// QuakeC has finished its (possibly delayed) start: take the box it chose
		c->feet = -ent->v.mins[2] * c->scale;
		c->half_width = ent->v.maxs[0] * c->scale;
		c->height = (ent->v.maxs[2] - ent->v.mins[2]) * c->scale;
		c->sized = 1;
		if (c->want_health > 0)
			ent->v.health = ent->v.max_health = c->want_health;
	}
	float feet[3];
	if (!c->attached)
	{
		const float speed2 = DotProduct (ent->v.velocity, ent->v.velocity);
		const qboolean landed = ((int)ent->v.flags & FL_ONGROUND) != 0;
		if (!landed && speed2 > 60.0f * 60.0f && qcvm->time - c->air_since < 1.0)
			return 0; // still in the air: ordinary monster physics
		VectorCopy (ent->v.origin, feet);
		float n[3];
		if (!CrawlerProject (t, feet, n))
			return 0;
		VectorCopy (n, c->normal);
		c->attached = 1;
		if (!landed)
		{
			// the leap never reached a floor, so QuakeC is still waiting for its touch
			eval_t *run = GetEdictFieldValue (ent, ED_FindFieldOffset ("th_run"));
			ent->v.touch = 0;
			if (run && run->function)
			{
				ent->v.think = run->function;
				ent->v.nextthink = qcvm->time + 0.1;
			}
		}
		CrawlerPose (c, feet);
		return 1;
	}
	if (!((int)ent->v.flags & FL_ONGROUND) && DotProduct (ent->v.velocity, ent->v.velocity) > 100.0f * 100.0f)
	{
		c->attached = 0; // QuakeC started a leap
		c->air_since = qcvm->time;
		ent->v.angles[0] = ent->v.angles[2] = 0;
		VectorCopy (ent->v.origin, feet);
		ent->v.mins[0] = ent->v.mins[1] = -c->half_width;
		ent->v.mins[2] = -c->feet;
		ent->v.maxs[0] = ent->v.maxs[1] = c->half_width;
		ent->v.maxs[2] = c->height - c->feet;
		VectorSubtract (ent->v.maxs, ent->v.mins, ent->v.size);
		return 0;
	}
	VectorMA (ent->v.origin, -c->feet, c->normal, feet);
	edict_t *enemy = PROG_TO_EDICT (ent->v.enemy);
	if (enemy == qcvm->edicts && qcvm->time > 2.0 && ent->v.takedamage)
	{
		// A swarm does not wait to see you: wake for the nearest living player in range.
		dfunction_t *found = ED_FindFunction ("FoundTarget");
		float		 best = terrain_crawler_sense.value;
		edict_t		*pick = NULL;
		for (int i = 1; i <= svs.maxclients; ++i)
		{
			edict_t *pl = EDICT_NUM (i);
			vec3_t	 d;
			if (!svs.clients[i - 1].active || pl->v.health <= 0 || ((int)pl->v.flags & FL_NOTARGET))
				continue;
			VectorSubtract (pl->v.origin, ent->v.origin, d);
			const float dist = sqrtf (DotProduct (d, d));
			if (dist < best)
			{
				best = dist;
				pick = pl;
			}
		}
		if (pick && found)
		{
			ent->v.enemy = EDICT_TO_PROG (pick);
			pr_global_struct->self = EDICT_TO_PROG (ent);
			pr_global_struct->time = qcvm->time;
			PR_ExecuteProgram (found - qcvm->functions);
			enemy = pick;
		}
	}
	if (enemy != qcvm->edicts && !enemy->free && enemy->v.health > 0)
	{
		float to[3], body[3];
		const float reach = 52.0f + c->half_width; // stop here and let QuakeC attack
		VectorMA (feet, c->height * 0.5f, c->normal, body);
		VectorSubtract (enemy->v.origin, body, to);
		const float dist = sqrtf (DotProduct (to, to));
		// Far off, follow the flow field through the cave; close by, go straight at them.
		int	  cells = 0;
		float flow[3];
		NavUpdate (t);
		if (dist > 160.0f && NavDirection (t, body, flow, &cells) && cells > 3)
			VectorCopy (flow, to);
		const float want = sqrtf (DotProduct (to, to));
		VectorMA (to, -DotProduct (to, c->normal), c->normal, to);
		// Straight above or below the enemy the surface offers no direction; keep going the
		// way it was heading, which carries it round the curve of a passage.
		if (sqrtf (DotProduct (to, to)) < want * 0.25f)
			VectorCopy (c->forward, to);
		const qboolean chase = dist > reach && VectorNormalize (to) > 0.01f;
		if (!chase)
		{
			VectorCopy (to, c->forward); // in reach: face them and let QuakeC attack
			to[0] = to[1] = to[2] = 0;
		}
		// keep a body's width from the other crawlers, moving or not
		float push[3] = {0, 0, 0};
		for (int i = 0; i < sv_terrain_num_crawlers; ++i)
		{
			const terrain_crawler_t *o = &sv_terrain_crawlers[i];
			float					 away[3], other[3];
			if (o == c || !o->attached)
				continue;
			VectorMA (o->ent->v.origin, o->height * 0.5f - o->feet, o->normal, other);
			VectorSubtract (body, other, away);
			float		gap = sqrtf (DotProduct (away, away));
			const float room = (c->half_width + o->half_width) * 1.2f;
			if (gap >= room)
				continue;
			if (gap < 1.0f)
			{
				// exactly stacked: split by slot order
				away[0] = o < c ? 1.0f : -1.0f;
				away[1] = o < c ? 0.5f : -0.5f;
				away[2] = 0;
				gap = 1.0f;
			}
			VectorMA (push, (room - gap) / room / gap, away, push);
		}
		VectorMA (push, -DotProduct (push, c->normal), c->normal, push);
		VectorMA (to, 1.5f, push, to);
		const float rate = sqrtf (DotProduct (to, to));
		if (rate > 0.05f)
		{
			float next[3], n[3];
			VectorMA (feet, terrain_crawler_speed.value * c->speed * q_min (rate, 1.0f) / rate * (float)host_frametime, to, next);
			if (CrawlerProject (t, next, n) && CrawlerRoom (c, feet, next, n))
			{
				VectorCopy (next, feet);
				for (int a = 0; a < 3; ++a)
					c->normal[a] = c->normal[a] * 0.7f + n[a] * 0.3f;
				VectorNormalize (c->normal);
				if (chase)
					for (int a = 0; a < 3; ++a)
						c->forward[a] = c->forward[a] * 0.8f + to[a] / rate * 0.2f;
			}
		}
	}
	CrawlerPose (c, feet);
	return 1;
}

static qboolean PlaceFindFloor (uint32_t *rng, const float mins[3], const float maxs[3], const float near[3], float min_dist, float max_dist, float out[3]);

static void CrawlerSpawnAll (uint32_t *rng, const float start[3])
{
	const int want = q_min ((int)terrain_crawlers.value, MAX_CRAWLERS);
	char	  types[512];
	q_strlcpy (types, terrain_crawler_types.string, sizeof (types));
	const char *spec[16];
	int			num_specs = 0;
	for (char *tok = strtok (types, ", "); tok && num_specs < 16; tok = strtok (NULL, ", "))
		spec[num_specs++] = tok;
	for (int i = 0; i < want && num_specs; ++i)
	{
		char  name[64] = "";
		float scale = 1.0f, health = 0.0f, speed = 1.0f;
		int	  spawnflags = 0;
		sscanf (spec[i % num_specs], "%63[^:]:%f:%f:%f:%d", name, &scale, &health, &speed, &spawnflags);
		dfunction_t *func = ED_FindFunction (name);
		if (!func)
		{
			Con_Printf ("terrain: this progs.dat has no %s to crawl\n", name);
			continue;
		}
		scale = q_max (0.2f, q_min (scale, 4.0f));
		// big ones only need a spot a mid-sized box fits: they cling to the surface, they don't stand in a room
		const float hw = q_min (32.0f * scale, 56.0f), up = q_min (64.0f * scale, 112.0f);
		const float mins[3] = {-hw, -hw, -24 * scale}, maxs[3] = {hw, hw, up};
		float		spot[3];
		qboolean	clear = false;
		// not on top of another monster: mods telefrag whatever spawns inside something
		for (int attempt = 0; attempt < 12 && !clear; ++attempt)
		{
			if (!PlaceFindFloor (rng, mins, maxs, start, 1100.0f, 4096.0f, spot))
				break;
			clear = true;
			for (int e = svs.maxclients + 1; clear && e < qcvm->num_edicts; ++e)
			{
				edict_t *other = EDICT_NUM (e);
				vec3_t	 d;
				if (other->free || !((int)other->v.flags & FL_MONSTER))
					continue;
				VectorSubtract (other->v.origin, spot, d);
				clear = sqrtf (DotProduct (d, d)) > 64.0f * scale + 80.0f;
			}
		}
		if (!clear)
			continue;
		edict_t *ed = ED_Alloc ();
		ed->v.classname = PR_SetEngineString (func->s_name ? PR_GetString (func->s_name) : name);
		ed->v.spawnflags = spawnflags;
		if (health > 0)
			ed->v.health = health; // some mods keep a preset health
		VectorCopy (spot, ed->v.origin);
		pr_global_struct->self = EDICT_TO_PROG (ed);
		PR_ExecuteProgram (func - qcvm->functions);
		if (ed->free)
			continue;
		terrain_crawler_t *c = &sv_terrain_crawlers[sv_terrain_num_crawlers++];
		memset (c, 0, sizeof (*c));
		c->ent = ed;
		c->normal[2] = 1.0f;
		c->forward[0] = 1.0f;
		c->attached = 1;
		c->scale = scale;
		c->speed = speed > 0 ? speed : 1.0f;
		c->want_health = health;
		c->feet = 24.0f * scale;
		c->half_width = 30.0f * scale;
		c->height = 72.0f * scale;
	}
	Con_Printf ("terrain: %d crawlers\n", sv_terrain_num_crawlers);
}

/*
Called right after the map's entities spawn and before any of them think. A drg* map lists its
monsters and items at arbitrary spots; this moves them onto the generated cave floor: the player
start under the centre, items near it, monsters further out. The choice depends only on the seed.
*/
void SV_Terrain_PlaceEntities (void)
{
	if (!terrain_active || strncmp (sv.name, "drg", 3))
		return;
	const terrain_t *t = terrain_active;
	SV_Precache_Model (FLARE_MODEL); // while the map loads, so every client has it
	uint32_t		 rng = t->seed * 747796405u + 2891336453u;
	if (!rng)
		rng = 1;
	float		start[3];
	const float player_mins[3] = {-16, -16, -24}, player_maxs[3] = {16, 16, 32};
	for (int a = 0; a < 3; ++a)
		start[a] = (float)t->origin[a] + (float)(t->size[a] * TERRAIN_CHUNK_UNITS) * 0.5f;
	terrain_hit_t hit;
	float		  below[3] = {start[0], start[1], start[2] - 768.0f};
	if (Terrain_TraceBox (t, start, player_mins, player_maxs, below, &hit) && !hit.startsolid)
		start[2] -= 768.0f * hit.fraction - 1.0f;

	int monsters = 0, items = 0, failed = 0;
	for (int i = 1; i < qcvm->num_edicts; ++i)
	{
		edict_t *ed = EDICT_NUM (i);
		if (ed->free)
			continue;
		const char *name = PR_GetString (ed->v.classname);
		float		spot[3];
		if (!strncmp (name, "info_player_", 12))
		{
			VectorCopy (start, spot); // the start entity's origin is the player's origin
		}
		else if (!strncmp (name, "monster_", 8) || !strncmp (name, "item_", 5) || !strncmp (name, "weapon_", 7))
		{
			const qboolean monster = name[0] == 'm';
			float		   mins[3], maxs[3];
			VectorCopy (ed->v.mins, mins);
			VectorCopy (ed->v.maxs, maxs);
			if (maxs[0] - mins[0] < 8.0f || maxs[2] - mins[2] < 8.0f)
			{
				mins[0] = mins[1] = -16.0f;
				maxs[0] = maxs[1] = 16.0f;
				mins[2] = 0.0f;
				maxs[2] = 56.0f;
			}
			if (!PlaceFindFloor (&rng, mins, maxs, start, monster ? 450.0f : 60.0f, monster ? 4096.0f : 320.0f, spot))
			{
				++failed;
				continue;
			}
			if (monster)
				++monsters;
			else
				++items;
		}
		else
			continue;
		VectorCopy (spot, ed->v.origin);
		VectorCopy (spot, ed->v.oldorigin);
		SV_LinkEdict (ed, false);
	}
	Con_Printf ("terrain: placed %d monsters and %d items on the cave floor (%d found no spot)\n", monsters, items, failed);
	WalkNavUpdate (); // while the map loads rather than at the first chase
	CrawlerSpawnAll (&rng, start);
}

/*
Per-player terrain abilities, run once per server frame before the player moves.
Both work from fields every progs.dat has, so they need no QuakeC support.
*/
void SV_Terrain_PlayerFrame (edict_t *ent, int num)
{
	const terrain_t *t = terrain_active;
	if (!t || ent->free || ent->v.health <= 0 || num < 1 || num > MAX_SCOREBOARD)
		return;

	// The edict's button fields are only valid while a queued move is being run, so read the
	// client's latest command instead.
	const int buttons = svs.clients[num - 1].cmd.buttons | (ent->v.button0 ? 1 : 0) | (ent->v.button2 ? 2 : 0);

	// Ledge vault: hold jump against a ledge no higher than terrain_vault_height above the feet
	// and the player is lifted over its lip. Detection: blocked ahead, a walkable top a little
	// further on, and room for the body above where it stands now.
	qboolean vaulting = false;
	if (terrain_vault.value && ent->v.movetype == MOVETYPE_WALK)
	{
		__typeof__ (sv_terrain_vaults[0]) *vs = &sv_terrain_vaults[num];
		if (vs->until > qcvm->time && (buttons & 2))
		{
			vaulting = true;
			ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
			if (ent->v.origin[2] < vs->top_z)
			{
				ent->v.velocity[0] = vs->dir[0] * 40.0f;
				ent->v.velocity[1] = vs->dir[1] * 40.0f;
				ent->v.velocity[2] = 260.0f;
			}
			else
			{
				ent->v.velocity[0] = vs->dir[0] * 190.0f;
				ent->v.velocity[1] = vs->dir[1] * 190.0f;
				ent->v.velocity[2] = 40.0f;
				vs->until = 0;
				vs->next = qcvm->time + 0.3;
			}
		}
		else if ((buttons & 2) && qcvm->time >= vs->next)
		{
			vs->until = 0;
			const float yaw = ent->v.v_angle[1] * M_PI / 180.0f;
			vec3_t		dir = {cosf (yaw), sinf (yaw), 0}, ahead;
			VectorMA (ent->v.origin, 16.0f, dir, ahead);
			trace_t wall = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, ahead, MOVE_NOMONSTERS, ent);
			for (float reach = 24.0f; wall.fraction < 1.0f && !vs->until && reach <= 40.0f; reach += 8.0f)
			{
				vec3_t top, bottom, above;
				VectorMA (ent->v.origin, reach, dir, top);
				VectorCopy (top, bottom);
				top[2] += terrain_vault_height.value;
				bottom[2] += 19.0f; // anything lower is an ordinary step
				trace_t ledge = SV_Move (top, ent->v.mins, ent->v.maxs, bottom, MOVE_NOMONSTERS, ent);
				if (ledge.startsolid || ledge.allsolid || ledge.fraction == 1.0f || ledge.plane.normal[2] < SV_Terrain_WalkNormal ())
					continue;
				VectorCopy (ent->v.origin, above);
				above[2] = ledge.endpos[2] + 2.0f;
				trace_t rise = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, above, MOVE_NOMONSTERS, ent);
				if (rise.fraction < 1.0f || rise.startsolid)
					continue;
				vs->top_z = above[2];
				vs->dir[0] = dir[0];
				vs->dir[1] = dir[1];
				vs->until = qcvm->time + 0.25 + (above[2] - ent->v.origin[2]) / 200.0;
				Con_DPrintf ("terrain: vault %.0f up\n", above[2] - ent->v.origin[2]);
			}
		}
	}

	if (!vaulting && terrain_jetpack.value && (buttons & 2) && !((int)ent->v.flags & FL_ONGROUND) && ent->v.movetype == MOVETYPE_WALK)
	{
		const float top = terrain_jetpack_speed.value;
		if (ent->v.velocity[2] < top)
			ent->v.velocity[2] = q_min (top, ent->v.velocity[2] + terrain_jetpack_thrust.value * (float)host_frametime);
	}

	if (terrain_axedig.value && (buttons & 1) && (int)ent->v.weapon == IT_AXE && qcvm->time >= sv_terrain_next_dig[num])
	{
		const float reach = 96.0f, zero[3] = {0, 0, 0};
		vec3_t		forward, right, up, eye, end;
		AngleVectors (ent->v.v_angle, forward, right, up);
		VectorAdd (ent->v.origin, ent->v.view_ofs, eye);
		VectorMA (eye, reach, forward, end);
		// Bite where the view ray meets the terrain; when it meets nothing (a ledge below eye level
		// is what blocks the player), bite the space straight ahead anyway.
		terrain_hit_t hit;
		vec3_t		  center;
		float		  ahead = 40.0f;
		if (Terrain_TraceBox (t, eye, zero, zero, end, &hit))
			ahead = (hit.startsolid ? 0.0f : hit.fraction * reach) + 20.0f;
		VectorMA (eye, ahead, forward, center);
		center[2] -= 6.0f; // keeps the floor of a dug passage near foot level
		const int changed = SV_Terrain_Edit (TERRAIN_OP_DIG, center, 52.0f);
		Con_DPrintf ("terrain: axe bite at %.0f %.0f %.0f changed %d samples\n", center[0], center[1], center[2], changed);
		sv_terrain_next_dig[num] = qcvm->time + 0.5; // one bite per axe swing
	}
}

/*
Explosions dig. QuakeC announces one by broadcasting svc_temp_entity, an explosion type and three
coordinates, whoever caused it; the Write builtins pass each broadcast value through here.
kind: 0 byte, 1 coord, anything else resets.
*/
void SV_Terrain_SniffWrite (int dest, int kind, float value)
{
	if (!terrain_active || dest != MSG_BROADCAST)
		return;
	if (kind == 0 && sv_terrain_sniff_state == 0 && (int)value == svc_temp_entity)
		sv_terrain_sniff_state = 1;
	else if (kind == 0 && sv_terrain_sniff_state == 1 && ((int)value == TE_EXPLOSION || (int)value == TE_TAREXPLOSION || (int)value == TE_EXPLOSION2))
	{
		sv_terrain_sniff_state = 2;
		sv_terrain_sniff_count = 0;
	}
	else if (kind == 1 && sv_terrain_sniff_state == 2)
	{
		sv_terrain_sniff_pos[sv_terrain_sniff_count++] = value;
		if (sv_terrain_sniff_count == 3)
		{
			sv_terrain_sniff_state = 0;
			if (terrain_blast.value > 0.0f)
				SV_Terrain_Edit (TERRAIN_OP_DIG, sv_terrain_sniff_pos, terrain_blast.value);
		}
	}
	else
		sv_terrain_sniff_state = 0;
}

/*
terrain_flare
The local player throws a light that bounces and stays where it lands. The oldest goes out when
there are more than MAX_FLARES.
*/
static void SV_Terrain_Flare_f (void)
{
	if (!terrain_active || !sv.active || !sv_player)
	{
		Con_Printf ("terrain_flare: needs a terrain and a local server\n");
		return;
	}
	PR_SwitchQCVM (&sv.qcvm);
	edict_t **slot = &sv_terrain_flares[sv_terrain_next_flare];
	sv_terrain_next_flare = (sv_terrain_next_flare + 1) % MAX_FLARES;
	if (*slot && !(*slot)->free && !strcmp (PR_GetString ((*slot)->v.classname), "terrain_flare"))
		ED_Free (*slot);
	edict_t *player = sv_player;
	edict_t *flare = *slot = ED_Alloc ();
	vec3_t	 forward, right, up;
	AngleVectors (player->v.v_angle, forward, right, up);
	flare->v.classname = PR_SetEngineString ("terrain_flare");
	flare->v.movetype = MOVETYPE_BOUNCE;
	flare->v.solid = SOLID_NOT;
	flare->v.model = PR_SetEngineString (FLARE_MODEL);
	flare->v.modelindex = SV_ModelIndex (FLARE_MODEL);
	flare->v.effects = EF_BRIGHTLIGHT;
	flare->v.owner = EDICT_TO_PROG (player);
	for (int i = 0; i < 3; ++i)
	{
		flare->v.origin[i] = player->v.origin[i] + player->v.view_ofs[i] + forward[i] * 12.0f;
		flare->v.velocity[i] = forward[i] * 650.0f + up[i] * 120.0f + player->v.velocity[i] * 0.5f;
	}
	SV_LinkEdict (flare, false);
	PR_SwitchQCVM (NULL);
}

/*
terrain_ents
Lists the monsters with where they stand relative to the terrain; the scripted check reads it.
*/
// Debug: every monster hunts the first player now, seen or not, so footing can be measured.
static void SV_Terrain_Wake_f (void)
{
	if (!terrain_active || !sv.active)
		return;
	PR_SwitchQCVM (&sv.qcvm);
	dfunction_t *found = ED_FindFunction ("FoundTarget");
	int			 woke = 0;
	for (int i = svs.maxclients + 1; found && i < qcvm->num_edicts; ++i)
	{
		edict_t *ed = EDICT_NUM (i);
		if (ed->free || ed->v.health <= 0 || !((int)ed->v.flags & FL_MONSTER) || ed->v.enemy)
			continue;
		ed->v.enemy = EDICT_TO_PROG (EDICT_NUM (1));
		pr_global_struct->self = EDICT_TO_PROG (ed);
		pr_global_struct->time = qcvm->time;
		PR_ExecuteProgram (found - qcvm->functions);
		++woke;
	}
	Con_Printf ("terrain_wake: %d monsters\n", woke);
	PR_SwitchQCVM (NULL);
}

static void SV_Terrain_Ents_f (void)
{
	if (!terrain_active || !sv.active)
	{
		Con_Printf ("terrain_ents: needs a terrain and a local server\n");
		return;
	}
	PR_SwitchQCVM (&sv.qcvm);
	const float *player = EDICT_NUM (1)->v.origin;
	for (int i = 1; i < qcvm->num_edicts; ++i)
	{
		edict_t *ed = EDICT_NUM (i);
		if (ed->free || (strncmp (PR_GetString (ed->v.classname), "monster_", 8) && strcmp (PR_GetString (ed->v.classname), "terrain_flare")))
			continue;
		const float *o = ed->v.origin;
		const float	 d[3] = {o[0] - player[0], o[1] - player[1], o[2] - player[2]};
		Con_Printf (
			"terrain_ent: %d %s origin %.0f %.0f %.0f health %.0f onground %d overlap %d dist %.0f route %d\n", i, PR_GetString (ed->v.classname), o[0], o[1],
			o[2], ed->v.health, ((int)ed->v.flags & FL_ONGROUND) != 0, Terrain_BoxOverlaps (terrain_active, o, ed->v.mins, ed->v.maxs),
			sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), ed->v.health > 0 && ((int)ed->v.flags & FL_MONSTER) ? WalkSteps (ed, EDICT_NUM (1)) : -1);
	}
	Con_Printf (
		"terrain_steps: ok %d blocked %d edge %d footing %d (mid %d corners %d) crawlers %d\n", sv_terrain_step_stats[0], sv_terrain_step_stats[1],
		sv_terrain_step_stats[2], sv_terrain_step_stats[3], sv_terrain_step_stats[4], sv_terrain_step_stats[5], sv_terrain_num_crawlers);
	PR_SwitchQCVM (NULL);
}

/*
==============================================================================

NETSYNC

A client's copy of the cave is the seed's cave plus the op log replayed in order. Each spawned
client of this engine (the pinned QSVR protocol) gets the region's parameters once, then every op
it has not had yet, as hidden stuffed commands on its reliable channel: ordered, never lost, and a
plain Quake client simply never receives them. The op stream carries the index of its first op,
so a client can tell when it has missed or repeated one.

==============================================================================
*/
#define TERRAIN_OPS_PER_COMMAND 60
static struct
{
	qboolean live, need_init;
	unsigned epoch;
	int		 ops;
} sv_terrain_sent[MAX_SCOREBOARD];

static qboolean SV_Terrain_Stuff (client_t *client, const char *command)
{
	sizebuf_t *msg = &client->message;
	const int  need = (int)strlen (command) + 2;					  // svc_stufftext and the terminator
	if (msg->overflowed || msg->cursize + need > msg->maxsize / 2)	  // leave half for everything else
		return false;
	MSG_WriteByte (msg, svc_stufftext);
	MSG_WriteString (msg, command);
	return !msg->overflowed;
}

void SV_Terrain_SendToClient (client_t *client)
{
	const int slot = (int)(client - svs.clients);
	if (slot < 0 || slot >= MAX_SCOREBOARD)
		return;
	__typeof__ (sv_terrain_sent[0]) *sent = &sv_terrain_sent[slot];
	if (!client->active || !client->spawned || !client->netconnection || client->protocol_qsvr != QSVR_PROTOCOL_PINNED)
	{
		sent->live = false;
		return;
	}
	if (!sent->live)
	{
		sent->live = true;
		sent->need_init = true;
	}
	char command[2048];
	if (sent->need_init || sent->epoch != terrain_epoch)
	{
		const terrain_t *t = terrain_active;
		if (t)
			q_snprintf (
				command, sizeof (command), "//terrain_init %u %d %d %d %d %d %d %d %.4f\n", t->seed, t->origin[0], t->origin[1], t->origin[2], t->size[0],
				t->size[1], t->size[2], sv_terrain_ice_used ? 1 : 0, t->walk_normal);
		else
			q_strlcpy (command, "//terrain_init none\n", sizeof (command));
		if (!SV_Terrain_Stuff (client, command))
			return;
		sent->need_init = false;
		sent->epoch = terrain_epoch;
		sent->ops = 0;
	}
	while (terrain_active && sent->ops < sv_terrain_num_ops)
	{
		const int count = q_min (TERRAIN_OPS_PER_COMMAND, sv_terrain_num_ops - sent->ops);
		int		  len = q_snprintf (command, sizeof (command), "//terrain_ops %d %d ", sent->ops, TERRAIN_OP_BYTES);
		for (int i = 0; i < count; ++i)
		{
			uint8_t packed[TERRAIN_OP_BYTES];
			Terrain_OpPack (&sv_terrain_ops[sent->ops + i], packed);
			for (int b = 0; b < TERRAIN_OP_BYTES; ++b)
				len += q_snprintf (command + len, sizeof (command) - len, "%02x", packed[b]);
		}
		q_strlcpy (command + len, "\n", sizeof (command) - len);
		if (!SV_Terrain_Stuff (client, command))
			return; // the rest goes in a later frame
		sent->ops += count;
	}
}

void SV_Terrain_Init (void)
{
	Cvar_RegisterVariable (&terrain_seed);
	Cvar_RegisterVariable (&terrain_axedig);
	Cvar_RegisterVariable (&terrain_jetpack);
	Cvar_RegisterVariable (&terrain_jetpack_thrust);
	Cvar_RegisterVariable (&terrain_jetpack_speed);
	Cvar_RegisterVariable (&terrain_ice);
	Cvar_RegisterVariable (&terrain_blast);
	Cvar_RegisterVariable (&terrain_slope);
	Cvar_RegisterVariable (&terrain_vault);
	Cvar_RegisterVariable (&terrain_vault_height);
	Cvar_RegisterVariable (&terrain_crawlers);
	Cvar_RegisterVariable (&terrain_crawler_speed);
	Cvar_RegisterVariable (&terrain_crawler_types);
	Cvar_RegisterVariable (&terrain_crawler_sense);
	Cvar_RegisterVariable (&terrain_monsterstep);
	Cvar_RegisterVariable (&terrain_pathfind);
	Cvar_RegisterVariable (&terrain_monsterclimb);
	Cmd_AddCommand ("terrain_ents", SV_Terrain_Ents_f);
	Cmd_AddCommand ("terrain_wake", SV_Terrain_Wake_f);
	Cmd_AddCommand ("terrain_flare", SV_Terrain_Flare_f);
}
