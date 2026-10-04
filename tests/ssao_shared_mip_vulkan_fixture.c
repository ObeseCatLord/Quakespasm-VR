/* Real Vulkan regression for the non-subgroup SSAO shared-mip compute shader.
 * Build/run from the repository root:
 *   p=/tmp/qsvr-ssao-shared; glslc --target-env=vulkan1.1 -IShaders \
 *     Shaders/ssao_mip_shared.comp -o "$p.spv"
 *   cc -std=gnu11 -O0 tests/ssao_shared_mip_vulkan_fixture.c -lvulkan -lm -o "$p"
 *   "$p" "$p.spv"
 * Exit 77 means no Vulkan 1.1 compute device.  FP16 and MSAA variants are
 * intentionally excluded; this covers the ordinary fp32, sampled-depth SPIR-V.
 */
#include <vulkan/vulkan.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "SSAO_SHARED_MIP_FAILED line=%d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define VK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "SSAO_SHARED_MIP_FAILED line=%d: %s VkResult=%d\n", __LINE__, #x, r_); exit(1); } } while (0)
#define HMAX 0x7bffu
typedef struct { VkBuffer h; VkDeviceMemory m; void *map; } buffer_t;
typedef struct { VkImage h; VkDeviceMemory m; VkImageView v[5]; } image_t;
typedef struct { int w, h; const char *name; } testcase_t;
static struct { VkInstance instance; VkPhysicalDevice gpu; VkDevice device; VkQueue queue;
	VkPhysicalDeviceMemoryProperties memory; VkCommandPool pool; VkCommandBuffer cb;
	VkDescriptorSetLayout set_layout; VkDescriptorPool descriptor_pool; VkDescriptorSet set;
	VkPipelineLayout pipeline_layout; VkPipeline pipeline; VkSampler sampler; } f;

