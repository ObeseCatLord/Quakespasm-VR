/* Execute the production renderer camera adapter and host-abort cleanup.
 * GPU waiting is spied: this verifies the drain boundary, not a Vulkan fence. */
#include "../Quake/gl_rmain.c"
#include "../Quake/gl_screen.c"
#include "../Quake/view.c"
#include "../Quake/chase.c"

vulkanglobals_t		 vulkan_globals;
client_state_t cl;
client_static_t cls;
server_t sv;
double host_frametime;
qboolean noclip_anglehack;
qboolean con_forcedup;
int r_trace_line_cache_counter;
cvar_t lookspring;
static int chase_traces;
/* Camera-owner fixture: controller assembly is covered by the native input
 * probe. Observe invalidation without introducing a duplicate input policy. */
static int motion_invalidations;
void VR_InputInvalidateMotion (void) { ++motion_invalidations; }
void VR_InputApplyPending (usercmd_t *cmd) { (void)cmd; }
qboolean SV_RecursiveHullCheck (hull_t *hull, vec3_t p1, vec3_t p2, trace_t *trace, unsigned int hitcontents)
{
	// Run the real chase calculation with a known collision point, followed
	// by an unobstructed view ray. GPU smoke uses the real map hull instead.
	if (chase_traces++ % 2 == 0)
	{
		trace->endpos[0] = 10;
		trace->endpos[1] = 20;
		trace->endpos[2] = 30;
	}
	else
		VectorCopy (p2, trace->endpos);
	return true;
}
static vrxr_frame_t	 test_frame;
static unsigned char uniform_data[160];
static int			 gpu_pending, gpu_drains, xr_aborts;
const vrxr_frame_t	*GL_OpenXRFrame (void)
{
	return vulkan_globals.stereo_active ? &test_frame : NULL;
}
byte *R_UniformAllocate (int size, VkBuffer *buffer, uint32_t *offset, VkDescriptorSet *set)
{
	assert (size == sizeof uniform_data);
	*buffer = (VkBuffer)(uintptr_t)1;
	*offset = 0;
	*set = (VkDescriptorSet)(uintptr_t)1;
	return uniform_data;
}
void GL_WaitForDeviceIdle (void)
{
	++gpu_drains;
	gpu_pending = 0;
}
void VRXR_AbortFrame (void)
{
	assert (!gpu_pending);
	++xr_aborts;
}
static void near_value (float actual, float expected)
{
	assert (fabsf (actual - expected) < .002f);
}
static void head_yaw (float degrees)
{
	const float radians = degrees * M_PI / 180.f;
	float (*head)[4] = test_frame.devices[0].matrix;
	head[0][0] = head[2][2] = cosf (radians);
	head[0][2] = sinf (radians);
	head[2][0] = -sinf (radians);
	for (int eye = 0; eye < 2; ++eye)
	{
		memcpy (test_frame.views[eye].matrix, head, sizeof test_frame.views[eye].matrix);
		for (int row = 0; row < 3; ++row)
			test_frame.views[eye].matrix[row][3] += head[row][0] * (eye ? .032f : -.032f);
	}
}
static void restore (void)
{
	R_RestoreStereoView ();
	near_value (r_refdef.vieworg[0], 100);
	near_value (r_refdef.vieworg[1], 200);
	near_value (r_refdef.vieworg[2], 300);
	for (int i = 0; i < 3; ++i)
		near_value (r_refdef.viewangles[i], 0);
}
static void test_roomscale_eye_anchor (void)
{
	const float units = V_VRUnitsPerMetre ();
	float base_y, anchored_z;
	cls.signon = SIGNONS;
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	cls.demoplayback = false;
	cl.paused = false;
	cl.intermission = 0;
	cl.fixangle_time = -1;
	cl.stats[STAT_HEALTH] = 100;
	cl.cmd.vr_active = cl.pendingcmd.vr_active = false;
	chase_active.value = 0;
	vr_aimmode.value = VR_AIMMODE_CONTROLLER;
	test_frame.focused = test_frame.should_render = test_frame.floor_referenced = 1;
	test_frame.devices[0].valid = 1;
	test_frame.devices[0].matrix[0][3] = 0;
	test_frame.devices[0].matrix[1][3] = 1.7f;
	test_frame.devices[0].matrix[2][3] = 0;
	head_yaw (0);
	VectorCopy (vec3_origin, cl.viewangles);
	V_ResetTrackedAim ();
	R_InvalidateStereoReference ();
	V_UpdateTrackedAim ();
	V_CalcRefdef ();
	assert (base_player_view && tracked_aim_ready);
	base_y = r_refdef.vieworg[1];
	R_PrepareStereoFrame ();
	R_RestoreStereoView ();
	// Before command activation, the existing camera remains positional.
	test_frame.devices[0].matrix[0][3] = .2f;
	head_yaw (0);
	V_UpdateTrackedAim ();
	assert (!V_TrackedBodyOwnsRoomscale ());
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[1], base_y - .2f * units);
	R_RestoreStereoView ();
	// A preview can own the body step before the first tagged command is sent.
	cl.pendingcmd.vr_active = true;
	assert (V_TrackedBodyOwnsRoomscale ());
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[1], base_y);
	anchored_z = r_refdef.vieworg[2];
	R_RestoreStereoView ();
	// After send clears pending, the last private command retains the anchor.
	cl.pendingcmd.vr_active = false;
	cl.cmd.vr_active = true;
	test_frame.devices[0].matrix[0][3] = .3f; // blocked step: body stays put
	test_frame.devices[0].matrix[1][3] = 1.8f;
	head_yaw (0);
	V_UpdateTrackedAim ();
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[1], base_y);
	near_value (r_refdef.vieworg[2], anchored_z + .1f * units);
	R_RestoreStereoView ();
	// An accepted body step moves the eye with the player, without adding the
	// same raw HMD displacement a second time.
	cl.entities[cl.viewentity].origin[1] += 5;
	V_CalcRefdef ();
	base_y = r_refdef.vieworg[1];
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[1], base_y);
	R_RestoreStereoView ();
	R_InvalidateStereoReference ();
	V_UpdateTrackedAim ();
	test_frame.devices[0].matrix[0][3] = .4f;
	head_yaw (0);
	V_UpdateTrackedAim ();
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[1], base_y);
	R_RestoreStereoView ();
	// Public peers never acquire a body-motion anchor from a stale private cmd.
	test_frame.devices[0].matrix[0][3] = .5f;
	head_yaw (0);
	V_UpdateTrackedAim ();
	cl.protocol_qsvr = 0;
	assert (!V_TrackedBodyOwnsRoomscale ());
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[1], base_y - .1f * units);
	R_RestoreStereoView ();
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	vr_aimmode.value = VR_AIMMODE_HEAD_MYAW;
	assert (!V_TrackedBodyOwnsRoomscale ());
	vr_aimmode.value = VR_AIMMODE_CONTROLLER;
	chase_active.value = 1;
	V_CalcRefdef ();
	assert (!V_TrackedBodyOwnsRoomscale ());
	chase_active.value = 0;
	cl.cmd.vr_active = false;
}
int main (void)
{
	vr_world_scale.value = 1;
	vr_floor_offset.value = -16;
	const float default_units = 1.f / (1.5f * .0254f);
	vulkan_globals.stereo_active = true;
	test_frame.should_render = 1;
	r_refdef.vieworg[0] = 100;
	r_refdef.vieworg[1] = 200;
	r_refdef.vieworg[2] = 300;
	test_frame.devices[0].valid = 1;
	for (int k = 0; k < 3; ++k)
		test_frame.devices[0].matrix[k][k] = 1;
	for (int eye = 0; eye < 2; ++eye)
	{
		for (int k = 0; k < 3; ++k)
			test_frame.views[eye].matrix[k][k] = 1;
		test_frame.views[eye].matrix[0][3] = eye ? .032f : -.032f;
		test_frame.views[eye].left = -1;
		test_frame.views[eye].right = 1;
		test_frame.views[eye].down = -1;
		test_frame.views[eye].up = 1;
	}
	R_PrepareStereoFrame ();
	near_value (r_stereo_origins[0][1], 200 + .032f * default_units);
	near_value (r_stereo_origins[1][1], 200 - .032f * default_units);
	restore ();
	// Repeated paused-camera rendering must not accumulate head translation.
	test_frame.devices[0].matrix[0][3] = .25f;
	for (int frame = 0; frame < 3; ++frame)
	{
		R_PrepareStereoFrame ();
		near_value (r_refdef.vieworg[1], 200 - .25f * default_units);
		restore ();
	}
	// Reference notification is consumed on a skipped frame, before preparation.
	R_InvalidateStereoReference ();
	test_frame.reference_changed = 0;
	test_frame.devices[0].matrix[0][3] = 3;
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[1], 200);
	// Outstanding old GPU data must be drained before abandoning this begin.
	gpu_pending = 1;
	in_update_screen = true;
	SCR_AbortXRFrame ();
	assert (gpu_drains == 1 && xr_aborts == 1 && !in_update_screen);
	restore ();
	vulkan_globals.stereo_active = false;
	SCR_AbortXRFrame ();
	assert (gpu_drains == 1 && xr_aborts == 1);

	// Run the real donor view preparation with inherited comfort controls.
	entity_t player = {0};
	qmodel_t world = {0};
	cl.worldmodel = &world;
	cl.entities = &player;
	cl.stats[STAT_VIEWHEIGHT] = 22;
	cl.stats[STAT_HEALTH] = 100;
	cl_bobcycle.value = .6f;
	cl_bobup.value = .5f;
	cl_bob.value = .02f;
	cl_rollangle.value = 2;
	cl_rollspeed.value = 200;
	cl.velocity[1] = 200;
	cl.time = .1;
	host_frametime = .01;
	assert (V_CalcBob () != 0);
	assert (V_CalcRoll (vec3_origin, cl.velocity) != 0);
	vulkan_globals.stereo_active = true;
	near_value (V_CalcBob (), 0);
	near_value (V_CalcRoll (vec3_origin, cl.velocity), 0);
	test_frame.devices[0].matrix[0][3] = 0;
	test_frame.devices[0].matrix[1][3] = 1.7f;
	for (int eye = 0; eye < 2; ++eye)
		test_frame.views[eye].matrix[1][3] = 1.7f;
	test_frame.floor_referenced = 1;
	R_InvalidateStereoReference ();
	V_CalcRefdef ();
	const float bias = 1.f / 32.f;
	for (int pause_frame = 0; pause_frame < 3; ++pause_frame)
	{
		R_PrepareStereoFrame ();
		near_value (r_refdef.vieworg[2], bias + 1.7f * default_units - 16);
		R_RestoreStereoView ();
	}
	// Updated server viewheight cannot corrupt a paused, older view base.
	cl.stats[STAT_VIEWHEIGHT] = 8;
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[2], bias + 1.7f * default_units - 16);
	R_RestoreStereoView ();
	// Crouching and worldscale move height; floor offset stays in Quake units.
	test_frame.devices[0].matrix[1][3] = 1.2f;
	for (int eye = 0; eye < 2; ++eye)
		test_frame.views[eye].matrix[1][3] = 1.2f;
	vr_world_scale.value = 2;
	vr_floor_offset.value = -10;
	r_refdef.viewangles[PITCH] = 60;
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[2], bias + 1.2f * default_units * 2 - 10);
	near_value (r_stereo_radius, .032f * default_units * 2);
	R_RestoreStereoView ();
	// LOCAL keeps relative vertical tracking rather than using its zero as floor.
	test_frame.floor_referenced = 0;
	vr_world_scale.value = 1;
	vr_floor_offset.value = -16;
	R_InvalidateStereoReference ();
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[2], bias + 22);
	R_RestoreStereoView ();
	test_frame.devices[0].matrix[1][3] += .25f;
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[2], bias + 22 + .25f * default_units);
	R_RestoreStereoView ();
	// Invalid settings never publish non-finite projection units.
	vr_world_scale.value = NAN;
	near_value (V_VRUnitsPerMetre (), default_units);
	vr_world_scale.value = FLT_MAX;
	near_value (V_VRUnitsPerMetre (), default_units);
	vr_world_scale.value = 0;
	near_value (V_VRUnitsPerMetre (), default_units);
	// Death roll and damage kick stay opt-in in a usable XR view.
	VectorCopy (vec3_origin, r_refdef.viewangles);
	cl.stats[STAT_HEALTH] = 0;
	cl.v_dmg_time = .5f;
	cl.v_dmg_roll = 5;
	cl.v_dmg_pitch = 8;
	v_kicktime.value = .5f;
	V_CalcViewRoll ();
	near_value (r_refdef.viewangles[ROLL], 0);
	near_value (r_refdef.viewangles[PITCH], 0);
	assert (cl.v_dmg_time < .5f);
	vr_viewkick.value = 1;
	V_CalcViewRoll ();
	assert (r_refdef.viewangles[ROLL] != 0 && r_refdef.viewangles[PITCH] != 0);
	// Both donor gun-kick modes obey the inherited VR toggle.
	cl.stats[STAT_HEALTH] = 100;
	cl.v_dmg_time = 0;
	cl.punchangle[PITCH] = 8;
	vr_viewkick.value = 0;
	v_gunkick.value = 1;
	V_CalcRefdef ();
	near_value (r_refdef.viewangles[PITCH], 0);
	vr_viewkick.value = 1;
	V_CalcRefdef ();
	near_value (r_refdef.viewangles[PITCH], 8);
	v_gunkick.value = 2;
	v_punchangles[0][PITCH] = 10;
	v_punchangles_times[0] = .1;
	v_punchangles_times[1] = 0;
	vr_viewkick.value = 0;
	V_CalcRefdef ();
	near_value (r_refdef.viewangles[PITCH], 0);
	vr_viewkick.value = 1;
	V_CalcRefdef ();
	near_value (r_refdef.viewangles[PITCH], 2);
	// Recoil decays while hidden rather than replaying when the toggle returns.
	vr_viewkick.value = 0;
	v_punchangles[1][PITCH] = v_punchangles[0][PITCH];
	v_punchangles[0][PITCH] = 0;
	for (int frame = 0; frame < 20; ++frame)
		V_CalcRefdef ();
	vr_viewkick.value = 1;
	V_CalcRefdef ();
	near_value (r_refdef.viewangles[PITCH], 0);
	// A prepared chase base remains camera-relative even with a known floor.
	chase_active.value = 1;
	test_frame.floor_referenced = 1;
	vr_world_scale.value = 2;
	R_InvalidateStereoReference ();
	V_CalcRefdef ();
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[2], 30);
	R_RestoreStereoView ();
	// Paused configuration changes do not reclassify the older chase base.
	chase_active.value = 0;
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[2], 30);
	R_RestoreStereoView ();
	cl.stats[STAT_HEALTH] = 0;
	vulkan_globals.stereo_active = false;
	V_CalcViewRoll ();
	near_value (r_refdef.viewangles[ROLL], 80);

	// Head aiming reaches the real donor command constructor. The view must
	// contain that head rotation once, including with an older paused base.
	vulkan_globals.stereo_active = true;
	cls.signon = SIGNONS;
	cl.fixangle_time = -1;
	cl.stats[STAT_HEALTH] = 100;
	cl.v_dmg_time = 0;
	v_gunkick.value = 0;
	vr_viewkick.value = 0;
	vr_world_scale.value = 1;
	vr_aimmode.value = VR_AIMMODE_HEAD_MYAW;
	vr_deadzone.value = 30;
	VectorCopy (vec3_origin, cl.viewangles);
	cl.viewangles[YAW] = 90;
	V_ResetTrackedAim ();
	V_UpdateTrackedAim ();
	V_CalcRefdef ();
	head_yaw (15);
	for (int frame = 0; frame < 3; ++frame)
	{
		V_UpdateTrackedAim ();
		usercmd_t command;
		CL_BaseMove (&command);
		near_value (command.viewangles[YAW], 105);
		R_PrepareStereoFrame ();
		near_value (r_refdef.viewangles[YAW], 105);
		R_RestoreStereoView ();
	}
	// A reference rebase preserves visual/aim separation and input received
	// while the new tracking basis is temporarily unavailable.
	vr_aimmode.value = VR_AIMMODE_MOUSE_MYAW;
	V_TrackedAimModeChanged (NULL);
	cl.viewangles[YAW] = 20;
	V_UpdateTrackedAim ();
	near_value (tracked_view_angles[YAW], 35);
	V_RebaseTrackedAim ();
	test_frame.devices[0].valid = 0;
	cl.viewangles[YAW] += 5;
	V_UpdateTrackedAim ();
	assert (tracked_reference_pending);
	head_yaw (75);
	test_frame.devices[0].valid = 1;
	V_UpdateTrackedAim ();
	near_value (cl.viewangles[YAW], 25);
	near_value (tracked_view_angles[YAW], 40);
	assert (!tracked_reference_pending);
	// Mode changes clear only mode-specific requests, retaining the rebase.
	V_RebaseTrackedAim ();
	vr_aimmode.value = VR_AIMMODE_MOUSE_MYAW_MPITCH;
	V_TrackedAimModeChanged (NULL);
	assert (tracked_reference_pending);
	head_yaw (90);
	V_UpdateTrackedAim ();
	near_value (tracked_view_angles[YAW], 40);
	// Authoritative absolute and relative angles retain their distinct meaning.
	vr_aimmode.value = VR_AIMMODE_HEAD_MYAW;
	V_TrackedAimModeChanged (NULL);
	cl.viewangles[YAW] = 120;
	cl.viewangles[PITCH] = 6;
	V_SetTrackedAngles (cl.viewangles);
	cl.fixangle_time = cl.mtime[0];
	V_UpdateTrackedAim ();
	near_value (cl.viewangles[PITCH], 6);
	near_value (tracked_view_angles[PITCH], 0);
	near_value (tracked_view_angles[YAW], 120);
	cl.fixangle_time = -1;
	V_UpdateTrackedAim ();
	near_value (cl.viewangles[PITCH], 0);
	vec3_t server_delta = {0, 30, 0};
	cl.viewangles[YAW] += 30;
	V_TrackedAngleDelta (server_delta);
	V_UpdateTrackedAim ();
	near_value (cl.viewangles[YAW], 150);
	near_value (tracked_view_angles[YAW], 150);
	// Centerview copies visible aim without clearing tracking orientation.
	vr_aimmode.value = VR_AIMMODE_MOUSE_MYAW;
	V_TrackedAimModeChanged (NULL);
	cl.viewangles[YAW] = 10;
	V_UpdateTrackedAim ();
	float before_yaw = tracked_yaw;
	float before_orientation = tracked_previous_orientation[YAW];
	V_StartPitchDrift ();
	near_value (cl.viewangles[YAW], tracked_view_angles[YAW]);
	near_value (tracked_previous_aim[YAW], cl.viewangles[YAW]);
	near_value (tracked_yaw, before_yaw);
	near_value (tracked_previous_orientation[YAW], before_orientation);
	// An accepted server target survives unavailable tracking and takes
	// precedence over a simultaneous reference-origin rebase.
	vr_aimmode.value = VR_AIMMODE_CONTROLLER;
	V_TrackedAimModeChanged (NULL);
	cl.viewangles[YAW] = 270;
	V_SetTrackedAngles (cl.viewangles);
	cl.stats[STAT_WEAPON] = 1;
	V_RequestTrackedServerYaw (270);
	test_frame.devices[0].valid = 0;
	V_UpdateTrackedAim ();
	assert (tracked_server_yaw_pending);
	V_RebaseTrackedAim ();
	head_yaw (135);
	test_frame.devices[0].valid = 1;
	V_UpdateTrackedAim ();
	near_value (tracked_view_angles[YAW], 270);
	near_value (cl.viewangles[YAW], 270);
	assert (!tracked_server_yaw_pending && !tracked_reference_pending);
	// An expired angle lock cannot preserve an obsolete gameplay yaw target.
	V_RequestTrackedServerYaw (90);
	test_frame.devices[0].valid = 0;
	cl.stats[STAT_WEAPON] = 0;
	V_ValidateTrackedServerYaw (); // completed message, even if no frame runs
	cl.stats[STAT_WEAPON] = 1;
	assert (!tracked_server_yaw_pending);
	V_PushTrackedYaw ();
	cl.stats[STAT_WEAPON] = 0;
	V_ValidateTrackedServerYaw ();
	assert (tracked_server_yaw_pending); // standalone setview is distinct
	cl.intermission = 1;
	V_ValidateTrackedServerYaw ();
	assert (!tracked_server_yaw_pending);
	cl.intermission = 0;
	test_frame.devices[0].valid = 1;
	// Locked commands retain their authority while visual head movement
	// accumulates. Unlock publishes it once, including repeated static poses.
	for (int mode = VR_AIMMODE_HEAD_MYAW; mode <= VR_AIMMODE_HEAD_MYAW_MPITCH; ++mode)
	{
		V_ResetTrackedAim ();
		vr_aimmode.value = mode;
		VectorCopy (vec3_origin, cl.viewangles);
		cl.viewangles[YAW] = 90;
		head_yaw (0);
		cl.fixangle_time = -1;
		V_UpdateTrackedAim ();
		cl.fixangle_time = cl.mtime[0];
		for (int sample = 1; sample <= 3; ++sample)
		{
			head_yaw (sample == 1 ? 10 : 20);
			V_UpdateTrackedAim ();
			near_value (cl.viewangles[YAW], 90);
			near_value (tracked_view_angles[YAW], sample == 1 ? 100 : 110);
		}
		cl.fixangle_time = -1;
		for (int sample = 0; sample < 2; ++sample)
		{
			V_UpdateTrackedAim ();
			usercmd_t command;
			CL_BaseMove (&command);
			near_value (command.viewangles[YAW], 110);
			near_value (tracked_view_angles[YAW], 110);
		}
	}
	// Centerview between lock expiry and the next rendered sample must not
	// replay an aim correction it has already copied into command angles.
	V_ResetTrackedAim ();
	vr_aimmode.value = VR_AIMMODE_HEAD_MYAW;
	cl.viewangles[YAW] = 90;
	head_yaw (0);
	V_UpdateTrackedAim ();
	cl.fixangle_time = cl.mtime[0];
	head_yaw (20);
	V_UpdateTrackedAim ();
	near_value (tracked_withheld_aim[YAW], 20);
	cl.fixangle_time = -1;
	V_StartPitchDrift ();
	V_UpdateTrackedAim ();
	near_value (cl.viewangles[YAW], 110);
	near_value (tracked_view_angles[YAW], 110);
	// Real chase orientation follows visual aim while retaining collision
	// position. A paused base receives only the subsequent head-angle delta.
	for (int mode = VR_AIMMODE_MOUSE_MYAW; mode <= VR_AIMMODE_CONTROLLER; mode += 1)
	{
		if (mode == VR_AIMMODE_BLENDED || mode == VR_AIMMODE_BLENDED_NOPITCH)
			continue;
		V_ResetTrackedAim ();
		vr_aimmode.value = mode;
		VectorCopy (vec3_origin, cl.viewangles);
		cl.viewangles[YAW] = 20;
		head_yaw (0);
		V_UpdateTrackedAim ();
		head_yaw (15);
		V_UpdateTrackedAim ();
		chase_active.value = 1;
		R_InvalidateStereoReference ();
		V_CalcRefdef ();
		near_value (r_refdef.vieworg[2], 30);
		near_value (r_refdef.viewangles[YAW], 35);
		const float prepared_yaw = r_refdef.viewangles[YAW];
		R_PrepareStereoFrame ();
		near_value (r_refdef.viewangles[YAW], prepared_yaw);
		R_RestoreStereoView ();
		V_UpdateTrackedAim (); // consume pending origin rebase before motion
		head_yaw (25);
		V_UpdateTrackedAim ();
		R_PrepareStereoFrame ();
		near_value (r_refdef.viewangles[YAW], prepared_yaw + 10);
		near_value (r_refdef.vieworg[2], 30);
		R_RestoreStereoView ();
	}
	chase_active.value = 0;
	// Clearing a client forgets the old world and prepared-base eligibility.
	V_ResetTrackedAim ();
	assert (!base_player_view && !tracked_aim_ready);
	cl.viewangles[YAW] = 180;
	V_UpdateTrackedAim ();
	near_value (tracked_view_angles[YAW], 180);
	// A paused new client must prepare its own base, not subtract the previous
	// map's aim snapshot. Later paused frames keep that newly prepared base.
	cl.paused = true;
	assert (!base_angles_valid);
	V_SetupFrame ();
	assert (base_angles_valid && base_player_view);
	R_PrepareStereoFrame ();
	near_value (fabsf (r_refdef.viewangles[YAW]), 180);
	R_RestoreStereoView ();
	player.origin[0] += 100;
	const float prepared_x = r_refdef.vieworg[0];
	V_SetupFrame ();
	near_value (r_refdef.vieworg[0], prepared_x);
	cl.paused = false;
	cls.demoplayback = true;
	cl.viewangles[YAW] = 77;
	V_UpdateTrackedAim ();
	near_value (cl.viewangles[YAW], 77);
	cls.demoplayback = false;
	// Local turns use one effective basis before submission and are committed
	// once after origin rebasing, in every inherited aim resolver mode.
	test_frame.focused = 1;
	cl.fixangle_time = -1;
	cl.intermission = 0;
	for (int mode = 1; mode <= 7; ++mode)
	{
		vr_aimmode.value = mode;
		VectorCopy (vec3_origin, cl.viewangles);
		head_yaw (0);
		V_ResetTrackedAim ();
		V_UpdateTrackedAim ();
		assert (V_TurnTrackedYaw (-45));
		vec3_t mapped;
		assert (V_TrackedMovementAngles (VR_MOVEMENT_MODE_FOLLOW_HEAD, 0, mapped));
		near_value (mapped[YAW], -45);
		near_value (tracked_previous_orientation[YAW], 0);
		V_RebaseTrackedAim ();
		head_yaw (70);
		assert (!V_TrackedMovementAngles (VR_MOVEMENT_MODE_FOLLOW_HEAD, 0, mapped));
		V_UpdateTrackedAim ();
		near_value (tracked_local_yaw, 0);
		near_value (tracked_view_angles[YAW], -45);
		V_UpdateTrackedAim ();
		near_value (tracked_view_angles[YAW], -45);
	}
	// Relative authority composes with an uncommitted turn; absolute authority
	// discards it. Neither path leaves a sampled movement record eligible.
	int old_invalidations = motion_invalidations;
	assert (V_TurnTrackedYaw (-15));
	vec3_t relative = {0, 30, 0};
	V_TrackedAngleDelta (relative);
	near_value (tracked_local_yaw, -15);
	assert (motion_invalidations > old_invalidations);
	V_UpdateTrackedAim ();
	near_value (tracked_view_angles[YAW], -30);
	assert (V_TurnTrackedYaw (-45));
	cl.viewangles[YAW] = 90;
	V_SetTrackedAngles (cl.viewangles);
	cl.stats[STAT_WEAPON] = 1;
	V_RequestTrackedServerYaw (90);
	near_value (tracked_local_yaw, 0);
	cl.fixangle_time = cl.mtime[0];
	assert (!V_TurnTrackedYaw (-45));
	V_UpdateTrackedAim ();
	near_value (tracked_view_angles[YAW], 90);
	cl.fixangle_time = -1;
	test_frame.focused = 0;
	assert (!V_TurnTrackedYaw (-45));
	test_roomscale_eye_anchor ();
	puts ("Production stereo camera: eye separation, pause restoration, skipped reference invalidation and abort GPU-drain boundary passed");
	puts ("Inherited floor/scale/comfort: floor height, crouch, pitched basis, paused viewheight, LOCAL fallback and desktop gates passed");
	puts ("Head aiming: actual command angles and paused visual view contain one head rotation");
	puts ("Aim transitions: reference loss, mode changes, authoritative angles, centerview, pending cancellation/priority, locked accumulation, real chase and client clear passed");
	puts ("Local turning: effective command basis, all-mode rebase retention, single commit and authority precedence passed");
	puts ("Roomscale camera anchor: pending/sent private body motion removes duplicate horizontal HMD offset; public and vertical paths remain distinct");
}
