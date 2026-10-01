/* Exercise the production command owner without a renderer/device. Session
 * availability is supplied by a spy; the backend recovery fixture qualifies it. */
#include "../Quake/gl_vidsdl.c"

vulkanglobals_t vulkan_globals;
cvar_t vr_foveation, vr_eye_tracking;
keydest_t key_dest;
static int eye_available, update_calls, update_result, last_mode, last_allow_eye;
static float update_centers[2][2];
static const char *enable_argument;
static vrxr_stop_reason_t stop_reason;
static int retry_available, retry_queries, messages;
static int novr, adoptions, adoption_result = 1, registrations;
static int runtime_fb_supported, last_density_request;
static int attachments, input_releases, reference_invalidations, restored_views, joins;
static int aborts, detaches, attachment_result = 1;
static int recovery_release_baseline = -1;
static vrxr_stop_reason_t abort_stop, detach_stop;
static void (*retire_callback)(void *);
atomic_uint32_t num_vulkan_misc_allocations;
int Cmd_Argc (void) { return 2; }
int COM_CheckParm (const char *argument) { assert (!strcmp (argument, "-novr")); return novr; }
const char *Cmd_Argv (int argument) { assert (argument == 1); return enable_argument; }
void Con_Printf (const char *format, ...) { (void)format; ++messages; }
vrxr_stop_reason_t VRXR_StopReason (void) { return stop_reason; }
int VRXR_VulkanRetryAvailable (void) { ++retry_queries; return retry_available; }
int VRXR_AdoptVulkan (void (*log)(const char *), VkInstance instance, VkPhysicalDevice physical, VkDevice device)
{
 (void)log; (void)instance; (void)physical; (void)device;
 assert (joins && !vulkan_globals.stereo_active); ++adoptions;
 if (adoption_result) retry_available = 1;
 return adoption_result;
}
int VRXR_SetVulkanQueueCallbacks (void (*lock)(void *), void (*unlock)(void *), void *owner)
{ assert (lock == GL_OpenXRLockQueue && unlock == GL_OpenXRUnlockQueue && owner == vulkan_globals.queue_mutex); ++registrations; return 1; }
int VRXR_VulkanFoveationSupported (void) { return runtime_fb_supported; }
int VRXR_VulkanFoveationEyeSupported (void) { return 0; }
int VRXR_VulkanSwapchainImageFlagsSupported (void) { return 0; }
int VRXR_AttachVulkan (uint32_t family, uint32_t index, VkImageUsageFlags usage,
 uint32_t layers, void (*retire)(void *), void *owner, int density, VkImageCreateFlags flags)
{
 (void)family; (void)usage; (void)owner; (void)flags;
 last_density_request = density;
 assert (density == (!openxr_density_backend_failed && vulkan_globals.openxr_fragment_density_map_enabled && runtime_fb_supported));
 assert (index == 0 && layers == 2); ++attachments; retire_callback = retire; stop_reason = VRXR_STOP_NONE; return attachment_result;
}
void VRXR_AbortFrame (void)
{
 assert (input_releases); ++aborts;
 if (recovery_release_baseline >= 0)
  assert (input_releases == recovery_release_baseline + 1);
 if (abort_stop != VRXR_STOP_NONE)
 {
  stop_reason = abort_stop;
  // Actual terminal abort synchronously retires images before detach.
  if (retire_callback) { retire_callback (NULL); retire_callback = NULL; }
 }
}
void VRXR_DetachVulkan (void)
{
 assert (input_releases); ++detaches;
 if (retire_callback) { retire_callback (NULL); retire_callback = NULL; }
 if (detach_stop != VRXR_STOP_NONE) stop_reason = detach_stop;
}
int VRXR_GetViewSize (int eye, unsigned *width, unsigned *height)
{ assert (eye == 0); *width = 24; *height = 40; return 1; }
VkFormat VRXR_VulkanColorFormat (void) { return VK_FORMAT_R8G8B8A8_SRGB; }
int VRXR_VulkanFoveationEyeAvailable (void) { return eye_available; }
int VRXR_UpdateVulkanFoveation (int mode, int allow_eye, float centers[2][2])
{
 ++update_calls; last_mode = mode; last_allow_eye = allow_eye;
 memcpy (centers, update_centers, sizeof update_centers);
 return mode == 0 && update_result >= 0 ? 0 : update_result;
}
void VR_InputCommands (const vrxr_frame_t *frame) { assert (!frame); ++input_releases; }
void R_RestoreStereoView (void) { ++restored_views; }
void R_InvalidateStereoReference (void) { ++reference_invalidations; }
qboolean Tasks_IsWorker (void) { return false; }
qboolean Task_Join (task_handle_t task, uint32_t timeout)
{ (void)task; assert (timeout == TASK_TIMEOUT_INFINITE); ++joins; return true; }
void R_SubmitStagingBuffers (void) { assert (!"GPU work is outside this fixture"); }
void R_VRIKRenderShutdown (void) {}
void R_DestroyPipelines (void) {}
void R_DestroyFrameBuffers (void) {}
void R_DestroyRenderPasses (void) {}
void R_DestroySSAO (void) {}
void R_FreeDescriptorSet (VkDescriptorSet set, vulkan_desc_set_layout_t *layout) { (void)set; (void)layout; }
void R_FreeVulkanMemory (vulkan_memory_t *memory, atomic_uint32_t *count)
{ (void)count; assert (!memory->handle); }
void Mem_Free (const void *memory) { free ((void *)memory); }
VKAPI_ATTR void VKAPI_CALL vkDestroyImage (VkDevice device, VkImage image, const VkAllocationCallbacks *allocator)
{ (void)device; (void)allocator; assert (!image); }
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView (VkDevice device, VkImageView view, const VkAllocationCallbacks *allocator)
{ (void)device; (void)allocator; assert (!view); }

