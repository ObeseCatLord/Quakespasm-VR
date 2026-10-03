/* Native density pass/framebuffer constructors with controlled Vulkan dispatch. */
#include <assert.h>
#include <stdlib.h>
#ifdef NDEBUG
#error "OpenXR pass/framebuffer fixture requires assertions"
#endif
#define RENDER_ACQUIRE_FIXTURE_EXTERNAL_PASS_SPY
#define main InheritedRenderAcquireFixtureMain
#include "render_acquire_fixture.c"
#undef main
#undef RENDER_ACQUIRE_FIXTURE_EXTERNAL_PASS_SPY

viddef_t vid;
cvar_t r_oit;
oit_mode_t frame_oit_mode;
#define CTX_CAP 16
#define RESOURCE_CAP 128
static cb_context_t inheritedcontexts[SCBX_NUM][CTX_CAP];
static unsigned native_context_count[SCBX_NUM];
static VkImageView density_views[3], swapchain_views[3];
static render_framebuffer_images_t framebuffer_images;
static unsigned image_destroys, image_view_destroys, null_fb_destroys;
static uintptr_t next_pass_handle = 1000, next_fb_handle = 2000;
typedef struct { VkRenderPass handle; bool density, warp, destroyed; } pass_t;
typedef struct { VkFramebuffer handle; bool destroyed; } framebuffer_t;
static pass_t passes[RESOURCE_CAP];
static framebuffer_t framebuffers[RESOURCE_CAP];
static unsigned pass_count, framebuffer_count, ordinary_calls, density_calls;
static unsigned fb_calls, density_fb_calls, destroyed_passes, destroyed_fbs;
static bool reject_pass2, reject_fb2;
static const VkRenderPass bad_pass = (VkRenderPass)(uintptr_t)0xFA110001;
static const VkFramebuffer bad_fb = (VkFramebuffer)(uintptr_t)0xFA110002;

void Cvar_SetValueQuick (cvar_t *var, const float value)
{
	(void)var; (void)value;
	assert (!"Valid SSAO capabilities must not change the cvar");
	abort ();
}

