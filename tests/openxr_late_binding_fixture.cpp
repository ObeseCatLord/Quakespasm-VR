/* Reuse production-boundary creation/session/queue spies for original-Vulkan
 * adoption. Runtime discovery and driver/GPU execution require separate proof. */
#define VRXR_SESSION_RECOVERY_HELPERS_ONLY
#include "openxr_session_recovery_fixture.cpp"

namespace {
static const char *required_instance_names, *required_device_names;
static int instance_name_queries, device_name_queries, name_growth_queries;
static bool malformed_names, growing_names;
static bool endless_growth, zero_size, excessive_size;
static XrResult extension_query_result;
static XrResult legacy_names(const char *names, uint32_t capacity, uint32_t *count, char *buffer) {
 if(extension_query_result!=XR_SUCCESS) return extension_query_result;
 *count=uint32_t(std::strlen(names)+1);
 if(zero_size) *count=0;
 if(excessive_size) *count=2*1024*1024;
 if(!capacity) return XR_SUCCESS;
 if(endless_growth || (growing_names && ++name_growth_queries<=2)) { *count=capacity+1; return XR_ERROR_SIZE_INSUFFICIENT; }
 if(capacity<*count) return XR_ERROR_SIZE_INSUFFICIENT;
 std::memcpy(buffer,names,*count);
 if(malformed_names) buffer[*count-1]='x';
 return XR_SUCCESS;
}
static XrResult XRAPI_PTR legacy_instance_names(XrInstance, XrSystemId system,
 uint32_t capacity, uint32_t *count, char *buffer) {
 assert(system==7);++instance_name_queries;
 return legacy_names(required_instance_names,capacity,count,buffer);
}
static XrResult XRAPI_PTR legacy_device_names(XrInstance, XrSystemId system,
 uint32_t capacity, uint32_t *count, char *buffer) {
 assert(system==7);++device_name_queries;
 return legacy_names(required_device_names,capacity,count,buffer);
}
static XrResult XRAPI_PTR legacy_physical(XrInstance, XrSystemId system,
 VkInstance instance, VkPhysicalDevice *physical) {
 assert(system==7 && instance==fake_instance);*physical=offered_physical;return XR_SUCCESS;
}

}
namespace {
static bool loader_absent, legacy_absent;
static int discovery_creates;
static uint32_t physical_api;
static void VKAPI_PTR legacy_properties(VkPhysicalDevice selected, VkPhysicalDeviceProperties *properties) {
 assert(selected==fake_physical);*properties={};properties->apiVersion=physical_api;
}
static PFN_vkVoidFunction VKAPI_PTR legacy_proc(VkInstance instance, const char *name) {
 if(!std::strcmp(name,"vkGetPhysicalDeviceProperties")) return reinterpret_cast<PFN_vkVoidFunction>(legacy_properties);
 return recovery_proc(instance,name);
}
static XrResult XRAPI_PTR legacy_extensions(const char *,uint32_t capacity,uint32_t *count,XrExtensionProperties *properties) {
 *count=legacy_absent ? 0 : 1;
 if(capacity && *count) { std::strcpy(properties[0].extensionName,XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);properties[0].extensionVersion=XR_KHR_vulkan_enable_SPEC_VERSION; }
 return XR_SUCCESS;
}
static XrResult XRAPI_PTR legacy_create_instance(const XrInstanceCreateInfo *info, XrInstance *instance) {
 assert(info->enabledExtensionCount==1 && !std::strcmp(info->enabledExtensionNames[0],XR_KHR_VULKAN_ENABLE_EXTENSION_NAME));
 ++discovery_creates;*instance=reinterpret_cast<XrInstance>(1010 + discovery_creates);return XR_SUCCESS;
}
static XrResult XRAPI_PTR legacy_instance_properties(XrInstance, XrInstanceProperties *properties) {
 std::strcpy(properties->runtimeName,"Fixture runtime");properties->runtimeVersion=XR_MAKE_VERSION(1,0,0);return XR_SUCCESS;
}
static XrResult XRAPI_PTR legacy_system_properties(XrInstance,XrSystemId,XrSystemProperties *properties) {
 std::strcpy(properties->systemName,"Fixture system");return XR_SUCCESS;
}
static XrResult XRAPI_PTR legacy_result_string(XrInstance,XrResult,char *text) { std::strcpy(text,"fixture result");return XR_SUCCESS; }
static void XRAPI_PTR unexpected_call() { assert(!"unsupported fixture dispatch invoked"); }
static XrResult XRAPI_PTR legacy_gpa(XrInstance instance,const char *name,PFN_xrVoidFunction *function) {
#define MAP(text,fn) if(!std::strcmp(name,text)) { *function=reinterpret_cast<PFN_xrVoidFunction>(fn);return XR_SUCCESS; }
 MAP("xrCreateInstance",legacy_create_instance) MAP("xrEnumerateInstanceExtensionProperties",legacy_extensions)
 MAP("xrDestroyInstance",recovery_destroy_instance) MAP("xrGetInstanceProperties",legacy_instance_properties)
 MAP("xrGetSystem",recovery_get_system) MAP("xrGetSystemProperties",legacy_system_properties) MAP("xrResultToString",legacy_result_string)
 MAP("xrCreateSession",recovery_create_session) MAP("xrDestroySession",recovery_destroy_session)
 MAP("xrPollEvent",recovery_poll) MAP("xrBeginSession",recovery_begin_session) MAP("xrEndSession",recovery_end_session)
 MAP("xrCreateReferenceSpace",recovery_reference_space) MAP("xrDestroySpace",recovery_destroy_space) MAP("xrLocateSpace",locate_space)
 MAP("xrEnumerateViewConfigurationViews",stereo_config) MAP("xrEnumerateSwapchainFormats",formats)
 MAP("xrCreateSwapchain",create_array) MAP("xrDestroySwapchain",recovery_destroy_chain) MAP("xrEnumerateSwapchainImages",images)
 MAP("xrAcquireSwapchainImage",acquire_array) MAP("xrWaitSwapchainImage",wait_array) MAP("xrReleaseSwapchainImage",recovery_release)
 MAP("xrWaitFrame",recovery_wait) MAP("xrBeginFrame",queue_begin) MAP("xrEndFrame",recovery_end_frame) MAP("xrLocateViews",locate_views)
 MAP("xrStringToPath",recovery_path) MAP("xrCreateActionSet",recovery_action_set) MAP("xrDestroyActionSet",recovery_destroy_action_set)
 MAP("xrCreateAction",recovery_action) MAP("xrSuggestInteractionProfileBindings",recovery_suggest)
 MAP("xrAttachSessionActionSets",recovery_attach_actions) MAP("xrCreateActionSpace",recovery_action_space) MAP("xrSyncActions",sync_actions)
 MAP("xrGetVulkanGraphicsRequirementsKHR",recovery_requirements) MAP("xrGetVulkanInstanceExtensionsKHR",legacy_instance_names)
 MAP("xrGetVulkanDeviceExtensionsKHR",legacy_device_names) MAP("xrGetVulkanGraphicsDeviceKHR",legacy_physical)
#undef MAP
 if(!std::strcmp(name,"xrEnumerateEnvironmentBlendModes")) return recovery_gpa(instance,name,function);
 *function=unexpected_call;return XR_SUCCESS;
}
static void late_setup(bool direct_desktop=false) {
 recovery_reset();g=State();
 // Same actual create boundary, with desktop-selected handles and no XR owner.
 VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
 const char *names[]={"VK_FAKE_required_extra"};
 VkInstanceCreateInfo info={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;
 info.enabledExtensionCount=direct_desktop ? 1 : 0;info.ppEnabledExtensionNames=names;
 {
  VkInstance instance;assert(driver_create_instance(&info,nullptr,&instance)==VK_SUCCESS);
  VRXR_RecordVulkanInstance(legacy_proc,&info,instance);
  float priority=1;VkDeviceQueueCreateInfo queue={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  queue.queueFamilyIndex=3;queue.queueCount=1;queue.pQueuePriorities=&priority;
  VkPhysicalDeviceMultiviewFeatures multiview={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};multiview.multiview=VK_TRUE;
  VkDeviceCreateInfo device_info={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};device_info.pNext=&multiview;device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue;
  VkDevice device;assert(driver_create_device(fake_physical,&device_info,nullptr,&device)==VK_SUCCESS);
  VRXR_RecordVulkanDevice(fake_physical,&device_info,device);
 }
 loader_absent=legacy_absent=false;required_instance_names=required_device_names="";
 instance_name_queries=device_name_queries=name_growth_queries=discovery_creates=0;
 malformed_names=growing_names=false;extension_query_result=XR_SUCCESS;physical_api=VK_API_VERSION_1_1;
 endless_growth=zero_size=excessive_size=false;
 instance_loss_event=event_pending=false;
}
static bool adopt() { return VRXR_AdoptVulkan(nullptr,fake_instance,fake_physical,fake_device)!=0; }
}
/* Exercise real backend loader/discovery dispatch with a simulated runtime. */
extern "C" SDL_SharedObject *SDLCALL SDL_LoadObject(const char *) {
 return loader_absent ? nullptr : reinterpret_cast<SDL_SharedObject *>(1);
}
extern "C" SDL_FunctionPointer SDLCALL SDL_LoadFunction(SDL_SharedObject *,const char *name) {
 assert(!std::strcmp(name,"xrGetInstanceProcAddr"));return reinterpret_cast<SDL_FunctionPointer>(legacy_gpa);
}
extern "C" void SDLCALL SDL_UnloadObject(SDL_SharedObject *) {}

int main() {
 late_setup(true);loader_absent=true;assert(!adopt() && !g.instance && g_creation.device==fake_device && !discovery_creates);
 loader_absent=false;assert(adopt() && g.legacyVulkan && g_creation.device==fake_device);
 assert(!VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0)); // registration lost with XR teardown
 assert(VRXR_SetVulkanQueueCallbacks(queue_lock,queue_unlock,&queue_lock_depth));
 assert(VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0));recovered_frame();
 const uint64_t previous_sample=g_sample_id;const VkDevice kept_device=g_creation.device;
 instance_loss_event=event_pending=true;
 vrxr_frame_t sample;assert(VRXR_BeginFrame(&sample)==-1 && VRXR_StopReason()==VRXR_STOP_INSTANCE_LOST);
 assert(!g.instance && !VRXR_VulkanRetryAvailable() && g_creation.device==kept_device);
 assert(adopt() && VRXR_SetVulkanQueueCallbacks(queue_lock,queue_unlock,&queue_lock_depth));
 assert(VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0));recovered_frame();
 assert(g_sample_id>previous_sample && g_creation.device==kept_device && submitted_layers==1);
 VRXR_Shutdown();
 late_setup();legacy_absent=true;assert(!adopt() && !g.instance && g_creation.device==fake_device);
 late_setup(true);required_instance_names="VK_FAKE_required";assert(!adopt() && !g.instance); // exact names, not prefix
 late_setup(true);required_instance_names="  VK_FAKE_required_extra  ";growing_names=true;assert(adopt());VRXR_Shutdown();
 late_setup();malformed_names=true;assert(!adopt() && !g.instance);
 late_setup();endless_growth=true;assert(!adopt() && !g.instance);
 late_setup();zero_size=true;assert(!adopt() && !g.instance);
 late_setup();excessive_size=true;assert(!adopt() && !g.instance);
 late_setup();required_instance_names="VK_FAKE_bad\tname";assert(!adopt() && !g.instance);
 late_setup();required_device_names="VK_FAKE_unenabled";assert(!adopt() && !g.instance);
 late_setup();extension_query_result=XR_ERROR_RUNTIME_FAILURE;assert(!adopt() && !g.instance);
 late_setup();offered_physical=reinterpret_cast<VkPhysicalDevice>(999);assert(!adopt() && !g.instance);
 late_setup();minimum_api=XR_MAKE_VERSION(1,2,0);assert(!adopt() && !g.instance);
 late_setup();minimum_api=XR_MAKE_VERSION(1,1,0);physical_api=VK_API_VERSION_1_0;assert(!adopt() && !g.instance);
 late_setup();assert(!VRXR_AdoptVulkan(nullptr,reinterpret_cast<VkInstance>(99),fake_physical,fake_device));assert(!g.instance);
 // Handles genuinely created by enable2 can subsequently qualify original
 // Vulkan on a fresh XR instance, without relabeling that instance as enable2.
 recovery_setup();VRXR_Shutdown();
 assert(adopt() && g.legacyVulkan && VRXR_SetVulkanQueueCallbacks(queue_lock,queue_unlock,&queue_lock_depth));
 assert(VRXR_AttachVulkan(3,0,0,2,recovery_retire,nullptr,0,0));recovered_frame();VRXR_Shutdown();
 puts("OPENXR_LATE_BINDING_PASSED actual discovery/qualification/session/new frame/loss/rediscovery; simulated XR/driver, retained handle identities");
 VRXR_ForgetVulkanCreation();return 0;
}
