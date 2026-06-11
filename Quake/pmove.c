/*
Copyright (C) 1996-1997 Id Software, Inc.

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

#include "quakedef.h"
#include "vr.h"
#include "pmove.h"

movevars_t		movevars;
playermove_t	pmove;
extern cvar_t	pm_noround;	//evile.
extern cvar_t	sv_accelerate;
extern cvar_t	sv_edgefriction;
extern cvar_t	sv_friction;
extern cvar_t	sv_gravity;
extern cvar_t	sv_maxspeed;
extern cvar_t	sv_stopspeed;

static float		frametime;

static vec3_t		forward, right, up;
static int		pmove_trace_entnum;

static qboolean PM_TransformedHullCheck (qmodel_t *model, vec3_t start, vec3_t end, vec3_t mins, vec3_t maxs, trace_t *trace, vec3_t origin, vec3_t angles);
static	hull_t		box_hull;
static	mclipnode_t	box_clipnodes[6];
static	mplane_t	box_planes[6];
static qboolean	pm_initialized;
static movevars_t	clmovevars;
static qboolean	clmovevars_valid;

static cvar_t pm_bunnyspeedcap = {"pm_bunnyspeedcap", "", CVAR_SERVERINFO};
static cvar_t pm_bunnyfriction = {"pm_bunnyfriction", "1", CVAR_SERVERINFO};
static cvar_t pm_ktjump = {"pm_ktjump", "", CVAR_SERVERINFO};
static cvar_t pm_slidefix = {"pm_slidefix", "1", CVAR_SERVERINFO};
static cvar_t pm_airstep = {"pm_airstep", "", CVAR_SERVERINFO};
static cvar_t pm_pground = {"pm_pground", "", CVAR_SERVERINFO};
static cvar_t pm_stepdown = {"pm_stepdown", "", CVAR_SERVERINFO};
static cvar_t pm_walljump = {"pm_walljump", "", CVAR_SERVERINFO};
static cvar_t pm_slidyslopes = {"pm_slidyslopes", "", CVAR_SERVERINFO};
static cvar_t pm_autobunny = {"pm_autobunny", "", CVAR_SERVERINFO};
static cvar_t pm_watersinkspeed = {"pm_watersinkspeed", "", CVAR_SERVERINFO};
static cvar_t pm_flyfriction = {"pm_flyfriction", "", CVAR_SERVERINFO};
static cvar_t pm_edgefriction = {"pm_edgefriction", "2", CVAR_NONE};
static cvar_t pm_stepheight = {"pm_stepheight", "", CVAR_NONE};
static cvar_t sv_airaccelerate = {"sv_airaccelerate", "-1", CVAR_SERVERINFO};
static cvar_t sv_wateraccelerate = {"sv_wateraccelerate", "-1", CVAR_SERVERINFO};
static cvar_t sv_waterfriction = {"sv_waterfriction", "4", CVAR_SERVERINFO};
static cvar_t sv_spectatormaxspeed = {"sv_spectatormaxspeed", "500", CVAR_SERVERINFO};

// Axis vectors come from AngleVectors; this mirrors QSS-M's transform convention.
#define QAxisTransform(a, v, c) \
	do { \
		(c)[0] = DotProduct((a)[0], (v)); \
		(c)[1] = -DotProduct((a)[1], (v)); \
		(c)[2] = DotProduct((a)[2], (v)); \
	} while (0)

#define QAxisDeTransform(a, v, c) \
	do { \
		VectorScale((a)[0], (v)[0], (c)); \
		VectorMA((c), -(v)[1], (a)[1], (c)); \
		VectorMA((c), (v)[2], (a)[2], (c)); \
	} while (0)

void PM_Init (void)
{
	PM_InitBoxHull();
	pm_initialized = true;
}

static void PM_EnsureInitialized (void)
{
	if (!pm_initialized)
		PM_Init ();
}

static float PM_CvarOrDefault (const cvar_t *var, float fallback)
{
	return var->string[0] ? var->value : fallback;
}

static unsigned int PM_PackMoveFlags (const movevars_t *mv)
{
	unsigned int flags = mv->flags;
	int walljump = mv->walljump;

	if (walljump < 0)
		walljump = 0;
	if (walljump > 3)
		walljump = 3;

	if (mv->slidefix)
		flags |= MOVEFLAG_PM_SLIDEFIX;
	if (mv->airstep)
		flags |= MOVEFLAG_PM_AIRSTEP;
	if (mv->pground)
		flags |= MOVEFLAG_PM_PGROUND;
	if (mv->stepdown)
		flags |= MOVEFLAG_PM_STEPDOWN;
	if (mv->slidyslopes)
		flags |= MOVEFLAG_PM_SLIDYSLOPES;
	if (mv->autobunny)
		flags |= MOVEFLAG_PM_AUTOBUNNY;
	if (mv->bunnyfriction)
		flags |= MOVEFLAG_PM_BUNNYFRICTION;
	flags &= ~MOVEFLAG_PM_WALLJUMP_MASK;
	flags |= (unsigned int)walljump << MOVEFLAG_PM_WALLJUMP_SHIFT;
	return flags;
}

static void PM_UnpackMoveFlags (movevars_t *mv)
{
	mv->slidefix = !!(mv->flags & MOVEFLAG_PM_SLIDEFIX);
	mv->airstep = !!(mv->flags & MOVEFLAG_PM_AIRSTEP);
	mv->pground = !!(mv->flags & MOVEFLAG_PM_PGROUND);
	mv->stepdown = !!(mv->flags & MOVEFLAG_PM_STEPDOWN);
	mv->slidyslopes = !!(mv->flags & MOVEFLAG_PM_SLIDYSLOPES);
	mv->autobunny = !!(mv->flags & MOVEFLAG_PM_AUTOBUNNY);
	mv->bunnyfriction = !!(mv->flags & MOVEFLAG_PM_BUNNYFRICTION);
	mv->walljump = (mv->flags & MOVEFLAG_PM_WALLJUMP_MASK) >> MOVEFLAG_PM_WALLJUMP_SHIFT;
}

static void PM_SetBaseMoveVars (movevars_t *mv, unsigned int protocolflags, qboolean server_side)
{
	float jump_velocity;

	memset (mv, 0, sizeof(*mv));
	mv->gravity = sv_gravity.value;
	mv->stopspeed = sv_stopspeed.value;
	mv->maxspeed = sv_maxspeed.value;
	mv->spectatormaxspeed = sv_spectatormaxspeed.value;
	mv->maxairspeed = 30;
	mv->accelerate = sv_accelerate.value;
	mv->airaccelerate = (sv_airaccelerate.value < 0) ?
		sv_accelerate.value : sv_airaccelerate.value;
	mv->wateraccelerate = (sv_wateraccelerate.value < 0) ?
		sv_accelerate.value : sv_wateraccelerate.value;
	mv->friction = sv_friction.value;
	mv->waterfriction = sv_waterfriction.value;
	mv->flyfriction = PM_CvarOrDefault (&pm_flyfriction, sv_friction.value);
	mv->entgravity = 1.0f;
	mv->bunnyspeedcap = pm_bunnyspeedcap.value;
	mv->ktjump = pm_ktjump.value;
	mv->airstep = pm_airstep.value != 0;
	mv->stepheight = PM_CvarOrDefault (&pm_stepheight, 18);
	mv->stepdown = pm_stepdown.value != 0;
	mv->walljump = pm_walljump.value;
	mv->slidefix = pm_slidefix.value != 0;
	mv->pground = pm_pground.value != 0;
	mv->slidyslopes = pm_slidyslopes.value != 0;
	mv->autobunny = pm_autobunny.value != 0;
	mv->bunnyfriction = pm_bunnyfriction.value != 0;
	mv->watersinkspeed = PM_CvarOrDefault (&pm_watersinkspeed, 60);
	mv->edgefriction = PM_CvarOrDefault (&pm_edgefriction, sv_edgefriction.value);
	jump_velocity = (server_side || vr_enabled.value) && sv_vr_jump_velocity.value > 270 ?
		sv_vr_jump_velocity.value : 270;
	mv->jumpspeed = jump_velocity;
	mv->protocolflags = protocolflags;
	mv->flags = MOVEFLAG_VALID | MOVEFLAG_NOGRAVITYONGROUND |
		(pm_edgefriction.string[0] ? 0 : MOVEFLAG_QWEDGEBOX);
	mv->flags = PM_PackMoveFlags (mv);
}

void PM_InitBoxHull (void)
{
	int		i;
	int		side;

	box_hull.clipnodes = box_clipnodes;
	box_hull.planes = box_planes;
	box_hull.firstclipnode = 0;
	box_hull.lastclipnode = 5;

	for (i=0 ; i<6 ; i++)
	{
		box_clipnodes[i].planenum = i;

		side = i&1;

		box_clipnodes[i].children[side] = CONTENTS_EMPTY;
		if (i != 5)
			box_clipnodes[i].children[side^1] = i + 1;
		else
			box_clipnodes[i].children[side^1] = CONTENTS_SOLID;

		box_planes[i].type = i>>1;
		VectorClear(box_planes[i].normal);
		box_planes[i].normal[i>>1] = 1;
	}
}

static hull_t *PM_HullForBox (vec3_t mins, vec3_t maxs)
{
	box_planes[0].dist = maxs[0];
	box_planes[1].dist = mins[0];
	box_planes[2].dist = maxs[1];
	box_planes[3].dist = mins[1];
	box_planes[4].dist = maxs[2];
	box_planes[5].dist = mins[2];

	return &box_hull;
}

static unsigned int PM_ContentsMaskFromQ1 (int contents)
{
	if (contents <= CONTENTS_CURRENT_0 && contents >= CONTENTS_CURRENT_DOWN)
		contents = CONTENTS_WATER;
	if (contents >= 0)
		return 0;
	return CONTENTMASK_FROMQ1(contents);
}

static unsigned int PM_TransformedModelPointContents (qmodel_t *mod, vec3_t p, vec3_t origin, vec3_t angles)
{
	vec3_t p_l, axis[3], p_t;

	if (!mod || mod->type != mod_brush)
		return CONTENTBIT_EMPTY;

	VectorSubtract (p, origin, p_l);

	if (angles[0] || angles[1] || angles[2])
	{
		AngleVectors (angles, axis[0], axis[1], axis[2]);
		QAxisTransform(axis, p_l, p_t);
		return PM_ContentsMaskFromQ1(SV_HullPointContents(&mod->hulls[0], mod->hulls[0].firstclipnode, p_t));
	}

	return PM_ContentsMaskFromQ1(SV_HullPointContents(&mod->hulls[0], mod->hulls[0].firstclipnode, p_l));
}

int PM_PointContents (vec3_t p)
{
	int			num;
	unsigned int	pc;
	physent_t	*pe;
	qmodel_t	*pm;

	pm = pmove.physents[0].model;
	if (!pm || pm->needload)
		return CONTENTBIT_EMPTY;

	pc = PM_ContentsMaskFromQ1(SV_HullPointContents(&pm->hulls[0],
		pm->hulls[0].firstclipnode, p));

	for (num = 1; num < pmove.numphysent; num++)
	{
		pe = &pmove.physents[num];

		if (pe->info == pmove.skipent)
			continue;

		pm = pe->model;
		if (pm)
		{
			if (p[0] >= pe->origin[0]+pm->mins[0] && p[0] <= pe->origin[0]+pm->maxs[0] &&
				p[1] >= pe->origin[1]+pm->mins[1] && p[1] <= pe->origin[1]+pm->maxs[1] &&
				p[2] >= pe->origin[2]+pm->mins[2] && p[2] <= pe->origin[2]+pm->maxs[2])
			{
				if (pe->forcecontentsmask)
				{
					if (PM_TransformedModelPointContents(pm, p, pe->origin, pe->angles) != CONTENTBIT_EMPTY)
						pc |= pe->forcecontentsmask;
				}
				else
					pc |= PM_TransformedModelPointContents(pm, p, pe->origin, pe->angles);
			}
		}
		else if (pe->forcecontentsmask)
		{
			if (p[0] >= pe->origin[0]+pe->mins[0] && p[0] <= pe->origin[0]+pe->maxs[0] &&
				p[1] >= pe->origin[1]+pe->mins[1] && p[1] <= pe->origin[1]+pe->maxs[1] &&
				p[2] >= pe->origin[2]+pe->mins[2] && p[2] <= pe->origin[2]+pe->maxs[2])
				pc |= pe->forcecontentsmask;
		}
	}

	return pc;
}

int PM_ExtraBoxContents (vec3_t p)
{
	int			num;
	int			pc = 0;
	physent_t	*pe;
	qmodel_t	*pm;
	trace_t		tr;

	for (num = 1; num < pmove.numphysent; num++)
	{
		pe = &pmove.physents[num];
		pm = pe->model;
		if (pm)
		{
			if (pe->forcecontentsmask)
			{
				if (!PM_TransformedHullCheck(pm, p, p, pmove.player_mins, pmove.player_maxs, &tr, pe->origin, pe->angles))
					continue;
				if (tr.startsolid || tr.inwater)
					pc |= pe->forcecontentsmask;
			}
		}
		else if (pe->forcecontentsmask)
		{
			if (p[0]+pmove.player_maxs[0] >= pe->origin[0]+pe->mins[0] && p[0]+pmove.player_mins[0] <= pe->origin[0]+pe->maxs[0] &&
				p[1]+pmove.player_maxs[1] >= pe->origin[1]+pe->mins[1] && p[1]+pmove.player_mins[1] <= pe->origin[1]+pe->maxs[1] &&
				p[2]+pmove.player_maxs[2] >= pe->origin[2]+pe->mins[2] && p[2]+pmove.player_mins[2] <= pe->origin[2]+pe->maxs[2])
				pc |= pe->forcecontentsmask;
		}
	}

	return pc;
}

static qboolean PM_TransformedHullCheck (qmodel_t *model, vec3_t start, vec3_t end, vec3_t player_mins, vec3_t player_maxs, trace_t *trace, vec3_t origin, vec3_t angles)
{
	vec3_t		start_l, end_l;
	int		i;
	vec3_t		axis[3], start_t, end_t;
	hull_t		*hull;

	VectorSubtract (start, origin, start_l);
	VectorSubtract (end, origin, end_l);

	memset (trace, 0, sizeof(*trace));
	trace->fraction = 1;
	trace->allsolid = true;
	VectorCopy (end_l, trace->endpos);

	if (model && model->type == mod_brush)
	{
		hull = &model->hulls[(player_maxs[0]-player_mins[0] < 3) ? 0 : 1];
		if (angles[0] || angles[1] || angles[2])
		{
			AngleVectors (angles, axis[0], axis[1], axis[2]);
			QAxisTransform(axis, start_l, start_t);
			QAxisTransform(axis, end_l, end_t);
			VectorCopy (end_t, trace->endpos);
			SV_RecursiveHullCheck(hull, hull->firstclipnode, 0, 1, start_t, end_t, trace);
			VectorCopy(trace->plane.normal, end_t);
			QAxisDeTransform(axis, end_t, trace->plane.normal);
			VectorCopy(trace->endpos, end_t);
			QAxisDeTransform(axis, end_t, trace->endpos);
		}
		else
		{
			for (i = 0; i < 3; i++)
			{
				if (start_l[i]+player_mins[i] > model->maxs[i] && end_l[i]+player_mins[i] > model->maxs[i])
					return false;
				if (start_l[i]+player_maxs[i] < model->mins[i] && end_l[i]+player_maxs[i] < model->mins[i])
					return false;
			}

			SV_RecursiveHullCheck(hull, hull->firstclipnode, 0, 1, start_l, end_l, trace);
		}
	}
	else
	{
		for (i = 0; i < 3; i++)
		{
			if (start_l[i]+player_mins[i] > box_planes[0+i*2].dist && end_l[i]+player_mins[i] > box_planes[0+i*2].dist)
				return false;
			if (start_l[i]+player_maxs[i] < box_planes[1+i*2].dist && end_l[i]+player_maxs[i] < box_planes[1+i*2].dist)
				return false;
		}

		SV_RecursiveHullCheck(&box_hull, box_hull.firstclipnode, 0, 1, start_l, end_l, trace);
	}

	VectorAdd (trace->endpos, origin, trace->endpos);
	return true;
}

qboolean PM_TestPlayerPosition (vec3_t pos)
{
	int			i;
	physent_t	*pe;
	vec3_t		mins, maxs;
	hull_t		*hull;
	trace_t		trace;

	for (i=0 ; i<pmove.numphysent ; i++)
	{
		pe = &pmove.physents[i];

		if (pe->info == pmove.skipent)
			continue;

		if (pe->forcecontentsmask && !(pe->forcecontentsmask & MASK_PLAYERSOLID))
			continue;

		if (pe->model)
		{
			if (!PM_TransformedHullCheck(pe->model, pos, pos, pmove.player_mins, pmove.player_maxs, &trace, pe->origin, pe->angles))
				continue;
			if (trace.allsolid)
				return false;
		}
		else
		{
			VectorSubtract (pe->mins, pmove.player_maxs, mins);
			VectorSubtract (pe->maxs, pmove.player_mins, maxs);
			hull = PM_HullForBox (mins, maxs);
			VectorSubtract(pos, pe->origin, mins);

			if (PM_ContentsMaskFromQ1(SV_HullPointContents(hull, hull->firstclipnode, mins)) & MASK_PLAYERSOLID)
				return false;
		}
	}

	pmove.safeorigin_known = true;
	VectorCopy (pos, pmove.safeorigin);

	return true;
}

static int PM_LastTraceEntNum (void)
{
	return pmove_trace_entnum;
}

trace_t PM_PlayerTrace (vec3_t start, vec3_t end, unsigned int solidmask)
{
	trace_t		trace, total;
	int		i;
	int		total_entnum;
	physent_t	*pe;

	memset (&total, 0, sizeof(total));
	total.fraction = 1;
	total_entnum = -1;
	VectorCopy (end, total.endpos);

	for (i=0 ; i<pmove.numphysent ; i++)
	{
		pe = &pmove.physents[i];

		if (pe->info == pmove.skipent)
			continue;
		if (pe->forcecontentsmask && !(pe->forcecontentsmask & solidmask))
			continue;

		if (!pe->model || pe->model->needload)
		{
			vec3_t mins, maxs;

			VectorSubtract (pe->mins, pmove.player_maxs, mins);
			VectorSubtract (pe->maxs, pmove.player_mins, maxs);
			PM_HullForBox (mins, maxs);

			if (!PM_TransformedHullCheck(NULL, start, end, pmove.player_mins, pmove.player_maxs, &trace, pe->origin, pe->angles))
				continue;
		}
		else
		{
			if (!PM_TransformedHullCheck(pe->model, start, end, pmove.player_mins, pmove.player_maxs, &trace, pe->origin, pe->angles))
				continue;
		}

		if (trace.allsolid)
			trace.startsolid = true;

		if (trace.fraction < total.fraction || (trace.startsolid && !total.startsolid))
		{
			total = trace;
			total_entnum = i;
		}
	}

	if (total.startsolid)
		total.fraction = 0;
	pmove_trace_entnum = total_entnum;
	return total;
}

trace_t PM_TraceLine (vec3_t start, vec3_t end)
{
	VectorClear(pmove.player_mins);
	VectorClear(pmove.player_maxs);
	return PM_PlayerTrace(start, end, MASK_PLAYERSOLID);
}

#define	MIN_STEP_NORMAL	0.7		// roughly 45 degrees

#define	STOP_EPSILON	0.1
#define BLOCKED_FLOOR	1
#define BLOCKED_STEP	2
#define BLOCKED_OTHER	4
#define BLOCKED_ANY		7

/*
** Add an entity to touch list, discarding duplicates
*/
static void PM_AddTouchedEnt (int num)
{
	if (pmove.numtouch == MAX_PHYSENTS)
		return;

	if (pmove.numtouch)
		if (pmove.touchindex[pmove.numtouch - 1] == num)
			return; // already added

	pmove.touchindex[pmove.numtouch] = num;
	VectorCopy(pmove.velocity, pmove.touchvel[pmove.numtouch]);
	pmove.numtouch++;
}


