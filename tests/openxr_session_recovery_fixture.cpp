/* Actual backend session creation, event/frame teardown and explicit retry.
 * Runtime/Vulkan dispatch is simulated; no renderer tasks, GPU or HMD run.
 * Existing creation/provenance and image/queue fixture helpers are reused. */
#define main ImportedVulkanBoundaryMain
#include "vr_openxr_vulkan_fixture.cpp"
#undef main

namespace {
static int session_creates, session_destroys, instance_destroys;
static int recovery_retirements, recovery_chain_destroys, system_queries, requirements_queries;
static XrSystemId offered_system;
static VkPhysicalDevice offered_physical;
static XrResult system_result, requirements_result, poll_result, frame_result;
static XrResult destroy_session_result, release_session_result, end_session_frame_result;
static XrVersion minimum_api;
static XrSessionState queued_state;
static XrSession event_session;
static bool instance_loss_event, event_pending;
static uintptr_t resource_id;

static XrResult XRAPI_PTR recovery_get_system(XrInstance, const XrSystemGetInfo *info, XrSystemId *system) {
 assert(info->formFactor==XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY);++system_queries;
 if(system_result==XR_SUCCESS) *system=offered_system;
 return system_result;
}
static XrResult XRAPI_PTR recovery_requirements(XrInstance, XrSystemId system, XrGraphicsRequirementsVulkanKHR *info) {
 assert(system==7);++requirements_queries;
 info->minApiVersionSupported=minimum_api;info->maxApiVersionSupported=XR_MAKE_VERSION(1,3,0);
 return requirements_result;
}
static XrResult XRAPI_PTR recovery_physical(XrInstance, const XrVulkanGraphicsDeviceGetInfoKHR *info, VkPhysicalDevice *physical) {
 assert(info->systemId==7 && info->vulkanInstance==fake_instance);
 *physical=offered_physical;return XR_SUCCESS;
}
static XrResult XRAPI_PTR recovery_poll(XrInstance, XrEventDataBuffer *event) {
 assert(!queue_lock_depth);
 if(poll_result!=XR_SUCCESS) return poll_result;
 if(!event_pending) return XR_EVENT_UNAVAILABLE;
 event_pending=false;
 if(instance_loss_event) event->type=XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING;
 else {
  auto *change=reinterpret_cast<XrEventDataSessionStateChanged *>(event);
  change->type=XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;change->session=event_session;change->state=queued_state;
 }
 return XR_SUCCESS;
}
static void queue_state(XrSessionState state, XrSession session=XR_NULL_HANDLE) {
 queued_state=state;event_session=session ? session : g.session;
 instance_loss_event=false;event_pending=true;
}
static XrResult XRAPI_PTR recovery_create_session(XrInstance, const XrSessionCreateInfo *info, XrSession *session) {
 const auto *binding=reinterpret_cast<const XrGraphicsBindingVulkan2KHR *>(info->next);
 assert(info->systemId==7 && binding && binding->instance==fake_instance &&
  binding->physicalDevice==fake_physical && binding->device==fake_device &&
  binding->queueFamilyIndex==3 && binding->queueIndex==0);
 ++session_creates;*session=reinterpret_cast<XrSession>(uintptr_t(500+session_creates));return XR_SUCCESS;
}
static XrResult XRAPI_PTR recovery_destroy_session(XrSession) {
 assert(!queue_lock_depth && recovery_retirements>0 && recovery_chain_destroys==session_creates);
 ++session_destroys;return destroy_session_result;
}
static XrResult XRAPI_PTR recovery_destroy_instance(XrInstance) { ++instance_destroys;return XR_SUCCESS; }
static void recovery_retire(void *) { assert(!queue_lock_depth);++recovery_retirements; }
static XrResult XRAPI_PTR recovery_destroy_chain(XrSwapchain) {
 assert(recovery_retirements==session_creates);++recovery_chain_destroys;return XR_SUCCESS;
}
static XrResult XRAPI_PTR recovery_blend(XrInstance, XrSystemId, XrViewConfigurationType,
 uint32_t capacity, uint32_t *count, XrEnvironmentBlendMode *modes) {
 *count=1;if(capacity) modes[0]=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;return XR_SUCCESS;
}
static XrResult XRAPI_PTR recovery_gpa(XrInstance, const char *name, PFN_xrVoidFunction *function) {
 if(!std::strcmp(name,"xrEnumerateEnvironmentBlendModes")) {
  *function=reinterpret_cast<PFN_xrVoidFunction>(recovery_blend);return XR_SUCCESS;
 }
 return XR_ERROR_FUNCTION_UNSUPPORTED;
}
static XrResult XRAPI_PTR recovery_reference_space(XrSession, const XrReferenceSpaceCreateInfo *, XrSpace *space) {
 *space=reinterpret_cast<XrSpace>(++resource_id);return XR_SUCCESS;
}
static XrResult XRAPI_PTR recovery_destroy_space(XrSpace) { return XR_SUCCESS; }
static XrResult XRAPI_PTR recovery_path(XrInstance, const char *text, XrPath *path) {
 *path=!std::strcmp(text,"/user/hand/left") ? 1 : !std::strcmp(text,"/user/hand/right") ? 2 : ++resource_id;
 return XR_SUCCESS;
}
static XrResult XRAPI_PTR recovery_action_set(XrInstance, const XrActionSetCreateInfo *, XrActionSet *set) {
 *set=reinterpret_cast<XrActionSet>(++resource_id);return XR_SUCCESS;
}
static XrResult XRAPI_PTR recovery_destroy_action_set(XrActionSet) { return XR_SUCCESS; }
static XrResult XRAPI_PTR recovery_action(XrActionSet, const XrActionCreateInfo *, XrAction *action) {
 *action=reinterpret_cast<XrAction>(++resource_id);return XR_SUCCESS;
}
static XrResult XRAPI_PTR recovery_suggest(XrInstance, const XrInteractionProfileSuggestedBinding *) { return XR_SUCCESS; }
static XrResult XRAPI_PTR recovery_attach_actions(XrSession, const XrSessionActionSetsAttachInfo *) { return XR_SUCCESS; }
static XrResult XRAPI_PTR recovery_action_space(XrSession, const XrActionSpaceCreateInfo *, XrSpace *space) {
 *space=reinterpret_cast<XrSpace>(++resource_id);return XR_SUCCESS;
}
static XrResult XRAPI_PTR recovery_begin_session(XrSession, const XrSessionBeginInfo *) { return XR_SUCCESS; }
static XrResult XRAPI_PTR recovery_end_session(XrSession) { return XR_SUCCESS; }
static XrResult XRAPI_PTR recovery_wait(XrSession, const XrFrameWaitInfo *, XrFrameState *state) {
 state->predictedDisplayTime=1;state->shouldRender=XR_TRUE;return frame_result;
}
static XrResult XRAPI_PTR recovery_release(XrSwapchain chain, const XrSwapchainImageReleaseInfo *info) {
 release_array(chain,info);return release_session_result;
}
static XrResult XRAPI_PTR recovery_end_frame(XrSession session, const XrFrameEndInfo *info) {
 end_array(session,info);return end_session_frame_result;
}
static void VKAPI_PTR recovery_families(VkPhysicalDevice, uint32_t *count, VkQueueFamilyProperties *properties) {
 *count=4;if(properties) { std::memset(properties,0,4*sizeof(*properties));properties[3].queueFlags=VK_QUEUE_GRAPHICS_BIT;properties[3].queueCount=1; }
}
static void VKAPI_PTR recovery_properties(VkPhysicalDevice, VkPhysicalDeviceProperties *properties) {
 *properties={};properties->apiVersion=VK_API_VERSION_1_1;
}
static PFN_vkVoidFunction VKAPI_PTR recovery_proc(VkInstance, const char *name) {
 if(!std::strcmp(name,"vkGetPhysicalDeviceQueueFamilyProperties")) return reinterpret_cast<PFN_vkVoidFunction>(recovery_families);
 if(!std::strcmp(name,"vkGetPhysicalDeviceProperties")) return reinterpret_cast<PFN_vkVoidFunction>(recovery_properties);
 return driver_proc(VK_NULL_HANDLE,name);
}
static void recovery_reset() {
 reset();resource_id=1000;offered_system=7;offered_physical=fake_physical;
 session_creates=session_destroys=instance_destroys=recovery_retirements=recovery_chain_destroys=system_queries=requirements_queries=0;
 system_result=requirements_result=poll_result=frame_result=XR_SUCCESS;
 destroy_session_result=release_session_result=end_session_frame_result=XR_SUCCESS;
 minimum_api=XR_MAKE_VERSION(1,0,0);instance_loss_event=event_pending=false;
}
static void recovery_setup() {
 recovery_reset();
 VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
 VkInstanceCreateInfo info={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;
 VkInstance instance;assert(VRXR_CreateVulkanInstance(recovery_proc,&info,&instance));
 assert(VRXR_VulkanPhysicalDevice(instance)==fake_physical);
 float priority=1;VkDeviceQueueCreateInfo queue={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
 queue.queueFamilyIndex=3;queue.queueCount=1;queue.pQueuePriorities=&priority;
 VkDeviceCreateInfo device={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};device.queueCreateInfoCount=1;device.pQueueCreateInfos=&queue;
 VkPhysicalDeviceMultiviewFeatures multiview={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
 multiview.multiview=VK_TRUE;device.pNext=&multiview;
 VkDevice handle;assert(VRXR_CreateVulkanDevice(&device,&handle));
 assert(VRXR_SetVulkanQueueCallbacks(queue_lock,queue_unlock,&queue_lock_depth));
 g.xr.GetSystem=recovery_get_system;g.xr.VulkanRequirements=recovery_requirements;g.xr.VulkanGraphicsDevice=recovery_physical;
 g.xr.PollEvent=recovery_poll;g.xr.CreateSession=recovery_create_session;g.xr.DestroySession=recovery_destroy_session;g.xr.DestroyInstance=recovery_destroy_instance;
 g.xr.gpa=recovery_gpa;g.xr.CreateReferenceSpace=recovery_reference_space;g.xr.DestroySpace=recovery_destroy_space;
 g.xr.StringToPath=recovery_path;g.xr.CreateActionSet=recovery_action_set;g.xr.DestroyActionSet=recovery_destroy_action_set;
 g.xr.CreateAction=recovery_action;g.xr.SuggestBindings=recovery_suggest;g.xr.AttachActionSets=recovery_attach_actions;g.xr.CreateActionSpace=recovery_action_space;
 g.xr.EnumerateViewConfigurationViews=stereo_config;g.xr.EnumerateSwapchainFormats=formats;g.xr.CreateSwapchain=create_array;g.xr.EnumerateSwapchainImages=images;
 g.xr.AcquireSwapchainImage=acquire_array;g.xr.WaitSwapchainImage=wait_array;g.xr.ReleaseSwapchainImage=recovery_release;g.xr.EndFrame=recovery_end_frame;
 g.xr.DestroySwapchain=recovery_destroy_chain;g.xr.BeginSession=recovery_begin_session;g.xr.EndSession=recovery_end_session;
 g.xr.WaitFrame=recovery_wait;g.xr.BeginFrame=queue_begin;g.xr.SyncActions=sync_actions;g.xr.LocateSpace=locate_space;g.xr.LocateViews=locate_views;
 assert(VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0));
}
static void recovered_frame() {
 queue_state(XR_SESSION_STATE_READY);
 vrxr_frame_t sample;assert(VRXR_BeginFrame(&sample)==1 && sample.sample_id && sample.should_render);
 assert(!sample.focused && !sample.hands[0].active && !sample.hands[1].active);
 assert(VRXR_VulkanEyeSubmitted(0) && VRXR_VulkanEyeSubmitted(1));VRXR_EndFrame();
 assert(!g.frameBegun && !queue_lock_depth && queue_locks==queue_unlocks);
}
}

#ifndef VRXR_SESSION_RECOVERY_HELPERS_ONLY
int main() {
 recovery_setup();recovered_frame();
 const XrSession old_session=g.session;
 queue_state(XR_SESSION_STATE_LOSS_PENDING);
 vrxr_frame_t sample;assert(VRXR_BeginFrame(&sample)==-1);
 assert(VRXR_StopReason()==VRXR_STOP_SESSION_LOST && VRXR_VulkanRetryAvailable());
 assert(!g.session && !g.initialized && !g.frameBegun && !g.terminal && !instance_destroys);
 assert(session_destroys==1 && recovery_retirements==1 && recovery_chain_destroys==1);
 assert(g.vk.instance==fake_instance && g.vk.device==fake_device && g.vk.lockQueue==queue_lock);
 for(int i=0;i<3;++i) assert(VRXR_BeginFrame(&sample)==-1 && session_creates==1);
 system_result=XR_ERROR_FORM_FACTOR_UNAVAILABLE;
 assert(!VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0) && !g.session && session_creates==1);
 system_result=XR_SUCCESS;offered_system=8;
 assert(!VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0) && g.system==7 && session_creates==1);
 offered_system=7;minimum_api=XR_MAKE_VERSION(1,2,0);
 assert(!VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0) && session_creates==1);
 minimum_api=XR_MAKE_VERSION(1,0,0);offered_physical=(VkPhysicalDevice)(uintptr_t)888;
 assert(!VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0) && g.vk.physicalDevice==fake_physical && session_creates==1);
 offered_physical=fake_physical;
 assert(VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0) && session_creates==2 && g.session!=old_session);
 queue_state(XR_SESSION_STATE_LOSS_PENDING,old_session);
 assert(VRXR_BeginFrame(&sample)==0 && !g.terminal); // stale session event ignored
 recovered_frame();
 queue_state(XR_SESSION_STATE_EXITING);assert(VRXR_BeginFrame(&sample)==-1);
 assert(VRXR_StopReason()==VRXR_STOP_EXITING && VRXR_VulkanRetryAvailable());
 for(int i=0;i<3;++i) assert(VRXR_BeginFrame(&sample)==-1 && session_creates==2);
 assert(VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0) && session_creates==3);
 recovered_frame();VRXR_Shutdown();assert(instance_destroys==1 && session_destroys==3 && !VRXR_VulkanRetryAvailable());

 recovery_setup();VRXR_DetachVulkan();
 assert(VRXR_StopReason()==VRXR_STOP_NONE && VRXR_VulkanRetryAvailable());
 offered_system=8;
 assert(!VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0) && session_creates==1 && g.system==7);
 offered_system=7;
 assert(VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0));recovered_frame();VRXR_Shutdown();

 for(int which=0;which<2;++which) {
  recovery_setup();queue_state(XR_SESSION_STATE_READY);
  assert(VRXR_BeginFrame(&sample)==1);
  assert(VRXR_VulkanEyeSubmitted(0) && VRXR_VulkanEyeSubmitted(1));
  if(which) end_session_frame_result=XR_SESSION_LOSS_PENDING;
  else release_session_result=XR_SESSION_LOSS_PENDING;
  VRXR_EndFrame();
  assert(VRXR_StopReason()==VRXR_STOP_SESSION_LOST && VRXR_VulkanRetryAvailable() && !g.session && !instance_destroys);
  assert(!queue_lock_depth && queue_locks==queue_unlocks);
  release_session_result=end_session_frame_result=XR_SUCCESS;
  assert(VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0));recovered_frame();VRXR_Shutdown();
 }

 recovery_setup();destroy_session_result=XR_ERROR_RUNTIME_FAILURE;
 queue_state(XR_SESSION_STATE_LOSS_PENDING);
 assert(VRXR_BeginFrame(&sample)==-1 && instance_destroys==1 && !VRXR_VulkanRetryAvailable());
 assert(VRXR_StopReason()==VRXR_STOP_SESSION_LOST);
 recovery_setup();destroy_session_result=XR_ERROR_RUNTIME_FAILURE;
 queue_state(XR_SESSION_STATE_EXITING);
 assert(VRXR_BeginFrame(&sample)==-1 && instance_destroys==1 && !VRXR_VulkanRetryAvailable());
 assert(VRXR_StopReason()==VRXR_STOP_EXITING);
 recovery_setup();destroy_session_result=XR_ERROR_RUNTIME_FAILURE;VRXR_DetachVulkan();
 assert(instance_destroys==1 && !VRXR_VulkanRetryAvailable() && VRXR_StopReason()==VRXR_STOP_FAILURE);

 for(int which=0;which<2;++which) {
  recovery_setup();queue_state(which ? XR_SESSION_STATE_LOSS_PENDING : XR_SESSION_STATE_EXITING);
  assert(VRXR_BeginFrame(&sample)==-1 && VRXR_VulkanRetryAvailable());
  poll_result=XR_ERROR_RUNTIME_FAILURE;
  assert(!VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0));
  assert(instance_destroys==1 && !VRXR_VulkanRetryAvailable() && !g.instance);
  assert(VRXR_StopReason()==VRXR_STOP_FAILURE);
 }

 recovery_setup();queue_state(XR_SESSION_STATE_READY);frame_result=XR_SESSION_LOSS_PENDING;
 assert(VRXR_BeginFrame(&sample)==-1 && VRXR_StopReason()==VRXR_STOP_SESSION_LOST && VRXR_VulkanRetryAvailable());
 frame_result=XR_SUCCESS;assert(VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0));recovered_frame();VRXR_Shutdown();

 recovery_setup();queue_state(XR_SESSION_STATE_READY);frame_result=XR_ERROR_SESSION_LOST;
 assert(VRXR_BeginFrame(&sample)==-1 && VRXR_StopReason()==VRXR_STOP_SESSION_LOST && VRXR_VulkanRetryAvailable());
 instance_loss_event=event_pending=true;
 assert(!VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0));
 assert(instance_destroys==1 && VRXR_StopReason()==VRXR_STOP_INSTANCE_LOST && !VRXR_VulkanRetryAvailable());

 recovery_setup();instance_loss_event=event_pending=true;
 assert(VRXR_BeginFrame(&sample)==-1 && instance_destroys==1 && !VRXR_VulkanRetryAvailable());
 assert(VRXR_StopReason()==VRXR_STOP_INSTANCE_LOST);
 puts("OPENXR_SESSION_RECOVERY_PASSED actual backend creation/event/explicit retry/new frame; simulated runtime/Vulkan dispatch");
}
#endif
