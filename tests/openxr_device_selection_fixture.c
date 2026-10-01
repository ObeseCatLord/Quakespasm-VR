#ifdef NDEBUG
#error "OpenXR selection fixture requires assertions"
#endif
#include <setjmp.h>

#define OPENXR_DEVICE_SELECTION_CUSTOM_SPIES
#define main openxr_enable_fixture_main
#include "openxr_enable_fixture.c"
#undef main
#undef OPENXR_DEVICE_SELECTION_CUSTOM_SPIES

int com_argc;
char **com_argv;

typedef enum { FAMILY_NONE, FAMILY_FDM, FAMILY_KHR } selected_family_t;
typedef struct
{
	const char *name;
	int openxr, novr, fdm_ext, khr_ext, offset_ext;
	int eye, image_flags, fixed, non_subsampled, density_rg8, density_layers;
	int offset_feature, offset_formats, msaa_formats;
	int fsaa, sample_shading, explicit_size, mode;
	selected_family_t expected;
	int expected_offset;
} selection_case_t;

#define CASE(label, family, ...) {.name=label, .expected=family, __VA_ARGS__}
static const selection_case_t cases[] = {
	CASE("XR eye FB/META", FAMILY_FDM, .openxr=1, .fdm_ext=1, .khr_ext=1, .offset_ext=1, .eye=1, .image_flags=1,
		.fixed=1, .non_subsampled=1, .density_rg8=1, .density_layers=2, .offset_feature=1, .offset_formats=1,
		.msaa_formats=1, .fsaa=1, .mode=VRF_MODE_EYE_TRACKED, .expected_offset=1),
	CASE("desktop prefers KHR", FAMILY_KHR, .fdm_ext=1, .khr_ext=1, .non_subsampled=1, .density_rg8=1, .density_layers=2, .fsaa=1),
	CASE("desktop prepares FDM", FAMILY_FDM, .fdm_ext=1, .non_subsampled=1, .density_rg8=1, .density_layers=2, .fsaa=1),
	CASE("XR fixed FB without eye offsets", FAMILY_FDM, .openxr=1, .fdm_ext=1, .khr_ext=1, .offset_ext=1,
		.fixed=1, .non_subsampled=1, .density_rg8=1, .density_layers=2, .fsaa=1, .mode=VRF_MODE_FIXED),
	CASE("sample shading setting", FAMILY_KHR, .openxr=1, .fdm_ext=1, .khr_ext=1, .offset_ext=1,
		.eye=1, .image_flags=1, .non_subsampled=1, .density_rg8=1, .density_layers=2, .offset_feature=1,
		.offset_formats=1, .msaa_formats=1, .fsaa=4, .sample_shading=1, .mode=VRF_MODE_EYE_TRACKED),
	CASE("paired render size", FAMILY_KHR, .openxr=1, .fdm_ext=1, .khr_ext=1, .offset_ext=1, .eye=1, .image_flags=1,
		.non_subsampled=1, .density_rg8=1, .density_layers=2, .offset_feature=1, .offset_formats=1,
		.msaa_formats=1, .fsaa=1, .explicit_size=1, .mode=VRF_MODE_EYE_TRACKED),
	CASE("offset scene formats rejected", FAMILY_KHR, .openxr=1, .fdm_ext=1, .khr_ext=1, .offset_ext=1,
		.eye=1, .image_flags=1, .non_subsampled=1, .density_rg8=1, .density_layers=2, .offset_feature=1,
		.offset_formats=0, .msaa_formats=1, .fsaa=1, .mode=VRF_MODE_EYE_TRACKED),
	CASE("required MSAA offset format rejected", FAMILY_KHR, .openxr=1, .fdm_ext=1, .khr_ext=1, .offset_ext=1,
		.eye=1, .image_flags=1, .non_subsampled=1, .density_rg8=1, .density_layers=2, .offset_feature=1,
		.offset_formats=1, .msaa_formats=0, .fsaa=4, .mode=VRF_MODE_EYE_TRACKED),
	CASE("neither family available", FAMILY_NONE, .openxr=1, .fsaa=1),
	CASE("novr disables both families", FAMILY_NONE, .novr=1, .fdm_ext=1, .khr_ext=1, .offset_ext=1,
		.non_subsampled=1, .density_rg8=1, .density_layers=2, .fsaa=1)
};
#undef CASE

static const selection_case_t *active_case;
static jmp_buf create_boundary;
static VkPhysicalDevice const test_physical = (VkPhysicalDevice)(uintptr_t)0x401;
static VkInstance const test_instance = (VkInstance)(uintptr_t)0x402;
static VkSurfaceKHR const test_surface = (VkSurfaceKHR)(uintptr_t)0x403;
static struct
{
	int calls, fdm_extension, offset_extension, khr_extension;
	int fdm_feature, offset_feature, khr_feature;
	int fdm_nodes, offset_nodes, khr_nodes;
	int renderpass2_extension;
} observed;

