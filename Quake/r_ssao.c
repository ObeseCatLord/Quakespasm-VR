// Experimental screen-space contact occlusion: entity occluders, world receivers.
#include "quakedef.h"
#include "r_ssao.h"

typedef enum
{
	SSAO_DEPTH_PYRAMID,
	SSAO_AO,
	SSAO_FILTERED,
	SSAO_EDGES,
	SSAO_HILBERT,
	SSAO_IMAGE_COUNT
} ssao_image_t;

enum
{
	SSAO_ENTITY_MASK,
	SSAO_SCENE_DEPTH,
	SSAO_DEPTH_COUNT
};

cvar_t					 r_ssao = {"r_ssao", "1", CVAR_ARCHIVE};
static cvar_t			 r_vr_ssao = {"vr_ssao", "0", CVAR_ARCHIVE};
static cvar_t			 r_ssao_radius = {"r_ssao_radius", "32", CVAR_ARCHIVE};
static cvar_t			 r_ssao_strength = {"r_ssao_strength", "1.0", CVAR_ARCHIVE};
vulkan_pipeline_layout_t ssao_layout;
vulkan_pipeline_t		 ssao_pipelines[MAIN_RENDER_PASS_VARIANT_COUNT];
vulkan_pipeline_layout_t ssao_compute_layout;
vulkan_pipeline_t		 ssao_prepare_pipeline, ssao_evaluate_pipeline, ssao_filter_pipeline;
static VkImage			 scene_depth;
#define SSAO_MAX_EYES 2
static qboolean			 ssao_stereo;
static VkImageView		 views[SSAO_DEPTH_COUNT][SSAO_MAX_EYES];
static VkDescriptorSet	 descriptors[SSAO_DEPTH_COUNT][SSAO_MAX_EYES];
static VkImageView		 depth_array_views[SSAO_DEPTH_COUNT];
static VkDescriptorSet	 composite_descriptors[4];
static VkSampler		 sampler;
#ifdef _DEBUG
static cvar_t r_ssao_debug = {"r_ssao_debug", "0", CVAR_NONE};
#endif
vulkan_pipeline_t		 ssao_mip_pipeline;
vulkan_desc_set_layout_t ssao_mip_set_layout;
static VkImage		   working_images[SSAO_IMAGE_COUNT];
static VkImageView	   working_views[SSAO_IMAGE_COUNT][SSAO_MAX_EYES];
static VkImageView	   working_array_views[SSAO_IMAGE_COUNT];
static vulkan_memory_t working_memory[SSAO_IMAGE_COUNT];
static VkDescriptorSet working_read[SSAO_IMAGE_COUNT][SSAO_MAX_EYES], working_write[SSAO_IMAGE_COUNT][SSAO_MAX_EYES];

static VkImageView	   mip_views[5][SSAO_MAX_EYES];
static VkDescriptorSet prepared_read[SSAO_MAX_EYES], prepared_write[SSAO_MAX_EYES];
static VkDescriptorSet mip_descriptors[SSAO_MAX_EYES];

void R_InitSSAO (void)
{
	Cvar_RegisterVariable (&r_ssao);
	Cvar_RegisterVariable (&r_vr_ssao);
	Cvar_RegisterVariable (&r_ssao_radius);
	Cvar_RegisterVariable (&r_ssao_strength);
#ifdef _DEBUG
	Cvar_RegisterVariable (&r_ssao_debug);
#endif
}

qboolean R_SSAOEnabled (void)
{
	return r_ssao.value > 0 && (!vulkan_globals.stereo_active || r_vr_ssao.value > 0);
}

