/* Exercise transplanted movement against donor hull collision, not a mock trace.
 * This verifies the shared solver, not command transport or gameplay parity. */
#include "../Quake/pmove.c"
#include <stdarg.h>
#include <assert.h>

cvar_t			pr_checkextension;
extern cvar_t	sv_fte_recursivehullckeck;
void			Con_DPrintf (const char *fmt, ...) {}
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
	/* msec == 0 selects the inherited QSS-M single-step path. Tests that
	 * exercise negotiated explicit timing opt into msec below. */
	pmove.cmd.msec = 0;
	pmove.cmd.seconds = .1f;
}

static void ladder_velocity (float pitch, qboolean vr_active, vec3_t velocity)
{
	prepare ();
	pmove.origin[2] = 128;
	VectorSet (pmove.gravitydir, 0, 0, -1);
	pmove.angles[PITCH] = pitch;
	pmove.cmd.forwardmove = 200;
	pmove.cmd.sidemove = 100;
	pmove.cmd.vr_active = vr_active;
	frametime = .1f;
	AngleVectors (pmove.angles, forward, right, up);
	PM_EnsureInitialized ();
	PM_LadderMove ();
	VectorCopy (pmove.velocity, velocity);
}

static void water_velocity (qboolean vr_active, vec3_t velocity)
{
	prepare ();
	pmove.origin[2] = 128;
	VectorSet (pmove.gravitydir, 0, 0, -1);
	movevars.jumpspeed = 400;
	movevars.watersinkspeed = 60;
	pmove.cmd.buttons = BUTTON_JUMP;
	pmove.cmd.vr_active = vr_active;
	frametime = .1f;
	AngleVectors (pmove.angles, forward, right, up);
	PM_EnsureInitialized ();
	PM_WaterMove ();
	VectorCopy (pmove.velocity, velocity);
}

static void check_touch_policy (int msec)
{
	prepare ();
	pmove.cmd.msec = msec;

	VectorSet (pmove.velocity, 1, 2, 3);
	PM_AddTouchedEnt (1);
	VectorSet (pmove.velocity, 10, 20, 30);
	PM_AddTouchedEnt (1);
	VectorSet (pmove.velocity, 4, 5, 6);
	PM_AddTouchedEnt (2);
	VectorSet (pmove.velocity, 7, 8, 9);
	PM_AddTouchedEnt (1);

	assert (pmove.touchindex[0] == 1);
	near_value (pmove.touchvel[0][0], 1, .001f);
	near_value (pmove.touchvel[0][1], 2, .001f);
	near_value (pmove.touchvel[0][2], 3, .001f);
	assert (pmove.touchindex[1] == 2);
	near_value (pmove.touchvel[1][0], 4, .001f);
	near_value (pmove.touchvel[1][1], 5, .001f);
	near_value (pmove.touchvel[1][2], 6, .001f);

	if (!msec)
	{
		assert (pmove.numtouch == 3 && pmove.touchindex[2] == 1);
		near_value (pmove.touchvel[2][0], 7, .001f);
		near_value (pmove.touchvel[2][1], 8, .001f);
		near_value (pmove.touchvel[2][2], 9, .001f);
	}
	else
	{
		assert (pmove.numtouch == 2);
	}
}

static void check_safeorigin_recovery (void)
{
	vec3_t safe = {160, 0, 24};

	prepare ();
	pmove.numphysent = 2;
	pmove.physents[1].info = 1;
	VectorSet (pmove.physents[1].mins, -64, -64, 0);
	VectorSet (pmove.physents[1].maxs, 64, 64, 64);
	assert (PM_TestPlayerPosition (safe));
	near_value (pmove.safeorigin[0], safe[0], .001f);
	near_value (pmove.safeorigin[1], safe[1], .001f);
	near_value (pmove.safeorigin[2], safe[2], .001f);

	VectorSet (pmove.origin, 0, 0, 24);
	assert (!PM_TestPlayerPosition (pmove.origin));
	PM_NudgePosition ();
	near_value (pmove.origin[0], safe[0], .001f);
	near_value (pmove.origin[1], safe[1], .001f);
	near_value (pmove.origin[2], safe[2], .001f);
}

