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
#include "pmove.h"
#include "vr_gorilla.h"
#include "vr_gorilla_swim.h"
#include <limits.h>

movevars_t		movevars;
playermove_t	pmove;

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

#define PM_VANILLA_JUMP_VELOCITY 270.0f
#define PM_VANILLA_WATERJUMP_VELOCITY 310.0f
#define PM_VR_SWIM_JUMP_UPMOVE 200.0f
#define PM_MAX_SUBSTEP_SECONDS 0.025f
#define PM_VR_ROOMSCALE_MAX_DELTA 16.0f

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

static qboolean PM_IsVRMove (void)
{
	/* Keep VR swim/waterjump policy command-scoped. A generic mod may raise
	 * jumpspeed without becoming a VR movement mode. */
	return pmove.cmd.vr_active;
}

qboolean PM_VRInstantStopNeutralInput (const usercmd_t *cmd,
	qboolean block_teleport_backmove)
{
	float forward;

	if (!cmd)
		return false;
	forward = cmd->forwardmove;
	if (block_teleport_backmove && forward < 0.0f)
		forward = 0.0f;
	/* WALK ignores upmove, and orthogonal forward/right vectors cannot cancel. */
	return forward == 0.0f && cmd->sidemove == 0.0f;
}

static qboolean PM_VRInstantStopEligible (void)
{
	if (!(movevars.flags & MOVEFLAG_VR_INSTANT_STOP) || !PM_IsVRMove () ||
		pmove.pm_type != PM_NORMAL || !pmove.onground ||
		pmove.waterlevel >= 2 || pmove.onladder || pmove.waterjumptime ||
		!PM_VRInstantStopNeutralInput (&pmove.cmd,
			pmove.block_teleport_backmove))
		return false;
	/* Gorilla owns the command while its accepted hand input is eligible,
	 * including an idle hand that is still carrying body momentum. */
	if (pmove.gorilla_allowed &&
		(((pmove.cmd.vr_gorilla.flags & VR_GORILLA_HANDS) == VR_GORILLA_HANDS) ||
		 (pmove.cmd.vr_gorilla_motion.flags & VR_GORILLA_MOTION_ACTIVE)))
		return false;
	return true;
}

static float PM_VRJumpScale (void)
{
	if (PM_IsVRMove () && movevars.jumpspeed > PM_VANILLA_JUMP_VELOCITY)
		return movevars.jumpspeed / PM_VANILLA_JUMP_VELOCITY;
	return 1.0f;
}

