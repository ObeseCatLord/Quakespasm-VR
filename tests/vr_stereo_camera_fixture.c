/* Execute the production renderer camera adapter and host-abort cleanup.
 * GPU waiting is spied: this verifies the drain boundary, not a Vulkan fence. */
#include "../Quake/gl_rmain.c"
#include "../Quake/gl_screen.c"
#include "../Quake/view.c"

vulkanglobals_t		 vulkan_globals;
client_state_t cl;
client_static_t cls;
double host_frametime;
qboolean noclip_anglehack;
cvar_t chase_active, cl_forwardspeed, lookspring;
qboolean CL_AngleLocked (void) { return false; }
void Chase_UpdateForDrawing (void)
{
	// Simulate the completed collision-traced base. The live smoke also runs
	// the real donor chase code; this spy isolates the floor-classification bug.
	r_refdef.vieworg[0] = 10;
	r_refdef.vieworg[1] = 20;
	r_refdef.vieworg[2] = 30;
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
static void restore (void)
{
	R_RestoreStereoView ();
	near_value (r_refdef.vieworg[0], 100);
	near_value (r_refdef.vieworg[1], 200);
	near_value (r_refdef.vieworg[2], 300);
	for (int i = 0; i < 3; ++i)
		near_value (r_refdef.viewangles[i], 0);
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
	puts ("Production stereo camera: eye separation, pause restoration, skipped reference invalidation and abort GPU-drain boundary passed");
	puts ("Inherited floor/scale/comfort: floor height, crouch, pitched basis, paused viewheight, LOCAL fallback and desktop gates passed");
}
