/* Real donor descriptor/pipeline layout owners on a multiview-ready device.
 * No window, runtime, shaders or scene draw. Compare desktop readiness off/on.
 * cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -ffunction-sections -fdata-sections \
 *   tests/openxr_layout_fixture.c Quake/r_ssao.c -Wl,--gc-sections \
 *   $(pkg-config --cflags --libs sdl3) -lvulkan -lm -o /tmp/qsvr-openxr-layout
 */
#include "../Quake/gl_rmisc.c"

vulkanglobals_t vulkan_globals;
static struct { uint64_t handle; VkObjectType type; } objects[128];
static unsigned object_count;
void Sys_Printf (const char *format, ...) { (void)format; }
void Sys_Error (const char *format, ...)
{
 va_list args; va_start (args, format); vfprintf (stderr, format, args); va_end (args); abort ();
}
void GL_SetObjectName (uint64_t handle, VkObjectType type, const char *name)
{
 (void)name;
 assert (handle && object_count < countof (objects));
 assert (type == VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT || type == VK_OBJECT_TYPE_PIPELINE_LAYOUT);
 objects[object_count].handle = handle; objects[object_count++].type = type;
}
#define CHECK_VK(call) do { VkResult result = (call); if (result != VK_SUCCESS) { \
 fprintf (stderr, "OPENXR_LAYOUT_FAILED %s result=%d\n", #call, result); return 1; } } while (0)
int main (void)
{
 VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1};
 VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
 VkInstance instance; CHECK_VK (vkCreateInstance (&instance_info, NULL, &instance));
 uint32_t count = 0;
 if (vkEnumeratePhysicalDevices (instance, &count, NULL) != VK_SUCCESS || !count)
 { vkDestroyInstance (instance, NULL); return 77; }
 VkPhysicalDevice *devices = malloc (count * sizeof (*devices));
 CHECK_VK (vkEnumeratePhysicalDevices (instance, &count, devices));
 VkPhysicalDevice physical = devices[0]; free (devices);
 VkPhysicalDeviceMultiviewFeatures multiview = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
 VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &multiview};
 vkGetPhysicalDeviceFeatures2 (physical, &features);
 VkPhysicalDeviceMultiviewProperties limits = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES};
 VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &limits};
 vkGetPhysicalDeviceProperties2 (physical, &properties);
 if (properties.properties.apiVersion < VK_API_VERSION_1_1 || !multiview.multiview ||
  limits.maxMultiviewViewCount < 2 || properties.properties.limits.maxBoundDescriptorSets < 6)
 {
  printf ("OPENXR_LAYOUT_SKIPPED needs core1.1/multiview/two views/six sets; api=%u multiview=%u views=%u sets=%u\n",
   properties.properties.apiVersion, multiview.multiview, limits.maxMultiviewViewCount, properties.properties.limits.maxBoundDescriptorSets);
  vkDestroyInstance (instance, NULL); return 77;
 }
 vkGetPhysicalDeviceQueueFamilyProperties (physical, &count, NULL);
 VkQueueFamilyProperties *families = malloc (count * sizeof (*families));
 vkGetPhysicalDeviceQueueFamilyProperties (physical, &count, families);
 uint32_t family = 0; while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
 free (families); assert (family < count);
 float priority = 1;
 VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family,
  .queueCount = 1, .pQueuePriorities = &priority};
 multiview.multiviewGeometryShader = multiview.multiviewTessellationShader = VK_FALSE;
 VkDeviceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &multiview,
  .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue};
 VkDevice device; CHECK_VK (vkCreateDevice (physical, &info, NULL, &device));
 for (int ready = 0; ready <= 1; ++ready)
 {
  memset (&vulkan_globals, 0, sizeof (vulkan_globals)); vulkan_globals.device = device;
  vulkan_globals.openxr_multiview_available = ready;
  assert (!vulkan_globals.stereo_active); // desktop output, both readiness modes
  R_CreateDescriptorSetLayouts (); R_CreatePipelineLayouts ();
  assert (vulkan_globals.basic_pipeline_layout.handle && vulkan_globals.gui_pipeline_layout.handle);
  printf ("OPENXR_LAYOUT_PASSED actual donor descriptor/pipeline owners desktop multiview_ready=%d objects=%u\n", ready, object_count);
  for (unsigned i = object_count; i-- > 0;)
   if (objects[i].type == VK_OBJECT_TYPE_PIPELINE_LAYOUT)
    vkDestroyPipelineLayout (device, (VkPipelineLayout)objects[i].handle, NULL);
   else vkDestroyDescriptorSetLayout (device, (VkDescriptorSetLayout)objects[i].handle, NULL);
  object_count = 0;
 }
 vkDestroyDevice (device, NULL); vkDestroyInstance (instance, NULL); return 0;
}
