/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2016 Axel Gneiting

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
// gl_vidsdl.c -- SDL vid component

#include "quakedef.h"
#include "r_ssao.h"
#define NO_SDL_VULKAN_TYPEDEFS
#include "cfgfile.h"
#include "bgmusic.h"
#include "resource.h"
#include "palette.h"
#include "menu.h"
#include "steam.h"
#include "vr_openxr.h"
#include "vr_openxr_vulkan.h"
#include "vr_openxr_math.h"
#include "vr_input.h"
#include "vr_foveation_rate_map.h"
#include "r_vrik_render.h"

#ifdef USE_SDL3
#include <SDL3/SDL_vulkan.h>
#else
#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#include <SDL2/SDL_vulkan.h>
#else
#include "SDL_vulkan.h"
#endif
#endif

#ifdef _WIN32
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

#include <float.h>
#include <limits.h>
#include <time.h>

#define MAX_MODE_LIST  600 // johnfitz -- was 30
#define MAX_BPPS_LIST  5
#define MAX_RATES_LIST 20
#define MAXWIDTH	   10000
#define MAXHEIGHT	   10000

#define MAX_SWAP_CHAIN_IMAGES 8
#define REQUIRED_COLOR_BUFFER_FEATURES                                                                                             \
	(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | \
	 VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)

#define DEFAULT_REFRESHRATE 60

typedef struct
{
	int	  width;
	int	  height;
	float refreshrate;
} vmode_t;

static vmode_t *modelist = NULL;
static int		nummodes;

static qboolean vid_initialized = false;
static qboolean has_focus = true;
static uint32_t num_images_acquired = 0;
static qboolean openxr_vulkan_binding = false;
static uint32_t openxr_vulkan_api_version;
static uint32_t openxr_vulkan_minimum_version;
static qboolean openxr_attach_attempted;
static qboolean openxr_session_wanted;
static qboolean openxr_session_change_pending;
static qboolean openxr_frame_submitted;
static vrxr_frame_t openxr_frame;
static vrf_policy_state_t openxr_foveation_policy;
static VkImageView *openxr_image_views;
static VkImageView *openxr_density_image_views;
static uint32_t openxr_image_count;
static uint32_t openxr_image_index;
static qboolean openxr_density_eye_active;
static qboolean openxr_density_backend_failed;
static VkOffset2D openxr_density_offsets[2];
static int openxr_desktop_width, openxr_desktop_height;
static VkExtent2D openxr_mirror_extent;
static VkImage openxr_mirror_snapshots[DOUBLE_BUFFERED];
static vulkan_memory_t openxr_mirror_memory[DOUBLE_BUFFERED];
static VkCommandPool openxr_mirror_command_pool;
static VkCommandBuffer openxr_mirror_commands[DOUBLE_BUFFERED];
static VkFence openxr_mirror_fences[DOUBLE_BUFFERED];
static VkFence openxr_mirror_acquire_fence;
static qboolean openxr_mirror_acquire_pending;
static uint32_t openxr_mirror_acquire_image;
static int openxr_mirror_acquire_slot;
static qboolean openxr_mirror_acquire_suboptimal;
static qboolean openxr_mirror_submitted[DOUBLE_BUFFERED];
static qboolean openxr_mirror_slot_available[DOUBLE_BUFFERED];
static qboolean openxr_mirror_snapshot_initialized[DOUBLE_BUFFERED];
static qboolean openxr_mirror_ready, openxr_mirror_copy_ready;
static int openxr_mirror_frame_slot;

static SDL_Window *draw_context;

static qboolean vid_locked = false; // johnfitz
static qboolean vid_changed = false;

static void VID_Menu_RebuildModeList (void); // johnfitz
static void VID_Restart_f (void);

static void ClearAllStates (void);
static void GL_InitInstance (void);
static void GL_InitDevice (void);
static void GL_OpenXRPrepareVulkan (uint32_t loader_api_version);
static void GL_OpenXRCreationFailed (void);
static bool GL_CreateFrameBuffers (void);
static void GL_CreateOITBuffers (void);
static void GL_DestroyOITBuffers (void);
static void GL_DestroyRenderResources (void);
static qboolean GL_CreateFragmentShadingRateImage (void);
static void GL_DestroyFragmentShadingRateImage (void);
static void GL_DestroyMirrorResources (void);
static void GL_DestroySwapChainResources (void);

viddef_t		vid; // global video state
modestate_t		modestate = MS_UNINIT;
extern qboolean scr_initialized;

extern VkAccelerationStructureKHR bmodel_tlas;

//====================================

// johnfitz -- new cvars
static cvar_t vid_fullscreen = {"vid_fullscreen", "1", CVAR_ARCHIVE}; // QuakeSpasm, was "1"
static cvar_t vid_width = {"vid_width", "1280", CVAR_ARCHIVE};		  // QuakeSpasm, was 640
static cvar_t r_width = {"r_width", "-1", CVAR_ARCHIVE};
static cvar_t r_height = {"r_height", "-1", CVAR_ARCHIVE};
static cvar_t r_upscalefilter = {"r_upscalefilter", "0", CVAR_ARCHIVE};
static cvar_t vid_height = {"vid_height", "720", CVAR_ARCHIVE}; // QuakeSpasm, was 480
static cvar_t vid_refreshrate = {"vid_refreshrate", "60", CVAR_ARCHIVE};
static cvar_t vid_vsync = {"vid_vsync", "1", CVAR_ARCHIVE};
static cvar_t vid_maxframelatency = {"vid_maxframelatency", "2", CVAR_ARCHIVE};		// max frames queued for display under vsync, 0 = uncapped
static cvar_t vid_desktopfullscreen = {"vid_desktopfullscreen", "1", CVAR_ARCHIVE}; // QuakeSpasm
static cvar_t vid_borderless = {"vid_borderless", "0", CVAR_ARCHIVE};				// QuakeSpasm
cvar_t		  vid_palettize = {"vid_palettize", "0", CVAR_ARCHIVE};
cvar_t		  vid_filter = {"vid_filter", "1", CVAR_ARCHIVE};
cvar_t		  vid_anisotropic = {"vid_anisotropic", "1", CVAR_ARCHIVE}; // 0=off, 1=hardware max, >1=requested level
cvar_t		  vid_fsaa = {"vid_fsaa", "4", CVAR_ARCHIVE};
cvar_t		  vid_fsaamode = {"vid_fsaamode", "0", CVAR_ARCHIVE};
cvar_t		  vid_gamma = {"gamma", "0.9", CVAR_ARCHIVE};		// johnfitz -- moved here from view.c
cvar_t		  vid_contrast = {"contrast", "1.4", CVAR_ARCHIVE}; // QuakeSpasm, MarkV
#if defined(_DEBUG)
static cvar_t r_raydebug = {"r_raydebug", "0", 0};
#endif

static VkInstance				vulkan_instance;
static VkPhysicalDevice			vulkan_physical_device;
static VkSurfaceKHR				vulkan_surface;
static qboolean					surface_lost;
static VkSurfaceCapabilitiesKHR vulkan_surface_capabilities;
static VkSwapchainKHR			vulkan_swapchain;

static uint32_t			num_swap_chain_images;
static qboolean			render_resources_created = false;
static uint32_t			current_cb_index;
typedef struct
{
	VkBuffer buffer;
	VkDeviceSize offset;
	uint32_t vertex_count;
} xr_hidden_area_draw_t;
static xr_hidden_area_draw_t hidden_area_draws[DOUBLE_BUFFERED];
static xr_hidden_area_draw_t GL_PrepareHiddenAreaMesh (void);
static VkCommandPool	primary_command_pools[PCBX_NUM];
static VkCommandPool   *secondary_command_pools[SCBX_NUM];
static VkCommandPool	transient_command_pool;
static VkCommandBuffer	primary_command_buffers[PCBX_NUM][DOUBLE_BUFFERED];
static VkCommandBuffer *secondary_command_buffers[SCBX_NUM][DOUBLE_BUFFERED];
static VkFence			command_buffer_fences[DOUBLE_BUFFERED];
static qboolean			frame_submitted[DOUBLE_BUFFERED];
static VkQueryPool		timestamp_query_pool;
static uint32_t			timestamp_valid_bits;
static qboolean			timestamps_written[DOUBLE_BUFFERED];
static qboolean			ssao_timestamps_written[DOUBLE_BUFFERED];
static qboolean			frame_timing_enabled;
static VkSemaphore		image_aquired_semaphores[DOUBLE_BUFFERED];
static VkSemaphore		draw_complete_semaphores[MAX_SWAP_CHAIN_IMAGES];
static VkImage			swapchain_images[MAX_SWAP_CHAIN_IMAGES];
static VkImageView		swapchain_images_views[MAX_SWAP_CHAIN_IMAGES];
static VkImage			depth_buffer;
static vulkan_memory_t	depth_buffer_memory;
static VkImageView		depth_buffer_view;
static vulkan_memory_t	color_buffers_memory[NUM_COLOR_BUFFERS];
static VkImageView		color_buffers_view[NUM_COLOR_BUFFERS];
static vulkan_memory_t	oit_accum_buffer_memory;
static vulkan_memory_t	oit_reveal_buffer_memory;
static VkImageView		oit_accum_buffer_view;
static VkImageView		oit_reveal_buffer_view;
static vulkan_memory_t	mboit_b0_buffer_memory;
static vulkan_memory_t	mboit_moments0_buffer_memory;
static vulkan_memory_t	mboit_color_buffer_memory;
static VkImageView		mboit_b0_buffer_view;
static VkImageView		mboit_moments0_buffer_view;
static VkImageView		mboit_color_buffer_view;
static VkImage			fragment_shading_rate_image;
static vulkan_memory_t	fragment_shading_rate_image_memory;
static VkImageView		fragment_shading_rate_image_view;
static VkExtent2D		fragment_shading_rate_image_extent;
static uint32_t			fragment_shading_rate_image_layers;
static byte				*fragment_shading_rate_map;
static byte				*fragment_shading_rate_uploaded_map;
static size_t			fragment_shading_rate_map_size;
static qboolean		fragment_shading_rate_image_initialized;
static VkImage			msaa_color_buffer;
static vulkan_memory_t	msaa_color_buffer_memory;
static VkImageView		msaa_color_buffer_view;
static VkDescriptorSet	postprocess_descriptor_set;
static VkImage			ui_color_buffer;
static VkImageView		ui_color_buffer_view;
static vulkan_memory_t	ui_color_buffer_memory;
static VkDescriptorSet	scene_upscale_descriptor_set;
static VkDescriptorSet	wboit_resolve_descriptor_set;
static VkBuffer			palette_colors_buffer;
static VkBufferView		palette_buffer_view;
static VkBuffer			palette_octree_buffer;

static PFN_vkGetInstanceProcAddr					  fpGetInstanceProcAddr;
static PFN_vkGetDeviceProcAddr						  fpGetDeviceProcAddr;
static PFN_vkDestroySurfaceKHR						  fpDestroySurfaceKHR;
static PFN_vkGetPhysicalDeviceSurfaceSupportKHR		  fpGetPhysicalDeviceSurfaceSupportKHR;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR  fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
static PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR fpGetPhysicalDeviceSurfaceCapabilities2KHR;
static PFN_vkGetPhysicalDeviceSurfaceFormatsKHR		  fpGetPhysicalDeviceSurfaceFormatsKHR;
static PFN_vkGetPhysicalDeviceSurfacePresentModesKHR  fpGetPhysicalDeviceSurfacePresentModesKHR;
static PFN_vkCreateSwapchainKHR						  fpCreateSwapchainKHR;
static PFN_vkDestroySwapchainKHR					  fpDestroySwapchainKHR;
static PFN_vkGetSwapchainImagesKHR					  fpGetSwapchainImagesKHR;
static PFN_vkAcquireNextImageKHR					  fpAcquireNextImageKHR;
static PFN_vkQueuePresentKHR						  fpQueuePresentKHR;
#if defined(VK_KHR_present_wait2)
static PFN_vkWaitForPresent2KHR fpWaitForPresent2KHR;
static uint64_t					current_present_id;
static qboolean					swapchain_present_wait;
#endif
static PFN_vkEnumerateInstanceVersion	  fpEnumerateInstanceVersion;
static PFN_vkGetPhysicalDeviceFeatures2	  fpGetPhysicalDeviceFeatures2;
static PFN_vkGetPhysicalDeviceProperties2 fpGetPhysicalDeviceProperties2;
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
static PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR fpGetPhysicalDeviceFragmentShadingRatesKHR;
static VkImageFormatProperties fragment_shading_rate_image_format_properties;
static VkSampleCountFlagBits fragment_shading_rate_sample_count;
static qboolean fragment_shading_rate_sample_query_known;
static qboolean fragment_shading_rate_1x1_supported;
static qboolean fragment_shading_rate_2x2_supported;
static qboolean fragment_shading_rate_4x4_supported;
#endif
#if defined(VK_EXT_full_screen_exclusive)
static PFN_vkAcquireFullScreenExclusiveModeEXT fpAcquireFullScreenExclusiveModeEXT;
static PFN_vkReleaseFullScreenExclusiveModeEXT fpReleaseFullScreenExclusiveModeEXT;
#endif

#ifdef _DEBUG
static PFN_vkCreateDebugUtilsMessengerEXT fpCreateDebugUtilsMessengerEXT;
static PFN_vkDestroyDebugUtilsMessengerEXT fpDestroyDebugUtilsMessengerEXT;
PFN_vkSetDebugUtilsObjectNameEXT		  fpSetDebugUtilsObjectNameEXT;

VkDebugUtilsMessengerEXT debug_utils_messenger;

VkBool32 VKAPI_PTR DebugMessageCallback (
	VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_types,
	const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data)
{
	Sys_Printf ("%s\n", callback_data->pMessage);
	return VK_FALSE;
}
#endif

// Swap chain
static uint32_t current_swapchain_buffer;

// Screenshots
qboolean	take_screenshot = false;
static char screenshot_ext[4];
char		screenshot_imagename[MAX_OSPATH]; // johnfitz -- was [80]
int			screenshot_quality;

task_handle_t prev_end_rendering_task = INVALID_TASK_HANDLE;