// The spatial noise repeats every 64 pixels; build its index once per resource creation.
static void R_UploadSSAOHilbert (void)
{
	VkBuffer			 buffer;
	VkCommandBuffer		 cb;
	int					 offset;
	uint16_t			*data = (uint16_t *)R_StagingAllocate (64 * 64 * sizeof (*data), 4, &cb, &buffer, &offset);
	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = working_images[SSAO_HILBERT],
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
	vkCmdPipelineBarrier (cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
	const VkBufferImageCopy region = {.bufferOffset = offset, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {64, 64, 1}};
	vkCmdCopyBufferToImage (cb, buffer, working_images[SSAO_HILBERT], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	vkCmdPipelineBarrier (cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
	R_StagingBeginCopy ();
	for (uint32_t y = 0; y < 64; ++y)
		for (uint32_t x = 0; x < 64; ++x)
		{
			uint32_t px = x, py = y, index = 0;
			for (uint32_t level = 32; level; level /= 2)
			{
				uint32_t rx = (px & level) != 0, ry = (py & level) != 0;
				index += level * level * ((3 * rx) ^ ry);
				if (!ry)
				{
					if (rx)
					{
						px = 63 - px;
						py = 63 - py;
					}
					uint32_t temp = px;
					px = py;
					py = temp;
				}
			}
			data[y * 64 + x] = index;
		}
	R_StagingEndCopy ();
}

static VkImageView R_CreateSSAOImageView (VkImage image, VkFormat format, VkImageAspectFlags aspect,
	uint32_t base_mip, uint32_t mip_count, uint32_t base_layer, uint32_t layer_count, VkImageViewType type)
{
	VkImageView view;
	const VkImageViewCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image,
		.viewType = type,
		.format = format,
		.subresourceRange = {aspect, base_mip, mip_count, base_layer, layer_count}};
	if (vkCreateImageView (vulkan_globals.device, &info, NULL, &view) != VK_SUCCESS)
		Sys_Error ("Couldn't create SSAO image view");
	return view;
}

static VkDescriptorSet R_CreateSSAOImageDescriptor (vulkan_desc_set_layout_t *layout, VkDescriptorType type,
	VkImageView view, VkImageLayout image_layout)
{
	VkDescriptorSet descriptor = R_AllocateDescriptorSet (layout);
	const VkDescriptorImageInfo image = {sampler, view, image_layout};
	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descriptor,
		.descriptorCount = 1,
		.descriptorType = type,
		.pImageInfo = &image};
	vkUpdateDescriptorSets (vulkan_globals.device, 1, &write, 0, NULL);
	return descriptor;
}

void R_CreateSSAO (VkImage depth)
{
	if (!R_SSAOEnabled ())
		return;
	scene_depth = depth;
	ssao_stereo = vulkan_globals.stereo_active;
	const int eye_count = ssao_stereo ? 2 : 1;
	const VkSamplerCreateInfo sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_NEAREST,
		.minFilter = VK_FILTER_NEAREST,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.maxLod = 4,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	};
	if (vkCreateSampler (vulkan_globals.device, &sampler_info, NULL, &sampler) != VK_SUCCESS)
		Sys_Error ("Couldn't create SSAO sampler");

	for (int i = 0; i < SSAO_DEPTH_COUNT; ++i)
	{
		const VkImageAspectFlags aspect = i == SSAO_ENTITY_MASK ? VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
		for (int eye = 0; eye < eye_count; ++eye)
		{
			views[i][eye] = R_CreateSSAOImageView (
				scene_depth, vulkan_globals.depth_format, aspect, 0, 1, eye, 1, VK_IMAGE_VIEW_TYPE_2D);
			descriptors[i][eye] = R_CreateSSAOImageDescriptor (&vulkan_globals.single_texture_set_layout,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, views[i][eye], VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
		}
		if (ssao_stereo)
		{
			depth_array_views[i] = R_CreateSSAOImageView (
				scene_depth, vulkan_globals.depth_format, aspect, 0, 1, 0, 2, VK_IMAGE_VIEW_TYPE_2D_ARRAY);
			composite_descriptors[i] = R_CreateSSAOImageDescriptor (&vulkan_globals.single_texture_set_layout,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, depth_array_views[i], VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
		}
	}

	for (int i = 0; i < SSAO_IMAGE_COUNT; ++i)
	{
		const VkFormat format = i == SSAO_DEPTH_PYRAMID ? VK_FORMAT_R16G16_SFLOAT
			: i == SSAO_EDGES ? VK_FORMAT_R8_UNORM
			: i == SSAO_HILBERT ? VK_FORMAT_R16_UINT
			: VK_FORMAT_R8_UNORM;
		const VkImageCreateInfo image_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = format,
			.extent = {i == SSAO_HILBERT ? 64 : vid.render_width, i == SSAO_HILBERT ? 64 : vid.render_height, 1},
			.mipLevels = i == SSAO_DEPTH_PYRAMID ? 5 : 1,
			.arrayLayers = ssao_stereo && i != SSAO_HILBERT ? 2 : 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | (i == SSAO_HILBERT ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : VK_IMAGE_USAGE_STORAGE_BIT),
		};
		if (vkCreateImage (vulkan_globals.device, &image_info, NULL, &working_images[i]) != VK_SUCCESS)
			Sys_Error ("Couldn't create SSAO working image");
		VkMemoryRequirements requirements;
		vkGetImageMemoryRequirements (vulkan_globals.device, working_images[i], &requirements);
		const VkMemoryDedicatedAllocateInfoKHR dedicated = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR, .image = working_images[i]};
		VkMemoryAllocateInfo allocation = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.pNext = vulkan_globals.dedicated_allocation ? &dedicated : NULL,
			.allocationSize = requirements.size,
			.memoryTypeIndex = GL_MemoryTypeFromProperties (requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0),
		};
		R_AllocateVulkanMemory (&working_memory[i], &allocation, VULKAN_MEMORY_TYPE_DEVICE, &num_vulkan_misc_allocations);
		if (vkBindImageMemory (vulkan_globals.device, working_images[i], working_memory[i].handle, 0) != VK_SUCCESS)
			Sys_Error ("Couldn't bind SSAO working image memory");

		for (int eye = 0; eye < eye_count; ++eye)
		{
			const uint32_t layer = i == SSAO_HILBERT ? 0 : eye;
			working_views[i][eye] = R_CreateSSAOImageView (working_images[i], format, VK_IMAGE_ASPECT_COLOR_BIT, 0,
				image_info.mipLevels, layer, 1, VK_IMAGE_VIEW_TYPE_2D);
			working_read[i][eye] = R_CreateSSAOImageDescriptor (&vulkan_globals.single_texture_set_layout,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, working_views[i][eye], VK_IMAGE_LAYOUT_GENERAL);
			if (i != SSAO_DEPTH_PYRAMID && i != SSAO_HILBERT)
				working_write[i][eye] = R_CreateSSAOImageDescriptor (&vulkan_globals.single_texture_cs_write_set_layout,
					VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, working_views[i][eye], VK_IMAGE_LAYOUT_GENERAL);
		}
		if (ssao_stereo && (i == SSAO_AO || i == SSAO_DEPTH_PYRAMID))
		{
			working_array_views[i] = R_CreateSSAOImageView (
				working_images[i], format, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2, VK_IMAGE_VIEW_TYPE_2D_ARRAY);
			const int descriptor_index = i == SSAO_AO ? 2 : 3;
			composite_descriptors[descriptor_index] = R_CreateSSAOImageDescriptor (&vulkan_globals.single_texture_set_layout,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, working_array_views[i], VK_IMAGE_LAYOUT_GENERAL);
		}
	}

	R_UploadSSAOHilbert ();
	for (int eye = 0; eye < eye_count; ++eye)
	{
		for (int mip = 0; mip < 5; ++mip)
			mip_views[mip][eye] = R_CreateSSAOImageView (working_images[SSAO_DEPTH_PYRAMID], VK_FORMAT_R16G16_SFLOAT,
				VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, eye, 1, VK_IMAGE_VIEW_TYPE_2D);
		prepared_read[eye] = R_CreateSSAOImageDescriptor (&vulkan_globals.single_texture_set_layout,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, mip_views[0][eye], VK_IMAGE_LAYOUT_GENERAL);
		prepared_write[eye] = R_CreateSSAOImageDescriptor (&vulkan_globals.single_texture_cs_write_set_layout,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, mip_views[0][eye], VK_IMAGE_LAYOUT_GENERAL);
		mip_descriptors[eye] = R_AllocateDescriptorSet (&ssao_mip_set_layout);
		for (int binding = 0; binding < 6; ++binding)
		{
			const VkDescriptorImageInfo image = {
				sampler, binding == 0 ? views[SSAO_SCENE_DEPTH][eye] : mip_views[binding - 1][eye],
				binding == 0 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL};
			const VkWriteDescriptorSet write = {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = mip_descriptors[eye],
				.dstBinding = binding,
				.descriptorCount = 1,
				.descriptorType = binding == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.pImageInfo = &image};
			vkUpdateDescriptorSets (vulkan_globals.device, 1, &write, 0, NULL);
		}
	}
}

