/* Exercise the production frame description/recorder with Vulkan command spies.
 * No Vulkan device, WSI window, runtime, or copied frame graph is used. */
#include "../Quake/r_passes.c"

vulkanglobals_t vulkan_globals;
cvar_t			r_ssao;
VkAccelerationStructureKHR bmodel_tlas = VK_NULL_HANDLE;

static unsigned pass_begins, pass_ends, scene_commands, ui_commands, readbacks, ssao_steps;
static qboolean in_pass;
static unsigned created_passes;

void GL_SetObjectName (uint64_t handle, VkObjectType type, const char *name) {}
qboolean R_SSAOEnabled (void)
{
	return r_ssao.value > 0;
}
#ifndef RENDER_ACQUIRE_FIXTURE_EXTERNAL_PASS_SPY
VKAPI_ATTR VkResult VKAPI_CALL
vkCreateRenderPass (VkDevice device, const VkRenderPassCreateInfo *info, const VkAllocationCallbacks *allocator, VkRenderPass *pass)
{
	const VkRenderPassMultiviewCreateInfo *views = info->pNext;
	assert (!!views == !!vulkan_globals.stereo_active);
	if (views)
	{
		assert (views->sType == VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO);
		assert (views->subpassCount == info->subpassCount);
		for (uint32_t i = 0; i < views->subpassCount; ++i)
			assert (views->pViewMasks[i] == 3);
		assert (views->correlationMaskCount == 1 && views->pCorrelationMasks[0] == 3);
	}
	for (uint32_t i = 0; i < info->attachmentCount; ++i)
		if (info->pAttachments[i].format == current_layout.swapchain_format)
			assert (info->pAttachments[i].finalLayout == (views ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR));
	*pass = (VkRenderPass)(uintptr_t)++created_passes;
	return VK_SUCCESS;
}
#endif

void Sys_Error (const char *format, ...)
{
	va_list args;
	va_start (args, format);
	vfprintf (stderr, format, args);
	va_end (args);
	abort ();
}

void Con_Printf (const char *format, ...) { (void)format; }
VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp (VkCommandBuffer command_buffer, VkPipelineStageFlagBits stage,
	VkQueryPool query_pool, uint32_t query) { assert (query_pool != VK_NULL_HANDLE); }