static const char *extension_names[8];
static uint32_t extension_count;
static void capture_create_info (const VkDeviceCreateInfo *info);

static VkSampleCountFlags sample_flags (int samples)
{
	switch (samples)
	{
	case 2: return VK_SAMPLE_COUNT_2_BIT;
	case 4: return VK_SAMPLE_COUNT_4_BIT;
	case 8: return VK_SAMPLE_COUNT_8_BIT;
	case 16: return VK_SAMPLE_COUNT_16_BIT;
	default: return VK_SAMPLE_COUNT_1_BIT;
	}
}

int COM_CheckParm (const char *argument)
{
	if (!strcmp (argument, "-novr")) return novr;
	assert (!strcmp (argument, "-device"));
	return 0;
}
int VRXR_VulkanFoveationSupported (void) { return active_case->fixed; }
int VRXR_VulkanFoveationEyeSupported (void) { return active_case->eye; }
int VRXR_VulkanSwapchainImageFlagsSupported (void) { return active_case->image_flags; }
VkPhysicalDevice VRXR_VulkanPhysicalDevice (VkInstance instance)
{ assert (instance == test_instance); return test_physical; }
int VRXR_CreateVulkanDevice (const VkDeviceCreateInfo *info, VkDevice *device)
{ (void)device; capture_create_info (info); return 0; }

static void capture_create_info (const VkDeviceCreateInfo *info)
{
	assert (info && info->sType == VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
	assert (++observed.calls == 1 && info->enabledExtensionCount <= 48);
	for (uint32_t i = 0; i < info->enabledExtensionCount; ++i)
	{
		const char *name = info->ppEnabledExtensionNames[i];
		assert (name);
		if (!strcmp (name, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME)) observed.renderpass2_extension = 1;
		if (!strcmp (name, VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME)) observed.fdm_extension = 1;
		if (!strcmp (name, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME)) observed.khr_extension = 1;
#if defined(VK_QCOM_fragment_density_map_offset)
		if (!strcmp (name, VK_QCOM_FRAGMENT_DENSITY_MAP_OFFSET_EXTENSION_NAME)) observed.offset_extension = 1;
#endif
	}
	const VkBaseInStructure *node = (const VkBaseInStructure *)info->pNext;
	unsigned int depth = 0;
	for (; node && depth < 32; ++depth, node = node->pNext)
	{
#if defined(VK_EXT_fragment_density_map)
		if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT)
		{
			++observed.fdm_nodes;
			const VkPhysicalDeviceFragmentDensityMapFeaturesEXT *features = (const void *)node;
			assert (features->fragmentDensityMapNonSubsampledImages == VK_TRUE &&
				features->fragmentDensityMapDynamic == VK_FALSE);
			observed.fdm_feature = ((const VkPhysicalDeviceFragmentDensityMapFeaturesEXT *)node)->fragmentDensityMap == VK_TRUE;
		}
#endif
#if defined(VK_QCOM_fragment_density_map_offset)
		if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_OFFSET_FEATURES_QCOM)
		{
			++observed.offset_nodes;
			observed.offset_feature = ((const VkPhysicalDeviceFragmentDensityMapOffsetFeaturesQCOM *)node)->fragmentDensityMapOffset == VK_TRUE;
		}
#endif
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
		if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR)
		{
			++observed.khr_nodes;
			const VkPhysicalDeviceFragmentShadingRateFeaturesKHR *features = (const void *)node;
			assert (features->pipelineFragmentShadingRate == VK_FALSE &&
				features->primitiveFragmentShadingRate == VK_FALSE);
			observed.khr_feature = ((const VkPhysicalDeviceFragmentShadingRateFeaturesKHR *)node)->attachmentFragmentShadingRate == VK_TRUE;
		}
#endif
	}
	assert (!node && depth < 32);
	longjmp (create_boundary, 1);
}

static void VKAPI_CALL reply_properties2 (VkPhysicalDevice physical, VkPhysicalDeviceProperties2 *root)
{
	assert (physical == test_physical);
	for (VkBaseOutStructure *node = (VkBaseOutStructure *)root->pNext; node; node = node->pNext)
	{
		if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES)
			((VkPhysicalDeviceMultiviewProperties *)node)->maxMultiviewViewCount = 2;
#if defined(VK_EXT_fragment_density_map)
		if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_PROPERTIES_EXT)
			((VkPhysicalDeviceFragmentDensityMapPropertiesEXT *)node)->maxFragmentDensityTexelSize = (VkExtent2D){4, 4};
