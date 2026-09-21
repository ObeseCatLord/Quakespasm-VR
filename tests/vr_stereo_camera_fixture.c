/* Execute the production renderer camera adapter and host-abort cleanup.
 * GPU waiting is spied: this verifies the drain boundary, not a Vulkan fence. */
#include "../Quake/gl_rmain.c"
#include "../Quake/gl_screen.c"

vulkanglobals_t		 vulkan_globals;
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
	vulkan_globals.stereo_active = true;
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
	near_value (r_stereo_origins[0][1], 200 + .032f * VR_STEREO_UNITS_PER_METRE);
	near_value (r_stereo_origins[1][1], 200 - .032f * VR_STEREO_UNITS_PER_METRE);
	restore ();
	// Repeated paused-camera rendering must not accumulate head translation.
	test_frame.devices[0].matrix[0][3] = .25f;
	for (int frame = 0; frame < 3; ++frame)
	{
		R_PrepareStereoFrame ();
		near_value (r_refdef.vieworg[1], 200 - .25f * VR_STEREO_UNITS_PER_METRE);
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
	puts ("Production stereo camera: eye separation, pause restoration, skipped reference invalidation and abort GPU-drain boundary passed");
}
