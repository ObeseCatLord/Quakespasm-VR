/* Exercise the production SDL resize boundary without a Vulkan device. */
#include "../Quake/gl_vidsdl.c"

vulkanglobals_t vulkan_globals;
static int		console_rescales;
static void		rescale (cvar_t *var)
{
	++console_rescales;
}
cvar_t *Cvar_FindVar (const char *name)
{
	static cvar_t scale;
	assert (!strcmp (name, "scr_conscale"));
	scale.callback = rescale;
	return &scale;
}
int main (void)
{
	vulkan_globals.stereo_active = false;
	VID_WindowSizeChanged (640, 480);
	assert (vid.width == 640 && vid.height == 480 && vid.restart_next_frame && console_rescales == 1);
	// Reproduce a queued desktop-size event arriving after XR has attached.
	vulkan_globals.stereo_active = true;
	vid.width = 896;
	vid.height = 1007;
	vid.restart_next_frame = false;
	VID_WindowSizeChanged (640, 480);
	assert (vid.width == 896 && vid.height == 1007 && !vid.restart_next_frame && console_rescales == 1);
	assert (openxr_desktop_width == 640 && openxr_desktop_height == 480);
	VID_WindowSizeChanged (800, 600);
	assert (vid.width == 896 && vid.height == 1007 && openxr_desktop_width == 800 && openxr_desktop_height == 600);
	vulkan_globals.stereo_active = false;
	VID_WindowSizeChanged (1024, 768);
	assert (vid.width == 1024 && vid.height == 768 && vid.restart_next_frame && console_rescales == 2);
	puts ("Production resize boundary: desktop resize preserved, delayed window events retain XR target dimensions");
}