static void preparation_cases (void)
{
 VkImageView empty_density_view = VK_NULL_HANDLE;
 openxr_density_image_views = &empty_density_view;
 vulkan_globals.openxr_fragment_density_map_active = true;
 vulkan_globals.openxr_fragment_density_offset_enabled = true;
 vulkan_globals.openxr_fragment_density_offset_granularity = (VkExtent2D){8, 4};
 vid.render_width = 896; vid.render_height = 1008;
 eye_available = 1; vr_foveation.value = 2; vr_eye_tracking.value = 1;
 key_dest = key_game; update_result = 2;
 update_centers[0][0] = .25f; update_centers[0][1] = -.25f;
 update_centers[1][0] = -.5f; update_centers[1][1] = .5f;
 assert (GL_PrepareRuntimeFoveation ());
 assert (update_calls == 1 && last_mode == 2 && last_allow_eye);
 assert (openxr_density_eye_active && vulkan_globals.openxr_fragment_density_frame_active);
 assert (openxr_density_offsets[0].x == 112 && openxr_density_offsets[0].y == -128);
 assert (openxr_density_offsets[1].x == -224 && openxr_density_offsets[1].y == 252);
 // Opt-out and unavailable eye profiles must request off, never fixed.
 vr_eye_tracking.value = 0; update_result = 0;
 assert (GL_PrepareRuntimeFoveation () && last_mode == 0 && !last_allow_eye);
 assert (!openxr_density_eye_active && !vulkan_globals.openxr_fragment_density_frame_active);
 assert (!openxr_density_offsets[0].x && !openxr_density_offsets[1].y);
 vr_eye_tracking.value = 1; eye_available = 0;
 assert (GL_PrepareRuntimeFoveation () && last_mode == 0 && !last_allow_eye);
 eye_available = 1; key_dest = key_menu;
 assert (GL_PrepareRuntimeFoveation () && last_mode == 0 && !last_allow_eye);
 key_dest = key_game; vr_foveation.value = 1; update_result = 1;
 assert (GL_PrepareRuntimeFoveation () && last_mode == 1 && !last_allow_eye);
 assert (!openxr_density_eye_active && vulkan_globals.openxr_fragment_density_frame_active);
 // Invalid offset conversion requests a second, explicit off update.
 vr_foveation.value = 2; update_result = 2; update_centers[1][1] = NAN;
 const int before_invalid = update_calls;
 assert (GL_PrepareRuntimeFoveation () && update_calls == before_invalid + 2);
 assert (last_mode == 0 && !last_allow_eye && !openxr_density_eye_active);
 assert (!vulkan_globals.openxr_fragment_density_frame_active);
 // A failed restoration is unusable, and the failure latch retires all
 // per-frame activity. Recovery is qualified separately below.
 update_result = -1;
 assert (!GL_PrepareRuntimeFoveation ());
 assert (openxr_density_backend_failed && !vulkan_globals.openxr_fragment_density_map_active);
 assert (!openxr_density_eye_active && !vulkan_globals.openxr_fragment_density_frame_active);
 const int failed_updates = update_calls;
 update_result = 0;
 assert (GL_PrepareRuntimeFoveation () && update_calls == failed_updates + 1);
 assert (last_mode == 0 && !last_allow_eye);
 assert (openxr_density_backend_failed && !vulkan_globals.openxr_fragment_density_frame_active);
 // Missing density views must also establish off, rather than bypass it.
 openxr_density_image_views = NULL; openxr_density_backend_failed = false;
 vulkan_globals.openxr_fragment_density_map_active = true;
 last_mode = 2; last_allow_eye = 1;
 const int missing_updates = update_calls;
 update_result = 0;
 assert (GL_PrepareRuntimeFoveation () && last_mode == 0 && !last_allow_eye);
 assert (update_calls == missing_updates + 1);
 update_result = -1;
 assert (!GL_PrepareRuntimeFoveation () && update_calls == missing_updates + 2);
 openxr_density_image_views = NULL;
 openxr_density_backend_failed = false;
 vulkan_globals.openxr_fragment_density_offset_enabled = false;
}