void R_DestroySSAO (void)
{
	for (int i = 0; i < countof (composite_descriptors); ++i)
		if (composite_descriptors[i])
			R_FreeDescriptorSet (composite_descriptors[i], &vulkan_globals.single_texture_set_layout);
	memset (composite_descriptors, 0, sizeof (composite_descriptors));
	for (int eye = 0; eye < SSAO_MAX_EYES; ++eye)
	{
		if (mip_descriptors[eye])
			R_FreeDescriptorSet (mip_descriptors[eye], &ssao_mip_set_layout);
		mip_descriptors[eye] = VK_NULL_HANDLE;
		if (prepared_read[eye])
			R_FreeDescriptorSet (prepared_read[eye], &vulkan_globals.single_texture_set_layout);
		if (prepared_write[eye])
			R_FreeDescriptorSet (prepared_write[eye], &vulkan_globals.single_texture_cs_write_set_layout);
		prepared_read[eye] = prepared_write[eye] = VK_NULL_HANDLE;
		for (int mip = 0; mip < countof (mip_views); ++mip)
		{
			if (mip_views[mip][eye])
				vkDestroyImageView (vulkan_globals.device, mip_views[mip][eye], NULL);
			mip_views[mip][eye] = VK_NULL_HANDLE;
		}
	}
	for (int i = 0; i < SSAO_IMAGE_COUNT; ++i)
	{
		for (int eye = 0; eye < SSAO_MAX_EYES; ++eye)
		{
			if (working_read[i][eye])
				R_FreeDescriptorSet (working_read[i][eye], &vulkan_globals.single_texture_set_layout);
			if (working_write[i][eye])
				R_FreeDescriptorSet (working_write[i][eye], &vulkan_globals.single_texture_cs_write_set_layout);
			if (working_views[i][eye])
				vkDestroyImageView (vulkan_globals.device, working_views[i][eye], NULL);
			working_read[i][eye] = working_write[i][eye] = VK_NULL_HANDLE;
			working_views[i][eye] = VK_NULL_HANDLE;
		}
		if (working_array_views[i])
			vkDestroyImageView (vulkan_globals.device, working_array_views[i], NULL);
		working_array_views[i] = VK_NULL_HANDLE;
		if (working_images[i])
			vkDestroyImage (vulkan_globals.device, working_images[i], NULL);
		if (working_memory[i].handle)
			R_FreeVulkanMemory (&working_memory[i], &num_vulkan_misc_allocations);
		working_images[i] = VK_NULL_HANDLE;
	}
	for (int i = 0; i < SSAO_DEPTH_COUNT; ++i)
	{
		for (int eye = 0; eye < SSAO_MAX_EYES; ++eye)
		{
			if (descriptors[i][eye])
				R_FreeDescriptorSet (descriptors[i][eye], &vulkan_globals.single_texture_set_layout);
			if (views[i][eye])
				vkDestroyImageView (vulkan_globals.device, views[i][eye], NULL);
			descriptors[i][eye] = VK_NULL_HANDLE;
			views[i][eye] = VK_NULL_HANDLE;
		}
		if (depth_array_views[i])
			vkDestroyImageView (vulkan_globals.device, depth_array_views[i], NULL);
		depth_array_views[i] = VK_NULL_HANDLE;
	}
	scene_depth = VK_NULL_HANDLE;
	ssao_stereo = false;
	if (sampler)
		vkDestroySampler (vulkan_globals.device, sampler, NULL);
	sampler = VK_NULL_HANDLE;
}

