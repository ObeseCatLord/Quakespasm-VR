/* Calls the actual texture upload owner; only allocation/Vulkan boundaries are
 * captured. Expected extents are literal witnesses, not copies of its helpers.
 * Run with tests/run_anisotropy_native.py. No Vulkan device is needed here. */
#include <setjmp.h>
#include "../Quake/gl_texmgr.c"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "TEXMGR_MIP_FAILED line=%d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define HANDLE(type, n) ((type)(uintptr_t)(n))
#define GUARD 32
#define POISON 0xcd

vulkanglobals_t vulkan_globals;
cvar_t vid_filter = {"vid_filter", "1", CVAR_NONE, 1};
atomic_uint32_t num_vulkan_tex_allocations;
qboolean in_update_screen;
THREAD_LOCAL size_t thread_stack_alloc_size;
size_t max_thread_stack_alloc_size;
static VkImageCreateInfo image_info;
static VkBufferImageCopy copies[MAX_MIPS];
static uint32_t copy_count, image_calls, view_levels, barrier_calls;
static int staging_size, begin_calls, end_calls, descriptor_calls;
static byte *staging;
static jmp_buf fatal_jump;
static qboolean expect_bound;

void Sys_Error(const char *format, ...)
{
	char message[256];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	if (expect_bound && !strcmp(message, "Texture has over 16 mips"))
		longjmp(fatal_jump, 1);
	fprintf(stderr, "TEXMGR_MIP_FAILED Sys_Error: %s\n", message);
	exit(1);
}
char *va(const char *format, ...)
{
	static char message[256];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	return message;
}
void *Mem_AllocNonZero(const size_t size) { void *p = malloc(size); CHECK(p); return p; }
void *Mem_Alloc(const size_t size) { void *p = calloc(1, size); CHECK(p); return p; }
void Mem_Free(const void *p) { free((void *)p); }
void GL_SetObjectName(uint64_t h, VkObjectType t, const char *n) { (void)h; (void)t; (void)n; }
void GL_WaitForDeviceIdle(void) { CHECK(0); }
float R_AnisotropyLevel(void) { return 1; }
glheapallocation_t *GL_HeapAllocate(glheap_t *h, VkDeviceSize s, VkDeviceSize a, atomic_uint32_t *n)
{ (void)h; (void)n; CHECK(s && a); return HANDLE(glheapallocation_t *, 1); }
VkDeviceMemory GL_HeapGetAllocationMemory(glheapallocation_t *a) { CHECK(a); return HANDLE(VkDeviceMemory, 2); }
VkDeviceSize GL_HeapGetAllocationOffset(glheapallocation_t *a) { CHECK(a); return 0; }
void GL_HeapFree(glheap_t *h, glheapallocation_t *a, atomic_uint32_t *n) { (void)h; (void)a; (void)n; CHECK(0); }
VkDescriptorSet R_AllocateDescriptorSet(vulkan_desc_set_layout_t *l)
{ CHECK(l == &vulkan_globals.single_texture_set_layout); return HANDLE(VkDescriptorSet, 3); }
void R_FreeDescriptorSet(VkDescriptorSet s, vulkan_desc_set_layout_t *l) { (void)s; (void)l; CHECK(0); }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice d, const VkImageCreateInfo *c, const VkAllocationCallbacks *a, VkImage *i)
{ (void)d; (void)a; CHECK(++image_calls == 1); image_info = *c; *i = HANDLE(VkImage, 4); return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice d, VkImage i, VkMemoryRequirements *r)
{ (void)d; CHECK(i == HANDLE(VkImage, 4)); *r = (VkMemoryRequirements){.size=4096, .alignment=4, .memoryTypeBits=1}; }
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice d, VkImage i, VkDeviceMemory m, VkDeviceSize o)
{ (void)d; CHECK(i == HANDLE(VkImage, 4) && m == HANDLE(VkDeviceMemory, 2) && o == 0); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice d, const VkImageViewCreateInfo *c, const VkAllocationCallbacks *a, VkImageView *v)
{ (void)d; (void)a; CHECK(!view_levels); view_levels = c->subresourceRange.levelCount; CHECK(c->image == HANDLE(VkImage, 4)); *v = HANDLE(VkImageView, 5); return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice d, uint32_t n, const VkWriteDescriptorSet *w, uint32_t c, const VkCopyDescriptorSet *p)
{ (void)d; (void)p; CHECK(n == 1 && c == 0 && w->pImageInfo->imageView == HANDLE(VkImageView, 5)); ++descriptor_calls; }
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer cb, VkPipelineStageFlags s, VkPipelineStageFlags d,
	VkDependencyFlags f, uint32_t nm, const VkMemoryBarrier *m, uint32_t nb, const VkBufferMemoryBarrier *b,
	uint32_t ni, const VkImageMemoryBarrier *i)
{
	(void)s; (void)d; (void)f; (void)m; (void)b;
	CHECK(cb == HANDLE(VkCommandBuffer, 6) && !nm && !nb && ni == 1);
	CHECK(i->subresourceRange.levelCount == image_info.mipLevels && i->subresourceRange.layerCount == 1);
	CHECK(i->oldLayout == (barrier_calls ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED));
	CHECK(i->newLayout == (barrier_calls ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));
	CHECK(++barrier_calls <= 2);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer cb, VkBuffer b, VkImage i,
	VkImageLayout l, uint32_t n, const VkBufferImageCopy *r)
{
	CHECK(cb == HANDLE(VkCommandBuffer, 6) && b == HANDLE(VkBuffer, 7) && i == HANDLE(VkImage, 4));
	CHECK(l == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && !copy_count && n <= MAX_MIPS);
	copy_count = n;
	memcpy(copies, r, n * sizeof(*r));
}
byte *R_StagingAllocate(int size, int alignment, VkCommandBuffer *cb, VkBuffer *b, int *offset)
{
	CHECK(!staging && size > 0 && alignment == 4);
	staging_size = size;
	staging = malloc((size_t)size + 2 * GUARD);
	CHECK(staging);
	memset(staging, POISON, (size_t)size + 2 * GUARD);
	*cb = HANDLE(VkCommandBuffer, 6); *b = HANDLE(VkBuffer, 7); *offset = GUARD;
	return staging + GUARD;
}
void R_StagingBeginCopy(void) { CHECK(copy_count && !begin_calls++); }
void R_StagingEndCopy(void) { CHECK(begin_calls == 1 && !end_calls++); }