static void check_roomscale_wall_duration (int msec, float seconds,
	float expected_y)
{
	prepare ();
	pmove.numphysent = 2;
	pmove.physents[1].info = 1;
	VectorSet (pmove.physents[1].mins, 24, -64, 0);
	VectorSet (pmove.physents[1].maxs, 40, 64, 64);
	pmove.cmd.msec = msec;
	pmove.cmd.seconds = seconds;
	pmove.cmd.vr_active = true;
	VectorSet (pmove.cmd.vr_roomscalemove, 15, .006f, 0);

	PM_PlayerMove (1);
	near_value (pmove.origin[0], 8, .2f);
	near_value (pmove.origin[1], expected_y, .0005f);
	near_value (pmove.origin[2], 24, .04f);
	near_value (pmove.velocity[0], 0, .01f);
	near_value (pmove.velocity[1], 0, .01f);
	near_value (pmove.velocity[2], 0, .01f);
	assert (pmove.numtouch > 0 && pmove.touchindex[0] == 1);
}

static void check_roomscale_invalid_duration (float seconds)
{
	prepare ();
	pmove.cmd.msec = 100;
	pmove.cmd.seconds = seconds;
	pmove.cmd.vr_active = true;
	pmove.cmd.vr_roomscalemove[0] = 8;

	PM_ApplyPreThinkRoomScale ();
	near_value (pmove.origin[0], 0, .01f);
	near_value (pmove.origin[1], 0, .01f);
	near_value (pmove.origin[2], 24, .04f);
	near_value (pmove.velocity[0], 0, .01f);
	near_value (pmove.velocity[1], 0, .01f);
	near_value (pmove.velocity[2], 0, .01f);
}

static void check_transient_fluid_crossing (void)
{
	prepare ();
	pmove.numphysent = 2;
	pmove.physents[1].info = 1;
	pmove.physents[1].forcecontentsmask = CONTENTBIT_WATER;
	VectorSet (pmove.physents[1].mins, 4, -100, 0);
	VectorSet (pmove.physents[1].maxs, 12, 100, 80);
	pmove.cmd.msec = 100;
	pmove.cmd.seconds = .1f;
	pmove.cmd.forwardmove = 320;
	pmove.velocity[0] = 320;
	PM_PlayerMove (1);
	assert (pmove.origin[0] > 12);
	assert (pmove.waterlevel == 0);
	assert (pmove.fluid_contacted);
	PM_PlayerMove (1);
	assert (pmove.waterlevel == 0 && !pmove.fluid_contacted);
}