static ssao_constants_t R_SSAOConstants (void)
{
	return (ssao_constants_t){
		.viewport = {r_scene_vrect.x, r_scene_vrect.y, 1.0f / r_scene_vrect.width, 1.0f / r_scene_vrect.height},
		.projection =
			{1.0f / vulkan_globals.projection_matrix[0], 1.0f / vulkan_globals.projection_matrix[5], vulkan_globals.projection_matrix[14],
			 vulkan_globals.sample_count},
		.settings =
			{CLAMP (1, r_ssao_radius.value, 128), CLAMP (0, r_ssao_strength.value, 1), Fog_GetDensity () / 64.0f,
#ifdef _DEBUG
			 CLAMP (0, r_ssao_debug.value, 3)
#else
			 0
#endif
			},
	};
}

static void R_SSAOSetEyeProjection (ssao_constants_t *constants, int eye)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	if (!frame || eye < 0 || eye >= 2)
		return;
	const vrxr_view_t *view = &frame->views[eye];
	constants->viewport[0] = vid.render_width;
	constants->viewport[1] = vid.render_height;
	constants->viewport[2] = view->right - view->left;
	constants->viewport[3] = view->down - view->up;
	constants->projection[0] = view->left;
	constants->projection[1] = view->up;
}

void R_PrepareSSAOWorldDepth (cb_context_t *cbx)
{
	if (!R_SSAOEnabled () || !r_scene_vrect.width || !r_scene_vrect.height)
		return;
	const VkCommandBuffer cb = cbx->cb;
	const int eye_count = ssao_stereo ? 2 : 1;
	for (int eye = 0; eye < eye_count; ++eye)
	{
		VkImageMemoryBarrier barriers[] = {
			{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			 .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			 .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			 .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			 .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
			 .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			 .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			 .image = scene_depth,
			 .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, eye, 1}},
			{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			 .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			 .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			 .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			 .newLayout = VK_IMAGE_LAYOUT_GENERAL,
			 .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			 .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			 .image = working_images[SSAO_DEPTH_PYRAMID],
			 .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 5, eye, 1}}};
		vulkan_globals.vk_cmd_pipeline_barrier (
			cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, countof (barriers), barriers);
		ssao_constants_t constants = R_SSAOConstants ();
		const VkDescriptorSet sets[] = {descriptors[SSAO_ENTITY_MASK][eye], descriptors[SSAO_SCENE_DEPTH][eye], prepared_write[eye]};
		vulkan_globals.vk_cmd_bind_pipeline (cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_prepare_pipeline.handle);
		vulkan_globals.vk_cmd_bind_descriptor_sets (cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_prepare_pipeline.layout.handle, 0,
			countof (sets), sets, 0, NULL);
		vulkan_globals.vk_cmd_push_constants (cb, ssao_prepare_pipeline.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0,
			sizeof (constants), &constants);
		vulkan_globals.vk_cmd_dispatch (cb, (vid.render_width + 7) / 8, (vid.render_height + 7) / 8, 1);
		barriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barriers[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		barriers[0].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		barriers[0].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		vulkan_globals.vk_cmd_pipeline_barrier (
			cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			0, 0, NULL, 0, NULL, 1, barriers);
	}
}