/*
==================
PM_ClipVelocity

Slide off of the impacting object
==================
*/

void PM_ClipVelocity (vec3_t in, vec3_t normal, vec3_t out, float overbounce)
{
	float	backoff;
	float	change;
	int		i;

	backoff = DotProduct (in, normal) * overbounce;

	for (i=0 ; i<3 ; i++)
	{
		change = normal[i]*backoff;
		out[i] = in[i] - change;
		if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
			out[i] = 0;
	}
}

/*
============
PM_SlideMove

The basic solid body movement clip that slides along multiple planes
============
*/
#define	MAX_CLIP_PLANES	5

int PM_SlideMove (void)
{
	int			bumpcount, numbumps;
	vec3_t		dir;
	float		d;
	int			numplanes;
	vec3_t		planes[MAX_CLIP_PLANES];
	vec3_t		primal_velocity, original_velocity;
	int			i, j;
	trace_t		trace;
	vec3_t		end;
	float		time_left;
	int			blocked;
	vec3_t		start;

	numbumps = 4;

	blocked = 0;
	VectorCopy (pmove.velocity, original_velocity);
	VectorCopy (pmove.velocity, primal_velocity);
	numplanes = 0;

	time_left = frametime;

//	VectorAdd(pmove.velocity, pmove.basevelocity, pmove.velocity);

	for (bumpcount=0 ; bumpcount<numbumps ; bumpcount++)
	{
		for (i=0 ; i<3 ; i++)
			end[i] = pmove.origin[i] + time_left * pmove.velocity[i];

		VectorCopy(pmove.origin, start);
		trace = PM_PlayerTrace (start, end, MASK_PLAYERSOLID);

		if (trace.startsolid || trace.allsolid)
		{	// entity is trapped in another solid
			VectorClear (pmove.velocity);
			return 3;
		}

		if (trace.fraction > 0)
		{	// actually covered some distance
			VectorCopy (trace.endpos, pmove.origin);
			numplanes = 0;
		}

		if (trace.fraction == 1)
			 break;		// moved the entire distance

		// save entity for contact
		PM_AddTouchedEnt (PM_LastTraceEntNum());

		if (trace.plane.normal[2] >= MIN_STEP_NORMAL)
			blocked |= BLOCKED_FLOOR;
		else if (!trace.plane.normal[2])
			blocked |= BLOCKED_STEP;
		else
			blocked |= BLOCKED_OTHER;

		time_left -= time_left * trace.fraction;

	// cliped to another plane
		if (numplanes >= MAX_CLIP_PLANES)
		{	// this shouldn't really happen
			VectorClear (pmove.velocity);
			break;
		}

		VectorCopy (trace.plane.normal, planes[numplanes]);
		numplanes++;

//
// modify original_velocity so it parallels all of the clip planes
//
		for (i=0 ; i<numplanes ; i++)
		{
			if (movevars.walljump == 2)	//just bounce off!
			{	//pinball
				PM_ClipVelocity (original_velocity, planes[i], pmove.velocity, 2);
				return blocked;
			}
			//regular run at a wall and jump off
			if (movevars.walljump && planes[i][2] != 1	//not on floors
				&& Length(pmove.velocity)>200 && pmove.cmd.buttons & 2 && !pmove.jump_held && !pmove.waterjumptime)
			{
				PM_ClipVelocity (original_velocity, planes[i], pmove.velocity, 2);
				if (pmove.velocity[2] < movevars.jumpspeed)
					pmove.velocity[2] = movevars.jumpspeed;
				pmove.jump_secs = pmove.cmd.seconds;
				pmove.jump_held = true;
				pmove.waterjumptime = 0;
				return blocked;
			}
			PM_ClipVelocity (original_velocity, planes[i], pmove.velocity, 1);
			for (j=0 ; j<numplanes ; j++)
				if (j != i)
				{
					if (DotProduct (pmove.velocity, planes[j]) < 0)
						break;	// not ok
				}
			if (j == numplanes)
				break;
		}

		if (i != numplanes)
		{	// go along this plane
		}
		else
		{	// go along the crease
			if (numplanes != 2)
			{
				VectorClear (pmove.velocity);
				break;
			}
			CrossProduct (planes[0], planes[1], dir);
			d = DotProduct (dir, pmove.velocity);
			VectorScale (dir, d, pmove.velocity);
		}

//
// if velocity is against the original velocity, stop dead
// to avoid tiny occilations in sloping corners
//
		if (DotProduct (pmove.velocity, primal_velocity) <= 0)
		{
			VectorClear (pmove.velocity);
			break;
		}
	}

	if (pmove.waterjumptime)
	{
		VectorCopy (primal_velocity, pmove.velocity);
	}
	return blocked;
}

