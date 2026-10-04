/* Real Vulkan mip qualification of the upstream seven-binding SSAO ABI.
 * Reuses the original scalar filter/tile oracle. Both eye layer views have
 * independent descriptor sets and clears; every dispatch reads back both eyes.
 * Run tests/run_ssao_mip_vulkan.py with an explicit stable shader root.
 * Exit 77 means the requested variant is unsupported, never a GPU pass.
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
typedef struct { VkImage h; VkDeviceMemory m; VkImageView v[2][5]; } image_t;
typedef struct { int w, h; const char *name; int scale, inset; } testcase_t;
static int use_fp16, use_subgroup;
static const char *variant;
static struct { VkInstance instance; VkPhysicalDevice gpu; VkDevice device; VkQueue queue;
	VkPhysicalDeviceMemoryProperties memory; VkCommandPool pool; VkCommandBuffer cb;
	VkDescriptorSetLayout set_layout; VkDescriptorPool descriptor_pool; VkDescriptorSet set[2];
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
		.format=format, .extent={(uint32_t)w,(uint32_t)h,1}, .mipLevels=(uint32_t)mips, .arrayLayers=2,
		.samples=VK_SAMPLE_COUNT_1_BIT, .tiling=VK_IMAGE_TILING_OPTIMAL, .usage=usage};
	VK(vkCreateImage(f.device, &ci, NULL, &x.h)); VkMemoryRequirements mr; vkGetImageMemoryRequirements(f.device, x.h, &mr);
	VkMemoryAllocateInfo ai = {.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize=mr.size, .memoryTypeIndex=memory_type(mr.memoryTypeBits, 0)};
	VK(vkAllocateMemory(f.device, &ai, NULL, &x.m)); VK(vkBindImageMemory(f.device, x.h, x.m, 0));
	for (int eye=0; eye<2; ++eye) for (int i=0; i<mips; ++i) { VkImageViewCreateInfo vi = {.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image=x.h,
		.viewType=VK_IMAGE_VIEW_TYPE_2D, .format=format, .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,(uint32_t)i,1,(uint32_t)eye,1}};
		VK(vkCreateImageView(f.device, &vi, NULL, &x.v[eye][i])); } return x;
}
static void destroy_buffer(buffer_t b) { if (b.h) { vkUnmapMemory(f.device,b.m); vkDestroyBuffer(f.device,b.h,NULL); vkFreeMemory(f.device,b.m,NULL); } }
static void destroy_image(image_t x, int mips) { for(int eye=0;eye<2;++eye) for(int i=0;i<mips;++i) if(x.v[eye][i]) vkDestroyImageView(f.device,x.v[eye][i],NULL); if(x.h) vkDestroyImage(f.device,x.h,NULL); if(x.m) vkFreeMemory(f.device,x.m,NULL); }
static void begin(void) { VK(vkResetCommandBuffer(f.cb,0)); VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT}; VK(vkBeginCommandBuffer(f.cb,&bi)); }
static void end(void) { VK(vkEndCommandBuffer(f.cb)); VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&f.cb}; VK(vkQueueSubmit(f.queue,1,&si,VK_NULL_HANDLE)); VK(vkQueueWaitIdle(f.queue)); }
static void barrier(VkImage image, VkImageLayout old, VkImageLayout next, VkAccessFlags src, VkAccessFlags dst, VkPipelineStageFlags from, VkPipelineStageFlags to, uint32_t mip, uint32_t levels, uint32_t eye, uint32_t eyes)
{
	VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=src,.dstAccessMask=dst,.oldLayout=old,.newLayout=next,
		.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,mip,levels,eye,eyes}};
	vkCmdPipelineBarrier(f.cb,from,to,0,0,NULL,0,NULL,1,&b);
}
static int extension(const char *name)
{
	uint32_t n=0; VK(vkEnumerateDeviceExtensionProperties(f.gpu,NULL,&n,NULL));
	VkExtensionProperties *p=calloc(n,sizeof(*p)); CHECK(p);
	VK(vkEnumerateDeviceExtensionProperties(f.gpu,NULL,&n,p)); int found=0;
	for(uint32_t i=0;i<n;++i) if(!strcmp(p[i].extensionName,name)) found=1;
	free(p); return found;
}
static void init_device(void)
{
	VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="ssao mip qualification",.apiVersion=VK_API_VERSION_1_1};
	VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app}; VkResult r=vkCreateInstance(&ii,NULL,&f.instance); if(r!=VK_SUCCESS) skip("vkCreateInstance",r);
	uint32_t n=0, family=0; r=vkEnumeratePhysicalDevices(f.instance,&n,NULL); if(r!=VK_SUCCESS||!n) skip("vkEnumeratePhysicalDevices",r);
	VkPhysicalDevice *g=calloc(n,sizeof(*g)); CHECK(g); VK(vkEnumeratePhysicalDevices(f.instance,&n,g));
	for(uint32_t i=0;i<n&&!f.gpu;++i) { VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(g[i],&p); if(p.apiVersion<VK_API_VERSION_1_1) continue;
		uint32_t q=0; vkGetPhysicalDeviceQueueFamilyProperties(g[i],&q,NULL); VkQueueFamilyProperties *qs=calloc(q,sizeof(*qs)); CHECK(qs); vkGetPhysicalDeviceQueueFamilyProperties(g[i],&q,qs);
		for(uint32_t j=0;j<q;++j) if(qs[j].queueCount&&(qs[j].queueFlags&VK_QUEUE_COMPUTE_BIT)) { f.gpu=g[i]; family=j; break; } free(qs); }
	free(g); if(!f.gpu) skip("no Vulkan 1.1 compute queue",VK_ERROR_FEATURE_NOT_PRESENT);
	VkPhysicalDeviceFeatures supported; vkGetPhysicalDeviceFeatures(f.gpu,&supported); if(!supported.shaderStorageImageExtendedFormats) skip("rg16f storage image",VK_ERROR_FEATURE_NOT_PRESENT);
	const VkFormat formats[]={VK_FORMAT_R16G16_SFLOAT,VK_FORMAT_R32_UINT,VK_FORMAT_R32_SFLOAT};
	const VkFormatFeatureFlags needed[]={VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT,
		VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT,VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT};
	for(int i=0;i<3;++i) { VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties(f.gpu,formats[i],&fp); if((fp.optimalTilingFeatures&needed[i])!=needed[i]) skip("required image format",VK_ERROR_FORMAT_NOT_SUPPORTED); }
	const char *extensions[2]; uint32_t extension_count=0;
	VkPhysicalDeviceShaderFloat16Int8Features half={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
	VkPhysicalDeviceSubgroupSizeControlFeaturesEXT control={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT};
	VkPhysicalDeviceFeatures2 features2={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
	if(use_fp16) {
		if(!extension(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME)) skip("renderer FP16 extension",VK_ERROR_FEATURE_NOT_PRESENT);
		extensions[extension_count++]=VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME; half.pNext=features2.pNext; features2.pNext=&half;
	}
	if(use_subgroup) {
		if(!extension(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME)) skip("renderer full subgroup extension",VK_ERROR_FEATURE_NOT_PRESENT);
		extensions[extension_count++]=VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME; control.pNext=features2.pNext; features2.pNext=&control;
	}
	vkGetPhysicalDeviceFeatures2(f.gpu,&features2);
	if(use_fp16&&!half.shaderFloat16) skip("shaderFloat16",VK_ERROR_FEATURE_NOT_PRESENT);
	VkPhysicalDeviceSubgroupSizeControlPropertiesEXT size={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES_EXT};
	VkPhysicalDeviceSubgroupProperties subgroup={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,.pNext=use_subgroup?&size:NULL};
	VkPhysicalDeviceProperties2 props={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&subgroup}; vkGetPhysicalDeviceProperties2(f.gpu,&props);
	if(use_subgroup&&(!control.subgroupSizeControl||!control.computeFullSubgroups||
		!(subgroup.supportedStages&VK_SHADER_STAGE_COMPUTE_BIT)||!(subgroup.supportedOperations&VK_SUBGROUP_FEATURE_SHUFFLE_BIT)||
		size.minSubgroupSize<4||size.maxSubgroupSize>64)) skip("renderer subgroup eligibility",VK_ERROR_FEATURE_NOT_PRESENT);
	printf("SSAO_MIP_DEVICE variant=%s name=%s subgroup=%u fp16=%d\n",variant,props.properties.deviceName,subgroup.subgroupSize,use_fp16);
	half.shaderInt8=VK_FALSE; control.subgroupSizeControl=use_subgroup; control.computeFullSubgroups=use_subgroup;
	float priority=1; VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
	VkPhysicalDeviceFeatures features={.shaderStorageImageExtendedFormats=VK_TRUE}; VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext=features2.pNext,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.pEnabledFeatures=&features,
		.enabledExtensionCount=extension_count,.ppEnabledExtensionNames=extensions};
	VK(vkCreateDevice(f.gpu,&di,NULL,&f.device)); vkGetDeviceQueue(f.device,family,0,&f.queue); vkGetPhysicalDeviceMemoryProperties(f.gpu,&f.memory);
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
	VkDescriptorSetLayoutBinding b[7]={0}; b[0]=(VkDescriptorSetLayoutBinding){0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT};
	for(int i=1;i<7;++i) b[i]=(VkDescriptorSetLayoutBinding){(uint32_t)i,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT};
	VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=7,.pBindings=b}; VK(vkCreateDescriptorSetLayout(f.device,&li,NULL,&f.set_layout));
	VkDescriptorPoolSize ps[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,12}}; VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=2,.poolSizeCount=2,.pPoolSizes=ps}; VK(vkCreateDescriptorPool(f.device,&pi,NULL,&f.descriptor_pool));
	VkDescriptorSetLayout layouts[2]={f.set_layout,f.set_layout}; VkDescriptorSetAllocateInfo si={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=f.descriptor_pool,.descriptorSetCount=2,.pSetLayouts=layouts}; VK(vkAllocateDescriptorSets(f.device,&si,f.set));
	VkPushConstantRange pc={VK_SHADER_STAGE_COMPUTE_BIT,0,48}; VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&f.set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&pc}; VK(vkCreatePipelineLayout(f.device,&pli,NULL,&f.pipeline_layout));
	VkShaderModule s=shader(path); VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=s,.pName="main",.flags=use_subgroup?(VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT_EXT|VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT):0},.layout=f.pipeline_layout}; VK(vkCreateComputePipelines(f.device,VK_NULL_HANDLE,1,&ci,NULL,&f.pipeline)); vkDestroyShaderModule(f.device,s,NULL);
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
static float depth_scene(int x,int y) { return x==2&&y==2 ? 0.f : ldexpf(1.f, -5 + ((x*19+y*7)%4)); }
static int scene_w(testcase_t t) { return (t.w-1)*t.scale+1; }
static int scene_h(testcase_t t) { return (t.h-1)*t.scale+1; }
static int viewport_x(testcase_t t) { return t.inset ? 3 : 0; }
static int viewport_y(testcase_t t) { return t.inset ? 1 : 0; }
static float sampled_scene(testcase_t t,int x,int y,int eye)
{
	int sx=x*t.scale,sy=y*t.scale,vx=viewport_x(t),vy=viewport_y(t);
	return sx<vx||sy<vy||sx>=scene_w(t)-vx||sy>=scene_h(t)-vy ? 0 : depth_scene(sx+11*eye,sy+7*eye);
}
static float view_depth(testcase_t t,int x,int y,int eye)
{ float z=sampled_scene(t,x,y,eye); return f32(f16(z>0?fminf(.25f/z,65472.f):65504.f)); }
/* Deliberately sparse mismatches: expected packed flags are derived directly
 * from pixel coordinates, independently of the shader's lane/tile reduction. */