static void check_vr_instant_stop (void)
{
	usercmd_t input = {0};
	input.forwardmove = -200;
	assert (PM_VRInstantStopNeutralInput (&input, true));
	assert (!PM_VRInstantStopNeutralInput (&input, false));
	input.sidemove = 20;
	assert (!PM_VRInstantStopNeutralInput (&input, true));

	prepare ();
	pmove.cmd.msec = 100;
	pmove.cmd.vr_active = true;
	pmove.velocity[0] = 200;
	PM_PlayerMove (1);
	assert (pmove.velocity[0] > 0); /* default-off friction */

	prepare ();
	movevars.flags |= MOVEFLAG_VR_INSTANT_STOP;
	pmove.cmd.msec = 100;
	pmove.cmd.vr_active = true;
	pmove.velocity[0] = 200;
	PM_PlayerMove (1);
	near_value (pmove.velocity[0], 0, .01f);
	near_value (pmove.origin[0], 0, .01f);

	prepare ();
	movevars.flags |= MOVEFLAG_VR_INSTANT_STOP;
	pmove.cmd.msec = 100;
	pmove.velocity[0] = 200;
	PM_PlayerMove (1);
	assert (pmove.velocity[0] > 0); /* desktop stays on vkQuake friction */

	prepare ();
	movevars.flags |= MOVEFLAG_VR_INSTANT_STOP;
	pmove.cmd.msec = 100;
	pmove.cmd.vr_active = true;
	pmove.cmd.forwardmove = 100;
	pmove.velocity[0] = 200;
	PM_PlayerMove (1);
	assert (pmove.velocity[0] > 0); /* moving stick is not a stop */

	prepare ();
	movevars.flags |= MOVEFLAG_VR_INSTANT_STOP;
	pmove.cmd.msec = 100;
	pmove.cmd.vr_active = true;
	pmove.vr_instant_stop_preapplied = true;
	pmove.velocity[0] = 200;
	PM_PlayerMove (1);
	assert (pmove.velocity[0] > 0); /* preserve post-PreThink impulse */

	prepare ();
	movevars.flags |= MOVEFLAG_VR_INSTANT_STOP;
	pmove.cmd.msec = 100;
	pmove.cmd.vr_active = true;
	pmove.cmd.buttons = BUTTON_JUMP;
	pmove.velocity[0] = 200;
	PM_PlayerMove (1);
	near_value (pmove.velocity[0], 0, .01f);
	assert (pmove.velocity[2] > 0); /* stop does not consume the jump */
}

static void check_teleport_backmove (void)
{
	vec3_t blocked, unrestricted;

	prepare ();
	pmove.origin[2] = 128;
	VectorSet (pmove.gravitydir, 0, 0, -1);
	pmove.cmd.forwardmove = -200;
	pmove.cmd.sidemove = 100;
	pmove.block_teleport_backmove = true;
	frametime = .1f;
	AngleVectors (pmove.angles, forward, right, up);
	PM_EnsureInitialized ();
	PM_AirMove ();
	VectorCopy (pmove.velocity, blocked);

	prepare ();
	pmove.origin[2] = 128;
	VectorSet (pmove.gravitydir, 0, 0, -1);
	pmove.cmd.forwardmove = -200;
	pmove.cmd.sidemove = 100;
	frametime = .1f;
	AngleVectors (pmove.angles, forward, right, up);
	PM_EnsureInitialized ();
	PM_AirMove ();
	VectorCopy (pmove.velocity, unrestricted);

	near_value (blocked[0], 0, .01f);
	assert (fabsf (blocked[1]) > 1.0f);
	assert (unrestricted[0] < -1.0f);
}

static void check_qc_takeoff (int msec, float takeoff_velocity)
{
	float expected_velocity = takeoff_velocity - 80;
	float expected_origin = msec ?
		(24 + takeoff_velocity * .1f - 5) :
		(24 + expected_velocity * .1f);

	prepare ();
	pmove.cmd.msec = msec;
	pmove.cmd.buttons = BUTTON_JUMP;
	pmove.qc_jump_owner = true;
	pmove.velocity[2] = takeoff_velocity;
	PM_PlayerMove (1);
	/* A held button must not add the native jump speed or consume its latch. */
	assert (!pmove.jump_held);
	assert (!pmove.onground);
	near_value (pmove.velocity[2], expected_velocity, .01f);
	near_value (pmove.origin[2], expected_origin, .04f);

	/* The external takeoff remains airborne while rising, then lands normally. */
	for (int i = 0; i < 30; ++i)
		PM_PlayerMove (1);
	near_value (pmove.origin[2], 24, .04f);
	assert (pmove.onground);
	assert (!pmove.jump_held);
}

static void check_qc_takeoff_release (int msec)
{
	prepare ();
	pmove.cmd.msec = msec;
	pmove.qc_jump_owner = true;
	pmove.jump_held = true;
	pmove.velocity[2] = 120;
	PM_PlayerMove (1);
	/* PM_CheckJump must not clear a QC-owned held/release latch. */
	assert (pmove.jump_held);
}

