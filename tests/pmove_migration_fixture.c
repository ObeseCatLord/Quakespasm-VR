/* Exercise transplanted movement against donor hull collision, not a mock trace.
 * This verifies the shared solver, not command transport or gameplay parity. */
#include "../Quake/pmove.c"
#include <stdarg.h>
#include <assert.h>

cvar_t			sv_accelerate, sv_edgefriction, sv_friction, sv_gravity, sv_maxspeed, sv_stopspeed;
cvar_t			sv_vr_jump_velocity;
cvar_t			pr_checkextension;
extern cvar_t	sv_fte_recursivehullckeck;
void			Con_DPrintf (const char *fmt, ...) {}
static qboolean tracked_session;
qboolean		V_TrackedSessionActive (void)
{
	return tracked_session;
}
void Sys_Error (const char *fmt, ...)
{
	va_list args;
	va_start (args, fmt);
	vfprintf (stderr, fmt, args);
	va_end (args);
	abort ();
}
static qmodel_t	   floor_model;
static mplane_t	   floor_planes[2];
static mclipnode_t floor_nodes[2];

static void near_value (float actual, float expected, float tolerance)
{
	if (!isfinite (actual) || !isfinite (expected) || fabsf (actual - expected) > tolerance)
	{
		fprintf (stderr, "movement mismatch: %.6f expected %.6f +/- %.6f\n", actual, expected, tolerance);
		abort ();
	}
}
static void prepare (void)
{
	memset (&pmove, 0, sizeof pmove);
	memset (&movevars, 0, sizeof movevars);
	movevars.gravity = 800;
	movevars.entgravity = 1;
	movevars.maxspeed = 320;
	movevars.maxairspeed = 30;
	movevars.accelerate = movevars.airaccelerate = movevars.wateraccelerate = 10;
	movevars.friction = movevars.waterfriction = movevars.flyfriction = 4;
	movevars.stopspeed = 100;
	movevars.edgefriction = 2;
	movevars.stepheight = 18;
	movevars.jumpspeed = 270;
	movevars.bunnyfriction = movevars.slidefix = true;
	movevars.flags = MOVEFLAG_VALID | MOVEFLAG_NOGRAVITYONGROUND;
	pmove.pm_type = PM_NORMAL;
	pmove.skipent = -1;
	VectorSet (pmove.player_mins, -16, -16, -24);
	VectorSet (pmove.player_maxs, 16, 16, 32);
	pmove.origin[2] = 24;
	pmove.numphysent = 1;
	pmove.physents[0].model = &floor_model;
	pmove.physents[0].info = 0;
	pmove.cmd.msec = 100;
	pmove.cmd.seconds = .1f;
}
int main (void)
{
	floor_model.type = mod_brush;
	VectorSet (floor_model.mins, -4096, -4096, -4096);
	VectorSet (floor_model.maxs, 4096, 4096, 4096);
	for (int i = 0; i < 2; ++i)
	{
		floor_planes[i].normal[2] = 1;
		floor_planes[i].type = 2;
		floor_planes[i].dist = i ? 24 : 0;
		floor_nodes[i].children[0] = CONTENTS_EMPTY;
		floor_nodes[i].children[1] = CONTENTS_SOLID;
		floor_model.hulls[i].planes = &floor_planes[i];
		floor_model.hulls[i].clipnodes = &floor_nodes[i];
		floor_model.hulls[i].firstclipnode = floor_model.hulls[i].lastclipnode = 0;
	}
	// Exercise both donor trace implementations, including the enabled default.
	for (int fast = 0; fast < 2; ++fast)
	{
		pr_checkextension.value = 1;
		sv_fte_recursivehullckeck.value = fast;
		prepare ();
		pmove.cmd.forwardmove = 320;
		for (int i = 0; i < 10; ++i)
			PM_PlayerMove (1);
		assert (pmove.origin[0] > 200 && pmove.origin[0] < 320);
		near_value (pmove.origin[2], 24, .04f);
		assert (pmove.onground);
		near_value (pmove.velocity[0], 320, .01f);

		// Four duration substeps must apply physical displacement only once.
		prepare ();
		pmove.cmd.vr_active = pmove.cmd.vr_handpos_relative = true;
		pmove.cmd.vr_roomscalemove[0] = 8;
		PM_PlayerMove (1);
		near_value (pmove.origin[0], 8, .01f);
		near_value (pmove.cmd.seconds, .1f, .00001f);
		assert (pmove.cmd.msec == 100);
		// An outlier is rejected rather than clamped into artificial movement.
		prepare ();
		pmove.cmd.vr_active = true;
		pmove.cmd.vr_roomscalemove[0] = 1000;
		PM_PlayerMove (1);
		near_value (pmove.origin[0], 0, .01f);
		// Frozen commands cannot turn, walk or apply roomscale displacement.
		prepare ();
		pmove.pm_type = PM_FREEZE;
		pmove.cmd.forwardmove = 320;
		pmove.cmd.viewangles[1] = 90;
		pmove.cmd.vr_active = true;
		pmove.cmd.vr_roomscalemove[0] = 8;
		PM_PlayerMove (1);
		near_value (pmove.origin[0], 0, .01f);
		near_value (pmove.angles[1], 0, .01f);
		// A normal jump leaves the floor, then returns through real donor tracing.
		prepare ();
		pmove.cmd.buttons = BUTTON_JUMP;
		PM_PlayerMove (1);
		assert (pmove.origin[2] > 24 && pmove.velocity[2] > 0);
		pmove.cmd.buttons = 0;
		for (int i = 0; i < 20; ++i)
			PM_PlayerMove (1);
		near_value (pmove.origin[2], 24, .04f);
		assert (pmove.onground);
		// A received entity box collides at its expanded player boundary.
		prepare ();
		pmove.numphysent = 2;
		pmove.physents[1].info = 1;
		VectorSet (pmove.physents[1].mins, 32, -64, 0);
		VectorSet (pmove.physents[1].maxs, 48, 64, 64);
		vec3_t	end = {64, 0, 24};
		trace_t trace = PM_PlayerTrace (pmove.origin, end, MASK_PLAYERSOLID);
		near_value (trace.endpos[0], 16, .04f);
		near_value (trace.plane.normal[0], -1, .001f);
		assert (trace.fraction < 1 && !trace.startsolid);

		// The inherited transform must retain a rotated brush's normal and hit
		// position through the donor signature, including stationary solid tests.
		qmodel_t	wall = floor_model;
		mplane_t	plane = {0};
		mclipnode_t node = {0};
		plane.normal[0] = 1;
		plane.type = 0;
		node.children[0] = CONTENTS_EMPTY;
		node.children[1] = CONTENTS_SOLID;
		wall.hulls[0].planes = &plane;
		wall.hulls[0].clipnodes = &node;
		vec3_t start = {10, 20, 30}, stop = {10, -20, 30}, rotation = {0, 90, 0};
		assert (PM_TransformedHullCheck (&wall, start, stop, vec3_origin, vec3_origin, &trace, vec3_origin, rotation));
		near_value (trace.endpos[1], 0, .04f);
		near_value (trace.plane.normal[1], 1, .001f);
		assert (trace.fraction > .49f && trace.fraction < .51f);
		assert (PM_TransformedHullCheck (&wall, stop, stop, vec3_origin, vec3_origin, &trace, vec3_origin, rotation));
		assert (trace.startsolid && trace.allsolid);
		// Non-solid water remains traversable, while its contents stay observable.
		node.children[1] = CONTENTS_WATER;
		assert (PM_TransformedHullCheck (&wall, start, stop, vec3_origin, vec3_origin, &trace, vec3_origin, rotation));
		near_value (trace.fraction, 1, .001f);
		assert (!trace.allsolid && !trace.startsolid && trace.inwater);
		// Source recursive hulls treat raw CLIP leaves as non-solid. The donor
		// fast mask must preserve that, independently of physent filtering.
		node.children[1] = CONTENTS_CLIP;
		assert (PM_TransformedHullCheck (&wall, start, stop, vec3_origin, vec3_origin, &trace, vec3_origin, rotation));
		near_value (trace.fraction, 1, .001f);
		assert (!trace.allsolid && !trace.startsolid);
	}
	puts ("Inherited PMove with donor hulls: walk, jump, freeze, once-only roomscale, box/rotated brush, stationary and water passed");
}