static void skip(const char *where, VkResult r)
{
	printf("SSAO_SHARED_MIP_VULKAN_SKIPPED %s VkResult=%d\n", where, r);
	if (f.instance) vkDestroyInstance(f.instance, NULL);
	exit(77);
}
static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags)
{
	for (uint32_t i = 0; i < f.memory.memoryTypeCount; ++i)
		if ((bits & (1u << i)) && (f.memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
	CHECK(0); return 0;
}
static buffer_t buffer(VkDeviceSize size)
{
	buffer_t b = {0}; VkBufferCreateInfo ci = {.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size=size,
		.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};
	VK(vkCreateBuffer(f.device, &ci, NULL, &b.h)); VkMemoryRequirements mr; vkGetBufferMemoryRequirements(f.device, b.h, &mr);
	VkMemoryAllocateInfo ai = {.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize=mr.size,
		.memoryTypeIndex=memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
	VK(vkAllocateMemory(f.device, &ai, NULL, &b.m)); VK(vkBindBufferMemory(f.device, b.h, b.m, 0));
	VK(vkMapMemory(f.device, b.m, 0, VK_WHOLE_SIZE, 0, &b.map)); return b;
}
static image_t image(VkFormat format, int w, int h, int mips, VkImageUsageFlags usage)
{
	image_t x = {0}; VkImageCreateInfo ci = {.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType=VK_IMAGE_TYPE_2D,
		.format=format, .extent={(uint32_t)w,(uint32_t)h,1}, .mipLevels=(uint32_t)mips, .arrayLayers=1,
		.samples=VK_SAMPLE_COUNT_1_BIT, .tiling=VK_IMAGE_TILING_OPTIMAL, .usage=usage};
	VK(vkCreateImage(f.device, &ci, NULL, &x.h)); VkMemoryRequirements mr; vkGetImageMemoryRequirements(f.device, x.h, &mr);
	VkMemoryAllocateInfo ai = {.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize=mr.size, .memoryTypeIndex=memory_type(mr.memoryTypeBits, 0)};
	VK(vkAllocateMemory(f.device, &ai, NULL, &x.m)); VK(vkBindImageMemory(f.device, x.h, x.m, 0));
	for (int i=0; i<mips; ++i) { VkImageViewCreateInfo vi = {.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image=x.h,
		.viewType=VK_IMAGE_VIEW_TYPE_2D, .format=format, .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,(uint32_t)i,1,0,1}};
		VK(vkCreateImageView(f.device, &vi, NULL, &x.v[i])); } return x;
}
static void destroy_buffer(buffer_t b) { if (b.h) { vkUnmapMemory(f.device,b.m); vkDestroyBuffer(f.device,b.h,NULL); vkFreeMemory(f.device,b.m,NULL); } }
static void destroy_image(image_t x, int mips) { for(int i=0;i<mips;++i) if(x.v[i]) vkDestroyImageView(f.device,x.v[i],NULL); if(x.h) vkDestroyImage(f.device,x.h,NULL); if(x.m) vkFreeMemory(f.device,x.m,NULL); }
static void begin(void) { VK(vkResetCommandBuffer(f.cb,0)); VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT}; VK(vkBeginCommandBuffer(f.cb,&bi)); }
static void end(void) { VK(vkEndCommandBuffer(f.cb)); VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&f.cb}; VK(vkQueueSubmit(f.queue,1,&si,VK_NULL_HANDLE)); VK(vkQueueWaitIdle(f.queue)); }
static void barrier(VkImage image, VkImageLayout old, VkImageLayout next, VkAccessFlags src, VkAccessFlags dst, VkPipelineStageFlags from, VkPipelineStageFlags to, uint32_t mip, uint32_t levels)
{
	VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=src,.dstAccessMask=dst,.oldLayout=old,.newLayout=next,
		.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,mip,levels,0,1}};
	vkCmdPipelineBarrier(f.cb,from,to,0,0,NULL,0,NULL,1,&b);
}
static void init_device(void)
{
	VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="ssao shared mip fixture",.apiVersion=VK_API_VERSION_1_1};
	VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app}; VkResult r=vkCreateInstance(&ii,NULL,&f.instance); if(r!=VK_SUCCESS) skip("vkCreateInstance",r);
	uint32_t n=0, family=0; r=vkEnumeratePhysicalDevices(f.instance,&n,NULL); if(r!=VK_SUCCESS||!n) skip("vkEnumeratePhysicalDevices",r);
	VkPhysicalDevice *g=calloc(n,sizeof(*g)); CHECK(g); VK(vkEnumeratePhysicalDevices(f.instance,&n,g));
	for(uint32_t i=0;i<n&&!f.gpu;++i) { VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(g[i],&p); if(p.apiVersion<VK_API_VERSION_1_1) continue;
		uint32_t q=0; vkGetPhysicalDeviceQueueFamilyProperties(g[i],&q,NULL); VkQueueFamilyProperties *qs=calloc(q,sizeof(*qs)); CHECK(qs); vkGetPhysicalDeviceQueueFamilyProperties(g[i],&q,qs);
		for(uint32_t j=0;j<q;++j) if(qs[j].queueCount&&(qs[j].queueFlags&VK_QUEUE_COMPUTE_BIT)) { f.gpu=g[i]; family=j; break; } free(qs); }
	free(g); if(!f.gpu) skip("no Vulkan 1.1 compute queue",VK_ERROR_FEATURE_NOT_PRESENT);
	VkPhysicalDeviceFeatures supported; vkGetPhysicalDeviceFeatures(f.gpu,&supported); if(!supported.shaderStorageImageExtendedFormats) skip("rg16f storage image",VK_ERROR_FEATURE_NOT_PRESENT);
	VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties(f.gpu,VK_FORMAT_R16G16_SFLOAT,&fp); if(!(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) skip("rg16f storage format",VK_ERROR_FORMAT_NOT_SUPPORTED);
	float priority=1; VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
	VkPhysicalDeviceFeatures features={.shaderStorageImageExtendedFormats=VK_TRUE}; VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.pEnabledFeatures=&features};
	r=vkCreateDevice(f.gpu,&di,NULL,&f.device); if(r!=VK_SUCCESS) skip("vkCreateDevice",r); vkGetDeviceQueue(f.device,family,0,&f.queue); vkGetPhysicalDeviceMemoryProperties(f.gpu,&f.memory);
	VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family}; VK(vkCreateCommandPool(f.device,&pi,NULL,&f.pool));
	VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=f.pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1}; VK(vkAllocateCommandBuffers(f.device,&ai,&f.cb));
}
static VkShaderModule shader(const char *path)
{
	FILE *in=fopen(path,"rb"); if(!in) { perror(path); exit(1); } CHECK(!fseek(in,0,SEEK_END)); long n=ftell(in); CHECK(n>=20&&!(n&3)&&!fseek(in,0,SEEK_SET));
	uint32_t *code=malloc((size_t)n); CHECK(code&&fread(code,1,(size_t)n,in)==(size_t)n&&code[0]==0x07230203&&!fclose(in));
	VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)n,.pCode=code}; VkShaderModule s; VK(vkCreateShaderModule(f.device,&ci,NULL,&s)); free(code); return s;
}
static void init_pipeline(const char *path)
{
	VkDescriptorSetLayoutBinding b[6]={0}; b[0]=(VkDescriptorSetLayoutBinding){0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT};
	for(int i=1;i<6;++i) b[i]=(VkDescriptorSetLayoutBinding){(uint32_t)i,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT};
	VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=6,.pBindings=b}; VK(vkCreateDescriptorSetLayout(f.device,&li,NULL,&f.set_layout));
	VkDescriptorPoolSize ps[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,5}}; VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=ps}; VK(vkCreateDescriptorPool(f.device,&pi,NULL,&f.descriptor_pool));
	VkDescriptorSetAllocateInfo si={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=f.descriptor_pool,.descriptorSetCount=1,.pSetLayouts=&f.set_layout}; VK(vkAllocateDescriptorSets(f.device,&si,&f.set));
	VkPushConstantRange pc={VK_SHADER_STAGE_COMPUTE_BIT,0,48}; VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&f.set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&pc}; VK(vkCreatePipelineLayout(f.device,&pli,NULL,&f.pipeline_layout));
	VkShaderModule s=shader(path); VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=s,.pName="main"},.layout=f.pipeline_layout}; VK(vkCreateComputePipelines(f.device,VK_NULL_HANDLE,1,&ci,NULL,&f.pipeline)); vkDestroyShaderModule(f.device,s,NULL);
	VkSamplerCreateInfo sampler={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE}; VK(vkCreateSampler(f.device,&sampler,NULL,&f.sampler));
}
static uint16_t f16(float x)
{
	uint32_t u; memcpy(&u,&x,4); uint32_t s=(u>>16)&0x8000, e=(u>>23)&255, m=u&0x7fffff;
	if(e==255) return (uint16_t)(s|(m?0x7e00:0x7c00)); if(e>142) return (uint16_t)(s|0x7c00); if(e<113) { if(e<103) return (uint16_t)s; m=(m|0x800000)>>(114-e); return (uint16_t)(s+((m+0xfff+((m>>13)&1))>>13)); }
	return (uint16_t)(s+((e-112)<<10)+((m+0xfff+((m>>13)&1))>>13));
}
static float f32(uint16_t h)
{
	uint32_t s=(uint32_t)(h&0x8000)<<16, e=(h>>10)&31, m=h&1023, u; if(!e) { if(!m) u=s; else { e=113; while(!(m&1024)) { m<<=1; --e; } u=s|(e<<23)|((m&1023)<<13); } } else u=s|((e+112)<<23)|(m<<13); float x; memcpy(&x,&u,4); return x;
}
static float depth_world(int x,int y) { return ((x*3+y*5)%11)==0 ? 65504.f : .5f+(float)((x*17+y*29)%160)*.03125f; }
static float depth_scene(int x,int y) { return x==2&&y==2 ? 0.f : ldexpf(1.f, -5 + ((x*19+y*7)%4)); }
static float clamp1(float x) { return x<0?0:x>1?1:x; }
static float filter(float a,float b,float c,float d,float radius)
{
	float farthest=fmaxf(fmaxf(a<65504.f?a:0,b<65504.f?b:0),fmaxf(c<65504.f?c:0,d<65504.f?d:0)); if(farthest==0) return 65504.f;
	a=a<65504.f?a:farthest; b=b<65504.f?b:farthest; c=c<65504.f?c:farthest; d=d<65504.f?d:farthest;
	float effect=.75f*radius*1.457f, range=.615f*effect, from=effect*(1.f-.615f), mul=-1.f/range, add=from/range+1.f;
	float w0=clamp1((farthest-a)*mul+add),w1=clamp1((farthest-b)*mul+add),w2=clamp1((farthest-c)*mul+add),w3=clamp1((farthest-d)*mul+add);
	return (w0*a+w1*b+w2*c+w3*d)/(w0+w1+w2+w3);
}
static int mw(testcase_t c,int m) { int x=c.w>>m; return x?x:1; } static int mh(testcase_t c,int m) { int y=c.h>>m; return y?y:1; }
static void put(uint16_t *out,int p,float a,float b) { out[2*p]=f16(a); out[2*p+1]=f16(b); }
static void reduce2(float *out,const float *a,const float *b,const float *c,const float *d) { out[0]=filter(a[0],b[0],c[0],d[0],2.75f); out[1]=filter(a[1],b[1],c[1],d[1],2.75f); }
static void reference(testcase_t t,uint16_t *want[5])
{
	float *base=calloc((size_t)t.w*t.h*2,sizeof(*base)); CHECK(base); for(int y=0;y<t.h;++y) for(int x=0;x<t.w;++x) { int p=y*t.w+x; base[2*p]=f32(f16(depth_world(x,y))); float z=depth_scene(x,y); base[2*p+1]=f32(f16(z>0?fminf(.25f/z,65472.f):65504.f)); put(want[0],p,base[2*p],base[2*p+1]); }
	for(int gy=0;gy<(t.h+15)/16;++gy) for(int gx=0;gx<(t.w+15)/16;++gx) {
		float d1[64][2],d2[16][2],d3[4][2]; int w1=mw(t,1),h1=mh(t,1),w2=mw(t,2),h2=mh(t,2),w3=mw(t,3),h3=mh(t,3),w4=mw(t,4),h4=mh(t,4);
		for(int i=0;i<64;++i) { int lx=(i&1)|((i>>1)&2)|((i>>2)&4),ly=((i>>1)&1)|((i>>2)&2)|((i>>3)&4),px=gx*8+lx,py=gy*8+ly; float q[4][2];
			for(int k=0;k<4;++k) { int x=2*px+(k&1),y=2*py+(k>>1); q[k][0]=q[k][1]=65504.f; if(x<t.w&&y<t.h) { q[k][0]=base[2*(y*t.w+x)]; q[k][1]=base[2*(y*t.w+x)+1]; } } reduce2(d1[ly*8+lx],q[0],q[1],q[2],q[3]); }
		if(gx*8>=w1||gy*8>=h1) continue;
		for(int i=0;i<64;++i) { int lx=(i&1)|((i>>1)&2)|((i>>2)&4),ly=((i>>1)&1)|((i>>2)&2)|((i>>3)&4),px=gx*8+lx,py=gy*8+ly; if(px<w1&&py<h1) put(want[1],py*w1+px,d1[ly*8+lx][0],d1[ly*8+lx][1]); }
		if(gx*8+8>w1||gy*8+8>h1) { float raw[64][2]; memcpy(raw,d1,sizeof(raw)); for(int i=0;i<64;++i) { int lx=(i&1)|((i>>1)&2)|((i>>2)&4),ly=((i>>1)&1)|((i>>2)&2)|((i>>3)&4); int cx=lx<w1-gx*8?lx:w1-gx*8-1,cy=ly<h1-gy*8?ly:h1-gy*8-1; memcpy(d1[ly*8+lx],raw[cy*8+cx],sizeof(d1[i])); } }
		for(int i=0;i<16;++i) { int x=(i&3)*2,y=(i>>2)*2; reduce2(d2[i],d1[y*8+x],d1[y*8+x+1],d1[(y+1)*8+x],d1[(y+1)*8+x+1]); int px=gx*4+(i&3),py=gy*4+(i>>2); if(px<w2&&py<h2) put(want[2],py*w2+px,d2[i][0],d2[i][1]); }
		for(int i=0;i<4;++i) { int x=(i&1)*2,y=(i>>1)*2, lx=w2-gx*4-1,ly=h2-gy*4-1; if(lx>3)lx=3;if(ly>3)ly=3;if(lx<0)lx=0;if(ly<0)ly=0; int x1=x+1>lx?lx:x+1,y1=y+1>ly?ly:y+1; x=x>lx?lx:x;y=y>ly?ly:y; reduce2(d3[i],d2[y*4+x],d2[y*4+x1],d2[y1*4+x],d2[y1*4+x1]); int px=gx*2+(i&1),py=gy*2+(i>>1); if(px<w3&&py<h3) put(want[3],py*w3+px,d3[i][0],d3[i][1]); }
		if(gx<w4&&gy<h4) { int lx=w3-gx*2-1,ly=h3-gy*2-1;if(lx>1)lx=1;if(ly>1)ly=1;if(lx<0)lx=0;if(ly<0)ly=0; reduce2(d1[0],d3[0],d3[lx],d3[ly*2],d3[ly*2+lx]); put(want[4],gy*w4+gx,d1[0][0],d1[0][1]); }
	}
	CHECK(!(t.w&1) || want[0][2*((t.h-1)*t.w+t.w-1)+1] != HMAX); free(base);
}
/* GPU division/contraction may straddle one half-float rounding boundary.
 * Missing-depth sentinels must still match exactly. */