#define GET_INSTANCE_PROC_ADDR(entrypoint)                                                              \
	{                                                                                                   \
		fp##entrypoint = (PFN_vk##entrypoint)fpGetInstanceProcAddr (vulkan_instance, "vk" #entrypoint); \
		if (fp##entrypoint == NULL)                                                                     \
			Sys_Error ("vkGetInstanceProcAddr failed to find vk" #entrypoint);                          \
	}

#define GET_GLOBAL_INSTANCE_PROC_ADDR(_var, entrypoint)                                               \
	{                                                                                                 \
		vulkan_globals._var = (PFN_##entrypoint)fpGetInstanceProcAddr (vulkan_instance, #entrypoint); \
		if (vulkan_globals._var == NULL)                                                              \
			Sys_Error ("vkGetInstanceProcAddr failed to find " #entrypoint);                          \
	}

#define GET_DEVICE_PROC_ADDR(entrypoint)                                                                    \
	{                                                                                                       \
		fp##entrypoint = (PFN_vk##entrypoint)fpGetDeviceProcAddr (vulkan_globals.device, "vk" #entrypoint); \
		if (fp##entrypoint == NULL)                                                                         \
			Sys_Error ("vkGetDeviceProcAddr failed to find vk" #entrypoint);                                \
	}

#define GET_GLOBAL_DEVICE_PROC_ADDR(_var, entrypoint)                                                     \
	{                                                                                                     \
		vulkan_globals._var = (PFN_##entrypoint)fpGetDeviceProcAddr (vulkan_globals.device, #entrypoint); \
		if (vulkan_globals._var == NULL)                                                                  \
			Sys_Error ("vkGetDeviceProcAddr failed to find " #entrypoint);                                \
	}

/*
================
VID_Gamma_Init -- call on init
================
*/
static void VID_Gamma_Init (void)
{
	Cvar_RegisterVariable (&vid_gamma);
	Cvar_RegisterVariable (&vid_contrast);
}

/*
======================
VID_GetCurrentWidth
======================
*/
static int VID_GetCurrentWidth (void)
{
	int w = 0, h = 0;
#ifdef USE_SDL3
	SDL_GetWindowSizeInPixels (draw_context, &w, &h);
#else
	SDL_Vulkan_GetDrawableSize (draw_context, &w, &h);
#endif
	return w;
}

/*
=======================
VID_GetCurrentHeight
=======================
*/
static int VID_GetCurrentHeight (void)
{
	int w = 0, h = 0;
#ifdef USE_SDL3
	SDL_GetWindowSizeInPixels (draw_context, &w, &h);
#else
	SDL_Vulkan_GetDrawableSize (draw_context, &w, &h);
#endif
	return h;
}

/*
================
VID_GetCurrentWindowWidth

Window size in points; vid.width / vid.height are in pixels.
================
*/
static int VID_GetCurrentWindowWidth (void)
{
	int w = 0, h = 0;
	SDL_GetWindowSize (draw_context, &w, &h);
	return w;
}

/*
================
VID_GetCurrentWindowHeight
================
*/
static int VID_GetCurrentWindowHeight (void)
{
	int w = 0, h = 0;
	SDL_GetWindowSize (draw_context, &w, &h);
	return h;
}

/*
====================
VID_GetCurrentRefreshRate
====================
*/
static SDL_DisplayMode VID_GetDesktopDisplayMode (void)
{
#ifdef USE_SDL3
	const SDL_DisplayID	   display = draw_context ? SDL_GetDisplayForWindow (draw_context) : SDL_GetPrimaryDisplay ();
	const SDL_DisplayMode *mode = display ? SDL_GetDesktopDisplayMode (display) : NULL;
	if (!mode)
		Sys_Error ("Could not get desktop display mode: %s", SDL_GetError ());
	return *mode;
#else
	const int		display = draw_context ? SDL_GetWindowDisplayIndex (draw_context) : 0;
	SDL_DisplayMode mode;
	if (display < 0 || SDL_GetDesktopDisplayMode (display, &mode) != 0)
		Sys_Error ("Could not get desktop display mode: %s", SDL_GetError ());
	return mode;
#endif
}

static float VID_GetCurrentRefreshRate (void)
{
#ifdef USE_SDL3
	SDL_DisplayID		   current_display;
	const SDL_DisplayMode *mode;

	current_display = SDL_GetDisplayForWindow (draw_context);
	if (current_display == 0)
		current_display = SDL_GetPrimaryDisplay ();

	mode = SDL_GetCurrentDisplayMode (current_display);
	if (!mode)
		return DEFAULT_REFRESHRATE;

	return mode->refresh_rate;
#else
	int				current_display;
	SDL_DisplayMode mode;

	current_display = SDL_GetWindowDisplayIndex (draw_context);
	if (current_display < 0)
		current_display = 0;

	if (SDL_GetCurrentDisplayMode (current_display, &mode) != 0)
		return DEFAULT_REFRESHRATE;

	return mode.refresh_rate;
#endif
}

/*
====================
VID_GetCurrentBPP
====================
*/
static int VID_GetCurrentBPP (void)
{
	const Uint32 pixelFormat = SDL_GetWindowPixelFormat (draw_context);
	return SDL_BITSPERPIXEL (pixelFormat);
}

/*
====================
VID_GetFullscreen

returns true if we are in regular fullscreen or "desktop fullscren"
====================
*/
static qboolean VID_GetFullscreen (void)
{
	return (SDL_GetWindowFlags (draw_context) & SDL_WINDOW_FULLSCREEN) != 0;
}

/*
====================
VID_GetDesktopFullscreen

returns true if we are specifically in "desktop fullscreen" mode
====================
*/
static qboolean VID_GetDesktopFullscreen (void)
{
#ifdef USE_SDL3
	// In SDL3, check if fullscreen mode is NULL (desktop fullscreen) or has a mode (exclusive fullscreen)
	return SDL_GetWindowFullscreenMode (draw_context) == NULL && (SDL_GetWindowFlags (draw_context) & SDL_WINDOW_FULLSCREEN);
#else
	return (SDL_GetWindowFlags (draw_context) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
}

/*
====================
VID_GetWindow

used by pl_win.c
====================
*/
void *VID_GetWindow (void)
{
	return draw_context;
}

/*
====================
VID_HasMouseOrInputFocus
====================
*/
qboolean VID_HasMouseOrInputFocus (void)
{
	return (SDL_GetWindowFlags (draw_context) & (SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_INPUT_FOCUS)) != 0;
}

/*
====================
VID_IsMinimized
====================
*/
qboolean VID_IsMinimized (void)
{
#ifdef USE_SDL3
	return (SDL_GetWindowFlags (draw_context) & SDL_WINDOW_MINIMIZED) != 0;
#else
	return !(SDL_GetWindowFlags (draw_context) & SDL_WINDOW_SHOWN);
#endif
}

/*
================
VID_SDL_GetDisplayMode

Returns a pointer to a SDL_DisplayMode structure with the requested size.
Returns NULL if the size is not available at all.

SDL3: searches the display the window is on (the primary display before the
window exists) and picks the available mode with the closest refresh rate.
SDL2: requires an exact refresh rate match on the primary display.

This is passed to SDL_SetWindowFullscreenMode to specify a pixel format
with the requested bpp. If we didn't care about bpp we could just pass NULL.
================
*/
static const SDL_DisplayMode *VID_SDL_GetDisplayMode (int width, int height, float refreshrate)
{
#ifdef USE_SDL3
	static SDL_DisplayMode result;
	qboolean			   found = false;
	float				   best_dist = FLT_MAX;
	int					   i;

	SDL_DisplayID display = draw_context ? SDL_GetDisplayForWindow (draw_context) : 0;
	if (display == 0)
		display = SDL_GetPrimaryDisplay ();

	int				  count = 0;
	SDL_DisplayMode **modes = (SDL_DisplayMode **)SDL_GetFullscreenDisplayModes (display, &count);
	if (!modes)
		return NULL;

	for (i = 0; i < count; i++)
	{
		const SDL_DisplayMode *mode = modes[i];
		if (mode->w != width || mode->h != height || SDL_BITSPERPIXEL (mode->format) < 24)
			continue;

		const float dist = fabsf (mode->refresh_rate - refreshrate);
		if (dist < best_dist)
		{
			best_dist = dist;
			// copy before SDL_free: the mode structs live inside the same
			// allocation as the returned pointer array
			result = *mode;
			found = true;
		}
	}
	SDL_free (modes);
	return found ? &result : NULL;
#else
	static SDL_DisplayMode mode;
	const int			   sdlmodes = SDL_GetNumDisplayModes (0);
	int					   i;

	for (i = 0; i < sdlmodes; i++)
	{
		if (SDL_GetDisplayMode (0, i, &mode) != 0)
			continue;

		if (mode.w == width && mode.h == height && SDL_BITSPERPIXEL (mode.format) >= 24 && mode.refresh_rate == refreshrate)
		{
			return &mode;
		}
	}
	return NULL;
#endif
}

/*
================
VID_ValidMode
================
*/
static qboolean VID_ValidMode (int width, int height, float refreshrate, qboolean fullscreen)
{
	// Borderless desktop fullscreen uses the current desktop mode.
	if (fullscreen && vid_fullscreen.value == 1 && vid_desktopfullscreen.value)
		return true;

	if (width < 320)
		return false;

	if (height < 200)
		return false;

	if (fullscreen && VID_SDL_GetDisplayMode (width, height, refreshrate) == NULL)
		return false;

	return true;
}

/*
================
VID_SetMode
================
*/
static qboolean VID_SetMode (int width, int height, float refreshrate, qboolean fullscreen)
{
	int	   temp;
	Uint32 flags;
	char   caption[50];
	int	   previous_display;

	// so Con_Printfs don't mess us up by forcing vid and snd updates
	temp = scr_disabled_for_loading;
	scr_disabled_for_loading = true;

	CDAudio_Pause ();
	BGM_Pause ();

	q_snprintf (caption, sizeof (caption), ENGINE_NAME_AND_VER);

	/* Create the window if needed, hidden */
	if (!draw_context)
	{
		flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN;

#ifdef USE_SDL3
		flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif

		if (vid_borderless.value)
			flags |= SDL_WINDOW_BORDERLESS;
		else if (!fullscreen)
			flags |= SDL_WINDOW_RESIZABLE;

#ifdef USE_SDL3
		draw_context = SDL_CreateWindow (caption, width, height, flags);
#else
		draw_context = SDL_CreateWindow (caption, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, flags);
#endif
		if (!draw_context)
			Sys_Error ("Couldn't create window: %s", SDL_GetError ());

#ifdef USE_SDL3
		previous_display = 0;
#else
		previous_display = -1;
#endif
	}
	else
	{
#ifdef USE_SDL3
		previous_display = SDL_GetDisplayForWindow (draw_context);
#else
		previous_display = SDL_GetWindowDisplayIndex (draw_context);
#endif
	}

	/* Ensure the window is not fullscreen */
	if (VID_GetFullscreen ())
	{
		qboolean ok;
#ifdef USE_SDL3
		ok = SDL_SetWindowFullscreen (draw_context, false);
#else
		ok = SDL_SetWindowFullscreen (draw_context, 0) == 0;
#endif
		if (!ok)
			Sys_Error ("Couldn't set fullscreen state mode: %s", SDL_GetError ());
	}

	/* Set window size and display mode */
	SDL_SetWindowSize (draw_context, width, height);
	if (previous_display >= 0)
		SDL_SetWindowPosition (draw_context, SDL_WINDOWPOS_CENTERED_DISPLAY (previous_display), SDL_WINDOWPOS_CENTERED_DISPLAY (previous_display));
	else
		SDL_SetWindowPosition (draw_context, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

#ifdef USE_SDL3
	// Set fullscreen mode: NULL for desktop fullscreen, specific mode for exclusive fullscreen
	if (vid_fullscreen.value == 1 && vid_desktopfullscreen.value)
		SDL_SetWindowFullscreenMode (draw_context, NULL);
	else
		SDL_SetWindowFullscreenMode (draw_context, VID_SDL_GetDisplayMode (width, height, refreshrate));
	SDL_SetWindowBordered (draw_context, vid_borderless.value ? false : true);
#else
	SDL_SetWindowDisplayMode (draw_context, VID_SDL_GetDisplayMode (width, height, refreshrate));
	SDL_SetWindowBordered (draw_context, vid_borderless.value ? SDL_FALSE : SDL_TRUE);
#endif

	/* Make window fullscreen if needed, and show the window */

	if (fullscreen)
	{
#ifdef USE_SDL3
		if (!SDL_SetWindowFullscreen (draw_context, true))
			Sys_Error ("Couldn't set fullscreen state mode: %s", SDL_GetError ());
#else
		Uint32 fullscreen_flag = (vid_fullscreen.value == 1 && vid_desktopfullscreen.value) ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN;
		if (SDL_SetWindowFullscreen (draw_context, fullscreen_flag) != 0)
			Sys_Error ("Couldn't set fullscreen state mode: %s", SDL_GetError ());
#endif
	}

	SDL_ShowWindow (draw_context);
	SDL_RaiseWindow (draw_context);

#ifdef USE_SDL3
	// window size, position and fullscreen changes are asynchronous requests
	// on some platforms (X11, Wayland); wait until they are actually applied
	// so the sizes queried below are correct
	SDL_SyncWindow (draw_context);
#endif

	vid.width = VID_GetCurrentWidth ();
	vid.height = VID_GetCurrentHeight ();
	vid.conwidth = vid.width & 0xFFFFFFF8;
	vid.conheight = vid.conwidth * vid.height / vid.width;

	modestate = VID_GetFullscreen () ? MS_FULLSCREEN : MS_WINDOWED;

	CDAudio_Resume ();
	BGM_Resume ();
	scr_disabled_for_loading = temp;

	// fix the leftover Alt from any Alt-Tab or the like that switched us away
	ClearAllStates ();

	vid.recalc_refdef = 1;

	// no pending changes
	vid_changed = false;

	SCR_UpdateRelativeScale ();

	return true;
}

/*
===================
VID_Changed_f -- kristian -- notify us that a value has changed that requires a vid_restart
===================
*/
static void VID_Changed_f (cvar_t *var)
{
	vid_changed = true;
}

/*
===================
VID_FilterChanged_f
===================
*/
static void VID_FilterChanged_f (cvar_t *var)
{
	R_InitSamplers ();
}

/*
===================
VID_FSAAChanged_f
===================
*/
static void VID_FSAAChanged_f (cvar_t *var)
{
	VID_Restart (false);
}

/*
===================
VID_VsyncChanged_f -- vsync only needs the swapchain recreated, apply it immediately
===================
*/
static void VID_VsyncChanged_f (cvar_t *var)
{
	VID_Restart (false);
}

/*
================
VID_Test -- johnfitz -- like vid_restart, but asks for confirmation after switching modes
================
*/
static void VID_Test (void)
{
	int	  old_width, old_height, old_fullscreen;
	float old_refreshrate;

	if (vid_locked || !vid_changed)
		return;
	//
	// now try the switch
	//
	old_width = VID_GetCurrentWindowWidth ();
	old_height = VID_GetCurrentWindowHeight ();
	old_refreshrate = VID_GetCurrentRefreshRate ();
	old_fullscreen = VID_GetFullscreen () ? (vulkan_globals.swap_chain_full_screen_exclusive ? 2 : 1) : 0;
	VID_Restart (true);

	// pop up confirmation dialoge
	if (!SCR_ModalMessage ("Would you like to keep this\nvideo mode? (y/n)\n", 5.0f))
	{
		// revert cvars and mode
		Cvar_SetValueQuick (&vid_width, old_width);
		Cvar_SetValueQuick (&vid_height, old_height);
		Cvar_SetValueQuick (&vid_refreshrate, old_refreshrate);
		Cvar_SetValueQuick (&vid_fullscreen, old_fullscreen);
		VID_Restart (true);
	}
}

/*
================
VID_Unlock -- johnfitz
================
*/
static void VID_Unlock (void)
{
	vid_locked = false;
	VID_SyncCvars ();
}

/*
================
VID_Lock -- ericw

Subsequent changes to vid_* mode settings, and vid_restart commands, will
be ignored until the "vid_unlock" command is run.

Used when changing gamedirs so the current settings override what was saved
in the config.cfg.
================
*/
void VID_Lock (void)
{
	vid_locked = true;
}

//==============================================================================
//
//	Vulkan Stuff
//
//==============================================================================

/*
===============
GL_SetObjectName
===============
*/
void GL_SetObjectName (uint64_t object, VkObjectType object_type, const char *name)
{
#ifdef _DEBUG
	if (fpSetDebugUtilsObjectNameEXT && name)
	{
		ZEROED_STRUCT (VkDebugUtilsObjectNameInfoEXT, nameInfo);
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = object_type;
		nameInfo.objectHandle = object;
		nameInfo.pObjectName = name;
		fpSetDebugUtilsObjectNameEXT (vulkan_globals.device, &nameInfo);
	};
#endif
}

static int GL_CompareVulkanApiVersions (uint32_t left, uint32_t right)
{
	left = VK_MAKE_VERSION (VK_API_VERSION_MAJOR (left), VK_API_VERSION_MINOR (left), 0);
	right = VK_MAKE_VERSION (VK_API_VERSION_MAJOR (right), VK_API_VERSION_MINOR (right), 0);
	return (left > right) - (left < right);
}

static void GL_OpenXRLog (const char *message)
{
	Con_Printf ("%s\n", message);
}

static void GL_OpenXRLockQueue (void *owner)
{
	SDL_LockMutex ((SDL_Mutex *)owner);
}

static void GL_OpenXRUnlockQueue (void *owner)
{
	SDL_UnlockMutex ((SDL_Mutex *)owner);
}

static void GL_OpenXRPrepareVulkan (uint32_t loader_api_version)
{
	const qboolean openxr_requested = COM_CheckParm ("-openxr") && !COM_CheckParm ("-novr");
	if (COM_CheckParm ("-openxr") && COM_CheckParm ("-novr"))
		Con_Printf ("OpenXR bootstrap skipped: -novr takes precedence over -openxr.\n");
	if (!openxr_requested)
		return;

	uint32_t minimum_version = 0;
	uint32_t maximum_version = 0;
	if (!VRXR_PrepareVulkan (GL_OpenXRLog, &minimum_version, &maximum_version))
	{
		Con_Printf ("OpenXR bootstrap unavailable: runtime discovery failed; continuing with desktop Vulkan.\n");
		return;
	}

	minimum_version = VK_MAKE_VERSION (VK_API_VERSION_MAJOR (minimum_version), VK_API_VERSION_MINOR (minimum_version), 0);
	maximum_version = VK_MAKE_VERSION (VK_API_VERSION_MAJOR (maximum_version), VK_API_VERSION_MINOR (maximum_version), 0);
	loader_api_version = VK_MAKE_VERSION (VK_API_VERSION_MAJOR (loader_api_version), VK_API_VERSION_MINOR (loader_api_version), 0);
	openxr_vulkan_api_version = VK_API_VERSION_1_1;
	if (GL_CompareVulkanApiVersions (minimum_version, openxr_vulkan_api_version) > 0)
		openxr_vulkan_api_version = minimum_version;
	if (GL_CompareVulkanApiVersions (maximum_version, openxr_vulkan_api_version) < 0)
		openxr_vulkan_api_version = maximum_version;
	if (GL_CompareVulkanApiVersions (loader_api_version, openxr_vulkan_api_version) < 0)
		openxr_vulkan_api_version = loader_api_version;

	if (!minimum_version || !maximum_version || GL_CompareVulkanApiVersions (minimum_version, maximum_version) > 0 ||
		GL_CompareVulkanApiVersions (openxr_vulkan_api_version, minimum_version) < 0)
	{
		Con_Printf ("OpenXR bootstrap unavailable: runtime minimum Vulkan %u.%u, tested maximum %u.%u; loader supports %u.%u. Continuing with desktop Vulkan.\n",
			VK_API_VERSION_MAJOR (minimum_version), VK_API_VERSION_MINOR (minimum_version), VK_API_VERSION_MAJOR (maximum_version),
			VK_API_VERSION_MINOR (maximum_version), VK_API_VERSION_MAJOR (loader_api_version), VK_API_VERSION_MINOR (loader_api_version));
		VRXR_Shutdown ();
		openxr_vulkan_api_version = 0;
		return;
	}

	openxr_vulkan_minimum_version = minimum_version;
	openxr_vulkan_binding = true;
	openxr_session_wanted = true;
	Con_Printf ("OpenXR bootstrap: requesting Vulkan %u.%u; session is not attached.\n", VK_API_VERSION_MAJOR (openxr_vulkan_api_version),
		VK_API_VERSION_MINOR (openxr_vulkan_api_version));
}

static void GL_ClearOpenXRFragmentShadingRate (void)
{
	vulkan_globals.openxr_fragment_shading_rate_available = false;
	vulkan_globals.openxr_fragment_shading_rate_active = false;
	vulkan_globals.openxr_fragment_density_map_enabled = false;
	vulkan_globals.openxr_fragment_density_map_active = false;
	vulkan_globals.openxr_fragment_density_frame_active = false;
	vulkan_globals.openxr_fragment_density_offset_enabled = false;
	vulkan_globals.openxr_fragment_density_map_max_texel_size = (VkExtent2D){0, 0};
	vulkan_globals.openxr_fragment_density_offset_granularity = (VkExtent2D){0, 0};
	vulkan_globals.openxr_fragment_shading_rate_texel_size.width = 0;
	vulkan_globals.openxr_fragment_shading_rate_texel_size.height = 0;
	vulkan_globals.openxr_layered_shading_rate_attachments = false;
	vulkan_globals.vk_create_render_pass2 = NULL;
	vulkan_globals.vk_cmd_begin_render_pass2 = NULL;
	vulkan_globals.vk_cmd_end_render_pass2 = NULL;
	openxr_density_eye_active = false;
	openxr_density_backend_failed = false;
	memset (openxr_density_offsets, 0, sizeof (openxr_density_offsets));
	vulkan_globals.vk_cmd_set_fragment_shading_rate = NULL;
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
	fpGetPhysicalDeviceFragmentShadingRatesKHR = NULL;
	memset (&fragment_shading_rate_image_format_properties, 0, sizeof (fragment_shading_rate_image_format_properties));
	fragment_shading_rate_sample_count = 0;
	fragment_shading_rate_sample_query_known = false;
	fragment_shading_rate_1x1_supported = false;
	fragment_shading_rate_2x2_supported = false;
	fragment_shading_rate_4x4_supported = false;
#endif
	VRF_ResetPolicy (&openxr_foveation_policy);
}

static uint32_t GL_FragmentShadingRateLog2 (uint32_t value)
{
	uint32_t result = 0;
	while (value > 1)
	{
		value >>= 1;
		++result;
	}
	return result;
}

static qboolean GL_SelectFragmentShadingRateTexelSize (const VkPhysicalDeviceFragmentShadingRatePropertiesKHR *properties, VkExtent2D *texel_size)
{
	const VkExtent2D min_size = properties->minFragmentShadingRateAttachmentTexelSize;
	const VkExtent2D max_size = properties->maxFragmentShadingRateAttachmentTexelSize;
	uint32_t best_score = UINT32_MAX;
	uint32_t best_aspect = UINT32_MAX;
	qboolean found = false;
	if (!min_size.width || !min_size.height || !max_size.width || !max_size.height || !properties->maxFragmentShadingRateAttachmentTexelSizeAspectRatio)
		return false;

	for (uint32_t width = min_size.width; width <= max_size.width;)
	{
		for (uint32_t height = min_size.height; height <= max_size.height;)
		{
			const uint32_t aspect = width > height ? width / height : height / width;
			if (aspect <= properties->maxFragmentShadingRateAttachmentTexelSizeAspectRatio)
			{
				const uint32_t width_log = GL_FragmentShadingRateLog2 (width);
				const uint32_t height_log = GL_FragmentShadingRateLog2 (height);
				const uint32_t score = (width_log > 4 ? width_log - 4 : 4 - width_log) + (height_log > 4 ? height_log - 4 : 4 - height_log);
				if (!found || score < best_score || (score == best_score && aspect < best_aspect))
				{
					texel_size->width = width;
					texel_size->height = height;
					best_score = score;
					best_aspect = aspect;
					found = true;
				}
			}
			if (height > max_size.height / 2)
				break;
			height *= 2;
		}
		if (width > max_size.width / 2)
			break;
		width *= 2;
	}
	return found;
}

static qboolean GL_QueryFragmentShadingRatesForSamples (VkSampleCountFlagBits samples)
{
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
	fragment_shading_rate_sample_count = samples;
	fragment_shading_rate_sample_query_known = true;
	fragment_shading_rate_1x1_supported = false;
	fragment_shading_rate_2x2_supported = false;
	fragment_shading_rate_4x4_supported = false;
	if (!fpGetPhysicalDeviceFragmentShadingRatesKHR || !vulkan_globals.openxr_fragment_shading_rate_available)
		return false;

	uint32_t rate_count = 0;
	if (fpGetPhysicalDeviceFragmentShadingRatesKHR (vulkan_physical_device, &rate_count, NULL) != VK_SUCCESS || !rate_count || rate_count > 256)
		return false;
	VkPhysicalDeviceFragmentShadingRateKHR *rates = Mem_Alloc (sizeof (*rates) * rate_count);
	for (uint32_t i = 0; i < rate_count; ++i)
	{
		rates[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_KHR;
		rates[i].pNext = NULL;
	}
	const VkResult result = fpGetPhysicalDeviceFragmentShadingRatesKHR (vulkan_physical_device, &rate_count, rates);
	if (result == VK_SUCCESS)
	{
		for (uint32_t i = 0; i < rate_count; ++i)
		{
			if (!(rates[i].sampleCounts & samples))
				continue;
			if (rates[i].fragmentSize.width == 1 && rates[i].fragmentSize.height == 1)
				fragment_shading_rate_1x1_supported = true;
			else if (rates[i].fragmentSize.width == 2 && rates[i].fragmentSize.height == 2)
				fragment_shading_rate_2x2_supported = true;
			else if (rates[i].fragmentSize.width == 4 && rates[i].fragmentSize.height == 4)
				fragment_shading_rate_4x4_supported = true;
		}
	}
	Mem_Free (rates);
	return result == VK_SUCCESS && fragment_shading_rate_1x1_supported &&
		(fragment_shading_rate_2x2_supported || fragment_shading_rate_4x4_supported);
#else
	(void)samples;
	return false;
#endif
}

static qboolean GL_FoveationRequestedActive (int render_width, int render_height)
{
	const int mode = VRF_RequestedMode (vr_foveation.value);
	const qboolean requested = mode == VRF_MODE_FIXED ||
		(mode == VRF_MODE_EYE_TRACKED && VRF_EyeTrackingEnabled (vr_eye_tracking.value) && VRXR_GazeSupported ());
	if (!vulkan_globals.stereo_active || !vulkan_globals.openxr_fragment_shading_rate_available || vulkan_globals.supersampling || !requested)
		return false;

#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
	if (fragment_shading_rate_sample_query_known && fragment_shading_rate_sample_count == vulkan_globals.sample_count &&
		(!fragment_shading_rate_1x1_supported || (!fragment_shading_rate_2x2_supported && !fragment_shading_rate_4x4_supported)))
		return false;
	unsigned int width, height;
	const VkExtent2D texel_size = vulkan_globals.openxr_fragment_shading_rate_texel_size;
	if (render_width <= 0 || render_height <= 0 || !fragment_shading_rate_image_format_properties.maxExtent.width ||
		!fragment_shading_rate_image_format_properties.maxExtent.height ||
		!VRF_RateMapExtent (render_width, texel_size.width, &width) || !VRF_RateMapExtent (render_height, texel_size.height, &height) ||
		width > fragment_shading_rate_image_format_properties.maxExtent.width ||
		height > fragment_shading_rate_image_format_properties.maxExtent.height ||
		fragment_shading_rate_image_format_properties.maxArrayLayers <
			(vulkan_globals.openxr_layered_shading_rate_attachments ? 2u : 1u))
		return false;
	const uint32_t layers = vulkan_globals.openxr_layered_shading_rate_attachments ? 2u : 1u;
	if ((size_t)width > SIZE_MAX / height)
		return false;
	const size_t tile_count = (size_t)width * height;
	if (tile_count > SIZE_MAX / layers || tile_count * layers > INT_MAX)
		return false;
#else
	(void)render_width;
	(void)render_height;
	return false;
#endif
	return true;
}

static void GL_OpenXRCreationFailed (void)
{
	GL_ClearOpenXRFragmentShadingRate ();
	VRXR_Shutdown ();
	if (vulkan_globals.device != VK_NULL_HANDLE)
	{
		vkDestroyDevice (vulkan_globals.device, NULL);
		vulkan_globals.device = VK_NULL_HANDLE;
	}
	if (vulkan_surface != VK_NULL_HANDLE)
	{
		vkDestroySurfaceKHR (vulkan_instance, vulkan_surface, NULL);
		vulkan_surface = VK_NULL_HANDLE;
	}
#ifdef _DEBUG
	if (debug_utils_messenger != VK_NULL_HANDLE)
	{
		fpDestroyDebugUtilsMessengerEXT (vulkan_instance, debug_utils_messenger, NULL);
		debug_utils_messenger = VK_NULL_HANDLE;
	}
#endif
	if (vulkan_instance != VK_NULL_HANDLE)
	{
		vkDestroyInstance (vulkan_instance, NULL);
		vulkan_instance = VK_NULL_HANDLE;
	}
	vulkan_physical_device = VK_NULL_HANDLE;
	openxr_vulkan_binding = false;
	openxr_session_wanted = false;
	openxr_session_change_pending = false;
	openxr_attach_attempted = false;
	openxr_vulkan_api_version = 0;
	openxr_vulkan_minimum_version = 0;
	vulkan_globals.openxr_vulkan_available = false;
	vulkan_globals.openxr_multiview_available = false;
	vulkan_globals.openxr_max_multiview_view_count = 0;
}

/*
===============
GL_CreateSurface
===============
*/
static void GL_CreateSurface (void)
{
#ifdef USE_SDL3
	if (!SDL_Vulkan_CreateSurface (draw_context, vulkan_instance, NULL, &vulkan_surface))
		Sys_Error ("Couldn't create Vulkan surface: %s", SDL_GetError ());
#else
	if (!SDL_Vulkan_CreateSurface (draw_context, vulkan_instance, &vulkan_surface))
		Sys_Error ("Couldn't create Vulkan surface: %s", SDL_GetError ());
#endif
}

/*
===============
GL_InitInstance
===============
*/
static void GL_InitInstance (void)
{
	VkResult	 err;
	uint32_t	 i;
	unsigned int sdl_extension_count;
	vulkan_globals.debug_utils = false;

#ifdef USE_SDL3
	const char *const *sdl_extensions = SDL_Vulkan_GetInstanceExtensions (&sdl_extension_count);
	if (!sdl_extensions)
		Sys_Error ("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError ());

	const char **const instance_extensions = Mem_Alloc (sizeof (const char *) * (sdl_extension_count + 4));
	for (i = 0; i < sdl_extension_count; i++)
		instance_extensions[i] = sdl_extensions[i];
#else
	if (!SDL_Vulkan_GetInstanceExtensions (draw_context, &sdl_extension_count, NULL))
		Sys_Error ("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError ());

	const char **const instance_extensions = Mem_Alloc (sizeof (const char *) * (sdl_extension_count + 4));
	if (!SDL_Vulkan_GetInstanceExtensions (draw_context, &sdl_extension_count, instance_extensions))
		Sys_Error ("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError ());
#endif

	uint32_t instance_extension_count;
	err = vkEnumerateInstanceExtensionProperties (NULL, &instance_extension_count, NULL);

	uint32_t additionalExtensionCount = 0;

	vulkan_globals.get_surface_capabilities_2 = false;
	vulkan_globals.get_physical_device_properties_2 = false;
	if (err == VK_SUCCESS || instance_extension_count > 0)
	{
		VkExtensionProperties *extension_props = (VkExtensionProperties *)Mem_Alloc (sizeof (VkExtensionProperties) * instance_extension_count);
		err = vkEnumerateInstanceExtensionProperties (NULL, &instance_extension_count, extension_props);

		for (i = 0; i < instance_extension_count; ++i)
		{
			if (strcmp (VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, extension_props[i].extensionName) == 0)
				vulkan_globals.get_surface_capabilities_2 = true;
			if (strcmp (VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, extension_props[i].extensionName) == 0)
				vulkan_globals.get_physical_device_properties_2 = true;
#ifdef _DEBUG
			if (strcmp (VK_EXT_DEBUG_UTILS_EXTENSION_NAME, extension_props[i].extensionName) == 0)
				vulkan_globals.debug_utils = true;
#endif
		}

		Mem_Free (extension_props);
	}

	vulkan_globals.vulkan_1_1_available = false;
	fpGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr ();
	GET_INSTANCE_PROC_ADDR (EnumerateInstanceVersion);
	uint32_t loader_api_version = VK_API_VERSION_1_0;
	if (fpEnumerateInstanceVersion)
	{
		fpEnumerateInstanceVersion (&loader_api_version);
		if (loader_api_version >= VK_MAKE_VERSION (1, 1, 0))
		{
			Con_Printf ("Using Vulkan 1.1\n");
			vulkan_globals.vulkan_1_1_available = true;
		}
	}
	GL_OpenXRPrepareVulkan (loader_api_version);

	ZEROED_STRUCT (VkApplicationInfo, application_info);
	application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application_info.pApplicationName = "vkQuake";
	application_info.applicationVersion = 1;
	application_info.pEngineName = "vkQuake";
	application_info.engineVersion = 1;
	application_info.apiVersion = openxr_vulkan_binding ? openxr_vulkan_api_version
													: vulkan_globals.vulkan_1_1_available ? VK_MAKE_VERSION (1, 1, 0) : VK_MAKE_VERSION (1, 0, 0);
	if (openxr_vulkan_binding)
		vulkan_globals.vulkan_1_1_available = GL_CompareVulkanApiVersions (application_info.apiVersion, VK_API_VERSION_1_1) >= 0;

	ZEROED_STRUCT (VkInstanceCreateInfo, instance_create_info);
	instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_create_info.pApplicationInfo = &application_info;
	instance_create_info.ppEnabledExtensionNames = instance_extensions;

	if (vulkan_globals.get_surface_capabilities_2)
		instance_extensions[sdl_extension_count + additionalExtensionCount++] = VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
	if (vulkan_globals.get_physical_device_properties_2)
		instance_extensions[sdl_extension_count + additionalExtensionCount++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;

#ifdef _DEBUG
	if (vulkan_globals.debug_utils)
		instance_extensions[sdl_extension_count + additionalExtensionCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

	const VkValidationFeatureEnableEXT sync_validation_enables[] = {
		VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
	};
	const VkValidationFeatureEnableEXT gpu_validation_enables[] = {
		VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
		VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
	};
	const VkValidationFeatureDisableEXT gpu_validation_disables[] = {
		VK_VALIDATION_FEATURE_DISABLE_CORE_CHECKS_EXT,
		VK_VALIDATION_FEATURE_DISABLE_API_PARAMETERS_EXT,
		VK_VALIDATION_FEATURE_DISABLE_OBJECT_LIFETIMES_EXT,
		VK_VALIDATION_FEATURE_DISABLE_THREAD_SAFETY_EXT,
	};

	ZEROED_STRUCT (VkValidationFeaturesEXT, validation_features);
	const char *const layer_names[] = {"VK_LAYER_KHRONOS_validation"};
	if (vulkan_globals.validation)
	{
		Con_Printf ("Using VK_LAYER_KHRONOS_validation\n");
		instance_create_info.enabledLayerCount = 1;
		instance_create_info.ppEnabledLayerNames = layer_names;
		if (vulkan_globals.validation >= 2)
		{
			instance_extensions[sdl_extension_count + additionalExtensionCount++] = VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;
			validation_features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
			if (vulkan_globals.validation == 2)
			{
				validation_features.enabledValidationFeatureCount = countof (sync_validation_enables);
				validation_features.pEnabledValidationFeatures = sync_validation_enables;
			}
			else
			{
				validation_features.enabledValidationFeatureCount = countof (gpu_validation_enables);
				validation_features.pEnabledValidationFeatures = gpu_validation_enables;
				validation_features.disabledValidationFeatureCount = countof (gpu_validation_disables);
				validation_features.pDisabledValidationFeatures = gpu_validation_disables;
			}
			instance_create_info.pNext = &validation_features;
		}
	}
#endif

	instance_create_info.enabledExtensionCount = sdl_extension_count + additionalExtensionCount;

	if (openxr_vulkan_binding)
	{
		if (!VRXR_CreateVulkanInstance (fpGetInstanceProcAddr, &instance_create_info, &vulkan_instance))
		{
			GL_OpenXRCreationFailed ();
			Sys_Error ("OpenXR bootstrap failed to create the Vulkan instance");
		}
	}
	else
	{
		err = vkCreateInstance (&instance_create_info, NULL, &vulkan_instance);
		if (err != VK_SUCCESS)
			Sys_Error ("Couldn't create Vulkan instance with code %i", (int)err);
	}

#ifdef _DEBUG
	if (vulkan_globals.validation == 2)
		Con_Printf (" VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT\n");
	else if (vulkan_globals.validation == 3)
	{
		Con_Printf (" VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT\n");
		Con_Printf (" VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT\n");
		Con_Printf (" VK_VALIDATION_FEATURE_DISABLE_CORE_CHECKS_EXT\n");
		Con_Printf (" VK_VALIDATION_FEATURE_DISABLE_API_PARAMETERS_EXT\n");
		Con_Printf (" VK_VALIDATION_FEATURE_DISABLE_OBJECT_LIFETIMES_EXT\n");
		Con_Printf (" VK_VALIDATION_FEATURE_DISABLE_THREAD_SAFETY_EXT\n");
	}
#endif

	GL_CreateSurface ();

	GET_INSTANCE_PROC_ADDR (GetDeviceProcAddr);
	GET_INSTANCE_PROC_ADDR (DestroySurfaceKHR);
	GET_INSTANCE_PROC_ADDR (GetPhysicalDeviceSurfaceSupportKHR);
	GET_INSTANCE_PROC_ADDR (GetPhysicalDeviceSurfaceCapabilitiesKHR);
	GET_INSTANCE_PROC_ADDR (GetPhysicalDeviceSurfaceFormatsKHR);
	GET_INSTANCE_PROC_ADDR (GetPhysicalDeviceSurfacePresentModesKHR);
	GET_INSTANCE_PROC_ADDR (GetSwapchainImagesKHR);

	if (vulkan_globals.get_physical_device_properties_2 || (openxr_vulkan_binding && vulkan_globals.vulkan_1_1_available))
	{
		if (openxr_vulkan_binding && !vulkan_globals.vulkan_1_1_available)
		{
			fpGetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2)fpGetInstanceProcAddr (vulkan_instance, "vkGetPhysicalDeviceProperties2KHR");
			fpGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)fpGetInstanceProcAddr (vulkan_instance, "vkGetPhysicalDeviceFeatures2KHR");
			if (!fpGetPhysicalDeviceProperties2 || !fpGetPhysicalDeviceFeatures2)
			{
				GL_OpenXRCreationFailed ();
				Sys_Error ("OpenXR bootstrap requires Vulkan physical-device query entry points");
			}
		}
		else
		{
			GET_INSTANCE_PROC_ADDR (GetPhysicalDeviceProperties2);
			GET_INSTANCE_PROC_ADDR (GetPhysicalDeviceFeatures2);
		}
	}

	if (vulkan_globals.get_surface_capabilities_2)
		GET_INSTANCE_PROC_ADDR (GetPhysicalDeviceSurfaceCapabilities2KHR);

	Con_Printf ("Instance extensions:\n");
	for (i = 0; i < (sdl_extension_count + additionalExtensionCount); ++i)
		Con_Printf (" %s\n", instance_extensions[i]);
	Con_Printf ("\n");

#ifdef _DEBUG
	if (vulkan_globals.validation && vulkan_globals.debug_utils)
	{
		Con_Printf ("Creating debug report callback\n");
		GET_INSTANCE_PROC_ADDR (DestroyDebugUtilsMessengerEXT);
		GET_INSTANCE_PROC_ADDR (CreateDebugUtilsMessengerEXT);
		if (fpCreateDebugUtilsMessengerEXT)
		{
			ZEROED_STRUCT (VkDebugUtilsMessengerCreateInfoEXT, debug_utils_messenger_create_info);
			debug_utils_messenger_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			debug_utils_messenger_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
			debug_utils_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
			debug_utils_messenger_create_info.pfnUserCallback = DebugMessageCallback;

			err = fpCreateDebugUtilsMessengerEXT (vulkan_instance, &debug_utils_messenger_create_info, NULL, &debug_utils_messenger);
			if (err != VK_SUCCESS)
				Sys_Error ("Could not create debug report callback with code %i", (int)err);
		}
	}
#endif

	Mem_Free ((void *)instance_extensions);
}

enum
{
	DRIVER_ID_AMD_PROPRIETARY = 1,
	DRIVER_ID_AMD_OPEN_SOURCE = 2,
	DRIVER_ID_MESA_RADV = 3,
	DRIVER_ID_NVIDIA_PROPRIETARY = 4,
	DRIVER_ID_INTEL_PROPRIETARY_WINDOWS = 5,
	DRIVER_ID_INTEL_OPEN_SOURCE_MESA = 6,
	DRIVER_ID_IMAGINATION_PROPRIETARY = 7,
	DRIVER_ID_QUALCOMM_PROPRIETARY = 8,
	DRIVER_ID_ARM_PROPRIETARY = 9,
	DRIVER_ID_GOOGLE_SWIFTSHADER = 10,
	DRIVER_ID_GGP_PROPRIETARY = 11,
	DRIVER_ID_BROADCOM_PROPRIETARY = 12,
	DRIVER_ID_MESA_LLVMPIPE = 13,
	DRIVER_ID_MOLTENVK = 14,
	DRIVER_ID_COREAVI_PROPRIETARY = 15,
	DRIVER_ID_JUICE_PROPRIETARY = 16,
	DRIVER_ID_VERISILICON_PROPRIETARY = 17,
	DRIVER_ID_MESA_TURNIP = 18,
	DRIVER_ID_MESA_V3DV = 19,
	DRIVER_ID_MESA_PANVK = 20,
	DRIVER_ID_SAMSUNG_PROPRIETARY = 21,
	DRIVER_ID_MESA_VENUS = 22,
};

/*
===============
GetDeviceVendorFromDriverProperties
===============
*/
static const char *GetDeviceVendorFromDriverProperties (VkPhysicalDeviceDriverProperties *driver_properties)
{
	switch ((int)driver_properties->driverID)
	{
	case DRIVER_ID_AMD_PROPRIETARY:
	case DRIVER_ID_AMD_OPEN_SOURCE:
	case DRIVER_ID_MESA_RADV:
		return "AMD";
	case DRIVER_ID_NVIDIA_PROPRIETARY:
		return "NVIDIA";
	case DRIVER_ID_INTEL_PROPRIETARY_WINDOWS:
	case DRIVER_ID_INTEL_OPEN_SOURCE_MESA:
		return "Intel";
	case DRIVER_ID_IMAGINATION_PROPRIETARY:
		return "ImgTec";
	case DRIVER_ID_QUALCOMM_PROPRIETARY:
	case DRIVER_ID_MESA_TURNIP:
		return "Qualcomm";
	case DRIVER_ID_ARM_PROPRIETARY:
	case DRIVER_ID_MESA_PANVK:
		return "ARM";
	case DRIVER_ID_GOOGLE_SWIFTSHADER:
	case DRIVER_ID_GGP_PROPRIETARY:
		return "Google";
	case DRIVER_ID_BROADCOM_PROPRIETARY:
		return "Broadcom";
	case DRIVER_ID_MESA_V3DV:
		return "Raspberry Pi";
	case DRIVER_ID_MESA_LLVMPIPE:
	case DRIVER_ID_MESA_VENUS:
		return "MESA";
	case DRIVER_ID_MOLTENVK:
		return "MoltenVK";
	case DRIVER_ID_SAMSUNG_PROPRIETARY:
		return "Samsung";
	default:
		return NULL;
	}
}

/*
===============
GetDeviceVendorFromDeviceProperties
===============
*/
static const char *GetDeviceVendorFromDeviceProperties (void)
{
	switch (vulkan_globals.device_properties.vendorID)
	{
	case 0x8086:
		return "Intel";
	case 0x10DE:
		return "NVIDIA";
	case 0x1002:
		return "AMD";
	case 0x1010:
		return "ImgTec";
	case 0x13B5:
		return "ARM";
	case 0x5143:
		return "Qualcomm";
	}

	return NULL;
}

static void GL_SelectRenderFormats (VkBool32 extended_format_support)
{
	VkFormatProperties format_properties;
	vulkan_globals.color_format = VK_FORMAT_R8G8B8A8_UNORM;
	if (extended_format_support == VK_TRUE)
	{
		vkGetPhysicalDeviceFormatProperties (vulkan_physical_device, VK_FORMAT_A2B10G10R10_UNORM_PACK32, &format_properties);
		if ((format_properties.optimalTilingFeatures & REQUIRED_COLOR_BUFFER_FEATURES) == REQUIRED_COLOR_BUFFER_FEATURES)
		{
			Con_Printf ("Using A2B10G10R10 color buffer format\n");
			vulkan_globals.color_format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
		}
	}

	vkGetPhysicalDeviceFormatProperties (vulkan_physical_device, VK_FORMAT_D24_UNORM_S8_UINT, &format_properties);
	const qboolean d24_support = (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
	vkGetPhysicalDeviceFormatProperties (vulkan_physical_device, VK_FORMAT_D32_SFLOAT_S8_UINT, &format_properties);
	const qboolean d32_support = (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
	if (d32_support)
	{
		Con_Printf ("Using D32_S8 depth buffer format\n");
		vulkan_globals.depth_format = VK_FORMAT_D32_SFLOAT_S8_UINT;
	}
	else if (d24_support)
	{
		Con_Printf ("Using D24_S8 depth buffer format\n");
		vulkan_globals.depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
	}
	else
		Sys_Error ("Cannot find VK_FORMAT_D24_UNORM_S8_UINT or VK_FORMAT_D32_SFLOAT_S8_UINT depth buffer format");
	Con_Printf ("\n");
}

#if defined(VK_QCOM_fragment_density_map_offset)
static qboolean GL_DensityOffsetSceneFormatSupported (VkFormat format, VkImageUsageFlags usage, uint32_t width, uint32_t height)
{
	VkImageFormatProperties properties;
	return vkGetPhysicalDeviceImageFormatProperties (vulkan_physical_device, format, VK_IMAGE_TYPE_2D,
		VK_IMAGE_TILING_OPTIMAL, usage, VK_IMAGE_CREATE_FRAGMENT_DENSITY_MAP_OFFSET_BIT_QCOM, &properties) == VK_SUCCESS &&
		(properties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) && properties.maxArrayLayers >= 2 &&
		properties.maxExtent.width >= width && properties.maxExtent.height >= height;
}
#endif

/*
===============
GL_InitDevice
===============
*/
static void GL_InitDevice (void)
{
	VkResult err;
	uint32_t i;
	int		 arg_index;
	int		 device_index = 0;
	GL_ClearOpenXRFragmentShadingRate ();

	qboolean subgroup_size_control = false;

	arg_index = COM_CheckParm ("-device");
	if (openxr_vulkan_binding)
	{
		vulkan_physical_device = VRXR_VulkanPhysicalDevice (vulkan_instance);
		if (vulkan_physical_device == VK_NULL_HANDLE)
		{
			GL_OpenXRCreationFailed ();
			Sys_Error ("OpenXR bootstrap failed to obtain the runtime-selected Vulkan GPU");
		}
		if (arg_index)
			Con_Printf ("OpenXR bootstrap: ignoring -device; using the runtime-selected Vulkan GPU.\n");
		else
			Con_Printf ("OpenXR bootstrap: using the runtime-selected Vulkan GPU.\n");
	}
	else
	{
		uint32_t physical_device_count;
		err = vkEnumeratePhysicalDevices (vulkan_instance, &physical_device_count, NULL);
		if (err != VK_SUCCESS || physical_device_count == 0)
			Sys_Error ("Couldn't find any Vulkan devices with code %i", (int)err);

		if (arg_index && (arg_index < (com_argc - 1)))
		{
			const char *device_num = com_argv[arg_index + 1];
			device_index = CLAMP (0, atoi (device_num) - 1, (int)physical_device_count - 1);
		}

		VkPhysicalDevice *physical_devices = (VkPhysicalDevice *)Mem_Alloc (sizeof (VkPhysicalDevice) * physical_device_count);
		vkEnumeratePhysicalDevices (vulkan_instance, &physical_device_count, physical_devices);
		if (!arg_index)
		{
			// If no device was specified by command line pick first discrete GPU
			for (i = 0; i < physical_device_count; ++i)
			{
				VkPhysicalDeviceProperties device_properties;
				vkGetPhysicalDeviceProperties (physical_devices[i], &device_properties);
				if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				{
					device_index = (int)i;
					break;
				}
			}
		}
		vulkan_physical_device = physical_devices[device_index];
		Mem_Free (physical_devices);
	}

	qboolean found_swapchain_extension = false;
	vulkan_globals.dedicated_allocation = false;
	vulkan_globals.full_screen_exclusive = false;
	vulkan_globals.swap_chain_full_screen_acquired = false;
	vulkan_globals.screen_effects_sops = false;
	vulkan_globals.shader_float16 = false;
	vulkan_globals.ray_query = false;
	qboolean push_descriptor = false;

	vkGetPhysicalDeviceMemoryProperties (vulkan_physical_device, &vulkan_globals.memory_properties);
	vkGetPhysicalDeviceProperties (vulkan_physical_device, &vulkan_globals.device_properties);
	if (openxr_vulkan_binding)
	{
		const uint32_t device_version = vulkan_globals.device_properties.apiVersion;
		if (VK_API_VERSION_VARIANT (device_version) || GL_CompareVulkanApiVersions (device_version, openxr_vulkan_minimum_version) < 0)
		{
			GL_OpenXRCreationFailed ();
			Sys_Error ("OpenXR runtime-selected GPU does not meet its minimum Vulkan API version");
		}
		/* Instance version alone does not establish core device feature support. */
		vulkan_globals.vulkan_1_1_available = vulkan_globals.vulkan_1_1_available &&
			GL_CompareVulkanApiVersions (device_version, VK_API_VERSION_1_1) >= 0;
	}

	qboolean shader_float16_available = false;
	qboolean driver_properties_available = false;
	qboolean present_id = false;
	qboolean present_wait = false;
	qboolean fragment_shading_rate_extension = false;
	qboolean create_renderpass2_extension = false;
	qboolean create_renderpass2_core = false;
	qboolean dynamic_rendering_core = false;
	qboolean fragment_density_map_feature_enabled = false;
	qboolean fragment_density_offset_feature_enabled = false;
#if defined(VK_EXT_fragment_density_map)
	qboolean fragment_density_map_extension = false;
#if defined(VK_QCOM_fragment_density_map_offset)
	qboolean fragment_density_offset_qcom_extension = false;
	qboolean fragment_density_offset_ext_extension = false;
	qboolean dynamic_rendering_extension = false;
	qboolean depth_stencil_resolve_extension = false;
	qboolean fragment_density_offset_use_ext = false;
#endif
#endif
	qboolean fragment_shading_rate_usable = false;
	qboolean fragment_shading_rate_feature_enabled = false;
	qboolean fragment_shading_rate_layered = false;
	VkExtent2D fragment_shading_rate_texel_size = {0, 0};
	uint32_t device_extension_count;
	if (openxr_vulkan_binding)
	{
		create_renderpass2_core = GL_CompareVulkanApiVersions (openxr_vulkan_api_version, VK_API_VERSION_1_2) >= 0 &&
			GL_CompareVulkanApiVersions (vulkan_globals.device_properties.apiVersion, VK_API_VERSION_1_2) >= 0;
#if defined(VK_API_VERSION_1_3)
		dynamic_rendering_core = GL_CompareVulkanApiVersions (openxr_vulkan_api_version, VK_API_VERSION_1_3) >= 0 &&
			GL_CompareVulkanApiVersions (vulkan_globals.device_properties.apiVersion, VK_API_VERSION_1_3) >= 0;
#endif
	}
	err = vkEnumerateDeviceExtensionProperties (vulkan_physical_device, NULL, &device_extension_count, NULL);

	if (err == VK_SUCCESS || device_extension_count > 0)
	{
		VkExtensionProperties *device_extensions = (VkExtensionProperties *)Mem_Alloc (sizeof (VkExtensionProperties) * device_extension_count);
		err = vkEnumerateDeviceExtensionProperties (vulkan_physical_device, NULL, &device_extension_count, device_extensions);

		for (i = 0; i < device_extension_count; ++i)
		{
			if (strcmp (VK_KHR_SWAPCHAIN_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				found_swapchain_extension = true;
			if (strcmp (VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				vulkan_globals.dedicated_allocation = true;
			if (vulkan_globals.get_physical_device_properties_2 && strcmp (VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				driver_properties_available = true;
			if (strcmp (VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				subgroup_size_control = true;
#if defined(VK_EXT_full_screen_exclusive)
			if (strcmp (VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				vulkan_globals.full_screen_exclusive = true;
#endif
			if (strcmp (VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				shader_float16_available = true;
			if (strcmp (VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				push_descriptor = true;
			if (strcmp (VK_KHR_RAY_QUERY_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				vulkan_globals.ray_query = true;
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
			if (openxr_vulkan_binding && strcmp (VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				fragment_shading_rate_extension = true;
#endif
#if defined(VK_KHR_create_renderpass2)
			if (openxr_vulkan_binding && strcmp (VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				create_renderpass2_extension = true;
#endif
#if defined(VK_EXT_fragment_density_map)
			if (openxr_vulkan_binding && strcmp (VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				fragment_density_map_extension = true;
#if defined(VK_QCOM_fragment_density_map_offset)
			if (openxr_vulkan_binding && strcmp (VK_QCOM_FRAGMENT_DENSITY_MAP_OFFSET_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				fragment_density_offset_qcom_extension = true;
#if defined(VK_EXT_fragment_density_map_offset)
			if (openxr_vulkan_binding && strcmp (VK_EXT_FRAGMENT_DENSITY_MAP_OFFSET_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				fragment_density_offset_ext_extension = true;
#endif
#if defined(VK_KHR_dynamic_rendering)
			if (openxr_vulkan_binding && strcmp (VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				dynamic_rendering_extension = true;
#endif
#if defined(VK_KHR_depth_stencil_resolve)
			if (openxr_vulkan_binding && strcmp (VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				depth_stencil_resolve_extension = true;
#endif
#endif
#endif
#if defined(VK_KHR_present_wait2)
			if (strcmp (VK_KHR_PRESENT_ID_2_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				present_id = true;
			if (strcmp (VK_KHR_PRESENT_WAIT_2_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				present_wait = true;
#endif
		}

		Mem_Free (device_extensions);
	}
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
	if (openxr_vulkan_binding && fragment_shading_rate_extension)
		fpGetPhysicalDeviceFragmentShadingRatesKHR =
			(PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR)fpGetInstanceProcAddr (vulkan_instance, "vkGetPhysicalDeviceFragmentShadingRatesKHR");
#endif
	fragment_shading_rate_usable = openxr_vulkan_binding && fragment_shading_rate_extension &&
		(create_renderpass2_core || create_renderpass2_extension)
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
		&& fpGetPhysicalDeviceFragmentShadingRatesKHR != NULL
#endif
		;

	const char *vendor = NULL;
	ZEROED_STRUCT (VkPhysicalDeviceDriverProperties, driver_properties);
	if (driver_properties_available)
	{
		driver_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

		ZEROED_STRUCT (VkPhysicalDeviceProperties2, physical_device_properties_2);
		physical_device_properties_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		physical_device_properties_2.pNext = &driver_properties;
		fpGetPhysicalDeviceProperties2 (vulkan_physical_device, &physical_device_properties_2);

		vendor = GetDeviceVendorFromDriverProperties (&driver_properties);
	}

	if (!vendor)
		vendor = GetDeviceVendorFromDeviceProperties ();

	if (vendor)
		Con_Printf ("Vendor: %s\n", vendor);
	else
		Con_Printf ("Vendor: Unknown (0x%x)\n", vulkan_globals.device_properties.vendorID);

	Con_Printf ("Device: %s\n", vulkan_globals.device_properties.deviceName);

	if (driver_properties_available)
		Con_Printf ("Driver: %s %s\n", driver_properties.driverName, driver_properties.driverInfo);

	if (!found_swapchain_extension)
		Sys_Error ("Couldn't find %s extension", VK_KHR_SWAPCHAIN_EXTENSION_NAME);

	qboolean found_graphics_queue = false;

	uint32_t vulkan_queue_count;
	vkGetPhysicalDeviceQueueFamilyProperties (vulkan_physical_device, &vulkan_queue_count, NULL);
	if (vulkan_queue_count == 0)
	{
		Sys_Error ("Couldn't find any Vulkan queues");
	}

	VkQueueFamilyProperties *queue_family_properties = (VkQueueFamilyProperties *)Mem_Alloc (vulkan_queue_count * sizeof (VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties (vulkan_physical_device, &vulkan_queue_count, queue_family_properties);

	// Iterate over each queue to learn whether it supports presenting:
	VkBool32 *queue_supports_present = (VkBool32 *)Mem_Alloc (vulkan_queue_count * sizeof (VkBool32));
	for (i = 0; i < vulkan_queue_count; ++i)
		fpGetPhysicalDeviceSurfaceSupportKHR (vulkan_physical_device, i, vulkan_surface, &queue_supports_present[i]);

	for (i = 0; i < vulkan_queue_count; ++i)
	{
		if (((queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) && queue_supports_present[i])
		{
			found_graphics_queue = true;
			vulkan_globals.gfx_queue_family_index = i;
			break;
		}
	}

	Mem_Free (queue_supports_present);
	if (found_graphics_queue)
		timestamp_valid_bits = queue_family_properties[vulkan_globals.gfx_queue_family_index].timestampValidBits;
	Mem_Free (queue_family_properties);

	if (!found_graphics_queue)
		Sys_Error ("Couldn't find graphics queue");

	float queue_priorities[] = {0.0};
	ZEROED_STRUCT (VkDeviceQueueCreateInfo, queue_create_info);
	queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;
	queue_create_info.queueCount = 1;
	queue_create_info.pQueuePriorities = queue_priorities;

	ZEROED_STRUCT (VkPhysicalDeviceSubgroupProperties, physical_device_subgroup_properties);
	ZEROED_STRUCT (VkPhysicalDeviceSubgroupSizeControlPropertiesEXT, physical_device_subgroup_size_control_properties);
	ZEROED_STRUCT (VkPhysicalDeviceSubgroupSizeControlFeaturesEXT, subgroup_size_control_features);
	ZEROED_STRUCT (VkPhysicalDeviceBufferDeviceAddressFeaturesKHR, buffer_device_address_features);
	ZEROED_STRUCT (VkPhysicalDeviceAccelerationStructureFeaturesKHR, acceleration_structure_features);
	ZEROED_STRUCT (VkPhysicalDeviceRayQueryFeaturesKHR, ray_query_features);
	ZEROED_STRUCT (VkPhysicalDeviceShaderFloat16Int8Features, shader_float16_features);
	ZEROED_STRUCT (VkPhysicalDeviceMultiviewProperties, multiview_properties);
	ZEROED_STRUCT (VkPhysicalDeviceMultiviewFeatures, multiview_features);
#if defined(VK_EXT_fragment_density_map)
	ZEROED_STRUCT (VkPhysicalDeviceFragmentDensityMapPropertiesEXT, fragment_density_map_properties);
	ZEROED_STRUCT (VkPhysicalDeviceFragmentDensityMapFeaturesEXT, fragment_density_map_features);
#if defined(VK_QCOM_fragment_density_map_offset)
	ZEROED_STRUCT (VkPhysicalDeviceFragmentDensityMapOffsetPropertiesQCOM, fragment_density_offset_properties);
	ZEROED_STRUCT (VkPhysicalDeviceFragmentDensityMapOffsetFeaturesQCOM, fragment_density_offset_features);
	const qboolean fragment_density_offset_ext_usable = fragment_density_offset_ext_extension &&
		(dynamic_rendering_core || dynamic_rendering_extension) &&
		(create_renderpass2_core || depth_stencil_resolve_extension);
	const qboolean fragment_density_offset_extension = fragment_density_offset_qcom_extension || fragment_density_offset_ext_usable;
#endif
#endif
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
	ZEROED_STRUCT (VkPhysicalDeviceFragmentShadingRatePropertiesKHR, fragment_shading_rate_properties);
	ZEROED_STRUCT (VkPhysicalDeviceFragmentShadingRateFeaturesKHR, fragment_shading_rate_features);
#endif
#if defined(VK_KHR_present_wait2)
	ZEROED_STRUCT (VkPhysicalDevicePresentId2FeaturesKHR, present_id_features);
	ZEROED_STRUCT (VkPhysicalDevicePresentWait2FeaturesKHR, present_wait_features);
#endif
	memset (&vulkan_globals.physical_device_acceleration_structure_properties, 0, sizeof (vulkan_globals.physical_device_acceleration_structure_properties));
	if (vulkan_globals.vulkan_1_1_available)
	{
		ZEROED_STRUCT (VkPhysicalDeviceProperties2KHR, physical_device_properties_2);
		physical_device_properties_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		void **device_properties_next = &physical_device_properties_2.pNext;

		if (subgroup_size_control)
		{
			physical_device_subgroup_size_control_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES_EXT;
			CHAIN_PNEXT (device_properties_next, physical_device_subgroup_size_control_properties);
			physical_device_subgroup_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
			CHAIN_PNEXT (device_properties_next, physical_device_subgroup_properties);
		}
		if (vulkan_globals.ray_query)
		{
			vulkan_globals.physical_device_acceleration_structure_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
			CHAIN_PNEXT (device_properties_next, vulkan_globals.physical_device_acceleration_structure_properties);
		}
		if (openxr_vulkan_binding && GL_CompareVulkanApiVersions (openxr_vulkan_api_version, VK_API_VERSION_1_1) >= 0)
		{
			multiview_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES;
			CHAIN_PNEXT (device_properties_next, multiview_properties);
		}
#if defined(VK_EXT_fragment_density_map)
		if (fragment_density_map_extension && VRXR_VulkanFoveationSupported ())
		{
			fragment_density_map_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_PROPERTIES_EXT;
			CHAIN_PNEXT (device_properties_next, fragment_density_map_properties);
		}
#if defined(VK_QCOM_fragment_density_map_offset)
		if (fragment_density_map_extension && fragment_density_offset_extension && VRXR_VulkanFoveationEyeSupported ())
		{
			fragment_density_offset_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_OFFSET_PROPERTIES_QCOM;
			CHAIN_PNEXT (device_properties_next, fragment_density_offset_properties);
		}
#endif
#endif
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
		if (fragment_shading_rate_usable)
		{
			fragment_shading_rate_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR;
			CHAIN_PNEXT (device_properties_next, fragment_shading_rate_properties);
		}
#endif

		fpGetPhysicalDeviceProperties2 (vulkan_physical_device, &physical_device_properties_2);

		ZEROED_STRUCT (VkPhysicalDeviceFeatures2, physical_device_features_2);
		physical_device_features_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		void **device_features_next = &physical_device_features_2.pNext;

		if (shader_float16_available)
		{
			shader_float16_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
			CHAIN_PNEXT (device_features_next, shader_float16_features);
		}

		if (subgroup_size_control)
		{
			subgroup_size_control_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT;
			CHAIN_PNEXT (device_features_next, subgroup_size_control_features);
		}
		if (vulkan_globals.ray_query)
		{
			buffer_device_address_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;
			CHAIN_PNEXT (device_features_next, buffer_device_address_features);
			acceleration_structure_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
			CHAIN_PNEXT (device_features_next, acceleration_structure_features);
			ray_query_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
			CHAIN_PNEXT (device_features_next, ray_query_features);
		}
		if (openxr_vulkan_binding && GL_CompareVulkanApiVersions (openxr_vulkan_api_version, VK_API_VERSION_1_1) >= 0)
		{
			multiview_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;
			CHAIN_PNEXT (device_features_next, multiview_features);
		}
#if defined(VK_EXT_fragment_density_map)
		if (fragment_density_map_extension && VRXR_VulkanFoveationSupported ())
		{
			fragment_density_map_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT;
			CHAIN_PNEXT (device_features_next, fragment_density_map_features);
		}
#if defined(VK_QCOM_fragment_density_map_offset)
		if (fragment_density_map_extension && fragment_density_offset_extension && VRXR_VulkanFoveationEyeSupported ())
		{
			fragment_density_offset_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_OFFSET_FEATURES_QCOM;
			CHAIN_PNEXT (device_features_next, fragment_density_offset_features);
		}
#endif
#endif
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
		if (fragment_shading_rate_usable)
		{
			fragment_shading_rate_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR;
			CHAIN_PNEXT (device_features_next, fragment_shading_rate_features);
		}
#endif
#if defined(VK_KHR_present_wait2)
		if (present_id && present_wait)
		{
			present_id_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR;
			CHAIN_PNEXT (device_features_next, present_id_features);
			present_wait_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR;
			CHAIN_PNEXT (device_features_next, present_wait_features);
		}
#endif

		fpGetPhysicalDeviceFeatures2 (vulkan_physical_device, &physical_device_features_2);
		vulkan_globals.device_features = physical_device_features_2.features;
	}
	else
		vkGetPhysicalDeviceFeatures (vulkan_physical_device, &vulkan_globals.device_features);
	GL_SelectRenderFormats (vulkan_globals.device_features.shaderStorageImageExtendedFormats);
	vulkan_globals.openxr_max_multiview_view_count = multiview_properties.maxMultiviewViewCount;
	vulkan_globals.openxr_multiview_available = multiview_features.multiview && multiview_properties.maxMultiviewViewCount >= 2 &&
		vulkan_globals.device_properties.limits.maxBoundDescriptorSets >= 6;
	if (openxr_vulkan_binding && !vulkan_globals.openxr_multiview_available)
		Con_Printf ("OpenXR bootstrap: core multiview with two views is unavailable; session is not attached.\n");

#if defined(VK_EXT_fragment_density_map)
	qboolean fragment_density_map_candidate = false;
#if defined(VK_QCOM_fragment_density_map_offset)
	qboolean fragment_density_offset_candidate = false;
#endif
	if (fragment_density_map_extension && (create_renderpass2_core || create_renderpass2_extension) &&
		vulkan_globals.openxr_multiview_available && VRXR_VulkanFoveationSupported () &&
		fragment_density_map_features.fragmentDensityMap &&
		fragment_density_map_properties.maxFragmentDensityTexelSize.width &&
		fragment_density_map_properties.maxFragmentDensityTexelSize.height)
	{
		VkFormatProperties density_format_properties;
		VkImageFormatProperties density_image_properties;
		vkGetPhysicalDeviceFormatProperties (vulkan_physical_device, VK_FORMAT_R8G8_UNORM, &density_format_properties);
		if ((density_format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_FRAGMENT_DENSITY_MAP_BIT_EXT) &&
			vkGetPhysicalDeviceImageFormatProperties (vulkan_physical_device, VK_FORMAT_R8G8_UNORM, VK_IMAGE_TYPE_2D,
				VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT, 0, &density_image_properties) == VK_SUCCESS &&
			(density_image_properties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) && density_image_properties.maxArrayLayers >= 2)
		{
			if (fragment_density_map_features.fragmentDensityMapNonSubsampledImages)
			{
				fragment_density_map_candidate = true;
				vulkan_globals.openxr_fragment_density_map_max_texel_size = fragment_density_map_properties.maxFragmentDensityTexelSize;
				Con_Printf ("OpenXR runtime FDM candidate: Vulkan feature, RG8 array format and non-subsampled scene images available; eye profile %s. Borrowed-map contract still unverified.\n",
					VRXR_VulkanFoveationEyeSupported () ? "available" : "unavailable");
#if defined(VK_QCOM_fragment_density_map_offset)
				fragment_density_offset_candidate = fragment_density_offset_extension && VRXR_VulkanFoveationEyeSupported () &&
					fragment_density_offset_features.fragmentDensityMapOffset &&
					fragment_density_offset_properties.fragmentDensityOffsetGranularity.width &&
					fragment_density_offset_properties.fragmentDensityOffsetGranularity.height &&
					GL_DensityOffsetSceneFormatSupported (vulkan_globals.color_format,
						VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
						VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 0, 0) &&
					GL_DensityOffsetSceneFormatSupported (vulkan_globals.depth_format,
						VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, 0);
				if (fragment_density_offset_candidate)
					vulkan_globals.openxr_fragment_density_offset_granularity = fragment_density_offset_properties.fragmentDensityOffsetGranularity;
#endif
			}
			else
				Con_Printf ("OpenXR runtime FDM requires subsampled scene images; this renderer currently keeps KHR shading rate or full-rate rendering.\n");
		}
	}
#endif

#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
	if (fragment_shading_rate_usable && fragment_shading_rate_features.attachmentFragmentShadingRate &&
		GL_SelectFragmentShadingRateTexelSize (&fragment_shading_rate_properties, &fragment_shading_rate_texel_size))
	{
		VkFormatProperties shading_rate_format_properties;
		VkImageFormatProperties shading_rate_image_properties;
		vkGetPhysicalDeviceFormatProperties (vulkan_physical_device, VK_FORMAT_R8_UINT, &shading_rate_format_properties);
		if ((shading_rate_format_properties.optimalTilingFeatures &
				(VK_FORMAT_FEATURE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) ==
				(VK_FORMAT_FEATURE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_FORMAT_FEATURE_TRANSFER_DST_BIT) &&
			vkGetPhysicalDeviceImageFormatProperties (vulkan_physical_device, VK_FORMAT_R8_UINT, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0, &shading_rate_image_properties) == VK_SUCCESS &&
			(shading_rate_image_properties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) != 0)
		{
			fragment_shading_rate_feature_enabled = true;
			fragment_shading_rate_image_format_properties = shading_rate_image_properties;
			fragment_shading_rate_layered = fragment_shading_rate_properties.layeredShadingRateAttachments &&
				shading_rate_image_properties.maxArrayLayers >= 2;
		}
	}
#endif

#if defined(VK_EXT_fragment_density_map)
	const qboolean khr_shading_rate_candidate = fragment_shading_rate_feature_enabled;
	// VID initializes before saved configs execute, so the current mode cvar
	// cannot safely select a fixed-only FB device. Keep the KHR eye path when
	// META eye capability is absent; the runtime route is still development-only.
	if (COM_CheckParm ("-vk-runtime-foveation") && fragment_density_map_candidate && VRXR_VulkanFoveationEyeSupported ())
	{
#if defined(VK_QCOM_fragment_density_map_offset)
		// XR_META_foveation_eye_tracked makes the runtime apply the gaze pattern
		// to its map. Do not enable the separate Vulkan offset path until the
		// borrowed map's offset creation flags and its semantics are verified.
		fragment_density_offset_feature_enabled = false;
		fragment_density_offset_use_ext = false;
		if (fragment_density_offset_candidate)
			Con_Printf ("OpenXR density-map offsets available but not qualified for borrowed runtime images.\n");
#endif
		fragment_density_map_feature_enabled = true;
		fragment_shading_rate_feature_enabled = false;
		Con_Printf ("OpenXR development density-map device selected; runtime image contract still requires validation.\n");
	}
	else if (COM_CheckParm ("-vk-runtime-foveation") && fragment_density_map_candidate)
		Con_Printf ("OpenXR runtime has no META eye-foveation capability; keeping KHR shading rate when available.\n");
	if (openxr_vulkan_binding)
		Con_Printf ("OpenXR foveation device: KHR attachment %s, FB density map %s, selected %s.\n",
			khr_shading_rate_candidate ? "capable" : "unavailable",
			fragment_density_map_candidate ? "candidate" : "unavailable",
			fragment_density_map_feature_enabled ? "FB development path" :
			(fragment_shading_rate_feature_enabled ? "KHR" : "full rate"));
#endif

#ifdef __APPLE__ // MoltenVK lies about this
	vulkan_globals.device_features.sampleRateShading = false;
#endif

	vulkan_globals.shader_float16 = shader_float16_features.shaderFloat16;
	if (vulkan_globals.shader_float16)
		Con_Printf ("Using FP16 shader arithmetic\n");

	vulkan_globals.screen_effects_sops =
		vulkan_globals.vulkan_1_1_available && subgroup_size_control && subgroup_size_control_features.subgroupSizeControl &&
		subgroup_size_control_features.computeFullSubgroups && ((physical_device_subgroup_properties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0) &&
		((physical_device_subgroup_properties.supportedOperations & VK_SUBGROUP_FEATURE_SHUFFLE_BIT) != 0)
		// Shader only supports subgroup sizes from 4 to 64. 128 can't be supported because Vulkan spec states that workgroup size
		// in x dimension must be a multiple of the subgroup size for VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT.
		&& (physical_device_subgroup_size_control_properties.minSubgroupSize >= 4) && (physical_device_subgroup_size_control_properties.maxSubgroupSize <= 64);
	if (vulkan_globals.screen_effects_sops)
		Con_Printf ("Using subgroup operations\n");

	vulkan_globals.ray_query = vulkan_globals.ray_query && push_descriptor && acceleration_structure_features.accelerationStructure &&
							   ray_query_features.rayQuery && buffer_device_address_features.bufferDeviceAddress;
	if (vulkan_globals.ray_query)
		Con_Printf ("Using ray queries\n");

	vulkan_globals.present_wait = false;
#if defined(VK_KHR_present_wait2)
	vulkan_globals.present_wait = vulkan_globals.vulkan_1_1_available && vulkan_globals.get_surface_capabilities_2 && present_id && present_wait &&
								  present_id_features.presentId2 && present_wait_features.presentWait2;
	if (vulkan_globals.present_wait)
		Con_Printf ("Using present wait\n");
#endif

	const char *device_extensions[32] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	uint32_t	numEnabledExtensions = 1;
	if (vulkan_globals.shader_float16)
		device_extensions[numEnabledExtensions++] = VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME;
	if (vulkan_globals.dedicated_allocation)
	{
		device_extensions[numEnabledExtensions++] = VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME;
		device_extensions[numEnabledExtensions++] = VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME;
	}
	if (vulkan_globals.screen_effects_sops)
		device_extensions[numEnabledExtensions++] = VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME;
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
	if (fragment_shading_rate_feature_enabled)
	{
		device_extensions[numEnabledExtensions++] = VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME;
		if (!create_renderpass2_core)
			device_extensions[numEnabledExtensions++] = VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME;
	}
#endif
#if defined(VK_EXT_fragment_density_map)
	if (fragment_density_map_feature_enabled)
	{
		device_extensions[numEnabledExtensions++] = VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME;
		if (!create_renderpass2_core)
			device_extensions[numEnabledExtensions++] = VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME;
#if defined(VK_QCOM_fragment_density_map_offset)
		if (fragment_density_offset_feature_enabled)
		{
			if (fragment_density_offset_use_ext)
			{
#if defined(VK_EXT_fragment_density_map_offset)
				device_extensions[numEnabledExtensions++] = VK_EXT_FRAGMENT_DENSITY_MAP_OFFSET_EXTENSION_NAME;
#endif
			}
			else
				device_extensions[numEnabledExtensions++] = VK_QCOM_FRAGMENT_DENSITY_MAP_OFFSET_EXTENSION_NAME;
			if (fragment_density_offset_use_ext)
			{
#if defined(VK_KHR_dynamic_rendering)
				if (!dynamic_rendering_core)
					device_extensions[numEnabledExtensions++] = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
#endif
#if defined(VK_KHR_depth_stencil_resolve)
				if (!create_renderpass2_core)
					device_extensions[numEnabledExtensions++] = VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME;
#endif
			}
		}
#endif
	}
#endif
#if defined(VK_EXT_full_screen_exclusive)
	if (vulkan_globals.full_screen_exclusive)
		device_extensions[numEnabledExtensions++] = VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME;
#endif
#if defined(VK_KHR_present_wait2)
	if (vulkan_globals.present_wait)
	{
		device_extensions[numEnabledExtensions++] = VK_KHR_PRESENT_ID_2_EXTENSION_NAME;
		device_extensions[numEnabledExtensions++] = VK_KHR_PRESENT_WAIT_2_EXTENSION_NAME;
	}
#endif
	if (vulkan_globals.ray_query)
	{
		device_extensions[numEnabledExtensions++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
		device_extensions[numEnabledExtensions++] = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;
		device_extensions[numEnabledExtensions++] = VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME;
		device_extensions[numEnabledExtensions++] = VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
		device_extensions[numEnabledExtensions++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
		device_extensions[numEnabledExtensions++] = VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME;
		device_extensions[numEnabledExtensions++] = VK_KHR_SPIRV_1_4_EXTENSION_NAME;
		device_extensions[numEnabledExtensions++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
		device_extensions[numEnabledExtensions++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
	}

	const VkBool32 extended_format_support = vulkan_globals.device_features.shaderStorageImageExtendedFormats;
	const VkBool32 independent_blend = vulkan_globals.device_features.independentBlend;
	const VkBool32 sampler_anisotropic = vulkan_globals.device_features.samplerAnisotropy;

	ZEROED_STRUCT (VkPhysicalDeviceFeatures, device_features);
	device_features.shaderStorageImageExtendedFormats = extended_format_support;
	device_features.independentBlend = independent_blend;
	device_features.samplerAnisotropy = sampler_anisotropic;
	device_features.sampleRateShading = vulkan_globals.device_features.sampleRateShading;
	device_features.fillModeNonSolid = vulkan_globals.device_features.fillModeNonSolid;
	device_features.multiDrawIndirect = vulkan_globals.device_features.multiDrawIndirect;

	vulkan_globals.non_solid_fill = (device_features.fillModeNonSolid == VK_TRUE) ? true : false;
	vulkan_globals.multi_draw_indirect = (device_features.multiDrawIndirect == VK_TRUE) ? true : false;

	ZEROED_STRUCT (VkDeviceCreateInfo, device_create_info);
	device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	void **device_create_info_next = (void **)&device_create_info.pNext;
	if (vulkan_globals.shader_float16)
	{
		shader_float16_features.pNext = NULL;
		shader_float16_features.shaderInt8 = VK_FALSE;
		CHAIN_PNEXT (device_create_info_next, shader_float16_features);
	}
	if (vulkan_globals.screen_effects_sops)
		CHAIN_PNEXT (device_create_info_next, subgroup_size_control_features);
	if (vulkan_globals.ray_query)
	{
		CHAIN_PNEXT (device_create_info_next, buffer_device_address_features);
		CHAIN_PNEXT (device_create_info_next, acceleration_structure_features);
		CHAIN_PNEXT (device_create_info_next, ray_query_features);
	}
	if (vulkan_globals.openxr_multiview_available)
	{
		multiview_features.multiview = VK_TRUE;
		multiview_features.multiviewGeometryShader = VK_FALSE;
		multiview_features.multiviewTessellationShader = VK_FALSE;
		multiview_features.pNext = NULL;
		CHAIN_PNEXT (device_create_info_next, multiview_features);
	}
#if defined(VK_EXT_fragment_density_map)
	if (fragment_density_map_feature_enabled)
	{
		fragment_density_map_features.pNext = NULL;
		fragment_density_map_features.fragmentDensityMap = VK_TRUE;
		fragment_density_map_features.fragmentDensityMapDynamic = VK_FALSE;
		fragment_density_map_features.fragmentDensityMapNonSubsampledImages = VK_TRUE;
		CHAIN_PNEXT (device_create_info_next, fragment_density_map_features);
#if defined(VK_QCOM_fragment_density_map_offset)
		if (fragment_density_offset_feature_enabled)
		{
			fragment_density_offset_features.pNext = NULL;
			fragment_density_offset_features.fragmentDensityMapOffset = VK_TRUE;
			CHAIN_PNEXT (device_create_info_next, fragment_density_offset_features);
		}
#endif
	}
#endif
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
	fragment_shading_rate_features.pNext = NULL;
	if (fragment_shading_rate_feature_enabled)
	{
		/* With a 1x1 pipeline rate, attachmentFragmentShadingRate permits a static attachment combiner. */
		fragment_shading_rate_features.pipelineFragmentShadingRate = VK_FALSE;
		fragment_shading_rate_features.primitiveFragmentShadingRate = VK_FALSE;
		fragment_shading_rate_features.attachmentFragmentShadingRate = VK_TRUE;
		CHAIN_PNEXT (device_create_info_next, fragment_shading_rate_features);
	}
#endif
#if defined(VK_KHR_present_wait)
	if (vulkan_globals.present_wait)
	{
		CHAIN_PNEXT (device_create_info_next, present_id_features);
		CHAIN_PNEXT (device_create_info_next, present_wait_features);
	}
#endif
	device_create_info.queueCreateInfoCount = 1;
	device_create_info.pQueueCreateInfos = &queue_create_info;
	device_create_info.enabledExtensionCount = numEnabledExtensions;
	device_create_info.ppEnabledExtensionNames = device_extensions;
	device_create_info.pEnabledFeatures = &device_features;

	if (openxr_vulkan_binding)
	{
		if (!VRXR_CreateVulkanDevice (&device_create_info, &vulkan_globals.device))
		{
			GL_OpenXRCreationFailed ();
			Sys_Error ("OpenXR bootstrap failed to create the Vulkan device");
		}
		vulkan_globals.openxr_vulkan_available = true;
		Con_Printf ("OpenXR graphics binding ready; stereo attachment follows at the first frame.\n");
	}
	else
	{
		err = vkCreateDevice (vulkan_physical_device, &device_create_info, NULL, &vulkan_globals.device);
		if (err != VK_SUCCESS)
			Sys_Error ("Couldn't create Vulkan device with code %i", (int)err);
	}

	GET_DEVICE_PROC_ADDR (CreateSwapchainKHR);
	GET_DEVICE_PROC_ADDR (DestroySwapchainKHR);
	GET_DEVICE_PROC_ADDR (GetSwapchainImagesKHR);
	GET_DEVICE_PROC_ADDR (AcquireNextImageKHR);
	GET_DEVICE_PROC_ADDR (QueuePresentKHR);
	if (fragment_shading_rate_feature_enabled || fragment_density_map_feature_enabled)
	{
		const char *create_renderpass2_name = create_renderpass2_core ? "vkCreateRenderPass2" : "vkCreateRenderPass2KHR";
		vulkan_globals.vk_create_render_pass2 = (PFN_vkCreateRenderPass2KHR)fpGetDeviceProcAddr (vulkan_globals.device, create_renderpass2_name);
		if (fragment_shading_rate_feature_enabled)
		{
			vulkan_globals.vk_cmd_set_fragment_shading_rate =
				(PFN_vkCmdSetFragmentShadingRateKHR)fpGetDeviceProcAddr (vulkan_globals.device, "vkCmdSetFragmentShadingRateKHR");
			if (vulkan_globals.vk_create_render_pass2 && vulkan_globals.vk_cmd_set_fragment_shading_rate)
			{
				vulkan_globals.openxr_fragment_shading_rate_available = true;
				vulkan_globals.openxr_fragment_shading_rate_texel_size = fragment_shading_rate_texel_size;
				vulkan_globals.openxr_layered_shading_rate_attachments = fragment_shading_rate_layered;
				Con_Printf ("OpenXR attachment fragment shading rate capability enabled (%ux%u texels; layered %s).\n",
					fragment_shading_rate_texel_size.width, fragment_shading_rate_texel_size.height,
					fragment_shading_rate_layered ? "available" : "unavailable");
			}
		}
		if (fragment_density_map_feature_enabled)
		{
			const char *begin_name = create_renderpass2_core ? "vkCmdBeginRenderPass2" : "vkCmdBeginRenderPass2KHR";
			const char *end_name = create_renderpass2_core ? "vkCmdEndRenderPass2" : "vkCmdEndRenderPass2KHR";
			vulkan_globals.vk_cmd_begin_render_pass2 =
				(PFN_vkCmdBeginRenderPass2KHR)fpGetDeviceProcAddr (vulkan_globals.device, begin_name);
			vulkan_globals.vk_cmd_end_render_pass2 =
				(PFN_vkCmdEndRenderPass2KHR)fpGetDeviceProcAddr (vulkan_globals.device, end_name);
			vulkan_globals.openxr_fragment_density_map_enabled = vulkan_globals.vk_create_render_pass2 &&
				vulkan_globals.vk_cmd_begin_render_pass2 && vulkan_globals.vk_cmd_end_render_pass2;
			vulkan_globals.openxr_fragment_density_offset_enabled =
				vulkan_globals.openxr_fragment_density_map_enabled && fragment_density_offset_feature_enabled;
		}
	}

	Con_Printf ("Device extensions:\n");
	for (i = 0; i < numEnabledExtensions; ++i)
		Con_Printf (" %s\n", device_extensions[i]);

#if defined(VK_EXT_full_screen_exclusive)
	if (vulkan_globals.full_screen_exclusive)
	{
		GET_DEVICE_PROC_ADDR (AcquireFullScreenExclusiveModeEXT);
		GET_DEVICE_PROC_ADDR (ReleaseFullScreenExclusiveModeEXT);
	}
#endif
#if defined(VK_KHR_present_wait2)
	if (vulkan_globals.present_wait)
		GET_DEVICE_PROC_ADDR (WaitForPresent2KHR);
#endif
	if (vulkan_globals.ray_query)
	{
		GET_GLOBAL_DEVICE_PROC_ADDR (vk_get_buffer_device_address, vkGetBufferDeviceAddressKHR);
		GET_GLOBAL_DEVICE_PROC_ADDR (vk_get_acceleration_structure_build_sizes, vkGetAccelerationStructureBuildSizesKHR);
		GET_GLOBAL_DEVICE_PROC_ADDR (vk_create_acceleration_structure, vkCreateAccelerationStructureKHR);
		GET_GLOBAL_DEVICE_PROC_ADDR (vk_destroy_acceleration_structure, vkDestroyAccelerationStructureKHR);
		GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_build_acceleration_structures, vkCmdBuildAccelerationStructuresKHR);
		GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_push_descriptor_set, vkCmdPushDescriptorSetKHR);
		GET_GLOBAL_DEVICE_PROC_ADDR (vk_get_acceleration_structure_device_address, vkGetAccelerationStructureDeviceAddressKHR);
	}
#ifdef _DEBUG
	if (vulkan_globals.debug_utils)
	{
		GET_INSTANCE_PROC_ADDR (SetDebugUtilsObjectNameEXT);
		GET_GLOBAL_INSTANCE_PROC_ADDR (vk_cmd_begin_debug_utils_label, vkCmdBeginDebugUtilsLabelEXT);
		GET_GLOBAL_INSTANCE_PROC_ADDR (vk_cmd_end_debug_utils_label, vkCmdEndDebugUtilsLabelEXT);
	}
#endif

	vkGetDeviceQueue (vulkan_globals.device, vulkan_globals.gfx_queue_family_index, 0, &vulkan_globals.queue);
	vulkan_globals.queue_mutex = SDL_CreateMutex ();
	if (!vulkan_globals.queue_mutex)
		Sys_Error ("Couldn't create Vulkan queue mutex: %s", SDL_GetError ());
	// The runtime can use this same queue in its frame/image calls. Register the
	// donor lock without holding it across runtime error handling or retirement.
	if (openxr_vulkan_binding && !VRXR_SetVulkanQueueCallbacks (GL_OpenXRLockQueue, GL_OpenXRUnlockQueue, vulkan_globals.queue_mutex))
		Sys_Error ("Couldn't register OpenXR Vulkan queue synchronization");

	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_bind_pipeline, vkCmdBindPipeline);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_push_constants, vkCmdPushConstants);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_bind_descriptor_sets, vkCmdBindDescriptorSets);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_bind_index_buffer, vkCmdBindIndexBuffer);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_bind_vertex_buffers, vkCmdBindVertexBuffers);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_draw, vkCmdDraw);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_draw_indexed, vkCmdDrawIndexed);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_draw_indexed_indirect, vkCmdDrawIndexedIndirect);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_pipeline_barrier, vkCmdPipelineBarrier);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_copy_buffer_to_image, vkCmdCopyBufferToImage);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_dispatch, vkCmdDispatch);
}

/*
===============
GL_InitCommandBuffers
===============
*/
static void GL_InitCommandBuffers (void)
{
	Con_Printf ("Creating command buffers\n");

	VkResult err;

	{
		ZEROED_STRUCT (VkCommandPoolCreateInfo, command_pool_create_info);
		command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;
		err = vkCreateCommandPool (vulkan_globals.device, &command_pool_create_info, NULL, &transient_command_pool);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateCommandPool failed with code %i", (int)err);
	}

	ZEROED_STRUCT (VkCommandPoolCreateInfo, command_pool_create_info);
	command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;

	for (int pcbx_index = 0; pcbx_index < PCBX_NUM; ++pcbx_index)
	{
		err = vkCreateCommandPool (vulkan_globals.device, &command_pool_create_info, NULL, &primary_command_pools[pcbx_index]);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateCommandPool failed with code %i", (int)err);

		ZEROED_STRUCT (VkCommandBufferAllocateInfo, command_buffer_allocate_info);
		command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		command_buffer_allocate_info.commandPool = primary_command_pools[pcbx_index];
		command_buffer_allocate_info.commandBufferCount = DOUBLE_BUFFERED;

		err = vkAllocateCommandBuffers (vulkan_globals.device, &command_buffer_allocate_info, primary_command_buffers[pcbx_index]);
		if (err != VK_SUCCESS)
			Sys_Error ("vkAllocateCommandBuffers failed with code %i", (int)err);
		for (int i = 0; i < DOUBLE_BUFFERED; ++i)
			GL_SetObjectName (
				(uint64_t)(uintptr_t)primary_command_buffers[pcbx_index][i], VK_OBJECT_TYPE_COMMAND_BUFFER, va ("PCBX index: %d cb_index: %d", pcbx_index, i));
	}

	for (int scbx_index = 0; scbx_index < SCBX_NUM; ++scbx_index)
	{
		const int multiplicity = R_SecondaryContextCount (scbx_index);
		if (!multiplicity)
			continue;
		vulkan_globals.secondary_cb_contexts[scbx_index] = Mem_Alloc (multiplicity * sizeof (cb_context_t));
		secondary_command_pools[scbx_index] = Mem_Alloc (multiplicity * sizeof (VkCommandPool));
		for (int i = 0; i < DOUBLE_BUFFERED; ++i)
			secondary_command_buffers[scbx_index][i] = Mem_Alloc (multiplicity * sizeof (VkCommandBuffer));
		for (int i = 0; i < multiplicity; ++i)
		{
			err = vkCreateCommandPool (vulkan_globals.device, &command_pool_create_info, NULL, &secondary_command_pools[scbx_index][i]);
			if (err != VK_SUCCESS)
				Sys_Error ("vkCreateCommandPool failed with code %i", (int)err);

			ZEROED_STRUCT (VkCommandBufferAllocateInfo, command_buffer_allocate_info);
			command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			command_buffer_allocate_info.commandPool = secondary_command_pools[scbx_index][i];
			command_buffer_allocate_info.commandBufferCount = DOUBLE_BUFFERED;
			command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;

			VkCommandBuffer command_buffers[DOUBLE_BUFFERED];
			err = vkAllocateCommandBuffers (vulkan_globals.device, &command_buffer_allocate_info, command_buffers);
			if (err != VK_SUCCESS)
				Sys_Error ("vkAllocateCommandBuffers failed with code %i", (int)err);
			for (int j = 0; j < DOUBLE_BUFFERED; ++j)
			{
				secondary_command_buffers[scbx_index][j][i] = command_buffers[j];
				GL_SetObjectName (
					(uint64_t)(uintptr_t)command_buffers[j], VK_OBJECT_TYPE_COMMAND_BUFFER, va ("SCBX index: %d sub_index: %d cb_index: %d", scbx_index, i, j));
			}
		}
	}

	ZEROED_STRUCT (VkFenceCreateInfo, fence_create_info);
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

	for (int i = 0; i < DOUBLE_BUFFERED; ++i)
	{
		err = vkCreateFence (vulkan_globals.device, &fence_create_info, NULL, &command_buffer_fences[i]);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateFence failed with code %i", (int)err);
	}
}

/*
===============
GL_CreateDepthBuffer
===============
*/
static void GL_CreateDepthBuffer (void)
{
	Sys_Printf ("Creating depth buffer\n");

	if (depth_buffer != VK_NULL_HANDLE)
		return;

	VkResult err;

	ZEROED_STRUCT (VkImageCreateInfo, image_create_info);
	image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_create_info.pNext = NULL;
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.format = vulkan_globals.depth_format;
	image_create_info.extent.width = vid.render_width;
	image_create_info.extent.height = vid.render_height;
	image_create_info.extent.depth = 1;
	image_create_info.mipLevels = 1;
	image_create_info.arrayLayers = vulkan_globals.stereo_active ? 2 : 1;
	image_create_info.samples = vulkan_globals.sample_count;
	image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_create_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	if (R_SSAOEnabled ())
		image_create_info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
#if defined(VK_QCOM_fragment_density_map_offset)
	if (vulkan_globals.openxr_fragment_density_map_active && vulkan_globals.openxr_fragment_density_offset_enabled)
		image_create_info.flags |= VK_IMAGE_CREATE_FRAGMENT_DENSITY_MAP_OFFSET_BIT_QCOM;
#endif

	assert (depth_buffer == VK_NULL_HANDLE);
	err = vkCreateImage (vulkan_globals.device, &image_create_info, NULL, &depth_buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateImage failed with code %i", (int)err);

	GL_SetObjectName ((uint64_t)depth_buffer, VK_OBJECT_TYPE_IMAGE, "Depth Buffer");

	VkMemoryRequirements memory_requirements;
	vkGetImageMemoryRequirements (vulkan_globals.device, depth_buffer, &memory_requirements);

	ZEROED_STRUCT (VkMemoryDedicatedAllocateInfoKHR, dedicated_allocation_info);
	dedicated_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
	dedicated_allocation_info.image = depth_buffer;

	ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

	if (vulkan_globals.dedicated_allocation)
		memory_allocate_info.pNext = &dedicated_allocation_info;

	assert (depth_buffer_memory.handle == VK_NULL_HANDLE);
	R_AllocateVulkanMemory (&depth_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE, &num_vulkan_misc_allocations);
	GL_SetObjectName ((uint64_t)depth_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Depth Buffer");

	err = vkBindImageMemory (vulkan_globals.device, depth_buffer, depth_buffer_memory.handle, 0);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindImageMemory failed with code %i", (int)err);

	ZEROED_STRUCT (VkImageViewCreateInfo, image_view_create_info);
	image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	image_view_create_info.format = vulkan_globals.depth_format;
	image_view_create_info.image = depth_buffer;
	image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	image_view_create_info.subresourceRange.baseMipLevel = 0;
	image_view_create_info.subresourceRange.levelCount = 1;
	image_view_create_info.subresourceRange.baseArrayLayer = 0;
	image_view_create_info.subresourceRange.layerCount = vulkan_globals.stereo_active ? 2 : 1;
	image_view_create_info.viewType = vulkan_globals.stereo_active ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
	image_view_create_info.flags = 0;

	assert (depth_buffer_view == VK_NULL_HANDLE);
	err = vkCreateImageView (vulkan_globals.device, &image_view_create_info, NULL, &depth_buffer_view);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateImageView failed with code %i", (int)err);

	GL_SetObjectName ((uint64_t)depth_buffer_view, VK_OBJECT_TYPE_IMAGE_VIEW, "Depth Buffer View");
	vulkan_globals.particle_depth_descriptor_set = R_AllocateDescriptorSet (&vulkan_globals.input_attachment_set_layout);
	const VkDescriptorImageInfo depth_info = {VK_NULL_HANDLE, depth_buffer_view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
	const VkWriteDescriptorSet	depth_write = {
		 .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		 .dstSet = vulkan_globals.particle_depth_descriptor_set,
		 .dstBinding = 0,
		 .descriptorCount = 1,
		 .descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
		 .pImageInfo = &depth_info,
	 };
	vkUpdateDescriptorSets (vulkan_globals.device, 1, &depth_write, 0, NULL);
}

/*
===============
GL_CreateUIColorBuffer
===============
*/
static void GL_CreateUIColorBuffer (void)
{
	VkResult err;

	Sys_Printf ("Creating native UI color buffer\n");

	ZEROED_STRUCT (VkImageCreateInfo, image_create_info);
	image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_create_info.pNext = NULL;
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.format = vulkan_globals.color_format;
	image_create_info.extent.width = vid.width;
	image_create_info.extent.height = vid.height;
	image_create_info.extent.depth = 1;
	image_create_info.mipLevels = 1;
	image_create_info.arrayLayers = vulkan_globals.stereo_active ? 2 : 1;
	image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

	assert (ui_color_buffer == VK_NULL_HANDLE);
	err = vkCreateImage (vulkan_globals.device, &image_create_info, NULL, &ui_color_buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateImage failed with code %i", (int)err);

	GL_SetObjectName ((uint64_t)ui_color_buffer, VK_OBJECT_TYPE_IMAGE, "UI Color Buffer");

	VkMemoryRequirements memory_requirements;
	vkGetImageMemoryRequirements (vulkan_globals.device, ui_color_buffer, &memory_requirements);

	ZEROED_STRUCT (VkMemoryDedicatedAllocateInfoKHR, dedicated_allocation_info);
	dedicated_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
	dedicated_allocation_info.image = ui_color_buffer;

	ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

	if (vulkan_globals.dedicated_allocation)
		memory_allocate_info.pNext = &dedicated_allocation_info;

	assert (ui_color_buffer_memory.handle == VK_NULL_HANDLE);
	R_AllocateVulkanMemory (&ui_color_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE, &num_vulkan_misc_allocations);
	GL_SetObjectName ((uint64_t)ui_color_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "UI Color Buffer");

	err = vkBindImageMemory (vulkan_globals.device, ui_color_buffer, ui_color_buffer_memory.handle, 0);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindImageMemory failed with code %i", (int)err);

	ZEROED_STRUCT (VkImageViewCreateInfo, image_view_create_info);
	image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	image_view_create_info.format = vulkan_globals.color_format;
	image_view_create_info.image = ui_color_buffer;
	image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_view_create_info.subresourceRange.baseMipLevel = 0;
	image_view_create_info.subresourceRange.levelCount = 1;
	image_view_create_info.subresourceRange.baseArrayLayer = 0;
	image_view_create_info.subresourceRange.layerCount = vulkan_globals.stereo_active ? 2 : 1;
	image_view_create_info.viewType = vulkan_globals.stereo_active ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
	image_view_create_info.flags = 0;

	assert (ui_color_buffer_view == VK_NULL_HANDLE);
	err = vkCreateImageView (vulkan_globals.device, &image_view_create_info, NULL, &ui_color_buffer_view);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateImageView failed with code %i", (int)err);

	GL_SetObjectName ((uint64_t)ui_color_buffer_view, VK_OBJECT_TYPE_IMAGE_VIEW, "UI Color Buffer View");
}

static void GL_CreateColorBuffer (void)
{
	VkResult err;
	int		 i;

	Sys_Printf ("Creating color buffer\n");

	ZEROED_STRUCT (VkImageCreateInfo, image_create_info);
	image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_create_info.pNext = NULL;
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.format = vulkan_globals.color_format;
	image_create_info.extent.width = vid.render_width;
	image_create_info.extent.height = vid.render_height;
	image_create_info.extent.depth = 1;
	image_create_info.mipLevels = 1;
	image_create_info.arrayLayers = vulkan_globals.stereo_active ? 2 : 1;
	image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_create_info.usage =
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
#if defined(VK_QCOM_fragment_density_map_offset)
	if (vulkan_globals.openxr_fragment_density_map_active && vulkan_globals.openxr_fragment_density_offset_enabled)
		image_create_info.flags |= VK_IMAGE_CREATE_FRAGMENT_DENSITY_MAP_OFFSET_BIT_QCOM;
#endif

	for (i = 0; i < NUM_COLOR_BUFFERS; ++i)
	{
		assert (vulkan_globals.color_buffers[i] == VK_NULL_HANDLE);
		err = vkCreateImage (vulkan_globals.device, &image_create_info, NULL, &vulkan_globals.color_buffers[i]);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateImage failed with code %i", (int)err);

		GL_SetObjectName ((uint64_t)vulkan_globals.color_buffers[i], VK_OBJECT_TYPE_IMAGE, va ("Color Buffer %d", i));

		VkMemoryRequirements memory_requirements;
		vkGetImageMemoryRequirements (vulkan_globals.device, vulkan_globals.color_buffers[i], &memory_requirements);

		ZEROED_STRUCT (VkMemoryDedicatedAllocateInfoKHR, dedicated_allocation_info);
		dedicated_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
		dedicated_allocation_info.image = vulkan_globals.color_buffers[i];

		ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

		if (vulkan_globals.dedicated_allocation)
			memory_allocate_info.pNext = &dedicated_allocation_info;

		assert (color_buffers_memory[i].handle == VK_NULL_HANDLE);
		R_AllocateVulkanMemory (&color_buffers_memory[i], &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE, &num_vulkan_misc_allocations);
		GL_SetObjectName ((uint64_t)color_buffers_memory[i].handle, VK_OBJECT_TYPE_DEVICE_MEMORY, va ("Color Buffer %d", i));

		err = vkBindImageMemory (vulkan_globals.device, vulkan_globals.color_buffers[i], color_buffers_memory[i].handle, 0);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindImageMemory failed with code %i", (int)err);

		ZEROED_STRUCT (VkImageViewCreateInfo, image_view_create_info);
		image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		image_view_create_info.format = vulkan_globals.color_format;
		image_view_create_info.image = vulkan_globals.color_buffers[i];
		image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_view_create_info.subresourceRange.baseMipLevel = 0;
		image_view_create_info.subresourceRange.levelCount = 1;
		image_view_create_info.subresourceRange.baseArrayLayer = 0;
		image_view_create_info.subresourceRange.layerCount = vulkan_globals.stereo_active ? 2 : 1;
		image_view_create_info.viewType = vulkan_globals.stereo_active ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
		image_view_create_info.flags = 0;

		assert (color_buffers_view[i] == VK_NULL_HANDLE);
		err = vkCreateImageView (vulkan_globals.device, &image_view_create_info, NULL, &color_buffers_view[i]);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateImageView failed with code %i", (int)err);

		GL_SetObjectName ((uint64_t)color_buffers_view[i], VK_OBJECT_TYPE_IMAGE_VIEW, va ("Color Buffer View %d", i));
	}

	vulkan_globals.sample_count = VK_SAMPLE_COUNT_1_BIT;
	vulkan_globals.supersampling = false;

	{
		const int fsaa = (int)vid_fsaa.value;

		VkImageFormatProperties image_format_properties;
		vkGetPhysicalDeviceImageFormatProperties (
			vulkan_physical_device, vulkan_globals.color_format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, image_create_info.usage, 0,
			&image_format_properties);

		// Workaround: Intel advertises 16 samples but crashes when using it.
		if ((fsaa >= 16) && (image_format_properties.sampleCounts & VK_SAMPLE_COUNT_16_BIT) && (vulkan_globals.device_properties.vendorID != 0x8086))
			vulkan_globals.sample_count = VK_SAMPLE_COUNT_16_BIT;
		else if ((fsaa >= 8) && (image_format_properties.sampleCounts & VK_SAMPLE_COUNT_8_BIT))
			vulkan_globals.sample_count = VK_SAMPLE_COUNT_8_BIT;
		else if ((fsaa >= 4) && (image_format_properties.sampleCounts & VK_SAMPLE_COUNT_4_BIT))
			vulkan_globals.sample_count = VK_SAMPLE_COUNT_4_BIT;
		else if ((fsaa >= 2) && (image_format_properties.sampleCounts & VK_SAMPLE_COUNT_2_BIT))
			vulkan_globals.sample_count = VK_SAMPLE_COUNT_2_BIT;

		switch (vulkan_globals.sample_count)
		{
		case VK_SAMPLE_COUNT_2_BIT:
			Sys_Printf ("2 AA Samples\n");
			break;
		case VK_SAMPLE_COUNT_4_BIT:
			Sys_Printf ("4 AA Samples\n");
			break;
		case VK_SAMPLE_COUNT_8_BIT:
			Sys_Printf ("8 AA Samples\n");
			break;
		case VK_SAMPLE_COUNT_16_BIT:
			Sys_Printf ("16 AA Samples\n");
			break;
		default:
			break;
		}
	}
	GL_QueryFragmentShadingRatesForSamples (vulkan_globals.sample_count);

	if (vulkan_globals.sample_count != VK_SAMPLE_COUNT_1_BIT)
	{
		vulkan_globals.supersampling = (vulkan_globals.device_features.sampleRateShading && vid_fsaamode.value >= 1) ? true : false;

		if (vulkan_globals.supersampling)
			Sys_Printf ("Supersampling enabled\n");

		image_create_info.samples = vulkan_globals.sample_count;
		image_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		image_create_info.flags = 0;

		assert (msaa_color_buffer == VK_NULL_HANDLE);
		err = vkCreateImage (vulkan_globals.device, &image_create_info, NULL, &msaa_color_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateImage failed with code %i", (int)err);

		GL_SetObjectName ((uint64_t)msaa_color_buffer, VK_OBJECT_TYPE_IMAGE, "MSAA Color Buffer");

		VkMemoryRequirements memory_requirements;
		vkGetImageMemoryRequirements (vulkan_globals.device, msaa_color_buffer, &memory_requirements);

		ZEROED_STRUCT (VkMemoryDedicatedAllocateInfoKHR, dedicated_allocation_info);
		dedicated_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
		dedicated_allocation_info.image = msaa_color_buffer;

		ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

		if (vulkan_globals.dedicated_allocation)
			memory_allocate_info.pNext = &dedicated_allocation_info;

		assert (msaa_color_buffer_memory.handle == VK_NULL_HANDLE);
		R_AllocateVulkanMemory (&msaa_color_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE, &num_vulkan_misc_allocations);
		GL_SetObjectName ((uint64_t)msaa_color_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "MSAA Color Buffer");

		err = vkBindImageMemory (vulkan_globals.device, msaa_color_buffer, msaa_color_buffer_memory.handle, 0);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindImageMemory failed with code %i", (int)err);

		ZEROED_STRUCT (VkImageViewCreateInfo, image_view_create_info);
		image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		image_view_create_info.format = vulkan_globals.color_format;
		image_view_create_info.image = msaa_color_buffer;
		image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_view_create_info.subresourceRange.baseMipLevel = 0;
		image_view_create_info.subresourceRange.levelCount = 1;
		image_view_create_info.subresourceRange.baseArrayLayer = 0;
		image_view_create_info.subresourceRange.layerCount = vulkan_globals.stereo_active ? 2 : 1;
		image_view_create_info.viewType = vulkan_globals.stereo_active ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
		image_view_create_info.flags = 0;

		assert (msaa_color_buffer_view == VK_NULL_HANDLE);
		err = vkCreateImageView (vulkan_globals.device, &image_view_create_info, NULL, &msaa_color_buffer_view);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateImageView failed with code %i", (int)err);
	}
	else
		Sys_Printf ("AA disabled\n");

	if (R_UseOIT ())
		GL_CreateOITBuffers ();
}

static qboolean GL_CreateFragmentShadingRateImage (void)
{
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
	if (!vulkan_globals.openxr_fragment_shading_rate_active || !vulkan_globals.stereo_active)
		return false;
	if (vulkan_globals.supersampling)
	{
		vulkan_globals.openxr_fragment_shading_rate_active = false;
		Con_Printf ("OpenXR shading-rate map disabled while supersampling is active.\n");
		return false;
	}
	if (!fragment_shading_rate_sample_query_known || fragment_shading_rate_sample_count != vulkan_globals.sample_count)
		GL_QueryFragmentShadingRatesForSamples (vulkan_globals.sample_count);
	if (!fragment_shading_rate_sample_query_known || fragment_shading_rate_sample_count != vulkan_globals.sample_count ||
		!fragment_shading_rate_1x1_supported || (!fragment_shading_rate_2x2_supported && !fragment_shading_rate_4x4_supported))
	{
		vulkan_globals.openxr_fragment_shading_rate_active = false;
		Con_Printf ("OpenXR shading-rate map disabled: no coarse rate supports the scene sample count.\n");
		return false;
	}

	const VkExtent2D texel_size = vulkan_globals.openxr_fragment_shading_rate_texel_size;
	unsigned int rate_width, rate_height;
	if (vid.render_width <= 0 || vid.render_height <= 0 || !VRF_RateMapExtent (vid.render_width, texel_size.width, &rate_width) ||
		!VRF_RateMapExtent (vid.render_height, texel_size.height, &rate_height))
	{
		vulkan_globals.openxr_fragment_shading_rate_active = false;
		Con_Printf ("OpenXR shading-rate map disabled: invalid scene extent or texel size.\n");
		return false;
	}
	const uint32_t layers = vulkan_globals.openxr_layered_shading_rate_attachments ? 2 : 1;
	if (rate_width > fragment_shading_rate_image_format_properties.maxExtent.width ||
		rate_height > fragment_shading_rate_image_format_properties.maxExtent.height ||
		layers > fragment_shading_rate_image_format_properties.maxArrayLayers)
	{
		vulkan_globals.openxr_fragment_shading_rate_active = false;
		Con_Printf ("OpenXR shading-rate map disabled: scene map exceeds Vulkan image limits.\n");
		return false;
	}
	const size_t tile_count = (size_t)rate_width * rate_height;
	if (tile_count > SIZE_MAX / layers || tile_count * layers > INT_MAX)
	{
		vulkan_globals.openxr_fragment_shading_rate_active = false;
		Con_Printf ("OpenXR shading-rate map disabled: scene map is too large.\n");
		return false;
	}
	fragment_shading_rate_map_size = tile_count * layers;
	fragment_shading_rate_map = Mem_Alloc (fragment_shading_rate_map_size);
	fragment_shading_rate_uploaded_map = Mem_Alloc (fragment_shading_rate_map_size);
	memset (fragment_shading_rate_map, 0, fragment_shading_rate_map_size);
	fragment_shading_rate_image_extent = (VkExtent2D){rate_width, rate_height};
	fragment_shading_rate_image_layers = layers;

	ZEROED_STRUCT (VkImageCreateInfo, image_create_info);
	image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.format = VK_FORMAT_R8_UINT;
	image_create_info.extent.width = rate_width;
	image_create_info.extent.height = rate_height;
	image_create_info.extent.depth = 1;
	image_create_info.mipLevels = 1;
	image_create_info.arrayLayers = layers;
	image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_create_info.usage = VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	VkResult result = vkCreateImage (vulkan_globals.device, &image_create_info, NULL, &fragment_shading_rate_image);
	if (result != VK_SUCCESS)
		Sys_Error ("Couldn't create OpenXR shading-rate image: %d", result);
	GL_SetObjectName ((uint64_t)fragment_shading_rate_image, VK_OBJECT_TYPE_IMAGE, "OpenXR Shading Rate Map");

	VkMemoryRequirements memory_requirements;
	vkGetImageMemoryRequirements (vulkan_globals.device, fragment_shading_rate_image, &memory_requirements);
	ZEROED_STRUCT (VkMemoryDedicatedAllocateInfoKHR, dedicated_allocation_info);
	dedicated_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
	dedicated_allocation_info.image = fragment_shading_rate_image;
	ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
	if (vulkan_globals.dedicated_allocation)
		memory_allocate_info.pNext = &dedicated_allocation_info;
	R_AllocateVulkanMemory (&fragment_shading_rate_image_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE, &num_vulkan_misc_allocations);
	result = vkBindImageMemory (vulkan_globals.device, fragment_shading_rate_image, fragment_shading_rate_image_memory.handle, 0);
	if (result != VK_SUCCESS)
		Sys_Error ("Couldn't bind OpenXR shading-rate image memory: %d", result);
	GL_SetObjectName ((uint64_t)fragment_shading_rate_image_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "OpenXR Shading Rate Map");

	ZEROED_STRUCT (VkImageViewCreateInfo, image_view_create_info);
	image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	image_view_create_info.image = fragment_shading_rate_image;
	image_view_create_info.viewType = layers == 2 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
	image_view_create_info.format = VK_FORMAT_R8_UINT;
	image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_view_create_info.subresourceRange.levelCount = 1;
	image_view_create_info.subresourceRange.layerCount = layers;
	result = vkCreateImageView (vulkan_globals.device, &image_view_create_info, NULL, &fragment_shading_rate_image_view);
	if (result != VK_SUCCESS)
		Sys_Error ("Couldn't create OpenXR shading-rate image view: %d", result);
	GL_SetObjectName ((uint64_t)fragment_shading_rate_image_view, VK_OBJECT_TYPE_IMAGE_VIEW, "OpenXR Shading Rate Map View");
	fragment_shading_rate_image_initialized = false;
	return true;
#else
	return false;
#endif
}

static void GL_DestroyFragmentShadingRateImage (void)
{
	if (fragment_shading_rate_image_view != VK_NULL_HANDLE)
		vkDestroyImageView (vulkan_globals.device, fragment_shading_rate_image_view, NULL);
	if (fragment_shading_rate_image != VK_NULL_HANDLE)
		vkDestroyImage (vulkan_globals.device, fragment_shading_rate_image, NULL);
	if (fragment_shading_rate_image_memory.handle != VK_NULL_HANDLE)
		R_FreeVulkanMemory (&fragment_shading_rate_image_memory, &num_vulkan_misc_allocations);
	if (fragment_shading_rate_map)
		Mem_Free (fragment_shading_rate_map);
	if (fragment_shading_rate_uploaded_map)
		Mem_Free (fragment_shading_rate_uploaded_map);
	fragment_shading_rate_image = VK_NULL_HANDLE;
	fragment_shading_rate_image_view = VK_NULL_HANDLE;
	fragment_shading_rate_image_extent = (VkExtent2D){0, 0};
	fragment_shading_rate_image_layers = 0;
	fragment_shading_rate_map = NULL;
	fragment_shading_rate_uploaded_map = NULL;
	fragment_shading_rate_map_size = 0;
	fragment_shading_rate_image_initialized = false;
}

static void GL_CreateOITImage (VkImage *image, vulkan_memory_t *memory, VkImageView *view, VkFormat format, const char *name)
{
	VkResult err;

	ZEROED_STRUCT (VkImageCreateInfo, image_create_info);
	image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.extent.width = vid.render_width;
	image_create_info.extent.height = vid.render_height;
	image_create_info.extent.depth = 1;
	image_create_info.mipLevels = 1;
	image_create_info.arrayLayers = vulkan_globals.stereo_active ? 2 : 1;
	image_create_info.samples = vulkan_globals.sample_count;
	image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	image_create_info.format = format;

	assert (*image == VK_NULL_HANDLE);
	err = vkCreateImage (vulkan_globals.device, &image_create_info, NULL, image);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateImage failed with code %i", (int)err);
	GL_SetObjectName ((uint64_t)*image, VK_OBJECT_TYPE_IMAGE, name);

	{
		VkMemoryRequirements memory_requirements;
		vkGetImageMemoryRequirements (vulkan_globals.device, *image, &memory_requirements);

		ZEROED_STRUCT (VkMemoryDedicatedAllocateInfoKHR, dedicated_allocation_info);
		dedicated_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
		dedicated_allocation_info.image = *image;

		ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

		if (vulkan_globals.dedicated_allocation)
			memory_allocate_info.pNext = &dedicated_allocation_info;

		assert (memory->handle == VK_NULL_HANDLE);
		R_AllocateVulkanMemory (memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE, &num_vulkan_misc_allocations);
		GL_SetObjectName ((uint64_t)memory->handle, VK_OBJECT_TYPE_DEVICE_MEMORY, name);

		err = vkBindImageMemory (vulkan_globals.device, *image, memory->handle, 0);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindImageMemory failed with code %i", (int)err);
	}

	{
		ZEROED_STRUCT (VkImageViewCreateInfo, image_view_create_info);
		image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		image_view_create_info.format = format;
		image_view_create_info.image = *image;
		image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_view_create_info.subresourceRange.baseMipLevel = 0;
		image_view_create_info.subresourceRange.levelCount = 1;
		image_view_create_info.subresourceRange.baseArrayLayer = 0;
		image_view_create_info.subresourceRange.layerCount = vulkan_globals.stereo_active ? 2 : 1;
		image_view_create_info.viewType = vulkan_globals.stereo_active ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;

		assert (*view == VK_NULL_HANDLE);
		err = vkCreateImageView (vulkan_globals.device, &image_view_create_info, NULL, view);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateImageView failed with code %i", (int)err);
		GL_SetObjectName ((uint64_t)*view, VK_OBJECT_TYPE_IMAGE_VIEW, va ("%s View", name));
	}
}

/*
===============
GL_CreateOITBuffers
===============
*/
static void GL_CreateOITBuffers (void)
{
	VkResult err;

	ZEROED_STRUCT (VkImageCreateInfo, image_create_info);
	image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.extent.width = vid.render_width;
	image_create_info.extent.height = vid.render_height;
	image_create_info.extent.depth = 1;
	image_create_info.mipLevels = 1;
	image_create_info.arrayLayers = vulkan_globals.stereo_active ? 2 : 1;
	image_create_info.samples = vulkan_globals.sample_count;
	image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

	if (R_UseWBOIT ())
	{
		image_create_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		assert (vulkan_globals.oit_accum_buffer == VK_NULL_HANDLE);
		err = vkCreateImage (vulkan_globals.device, &image_create_info, NULL, &vulkan_globals.oit_accum_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateImage failed with code %i", (int)err);

		GL_SetObjectName ((uint64_t)vulkan_globals.oit_accum_buffer, VK_OBJECT_TYPE_IMAGE, "OIT Accum Buffer");

		{
			VkMemoryRequirements memory_requirements;
			vkGetImageMemoryRequirements (vulkan_globals.device, vulkan_globals.oit_accum_buffer, &memory_requirements);

			ZEROED_STRUCT (VkMemoryDedicatedAllocateInfoKHR, dedicated_allocation_info);
			dedicated_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
			dedicated_allocation_info.image = vulkan_globals.oit_accum_buffer;

			ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
			memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			memory_allocate_info.allocationSize = memory_requirements.size;
			memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

			if (vulkan_globals.dedicated_allocation)
				memory_allocate_info.pNext = &dedicated_allocation_info;

			assert (oit_accum_buffer_memory.handle == VK_NULL_HANDLE);
			R_AllocateVulkanMemory (&oit_accum_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE, &num_vulkan_misc_allocations);
			GL_SetObjectName ((uint64_t)oit_accum_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "OIT Accum Buffer");

			err = vkBindImageMemory (vulkan_globals.device, vulkan_globals.oit_accum_buffer, oit_accum_buffer_memory.handle, 0);
			if (err != VK_SUCCESS)
				Sys_Error ("vkBindImageMemory failed with code %i", (int)err);
		}

		{
			ZEROED_STRUCT (VkImageViewCreateInfo, image_view_create_info);
			image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			image_view_create_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
			image_view_create_info.image = vulkan_globals.oit_accum_buffer;
			image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			image_view_create_info.subresourceRange.baseMipLevel = 0;
			image_view_create_info.subresourceRange.levelCount = 1;
			image_view_create_info.subresourceRange.baseArrayLayer = 0;
			image_view_create_info.subresourceRange.layerCount = vulkan_globals.stereo_active ? 2 : 1;
			image_view_create_info.viewType = vulkan_globals.stereo_active ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
			image_view_create_info.flags = 0;

			assert (oit_accum_buffer_view == VK_NULL_HANDLE);
			err = vkCreateImageView (vulkan_globals.device, &image_view_create_info, NULL, &oit_accum_buffer_view);
			if (err != VK_SUCCESS)
				Sys_Error ("vkCreateImageView failed with code %i", (int)err);

			GL_SetObjectName ((uint64_t)oit_accum_buffer_view, VK_OBJECT_TYPE_IMAGE_VIEW, "OIT Accum Buffer View");
		}

		image_create_info.format = VK_FORMAT_R8_UNORM;
		assert (vulkan_globals.oit_reveal_buffer == VK_NULL_HANDLE);
		err = vkCreateImage (vulkan_globals.device, &image_create_info, NULL, &vulkan_globals.oit_reveal_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateImage failed with code %i", (int)err);

		GL_SetObjectName ((uint64_t)vulkan_globals.oit_reveal_buffer, VK_OBJECT_TYPE_IMAGE, "OIT Reveal Buffer");

		{
			VkMemoryRequirements memory_requirements;
			vkGetImageMemoryRequirements (vulkan_globals.device, vulkan_globals.oit_reveal_buffer, &memory_requirements);

			ZEROED_STRUCT (VkMemoryDedicatedAllocateInfoKHR, dedicated_allocation_info);
			dedicated_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
			dedicated_allocation_info.image = vulkan_globals.oit_reveal_buffer;

			ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
			memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			memory_allocate_info.allocationSize = memory_requirements.size;
			memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

			if (vulkan_globals.dedicated_allocation)
				memory_allocate_info.pNext = &dedicated_allocation_info;

			assert (oit_reveal_buffer_memory.handle == VK_NULL_HANDLE);
			R_AllocateVulkanMemory (&oit_reveal_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE, &num_vulkan_misc_allocations);
			GL_SetObjectName ((uint64_t)oit_reveal_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "OIT Reveal Buffer");

			err = vkBindImageMemory (vulkan_globals.device, vulkan_globals.oit_reveal_buffer, oit_reveal_buffer_memory.handle, 0);
			if (err != VK_SUCCESS)
				Sys_Error ("vkBindImageMemory failed with code %i", (int)err);
		}

		{
			ZEROED_STRUCT (VkImageViewCreateInfo, image_view_create_info);
			image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			image_view_create_info.format = VK_FORMAT_R8_UNORM;
			image_view_create_info.image = vulkan_globals.oit_reveal_buffer;
			image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			image_view_create_info.subresourceRange.baseMipLevel = 0;
			image_view_create_info.subresourceRange.levelCount = 1;
			image_view_create_info.subresourceRange.baseArrayLayer = 0;
			image_view_create_info.subresourceRange.layerCount = vulkan_globals.stereo_active ? 2 : 1;
			image_view_create_info.viewType = vulkan_globals.stereo_active ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
			image_view_create_info.flags = 0;

			assert (oit_reveal_buffer_view == VK_NULL_HANDLE);
			err = vkCreateImageView (vulkan_globals.device, &image_view_create_info, NULL, &oit_reveal_buffer_view);
			if (err != VK_SUCCESS)
				Sys_Error ("vkCreateImageView failed with code %i", (int)err);

			GL_SetObjectName ((uint64_t)oit_reveal_buffer_view, VK_OBJECT_TYPE_IMAGE_VIEW, "OIT Reveal Buffer View");
		}
	}

	if (R_UseMBOIT ())
	{
		GL_CreateOITImage (&vulkan_globals.mboit_b0_buffer, &mboit_b0_buffer_memory, &mboit_b0_buffer_view, VK_FORMAT_R32_SFLOAT, "MBOIT B0 Buffer");
		GL_CreateOITImage (
			&vulkan_globals.mboit_moments0_buffer, &mboit_moments0_buffer_memory, &mboit_moments0_buffer_view, VK_FORMAT_R32G32B32A32_SFLOAT,
			"MBOIT Moments 0 Buffer");
		GL_CreateOITImage (
			&vulkan_globals.mboit_color_buffer, &mboit_color_buffer_memory, &mboit_color_buffer_view, VK_FORMAT_R16G16B16A16_SFLOAT, "MBOIT Color Buffer");
	}
}

/*
===============
GL_DestroyOITBuffers
===============
*/
static void GL_DestroyOITBuffers (void)
{
	if (oit_accum_buffer_view != VK_NULL_HANDLE)
	{
		vkDestroyImageView (vulkan_globals.device, oit_accum_buffer_view, NULL);
		oit_accum_buffer_view = VK_NULL_HANDLE;
	}
	if (vulkan_globals.oit_accum_buffer != VK_NULL_HANDLE)
	{
		vkDestroyImage (vulkan_globals.device, vulkan_globals.oit_accum_buffer, NULL);
		vulkan_globals.oit_accum_buffer = VK_NULL_HANDLE;
	}
	if (oit_accum_buffer_memory.handle != VK_NULL_HANDLE)
		R_FreeVulkanMemory (&oit_accum_buffer_memory, &num_vulkan_misc_allocations);

	if (oit_reveal_buffer_view != VK_NULL_HANDLE)
	{
		vkDestroyImageView (vulkan_globals.device, oit_reveal_buffer_view, NULL);
		oit_reveal_buffer_view = VK_NULL_HANDLE;
	}
	if (vulkan_globals.oit_reveal_buffer != VK_NULL_HANDLE)
	{
		vkDestroyImage (vulkan_globals.device, vulkan_globals.oit_reveal_buffer, NULL);
		vulkan_globals.oit_reveal_buffer = VK_NULL_HANDLE;
	}
	if (oit_reveal_buffer_memory.handle != VK_NULL_HANDLE)
		R_FreeVulkanMemory (&oit_reveal_buffer_memory, &num_vulkan_misc_allocations);

	if (mboit_b0_buffer_view != VK_NULL_HANDLE)
	{
		vkDestroyImageView (vulkan_globals.device, mboit_b0_buffer_view, NULL);
		mboit_b0_buffer_view = VK_NULL_HANDLE;
	}
	if (vulkan_globals.mboit_b0_buffer != VK_NULL_HANDLE)
	{
		vkDestroyImage (vulkan_globals.device, vulkan_globals.mboit_b0_buffer, NULL);
		vulkan_globals.mboit_b0_buffer = VK_NULL_HANDLE;
	}
	if (mboit_b0_buffer_memory.handle != VK_NULL_HANDLE)
		R_FreeVulkanMemory (&mboit_b0_buffer_memory, &num_vulkan_misc_allocations);

	if (mboit_moments0_buffer_view != VK_NULL_HANDLE)
	{
		vkDestroyImageView (vulkan_globals.device, mboit_moments0_buffer_view, NULL);
		mboit_moments0_buffer_view = VK_NULL_HANDLE;
	}
	if (vulkan_globals.mboit_moments0_buffer != VK_NULL_HANDLE)
	{
		vkDestroyImage (vulkan_globals.device, vulkan_globals.mboit_moments0_buffer, NULL);
		vulkan_globals.mboit_moments0_buffer = VK_NULL_HANDLE;
	}
	if (mboit_moments0_buffer_memory.handle != VK_NULL_HANDLE)
		R_FreeVulkanMemory (&mboit_moments0_buffer_memory, &num_vulkan_misc_allocations);

	if (mboit_color_buffer_view != VK_NULL_HANDLE)
	{
		vkDestroyImageView (vulkan_globals.device, mboit_color_buffer_view, NULL);
		mboit_color_buffer_view = VK_NULL_HANDLE;
	}
	if (vulkan_globals.mboit_color_buffer != VK_NULL_HANDLE)
	{
		vkDestroyImage (vulkan_globals.device, vulkan_globals.mboit_color_buffer, NULL);
		vulkan_globals.mboit_color_buffer = VK_NULL_HANDLE;
	}
	if (mboit_color_buffer_memory.handle != VK_NULL_HANDLE)
		R_FreeVulkanMemory (&mboit_color_buffer_memory, &num_vulkan_misc_allocations);
}

/*
===============
GL_UpdateDescriptorSets
===============
*/
void GL_UpdateDescriptorSets (void)
{
	if (!render_resources_created)
		return;

	GL_WaitForDeviceIdle ();

	if (scene_upscale_descriptor_set != VK_NULL_HANDLE)
	{
		R_FreeDescriptorSet (scene_upscale_descriptor_set, &vulkan_globals.single_texture_set_layout);
		scene_upscale_descriptor_set = VK_NULL_HANDLE;
	}
	if (ui_color_buffer)
	{
		scene_upscale_descriptor_set = R_AllocateDescriptorSet (&vulkan_globals.single_texture_set_layout);
		const VkDescriptorImageInfo scene_info = {vulkan_globals.gui_linear_sampler, color_buffers_view[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		const VkWriteDescriptorSet	write = {
			 .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			 .dstSet = scene_upscale_descriptor_set,
			 .dstBinding = 0,
			 .descriptorCount = 1,
			 .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			 .pImageInfo = &scene_info};
		vkUpdateDescriptorSets (vulkan_globals.device, 1, &write, 0, NULL);
	}

	if (postprocess_descriptor_set != VK_NULL_HANDLE)
		R_FreeDescriptorSet (postprocess_descriptor_set, &vulkan_globals.input_attachment_set_layout);
	postprocess_descriptor_set = R_AllocateDescriptorSet (&vulkan_globals.input_attachment_set_layout);

	ZEROED_STRUCT (VkDescriptorImageInfo, image_info);
	image_info.imageView = ui_color_buffer_view ? ui_color_buffer_view : color_buffers_view[0];
	image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	ZEROED_STRUCT (VkWriteDescriptorSet, input_attachment_write);
	input_attachment_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	input_attachment_write.dstBinding = 0;
	input_attachment_write.dstArrayElement = 0;
	input_attachment_write.descriptorCount = 1;
	input_attachment_write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	input_attachment_write.dstSet = postprocess_descriptor_set;
	input_attachment_write.pImageInfo = &image_info;
	vkUpdateDescriptorSets (vulkan_globals.device, 1, &input_attachment_write, 0, NULL);

	if (wboit_resolve_descriptor_set != VK_NULL_HANDLE)
	{
		R_FreeDescriptorSet (wboit_resolve_descriptor_set, &vulkan_globals.oit_input_attachment_set_layout);
		wboit_resolve_descriptor_set = VK_NULL_HANDLE;
	}
	if (vulkan_globals.mboit_input_attachment_descriptor_set != VK_NULL_HANDLE)
	{
		R_FreeDescriptorSet (vulkan_globals.mboit_input_attachment_descriptor_set, &vulkan_globals.mboit_input_attachment_set_layout);
		vulkan_globals.mboit_input_attachment_descriptor_set = VK_NULL_HANDLE;
	}
	if (R_UseWBOIT ())
	{
		wboit_resolve_descriptor_set = R_AllocateDescriptorSet (&vulkan_globals.oit_input_attachment_set_layout);

		ZEROED_STRUCT_ARRAY (VkDescriptorImageInfo, oit_image_infos, 2);
		oit_image_infos[0].imageView = oit_accum_buffer_view;
		oit_image_infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		oit_image_infos[1].imageView = oit_reveal_buffer_view;
		oit_image_infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		ZEROED_STRUCT_ARRAY (VkWriteDescriptorSet, oit_writes, 2);
		for (int i = 0; i < 2; ++i)
		{
			oit_writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			oit_writes[i].dstBinding = i;
			oit_writes[i].dstArrayElement = 0;
			oit_writes[i].descriptorCount = 1;
			oit_writes[i].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
			oit_writes[i].dstSet = wboit_resolve_descriptor_set;
			oit_writes[i].pImageInfo = &oit_image_infos[i];
		}
		vkUpdateDescriptorSets (vulkan_globals.device, countof (oit_writes), oit_writes, 0, NULL);
	}
	else if (R_UseMBOIT ())
	{
		vulkan_globals.mboit_input_attachment_descriptor_set = R_AllocateDescriptorSet (&vulkan_globals.mboit_input_attachment_set_layout);

		ZEROED_STRUCT_ARRAY (VkDescriptorImageInfo, mboit_image_infos, 3);
		mboit_image_infos[0].imageView = mboit_b0_buffer_view;
		mboit_image_infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		mboit_image_infos[1].imageView = mboit_moments0_buffer_view;
		mboit_image_infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		mboit_image_infos[2].imageView = mboit_color_buffer_view;
		mboit_image_infos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		ZEROED_STRUCT_ARRAY (VkWriteDescriptorSet, mboit_writes, 3);
		for (int i = 0; i < 3; ++i)
		{
			mboit_writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			mboit_writes[i].dstBinding = i;
			mboit_writes[i].dstArrayElement = 0;
			mboit_writes[i].descriptorCount = 1;
			mboit_writes[i].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
			mboit_writes[i].dstSet = vulkan_globals.mboit_input_attachment_descriptor_set;
			mboit_writes[i].pImageInfo = &mboit_image_infos[i];
		}
		vkUpdateDescriptorSets (vulkan_globals.device, countof (mboit_writes), mboit_writes, 0, NULL);
	}

	if (vulkan_globals.screen_effects_desc_set != VK_NULL_HANDLE)
		R_FreeDescriptorSet (vulkan_globals.screen_effects_desc_set, &vulkan_globals.screen_effects_set_layout);
	vulkan_globals.screen_effects_desc_set = R_AllocateDescriptorSet (&vulkan_globals.screen_effects_set_layout);

	ZEROED_STRUCT (VkDescriptorImageInfo, input_image_info);
	input_image_info.imageView = color_buffers_view[1];
	input_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	input_image_info.sampler = vulkan_globals.linear_sampler;

	ZEROED_STRUCT (VkDescriptorImageInfo, output_image_info);
	output_image_info.imageView = color_buffers_view[0];
	output_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	ZEROED_STRUCT (VkDescriptorBufferInfo, palette_octree_info);
	palette_octree_info.buffer = palette_octree_buffer;
	palette_octree_info.offset = 0;
	palette_octree_info.range = VK_WHOLE_SIZE;

	ZEROED_STRUCT (VkDescriptorImageInfo, blue_noise_image_info);
	blue_noise_image_info.imageView = bluenoisetexture->image_view;
	blue_noise_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	blue_noise_image_info.sampler = vulkan_globals.linear_sampler;

	ZEROED_STRUCT_ARRAY (VkWriteDescriptorSet, screen_effects_writes, 5);
	screen_effects_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	screen_effects_writes[0].dstBinding = 0;
	screen_effects_writes[0].dstArrayElement = 0;
	screen_effects_writes[0].descriptorCount = 1;
	screen_effects_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	screen_effects_writes[0].dstSet = vulkan_globals.screen_effects_desc_set;
	screen_effects_writes[0].pImageInfo = &input_image_info;

	screen_effects_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	screen_effects_writes[1].dstBinding = 1;
	screen_effects_writes[1].dstArrayElement = 0;
	screen_effects_writes[1].descriptorCount = 1;
	screen_effects_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	screen_effects_writes[1].dstSet = vulkan_globals.screen_effects_desc_set;
	screen_effects_writes[1].pImageInfo = &blue_noise_image_info;

	screen_effects_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	screen_effects_writes[2].dstBinding = 2;
	screen_effects_writes[2].dstArrayElement = 0;
	screen_effects_writes[2].descriptorCount = 1;
	screen_effects_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	screen_effects_writes[2].dstSet = vulkan_globals.screen_effects_desc_set;
	screen_effects_writes[2].pImageInfo = &output_image_info;

	screen_effects_writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	screen_effects_writes[3].dstBinding = 3;
	screen_effects_writes[3].dstArrayElement = 0;
	screen_effects_writes[3].descriptorCount = 1;
	screen_effects_writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
	screen_effects_writes[3].dstSet = vulkan_globals.screen_effects_desc_set;
	screen_effects_writes[3].pTexelBufferView = &palette_buffer_view;

	screen_effects_writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	screen_effects_writes[4].dstBinding = 4;
	screen_effects_writes[4].dstArrayElement = 0;
	screen_effects_writes[4].descriptorCount = 1;
	screen_effects_writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	screen_effects_writes[4].dstSet = vulkan_globals.screen_effects_desc_set;
	screen_effects_writes[4].pBufferInfo = &palette_octree_info;

	vkUpdateDescriptorSets (vulkan_globals.device, countof (screen_effects_writes), screen_effects_writes, 0, NULL);

#if defined(_DEBUG)
	if (vulkan_globals.ray_query)
	{
		if (vulkan_globals.ray_debug_desc_set != VK_NULL_HANDLE)
			R_FreeDescriptorSet (vulkan_globals.ray_debug_desc_set, &vulkan_globals.ray_debug_set_layout);
		vulkan_globals.ray_debug_desc_set = R_AllocateDescriptorSet (&vulkan_globals.ray_debug_set_layout);

		ZEROED_STRUCT_ARRAY (VkWriteDescriptorSet, ray_debug_writes, 1);

		ray_debug_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		ray_debug_writes[0].dstBinding = 0;
		ray_debug_writes[0].dstArrayElement = 0;
		ray_debug_writes[0].descriptorCount = 1;
		ray_debug_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		ray_debug_writes[0].dstSet = vulkan_globals.ray_debug_desc_set;
		ray_debug_writes[0].pImageInfo = &output_image_info;

		vkUpdateDescriptorSets (vulkan_globals.device, countof (ray_debug_writes), ray_debug_writes, 0, NULL);
	}
#endif
}

/*
===============
GL_CreateSwapChain
===============
*/
static qboolean GL_CreateSwapChain (qboolean mirror)
{
	uint32_t i;
	VkResult err;
	int width = mirror ? VID_GetCurrentWidth () : vid.width;
	int height = mirror ? VID_GetCurrentHeight () : vid.height;
	if (width <= 0 || height <= 0)
		return false;

#if defined(VK_EXT_full_screen_exclusive)
	qboolean use_exclusive_full_screen = false;
	qboolean try_use_exclusive_full_screen = !mirror &&
		vulkan_globals.full_screen_exclusive && vulkan_globals.want_full_screen_exclusive && has_focus && VID_GetFullscreen ();
	ZEROED_STRUCT (VkSurfaceFullScreenExclusiveInfoEXT, full_screen_exclusive_info);
	ZEROED_STRUCT (VkSurfaceFullScreenExclusiveWin32InfoEXT, full_screen_exclusive_win32_info);
	if (try_use_exclusive_full_screen)
	{
		HWND hwnd = (HWND)SDL_GetPointerProperty (SDL_GetWindowProperties (draw_context), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);

		HMONITOR monitor = MonitorFromWindow (hwnd, MONITOR_DEFAULTTOPRIMARY);

		full_screen_exclusive_win32_info.sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT;
		full_screen_exclusive_win32_info.pNext = NULL;
		full_screen_exclusive_win32_info.hmonitor = monitor;

		full_screen_exclusive_info.sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT;
		full_screen_exclusive_info.pNext = &full_screen_exclusive_win32_info;
		full_screen_exclusive_info.fullScreenExclusive = VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT;

		ZEROED_STRUCT (VkPhysicalDeviceSurfaceInfo2KHR, surface_info_2);
		surface_info_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
		surface_info_2.pNext = &full_screen_exclusive_info;
		surface_info_2.surface = vulkan_surface;

		ZEROED_STRUCT (VkSurfaceCapabilitiesFullScreenExclusiveEXT, surface_capabilities_full_screen_exclusive);
		surface_capabilities_full_screen_exclusive.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_FULL_SCREEN_EXCLUSIVE_EXT;
		surface_capabilities_full_screen_exclusive.fullScreenExclusiveSupported = VK_FALSE;

		VkSurfaceCapabilities2KHR surface_capabilitities_2;
		surface_capabilitities_2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
		surface_capabilitities_2.pNext = &surface_capabilities_full_screen_exclusive;

		err = fpGetPhysicalDeviceSurfaceCapabilities2KHR (vulkan_physical_device, &surface_info_2, &surface_capabilitities_2);
		if (err != VK_SUCCESS)
			Sys_Error ("Couldn't get surface capabilities with code %i", (int)err);

		vulkan_surface_capabilities = surface_capabilitities_2.surfaceCapabilities;
		use_exclusive_full_screen = surface_capabilities_full_screen_exclusive.fullScreenExclusiveSupported;
	}
	else
#endif
	{
		err = fpGetPhysicalDeviceSurfaceCapabilitiesKHR (vulkan_physical_device, vulkan_surface, &vulkan_surface_capabilities);
		if (err != VK_SUCCESS)
		{
			if (!mirror)
				Sys_Error ("Couldn't get surface capabilities with code %i", (int)err);
			Con_Printf ("OpenXR mirror surface capabilities unavailable (%d).\n", err);
			return false;
		}
	}

	if (mirror && !(vulkan_surface_capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
	{
		Con_Printf ("OpenXR mirror unavailable: desktop surface cannot accept transfer copies.\n");
		return false;
	}
	if (mirror && !(vulkan_surface_capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR))
	{
		Con_Printf ("OpenXR mirror unavailable: desktop surface requires a rotated presentation transform.\n");
		return false;
	}
	if (mirror && vulkan_surface_capabilities.currentExtent.width != UINT32_MAX &&
		vulkan_surface_capabilities.currentExtent.height != UINT32_MAX)
	{
		if (vulkan_surface_capabilities.currentExtent.width > INT_MAX ||
			vulkan_surface_capabilities.currentExtent.height > INT_MAX)
			return false;
		width = (int)vulkan_surface_capabilities.currentExtent.width;
		height = (int)vulkan_surface_capabilities.currentExtent.height;
	}
	else if (mirror)
	{
		uint32_t w = (uint32_t)width, h = (uint32_t)height;
		if (w < vulkan_surface_capabilities.minImageExtent.width)
			w = vulkan_surface_capabilities.minImageExtent.width;
		if (h < vulkan_surface_capabilities.minImageExtent.height)
			h = vulkan_surface_capabilities.minImageExtent.height;
		if (w > vulkan_surface_capabilities.maxImageExtent.width)
			w = vulkan_surface_capabilities.maxImageExtent.width;
		if (h > vulkan_surface_capabilities.maxImageExtent.height)
			h = vulkan_surface_capabilities.maxImageExtent.height;
		if (w > INT_MAX || h > INT_MAX || !w || !h)
			return false;
		width = (int)w;
		height = (int)h;
	}
	if (width <= 0 || height <= 0 ||
		(!mirror && (vulkan_surface_capabilities.currentExtent.width != UINT32_MAX ||
			vulkan_surface_capabilities.currentExtent.height != UINT32_MAX) &&
			(vulkan_surface_capabilities.currentExtent.width != (uint32_t)width ||
				vulkan_surface_capabilities.currentExtent.height != (uint32_t)height)))
	{
		return false;
	}

#if defined(VK_KHR_present_wait2)
	swapchain_present_wait = false;
	if (vulkan_globals.present_wait && !mirror)
	{
		ZEROED_STRUCT (VkSurfaceCapabilitiesPresentId2KHR, present_id_2_capabilities);
		present_id_2_capabilities.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_ID_2_KHR;
		ZEROED_STRUCT (VkSurfaceCapabilitiesPresentWait2KHR, present_wait_2_capabilities);
		present_wait_2_capabilities.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_WAIT_2_KHR;
		present_wait_2_capabilities.pNext = &present_id_2_capabilities;

		ZEROED_STRUCT (VkPhysicalDeviceSurfaceInfo2KHR, present_wait_surface_info);
		present_wait_surface_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
		present_wait_surface_info.surface = vulkan_surface;

		ZEROED_STRUCT (VkSurfaceCapabilities2KHR, surface_capabilities_2);
		surface_capabilities_2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
		surface_capabilities_2.pNext = &present_wait_2_capabilities;

		err = fpGetPhysicalDeviceSurfaceCapabilities2KHR (vulkan_physical_device, &present_wait_surface_info, &surface_capabilities_2);
		if (err == VK_SUCCESS)
			swapchain_present_wait = present_id_2_capabilities.presentId2Supported && present_wait_2_capabilities.presentWait2Supported;
	}
#endif

	uint32_t format_count;
	err = fpGetPhysicalDeviceSurfaceFormatsKHR (vulkan_physical_device, vulkan_surface, &format_count, NULL);
	if (err != VK_SUCCESS || !format_count)
	{
		if (!mirror)
			Sys_Error ("Couldn't get surface formats with code %i", (int)err);
		return false;
	}

	VkSurfaceFormatKHR *surface_formats = (VkSurfaceFormatKHR *)Mem_Alloc (format_count * sizeof (VkSurfaceFormatKHR));
	err = fpGetPhysicalDeviceSurfaceFormatsKHR (vulkan_physical_device, vulkan_surface, &format_count, surface_formats);
	if (err != VK_SUCCESS)
	{
		if (!mirror)
			Sys_Error ("fpGetPhysicalDeviceSurfaceFormatsKHR failed with code %i", (int)err);
		Mem_Free (surface_formats);
		return false;
	}

	VkFormat		swap_chain_format = VK_FORMAT_B8G8R8A8_UNORM;
	VkColorSpaceKHR swap_chain_color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

	if (surface_formats[0].format != VK_FORMAT_UNDEFINED || format_count > 1)
	{
		qboolean found_wanted_format = false;
		for (i = 0; i < format_count; ++i)
		{
			if (surface_formats[i].format == swap_chain_format && surface_formats[i].colorSpace == swap_chain_color_space)
			{
				found_wanted_format = true;
				break;
			}
		}

		// If we can't find VK_FORMAT_B8G8R8A8_UNORM/VK_COLOR_SPACE_SRGB_NONLINEAR_KHR select first entry
		// I doubt this will ever happen, but the spec doesn't guarantee it
		if (!found_wanted_format)
		{
			swap_chain_format = surface_formats[0].format;
			swap_chain_color_space = surface_formats[0].colorSpace;
		}
	}
	if (mirror && (swap_chain_color_space != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ||
		(swap_chain_format != VK_FORMAT_B8G8R8A8_UNORM &&
		 swap_chain_format != VK_FORMAT_R8G8B8A8_UNORM)))
	{
		qboolean found = false;
		for (i = 0; i < format_count; ++i)
			if ((surface_formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
				 surface_formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) &&
				surface_formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				swap_chain_format = surface_formats[i].format;
				found = true;
				break;
			}
		if (!found)
		{
			Mem_Free (surface_formats);
			Con_Printf ("OpenXR mirror unavailable: desktop surface has no RGBA8 UNORM format.\n");
			return false;
		}
	}

	uint32_t present_mode_count = 0;
	err = fpGetPhysicalDeviceSurfacePresentModesKHR (vulkan_physical_device, vulkan_surface, &present_mode_count, NULL);
	if (err != VK_SUCCESS || !present_mode_count)
	{
		if (!mirror)
			Sys_Error ("fpGetPhysicalDeviceSurfacePresentModesKHR failed with code %i", (int)err);
		Mem_Free (surface_formats);
		return false;
	}

	VkPresentModeKHR *present_modes = (VkPresentModeKHR *)Mem_Alloc (present_mode_count * sizeof (VkPresentModeKHR));
	err = fpGetPhysicalDeviceSurfacePresentModesKHR (vulkan_physical_device, vulkan_surface, &present_mode_count, present_modes);
	if (err != VK_SUCCESS)
	{
		if (!mirror)
			Sys_Error ("fpGetPhysicalDeviceSurfacePresentModesKHR failed with code %i", (int)err);
		Mem_Free (present_modes);
		Mem_Free (surface_formats);
		return false;
	}

	// VK_PRESENT_MODE_FIFO_KHR is always supported
	VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
	if (vid_vsync.value == 0 || mirror)
	{
		qboolean found_immediate = false;
		qboolean found_mailbox = false;
		for (i = 0; i < present_mode_count; ++i)
		{
			if (present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
				found_immediate = true;
			if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
				found_mailbox = true;
		}

		if (found_immediate)
			present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
		if (found_mailbox)
			present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
		if (!mirror && found_immediate)
			present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
	}

	Mem_Free (present_modes);

	switch (present_mode)
	{
	case VK_PRESENT_MODE_FIFO_KHR:
		Sys_Printf ("Using FIFO present mode\n");
		break;
	case VK_PRESENT_MODE_MAILBOX_KHR:
		Sys_Printf ("Using MAILBOX present mode\n");
		break;
	case VK_PRESENT_MODE_IMMEDIATE_KHR:
		Sys_Printf ("Using IMMEDIATE present mode\n");
		break;
	default:
		break;
	}

	ZEROED_STRUCT (VkSwapchainCreateInfoKHR, swapchain_create_info);
	swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchain_create_info.pNext = NULL;
	swapchain_create_info.surface = vulkan_surface;
	swapchain_create_info.minImageCount = q_max ((vid_vsync.value >= 2) ? 3 : 2, vulkan_surface_capabilities.minImageCount);
	if (mirror && vulkan_surface_capabilities.maxImageCount &&
		swapchain_create_info.minImageCount > vulkan_surface_capabilities.maxImageCount)
		swapchain_create_info.minImageCount = vulkan_surface_capabilities.maxImageCount;
	swapchain_create_info.imageFormat = swap_chain_format;
	swapchain_create_info.imageColorSpace = swap_chain_color_space;
	swapchain_create_info.imageExtent.width = width;
	swapchain_create_info.imageExtent.height = height;
	swapchain_create_info.imageUsage = mirror ? VK_IMAGE_USAGE_TRANSFER_DST_BIT :
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	swapchain_create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	swapchain_create_info.imageArrayLayers = 1;
	swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchain_create_info.queueFamilyIndexCount = 0;
	swapchain_create_info.pQueueFamilyIndices = NULL;
	swapchain_create_info.presentMode = present_mode;
	swapchain_create_info.clipped = true;
	swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	// Not all devices support ALPHA_OPAQUE
	if (!(vulkan_surface_capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
		swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
#if defined(VK_KHR_present_wait2)
	if (swapchain_present_wait)
		swapchain_create_info.flags |= VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR | VK_SWAPCHAIN_CREATE_PRESENT_WAIT_2_BIT_KHR;
#endif

	vulkan_globals.swap_chain_full_screen_exclusive = false;
	vulkan_globals.swap_chain_full_screen_acquired = false;

#if defined(VK_EXT_full_screen_exclusive)
	if (use_exclusive_full_screen)
	{
		swapchain_create_info.pNext = &full_screen_exclusive_info;
		vulkan_globals.swap_chain_full_screen_exclusive = true;
	}
#endif

	vulkan_globals.swap_chain_format = swap_chain_format;
	Mem_Free (surface_formats);

	assert (vulkan_swapchain == VK_NULL_HANDLE);
	err = fpCreateSwapchainKHR (vulkan_globals.device, &swapchain_create_info, NULL, &vulkan_swapchain);
	if (err != VK_SUCCESS)
	{
#if defined(VK_EXT_full_screen_exclusive)
		if (use_exclusive_full_screen)
		{
			// At least one person reported that a driver fails to create the swap chain even though it advertises full screen exclusivity
			swapchain_create_info.pNext = NULL;
			vulkan_globals.swap_chain_full_screen_exclusive = false;
			use_exclusive_full_screen = false;
			err = fpCreateSwapchainKHR (vulkan_globals.device, &swapchain_create_info, NULL, &vulkan_swapchain);
		}
#endif
		if (err != VK_SUCCESS)
		{
			if (!mirror)
				Sys_Error ("Couldn't create swap chain with code %i", (int)err);
			Con_Printf ("OpenXR mirror swapchain unavailable (Vulkan %i).\n", (int)err);
			return false;
		}
	}
	if (mirror)
		openxr_mirror_extent = (VkExtent2D){(uint32_t)width, (uint32_t)height};
	num_images_acquired = 0;
#if defined(VK_KHR_present_wait2)
	current_present_id = 0; // present ids are scoped to the swapchain
#endif

	for (i = 0; i < num_swap_chain_images; ++i)
		assert (swapchain_images[i] == VK_NULL_HANDLE);
	num_swap_chain_images = 0;
	err = fpGetSwapchainImagesKHR (vulkan_globals.device, vulkan_swapchain, &num_swap_chain_images, NULL);
	if (err != VK_SUCCESS || !num_swap_chain_images || num_swap_chain_images > MAX_SWAP_CHAIN_IMAGES)
	{
		if (!mirror)
			Sys_Error ("Couldn't get swap chain images with code %i", (int)err);
		num_swap_chain_images = 0;
		GL_DestroySwapChainResources ();
		return false;
	}

	err = fpGetSwapchainImagesKHR (vulkan_globals.device, vulkan_swapchain, &num_swap_chain_images, swapchain_images);
	if (err != VK_SUCCESS)
	{
		if (!mirror)
			Sys_Error ("Couldn't get swap chain images with code %i", (int)err);
		GL_DestroySwapChainResources ();
		return false;
	}

	ZEROED_STRUCT (VkImageViewCreateInfo, image_view_create_info);
	image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	image_view_create_info.format = vulkan_globals.swap_chain_format;
	image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_R;
	image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_G;
	image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_B;
	image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_A;
	image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_view_create_info.subresourceRange.baseMipLevel = 0;
	image_view_create_info.subresourceRange.levelCount = 1;
	image_view_create_info.subresourceRange.baseArrayLayer = 0;
	image_view_create_info.subresourceRange.layerCount = 1;
	image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	image_view_create_info.flags = 0;

	ZEROED_STRUCT (VkSemaphoreCreateInfo, semaphore_create_info);
	semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	for (i = 0; i < num_swap_chain_images; ++i)
	{
		GL_SetObjectName ((uint64_t)swapchain_images[i], VK_OBJECT_TYPE_IMAGE, "Swap Chain");
		/* A transfer-only mirror image has no valid image-view usage. The XR
		 * framebuffers use runtime image views, never these desktop images. */
		if (mirror)
			continue;

		assert (swapchain_images_views[i] == VK_NULL_HANDLE);
		image_view_create_info.image = swapchain_images[i];
		err = vkCreateImageView (vulkan_globals.device, &image_view_create_info, NULL, &swapchain_images_views[i]);
		if (err != VK_SUCCESS)
		{
			if (!mirror)
				Sys_Error ("vkCreateImageView failed with code %i", (int)err);
			GL_DestroySwapChainResources ();
			return false;
		}

		GL_SetObjectName ((uint64_t)swapchain_images_views[i], VK_OBJECT_TYPE_IMAGE_VIEW, "Swap Chain View");
	}

	for (i = 0; i < DOUBLE_BUFFERED; ++i)
	{
		if (mirror)
			continue;
		assert (image_aquired_semaphores[i] == VK_NULL_HANDLE);
		err = vkCreateSemaphore (vulkan_globals.device, &semaphore_create_info, NULL, &image_aquired_semaphores[i]);
		if (err != VK_SUCCESS)
		{
			if (!mirror)
				Sys_Error ("vkCreateSemaphore failed with code %i", (int)err);
			GL_DestroySwapChainResources ();
			return false;
		}
	}

	// one draw complete semaphore per swapchain image, indexed by the acquired image: a present keeps
	// using its wait semaphore until that image is re-acquired, so per frame slot semaphores can be
	// re-signaled too early when the same image doesn't come back for a while
	for (i = 0; i < num_swap_chain_images; ++i)
	{
		assert (draw_complete_semaphores[i] == VK_NULL_HANDLE);
		err = vkCreateSemaphore (vulkan_globals.device, &semaphore_create_info, NULL, &draw_complete_semaphores[i]);
		if (err != VK_SUCCESS)
		{
			if (!mirror)
				Sys_Error ("vkCreateSemaphore failed with code %i", (int)err);
			GL_DestroySwapChainResources ();
			return false;
		}
	}

	return true;
}

static void GL_DestroySwapChainResources (void)
{
	for (uint32_t i = 0; i < num_swap_chain_images && i < MAX_SWAP_CHAIN_IMAGES; ++i)
	{
		if (swapchain_images_views[i])
			vkDestroyImageView (vulkan_globals.device, swapchain_images_views[i], NULL);
		swapchain_images_views[i] = VK_NULL_HANDLE;
		swapchain_images[i] = VK_NULL_HANDLE;
		if (draw_complete_semaphores[i])
			vkDestroySemaphore (vulkan_globals.device, draw_complete_semaphores[i], NULL);
		draw_complete_semaphores[i] = VK_NULL_HANDLE;
	}
	for (int i = 0; i < DOUBLE_BUFFERED; ++i)
	{
		if (image_aquired_semaphores[i])
			vkDestroySemaphore (vulkan_globals.device, image_aquired_semaphores[i], NULL);
		image_aquired_semaphores[i] = VK_NULL_HANDLE;
	}
	if (vulkan_swapchain)
		fpDestroySwapchainKHR (vulkan_globals.device, vulkan_swapchain, NULL);
	vulkan_swapchain = VK_NULL_HANDLE;
	num_swap_chain_images = num_images_acquired = 0;
	openxr_mirror_extent = (VkExtent2D){0, 0};
}

static void GL_DestroyMirrorResources (void)
{
	openxr_mirror_ready = openxr_mirror_copy_ready = false;
	if (openxr_mirror_acquire_pending && openxr_mirror_acquire_fence)
		vkWaitForFences (vulkan_globals.device, 1,
			&openxr_mirror_acquire_fence, VK_TRUE, UINT64_MAX);
	openxr_mirror_acquire_pending = false;
	openxr_mirror_acquire_suboptimal = false;
	if (openxr_mirror_acquire_fence)
		vkDestroyFence (vulkan_globals.device, openxr_mirror_acquire_fence, NULL);
	openxr_mirror_acquire_fence = VK_NULL_HANDLE;
	for (int slot = 0; slot < DOUBLE_BUFFERED; ++slot)
	{
		if (openxr_mirror_fences[slot])
			vkDestroyFence (vulkan_globals.device, openxr_mirror_fences[slot], NULL);
		openxr_mirror_fences[slot] = VK_NULL_HANDLE;
		openxr_mirror_submitted[slot] = false;
		openxr_mirror_slot_available[slot] = false;
		openxr_mirror_snapshot_initialized[slot] = false;
		if (openxr_mirror_snapshots[slot])
			vkDestroyImage (vulkan_globals.device, openxr_mirror_snapshots[slot], NULL);
		openxr_mirror_snapshots[slot] = VK_NULL_HANDLE;
		if (openxr_mirror_memory[slot].handle)
			R_FreeVulkanMemory (&openxr_mirror_memory[slot], &num_vulkan_misc_allocations);
	}
	if (openxr_mirror_command_pool)
		vkDestroyCommandPool (vulkan_globals.device, openxr_mirror_command_pool, NULL);
	openxr_mirror_command_pool = VK_NULL_HANDLE;
	memset (openxr_mirror_commands, 0, sizeof (openxr_mirror_commands));
	openxr_mirror_extent = (VkExtent2D){0, 0};
}

/* Store already-encoded XR color bits as UNORM. A later UNORM-to-UNORM blit
 * resizes them without decoding sRGB into a dark desktop mirror. */
static qboolean GL_CreateMirrorResources (void)
{
	if (!vulkan_globals.stereo_active || !vulkan_swapchain ||
		!VRXR_VulkanTransferSourceAvailable () || num_swap_chain_images < 2)
		return false;
	const VkFormat xr_format = VRXR_VulkanColorFormat ();
	const VkFormat snapshot_format = xr_format == VK_FORMAT_R8G8B8A8_SRGB ?
		VK_FORMAT_R8G8B8A8_UNORM : xr_format == VK_FORMAT_B8G8R8A8_SRGB ?
		VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_UNDEFINED;
	if (snapshot_format == VK_FORMAT_UNDEFINED)
		return false;
	VkFormatProperties xr_props, snapshot_props, desktop_props;
	vkGetPhysicalDeviceFormatProperties (vulkan_physical_device, xr_format, &xr_props);
	vkGetPhysicalDeviceFormatProperties (vulkan_physical_device, snapshot_format, &snapshot_props);
	vkGetPhysicalDeviceFormatProperties (vulkan_physical_device, vulkan_globals.swap_chain_format, &desktop_props);
	if (!(xr_props.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) ||
		(snapshot_props.optimalTilingFeatures &
		 (VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
		  VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) !=
		 (VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
		  VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) ||
		(desktop_props.optimalTilingFeatures &
		 (VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT)) !=
		 (VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT))
	{
		Con_Printf ("OpenXR mirror unavailable: image formats cannot be linearly blitted.\n");
		return false;
	}
	const VkCommandPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = vulkan_globals.gfx_queue_family_index,
	};
	if (vkCreateCommandPool (vulkan_globals.device, &pool_info, NULL,
		&openxr_mirror_command_pool) != VK_SUCCESS)
		return false;
	const VkCommandBufferAllocateInfo command_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = openxr_mirror_command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = DOUBLE_BUFFERED,
	};
	if (vkAllocateCommandBuffers (vulkan_globals.device, &command_info,
		openxr_mirror_commands) != VK_SUCCESS)
		goto fail;
	for (int slot = 0; slot < DOUBLE_BUFFERED; ++slot)
	{
		const VkImageCreateInfo image_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = snapshot_format,
			.extent = {(uint32_t)vid.width, (uint32_t)vid.height, 1},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};
		if (vkCreateImage (vulkan_globals.device, &image_info, NULL,
			&openxr_mirror_snapshots[slot]) != VK_SUCCESS)
			goto fail;
		VkMemoryRequirements requirements;
		vkGetImageMemoryRequirements (vulkan_globals.device,
			openxr_mirror_snapshots[slot], &requirements);
		uint32_t memory_type = UINT32_MAX;
		for (uint32_t type = 0; type < vulkan_globals.memory_properties.memoryTypeCount; ++type)
			if ((requirements.memoryTypeBits & (1u << type)) &&
				(vulkan_globals.memory_properties.memoryTypes[type].propertyFlags &
				 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
			{
				memory_type = type;
				break;
			}
		if (memory_type == UINT32_MAX)
			goto fail;
		VkMemoryAllocateInfo allocation = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = requirements.size,
			.memoryTypeIndex = memory_type,
		};
		if (R_TryAllocateVulkanMemory (&openxr_mirror_memory[slot], &allocation,
			VULKAN_MEMORY_TYPE_DEVICE, &num_vulkan_misc_allocations) != VK_SUCCESS)
			goto fail;
		if (vkBindImageMemory (vulkan_globals.device, openxr_mirror_snapshots[slot],
			openxr_mirror_memory[slot].handle, 0) != VK_SUCCESS)
			goto fail;
		const VkFenceCreateInfo fence_info = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		};
		if (vkCreateFence (vulkan_globals.device, &fence_info, NULL,
			&openxr_mirror_fences[slot]) != VK_SUCCESS)
			goto fail;
		openxr_mirror_slot_available[slot] = true;
	}
	const VkFenceCreateInfo acquire_fence_info = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};
	if (vkCreateFence (vulkan_globals.device, &acquire_fence_info, NULL,
		&openxr_mirror_acquire_fence) != VK_SUCCESS)
		goto fail;
	openxr_mirror_ready = true;
	return true;
fail:
	GL_DestroyMirrorResources ();
	Con_Printf ("OpenXR mirror snapshot resources unavailable; VR remains active.\n");
	return false;
}

/*
===============
GL_CreateMainFrameBuffers
===============
*/
static void GL_DestroyXRImageViews (void)
{
	for (uint32_t i = 0; i < openxr_image_count; ++i)
		if (openxr_density_image_views && openxr_density_image_views[i])
			vkDestroyImageView (vulkan_globals.device, openxr_density_image_views[i], NULL);
	free (openxr_density_image_views);
	openxr_density_image_views = NULL;
	for (uint32_t i = 0; i < openxr_image_count; ++i)
		vkDestroyImageView (vulkan_globals.device, openxr_image_views[i], NULL);
	free (openxr_image_views);
	openxr_image_views = NULL;
	openxr_image_count = 0;
}

static void GL_CreateXRImageViews (void)
{
	if (openxr_image_views)
		return;
	openxr_image_count = VRXR_VulkanImageCount (0);
	if (!openxr_image_count)
		Sys_Error ("OpenXR has no stereo swapchain images");
	openxr_image_views = calloc (openxr_image_count, sizeof (*openxr_image_views));
	if (!openxr_image_views)
	{
		openxr_image_count = 0;
		Sys_Error ("Couldn't allocate OpenXR image views");
	}
	qboolean density_view_failed = false;
	for (uint32_t i = 0; i < openxr_image_count; ++i)
	{
		vrxr_vulkan_eye_t image;
		if (!VRXR_GetVulkanImage (0, i, &image) || image.array_layers != 2 || image.array_layer != 0)
			Sys_Error ("Invalid OpenXR stereo image metadata");
		const VkImageViewCreateInfo info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = image.image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
			.format = image.format,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2},
		};
		const VkResult result = vkCreateImageView (vulkan_globals.device, &info, NULL, &openxr_image_views[i]);
		if (result != VK_SUCCESS)
			Sys_Error ("Couldn't create OpenXR image view: %d", result);

		if (i == 0 && image.density_image)
			openxr_density_image_views = calloc (openxr_image_count, sizeof (*openxr_density_image_views));
		if (image.density_image && !openxr_density_image_views)
			density_view_failed = true;
		if (openxr_density_image_views && !density_view_failed)
		{
			const VkExtent2D max_texel = vulkan_globals.openxr_fragment_density_map_max_texel_size;
			const uint32_t minimum_width = max_texel.width ?
				(unsigned)vid.width / max_texel.width + ((unsigned)vid.width % max_texel.width != 0) : UINT32_MAX;
			const uint32_t minimum_height = max_texel.height ?
				(unsigned)vid.height / max_texel.height + ((unsigned)vid.height % max_texel.height != 0) : UINT32_MAX;
			if (!image.density_image || image.density_width < minimum_width || image.density_height < minimum_height)
			{
				density_view_failed = true;
				continue;
			}
			// XR_FB_foveation_vulkan returns a borrowed image and extent, but
			// no format. RG8 is the Vulkan-required FDM format and the format
			// used by existing XR integrations. A rejected view disables only
			// the optional density-map path, never ordinary stereo output.
			const VkImageViewCreateInfo density_info = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = image.density_image,
				.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
				.format = VK_FORMAT_R8G8_UNORM,
				.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2},
			};
			VkImageView created = VK_NULL_HANDLE;
			if (vkCreateImageView (vulkan_globals.device, &density_info, NULL, &created) != VK_SUCCESS)
				density_view_failed = true;
			else
				openxr_density_image_views[i] = created;
		}
	}
	if (density_view_failed)
	{
		for (uint32_t i = 0; i < openxr_image_count; ++i)
			if (openxr_density_image_views && openxr_density_image_views[i])
				vkDestroyImageView (vulkan_globals.device, openxr_density_image_views[i], NULL);
		free (openxr_density_image_views);
		openxr_density_image_views = NULL;
		Con_Printf ("OpenXR borrowed density-map views unavailable; rendering at full rate.\n");
	}
}

static bool GL_CreateFrameBuffers (void)
{
	const render_framebuffer_images_t images = {
		.width = vid.width,
		.height = vid.height,
		.render_width = vid.render_width,
		.render_height = vid.render_height,
		.ui_color = ui_color_buffer_view ? ui_color_buffer_view : color_buffers_view[0],
		.color = {color_buffers_view[0], color_buffers_view[1]},
		.depth = depth_buffer_view,
		.msaa_color = msaa_color_buffer_view,
		.oit_accum = oit_accum_buffer_view,
		.oit_reveal = oit_reveal_buffer_view,
		.mboit_b0 = mboit_b0_buffer_view,
		.mboit_moments = mboit_moments0_buffer_view,
		.mboit_color = mboit_color_buffer_view,
		.fragment_shading_rate = fragment_shading_rate_image_view,
		.density_map_count = openxr_density_image_views ? openxr_image_count : 0,
		.density_maps = openxr_density_image_views,
		.swapchain_count = vulkan_globals.stereo_active ? openxr_image_count : num_swap_chain_images,
		.swapchain = vulkan_globals.stereo_active ? openxr_image_views : swapchain_images_views,
	};
	return R_CreateFrameBuffers (&images);
}

/*
===============
GL_CreateRenderResources
===============
*/
static void VID_GetRenderSize (int *width, int *height)
{
	*width = vid.width;
	*height = vid.height;
	if (r_width.value > 0 && r_height.value > 0)
	{
		*width = (int)CLAMP (q_min (320, vid.width), r_width.value, vid.width);
		*height = (int)CLAMP (q_min (200, vid.height), r_height.value, vid.height);
	}
}

static qboolean GL_DensityFoveationRequestedActive (int render_width, int render_height)
{
	if (!vulkan_globals.stereo_active || !vulkan_globals.openxr_fragment_density_map_enabled ||
		openxr_density_backend_failed || !openxr_density_image_views ||
		render_width != vid.width || render_height != vid.height || vid_fsaa.value >= 2)
		return false;

	const int mode = VRF_RequestedMode (vr_foveation.value);
	if (vulkan_globals.openxr_fragment_density_offset_enabled)
	{
#if defined(VK_QCOM_fragment_density_map_offset)
		// Both scene attachments carry the offset bit when this device feature
		// is enabled, including during an explicitly requested fixed profile.
		if (!GL_DensityOffsetSceneFormatSupported (vulkan_globals.color_format,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, render_width, render_height) ||
			!GL_DensityOffsetSceneFormatSupported (vulkan_globals.depth_format,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				render_width, render_height))
			return false;
#else
		return false;
#endif
	}
	if (mode == VRF_MODE_FIXED)
		return VRXR_VulkanFoveationFixedAvailable ();
	if (mode != VRF_MODE_EYE_TRACKED || !VRF_EyeTrackingEnabled (vr_eye_tracking.value) ||
		!VRXR_VulkanFoveationEyeAvailable ())
		return false;

	return true;
}

static void GL_CreateRenderResources (void)
{
	if (sv.active && cls.signon < 1) // server has loaded the map but client hasn't called R_NewMap yet - wait until next frame
		return;

	if (!vulkan_globals.stereo_active && !GL_CreateSwapChain (false))
	{
		render_resources_created = false;
		return;
	}
	if (vulkan_globals.stereo_active && VRXR_VulkanTransferSourceAvailable () &&
		!GL_CreateSwapChain (true))
		Con_Printf ("OpenXR mirror is disabled for this desktop swapchain.\n");

	if (vulkan_globals.stereo_active)
	{
		GL_CreateXRImageViews ();
		GL_CreateMirrorResources ();
	}
	VID_GetRenderSize (&vid.render_width, &vid.render_height);
	vulkan_globals.openxr_fragment_density_map_active =
		GL_DensityFoveationRequestedActive (vid.render_width, vid.render_height);
	GL_CreateColorBuffer ();
	if (vid.render_width != vid.width || vid.render_height != vid.height)
		GL_CreateUIColorBuffer ();
	GL_CreateDepthBuffer ();
	if (R_SSAOEnabled ())
		R_CreateSSAO (depth_buffer);
	if (vulkan_globals.openxr_fragment_shading_rate_active && !GL_CreateFragmentShadingRateImage ())
		vulkan_globals.openxr_fragment_shading_rate_active = false;
	R_SetupRenderPasses ();
	if (!R_CreateRenderPasses ())
	{
		R_DestroyRenderPasses ();
		vulkan_globals.openxr_fragment_density_map_active = false;
		openxr_density_backend_failed = true;
		Con_Printf ("OpenXR density render passes unavailable; continuing with full-rate stereo.\n");
		R_SetupRenderPasses ();
		if (!R_CreateRenderPasses ())
			Sys_Error ("Couldn't create full-rate render passes");
	}
	if (!GL_CreateFrameBuffers ())
	{
		// A runtime-provided map is optional. Retire partially created
		// framebuffers before replacing only the render-pass topology.
		R_DestroyFrameBuffers ();
		R_DestroyRenderPasses ();
		vulkan_globals.openxr_fragment_density_map_active = false;
		openxr_density_backend_failed = true;
		Con_Printf ("OpenXR density framebuffers unavailable; continuing with full-rate stereo.\n");
		R_SetupRenderPasses ();
		if (!R_CreateRenderPasses ())
			Sys_Error ("Couldn't create full-rate render passes");
		if (!GL_CreateFrameBuffers ())
			Sys_Error ("Couldn't create full-rate framebuffers");
	}
	R_CreatePipelines ();

	render_resources_created = true;

	GL_UpdateDescriptorSets ();
}

/*
===============
GL_DestroyMainRenderPasses
===============
*/

/*
===============
GL_DestroyRenderResources
===============
*/
static void GL_DestroyRenderResources (void)
{
	render_resources_created = false;

	GL_WaitForDeviceIdle ();
	R_VRIKRenderShutdown ();

	R_DestroyPipelines ();

	if (postprocess_descriptor_set != VK_NULL_HANDLE)
	{
		R_FreeDescriptorSet (postprocess_descriptor_set, &vulkan_globals.input_attachment_set_layout);
		postprocess_descriptor_set = VK_NULL_HANDLE;
	}
	if (wboit_resolve_descriptor_set != VK_NULL_HANDLE)
	{
		R_FreeDescriptorSet (wboit_resolve_descriptor_set, &vulkan_globals.oit_input_attachment_set_layout);
		wboit_resolve_descriptor_set = VK_NULL_HANDLE;
	}
	if (vulkan_globals.mboit_input_attachment_descriptor_set != VK_NULL_HANDLE)
	{
		R_FreeDescriptorSet (vulkan_globals.mboit_input_attachment_descriptor_set, &vulkan_globals.mboit_input_attachment_set_layout);
		vulkan_globals.mboit_input_attachment_descriptor_set = VK_NULL_HANDLE;
	}

	if (vulkan_globals.screen_effects_desc_set != VK_NULL_HANDLE)
	{
		R_FreeDescriptorSet (vulkan_globals.screen_effects_desc_set, &vulkan_globals.screen_effects_set_layout);
		vulkan_globals.screen_effects_desc_set = VK_NULL_HANDLE;
	}

	R_DestroyFrameBuffers ();
	GL_DestroyMirrorResources ();
	GL_DestroyXRImageViews ();
	GL_DestroyFragmentShadingRateImage ();

	if (scene_upscale_descriptor_set != VK_NULL_HANDLE)
	{
		R_FreeDescriptorSet (scene_upscale_descriptor_set, &vulkan_globals.single_texture_set_layout);
		scene_upscale_descriptor_set = VK_NULL_HANDLE;
	}
	if (ui_color_buffer)
	{
		vkDestroyImageView (vulkan_globals.device, ui_color_buffer_view, NULL);
		vkDestroyImage (vulkan_globals.device, ui_color_buffer, NULL);
		R_FreeVulkanMemory (&ui_color_buffer_memory, &num_vulkan_misc_allocations);
		ui_color_buffer_view = VK_NULL_HANDLE;
		ui_color_buffer = VK_NULL_HANDLE;
	}

	if (msaa_color_buffer)
	{
		vkDestroyImageView (vulkan_globals.device, msaa_color_buffer_view, NULL);
		vkDestroyImage (vulkan_globals.device, msaa_color_buffer, NULL);
		R_FreeVulkanMemory (&msaa_color_buffer_memory, &num_vulkan_misc_allocations);

		msaa_color_buffer_view = VK_NULL_HANDLE;
		msaa_color_buffer = VK_NULL_HANDLE;
	}

	GL_DestroyOITBuffers ();

	for (int i = 0; i < NUM_COLOR_BUFFERS; ++i)
	{
		vkDestroyImageView (vulkan_globals.device, color_buffers_view[i], NULL);
		vkDestroyImage (vulkan_globals.device, vulkan_globals.color_buffers[i], NULL);
		R_FreeVulkanMemory (&color_buffers_memory[i], &num_vulkan_misc_allocations);

		color_buffers_view[i] = VK_NULL_HANDLE;
		vulkan_globals.color_buffers[i] = VK_NULL_HANDLE;
	}

	if (vulkan_globals.particle_depth_descriptor_set != VK_NULL_HANDLE)
	{
		R_FreeDescriptorSet (vulkan_globals.particle_depth_descriptor_set, &vulkan_globals.input_attachment_set_layout);
		vulkan_globals.particle_depth_descriptor_set = VK_NULL_HANDLE;
	}
	vkDestroyImageView (vulkan_globals.device, depth_buffer_view, NULL);
	R_DestroySSAO ();
	vkDestroyImage (vulkan_globals.device, depth_buffer, NULL);
	R_FreeVulkanMemory (&depth_buffer_memory, &num_vulkan_misc_allocations);

	depth_buffer_view = VK_NULL_HANDLE;
	depth_buffer = VK_NULL_HANDLE;

	GL_DestroySwapChainResources ();

	R_DestroyRenderPasses ();
}

/*
=================
GL_BeginRenderingTask
=================
*/
static qboolean GL_TimestampElapsedUs (uint64_t start, uint64_t end, uint32_t *elapsed_us)
{
	if (!timestamp_valid_bits || timestamp_valid_bits > 64 || !elapsed_us)
		return false;
	const uint64_t mask = timestamp_valid_bits == 64 ? UINT64_MAX : (UINT64_MAX >> (64 - timestamp_valid_bits));
	start &= mask;
	end &= mask;
	/* A wrap makes this interval ambiguous; omit the sample instead of
	 * reporting an enormous or spuriously small GPU duration. */
	if (end < start)
		return false;
	const double microseconds = (double)(end - start) * (double)vulkan_globals.device_properties.limits.timestampPeriod / 1000.0;
	if (!(microseconds >= 0.0 && microseconds <= UINT32_MAX))
		return false;
	*elapsed_us = (uint32_t)microseconds;
	return true;
}

void GL_BeginRenderingTask (void *unused)
{
	VkResult err;

	// Keep the profiling decision stable until this frame has been submitted.
	frame_timing_enabled = scr_speeds.value != 0;

	// Wait for this slot's previous GPU submission to finish before reusing
	// its command buffers and frame allocations.
	if (frame_submitted[current_cb_index])
	{
		const double wait_start = frame_timing_enabled ? Sys_DoubleTime () : 0.0;
		err = vkWaitForFences (vulkan_globals.device, 1, &command_buffer_fences[current_cb_index], VK_TRUE, UINT64_MAX);
		if (err != VK_SUCCESS)
			Sys_Error ("vkWaitForFences failed with code %i", (int)err);
		if (frame_timing_enabled)
			rs_gpuwaitaccum_us += (uint32_t)((Sys_DoubleTime () - wait_start) * 1000000.0);
	}
	frame_submitted[current_cb_index] = false;
	if (openxr_mirror_ready && openxr_mirror_submitted[current_cb_index])
	{
		const VkResult mirror_status = vkGetFenceStatus (vulkan_globals.device,
			openxr_mirror_fences[current_cb_index]);
		openxr_mirror_slot_available[current_cb_index] = mirror_status == VK_SUCCESS;
		if (mirror_status == VK_SUCCESS)
			openxr_mirror_submitted[current_cb_index] = false;
		else if (mirror_status != VK_NOT_READY)
			openxr_mirror_ready = false;
	}

	err = vkResetFences (vulkan_globals.device, 1, &command_buffer_fences[current_cb_index]);
	if (err != VK_SUCCESS)
		Sys_Error ("vkResetFences failed with code %i", (int)err);

	// Allocate GPU profiling resources only when scr_speeds is first enabled.
	if (frame_timing_enabled && (timestamp_query_pool == VK_NULL_HANDLE) && timestamp_valid_bits &&
		vulkan_globals.device_properties.limits.timestampComputeAndGraphics &&
		(vulkan_globals.device_properties.limits.timestampPeriod > 0.0f))
	{
		ZEROED_STRUCT (VkQueryPoolCreateInfo, query_pool_create_info);
		query_pool_create_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		query_pool_create_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
		query_pool_create_info.queryCount = 4 * DOUBLE_BUFFERED;
		err = vkCreateQueryPool (vulkan_globals.device, &query_pool_create_info, NULL, &timestamp_query_pool);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateQueryPool failed with code %i", (int)err);
	}

	// The fence wait above guarantees this slot's previous timestamps are available.
	rs_gputime_us = 0;
	rs_ssaotime_us = 0;
	rs_ssaotime_valid = false;
	if (frame_timing_enabled && timestamps_written[current_cb_index])
	{
		uint64_t timestamps[2];
		if (vkGetQueryPoolResults (
				vulkan_globals.device, timestamp_query_pool, current_cb_index * 4, 2, sizeof (timestamps), timestamps, sizeof (uint64_t),
				VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
			GL_TimestampElapsedUs (timestamps[0], timestamps[1], &rs_gputime_us);
	}
	if (frame_timing_enabled && ssao_timestamps_written[current_cb_index])
	{
		uint64_t timestamps[2];
		if (vkGetQueryPoolResults (
				vulkan_globals.device, timestamp_query_pool, current_cb_index * 4 + 2, 2, sizeof (timestamps), timestamps, sizeof (uint64_t),
				VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
			rs_ssaotime_valid = GL_TimestampElapsedUs (timestamps[0], timestamps[1], &rs_ssaotime_us);
	}
	timestamps_written[current_cb_index] = false;
	ssao_timestamps_written[current_cb_index] = false;

	R_CollectDynamicBufferGarbage ();
	R_CollectMeshBufferGarbage ();
	R_CollectTLASGarbage ();
	TexMgr_CollectGarbage ();

	for (int pcbx_index = 0; pcbx_index < PCBX_NUM; ++pcbx_index)
	{
		cb_context_t *cbx = &vulkan_globals.primary_cb_contexts[pcbx_index];
		cbx->cb = primary_command_buffers[pcbx_index][current_cb_index];
		cbx->current_canvas = CANVAS_INVALID;
		cbx->ui_panel_active = false;
		cbx->ui_panel_mvp_valid = false;
		memset (&cbx->current_pipeline, 0, sizeof (cbx->current_pipeline));

		ZEROED_STRUCT (VkCommandBufferBeginInfo, command_buffer_begin_info);
		command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		err = vkBeginCommandBuffer (cbx->cb, &command_buffer_begin_info);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBeginCommandBuffer failed with code %i", (int)err);

		R_BeginDebugUtilsLabel (cbx, "Primary CB");
	}

	if (frame_timing_enabled && (timestamp_query_pool != VK_NULL_HANDLE))
	{
		VkCommandBuffer first_cb = vulkan_globals.primary_cb_contexts[0].cb;
		vkCmdResetQueryPool (first_cb, timestamp_query_pool, current_cb_index * 4, 4);
		vkCmdWriteTimestamp (first_cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamp_query_pool, current_cb_index * 4);
	}

	for (int scbx_index = 0; scbx_index < SCBX_NUM; ++scbx_index)
	{
		for (int i = 0; i < R_SecondaryContextCount (scbx_index); ++i)
		{
			cb_context_t *cbx = &vulkan_globals.secondary_cb_contexts[scbx_index][i];
			cbx->cb = secondary_command_buffers[scbx_index][current_cb_index][i];
			cbx->current_canvas = CANVAS_INVALID;
			cbx->ui_panel_active = false;
			cbx->ui_panel_mvp_valid = false;
			cbx->depth_only = false;
			cbx->hidden_area_masked_world = false;
			memset (&cbx->current_pipeline, 0, sizeof (cbx->current_pipeline));

			{
				const main_render_pass_stencil_t main_render_pass_stencil = Sky_NeedStencil () ? MAIN_RENDER_PASS_STENCIL_CLEAR : MAIN_RENDER_PASS_NO_STENCIL;
				const main_render_pass_variant_t main_pass_variant = R_UseMBOIT ()	 ? MAIN_RENDER_PASS_MBOIT
																	 : R_UseWBOIT () ? MAIN_RENDER_PASS_OIT
																					 : MAIN_RENDER_PASS_STANDARD;
				R_ConfigureRenderContext (cbx, scbx_index, main_pass_variant, main_render_pass_stencil);
			}

			ZEROED_STRUCT (VkCommandBufferInheritanceInfo, inheritance_info);
			inheritance_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
			inheritance_info.renderPass = cbx->render_pass;
			inheritance_info.subpass = cbx->subpass;

			ZEROED_STRUCT (VkCommandBufferBeginInfo, command_buffer_begin_info);
			command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
			command_buffer_begin_info.pInheritanceInfo = &inheritance_info;

			err = vkBeginCommandBuffer (cbx->cb, &command_buffer_begin_info);
			if (err != VK_SUCCESS)
				Sys_Error ("vkBeginCommandBuffer failed with code %i", (int)err);

			R_BeginDebugUtilsLabel (cbx, va ("CBX %d", scbx_index));

			const qboolean ui = cbx->subpass_type == SUBPASS_UI || cbx->subpass_type == SUBPASS_POST_PROCESS;
			VkRect2D	   render_area;
			render_area.offset.x = 0;
			render_area.offset.y = 0;
			render_area.extent.width = ui ? vid.width : vid.render_width;
			render_area.extent.height = ui ? vid.height : vid.render_height;
			vkCmdSetScissor (cbx->cb, 0, 1, &render_area);

			VkViewport viewport;
			viewport.x = 0;
			viewport.y = 0;
			viewport.width = render_area.extent.width;
			viewport.height = render_area.extent.height;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport (cbx->cb, 0, 1, &viewport);

			if (scbx_index != SCBX_OIT_RESOLVE && !(scbx_index == SCBX_FTE_PARTICLES_BLEND && R_UseOIT ()) && R_HasGraphicsPipeline (cbx, PIPELINE_BASIC_BLEND))
			{
				R_BindGraphicsPipeline (cbx, PIPELINE_BASIC_BLEND);
				GL_SetCanvas (cbx, CANVAS_NONE);
			}
		}
	}

	R_SwapDynamicBuffers ();
	/* The world record tasks need this vertex stream before they start. The
	 * frame's dynamic mapping has just been reset and is flushed at submit. */
	hidden_area_draws[current_cb_index] = GL_PrepareHiddenAreaMesh ();
}

void GL_PrepareVRIKRenderTask (void *unused)
{
	const qboolean profile = scr_speeds.value == 3;
	const double start = profile ? Sys_DoubleTime () : 0.0;
	R_VRIKRenderPrepareFrame (current_cb_index);
	if (profile)
	{
		const double us = (Sys_DoubleTime () - start) * 1000000.0;
		rs_avatarprep_us = us >= 0.0 && us <= UINT32_MAX ? (uint32_t)us : 0;
	}
}

/*
=================
GL_SynchronizeEndRenderingTask
=================
*/
void GL_SynchronizeEndRenderingTask (void)
{
	// Wait until the CPU has submitted the frame. The GPU may still be drawing it.
	if (prev_end_rendering_task != INVALID_TASK_HANDLE)
	{
		Task_Join (prev_end_rendering_task, TASK_TIMEOUT_INFINITE);
		prev_end_rendering_task = INVALID_TASK_HANDLE;
	}
}

/*
=================
GL_BeginRendering
=================
*/
static oit_mode_t GL_FrameOITModeForCvarValue (int r_oit_value)
{
	if (r_oit_value == 1)
		return OIT_MODE_WBOIT;
	if (r_oit_value >= 2)
		return OIT_MODE_MBOIT;
	return OIT_MODE_NONE;
}

void GL_DrawSceneUpscale (cb_context_t *cbx)
{
	if (!ui_color_buffer)
		return;

	R_BeginDebugUtilsLabel (cbx, "Scene Upscale");
	const VkViewport viewport = {0, 0, vid.width, vid.height, 0, 1};
	const struct
	{
		float	 output_size_rcp[2];
		uint32_t upscale_filter;
	} constants = {{1.0f / vid.width, 1.0f / vid.height}, r_upscalefilter.value >= 1 ? 1 : 0};
	vkCmdSetViewport (cbx->cb, 0, 1, &viewport);
	R_BindGraphicsPipeline (cbx, PIPELINE_SCENE_UPSCALE);
	vkCmdBindDescriptorSets (cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, cbx->current_pipeline.layout.handle, 0, 1, &scene_upscale_descriptor_set, 0, NULL);
	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof (constants), &constants);
	vkCmdDraw (cbx->cb, 3, 1, 0, 0);
	GL_SetCanvas (cbx, CANVAS_NONE); // The next GUI draw must restore its projection constants.
	R_EndDebugUtilsLabel (cbx);
}

void VID_WindowSizeChanged (int width, int height)
{
	if (vulkan_globals.stereo_active)
	{
		// SDL reports the desktop window, including delayed startup events.
		// Runtime swapchain dimensions remain fixed for this XR attachment.
		if (width != openxr_desktop_width || height != openxr_desktop_height)
			vid.restart_next_frame = true;
		openxr_desktop_width = width;
		openxr_desktop_height = height;
		return;
	}
	vid.width = width;
	vid.height = height;
	vid.restart_next_frame = true;
	Cvar_FindVar ("scr_conscale")->callback (NULL);
}

static void GL_OpenXRRetireImages (void *unused)
{
	// This callback runs on the main XR owner, outside the queue lock. Retire
	// donor work and framebuffers before the runtime destroys borrowed images.
	GL_DestroyRenderResources ();
	R_RestoreStereoView ();
	R_InvalidateStereoReference ();
	openxr_frame_submitted = false;
	vulkan_globals.stereo_descriptor_set = VK_NULL_HANDLE;
	if (vulkan_globals.stereo_active)
	{
		vid.width = openxr_desktop_width;
		vid.height = openxr_desktop_height;
		vid.recalc_refdef = true;
	}
	vulkan_globals.stereo_active = false;
	memset (&openxr_frame, 0, sizeof (openxr_frame));
	vulkan_globals.stereo_color_format = VK_FORMAT_UNDEFINED;
}

static void GL_OpenXRAttach (void)
{
	if (!openxr_vulkan_binding || !openxr_session_wanted || openxr_attach_attempted)
		return;
	openxr_attach_attempted = true;
	if (!vulkan_globals.openxr_multiview_available)
	{
		Con_Printf ("OpenXR stereo needs two-view multiview and six descriptor sets; keeping desktop output.\n");
		return;
	}
	GL_SynchronizeEndRenderingTask ();
	GL_DestroyRenderResources ();
	openxr_desktop_width = vid.width;
	openxr_desktop_height = vid.height;
	if (!VRXR_AttachVulkan (vulkan_globals.gfx_queue_family_index, 0,
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 2, GL_OpenXRRetireImages, NULL,
		vulkan_globals.openxr_fragment_density_map_enabled, 0))
	{
		Con_Printf ("OpenXR session attachment failed; keeping desktop output.\n");
		return;
	}
	if (vulkan_globals.openxr_fragment_density_map_enabled && !VRXR_VulkanFoveationEyeAvailable ())
		Con_Printf ("OpenXR META eye profile unavailable after attachment; eye mode will render full rate. Restart without -vk-runtime-foveation for the KHR path.\n");
	unsigned width, height;
	if (!VRXR_GetViewSize (0, &width, &height) || !width || !height ||
		width > vulkan_globals.device_properties.limits.maxFramebufferWidth ||
		height > vulkan_globals.device_properties.limits.maxFramebufferHeight)
	{
		VRXR_DetachVulkan ();
		Con_Printf ("OpenXR eye dimensions exceed renderer limits; keeping desktop output.\n");
		return;
	}
	vulkan_globals.stereo_active = true;
	vulkan_globals.stereo_color_format = VRXR_VulkanColorFormat ();
	vid.width = width;
	vid.height = height;
	vid.recalc_refdef = true;
	Con_Printf ("OpenXR stereo session attached: %ux%u per eye.\n", width, height);
}

/* The Vulkan device is selected against OpenXR at startup. Toggle only its
 * session here; rediscovery after a lost runtime needs a new device binding. */
static void GL_OpenXREnable_f (void)
{
	const char *value = Cmd_Argv (1);
	if (Cmd_Argc () != 2 || (strcmp (value, "0") && strcmp (value, "1")))
	{
		Con_Printf ("vr_enable 0|1 (currently %d)\n", openxr_session_wanted ? 1 : 0);
		return;
	}
	if (!openxr_vulkan_binding)
	{
		Con_Printf ("OpenXR needs a startup-selected Vulkan binding; restart with -openxr.\n");
		return;
	}
	const qboolean enable = value[0] == '1';
	if (enable && VRXR_StopReason () != VRXR_STOP_NONE)
	{
		Con_Printf ("OpenXR runtime stopped; restart with -openxr to select a fresh system/device.\n");
		return;
	}
	if (enable && vulkan_globals.stereo_active)
	{
		/* Consecutive console commands can cancel a queued disable before
		 * the renderer reaches its next frame boundary. */
		if (openxr_session_change_pending && !openxr_session_wanted)
		{
			openxr_session_wanted = true;
			openxr_session_change_pending = false;
		}
		return;
	}
	if (!enable && !openxr_session_wanted && !vulkan_globals.stereo_active)
		return;
	openxr_session_wanted = enable;
	openxr_session_change_pending = true;
}

const vrxr_frame_t *GL_OpenXRFrame (void)
{
	return vulkan_globals.stereo_active ? &openxr_frame : NULL;
}

void GL_InvalidateXRInput (void)
{
	// Do not disturb render/view metadata owned by in-flight tasks.
	openxr_frame.focused = false;
	memset (openxr_frame.hands, 0, sizeof (openxr_frame.hands));
}

static void GL_SubmitXRMirror (int slot, uint32_t image_index)
{
	VkResult result;
	if (image_index >= num_swap_chain_images)
	{
		vid.restart_next_frame = true;
		return;
	}
	VkCommandBuffer cb = openxr_mirror_commands[slot];
	result = vkResetCommandBuffer (cb, 0);
	if (result != VK_SUCCESS)
		goto mirror_fail;
	const VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	result = vkBeginCommandBuffer (cb, &begin);
	if (result != VK_SUCCESS)
		goto mirror_fail;
	const VkImageMemoryBarrier before = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = swapchain_images[image_index],
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
	};
	vkCmdPipelineBarrier (cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &before);
	const VkImageBlit region = {
		.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
		.srcOffsets = {{0, 0, 0}, {vid.width, vid.height, 1}},
		.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
		.dstOffsets = {{0, 0, 0}, {(int)openxr_mirror_extent.width,
			(int)openxr_mirror_extent.height, 1}},
	};
	vkCmdBlitImage (cb, openxr_mirror_snapshots[slot], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		swapchain_images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &region, VK_FILTER_LINEAR);
	const VkImageMemoryBarrier after = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = 0,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = swapchain_images[image_index],
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
	};
	vkCmdPipelineBarrier (cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &after);
	result = vkEndCommandBuffer (cb);
	if (result != VK_SUCCESS)
		goto mirror_fail;
	result = vkResetFences (vulkan_globals.device, 1, &openxr_mirror_fences[slot]);
	if (result != VK_SUCCESS)
		goto mirror_fail;
	const VkSubmitInfo submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cb,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &draw_complete_semaphores[image_index],
	};
	SDL_LockMutex (vulkan_globals.queue_mutex);
	result = vkQueueSubmit (vulkan_globals.queue, 1, &submit, openxr_mirror_fences[slot]);
	SDL_UnlockMutex (vulkan_globals.queue_mutex);
	if (result != VK_SUCCESS)
		goto mirror_fail;
	openxr_mirror_submitted[slot] = true;
	const VkPresentInfoKHR present = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &draw_complete_semaphores[image_index],
		.swapchainCount = 1,
		.pSwapchains = &vulkan_swapchain,
		.pImageIndices = &image_index,
	};
	SDL_LockMutex (vulkan_globals.queue_mutex);
	result = fpQueuePresentKHR (vulkan_globals.queue, &present);
	SDL_UnlockMutex (vulkan_globals.queue_mutex);
	if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR ||
		result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR)
	{
		--num_images_acquired;
		if (result != VK_SUCCESS || openxr_mirror_acquire_suboptimal)
			vid.restart_next_frame = true;
		if (result == VK_ERROR_SURFACE_LOST_KHR)
			surface_lost = true;
		return;
	}
mirror_fail:
	Con_Printf ("OpenXR mirror presentation failed (%d); VR remains active.\n", result);
	openxr_mirror_ready = false;
	vid.restart_next_frame = true;
}

static void GL_PresentXRMirror (void)
{
	if (!openxr_mirror_ready || !vulkan_swapchain ||
		!openxr_mirror_extent.width || !openxr_mirror_extent.height)
		return;
	/* An acquired WSI image is not necessarily ready yet. Do not enqueue its
	 * wait on the XR graphics queue: poll the acquire fence on later frames. */
	if (openxr_mirror_acquire_pending)
	{
		const VkResult status = vkGetFenceStatus (vulkan_globals.device,
			openxr_mirror_acquire_fence);
		if (status == VK_NOT_READY)
			return;
		if (status != VK_SUCCESS)
		{
			openxr_mirror_ready = false;
			vid.restart_next_frame = true;
			return;
		}
		openxr_mirror_acquire_pending = false;
		GL_SubmitXRMirror (openxr_mirror_acquire_slot, openxr_mirror_acquire_image);
		if (!openxr_mirror_ready)
			return;
	}
	if (!openxr_mirror_copy_ready || num_images_acquired >= num_swap_chain_images - 1)
		return;
	const int slot = openxr_mirror_frame_slot;
	if (!openxr_mirror_slot_available[slot] ||
		vkResetFences (vulkan_globals.device, 1, &openxr_mirror_acquire_fence) != VK_SUCCESS)
		return;
	uint32_t image_index = 0;
	VkResult result = fpAcquireNextImageKHR (vulkan_globals.device, vulkan_swapchain,
		0, VK_NULL_HANDLE, openxr_mirror_acquire_fence, &image_index);
	if (result == VK_NOT_READY || result == VK_TIMEOUT)
		return;
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR)
	{
		vid.restart_next_frame = true;
		if (result == VK_ERROR_SURFACE_LOST_KHR)
			surface_lost = true;
		return;
	}
	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	{
		Con_Printf ("OpenXR mirror acquire failed (%d); disabling mirror until restart.\n", result);
		openxr_mirror_ready = false;
		return;
	}
	++num_images_acquired;
	openxr_mirror_acquire_pending = true;
	openxr_mirror_acquire_image = image_index;
	openxr_mirror_acquire_slot = slot;
	openxr_mirror_acquire_suboptimal = result == VK_SUBOPTIMAL_KHR;
	openxr_mirror_slot_available[slot] = false;
	if (vkGetFenceStatus (vulkan_globals.device, openxr_mirror_acquire_fence) == VK_SUCCESS)
	{
		openxr_mirror_acquire_pending = false;
		GL_SubmitXRMirror (slot, image_index);
	}
}

void GL_EndXRFrame (void)
{
	if (!vulkan_globals.stereo_active)
		return;
	// Recording completion is insufficient: the submission task must have
	// queued ALL application image accesses before releasing either eye.
	GL_SynchronizeEndRenderingTask ();
	if (openxr_frame_submitted)
	{
		VRXR_VulkanEyeSubmitted (0);
		VRXR_VulkanEyeSubmitted (1);
		VRXR_EndFrame ();
		GL_PresentXRMirror ();
	}
	else
		VRXR_AbortFrame ();
	openxr_frame_submitted = false;
	openxr_mirror_copy_ready = false;
}

static qboolean GL_PrepareRuntimeFoveation (void)
{
	openxr_density_eye_active = false;
	vulkan_globals.openxr_fragment_density_frame_active = false;
	memset (openxr_density_offsets, 0, sizeof (openxr_density_offsets));
	if (openxr_density_backend_failed)
		return true;
	if (!openxr_density_image_views)
		return true;

	int mode = !vulkan_globals.openxr_fragment_density_map_active || key_dest == key_menu ?
		VRF_MODE_OFF : VRF_RequestedMode (vr_foveation.value);
	const qboolean allow_eye = mode == VRF_MODE_EYE_TRACKED && VRF_EyeTrackingEnabled (vr_eye_tracking.value) &&
		VRXR_VulkanFoveationEyeAvailable ();
	if (mode == VRF_MODE_EYE_TRACKED && !allow_eye)
		mode = VRF_MODE_OFF;
	float centers[2][2];
	int effective = VRXR_UpdateVulkanFoveation (mode, allow_eye, centers);
	if (effective == VRF_MODE_EYE_TRACKED && vulkan_globals.openxr_fragment_density_offset_enabled)
	{
		const VkExtent2D granularity = vulkan_globals.openxr_fragment_density_offset_granularity;
		qboolean offsets_valid = true;
		for (int eye = 0; eye < 2; ++eye)
			if (!VRF_DensityOffset (centers[eye][0], vid.render_width, granularity.width, &openxr_density_offsets[eye].x) ||
				!VRF_DensityOffset (centers[eye][1], vid.render_height, granularity.height, &openxr_density_offsets[eye].y))
			{
				offsets_valid = false;
				break;
			}
		if (offsets_valid)
			openxr_density_eye_active = true;
		else
			effective = VRXR_UpdateVulkanFoveation (VRF_MODE_OFF, 0, centers);
	}
	if (effective >= 0)
	{
		vulkan_globals.openxr_fragment_density_frame_active = effective != VRF_MODE_OFF;
		return true;
	}
	openxr_density_backend_failed = true;
	vulkan_globals.openxr_fragment_density_map_active = false;
	Con_Printf ("OpenXR runtime foveation could not restore full rate; disabling density passes.\n");
	return false;
}

static void GL_PrepareFragmentShadingRateMap (void)
{
	if (!vulkan_globals.openxr_fragment_shading_rate_active || !fragment_shading_rate_map ||
		fragment_shading_rate_image == VK_NULL_HANDLE)
	{
		VRF_ResetPolicy (&openxr_foveation_policy);
		return;
	}

	const double requested_mode = key_dest == key_menu ? VRF_MODE_OFF : vr_foveation.value;
	int mode = VRF_SelectMode (&openxr_foveation_policy, requested_mode, vr_eye_tracking.value, &openxr_frame);
	if (!VRF_BuildRateMap (
			fragment_shading_rate_map, fragment_shading_rate_map_size, vid.render_width, vid.render_height,
			vulkan_globals.openxr_fragment_shading_rate_texel_size.width, vulkan_globals.openxr_fragment_shading_rate_texel_size.height,
			fragment_shading_rate_image_extent.width, fragment_shading_rate_image_extent.height, fragment_shading_rate_image_layers,
			mode, openxr_frame.views, &openxr_frame.gaze, fragment_shading_rate_2x2_supported, fragment_shading_rate_4x4_supported))
	{
		VRF_ResetPolicy (&openxr_foveation_policy);
		mode = VRF_MODE_OFF;
		VRF_BuildRateMap (
			fragment_shading_rate_map, fragment_shading_rate_map_size, vid.render_width, vid.render_height,
			vulkan_globals.openxr_fragment_shading_rate_texel_size.width, vulkan_globals.openxr_fragment_shading_rate_texel_size.height,
			fragment_shading_rate_image_extent.width, fragment_shading_rate_image_extent.height, fragment_shading_rate_image_layers,
			VRF_MODE_OFF, NULL, NULL, fragment_shading_rate_2x2_supported, fragment_shading_rate_4x4_supported);
	}
}

static void GL_UploadFragmentShadingRateMap (void)
{
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
	if (!vulkan_globals.openxr_fragment_shading_rate_active || fragment_shading_rate_image == VK_NULL_HANDLE ||
		!fragment_shading_rate_map || !fragment_shading_rate_uploaded_map || !fragment_shading_rate_map_size)
		return;
	// The image and its view persist across frames. A repeated map needs no
	// layout transition or transfer; the prior contents remain readable by
	// the next scene pass on the same graphics queue.
	if (fragment_shading_rate_image_initialized &&
		!memcmp (fragment_shading_rate_uploaded_map, fragment_shading_rate_map, fragment_shading_rate_map_size))
		return;

	VkCommandBuffer command_buffer;
	VkBuffer staging_buffer;
	int staging_offset;
	byte *staging_memory = R_StagingAllocate ((int)fragment_shading_rate_map_size, 1, &command_buffer, &staging_buffer, &staging_offset);
	const VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, fragment_shading_rate_image_layers};
	const VkImageMemoryBarrier before_copy = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = fragment_shading_rate_image_initialized ? VK_ACCESS_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR : 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = fragment_shading_rate_image_initialized ? VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR : VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = fragment_shading_rate_image,
		.subresourceRange = range,
	};
	const VkPipelineStageFlags before_stage = fragment_shading_rate_image_initialized
		? VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	vkCmdPipelineBarrier (command_buffer, before_stage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &before_copy);

	const VkBufferImageCopy copy = {
		.bufferOffset = (VkDeviceSize)staging_offset,
		.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, fragment_shading_rate_image_layers},
		.imageExtent = {fragment_shading_rate_image_extent.width, fragment_shading_rate_image_extent.height, 1},
	};
	vkCmdCopyBufferToImage (command_buffer, staging_buffer, fragment_shading_rate_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	const VkImageMemoryBarrier after_copy = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = fragment_shading_rate_image,
		.subresourceRange = range,
	};
	vkCmdPipelineBarrier (
		command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR,
		0, 0, NULL, 0, NULL, 1, &after_copy);

	R_StagingBeginCopy ();
	memcpy (staging_memory, fragment_shading_rate_map, fragment_shading_rate_map_size);
	R_StagingEndCopy ();
	R_SubmitStagingBuffers ();
	memcpy (fragment_shading_rate_uploaded_map, fragment_shading_rate_map, fragment_shading_rate_map_size);
	fragment_shading_rate_image_initialized = true;
#endif
}

qboolean GL_BeginRendering (qboolean use_tasks, task_handle_t *begin_rendering_task, int *width, int *height)
{
	if (openxr_session_change_pending)
	{
		GL_SynchronizeEndRenderingTask ();
		if (!openxr_session_wanted)
		{
			/* Release VR-owned keys and pending motion before the session's
			 * borrowed images and action spaces are retired. */
			VR_InputCommands (NULL);
			VRXR_DetachVulkan ();
		}
		openxr_attach_attempted = false;
		openxr_session_change_pending = false;
	}
	GL_OpenXRAttach ();
	if (!use_tasks || vulkan_globals.stereo_active)
		GL_SynchronizeEndRenderingTask ();

	const int		 requested_oit_value = (int)r_oit.value;
	const oit_mode_t requested_oit_mode = GL_FrameOITModeForCvarValue (requested_oit_value);
	const qboolean	 oit_mode_changed = (requested_oit_mode != frame_oit_mode);
	frame_oit_mode = requested_oit_mode;

	int render_width, render_height;
	VID_GetRenderSize (&render_width, &render_height);
	const qboolean render_size_changed = render_width != vid.render_width || render_height != vid.render_height;
	const qboolean foveation_active = GL_FoveationRequestedActive (render_width, render_height);
	const qboolean foveation_active_changed = foveation_active != vulkan_globals.openxr_fragment_shading_rate_active;
	vulkan_globals.openxr_fragment_shading_rate_active = foveation_active;
	const qboolean density_active = GL_DensityFoveationRequestedActive (render_width, render_height);
	const qboolean density_active_changed = render_resources_created &&
		density_active != vulkan_globals.openxr_fragment_density_map_active;
	if (render_resources_created)
		vulkan_globals.openxr_fragment_density_map_active = density_active;
	const qboolean render_pass_setup_changed = R_SetupRenderPasses ();
	if (vid.restart_next_frame || foveation_active_changed || density_active_changed ||
		(render_resources_created && (oit_mode_changed || render_pass_setup_changed || render_size_changed)))
	{
		VID_Restart (false);
		vid.restart_next_frame = false;
		frame_oit_mode = GL_FrameOITModeForCvarValue (requested_oit_value);
	}

	if (!render_resources_created)
	{
		GL_CreateRenderResources ();

		if (!render_resources_created)
		{
			GL_InvalidateXRInput ();
			return false;
		}
	}

	if (vulkan_globals.stereo_active)
	{
		// Clean up an abandoned serial refresh before reusing command buffers.
		VRXR_AbortFrame ();
		const int requested_mode = VRF_RequestedMode (vr_foveation.value);
		const int gaze_enabled = key_dest != key_menu && vulkan_globals.openxr_fragment_shading_rate_active && requested_mode == VRF_MODE_EYE_TRACKED &&
			VRF_EyeTrackingEnabled (vr_eye_tracking.value) && VRXR_GazeSupported ();
		VRXR_SetGazeEnabled (gaze_enabled);
		const int begun = VRXR_BeginFrame (&openxr_frame);
		if (openxr_frame.reference_changed)
			R_InvalidateStereoReference ();
		if (begun <= 0)
		{
			// A failure after action sync can leave a partially populated
			// frame. Never replay its held buttons on the next host iteration.
			memset (&openxr_frame, 0, sizeof (openxr_frame));
			VRF_ResetPolicy (&openxr_foveation_policy);
			return false;
		}
		if (!openxr_frame.should_render)
		{
			VRF_ResetPolicy (&openxr_foveation_policy);
			VRXR_EndFrame ();
			return false;
		}
		if (!VRXR_StereoClip (&openxr_frame, V_VRUnitsPerMetre (), 4.f, vulkan_globals.stereo_clip_from_center))
		{
			VRF_ResetPolicy (&openxr_foveation_policy);
			VRXR_AbortFrame ();
			return false;
		}
		vrxr_vulkan_eye_t image;
		if (!VRXR_GetVulkanEye (0, &image) || image.index >= openxr_image_count)
		{
			VRF_ResetPolicy (&openxr_foveation_policy);
			VRXR_AbortFrame ();
			return false;
		}
		openxr_image_index = image.index;
		openxr_frame_submitted = false;
		vulkan_globals.stereo_descriptor_set = VK_NULL_HANDLE;
		if (!GL_PrepareRuntimeFoveation ())
		{
			VRXR_AbortFrame ();
			vid.restart_next_frame = true;
			return false;
		}
		GL_PrepareFragmentShadingRateMap ();
	}
	*width = vid.width;
	*height = vid.height;

	// Submit the full map before a recording task can begin. Same-queue
	// barriers order this rewrite after last frame's FSR reads and before this
	// frame's scene pass.
	GL_UploadFragmentShadingRateMap ();
	if (use_tasks)
		*begin_rendering_task = Task_AllocateAndAssignFunc (GL_BeginRenderingTask, NULL, 0);
	else
		GL_BeginRenderingTask (NULL);

	return true;
}

/*
=================
GL_AcquireNextSwapChainImage
=================
*/
qboolean GL_AcquireNextSwapChainImage (void)
{
	if (num_images_acquired >= (num_swap_chain_images - 1))
	{
		return false;
	}

#if defined(VK_EXT_full_screen_exclusive)
	if (VID_GetFullscreen () && vulkan_globals.want_full_screen_exclusive && vulkan_globals.swap_chain_full_screen_exclusive &&
		!vulkan_globals.swap_chain_full_screen_acquired)
	{
		const VkResult result = fpAcquireFullScreenExclusiveModeEXT (vulkan_globals.device, vulkan_swapchain);
		if (result == VK_SUCCESS)
		{
			vulkan_globals.swap_chain_full_screen_acquired = true;
			Sys_Printf ("Full screen exclusive acquired\n");
		}
	}
	else if (!vulkan_globals.want_full_screen_exclusive && vulkan_globals.swap_chain_full_screen_exclusive && vulkan_globals.swap_chain_full_screen_acquired)
	{
		const VkResult result = fpReleaseFullScreenExclusiveModeEXT (vulkan_globals.device, vulkan_swapchain);
		if (result == VK_SUCCESS)
		{
			vulkan_globals.swap_chain_full_screen_acquired = false;
			Sys_Printf ("Full screen exclusive released\n");
		}
	}
#endif

	VkResult err = fpAcquireNextImageKHR (
		vulkan_globals.device, vulkan_swapchain, UINT64_MAX, image_aquired_semaphores[current_cb_index], VK_NULL_HANDLE, &current_swapchain_buffer);
#if defined(VK_EXT_full_screen_exclusive)
	if ((err == VK_ERROR_OUT_OF_DATE_KHR) || (err == VK_ERROR_SURFACE_LOST_KHR) || (err == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT))
#else
	if ((err == VK_ERROR_OUT_OF_DATE_KHR) || (err == VK_ERROR_SURFACE_LOST_KHR))
#endif
	{
		vid.restart_next_frame = true;
		if (err == VK_ERROR_SURFACE_LOST_KHR)
			surface_lost = true;
		return false;
	}
	else if (err == VK_SUBOPTIMAL_KHR)
	{
		vid.restart_next_frame = true;
	}
	else if (err != VK_SUCCESS)
		Sys_Error ("Couldn't acquire next image with code %i", (int)err);

	num_images_acquired += 1;
	return true;
}

void ScheduleScreenshotCopy (VkCommandBuffer command_buffer, VkBuffer *buffer, vulkan_memory_t *memory)
{
	R_CreateBuffer (
		buffer, memory, glwidth * glheight * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT, NULL,
		NULL, "Screenshot");

	{
		ZEROED_STRUCT (VkImageMemoryBarrier, image_barrier);
		image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		image_barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barrier.image = swapchain_images[current_swapchain_buffer];
		image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_barrier.subresourceRange.baseMipLevel = 0;
		image_barrier.subresourceRange.levelCount = 1;
		image_barrier.subresourceRange.baseArrayLayer = 0;
		image_barrier.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier (
			command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &image_barrier);
	}

	ZEROED_STRUCT (VkBufferImageCopy, image_copy);
	image_copy.bufferOffset = 0;
	image_copy.bufferRowLength = glwidth;
	image_copy.bufferImageHeight = glheight;
	image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_copy.imageSubresource.layerCount = 1;
	image_copy.imageExtent.width = glwidth;
	image_copy.imageExtent.height = glheight;
	image_copy.imageExtent.depth = 1;

	vkCmdCopyImageToBuffer (command_buffer, swapchain_images[current_swapchain_buffer], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, *buffer, 1, &image_copy);

	{
		ZEROED_STRUCT (VkImageMemoryBarrier, image_barrier);
		image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		image_barrier.dstAccessMask = 0;
		image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		image_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barrier.image = swapchain_images[current_swapchain_buffer];
		image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_barrier.subresourceRange.baseMipLevel = 0;
		image_barrier.subresourceRange.levelCount = 1;
		image_barrier.subresourceRange.baseArrayLayer = 0;
		image_barrier.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier (command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &image_barrier);
	}
}

void WriteScreenshot (VkBuffer buffer, vulkan_memory_t memory)
{
	SDL_LockMutex (vulkan_globals.queue_mutex);
	vkDeviceWaitIdle (vulkan_globals.device);
	SDL_UnlockMutex (vulkan_globals.queue_mutex);
	vulkan_globals.device_idle = true;

	void *buffer_ptr;
	vkMapMemory (vulkan_globals.device, memory.handle, 0, glwidth * glheight * 4, 0, &buffer_ptr);

	ZEROED_STRUCT (VkMappedMemoryRange, range);
	range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	range.memory = memory.handle;
	range.size = VK_WHOLE_SIZE;
	vkInvalidateMappedMemoryRanges (vulkan_globals.device, 1, &range);

	qboolean bgra = (vulkan_globals.swap_chain_format == VK_FORMAT_B8G8R8A8_UNORM) || (vulkan_globals.swap_chain_format == VK_FORMAT_B8G8R8A8_SRGB);
	if (bgra)
	{
		byte	 *data = (byte *)buffer_ptr;
		const int size = glwidth * glheight * 4;
		for (int i = 0; i < size; i += 4)
		{
			const byte temp = data[i];
			data[i] = data[i + 2];
			data[i + 2] = temp;
		}
	}

	// with the Steam API active, screenshots go to the Steam library instead (from Ironwail)
	if (Steam_SaveScreenshot (buffer_ptr, glwidth, glheight))
		Con_Printf ("Wrote screenshot to the Steam library\n");
	else
	{
		qboolean ok;
		if (!q_strncasecmp (screenshot_ext, "png", sizeof (screenshot_ext)))
			ok = Image_WritePNG (screenshot_imagename, buffer_ptr, glwidth, glheight, 32, true);
		else if (!q_strncasecmp (screenshot_ext, "tga", sizeof (screenshot_ext)))
			ok = Image_WriteTGA (screenshot_imagename, buffer_ptr, glwidth, glheight, 32, true);
		else if (!q_strncasecmp (screenshot_ext, "jpg", sizeof (screenshot_ext)))
			ok = Image_WriteJPG (screenshot_imagename, buffer_ptr, glwidth, glheight, 32, screenshot_quality, true);
		else
			ok = false;

		if (ok)
		{
			Con_SafePrintf ("Wrote ");
			Con_LinkPrintf (va ("%s/%s", com_gamedir, screenshot_imagename), "%s", screenshot_imagename);
			Con_SafePrintf ("\n");
		}
		else
			Con_Printf ("SCR_ScreenShot_f: Couldn't create %s\n", screenshot_imagename);
	}

	R_FreeBuffer (buffer, &memory, NULL);
}

/*
=================
GL_EndRenderingTask
=================
*/

static void GL_RecordOITResolveContext (end_rendering_parms_t *parms, VkRect2D render_area)
{
	if (!parms->use_oit)
		return;

	cb_context_t *cbx = vulkan_globals.secondary_cb_contexts[SCBX_OIT_RESOLVE];
	vkCmdSetScissor (cbx->cb, 0, 1, &render_area);

	VkViewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)parms->render_width;
	viewport.height = (float)parms->render_height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport (cbx->cb, 0, 1, &viewport);

	if (parms->use_mboit)
	{
		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.mboit_resolve_pipeline);
		vkCmdBindDescriptorSets (
			cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.mboit_resolve_pipeline.layout.handle, 0, 1,
			&vulkan_globals.mboit_input_attachment_descriptor_set, 0, NULL);
	}
	else
	{
		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.wboit_resolve_pipeline);
		vkCmdBindDescriptorSets (
			cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.wboit_resolve_pipeline.layout.handle, 0, 1, &wboit_resolve_descriptor_set, 0, NULL);
	}
	vkCmdDraw (cbx->cb, 3, 1, 0, 0);
}

typedef struct
{
	VkCommandBuffer commands;
	VkBuffer		buffer;
	vulkan_memory_t memory;
} frame_readback_t;

static void GL_RecordFrameReadback (void *data)
{
	frame_readback_t *readback = data;
	ScheduleScreenshotCopy (readback->commands, &readback->buffer, &readback->memory);
}

/* Snapshot both runtime meshes before the dynamic vertex flush. The borrowed
 * backend arrays can change on the next XR visibility-mask event. Missing or
 * malformed data disables the draw for both eyes, preserving the full image. */
static xr_hidden_area_draw_t GL_PrepareHiddenAreaMesh (void)
{
	xr_hidden_area_draw_t draw = {0};
	const float *source[2];
	uint32_t triangles[2];
	if (!vulkan_globals.stereo_active || !vr_hidden_area.value)
		return draw;
	for (int eye = 0; eye < 2; ++eye)
	{
		triangles[eye] = VRXR_GetHiddenAreaMesh (eye, &source[eye]);
		/* Avoid an unbounded per-frame upload from a broken runtime. */
		if (!source[eye] || !triangles[eye] || triangles[eye] > 65536)
			return draw;
	}
	const uint32_t vertex_count = 3 * q_max (triangles[0], triangles[1]);
	float (*vertices)[4] = (float (*)[4])R_VertexAllocate (
		(int)(vertex_count * sizeof (*vertices)), &draw.buffer, &draw.offset);
	if (!vertices)
		return (xr_hidden_area_draw_t){0};
	draw.vertex_count = VRXR_PackHiddenAreaVertices (openxr_frame.views, source,
		triangles, vertices, vertex_count);
	return draw;
}

qboolean GL_OpenXRHiddenAreaWorldEligible (const cb_context_t *cbx)
{
	return cbx && cbx->pipeline_variant >= 0 && cbx->pipeline_variant < MAIN_RENDER_PASS_VARIANT_COUNT &&
		vulkan_globals.stereo_active && openxr_frame.should_render &&
		hidden_area_draws[current_cb_index].vertex_count &&
		vulkan_globals.hidden_area_stencil_pipeline[cbx->pipeline_variant].handle != VK_NULL_HANDLE &&
		vulkan_globals.world_hidden_area_depth_replay_pipeline[cbx->pipeline_variant].handle != VK_NULL_HANDLE &&
		/* A configured density backend can still draw this frame entirely in
		 * the ordinary scene pass (for example, after gaze loss). Only its
		 * coarse-color frame needs the separate mask/stencil policy. */
		!(vulkan_globals.openxr_fragment_density_map_active &&
		  vulkan_globals.openxr_fragment_density_frame_active) &&
		vid.width == vid.render_width && vid.height == vid.render_height &&
		!render_warp && !vid_palettize.value && !(gl_polyblend.value && v_blend[3]) &&
		key_dest != key_menu;
}

void GL_RecordOpenXRHiddenAreaStencil (cb_context_t *cbx)
{
	assert (cbx && cbx->subpass_type == SUBPASS_MAIN &&
		cbx->pipeline_variant >= 0 && cbx->pipeline_variant < MAIN_RENDER_PASS_VARIANT_COUNT);
	const xr_hidden_area_draw_t *mesh = &hidden_area_draws[current_cb_index];
	const VkClearAttachment clear = {
		.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
		.clearValue.depthStencil.stencil = 0,
	};
	/* Multiview broadcasts this single layer rect to both eye layers. */
	const VkClearRect rect = {{{0, 0}, {vid.render_width, vid.render_height}}, 0, 1};
	vkCmdClearAttachments (cbx->cb, 1, &clear, 1, &rect);
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vulkan_globals.hidden_area_stencil_pipeline[cbx->pipeline_variant]);
	vkCmdBindVertexBuffers (cbx->cb, 0, 1, &mesh->buffer, &mesh->offset);
	vkCmdDraw (cbx->cb, mesh->vertex_count, 1, 0, 0);
}

static qboolean GL_RecordXRMirrorSnapshot (VkCommandBuffer cb, int slot)
{
	if (!openxr_mirror_ready || !openxr_mirror_slot_available[slot] ||
		!vulkan_globals.stereo_active || !openxr_frame.should_render)
		return false;
	const float setting = vr_mirror.value;
	const int mode = !isfinite (setting) || setting < 1.0f ? 0 : setting >= 2.0f ? 2 : 1;
	if (!mode)
		return false;
	vrxr_vulkan_eye_t eye;
	if (!VRXR_GetVulkanEye (mode - 1, &eye) || !eye.image ||
		eye.width != (uint32_t)vid.width || eye.height != (uint32_t)vid.height)
		return false;
	VkImageMemoryBarrier before[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = eye.image,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, eye.array_layer, 1},
		},
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = openxr_mirror_snapshot_initialized[slot] ? VK_ACCESS_TRANSFER_READ_BIT : 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = openxr_mirror_snapshot_initialized[slot] ?
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = openxr_mirror_snapshots[slot],
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
		},
	};
	vkCmdPipelineBarrier (cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
		0, NULL, 0, NULL, countof (before), before);
	const VkImageCopy region = {
		.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, eye.array_layer, 1},
		.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
		.extent = {(uint32_t)vid.width, (uint32_t)vid.height, 1},
	};
	vkCmdCopyImage (cb, eye.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		openxr_mirror_snapshots[slot], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	VkImageMemoryBarrier after[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.dstAccessMask = 0,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = eye.image,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, eye.array_layer, 1},
		},
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = openxr_mirror_snapshots[slot],
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
		},
	};
	vkCmdPipelineBarrier (cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0, 0, NULL, 0, NULL, countof (after), after);
	openxr_mirror_snapshot_initialized[slot] = true;
	return true;
}

static void GL_EndRenderingTask (end_rendering_parms_t *parms)
{
	R_SubmitStagingBuffers ();
	R_FlushDynamicBuffers ();

	VkResult err;
	int		 cb_index = current_cb_index;
	const xr_hidden_area_draw_t hidden_area = hidden_area_draws[cb_index];

	VkRect2D render_area;
	render_area.offset.x = 0;
	render_area.offset.y = 0;
	render_area.extent.width = parms->render_width;
	render_area.extent.height = parms->render_height;

	const double display_wait_start = frame_timing_enabled ? Sys_DoubleTime () : 0.0;
#if defined(VK_KHR_present_wait2)
	// cap the number of frames queued for display: DXGI layered swapchains force 3+ images, so
	// under FIFO the acquire alone lets the CPU run several vblanks ahead of scan out
	const uint64_t max_frame_latency = (uint64_t)CLAMP (1, (int)vid_maxframelatency.value, 8);
	if (swapchain_present_wait && parms->swapchain && (vid_maxframelatency.value > 0) && (vid_vsync.value > 0) && (current_present_id + 1 > max_frame_latency))
	{
		ZEROED_STRUCT (VkPresentWait2InfoKHR, present_wait_2_info);
		present_wait_2_info.sType = VK_STRUCTURE_TYPE_PRESENT_WAIT_2_INFO_KHR;
		present_wait_2_info.presentId = current_present_id + 1 - max_frame_latency;
		present_wait_2_info.timeout = 50ull * 1000ull * 1000ull;
		fpWaitForPresent2KHR (vulkan_globals.device, vulkan_swapchain, &present_wait_2_info);
	}
#endif

	qboolean swapchain_acquired = !vulkan_globals.stereo_active && parms->swapchain && GL_AcquireNextSwapChainImage ();
	const qboolean output_acquired = swapchain_acquired || vulkan_globals.stereo_active;
	if (frame_timing_enabled)
		rs_gpuwaitaccum_us += (uint32_t)((Sys_DoubleTime () - display_wait_start) * 1000000.0);
	if (output_acquired)
	{
		cb_context_t *cbx = vulkan_globals.secondary_cb_contexts[SCBX_POST_PROCESS];

		// Render post process
		GL_Viewport (cbx, 0, 0, vid.width, vid.height, 0.0f, 1.0f);
		float postprocess_values[2] = {vid_gamma.value, q_min (2.0f, q_max (1.0f, vid_contrast.value))};

		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.postprocess_pipeline);
		vkCmdBindDescriptorSets (
			cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.postprocess_pipeline.layout.handle, 0, 1, &postprocess_descriptor_set, 0, NULL);
		R_PushConstants (cbx, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 2 * sizeof (float), postprocess_values);
		vkCmdDraw (cbx->cb, 3, 1, 0, 0);
		if (hidden_area.vertex_count)
		{
			R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.hidden_area_black_pipeline);
			vkCmdBindVertexBuffers (cbx->cb, 0, 1, &hidden_area.buffer, &hidden_area.offset);
			vkCmdDraw (cbx->cb, hidden_area.vertex_count, 1, 0, 0);
		}
	}

	GL_RecordOITResolveContext (parms, render_area);

	for (int scbx_index = 0; scbx_index < SCBX_NUM; ++scbx_index)
	{
		for (int i = 0; i < R_SecondaryContextCount (scbx_index); ++i)
		{
			cb_context_t *cbx = &vulkan_globals.secondary_cb_contexts[scbx_index][i];
			R_EndDebugUtilsLabel (cbx);
			err = vkEndCommandBuffer (cbx->cb);
			if (err != VK_SUCCESS)
				Sys_Error ("vkEndCommandBuffer failed with code %i", (int)err);
		}
	}

	VkCommandBuffer render_passes_cb = vulkan_globals.primary_cb_contexts[PCBX_RENDER_PASSES].cb;

	frame_readback_t readback = {.commands = render_passes_cb};
	VkCommandBuffer	 submit_cbs[PCBX_NUM];
	bool ssao_written = false;
	const uint32_t	 submit_count =
		R_RecordFrame (parms, output_acquired, vulkan_globals.stereo_active ? openxr_image_index : current_swapchain_buffer,
			submit_cbs, countof (submit_cbs), take_screenshot && swapchain_acquired ? GL_RecordFrameReadback : NULL, &readback,
			frame_timing_enabled ? timestamp_query_pool : VK_NULL_HANDLE, cb_index * 4 + 2, &ssao_written);
	ssao_timestamps_written[cb_index] = ssao_written;
	openxr_mirror_copy_ready = GL_RecordXRMirrorSnapshot (render_passes_cb, cb_index);
	if (openxr_mirror_copy_ready)
		openxr_mirror_frame_slot = cb_index;

	if (frame_timing_enabled && (timestamp_query_pool != VK_NULL_HANDLE))
	{
		vkCmdWriteTimestamp (render_passes_cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_query_pool, (cb_index * 4) + 1);
		timestamps_written[cb_index] = true;
	}

	{
		for (int pcbx_index = 0; pcbx_index < PCBX_NUM; ++pcbx_index)
		{
			R_EndDebugUtilsLabel (&vulkan_globals.primary_cb_contexts[pcbx_index]);
			err = vkEndCommandBuffer (vulkan_globals.primary_cb_contexts[pcbx_index].cb);
			if (err != VK_SUCCESS)
				Sys_Error ("vkEndCommandBuffer failed with code %i", (int)err);
		}

		ZEROED_STRUCT (VkSubmitInfo, submit_info);
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = submit_count;
		submit_info.pCommandBuffers = submit_cbs;
		submit_info.waitSemaphoreCount = swapchain_acquired ? 1 : 0;
		submit_info.pWaitSemaphores = &image_aquired_semaphores[cb_index];
		submit_info.signalSemaphoreCount = swapchain_acquired ? 1 : 0;
		submit_info.pSignalSemaphores = swapchain_acquired ? &draw_complete_semaphores[current_swapchain_buffer] : NULL;
		VkPipelineStageFlags wait_dst_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		submit_info.pWaitDstStageMask = &wait_dst_stage_mask;

		SDL_LockMutex (vulkan_globals.queue_mutex);
		err = vkQueueSubmit (vulkan_globals.queue, 1, &submit_info, command_buffer_fences[cb_index]);
		SDL_UnlockMutex (vulkan_globals.queue_mutex);
		if (err != VK_SUCCESS)
			Sys_Error ("vkQueueSubmit failed with code %i", (int)err);
	}

	vulkan_globals.device_idle = false;
	frame_submitted[cb_index] = true;
	openxr_frame_submitted = vulkan_globals.stereo_active;

	if (take_screenshot && (readback.buffer != VK_NULL_HANDLE))
	{
		WriteScreenshot (readback.buffer, readback.memory);
	}
	// A failed acquire has no readable presentation image. Keep the request for
	// the next acquired frame, including across a swapchain restart.
	if (swapchain_acquired)
		take_screenshot = false;

	if (swapchain_acquired == true)
	{
		ZEROED_STRUCT (VkPresentInfoKHR, present_info);
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.swapchainCount = 1;
		present_info.pSwapchains = &vulkan_swapchain, present_info.pImageIndices = &current_swapchain_buffer;
		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores = &draw_complete_semaphores[current_swapchain_buffer];
#if defined(VK_KHR_present_wait2)
		ZEROED_STRUCT (VkPresentId2KHR, present_id_info);
		uint64_t next_present_id = current_present_id + 1;
		if (swapchain_present_wait)
		{
			present_id_info.sType = VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR;
			present_id_info.swapchainCount = 1;
			present_id_info.pPresentIds = &next_present_id;
			present_info.pNext = &present_id_info;
		}
#endif
		SDL_LockMutex (vulkan_globals.queue_mutex);
		err = fpQueuePresentKHR (vulkan_globals.queue, &present_info);
		SDL_UnlockMutex (vulkan_globals.queue_mutex);
#if defined(VK_KHR_present_wait2)
		if (swapchain_present_wait)
			current_present_id = next_present_id;
#endif
#if defined(VK_EXT_full_screen_exclusive)
		if ((err == VK_ERROR_OUT_OF_DATE_KHR) || (err == VK_ERROR_SURFACE_LOST_KHR) || (err == VK_SUBOPTIMAL_KHR) ||
			(err == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT))
#else
		if ((err == VK_ERROR_OUT_OF_DATE_KHR) || (err == VK_ERROR_SURFACE_LOST_KHR) || (err == VK_SUBOPTIMAL_KHR))
#endif
		{
			vid.restart_next_frame = true;
			if (err == VK_ERROR_SURFACE_LOST_KHR)
				surface_lost = true;
		}
		else if (err != VK_SUCCESS)
			Sys_Error ("vkQueuePresentKHR failed with code %i", (int)err);

		if (err == VK_SUCCESS || err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_ERROR_SURFACE_LOST_KHR)
			num_images_acquired -= 1;
	}

	current_cb_index = (current_cb_index + 1) % DOUBLE_BUFFERED;
}

/*
=================
GL_EndRendering
=================
*/
task_handle_t GL_EndRendering (qboolean use_tasks, qboolean swapchain)
{
	end_rendering_parms_t parms = {
		.swapchain = swapchain && !vulkan_globals.stereo_active,
		.use_oit = R_UseOIT (),
		.use_mboit = R_UseMBOIT (),
		.render_warp = render_warp,
		.vid_palettize = vid_palettize.value != 0,
		.polyblend = gl_polyblend.value != 0,
		.menu = key_dest == key_menu,
#if defined(_DEBUG)
		.ray_debug = !vulkan_globals.stereo_active && r_raydebug.value && (bmodel_tlas != VK_NULL_HANDLE),
#endif
		.vid_width = vid.width,
		.vid_height = vid.height,
		.render_width = vid.render_width,
		.render_height = vid.render_height,
		.time = fmod (cl.time, 2.0 * M_PI),
		.color_clear_value = vulkan_globals.color_clear_value,
		.density_eye_active = openxr_density_eye_active,
		.density_offsets = {openxr_density_offsets[0], openxr_density_offsets[1]},
		.v_blend[0] = v_blend[0],
		.v_blend[1] = v_blend[1],
		.v_blend[2] = v_blend[2],
		.v_blend[3] = v_blend[3],
		.origin =
			{
				r_refdef.vieworg[0],
				r_refdef.vieworg[1],
				r_refdef.vieworg[2],
			},
		.forward =
			{
				-vulkan_globals.view_matrix[2],
				-vulkan_globals.view_matrix[6],
				-vulkan_globals.view_matrix[10],
			},
		.right =
			{
				vulkan_globals.view_matrix[0],
				vulkan_globals.view_matrix[4],
				vulkan_globals.view_matrix[8],
			},
		.down =
			{
				-vulkan_globals.view_matrix[1],
				-vulkan_globals.view_matrix[5],
				-vulkan_globals.view_matrix[9],
			},
	};
	task_handle_t end_rendering_task = INVALID_TASK_HANDLE;
	if (use_tasks)
		end_rendering_task = Task_AllocateAndAssignFunc ((task_func_t)GL_EndRenderingTask, &parms, sizeof (parms));
	else
		GL_EndRenderingTask (&parms);
	return end_rendering_task;
}

/*
=================
GL_WaitForDeviceIdle
=================
*/
void GL_WaitForDeviceIdle (void)
{
	assert (!Tasks_IsWorker ());
	GL_SynchronizeEndRenderingTask ();
	if (!vulkan_globals.device_idle)
	{
		R_SubmitStagingBuffers ();
		SDL_LockMutex (vulkan_globals.queue_mutex);
		vkDeviceWaitIdle (vulkan_globals.device);
		SDL_UnlockMutex (vulkan_globals.queue_mutex);
	}

	vulkan_globals.device_idle = true;
}

/*
=================
VID_SetMouseCursor
=================
*/
static SDL_Cursor *cursor_default;
static SDL_Cursor *cursor_hand;
static SDL_Cursor *cursor_ibeam;

static void VID_CreateCursors (void)
{
#ifdef USE_SDL3
	cursor_default = SDL_CreateSystemCursor (SDL_SYSTEM_CURSOR_DEFAULT);
	cursor_hand = SDL_CreateSystemCursor (SDL_SYSTEM_CURSOR_POINTER);
	cursor_ibeam = SDL_CreateSystemCursor (SDL_SYSTEM_CURSOR_TEXT);
#else
	cursor_default = SDL_CreateSystemCursor (SDL_SYSTEM_CURSOR_ARROW);
	cursor_hand = SDL_CreateSystemCursor (SDL_SYSTEM_CURSOR_HAND);
	cursor_ibeam = SDL_CreateSystemCursor (SDL_SYSTEM_CURSOR_IBEAM);
#endif
}

static void VID_DestroyCursors (void)
{
#ifdef USE_SDL3
	SDL_DestroyCursor (cursor_default);
	SDL_DestroyCursor (cursor_hand);
	SDL_DestroyCursor (cursor_ibeam);
#else
	SDL_FreeCursor (cursor_default);
	SDL_FreeCursor (cursor_hand);
	SDL_FreeCursor (cursor_ibeam);
#endif
	cursor_default = NULL;
	cursor_hand = NULL;
	cursor_ibeam = NULL;
}

void VID_SetMouseCursor (mousecursor_t cursor)
{
	static mousecursor_t current_cursor = MOUSECURSOR_DEFAULT;

	if (cursor == current_cursor)
		return;
	current_cursor = cursor;

	switch (cursor)
	{
	case MOUSECURSOR_HAND:
		SDL_SetCursor (cursor_hand);
		break;

	case MOUSECURSOR_IBEAM:
		SDL_SetCursor (cursor_ibeam);
		break;

	case MOUSECURSOR_DEFAULT:
	default:
		SDL_SetCursor (cursor_default);
		break;
	}
}

/*
=================
VID_Shutdown
=================
*/
void VID_Shutdown (void)
{
	if (vid_initialized)
	{
		assert (draw_context != NULL);
		// The end task may still write/free a screenshot after the next host
		// frame reaches quit. Retire it before SDL can unload the Vulkan driver.
		GL_SynchronizeEndRenderingTask ();
		if (vulkan_globals.device && vulkan_globals.queue_mutex)
		{
			SDL_LockMutex (vulkan_globals.queue_mutex);
			vkDeviceWaitIdle (vulkan_globals.device);
			SDL_UnlockMutex (vulkan_globals.queue_mutex);
			vulkan_globals.device_idle = true;
		}
		if (vulkan_globals.device)
		{
			R_VRIKRenderShutdown ();
			if (render_resources_created)
			{
				R_DestroyPipelines ();
				render_resources_created = false;
			}
			R_DestroyStereoUIPipelineLayouts ();
		}
		if (openxr_vulkan_binding)
		{
			VRXR_Shutdown ();
			openxr_vulkan_binding = false;
			openxr_vulkan_api_version = 0;
			openxr_vulkan_minimum_version = 0;
			vulkan_globals.openxr_vulkan_available = false;
			vulkan_globals.openxr_multiview_available = false;
			vulkan_globals.openxr_max_multiview_view_count = 0;
		}
		GL_ClearOpenXRFragmentShadingRate ();
		VID_DestroyCursors ();
		SDL_DestroyWindow (draw_context);
		draw_context = NULL;
		SDL_QuitSubSystem (SDL_INIT_VIDEO);
		PL_VID_Shutdown ();
	}
}

/*
===================================================================

MAIN WINDOW

===================================================================
*/

/*
================
ClearAllStates
================
*/
static void ClearAllStates (void)
{
	Key_ClearStates ();
	IN_ClearStates ();
}

//==========================================================================
//
//  COMMANDS
//
//==========================================================================

/*
=================
VID_DescribeCurrentMode_f
=================
*/
static void VID_DescribeCurrentMode_f (void)
{
	if (draw_context)
		Con_Printf (
			"%dx%dx%d %gHz %s\n", VID_GetCurrentWidth (), VID_GetCurrentHeight (), VID_GetCurrentBPP (), VID_GetCurrentRefreshRate (),
			VID_GetFullscreen () ? "fullscreen" : "windowed");
}

/*
=================
VID_DescribeModes_f -- johnfitz -- changed formatting, and added refresh rates after each mode.
=================
*/
static void VID_DescribeModes_f (void)
{
	int i;
	int lastwidth, lastheight, count;

	lastwidth = lastheight = count = 0;

	for (i = 0; i < nummodes; i++)
	{
		if (lastwidth != modelist[i].width || lastheight != modelist[i].height)
		{
			if (count > 0)
				Con_SafePrintf ("\n");
			Con_SafePrintf ("   %4i x %4i : %g", modelist[i].width, modelist[i].height, modelist[i].refreshrate);
			lastwidth = modelist[i].width;
			lastheight = modelist[i].height;
			count++;
		}
	}
	Con_Printf ("\n%i modes\n", count);
}

//==========================================================================
//
//  INIT
//
//==========================================================================

/*
=================
VID_InitModelist
=================
*/
static void VID_InitModelist (void)
{
#ifdef USE_SDL3
	SDL_DisplayID	  display = SDL_GetPrimaryDisplay ();
	int				  count = 0;
	SDL_DisplayMode **modes = (SDL_DisplayMode **)SDL_GetFullscreenDisplayModes (display, &count);
	int				  i;

	if (!modes)
	{
		nummodes = 0;
		return;
	}

	modelist = Mem_Realloc (modelist, sizeof (vmode_t) * count);
	nummodes = 0;

	for (i = 0; i < count; i++)
	{
		const SDL_DisplayMode *mode = modes[i];
		modelist[nummodes].width = mode->w;
		modelist[nummodes].height = mode->h;
		modelist[nummodes].refreshrate = mode->refresh_rate;
		nummodes++;
	}

	SDL_free (modes);
#else
	const int sdlmodes = SDL_GetNumDisplayModes (0);
	int		  i;

	modelist = Mem_Realloc (modelist, sizeof (vmode_t) * sdlmodes);
	nummodes = 0;
	for (i = 0; i < sdlmodes; i++)
	{
		SDL_DisplayMode mode;

		if (SDL_GetDisplayMode (0, i, &mode) == 0)
		{
			modelist[nummodes].width = mode.w;
			modelist[nummodes].height = mode.h;
			modelist[nummodes].refreshrate = mode.refresh_rate;
			nummodes++;
		}
	}
#endif
}

/*
=================
R_CreatePaletteOctreeBuffers
=================
*/
static void R_CreatePaletteOctreeBuffers (uint32_t *colors, int num_colors, palette_octree_node_t *nodes, int num_nodes)
{
	const int colors_size = num_colors * sizeof (uint32_t);
	const int nodes_size = num_nodes * sizeof (palette_octree_node_t);

	buffer_create_info_t buffer_create_infos[2] = {
		{&palette_colors_buffer, colors_size, 0, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, NULL, NULL, "Palette colors"},
		{&palette_octree_buffer, nodes_size, 0, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, NULL, NULL, "Palette octree"},
	};

	vulkan_memory_t memory;
	R_CreateBuffers (
		countof (buffer_create_infos), buffer_create_infos, &memory, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &num_vulkan_misc_allocations, "Palette");

	{
		VkBuffer		staging_buffer;
		VkCommandBuffer command_buffer;
		int				staging_offset;
		uint32_t	   *staging_memory = (uint32_t *)R_StagingAllocate (colors_size, 1, &command_buffer, &staging_buffer, &staging_offset);

		VkBufferCopy region;
		region.srcOffset = staging_offset;
		region.dstOffset = 0;
		region.size = colors_size;
		vkCmdCopyBuffer (command_buffer, staging_buffer, palette_colors_buffer, 1, &region);

		R_StagingBeginCopy ();
		memcpy (staging_memory, colors, colors_size);
		R_StagingEndCopy ();

		ZEROED_STRUCT (VkBufferViewCreateInfo, buffer_view_create_info);
		buffer_view_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
		buffer_view_create_info.buffer = palette_colors_buffer;
		buffer_view_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
		buffer_view_create_info.range = VK_WHOLE_SIZE;
		VkResult err = vkCreateBufferView (vulkan_globals.device, &buffer_view_create_info, NULL, &palette_buffer_view);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBufferView failed with code %i", (int)err);
		GL_SetObjectName ((uint64_t)palette_buffer_view, VK_OBJECT_TYPE_BUFFER_VIEW, "Palette colors");
	}

	{
		VkBuffer		staging_buffer;
		VkCommandBuffer command_buffer;
		int				staging_offset;
		uint32_t	   *staging_memory = (uint32_t *)R_StagingAllocate (nodes_size, 1, &command_buffer, &staging_buffer, &staging_offset);

		VkBufferCopy region;
		region.srcOffset = staging_offset;
		region.dstOffset = 0;
		region.size = nodes_size;
		vkCmdCopyBuffer (command_buffer, staging_buffer, palette_octree_buffer, 1, &region);

		R_StagingBeginCopy ();
		memcpy (staging_memory, nodes, nodes_size);
		R_StagingEndCopy ();
	}
}

/*
===================
VID_Init
===================
*/
void VID_Init (void)
{
	static char vid_center[] = "SDL_VIDEO_CENTERED=center";
	int			p, width, height;
	float		refreshrate;
	int			display_width, display_height;
	float		display_refreshrate;
	qboolean	fullscreen;
	const char *read_vars[] = {"vid_fullscreen",		"vid_width",	"vid_height", "vid_refreshrate", "vid_vsync",
							   "vid_desktopfullscreen", "vid_fsaamode", "vid_fsaa",	  "vid_borderless"};
#define num_readvars countof (read_vars)

	Cvar_RegisterVariable (&vid_fullscreen); // johnfitz
	Cvar_RegisterVariable (&r_width);
	Cvar_RegisterVariable (&r_height);
	Cvar_RegisterVariable (&r_upscalefilter);
	Cvar_RegisterVariable (&vid_width);		  // johnfitz
	Cvar_RegisterVariable (&vid_height);	  // johnfitz
	Cvar_RegisterVariable (&vid_refreshrate); // johnfitz
	Cvar_RegisterVariable (&vid_vsync);		  // johnfitz
	Cvar_RegisterVariable (&vid_maxframelatency);
	Cvar_RegisterVariable (&vid_filter);
	Cvar_RegisterVariable (&vid_anisotropic);
	Cvar_RegisterVariable (&vid_fsaamode);
	Cvar_RegisterVariable (&vid_fsaa);
	Cvar_RegisterVariable (&vid_desktopfullscreen); // QuakeSpasm
	Cvar_RegisterVariable (&vid_borderless);		// QuakeSpasm
	Cvar_RegisterVariable (&vid_palettize);
#if defined(_DEBUG)
	Cvar_RegisterVariable (&r_raydebug);
#endif
	Cvar_SetCallback (&vid_fullscreen, VID_Changed_f);
	Cvar_SetCallback (&vid_width, VID_Changed_f);
	Cvar_SetCallback (&vid_height, VID_Changed_f);
	Cvar_SetCallback (&vid_refreshrate, VID_Changed_f);
	Cvar_SetCallback (&vid_filter, VID_FilterChanged_f);
	Cvar_SetCallback (&vid_anisotropic, VID_FilterChanged_f);
	Cvar_SetCallback (&vid_fsaamode, VID_FSAAChanged_f);
	Cvar_SetCallback (&vid_fsaa, VID_FSAAChanged_f);
	Cvar_SetCallback (&vid_vsync, VID_VsyncChanged_f);
	Cvar_SetCallback (&vid_desktopfullscreen, VID_Changed_f);
	Cvar_SetCallback (&vid_borderless, VID_Changed_f);

	Cmd_AddCommand ("vid_unlock", VID_Unlock);	   // johnfitz
	Cmd_AddCommand ("vid_restart", VID_Restart_f); // johnfitz
	Cmd_AddCommand ("vid_test", VID_Test);		   // johnfitz
	Cmd_AddCommand ("vid_describecurrentmode", VID_DescribeCurrentMode_f);
	Cmd_AddCommand ("vid_describemodes", VID_DescribeModes_f);
	Cmd_AddCommand ("vr_enable", GL_OpenXREnable_f);

#ifdef _DEBUG
	Cmd_AddCommand ("create_palette_octree", CreatePaletteOctree_f);
#endif

	putenv (vid_center); /* SDL_putenv is problematic in versions <= 1.2.9 */

#ifdef USE_SDL3
	if (!SDL_InitSubSystem (SDL_INIT_VIDEO))
#else
	if (SDL_InitSubSystem (SDL_INIT_VIDEO) < 0)
#endif
		Sys_Error ("Couldn't init SDL video: %s", SDL_GetError ());

	const SDL_DisplayMode desktop = VID_GetDesktopDisplayMode ();
	display_width = desktop.w;
	display_height = desktop.h;
	display_refreshrate = desktop.refresh_rate;

	Sys_Printf ("SDL Video Driver: %s\n", SDL_GetCurrentVideoDriver ());

	VID_CreateCursors ();

	if (CFG_OpenConfig (CONFIG_NAME) == 0)
	{
		CFG_ReadCvars (read_vars, num_readvars);
		CFG_CloseConfig ();
	}
	CFG_ReadCvarOverrides (read_vars, num_readvars);

	VID_InitModelist ();

	width = (int)vid_width.value;
	height = (int)vid_height.value;
	refreshrate = vid_refreshrate.value;
	fullscreen = (int)vid_fullscreen.value;
	vulkan_globals.want_full_screen_exclusive = vid_fullscreen.value >= 2;

	if (COM_CheckParm ("-current"))
	{
		width = display_width;
		height = display_height;
		refreshrate = display_refreshrate;
		fullscreen = true;
	}
	else
	{
		p = COM_CheckParm ("-width");
		if (p && p < com_argc - 1)
		{
			width = atoi (com_argv[p + 1]);

			if (!COM_CheckParm ("-height"))
				height = width * 3 / 4;
		}

		p = COM_CheckParm ("-height");
		if (p && p < com_argc - 1)
		{
			height = atoi (com_argv[p + 1]);

			if (!COM_CheckParm ("-width"))
				width = height * 4 / 3;
		}

		p = COM_CheckParm ("-refreshrate");
		if (p && p < com_argc - 1)
			refreshrate = (float)atof (com_argv[p + 1]);

		if (COM_CheckParm ("-window") || COM_CheckParm ("-w"))
			fullscreen = false;
		else if (COM_CheckParm ("-fullscreen") || COM_CheckParm ("-f"))
			fullscreen = true;
	}

	if (!VID_ValidMode (width, height, refreshrate, fullscreen))
	{
		width = (int)vid_width.value;
		height = (int)vid_height.value;
		refreshrate = vid_refreshrate.value;
		fullscreen = (int)vid_fullscreen.value;
	}

	if (!VID_ValidMode (width, height, refreshrate, fullscreen))
	{
		width = 640;
		height = 480;
		refreshrate = display_refreshrate;
		fullscreen = false;
	}

	vid_initialized = true;

	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));

	VID_SetMode (width, height, refreshrate, fullscreen);

	// set window icon
	PL_SetWindowIcon ();

	Con_Printf ("\nVulkan Initialization\n");
	SDL_Vulkan_LoadLibrary (NULL);
	GL_InitInstance ();
	GL_InitDevice ();
	GL_InitCommandBuffers ();
	vulkan_globals.staging_buffer_size = INITIAL_STAGING_BUFFER_SIZE_KB * 1024;
	R_InitStagingBuffers ();
	R_CreateDescriptorSetLayouts ();
	R_CreateDescriptorPool ();
	R_InitGPUBuffers ();
	R_InitMeshHeap ();
	TexMgr_InitHeap ();
	R_InitSamplers ();
	R_CreatePipelineLayouts ();
	R_CreatePaletteOctreeBuffers (palette_octree_colors, NUM_PALETTE_OCTREE_COLORS, palette_octree_nodes, NUM_PALETTE_OCTREE_NODES);
	// GL_CreateRenderResources ();

	// johnfitz -- removed code creating "glquake" subdirectory

	VID_Gamma_Init ();			 // johnfitz
	VID_Menu_RebuildModeList (); // johnfitz

	// QuakeSpasm: current vid settings should override config file settings.
	// so we have to lock the vid mode from now until after all config files are read.
	vid_locked = true;
}

/*
===================
VID_Restart
===================
*/
void VID_Restart (qboolean set_mode)
{
	if (!vid_initialized)
		return;

	GL_SynchronizeEndRenderingTask ();

	int		 width, height;
	float	 refreshrate;
	qboolean fullscreen;

	width = (int)vid_width.value;
	height = (int)vid_height.value;
	refreshrate = vid_refreshrate.value;
	fullscreen = vid_fullscreen.value ? true : false;
	vulkan_globals.want_full_screen_exclusive = vid_fullscreen.value >= 2;

	//
	// validate new mode
	//
	if (set_mode && !VID_ValidMode (width, height, refreshrate, fullscreen))
	{
		Con_Printf ("%dx%d %gHz %s is not a valid mode\n", width, height, refreshrate, fullscreen ? "fullscreen" : "windowed");
		return;
	}

	scr_initialized = false;

	GL_WaitForDeviceIdle ();
	GL_DestroyRenderResources ();

	//
	// set new mode
	//
	if (set_mode)
	{
		const int eye_width = vid.width, eye_height = vid.height;
		VID_SetMode (width, height, refreshrate, fullscreen);
		if (vulkan_globals.stereo_active)
		{
			openxr_desktop_width = vid.width;
			openxr_desktop_height = vid.height;
			vid.width = eye_width;
			vid.height = eye_height;
		}
	}

	if (surface_lost)
	{
		fpDestroySurfaceKHR (vulkan_instance, vulkan_surface, NULL);
		vulkan_surface = VK_NULL_HANDLE;
		GL_CreateSurface ();

		VkBool32	   supported = VK_FALSE;
		const VkResult result =
			fpGetPhysicalDeviceSurfaceSupportKHR (vulkan_physical_device, vulkan_globals.gfx_queue_family_index, vulkan_surface, &supported);
		if (result != VK_SUCCESS || !supported)
			Sys_Error ("Recreated Vulkan surface does not support the current presentation queue (code %i)", (int)result);
		surface_lost = false;
	}

	GL_CreateRenderResources ();

	// conwidth and conheight need to be recalculated
	vid.conwidth = (scr_conwidth.value > 0) ? (int)scr_conwidth.value : (scr_conscale.value > 0) ? (int)(vid.width / scr_conscale.value) : vid.width;
	vid.conwidth = CLAMP (320, vid.conwidth, vid.width);
	vid.conwidth &= 0xFFFFFFF8;
	vid.conheight = vid.conwidth * vid.height / vid.width;
	//
	// keep cvars in line with actual mode
	//
	if (set_mode)
	{
		if (vulkan_globals.stereo_active)
		{
			const int eye_width = vid.width, eye_height = vid.height;
			vid.width = openxr_desktop_width;
			vid.height = openxr_desktop_height;
			VID_SyncCvars ();
			vid.width = eye_width;
			vid.height = eye_height;
		}
		else
			VID_SyncCvars ();
	}

	//
	// update mouse grab
	//
	if (key_dest == key_console || key_dest == key_menu)
	{
		if (modestate == MS_WINDOWED)
			IN_Deactivate (true);
		else if (modestate == MS_FULLSCREEN && key_dest != key_menu)
			IN_HideCursor ();
	}

	R_InitSamplers ();

	SCR_UpdateRelativeScale ();
	VID_Menu_RebuildModeList ();

	scr_initialized = true;
}

/*
===================
VID_Restart_f -- johnfitz -- change video modes on the fly
===================
*/
static void VID_Restart_f (void)
{
	if (vid_locked || !vid_changed)
		return;
	VID_Restart (true);
}

/*
===================
VID_Toggle
new proc by S.A., called by alt-return key binding.
===================
*/
void VID_Toggle (void)
{
	qboolean toggleWorked;
	Uint32	 flags = 0;

	S_ClearBuffer ();

	if (!VID_GetFullscreen ())
	{
#ifdef USE_SDL3
		// Set fullscreen mode before enabling fullscreen
		if (vid_desktopfullscreen.value)
			SDL_SetWindowFullscreenMode (draw_context, NULL);
		else
			SDL_SetWindowFullscreenMode (draw_context, VID_SDL_GetDisplayMode (vid.width, vid.height, vid_refreshrate.value));
		flags = SDL_WINDOW_FULLSCREEN;
#else
		flags = vid_desktopfullscreen.value ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN;
#endif
	}

#ifdef USE_SDL3
	toggleWorked = SDL_SetWindowFullscreen (draw_context, flags != 0);
#else
	toggleWorked = (SDL_SetWindowFullscreen (draw_context, flags) == 0);
#endif
	if (toggleWorked)
	{
		modestate = VID_GetFullscreen () ? MS_FULLSCREEN : MS_WINDOWED;

		VID_SyncCvars ();

		// update mouse grab
		if (key_dest == key_console || key_dest == key_menu)
		{
			if (modestate == MS_WINDOWED)
				IN_Deactivate (true);
			else if (modestate == MS_FULLSCREEN && key_dest != key_menu)
				IN_HideCursor ();
		}
	}
}

/*
================
VID_SyncCvars -- johnfitz -- set vid cvars to match current video mode
================
*/
void VID_SyncCvars (void)
{
	if (draw_context)
	{
		if (!VID_GetDesktopFullscreen ())
		{
			Cvar_SetValueQuick (&vid_width, VID_GetCurrentWindowWidth ());
			Cvar_SetValueQuick (&vid_height, VID_GetCurrentWindowHeight ());
		}
		Cvar_SetValueQuick (&vid_refreshrate, VID_GetCurrentRefreshRate ());
		Cvar_SetQuick (&vid_fullscreen, VID_GetFullscreen () ? (vulkan_globals.want_full_screen_exclusive ? "2" : "1") : "0");
		// don't sync vid_desktopfullscreen, it's a user preference that
		// should persist even if we are in windowed mode.
	}

	vid_changed = false;
}

//==========================================================================
//
//  NEW VIDEO MENU -- johnfitz
//
//==========================================================================

enum
{
	VID_OPT_FULLSCREEN,
	VID_OPT_MODE,
	VID_OPT_REFRESHRATE,
	VID_OPT_VSYNC,
	VID_OPT_RENDER_RESOLUTION,
	VID_OPT_FILTER,
	VID_OPT_PADDING,
	VID_OPT_TEST,
	VID_OPT_APPLY,
	VIDEO_OPTIONS_ITEMS
};

static int video_options_cursor = 0;

typedef struct
{
	int width, height;
} vid_menu_mode;

// TODO: replace these fixed-length arrays with hunk_allocated buffers
static vid_menu_mode vid_menu_modes[MAX_MODE_LIST];
static int			 vid_menu_nummodes = 0;

static float vid_menu_rates[MAX_RATES_LIST];
static int	 vid_menu_numrates = 0;

static qboolean VID_Menu_Borderless (void)
{
	return vid_fullscreen.value == 1 && vid_desktopfullscreen.value;
}

static vid_menu_mode VID_Menu_OutputMode (void)
{
	if (!vid_changed)
		return (vid_menu_mode){vid.width, vid.height};
	if (VID_Menu_Borderless ())
	{
		const SDL_DisplayMode desktop = VID_GetDesktopDisplayMode ();
		return (vid_menu_mode){desktop.w, desktop.h};
	}
	vid_menu_mode mode = {(int)vid_width.value, (int)vid_height.value};
	if (!vid_fullscreen.value)
	{
		mode.width = mode.width * vid.width / q_max (1, VID_GetCurrentWindowWidth ());
		mode.height = mode.height * vid.height / q_max (1, VID_GetCurrentWindowHeight ());
	}
	return mode;
}

static vid_menu_mode VID_Menu_RenderMode (void)
{
	const vid_menu_mode output = VID_Menu_OutputMode ();
	if (r_width.value <= 0 || r_height.value <= 0)
		return (vid_menu_mode){-1, -1};

	vid_menu_mode mode;
	mode.width = (int)CLAMP (q_min (320, output.width), r_width.value, output.width);
	mode.height = (int)CLAMP (q_min (200, output.height), r_height.value, output.height);
	if (mode.width == output.width && mode.height == output.height)
		return (vid_menu_mode){-1, -1};
	return mode;
}

static qboolean VID_Menu_OptionSelectable (int option)
{
	if (option == VID_OPT_PADDING)
		return false;
	if (option == VID_OPT_FILTER && VID_Menu_RenderMode ().width < 0)
		return false;
	if (vid_fullscreen.value == 1 && vid_desktopfullscreen.value && (option == VID_OPT_MODE || option == VID_OPT_REFRESHRATE))
		return false;
	return true;
}

// common window sizes offered in addition to the display modes when windowed
static const vid_menu_mode vid_menu_windowed_modes[] = {
	{320, 240},	  {640, 480},	{800, 600},	  {1024, 768},	{1280, 720},  {1280, 800},	{1366, 768},  {1440, 900},	{1600, 900},  {1600, 1200}, {1680, 1050},
	{1920, 1080}, {1920, 1200}, {2560, 1080}, {2560, 1440}, {2560, 1600}, {3440, 1440}, {3840, 1600}, {3840, 2160}, {5120, 1440}, {5120, 2880},
};

/*
================
VID_Menu_AddMode
================
*/
static void VID_Menu_AddMode (int w, int h)
{
	int i;

	if (vid_menu_nummodes >= MAX_MODE_LIST)
		return;

	for (i = 0; i < vid_menu_nummodes; i++)
	{
		if (vid_menu_modes[i].width == w && vid_menu_modes[i].height == h)
			return;
	}

	vid_menu_modes[vid_menu_nummodes].width = w;
	vid_menu_modes[vid_menu_nummodes].height = h;
	vid_menu_nummodes++;
}

/*
================
VID_Menu_CompareModes
================
*/
static int VID_Menu_CompareModes (const void *a, const void *b)
{
	const vid_menu_mode *ma = (const vid_menu_mode *)a;
	const vid_menu_mode *mb = (const vid_menu_mode *)b;

	if (ma->width != mb->width)
		return mb->width - ma->width;
	return mb->height - ma->height;
}

/*
================
VID_Menu_RebuildModeList

regenerates mode list based on current vid_fullscreen. fullscreen offers the
display modes, windowed additionally offers common window sizes that fit on
the desktop since windows are not limited to display modes
================
*/
static void VID_Menu_BuildModeList (qboolean render_resolution)
{
	int i;

	vid_menu_nummodes = 0;
	if (render_resolution)
	{
		const vid_menu_mode output = VID_Menu_OutputMode ();
		const int			max_width = output.width;
		const int			max_height = output.height;
		VID_Menu_AddMode (-1, -1); // Native occupies the highest-resolution position in the cycle.
		if (max_width / 2 >= 320 && max_height / 2 >= 200)
		{
			VID_Menu_AddMode (max_width / 2, max_height / 2);
			VID_Menu_AddMode (max_width * 3 / 4, max_height * 3 / 4);
		}
		for (i = 0; i < (int)countof (vid_menu_windowed_modes); ++i)
			if (vid_menu_windowed_modes[i].width <= max_width && vid_menu_windowed_modes[i].height <= max_height)
				VID_Menu_AddMode (vid_menu_windowed_modes[i].width, vid_menu_windowed_modes[i].height);
		for (i = 0; i < nummodes; ++i)
			if (modelist[i].width <= max_width && modelist[i].height <= max_height)
				VID_Menu_AddMode (modelist[i].width, modelist[i].height);
		const vid_menu_mode current = VID_Menu_RenderMode ();
		VID_Menu_AddMode (current.width, current.height);
		// The output size is represented only by Native, never by a duplicate numeric entry.
		int count = 1;
		for (i = 1; i < vid_menu_nummodes; ++i)
			if (vid_menu_modes[i].width <= max_width && vid_menu_modes[i].height <= max_height &&
				(vid_menu_modes[i].width < max_width || vid_menu_modes[i].height < max_height))
				vid_menu_modes[count++] = vid_menu_modes[i];
		vid_menu_nummodes = count;
		qsort (vid_menu_modes + 1, vid_menu_nummodes - 1, sizeof (vid_menu_modes[0]), VID_Menu_CompareModes);
		return;
	}

	for (i = 0; i < nummodes; i++)
		VID_Menu_AddMode (modelist[i].width, modelist[i].height);

	if (!vid_fullscreen.value)
	{
		const SDL_DisplayMode desktop = VID_GetDesktopDisplayMode ();
		for (i = 0; i < (int)countof (vid_menu_windowed_modes); i++)
		{
			if (vid_menu_windowed_modes[i].width <= desktop.w && vid_menu_windowed_modes[i].height <= desktop.h)
				VID_Menu_AddMode (vid_menu_windowed_modes[i].width, vid_menu_windowed_modes[i].height);
		}
	}

	qsort (vid_menu_modes, vid_menu_nummodes, sizeof (vid_menu_modes[0]), VID_Menu_CompareModes);
}

static void VID_Menu_RebuildModeList (void)
{
	VID_Menu_BuildModeList (false);
}

/*
================
VID_Menu_RebuildRateList

regenerates rate list based on current vid_width, vid_height
================
*/
static void VID_Menu_RebuildRateList (void)
{
	int	  i, j;
	float r;

	vid_menu_numrates = 0;

	for (i = 0; i < nummodes; i++)
	{
		// rate list is limited to rates available with current width/height
		if (modelist[i].width != vid_width.value || modelist[i].height != vid_height.value)
			continue;

		r = modelist[i].refreshrate;

		for (j = 0; j < vid_menu_numrates; j++)
		{
			if (vid_menu_rates[j] == r)
				break;
		}

		if (j == vid_menu_numrates)
		{
			vid_menu_rates[j] = r;
			vid_menu_numrates++;
		}
	}

	// if there are no valid fullscreen refreshrates for this width/height, just pick one
	if (vid_menu_numrates == 0)
	{
		Cvar_SetValue ("vid_refreshrate", modelist[0].refreshrate);
		return;
	}

	// if vid_refreshrate is not in the new list, change vid_refreshrate
	for (i = 0; i < vid_menu_numrates; i++)
		if (vid_menu_rates[i] == vid_refreshrate.value)
			break;

	if (i == vid_menu_numrates)
		Cvar_SetValue ("vid_refreshrate", vid_menu_rates[0]);
}

/*
================
VID_Menu_ChooseNextMode

chooses next resolution in order, then updates vid_width and
vid_height cvars, then updates refreshrate lists
================
*/
static void VID_Menu_ChooseNextMode (int dir, qboolean render_resolution)
{
	int i;
	VID_Menu_BuildModeList (render_resolution);
	cvar_t			   *width = render_resolution ? &r_width : &vid_width;
	cvar_t			   *height = render_resolution ? &r_height : &vid_height;
	const vid_menu_mode current = render_resolution ? VID_Menu_RenderMode () : (vid_menu_mode){(int)width->value, (int)height->value};

	if (vid_menu_nummodes)
	{
		for (i = 0; i < vid_menu_nummodes; i++)
		{
			if (vid_menu_modes[i].width == current.width && vid_menu_modes[i].height == current.height)
				break;
		}

		if (i == vid_menu_nummodes) // can't find it in list, so it must be a custom windowed res
		{
			i = 0;
		}
		else
		{
			i += dir;
			if (i >= vid_menu_nummodes)
				i = 0;
			else if (i < 0)
				i = vid_menu_nummodes - 1;
		}

		Cvar_SetValueQuick (width, (float)vid_menu_modes[i].width);
		Cvar_SetValueQuick (height, (float)vid_menu_modes[i].height);
		if (!render_resolution)
			VID_Menu_RebuildRateList ();
	}
}

/*
================
VID_Menu_ChooseNextRate

chooses next refresh rate in order, then updates vid_refreshrate cvar
================
*/
static void VID_Menu_ChooseNextRate (int dir)
{
	int i;

	for (i = 0; i < vid_menu_numrates; i++)
	{
		if (vid_menu_rates[i] == vid_refreshrate.value)
			break;
	}

	if (i == vid_menu_numrates) // can't find it in list
	{
		i = 0;
	}
	else
	{
		i += dir;
		if (i >= vid_menu_numrates)
			i = 0;
		else if (i < 0)
			i = vid_menu_numrates - 1;
	}

	Cvar_SetValue ("vid_refreshrate", vid_menu_rates[i]);
}

/*
================
VID_Menu_ChooseNextFullScreenMode
================
*/
static void VID_Menu_ChooseNextFullScreenMode (int dir)
{
	int i, best, bestdist, dist;

	if (vulkan_globals.full_screen_exclusive)
		Cvar_SetValueQuick (&vid_fullscreen, (float)(((int)vid_fullscreen.value + 3 + dir) % 3));
	else
		Cvar_SetValueQuick (&vid_fullscreen, (float)(((int)vid_fullscreen.value + 2 + dir) % 2));

	VID_Menu_RebuildModeList ();
	if (VID_Menu_Borderless ())
		return;

	// if the current width/height is not in the new list, snap to the closest mode
	for (i = 0; i < vid_menu_nummodes; i++)
	{
		if (vid_menu_modes[i].width == vid_width.value && vid_menu_modes[i].height == vid_height.value)
			break;
	}

	if (i == vid_menu_nummodes && vid_menu_nummodes > 0)
	{
		best = 0;
		bestdist = INT_MAX;
		for (i = 0; i < vid_menu_nummodes; i++)
		{
			dist = abs (vid_menu_modes[i].width - (int)vid_width.value) + abs (vid_menu_modes[i].height - (int)vid_height.value);
			if (dist < bestdist)
			{
				bestdist = dist;
				best = i;
			}
		}
		Cvar_SetValueQuick (&vid_width, (float)vid_menu_modes[best].width);
		Cvar_SetValueQuick (&vid_height, (float)vid_menu_modes[best].height);
		VID_Menu_RebuildRateList ();
	}
}

/*
================
VID_Menu_ChooseNextVSyncMode
================
*/
static void VID_Menu_ChooseNextVSyncMode (int dir)
{
	Cvar_SetValueQuick (&vid_vsync, (float)(((int)vid_vsync.value + 3 + dir) % 3));
}

/*
================
M_Video_Key
================
*/
void M_Video_Key (int key)
{
	switch (key)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		VID_SyncCvars (); // sync cvars before leaving menu. FIXME: there are other ways to leave menu
		S_LocalSound ("misc/menu1.wav");
		M_Menu_Options_f ();
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		do
		{
			if (--video_options_cursor < 0)
				video_options_cursor = VIDEO_OPTIONS_ITEMS - 1;
		} while (!VID_Menu_OptionSelectable (video_options_cursor));
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		do
		{
			if (++video_options_cursor >= VIDEO_OPTIONS_ITEMS)
				video_options_cursor = 0;
		} while (!VID_Menu_OptionSelectable (video_options_cursor));
		break;

	case K_LEFTARROW:
		S_LocalSound ("misc/menu3.wav");
		switch (video_options_cursor)
		{
		case VID_OPT_MODE:
			VID_Menu_ChooseNextMode (1, false);
			break;
		case VID_OPT_RENDER_RESOLUTION:
			VID_Menu_ChooseNextMode (1, true);
			break;
		case VID_OPT_FILTER:
			if (VID_Menu_OptionSelectable (VID_OPT_FILTER))
				Cvar_SetValueQuick (&r_upscalefilter, r_upscalefilter.value >= 1 ? 0 : 1);
			break;
		case VID_OPT_REFRESHRATE:
			VID_Menu_ChooseNextRate (1);
			break;
		case VID_OPT_FULLSCREEN:
			VID_Menu_ChooseNextFullScreenMode (-1);
			break;
		case VID_OPT_VSYNC:
			VID_Menu_ChooseNextVSyncMode (-1);
			break;
		default:
			break;
		}
		break;

	case K_RIGHTARROW:
		S_LocalSound ("misc/menu3.wav");
		switch (video_options_cursor)
		{
		case VID_OPT_MODE:
			VID_Menu_ChooseNextMode (-1, false);
			break;
		case VID_OPT_RENDER_RESOLUTION:
			VID_Menu_ChooseNextMode (-1, true);
			break;
		case VID_OPT_FILTER:
			if (VID_Menu_OptionSelectable (VID_OPT_FILTER))
				Cvar_SetValueQuick (&r_upscalefilter, r_upscalefilter.value >= 1 ? 0 : 1);
			break;
		case VID_OPT_REFRESHRATE:
			VID_Menu_ChooseNextRate (-1);
			break;
		case VID_OPT_FULLSCREEN:
			VID_Menu_ChooseNextFullScreenMode (1);
			break;
		case VID_OPT_VSYNC:
			VID_Menu_ChooseNextVSyncMode (1);
			break;
		default:
			break;
		}
		break;

	case K_MOUSE1:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		switch (video_options_cursor)
		{
		case VID_OPT_MODE:
			VID_Menu_ChooseNextMode (-1, false);
			break;
		case VID_OPT_RENDER_RESOLUTION:
			VID_Menu_ChooseNextMode (-1, true);
			break;
		case VID_OPT_FILTER:
			if (VID_Menu_OptionSelectable (VID_OPT_FILTER))
				Cvar_SetValueQuick (&r_upscalefilter, r_upscalefilter.value >= 1 ? 0 : 1);
			break;
		case VID_OPT_REFRESHRATE:
			VID_Menu_ChooseNextRate (-1);
			break;
		case VID_OPT_FULLSCREEN:
			VID_Menu_ChooseNextFullScreenMode (1);
			break;
		case VID_OPT_VSYNC:
			VID_Menu_ChooseNextVSyncMode (1);
			break;
		case VID_OPT_TEST:
			Cbuf_AddText ("vid_test\n");
			break;
		case VID_OPT_APPLY:
			Cbuf_AddText ("vid_restart\n");
			break;
		default:
			break;
		}
		break;

	default:
		break;
	}
}

/*
================
M_Video_Draw
================
*/
void M_Video_Draw (cb_context_t *cbx)
{
	qpic_t *p;
	int		y = 4;

	// plaque
	p = Draw_CachePic ("gfx/qplaque.lmp");
	M_DrawTransPic (cbx, 16, y, p);

	// p = Draw_CachePic ("gfx/vidmodes.lmp");
	p = Draw_CachePic ("gfx/p_option.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, y, p);

	y += 36;

	// options
	for (int i = 0; i < VIDEO_OPTIONS_ITEMS; i++)
	{
		const qboolean selectable = VID_Menu_OptionSelectable (i);
		const qboolean dimmed = i != VID_OPT_PADDING && !selectable;
		if (dimmed)
			GL_SetCanvasColor (1, 1, 1, 0.375f);

		switch (i)
		{
		case VID_OPT_MODE:
			M_Print (cbx, MENU_LABEL_X, y, "Video mode");
			if (VID_Menu_Borderless ())
			{
				const vid_menu_mode mode = VID_Menu_OutputMode ();
				M_Print (cbx, MENU_VALUE_X, y, va ("%ix%i", mode.width, mode.height));
			}
			else
				M_Print (cbx, MENU_VALUE_X, y, va ("%ix%i", (int)vid_width.value, (int)vid_height.value));
			break;
		case VID_OPT_RENDER_RESOLUTION:
		{
			const vid_menu_mode mode = VID_Menu_RenderMode ();
			M_Print (cbx, MENU_LABEL_X, y, "Render resolution");
			M_Print (cbx, MENU_VALUE_X, y, mode.width < 0 ? "Native" : va ("%ix%i", mode.width, mode.height));
			break;
		}
		case VID_OPT_FILTER:
			M_Print (cbx, MENU_LABEL_X, y, "Filter");
			M_Print (cbx, MENU_VALUE_X, y, r_upscalefilter.value >= 1 ? "smooth" : "classic");
			break;
		case VID_OPT_REFRESHRATE:
			M_Print (cbx, MENU_LABEL_X, y, "Refresh rate");
			M_Print (cbx, MENU_VALUE_X, y, va ("%g", vid_refreshrate.value));
			break;
		case VID_OPT_FULLSCREEN:
			M_Print (cbx, MENU_LABEL_X, y, "Fullscreen");
			M_Print (cbx, MENU_VALUE_X, y, ((int)vid_fullscreen.value == 0) ? "off" : (((int)vid_fullscreen.value == 1) ? "borderless" : "exclusive"));
			break;
		case VID_OPT_VSYNC:
			M_Print (cbx, MENU_LABEL_X, y, "Vertical sync");
			M_Print (cbx, MENU_VALUE_X, y, ((int)vid_vsync.value == 0) ? "off" : (((int)vid_vsync.value == 1) ? "on" : "triple buffer"));
			break;
		case VID_OPT_TEST:
			M_Print (cbx, MENU_LABEL_X, y, "Test changes");
			break;
		case VID_OPT_APPLY:
			M_Print (cbx, MENU_LABEL_X, y, "Apply changes");
			break;
		}

		if (dimmed)
			GL_SetCanvasColor (1, 1, 1, 1);

		if (selectable)
			M_Mouse_UpdateCursor (&video_options_cursor, 12, 400, y, 8, i);
		if (video_options_cursor == i)
			Draw_Character (cbx, MENU_CURSOR_X, y, 12 + ((int)(realtime * 4) & 1));

		y += 8;
	}
}

/*
================
M_Menu_Video_f
================
*/
void M_Menu_Video_f (void)
{
	M_MenuChanged ();
	IN_Deactivate (modestate == MS_WINDOWED);
	key_dest = key_menu;
	m_state = m_video;
	m_entersound = true;

	// set all the cvars to match the current mode when entering the menu
	VID_SyncCvars ();
	if (!VID_Menu_OptionSelectable (video_options_cursor))
		video_options_cursor = VID_OPT_FULLSCREEN;

	// set up mode and rate lists based on current cvars
	VID_Menu_RebuildModeList ();
	VID_Menu_RebuildRateList ();
}

/*
==============================================================================

SCREEN SHOTS

==============================================================================
*/

static void SCR_ScreenShot_Usage (void)
{
	Con_Printf ("usage: screenshot <format> <quality>\n");
	Con_Printf ("   format must be \"png\" or \"tga\" or \"jpg\"\n");
	Con_Printf ("   quality must be 1-100\n");
	return;
}
/*
==================
SCR_GetScreenshotMapTitle
==================
*/

static void SCR_GetScreenshotMapTitle (char *buf, size_t maxchars)
{
	char   clean[countof (cl.levelname)] = {0};
	size_t i, j;

	for (i = j = 0; i + 1 < countof (cl.levelname) && cl.levelname[i]; i++)
	{
		char c = cl.levelname[i] & 0x7f;
		switch (c)
		{
		case '/':
		case '|':
		case ':':
		case ' ':
		case '\n':
		case '*':
		case '<':
		case '>':
		case '-':
		case '?':
		case '!':
		case '"':
		case '\t':
		case '\\':
			c = '_';
			break;
		default:
			break;
		}
		// remove leading spaces, replace consecutive spaces with a single one
		if (c != ' ' || (j > 0 && clean[j - 1] != c))
			clean[j++] = c;
	}
	clean[j++] = '\0';

	q_strlcpy (buf, clean, maxchars);
}

/*
==================
SCR_ScreenShot_f -- johnfitz
==================
*/
void SCR_ScreenShot_f (void)
{
	if ((vulkan_globals.swap_chain_format != VK_FORMAT_B8G8R8A8_UNORM) && (vulkan_globals.swap_chain_format != VK_FORMAT_B8G8R8A8_SRGB) &&
		(vulkan_globals.swap_chain_format != VK_FORMAT_R8G8B8A8_UNORM) && (vulkan_globals.swap_chain_format != VK_FORMAT_R8G8B8A8_SRGB))
	{
		Con_Printf ("SCR_ScreenShot_f: Unsupported surface format\n");
		return;
	}

	memcpy (screenshot_ext, "png", sizeof (screenshot_ext));

	if (Cmd_Argc () >= 2)
	{
		const char *requested_ext = Cmd_Argv (1);

		if (!q_strcasecmp ("png", requested_ext) || !q_strcasecmp ("tga", requested_ext) || !q_strcasecmp ("jpg", requested_ext))
			memcpy (screenshot_ext, requested_ext, sizeof (screenshot_ext));
		else
		{
			SCR_ScreenShot_Usage ();
			return;
		}
	}

	// read quality as the 3rd param (only used for JPG)
	screenshot_quality = 90;
	if (Cmd_Argc () >= 3)
		screenshot_quality = atoi (Cmd_Argv (2));
	if (screenshot_quality < 1 || screenshot_quality > 100)
	{
		SCR_ScreenShot_Usage ();
		return;
	}

	if ((vulkan_globals.swap_chain_format != VK_FORMAT_B8G8R8A8_UNORM) && (vulkan_globals.swap_chain_format != VK_FORMAT_B8G8R8A8_SRGB) &&
		(vulkan_globals.swap_chain_format != VK_FORMAT_R8G8B8A8_UNORM) && (vulkan_globals.swap_chain_format != VK_FORMAT_R8G8B8A8_SRGB))
	{
		Con_Printf ("SCR_ScreenShot_f: Unsupported surface format\n");
		return;
	}

	// find a file name to save it to
	int i;

	// retreive the current date
	time_t now;
	time (&now);
	struct tm *lt = localtime (&now);

	// extract map title:
	char map_title[128];
	SCR_GetScreenshotMapTitle (map_title, sizeof (map_title));

	// not all maps have a valid map title:
	const bool have_map_title = (strlen (map_title) > 0);

	for (i = 0; i < 100; i++)
	{
		q_snprintf (
			screenshot_imagename, sizeof (screenshot_imagename), "%s-%s%s-%04d%02d%02d-%02d%02d%02d-%02i.%s", SCREENSHOT_PREFIX,
			(have_map_title ? va ("%s-", map_title) : ""), cl.mapname, lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec, i,
			screenshot_ext); // "vkQuake%04scbx_index.tga"

		char checkname[MAX_OSPATH];
		q_snprintf (checkname, sizeof (checkname), "%s/%s", com_gamedir, screenshot_imagename);
		if (Sys_FileType (checkname) == FS_ENT_NONE)
			break; // file doesn't exist
	}
	if (i == 100)
	{
		Con_Printf ("SCR_ScreenShot_f: Couldn't find an unused filename\n");
		return;
	}

	take_screenshot = true;
}

void VID_FocusGained (void)
{
	has_focus = true;
	if (vulkan_globals.want_full_screen_exclusive)
	{
		vid.restart_next_frame = true;
	}
}

void VID_FocusLost (void)
{
	has_focus = false;
	if (vulkan_globals.want_full_screen_exclusive)
	{
		vid.restart_next_frame = true;
	}
}
