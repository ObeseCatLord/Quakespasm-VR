/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// sv_phys.c

#include "quakedef.h"
#include "pmove.h"
#include "vr_weapon_calibration.h"

/*


pushmove objects do not obey gravity, and do not interact with each other or trigger fields, but block normal movement and push normal objects when they move.

onground is set for toss objects when they come to a complete rest.  it is set for steping or walking objects

doors, plats, etc are SOLID_BSP, and MOVETYPE_PUSH
bonus items are SOLID_TRIGGER touch, and MOVETYPE_TOSS
corpses are SOLID_NOT and MOVETYPE_TOSS
crates are SOLID_BBOX and MOVETYPE_TOSS
walking monsters are SOLID_SLIDEBOX and MOVETYPE_STEP
flying/floating monsters are SOLID_SLIDEBOX and MOVETYPE_FLY

solid_edge items only clip against bsp models.

*/

cvar_t sv_friction = {"sv_friction", "4", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_stopspeed = {"sv_stopspeed", "100", CVAR_NONE};
cvar_t sv_gravity = {"sv_gravity", "800", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_maxvelocity = {"sv_maxvelocity", "2000", CVAR_NONE};
cvar_t sv_nostep = {"sv_nostep", "0", CVAR_NONE};
cvar_t sv_freezenonclients = {"sv_freezenonclients", "0", CVAR_NONE};
cvar_t sv_gameplayfix_spawnbeforethinks = {"sv_gameplayfix_spawnbeforethinks", "0", CVAR_NONE};
cvar_t sv_gameplayfix_bouncedownslopes = {"sv_gameplayfix_bouncedownslopes", "1", CVAR_NONE}; // fixes grenades making horrible noises on slopes.
cvar_t sv_fastpushmove = {"sv_fastpushmove", "1", CVAR_NONE};								  // 0=old SV_PushMove processing; 1= faster SV_PushMove, (default)
cvar_t sv_analyticphysics = {"sv_analyticphysics", "1", CVAR_NONE}; // gravity/friction integration matches 72Hz physics at any tick rate

qboolean sv_analyticphysics_frame = true; // sv_analyticphysics latched per SV_Physics, QC can flip the cvar mid-tick

#define MOVE_EPSILON 0.01

// max depth float rounding can embed an entity into the surface it rests on, anything deeper is a real overlap
#define PUSH_CONTACT_EPSILON (2 * DIST_EPSILON)
// deliberately wider than the attach epsilon: contact must clear decisively
// before a carried entity is released, or rounding makes riders chatter
#define PUSH_RELEASE_EPSILON 1.0f
#define MIN_WALK_NORMAL		 0.7f
#define STEPSIZE			 18

static void		SV_Physics_Toss (edict_t *ent);
static edict_t *sv_walk_support_pusher;
static vec3_t	sv_walk_support_normal;

// For usage by SV_PushMove, allocate at max possible size,
// fine to be static because all SV_phys is only called from the main thread.
static edict_t *pushable_ent_cache[MAX_EDICTS];
static int		num_pushable_ent_cache;

// Spatial hash over the pushable cache so each moving pusher only tests nearby
// entities instead of scanning the whole cache. Rebuilt once per SV_Physics.
// Entities that move during the tick are reinserted at their new position by
// SV_LinkEdict, so entries only need to cover linked positions. Entities
// allocated mid-tick (cache entries at index >= push_grid_tail_start) and
// oversized entities bypass the grid and are always tested.
#define PUSH_GRID_CELL_SHIFT 8 // 256 unit cells
#define PUSH_GRID_MAX_LARGE	 1024

typedef struct
{
	edict_t *ent;
	int		 next;
} push_grid_entry_t;

static hash_map_t		 *push_grid_map; // cell coords -> head index into push_grid_entries
static push_grid_entry_t *push_grid_entries;
static int				  push_grid_entries_capacity;
static int				  push_grid_num_entries;
static edict_t			 *push_grid_large[PUSH_GRID_MAX_LARGE];
static int				  push_grid_num_large;
static int				  push_grid_tail_start;
static qboolean			  push_grid_valid;
static qboolean			  push_grid_active; // inside SV_Physics with a built grid
static qcvm_t			 *push_grid_qcvm;	// vm the grid was built for, entities from other vms must not mix in

static qboolean SV_IsPushable (edict_t *ent)
{
	return ent->v.movetype != MOVETYPE_PUSH && ent->v.movetype != MOVETYPE_NONE && ent->v.movetype != MOVETYPE_NOCLIP;
}

typedef struct
{
	int32_t x, y, z;
} push_grid_cell_t;

static uint32_t PushGrid_HashCell (const void *const val)
{
	const push_grid_cell_t *cell = (const push_grid_cell_t *)val;
	return HashCombine (HashInt32 (&cell->x), HashCombine (HashInt32 (&cell->y), HashInt32 (&cell->z)));
}

static int PushGrid_Cell (float v)
{
	// garbage origins (NaN, huge floats from broken QC) must not reach the int conversion
	const float limit = 1 << 23;
	if (!(v >= -limit)) // also catches NaN
		v = -limit;
	else if (v > limit)
		v = limit;
	return ((int)floorf (v)) >> PUSH_GRID_CELL_SHIFT;
}

static void PushGrid_CellRange (const vec3_t absmin, const vec3_t absmax, float inflate, int lo[3], int hi[3])
{
	for (int i = 0; i < 3; i++)
	{
		lo[i] = PushGrid_Cell (absmin[i] - inflate);
		hi[i] = PushGrid_Cell (absmax[i] + inflate);
	}
}

static void PushGrid_Clear (void)
{
	if (!push_grid_map)
		push_grid_map = HashMap_Create (push_grid_cell_t, int32_t, &PushGrid_HashCell, NULL);
	HashMap_Clear (push_grid_map);
	push_grid_num_entries = 0;
	push_grid_num_large = 0;
	push_grid_valid = true;
	push_grid_active = false;
}

static void PushGrid_Insert (edict_t *ent)
{
	int lo[3], hi[3];
	PushGrid_CellRange (ent->v.absmin, ent->v.absmax, 0.0f, lo, hi);

	// per-axis span check before the multiply so huge boxes can't overflow the cell count
	if (hi[0] - lo[0] >= 4 || hi[1] - lo[1] >= 4 || hi[2] - lo[2] >= 4)
	{
		if (push_grid_num_large == PUSH_GRID_MAX_LARGE)
			push_grid_valid = false;
		else
			push_grid_large[push_grid_num_large++] = ent;
		return;
	}

	int cells = (hi[0] - lo[0] + 1) * (hi[1] - lo[1] + 1) * (hi[2] - lo[2] + 1);

	if (push_grid_num_entries + cells > push_grid_entries_capacity)
	{
		push_grid_entries_capacity = q_max (push_grid_entries_capacity * 2, 4096);
		push_grid_entries = Mem_Realloc (push_grid_entries, push_grid_entries_capacity * sizeof (push_grid_entry_t));
	}

	for (int x = lo[0]; x <= hi[0]; x++)
		for (int y = lo[1]; y <= hi[1]; y++)
			for (int z = lo[2]; z <= hi[2]; z++)
			{
				push_grid_cell_t key = {x, y, z};
				int32_t			 index = push_grid_num_entries;
				int32_t			*head = HashMap_Lookup (int32_t, push_grid_map, &key);

				push_grid_entries[index].ent = ent;
				push_grid_entries[index].next = head ? *head : -1;
				if (head)
					*head = index;
				else
					HashMap_Insert (push_grid_map, &key, &index);
				push_grid_num_entries++;
			}
}

/*
============
SV_PushGridEntityLinked

Called from SV_LinkEdict. All absbox changes pass through there, so
re-inserting keeps the grid a superset of every position an entity occupied
during this tick — including teleports (setorigin) and entities displaced by
earlier pushers. Stale entries are harmless: candidates are verified against
live state.
============
*/
void SV_PushGridEntityLinked (edict_t *ent)
{
	if (!push_grid_active || qcvm != push_grid_qcvm || ent->free)
		return;
	if (!SV_IsPushable (ent))
		return;
	PushGrid_Insert (ent);
}

/*
============
PushGrid_GatherCandidates

Collects pushable entities near the given box (in vanilla edict order, no
duplicates) into out. Returns the count, or -1 if the grid is unusable this
tick and the caller must scan the full cache.
============
*/
static int PushGrid_GatherCandidates (const vec3_t mins, const vec3_t maxs, edict_t **out)
{
	int num = 0;

	if (!push_grid_valid)
		return -1;

	// strictly overlapping absboxes always share a cell, and riders touching the
	// pusher overlap it through the +-1 absbox expansion in SV_LinkEdict, so no
	// inflation is needed in theory. The +2 is a safety margin for riders whose
	// ONGROUND/groundentity state outlives actual contact by a small gap (the
	// elevator DIST_EPSILON nudge, float drift); it almost never adds a cell.
	int lo[3], hi[3];
	PushGrid_CellRange (mins, maxs, 2.0f, lo, hi);

	for (int x = lo[0]; x <= hi[0]; x++)
		for (int y = lo[1]; y <= hi[1]; y++)
			for (int z = lo[2]; z <= hi[2]; z++)
			{
				push_grid_cell_t key = {x, y, z};
				int32_t			*head = HashMap_Lookup (int32_t, push_grid_map, &key);
				for (int i = head ? *head : -1; i >= 0; i = push_grid_entries[i].next)
				{
					if (num == MAX_EDICTS)
						return -1; // pathological duplication, let the caller scan the cache
					out[num++] = push_grid_entries[i].ent;
				}
			}

	if (num + push_grid_num_large + (num_pushable_ent_cache - push_grid_tail_start) > MAX_EDICTS)
		return -1;

	for (int i = 0; i < push_grid_num_large; i++)
		out[num++] = push_grid_large[i];

	// entities allocated after the grid was built
	for (int i = push_grid_tail_start; i < num_pushable_ent_cache; i++)
		out[num++] = pushable_ent_cache[i];

	// restore vanilla processing order (blocked pushers roll back everything
	// moved so far, so order is observable) and drop multi-cell duplicates
	for (int i = 1; i < num; i++)
	{
		edict_t *key = out[i];
		int		 j = i - 1;
		while (j >= 0 && out[j] > key)
		{
			out[j + 1] = out[j];
			j--;
		}
		out[j + 1] = key;
	}
	int unique = 0;
	for (int i = 0; i < num; i++)
		if (unique == 0 || out[unique - 1] != out[i])
			out[unique++] = out[i];

	return unique;
}

/*
================
SV_CheckAllEnts
================
*/
void SV_CheckAllEnts (void)
{
	int		 e;
	edict_t *check;

	// see if any solid entities are inside the final position
	check = NEXT_EDICT (qcvm->edicts);
	for (e = 1; e < qcvm->num_edicts; e++, check = NEXT_EDICT (check))
	{
		if (check->free)
			continue;
		if (check->v.movetype == MOVETYPE_PUSH || check->v.movetype == MOVETYPE_NONE || check->v.movetype == MOVETYPE_NOCLIP)
			continue;

		if (SV_TestEntityPosition (check))
			Con_Printf ("entity in invalid position\n");
	}
}

/*
================
SV_CheckVelocity
================
*/
void SV_CheckVelocity (edict_t *ent)
{
	int i;

	//
	// bound velocity
	//
	for (i = 0; i < 3; i++)
	{
		if (IS_NAN (ent->v.velocity[i]))
		{
			Con_DPrintf ("Got a NaN velocity on %s\n", PR_GetString (ent->v.classname));
			ent->v.velocity[i] = 0;
		}
		if (IS_NAN (ent->v.origin[i]))
		{
			Con_DPrintf ("Got a NaN origin on %s\n", PR_GetString (ent->v.classname));
			ent->v.origin[i] = 0;
		}
		if (ent->v.velocity[i] > sv_maxvelocity.value)
			ent->v.velocity[i] = sv_maxvelocity.value;
		else if (ent->v.velocity[i] < -sv_maxvelocity.value)
			ent->v.velocity[i] = -sv_maxvelocity.value;
	}
}

/*
=============
SV_RunThink

Runs thinking code if time.  There is some play in the exact time the think
function will be called, because it is called before any movement is done
in a frame.  Not used for pushmove objects, because they must be exact.
Returns false if the entity removed itself.
=============
*/
extern cvar_t sv_speeds;

double sv_speeds_think_ms, sv_speeds_pusher_ms, sv_speeds_build_ms;
int	   sv_speeds_thinks, sv_speeds_pushers, sv_speeds_pushables, sv_speeds_grid_entries;

static qboolean SV_RunThink (edict_t *ent)
{
	float	 thinktime;
	double	 think_start = 0;
	qboolean alive;

	thinktime = ent->v.nextthink;
	if (thinktime <= 0 || thinktime > qcvm->time + host_frametime)
		return true;

	if (sv_speeds.value && qcvm == &sv.qcvm)
		think_start = Sys_DoubleTime ();

	if (thinktime < qcvm->time)
		thinktime = qcvm->time; // don't let things stay in the past.
								// it is possible to start that way
								// by a trigger with a local time.

	ent->oldthinktime = thinktime;
	ent->oldframe = ent->v.frame; // johnfitz

	ent->v.nextthink = 0;
	pr_global_struct->time = thinktime;
	pr_global_struct->self = EDICT_TO_PROG (ent);
	pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
	ED_Retain (ent);
	PR_ExecuteProgram (ent->v.think);

	ent->lastthink = 0;
	alive = !ent->free;
	if (alive && ent->v.groundentity && ent->v.nextthink > 0 && ent->v.nextthink - thinktime < 0.105f &&
		ent->v.groundentity <= (qcvm->num_edicts - 1) * qcvm->edict_size)
	{
		edict_t *pusher = PROG_TO_EDICT (ent->v.groundentity);
		if (!pusher->free)
		{
			float pusher_remaining = pusher->v.nextthink - pusher->v.ltime;
			if (pusher_remaining > 0)
			{
				float time = q_min ((int)((ent->v.nextthink - qcvm->time) / host_frametime) * host_frametime, pusher_remaining);
				for (int i = 0; i < 3; i++)
				{
					ent->predthinkpos[i] = ent->v.origin[i] + pusher->v.velocity[i] * time;
					if (pusher->v.velocity[i] != 0.0f)
						ent->lastthink = thinktime;
				}
			}
		}
	}
	ED_Release (ent);

	if (think_start != 0)
	{
		sv_speeds_think_ms += (Sys_DoubleTime () - think_start) * 1000.0;
		sv_speeds_thinks++;
	}

	return alive;
}

/*
==================
SV_Impact

Two entities have touched, so run their touch functions
==================
*/
static void SV_Impact (edict_t *e1, edict_t *e2)
{
	assert (!e1->free && !e2->free);

	int old_self, old_other, e1_prog, e2_prog;

	old_self = pr_global_struct->self;
	old_other = pr_global_struct->other;
	e1_prog = EDICT_TO_PROG (e1);
	e2_prog = EDICT_TO_PROG (e2);

	pr_global_struct->time = qcvm->time;
	ED_Retain (e1);
	ED_Retain (e2);

	if (e1->v.touch && e1->v.solid != SOLID_NOT)
	{
		pr_global_struct->self = e1_prog;
		pr_global_struct->other = e2_prog;
		PR_ExecuteProgram (e1->v.touch);
	}

	// Run e2's touch function if e2 survives e1's callback.
	if (!e2->free && e2->v.touch && e2->v.solid != SOLID_NOT)
	{
		pr_global_struct->self = e2_prog;
		pr_global_struct->other = e1_prog;
		PR_ExecuteProgram (e2->v.touch);
	}

	ED_Release (e2);
	ED_Release (e1);

	pr_global_struct->self = old_self;
	pr_global_struct->other = old_other;
}

/*
==================
ClipVelocity

Slide off of the impacting object
returns the blocked flags (1 = floor, 2 = step / wall)
==================
*/
#define STOP_EPSILON 0.1

static int ClipVelocity (vec3_t in, vec3_t normal, vec3_t out, float overbounce)
{
	float backoff;
	float change;
	int	  i, blocked;

	blocked = 0;
	if (normal[2] > 0)
		blocked |= 1; // floor
	if (!normal[2])
		blocked |= 2; // step

	backoff = DotProduct (in, normal) * overbounce;

	for (i = 0; i < 3; i++)
	{
		change = normal[i] * backoff;
		out[i] = in[i] - change;
		if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
			out[i] = 0;
	}

	return blocked;
}

typedef enum
{
	SV_PUSHER_CONTACT_NONE,
	SV_PUSHER_CONTACT_SUPPORT_FLOOR,
	SV_PUSHER_CONTACT_SUPPORT_SIDE
} sv_pusher_contact_t;

static sv_pusher_contact_t SV_ClassifyWalkSupportContact (trace_t *trace)
{
	float  support_dot;
	vec3_t tangent_normal;

	if (!sv_walk_support_pusher || trace->ent != sv_walk_support_pusher)
		return SV_PUSHER_CONTACT_NONE;

	support_dot = DotProduct (trace->plane.normal, sv_walk_support_normal);
	if (support_dot > MIN_WALK_NORMAL)
		return SV_PUSHER_CONTACT_SUPPORT_FLOOR;
	if (support_dot <= 0)
		return SV_PUSHER_CONTACT_NONE;

	// While walking on a pusher, a non-floor contact with that same pusher is
	// lateral support geometry.  Clip against its tangent component so it can't
	// inject velocity away from the support plane the client is standing on.
	VectorMA (trace->plane.normal, -support_dot, sv_walk_support_normal, tangent_normal);
	if (VectorNormalize (tangent_normal) <= DIST_EPSILON)
		return SV_PUSHER_CONTACT_NONE;

	VectorCopy (tangent_normal, trace->plane.normal);
	return SV_PUSHER_CONTACT_SUPPORT_SIDE;
}

/*
============
SV_FlyMove

The basic solid body movement clip that slides along multiple planes
Returns the clipflags if the velocity was modified (hit something solid)
1 = floor
2 = wall / step
4 = dead stop
If steptrace is not NULL, the trace of any vertical wall hit will be stored
If move_velocity is supplied, use it for sweeps and clip both velocities.
The stored entity velocity must already include the full gravity update.
============
*/
#define MAX_CLIP_PLANES 5
static int SV_FlyMove (edict_t *ent, float time, const vec3_t move_velocity, trace_t *steptrace, qboolean callbacks)
{
	int					bumpcount, numbumps;
	vec3_t				dir;
	float				d;
	int					numplanes;
	vec3_t				planes[MAX_CLIP_PLANES];
	vec3_t				primal_velocity, original_velocity, new_velocity;
	vec3_t				sweep_velocity, original_end_velocity, new_end_velocity = {0}, impact_velocity;
	int					i, j;
	trace_t				trace;
	vec3_t				end;
	float				time_left;
	int					blocked;
	sv_pusher_contact_t pusher_contact;

	numbumps = 4;

	blocked = 0;
	VectorCopy (move_velocity ? move_velocity : ent->v.velocity, sweep_velocity);
	VectorCopy (sweep_velocity, original_velocity);
	VectorCopy (sweep_velocity, primal_velocity);
	VectorCopy (sweep_velocity, new_velocity);
	VectorCopy (ent->v.velocity, original_end_velocity);
	numplanes = 0;

	time_left = time;

	for (bumpcount = 0; bumpcount < numbumps; bumpcount++)
	{
		if (!sweep_velocity[0] && !sweep_velocity[1] && !sweep_velocity[2])
			break;

		for (i = 0; i < 3; i++)
			end[i] = ent->v.origin[i] + time_left * sweep_velocity[i];

		trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);

		if (trace.allsolid)
		{ // entity is trapped in another solid
			VectorCopy (vec3_origin, ent->v.velocity);
			return 3;
		}

		if (trace.fraction > 0)
		{ // actually covered some distance
			VectorCopy (trace.endpos, ent->v.origin);
			VectorCopy (sweep_velocity, original_velocity);
			VectorCopy (ent->v.velocity, original_end_velocity);
			numplanes = 0;
		}

		if (trace.fraction == 1)
			break; // moved the entire distance

		if (!trace.ent)
			Sys_Error ("SV_FlyMove: !trace.ent");

		pusher_contact = SV_ClassifyWalkSupportContact (&trace);

		if (pusher_contact != SV_PUSHER_CONTACT_SUPPORT_SIDE && trace.plane.normal[2] > MIN_WALK_NORMAL)
		{
			blocked |= 1; // floor
			if (trace.ent->v.solid == SOLID_BSP)
			{
				ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
				ent->v.groundentity = EDICT_TO_PROG (trace.ent);
			}
		}
		if (pusher_contact == SV_PUSHER_CONTACT_SUPPORT_SIDE || !trace.plane.normal[2])
		{
			blocked |= 2; // step
			if (steptrace)
				*steptrace = trace; // save for player extrafriction
		}

		//
		// run the impact function
		//
		assert_always (!ent->free);

		VectorCopy (ent->v.velocity, impact_velocity);
		if (callbacks)
			SV_Impact (ent, trace.ent);
		if (ent->free)
			break; // removed by the impact function

		// The crease branch below uses the current velocity, including QC edits.
		// A replacement velocity must not inherit the old gravity sweep bias.
		if (!VectorCompare (ent->v.velocity, impact_velocity))
			VectorCopy (ent->v.velocity, sweep_velocity);

		time_left -= time_left * trace.fraction;

		// cliped to another plane
		if (numplanes >= MAX_CLIP_PLANES)
		{ // this shouldn't really happen
			VectorCopy (vec3_origin, ent->v.velocity);
			return 3;
		}

		VectorCopy (trace.plane.normal, planes[numplanes]);
		numplanes++;

		//
		// Clip both velocities against the same planes. A candidate must also
		// keep the end velocity out of every plane, even near a jump's apex.
		//
		for (i = 0; i < numplanes; i++)
		{
			ClipVelocity (original_velocity, planes[i], new_velocity, 1);
			ClipVelocity (original_end_velocity, planes[i], new_end_velocity, 1);
			for (j = 0; j < numplanes; j++)
				if (j != i)
				{
					if (DotProduct (new_velocity, planes[j]) < 0 || DotProduct (new_end_velocity, planes[j]) < 0)
						break; // not ok
				}
			if (j == numplanes)
				break;
		}

		if (i != numplanes)
		{ // go along this plane
			VectorCopy (new_velocity, sweep_velocity);
			VectorCopy (new_end_velocity, ent->v.velocity);
		}
		else
		{ // go along the crease
			if (numplanes != 2)
			{
				//				Con_Printf ("clip velocity, numplanes == %i\n",numplanes);
				VectorCopy (vec3_origin, ent->v.velocity);
				return 7;
			}
			CrossProduct (planes[0], planes[1], dir);
			d = DotProduct (dir, sweep_velocity);
			VectorScale (dir, d, sweep_velocity);
			d = DotProduct (dir, ent->v.velocity);
			VectorScale (dir, d, ent->v.velocity);
		}

		//
		// if original velocity is against the original velocity, stop dead
		// to avoid tiny occilations in sloping corners
		//
		if (DotProduct (sweep_velocity, primal_velocity) <= 0)
		{
			VectorCopy (vec3_origin, ent->v.velocity);
			return blocked;
		}
	}

	return blocked;
}

static float SV_EntGravity (edict_t *ent)
{
	eval_t *val = GetEdictFieldValue (ent, ED_FindFieldOffset ("gravity"));
	return (val && val->_float) ? val->_float : 1.0f;
}

/*
============
SV_AddGravity

Apply gravity fully before movement and touch callbacks.
The separate sweep velocity preserves the canonical 72Hz freefall trajectory.
Clip both velocities on impact; no post-movement gravity update is needed.
============
*/
static void SV_AddGravity (edict_t *ent, vec3_t move_velocity)
{
	const float	 gravity = SV_EntGravity (ent) * sv_gravity.value;
	const double move_time = sv_analyticphysics_frame ? (host_frametime + 1.0 / MAX_PHYSICS_FREQ) * 0.5 : host_frametime;

	VectorCopy (ent->v.velocity, move_velocity);
	move_velocity[2] -= gravity * move_time;
	ent->v.velocity[2] -= gravity * host_frametime;
}

/*
===============================================================================

PUSHMOVE

===============================================================================
*/

// 0=off; 1=legacy DIST_EPSILON nudge, clients only; 2=legacy nudge, all entities; 3=robust pusher contact (default)
cvar_t sv_gameplayfix_elevators = {"sv_gameplayfix_elevators", "3", CVAR_NONE};

// Private C-side movement frame for pusher support. QuakeC still observes the
// normal FL_ONGROUND, groundentity, origin and velocity contract.
typedef enum
{
	SV_MOVE_FRAME_NONE,
	SV_MOVE_FRAME_GROUND
} sv_client_move_frame_state_t;

typedef struct
{
	edict_t						*pusher;
	sv_client_move_frame_state_t state;
	vec3_t						 support_normal;
} sv_client_move_frame_t;

typedef struct
{
	unsigned					 frame;
	int							 pusher_entnum;
	sv_client_move_frame_state_t state;
	vec3_t						 pusher_move;
} sv_pusher_support_record_t;

typedef struct
{
	qboolean				   present;
	qboolean				   onground;
	int						   groundentity;
	sv_pusher_support_record_t record;
} sv_pusher_support_backup_t;

static void SV_BeginPusherSupportFrame (void)
{
	if (!qcvm->pusher_support)
		qcvm->pusher_support = HashMap_Create (int, sv_pusher_support_record_t, &HashInt32, NULL);

	qcvm->pusher_support_frame++;
	if (!qcvm->pusher_support_frame)
	{
		// counter wrapped, so every stored frame stamp is now meaningless
		HashMap_Destroy (qcvm->pusher_support);
		qcvm->pusher_support = HashMap_Create (int, sv_pusher_support_record_t, &HashInt32, NULL);
		qcvm->pusher_support_frame = 1;
	}
}

static qboolean SV_TracePusherFloorAtOrigin (edict_t *ent, edict_t *pusher, const vec3_t pusher_origin, float probe_distance, trace_t *trace)
{
	int	   i;
	vec3_t old_absmin, old_absmax;
	vec3_t old_origin, start, end;

	for (i = 0; i < 3; i++)
	{
		const float delta = pusher_origin[i] - pusher->v.origin[i];
		old_absmin[i] = pusher->v.absmin[i] + delta;
		old_absmax[i] = pusher->v.absmax[i] + delta;
	}

	if (ent->v.absmin[0] >= old_absmax[0] || ent->v.absmin[1] >= old_absmax[1] || ent->v.absmax[0] <= old_absmin[0] || ent->v.absmax[1] <= old_absmin[1])
		return false;
	if (ent->v.absmin[2] > old_absmax[2] + probe_distance || ent->v.absmax[2] < old_absmin[2] - PUSH_CONTACT_EPSILON)
		return false;

	VectorCopy (pusher->v.origin, old_origin);
	VectorCopy (pusher_origin, pusher->v.origin);

	VectorCopy (ent->v.origin, start);
	start[2] += PUSH_CONTACT_EPSILON;
	VectorCopy (ent->v.origin, end);
	end[2] -= probe_distance;
	*trace = SV_ClipMoveToEntity (pusher, start, ent->v.mins, ent->v.maxs, end, CONTENTMASK_ANYSOLID);

	VectorCopy (old_origin, pusher->v.origin);

	return !trace->startsolid && trace->fraction < 1 && trace->plane.normal[2] > MIN_WALK_NORMAL;
}

static float SV_PusherMoveTimeThisFrame (edict_t *pusher)
{
	float thinktime;
	float movetime;

	thinktime = pusher->v.nextthink;
	if (thinktime < pusher->v.ltime + host_frametime)
	{
		movetime = thinktime - pusher->v.ltime;
		if (movetime < 0)
			movetime = 0;
	}
	else
		movetime = host_frametime;

	return movetime;
}

static qboolean SV_IsSupportPusher (edict_t *pusher)
{
	if (!pusher || pusher->free)
		return false;
	if (pusher->v.movetype != MOVETYPE_PUSH || pusher->v.solid != SOLID_BSP)
		return false;
	return true;
}

static qboolean SV_PusherWillMoveThisFrame (edict_t *pusher)
{
	if (!pusher->v.velocity[0] && !pusher->v.velocity[1] && !pusher->v.velocity[2])
		return false;
	return SV_PusherMoveTimeThisFrame (pusher) > 0;
}

static edict_t *SV_GetGroundPusher (edict_t *ent)
{
	edict_t *ground;

	if (sv_gameplayfix_elevators.value < 3.f || !((int)ent->v.flags & FL_ONGROUND))
		return NULL;
	if (ent->v.groundentity <= 0 || ent->v.groundentity > (qcvm->num_edicts - 1) * qcvm->edict_size)
		return NULL;

	ground = PROG_TO_EDICT (ent->v.groundentity);
	if (!SV_IsSupportPusher (ground))
		return NULL;

	return ground;
}

// Returns NULL when the entity has no record; only riders have one.
static sv_pusher_support_record_t *SV_GetPusherSupportRecord (edict_t *ent)
{
	int entnum;

	if (!qcvm->pusher_support)
		return NULL;

	entnum = NUM_FOR_EDICT (ent);
	if (entnum <= 0 || entnum >= MAX_EDICTS)
		return NULL;

	return HashMap_Lookup (sv_pusher_support_record_t, qcvm->pusher_support, &entnum);
}

static void SV_GetAppliedPusherSupportMove (edict_t *ent, vec3_t move)
{
	const sv_pusher_support_record_t *support = SV_GetPusherSupportRecord (ent);

	VectorCopy (vec3_origin, move);
	if (!support)
		return;
	if (support->frame == qcvm->pusher_support_frame)
		VectorCopy (support->pusher_move, move);
}

// A missing record is a valid state to restore, so the backup carries a flag
// rather than relying on a zeroed record meaning "absent". Public ground state
// is part of the same transaction because recording support updates it too.
static void SV_BackupPusherSupport (edict_t *ent, sv_pusher_support_backup_t *backup)
{
	const sv_pusher_support_record_t *support = SV_GetPusherSupportRecord (ent);

	memset (backup, 0, sizeof (*backup));
	backup->onground = (int)ent->v.flags & FL_ONGROUND;
	backup->groundentity = ent->v.groundentity;
	if (support)
	{
		backup->present = true;
		backup->record = *support;
	}
}

static void SV_RestorePusherSupport (edict_t *ent, const sv_pusher_support_backup_t *backup)
{
	int entnum = NUM_FOR_EDICT (ent);

	if (backup->onground)
		ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
	else
		ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
	ent->v.groundentity = backup->groundentity;

	if (entnum <= 0 || entnum >= MAX_EDICTS)
		return;

	if (backup->present)
		HashMap_Insert (qcvm->pusher_support, &entnum, &backup->record);
	else
		HashMap_Erase (qcvm->pusher_support, &entnum);
}

static qboolean SV_WritePusherSupportRecord (edict_t *ent, edict_t *pusher, sv_client_move_frame_state_t state, const vec3_t pusher_move)
{
	int						   pushernum, entnum;
	sv_pusher_support_record_t record;

	pushernum = NUM_FOR_EDICT (pusher);
	if (pushernum <= 0 || pushernum >= MAX_EDICTS)
		return false;
	entnum = NUM_FOR_EDICT (ent);
	if (entnum <= 0 || entnum >= MAX_EDICTS)
		return false;

	record.frame = qcvm->pusher_support_frame;
	record.pusher_entnum = pushernum;
	record.state = state;
	VectorCopy (pusher_move, record.pusher_move);

	HashMap_Insert (qcvm->pusher_support, &entnum, &record);
	return true;
}

static qboolean SV_EntityGroundEntityIsPusher (edict_t *ent, edict_t *pusher)
{
	if (ent->v.groundentity <= 0 || ent->v.groundentity > (qcvm->num_edicts - 1) * qcvm->edict_size)
		return false;

	return PROG_TO_EDICT (ent->v.groundentity) == pusher;
}

static qboolean SV_MovetypeUsesGroundFlag (edict_t *ent)
{
	return ent->v.movetype == MOVETYPE_WALK || ent->v.movetype == MOVETYPE_STEP || ent->v.movetype == MOVETYPE_TOSS || ent->v.movetype == MOVETYPE_GIB;
}

static qboolean SV_EntityClaimsPusherSupport (edict_t *ent, edict_t *pusher)
{
	if (SV_MovetypeUsesGroundFlag (ent))
		return ((int)ent->v.flags & FL_ONGROUND) && SV_EntityGroundEntityIsPusher (ent, pusher);

	// Other movetypes do not consistently use FL_ONGROUND/groundentity, but an
	// explicit assignment to another ground entity still relinquishes support.
	return !ent->v.groundentity || SV_EntityGroundEntityIsPusher (ent, pusher);
}

static qboolean SV_HasRecentPusherSupportRecord (edict_t *ent, edict_t *pusher)
{
	const sv_pusher_support_record_t *support = SV_GetPusherSupportRecord (ent);

	if (!support)
		return false;
	if (support->state != SV_MOVE_FRAME_GROUND)
		return false;
	if (support->pusher_entnum != NUM_FOR_EDICT (pusher))
		return false;

	// carried this frame or the one before it; older records are stale
	return support->frame == qcvm->pusher_support_frame || support->frame + 1 == qcvm->pusher_support_frame;
}

// groundentity only identifies the pusher to probe; support still requires a floor trace.
static qboolean SV_EntityHasPusherSupportAtOrigin (edict_t *ent, edict_t *pusher, const vec3_t pusher_origin, trace_t *trace)
{
	if (!SV_IsSupportPusher (pusher))
		return false;

	// QuakeC can explicitly leave established support (a player jump, fiend
	// pounce, etc.) after this entity's physics turn but before the pusher runs.
	// The old geometric contact lasts for the remainder of that tick and must
	// not re-establish the support record. Initial contact remains geometry-based
	// because some entity types do not maintain public ground state consistently.
	if (SV_HasRecentPusherSupportRecord (ent, pusher) && !SV_EntityClaimsPusherSupport (ent, pusher))
		return false;

	if (ent->v.movetype == MOVETYPE_WALK && !((int)ent->v.flags & FL_ONGROUND) && !SV_EntityGroundEntityIsPusher (ent, pusher))
		return false;

	return SV_TracePusherFloorAtOrigin (ent, pusher, pusher_origin, PUSH_CONTACT_EPSILON, trace);
}

// Contact established by a floor trace persists until something positively
// breaks it. Re-deriving support from geometry every frame lets float rounding
// drop a rider that never actually left the pusher.
static qboolean SV_HasPersistentPusherSupport (edict_t *ent, edict_t *pusher)
{
	if (!SV_HasRecentPusherSupportRecord (ent, pusher))
		return false;
	// QuakeC can jump, teleport, or reassign ground state after this entity's
	// release pass but before a later pusher consumes the record.
	if (!SV_EntityClaimsPusherSupport (ent, pusher))
		return false;

	return true;
}

static void SV_BreakPusherSupport (edict_t *ent)
{
	int entnum = NUM_FOR_EDICT (ent);

	if (entnum <= 0 || entnum >= MAX_EDICTS)
		return;

	HashMap_Erase (qcvm->pusher_support, &entnum);
}

static void SV_ClearRecordedPusherSupport (edict_t *ent, edict_t *pusher)
{
	SV_BreakPusherSupport (ent);

	// Only clear public ground state backed by our private support record.
	// Mods may use FL_ONGROUND/groundentity for their own movement logic even
	// when the named pusher is not geometrically beneath the entity.
	if (SV_EntityGroundEntityIsPusher (ent, pusher))
	{
		ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
		ent->v.groundentity = 0;
	}
}

static void SV_RecordPusherSupport (edict_t *ent, edict_t *pusher, const vec3_t pusher_move)
{
	trace_t trace;

	if (sv_gameplayfix_elevators.value < 3.f)
		return;

	// an established rider wedged against a neighbour fails the floor trace even
	// though it never left the pusher, so keep carrying it on the stored record
	if (!SV_EntityHasPusherSupportAtOrigin (ent, pusher, pusher->v.origin, &trace) && !SV_HasPersistentPusherSupport (ent, pusher))
		return;
	if (!SV_WritePusherSupportRecord (ent, pusher, SV_MOVE_FRAME_GROUND, pusher_move))
		return;

	if (SV_MovetypeUsesGroundFlag (ent))
	{
		ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
		ent->v.groundentity = EDICT_TO_PROG (pusher);
	}
}

static void SV_ClearClientMoveFrame (sv_client_move_frame_t *frame)
{
	frame->pusher = NULL;
	frame->state = SV_MOVE_FRAME_NONE;
	VectorCopy (vec3_origin, frame->support_normal);
}

static void SV_SetClientPusherMoveFrame (sv_client_move_frame_t *frame, edict_t *pusher, sv_client_move_frame_state_t state, const float *support_normal)
{
	frame->pusher = pusher;
	frame->state = state;
	if (support_normal)
		VectorCopy (support_normal, frame->support_normal);
	else
		VectorCopy (vec3_origin, frame->support_normal);
}

static qboolean SV_CaptureRecordedPusherMoveFrame (edict_t *ent, sv_client_move_frame_t *frame, const sv_pusher_support_record_t *record, edict_t *pusher)
{
	trace_t trace;

	switch (record->state)
	{
	case SV_MOVE_FRAME_GROUND:
		if (ent->v.groundentity != EDICT_TO_PROG (pusher))
			return false;
		if (!SV_EntityHasPusherSupportAtOrigin (ent, pusher, pusher->v.origin, &trace))
			return false;

		ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
		SV_SetClientPusherMoveFrame (frame, pusher, SV_MOVE_FRAME_GROUND, trace.plane.normal);
		return true;

	default:
		return false;
	}
}

static void SV_CaptureClientMoveFrameBeforeQC (edict_t *ent, sv_client_move_frame_t *frame)
{
	const sv_pusher_support_record_t *record;
	edict_t							 *pusher;
	trace_t							  trace;

	SV_ClearClientMoveFrame (frame);
	if (sv_gameplayfix_elevators.value < 3.f)
		return;

	record = SV_GetPusherSupportRecord (ent);
	if (record && record->frame && record->frame + 1 == qcvm->pusher_support_frame && record->pusher_entnum > 0 && record->pusher_entnum < qcvm->num_edicts)
	{
		pusher = EDICT_NUM (record->pusher_entnum);
		if (SV_IsSupportPusher (pusher))
		{
			if (SV_CaptureRecordedPusherMoveFrame (ent, frame, record, pusher))
				return;
		}
	}

	pusher = SV_GetGroundPusher (ent);
	if (pusher && SV_PusherWillMoveThisFrame (pusher) && SV_EntityHasPusherSupportAtOrigin (ent, pusher, pusher->v.origin, &trace))
		SV_SetClientPusherMoveFrame (frame, pusher, SV_MOVE_FRAME_GROUND, trace.plane.normal);
}

static qboolean SV_ClientMoveFrameHasGroundSupport (const sv_client_move_frame_t *frame)
{
	if (frame->state != SV_MOVE_FRAME_GROUND)
		return false;
	if (!SV_IsSupportPusher (frame->pusher))
		return false;
	if (DotProduct (frame->support_normal, frame->support_normal) <= DIST_EPSILON * DIST_EPSILON)
		return false;
	return true;
}

static void SV_SetWalkMoveFrameClipContext (const sv_client_move_frame_t *move_frame)
{
	if (!SV_ClientMoveFrameHasGroundSupport (move_frame))
	{
		sv_walk_support_pusher = NULL;
		VectorCopy (vec3_origin, sv_walk_support_normal);
		return;
	}

	sv_walk_support_pusher = move_frame->pusher;
	VectorCopy (move_frame->support_normal, sv_walk_support_normal);
}

static void SV_ClearWalkSupportClipContext (void)
{
	sv_walk_support_pusher = NULL;
	VectorCopy (vec3_origin, sv_walk_support_normal);
}

static int
SV_FlyMoveWithMoveFrameClipContext (edict_t *ent, float time, const sv_client_move_frame_t *move_frame, const vec3_t move_velocity, trace_t *steptrace, qboolean callbacks)
{
	int clip;

	SV_SetWalkMoveFrameClipContext (move_frame);
	clip = SV_FlyMove (ent, time, move_velocity, steptrace, callbacks);
	SV_ClearWalkSupportClipContext ();
	return clip;
}

static void SV_DropClientMoveFramePusherGround (edict_t *ent, sv_client_move_frame_t *move_frame)
{
	edict_t *pusher = move_frame->pusher;

	SV_ClearClientMoveFrame (move_frame);
	if (ent->v.groundentity == EDICT_TO_PROG (pusher))
		ent->v.groundentity = 0;
}

static void SV_UpdateClientMoveFrameAfterQC (edict_t *ent, sv_client_move_frame_t *move_frame)
{
	if (!move_frame->pusher)
		return;

	if (move_frame->state != SV_MOVE_FRAME_GROUND)
		return;

	if (!((int)ent->v.flags & FL_ONGROUND) && ent->v.movetype == MOVETYPE_WALK)
		SV_DropClientMoveFramePusherGround (ent, move_frame);
}

static qboolean SV_GroundClientOnMoveFramePusher (edict_t *ent, const sv_client_move_frame_t *move_frame)
{
	trace_t trace;

	if (!SV_ClientMoveFrameHasGroundSupport (move_frame))
		return false;
	if (!SV_TracePusherFloorAtOrigin (ent, move_frame->pusher, move_frame->pusher->v.origin, STEPSIZE, &trace))
		return false;

	VectorCopy (trace.endpos, ent->v.origin);
	SV_LinkEdict (ent, false);
	ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
	ent->v.groundentity = EDICT_TO_PROG (move_frame->pusher);
	ent->v.velocity[2] = 0;
	return true;
}

static qboolean SV_TestEntityPositionOnPusher (edict_t *ent, edict_t *pusher, const vec3_t pusher_origin, const vec3_t ent_origin)
{
	vec3_t	old_origin;
	vec3_t	trace_origin;
	trace_t trace;

	VectorCopy (pusher->v.origin, old_origin);
	VectorCopy (pusher_origin, pusher->v.origin);
	VectorCopy (ent_origin, trace_origin);
	trace = SV_ClipMoveToEntity (pusher, trace_origin, ent->v.mins, ent->v.maxs, trace_origin, CONTENTMASK_ANYSOLID);
	VectorCopy (old_origin, pusher->v.origin);
	return trace.startsolid;
}

static qboolean SV_EntityPositionBlockedIgnoringPusher (edict_t *ent, edict_t *pusher)
{
	float	 solid_backup;
	qboolean blocked;

	solid_backup = pusher->v.solid;
	pusher->v.solid = SOLID_NOT;
	blocked = SV_TestEntityPosition (ent) != NULL;
	pusher->v.solid = solid_backup;
	return blocked;
}

static qboolean SV_EntityRidingPusher (edict_t *ent, edict_t *pusher)
{
	return ((int)ent->v.flags & FL_ONGROUND) && SV_EntityGroundEntityIsPusher (ent, pusher);
}

// Owns the release decision for persistent support at the entity's physics
// turn. Consumers re-check the live ground state because QuakeC can change it
// again later in the same frame.
static void SV_UpdatePersistentPusherSupport (edict_t *ent)
{
	const sv_pusher_support_record_t *support;
	edict_t							 *pusher;
	trace_t							  trace;
	int								  pusher_entnum;

	if (sv_gameplayfix_elevators.value < 3.f)
		return;

	support = SV_GetPusherSupportRecord (ent);
	if (!support)
		return;
	if (support->state != SV_MOVE_FRAME_GROUND || support->pusher_entnum <= 0)
		return;

	// the record lives in hash map storage that any erase can move, so read what
	// is needed before touching the map again
	pusher_entnum = support->pusher_entnum;
	support = NULL;

	if (pusher_entnum >= qcvm->num_edicts)
	{
		SV_BreakPusherSupport (ent);
		return;
	}

	pusher = EDICT_NUM (pusher_entnum);
	if (!SV_IsSupportPusher (pusher))
	{
		if (pusher->free || pusher->v.solid == SOLID_NOT || pusher->v.solid == SOLID_TRIGGER)
			SV_ClearRecordedPusherSupport (ent, pusher);
		else
			SV_BreakPusherSupport (ent);
		return;
	}

	// left the ground under its own power, or QuakeC moved it onto something else
	if (!SV_EntityClaimsPusherSupport (ent, pusher))
	{
		SV_BreakPusherSupport (ent);
		return;
	}

	if (!SV_TracePusherFloorAtOrigin (ent, pusher, pusher->v.origin, PUSH_RELEASE_EPSILON, &trace))
		SV_ClearRecordedPusherSupport (ent, pusher);
}

// Adopt an existing QuakeC ground claim only after geometry confirms it. This
// gives riders present at spawn/load the same persistent support as riders
// already carried by SV_PushMove, without rewriting non-geometric ground state
// used by mods for custom movement.
static void SV_AdoptPusherSupport (edict_t *ent)
{
	edict_t *pusher;
	trace_t	 trace;

	if (sv_gameplayfix_elevators.value < 3.f || SV_GetPusherSupportRecord (ent))
		return;

	pusher = SV_GetGroundPusher (ent);
	if (!pusher)
		return;
	if (!SV_EntityHasPusherSupportAtOrigin (ent, pusher, pusher->v.origin, &trace))
		return;

	SV_WritePusherSupportRecord (ent, pusher, SV_MOVE_FRAME_GROUND, vec3_origin);
}

static qboolean SV_PusherBoundsOverlapEntity (edict_t *ent, const vec3_t mins, const vec3_t maxs)
{
	return !(
		ent->v.absmin[0] >= maxs[0] || ent->v.absmin[1] >= maxs[1] || ent->v.absmin[2] >= maxs[2] || ent->v.absmax[0] <= mins[0] ||
		ent->v.absmax[1] <= mins[1] || ent->v.absmax[2] <= mins[2]);
}

static qboolean
SV_PusherAffectsEntity (edict_t *ent, edict_t *pusher, const vec3_t pushorig, const vec3_t mins, const vec3_t maxs, qboolean robust_push, qboolean *riding)
{
	trace_t support_trace;

	*riding = false;

	if (robust_push && (SV_EntityHasPusherSupportAtOrigin (ent, pusher, pushorig, &support_trace) || SV_HasPersistentPusherSupport (ent, pusher)))
	{
		*riding = true;
		return true;
	}

	if (!robust_push && SV_EntityRidingPusher (ent, pusher))
	{
		*riding = true;
		return true;
	}

	if (!SV_PusherBoundsOverlapEntity (ent, mins, maxs))
		return false;

	if (!robust_push)
	{
		if (pusher->v.skin < 0)
			return SV_ClipMoveToEntity (pusher, ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, CONTENTMASK_ANYSOLID).startsolid;
		return SV_TestEntityPosition (ent) != NULL;
	}

	// Test the active pusher only; SV_TestEntityPosition can report an
	// unrelated platform the entity is already standing on.
	return SV_TestEntityPositionOnPusher (ent, pusher, pusher->v.origin, ent->v.origin);
}

static qboolean SV_PusherBlockIsPersistentRiderContact (
	edict_t *ent, edict_t *pusher, edict_t *block, const vec3_t pushorig, const vec3_t entorig, qboolean robust_push, qboolean riding)
{
	if (!robust_push || !riding || block != pusher)
		return false;

	// Existing rider contact with this pusher is not a new crush.
	return SV_TestEntityPositionOnPusher (ent, pusher, pushorig, entorig);
}

static trace_t SV_PushEntityMove (edict_t *ent, vec3_t start, vec3_t end)
{
	if (ent->v.movetype == MOVETYPE_FLYMISSILE)
		return SV_Move (start, ent->v.mins, ent->v.maxs, end, MOVE_MISSILE, ent);
	else if (ent->v.solid == SOLID_TRIGGER || ent->v.solid == SOLID_NOT)
		// only clip against bmodels
		return SV_Move (start, ent->v.mins, ent->v.maxs, end, MOVE_NOMONSTERS, ent);
	else
		return SV_Move (start, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent);
}

static trace_t SV_PushEntityMoveWithIgnoreMask (edict_t *ent, vec3_t start, vec3_t end, const sv_ignore_edicts_t *ignore_mask)
{
	if (!ignore_mask)
		return SV_PushEntityMove (ent, start, end);
	if (ent->v.movetype == MOVETYPE_FLYMISSILE)
		return SV_MoveWithEdictIgnoreMask (start, ent->v.mins, ent->v.maxs, end, MOVE_MISSILE, ent, ignore_mask);
	else if (ent->v.solid == SOLID_TRIGGER || ent->v.solid == SOLID_NOT)
		return SV_MoveWithEdictIgnoreMask (start, ent->v.mins, ent->v.maxs, end, MOVE_NOMONSTERS, ent, ignore_mask);
	else
		return SV_MoveWithEdictIgnoreMask (start, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent, ignore_mask);
}

static edict_t *SV_TestEntityPositionWithIgnoreMask (edict_t *ent, const sv_ignore_edicts_t *ignore_mask)
{
	trace_t trace;

	if (!ignore_mask)
		return SV_TestEntityPosition (ent);

	trace = SV_MoveWithEdictIgnoreMask (ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, 0, ent, ignore_mask);
	if (trace.startsolid)
		return trace.ent ? trace.ent : qcvm->edicts;

	return NULL;
}

/*
============
SV_PushEntityTo

Does not change the entities velocity at all
============
*/
static trace_t SV_PushEntityToWithIgnoreMask (edict_t *ent, vec3_t end, const sv_ignore_edicts_t *ignore_mask, qboolean callbacks)
{
	trace_t trace;

	trace = SV_PushEntityMoveWithIgnoreMask (ent, ent->v.origin, end, ignore_mask);

	// a move that starts solid registers no impact, so an entity marginally inside the
	// pusher it rests on would glide through it and fall out the far side. un-embed
	// with a sweep against the pusher and redo the move so it collides normally.
	if (trace.startsolid && ent->v.groundentity && sv_gameplayfix_elevators.value >= 3.f)
	{
		edict_t *ground = PROG_TO_EDICT (ent->v.groundentity);
		if (ground != qcvm->edicts && !ground->free && ground->v.movetype == MOVETYPE_PUSH && ground->v.solid == SOLID_BSP &&
			SV_ClipMoveToEntity (ground, ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, CONTENTMASK_ANYSOLID).startsolid)
		{
			vec3_t	above;
			trace_t exit;

			VectorCopy (ent->v.origin, above);
			above[2] += PUSH_CONTACT_EPSILON;
			exit = SV_ClipMoveToEntity (ground, above, ent->v.mins, ent->v.maxs, ent->v.origin, CONTENTMASK_ANYSOLID);
			if (!exit.startsolid && exit.fraction < 1)
			{
				Con_DPrintf2 ("SV_PushEntityTo: un-embedded entity %i from pusher %i\n", NUM_FOR_EDICT (ent), NUM_FOR_EDICT (ground));
				VectorCopy (exit.endpos, ent->v.origin);
				trace = SV_PushEntityMoveWithIgnoreMask (ent, ent->v.origin, end, ignore_mask);
			}
		}
	}

	if (trace.ent)
		assert_always (!trace.ent->free);

	VectorCopy (trace.endpos, ent->v.origin);

	ED_Retain (ent);
	if (trace.ent)
		ED_Retain (trace.ent);

	SV_LinkEdict (ent, callbacks);

	// Run the impact only while both collision participants still exist.
	if (callbacks && !ent->free && trace.ent && !trace.ent->free)
		SV_Impact (ent, trace.ent);

	if (trace.ent)
		ED_Release (trace.ent);
	ED_Release (ent);

	return trace;
}

static trace_t SV_PushEntityTo (edict_t *ent, vec3_t end, qboolean callbacks)
{
	return SV_PushEntityToWithIgnoreMask (ent, end, NULL, callbacks);
}

// Appends in the caller's order, which for pusher candidates is already sorted.
static void SV_IgnoreEdictsAddRider (sv_ignore_edicts_t *list, edict_t *ent)
{
	// lookups binary search this, so an unsorted append would silently miss
	assert (!list->num_riders || list->riders[list->num_riders - 1] < ent);
	list->riders[list->num_riders++] = ent;
}

/*
============
SV_PushMove
============
*/

static void SV_PushMove (edict_t *pusher, float movetime)
{
	int		 i;
	edict_t *check, *block;
	vec3_t	 mins, maxs, move;
	vec3_t	 entorig, pushorig;
	vec3_t	 querymins, querymaxs;
	int		 num_moved;

	if (!pusher->v.velocity[0] && !pusher->v.velocity[1] && !pusher->v.velocity[2])
	{
		pusher->v.ltime += movetime;
		return;
	}

	const qboolean robust_push = (sv_gameplayfix_elevators.value >= 3.f);
	const float	   newltime = pusher->v.ltime + movetime;
	vec3_t		   neworigin;

	// PushGrid_GatherCandidates fills this up to MAX_EDICTS before it gives up
	TEMP_ALLOC_COND (edict_t *, push_candidates, MAX_EDICTS, push_grid_active);

	// everything below holds at most one entry per candidate, so it is sized once
	// the candidate list is known
	TEMP_ALLOC_DECL (edict_t *, moved_edict);
	TEMP_ALLOC_DECL (vec3_t, moved_from);
	TEMP_ALLOC_DECL (sv_pusher_support_backup_t, moved_support);
	TEMP_ALLOC_DECL (edict_t *, push_edict);
	TEMP_ALLOC_DECL (qboolean, push_riding);
	TEMP_ALLOC_DECL (edict_t *, rider_ignore_storage);

	sv_ignore_edicts_t pusher_ignore_mask = {NULL, 0, pusher};
	sv_ignore_edicts_t rider_ignore_mask = {NULL, 0, NULL};
	sv_ignore_edicts_t pusher_rider_ignore_mask = {NULL, 0, pusher};

	VectorScale (pusher->v.velocity, movetime, move);
	VectorAdd (pusher->v.origin, move, neworigin);
	for (i = 0; i < 3; i++)
	{
		mins[i] = pusher->v.absmin[i] + move[i];
		maxs[i] = pusher->v.absmax[i] + move[i];
		// the grid query must span the whole sweep: riders rest on the pre-move
		// box and are exempt from the final-box overlap test below
		querymins[i] = q_min (pusher->v.absmin[i], mins[i]);
		querymaxs[i] = q_max (pusher->v.absmax[i], maxs[i]);
	}

	VectorCopy (pusher->v.origin, pushorig);
	ED_Retain (pusher);

	// move the pusher to it's final position

	VectorCopy (neworigin, pusher->v.origin);
	pusher->v.ltime = newltime;
	SV_LinkEdict (pusher, false);

	// see if any solid entities are inside the final position
	num_moved = 0;

	edict_t **fast_list = NULL;
	int		  fast_count = 0;

	if (push_grid_active)
	{
		fast_count = PushGrid_GatherCandidates (querymins, querymaxs, push_candidates);
		if (fast_count >= 0)
			fast_list = push_candidates;
		// If the grid is unusable this tick, leave fast_list NULL and fall
		// back to the canonical edict scan below.
	}

	const int max_candidates = fast_list ? fast_count : qcvm->num_edicts;
	TEMP_ALLOC_ASSIGN_COND (rider_ignore_storage, max_candidates, robust_push);
	if (robust_push)
	{
		// both views share the rider storage and differ only in whether the pusher
		// is ignored too
		rider_ignore_mask.riders = rider_ignore_storage;
		pusher_rider_ignore_mask.riders = rider_ignore_storage;
	}
	TEMP_ALLOC_ASSIGN (moved_edict, max_candidates);
	TEMP_ALLOC_ASSIGN (moved_from, max_candidates);
	TEMP_ALLOC_ASSIGN (moved_support, max_candidates);
	TEMP_ALLOC_ASSIGN_COND (push_edict, max_candidates, robust_push);
	TEMP_ALLOC_ASSIGN_COND (push_riding, max_candidates, robust_push);

	int num_push = 0;

	if (robust_push)
	{
		int		 scan_e = -1;
		edict_t *scan_check = NEXT_EDICT (qcvm->edicts);

		while (true)
		{
			qboolean riding;

			// bounded by max_candidates, which is what push_edict/push_riding were
			// sized to; the trailing -1 skips entity 0
			if (scan_e >= (fast_list ? fast_count - 1 : max_candidates - 1 - 1))
				break;

			scan_e++;

			if (fast_list)
			{
				scan_check = fast_list[scan_e];
			}
			else if (scan_e > 0)
			{
				scan_check = NEXT_EDICT (scan_check);
			}

			if (scan_check->free)
				continue;

			if (!SV_IsPushable (scan_check))
				continue;

			if (!SV_PusherAffectsEntity (scan_check, pusher, pushorig, mins, maxs, true, &riding))
				continue;

			push_edict[num_push] = scan_check;
			push_riding[num_push] = riding;
			ED_Retain (scan_check);
			num_push++;

			if (riding)
				SV_IgnoreEdictsAddRider (&rider_ignore_mask, scan_check);
		}

		// candidates arrive in ascending pointer order, so the list is already
		// sorted; both masks view the same storage
		pusher_rider_ignore_mask.num_riders = rider_ignore_mask.num_riders;
	}

	int e = -1;

	// beware, we skip entity 0:
	check = NEXT_EDICT (qcvm->edicts);

	while (true)
	{
		// max_candidates rather than a fresh num_edicts read: the pusher's blocked
		// function can spawn entities, and the scratch arrays were already sized
		if (e >= (robust_push ? num_push - 1 : (fast_list ? fast_count - 1 : max_candidates - 1 - 1)))
			break;

		e++;

		qboolean riding = false;

		if (robust_push)
		{
			check = push_edict[e];
			riding = push_riding[e];
		}
		else
		{
			if (fast_list)
			{
				check = fast_list[e];
			}
			else if (e > 0)
			{
				check = NEXT_EDICT (check);
			}

			if (check->free)
				continue;

			if (!SV_IsPushable (check))
				continue;

			if (!SV_PusherAffectsEntity (check, pusher, pushorig, mins, maxs, false, &riding))
				continue;
		}

		if (check->free)
			continue;

		if (!SV_IsPushable (check))
			continue;

		// remove the onground flag for non-players. riders keep it under robust
		// push: the support layer owns their ground state, and a transient clear
		// that survives a blocked rollback reads as the rider leaving the ground,
		// which erases its support record
		if (check->v.movetype != MOVETYPE_WALK && !(robust_push && riding))
			check->v.flags = (int)check->v.flags & ~FL_ONGROUND;

		VectorCopy (check->v.origin, entorig);
		VectorCopy (check->v.origin, moved_from[num_moved]);
		SV_BackupPusherSupport (check, &moved_support[num_moved]);
		moved_edict[num_moved] = check;
		ED_Retain (check);
		num_moved++;

		// QIP fix for end.bsp
		if (pusher->v.solid == SOLID_BSP		  // everything that blocks: bsp models = map brushes = doors, plats, etc.
			|| pusher->v.solid == SOLID_BBOX	  // normally boxes
			|| pusher->v.solid == SOLID_SLIDEBOX) // normally monsters
		{
			const sv_ignore_edicts_t *move_ignore_mask = (robust_push && riding) ? &pusher_rider_ignore_mask : &pusher_ignore_mask;
			const sv_ignore_edicts_t *block_ignore_mask = (robust_push && riding) ? &rider_ignore_mask : NULL;
			vec3_t					  dest;

			if (robust_push)
			{
				vec3_t applied_move;

				if (riding)
					SV_GetAppliedPusherSupportMove (check, applied_move);
				else
					VectorCopy (vec3_origin, applied_move);

				// Supported entities are carried by the pusher frame once per
				// physics frame. Composite movers therefore apply only the
				// difference from the support motion already applied.
				for (i = 0; i < 3; i++)
					dest[i] = entorig[i] + move[i] - applied_move[i];
			}
			else
				VectorAdd (entorig, move, dest);

			// try moving the contacted entity
			SV_PushEntityToWithIgnoreMask (check, dest, move_ignore_mask, true);
			if (pusher->free)
				break;
			if (check->free)
				continue;

			// if it is still inside the pusher, block
			if (pusher->v.skin < 0)
			{ // if it has forced contents then do things in a slightly different order, so water can push properly.
				block = SV_TestEntityPositionWithIgnoreMask (check, move_ignore_mask);
			}
			else
				block = SV_TestEntityPositionWithIgnoreMask (check, block_ignore_mask);
		}
		else
			block = NULL;
		if (block)
		{ // fail the move
			if (check->v.mins[0] == check->v.maxs[0])
				continue;

			if (SV_PusherBlockIsPersistentRiderContact (check, pusher, block, pushorig, entorig, robust_push, riding))
			{
				if (riding)
					SV_RecordPusherSupport (check, pusher, move);
				continue;
			}

			// riders only embed through their ground contact and never deeper than
			// PUSH_CONTACT_EPSILON, so a single sweep from above recovers the exact
			// contact position. must run before the corpse path so items don't get
			// their bbox zeroed over a rounding error; real squeezes still crush.
			if (robust_push && riding && block == pusher)
			{
				vec3_t	pushedorg, above;
				trace_t settle;

				VectorCopy (check->v.origin, pushedorg);
				VectorCopy (check->v.origin, above);
				above[2] += PUSH_CONTACT_EPSILON;
				settle = SV_PushEntityMove (check, above, pushedorg);
				if (!settle.startsolid)
				{
					VectorCopy (settle.endpos, check->v.origin);
					if (!SV_TestEntityPositionWithIgnoreMask (check, &rider_ignore_mask))
					{
						SV_LinkEdict (check, false);
						SV_RecordPusherSupport (check, pusher, move);
						continue;
					}
					VectorCopy (pushedorg, check->v.origin);
				}
			}

			if (check->v.solid == SOLID_NOT || check->v.solid == SOLID_TRIGGER)
			{ // corpse
				check->v.mins[0] = check->v.mins[1] = 0;
				VectorCopy (check->v.mins, check->v.maxs);
				continue;
			}

			// try moving the entity up a bit if it's blocked by the pusher while also standing on it
			if (!robust_push && riding && block == pusher &&
				(sv_gameplayfix_elevators.value >= 2.f || (sv_gameplayfix_elevators.value && NUM_FOR_EDICT (check) <= svs.maxclients)))
			{
				check->v.origin[2] += DIST_EPSILON;
				if (!SV_TestEntityPosition (check))
					continue;
			}

			VectorCopy (entorig, check->v.origin);
			SV_LinkEdict (check, true);

			if (!pusher->free)
			{
				VectorCopy (pushorig, pusher->v.origin);
				SV_LinkEdict (pusher, false);
				pusher->v.ltime -= movetime;

				// if the pusher has a "blocked" function, call it
				// otherwise, just stay in place until the obstacle is gone
				if (!check->free && pusher->v.blocked)
				{
					pr_global_struct->self = EDICT_TO_PROG (pusher);
					pr_global_struct->other = EDICT_TO_PROG (check);
					PR_ExecuteProgram (pusher->v.blocked);
				}
			}

			// move back any entities we already moved
			for (i = 0; i < num_moved; i++)
			{
				if (moved_edict[i]->free)
					continue;
				SV_RestorePusherSupport (moved_edict[i], &moved_support[i]);
				VectorCopy (moved_from[i], moved_edict[i]->v.origin);
				SV_LinkEdict (moved_edict[i], false);
			}
			break;
		}

		if (riding)
			SV_RecordPusherSupport (check, pusher, move);
	}

	for (i = num_moved - 1; i >= 0; i--)
		ED_Release (moved_edict[i]);
	for (i = num_push - 1; i >= 0; i--)
		ED_Release (push_edict[i]);
	ED_Release (pusher);

	TEMP_FREE (rider_ignore_storage);
	TEMP_FREE (push_candidates);
	TEMP_FREE (push_riding);
	TEMP_FREE (push_edict);
	TEMP_FREE (moved_support);
	TEMP_FREE (moved_from);
	TEMP_FREE (moved_edict);
}

/*
================
SV_Physics_Pusher

================
*/
static void SV_Physics_Pusher (edict_t *ent)
{
	float	 thinktime;
	float	 oldltime;
	float	 movetime;
	double	 push_start = 0;
	qboolean timing;

	oldltime = ent->v.ltime;

	thinktime = ent->v.nextthink;
	movetime = SV_PusherMoveTimeThisFrame (ent);

	timing = sv_speeds.value && qcvm == &sv.qcvm && (movetime || thinktime > oldltime);
	if (timing)
		push_start = Sys_DoubleTime ();

	if (movetime)
	{
		SV_PushMove (ent, movetime); // advances ent->v.ltime if not blocked
	}

	if (!ent->free && thinktime > oldltime && thinktime <= ent->v.ltime)
	{
		ent->v.nextthink = 0;
		pr_global_struct->time = qcvm->time;
		pr_global_struct->self = EDICT_TO_PROG (ent);
		pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
		ED_Retain (ent);
		PR_ExecuteProgram (ent->v.think);
		ED_Release (ent);
	}

	if (timing)
	{
		sv_speeds_pusher_ms += (Sys_DoubleTime () - push_start) * 1000.0;
		sv_speeds_pushers++;
	}
}

/*
===============================================================================

CLIENT MOVEMENT

===============================================================================
*/

/*
=============
SV_CheckStuck

This is a big hack to try and fix the rare case of getting stuck in the world
clipping hull.
=============
*/
static void SV_CheckStuck (edict_t *ent)
{
	int	   i, j;
	int	   z;
	vec3_t org;

	if (!SV_TestEntityPosition (ent))
	{
		VectorCopy (ent->v.origin, ent->v.oldorigin);
		return;
	}

	VectorCopy (ent->v.origin, org);
	VectorCopy (ent->v.oldorigin, ent->v.origin);
	if (!SV_TestEntityPosition (ent))
	{
		Con_DPrintf ("Unstuck.\n");
		SV_LinkEdict (ent, true);
		return;
	}

	for (z = 0; z < 18; z++)
		for (i = -1; i <= 1; i++)
			for (j = -1; j <= 1; j++)
			{
				ent->v.origin[0] = org[0] + i;
				ent->v.origin[1] = org[1] + j;
				ent->v.origin[2] = org[2] + z;
				if (!SV_TestEntityPosition (ent))
				{
					Con_DPrintf ("Unstuck.\n");
					SV_LinkEdict (ent, true);
					return;
				}
			}

	VectorCopy (org, ent->v.origin);
	Con_DPrintf ("player is stuck.\n");
}

static void SV_CheckStuckWithMoveFrame (edict_t *ent, const sv_client_move_frame_t *move_frame)
{
	if (!SV_ClientMoveFrameHasGroundSupport (move_frame))
	{
		SV_CheckStuck (ent);
		return;
	}

	if (!SV_TestEntityPosition (ent))
	{
		VectorCopy (ent->v.origin, ent->v.oldorigin);
		return;
	}

	if (!SV_EntityPositionBlockedIgnoringPusher (ent, move_frame->pusher))
	{
		VectorCopy (ent->v.origin, ent->v.oldorigin);
		return;
	}

	SV_CheckStuck (ent);
}

/*
=============
SV_CheckWater
=============
*/
static qboolean SV_CheckWater (edict_t *ent)
{
	vec3_t point;
	int	   cont;

	point[0] = ent->v.origin[0];
	point[1] = ent->v.origin[1];
	point[2] = ent->v.origin[2] + ent->v.mins[2] + 1;

	ent->v.waterlevel = 0;
	ent->v.watertype = CONTENTS_EMPTY;
	cont = SV_PointContents (point);
	if (cont <= CONTENTS_WATER)
	{
		ent->v.watertype = cont;
		ent->v.waterlevel = 1;
		point[2] = ent->v.origin[2] + (ent->v.mins[2] + ent->v.maxs[2]) * 0.5;
		cont = SV_PointContents (point);
		if (cont <= CONTENTS_WATER)
		{
			ent->v.waterlevel = 2;
			point[2] = ent->v.origin[2] + ent->v.view_ofs[2];
			cont = SV_PointContents (point);
			if (cont <= CONTENTS_WATER)
				ent->v.waterlevel = 3;
		}
	}

	return ent->v.waterlevel > 1;
}

/*
============
SV_WallFriction

============
*/
static void SV_WallFriction (edict_t *ent, trace_t *trace)
{
	vec3_t forward, right, up;
	float  d, i;
	vec3_t into, side;

	AngleVectors (ent->v.v_angle, forward, right, up);
	d = DotProduct (trace->plane.normal, forward);

	d += 0.5;
	if (d >= 0)
		return;

	// cut the tangential velocity
	i = DotProduct (trace->plane.normal, ent->v.velocity);
	VectorScale (trace->plane.normal, i, into);
	VectorSubtract (ent->v.velocity, into, side);

	ent->v.velocity[0] = side[0] * (1 + d);
	ent->v.velocity[1] = side[1] * (1 + d);
}

/*
=====================
SV_TryUnstick

Player has come to a dead stop, possibly due to the problem with limited
float precision at some angle joins in the BSP hull.

Try fixing by pushing one pixel in each direction.

This is a hack, but in the interest of good gameplay...
======================
*/
static int SV_TryUnstick (edict_t *ent, vec3_t oldvel, qboolean callbacks)
{
	int		i;
	vec3_t	oldorg;
	vec3_t	dir, dest;
	int		clip;
	trace_t steptrace;

	VectorCopy (ent->v.origin, oldorg);
	VectorCopy (vec3_origin, dir);

	for (i = 0; i < 8; i++)
	{
		// try pushing a little in an axial direction
		switch (i)
		{
		case 0:
			dir[0] = 2;
			dir[1] = 0;
			break;
		case 1:
			dir[0] = 0;
			dir[1] = 2;
			break;
		case 2:
			dir[0] = -2;
			dir[1] = 0;
			break;
		case 3:
			dir[0] = 0;
			dir[1] = -2;
			break;
		case 4:
			dir[0] = 2;
			dir[1] = 2;
			break;
		case 5:
			dir[0] = -2;
			dir[1] = 2;
			break;
		case 6:
			dir[0] = 2;
			dir[1] = -2;
			break;
		case 7:
			dir[0] = -2;
			dir[1] = -2;
			break;
		}

		VectorAdd (ent->v.origin, dir, dest);
		SV_PushEntityTo (ent, dest, callbacks);

		// retry the original move
		ent->v.velocity[0] = oldvel[0];
		ent->v.velocity[1] = oldvel[1];
		ent->v.velocity[2] = 0;
		clip = SV_FlyMove (ent, 0.1, NULL, &steptrace, callbacks);

		if (fabs (oldorg[1] - ent->v.origin[1]) > 4 || fabs (oldorg[0] - ent->v.origin[0]) > 4)
		{
			// Con_DPrintf ("unstuck!\n");
			return clip;
		}

		// go back to the original pos and try again
		VectorCopy (oldorg, ent->v.origin);
	}

	VectorCopy (vec3_origin, ent->v.velocity);
	return 7; // still not moving
}

/*
=====================
SV_WalkMove

Only used by players
======================
*/
static void SV_WalkMove (edict_t *ent, const sv_client_move_frame_t *move_frame, const vec3_t move_velocity, qboolean callbacks)
{
	vec3_t	upmove, downmove;
	vec3_t	oldorg, oldvel;
	vec3_t	nosteporg, nostepvel;
	int		clip;
	int		oldonground;
	trace_t steptrace, downtrace;

	//
	// do a regular slide move unless it looks like you ran into a step
	//
	oldonground = (int)ent->v.flags & FL_ONGROUND;
	ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;

	VectorCopy (ent->v.origin, oldorg);
	VectorCopy (ent->v.velocity, oldvel);

	clip = SV_FlyMoveWithMoveFrameClipContext (ent, host_frametime, move_frame, move_velocity, &steptrace, callbacks);

	if (!(clip & 2))
	{
		SV_GroundClientOnMoveFramePusher (ent, move_frame);
		return; // move didn't block on a step
	}

	if (!oldonground && ent->v.waterlevel == 0)
		return; // don't stair up while jumping

	if (ent->v.movetype != MOVETYPE_WALK)
		return; // gibbed by a trigger

	if (sv_nostep.value)
		return;

	if ((int)ent->v.flags & FL_WATERJUMP)
		return;

	VectorCopy (ent->v.origin, nosteporg);
	VectorCopy (ent->v.velocity, nostepvel);

	//
	// try moving up and forward to go up a step
	//
	VectorCopy (oldorg, ent->v.origin); // back to start pos

	VectorCopy (ent->v.origin, upmove);
	upmove[2] += STEPSIZE;

	// move up
	SV_PushEntityTo (ent, upmove, callbacks); // FIXME: don't link?

	// move forward
	ent->v.velocity[0] = oldvel[0];
	ent->v.velocity[1] = oldvel[1];
	ent->v.velocity[2] = 0;
	clip = SV_FlyMoveWithMoveFrameClipContext (ent, host_frametime, move_frame, NULL, &steptrace, callbacks);

	// check for stuckness, possibly due to the limited precision of floats
	// in the clipping hulls. Disable when using pr_checkextension to avoid
	// https://github.com/Shpoike/Quakespasm/issues/50.
	if (clip && !pr_checkextension.value)
	{
		if (fabs (oldorg[1] - ent->v.origin[1]) < 0.03125 && fabs (oldorg[0] - ent->v.origin[0]) < 0.03125)
		{ // stepping up didn't make any progress
			clip = SV_TryUnstick (ent, oldvel, callbacks);
		}
	}

	// extra friction based on view angle
	if (clip & 2)
		SV_WallFriction (ent, &steptrace);

	// move down
	VectorCopy (ent->v.origin, downmove);
	downmove[2] += -STEPSIZE + move_velocity[2] * host_frametime;
	downtrace = SV_PushEntityTo (ent, downmove, callbacks); // FIXME: don't link?

	if (downtrace.plane.normal[2] > MIN_WALK_NORMAL)
	{
		if (ent->v.solid == SOLID_BSP || (SV_ClientMoveFrameHasGroundSupport (move_frame) && downtrace.ent == move_frame->pusher))
		{
			ent->v.flags = (int)ent->v.flags | FL_ONGROUND;

			// Native pushes can touch triggers and free downtrace.ent.
			if (downtrace.ent && !downtrace.ent->free)
				ent->v.groundentity = EDICT_TO_PROG (downtrace.ent);
		}
	}
	else
	{
		// if the push down didn't end up on good ground, use the move without
		// the step up.  This happens near wall / slope combinations, and can
		// cause the player to hop up higher on a slope too steep to climb
		VectorCopy (nosteporg, ent->v.origin);
		VectorCopy (nostepvel, ent->v.velocity);
		SV_GroundClientOnMoveFramePusher (ent, move_frame);
	}
}

/*
================
SV_Physics_Client

Player character actions
================
*/
static void SV_Physics_ClientWalk (edict_t *ent, sv_client_move_frame_t *move_frame)
{
	vec3_t	 move_velocity, old_velocity;
	qboolean supported_by_pusher;
	qboolean in_water;
	qboolean apply_gravity;

	supported_by_pusher = SV_ClientMoveFrameHasGroundSupport (move_frame);
	in_water = SV_CheckWater (ent);
	apply_gravity = !supported_by_pusher && !in_water && !((int)ent->v.flags & FL_WATERJUMP);

	if (apply_gravity)
		SV_AddGravity (ent, move_velocity);
	else
	{
		if (supported_by_pusher)
			ent->v.velocity[2] = 0;
		VectorCopy (ent->v.velocity, move_velocity);
	}

	VectorCopy (ent->v.velocity, old_velocity);
	SV_CheckStuckWithMoveFrame (ent, move_frame);
	assert_always (!ent->free);
	// Unsticking can touch a trigger that replaces the velocity.
	if (!VectorCompare (ent->v.velocity, old_velocity))
		VectorCopy (ent->v.velocity, move_velocity);
	SV_WalkMove (ent, move_frame, move_velocity, true);
}

/* Physical head movement is an auxiliary translation, not a second player
 * think. Use the same collision/step solver, but let the normal physics pass
 * own velocity, ground state and QuakeC callbacks. */
static void SV_ApplyPrivateRoomScaleMove (edict_t *ent, client_t *client)
{
	sv_client_move_frame_t auxiliary_frame;
	vec3_t move, sweep_velocity, saved_velocity;
	float saved_flags;
	int saved_groundentity;

	VectorCopy (client->cmd.vr_roomscalemove, move);
	VectorCopy (vec3_origin, client->cmd.vr_roomscalemove);
	if (!client->cmd.vr_active || ent->v.movetype == MOVETYPE_NONE ||
		host_frametime <= 0)
		return;
	move[2] = 0; // head height does not raise the player's collision hull
	if (!move[0] && !move[1])
		return;

	VectorCopy (ent->v.velocity, saved_velocity);
	saved_flags = ent->v.flags;
	saved_groundentity = ent->v.groundentity;
	VectorScale (move, 1.0f / host_frametime, sweep_velocity);
	VectorCopy (sweep_velocity, ent->v.velocity);
	if (ent->v.movetype == MOVETYPE_NOCLIP)
		VectorAdd (ent->v.origin, move, ent->v.origin);
	else if (ent->v.movetype == MOVETYPE_WALK)
	{
		SV_CaptureClientMoveFrameBeforeQC (ent, &auxiliary_frame);
		SV_WalkMove (ent, &auxiliary_frame, sweep_velocity, false);
	}
	else
		SV_FlyMove (ent, host_frametime, NULL, NULL, false);
	VectorCopy (saved_velocity, ent->v.velocity);
	ent->v.flags = saved_flags;
	ent->v.groundentity = saved_groundentity;
	SV_LinkEdict (ent, false);
}

typedef struct sv_vr_weapon_pose_scope_s
{
	struct sv_vr_weapon_pose_scope_s *previous;
	edict_t *ent;
	qboolean applied, origin_relocated, linked;
	vec3_t origin, v_angle, forward, right, up;
} sv_vr_weapon_pose_scope_t;

static sv_vr_weapon_pose_scope_t *sv_vr_weapon_pose_scope;

void SV_ClearVRWeaponPoseScope (void)
{
	/* Host_Error/EndGame can unwind a QC callback past its normal restore. */
	sv_vr_weapon_pose_scope = NULL;
}

void SV_VRWeaponPoseSetOrigin (edict_t *ent)
{
	sv_vr_weapon_pose_scope_t *scope;
	if (qcvm != &sv.qcvm)
		return;
	for (scope = sv_vr_weapon_pose_scope; scope; scope = scope->previous)
		if (scope->ent == ent)
			scope->origin_relocated = true;
}

void SV_VRWeaponPoseLinked (edict_t *ent)
{
	sv_vr_weapon_pose_scope_t *scope;
	for (scope = sv_vr_weapon_pose_scope; scope; scope = scope->previous)
		if (scope->ent == ent)
			scope->linked = true;
}

static void SV_ClampVRMuzzleToWorld (edict_t *ent, vec3_t muzzle)
{
	vec3_t start, delta;
	trace_t trace;
	int i;
	VectorAdd (ent->v.origin, ent->v.view_ofs, start);
	for (i = 0; i < 3; i++)
		if (!isfinite (muzzle[i]))
		{
			VectorCopy (start, muzzle);
			return;
		}
	VectorSubtract (muzzle, start, delta);
	if (VectorLength (delta) > 512.0f)
	{
		VectorCopy (start, muzzle);
		return;
	}
	trace = SV_Move (start, vec3_origin, vec3_origin, muzzle,
		MOVE_NOMONSTERS, ent);
	if (trace.startsolid || trace.allsolid)
		VectorCopy (start, muzzle);
	else if (trace.fraction < 1.0f)
	{
		VectorCopy (trace.endpos, muzzle);
		if (VectorNormalize (delta) > 0.0f)
			VectorMA (muzzle, -1.0f, delta, muzzle);
	}
}

static void SV_BeginPrivateVRWeaponPose (edict_t *ent, client_t *client,
	sv_vr_weapon_pose_scope_t *scope)
{
	vec3_t muzzle, source_offset;
	const usercmd_t *cmd = &client->cmd;
	memset (scope, 0, sizeof (*scope));
	if (client->protocol_qsvr != QSVR_PROTOCOL_PINNED ||
		!cmd->vr_active || !cmd->vr_handpos_relative ||
		client->lastmovetime <= 0 || realtime - client->lastmovetime > 1.0)
		return;

	scope->applied = true;
	scope->ent = ent;
	scope->previous = sv_vr_weapon_pose_scope;
	sv_vr_weapon_pose_scope = scope;
	VectorCopy (ent->v.origin, scope->origin);
	VectorCopy (ent->v.v_angle, scope->v_angle);
	VectorCopy (pr_global_struct->v_forward, scope->forward);
	VectorCopy (pr_global_struct->v_right, scope->right);
	VectorCopy (pr_global_struct->v_up, scope->up);

	VectorAdd (scope->origin, cmd->vr_handpos, muzzle);
	VectorCopy (cmd->vr_handrot, ent->v.v_angle);
	/* QC roll is camera tilt, while wrist roll belongs to the weapon model. */
	ent->v.v_angle[ROLL] = 0;
	AngleVectors (ent->v.v_angle, pr_global_struct->v_forward,
		pr_global_struct->v_right, pr_global_struct->v_up);
	SV_ClampVRMuzzleToWorld (ent, muzzle);
	VR_WeaponCalibrationProjectileSourceOffset (
		PR_GetString (ent->v.weaponmodel), (int)ent->v.weapon,
		ent->v.v_angle, ent->v.view_ofs[2], source_offset);
	VectorSubtract (muzzle, source_offset, ent->v.origin);
}

static void SV_EndPrivateVRWeaponPose (edict_t *ent,
	const sv_vr_weapon_pose_scope_t *scope)
{
	if (!scope->applied)
		return;
	sv_vr_weapon_pose_scope = scope->previous;
	if (!ent->free)
	{
		if (!scope->origin_relocated)
		{
			VectorCopy (scope->origin, ent->v.origin);
			/* A QC size/model/link operation may have indexed the hand origin. */
			if (scope->linked)
				SV_LinkEdict (ent, false);
		}
		VectorCopy (scope->v_angle, ent->v.v_angle);
	}
	VectorCopy (scope->forward, pr_global_struct->v_forward);
	VectorCopy (scope->right, pr_global_struct->v_right);
	VectorCopy (scope->up, pr_global_struct->v_up);
}

static qboolean SV_RunPrivateVRWeaponThink (edict_t *ent, client_t *client)
{
	sv_vr_weapon_pose_scope_t scope;
	qboolean alive;
	if (ent->v.nextthink <= 0 || ent->v.nextthink > qcvm->time + host_frametime)
		return true;
	SV_BeginPrivateVRWeaponPose (ent, client, &scope);
	alive = SV_RunThink (ent);
	SV_EndPrivateVRWeaponPose (ent, &scope);
	return alive;
}

/* The private trial is deliberately narrower than the ordinary client owner:
 * stock hull, dry WALK, and no pusher/Gorilla authority. */
static qboolean SV_PrivateWalkTrialStockHull (edict_t *ent)
{
	vec3_t mins = {-16, -16, -24};
	vec3_t maxs = {16, 16, 32};
	return (int)ent->v.solid == SOLID_SLIDEBOX &&
		VectorCompare (ent->v.mins, mins) && VectorCompare (ent->v.maxs, maxs);
}

static const char *SV_PrivateWalkTrialStateError (edict_t *ent, client_t *client,
	const usercmd_t *cmd)
{
	eval_t *customphysics;
	int groundprog, groundnum;
	edict_t *ground;

	if (!client->active || !client->spawned || client->edict != ent || ent->free)
		return "client owner is no longer live";
	if (sv.paused)
		return "server paused";
	if (client->protocol_qsvr != QSVR_PROTOCOL_PINNED ||
		!SV_PrivateWalkTrialSelected (client))
		return "private profile selection changed";
	if ((int)ent->v.movetype != MOVETYPE_WALK ||
		!SV_PrivateWalkTrialStockHull (ent))
		return "owner left stock WALK hull";
	if (cmd && (cmd->vr_gorilla.flags || cmd->vr_gorilla_motion.flags))
		return "Gorilla input is outside the trial";
	if ((int)ent->v.flags & FL_WATERJUMP)
		return "waterjump state is outside the dry trial";
	SV_CheckWater (ent);
	if (ent->v.waterlevel != 0)
		return "owner is not dry";

	customphysics = GetEdictFieldValue (ent, qcvm->extfields.customphysics);
	if (customphysics && customphysics->function)
		return "customphysics became active";

	groundprog = (int)ent->v.groundentity;
	if (qcvm->edict_size <= 0 || groundprog < 0 || (groundprog &&
		(groundprog % qcvm->edict_size || groundprog / qcvm->edict_size >= qcvm->num_edicts)))
		return "invalid ground entity";
	if (groundprog)
	{
		groundnum = groundprog / qcvm->edict_size;
		ground = EDICT_NUM (groundnum);
		if (ground->free)
			return "stale ground entity";
		if (ground->v.movetype == MOVETYPE_PUSH && ground->v.solid == SOLID_BSP)
			return "owner is riding a pusher";
	}
	return NULL;
}

static qboolean SV_PrivateWalkTrialBuildBounds (edict_t *ent,
	const movevars_t *vars, float seconds, vec3_t bounds[2])
{
	float speed, reach, acceleration;
	int i;

	if (!isfinite (seconds) || seconds <= 0 || seconds > 0.1251f)
		return false;
	speed = 0;
	for (i = 0; i < 3; i++)
	{
		if (!isfinite (ent->v.origin[i]) || !isfinite (ent->v.velocity[i]) ||
			!isfinite (ent->v.mins[i]) || !isfinite (ent->v.maxs[i]))
			return false;
		speed = fmaxf (speed, fabsf (ent->v.velocity[i]));
	}
	acceleration = fmaxf (fabsf (vars->accelerate), fabsf (vars->airaccelerate)) *
		fabsf (vars->maxspeed);
	reach = (speed + fabsf (vars->maxspeed)) * seconds +
		0.5f * (fabsf (vars->gravity * vars->entgravity) + acceleration) * seconds * seconds +
	fabsf (vars->jumpspeed) * seconds + (float)vars->stepheight + 16.0f;
	if (!isfinite (reach))
		return false;
	for (i = 0; i < 3; i++)
	{
		bounds[0][i] = ent->v.origin[i] + ent->v.mins[i] - reach;
		bounds[1][i] = ent->v.origin[i] + ent->v.maxs[i] + reach;
		if (!isfinite (bounds[0][i]) || !isfinite (bounds[1][i]))
			return false;
	}
	return true;
}

static qboolean SV_PrivateWalkTrialCollect (edict_t *ent,
	const movevars_t *vars, float seconds, vec3_t bounds[2])
{
	int i;

	if (!SV_PrivateWalkTrialBuildBounds (ent, vars, seconds, bounds) ||
		!SV_CollectPMovePhysents (ent, bounds))
		return false;
	/* The bounds intentionally cover the maximum reachable command sweep, not
	 * just current overlap. A pusher anywhere in that envelope stays under the
	 * legacy owner until moving-pusher contact parity is qualified. */
	for (i = 1; i < pmove.numphysent; i++)
	{
		int number = pmove.physents[i].info;
		edict_t *other;
		if (number <= 0 || number >= qcvm->num_edicts)
			return false;
		other = EDICT_NUM (number);
		if (other->free)
			return false;
	}
	return true;
}

static void SV_PrivateWalkTrialDrop (client_t *client, const char *reason)
{
	Sys_Printf ("%s: private WALK trial disconnected: %s\n",
		client->name[0] ? client->name : "client", reason);
	if (client->active)
		SV_DropClient (false);
}

/* This owner runs only for explicitly selected private peers. Queue retirement
 * remains in SV_FinishPrivateUsercmds, after this function reports completion. */
static qboolean SV_Physics_ClientPrivateWalkTrial (edict_t *ent, client_t *client,
	unsigned queue_offset, qboolean accrue_credit)
{
	playermove_t saved_pmove = pmove;
	movevars_t saved_movevars = movevars, trial_movevars;
	client_t *saved_host_client = host_client;
	edict_t *saved_sv_player = sv_player;
	usercmd_t command, *queued = NULL;
	double saved_host_frametime = host_frametime;
	float saved_qc_frametime = pr_global_struct->frametime;
	vec3_t bounds[2], prethink_velocity;
	float seconds, prethink_flags, prethink_teleport_time;
	int prethink_groundentity;
	qboolean run_command = false, was_grounded = false, weapon_alive;
	qboolean command_completed = false;
	const char *failure = NULL;
	float result_jump_secs = 0;
	int i;

	ED_Retain (ent);
	host_client = client;
	sv_player = ent;
	if (!client->private_pmove_last_cmd_valid)
		client->private_pmove_jump_secs = 0.0f;

	if (!isfinite (client->private_pmove_credit_msec) ||
		client->private_pmove_credit_msec < 0.0)
	{
		failure = "invalid command-time credit";
		goto cleanup;
	}
	if (!isfinite (host_frametime) || host_frametime < 0.0)
	{
		failure = "invalid host frame time";
		goto cleanup;
	}
	if (accrue_credit && !sv.paused)
		client->private_pmove_credit_msec = fmin (250.0,
			client->private_pmove_credit_msec + host_frametime * 1000.0);
	if (client->private_pmove_credit_msec > 250.0)
		client->private_pmove_credit_msec = 250.0;

	if (client->private_cmd_queue_count > SV_PRIVATE_CMD_QUEUE_SIZE ||
		client->private_cmd_queue_head >= SV_PRIVATE_CMD_QUEUE_SIZE ||
		client->private_cmd_queue_msec > SV_PRIVATE_CMD_QUEUE_MAX_MSEC)
	{
		failure = "invalid accepted-command queue state";
		goto cleanup;
	}
	if (queue_offset < client->private_cmd_queue_count)
		queued = &client->private_cmd_queue[(client->private_cmd_queue_head +
			queue_offset) % SV_PRIVATE_CMD_QUEUE_SIZE];
	if (queued && (queued->msec < 1 || queued->msec > 125 ||
		(int)queued->sequence <= client->private_completed_move))
	{
		failure = "invalid accepted queue head";
		goto cleanup;
	}
	run_command = queued && client->private_pmove_credit_msec >= queued->msec;
	seconds = queued ? queued->msec * 0.001f : 0.125f;
	if (queued)
		command = *queued;
	else if (client->private_pmove_last_cmd_valid)
		command = client->private_pmove_last_cmd;
	else
	{
		memset (&command, 0, sizeof (command));
		VectorCopy (ent->v.v_angle, command.viewangles);
	}
	if ((failure = SV_PrivateWalkTrialStateError (ent, client, &command)) != NULL)
		goto cleanup;
	if (run_command && (!PMSV_BuildMoveVars (&trial_movevars, ent, sv.protocolflags) ||
		!SV_PrivateWalkTrialCollect (ent, &trial_movevars, seconds, bounds)))
	{
		failure = "PMove preflight failed";
		goto cleanup;
	}

	if (!run_command)
	{
		/* Maintenance runs only before any command has completed this frame. */
		if (queue_offset != 0)
			goto cleanup;
		/* Keep only the last completed levels and pose during a zero-time QC
		 * maintenance pass. An uncompleted queue head never leaks into callbacks. */
		if (client->private_pmove_last_cmd_valid)
			command = client->private_pmove_last_cmd;
		else
		{
			memset (&command, 0, sizeof (command));
			VectorCopy (ent->v.v_angle, command.viewangles);
		}
		command.impulse = 0;
		command.seconds = 0;
		command.msec = 0;
		VectorClear (command.vr_roomscalemove);
		client->cmd = command;
		VectorCopy (command.viewangles, ent->v.v_angle);
		ent->v.button0 = (command.buttons & 1) != 0;
		ent->v.button2 = (command.buttons & 2) != 0;
		ent->v.impulse = 0;
		host_frametime = 0;
		pr_global_struct->frametime = 0;
		SV_ClientUpdateAnglesForClient (client);
		pr_global_struct->time = qcvm->time;
		pr_global_struct->self = EDICT_TO_PROG (ent);
		PR_ExecuteProgram (pr_global_struct->PlayerPreThink);
		if (!client->active || ent->free)
		{
			failure = "player removed during maintenance PreThink";
			goto cleanup;
		}
		if ((failure = SV_PrivateWalkTrialStateError (ent, client, &command)) != NULL)
			goto cleanup;
		/* Scheduled Think follows the host clock even without a move command. */
		host_frametime = saved_host_frametime;
		pr_global_struct->frametime = saved_qc_frametime;
		if (!SV_RunPrivateVRWeaponThink (ent, client))
		{
			failure = "player removed during maintenance weapon Think";
			goto cleanup;
		}
		host_frametime = 0;
		pr_global_struct->frametime = 0;
		if ((failure = SV_PrivateWalkTrialStateError (ent, client, &command)) != NULL)
			goto cleanup;
		SV_LinkEdict (ent, true);
		if (!client->active || ent->free)
		{
			failure = "player removed during maintenance trigger callbacks";
			goto cleanup;
		}
		pr_global_struct->time = qcvm->time;
		pr_global_struct->frametime = 0;
		pr_global_struct->self = EDICT_TO_PROG (ent);
		{
			sv_vr_weapon_pose_scope_t weapon_scope;
			SV_BeginPrivateVRWeaponPose (ent, client, &weapon_scope);
			PR_ExecuteProgram (pr_global_struct->PlayerPostThink);
			SV_EndPrivateVRWeaponPose (ent, &weapon_scope);
		}
		if (!client->active || ent->free)
		{
			failure = "player removed during maintenance PostThink";
			goto cleanup;
		}
		ent->v.impulse = 0;
		client->cmd.impulse = 0;
		goto cleanup;
	}

	/* A complete head is staged exactly once; room-scale is consumed before QC
	 * and the shared PMove command below cannot apply it a second time. */
	command.seconds = seconds;
	client->cmd = command;
	VectorCopy (command.viewangles, ent->v.v_angle);
	ent->v.button0 = (command.buttons & 1) != 0;
	ent->v.button2 = (command.buttons & 2) != 0;
	ent->v.impulse = command.impulse;
	host_frametime = seconds;
	pr_global_struct->frametime = seconds;
	SV_ClientUpdateAnglesForClient (client);
	SV_ApplyPrivateRoomScaleMove (ent, client);

	VectorCopy (ent->v.velocity, prethink_velocity);
	prethink_flags = ent->v.flags;
	prethink_groundentity = (int)ent->v.groundentity;
	prethink_teleport_time = ent->v.teleport_time;
	was_grounded = ((int)prethink_flags & FL_ONGROUND) != 0;
	pr_global_struct->time = qcvm->time;
	pr_global_struct->frametime = seconds;
	pr_global_struct->self = EDICT_TO_PROG (ent);
	PR_ExecuteProgram (pr_global_struct->PlayerPreThink);
	if (!client->active || ent->free)
	{
		failure = "player removed during PreThink";
		goto cleanup;
	}
	/* Stock QC owns jump sounds and flags; PMove owns the actual dry jump
	 * impulse. Preserve a teleporter's deliberate pause at zero velocity. */
	if (was_grounded && (command.buttons & 2) && ent->v.teleport_time <= qcvm->time &&
		(prethink_teleport_time <= qcvm->time || ent->v.teleport_time == prethink_teleport_time) &&
		!(VectorCompare (ent->v.velocity, vec3_origin) &&
			!VectorCompare (prethink_velocity, vec3_origin)))
		VectorCopy (prethink_velocity, ent->v.velocity);
	SV_CheckVelocity (ent);
	if ((failure = SV_PrivateWalkTrialStateError (ent, client, &command)) != NULL)
		goto cleanup;
	/* Weapon Think is scheduled against the world frame, not the packet's
	 * duration; ordinary PMove below still consumes the complete command. */
	host_frametime = saved_host_frametime;
	pr_global_struct->frametime = saved_qc_frametime;
	weapon_alive = SV_RunPrivateVRWeaponThink (ent, client);
	host_frametime = seconds;
	pr_global_struct->frametime = seconds;
	if (!weapon_alive || !client->active || ent->free)
	{
		failure = "player removed during weapon Think";
		goto cleanup;
	}
	if ((failure = SV_PrivateWalkTrialStateError (ent, client, &command)) != NULL)
		goto cleanup;
	if (!PMSV_BuildMoveVars (&trial_movevars, ent, sv.protocolflags) ||
		!SV_PrivateWalkTrialCollect (ent, &trial_movevars, seconds, bounds))
	{
		failure = "post-QC physent collection failed";
		goto cleanup;
	}

	movevars = trial_movevars;
	pmove.pm_type = PM_NORMAL;
	pmove.cmd = client->cmd;
	pmove.cmd.seconds = seconds;
	pmove.cmd.msec = command.msec;
	pmove.cmd.impulse = command.impulse;
	VectorClear (pmove.cmd.vr_roomscalemove);
	VectorCopy (ent->v.origin, pmove.origin);
	VectorCopy (ent->v.velocity, pmove.velocity);
	VectorCopy (command.viewangles, pmove.angles);
	VectorSet (pmove.gravitydir, 0, 0, -1);
	VectorCopy (ent->v.mins, pmove.player_mins);
	VectorCopy (ent->v.maxs, pmove.player_maxs);
	/* QC may clear FL_JUMPRELEASED when it emits the stock jump sound. PMove
	 * must use the release state that existed before that PreThink callback. */
	pmove.jump_held = (((int)prethink_flags & FL_JUMPRELEASED) == 0);
	pmove.jump_secs = client->private_pmove_jump_secs;
	pmove.waterjumptime = 0;
	pmove.waterlevel = 0;
	pmove.watertype = CONTENTBIT_EMPTY;
	pmove.onladder = false;
	pmove.safeorigin_known = false;
	pmove.gorilla_allowed = false;
	pmove.gorilla_prepared = false;
	pmove.gorilla_authoring = false;
	memset (&pmove.gorilla, 0, sizeof (pmove.gorilla));
	pmove.numtouch = 0;
	pmove.onground = false;
	pmove.groundent = 0;
	if (was_grounded || ((int)ent->v.flags & FL_ONGROUND))
	{
		int groundprog = (int)ent->v.groundentity;
		if (!groundprog && was_grounded)
			groundprog = prethink_groundentity;
		if (!groundprog)
			pmove.onground = true; /* world */
		else if (groundprog > 0 && groundprog % qcvm->edict_size == 0)
		{
			int groundnum = groundprog / qcvm->edict_size;
			for (i = 0; i < pmove.numphysent; i++)
				if (pmove.physents[i].info == groundnum)
				{
					pmove.onground = true;
					pmove.groundent = i;
					break;
				}
		}
	}
	PM_PlayerMove (1.0f);
	result_jump_secs = pmove.jump_secs;
	if (pmove.onground && (pmove.groundent < 0 || pmove.groundent >= pmove.numphysent))
	{
		failure = "PMove returned an invalid ground entity";
		goto cleanup;
	}
	for (i = 0; i < pmove.numtouch; i++)
		if (pmove.touchindex[i] < 0 || pmove.touchindex[i] >= pmove.numphysent)
		{
			failure = "PMove returned an invalid solid touch";
			goto cleanup;
		}
	for (i = 0; i < pmove.numtouch; i++)
	{
		int number = pmove.physents[pmove.touchindex[i]].info;
		if (number > 0 && number < qcvm->num_edicts)
		{
			edict_t *other = EDICT_NUM (number);
			if (!other->free && other->v.movetype == MOVETYPE_PUSH &&
				other->v.solid == SOLID_BSP)
			{
				failure = "PMove contacted a moving pusher";
				goto cleanup;
			}
		}
	}

	VectorCopy (pmove.origin, ent->v.origin);
	VectorCopy (pmove.velocity, ent->v.velocity);
	if (pmove.onground)
	{
		ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
		ent->v.groundentity = EDICT_TO_PROG (EDICT_NUM (pmove.physents[pmove.groundent].info));
	}
	else
	{
		ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
		ent->v.groundentity = 0;
	}
	if (pmove.jump_held)
		ent->v.flags = (int)ent->v.flags & ~FL_JUMPRELEASED;
	else
		ent->v.flags = (int)ent->v.flags | FL_JUMPRELEASED;
	ent->v.waterlevel = pmove.waterlevel;
	ent->v.watertype = CONTENTS_EMPTY;
	if (pmove.watertype & CONTENTBIT_LAVA)
		ent->v.watertype = CONTENTS_LAVA;
	else if (pmove.watertype & CONTENTBIT_SLIME)
		ent->v.watertype = CONTENTS_SLIME;
	else if (pmove.watertype & CONTENTBIT_WATER)
		ent->v.watertype = CONTENTS_WATER;

	/* Link for QC contact queries, dispatch solid impacts, then let the normal
	 * trigger owner run once. SV_Impact callback velocity edits remain final. */
	SV_LinkEdict (ent, false);
	for (i = 0; i < pmove.numtouch && !ent->free; i++)
	{
		int number = pmove.physents[pmove.touchindex[i]].info;
		edict_t *other;
		if (number < 0 || number >= qcvm->num_edicts)
		{
			failure = "PMove touch entity is outside the server world";
			goto cleanup;
		}
		other = EDICT_NUM (number);
		if (!other->free && other != ent)
			SV_Impact (ent, other);
	}
	if (ent->free || !client->active)
	{
		failure = "player removed during solid impact callbacks";
		goto cleanup;
	}
	SV_LinkEdict (ent, true);
	if (!client->active || ent->free)
	{
		failure = "player removed during trigger callbacks";
		goto cleanup;
	}
	pr_global_struct->time = qcvm->time;
	pr_global_struct->frametime = seconds;
	pr_global_struct->self = EDICT_TO_PROG (ent);
	{
		sv_vr_weapon_pose_scope_t weapon_scope;
		SV_BeginPrivateVRWeaponPose (ent, client, &weapon_scope);
		PR_ExecuteProgram (pr_global_struct->PlayerPostThink);
		SV_EndPrivateVRWeaponPose (ent, &weapon_scope);
	}
	if (!client->active || ent->free)
	{
		failure = "player removed during PostThink";
		goto cleanup;
	}
	if ((failure = SV_PrivateWalkTrialStateError (ent, client, &command)) != NULL)
		goto cleanup;
	if (client->private_pmove_credit_msec < command.msec)
	{
		failure = "command-time credit changed during callbacks";
		goto cleanup;
	}
	client->private_completed_move = (int)command.sequence;
	client->private_pmove_last_cmd = command;
	client->private_pmove_last_cmd_valid = true;
	client->private_pmove_jump_secs = result_jump_secs;
	client->private_pmove_credit_msec -= command.msec;
	if (client->private_pmove_credit_msec < 0.000001)
		client->private_pmove_credit_msec = 0;
	client->cmd = command;
	client->cmd.impulse = 0;
	VectorClear (client->cmd.vr_roomscalemove);
	ent->v.impulse = 0;
	command_completed = true;

cleanup:
	if (failure)
		SV_PrivateWalkTrialDrop (client, failure);
	/* Impulses are one-shot even when maintenance has no accepted movement. */
	if (client->active)
		ent->v.impulse = 0;
	pmove = saved_pmove;
	movevars = saved_movevars;
	host_frametime = saved_host_frametime;
	pr_global_struct->frametime = saved_qc_frametime;
	host_client = saved_host_client;
	sv_player = saved_sv_player;
	ED_Release (ent);
	return command_completed;
}

static void SV_Physics_Client (edict_t *ent, int num)
{
	sv_client_move_frame_t move_frame;
	sv_vr_weapon_pose_scope_t weapon_scope;
	client_t *client = &svs.clients[num - 1];
	edict_t				  *retained_pusher;
	int completed_move;
	unsigned queue_offset;
	qboolean frame_completed = false;

	if (!svs.clients[num - 1].active)
		return; // unconnected slot

	if (!svs.clients[num - 1].knowntoqc && sv_gameplayfix_spawnbeforethinks.value)
		return; // don't spam prethinks before we called putclientinserver.

	if (SV_PrivateWalkTrialSelected (client))
	{
		/* Bound catch-up work while preserving each command's QC lifecycle. */
		for (queue_offset = 0; queue_offset < 8; queue_offset++)
		{
			if (!client->active || !SV_PrivateWalkTrialSelected (client) ||
				!SV_Physics_ClientPrivateWalkTrial (ent, client,
					queue_offset, queue_offset == 0))
				break;
		}
		return;
	}

	completed_move = client->lastmovemessage;
	ED_Retain (ent);
	if (svs.clients[num - 1].protocol_qsvr == QSVR_PROTOCOL_PINNED)
		SV_ApplyPrivateRoomScaleMove (ent, &svs.clients[num - 1]);
	SV_CaptureClientMoveFrameBeforeQC (ent, &move_frame);
	retained_pusher = move_frame.pusher;
	if (retained_pusher)
		ED_Retain (retained_pusher);

	//
	// call standard client pre-think
	//
	pr_global_struct->time = qcvm->time;
	pr_global_struct->self = EDICT_TO_PROG (ent);
	PR_ExecuteProgram (pr_global_struct->PlayerPreThink);

	assert_always (!ent->free);

	SV_UpdateClientMoveFrameAfterQC (ent, &move_frame);

	//
	// do a move
	//
	SV_CheckVelocity (ent);

	//
	// decide which move function to call
	//
	switch ((int)ent->v.movetype)
	{
	case MOVETYPE_NONE:
		if (!SV_RunPrivateVRWeaponThink (ent, client))
			goto done;
		break;

	case MOVETYPE_WALK:
		if (!SV_RunPrivateVRWeaponThink (ent, client))
			goto done;
		SV_Physics_ClientWalk (ent, &move_frame);
		break;

	case MOVETYPE_TOSS:
	case MOVETYPE_BOUNCE:
	case MOVETYPE_GIB:
		SV_Physics_Toss (ent);
		break;

	case MOVETYPE_FLY:
		if (!SV_RunPrivateVRWeaponThink (ent, client))
			goto done;
		SV_FlyMove (ent, host_frametime, NULL, NULL, true);
		break;

	case MOVETYPE_NOCLIP:
		if (!SV_RunPrivateVRWeaponThink (ent, client))
			goto done;
		VectorMA (ent->v.origin, host_frametime, ent->v.velocity, ent->v.origin);
		if (!SV_TestEntityPosition (ent))
			VectorCopy (ent->v.origin, ent->v.oldorigin);
		break;

	default:
		Host_EndGame ("SV_Physics_client: bad movetype %i", (int)ent->v.movetype);
	}

	//
	// call standard player post-think
	//
	SV_LinkEdict (ent, true);

	assert_always (!ent->free);

	pr_global_struct->time = qcvm->time;
	pr_global_struct->self = EDICT_TO_PROG (ent);
	SV_BeginPrivateVRWeaponPose (ent, client, &weapon_scope);
	PR_ExecuteProgram (pr_global_struct->PlayerPostThink);
	SV_EndPrivateVRWeaponPose (ent, &weapon_scope);
	frame_completed = true;

done:
	if (frame_completed && client->active && client->spawned && client->edict == ent && !ent->free &&
		client->protocol_qsvr == QSVR_PROTOCOL_PINNED)
		client->private_completed_move = completed_move;
	if (retained_pusher)
		ED_Release (retained_pusher);
	ED_Release (ent);
}

//============================================================================

/*
=============
SV_Physics_None

Non moving objects can only think
=============
*/
static void SV_Physics_None (edict_t *ent)
{
	// regular thinking
	SV_RunThink (ent);
}

/*
=============
SV_Physics_Noclip

A moving object that doesn't obey physics
=============
*/
static void SV_Physics_Noclip (edict_t *ent)
{
	// regular thinking
	if (!SV_RunThink (ent))
		return;

	// stationary: the move below would be an exact no-op, skip the relink (and its BSP leaf walk)
	if (!ent->v.velocity[0] && !ent->v.velocity[1] && !ent->v.velocity[2] && !ent->v.avelocity[0] && !ent->v.avelocity[1] && !ent->v.avelocity[2])
		return;

	VectorMA (ent->v.angles, host_frametime, ent->v.avelocity, ent->v.angles);
	VectorMA (ent->v.origin, host_frametime, ent->v.velocity, ent->v.origin);

	SV_LinkEdict (ent, false);
}

/*
==============================================================================

TOSS / BOUNCE

==============================================================================
*/

/*
=============
SV_CheckWaterTransition

=============
*/
void SV_CheckWaterTransition (edict_t *ent)
{
	int cont;

	cont = SV_PointContents (ent->v.origin);

	if (!ent->v.watertype)
	{ // just spawned here
		ent->v.watertype = cont;
		ent->v.waterlevel = 1;
		return;
	}

	if (cont <= CONTENTS_WATER)
	{
		if (ent->v.watertype == CONTENTS_EMPTY)
		{ // just crossed into water
			SV_StartSound (ent, NULL, 0, "misc/h2ohit1.wav", 255, 1);
		}
		ent->v.watertype = cont;
		ent->v.waterlevel = 1;
	}
	else
	{
		if (ent->v.watertype != CONTENTS_EMPTY)
		{ // just crossed into water
			SV_StartSound (ent, NULL, 0, "misc/h2ohit1.wav", 255, 1);
		}
		ent->v.watertype = CONTENTS_EMPTY;
		ent->v.waterlevel = cont;
	}
}

/*
=============
SV_Physics_Toss

Toss, bounce, and fly movement.  When onground, do nothing.
=============
*/
static void SV_Physics_Toss (edict_t *ent)
{
	trace_t trace;
	vec3_t	end, move_velocity;
	float	backoff;

	// regular thinking
	if (!SV_RunThink (ent))
		return;

	// if onground, return without moving
	if (((int)ent->v.flags & FL_ONGROUND))
		return;

	SV_CheckVelocity (ent);

	// add gravity
	if (ent->v.movetype != MOVETYPE_FLY && ent->v.movetype != MOVETYPE_FLYMISSILE)
		SV_AddGravity (ent, move_velocity);
	else
		VectorCopy (ent->v.velocity, move_velocity);

	// move angles
	VectorMA (ent->v.angles, host_frametime, ent->v.avelocity, ent->v.angles);

	// move origin
	VectorMA (ent->v.origin, host_frametime, move_velocity, end);
	trace = SV_PushEntityTo (ent, end, true);

	if (ent->free)
		return;

	if (trace.fraction == 1)
		return;

	if (ent->v.movetype == MOVETYPE_BOUNCE)
		backoff = 1.5;
	else
		backoff = 1;

	ClipVelocity (ent->v.velocity, trace.plane.normal, ent->v.velocity, backoff);

	// stop if on ground
	if (trace.plane.normal[2] > MIN_WALK_NORMAL)
	{
		if (ent->v.movetype != MOVETYPE_BOUNCE ||
			(sv_gameplayfix_bouncedownslopes.value ? DotProduct (trace.plane.normal, ent->v.velocity) : ent->v.velocity[2]) < 60)
		{
			ent->v.flags = (int)ent->v.flags | FL_ONGROUND;

			// SV_PushEntityTo() calls SV_LinkEdict (true) that could free trace.ent
			if (trace.ent && !trace.ent->free)
				ent->v.groundentity = EDICT_TO_PROG (trace.ent);

			VectorCopy (vec3_origin, ent->v.velocity);
			VectorCopy (vec3_origin, ent->v.avelocity);
		}
	}

	// check for in water
	SV_CheckWaterTransition (ent);
}

/*
===============================================================================

STEPPING MOVEMENT

===============================================================================
*/

/*
=============
SV_Physics_Step

Monsters freefall when they don't have a ground entity, otherwise
all movement is done with discrete steps.

This is also used for objects that have become still on the ground, but
will fall if the floor is pulled out from under them.
=============
*/
static void SV_Physics_Step (edict_t *ent)
{
	qboolean hitsound;
	vec3_t	 move_velocity;

	// freefall if not onground
	if (!((int)ent->v.flags & (FL_ONGROUND | FL_FLY | FL_SWIM)))
	{
		if (ent->v.velocity[2] < sv_gravity.value * -0.1)
			hitsound = true;
		else
			hitsound = false;

		SV_AddGravity (ent, move_velocity);
		SV_CheckVelocity (ent);
		// Bound the sweep as well as the stored end velocity after gravity.
		for (int i = 0; i < 3; i++)
		{
			if (IS_NAN (move_velocity[i]))
				move_velocity[i] = 0;
			if (move_velocity[i] > sv_maxvelocity.value)
				move_velocity[i] = sv_maxvelocity.value;
			else if (move_velocity[i] < -sv_maxvelocity.value)
				move_velocity[i] = -sv_maxvelocity.value;
		}
		SV_FlyMove (ent, host_frametime, move_velocity, NULL, true);
		SV_LinkEdict (ent, true);

		if (ent->free)
			return;

		if ((int)ent->v.flags & FL_ONGROUND) // just hit ground
		{
			if (hitsound)
				SV_StartSound (ent, NULL, 0, "demon/dland2.wav", 255, 1);
		}
	}

	// regular thinking
	if (SV_RunThink (ent))
		SV_CheckWaterTransition (ent);
}

//============================================================================

// track ED_Alloc during SV_Physics execution
static void SV_Physics_Alloc_Hook (edict_t *e)
{
	// Keep every cached slot attached to the same edict for the rest of this physics frame.
	pushable_ent_cache[num_pushable_ent_cache++] = e;
	ED_Retain (e);
}

/*
================
SV_Physics

================
*/
void SV_Physics (void)
{
	int		 i;
	int		 entity_cap; // For sv_freezenonclients
	edict_t *ent;

	ED_AllocHook_func previous_alloc_hook = NULL;

	int physics_mode;
	if (qcvm->extglobals.physics_mode)
		physics_mode = *qcvm->extglobals.physics_mode;
	else
		physics_mode = (qcvm == &cl.qcvm) ? 0 : 2; // csqc doesn't run thinks by default. it was meant to simplify implementations, but we just force fields to
												   // match ssqc so its not that large a burden.

	if (physics_mode)
		SV_BeginPusherSupportFrame ();

	if (!physics_mode)
	{
		qcvm->time += host_frametime;
		return;
	}
	else if (physics_mode == 1)
	{ // for dp compat. note that this violates MOVETYPE_PUSH.
		for (i = 0, ent = qcvm->edicts; i < qcvm->num_edicts; i++, ent = NEXT_EDICT (ent))
		{
			if (ent->free)
				continue;
			SV_RunThink (ent);
		}
		qcvm->time += host_frametime;
		return;
	}

	// let the progs know that a new frame has started
	if (pr_global_struct->StartFrame)
	{
		pr_global_struct->self = EDICT_TO_PROG (qcvm->edicts);
		pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
		pr_global_struct->time = qcvm->time;
		PR_ExecuteProgram (pr_global_struct->StartFrame);
	}

	// SV_CheckAllEnts ();

	//
	// treat each object in turn
	//
	ent = qcvm->edicts;

	if (sv_freezenonclients.value && qcvm == &sv.qcvm)
		entity_cap = svs.maxclients + 1; // Only run physics on clients and the world
	else
		entity_cap = qcvm->num_edicts;

	// QC can flip the cvars mid-tick, the whole tick must use one consistent decision
	const qboolean fast_pushers = (sv_fastpushmove.value > 0.f);
	sv_analyticphysics_frame = (sv_analyticphysics.value > 0.f);

	// fill the pushable entities cache and the spatial grid over it
	if (fast_pushers)
	{
		double build_start = 0;
		if (sv_speeds.value && qcvm == &sv.qcvm)
			build_start = Sys_DoubleTime ();

		num_pushable_ent_cache = 0;
		PushGrid_Clear ();
		// beware, we skip entity 0 here:
		edict_t *check = NEXT_EDICT (qcvm->edicts);
		for (int e = 1; e < qcvm->num_edicts; e++, check = NEXT_EDICT (check))
		{
			if (check->free)
				continue;
			if (!SV_IsPushable (check))
				continue;

			pushable_ent_cache[num_pushable_ent_cache++] = check;
			ED_Retain (check);
			PushGrid_Insert (check);
		}
		push_grid_tail_start = num_pushable_ent_cache;
		push_grid_qcvm = qcvm;
		push_grid_active = true;

		if (sv_speeds.value && qcvm == &sv.qcvm)
		{
			sv_speeds_build_ms += (Sys_DoubleTime () - build_start) * 1000.0;
			sv_speeds_pushables += num_pushable_ent_cache;
			sv_speeds_grid_entries += push_grid_num_entries;
		}

		previous_alloc_hook = ED_AllocSetHook (SV_Physics_Alloc_Hook);
	}

	// for (i=0 ; i<sv.num_edicts ; i++, ent = NEXT_EDICT(ent))
	for (i = 0; i < entity_cap; i++, ent = NEXT_EDICT (ent))
	{
		if (ent->free)
			continue;

		if (pr_global_struct->force_retouch)
		{
			SV_LinkEdict (ent, true); // force retouch even for stationary

			if (ent->free)
				continue;
		}

		// Release only support established by the private pusher record. QuakeC
		// is otherwise free to give FL_ONGROUND/groundentity custom semantics.
		SV_UpdatePersistentPusherSupport (ent);
		if (SV_MovetypeUsesGroundFlag (ent))
			SV_AdoptPusherSupport (ent);

		if (i > 0 && i <= svs.maxclients && qcvm == &sv.qcvm)
			SV_Physics_Client (ent, i);
		else if (ent->v.movetype == MOVETYPE_PUSH)
			SV_Physics_Pusher (ent);
		else if (ent->v.movetype == MOVETYPE_NONE)
			SV_Physics_None (ent);
		else if (ent->v.movetype == MOVETYPE_NOCLIP)
			SV_Physics_Noclip (ent);
		else if (ent->v.movetype == MOVETYPE_STEP)
			SV_Physics_Step (ent);
		else if (
			ent->v.movetype == MOVETYPE_TOSS || ent->v.movetype == MOVETYPE_GIB || ent->v.movetype == MOVETYPE_BOUNCE || ent->v.movetype == MOVETYPE_FLY ||
			ent->v.movetype == MOVETYPE_FLYMISSILE)
			SV_Physics_Toss (ent);
		else
			Host_EndGame ("SV_Physics: bad movetype %i", (int)ent->v.movetype);

		// johnfitz -- PROTOCOL_FITZQUAKE
		// capture interval to nextthink here and send it to client for better
		// lerp timing; ~0.1 intervals match what the client assumes but thinks
		// fire quantized to server ticks, so the exact value still improves
		// lerp timing where the extra bytes are affordable
		ent->sendinterval = false;
		ent->sendinterval_default = false;
		if (!ent->free && ent->v.nextthink > qcvm->time &&
			(ent->v.movetype == MOVETYPE_STEP || ent->v.movetype == MOVETYPE_WALK || ent->v.frame != ent->oldframe))
		{
			int j = Q_rint ((ent->v.nextthink - ent->oldthinktime) * 255);
			if (j == 25 || j == 26)
				ent->sendinterval_default = true;
			else if (j >= 0 && j < 256)
				ent->sendinterval = true;
		}
		// johnfitz
	}

	if (pr_global_struct->force_retouch)
		pr_global_struct->force_retouch--;

	if (!(sv_freezenonclients.value && qcvm == &sv.qcvm))
		qcvm->time += host_frametime;

	if (fast_pushers)
	{
		push_grid_active = false;
		ED_AllocSetHook (previous_alloc_hook);
		for (i = num_pushable_ent_cache - 1; i >= 0; i--)
			ED_Release (pushable_ent_cache[i]);
	}
}