static void recover_once (void)
{
 recovery_release_baseline = input_releases;
 GL_RecoverRuntimeFoveation ();
 assert (input_releases == recovery_release_baseline + 1);
 recovery_release_baseline = -1;
}

static void recovery_cases (void)
{
 const vrxr_stop_reason_t stops[] = {VRXR_STOP_EXITING, VRXR_STOP_SESSION_LOST,
  VRXR_STOP_INSTANCE_LOST, VRXR_STOP_FAILURE};
 const int previous_attachments = attachments, previous_releases = input_releases;
 openxr_density_backend_failed = true;
 stop_reason = VRXR_STOP_NONE; retry_available = 1;
 recover_once ();
 assert (input_releases == previous_releases + 1 && aborts == 1);
 assert (!vulkan_globals.stereo_active && !openxr_attach_attempted);
 assert (openxr_density_backend_failed);
 GL_OpenXRAttach ();
 assert (attachments == previous_attachments + 1 && !last_density_request);
 assert (vulkan_globals.stereo_active && openxr_attach_attempted);
 // A device selected for FDM cannot enable KHR during recovery.
 assert (vulkan_globals.openxr_fragment_density_map_enabled);
 assert (!vulkan_globals.openxr_fragment_shading_rate_available);
 for (int i = 0; i < 3; ++i) GL_OpenXRAttach ();
 assert (attachments == previous_attachments + 1);

 for (size_t i = 0; i < sizeof stops / sizeof stops[0]; ++i)
 {
  abort_stop = stops[i]; stop_reason = VRXR_STOP_NONE;
  // Prepare a distinct empty-resource session; its terminal abort owns
  // retirement, and subsequent detach must not retire it again.
  retire_callback = GL_OpenXRRetireImages;
  vulkan_globals.stereo_active = true; vid.width = 24; vid.height = 40;
  const int attempts = attachments;
  const int references = reference_invalidations;
  recover_once ();
  assert (reference_invalidations == references + 1 && !retire_callback);
  assert (!vulkan_globals.stereo_active && openxr_attach_attempted);
  for (int frame = 0; frame < 3; ++frame) GL_OpenXRAttach ();
  assert (attachments == attempts);
 }
 abort_stop = VRXR_STOP_NONE; stop_reason = VRXR_STOP_NONE;
 detach_stop = VRXR_STOP_FAILURE;
 recover_once ();
 assert (openxr_attach_attempted && !vulkan_globals.stereo_active);
 detach_stop = VRXR_STOP_NONE; stop_reason = VRXR_STOP_NONE; retry_available = 0;
 recover_once ();
 assert (openxr_attach_attempted && !vulkan_globals.stereo_active);
 retry_available = 1; attachment_result = 0;
 const int rejected_attempts = attachments;
 recover_once ();
 GL_OpenXRAttach ();
 assert (attachments == rejected_attempts + 1 && !last_density_request);
 assert (!vulkan_globals.stereo_active && vid.width == 640 && vid.height == 480);
 for (int frame = 0; frame < 3; ++frame) GL_OpenXRAttach ();
 assert (attachments == rejected_attempts + 1);
 attachment_result = 1;
}