/*
=============
PM_StepSlideMove

Each intersection will try to step over the obstruction instead of
sliding along it.
=============
*/
int PM_StepSlideMove (qboolean in_air)
{
	vec3_t	dest;
	trace_t	trace;
	vec3_t	original, originalvel, down, up, downvel;
	float	downdist, updist;
	int		blocked;
	float	stepsize;

	// try sliding forward both on ground and up 16 pixels
	// take the move that goes farthest
	VectorCopy (pmove.origin, original);
	VectorCopy (pmove.velocity, originalvel);

	blocked = PM_SlideMove ();

	if (!blocked)
	{
		if (!in_air && movevars.stepdown)
		{	//if we were onground, try stepping down after the move to try to stay on said ground.
			VectorMA (pmove.origin, movevars.stepheight, pmove.gravitydir, dest);
			trace = PM_PlayerTrace (pmove.origin, dest, MASK_PLAYERSOLID);
			if (trace.fraction != 1 && -DotProduct(pmove.gravitydir, trace.plane.normal) > MIN_STEP_NORMAL)
			{
				if (!trace.startsolid && !trace.allsolid)
					VectorCopy (trace.endpos, pmove.origin);
			}
		}

		return blocked;		// moved the entire distance
	}

	if (in_air)
	{
		// don't let us step up unless it's indeed a step we bumped in
		// (that is, there's solid ground below)
		float *org;

		if (!(blocked & BLOCKED_STEP))
			return blocked;

		org = (-DotProduct(pmove.gravitydir, originalvel) < 0) ? pmove.origin : original;
		VectorMA (org, movevars.stepheight, pmove.gravitydir, dest);
		trace = PM_PlayerTrace (org, dest, MASK_PLAYERSOLID);
		if (trace.fraction == 1 || -DotProduct(pmove.gravitydir, trace.plane.normal) < MIN_STEP_NORMAL)
			return blocked;

		// adjust stepsize, otherwise it would be possible to walk up a
		// a step higher than STEPSIZE
		//FIXME gravitydir, portals
		stepsize = movevars.stepheight - (org[2] - trace.endpos[2]);
	}
	else
		stepsize = movevars.stepheight;

	VectorCopy (pmove.origin, down);
	VectorCopy (pmove.velocity, downvel);

	VectorCopy (original, pmove.origin);
	VectorCopy (originalvel, pmove.velocity);

// move up a stair height
	VectorMA (pmove.origin, -stepsize, pmove.gravitydir, dest);
	trace = PM_PlayerTrace (pmove.origin, dest, MASK_PLAYERSOLID);
	if (!trace.startsolid && !trace.allsolid)
	{
		VectorCopy (trace.endpos, pmove.origin);
	}

	if (in_air && -DotProduct(pmove.gravitydir, original) < 0)
		VectorMA(pmove.velocity, -DotProduct(pmove.velocity, pmove.gravitydir), pmove.gravitydir, pmove.velocity); //z=0

	PM_SlideMove ();

// press down the stepheight
	VectorMA (pmove.origin, stepsize, pmove.gravitydir, dest);
	trace = PM_PlayerTrace (pmove.origin, dest, MASK_PLAYERSOLID);
	if (trace.fraction != 1 && -DotProduct(pmove.gravitydir, trace.plane.normal) < MIN_STEP_NORMAL)
		goto usedown;
	if (!trace.startsolid && !trace.allsolid)
	{
		VectorCopy (trace.endpos, pmove.origin);
	}

	if (-DotProduct(pmove.gravitydir, pmove.origin) < -DotProduct(pmove.gravitydir, original))
		goto usedown;

	VectorCopy (pmove.origin, up);

	// decide which one went farther (in the forwards direction regardless of step values)
	VectorSubtract(down, original, dest);
	VectorMA(dest, -DotProduct(dest, pmove.gravitydir), pmove.gravitydir, dest); //z=0
	downdist = DotProduct(dest, dest);
	VectorSubtract(up, original, dest);
	VectorMA(dest, -DotProduct(dest, pmove.gravitydir), pmove.gravitydir, dest); //z=0
	updist = DotProduct(dest, dest);

	if (downdist >= updist)
	{
usedown:
		VectorCopy (down, pmove.origin);
		VectorCopy (downvel, pmove.velocity);
		return blocked;
	}

	// copy z value from slide move
	VectorMA(pmove.velocity, DotProduct(downvel, pmove.gravitydir)-DotProduct(pmove.velocity, pmove.gravitydir), pmove.gravitydir, pmove.velocity); //z=downvel

	if (!pmove.onground && pmove.waterlevel < 2 && (blocked & BLOCKED_STEP)) {
		float scale;
		// in pm_airstep mode, walking up a 16 unit high step
		// will kill 16% of horizontal velocity
		scale = 1 - 0.01*(pmove.origin[2] - original[2]);
		//FIXME gravitydir
		pmove.velocity[0] *= scale;
		pmove.velocity[1] *= scale;
	}

	return blocked;
}



