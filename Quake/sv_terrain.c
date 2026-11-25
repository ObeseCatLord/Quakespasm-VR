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
static int			 sv_terrain_num_ops, sv_terrain_cap_ops;
static double		 sv_terrain_next_dig[MAX_SCOREBOARD + 1];

static cvar_t terrain_seed = {"terrain_seed", "1", CVAR_NONE};			   // cave seed for drg* maps
static cvar_t terrain_axedig = {"terrain_axedig", "1", CVAR_NONE};		   // the axe digs
static cvar_t terrain_jetpack = {"terrain_jetpack", "1", CVAR_NONE};	   // hold jump in the air to climb
static cvar_t terrain_jetpack_thrust = {"terrain_jetpack_thrust", "1500", CVAR_NONE}; // units/s^2, against sv_gravity
static cvar_t terrain_jetpack_speed = {"terrain_jetpack_speed", "260", CVAR_NONE};	  // fastest climb

#define DRG_SIZE_X 6
#define DRG_SIZE_Y 6
#define DRG_SIZE_Z 3

void SV_Terrain_Clear (void)
{
	if (!terrain_active)
		return;
	Terrain_Free (terrain_active);
	terrain_active = NULL;
	sv_terrain_num_ops = 0;
	++terrain_epoch;
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
	terrain_active = t;
	++terrain_epoch;
	memset (sv_terrain_next_dig, 0, sizeof (sv_terrain_next_dig));
	Con_Printf (
		"terrain: seed %u, %dx%dx%d chunks, generate %.1f ms, hash %016llx\n", seed, size[0], size[1], size[2], (Sys_DoubleTime () - start) * 1000.0,
		(unsigned long long)Terrain_Hash (t));
}

// Called while a map loads, before its entities spawn. Maps named drg* are shells for a cave that
// fills the middle of the map.
void SV_Terrain_NewMap (void)
{
	SV_Terrain_Clear ();
	if (strncmp (sv.name, "drg", 3))
		return;
	vec3_t		  center;
	const int32_t size[3] = {DRG_SIZE_X, DRG_SIZE_Y, DRG_SIZE_Z};
	for (int i = 0; i < 3; ++i)
		center[i] = (qcvm->worldmodel->mins[i] + qcvm->worldmodel->maxs[i]) * 0.5f;
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

	if (terrain_jetpack.value && (buttons & 2) && !((int)ent->v.flags & FL_ONGROUND) && ent->v.movetype == MOVETYPE_WALK)
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
terrain_ents
Lists the monsters with where they stand relative to the terrain; the scripted check reads it.
*/
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
		if (ed->free || strncmp (PR_GetString (ed->v.classname), "monster_", 8))
			continue;
		const float *o = ed->v.origin;
		const float	 d[3] = {o[0] - player[0], o[1] - player[1], o[2] - player[2]};
		Con_Printf (
			"terrain_ent: %d %s origin %.0f %.0f %.0f health %.0f onground %d overlap %d dist %.0f\n", i, PR_GetString (ed->v.classname), o[0], o[1], o[2],
			ed->v.health, ((int)ed->v.flags & FL_ONGROUND) != 0, Terrain_BoxOverlaps (terrain_active, o, ed->v.mins, ed->v.maxs),
			sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
	}
	PR_SwitchQCVM (NULL);
}

void SV_Terrain_Init (void)
{
	Cvar_RegisterVariable (&terrain_seed);
	Cvar_RegisterVariable (&terrain_axedig);
	Cvar_RegisterVariable (&terrain_jetpack);
	Cvar_RegisterVariable (&terrain_jetpack_thrust);
	Cvar_RegisterVariable (&terrain_jetpack_speed);
	Cmd_AddCommand ("terrain_ents", SV_Terrain_Ents_f);
}