static pass_t *find_pass (VkRenderPass h)
{
	for (unsigned i=0; i<pass_count; ++i) if (passes[i].handle==h) return &passes[i];
	return NULL;
}
static framebuffer_t *find_fb (VkFramebuffer h)
{
	for (unsigned i=0; i<framebuffer_count; ++i) if (framebuffers[i].handle==h) return &framebuffers[i];
	return NULL;
}
static VkRenderPass admit_pass (bool density, bool warp)
{
	assert (pass_count<RESOURCE_CAP);
	VkRenderPass h=(VkRenderPass)(uintptr_t)++next_pass_handle;
	assert (h && !find_pass(h));
	passes[pass_count++]=(pass_t){h,density,warp,false};
	return h;
}
static VkFramebuffer admit_fb (void)
{
	assert (framebuffer_count<RESOURCE_CAP);
	VkFramebuffer h=(VkFramebuffer)(uintptr_t)++next_fb_handle;
	assert (h && !find_fb(h));
	framebuffers[framebuffer_count++]=(framebuffer_t){h,false};
	return h;
}
static bool is_density_view (VkImageView h)
{
	for (int i=0;i<3;++i) if (density_views[i]==h) return true;
	return false;
}
static bool is_borrowed_view (VkImageView h)
{
	if (h==framebuffer_images.color[0] || h==framebuffer_images.color[1] ||
		h==framebuffer_images.depth || h==framebuffer_images.msaa_color ||
		h==framebuffer_images.ui_color || h==framebuffer_images.oit_accum ||
		h==framebuffer_images.oit_reveal) return true;
	for (int i=0;i<3;++i) if (h==density_views[i] || h==swapchain_views[i]) return true;
	return false;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass (
	VkDevice device, const VkRenderPassCreateInfo *info, const VkAllocationCallbacks *allocator, VkRenderPass *out)
{
	assert (device==vulkan_globals.device && info && !allocator && out && !*out);
	++ordinary_calls;
	const bool warp=info->attachmentCount==1 && info->subpassCount==1 && info->dependencyCount==2 &&
		info->pAttachments[0].format==VK_FORMAT_R8G8B8A8_UNORM;
	const VkRenderPassMultiviewCreateInfo *views=info->pNext;
	if (warp) assert (!views && !vulkan_globals.warp_render_pass);
	else
	{
		assert (views && views->sType==VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO);
		assert (views->subpassCount==info->subpassCount);
		for (uint32_t i=0;i<views->subpassCount;++i) assert (views->pViewMasks[i]==3);
		assert (views->correlationMaskCount==1 && views->pCorrelationMasks[0]==3);
	}
	for (uint32_t i=0;i<info->attachmentCount;++i)
		if (info->pAttachments[i].format==vulkan_globals.stereo_color_format)
			assert (info->pAttachments[i].finalLayout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	*out=admit_pass(false,warp);
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL fixture_create_render_pass2 (
	VkDevice device, const VkRenderPassCreateInfo2 *info, const VkAllocationCallbacks *allocator, VkRenderPass *out)
{
	assert (device==vulkan_globals.device && info && !allocator && out && !*out);
	assert (info->sType==VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2 && info->pNext);
	const VkRenderPassFragmentDensityMapCreateInfoEXT *d=info->pNext;
	assert (d->sType==VK_STRUCTURE_TYPE_RENDER_PASS_FRAGMENT_DENSITY_MAP_CREATE_INFO_EXT);
	assert (info->attachmentCount==4 && info->subpassCount==1);
	assert (info->pAttachments[0].format==VK_FORMAT_A2B10G10R10_UNORM_PACK32 &&
		info->pAttachments[0].samples==VK_SAMPLE_COUNT_1_BIT);
	assert (info->pAttachments[1].format==VK_FORMAT_D32_SFLOAT_S8_UINT &&
		info->pAttachments[1].samples==VK_SAMPLE_COUNT_4_BIT);
	assert (info->pAttachments[2].format==VK_FORMAT_A2B10G10R10_UNORM_PACK32 &&
		info->pAttachments[2].samples==VK_SAMPLE_COUNT_4_BIT);
	assert (info->pAttachments[3].format==VK_FORMAT_R8G8_UNORM &&
		info->pAttachments[3].finalLayout==VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT);
	assert (d->fragmentDensityMapAttachment.attachment==3 &&
		d->fragmentDensityMapAttachment.layout==VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT);
	assert (info->pSubpasses[0].viewMask==3 && info->correlatedViewMaskCount==1 &&
		info->pCorrelatedViewMasks[0]==3);
	assert (info->pSubpasses[0].colorAttachmentCount==1 &&
		info->pSubpasses[0].pColorAttachments[0].attachment==2);
	assert (info->pSubpasses[0].pResolveAttachments &&
		info->pSubpasses[0].pResolveAttachments[0].attachment==0);
	assert (info->pSubpasses[0].pDepthStencilAttachment &&
		info->pSubpasses[0].pDepthStencilAttachment->attachment==1);
	++density_calls;
	if (reject_pass2 && density_calls==2) { assert (!find_pass(bad_pass)); *out=bad_pass; return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
	*out=admit_pass(true,false);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer (
	VkDevice device, const VkFramebufferCreateInfo *info, const VkAllocationCallbacks *allocator, VkFramebuffer *out)
{
	assert (device==vulkan_globals.device && info && !allocator && out && !*out);
	pass_t *pass=find_pass(info->renderPass);
	assert (pass && !pass->destroyed && info->attachmentCount && info->layers==1);
	assert (info->width==32 && info->height==40);
	bool has_density=false;
	for (uint32_t i=0;i<info->attachmentCount;++i)
	{
		assert (info->pAttachments[i] && is_borrowed_view(info->pAttachments[i]));
		has_density|=is_density_view(info->pAttachments[i]);
	}
	++fb_calls;
	if (pass->density)
	{
		assert (info->attachmentCount==4 && has_density);
		const unsigned index=density_fb_calls++;
		assert (index<NUM_COLOR_BUFFERS*3);
		assert (info->pAttachments[0]==framebuffer_images.color[index/3]);
		assert (info->pAttachments[1]==framebuffer_images.depth);
		assert (info->pAttachments[2]==framebuffer_images.msaa_color);
		assert (info->pAttachments[3]==density_views[index%3]);
		if (density_fb_calls==2 && reject_fb2)
		{
			assert (!find_fb(bad_fb)); *out=bad_fb; return VK_ERROR_OUT_OF_DEVICE_MEMORY;
		}
	}
	else assert (!has_density);
	*out=admit_fb();
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass (
	VkDevice device, VkRenderPass h, const VkAllocationCallbacks *allocator)
{
	assert (device==vulkan_globals.device && h && !allocator);
	pass_t *p=find_pass(h); assert (p && !p->destroyed);
	p->destroyed=true; ++destroyed_passes;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer (
	VkDevice device, VkFramebuffer h, const VkAllocationCallbacks *allocator)
{
	assert (device==vulkan_globals.device && !allocator);
	if (!h) { ++null_fb_destroys; return; }
	framebuffer_t *f=find_fb(h); assert (f && !f->destroyed);
	f->destroyed=true; ++destroyed_fbs;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView (
	VkDevice d, VkImageView v, const VkAllocationCallbacks *a)
{
	(void)d; (void)v; (void)a; ++image_view_destroys;
	assert (!"Borrowed views must remain owned by the runtime");
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImage (VkDevice d, VkImage v, const VkAllocationCallbacks *a)
{
	(void)d; (void)v; (void)a; ++image_destroys;
	assert (!"Borrowed images must remain owned by the runtime");
}

static void reset_ledger (void)
{
	for (unsigned i=0;i<pass_count;++i) assert (passes[i].destroyed);
	for (unsigned i=0;i<framebuffer_count;++i) assert (framebuffers[i].destroyed);
	assert (!vulkan_globals.warp_render_pass);
	pass_count=framebuffer_count=ordinary_calls=density_calls=fb_calls=density_fb_calls=0;
	destroyed_passes=destroyed_fbs=0; reject_pass2=reject_fb2=false;
}
static void setup_inputs (bool density_active)
{
	assert (!vulkan_globals.warp_render_pass);
	memset (&vulkan_globals,0,sizeof(vulkan_globals));
	vulkan_globals.device=(VkDevice)(uintptr_t)1;
	vulkan_globals.vk_create_render_pass2=fixture_create_render_pass2;
	vulkan_globals.stereo_active=true; vulkan_globals.sample_count=VK_SAMPLE_COUNT_4_BIT;
	vulkan_globals.color_format=VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	vulkan_globals.depth_format=VK_FORMAT_D32_SFLOAT_S8_UINT;
	vulkan_globals.stereo_color_format=VK_FORMAT_R8G8B8A8_SRGB;
	vulkan_globals.swap_chain_format=VK_FORMAT_R8G8B8A8_SRGB;
	vulkan_globals.screen_effects_sops=true;
	vulkan_globals.device_features.shaderStorageImageExtendedFormats=VK_TRUE;
	vulkan_globals.openxr_fragment_density_map_enabled=true;
	vulkan_globals.openxr_fragment_density_map_active=density_active;
	vulkan_globals.openxr_fragment_shading_rate_active=false;
	memset (&vid,0,sizeof(vid)); vid.width=vid.render_width=32; vid.height=vid.render_height=40;
	r_ssao.value=1; r_oit.value=0; frame_oit_mode=OIT_MODE_NONE;
	memset (inheritedcontexts,0,sizeof(inheritedcontexts));
	for (int c=0;c<SCBX_NUM;++c)
	{
		native_context_count[c]=SECONDARY_CB_MULTIPLICITY[c];
		assert (native_context_count[c]<=CTX_CAP);
		vulkan_globals.secondary_cb_contexts[c]=inheritedcontexts[c];
		for (unsigned i=0;i<native_context_count[c];++i)
			inheritedcontexts[c][i].render_pass=(VkRenderPass)(uintptr_t)(0x70000+c*16+i);
	}
	for (int i=0;i<3;++i) { density_views[i]=(VkImageView)(uintptr_t)(301+i); swapchain_views[i]=(VkImageView)(uintptr_t)(401+i); }
	framebuffer_images=(render_framebuffer_images_t){
		.width=32,.height=40,.render_width=32,.render_height=40,
		.ui_color=(VkImageView)(uintptr_t)205,
		.color={(VkImageView)(uintptr_t)201,(VkImageView)(uintptr_t)202},
		.depth=(VkImageView)(uintptr_t)203,.msaa_color=(VkImageView)(uintptr_t)204,
		.oit_accum=(VkImageView)(uintptr_t)206,.oit_reveal=(VkImageView)(uintptr_t)207,
		.density_map_count=3,.density_maps=density_views,.swapchain_count=3,.swapchain=swapchain_views,
	};
}
static void assert_layout (bool density)
{
	assert (current_layout.stereo && current_layout.samples==VK_SAMPLE_COUNT_4_BIT && !current_layout.upscale);
	assert (current_layout.swapchain_format==VK_FORMAT_R8G8B8A8_SRGB);
	assert (current_layout.fragment_density_map==density && r_ssao.value==1 && r_oit.value==0);
	assert (frame_oit_mode==OIT_MODE_NONE);
	for (int v=0;v<MAIN_RENDER_PASS_VARIANT_COUNT;++v)
	{
		bool found=false; const frame_desc_t *f=&current_layout.variants[v];
		for (uint32_t p=0;p<f->pass_count;++p) found|=f->passes[p].target==FRAME_TARGET_DENSITY_SCENE;
		assert (found==density);
	}
}
static void configure_contexts (void)
{
	for (int c=0;c<SCBX_NUM;++c) if (native_context_count[c])
		for (int v=0;v<MAIN_RENDER_PASS_VARIANT_COUNT;++v)
		{
			cb_context_t *cb=&inheritedcontexts[c][0];
			R_ConfigureRenderContext(cb,c,v,MAIN_RENDER_PASS_STENCIL_CLEAR);
			pass_t *pass=find_pass(cb->render_pass);
			assert (pass && !pass->destroyed);
		}
}
static void assert_cleared (void)
{
	for (int v=0;v<MAIN_RENDER_PASS_VARIANT_COUNT;++v)
		for (uint32_t p=0;p<current_layout.variants[v].pass_count;++p)
		{
			assert (!physical_passes[v][p].framebuffers && !physical_passes[v][p].framebuffer_count);
			for (int s=0;s<MAIN_RENDER_PASS_STENCIL_COUNT;++s) assert (!physical_passes[v][p].handles[s]);
		}
	for (int c=0;c<SCBX_NUM;++c)
		for (unsigned i=0;i<native_context_count[c];++i) assert (!inheritedcontexts[c][i].render_pass);
}
static void assert_retired (void)
{
	for (unsigned i=0;i<pass_count;++i) assert (passes[i].destroyed);
	for (unsigned i=0;i<framebuffer_count;++i) assert (framebuffers[i].destroyed);
	assert (destroyed_passes==pass_count && destroyed_fbs==framebuffer_count);
	assert (!image_destroys && !image_view_destroys);
}
static void retire_warp_owner (void)
{
	if (vulkan_globals.warp_render_pass)
	{
		pass_t *p=find_pass(vulkan_globals.warp_render_pass);
		assert (p && p->warp && !p->destroyed);
		vkDestroyRenderPass(vulkan_globals.device,p->handle,NULL);
		vulkan_globals.warp_render_pass=VK_NULL_HANDLE;
	}
}
static unsigned expected_standard_fbs (void)
{
	unsigned n=0; const frame_desc_t *f=&current_layout.variants[MAIN_RENDER_PASS_STANDARD];
	for (uint32_t p=0;p<f->pass_count;++p)
		n+=f->passes[p].target==FRAME_TARGET_DENSITY_SCENE ? NUM_COLOR_BUFFERS*3 :
			f->passes[p].target==FRAME_TARGET_UI ? framebuffer_images.swapchain_count : NUM_COLOR_BUFFERS;
	return n;
}
static void cleanup_all (void)
{
	R_DestroyFrameBuffers(); R_DestroyRenderPasses(); retire_warp_owner();
	assert_cleared(); assert_retired();
}
static void run_ordinary_constructors_after_cleanup_input_reset (void)
{
	reset_ledger(); setup_inputs(false);
	assert (R_CreateRenderPasses()); assert_layout(false);
	assert (!density_calls && ordinary_calls>0); configure_contexts();
	assert (R_CreateFrameBuffers(&framebuffer_images));
	assert (!density_fb_calls && fb_calls==expected_standard_fbs());
	cleanup_all();
}
static void run_valid_baseline (void)
{
	reset_ledger(); setup_inputs(true);
	assert (R_CreateRenderPasses()); assert_layout(true);
	assert (density_calls==MAIN_RENDER_PASS_VARIANT_COUNT*MAIN_RENDER_PASS_STENCIL_COUNT);
	for (int v=0;v<MAIN_RENDER_PASS_VARIANT_COUNT;++v)
		for (uint32_t p=0;p<current_layout.variants[v].pass_count;++p)
			for (int s=0;s<MAIN_RENDER_PASS_STENCIL_COUNT;++s) assert (physical_passes[v][p].handles[s]);
	configure_contexts();
	assert (R_CreateFrameBuffers(&framebuffer_images));
	assert (density_fb_calls==NUM_COLOR_BUFFERS*3 && fb_calls==expected_standard_fbs());
	cleanup_all();
	unsigned pd=destroyed_passes, fd=destroyed_fbs;
	R_DestroyFrameBuffers(); R_DestroyRenderPasses(); retire_warp_owner();
	assert (destroyed_passes==pd && destroyed_fbs==fd); assert_retired();
}
static void run_second_density_pass_failure (void)
{
	reset_ledger(); setup_inputs(true); reject_pass2=true;
	assert (!R_CreateRenderPasses());
	assert (density_calls==2 && !ordinary_calls && pass_count==1 && !find_pass(bad_pass));
	for (int v=0;v<MAIN_RENDER_PASS_VARIANT_COUNT;++v)
		for (uint32_t p=0;p<MAX_FRAME_PASSES;++p)
			for (int s=0;s<MAIN_RENDER_PASS_STENCIL_COUNT;++s) assert (physical_passes[v][p].handles[s]!=bad_pass);
	R_DestroyRenderPasses(); assert_cleared(); assert_retired();
	run_ordinary_constructors_after_cleanup_input_reset();
}
static void run_second_density_fb_failure (void)
{
	reset_ledger(); setup_inputs(true);
	assert (R_CreateRenderPasses()); assert_layout(true); configure_contexts();
	reject_fb2=true; assert (!R_CreateFrameBuffers(&framebuffer_images));
	assert (fb_calls==2 && density_fb_calls==2 && framebuffer_count==1 && !find_fb(bad_fb));
	assert (current_layout.variants[MAIN_RENDER_PASS_STANDARD].passes[0].target==FRAME_TARGET_DENSITY_SCENE);
	physical_pass_t *partial=&physical_passes[MAIN_RENDER_PASS_STANDARD][0];
	assert (partial->framebuffers && partial->framebuffer_count==NUM_COLOR_BUFFERS*3);
	assert (partial->framebuffers[0] && !partial->framebuffers[1]);
	for (int v=0;v<MAIN_RENDER_PASS_VARIANT_COUNT;++v)
		for (uint32_t p=0;p<current_layout.variants[v].pass_count;++p)
			if (v!=MAIN_RENDER_PASS_STANDARD || p!=0) assert (!physical_passes[v][p].framebuffers);
	cleanup_all(); run_ordinary_constructors_after_cleanup_input_reset();
}
int main (void)
{
	run_valid_baseline(); run_second_density_pass_failure(); run_second_density_fb_failure();
	puts ("OpenXR density pass/framebuffer fault cleanup; ordinary constructors after cleanup/input reset passed");
	return 0;
}