/*
==================
PM_Friction

Handles both ground friction and water friction
==================
*/
void PM_Friction (void)
{
	float	speed, newspeed, control;
	float	friction;
	float	drop;
	vec3_t	start, stop;
	trace_t	trace;

	if (pmove.waterjumptime)
		return;

	speed = Length(pmove.velocity);
	if (speed < 1)
	{
//fixme: gravitydir fix needed
		pmove.velocity[0] = 0;
		pmove.velocity[1] = 0;
		if (pmove.pm_type == PM_FLY || pmove.pm_type == PM_6DOF)
			pmove.velocity[2] = 0;
		return;
	}

	if (pmove.waterlevel >= 2)
		// apply water friction, even if in fly mode
		drop = speed*movevars.waterfriction*pmove.waterlevel*frametime;
	else if (pmove.pm_type == PM_FLY || pmove.pm_type == PM_6DOF) {
		// apply flymode friction
		drop = speed * movevars.flyfriction * frametime;
	}
	else if (pmove.onground) {
		// apply ground friction
		friction = movevars.friction;
		if (movevars.edgefriction != 1.0)
		{
			// if the leading edge is over a dropoff, increase friction
			start[0] = stop[0] = pmove.origin[0] + pmove.velocity[0]/speed*16;
			start[1] = stop[1] = pmove.origin[1] + pmove.velocity[1]/speed*16;
			//FIXME: gravitydir.
			//id quirk: this is a tracebox, NOT a traceline, yet still starts BELOW the player.
			start[2] = pmove.origin[2] + pmove.player_mins[2];
			stop[2] = start[2] - 34;
			if (movevars.flags & MOVEFLAG_QWEDGEBOX)	//vanilla qw behaviour is to use a tracebox, which makes edge friction almost unnoticable.
				trace = PM_PlayerTrace (start, stop, MASK_PLAYERSOLID);
			else
			{	//traceline instead.
				vec3_t min, max;
				VectorCopy(pmove.player_mins, min);
				VectorCopy(pmove.player_maxs, max);
				VectorClear(pmove.player_mins);
				VectorClear(pmove.player_maxs);
				trace = PM_PlayerTrace (start, stop, MASK_PLAYERSOLID);
				VectorCopy(min, pmove.player_mins);
				VectorCopy(max, pmove.player_maxs);
			}
			if (trace.fraction == 1 && !trace.startsolid)
				friction *= movevars.edgefriction;
		}
		control = speed < movevars.stopspeed ? movevars.stopspeed : speed;
		drop = control*friction*frametime;
	}
	else if (pmove.onladder)
	{
		control = speed < movevars.stopspeed ? movevars.stopspeed : speed;
		drop = control*movevars.friction*frametime*6;
	}
	else
		return;		// in air, no friction

// scale the velocity
	newspeed = speed - drop;
	if (newspeed < 0)
		newspeed = 0;

	VectorScale (pmove.velocity, newspeed / speed, pmove.velocity);
}


/*
==============
PM_Accelerate
==============
*/
void PM_Accelerate (vec3_t wishdir, float wishspeed, float accel)
{
	int			i;
	float		addspeed, accelspeed, currentspeed;

	if (pmove.pm_type == PM_DEAD)
		return;
	if (pmove.waterjumptime)
		return;

	currentspeed = DotProduct (pmove.velocity, wishdir);
	addspeed = wishspeed - currentspeed;
	if (addspeed <= 0)
		return;
	accelspeed = accel*frametime*wishspeed;
	if (accelspeed > addspeed)
		accelspeed = addspeed;

	for (i=0 ; i<3 ; i++)
		pmove.velocity[i] += accelspeed*wishdir[i];
}

void PM_AirAccelerate (vec3_t wishdir, float wishspeed, float accel)
{
	int			i;
	float		addspeed, accelspeed, currentspeed, wishspd = wishspeed;
	float		originalspeed, newspeed, speedcap;

	if (pmove.pm_type == PM_DEAD)
		return;
	if (pmove.waterjumptime)
		return;

	if (movevars.bunnyspeedcap > 0)
	{
		originalspeed = sqrt(pmove.velocity[0]*pmove.velocity[0] +
						pmove.velocity[1]*pmove.velocity[1]);
	}
	else
		originalspeed = 0;	//shh compiler.

	if (wishspd > movevars.maxairspeed)
		wishspd = movevars.maxairspeed;
	currentspeed = DotProduct (pmove.velocity, wishdir);
	addspeed = wishspd - currentspeed;
	if (addspeed <= 0)
		return;
	accelspeed = accel * wishspeed * frametime;
	if (accelspeed > addspeed)
		accelspeed = addspeed;

	for (i=0 ; i<3 ; i++)
		pmove.velocity[i] += accelspeed*wishdir[i];

	if (movevars.bunnyspeedcap > 0)
	{
		newspeed = sqrt(pmove.velocity[0]*pmove.velocity[0] +
					pmove.velocity[1]*pmove.velocity[1]);
		if (newspeed > originalspeed)
		{
			speedcap = movevars.maxspeed * movevars.bunnyspeedcap;
			if (newspeed > speedcap)
			{
				if (originalspeed < speedcap)
					originalspeed = speedcap;
				pmove.velocity[0] *= originalspeed / newspeed;
				pmove.velocity[1] *= originalspeed / newspeed;
			}
		}
	}
}



/*
===================
PM_WaterMove
===================
*/
void PM_WaterMove (void)
{
	int		i;
	vec3_t	wishvel;
	float	wishspeed;
	vec3_t	wishdir;

//
// user intentions
//
	for (i=0 ; i<3 ; i++)
		wishvel[i] = forward[i]*pmove.cmd.forwardmove + right[i]*pmove.cmd.sidemove;

	if (pmove.pm_type != PM_FLY && !pmove.cmd.forwardmove && !pmove.cmd.sidemove && !pmove.cmd.upmove && !pmove.onladder)
	{
		VectorMA(wishvel, movevars.watersinkspeed, pmove.gravitydir, wishvel);
	}
	else
	{
		VectorMA(wishvel, -pmove.cmd.upmove, pmove.gravitydir, wishvel);
	}

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);

	if (wishspeed > movevars.maxspeed) {
		VectorScale (wishvel, movevars.maxspeed/wishspeed, wishvel);
		wishspeed = movevars.maxspeed;
	}
	wishspeed *= 0.7;

//
// water acceleration
//
	PM_Accelerate (wishdir, wishspeed, movevars.wateraccelerate);

	PM_StepSlideMove (false);
}


/*
*/
void PM_FlyMove (void)
{
	int		i;
	vec3_t	wishvel;
	float	wishspeed;
	vec3_t	wishdir;

	if (pmove.pm_type == PM_6DOF)
	{
		for (i=0 ; i<3 ; i++)
			wishvel[i] = forward[i]*pmove.cmd.forwardmove + right[i]*pmove.cmd.sidemove + up[i]*pmove.cmd.upmove;
	}
	else
	{
		for (i=0 ; i<3 ; i++)
			wishvel[i] = forward[i]*pmove.cmd.forwardmove + right[i]*pmove.cmd.sidemove;

		VectorMA(wishvel, -pmove.cmd.upmove, pmove.gravitydir, wishvel);
	}

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);

	if (wishspeed > movevars.maxspeed) {
		VectorScale (wishvel, movevars.maxspeed/wishspeed, wishvel);
		wishspeed = movevars.maxspeed;
	}

	PM_Accelerate (wishdir, wishspeed, movevars.accelerate);
	
	PM_StepSlideMove (false);
}

void PM_LadderMove (void)
{
	int		i;
	vec3_t	wishvel;
	float	wishspeed;
	vec3_t	wishdir;
	vec3_t	start, dest;
	trace_t	trace;

//
// user intentions
//
	if (pmove.cmd.vr_active)
	{
		vec3_t yawangles, ladder_forward, ladder_right, ladder_up;

		VectorCopy (pmove.angles, yawangles);
		yawangles[PITCH] = 0;
		yawangles[ROLL] = 0;
		AngleVectors (yawangles, ladder_forward, ladder_right, ladder_up);

		VectorMA (ladder_forward, -DotProduct(ladder_forward, pmove.gravitydir), pmove.gravitydir, ladder_forward);
		if (VectorNormalize (ladder_forward) < 0.001f)
			VectorClear (ladder_forward);
		VectorMA (ladder_right, -DotProduct(ladder_right, pmove.gravitydir), pmove.gravitydir, ladder_right);
		if (VectorNormalize (ladder_right) < 0.001f)
			VectorClear (ladder_right);

		VectorClear (wishvel);
		VectorMA (wishvel, pmove.cmd.forwardmove * 0.35f, ladder_forward, wishvel);
		VectorMA (wishvel, pmove.cmd.sidemove * 0.35f, ladder_right, wishvel);
		VectorMA (wishvel, -pmove.cmd.forwardmove * 0.35f, pmove.gravitydir, wishvel);
	}
	else
	{
		for (i=0 ; i<3 ; i++)
			wishvel[i] = forward[i]*pmove.cmd.forwardmove + right[i]*pmove.cmd.sidemove + up[i]*pmove.cmd.upmove;

		if (wishvel[2] >= 100 || wishvel[2] <= -100)	//large up/down move
			wishvel[2]*=10;
	}

	if (pmove.cmd.buttons & 2)
	{
		VectorMA(wishvel, -movevars.maxspeed, pmove.gravitydir, wishvel);
	}

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);

	if (wishspeed > movevars.maxspeed)
	{
		VectorScale (wishvel, movevars.maxspeed/wishspeed, wishvel);
		wishspeed = movevars.maxspeed;
	}

	PM_Accelerate (wishdir, wishspeed, movevars.wateraccelerate);