typedef struct { uint32_t width, height; } extent_t;
static const extent_t wide[] = {{256,64},{128,32},{64,16},{32,8},{16,4},{8,2},{4,1},{2,1},{1,1}};
static const extent_t tall[] = {{64,256},{32,128},{16,64},{8,32},{4,16},{2,8},{1,4},{1,2},{1,1}};
static const extent_t row[] = {{8,1},{4,1},{2,1},{1,1}};
static const extent_t column[] = {{1,8},{1,4},{1,2},{1,1}};
static const extent_t single[] = {{1,1}};
static const extent_t odd[] = {{7,3},{3,1},{1,1}};
static const extent_t bounded[] = {{32768,1},{16384,1},{8192,1},{4096,1},{2048,1},{1024,1},{512,1},{256,1},
	{128,1},{64,1},{32,1},{16,1},{8,1},{4,1},{2,1},{1,1}};

static void upload_case(const extent_t *expected, size_t levels, qboolean constant, qboolean mipmap)
{
	const uint32_t width = expected[0].width, height = expected[0].height;
	const size_t bytes = (size_t)width * height * 4;
	byte *pixels = malloc(bytes);
	CHECK(pixels);
	for (uint32_t y = 0; y < height; ++y)
		for (uint32_t x = 0; x < width; ++x)
		{
			byte *p = pixels + ((size_t)y * width + x) * 4;
			p[0] = constant ? 37 : ((width >= height ? x < width/2 : y < height/2) ? 0 : 240);
			p[1] = 83; p[2] = 149; p[3] = 255;
		}
	image_calls = copy_count = view_levels = barrier_calls = 0;
	begin_calls = end_calls = descriptor_calls = 0;
	gltexture_t texture = {.width=width, .height=height, .source_format=SRC_RGBA,
		.flags=mipmap ? TEXPREF_MIPMAP : 0};
	TexMgr_LoadImage32(&texture, (unsigned *)pixels);
	CHECK(image_calls == 1 && copy_count == levels && image_info.mipLevels == levels && view_levels == levels);
	CHECK(image_info.extent.width == width && image_info.extent.height == height && image_info.arrayLayers == 1);
	CHECK(begin_calls == 1 && end_calls == 1 && barrier_calls == 2 && descriptor_calls == 1);
	CHECK(!memcmp(staging + GUARD, pixels, bytes));
	size_t offset = GUARD;
	for (size_t level = 0; level < levels; ++level)
	{
		const VkBufferImageCopy *r = copies + level;
		CHECK(r->bufferOffset == offset && !r->bufferRowLength && !r->bufferImageHeight);
		CHECK(r->imageSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && r->imageSubresource.mipLevel == level);
		CHECK(!r->imageSubresource.baseArrayLayer && r->imageSubresource.layerCount == 1);
		CHECK(!r->imageOffset.x && !r->imageOffset.y && !r->imageOffset.z);
		CHECK(r->imageExtent.width == expected[level].width && r->imageExtent.height == expected[level].height && r->imageExtent.depth == 1);
		const size_t end = offset + (size_t)expected[level].width * expected[level].height * 4;
		CHECK(end <= GUARD + (size_t)staging_size);
		for (size_t pos = offset; pos < end; pos += 4)
		{
			CHECK(staging[pos+1] == 83 && staging[pos+2] == 149 && staging[pos+3] == 255);
			if (constant) CHECK(staging[pos] == 37);
		}
		if (!constant && mipmap && level == levels-1)
			CHECK(abs((int)staging[offset] - 120) <= 1);
		if (!constant && mipmap && level == levels-2)
			CHECK(staging[offset] < 120 && staging[offset+4] > 120);
		offset = end;
	}
	CHECK(offset == GUARD + (size_t)staging_size);
	for (int i = 0; i < GUARD; ++i)
		CHECK(staging[i] == POISON && staging[offset+i] == POISON);
	printf("TEXMGR_MIP_UPLOAD_PASSED %ux%u levels=%zu bytes=%d pattern=%s mipmap=%d\n",
		width, height, levels, staging_size, constant ? "constant" : "split", mipmap);
	free(staging); staging = NULL; free(pixels);
}

