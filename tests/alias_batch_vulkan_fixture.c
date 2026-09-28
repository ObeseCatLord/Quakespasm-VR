/* Offline alias shader/batching equivalence; no SDL initialization, window or XR.
 * Uses production draws and real Vulkan dispatch, with a fixed mapped test UBO.
 * Run from the repository root; the reference is pinned before alias batching:
 *   p=/tmp/qsvr-alias-vulkan
 *   git show 62de7c26:Shaders/alias.vert > "$p-old.vert"
 *   glslangValidator -V --target-env vulkan1.1 -IShaders "$p-old.vert" -o "$p-old.spv"
 *   glslangValidator -V --target-env vulkan1.1 -IShaders Shaders/alias.vert -o "$p-new.spv"
 *   glslangValidator -V --target-env vulkan1.1 -IShaders Shaders/alias.frag -o "$p-frag.spv"
 *   glslangValidator -V --target-env vulkan1.1 -IShaders -DSTEREO=1 "$p-old.vert" -o "$p-old-stereo.spv"
 *   glslangValidator -V --target-env vulkan1.1 -IShaders -DSTEREO=1 Shaders/alias.vert -o "$p-new-stereo.spv"
 *   cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -O0 -ffunction-sections -fdata-sections -IQuake \
 *     tests/alias_batch_vulkan_fixture.c Quake/mathlib.c -Wl,--gc-sections \
 *     -lvulkan -lm $(pkg-config --cflags --libs sdl3) -o "$p-fixture"
 *   VK_ICD_FILENAMES=/usr/lib/chromium/vk_swiftshader_icd.json "$p-fixture" \
 *     "$p-old.spv" "$p-new.spv" "$p-frag.spv" "$p-old-stereo.spv" "$p-new-stereo.spv"
 * Select an existing accessible ICD; the example is this host's CPU SwiftShader.
 * Exit 77 means no accessible Vulkan 1.1 graphics device; mismatches exit 1.
 * Scope: opaque shader/instance equivalence, not whole-renderer or timing proof.
 */
#include "../Quake/r_alias.c"

vulkanglobals_t vulkan_globals;
qboolean r_fullbright_cheatsafe, r_lightmap_cheatsafe;
cvar_t r_fullbright;
const r_vrik_prepared_palette_t *R_VRIKRenderLookup (const entity_t *e) { return NULL; }
qboolean Mod_IsAdmittedAvatarModel (const qmodel_t *m) { return false; }
VkPipeline R_ResolvePipelineInstance (const cb_context_t *c, vulkan_pipeline_t p) { return p.handle; }