qboolean Sky_NeedStencil (void)
{
	return false;
}
void R_PrepareSSAOWorldDepth (cb_context_t *cbx)
{
	++ssao_steps;
}
void R_ComputeSSAO (cb_context_t *cbx)
{
	++ssao_steps;
}
void	   GL_SetCanvas (cb_context_t *cbx, canvastype canvas) {}
VkPipeline R_ResolvePipelineInstance (const cb_context_t *cbx, vulkan_pipeline_t pipeline)
{
	return pipeline.handle;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass (VkCommandBuffer cb, const VkRenderPassBeginInfo *info, VkSubpassContents contents)
{
	assert (!in_pass && info->framebuffer != VK_NULL_HANDLE);
	in_pass = true;
	++pass_begins;
}
VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass (VkCommandBuffer cb, VkSubpassContents contents)
{
	assert (in_pass);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass (VkCommandBuffer cb)
{
	assert (in_pass);
	in_pass = false;
	++pass_ends;
}
VKAPI_ATTR void VKAPI_CALL vkCmdExecuteCommands (VkCommandBuffer cb, uint32_t count, const VkCommandBuffer *secondary)
{
	assert (in_pass);
	for (uint32_t i = 0; i < count; ++i)
	{
		const unsigned context = ((uintptr_t)secondary[i] - 1) / 16;
		assert (context < SCBX_NUM);
		if (context == SCBX_GUI || context == SCBX_POST_PROCESS)
			++ui_commands;
		else
			++scene_commands;
	}
}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier (
	VkCommandBuffer cb, VkPipelineStageFlags src, VkPipelineStageFlags dst, VkDependencyFlags flags, uint32_t memories, const VkMemoryBarrier *memory,
	uint32_t buffers, const VkBufferMemoryBarrier *buffer, uint32_t images, const VkImageMemoryBarrier *image)
{
	assert (!in_pass);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets (
	VkCommandBuffer cb, VkPipelineBindPoint bind, VkPipelineLayout layout, uint32_t first, uint32_t count, const VkDescriptorSet *sets, uint32_t offsets,
	const uint32_t *dynamic_offsets)
{
	abort (); // Screen effects are disabled in this acquisition-specific fixture.
}
VKAPI_ATTR void VKAPI_CALL vkCmdDispatch (VkCommandBuffer cb, uint32_t x, uint32_t y, uint32_t z)
{
	abort ();
}

static void readback (void *data)
{
	assert (!in_pass && data == &readbacks);
	++readbacks;
}

static void check_variant (main_render_pass_variant_t variant, bool ssao, bool acquired, bool stereo, VkSampleCountFlagBits samples)
{
	static cb_context_t	 contexts[SCBX_NUM][16];
	static VkFramebuffer framebuffers[MAX_FRAME_PASSES][NUM_COLOR_BUFFERS];
	memset (&current_layout, 0, sizeof (current_layout));
	memset (physical_passes, 0, sizeof (physical_passes));
	current_layout.samples = samples;
	current_layout.stereo = stereo;
	current_layout.swapchain_format = stereo ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM;
	vulkan_globals.stereo_active = stereo;
	vulkan_globals.sample_count = samples;
	vulkan_globals.color_format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	vulkan_globals.depth_format = VK_FORMAT_D32_SFLOAT_S8_UINT;
	r_ssao.value = ssao;
	frame_desc_t *frame = &current_layout.variants[variant];
	R_DescribeFrame (frame, variant);
	created_passes = 0;
	R_CreateScenePasses (variant);
	R_CreateUIPasses (variant);
	assert (created_passes == frame->pass_count * MAIN_RENDER_PASS_STENCIL_COUNT);
	unsigned expected_scene_passes = 0;
	for (uint32_t i = 0; i < frame->pass_count; ++i)
	{
		physical_pass_t *physical = &physical_passes[variant][i];
		if (frame->passes[i].target == FRAME_TARGET_UI && !acquired)
			continue; // Deliberately no accessible UI framebuffer after failed acquire.
		if (frame->passes[i].target == FRAME_TARGET_SCENE)
			++expected_scene_passes;
		physical->framebuffer_count = NUM_COLOR_BUFFERS;
		physical->framebuffers = framebuffers[i];
		for (int j = 0; j < NUM_COLOR_BUFFERS; ++j)
			framebuffers[i][j] = (VkFramebuffer)(uintptr_t)(i * NUM_COLOR_BUFFERS + j + 1);
	}
	for (unsigned i = 0; i < SCBX_NUM; ++i)
	{
		assert (SECONDARY_CB_MULTIPLICITY[i] <= 16);
		vulkan_globals.secondary_cb_contexts[i] = contexts[i];
		for (int j = 0; j < SECONDARY_CB_MULTIPLICITY[i]; ++j)
			contexts[i][j].cb = (VkCommandBuffer)(uintptr_t)(i * 16 + j + 1);
	}
	for (unsigned i = 0; i < PCBX_NUM; ++i)
		vulkan_globals.primary_cb_contexts[i].cb = (VkCommandBuffer)(uintptr_t)(1000 + i);
	pass_begins = pass_ends = scene_commands = ui_commands = readbacks = ssao_steps = 0;
	in_pass = false;
	end_rendering_parms_t parms = {
		.swapchain = true, // Intent must not substitute for actual acquisition.
		.use_oit = variant == MAIN_RENDER_PASS_OIT,
		.use_mboit = variant == MAIN_RENDER_PASS_MBOIT,
		.vid_width = 640,
		.vid_height = 480,
		.render_width = 320,
		.render_height = 240,
	};
	VkCommandBuffer commands[PCBX_NUM];
	bool ssao_timestamps_written = true;
	const uint32_t	count = R_RecordFrame (&parms, acquired, acquired ? 1 : UINT32_MAX, commands, countof (commands),
		readback, &readbacks, VK_NULL_HANDLE, 0, &ssao_timestamps_written);
	assert (count == 4); // Three prepared buffers plus the scene/UI recorder.
	assert (!ssao_timestamps_written); // This command-spy fixture supplies no query pool.
	assert (commands[0] == vulkan_globals.primary_cb_contexts[PCBX_BUILD_ACCELERATION_STRUCTURES].cb);
	assert (commands[1] == vulkan_globals.primary_cb_contexts[PCBX_UPDATE_LIGHTMAPS].cb);
	assert (commands[2] == vulkan_globals.primary_cb_contexts[PCBX_UPDATE_WARP].cb);
	assert (commands[3] == vulkan_globals.primary_cb_contexts[PCBX_RENDER_PASSES].cb);
	assert (!in_pass && pass_begins == pass_ends);
	assert (pass_begins == expected_scene_passes + acquired);
	assert (scene_commands > 0 && ui_commands == (acquired ? 2 : 0));
	assert (readbacks == acquired && ssao_steps == (ssao ? 2 : 0));
}

int main (void)
{
	for (int stereo = 0; stereo < 2; ++stereo)
	{
		for (int msaa = 0; msaa < 2; ++msaa)
		{
			for (int variant = 0; variant < MAIN_RENDER_PASS_VARIANT_COUNT; ++variant)
			{
				for (int ssao = 0; ssao < 2; ++ssao)
				{
					check_variant (variant, ssao, true, stereo, msaa ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT);
					const unsigned acquired_scene_commands = scene_commands;
					check_variant (variant, ssao, false, stereo, msaa ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT);
					assert (scene_commands == acquired_scene_commands);
				}
			}
		}
	}
	puts ("Render acquisition boundary: desktop/stereo OIT/SSAO/MSAA variants, view masks, output layouts and unowned presentation suppression passed");
	return 0;
}