int main(void)
{
	texmgr_mutex = SDL_CreateMutex(); CHECK(texmgr_mutex);
	vulkan_globals.device_properties.limits.maxImageDimension2D = 65536;
	upload_case(wide, countof(wide), true, true);
	upload_case(tall, countof(tall), true, true);
	upload_case(wide, countof(wide), false, true);
	upload_case(tall, countof(tall), false, true);
	upload_case(row, countof(row), false, true);
	upload_case(column, countof(column), false, true);
	upload_case(single, countof(single), true, true);
	upload_case(odd, countof(odd), true, true);
	upload_case(bounded, countof(bounded), true, true);
	upload_case(wide, 1, true, false);
	image_calls = 0; expect_bound = true;
	if (!setjmp(fatal_jump))
	{
		gltexture_t texture = {.width=65536, .height=1, .source_format=SRC_RGBA, .flags=TEXPREF_MIPMAP};
		TexMgr_LoadImage32(&texture, NULL);
		CHECK(0);
	}
	/* The fatal production guard exits while owning the mutex. */
	SDL_UnlockMutex(texmgr_mutex);
	CHECK(!image_calls);
	SDL_DestroyMutex(texmgr_mutex);
	printf("TEXMGR_MIP_NATIVE_PASSED production_upload MAX_MIPS=16 oversized_rejected_before_image_creation\n");
	return 0;
}
