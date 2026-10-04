/* Actual enable2 callback capture, with simulated XR and either spied driver
 * dispatch or a real headless Vulkan device. No session/HMD/rendering proof. */
#include <cassert>
#include <thread>
#include "../Quake/vr_openxr.cpp"
namespace {
static XrResult wrapper_result;
static bool threaded, multiple_creates, multiple_instances, reused_device, bypass;
static bool use_cached_device, missing_instance_proc, missing_device_proc;
static bool shared_device_dispatch;
static bool uncaptured_device_output, unselected_device;
static PFN_vkCreateDevice cached_device;
static const char *instance_extra, *device_extra;
static uint32_t merged_api;
static int driver_instances, driver_devices;
static VkPhysicalDevice physical;
static VKAPI_ATTR VkResult VKAPI_CALL spy_instance(const VkInstanceCreateInfo *info, const VkAllocationCallbacks *allocator, VkInstance *out) {
 assert(!allocator && info->enabledExtensionCount==2);
 assert(!std::strcmp(info->ppEnabledExtensionNames[1],instance_extra));
 *out=reinterpret_cast<VkInstance>(uintptr_t(101 + driver_instances++));return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL spy_device(VkPhysicalDevice selected, const VkDeviceCreateInfo *info, const VkAllocationCallbacks *allocator, VkDevice *out) {
 assert(!allocator && selected==(unselected_device ? reinterpret_cast<VkPhysicalDevice>(302) : physical) && info->queueCreateInfoCount==2);
 assert(info->pQueueCreateInfos[1].queueFamilyIndex==7 && info->pQueueCreateInfos[1].flags==VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT);
 assert(info->enabledExtensionCount==2 && !std::strcmp(info->ppEnabledExtensionNames[1],device_extra));
 *out=reinterpret_cast<VkDevice>(uintptr_t(201 + (reused_device ? 0 : driver_devices)));++driver_devices;return VK_SUCCESS;
}
static void VKAPI_PTR arbitrary_proc() {}
static VKAPI_ATTR VkResult VKAPI_CALL unrelated_device(VkPhysicalDevice,const VkDeviceCreateInfo *,const VkAllocationCallbacks *,VkDevice *) {
 assert(!"foreign-instance lookup redirected pinned renderer dispatch");return VK_ERROR_INITIALIZATION_FAILED;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL spy_proc(VkInstance instance, const char *name) {
 if(!std::strcmp(name,"vkCreateInstance")) return missing_instance_proc ? nullptr : reinterpret_cast<PFN_vkVoidFunction>(spy_instance);
 if(!std::strcmp(name,"vkCreateDevice")) {
  if(missing_device_proc) return nullptr;
  return instance==reinterpret_cast<VkInstance>(999) && !shared_device_dispatch ? reinterpret_cast<PFN_vkVoidFunction>(unrelated_device) : reinterpret_cast<PFN_vkVoidFunction>(spy_device);
 }
 if(!std::strcmp(name,"vkGetInstanceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(spy_proc);
 return arbitrary_proc;
}
static XrResult XRAPI_PTR wrapped_instance(XrInstance, const XrVulkanInstanceCreateInfoKHR *info, VkInstance *out, VkResult *result) {
 assert(info->pfnGetInstanceProcAddr && !info->vulkanAllocator);
 if(bypass) { *out=reinterpret_cast<VkInstance>(99);*result=VK_SUCCESS;return wrapper_result; }
 VkInstanceCreateInfo merged=*info->vulkanCreateInfo;
 VkApplicationInfo application=*merged.pApplicationInfo;application.apiVersion=merged_api;merged.pApplicationInfo=&application;
 std::vector<const char *> names;
 for(uint32_t i=0;i<merged.enabledExtensionCount;++i) names.push_back(merged.ppEnabledExtensionNames[i]);
 if(instance_extra) names.push_back(instance_extra);
 merged.ppEnabledExtensionNames=names.data();merged.enabledExtensionCount=uint32_t(names.size());
 auto invoke=[&] {
  // Some runtimes retrieve getProc again through the supplied dispatch.
  auto getProc=reinterpret_cast<PFN_vkGetInstanceProcAddr>(info->pfnGetInstanceProcAddr(VK_NULL_HANDLE,"vkGetInstanceProcAddr"));
  assert(getProc);
  auto create=reinterpret_cast<PFN_vkCreateInstance>(getProc(VK_NULL_HANDLE,"vkCreateInstance"));assert(create);
  *result=create(&merged,info->vulkanAllocator,out);
  if(use_cached_device) {
   cached_device=reinterpret_cast<PFN_vkCreateDevice>(getProc(*out,"vkCreateDevice"));assert(cached_device);
   assert(getProc(reinterpret_cast<VkInstance>(999),"vkCreateDevice")==reinterpret_cast<PFN_vkVoidFunction>(unrelated_device));
  }
  if(multiple_instances) { VkInstance ignored;assert(create(&merged,nullptr,&ignored)==VK_SUCCESS); }
 };
 if(threaded) { std::thread worker(invoke);worker.join(); } else invoke();
 return wrapper_result;
}
static XrResult XRAPI_PTR wrapped_device(XrInstance, const XrVulkanDeviceCreateInfoKHR *info, VkDevice *out, VkResult *result) {
 VkDeviceCreateInfo merged=*info->vulkanCreateInfo;
 std::vector<const char *> names;
 for(uint32_t i=0;i<merged.enabledExtensionCount;++i) names.push_back(merged.ppEnabledExtensionNames[i]);
 if(device_extra) names.push_back(device_extra);
 merged.ppEnabledExtensionNames=names.data();merged.enabledExtensionCount=uint32_t(names.size());
 auto invoke=[&] {
  auto create=use_cached_device ? cached_device : reinterpret_cast<PFN_vkCreateDevice>(info->pfnGetInstanceProcAddr(
   shared_device_dispatch ? reinterpret_cast<VkInstance>(999) : g.vk.instance,"vkCreateDevice"));assert(create);
  *result=create(unselected_device ? reinterpret_cast<VkPhysicalDevice>(302) : info->vulkanPhysicalDevice,&merged,info->vulkanAllocator,out);
  if(uncaptured_device_output) *out=reinterpret_cast<VkDevice>(9999);
  if(multiple_creates) {
   VkDevice ignored;
   VkPhysicalDeviceMultiviewFeatures later={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
   if(reused_device) merged.pNext=&later; // different live facts under the recycled handle
   assert(create(info->vulkanPhysicalDevice,&merged,nullptr,&ignored)==VK_SUCCESS);
  }
 };
 if(threaded) { std::thread worker(invoke);worker.join(); } else invoke();
 return wrapper_result;
}
static XrResult XRAPI_PTR destroy_instance(XrInstance) { return XR_SUCCESS; }
static void setup() {
 VRXR_ForgetVulkanCreation();g=State();g.useVulkan=true;g.instance=reinterpret_cast<XrInstance>(1);g.system=7;
 g.vk.requirements.minApiVersionSupported=XR_MAKE_VERSION(1,1,0);
 g.vk.requirements.maxApiVersionSupported=XR_MAKE_VERSION(1,3,0);
 g.xr.CreateVulkanInstance=wrapped_instance;g.xr.CreateVulkanDevice=wrapped_device;g.xr.DestroyInstance=destroy_instance;
 wrapper_result=XR_SUCCESS;threaded=multiple_creates=multiple_instances=reused_device=bypass=false;driver_instances=driver_devices=0;
 use_cached_device=missing_instance_proc=missing_device_proc=false;cached_device=nullptr;
 shared_device_dispatch=false;
 uncaptured_device_output=unselected_device=false;
 instance_extra="VK_FAKE_runtime_instance";device_extra=VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME;merged_api=VK_API_VERSION_1_2;
 physical=reinterpret_cast<VkPhysicalDevice>(301);
}
static void spy_checks() {
 VkApplicationInfo application={VK_STRUCTURE_TYPE_APPLICATION_INFO};application.apiVersion=VK_API_VERSION_1_1;
 const char *instance_names[]={"VK_FAKE_app_instance"};
 VkInstanceCreateInfo info={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&application;info.enabledExtensionCount=1;info.ppEnabledExtensionNames=instance_names;
 setup();multiple_instances=true;
 VkInstance instance;assert(!VRXR_CreateVulkanInstance(spy_proc,&info,&instance));
 assert(instance && driver_instances==2 && !g_creation.instance && !g.vk.instance);
 setup();threaded=use_cached_device=true;
 assert(VRXR_CreateVulkanInstance(spy_proc,&info,&instance));
 assert(instance==reinterpret_cast<VkInstance>(101) && driver_instances==1);
 assert(g_creation.apiVersion==merged_api && g.vk.apiVersion==merged_api);
 assert(g_creation.instanceExtensions.size()==2 && g_creation.instanceExtensions[1]==instance_extra);
 assert(capture_proc(instance,"arbitrary")==arbitrary_proc);
 g.vk.physicalDevice=physical;
 float priority=1;VkDeviceQueueCreateInfo queues[2]={{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO},{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}};
 for(auto &queue:queues) { queue.queueCount=1;queue.pQueuePriorities=&priority; }
 queues[0].queueFamilyIndex=3;queues[1].queueFamilyIndex=7;queues[1].flags=VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT;
 VkPhysicalDeviceMultiviewFeatures multiview={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};multiview.multiview=VK_TRUE;
 VkPhysicalDeviceFragmentDensityMapFeaturesEXT density={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT};density.fragmentDensityMap=VK_TRUE;density.pNext=&multiview;
 const char *device_names[]={"VK_FAKE_app_device"};
 VkDeviceCreateInfo device_info={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};device_info.queueCreateInfoCount=2;device_info.pQueueCreateInfos=queues;device_info.enabledExtensionCount=1;device_info.ppEnabledExtensionNames=device_names;device_info.pNext=&density;
 VkDevice device;
 missing_device_proc=true;assert(!VRXR_CreateVulkanDevice(&device_info,&device));assert(!device && !driver_devices);
 missing_device_proc=false;
 // Successful creation cannot authorize a different returned handle or GPU.
 for(int negative=0;negative<2;++negative) {
  uncaptured_device_output=negative==0;unselected_device=negative==1;
  assert(!VRXR_CreateVulkanDevice(&device_info,&device));
  assert(device && !g.vk.device && !g_creation.device && !g_creation.physicalDevice);
  assert(g_creation.deviceExtensions.empty() && g_creation.queues.empty() && !g_creation.multiview && !g_creation.densityMap);
  assert(g.vk.physicalDevice==physical && g_creation.instance==instance && g_creation.apiVersion==merged_api);
 }
 uncaptured_device_output=unselected_device=false;
 wrapper_result=XR_ERROR_RUNTIME_FAILURE;assert(!VRXR_CreateVulkanDevice(&device_info,&device));
 assert(device && !g.vk.device && !g_creation.device && g_creation.deviceExtensions.empty());
 wrapper_result=XR_SUCCESS;assert(VRXR_CreateVulkanDevice(&device_info,&device));
 assert(g_creation.device==device && g_creation.deviceExtensions[1]==device_extra && g_creation.multiview && g_creation.densityMap);
 assert(g.vk.queues.size()==2 && g.vk.queues[1].flags==VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT);
 // SteamVR can resolve the same loader function using an internal instance.
 // Keep capture/output validation while preserving genuinely foreign dispatch.
 g.vk.device=VK_NULL_HANDLE;g_creation.device=VK_NULL_HANDLE;
 shared_device_dispatch=true;use_cached_device=false;
 assert(VRXR_CreateVulkanDevice(&device_info,&device));
 assert(g_creation.device==device && g_creation.physicalDevice==physical && g_creation.multiview && g_creation.densityMap);
 shared_device_dispatch=false;
 // Simulate a runtime replacing a destroyed device with a recycled handle;
 // distinguish the later create's features so selecting the old record fails.
 g.vk.device=VK_NULL_HANDLE;g_creation.device=VK_NULL_HANDLE;
 driver_devices=0;multiple_creates=reused_device=true;
 assert(VRXR_CreateVulkanDevice(&device_info,&device));
 assert(driver_devices==2 && device==reinterpret_cast<VkDevice>(201));
 assert(!g_creation.multiview && !g_creation.densityMap && !g.vk.fragmentDensityMapEnabled);
 VRXR_Shutdown();assert(!g.vk.device && g_creation.device==device && g_creation.apiVersion==merged_api);
 VRXR_ForgetVulkanCreation();assert(!g_creation.instance && !g_creation.device && g_creation.queues.empty());
 setup();wrapper_result=XR_ERROR_RUNTIME_FAILURE;assert(!VRXR_CreateVulkanInstance(spy_proc,&info,&instance));
 assert(instance && !g.vk.instance && !g_creation.instance && g_creation.instanceExtensions.empty());
 setup();bypass=true;assert(!VRXR_CreateVulkanInstance(spy_proc,&info,&instance));assert(instance && !g_creation.instance);
 setup();missing_instance_proc=true;assert(!VRXR_CreateVulkanInstance(spy_proc,&info,&instance));assert(!instance && !driver_instances && !g_creation.instance);
 setup();merged_api=VK_API_VERSION_1_0;assert(!VRXR_CreateVulkanInstance(spy_proc,&info,&instance));assert(instance && !g_creation.instance && !g.vk.instance);
 // Direct renderer creation keeps names/API/features without needing XR discovery.
 g=State();VRXR_RecordVulkanInstance(spy_proc,&info,instance);VRXR_RecordVulkanDevice(physical,&device_info,device);
 assert(g_creation.apiVersion==application.apiVersion && g_creation.instanceExtensions.size()==1 && g_creation.deviceExtensions.size()==1);
 assert(!g_creation.densityMap && g_creation.multiview && g_creation.queues.size()==2);
 VRXR_Shutdown();assert(g_creation.device==device);VRXR_ForgetVulkanCreation();
}
static bool has_instance_extension(const char *name) {
 uint32_t count=0;assert(vkEnumerateInstanceExtensionProperties(nullptr,&count,nullptr)==VK_SUCCESS);
 std::vector<VkExtensionProperties> extensions(count);assert(vkEnumerateInstanceExtensionProperties(nullptr,&count,extensions.data())==VK_SUCCESS);
 for(const auto &extension:extensions) if(!std::strcmp(name,extension.extensionName)) return true;
 return false;
}
static bool has_device_extension(VkPhysicalDevice selected,const char *name) {
 uint32_t count=0;assert(vkEnumerateDeviceExtensionProperties(selected,nullptr,&count,nullptr)==VK_SUCCESS);
 std::vector<VkExtensionProperties> extensions(count);assert(vkEnumerateDeviceExtensionProperties(selected,nullptr,&count,extensions.data())==VK_SUCCESS);
 for(const auto &extension:extensions) if(!std::strcmp(name,extension.extensionName)) return true;
 return false;
}
static int real_driver_checks() {
 setup();instance_extra=VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;device_extra=VK_KHR_MAINTENANCE_1_EXTENSION_NAME;merged_api=VK_API_VERSION_1_1;
 if(!has_instance_extension(instance_extra)) return 77;
 VkApplicationInfo application={VK_STRUCTURE_TYPE_APPLICATION_INFO};application.apiVersion=VK_API_VERSION_1_1;
 VkInstanceCreateInfo info={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&application;
 VkInstance instance;assert(VRXR_CreateVulkanInstance(vkGetInstanceProcAddr,&info,&instance));
 assert(g_creation.instance==instance && g_creation.instanceExtensions.size()==1 && g_creation.instanceExtensions[0]==instance_extra);
 uint32_t count=0;VkResult enumeration=vkEnumeratePhysicalDevices(instance,&count,nullptr);
 if(enumeration!=VK_SUCCESS) {
  VRXR_Shutdown();VRXR_ForgetVulkanCreation();vkDestroyInstance(instance,nullptr);
  puts("OPENXR_VULKAN_CREATION_REAL_SKIPPED no accessible Vulkan driver");return 77;
 }
 if(!count) { VRXR_Shutdown();VRXR_ForgetVulkanCreation();vkDestroyInstance(instance,nullptr);return 77; }
 std::vector<VkPhysicalDevice> devices(count);assert(vkEnumeratePhysicalDevices(instance,&count,devices.data())==VK_SUCCESS);
 physical=devices[0];g.vk.physicalDevice=physical;
 if(!has_device_extension(physical,device_extra)) { VRXR_Shutdown();VRXR_ForgetVulkanCreation();vkDestroyInstance(instance,nullptr);return 77; }
 vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);
 vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());uint32_t family=0;
 while(family<count && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)) ++family;
 assert(family<count);
 float priority=1;VkDeviceQueueCreateInfo queue={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue.queueFamilyIndex=family;queue.queueCount=1;queue.pQueuePriorities=&priority;
 VkPhysicalDeviceMultiviewFeatures multiview={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
 VkPhysicalDeviceFeatures2 features={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&multiview;vkGetPhysicalDeviceFeatures2(physical,&features);
 VkDeviceCreateInfo device_info={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue;device_info.pNext=&multiview;
 VkDevice device;assert(VRXR_CreateVulkanDevice(&device_info,&device));
 assert(g_creation.device==device && g_creation.deviceExtensions.size()==1 && g_creation.deviceExtensions[0]==device_extra);
 assert(g_creation.multiview==bool(multiview.multiview) && g_creation.queues[0].family==family);
 VRXR_Shutdown();assert(g_creation.instance==instance && g_creation.device==device);
 VRXR_ForgetVulkanCreation();vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);
 puts("OPENXR_VULKAN_CREATION_REAL_PASSED simulated XR merged extensions, real headless instance/device/multiview");return 0;
}
}
int main(int argc,char **argv) {
 spy_checks();puts("OPENXR_VULKAN_CREATION_PASSED merged names/API/features/queues, output matching, threaded callbacks, failure/lifetime");
 return argc==2 && !std::strcmp(argv[1],"--real-vulkan") ? real_driver_checks() : 0;
}