#define W 128
#define IMAGE_BYTES (W * W * 4)
#define ARENA_BYTES 8192
#define CHECK(x) do { if (!(x)) { fprintf (stderr, "ALIAS_BATCH_VULKAN_FAILED line=%d: %s\n", __LINE__, #x); exit (1); } } while (0)
#define VK(x) do { VkResult result_ = (x); if (result_ != VK_SUCCESS) { \
	fprintf (stderr, "ALIAS_BATCH_VULKAN_FAILED line=%d: %s VkResult=%d\n", __LINE__, #x, result_); exit (1); } } while (0)
typedef struct { VkBuffer handle; VkDeviceMemory memory; byte *mapped; } test_buffer_t;
typedef struct { VkImage handle; VkDeviceMemory memory; VkImageView view; } test_image_t;
static struct
{
	VkInstance instance;
	VkPhysicalDevice gpu;
	VkDevice device;
	VkQueue queue;
	VkPhysicalDeviceMemoryProperties memory;
	VkCommandPool pool;
	VkCommandBuffer command;
	VkDescriptorSetLayout layouts[6];
	VkDescriptorPool descriptors;
	VkDescriptorSet sets[6];
	VkPipelineLayout layout;
	VkSampler sampler;
	test_buffer_t uniform, vertices, indices, readback;
	test_image_t skin;
	uint32_t alignment, uniform_start, used, allocations, uploaded, draws, draw_sizes[2];
	VkBool32 multiview;
} fixture;

static void device_skip (const char *operation, VkResult result)
{
	printf ("ALIAS_BATCH_VULKAN_DESKTOP_SKIPPED %s VkResult=%d\n", operation, result);
	puts ("ALIAS_BATCH_VULKAN_STEREO_SKIPPED no accessible desktop graphics device");
	if (fixture.instance) vkDestroyInstance (fixture.instance, NULL);
	exit (77);
}
static uint32_t aligned (uint32_t value) { return (value + fixture.alignment - 1) & ~(fixture.alignment - 1); }
static uint32_t memory_type (uint32_t bits, VkMemoryPropertyFlags flags)
{
	for (uint32_t i = 0; i < fixture.memory.memoryTypeCount; ++i)
		if ((bits & (1u << i)) && (fixture.memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
	CHECK (false);
	return 0;
}
static test_buffer_t buffer_create (VkDeviceSize size, VkBufferUsageFlags usage)
{
	test_buffer_t b = {0};
	VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage};
	VK (vkCreateBuffer (fixture.device, &info, NULL, &b.handle));
	VkMemoryRequirements requirements;
	vkGetBufferMemoryRequirements (fixture.device, b.handle, &requirements);
	VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = requirements.size,
		.memoryTypeIndex = memory_type (requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
	VK (vkAllocateMemory (fixture.device, &allocation, NULL, &b.memory));
	VK (vkBindBufferMemory (fixture.device, b.handle, b.memory, 0));
	VK (vkMapMemory (fixture.device, b.memory, 0, VK_WHOLE_SIZE, 0, (void **)&b.mapped));
	return b;
}
static test_image_t image_create (uint32_t width, uint32_t layers, VkImageUsageFlags usage)
{
	test_image_t image = {0};
	VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {width, width, 1}, .mipLevels = 1, .arrayLayers = layers,
		.samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage};
	VK (vkCreateImage (fixture.device, &info, NULL, &image.handle));
	VkMemoryRequirements requirements;
	vkGetImageMemoryRequirements (fixture.device, image.handle, &requirements);
	VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = requirements.size,
		.memoryTypeIndex = memory_type (requirements.memoryTypeBits, 0)};
	VK (vkAllocateMemory (fixture.device, &allocation, NULL, &image.memory));
	VK (vkBindImageMemory (fixture.device, image.handle, image.memory, 0));
	VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image.handle,
		.viewType = layers == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY, .format = info.format,
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers}};
	VK (vkCreateImageView (fixture.device, &view, NULL, &image.view));
	return image;
}
static void image_destroy (test_image_t image)
{
	vkDestroyImageView (fixture.device, image.view, NULL);
	vkDestroyImage (fixture.device, image.handle, NULL);
	vkFreeMemory (fixture.device, image.memory, NULL);
}
static void begin_commands (void)
{
	VK (vkResetCommandBuffer (fixture.command, 0));
	VkCommandBufferBeginInfo info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	VK (vkBeginCommandBuffer (fixture.command, &info));
}
static void finish_commands (void)
{
	VK (vkEndCommandBuffer (fixture.command));
	VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &fixture.command};
	VK (vkQueueSubmit (fixture.queue, 1, &submit, VK_NULL_HANDLE));
	VK (vkQueueWaitIdle (fixture.queue)); // Test-only synchronization before mapped readback/reuse.
}
byte *R_UniformAllocate (int size, VkBuffer *buffer, uint32_t *offset, VkDescriptorSet *set)
{
	CHECK (size > 0 && size <= MAX_UNIFORM_ALLOC && fixture.used + MAX_UNIFORM_ALLOC <= ARENA_BYTES);
	*buffer = fixture.uniform.handle;
	*offset = fixture.used;
	*set = fixture.sets[2];
	fixture.used = aligned (fixture.used + size);
	fixture.allocations++;
	fixture.uploaded += size;
	return fixture.uniform.mapped + *offset;
}
static VKAPI_ATTR void VKAPI_CALL counted_draw (VkCommandBuffer cb, uint32_t indices, uint32_t instances,
	uint32_t first, int32_t vertex, uint32_t first_instance)
{
	CHECK (fixture.draws < 2 && indices == 3 && instances >= 1 && instances <= 2);
	CHECK (first == 0 && vertex == 0 && first_instance == 0);
	fixture.draw_sizes[fixture.draws++] = instances;
	vkCmdDrawIndexed (cb, indices, instances, first, vertex, first_instance);
}
static void initialize_device (void)
{
	VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "alias batch fixture", .apiVersion = VK_API_VERSION_1_1};
	VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
	VkResult result = vkCreateInstance (&info, NULL, &fixture.instance);
	if (result != VK_SUCCESS) device_skip ("vkCreateInstance", result);
	uint32_t count = 0, family = 0;
	result = vkEnumeratePhysicalDevices (fixture.instance, &count, NULL);
	if (result != VK_SUCCESS || !count) device_skip ("vkEnumeratePhysicalDevices", result);
	VkPhysicalDevice *devices = calloc (count, sizeof (*devices));
	CHECK (devices);
	VK (vkEnumeratePhysicalDevices (fixture.instance, &count, devices));
	VkPhysicalDeviceProperties selected = {0};
	for (uint32_t i = 0; i < count; ++i)
	{
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties (devices[i], &properties);
		if (properties.apiVersion < VK_API_VERSION_1_1) continue;
		uint32_t families = 0;
		vkGetPhysicalDeviceQueueFamilyProperties (devices[i], &families, NULL);
		VkQueueFamilyProperties *queues = calloc (families, sizeof (*queues));
		CHECK (queues);
		vkGetPhysicalDeviceQueueFamilyProperties (devices[i], &families, queues);
		for (uint32_t j = 0; j < families; ++j)
			if (queues[j].queueCount && (queues[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
				(!fixture.gpu || (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU && selected.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU)))
			{ fixture.gpu = devices[i]; family = j; selected = properties; break; }
		free (queues);
	}
	free (devices);
	if (!fixture.gpu) device_skip ("no Vulkan 1.1 graphics queue", VK_ERROR_FEATURE_NOT_PRESENT);
	VkPhysicalDeviceMultiviewFeatures multiview = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
	VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &multiview};
	vkGetPhysicalDeviceFeatures2 (fixture.gpu, &features);
	VkPhysicalDeviceMultiviewProperties views = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES};
	VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &views};
	vkGetPhysicalDeviceProperties2 (fixture.gpu, &properties);
	fixture.multiview = multiview.multiview && views.maxMultiviewViewCount >= 2 &&
		views.maxMultiviewInstanceIndex >= 1 && selected.limits.maxBoundDescriptorSets >= 6;
	multiview.multiview = fixture.multiview;
	multiview.multiviewGeometryShader = multiview.multiviewTessellationShader = VK_FALSE;
	float priority = 1;
	VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family,
		.queueCount = 1, .pQueuePriorities = &priority};
	VkDeviceCreateInfo device = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &multiview, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue};
	result = vkCreateDevice (fixture.gpu, &device, NULL, &fixture.device);
	if (result != VK_SUCCESS) device_skip ("vkCreateDevice", result);
	vkGetDeviceQueue (fixture.device, family, 0, &fixture.queue);
	vkGetPhysicalDeviceMemoryProperties (fixture.gpu, &fixture.memory);
	fixture.alignment = (uint32_t)selected.limits.minUniformBufferOffsetAlignment;
	CHECK (fixture.alignment && selected.limits.maxUniformBufferRange >= MAX_UNIFORM_ALLOC);
	printf ("ALIAS_BATCH_VULKAN_DEVICE %s type=%u multiview=%u\n", selected.deviceName, selected.deviceType, fixture.multiview);
	VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = family};
	VK (vkCreateCommandPool (fixture.device, &pool, NULL, &fixture.pool));
	VkCommandBufferAllocateInfo command = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = fixture.pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
	VK (vkAllocateCommandBuffers (fixture.device, &command, &fixture.command));
	vulkan_globals.vk_cmd_bind_pipeline = vkCmdBindPipeline;
	vulkan_globals.vk_cmd_push_constants = vkCmdPushConstants;
	vulkan_globals.vk_cmd_bind_descriptor_sets = vkCmdBindDescriptorSets;
	vulkan_globals.vk_cmd_bind_vertex_buffers = vkCmdBindVertexBuffers;
	vulkan_globals.vk_cmd_bind_index_buffer = vkCmdBindIndexBuffer;
	vulkan_globals.vk_cmd_draw_indexed = counted_draw;
}
static void initialize_resources (void)
{
	fixture.uniform = buffer_create (ARENA_BYTES, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	fixture.vertices = buffer_create (96, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	fixture.indices = buffer_create (6, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	fixture.readback = buffer_create (2 * IMAGE_BYTES, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	struct { uint16_t position[4]; int8_t normal[4]; } poses[6] = {
		{{0, 0, 0, 0}, {127, 0, 0, 0}}, {{65535, 0, 0, 0}, {0, 127, 0, 0}}, {{0, 65535, 0, 0}, {0, 0, 127, 0}},
		{{2000, 2000, 0, 0}, {0, -127, 0, 0}}, {{60000, 4500, 0, 0}, {-127, 0, 0, 0}}, {{5000, 60000, 0, 0}, {0, 0, -127, 0}}};
	const float texcoords[6] = {0, 0, 1, 0, 0, 1};
	const uint16_t indices[3] = {0, 1, 2};
	CHECK (sizeof (poses) == 72 && sizeof (meshxyz_t) == 12 && sizeof (aliasubo_t) == 112);
	memcpy (fixture.vertices.mapped, poses, sizeof (poses));
	memcpy (fixture.vertices.mapped + 72, texcoords, sizeof (texcoords));
	memcpy (fixture.indices.mapped, indices, sizeof (indices));
	struct { float clip[2][16], eye[2][4]; } stereo = {0};
	for (int eye = 0; eye < 2; ++eye)
	{
		IdentityMatrix (stereo.clip[eye]);
		stereo.clip[eye][12] = eye ? .05f : -.065f;
		stereo.clip[eye][13] = eye ? .03f : -.04f;
		stereo.eye[eye][0] = eye ? .032f : -.032f;
	}
	vulkan_globals.stereo_uniform_offset = aligned (sizeof (stereo));
	fixture.uniform_start = aligned (vulkan_globals.stereo_uniform_offset + sizeof (stereo));
	CHECK (fixture.uniform_start + 2 * aligned (MAX_UNIFORM_ALLOC) <= ARENA_BYTES);
	memset (fixture.uniform.mapped, 0, ARENA_BYTES);
	memcpy (fixture.uniform.mapped + vulkan_globals.stereo_uniform_offset, &stereo, sizeof (stereo));
	fixture.skin = image_create (1, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	memset (fixture.readback.mapped, 255, 4);
	begin_commands ();
	VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = fixture.skin.handle, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
	vkCmdPipelineBarrier (fixture.command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
	VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {1, 1, 1}};
	vkCmdCopyBufferToImage (fixture.command, fixture.readback.handle, fixture.skin.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vkCmdPipelineBarrier (fixture.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
	finish_commands ();
	VkSamplerCreateInfo sampler = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST, .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
	VK (vkCreateSampler (fixture.device, &sampler, NULL, &fixture.sampler));
	for (int i = 0; i < 6; ++i)
	{
		VkDescriptorSetLayoutBinding binding = {.binding = 0, .descriptorCount = 1,
			.descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.stageFlags = i < 2 ? VK_SHADER_STAGE_FRAGMENT_BIT : i == 2 ? VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT};
		VkDescriptorSetLayoutCreateInfo layout = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = i == 3 || i == 4 ? 0 : 1, .pBindings = &binding};
		VK (vkCreateDescriptorSetLayout (fixture.device, &layout, NULL, &fixture.layouts[i]));
	}
	VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 2}};
	VkDescriptorPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 6, .poolSizeCount = 2, .pPoolSizes = sizes};
	VK (vkCreateDescriptorPool (fixture.device, &pool, NULL, &fixture.descriptors));
	VkDescriptorSetAllocateInfo sets = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = fixture.descriptors, .descriptorSetCount = 6, .pSetLayouts = fixture.layouts};
	VK (vkAllocateDescriptorSets (fixture.device, &sets, fixture.sets));
	vulkan_globals.stereo_descriptor_set = fixture.sets[5];
	VkDescriptorImageInfo skin = {.sampler = fixture.sampler, .imageView = fixture.skin.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	VkDescriptorBufferInfo buffers[2] = {{fixture.uniform.handle, 0, MAX_UNIFORM_ALLOC}, {fixture.uniform.handle, 0, sizeof (stereo)}};
	for (int i = 0; i < 4; ++i)
	{
		VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = fixture.sets[i == 3 ? 5 : i],
			.descriptorCount = 1, .descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.pImageInfo = i < 2 ? &skin : NULL, .pBufferInfo = i < 2 ? NULL : &buffers[i - 2]};
		vkUpdateDescriptorSets (fixture.device, 1, &write, 0, NULL);
	}
	VkPushConstantRange push = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80};
	VkPipelineLayoutCreateInfo layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = fixture.multiview ? 6 : 3,
		.pSetLayouts = fixture.layouts, .pushConstantRangeCount = 1, .pPushConstantRanges = &push};
	VK (vkCreatePipelineLayout (fixture.device, &layout, NULL, &fixture.layout));
}
static VkShaderModule shader (const char *path)
{
	FILE *file = fopen (path, "rb");
	if (!file) { perror (path); CHECK (file); }
	CHECK (!fseek (file, 0, SEEK_END));
	long size = ftell (file);
	CHECK (size >= 20 && size % 4 == 0 && !fseek (file, 0, SEEK_SET));
	uint32_t *code = malloc (size);
	CHECK (code && fread (code, 1, size, file) == (size_t)size && code[0] == 0x07230203);
	CHECK (!fclose (file));
	VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = size, .pCode = code};
	VkShaderModule module;
	VK (vkCreateShaderModule (fixture.device, &info, NULL, &module));
	free (code);
	return module;
}
static VkPipeline pipeline_create (VkRenderPass pass, VkShaderModule vertex, VkShaderModule fragment)
{
	VkPipelineShaderStageCreateInfo stages[2] = {
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex, .pName = "main"},
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment, .pName = "main"}};
	VkVertexInputBindingDescription bindings[3] = {{0, 8, VK_VERTEX_INPUT_RATE_VERTEX}, {1, 12, VK_VERTEX_INPUT_RATE_VERTEX}, {2, 12, VK_VERTEX_INPUT_RATE_VERTEX}};
	VkVertexInputAttributeDescription attributes[5] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 1, VK_FORMAT_R16G16B16A16_UNORM, 0},
		{2, 1, VK_FORMAT_R8G8B8A8_SNORM, 8}, {3, 2, VK_FORMAT_R16G16B16A16_UNORM, 0}, {4, 2, VK_FORMAT_R8G8B8A8_SNORM, 8}};
	VkPipelineVertexInputStateCreateInfo input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 3, .pVertexBindingDescriptions = bindings, .vertexAttributeDescriptionCount = 5, .pVertexAttributeDescriptions = attributes};
	VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
	VkViewport viewport = {0, 0, W, W, 0, 1};
	VkRect2D scissor = {{0, 0}, {W, W}};
	VkPipelineViewportStateCreateInfo view = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor};
	VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1};
	VkPipelineMultisampleStateCreateInfo samples = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
	VkPipelineColorBlendAttachmentState attachment = {.colorWriteMask = 15};
	VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &attachment};
	VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = stages,
		.pVertexInputState = &input, .pInputAssemblyState = &assembly, .pViewportState = &view, .pRasterizationState = &raster,
		.pMultisampleState = &samples, .pColorBlendState = &blend, .layout = fixture.layout, .renderPass = pass};
	VkPipeline pipeline;
	VK (vkCreateGraphicsPipelines (fixture.device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline));
	return pipeline;
}
static void render (VkRenderPass pass, VkFramebuffer framebuffer, test_image_t image, VkPipeline handle,
	int layers, qboolean batch, qboolean md3, byte *pixels)
{
	fixture.used = fixture.uniform_start;
	fixture.allocations = fixture.uploaded = fixture.draws = 0;
	memset (fixture.uniform.mapped + fixture.used, 0, ARENA_BYTES - fixture.used);
	vulkan_globals.stereo_active = layers == 2;
	vulkan_pipeline_t pipeline = {.handle = handle, .layout = {.handle = fixture.layout,
		.push_constant_range = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80}}};
	vulkan_globals.alias_pipelines[0][0] = pipeline;
	begin_commands ();
	VkClearValue clear = {.color = {{0, 0, 0, 0}}};
	VkRenderPassBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = pass, .framebuffer = framebuffer,
		.renderArea = {{0, 0}, {W, W}}, .clearValueCount = 1, .pClearValues = &clear};
	vkCmdBeginRenderPass (fixture.command, &begin, VK_SUBPASS_CONTENTS_INLINE);
	cb_context_t cbx = {.cb = fixture.command, .subpass_type = SUBPASS_MAIN};
	R_BindPipeline (&cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline); // Initializes production push-constant tracking.
	float push[20] = {0};
	IdentityMatrix (push);
	vkCmdPushConstants (fixture.command, fixture.layout, pipeline.layout.push_constant_range.stageFlags, 0, sizeof (push), push);
	aliashdr_t header = {.poseverttype = md3 ? PV_QUAKE3 : PV_QUAKE1, .numindexes = 3, .numverts_vbo = 3, .vbostofs = 72,
		.vertex_buffer = fixture.vertices.handle, .index_buffer = fixture.indices.handle};
	gltexture_t skin = {.descriptor_set = fixture.sets[0]};
	r_alias_batch_t pending;
	if (batch) R_AliasBatchBegin (&cbx, &pending);
	for (int i = 0; i < 2; ++i)
	{
		entity_t entity = {0};
		lerpdata_t lerp = {.pose1 = 0, .pose2 = 1, .blend = i ? .73f : .18f};
		vec3_t shade = {i ? -.3f : .7f, i ? .8f : .2f, i ? .1f : .5f};
		vec3_t light = {i ? .06f : .18f, i ? .22f : .07f, i ? .10f : .11f};
		float model[16];
		IdentityMatrix (model);
		model[0] = model[5] = model[10] = (i ? .0023f : .0025f) * (md3 ? 255.0f / 65535.0f : 1.0f);
		model[12] = (i ? .12f : -.9f) + (md3 ? 32768 * model[0] : 0);
		model[13] = (i ? -.2f : -.45f) + (md3 ? 32768 * model[5] : 0);
		model[14] = .4f + (md3 ? 32768 * model[10] : 0);
		GL_DrawAliasFrame (&cbx, &entity, &header, &header, lerp, &skin, NULL, model, 1, false, shade, light, 0, false, false, false, -1);
	}
	if (batch) { CHECK (pending.count == 2 && !fixture.draws); R_AliasBatchEnd (&cbx); }
	CHECK (!cbx.alias_batch && fixture.draws == (batch ? 1u : 2u) && fixture.allocations == fixture.draws);
	CHECK (fixture.uploaded == 2 * sizeof (aliasubo_t) && fixture.draw_sizes[0] == (batch ? 2u : 1u));
	if (!batch) CHECK (fixture.draw_sizes[1] == 1);
	vkCmdEndRenderPass (fixture.command);
	VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers}, .imageExtent = {W, W, 1}};
	vkCmdCopyImageToBuffer (fixture.command, image.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, fixture.readback.handle, 1, &copy);
	VkBufferMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = fixture.readback.handle, .size = VK_WHOLE_SIZE};
	vkCmdPipelineBarrier (fixture.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &barrier, 0, NULL);
	finish_commands ();
	memcpy (pixels, fixture.readback.mapped, layers * IMAGE_BYTES);
}
static void compare_suite (int layers, const char *old_path, const char *new_path, VkShaderModule fragment)
{
	test_image_t image = image_create (W, layers, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	VkAttachmentDescription attachment = {.format = VK_FORMAT_R8G8B8A8_UNORM, .samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
	VkAttachmentReference color = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &color};
	VkSubpassDependency dependencies[2] = {
		{VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0},
		{0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 0}};
	uint32_t mask = 3;
	VkRenderPassMultiviewCreateInfo multiview = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO,
		.subpassCount = 1, .pViewMasks = &mask, .correlationMaskCount = 1, .pCorrelationMasks = &mask};
	VkRenderPassCreateInfo info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .pNext = layers == 2 ? &multiview : NULL,
		.attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass, .dependencyCount = 2, .pDependencies = dependencies};
	VkRenderPass pass;
	VK (vkCreateRenderPass (fixture.device, &info, NULL, &pass));
	VkFramebufferCreateInfo target = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = pass,
		.attachmentCount = 1, .pAttachments = &image.view, .width = W, .height = W, .layers = 1};
	VkFramebuffer framebuffer;
	VK (vkCreateFramebuffer (fixture.device, &target, NULL, &framebuffer));
	VkShaderModule vertices[2] = {shader (old_path), shader (new_path)};
	VkPipeline pipelines[2] = {pipeline_create (pass, vertices[0], fragment), pipeline_create (pass, vertices[1], fragment)};
	byte *pixels[3];
	for (int i = 0; i < 3; ++i) { pixels[i] = malloc (layers * IMAGE_BYTES); CHECK (pixels[i]); }
	for (int md3 = 0; md3 < 2; ++md3)
	{
		for (int variant = 0; variant < 3; ++variant)
			render (pass, framebuffer, image, pipelines[variant != 0], layers, variant == 2, md3, pixels[variant]);
		CHECK (!memcmp (pixels[0], pixels[1], layers * IMAGE_BYTES));
		CHECK (!memcmp (pixels[0], pixels[2], layers * IMAGE_BYTES));
		for (int layer = 0; layer < layers; ++layer)
		{
			unsigned occupied[2] = {0}, red[2] = {0}, green[2] = {0};
			uint32_t hash = 2166136261u;
			const byte *eye = pixels[0] + layer * IMAGE_BYTES;
			for (int p = 0; p < W * W; ++p)
			{
				const byte *rgba = eye + 4 * p;
				for (int c = 0; c < 4; ++c) hash = (hash ^ rgba[c]) * 16777619u;
				if (!(rgba[0] | rgba[1] | rgba[2] | rgba[3])) continue;
				CHECK (rgba[3] == 255 && (rgba[0] | rgba[1] | rgba[2]));
				int half = p % W >= W / 2;
				occupied[half]++; red[half] += rgba[0]; green[half] += rgba[1];
			}
			CHECK (occupied[0] > 64 && occupied[1] > 64 && occupied[0] + occupied[1] < W * W / 2);
			CHECK (red[0] > green[0] && green[1] > red[1]);
			printf ("ALIAS_BATCH_VULKAN_PIXELS mode=%s model=%s eye=%d left=%u right=%u hash=%08x exact=1\n",
				layers == 2 ? "stereo" : "desktop", md3 ? "MD3" : "MDL", layer, occupied[0], occupied[1], hash);
		}
		if (layers == 2) CHECK (memcmp (pixels[0], pixels[0] + IMAGE_BYTES, IMAGE_BYTES));
	}
	for (int i = 0; i < 3; ++i) free (pixels[i]);
	for (int i = 0; i < 2; ++i) { vkDestroyPipeline (fixture.device, pipelines[i], NULL); vkDestroyShaderModule (fixture.device, vertices[i], NULL); }
	vkDestroyFramebuffer (fixture.device, framebuffer, NULL);
	vkDestroyRenderPass (fixture.device, pass, NULL);
	image_destroy (image);
	puts (layers == 2 ? "ALIAS_BATCH_VULKAN_STEREO_PASSED" : "ALIAS_BATCH_VULKAN_DESKTOP_PASSED");
}
int main (int argc, char **argv)
{
	if (argc != 4 && argc != 6) { fprintf (stderr, "usage: %s old.vert.spv new.vert.spv alias.frag.spv [old.stereo.spv new.stereo.spv]\n", argv[0]); return 2; }
	initialize_device ();
	initialize_resources ();
	VkShaderModule fragment = shader (argv[3]);
	compare_suite (1, argv[1], argv[2], fragment);
	if (fixture.multiview && argc == 6) compare_suite (2, argv[4], argv[5], fragment);
	else puts (fixture.multiview ? "ALIAS_BATCH_VULKAN_STEREO_SKIPPED stereo shader paths not supplied" : "ALIAS_BATCH_VULKAN_STEREO_SKIPPED multiview unsupported");
	vkDestroyShaderModule (fixture.device, fragment, NULL);
	vkDestroyPipelineLayout (fixture.device, fixture.layout, NULL);
	vkDestroyDescriptorPool (fixture.device, fixture.descriptors, NULL);
	for (int i = 0; i < 6; ++i) vkDestroyDescriptorSetLayout (fixture.device, fixture.layouts[i], NULL);
	vkDestroySampler (fixture.device, fixture.sampler, NULL);
	image_destroy (fixture.skin);
	test_buffer_t buffers[4] = {fixture.uniform, fixture.vertices, fixture.indices, fixture.readback};
	for (int i = 0; i < 4; ++i) { vkUnmapMemory (fixture.device, buffers[i].memory); vkDestroyBuffer (fixture.device, buffers[i].handle, NULL); vkFreeMemory (fixture.device, buffers[i].memory, NULL); }
	vkDestroyCommandPool (fixture.device, fixture.pool, NULL);
	vkDestroyDevice (fixture.device, NULL);
	vkDestroyInstance (fixture.instance, NULL);
	return 0;
}