// assume it is a stair or a slope, so press down from stepheight above
	VectorMA (pmove.origin, frametime, pmove.velocity, dest);
	VectorMA(dest, -(movevars.stepheight + 1), pmove.gravitydir, start);
	trace = PM_PlayerTrace (start, dest, MASK_PLAYERSOLID);
	if (!trace.startsolid && !trace.allsolid)	// FIXME: check steep slope?
	{	// walked up the step
		VectorCopy (trace.endpos, pmove.origin);
		return;
	}

	PM_FlyMove ();

}

/*
===================
PM_AirMove

===================
*/
void PM_AirMove (void)
{
	int			i;
	float		fmove, smove;
	vec3_t		wishdir;
	float		wishspeed;

	if (pmove.gravitydir[2] == -1 && (pmove.angles[0] == 90 || pmove.angles[0] == -90))
	{	//HACK: attempt to avoid a stupid numerical precision issue.
		//You know its a hack because I'm comparing exact angles.
		vec3_t tmp;
		VectorSet(tmp, pmove.angles[0]*0.99, pmove.angles[1], pmove.angles[2]);
		AngleVectors (tmp, forward, right, up);
	}

	fmove = pmove.cmd.forwardmove;
	smove = pmove.cmd.sidemove;
	VectorMA(forward, -DotProduct(forward, pmove.gravitydir), pmove.gravitydir, forward); //z=0
	VectorMA(right, -DotProduct(right, pmove.gravitydir), pmove.gravitydir, right); //z=0
	VectorNormalize (forward);
	VectorNormalize (right);

	for (i=0 ; i<3 ; i++)
		wishdir[i] = forward[i]*fmove + right[i]*smove;
	VectorMA(wishdir, -DotProduct(wishdir, pmove.gravitydir), pmove.gravitydir, wishdir); //z=0

	wishspeed = VectorNormalize(wishdir);

//
// clamp to server defined max speed
//
	if (wishspeed > movevars.maxspeed)
	{
		wishspeed = movevars.maxspeed;
	}

	if (pmove.onground)
	{
		if (movevars.slidefix)
		{
			if (DotProduct(pmove.velocity, pmove.gravitydir) < 0)
			{
				VectorMA(pmove.velocity, -DotProduct(pmove.velocity, pmove.gravitydir), pmove.gravitydir, pmove.velocity); //z=0
				//pmove.velocity[2] = min(pmove.velocity[2], 0);	// bound above by 0
			}
			PM_Accelerate (wishdir, wishspeed, movevars.accelerate);
			// add gravity
			VectorMA(pmove.velocity, movevars.entgravity * movevars.gravity * frametime, pmove.gravitydir, pmove.velocity);
		}
		else
		{
			VectorMA(pmove.velocity, -DotProduct(pmove.velocity, pmove.gravitydir), pmove.gravitydir, pmove.velocity); //z=0
			PM_Accelerate (wishdir, wishspeed, movevars.accelerate);
		}

		//clear the z out, so we can test if we're moving horizontally relative to gravity
		VectorMA(pmove.velocity, -DotProduct(pmove.velocity, pmove.gravitydir), pmove.gravitydir, wishdir);
		if (!DotProduct(wishdir, wishdir) && !movevars.slidyslopes)
		{
			//clear z if we're not moving
			VectorClear(pmove.velocity);
			return;
		}
		else if (!movevars.slidefix && !movevars.slidyslopes)
			VectorMA(pmove.velocity, -DotProduct(pmove.velocity, pmove.gravitydir), pmove.gravitydir, pmove.velocity); //z=0

		PM_StepSlideMove(false);
	}
	else
	{
		int blocked;

		// not on ground, so little effect on velocity
		PM_AirAccelerate (wishdir, wishspeed, (movevars.flags&MOVEFLAG_USEAIRACCEL)?movevars.airaccelerate:movevars.accelerate);

		// add gravity
		VectorMA(pmove.velocity, movevars.entgravity * movevars.gravity * frametime, pmove.gravitydir, pmove.velocity);

		if (DotProduct(pmove.velocity,pmove.velocity) > 1000*1000)
		{
			//when in a windtunnel, step up from where we are rather than the actual ground in order to more closely match nq.
			//this is needed for r1m5 (770 800 192), just beyond the silver key door.
			blocked = PM_StepSlideMove (false);
		}
		else if (movevars.airstep)
			blocked = PM_StepSlideMove (true);
		else
			blocked = PM_SlideMove ();

		if (movevars.pground && (blocked & BLOCKED_FLOOR))
			pmove.onground = true;
	}
}


static plane_t	groundplane;	//valid only when pmove.onground

/*
=============
PM_CategorizePosition
=============
*/
void PM_CategorizePosition (void)
{
	vec3_t		point;
	int			cont;
	trace_t		trace;

	if (pmove.gravitydir[0] == 0 && pmove.gravitydir[1] == 0 && pmove.gravitydir[2] == 0)
	{
		pmove.gravitydir[0] = 0;
		pmove.gravitydir[1] = 0;
		pmove.gravitydir[2] = -1;
	}
	if (pmove.pm_type == PM_WALLWALK)
	{
		vec3_t tmin,tmax;
		VectorCopy(pmove.player_mins, tmin);
		VectorCopy(pmove.player_maxs, tmax);

//		//try tracing forwards+down
//		VectorMA(pmove.origin, -48, up, point);
//		VectorMA(point, 48, forward, point);
//		trace = PM_TraceLine(pmove.origin, point);
//		trace.fraction = 1;
//		if (1)//trace.fraction == 1)
		{	//getting desparate
			VectorMA(pmove.origin, -48, up, point);
			VectorMA(point, 48, forward, point);
			trace = PM_TraceLine(pmove.origin, point);
		}
		if (trace.fraction == 1)
		{
			//try tracing directly down only (we may be stepping off a cliff)
			VectorMA(pmove.origin, -48, up, point);
			trace = PM_TraceLine(pmove.origin, point);
		}
		if (trace.fraction == 1)
		{
			vec3_t point2;
			//try tracing back from the cliff to see if we can find the ground beyond
			VectorMA(point, 48, forward, point2);
			VectorMA(point2, 48, forward, point);
			trace = PM_TraceLine(point2, point);
		}
		if (trace.fraction == 1)
		{	//getting desparate
			VectorMA(pmove.origin, -48, up, point);
			VectorMA(point, -48, forward, point);
			trace = PM_TraceLine(pmove.origin, point);
		}

		VectorCopy(tmin, pmove.player_mins);
		VectorCopy(tmax, pmove.player_maxs);

		if (trace.fraction < 1)
			VectorNegate(trace.plane.normal, pmove.gravitydir);
	}

// if the player hull point one unit down is solid, the player
// is on ground

// see if standing on something solid
	VectorAdd(pmove.origin, pmove.gravitydir, point);
	trace.startsolid = trace.allsolid = true;
	VectorClear(trace.endpos);
	if (-DotProduct(pmove.gravitydir, pmove.velocity) > 180)
	{
		pmove.onground = false;
	}
	else if (!movevars.pground || pmove.onground)
	{
		trace = PM_PlayerTrace (pmove.origin, point, MASK_PLAYERSOLID);
		if (!trace.startsolid && trace.fraction < 1 && -DotProduct(pmove.gravitydir, trace.plane.normal) < MIN_STEP_NORMAL)
		{	//if the trace hit a slope, slide down the slope to see if we can find ground below. this should fix the 'base-of-slope-is-slide' bug.
			vec3_t bounce;
			PM_ClipVelocity (pmove.gravitydir, trace.plane.normal, bounce, 2);
			VectorMA(trace.endpos, 1-trace.fraction, bounce, point);
			trace = PM_PlayerTrace (trace.endpos, point, MASK_PLAYERSOLID);
		}

		if (!trace.startsolid && (trace.fraction == 1 || -DotProduct(pmove.gravitydir, trace.plane.normal) < MIN_STEP_NORMAL))
			pmove.onground = false;
		else
		{
			pmove.onground = !trace.startsolid;
			pmove.groundent = PM_LastTraceEntNum();
			groundplane = trace.plane;
			pmove.waterjumptime = 0;
		}

		// standing on an entity other than the world
		if (PM_LastTraceEntNum() > 0)
			PM_AddTouchedEnt (PM_LastTraceEntNum());
	}

//
// get waterlevel
//
	pmove.waterlevel = 0;
	pmove.watertype = CONTENTBIT_EMPTY;

	//FIXME: gravitydir
	VectorCopy(pmove.origin, point);
	point[2] = pmove.origin[2] + pmove.player_mins[2] + 1;
	cont = PM_PointContents (point);

	if (cont & CONTENTBITS_FLUID)
	{
		pmove.watertype = cont;
		pmove.waterlevel = 1;
		point[2] = pmove.origin[2] + (pmove.player_mins[2] + pmove.player_maxs[2])*0.5;
		cont = PM_PointContents (point);
		if (cont & CONTENTBITS_FLUID)
		{
			pmove.waterlevel = 2;
			point[2] = pmove.origin[2] + pmove.player_mins[2]+24+DEFAULT_VIEWHEIGHT;
			cont = PM_PointContents (point);
			if (cont & CONTENTBITS_FLUID)
				pmove.waterlevel = 3;
		}
	}

	//bsp objects marked as ladders mark regions to stand in to be classed as on a ladder.
	cont = PM_ExtraBoxContents(pmove.origin);

	if (pmove.physents[0].model)
	{
		//contents-based ladders
		if (cont & CONTENTBIT_LADDER)
		{
			trace_t t;
			vec3_t flatforward, fwd1;

			flatforward[0] = forward[0];
			flatforward[1] = forward[1];
			flatforward[2] = 0;
			VectorNormalize (flatforward);

			VectorMA (pmove.origin, 24, flatforward, fwd1);

			//if we hit a wall when going forwards and we are in a ladder region, then we are on a ladder.
			t = PM_PlayerTrace(pmove.origin, fwd1, MASK_PLAYERSOLID);
			if (t.fraction < 1)
			{
				pmove.onladder = true;
				pmove.onground = false;	// too steep
			}
		}
	}

	if (!movevars.pground && pmove.onground && pmove.pm_type != PM_FLY && pmove.waterlevel < 2)
	{
		// snap to ground so that we can't jump higher than we're supposed to
		if (!trace.startsolid && !trace.allsolid)
			VectorCopy (trace.endpos, pmove.origin);
	}
}


