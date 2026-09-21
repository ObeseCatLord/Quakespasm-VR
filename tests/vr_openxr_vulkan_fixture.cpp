/* Production-boundary ownership and creation-result fault cases.
 * Ported from the OpenXR branch at 3080841333fa94000df7e1fb9e549c7158685dd6. */
#include <cassert>
#include "../Quake/vr_openxr.cpp"
namespace {
static XrResult xr_result;
static VkResult vk_result;
static int instance_calls, device_calls, retirement, swapchain_destroys;
static int queue_lock_depth, queue_locks, queue_unlocks, error_logs;
static VkInstance fake_instance=(VkInstance)(uintptr_t)101;
static VkPhysicalDevice fake_physical=(VkPhysicalDevice)(uintptr_t)102;
static VkDevice fake_device=(VkDevice)(uintptr_t)103;
static XrResult XRAPI_PTR create_instance(XrInstance,const XrVulkanInstanceCreateInfoKHR *info,VkInstance *out,VkResult *result) {
 assert(info->systemId==7 && !info->vulkanAllocator); ++instance_calls; *result=vk_result;
 *out=vk_result==VK_SUCCESS ? fake_instance : VK_NULL_HANDLE; return xr_result;
}
static XrResult XRAPI_PTR get_physical(XrInstance,const XrVulkanGraphicsDeviceGetInfoKHR *info,VkPhysicalDevice *out) {
 assert(info->vulkanInstance==fake_instance); *out=fake_physical; return XR_SUCCESS;
}
static XrResult XRAPI_PTR create_device(XrInstance,const XrVulkanDeviceCreateInfoKHR *info,VkDevice *out,VkResult *result) {
 assert(info->vulkanPhysicalDevice==fake_physical && !info->vulkanAllocator); ++device_calls; *result=vk_result;
 *out=vk_result==VK_SUCCESS ? fake_device : VK_NULL_HANDLE; return xr_result;
}
static PFN_vkVoidFunction VKAPI_PTR unused_proc(VkInstance,const char *) { return 0; }
static int queue_proc_calls;
static PFN_vkVoidFunction VKAPI_PTR counting_proc(VkInstance,const char *) { ++queue_proc_calls;return 0; }
static XrResult XRAPI_PTR no_events(XrInstance,XrEventDataBuffer *) { return XR_EVENT_UNAVAILABLE; }
static void queue_lock(void *owner) { assert(owner==&queue_lock_depth && !queue_lock_depth);++queue_lock_depth;++queue_locks; }
static void queue_unlock(void *owner) { assert(owner==&queue_lock_depth && queue_lock_depth==1);--queue_lock_depth;++queue_unlocks; }
static void log_error(const char *) { assert(!queue_lock_depth);++error_logs; }
static void retire(void *owner) { assert(owner==&retirement && !queue_lock_depth);assert(!swapchain_destroys);++retirement; }
static XrResult XRAPI_PTR destroy_chain(XrSwapchain) {assert(retirement==1);++swapchain_destroys;return XR_SUCCESS;}
static int array_creates, acquires, releases, submitted_layers;
static bool expect_retirement;
static XrResult XRAPI_PTR stereo_config(XrInstance,XrSystemId,XrViewConfigurationType,uint32_t capacity,uint32_t *count,XrViewConfigurationView *views) {
 *count=2;
 if(capacity) for(int i=0;i<2;++i) {
  views[i].recommendedImageRectWidth=20+i*4;views[i].recommendedImageRectHeight=30+i*10;
 }
 return XR_SUCCESS;
}
static XrResult XRAPI_PTR formats(XrSession,uint32_t capacity,uint32_t *count,int64_t *values) {
 *count=1;if(capacity) values[0]=VK_FORMAT_R8G8B8A8_SRGB;return XR_SUCCESS;
}
static XrResult XRAPI_PTR create_array(XrSession,const XrSwapchainCreateInfo *info,XrSwapchain *out) {
 assert(info->arraySize==2 && info->width==24 && info->height==40 && info->sampleCount==1);
 ++array_creates;*out=(XrSwapchain)(uintptr_t)11;return XR_SUCCESS;
}
static XrResult XRAPI_PTR images(XrSwapchain,uint32_t capacity,uint32_t *count,XrSwapchainImageBaseHeader *base) {
 *count=3;
 if(capacity) for(int i=0;i<3;++i) reinterpret_cast<XrSwapchainImageVulkan2KHR *>(base)[i].image=(VkImage)(uintptr_t)(200+i);
 return XR_SUCCESS;
}
static XrResult array_result, release_result, end_result;
static XrResult XRAPI_PTR acquire_array(XrSwapchain handle,const XrSwapchainImageAcquireInfo *,uint32_t *index) {
 assert(handle==(XrSwapchain)(uintptr_t)11 && queue_lock_depth==1);++acquires;*index=2;return XR_SUCCESS;
}
static XrResult XRAPI_PTR queue_acquire(XrSwapchain,const XrSwapchainImageAcquireInfo *,uint32_t *index) { assert(queue_lock_depth==1);*index=0;return array_result; }
static XrResult XRAPI_PTR wait_array(XrSwapchain,const XrSwapchainImageWaitInfo *) { assert(!queue_lock_depth);return XR_SUCCESS; }
static XrResult XRAPI_PTR release_array(XrSwapchain handle,const XrSwapchainImageReleaseInfo *) {
 assert(handle==(XrSwapchain)(uintptr_t)11 && queue_lock_depth==1);
 if(expect_retirement) assert(retirement==1);
 ++releases;return XR_SUCCESS;
}
static XrResult XRAPI_PTR queue_release(XrSwapchain,const XrSwapchainImageReleaseInfo *) { assert(queue_lock_depth==1);return release_result; }
static XrResult XRAPI_PTR end_array(XrSession,const XrFrameEndInfo *info) {
 assert(queue_lock_depth==1);
 if(expect_retirement) assert(retirement==1);
 submitted_layers=info->layerCount;
 if(info->layerCount) {
  const auto *projection=reinterpret_cast<const XrCompositionLayerProjection *>(info->layers[0]);
  assert(projection->viewCount==2);
  for(int i=0;i<2;++i) {
   const auto &sub=projection->views[i].subImage;
   assert(sub.swapchain==(XrSwapchain)(uintptr_t)11 && sub.imageArrayIndex==(uint32_t)i);
   assert(sub.imageRect.extent.width==24 && sub.imageRect.extent.height==40);
  }
 }
 return XR_SUCCESS;
}
static XrResult XRAPI_PTR queue_end(XrSession,const XrFrameEndInfo *) { assert(queue_lock_depth==1);return end_result; }
static XrResult XRAPI_PTR queue_wait_frame(XrSession,const XrFrameWaitInfo *,XrFrameState *) { assert(!queue_lock_depth);return XR_SUCCESS; }
static XrResult XRAPI_PTR queue_begin(XrSession,const XrFrameBeginInfo *) { assert(queue_lock_depth==1);return array_result; }
static void reset() {
 g=State();g.useVulkan=true;g.instance=(XrInstance)(uintptr_t)1;g.system=7;
 g.vk.requirements.minApiVersionSupported=XR_MAKE_VERSION(1,0,0);
 g.vk.requirements.maxApiVersionSupported=XR_MAKE_VERSION(1,3,0);
 g.xr.CreateVulkanInstance=create_instance;g.xr.VulkanGraphicsDevice=get_physical;g.xr.CreateVulkanDevice=create_device;
 g.xr.PollEvent=no_events;expect_retirement=false;
 xr_result=XR_SUCCESS;vk_result=VK_SUCCESS;array_result=release_result=end_result=XR_SUCCESS;
 instance_calls=device_calls=retirement=swapchain_destroys=queue_lock_depth=queue_locks=queue_unlocks=error_logs=queue_proc_calls=0;
}
}
int main() {
 VkApplicationInfo application={VK_STRUCTURE_TYPE_APPLICATION_INFO};application.apiVersion=VK_API_VERSION_1_1;
 VkInstanceCreateInfo instance_info={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};instance_info.pApplicationInfo=&application;
 VkInstance instance;
 // Runtime requirements ignore patches; the maximum is advisory.
 reset(); application.apiVersion=VK_API_VERSION_1_3+1;
 assert(VRXR_CreateVulkanInstance(unused_proc,&instance_info,&instance));assert(instance_calls==1);
 reset(); application.apiVersion=VK_MAKE_API_VERSION(0,1,4,0);
 assert(VRXR_CreateVulkanInstance(unused_proc,&instance_info,&instance));assert(instance_calls==1);
 reset(); g.vk.requirements.minApiVersionSupported=XR_MAKE_VERSION(1,1,99);
 application.apiVersion=VK_API_VERSION_1_1;
 assert(VRXR_CreateVulkanInstance(unused_proc,&instance_info,&instance));
 assert(vulkan_version(g.vk.requirements.minApiVersionSupported)==VK_API_VERSION_1_1);
 reset(); g.vk.requirements.minApiVersionSupported=XR_MAKE_VERSION(1,2,0);
 assert(!VRXR_CreateVulkanInstance(unused_proc,&instance_info,&instance));assert(!instance_calls);
 reset(); application.apiVersion=VK_MAKE_API_VERSION(1,1,1,0);
 assert(!VRXR_CreateVulkanInstance(unused_proc,&instance_info,&instance));assert(!instance_calls);
 reset(); application.apiVersion=VK_API_VERSION_1_1;
 xr_result=XR_ERROR_RUNTIME_FAILURE;
 assert(!VRXR_CreateVulkanInstance(unused_proc,&instance_info,&instance));assert(!g.vk.instance);
 assert(instance==fake_instance); // caller can clean a created handle even on XR failure
 xr_result=XR_SUCCESS;vk_result=VK_ERROR_OUT_OF_DEVICE_MEMORY;
 assert(!VRXR_CreateVulkanInstance(unused_proc,&instance_info,&instance));assert(!instance);
 vk_result=VK_SUCCESS;assert(VRXR_CreateVulkanInstance(unused_proc,&instance_info,&instance));
 assert(instance==fake_instance);assert(!VRXR_VulkanPhysicalDevice((VkInstance)(uintptr_t)999));
 assert(VRXR_VulkanPhysicalDevice(instance)==fake_physical);
 float priority=1;VkDeviceQueueCreateInfo queue={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
 queue.queueFamilyIndex=3;queue.queueCount=1;queue.pQueuePriorities=&priority;
 VkDeviceCreateInfo device_info={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue;
 VkDevice device;vk_result=VK_ERROR_FEATURE_NOT_PRESENT;
 assert(!VRXR_CreateVulkanDevice(&device_info,&device));assert(!g.vk.device);
 vk_result=VK_SUCCESS;assert(VRXR_CreateVulkanDevice(&device_info,&device));assert(g.vk.queues.size()==1);
 assert(!VRXR_SetVulkanQueueCallbacks(queue_lock,0,&queue_lock_depth));
 assert(!VRXR_SetVulkanQueueCallbacks(0,queue_unlock,&queue_lock_depth));
 assert(VRXR_SetVulkanQueueCallbacks(queue_lock,queue_unlock,&queue_lock_depth));
 VRXR_DetachVulkan();
 assert(g.vk.lockQueue==queue_lock && g.vk.unlockQueue==queue_unlock);
 g.session=(XrSession)(uintptr_t)9;
 assert(!VRXR_SetVulkanQueueCallbacks(0,0,0));
 assert(g.vk.lockQueue==queue_lock && g.vk.unlockQueue==queue_unlock);
 g.session=XR_NULL_HANDLE;
 assert(VRXR_SetVulkanQueueCallbacks(0,0,0));
 g.vk.getProc=counting_proc;
 // An otherwise valid queue makes the missing-callback check discriminating.
 assert(!VRXR_AttachVulkan(3,0,0,1,retire,&retirement,0,0));assert(!g.vk.retireImages);
 assert(!queue_proc_calls);
 assert(VRXR_SetVulkanQueueCallbacks(queue_lock,queue_unlock,&queue_lock_depth));
 assert(!VRXR_AttachVulkan(2,0,0,1,retire,&retirement,0,0));assert(!g.vk.retireImages);
 assert(!VRXR_AttachVulkan(3,1,0,1,retire,&retirement,0,0));assert(!g.vk.retireImages);
 assert(!queue_proc_calls);
 g.frameBegun=g.shouldRender=true;g.vk.format=VK_FORMAT_R8G8B8A8_SRGB;
 for(int eye=0;eye<2;++eye) {
  Chain &chain=g.chain[eye];chain.acquired=chain.waited=true;chain.width=20+eye;chain.height=30;
  chain.index=eye ? 0 : 2;chain.vulkanImages.resize(3);
  chain.vulkanImages[chain.index].image=(VkImage)(uintptr_t)(200+eye);
 }
 vrxr_vulkan_eye_t left,right;
 assert(VRXR_GetVulkanEye(0,&left)&&VRXR_GetVulkanEye(1,&right));
 assert(left.index==2 && right.index==0 && left.width==20 && right.width==21);
 assert(VRXR_VulkanEyeSubmitted(1));assert(!VRXR_GetVulkanEye(1,&right));assert(!VRXR_VulkanEyeSubmitted(1));
 assert(!g.chain[0].copiedMask);g.chain[0].waited=false;assert(!VRXR_GetVulkanEye(0,&left));
 g.frameBegun=false;g.chain[0].handle=(XrSwapchain)(uintptr_t)11;g.chain[1].handle=(XrSwapchain)(uintptr_t)12;
 g.vk.retireImages=retire;g.vk.owner=&retirement;g.xr.DestroySwapchain=destroy_chain;
 VRXR_Shutdown();assert(retirement==1 && swapchain_destroys==2 && !g.vk.lockQueue && !g.vk.unlockQueue);VRXR_Shutdown();assert(retirement==1);
 reset();g.log=log_error;g.initialized=g.sessionRunning=true;g.session=(XrSession)(uintptr_t)9;
 assert(VRXR_SetVulkanQueueCallbacks(queue_lock,queue_unlock,&queue_lock_depth)==0);
 g.session=XR_NULL_HANDLE;assert(VRXR_SetVulkanQueueCallbacks(queue_lock,queue_unlock,&queue_lock_depth));g.session=(XrSession)(uintptr_t)9;
 g.xr.WaitFrame=queue_wait_frame;g.xr.BeginFrame=queue_begin;array_result=XR_ERROR_RUNTIME_FAILURE;
 vrxr_frame_t frame;assert(VRXR_BeginFrame(&frame)==-1);assert(!queue_lock_depth && queue_locks==queue_unlocks && error_logs==1);
 reset();g.log=log_error;g.session=(XrSession)(uintptr_t)9;assert(VRXR_SetVulkanQueueCallbacks(0,0,0)==0);
 g.session=XR_NULL_HANDLE;assert(VRXR_SetVulkanQueueCallbacks(queue_lock,queue_unlock,&queue_lock_depth));g.session=(XrSession)(uintptr_t)9;
 g.chain[0].handle=(XrSwapchain)(uintptr_t)11;g.xr.AcquireSwapchainImage=queue_acquire;g.xr.WaitSwapchainImage=wait_array;
 array_result=XR_ERROR_RUNTIME_FAILURE;assert(!begin_images());assert(g.terminal && !queue_lock_depth && queue_locks==queue_unlocks && error_logs==1);
 reset();g.log=log_error;assert(VRXR_SetVulkanQueueCallbacks(queue_lock,queue_unlock,&queue_lock_depth));g.session=(XrSession)(uintptr_t)9;
 g.chain[0].handle=(XrSwapchain)(uintptr_t)11;g.chain[0].acquired=g.chain[0].waited=true;g.xr.ReleaseSwapchainImage=queue_release;g.xr.EndFrame=queue_end;
 release_result=end_result=XR_ERROR_RUNTIME_FAILURE;end_frame(false);assert(g.terminal && !queue_lock_depth && queue_locks==queue_unlocks && error_logs==2);
 reset();g.log=log_error;assert(VRXR_SetVulkanQueueCallbacks(queue_lock,queue_unlock,&queue_lock_depth));g.session=(XrSession)(uintptr_t)9;
 g.chain[0].handle=(XrSwapchain)(uintptr_t)11;g.chain[0].acquired=g.chain[0].waited=true;g.xr.ReleaseSwapchainImage=queue_release;g.xr.EndFrame=queue_end;
 array_result=XR_SUCCESS;end_frame(false);assert(!queue_lock_depth && queue_locks==queue_unlocks && !error_logs);
 g.frameBegun=true;g.chain[0].acquired=g.chain[0].waited=true;end_result=XR_ERROR_RUNTIME_FAILURE;end_frame(false);assert(g.terminal && !queue_lock_depth && queue_locks==queue_unlocks && error_logs==1);
 reset();g.vk.arrayLayers=2;assert(VRXR_SetVulkanQueueCallbacks(queue_lock,queue_unlock,&queue_lock_depth));g.session=(XrSession)(uintptr_t)9;g.initialized=true;
 g.xr.EnumerateViewConfigurationViews=stereo_config;g.xr.EnumerateSwapchainFormats=formats;
 g.xr.CreateSwapchain=create_array;g.xr.EnumerateSwapchainImages=images;
 g.xr.AcquireSwapchainImage=acquire_array;g.xr.WaitSwapchainImage=wait_array;
 g.xr.ReleaseSwapchainImage=release_array;g.xr.EndFrame=end_array;
 array_creates=acquires=releases=submitted_layers=0;
 assert(create_swapchains() && array_creates==1 && !g.chain[1].handle);
 unsigned width,height;assert(VRXR_GetViewSize(1,&width,&height) && width==24 && height==40);
 assert(begin_images() && acquires==1);
 g.frameBegun=g.shouldRender=true;
 assert(VRXR_GetVulkanEye(0,&left) && VRXR_GetVulkanEye(1,&right));
 assert(left.image==right.image && left.index==right.index && left.index==2);
 assert(left.array_layers==2 && right.array_layers==2 && left.array_layer==0 && right.array_layer==1);
 assert(VRXR_VulkanEyeSubmitted(0));assert(!VRXR_VulkanEyeSubmitted(0));
 assert(VRXR_GetVulkanEye(1,&right));
 VRXR_EndFrame();assert(releases==1 && submitted_layers==0); // missing right eye never submits a layer
 assert(begin_images() && acquires==2);g.frameBegun=g.shouldRender=true;
 assert(VRXR_VulkanEyeSubmitted(0) && VRXR_VulkanEyeSubmitted(1));
 VRXR_EndFrame();assert(releases==2 && submitted_layers==1);
 // Detach a begun frame: retire unlocked before release/end take the still-live
 // queue callbacks, then destroy the image and retain the queue registration.
 assert(begin_images() && acquires==3);g.frameBegun=g.shouldRender=true;
 g.vk.retireImages=retire;g.vk.owner=&retirement;g.xr.DestroySwapchain=destroy_chain;
 expect_retirement=true;
 VRXR_DetachVulkan();
 assert(retirement==1 && swapchain_destroys==1 && releases==3 && submitted_layers==0);
 assert(!queue_lock_depth && queue_locks==queue_unlocks && queue_locks==9);
 assert(g.vk.lockQueue==queue_lock && g.vk.unlockQueue==queue_unlock && g.vk.queueOwner==&queue_lock_depth);
 assert(!g.session && !g.frameBegun);
 VRXR_Shutdown();assert(retirement==1 && !g.vk.lockQueue && !g.vk.unlockQueue && !g.vk.queueOwner);
 puts("OpenXR Vulkan boundary: creation/version/provenance, image ownership, queue locking, failure unwind and begun-frame retirement passed");
}