static float PM_WaterUpMove (void)
{
	float upmove = pmove.cmd.upmove;

	if (PM_IsVRMove () && (pmove.cmd.buttons & BUTTON_JUMP) &&
		upmove < PM_VR_SWIM_JUMP_UPMOVE)
		upmove = PM_VR_SWIM_JUMP_UPMOVE;

	return upmove;
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

	// Preserve the source leaf test: only CONTENTS_SOLID blocks. Physent
	// filtering still uses the caller mask; raw CLIP leaves are not solid here.
	if (model && model->type == mod_brush)
	{
		hull = &model->hulls[(player_maxs[0]-player_mins[0] < 3) ? 0 : 1];
		if (angles[0] || angles[1] || angles[2])
		{
			AngleVectors (angles, axis[0], axis[1], axis[2]);
			QAxisTransform(axis, start_l, start_t);
			QAxisTransform(axis, end_l, end_t);
			VectorCopy (end_t, trace->endpos);
			SV_RecursiveHullCheck (hull, start_t, end_t, trace, CONTENTMASK_FROMQ1 (CONTENTS_SOLID));
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

			SV_RecursiveHullCheck (hull, start_l, end_l, trace, CONTENTMASK_FROMQ1 (CONTENTS_SOLID));
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

		SV_RecursiveHullCheck (&box_hull, start_l, end_l, trace, CONTENTMASK_FROMQ1 (CONTENTS_SOLID));
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

static void PM_AddTouchedEnt (int num);

static trace_t PM_PlayerTraceFiltered (vec3_t start, vec3_t end,
	unsigned int solidmask, qboolean brushonly)
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
		if (brushonly && (!pe->model || pe->model->needload ||
			pe->model->type != mod_brush))
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

trace_t PM_PlayerTrace (vec3_t start, vec3_t end, unsigned int solidmask)
{
	return PM_PlayerTraceFiltered(start, end, solidmask, false);
}

static vr_gorilla_trace_t PM_GorillaTrace (void *context,
	const float *start, const float *end, int body)
{
	vr_gorilla_trace_t result;
	trace_t trace;
	vec3_t mins, maxs, a, b;
	int saved_trace = pmove_trace_entnum, index;
	(void)context;
	VectorCopy(start, a);
	VectorCopy(end, b);
	VectorCopy(pmove.player_mins, mins);
	VectorCopy(pmove.player_maxs, maxs);
	if (!body)
	{
		VectorClear(pmove.player_mins);
		VectorClear(pmove.player_maxs);
	}
	trace = PM_PlayerTraceFiltered(a, b, MASK_PLAYERSOLID, !body);
	index = pmove_trace_entnum;
	VectorCopy(mins, pmove.player_mins);
	VectorCopy(maxs, pmove.player_maxs);
	pmove_trace_entnum = saved_trace;
	memset(&result, 0, sizeof(result));
	result.fraction = trace.fraction;
	result.startsolid = trace.startsolid;
	result.allsolid = trace.allsolid;
	VectorCopy(trace.endpos, result.end);
	VectorCopy(trace.plane.normal, result.normal);
	/* Client physents use negative numbers; surface IDs on the wire do not. */
	result.entity = index >= 0 ? abs(pmove.physents[index].info) : -1;
	if (body && index >= 0 && trace.fraction < 1)
		PM_AddTouchedEnt(index);
	return result;
}

static int PM_GorillaSurface(void *context, int entity, unsigned int *model,
	const float *point, float *out, int to_world)
{
	int i;
	vec3_t axes[3], local;
	(void)context;
	for (i = 1; i < pmove.numphysent; i++)
	{
		physent_t *pe = &pmove.physents[i];
		if (abs(pe->info) != entity)
			continue;
		if (!pe->model || pe->model->needload || pe->model->type != mod_brush ||
			!pe->modelindex || (to_world && *model != pe->modelindex) ||
			!VRG_Finite(pe->origin) || !VRG_Finite(pe->angles))
			return 0;
		*model = pe->modelindex;
		AngleVectors(pe->angles, axes[0], axes[1], axes[2]);
		if (to_world)
		{
			QAxisDeTransform(axes, point, local);
			VectorAdd(local, pe->origin, out);
		}
		else
		{
			VectorSubtract(point, pe->origin, local);
			QAxisTransform(axes, local, out);
		}
		return VRG_Finite(out);
	}
	return 0;
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
	int		i;

	if (pmove.numtouch == MAX_PHYSENTS)
		return;

	/* Preserve QSS-M's adjacent-only touch filtering for inherited single-step
	 * commands. Private duration commands span deterministic substeps and a
	 * room-scale sweep, so suppress repeated impacts across the whole command. */
	if (!pmove.cmd.msec)
	{
		if (pmove.numtouch && pmove.touchindex[pmove.numtouch - 1] == num)
			return;
	}
	else
	{
		for (i = 0; i < pmove.numtouch; i++)
			if (pmove.touchindex[i] == num)
				return;
	}

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
	float	upmove;

//
// user intentions
//
	for (i=0 ; i<3 ; i++)
		wishvel[i] = forward[i]*pmove.cmd.forwardmove + right[i]*pmove.cmd.sidemove;

	upmove = PM_WaterUpMove ();

	if (pmove.pm_type != PM_FLY && !pmove.cmd.forwardmove && !pmove.cmd.sidemove && !upmove && !pmove.onladder && !pmove.gorilla_swim_stroke)
	{
		VectorMA(wishvel, movevars.watersinkspeed, pmove.gravitydir, wishvel);
	}
	else
	{
		VectorMA(wishvel, -upmove, pmove.gravitydir, wishvel);
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
	if (pmove.block_teleport_backmove && fmove < 0)
		fmove = 0;
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
			if (!pmove.gorilla_braced)
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
		if (!pmove.gorilla_braced)
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
/* Gorilla pushes are often smaller than the ordinary one-unit ground snap,
 * and their launch speeds can be below Quake's 180-unit airborne threshold.
 * Never apply these exceptions merely because the server offers Gorilla. */
static qboolean PM_GorillaGroundContact (void)
{
	return pmove.gorilla_allowed && !pmove.onladder && !pmove.waterjumptime &&
		pmove.pm_type == PM_NORMAL &&
		((pmove.cmd.vr_active && pmove.cmd.vr_handpos_relative &&
		  (pmove.cmd.vr_gorilla_motion.flags & VR_GORILLA_MOTION_ACTIVE)) ||
		 VRG_InputValid(&pmove.cmd.vr_gorilla));
}

void PM_CategorizePosition (void)
{
	vec3_t		point;
	int			cont;
	trace_t		trace;
	qboolean	gorilla_airborne_brace;

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
	gorilla_airborne_brace = PM_GorillaGroundContact() && !pmove.onground &&
		DotProduct(pmove.gravitydir, pmove.velocity) <= 0 &&
		(pmove.gorilla_braced || pmove.gorilla.touching ||
		 (pmove.cmd.vr_gorilla_motion.flags & VR_GORILLA_MOTION_BRACED));
	VectorAdd(pmove.origin, pmove.gravitydir, point);
	trace.startsolid = trace.allsolid = true;
	VectorClear(trace.endpos);
	if (-DotProduct(pmove.gravitydir, pmove.velocity) > 180 ||
		(PM_GorillaGroundContact() && -DotProduct(pmove.gravitydir, pmove.velocity) > .01f))
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

		/* Retain a real hand-created lift across command boundaries, but a
		 * stationary grounded brace still follows ordinary support snapping.
		 * A trace starting at support (fraction zero) must ground us again. */
		if (!trace.startsolid && (trace.fraction == 1 || -DotProduct(pmove.gravitydir, trace.plane.normal) < MIN_STEP_NORMAL ||
			(gorilla_airborne_brace && trace.fraction > .0001f)))
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
		pmove.fluid_contacted = true;
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
	pmove.velocity[2] = PM_VANILLA_WATERJUMP_VELOCITY * PM_VRJumpScale ();
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
/*
====================
PM_ApplyVRRoomScaleMove

Room-scale tracking is an external horizontal displacement, not locomotion.
Move it through the same hull and step-slide code as walking while restoring
the locomotion velocity afterwards.  A large tracker jump is ignored in its
entirety rather than clipped to the maximum, which avoids turning a bad sample
into an apparent player movement.
====================
*/
static void PM_ApplyVRRoomScaleMove (float command_seconds)
{
	vec3_t	move;
	vec3_t	velocity;
	float	saved_frametime;
	float	horizontal_length;
	float	sweep_seconds = 1.0f;
	float	velocity_scale = 1.0f;
	float	saved_jump_secs;
	float	saved_waterjumptime;
	qboolean saved_jump_held;

	if (!pmove.cmd.vr_active || pmove.pm_type == PM_DEAD ||
		pmove.pm_type == PM_NONE || pmove.pm_type == PM_FREEZE)
		return;
	/* Private commands carry an explicit accepted duration. Keep the legacy
	 * displacement-as-velocity behavior for callers without that duration. */
	if (pmove.cmd.msec)
	{
		if (pmove.cmd.msec > 125 || !isfinite(command_seconds) ||
			command_seconds <= 0)
			return;
		sweep_seconds = command_seconds;
		velocity_scale = 1.0f / sweep_seconds;
		if (!isfinite(velocity_scale))
			return;
	}

	VectorCopy (pmove.cmd.vr_roomscalemove, move);
	horizontal_length = sqrtf(move[0]*move[0] + move[1]*move[1]);

	/* Tracking data is expected to be horizontal; reject malformed/outlier
	 * samples instead of accepting a partial vertical teleport. */
	if (fabsf(move[2]) > PM_VR_ROOMSCALE_MAX_DELTA ||
		horizontal_length > PM_VR_ROOMSCALE_MAX_DELTA)
	{
		Con_DPrintf ("PMove: ignored VR room-scale tracking outlier (%g %g %g)\n",
			move[0], move[1], move[2]);
		return;
	}
	if (horizontal_length == 0)
		return;
	move[2] = 0;

	VectorCopy (pmove.velocity, velocity);
	saved_frametime = frametime;
	saved_jump_secs = pmove.jump_secs;
	saved_waterjumptime = pmove.waterjumptime;
	saved_jump_held = pmove.jump_held;
	frametime = sweep_seconds;
	VectorScale (move, velocity_scale, pmove.velocity);
	/* Tracking must obey the same airborne step policy as locomotion. */
	if (pmove.onground || pmove.waterlevel >= 2 || pmove.pm_type == PM_FLY ||
		pmove.pm_type == PM_6DOF || pmove.onladder || pmove.waterjumptime ||
		VectorLength (velocity) > 1000)
		PM_StepSlideMove (false);
	else if (movevars.airstep)
		PM_StepSlideMove (true);
	else
		PM_SlideMove ();
	VectorCopy (velocity, pmove.velocity);
	pmove.jump_secs = saved_jump_secs;
	pmove.waterjumptime = saved_waterjumptime;
	pmove.jump_held = saved_jump_held;
	frametime = saved_frametime;
}

static void PM_GorillaCheckLift (const vec3_t previous_origin)
{
	vec3_t displacement;
	VectorSubtract(pmove.origin, previous_origin, displacement);
	if (PM_GorillaGroundContact() && DotProduct(displacement, pmove.gravitydir) < -.0001f)
		pmove.onground = false;
}

static void PM_PlayerMoveStep (float gamespeed, qboolean apply_roomscale,
	qboolean prepare_gorilla, float command_seconds)
{
//	int i;
//	int tmp;	//for rounding

	frametime = pmove.cmd.seconds * gamespeed;

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
		if (apply_roomscale && pmove.cmd.vr_active)
		{
			PM_CategorizePosition ();
			PM_ApplyVRRoomScaleMove (command_seconds);
		}
		PM_SpectatorMove ();
		pmove.onground = false;
		return;
	}

	PM_NudgePosition ();

	// set onground, watertype, and waterlevel
	PM_CategorizePosition ();
	if (apply_roomscale)
	{
		PM_ApplyVRRoomScaleMove (command_seconds);
		PM_CategorizePosition ();
	}
	/* The selected server evaluates this before QuakeC. Its later PMove pass
	 * must retain any velocity authored by PreThink or weapon Think. Replay
	 * evaluates the same input once, before Gorilla and native friction. */
	if (prepare_gorilla && !pmove.vr_instant_stop_preapplied &&
		PM_VRInstantStopEligible ())
	{
		pmove.velocity[0] = 0.0f;
		pmove.velocity[1] = 0.0f;
	}

	/* Only actual ladder contact restores sticks. Water retains its native
	 * drag/timers while physical palms can still push solid surfaces. */
	if (prepare_gorilla && !pmove.gorilla_prepared)
	{
		vec3_t hand_origin, hand_velocity;
		qboolean hand_launched = false;
		VectorCopy(pmove.origin, hand_origin);
		VectorCopy(pmove.velocity, hand_velocity);
		pmove.gorilla_braced = false;
		pmove.gorilla_swim_stroke = false;
		pmove.gorilla_contact[0] = pmove.gorilla_contact[1] = -1;
		if (pmove.gorilla_allowed && pmove.cmd.vr_active &&
			(pmove.pm_type == PM_NORMAL || pmove.pm_type == PM_FLY) &&
			!pmove.onladder &&
			(pmove.cmd.vr_gorilla_motion.flags & VR_GORILLA_MOTION_ACTIVE))
		{
			const vr_gorilla_motion_t *motion = &pmove.cmd.vr_gorilla_motion;
			/* The command owner already solved its hand constraints. Reuse
			 * body clipping, then let ordinary physics apply native forces.
			 * Unfulfilled displacement is discarded, never banked as debt. */
			VRG_Reset(&pmove.gorilla);
			pmove.gorilla.initialized = 1;
			VRG_MoveBody(NULL, PM_GorillaTrace, pmove.origin, motion->displacement);
			if (!pmove.waterjumptime)
				VectorAdd(pmove.velocity, motion->impulse, pmove.velocity);
			hand_launched = (motion->flags & VR_GORILLA_MOTION_LAUNCHED) != 0;
			pmove.gorilla_braced = !pmove.waterjumptime &&
				(motion->flags & VR_GORILLA_MOTION_BRACED) != 0;
			PM_GorillaCheckLift(hand_origin);
			PM_CategorizePosition();
			pmove.gorilla_swim_stroke = pmove.waterlevel >= 2 &&
				!pmove.waterjumptime && (motion->flags & VR_GORILLA_MOTION_SWIM);
			for (int hand = 0; hand < 2; ++hand)
				for (int index = 0; index < pmove.numphysent; ++index)
					if (motion->contact[hand] >= 0 &&
						abs(pmove.physents[index].info) == motion->contact[hand] &&
						pmove.physents[index].modelindex == motion->contact_model[hand])
						pmove.gorilla_contact[hand] = motion->contact[hand];
		}
		else if (pmove.gorilla_allowed &&
			(pmove.pm_type == PM_NORMAL || pmove.pm_type == PM_FLY) &&
			!pmove.onladder &&
			VRG_InputValid(&pmove.cmd.vr_gorilla))
		{
			vec3_t native_velocity;
			VectorCopy(pmove.velocity, native_velocity);
			vr_gorilla_result_t result = VRG_Step(&pmove.gorilla,
				&pmove.cmd.vr_gorilla, pmove.origin, pmove.velocity,
				command_seconds * gamespeed,
				movevars.gravity * movevars.entgravity, NULL, PM_GorillaTrace,
				PM_GorillaSurface);
			if (pmove.waterjumptime || (pmove.waterlevel >= 2 && !result.launched)) {
				VectorCopy(native_velocity, pmove.velocity);
				result.braced = false;
			}
			pmove.gorilla_braced = result.braced;
			hand_launched = result.launched;
			pmove.gorilla_contact[0] = result.contact[0];
			pmove.gorilla_contact[1] = result.contact[1];
			PM_GorillaCheckLift(hand_origin);
			PM_CategorizePosition();
			if (result.stepped && pmove.waterlevel >= 2 && !pmove.waterjumptime) {
				unsigned int liquid = 0, solid = 0;
				vec3_t palm;
				for (int hand = 0; hand < 2; ++hand) {
					VectorAdd(pmove.origin, pmove.cmd.vr_gorilla.hand[hand], palm);
					if (PM_PointContents(palm) & CONTENTBITS_FLUID)
						liquid |= 1u << hand;
					if (result.contact[hand] >= 0)
						solid |= 1u << hand;
				}
				pmove.gorilla_swim_stroke = VRG_SwimImpulse(&pmove.cmd.vr_gorilla,
					liquid, solid, command_seconds * gamespeed,
					movevars.maxspeed * .7f, pmove.velocity);
			}
		}
		else
			VRG_Reset(&pmove.gorilla);
		if (pmove.gorilla_authoring)
		{
			vr_gorilla_motion_t *motion = &pmove.gorilla_authored_motion;
			memset(motion, 0, sizeof(*motion));
			motion->flags = (pmove.gorilla.initialized ? VR_GORILLA_MOTION_ACTIVE : 0) |
				(pmove.gorilla_braced ? VR_GORILLA_MOTION_BRACED : 0) |
				(pmove.gorilla_swim_stroke ? VR_GORILLA_MOTION_SWIM : 0) |
				(hand_launched ? VR_GORILLA_MOTION_LAUNCHED : 0);
			VectorSubtract(pmove.origin, hand_origin, motion->displacement);
			VectorSubtract(pmove.velocity, hand_velocity, motion->impulse);
			for (int hand = 0; hand < 2; ++hand)
			{
				motion->contact[hand] = pmove.gorilla_contact[hand];
				for (int index = 0; index < pmove.numphysent; ++index)
					if (motion->contact[hand] >= 0 &&
						abs(pmove.physents[index].info) == motion->contact[hand])
						motion->contact_model[hand] = pmove.physents[index].modelindex;
			}
		}
	}
	if (pmove.gorilla_allowed && pmove.gorilla.initialized &&
		(pmove.pm_type == PM_NORMAL || pmove.pm_type == PM_FLY) && !pmove.onladder)
	{
		pmove.cmd.forwardmove = pmove.cmd.sidemove = pmove.cmd.upmove = 0;
	}

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

/* Apply the existing PMove room-scale sweep without running a movement step.
 * The command owner handles entity save/link and touch dispatch around QC. */
void PM_ApplyPreThinkRoomScale (void)
{
	vec3_t saved_forward, saved_right, saved_up, saved_angles;

	PM_EnsureInitialized ();
	if (!pmove.cmd.vr_active ||
		(!pmove.cmd.vr_roomscalemove[0] && !pmove.cmd.vr_roomscalemove[1]) ||
		pmove.pm_type == PM_NONE || pmove.pm_type == PM_FREEZE ||
		pmove.pm_type == PM_DEAD)
		return;

	VectorCopy (forward, saved_forward);
	VectorCopy (right, saved_right);
	VectorCopy (up, saved_up);
	VectorCopy (pmove.angles, saved_angles);
	VectorCopy (pmove.cmd.viewangles, pmove.angles);
	AngleVectors (pmove.angles, forward, right, up);

	if (pmove.pm_type == PM_SPECTATOR || pmove.pm_type == PM_OLD_SPECTATOR)
	{
		PM_CategorizePosition ();
		PM_ApplyVRRoomScaleMove (pmove.cmd.seconds);
		goto done;
	}

	PM_NudgePosition ();
	PM_CategorizePosition ();
	PM_ApplyVRRoomScaleMove (pmove.cmd.seconds);
	PM_CategorizePosition ();

done:
	VectorCopy (saved_angles, pmove.angles);
	VectorCopy (saved_forward, forward);
	VectorCopy (saved_right, right);
	VectorCopy (saved_up, up);
}

void PM_PlayerMove (float gamespeed)
{
	usercmd_t	cmd;
	float		seconds;
	float		step_seconds;
	int			steps;
	int			i;

	PM_EnsureInitialized ();
	pmove.fluid_contacted = false;
	memset(&pmove.gorilla_authored_motion, 0, sizeof(pmove.gorilla_authored_motion));
	pmove.numtouch = 0;
	pmove.gorilla_contact[0] = pmove.gorilla_contact[1] = -1;
	if (!pmove.gorilla_prepared) {
		pmove.gorilla_braced = false;
		pmove.gorilla_swim_stroke = false;
	}
	if (pmove.pm_type != PM_NORMAL && pmove.pm_type != PM_FLY)
		VRG_Reset(&pmove.gorilla);
	cmd = pmove.cmd;
	seconds = cmd.seconds;

	/* Offline/legacy callers do not carry the negotiated duration byte. Keep
	 * their original single-step semantics completely unchanged. */
	if (!cmd.msec)
	{
		PM_PlayerMoveStep (gamespeed, false, true, seconds);
		if (pmove.gorilla.initialized)
			VectorCopy(pmove.origin, pmove.gorilla.origin);
		pmove.cmd = cmd;
		return;
	}

	/* Keep a zero-length command's legacy categorization behavior. */
	if (seconds <= 0)
	{
		PM_PlayerMoveStep (gamespeed, true, true, seconds);
		if (pmove.gorilla.initialized)
			VectorCopy(pmove.origin, pmove.gorilla.origin);
		pmove.cmd = cmd;
		return;
	}

	steps = (int)ceilf(seconds / PM_MAX_SUBSTEP_SECONDS);
	if (steps < 1)
		steps = 1;
	step_seconds = seconds / steps;
	for (i = 0; i < steps; i++)
	{
		pmove.cmd = cmd;
		pmove.cmd.seconds = step_seconds;
		PM_PlayerMoveStep (gamespeed, i == 0, i == 0, seconds);
	}
	if (pmove.gorilla.initialized)
		VectorCopy(pmove.origin, pmove.gorilla.origin);

	/* PM_PlayerMove historically leaves the caller's command untouched. */
	pmove.cmd = cmd;
}

static void PM_DecodeSolidSize (unsigned int solidsize, vec3_t mins, vec3_t maxs)
{
	maxs[0] = maxs[1] = solidsize & 255;
	mins[0] = mins[1] = -maxs[0];
	mins[2] = -(int)((solidsize >> 8) & 255);
	maxs[2] = (int)((solidsize >> 16) & 65535) - 32768;
}

/* A read-only scene query for tracked weapons, not a movement simulation.
 * Brushes use their linked pose, boxes their received collision hull (which
 * need not exactly match an alias model's later step-animation interpolation).
 * Main-thread only: no QC, callbacks or yields while box scratch is borrowed. */
cl_weapon_trace_t CL_TraceWeapon (const vec3_t start, const vec3_t end)
{
	cl_weapon_trace_t result;
	trace_t trace;
	vec3_t a, b, mins, maxs;
	float saved_dist[6];
	int i;

	memset (&result, 0, sizeof(result));
	result.fraction = 1;
	result.entity = -1;
	VectorCopy (end, result.endpos);
	if (!cl.worldmodel || cl.worldmodel->needload)
		return result;
	for (i = 0; i < 3; i++)
		if (!isfinite(start[i]) || !isfinite(end[i]))
			return result;
	VectorCopy (start, a);
	VectorCopy (end, b);
	PM_EnsureInitialized ();
	for (i = 0; i < 6; i++)
		saved_dist[i] = box_planes[i].dist;
	for (i = 0; i < cl.num_entities || i == 0; i++)
	{
		qmodel_t *model = cl.worldmodel;
		vec3_t origin = {0, 0, 0}, angles = {0, 0, 0};
		if (i)
		{
			entity_t *ent;
			unsigned int solid;
			if (!cl.entities || i == cl.viewentity)
				continue;
			ent = &cl.entities[i];
			solid = ent->netstate.solidsize;
			/* Replacement updates clear solids on removal. Legacy snapshots
			 * may retain old fields, so message presence is required as well. */
			if (solid == ES_SOLID_NOT || ent->msgtime != cl.mtime[0])
				continue;
			if (cl.gametype == GAME_COOP && i <= cl.maxclients &&
				(cl.stats[STAT_VR_COOP_POLICY] & VR_COOP_POLICY_NO_PLAYER_CLIP))
				continue;
			if (solid == ES_SOLID_BSP)
			{
				model = ent->model;
				if (!model || model->needload || model->type != mod_brush)
					continue;
				switch ((signed char)ent->netstate.skin)
				{
				case CONTENTS_WATER: case CONTENTS_SLIME: case CONTENTS_LAVA:
				case CONTENTS_LADDER:
					continue;
				default:
					break;
				}
				VectorCopy (ent->origin, origin);
				VectorCopy (ent->angles, angles);
			}
			else
			{
				model = NULL;
				PM_DecodeSolidSize (solid, mins, maxs);
				PM_HullForBox (mins, maxs);
				VectorCopy (ent->netstate.origin, origin);
			}
		}
		if (!PM_TransformedHullCheck(model, a, b, vec3_origin, vec3_origin,
			&trace, origin, angles))
			continue;
		if (trace.allsolid || trace.startsolid)
		{
			trace.startsolid = true;
			trace.fraction = 0;
			VectorCopy (a, trace.endpos);
			VectorClear (trace.plane.normal);
		}
		if (trace.fraction < result.fraction ||
			(trace.startsolid && !result.startsolid))
		{
			result.startsolid = trace.startsolid;
			result.allsolid = trace.allsolid;
			result.fraction = trace.fraction;
			result.entity = i;
			VectorCopy (trace.endpos, result.endpos);
			VectorCopy (trace.plane.normal, result.normal);
		}
	}
	for (i = 0; i < 6; i++)
		box_planes[i].dist = saved_dist[i];
	return result;
}

/* Adapted from ../quakespasm-openvr/Quake/vr.c:7196-7253, the donor's
 * stateless two-stage tracked-weapon resolver.
 * Stage one retracts the grip and muzzle endpoints from obstructions; stage
 * two verifies that the translated body, grip, shaft and edge all fit. This
 * only queries the client scene and never changes player movement state. */
qboolean CL_ResolveWeaponCollision (const vec3_t torso, const vec3_t grip,
	const vec3_t base, const vec3_t tip, vec3_t delta)
{
	vec3_t resolved_grip, endpoints[2], extra = {0, 0, 0};
	vec3_t body_to_grip, body_to_resolved_grip;
	float greatest = 0, original_reach2, resolved_reach2;
	cl_weapon_trace_t trace;

	VectorCopy (vec3_origin, delta);
	for (int axis = 0; axis < 3; ++axis)
		if (!isfinite (torso[axis]) || !isfinite (grip[axis]) ||
			!isfinite (base[axis]) || !isfinite (tip[axis]))
			return false;

	trace = CL_TraceWeapon (torso, grip);
	if (trace.startsolid || trace.allsolid)
		return false;
	VectorCopy (trace.endpos, resolved_grip);
	VectorSubtract (resolved_grip, grip, delta);
	VectorAdd (base, delta, endpoints[0]);
	VectorAdd (tip, delta, endpoints[1]);

	for (int point = 0; point < 2; ++point)
	{
		vec3_t correction;
		trace = CL_TraceWeapon (resolved_grip, endpoints[point]);
		if (trace.startsolid || trace.allsolid)
			goto unresolved;
		VectorSubtract (trace.endpos, endpoints[point], correction);
		const float length2 = DotProduct (correction, correction);
		if (length2 > greatest)
		{
			greatest = length2;
			VectorCopy (correction, extra);
		}
	}

	VectorAdd (delta, extra, delta);
	VectorAdd (grip, delta, resolved_grip);
	VectorAdd (base, delta, endpoints[0]);
	VectorAdd (tip, delta, endpoints[1]);
	VectorSubtract (grip, torso, body_to_grip);
	original_reach2 = DotProduct (body_to_grip, body_to_grip);
	VectorSubtract (resolved_grip, torso, body_to_resolved_grip);
	resolved_reach2 = DotProduct (body_to_resolved_grip, body_to_resolved_grip);
	/* A calibrated muzzle may sit behind or below the torso anchor. Never
	 * retract the held pose farther from that anchor than its raw grip. */
	if (resolved_reach2 > original_reach2 +
		1e-5f * fmaxf (1.0f, original_reach2))
		goto unresolved;

	/* Retraction can push the grip or shaft into a rear wall. Do not slide the
	 * player or iterate an impossible pose into an apparent clear result. */
	for (int segment = 0; segment < 4; ++segment)
	{
		trace = CL_TraceWeapon (
			segment == 0 ? torso : segment == 3 ? endpoints[0] : resolved_grip,
			segment == 0 ? resolved_grip : segment == 1 ? endpoints[0] : endpoints[1]);
		if (trace.startsolid || trace.allsolid || trace.fraction < 1)
			goto unresolved;
	}
	return true;

unresolved:
	VectorCopy (vec3_origin, delta);
	return false;
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

void PMCL_AddEntities (vec3_t boxminmax[2])
{
	entity_t	*touch;
	physent_t	*phys;
	vec3_t		mins, maxs;
	int			i;

	PM_EnsureInitialized ();

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
		if (cl.gametype == GAME_COOP &&
			(cl.stats[STAT_VR_COOP_POLICY] & VR_COOP_POLICY_NO_PLAYER_CLIP) &&
			i <= cl.maxclients)
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
		phys->modelindex = phys->model ? touch->netstate.modelindex : 0;
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

static float PMCL_GetKeyValue (const char *key, float fallback)
{
	char buf[sizeof(cl.serverinfo)];
	char *end;
	const char *value;
	float parsed;

	value = Info_GetKey (cl.serverinfo, key, buf, sizeof(buf));
	if (!*value)
		return fallback;
	parsed = strtof (value, &end);
	if (end == value || *end || !isfinite(parsed))
		return fallback;
	return parsed;
}

static int PMCL_GetKeyInteger (const char *key, int fallback)
{
	float value = PMCL_GetKeyValue (key, fallback);
	if ((double)value < INT_MIN || (double)value > INT_MAX)
		return fallback;
	return (int)value;
}

void PMCL_ServerinfoUpdated (void)
{
	PM_EnsureInitialized ();
	memset (&clmovevars, 0, sizeof(clmovevars));
	clmovevars.accelerate = PMCL_GetKeyValue ("sv_accelerate", 10);
	clmovevars.airaccelerate = PMCL_GetKeyValue ("sv_airaccelerate", 10);
	clmovevars.friction = PMCL_GetKeyValue ("sv_friction", 4);
	clmovevars.gravity = PMCL_GetKeyValue ("sv_gravity", 800);
	clmovevars.stopspeed = PMCL_GetKeyValue ("sv_stopspeed", 100);
	clmovevars.wateraccelerate = PMCL_GetKeyValue ("sv_wateraccelerate", 10);
	clmovevars.waterfriction = PMCL_GetKeyValue ("sv_waterfriction", 4);
	clmovevars.entgravity = 1.0f;
	clmovevars.maxspeed = PMCL_GetKeyValue ("sv_maxspeed", 320);
	clmovevars.spectatormaxspeed = PMCL_GetKeyValue ("sv_spectatormaxspeed", 500);
	clmovevars.bunnyspeedcap = PMCL_GetKeyValue ("pm_bunnyspeedcap", 0);
	clmovevars.ktjump = PMCL_GetKeyValue ("pm_ktjump", 0);
	clmovevars.airstep = PMCL_GetKeyValue ("pm_airstep", 0) != 0;
	clmovevars.stepheight = PMCL_GetKeyInteger ("pm_stepheight", 18);
	clmovevars.stepdown = PMCL_GetKeyValue ("pm_stepdown", 0) != 0;
	clmovevars.walljump = PMCL_GetKeyInteger ("pm_walljump", 0);
	clmovevars.slidefix = PMCL_GetKeyValue ("pm_slidefix", 0) != 0;
	clmovevars.pground = PMCL_GetKeyValue ("pm_pground", 0) != 0;
	clmovevars.slidyslopes = PMCL_GetKeyValue ("pm_slidyslopes", 0) != 0;
	clmovevars.autobunny = PMCL_GetKeyValue ("pm_autobunny", 0) != 0;
	clmovevars.bunnyfriction = PMCL_GetKeyValue ("pm_bunnyfriction", 0) != 0;
	clmovevars.watersinkspeed = PMCL_GetKeyValue ("*pm_watersinkspeed", 60);
	clmovevars.flyfriction = PMCL_GetKeyValue ("*pm_flyfriction", 4);
	clmovevars.edgefriction = PMCL_GetKeyValue ("*pm_edgefriction", 2);
	clmovevars.protocolflags = cl.protocolflags;
	clmovevars.flags = MOVEFLAG_VALID | MOVEFLAG_NOGRAVITYONGROUND |
		(PMCL_GetKeyValue ("pm_edgefriction", -1000) != -1000 ? 0 : MOVEFLAG_QWEDGEBOX);
	clmovevars.jumpspeed = PM_VANILLA_JUMP_VELOCITY;
	clmovevars.maxairspeed = 30;
	clmovevars_valid = true;
}

void PMCL_ClearMoveVars (void)
{
	clmovevars_valid = false;
}

qboolean PMCL_SetMoveVars (void)
{
	static const int shared_stats[] = {
		STAT_MOVEVARS_STEPHEIGHT, STAT_MOVEVARS_GRAVITY, STAT_MOVEVARS_STOPSPEED,
		STAT_MOVEVARS_MAXSPEED, STAT_MOVEVARS_SPECTATORMAXSPEED, STAT_MOVEVARS_ACCELERATE,
		STAT_MOVEVARS_AIRACCELERATE, STAT_MOVEVARS_WATERACCELERATE, STAT_MOVEVARS_FRICTION,
		STAT_MOVEVARS_WATERFRICTION, STAT_MOVEVARS_EDGEFRICTION, STAT_MOVEVARS_ENTGRAVITY,
		STAT_MOVEVARS_JUMPVELOCITY, STAT_MOVEVARS_MAXAIRSPEED};
	static const int private_stats[] = {
		STAT_MOVEVARS_WATERSINKSPEED, STAT_MOVEVARS_FLYFRICTION,
		STAT_MOVEVARS_BUNNYSPEEDCAP, STAT_MOVEVARS_KTJUMP};
	qboolean private_move = cl.protocol_qsvr == QSVR_PROTOCOL_PINNED;
	unsigned int i;

	PM_EnsureInitialized ();
	if (!clmovevars_valid)
		PMCL_ServerinfoUpdated ();
	movevars = clmovevars;
	movevars.protocolflags = cl.protocolflags;
	if (cl.protocol_qsvr && (!private_move ||
		(cl.protocol_pext2 & QSVR_PEXT2_REQUIRED) != QSVR_PEXT2_REQUIRED))
		return false;
	if (private_move && !(cl.stats[STAT_MOVEFLAGS] & MOVEFLAG_VALID))
		return false;
	if ((cl.protocol_pext2 & PEXT2_PREDINFO) && (cl.stats[STAT_MOVEFLAGS] & MOVEFLAG_VALID))
	{
		for (i = 0; i < countof(shared_stats); i++)
			if (!isfinite(cl.statsf[shared_stats[i]]))
				return false;
		if ((double)cl.statsf[STAT_MOVEVARS_STEPHEIGHT] < INT_MIN ||
			(double)cl.statsf[STAT_MOVEVARS_STEPHEIGHT] > INT_MAX)
			return false;
		if (private_move)
			for (i = 0; i < countof(private_stats); i++)
				if (!isfinite(cl.statsf[private_stats[i]]))
					return false;

		movevars.stepheight = cl.statsf[STAT_MOVEVARS_STEPHEIGHT];
		movevars.flags = cl.stats[STAT_MOVEFLAGS];
		if (!private_move || !cl.vr_instant_stop_supported ||
			!cl.vr_instant_stop_cap_sent)
			movevars.flags &= ~MOVEFLAG_VR_INSTANT_STOP;
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
		if (private_move)
		{
			movevars.watersinkspeed = cl.statsf[STAT_MOVEVARS_WATERSINKSPEED];
			movevars.flyfriction = cl.statsf[STAT_MOVEVARS_FLYFRICTION];
			movevars.bunnyspeedcap = cl.statsf[STAT_MOVEVARS_BUNNYSPEEDCAP];
			movevars.ktjump = cl.statsf[STAT_MOVEVARS_KTJUMP];
			PM_UnpackMoveFlags (&movevars);
			if (movevars.entgravity <= 0)
				movevars.entgravity = 1.0f;
		}
	}
	if (!private_move)
	{
		cl.vr_instant_stop_policy_seen = false;
		cl.vr_instant_stop_resume_ack = 0;
	}
	else
	{
		const qboolean enabled = (movevars.flags & MOVEFLAG_VR_INSTANT_STOP) != 0;
		if (!cl.vr_instant_stop_policy_seen)
		{
			cl.vr_instant_stop_policy_seen = true;
			cl.vr_instant_stop_policy = enabled;
			cl.vr_instant_stop_resume_ack = enabled && cl.movemessages > 0 ?
				cl.movemessages - 1 : 0;
		}
		else if (enabled != cl.vr_instant_stop_policy)
		{
			cl.vr_instant_stop_policy = enabled;
			cl.vr_instant_stop_resume_ack = cl.movemessages > 0 ?
				cl.movemessages - 1 : 0;
		}
		/* Do not replay older outstanding commands with a newly received
		 * server rule. Resume once their authoritative ACK has arrived. */
		if (cl.ackedmovemessages < cl.vr_instant_stop_resume_ack)
			return false;
	}
	return true;
}

extern cvar_t sv_gravity;
extern cvar_t sv_stopspeed;
extern cvar_t sv_maxspeed;
extern cvar_t sv_accelerate;
extern cvar_t sv_friction;
extern cvar_t sv_edgefriction;

static unsigned int PM_PackMoveFlags (const movevars_t *mv)
{
	unsigned int flags = mv->flags;
	int walljump = mv->walljump;
	const unsigned int pmflags = MOVEFLAG_PM_SLIDEFIX | MOVEFLAG_PM_AIRSTEP |
		MOVEFLAG_PM_PGROUND | MOVEFLAG_PM_STEPDOWN | MOVEFLAG_PM_SLIDYSLOPES |
		MOVEFLAG_PM_AUTOBUNNY | MOVEFLAG_PM_BUNNYFRICTION |
		MOVEFLAG_PM_WALLJUMP_MASK;

	if (walljump < 0)
		walljump = 0;
	if (walljump > 3)
		walljump = 3;
	flags &= ~pmflags;

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
	flags |= (unsigned int)walljump << MOVEFLAG_PM_WALLJUMP_SHIFT;
	return flags;
}

static qboolean PM_MoveVarsFinite (const movevars_t *mv)
{
	return isfinite (mv->gravity) && isfinite (mv->stopspeed) &&
		isfinite (mv->maxspeed) && isfinite (mv->spectatormaxspeed) &&
		isfinite (mv->maxairspeed) && isfinite (mv->accelerate) &&
		isfinite (mv->airaccelerate) && isfinite (mv->wateraccelerate) &&
		isfinite (mv->friction) && isfinite (mv->waterfriction) &&
		isfinite (mv->flyfriction) && isfinite (mv->entgravity) &&
		isfinite (mv->bunnyspeedcap) && isfinite (mv->watersinkspeed) &&
		isfinite (mv->ktjump) && isfinite (mv->edgefriction) &&
		isfinite (mv->jumpspeed);
}

qboolean PMSV_BuildMoveVars (movevars_t *out, edict_t *player, unsigned int protocolflags)
{
	movevars_t vars;
	eval_t *entgravity;
	float stepheight;

	if (!out)
		return false;

	memset (&vars, 0, sizeof(vars));
	vars.gravity = sv_gravity.value;
	vars.stopspeed = sv_stopspeed.value;
	vars.maxspeed = sv_maxspeed.value;
	vars.spectatormaxspeed = 500.0f;
	vars.maxairspeed = 30.0f;
	vars.accelerate = sv_accelerate.value;
	vars.airaccelerate = sv_accelerate.value;
	vars.wateraccelerate = sv_accelerate.value;
	vars.friction = sv_friction.value;
	vars.waterfriction = 4.0f;
	vars.flyfriction = 4.0f;
	vars.entgravity = 1.0f;
	vars.bunnyspeedcap = 0.0f;
	vars.watersinkspeed = 60.0f;
	vars.ktjump = 0.0f;
	vars.edgefriction = sv_edgefriction.value;
	vars.jumpspeed = PM_VANILLA_JUMP_VELOCITY;
	vars.stepheight = 18;
	/* QSS-M's server defaults, supplied to private clients as movement stats. */
	vars.slidefix = true;
	vars.bunnyfriction = true;
	vars.protocolflags = protocolflags;
	vars.flags = MOVEFLAG_VALID | MOVEFLAG_NOGRAVITYONGROUND;

	if (player && qcvm)
	{
		entgravity = GetEdictFieldValue (player, qcvm->extfields.gravity);
		if (entgravity && entgravity->_float != 0.0f)
			vars.entgravity = entgravity->_float;
	}
	if (vars.entgravity <= 0.0f)
		vars.entgravity = 1.0f;

	if (!PM_MoveVarsFinite (&vars))
		return false;
	stepheight = (float)vars.stepheight;
	if (!isfinite (stepheight) || (double)stepheight != (double)vars.stepheight)
		return false;

	*out = vars;
	return true;
}

qboolean PMSV_ExportMoveStats (const movevars_t *vars, float *fstat, int *istat)
{
	unsigned int flags;
	float stepheight;

	if (!vars || !fstat || !istat || !(vars->flags & MOVEFLAG_VALID) ||
		!PM_MoveVarsFinite (vars))
		return false;
	stepheight = (float)vars->stepheight;
	if (!isfinite (stepheight) || (double)stepheight != (double)vars->stepheight)
		return false;

	flags = PM_PackMoveFlags (vars);
	fstat[STAT_MOVEVARS_STEPHEIGHT] = stepheight;
	fstat[STAT_MOVEVARS_GRAVITY] = vars->gravity;
	fstat[STAT_MOVEVARS_STOPSPEED] = vars->stopspeed;
	fstat[STAT_MOVEVARS_MAXSPEED] = vars->maxspeed;
	fstat[STAT_MOVEVARS_SPECTATORMAXSPEED] = vars->spectatormaxspeed;
	fstat[STAT_MOVEVARS_ACCELERATE] = vars->accelerate;
	fstat[STAT_MOVEVARS_AIRACCELERATE] = vars->airaccelerate;
	fstat[STAT_MOVEVARS_WATERACCELERATE] = vars->wateraccelerate;
	fstat[STAT_MOVEVARS_FRICTION] = vars->friction;
	fstat[STAT_MOVEVARS_WATERFRICTION] = vars->waterfriction;
	fstat[STAT_MOVEVARS_EDGEFRICTION] = vars->edgefriction;
	fstat[STAT_MOVEVARS_ENTGRAVITY] = vars->entgravity;
	fstat[STAT_MOVEVARS_TIMESCALE] = 1.0f;
	fstat[STAT_MOVEVARS_JUMPVELOCITY] = vars->jumpspeed;
	fstat[STAT_MOVEVARS_MAXAIRSPEED] = vars->maxairspeed;
	fstat[STAT_MOVEVARS_WATERSINKSPEED] = vars->watersinkspeed;
	fstat[STAT_MOVEVARS_FLYFRICTION] = vars->flyfriction;
	fstat[STAT_MOVEVARS_BUNNYSPEEDCAP] = vars->bunnyspeedcap;
	fstat[STAT_MOVEVARS_KTJUMP] = vars->ktjump;
	memcpy (&istat[STAT_MOVEFLAGS], &flags, sizeof(istat[STAT_MOVEFLAGS]));
	return true;
}