/*
=============
PM_CheckJump
=============
*/
static void PM_CheckJump (void)
{
	if (pmove.pm_type == PM_FLY)
		return;

	if (pmove.pm_type == PM_DEAD)
	{
		pmove.jump_held = true;	// don't jump on respawn
		return;
	}

	if (!(pmove.cmd.buttons & BUTTON_JUMP))
	{
		pmove.jump_held = false;
		return;
	}

	if (pmove.waterjumptime)
		return;

	if (pmove.waterlevel >= 2)
	{	// swimming, not jumping
		float speed;
		pmove.onground = false;

		if (pmove.watertype == CONTENTBIT_WATER)
			speed = 100;
		else if (pmove.watertype == CONTENTBIT_SLIME)
			speed = 80;
		else
			speed = 50;

		VectorMA(pmove.velocity, -speed-DotProduct(pmove.velocity, pmove.gravitydir), pmove.gravitydir, pmove.velocity);
		return;
	}

	if (!pmove.onground)
		return;		// in air, so no effect

	if (pmove.jump_held && !pmove.jump_secs)
		return;		// don't pogo stick

	// check for jump bug
	// groundplane normal was set in the call to PM_CategorizePosition
	if (!movevars.pground && -DotProduct(pmove.gravitydir, pmove.velocity) < 0 && DotProduct(pmove.velocity, groundplane.normal) < -0.1)
	{
		// pmove.velocity is pointing into the ground, clip it
		PM_ClipVelocity (pmove.velocity, groundplane.normal, pmove.velocity, 1);
	}

	pmove.onground = false;
	VectorMA(pmove.velocity, -movevars.jumpspeed, pmove.gravitydir, pmove.velocity);

	if (movevars.ktjump > 0 && pmove.pm_type != PM_WALLWALK)
	{
		if (movevars.ktjump > 1)
			movevars.ktjump = 1;
		if (pmove.velocity[2] < movevars.jumpspeed)
			pmove.velocity[2] = pmove.velocity[2] * (1 - movevars.ktjump)
				+ movevars.jumpspeed * movevars.ktjump;
	}

	pmove.jump_held = true;		// don't jump again until released
	pmove.jump_secs = pmove.cmd.seconds;
}

/*
=============
PM_CheckWaterJump
=============
*/
static void PM_CheckWaterJump (void)
{
	vec3_t	spot, spot2;
//	int		cont;
	vec3_t	flatforward;
	trace_t tr;
	vec3_t oldmin, oldmax;

	if (pmove.waterjumptime>0)
		return;
	if (pmove.pm_type == PM_DEAD)
		return;

	// don't hop out if we just jumped in
	if (pmove.velocity[2] < -180)
		return;

	// see if near an edge
	flatforward[0] = forward[0];
	flatforward[1] = forward[1];
	flatforward[2] = 0;
	VectorNormalize (flatforward);

	VectorCopy(pmove.player_mins, oldmin);
	VectorCopy(pmove.player_maxs, oldmax);
	VectorCopy(pmove.origin, spot);
	spot[2] += 8 + 24+pmove.player_mins[2];	//hexen2 fix. calculated from the normal bottom of bbox
	VectorMA (spot, 24, flatforward, spot2);
	tr = PM_TraceLine(spot, spot2);
	VectorCopy(oldmin, pmove.player_mins);
	VectorCopy(oldmax, pmove.player_maxs);
	if (tr.fraction == 1)	//(possibly) give up if open at waist
	{	//NQ bug workaround: NQ does waterjump checks inside prethink, and THEN sets waterlevel after.
		//The player then moves to where waterlevel SHOULD be 3, except you're still allowed to waterjump because of last frame.
		//Which is horrible buggy framerate-dependant behaviour...
		//so lets just try again 2qu up.
		//This'll cause slight prediction issues with other qw engines, and maybe some newly bugged maps, but those maps were probably already buggy with a low enough nq framerate.
		spot[2] += 2;
		spot2[2] += 2;
		tr = PM_TraceLine(spot, spot2);
		VectorCopy(oldmin, pmove.player_mins);
		VectorCopy(oldmax, pmove.player_maxs);
		if (tr.fraction == 1)	//give up if open at waist
			return;
	}
	spot[2] += 24;
	spot2[2] += 24;
	tr = PM_TraceLine(spot, spot2);
	VectorCopy(oldmin, pmove.player_mins);
	VectorCopy(oldmax, pmove.player_maxs);
	if (tr.fraction < 1)	//give up if blocked at eye
		return;

	// jump out of water
	VectorScale (flatforward, 50, pmove.velocity);
	pmove.velocity[2] = 310;
	pmove.waterjumptime = 2;	// safety net
	pmove.jump_held = true;		// don't jump again until released
}

/*
=================
PM_NudgePosition

If pmove.origin is in a solid position,
try nudging slightly on all axis to
allow for the cut precision of the net coordinates
=================
*/
static void PM_NudgePosition (void)
{
	vec3_t	base, nudged;
	int		x, y, z;
	int		i;
	static float	sign[] = {0, -1/8.0, 1/8.0};

	VectorCopy (pmove.origin, base);

	//really we want to just use this here
	//base[i] = MSG_FromCoord(MSG_ToCoord(pmove.origin[i], movevars.coordsize), movevars.coordsize);
	//but it has overflow issues, so do things the painful way instead.
	//this stuff is so annoying because we're trying to avoid biasing the position towards 0. you'll see the effects of that if you use a low forwardspeed or low sv_gamespeed etc, but its also noticable with default settings too.
	if ((movevars.protocolflags & PRFL_FLOATCOORD)	//float precision on the network. no need to truncate.
			|| (movevars.protocolflags & PRFL_24BITCOORD)) //utter pain. we're never gonna use it anyway.
	{
		VectorCopy (base, nudged);
	}
	else if (movevars.protocolflags & PRFL_INT32COORD)
	{
		for (i=0 ; i<3 ; i++)
		{
			if (base[i] >= 0)
				nudged[i] = (intmax_t)(base[i]*16+0.5f) / 16.0;
			else
				nudged[i] = (intmax_t)(base[i]*16-0.5f) / 16.0;
		}
	}
	else if (1)	//1/8th precision, but don't truncate because that screws everything up.
	{
		for (i=0 ; i<3 ; i++)
		{
			if (base[i] >= 0)
				nudged[i] = (intmax_t)(base[i]*8+0.5f) / 8.0;
			else
				nudged[i] = (intmax_t)(base[i]*8-0.5f) / 8.0;
		}
	}
	else for (i=0 ; i<3 ; i++)
		nudged[i] = ((intmax_t) (pmove.origin[i] * 8)) * 0.125;	//legacy compat, which biases towards the origin.

//	VectorCopy (base, pmove.origin);

	//if we're moving, allow that spot without snapping to any grid
//	if (pmove.velocity[0] || pmove.velocity[1] || pmove.velocity[2])
//		if (PM_TestPlayerPosition (pmove.origin))
//			return;

	//this is potentially 27 tests, and required for qw compat...
	//with unquantized floors it often succeeds only after 19 checks. which sucks.
	for (z=0 ; z<countof(sign) ; z++)
	{
		for (x=0 ; x<countof(sign) ; x++)
		{
			for (y=0 ; y<countof(sign) ; y++)
			{
				pmove.origin[0] = nudged[0] + sign[x];
				pmove.origin[1] = nudged[1] + sign[y];
				pmove.origin[2] = nudged[2] + sign[z];
				if (PM_TestPlayerPosition (pmove.origin))
					return;
			}
		}
	}

	//still not managed it... be more agressive axially.
	for (z=0 ; z<3; z++)
	{
		VectorCopy(base, pmove.origin);
		pmove.origin[z] = nudged[z] + (2/8.0);
		if (PM_TestPlayerPosition (pmove.origin))
			return;

		VectorCopy(base, pmove.origin);
		pmove.origin[z] = nudged[z] - (2/8.0);
		if (PM_TestPlayerPosition (pmove.origin))
			return;
	}

	//be more aggresssive at moving up, to match NQ
	for (z=1 ; z<movevars.stepheight ; z++)
	{
		for (x=0 ; x<3 ; x++)
		{
			for (y=0 ; y<3 ; y++)
			{
				pmove.origin[0] = nudged[0] + sign[x];
				pmove.origin[1] = nudged[1] + sign[y];
				pmove.origin[2] = nudged[2] + z;
				if (PM_TestPlayerPosition (pmove.origin))
					return;
			}
		}
	}

	if (pmove.safeorigin_known && PM_TestPlayerPosition(pmove.safeorigin))
	{
		VectorCopy (pmove.safeorigin, pmove.origin);
	}
	else
	{
		VectorCopy (base, pmove.origin);
	}
//	Con_DPrintf ("NudgePosition: stuck\n");
}