void R_ComputeSSAO (cb_context_t *cbx)
{
	const VkCommandBuffer cb = cbx->cb;
	const int eye_count = ssao_stereo ? 2 : 1;
	// Transition both depth layers before the one multiview composite, even if the view rectangle is empty.
	for (int eye = 0; eye < eye_count; ++eye)
	{
		const VkImageMemoryBarrier depth_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = scene_depth,
			.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, eye, 1}};
		vulkan_globals.vk_cmd_pipeline_barrier (
			cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, NULL, 0, NULL, 1, &depth_barrier);
	}
	if (!r_scene_vrect.width || !r_scene_vrect.height)
		return;

	for (int eye = 0; eye < eye_count; ++eye)
	{
		ssao_constants_t constants = R_SSAOConstants ();
		// Preserve prepared world depth; discard only this eye's other outputs.
		VkImageMemoryBarrier barriers[SSAO_HILBERT];
		for (int i = 0; i < SSAO_HILBERT; ++i)
			barriers[i] = (VkImageMemoryBarrier){
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
				.oldLayout = i == SSAO_DEPTH_PYRAMID ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = working_images[i],
				.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, i == SSAO_DEPTH_PYRAMID ? 5 : 1, eye, 1}};
		vulkan_globals.vk_cmd_pipeline_barrier (
			cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, NULL, 0, NULL, countof (barriers), barriers);

		const uint32_t width = vid.render_width, height = vid.render_height;
		const uint32_t groups_x = (width + 7) / 8, groups_y = (height + 7) / 8;
		const VkMemoryBarrier read_barrier = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};

		// Resolve combined depth and build all depth levels in one dispatch.
		vulkan_globals.vk_cmd_bind_pipeline (cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_mip_pipeline.handle);
		vulkan_globals.vk_cmd_bind_descriptor_sets (
			cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_mip_pipeline.layout.handle, 0, 1, &mip_descriptors[eye], 0, NULL);
		vulkan_globals.vk_cmd_push_constants (cb, ssao_mip_pipeline.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0,
			sizeof (constants), &constants);
		vulkan_globals.vk_cmd_dispatch (cb, (width + 15) / 16, (height + 15) / 16, 1);
		vulkan_globals.vk_cmd_pipeline_barrier (
			cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &read_barrier, 0, NULL, 0, NULL);

		// The desktop projection calculation remains byte-for-byte equivalent.
		constants.settings[2] = 0;
		if (ssao_stereo)
			R_SSAOSetEyeProjection (&constants, eye);
		else
		{
			constants.viewport[0] = width;
			constants.viewport[1] = height;
			constants.viewport[2] = 2.0f * width / r_scene_vrect.width / vulkan_globals.projection_matrix[0];
			constants.viewport[3] = 2.0f * height / r_scene_vrect.height / vulkan_globals.projection_matrix[5];
			constants.projection[0] = (-1.0f - 2.0f * r_scene_vrect.x / r_scene_vrect.width) / vulkan_globals.projection_matrix[0];
			constants.projection[1] = (-1.0f - 2.0f * (r_scene_vrect.y) / r_scene_vrect.height) / vulkan_globals.projection_matrix[5];
		}

		constants.settings[3] = (int)CLAMP (1, r_ssao.value, 3);
		const VkDescriptorSet evaluate_sets[] = {
			working_read[SSAO_DEPTH_PYRAMID][eye], working_read[SSAO_HILBERT][0],
			working_write[SSAO_AO][eye], working_write[SSAO_EDGES][eye]};
		vulkan_globals.vk_cmd_bind_pipeline (cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_evaluate_pipeline.handle);
		vulkan_globals.vk_cmd_bind_descriptor_sets (
			cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_evaluate_pipeline.layout.handle, 0, countof (evaluate_sets), evaluate_sets, 0, NULL);
		vulkan_globals.vk_cmd_push_constants (cb, ssao_evaluate_pipeline.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0,
			sizeof (constants), &constants);
		vulkan_globals.vk_cmd_dispatch (cb, groups_x, groups_y, 1);
		vulkan_globals.vk_cmd_pipeline_barrier (
			cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &read_barrier, 0, NULL, 0, NULL);

		const uint32_t filter_groups_x = (width + 15) / 16;
		const VkDescriptorSet filter_sets[] = {
			working_read[SSAO_AO][eye], working_read[SSAO_EDGES][eye], working_write[SSAO_FILTERED][eye]};
		vulkan_globals.vk_cmd_bind_pipeline (cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_filter_pipeline.handle);
		vulkan_globals.vk_cmd_bind_descriptor_sets (
			cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_filter_pipeline.layout.handle, 0, countof (filter_sets), filter_sets, 0, NULL);
		vulkan_globals.vk_cmd_push_constants (cb, ssao_filter_pipeline.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0,
			sizeof (constants), &constants);
		vulkan_globals.vk_cmd_dispatch (cb, filter_groups_x, groups_y, 1);
		const VkMemoryBarrier filter_barrier = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
		vulkan_globals.vk_cmd_pipeline_barrier (
			cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &filter_barrier, 0, NULL, 0, NULL);

		const VkDescriptorSet final_sets[] = {
			working_read[SSAO_FILTERED][eye], working_read[SSAO_EDGES][eye], working_write[SSAO_AO][eye]};
		constants.settings[2] = 1;
		vulkan_globals.vk_cmd_bind_pipeline (cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_filter_pipeline.handle);
		vulkan_globals.vk_cmd_bind_descriptor_sets (
			cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_filter_pipeline.layout.handle, 0, countof (final_sets), final_sets, 0, NULL);
		vulkan_globals.vk_cmd_push_constants (cb, ssao_filter_pipeline.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0,
			sizeof (constants), &constants);
		vulkan_globals.vk_cmd_dispatch (cb, filter_groups_x, groups_y, 1);
