/* Exercise the production SDL focus owner across mirror and XR transitions. */
#include "../Quake/in_sdl.c"
#include <assert.h>

vulkanglobals_t vulkan_globals;
static qboolean muted, video_focused;
void S_BlockSound (void) { muted = true; }
void S_UnblockSound (void) { muted = false; }
void VID_FocusGained (void) { video_focused = true; }
void VID_FocusLost (void) { video_focused = false; }
qboolean Key_TextEntry (void) { return false; }
void Con_Printf (const char *format, ...) { (void)format; }
void Con_DPrintf (const char *format, ...) { (void)format; }
double Sys_DoubleTime (void) { return 0.0; }
void *VID_GetWindow (void) { return NULL; }

int main (void)
{
	IN_WindowFocusChanged (true);
	assert (!muted && video_focused);
	IN_WindowFocusChanged (false);
	assert (muted && !video_focused);
	/* Attachment must repair a prior desktop mute without a focus event. */
	vulkan_globals.stereo_active = true;
	IN_UpdateInputMode ();
	assert (!muted && !video_focused);
	IN_WindowFocusChanged (true);
	IN_WindowFocusChanged (false);
	assert (!muted && !video_focused);
	/* Detachment restores desktop muting, also without a new SDL event. */
	vulkan_globals.stereo_active = false;
	IN_UpdateInputMode ();
	assert (muted && !video_focused);
	IN_WindowFocusChanged (true);
	assert (!muted && video_focused);
	return 0;
}