/*
===============
PM_SpectatorMove
===============
*/
void PM_SpectatorMove (void)
{
	float	speed, drop, friction, control, newspeed;
	float	currentspeed, addspeed, accelspeed;
	int			i;
	vec3_t		wishvel;
	float		fmove, smove;
	vec3_t		wishdir;
	float		wishspeed;

	// friction

	speed = Length (pmove.velocity);
	if (speed < 1)
	{
		VectorClear (pmove.velocity);
	}
	else
	{
		drop = 0;

		friction = movevars.friction*1.5;	// extra friction
		control = speed < movevars.stopspeed ? movevars.stopspeed : speed;
		drop += control*friction*frametime;

		// scale the velocity
		newspeed = speed - drop;
		if (newspeed < 0)
			newspeed = 0;
		newspeed /= speed;

		VectorScale (pmove.velocity, newspeed, pmove.velocity);
	}

	// accelerate
	fmove = pmove.cmd.forwardmove;
	smove = pmove.cmd.sidemove;

	VectorNormalize (forward);
	VectorNormalize (right);

	for (i=0 ; i<3 ; i++)
		wishvel[i] = forward[i]*fmove + right[i]*smove;
	wishvel[2] += pmove.cmd.upmove;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);

	//
	// clamp to server defined max speed
	//
	if (wishspeed > movevars.spectatormaxspeed)
	{
		VectorScale (wishvel, movevars.spectatormaxspeed/wishspeed, wishvel);
		wishspeed = movevars.spectatormaxspeed;
	}

	currentspeed = DotProduct(pmove.velocity, wishdir);
	addspeed = wishspeed - currentspeed;

	// Buggy QW spectator mode, kept for compatibility
	if (pmove.pm_type == PM_OLD_SPECTATOR)
	{
		if (addspeed <= 0)
			return;
	}

	if (addspeed > 0) {
		accelspeed = movevars.accelerate*frametime*wishspeed;
		if (accelspeed > addspeed)
			accelspeed = addspeed;

		for (i=0 ; i<3 ; i++)
			pmove.velocity[i] += accelspeed*wishdir[i];
	}

	// move
	VectorMA (pmove.origin, frametime, pmove.velocity, pmove.origin);
}

/*
=============
PM_PlayerMove

Returns with origin, angles, and velocity modified in place.

Numtouch and touchindex[] will be set if any of the physents
were contacted during the move.
=============
*/
void PM_PlayerMove (float gamespeed)
{
//	int i;
//	int tmp;	//for rounding

	PM_EnsureInitialized ();
	frametime = pmove.cmd.seconds * gamespeed;
	pmove.numtouch = 0;

	if (pmove.pm_type == PM_NONE || pmove.pm_type == PM_FREEZE)
	{
		PM_CategorizePosition ();
		return;
	}

	// take angles directly from command
	VectorCopy(pmove.cmd.viewangles, pmove.angles);
	AngleVectors (pmove.angles, forward, right, up);

	if (pmove.pm_type == PM_SPECTATOR || pmove.pm_type == PM_OLD_SPECTATOR)
	{
		PM_SpectatorMove ();
		pmove.onground = false;
		return;
	}

	PM_NudgePosition ();

	// set onground, watertype, and waterlevel
	PM_CategorizePosition ();

	if (movevars.autobunny && !pmove.onground)
		pmove.jump_held = false;

	if (pmove.waterlevel == 2 && pmove.pm_type != PM_FLY)
		PM_CheckWaterJump ();

	if (-DotProduct(pmove.gravitydir, pmove.velocity) < 0 || pmove.pm_type == PM_DEAD)
		pmove.waterjumptime = 0;

	if (pmove.waterjumptime)
	{
		pmove.waterjumptime -= frametime;
		if (pmove.waterjumptime < 0)
			pmove.waterjumptime = 0;
	}

	if (pmove.jump_secs)
	{
		pmove.jump_secs += pmove.cmd.seconds;
		if (pmove.jump_secs > 0.050f)
			pmove.jump_secs = 0;
	}


	if (!movevars.bunnyfriction)
		PM_CheckJump ();	//qw-style bunny
	PM_Friction ();

	if (pmove.waterlevel >= 2)
		PM_WaterMove ();
	else if (pmove.pm_type == PM_FLY || pmove.pm_type == PM_6DOF)
		PM_FlyMove ();
	else if (pmove.onladder)
		PM_LadderMove ();
	else
		PM_AirMove ();

	if (movevars.bunnyfriction)
		PM_CheckJump ();	//nq-style bunny. note tick rate differences too.

/*	//round to network precision
	for (i = 0; i < 3; i++)
	{
		tmp = floor(pmove.velocity[i]*8 + 0.5);
		pmove.velocity[i] = tmp/8.0;
		tmp = floor(pmove.origin[i]*8 + 0.5);
		pmove.origin[i] = tmp/8.0;
	}
	PM_NudgePosition ();
*/
	// set onground, watertype, and waterlevel for final spot
	PM_CategorizePosition ();

	// this is to make sure landing sound is not played twice
	// and falling damage is calculated correctly
	if (!movevars.pground && pmove.onground && -DotProduct(pmove.gravitydir, pmove.velocity) < -300
		&& DotProduct(pmove.velocity, groundplane.normal) < -0.1)
	{
		PM_ClipVelocity (pmove.velocity, groundplane.normal, pmove.velocity, 1);
	}
}

static void PM_DecodeSolidSize (unsigned int solidsize, vec3_t mins, vec3_t maxs)
{
	maxs[0] = maxs[1] = solidsize & 255;
	mins[0] = mins[1] = -maxs[0];
	mins[2] = -(int)((solidsize >> 8) & 255);
	maxs[2] = (int)((solidsize >> 16) & 65535) - 32768;
}

static qboolean PM_BoundsOverlap (const vec3_t mins1, const vec3_t maxs1,
	const vec3_t mins2, const vec3_t maxs2)
{
	int i;

	for (i = 0; i < 3; i++)
		if (mins1[i] > maxs2[i] || maxs1[i] < mins2[i])
			return false;
	return true;
}

void World_AddEntsToPmove (edict_t *ignore, vec3_t boxminmax[2])
{
	entity_t	*touch;
	physent_t	*phys;
	vec3_t		mins, maxs;
	int			i;

	PM_EnsureInitialized ();

	if (ignore)
	{
		edict_t *other;

		pmove.skipent = NUM_FOR_EDICT(ignore);
		memset (pmove.physents, 0, sizeof(pmove.physents));
		pmove.physents[0].model = sv.worldmodel;
		VectorClear (pmove.physents[0].origin);
		VectorClear (pmove.physents[0].angles);
		pmove.physents[0].forcecontentsmask = 0;
		pmove.physents[0].info = 0;
		pmove.numphysent = 1;

		if (!sv.worldmodel || !qcvm)
			return;

		for (i = 1, other = NEXT_EDICT(qcvm->edicts);
			 i < qcvm->num_edicts;
			 i++, other = NEXT_EDICT(other))
		{
			int solid;

			if (other->free || other == ignore)
				continue;
			solid = (int)other->v.solid;
			if (solid != SOLID_BBOX && solid != SOLID_SLIDEBOX && solid != SOLID_BSP)
				continue;
			if (boxminmax &&
				(boxminmax[0][0] > other->v.absmax[0] ||
				 boxminmax[0][1] > other->v.absmax[1] ||
				 boxminmax[0][2] > other->v.absmax[2] ||
				 boxminmax[1][0] < other->v.absmin[0] ||
				 boxminmax[1][1] < other->v.absmin[1] ||
				 boxminmax[1][2] < other->v.absmin[2]))
				continue;
			if (PROG_TO_EDICT(other->v.owner) == ignore)
				continue;
			if (PROG_TO_EDICT(ignore->v.owner) == other)
				continue;

			if (pmove.numphysent == countof(pmove.physents))
				return;

			phys = &pmove.physents[pmove.numphysent];
			phys->info = i;
			phys->model = NULL;
			if (solid == SOLID_BSP)
			{
				int modelindex = (int)other->v.modelindex;
				if (modelindex > 0 && modelindex < MAX_MODELS &&
					sv.models[modelindex] && sv.models[modelindex]->type == mod_brush)
					phys->model = sv.models[modelindex];
			}
			VectorCopy (other->v.origin, phys->origin);
			VectorCopy (other->v.mins, phys->mins);
			VectorCopy (other->v.maxs, phys->maxs);
			VectorCopy (other->v.angles, phys->angles);
			phys->forcecontentsmask = 0;
			switch ((int)other->v.skin)
			{
			case CONTENTS_WATER:
				phys->forcecontentsmask = CONTENTBIT_WATER;
				break;
			case CONTENTS_LAVA:
				phys->forcecontentsmask = CONTENTBIT_LAVA;
				break;
			case CONTENTS_SLIME:
				phys->forcecontentsmask = CONTENTBIT_SLIME;
				break;
			case CONTENTS_SKY:
				phys->forcecontentsmask = CONTENTBIT_SKY;
				break;
			case CONTENTS_CLIP:
				phys->forcecontentsmask = CONTENTBIT_CLIP;
				break;
			case CONTENTS_LADDER:
				phys->forcecontentsmask = CONTENTBIT_LADDER;
				break;
			default:
				break;
			}
			pmove.numphysent++;
		}
		return;
	}

	memset (pmove.physents, 0, sizeof(pmove.physents));
	pmove.physents[0].model = cl.worldmodel;
	VectorClear (pmove.physents[0].origin);
	VectorClear (pmove.physents[0].angles);
	pmove.physents[0].forcecontentsmask = 0;
	pmove.physents[0].info = 0;
	pmove.numphysent = 1;

	if (!cl.worldmodel || !cl.entities)
		return;

	for (i = 1, touch = cl.entities + 1; i < cl.num_entities; i++, touch++)
	{
		unsigned int solidsize;
		signed char contents_skin;

		solidsize = touch->netstate.solidsize;
		if (solidsize == ES_SOLID_NOT)
			continue;
		if (i == cl.viewentity)
			continue;

		if (solidsize == ES_SOLID_BSP)
		{
			if (!touch->model || touch->model->type != mod_brush)
				continue;
			VectorCopy (touch->model->mins, mins);
			VectorCopy (touch->model->maxs, maxs);
		}
		else
			PM_DecodeSolidSize (solidsize, mins, maxs);

		if (boxminmax)
		{
			vec3_t absmins, absmaxs;

			VectorAdd (touch->netstate.origin, mins, absmins);
			VectorAdd (touch->netstate.origin, maxs, absmaxs);
			if (!PM_BoundsOverlap (absmins, absmaxs, boxminmax[0], boxminmax[1]))
				continue;
		}

		if (pmove.numphysent == countof(pmove.physents))
			return;

		phys = &pmove.physents[pmove.numphysent];
		VectorCopy (mins, phys->mins);
		VectorCopy (maxs, phys->maxs);
		VectorCopy (touch->netstate.origin, phys->origin);
		VectorCopy (touch->netstate.angles, phys->angles);
		phys->model = (solidsize == ES_SOLID_BSP) ? touch->model : NULL;
		phys->info = -i;
		phys->forcecontentsmask = 0;

		contents_skin = (signed char)touch->netstate.skin;
		switch (contents_skin)
		{
		case CONTENTS_WATER:
			phys->forcecontentsmask = CONTENTBIT_WATER;
			break;
		case CONTENTS_LAVA:
			phys->forcecontentsmask = CONTENTBIT_LAVA;
			break;
		case CONTENTS_SLIME:
			phys->forcecontentsmask = CONTENTBIT_SLIME;
			break;
		case CONTENTS_SKY:
			phys->forcecontentsmask = CONTENTBIT_SKY;
			break;
		case CONTENTS_CLIP:
			phys->forcecontentsmask = CONTENTBIT_CLIP;
			break;
		case CONTENTS_LADDER:
			phys->forcecontentsmask = CONTENTBIT_LADDER;
			break;
		default:
			break;
		}

		pmove.numphysent++;
	}
}