static int mismatching(testcase_t t,int x,int y,int eye,int round)
{
	if(eye==round) return 0;
	if(!eye) return (x==0&&y==0)||(x==127&&y==127)||(x==1023&&y==511);
	return (x==128&&y==128)||(x==1024&&y==512)||(x==t.w-1&&y==t.h-1);
}
static float prepared_world(testcase_t t,int x,int y,int eye,int round)
{ float z=view_depth(t,x,y,eye); return mismatching(t,x,y,eye,round) ? (z==65504.f?4.f:z+1.f) : z; }
static int tag_w(testcase_t t) { return (t.w+1023)/1024; }
static int tag_h(testcase_t t) { return (t.h+511)/512; }
static void reference_tags(testcase_t t,uint32_t *tags,int eye,int round)
{
	memset(tags,0,(size_t)tag_w(t)*tag_h(t)*4);
	for(int y=0;y<t.h;++y) for(int x=0;x<t.w;++x) if(mismatching(t,x,y,eye,round)) {
		int tx=x/128,ty=y/128;
		tags[(ty/4)*tag_w(t)+tx/8] |= 1u<<((ty%4)*8+tx%8);
	}
}
static float hp(float x) { return use_fp16?f32(f16(x)):x; }
static float clamp1(float x) { return x<0?0:x>1?1:x; }
static float filter(float a,float b,float c,float d,float radius)
{
	float farthest=fmaxf(fmaxf(a<65504.f?a:0,b<65504.f?b:0),fmaxf(c<65504.f?c:0,d<65504.f?d:0)); if(farthest==0) return 65504.f;
	a=a<65504.f?a:farthest; b=b<65504.f?b:farthest; c=c<65504.f?c:farthest; d=d<65504.f?d:farthest;
	float effect=hp(hp(hp(.75f)*hp(radius))*hp(1.457f)), range=hp(hp(.615f)*effect), from=hp(effect*hp(1.f-hp(.615f))), mul=hp(-1.f/range), add=hp(hp(from/range)+1.f);
	float w0=clamp1(hp(hp(hp(farthest-a)*mul)+add)),w1=clamp1(hp(hp(hp(farthest-b)*mul)+add)),w2=clamp1(hp(hp(hp(farthest-c)*mul)+add)),w3=clamp1(hp(hp(hp(farthest-d)*mul)+add));
	return hp(hp(hp(hp(hp(w0*a)+hp(w1*b))+hp(w2*c))+hp(w3*d))/hp(hp(hp(w0+w1)+w2)+w3));
}
static int mw(testcase_t c,int m) { int x=c.w>>m; return x?x:1; } static int mh(testcase_t c,int m) { int y=c.h>>m; return y?y:1; }
static void put(uint16_t *out,int p,float a,float b) { out[2*p]=f16(a); out[2*p+1]=f16(b); }
static void reduce2(float *out,const float *a,const float *b,const float *c,const float *d) { out[0]=filter(a[0],b[0],c[0],d[0],2.75f); out[1]=filter(a[1],b[1],c[1],d[1],2.75f); }
static void reference(testcase_t t,uint16_t *want[5],int eye,int round)
{
	float *base=calloc((size_t)t.w*t.h*2,sizeof(*base)); CHECK(base); for(int y=0;y<t.h;++y) for(int x=0;x<t.w;++x) { int p=y*t.w+x; base[2*p]=prepared_world(t,x,y,eye,round); base[2*p+1]=view_depth(t,x,y,eye); put(want[0],p,base[2*p],base[2*p+1]); }
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
	free(base);
}
/* FP32 allows one half ULP for GPU contraction; FP16 allows four for the
 * explicitly rounded arithmetic oracle. Missing-depth values and tags are exact.
 * The untouched eye is compared byte-for-byte to its previous GPU snapshot. */