int main (void)
{
 preparation_cases ();
 messages = 0;
 enable_argument = "1";
 openxr_vulkan_binding = false;
 novr = 1;
 GL_OpenXREnable_f ();
 assert (!openxr_session_change_pending && !retry_queries && messages == 1);
 novr = 0;
 openxr_vulkan_binding = true;
 stop_reason = VRXR_STOP_SESSION_LOST;
 GL_OpenXREnable_f ();
 assert (openxr_session_change_pending && !retry_queries);
 retry_available = 1;
 GL_OpenXREnable_f ();
 assert (openxr_session_wanted && openxr_session_change_pending);
 // Command merely schedules attachment; it does not activate stereo.
 assert (!vulkan_globals.stereo_active);
 openxr_session_wanted = openxr_session_change_pending = false;
 stop_reason = VRXR_STOP_EXITING;
 GL_OpenXREnable_f ();
 assert (openxr_session_wanted && openxr_session_change_pending);
 openxr_session_wanted = openxr_session_change_pending = false;
 stop_reason = VRXR_STOP_INSTANCE_LOST; retry_available = 0;
 GL_OpenXREnable_f ();
 assert (openxr_session_change_pending);
 stop_reason = VRXR_STOP_NONE;
 GL_OpenXREnable_f ();
 assert (openxr_session_wanted && openxr_session_change_pending);
 vulkan_globals.stereo_active = true;
 enable_argument = "0"; GL_OpenXREnable_f ();
 assert (!openxr_session_wanted && openxr_session_change_pending);
 enable_argument = "1"; GL_OpenXREnable_f ();
 assert (openxr_session_wanted && !openxr_session_change_pending);
 // Exercise the real command, frame-transition, attach and retirement owners.
 vulkan_globals.stereo_active = false; openxr_attach_attempted = true;
 vulkan_globals.openxr_multiview_available = true; vulkan_globals.device_idle = true;
 vulkan_globals.device_properties.limits.maxFramebufferWidth = 100;
 vulkan_globals.device_properties.limits.maxFramebufferHeight = 100;
 vid.width = 640; vid.height = 480;
 stop_reason = VRXR_STOP_EXITING; retry_available = 1;
 for (int frame = 0; frame < 3; ++frame) { GL_OpenXRApplySessionChange (); GL_OpenXRAttach (); }
 assert (!attachments && openxr_attach_attempted && !vulkan_globals.stereo_active);
 GL_OpenXREnable_f ();
 assert (openxr_session_change_pending && openxr_attach_attempted && !attachments);
 prev_end_rendering_task = 17;
 GL_OpenXRApplySessionChange ();
 assert (!openxr_attach_attempted && !openxr_session_change_pending && joins == 1);
 GL_OpenXRAttach ();
 assert (attachments == 1 && vulkan_globals.stereo_active && vid.width == 24 && vid.height == 40);
 openxr_frame.sample_id = 12; openxr_frame.focused = true;
 stop_reason = VRXR_STOP_EXITING; retire_callback (NULL);
 assert (!vulkan_globals.stereo_active && !openxr_frame.sample_id && !openxr_frame.focused);
 assert (reference_invalidations == 1 && restored_views == 1 && vid.width == 640 && vid.height == 480);
 for (int frame = 0; frame < 3; ++frame) { GL_OpenXRApplySessionChange (); GL_OpenXRAttach (); }
 assert (attachments == 1);
 GL_OpenXREnable_f (); GL_OpenXRApplySessionChange (); GL_OpenXRAttach ();
 assert (attachments == 2 && vulkan_globals.stereo_active);
 enable_argument = "0"; GL_OpenXREnable_f (); GL_OpenXRApplySessionChange (); GL_OpenXRAttach ();
 assert (input_releases == 1 && !vulkan_globals.stereo_active && attachments == 2);
 assert (reference_invalidations == 2 && vid.width == 640 && vid.height == 480);
 // Ordinary desktop/instance loss uses fresh adoption only at the frame owner.
 enable_argument = "1"; openxr_vulkan_binding = false; retry_available = 0;
 stop_reason = VRXR_STOP_INSTANCE_LOST; adoption_result = 0;
 GL_OpenXREnable_f (); assert (openxr_session_change_pending && !adoptions);
 GL_OpenXRApplySessionChange (); GL_OpenXRAttach ();
 assert (adoptions == 1 && attachments == 2 && !vulkan_globals.stereo_active);
 for (int frame = 0; frame < 3; ++frame) GL_OpenXRAttach ();
 assert (adoptions == 1);
 adoption_result = 1; GL_OpenXREnable_f (); GL_OpenXRApplySessionChange (); GL_OpenXRAttach ();
 assert (adoptions == 2 && attachments == 3 && registrations == 3 && vulkan_globals.stereo_active);
 // A desktop device prepared with FDM still attaches full-rate VR when the
 // newly discovered runtime lacks FB. The same device can request FDM after
 // a later runtime discovery that advertises it; never infer fixed mode.
 enable_argument = "0"; GL_OpenXREnable_f (); GL_OpenXRApplySessionChange ();
 assert (!vulkan_globals.stereo_active);
 vulkan_globals.openxr_fragment_density_map_enabled = true;
 openxr_vulkan_binding = false; retry_available = 0; runtime_fb_supported = 0;
 enable_argument = "1"; GL_OpenXREnable_f (); GL_OpenXRApplySessionChange (); GL_OpenXRAttach ();
 assert (attachments == 4 && !last_density_request && vulkan_globals.stereo_active);
 enable_argument = "0"; GL_OpenXREnable_f (); GL_OpenXRApplySessionChange ();
 runtime_fb_supported = 1;
 enable_argument = "1"; GL_OpenXREnable_f (); GL_OpenXRApplySessionChange (); GL_OpenXRAttach ();
 assert (attachments == 5 && last_density_request && vulkan_globals.stereo_active);
 recovery_cases ();
 puts ("OPENXR_ENABLE_PASSED actual command/transition/attach/retirement; simulated runtime, camera/input and empty GPU resources");
 return 0;
}