#endif
#if defined(VK_QCOM_fragment_density_map_offset)
		if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_OFFSET_PROPERTIES_QCOM)
			((VkPhysicalDeviceFragmentDensityMapOffsetPropertiesQCOM *)node)->fragmentDensityOffsetGranularity = (VkExtent2D){4, 4};
#endif
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
		if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR)
		{
			VkPhysicalDeviceFragmentShadingRatePropertiesKHR *rate = (VkPhysicalDeviceFragmentShadingRatePropertiesKHR *)node;
			rate->minFragmentShadingRateAttachmentTexelSize = (VkExtent2D){4, 4};
			rate->maxFragmentShadingRateAttachmentTexelSize = (VkExtent2D){4, 4};
			rate->maxFragmentShadingRateAttachmentTexelSizeAspectRatio = 4;
		}
#endif
	}
}

static void VKAPI_CALL reply_features2 (VkPhysicalDevice physical, VkPhysicalDeviceFeatures2 *root)
{
	assert (physical == test_physical);
	for (VkBaseOutStructure *node = (VkBaseOutStructure *)root->pNext; node; node = node->pNext)
	{
		if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES)
			((VkPhysicalDeviceMultiviewFeatures *)node)->multiview = VK_TRUE;
#if defined(VK_EXT_fragment_density_map)
		if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT)
		{
			VkPhysicalDeviceFragmentDensityMapFeaturesEXT *density = (VkPhysicalDeviceFragmentDensityMapFeaturesEXT *)node;
			density->fragmentDensityMap = VK_TRUE;
			density->fragmentDensityMapDynamic = VK_TRUE;
			density->fragmentDensityMapNonSubsampledImages = active_case->non_subsampled ? VK_TRUE : VK_FALSE;
		}
#endif
#if defined(VK_QCOM_fragment_density_map_offset)
		if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_OFFSET_FEATURES_QCOM)
			((VkPhysicalDeviceFragmentDensityMapOffsetFeaturesQCOM *)node)->fragmentDensityMapOffset = active_case->offset_feature ? VK_TRUE : VK_FALSE;
#endif
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
		if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR)
		{
			VkPhysicalDeviceFragmentShadingRateFeaturesKHR *features = (void *)node;
			features->attachmentFragmentShadingRate = VK_TRUE;
			features->pipelineFragmentShadingRate = features->primitiveFragmentShadingRate = VK_TRUE;
		}
#endif
	}
}