static int run(testcase_t t)
{
	int case_failed=0;
	VkDeviceSize off[2][5],tag_off[2],bytes=0;
	for(int eye=0;eye<2;++eye) for(int m=0;m<5;++m) { off[eye][m]=bytes; bytes+=(VkDeviceSize)mw(t,m)*mh(t,m)*4; }
	const size_t tag_bytes=(size_t)tag_w(t)*tag_h(t)*4;
	for(int eye=0;eye<2;++eye) { tag_off[eye]=bytes; bytes+=tag_bytes; }
	VkDeviceSize scene_off=bytes,scene_bytes=(VkDeviceSize)scene_w(t)*scene_h(t)*4;
	buffer_t io=buffer(bytes+2*scene_bytes);
	uint16_t *want[2][5],*snapshot[2][5]; uint32_t *tags[2],*tag_snapshot[2];
	for(int eye=0;eye<2;++eye) {
		for(int m=0;m<5;++m) { size_t n=(size_t)mw(t,m)*mh(t,m)*2;
			want[eye][m]=malloc(n*2); snapshot[eye][m]=malloc(n*2); CHECK(want[eye][m]&&snapshot[eye][m]);
			for(size_t i=0;i<n;++i) snapshot[eye][m][i]=HMAX;
		}
		for(int y=0;y<t.h;++y) for(int x=0;x<t.w;++x) snapshot[eye][0][2*(y*t.w+x)]=f16(prepared_world(t,x,y,eye,0));
		tags[eye]=malloc(tag_bytes); tag_snapshot[eye]=malloc(tag_bytes); CHECK(tags[eye]&&tag_snapshot[eye]);
		for(size_t i=0;i<tag_bytes/4;++i) tag_snapshot[eye][i]=0xa5a5a5a5u;
		memcpy((char *)io.map+off[eye][0],snapshot[eye][0],(size_t)t.w*t.h*4);
		float *scene=(float *)((char *)io.map+scene_off+eye*scene_bytes);
		for(int y=0;y<scene_h(t);++y) for(int x=0;x<scene_w(t);++x) scene[y*scene_w(t)+x]=depth_scene(x+11*eye,y+7*eye);
	}
	CHECK(memcmp(snapshot[0][0],snapshot[1][0],(size_t)t.w*t.h*4)!=0);
	image_t pyramid=image(VK_FORMAT_R16G16_SFLOAT,t.w,t.h,5,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	image_t depth=image(VK_FORMAT_R32_SFLOAT,scene_w(t),scene_h(t),1,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	image_t mismatch=image(VK_FORMAT_R32_UINT,tag_w(t),tag_h(t),1,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	begin();
	barrier(pyramid.h,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,5,0,2);
	barrier(depth.h,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,0,2);
	barrier(mismatch.h,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,0,2);
	VkClearColorValue missing={.float32={65504.f,65504.f,0,0}},poison={.uint32={0xa5a5a5a5u,0,0,0}};
	VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,5,0,2}; vkCmdClearColorImage(f.cb,pyramid.h,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&missing,1,&range);
	range.levelCount=1; vkCmdClearColorImage(f.cb,mismatch.h,VK_IMAGE_LAYOUT_GENERAL,&poison,1,&range);
	barrier(pyramid.h,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,0,2);
	for(int eye=0;eye<2;++eye) {
		VkBufferImageCopy copy={.bufferOffset=off[eye][0],.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,(uint32_t)eye,1},.imageExtent={(uint32_t)t.w,(uint32_t)t.h,1}};
		vkCmdCopyBufferToImage(f.cb,io.h,pyramid.h,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
		copy.bufferOffset=scene_off+eye*scene_bytes; copy.imageExtent=(VkExtent3D){(uint32_t)scene_w(t),(uint32_t)scene_h(t),1};
		vkCmdCopyBufferToImage(f.cb,io.h,depth.h,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
	}
	barrier(pyramid.h,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,5,0,2);
	barrier(depth.h,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,0,2);
	barrier(mismatch.h,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,0,2); end();
	for(int eye=0;eye<2;++eye) {
		VkDescriptorImageInfo infos[7]={0}; infos[0]=(VkDescriptorImageInfo){f.sampler,depth.v[eye][0],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		for(int i=1;i<6;++i) infos[i]=(VkDescriptorImageInfo){VK_NULL_HANDLE,pyramid.v[eye][i-1],VK_IMAGE_LAYOUT_GENERAL};
		infos[6]=(VkDescriptorImageInfo){VK_NULL_HANDLE,mismatch.v[eye][0],VK_IMAGE_LAYOUT_GENERAL};
		VkWriteDescriptorSet writes[7]; for(int i=0;i<7;++i) writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=f.set[eye],.dstBinding=(uint32_t)i,.descriptorCount=1,.descriptorType=i?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&infos[i]}; vkUpdateDescriptorSets(f.device,7,writes,0,NULL);
	}
	for(int round=0;round<2;++round) for(int eye=0;eye<2;++eye) {
		reference(t,want[eye],eye,round); reference_tags(t,tags[eye],eye,round);
		if(t.w==1025&&t.h==513) {
			if(eye==round) CHECK(tags[eye][0]==0&&tags[eye][1]==0&&tags[eye][2]==0&&tags[eye][3]==0);
			else if(eye==0) CHECK(tags[eye][0]==0x80000001u&&tags[eye][1]==0&&tags[eye][2]==0&&tags[eye][3]==0);
			else CHECK(tags[eye][0]==0x200u&&tags[eye][1]==0&&tags[eye][2]==0&&tags[eye][3]==1);
		}
		uint16_t *base=(uint16_t *)((char *)io.map+off[eye][0]);
		for(int y=0;y<t.h;++y) for(int x=0;x<t.w;++x) put(base,y*t.w+x,prepared_world(t,x,y,eye,round),65504.f);
		begin();
		barrier(pyramid.h,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,eye,1);
		VkBufferImageCopy input={.bufferOffset=off[eye][0],.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,(uint32_t)eye,1},.imageExtent={(uint32_t)t.w,(uint32_t)t.h,1}};
		vkCmdCopyBufferToImage(f.cb,io.h,pyramid.h,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&input);
		barrier(pyramid.h,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,eye,1);
		barrier(mismatch.h,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,eye,1);
		VkClearColorValue clear={.uint32={0,0,0,0}}; VkImageSubresourceRange tag_range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,(uint32_t)eye,1};
		vkCmdClearColorImage(f.cb,mismatch.h,VK_IMAGE_LAYOUT_GENERAL,&clear,1,&tag_range);
		barrier(mismatch.h,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,eye,1);
		float pc[12]={(float)viewport_x(t),(float)viewport_y(t),1.f/(scene_w(t)-2*viewport_x(t)),1.f/(scene_h(t)-2*viewport_y(t)),1,1,.25f,1,2.75f,0,0,(float)t.scale};
		vkCmdBindPipeline(f.cb,VK_PIPELINE_BIND_POINT_COMPUTE,f.pipeline); vkCmdBindDescriptorSets(f.cb,VK_PIPELINE_BIND_POINT_COMPUTE,f.pipeline_layout,0,1,&f.set[eye],0,NULL); vkCmdPushConstants(f.cb,f.pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(pc),pc); vkCmdDispatch(f.cb,(t.w+15)/16,(t.h+15)/16,1);
		barrier(pyramid.h,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,5,0,2);
		barrier(mismatch.h,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,0,2);
		for(int other=0;other<2;++other) {
			VkBufferImageCopy out[5]; for(int m=0;m<5;++m) out[m]=(VkBufferImageCopy){.bufferOffset=off[other][m],.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,(uint32_t)m,(uint32_t)other,1},.imageExtent={(uint32_t)mw(t,m),(uint32_t)mh(t,m),1}};
			vkCmdCopyImageToBuffer(f.cb,pyramid.h,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,io.h,5,out);
			VkBufferImageCopy tag_copy={.bufferOffset=tag_off[other],.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,(uint32_t)other,1},.imageExtent={(uint32_t)tag_w(t),(uint32_t)tag_h(t),1}};
			vkCmdCopyImageToBuffer(f.cb,mismatch.h,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,io.h,1,&tag_copy);
		}
		VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=io.h,.size=bytes}; vkCmdPipelineBarrier(f.cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
		barrier(pyramid.h,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,5,0,2);
		barrier(mismatch.h,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,0,2); end();
		for(int other=0;other<2;++other) {
			for(int m=0;m<5;++m) { uint16_t *got=(uint16_t *)((char *)io.map+off[other][m]); size_t n=(size_t)mw(t,m)*mh(t,m)*2;
				if(other!=eye) {
					if(memcmp(got,snapshot[other][m],n*2)) {
						if(!case_failed) fprintf(stderr,"SSAO_MIP_EYE_ISOLATION_MISMATCH variant=%s case=%s scale=%d dispatch_eye=%d read_eye=%d round=%d mip=%d\n",variant,t.name,t.scale,eye,other,round,m);
						case_failed=1;
					}
				}
				else for(size_t i=0;i<n;++i) if(got[i]!=want[eye][m][i]&&(got[i]==HMAX||want[eye][m][i]==HMAX||abs((int)got[i]-(int)want[eye][m][i])>(use_fp16?4:1))) {
					if(!case_failed) fprintf(stderr,"SSAO_MIP_MISMATCH variant=%s case=%s scale=%d eye=%d round=%d mip=%d pixel=%zu channel=%zu got=%04x want=%04x\n",variant,t.name,t.scale,eye,round,m,i/2,i&1,got[i],want[eye][m][i]); case_failed=1; break; }
				memcpy(snapshot[other][m],got,n*2);
			}
			uint32_t *got=(uint32_t *)((char *)io.map+tag_off[other]);
			const uint32_t *expected=other==eye?tags[eye]:tag_snapshot[other];
			for(size_t i=0;i<tag_bytes/4;++i) if(got[i]!=expected[i]) { if(!case_failed) fprintf(stderr,"SSAO_MIP_TAG_MISMATCH variant=%s case=%s scale=%d dispatch_eye=%d read_eye=%d round=%d word=%zu got=%08x want=%08x\n",variant,t.name,t.scale,eye,other,round,i,got[i],expected[i]); case_failed=1; break; }
			memcpy(tag_snapshot[other],got,tag_bytes);
		}
	}
	printf("SSAO_MIP_CASE_%s variant=%s case=%s scale=%d inset=%d eyes=2 rounds=2 mips=5 exact_tags_and_other_eye\n",case_failed?"FAILED":"PASSED",variant,t.name,t.scale,t.inset);
	for(int eye=0;eye<2;++eye) { for(int m=0;m<5;++m) { free(want[eye][m]); free(snapshot[eye][m]); } free(tags[eye]); free(tag_snapshot[eye]); }
	destroy_image(mismatch,1); destroy_image(depth,1); destroy_image(pyramid,5); destroy_buffer(io);
	return case_failed;
}
int main(int argc,char **argv)
{
	if(argc!=2&&argc!=3) { fprintf(stderr,"usage: %s shader.spv [shared-fp32|shared-fp16|subgroup-fp32|subgroup-fp16]\n",argv[0]); return 2; }
	variant=argc==3?argv[2]:"shared-fp32"; CHECK(!strcmp(variant,"shared-fp32")||!strcmp(variant,"shared-fp16")||!strcmp(variant,"subgroup-fp32")||!strcmp(variant,"subgroup-fp16"));
	use_fp16=strstr(variant,"fp16")!=NULL; use_subgroup=!strncmp(variant,"subgroup",8);
	init_device(); init_pipeline(argv[1]);
	testcase_t cases[]={{32,32,"32x32",1,0},{33,19,"33x19",1,1},{17,17,"17x17",1,0},{6,16,"6x16",1,0},
		{128,128,"128x128",1,0},{129,129,"129x129",1,1},{257,131,"257x131",1,0},{1025,513,"1025x513",1,0}};
	int failed=0;
	for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) for(int scale=1;scale<=2;++scale) { cases[i].scale=scale; failed+=run(cases[i]); }
	vkDestroySampler(f.device,f.sampler,NULL); vkDestroyPipeline(f.device,f.pipeline,NULL); vkDestroyPipelineLayout(f.device,f.pipeline_layout,NULL); vkDestroyDescriptorPool(f.device,f.descriptor_pool,NULL); vkDestroyDescriptorSetLayout(f.device,f.set_layout,NULL); vkDestroyCommandPool(f.device,f.pool,NULL); vkDestroyDevice(f.device,NULL); vkDestroyInstance(f.instance,NULL);
	if(failed) { printf("SSAO_MIP_VULKAN_FAILED variant=%s cases=16 dispatches=64 failed_cases=%d\n",variant,failed); return 1; }
	printf("SSAO_MIP_VULKAN_PASSED variant=%s cases=16 dispatches=64\n",variant);
	if(!strcmp(variant,"shared-fp32")) printf("SSAO_SHARED_MIP_VULKAN_PASSED seven_bindings exact_tags both_eyes\n");
	return 0;
}