void PMCL_ServerinfoUpdated (void)
{
	PM_EnsureInitialized ();
	PM_SetBaseMoveVars (&clmovevars, cl.protocolflags, false);
	clmovevars_valid = true;
}

void PMCL_SetMoveVars (void)
{
	PM_EnsureInitialized ();
	if (!clmovevars_valid)
		PMCL_ServerinfoUpdated ();
	movevars = clmovevars;
	if ((cl.protocol_pext2 & PEXT2_PREDINFO) &&
		(cl.stats[STAT_MOVEFLAGS] & MOVEFLAG_VALID))
	{
		movevars.stepheight = cl.statsf[STAT_MOVEVARS_STEPHEIGHT];
		movevars.flags = cl.stats[STAT_MOVEFLAGS];
		movevars.gravity = cl.statsf[STAT_MOVEVARS_GRAVITY];
		movevars.stopspeed = cl.statsf[STAT_MOVEVARS_STOPSPEED];
		movevars.maxspeed = cl.statsf[STAT_MOVEVARS_MAXSPEED];
		movevars.spectatormaxspeed = cl.statsf[STAT_MOVEVARS_SPECTATORMAXSPEED];
		movevars.accelerate = cl.statsf[STAT_MOVEVARS_ACCELERATE];
		movevars.airaccelerate = cl.statsf[STAT_MOVEVARS_AIRACCELERATE];
		movevars.wateraccelerate = cl.statsf[STAT_MOVEVARS_WATERACCELERATE];
		movevars.friction = cl.statsf[STAT_MOVEVARS_FRICTION];
		movevars.waterfriction = cl.statsf[STAT_MOVEVARS_WATERFRICTION];
		movevars.edgefriction = cl.statsf[STAT_MOVEVARS_EDGEFRICTION];
		movevars.entgravity = cl.statsf[STAT_MOVEVARS_ENTGRAVITY];
		movevars.jumpspeed = cl.statsf[STAT_MOVEVARS_JUMPVELOCITY];
		movevars.maxairspeed = cl.statsf[STAT_MOVEVARS_MAXAIRSPEED];
		movevars.watersinkspeed = cl.statsf[STAT_MOVEVARS_WATERSINKSPEED];
		movevars.flyfriction = cl.statsf[STAT_MOVEVARS_FLYFRICTION];
		movevars.bunnyspeedcap = cl.statsf[STAT_MOVEVARS_BUNNYSPEEDCAP];
		movevars.ktjump = cl.statsf[STAT_MOVEVARS_KTJUMP];
		PM_UnpackMoveFlags (&movevars);
		if (!(movevars.flags & MOVEFLAG_VALID))
			movevars.flags = MOVEFLAG_VALID | MOVEFLAG_NOGRAVITYONGROUND;
		if (movevars.entgravity <= 0)
			movevars.entgravity = 1.0f;
	}
}

void PMSV_UpdateMovevars (void)
{
	PM_EnsureInitialized ();
	PM_SetBaseMoveVars (&movevars, sv.protocolflags, true);
}

void PMSV_SetMoveStats (edict_t *plent, float *fstat, int *istat)
{
	eval_t *entgrav;
	float entgravity;

	PMSV_UpdateMovevars ();
	fstat[STAT_MOVEVARS_STEPHEIGHT] = movevars.stepheight;
	istat[STAT_MOVEFLAGS] = PM_PackMoveFlags (&movevars);
	fstat[STAT_MOVEVARS_GRAVITY] = movevars.gravity;
	fstat[STAT_MOVEVARS_STOPSPEED] = movevars.stopspeed;
	fstat[STAT_MOVEVARS_MAXSPEED] = movevars.maxspeed;
	fstat[STAT_MOVEVARS_SPECTATORMAXSPEED] = movevars.spectatormaxspeed;
	fstat[STAT_MOVEVARS_ACCELERATE] = movevars.accelerate;
	fstat[STAT_MOVEVARS_AIRACCELERATE] = movevars.airaccelerate;
	fstat[STAT_MOVEVARS_WATERACCELERATE] = movevars.wateraccelerate;
	fstat[STAT_MOVEVARS_FRICTION] = movevars.friction;
	fstat[STAT_MOVEVARS_WATERFRICTION] = movevars.waterfriction;
	fstat[STAT_MOVEVARS_EDGEFRICTION] = movevars.edgefriction;
	entgrav = plent ? GetEdictFieldValue(plent, qcvm->extfields.gravity) : NULL;
	entgravity = (entgrav && entgrav->_float) ? entgrav->_float : 1.0f;
	fstat[STAT_MOVEVARS_ENTGRAVITY] = entgravity;
	fstat[STAT_MOVEVARS_TIMESCALE] = 1;
	fstat[STAT_MOVEVARS_JUMPVELOCITY] = movevars.jumpspeed;
	fstat[STAT_MOVEVARS_MAXAIRSPEED] = movevars.maxairspeed;
	fstat[STAT_MOVEVARS_WATERSINKSPEED] = movevars.watersinkspeed;
	fstat[STAT_MOVEVARS_FLYFRICTION] = movevars.flyfriction;
	fstat[STAT_MOVEVARS_BUNNYSPEEDCAP] = movevars.bunnyspeedcap;
	fstat[STAT_MOVEVARS_KTJUMP] = movevars.ktjump;
}

void PF_sv_pmove (void)
{
	edict_t *ent = G_EDICT(OFS_PARM0);
	usercmd_t cmd;

	memset(&cmd, 0, sizeof(cmd));
	if (host_client && host_client->edict == ent)
		cmd = host_client->cmd;

	if (qcvm->extglobals.input_sequence)
		cmd.sequence = *qcvm->extglobals.input_sequence;
	if (qcvm->extglobals.input_servertime)
		cmd.servertime = *qcvm->extglobals.input_servertime;
	if (qcvm->extglobals.input_timelength)
		cmd.seconds = *qcvm->extglobals.input_timelength;
	if (qcvm->extglobals.input_movevalues) {
		cmd.forwardmove = qcvm->extglobals.input_movevalues[0];
		cmd.sidemove = qcvm->extglobals.input_movevalues[1];
		cmd.upmove = qcvm->extglobals.input_movevalues[2];
	}
	if (qcvm->extglobals.input_angles)
		VectorCopy(qcvm->extglobals.input_angles, cmd.viewangles);
	if (qcvm->extglobals.input_buttons)
		cmd.buttons = *qcvm->extglobals.input_buttons;
	if (qcvm->extglobals.input_impulse)
		cmd.impulse = *qcvm->extglobals.input_impulse;
	if (qcvm->extglobals.input_weapon)
		cmd.weapon = *qcvm->extglobals.input_weapon;
	if (qcvm->extglobals.input_cursor_screen) {
		cmd.cursor_screen[0] = qcvm->extglobals.input_cursor_screen[0];
		cmd.cursor_screen[1] = qcvm->extglobals.input_cursor_screen[1];
	}
	if (qcvm->extglobals.input_cursor_trace_start)
		VectorCopy(qcvm->extglobals.input_cursor_trace_start, cmd.cursor_start);
	if (qcvm->extglobals.input_cursor_trace_endpos)
		VectorCopy(qcvm->extglobals.input_cursor_trace_endpos, cmd.cursor_impact);
	if (qcvm->extglobals.input_cursor_entitynumber)
		cmd.cursor_entitynumber = *qcvm->extglobals.input_cursor_entitynumber;

	SV_RunPMoveForEntity(ent, &cmd);
}

void PM_Register (void)
{
	PM_EnsureInitialized ();
	Cvar_RegisterVariable (&pm_bunnyspeedcap);
	Cvar_RegisterVariable (&pm_bunnyfriction);
	Cvar_RegisterVariable (&pm_ktjump);
	Cvar_RegisterVariable (&pm_slidefix);
	Cvar_RegisterVariable (&pm_airstep);
	Cvar_RegisterVariable (&pm_pground);
	Cvar_RegisterVariable (&pm_stepdown);
	Cvar_RegisterVariable (&pm_walljump);
	Cvar_RegisterVariable (&pm_slidyslopes);
	Cvar_RegisterVariable (&pm_autobunny);
	Cvar_RegisterVariable (&pm_watersinkspeed);
	Cvar_RegisterVariable (&pm_flyfriction);
	Cvar_RegisterVariable (&pm_edgefriction);
	Cvar_RegisterVariable (&pm_stepheight);
	Cvar_RegisterVariable (&sv_airaccelerate);
	Cvar_RegisterVariable (&sv_wateraccelerate);
	Cvar_RegisterVariable (&sv_waterfriction);
	Cvar_RegisterVariable (&sv_spectatormaxspeed);
}