#ifdef __APPLE__
		const VkMemoryBarrier composite_barrier = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
		vulkan_globals.vk_cmd_pipeline_barrier (
			cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &composite_barrier, 0, NULL, 0, NULL);
#endif
	}
}

void R_DrawSSAOTask (void *unused)
{
	if (!R_SSAOEnabled ())
		return;
	cb_context_t *cbx = vulkan_globals.secondary_cb_contexts[SCBX_ENTITY_SSAO];
	const VkRect2D rect = {{r_scene_vrect.x, r_scene_vrect.y}, {r_scene_vrect.width, r_scene_vrect.height}};
	if (!rect.extent.width || !rect.extent.height)
		return;
	const VkViewport viewport = {rect.offset.x, rect.offset.y, rect.extent.width, rect.extent.height, 0, 1};
	vkCmdSetViewport (cbx->cb, 0, 1, &viewport);
	vkCmdSetScissor (cbx->cb, 0, 1, &rect);
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, ssao_pipelines[cbx->pipeline_variant]);
	const VkDescriptorSet desktop_sets[] = {
		descriptors[SSAO_ENTITY_MASK][0], descriptors[SSAO_SCENE_DEPTH][0], working_read[SSAO_AO][0], prepared_read[0]};
	const VkDescriptorSet *sets = ssao_stereo ? composite_descriptors : desktop_sets;
	vulkan_globals.vk_cmd_bind_descriptor_sets (cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ssao_layout.handle, 0, 4, sets, 0, NULL);
	const ssao_constants_t constants = R_SSAOConstants ();
	R_PushConstants (cbx, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (constants), &constants);
	vulkan_globals.vk_cmd_draw (cbx->cb, 3, 1, 0, 0);
}