#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
static VkResult VKAPI_CALL reply_shading_rates (VkPhysicalDevice physical, uint32_t *count,
	VkPhysicalDeviceFragmentShadingRateKHR *rates)
{
	assert (physical == test_physical);
	if (!rates) { *count = 3; return VK_SUCCESS; }
	assert (*count >= 3);
	const VkExtent2D sizes[3] = {{1, 1}, {2, 2}, {4, 4}};
	for (uint32_t i = 0; i < 3; ++i)
	{
		rates[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_KHR;
		rates[i].pNext = NULL;
		rates[i].fragmentSize = sizes[i];
		rates[i].sampleCounts = VK_SAMPLE_COUNT_1_BIT;
	}
	*count = 3;
	return VK_SUCCESS;
}
#endif

static PFN_vkVoidFunction VKAPI_CALL reply_proc (VkInstance instance, const char *name)
{
	assert (instance == test_instance);
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
	if (!strcmp (name, "vkGetPhysicalDeviceFragmentShadingRatesKHR"))
		return (PFN_vkVoidFunction)reply_shading_rates;
#endif
	return NULL;
}

static VkResult VKAPI_CALL reply_surface_support (VkPhysicalDevice physical, uint32_t family,
	VkSurfaceKHR surface, VkBool32 *supported)
{
	assert (physical == test_physical && family == 0 && surface == test_surface);
	*supported = VK_TRUE;
	return VK_SUCCESS;
}

qboolean R_SSAOEnabled (void) { return false; }
void *Mem_Alloc (size_t size) { return malloc (size); }
void VRXR_Shutdown (void) {}
void VRXR_ForgetVulkanCreation (void) {}
void VRXR_RecordVulkanDevice (VkPhysicalDevice physical, const VkDeviceCreateInfo *info, VkDevice device)
{ (void)physical; (void)info; (void)device; }
void Sys_Error (const char *format, ...) { (void)format; assert (!"unexpected Sys_Error"); abort (); }

VkResult VKAPI_CALL vkEnumeratePhysicalDevices (VkInstance instance, uint32_t *count, VkPhysicalDevice *devices)
{
	assert (instance == test_instance);
	if (!devices) *count = 1;
	else { assert (*count >= 1); devices[0] = test_physical; *count = 1; }
	return VK_SUCCESS;
}
VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties (VkPhysicalDevice physical, const char *layer,
	uint32_t *count, VkExtensionProperties *properties)
{
	assert (physical == test_physical && !layer);
	if (!properties) { *count = extension_count; return VK_SUCCESS; }
	const uint32_t capacity = *count;
	for (uint32_t i = 0; i < capacity && i < extension_count; ++i)
	{
		memset (&properties[i], 0, sizeof properties[i]);
		strncpy (properties[i].extensionName, extension_names[i], sizeof properties[i].extensionName - 1);
		properties[i].specVersion = 1;
	}
	*count = capacity < extension_count ? capacity : extension_count;
	return VK_SUCCESS;
}
void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties (VkPhysicalDevice physical, VkPhysicalDeviceMemoryProperties *properties)
{ assert (physical == test_physical); memset (properties, 0, sizeof *properties); }
void VKAPI_CALL vkGetPhysicalDeviceProperties (VkPhysicalDevice physical, VkPhysicalDeviceProperties *properties)
{
	assert (physical == test_physical);
	memset (properties, 0, sizeof *properties);
	properties->apiVersion = VK_API_VERSION_1_2;
	properties->deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
	properties->limits.maxBoundDescriptorSets = 6;
	strcpy (properties->deviceName, "selection fixture GPU");
}
void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties (VkPhysicalDevice physical, uint32_t *count,
	VkQueueFamilyProperties *properties)
{
	assert (physical == test_physical);
	if (!properties) *count = 1;
	else { assert (*count >= 1); memset (properties, 0, sizeof *properties); properties[0].queueCount = 1;
		properties[0].queueFlags = VK_QUEUE_GRAPHICS_BIT; properties[0].timestampValidBits = 32; *count = 1; }
}
void VKAPI_CALL vkGetPhysicalDeviceFormatProperties (VkPhysicalDevice physical, VkFormat format, VkFormatProperties *properties)
{
	assert (physical == test_physical);
	memset (properties, 0, sizeof *properties);
	if (format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT)
		properties->optimalTilingFeatures = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
#if defined(VK_EXT_fragment_density_map)
	if (format == VK_FORMAT_R8G8_UNORM && active_case->density_rg8)
		properties->optimalTilingFeatures |= VK_FORMAT_FEATURE_FRAGMENT_DENSITY_MAP_BIT_EXT;
#endif
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
	if (format == VK_FORMAT_R8_UINT && active_case->khr_ext)
		properties->optimalTilingFeatures = VK_FORMAT_FEATURE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
#endif
}

VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties (VkPhysicalDevice physical, VkFormat format,
	VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
	VkImageFormatProperties *properties)
{
	assert (physical == test_physical && type == VK_IMAGE_TYPE_2D && tiling == VK_IMAGE_TILING_OPTIMAL);
	memset (properties, 0, sizeof *properties);
	properties->maxExtent = (VkExtent3D){2048, 2048, 1};
	properties->maxArrayLayers = 2;
	properties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
#if defined(VK_QCOM_fragment_density_map_offset)
	if (flags & VK_IMAGE_CREATE_FRAGMENT_DENSITY_MAP_OFFSET_BIT_QCOM)
	{
		if (!active_case->offset_formats) return VK_ERROR_FORMAT_NOT_SUPPORTED;
		if (usage == VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT && !active_case->msaa_formats)
			properties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
		else if (usage == VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT || (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
			properties->sampleCounts = sample_flags (active_case->fsaa);
		return VK_SUCCESS;
	}
#endif
#if defined(VK_EXT_fragment_density_map)
	if (format == VK_FORMAT_R8G8_UNORM && usage == VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT)
	{
		if (!active_case->density_rg8) return VK_ERROR_FORMAT_NOT_SUPPORTED;
		properties->maxArrayLayers = active_case->density_layers;
		return VK_SUCCESS;
	}
#endif
#if defined(VK_KHR_fragment_shading_rate) && defined(VK_KHR_create_renderpass2)
	if (format == VK_FORMAT_R8_UINT && usage ==
		(VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
		return VK_SUCCESS;
#endif
	if (format == vulkan_globals.color_format && usage != VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT && active_case->fsaa >= 2)
		properties->sampleCounts = sample_flags (active_case->fsaa);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice (VkPhysicalDevice physical, const VkDeviceCreateInfo *info,
	const VkAllocationCallbacks *allocator, VkDevice *device)
{
	assert (physical == test_physical && !allocator);
	(void)device;
	capture_create_info (info);
	return VK_ERROR_INITIALIZATION_FAILED;
}

static void build_extensions (const selection_case_t *test)
{
	extension_count = 0;
	extension_names[extension_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	if (test->fdm_ext) extension_names[extension_count++] = VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME;
	if (test->khr_ext) extension_names[extension_count++] = VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME;
	if (test->fdm_ext || test->khr_ext) extension_names[extension_count++] = VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME;
#if defined(VK_QCOM_fragment_density_map_offset)
	if (test->offset_ext) extension_names[extension_count++] = VK_QCOM_FRAGMENT_DENSITY_MAP_OFFSET_EXTENSION_NAME;
#endif
}

static void run_case (const selection_case_t *test)
{
	active_case = test;
	memset (&observed, 0, sizeof observed);
	memset (&vulkan_globals, 0, sizeof vulkan_globals);
	openxr_vulkan_binding = test->openxr;
	openxr_vulkan_api_version = test->openxr ? VK_API_VERSION_1_2 : 0;
	openxr_vulkan_minimum_version = test->openxr ? VK_API_VERSION_1_1 : 0;
	openxr_session_wanted = openxr_session_change_pending = openxr_attach_attempted = false;
	vulkan_instance = test_instance; vulkan_surface = test_surface; vulkan_physical_device = VK_NULL_HANDLE;
	vulkan_globals.vulkan_1_1_available = true;
	fpGetPhysicalDeviceProperties2 = reply_properties2;
	fpGetPhysicalDeviceFeatures2 = reply_features2;
	fpGetInstanceProcAddr = reply_proc;
	fpGetPhysicalDeviceSurfaceSupportKHR = reply_surface_support;
	novr = test->novr; runtime_fb_supported = test->fixed;
	vr_foveation.value = (float)test->mode; vr_eye_tracking.value = 1;
	vid_fsaa.value = (float)test->fsaa; vid_fsaamode.value = test->sample_shading ? 1.0f : 0.0f;
	r_width.value = test->explicit_size ? 640.0f : 0.0f;
	r_height.value = test->explicit_size ? 480.0f : 0.0f;
	build_extensions (test);
	if (setjmp (create_boundary) == 0)
	{
		GL_InitDevice ();
		assert (!"GL_InitDevice did not reach the native create boundary");
	}
	assert (observed.calls == 1);
	assert (observed.fdm_nodes == (test->expected == FAMILY_FDM));
	assert (observed.khr_nodes == (test->expected == FAMILY_KHR));
	assert (observed.offset_nodes == test->expected_offset);
	assert (observed.renderpass2_extension == (!test->openxr && test->expected != FAMILY_NONE));
	if (test->expected == FAMILY_FDM)
	{
		assert (observed.fdm_extension && observed.fdm_feature);
		assert (!observed.khr_extension && !observed.khr_feature);
		assert (observed.offset_extension == test->expected_offset);
		assert (observed.offset_feature == test->expected_offset);
	}
	else if (test->expected == FAMILY_KHR)
	{
		assert (observed.khr_extension && observed.khr_feature);
		assert (!observed.fdm_extension && !observed.fdm_feature);
		assert (!observed.offset_extension && !observed.offset_feature);
	}
	else
		assert (!observed.khr_extension && !observed.khr_feature && !observed.fdm_extension &&
			!observed.fdm_feature && !observed.offset_extension && !observed.offset_feature);
	assert (!(observed.fdm_extension && observed.khr_extension));
	printf ("selection case passed: %s\n", test->name);
}

int main (void)
{
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
		run_case (&cases[i]);
	for (int missing = 0; missing < 7; ++missing)
	{
		selection_case_t test = cases[0];
		test.expected = FAMILY_KHR; test.expected_offset = 0;
		switch (missing)
		{
		case 0: test.name = "XR eye unavailable"; test.eye = 0; break;
		case 1: test.name = "XR image flags unavailable"; test.image_flags = 0; break;
		case 2: test.name = "offset extension unavailable"; test.offset_ext = 0; break;
		case 3: test.name = "offset feature unavailable"; test.offset_feature = 0; break;
		case 4: test.name = "FDM subsampled only"; test.non_subsampled = 0; break;
		case 5: test.name = "FDM RG8 rejection"; test.density_rg8 = 0; break;
		case 6: test.name = "FDM array rejection"; test.density_layers = 1; break;
		}
		run_case (&test);
	}
	puts ("OPENXR_DEVICE_SELECTION_PASSED actual GL_InitDevice create-request capture; no driver creation");
	return 0;
}