static void run(testcase_t t)
{
	VkDeviceSize off[5], bytes=0; for(int m=0;m<5;++m) { off[m]=bytes; bytes+=(VkDeviceSize)mw(t,m)*mh(t,m)*4; } VkDeviceSize scene_off=bytes, total=bytes+(VkDeviceSize)t.w*t.h*4;
	uint16_t *want[5]; for(int m=0;m<5;++m) { want[m]=calloc((size_t)mw(t,m)*mh(t,m),4); CHECK(want[m]); } reference(t,want);
	buffer_t io=buffer(total); memcpy(io.map,want[0],(size_t)t.w*t.h*4); float *scene=(float *)((char *)io.map+scene_off); for(int y=0;y<t.h;++y) for(int x=0;x<t.w;++x) scene[y*t.w+x]=depth_scene(x,y);
	image_t pyramid=image(VK_FORMAT_R16G16_SFLOAT,t.w,t.h,5,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT), depth=image(VK_FORMAT_R32_SFLOAT,t.w,t.h,1,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	begin(); barrier(pyramid.h,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1); barrier(depth.h,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1);
	VkBufferImageCopy copy[2]={0}; copy[0]=(VkBufferImageCopy){.bufferOffset=0,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={(uint32_t)t.w,(uint32_t)t.h,1}}; copy[1]=(VkBufferImageCopy){.bufferOffset=scene_off,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={(uint32_t)t.w,(uint32_t)t.h,1}}; vkCmdCopyBufferToImage(f.cb,io.h,pyramid.h,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy[0]); vkCmdCopyBufferToImage(f.cb,io.h,depth.h,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy[1]);
	barrier(pyramid.h,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1); barrier(pyramid.h,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,1,4); barrier(depth.h,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1); end();
	VkDescriptorImageInfo infos[6]={0}; infos[0]=(VkDescriptorImageInfo){f.sampler,depth.v[0],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; for(int i=1;i<6;++i) infos[i]=(VkDescriptorImageInfo){VK_NULL_HANDLE,pyramid.v[i-1],VK_IMAGE_LAYOUT_GENERAL}; VkWriteDescriptorSet writes[6]; for(int i=0;i<6;++i) writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=f.set,.dstBinding=(uint32_t)i,.descriptorCount=1,.descriptorType=i?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&infos[i]}; vkUpdateDescriptorSets(f.device,6,writes,0,NULL);
	float pc[12]={0,0,1.f/t.w,1.f/t.h,1,1,.25f,1,2.75f,0,0,0}; begin(); vkCmdBindPipeline(f.cb,VK_PIPELINE_BIND_POINT_COMPUTE,f.pipeline); vkCmdBindDescriptorSets(f.cb,VK_PIPELINE_BIND_POINT_COMPUTE,f.pipeline_layout,0,1,&f.set,0,NULL); vkCmdPushConstants(f.cb,f.pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(pc),pc); vkCmdDispatch(f.cb,(t.w+15)/16,(t.h+15)/16,1); barrier(pyramid.h,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,5);
	VkBufferImageCopy out[5]; for(int m=0;m<5;++m) out[m]=(VkBufferImageCopy){.bufferOffset=off[m],.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,(uint32_t)m,0,1},.imageExtent={(uint32_t)mw(t,m),(uint32_t)mh(t,m),1}}; vkCmdCopyImageToBuffer(f.cb,pyramid.h,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,io.h,5,out); VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=io.h,.size=bytes}; vkCmdPipelineBarrier(f.cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL); end();
	for(int m=0;m<5;++m) { uint16_t *got=(uint16_t *)((char *)io.map+off[m]); int n=mw(t,m)*mh(t,m)*2; for(int i=0;i<n;++i) if(got[i]!=want[m][i] && (got[i]==HMAX || want[m][i]==HMAX || abs((int)got[i]-(int)want[m][i])>1)) { fprintf(stderr,"SSAO_SHARED_MIP_MISMATCH case=%s mip=%d channel=%d got=%04x want=%04x\n",t.name,m,i&1,got[i],want[m][i]); exit(1); } free(want[m]); }
	printf("SSAO_SHARED_MIP_VULKAN_PASSED case=%s mips=5 fp32_rg16f_max_ulp=1\n",t.name); destroy_image(depth,1); destroy_image(pyramid,5); destroy_buffer(io);
}
int main(int argc,char **argv)
{
	if(argc!=2) { fprintf(stderr,"usage: %s ssao_mip_shared.comp.spv\n",argv[0]); return 2; } init_device(); init_pipeline(argv[1]); testcase_t cases[]={{32,32,"32x32"},{33,19,"33x19"},{17,17,"17x17"},{6,16,"6x16"}}; for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) run(cases[i]); vkDestroySampler(f.device,f.sampler,NULL); vkDestroyPipeline(f.device,f.pipeline,NULL); vkDestroyPipelineLayout(f.device,f.pipeline_layout,NULL); vkDestroyDescriptorPool(f.device,f.descriptor_pool,NULL); vkDestroyDescriptorSetLayout(f.device,f.set_layout,NULL); vkDestroyCommandPool(f.device,f.pool,NULL); vkDestroyDevice(f.device,NULL); vkDestroyInstance(f.instance,NULL); return 0;
}