static void check_qc_short_takeoff_then_release (void)
{
	prepare ();
	pmove.cmd.msec = 5;
	pmove.cmd.seconds = .005f;
	pmove.cmd.buttons = BUTTON_JUMP;
	pmove.qc_jump_owner = true;
	pmove.jump_held = true;
	pmove.velocity[2] = 120;
	PM_PlayerMove (1);
	assert (!pmove.onground && pmove.origin[2] > 24.0f &&
		pmove.origin[2] < 25.0f);

	/* The next QC PreThink releases its latch, but QC remains the jump owner.
	 * The rising player is still inside the one-unit floor probe. */
	pmove.cmd.buttons = 0;
	pmove.jump_held = false;
	PM_PlayerMove (1);
	assert (!pmove.onground && !pmove.jump_held);
	assert (pmove.velocity[2] > 0.0f && pmove.origin[2] > 24.5f);
}

static void check_qc_takeoff_water (void)
{
	prepare ();
	pmove.numphysent = 2;
	pmove.physents[1].info = 1;
	pmove.physents[1].forcecontentsmask = CONTENTBIT_WATER;
	VectorSet (pmove.physents[1].mins, -64, -64, 0);
	VectorSet (pmove.physents[1].maxs, 64, 64, 80);
	pmove.qc_jump_owner = true;
	pmove.velocity[2] = 120;
	PM_CategorizePosition ();
	assert (!pmove.onground);
	assert (pmove.waterlevel == 3 && (pmove.watertype & CONTENTBIT_WATER));
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
		check_teleport_backmove ();
		check_transient_fluid_crossing ();
		check_vr_instant_stop ();
		check_qc_takeoff (0, 120);
		check_qc_takeoff (0, 300);
		check_qc_takeoff (100, 120);
		check_qc_takeoff (100, 300);
		check_qc_takeoff_release (0);
		check_qc_takeoff_release (100);
		check_qc_short_takeoff_then_release ();
		check_qc_takeoff_water ();
		prepare ();
		pmove.cmd.forwardmove = 320;
		for (int i = 0; i < 10; ++i)
			PM_PlayerMove (1);
		/* Pinned QSS-M 03a498aa reaches maxspeed at x=320 after these ten
		 * single-step commands with the same movevars and floor hull. */
		near_value (pmove.origin[0], 320, .01f);
		near_value (pmove.origin[2], 24, .04f);
		assert (pmove.onground);
		near_value (pmove.velocity[0], 320, .01f);

		/* Negotiated 100 ms commands retain the private deterministic-substep
		 * behavior that predates the QSS-M single-step compatibility path. */
		prepare ();
		pmove.cmd.msec = 100;
		pmove.cmd.forwardmove = 320;
		for (int i = 0; i < 10; ++i)
			PM_PlayerMove (1);
		assert (pmove.origin[0] > 200 && pmove.origin[0] < 320);
		near_value (pmove.origin[2], 24, .04f);
		assert (pmove.onground);
		near_value (pmove.velocity[0], 320, .01f);
		assert (pmove.cmd.msec == 100);

		/* Pinned OpenVR 1327f795 flattens pitch for VR ladders, while pinned
		 * QSS-M 03a498aa keeps its ordinary pitch-sensitive ladder wishvel. */
		vec3_t vr_ladder_flat, vr_ladder_pitched, qss_ladder_flat, qss_ladder_pitched;
		ladder_velocity (0, true, vr_ladder_flat);
		ladder_velocity (60, true, vr_ladder_pitched);
		for (int axis = 0; axis < 3; ++axis)
			near_value (vr_ladder_pitched[axis], vr_ladder_flat[axis], .01f);
		near_value (vr_ladder_flat[0], 70, .01f);
		near_value (vr_ladder_flat[1], -35, .01f);
		near_value (vr_ladder_flat[2], 70, .01f);
		ladder_velocity (0, false, qss_ladder_flat);
		near_value (qss_ladder_flat[0], 200, .01f);
		near_value (qss_ladder_flat[1], -100, .01f);
		near_value (qss_ladder_flat[2], 0, .01f);
		ladder_velocity (60, false, qss_ladder_pitched);
		near_value (qss_ladder_pitched[0], 18.4139f, .02f);
		near_value (qss_ladder_pitched[1], -18.4139f, .02f);
		near_value (qss_ladder_pitched[2], -318.9386f, .02f);

		/* A raised generic jump speed must not opt into OpenVR's jump-to-swim
		 * upmove. Exercise the production PM_WaterMove path for both cases. */
		vec3_t generic_water, vr_water;
		water_velocity (false, generic_water);
		water_velocity (true, vr_water);
		near_value (generic_water[2], -42, .01f);
		near_value (vr_water[2], 140, .01f);

		// Four duration substeps must apply physical displacement only once.
		prepare ();
		pmove.cmd.msec = 100;
		pmove.cmd.vr_active = pmove.cmd.vr_handpos_relative = true;
		pmove.cmd.vr_roomscalemove[0] = 8;
		PM_PlayerMove (1);
		near_value (pmove.origin[0], 8, .01f);
		near_value (pmove.origin[1], 0, .01f);
		near_value (pmove.velocity[0], 0, .01f);
		near_value (pmove.velocity[1], 0, .01f);
		near_value (pmove.velocity[2], 0, .01f);
		near_value (pmove.cmd.seconds, .1f, .00001f);
		assert (pmove.cmd.msec == 100);

		/* The 125 ms roomscale delta is applied during the first movement
		 * substep, but clipping uses the full command duration: .006/.125 is
		 * below STOP_EPSILON. A 25 ms command makes it .24 units/s, so the same
		 * tangential sweep survives the wall clip. */
		check_roomscale_wall_duration (125, .125f, 0);
		check_roomscale_wall_duration (25, .025f, .006f);
		check_roomscale_invalid_duration (0);
		check_roomscale_invalid_duration (NAN);

		// An outlier is rejected rather than clamped into artificial movement.
		prepare ();
		pmove.cmd.msec = 100;
		pmove.cmd.vr_active = true;
		pmove.cmd.vr_roomscalemove[0] = 1000;
		PM_PlayerMove (1);
		near_value (pmove.origin[0], 0, .01f);
		// Frozen commands cannot turn, walk or apply roomscale displacement.
		prepare ();
		pmove.pm_type = PM_FREEZE;
		pmove.cmd.msec = 100;
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
		/* QSS-M authors the 270 upswing on the jump command; translation starts
		 * on the following command. */
		near_value (pmove.origin[2], 24, .04f);
		near_value (pmove.velocity[2], 270, .01f);
		assert (!pmove.onground);
		pmove.cmd.buttons = 0;
		for (int i = 0; i < 20; ++i)
			PM_PlayerMove (1);
		near_value (pmove.origin[2], 24, .04f);
		assert (pmove.onground);

		prepare ();
		pmove.cmd.msec = 100;
		pmove.cmd.buttons = BUTTON_JUMP;
		PM_PlayerMove (1);
		assert (pmove.origin[2] > 24 && pmove.velocity[2] > 0);
		assert (pmove.cmd.msec == 100);
		pmove.cmd.buttons = 0;
		for (int i = 0; i < 20; ++i)
			PM_PlayerMove (1);
		near_value (pmove.origin[2], 24, .04f);
		assert (pmove.onground);

		check_touch_policy (0);
		check_touch_policy (100);
		check_safeorigin_recovery ();
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
	puts ("QSS-M generic PMove with donor hulls plus explicit VR deltas: walk, jump, ladders, swim gating, freeze, once-only roomscale, box/rotated brush, stationary and water passed");
}
