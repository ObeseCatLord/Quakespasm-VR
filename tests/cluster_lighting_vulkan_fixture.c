/* Deferred real-SPIR-V compute fixture. Adapted from ssao_shared_mip_vulkan_fixture.c.
 * Uses only its own instance/device/buffers/queue/fence; software ICD is acceptable.
 * Build/run instructions are in cluster_lighting_graphics_cases.md. Exit 77: unavailable.
 * CPU reference is independent of GLSL execution; actual dispatch/readback must agree.
 */
#include <vulkan/vulkan.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"CLUSTER_VULKAN_FAILED line=%d: %s\n",__LINE__,#x); exit(1); } } while (0)
#define VK(x) do { VkResult r_=(x); if(r_!=VK_SUCCESS) { fprintf(stderr,"CLUSTER_VULKAN_FAILED line=%d: %s result=%d\n",__LINE__,#x,r_); exit(1); } } while (0)
#define NX 32u
#define NY 16u
#define NZ 32u
#define NC (NX*NY*NZ)
typedef struct { float origin[3], radius, color[3], minlight, cone_dir[3], cone_cos; } light_t;
typedef struct {
	float inverse[2][16], eye[2][4], forward[2][4], viewport[4], params[4];
	uint32_t counts[4]; light_t lights[64]; uint32_t masks[2][NC][2];
} frame_t;
_Static_assert(sizeof(light_t)==48 && sizeof(frame_t)==265456,"real frame ABI size");
_Static_assert(offsetof(frame_t,eye)==128 && offsetof(frame_t,forward)==160,"eye ABI");
_Static_assert(offsetof(frame_t,viewport)==192 && offsetof(frame_t,params)==208 && offsetof(frame_t,counts)==224,"header ABI");
_Static_assert(offsetof(frame_t,lights)==240 && offsetof(frame_t,masks)==3312,"light/mask ABI");
typedef struct { VkBuffer h; VkDeviceMemory memory; void *map; int coherent; } buffer_t;
typedef struct { double x,y,z; } vec3;
typedef struct { vec3 lo,hi; } bounds_t;
typedef struct { unsigned eye,x,y,z,light; } witness_t;
typedef struct { const char *name; frame_t *input; witness_t witnesses[70]; unsigned witnesses_n; } case_t;
static struct {
	VkInstance instance; VkPhysicalDevice gpu; VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceMemoryProperties memory; VkDevice device; VkQueue queue;
	VkCommandPool pool; VkCommandBuffer cb; VkFence fence;
	VkDescriptorSetLayout empty_layout,set_layout; VkDescriptorPool descriptor_pool;
	VkDescriptorSet sets[2]; VkPipelineLayout pipeline_layout; VkPipeline pipeline;
	VkDeviceSize stride; buffer_t upload,readback;
} f;

static void skip(const char *where,VkResult r)
{
	printf("CLUSTER_VULKAN_SKIPPED %s result=%d\n",where,r);
	if(f.instance) vkDestroyInstance(f.instance,NULL);
	exit(77);
}
static void init_device(void)
{
	VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="cluster light fixture",.apiVersion=VK_API_VERSION_1_1};
	VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
	VkResult r=vkCreateInstance(&ii,NULL,&f.instance); if(r!=VK_SUCCESS) skip("instance",r);
	uint32_t n=0,family=0; r=vkEnumeratePhysicalDevices(f.instance,&n,NULL); if(r!=VK_SUCCESS||!n) skip("physical devices",r);
	VkPhysicalDevice *g=calloc(n,sizeof(*g)); CHECK(g); VK(vkEnumeratePhysicalDevices(f.instance,&n,g));
	for(uint32_t i=0;i<n&&!f.gpu;++i) {
		VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(g[i],&p);
		if(p.apiVersion<VK_API_VERSION_1_1||p.limits.maxBoundDescriptorSets<5||p.limits.maxStorageBufferRange<sizeof(frame_t)) continue;
		uint32_t q=0; vkGetPhysicalDeviceQueueFamilyProperties(g[i],&q,NULL);
		VkQueueFamilyProperties *qs=calloc(q,sizeof(*qs)); CHECK(qs); vkGetPhysicalDeviceQueueFamilyProperties(g[i],&q,qs);
		for(uint32_t j=0;j<q;++j) if(qs[j].queueCount&&(qs[j].queueFlags&VK_QUEUE_COMPUTE_BIT)) { f.gpu=g[i]; family=j; f.properties=p; break; }
		free(qs);
	}
	free(g); if(!f.gpu) skip("no Vulkan 1.1 compute device with set4/storage range",VK_ERROR_FEATURE_NOT_PRESENT);
	float priority=1; VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
	VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
	r=vkCreateDevice(f.gpu,&di,NULL,&f.device); if(r!=VK_SUCCESS) skip("device",r);
	vkGetDeviceQueue(f.device,family,0,&f.queue); vkGetPhysicalDeviceMemoryProperties(f.gpu,&f.memory);
	VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};
	VK(vkCreateCommandPool(f.device,&pi,NULL,&f.pool));
	VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=f.pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
	VK(vkAllocateCommandBuffers(f.device,&ai,&f.cb)); VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VK(vkCreateFence(f.device,&fi,NULL,&f.fence));
	/* Use at least 256-byte alignment so the two byte-addressed slots have a
	 * padding gap even when the device only requires 16; catch typed-array offsets. */
	VkDeviceSize alignment=f.properties.limits.minStorageBufferOffsetAlignment; if(alignment<256) alignment=256;
	f.stride=((sizeof(frame_t)+alignment-1)/alignment)*alignment;
	CHECK(f.stride%f.properties.limits.minStorageBufferOffsetAlignment==0);
	printf("CLUSTER_VULKAN_DEVICE %s stride=%llu min_alignment=%llu\n",f.properties.deviceName,
		(unsigned long long)f.stride,(unsigned long long)f.properties.limits.minStorageBufferOffsetAlignment);
}
static buffer_t buffer(VkDeviceSize size,VkBufferUsageFlags usage)
{
	buffer_t b={0}; VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=usage};
	VK(vkCreateBuffer(f.device,&ci,NULL,&b.h)); VkMemoryRequirements mr; vkGetBufferMemoryRequirements(f.device,b.h,&mr);
	uint32_t type=UINT32_MAX;
	for(uint32_t i=0;i<f.memory.memoryTypeCount;++i) if((mr.memoryTypeBits&(1u<<i))&&(f.memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
		if(type==UINT32_MAX) type=i;
		if(f.memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) { type=i; break; }
	}
	CHECK(type!=UINT32_MAX); b.coherent=!!(f.memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,.memoryTypeIndex=type};
	VK(vkAllocateMemory(f.device,&ai,NULL,&b.memory)); VK(vkBindBufferMemory(f.device,b.h,b.memory,0)); VK(vkMapMemory(f.device,b.memory,0,VK_WHOLE_SIZE,0,&b.map)); return b;
}
static void cache(buffer_t b,int invalidate)
{
	if(b.coherent) return;
	VkMappedMemoryRange r={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=b.memory,.size=VK_WHOLE_SIZE};
	if(invalidate) VK(vkInvalidateMappedMemoryRanges(f.device,1,&r)); else VK(vkFlushMappedMemoryRanges(f.device,1,&r));
}
static void destroy_buffer(buffer_t b) { vkUnmapMemory(f.device,b.memory); vkDestroyBuffer(f.device,b.h,NULL); vkFreeMemory(f.device,b.memory,NULL); }
/* Minimal SPIR-V reflection: inspect production descriptor and layout decorations,
 * not a separately compiled test shader. Opcodes/decorations are SPIR-V core values. */
static const uint32_t *instruction(const uint32_t *c,size_t n,uint32_t op,uint32_t id)
{
	for(size_t i=5;i<n;i+=c[i]>>16) { CHECK((c[i]>>16)>0&&i+(c[i]>>16)<=n); if((c[i]&65535)==op&&c[i+1]==id) return c+i; }
	return NULL;
}
static uint32_t decoration(const uint32_t *c,size_t n,uint32_t id,uint32_t member,uint32_t kind)
{
	for(size_t i=5;i<n;i+=c[i]>>16) {
		if(member==UINT32_MAX&&(c[i]&65535)==71&&(c[i]>>16)>=4&&c[i+1]==id&&c[i+2]==kind) return c[i+3];
		if(member!=UINT32_MAX&&(c[i]&65535)==72&&(c[i]>>16)>=5&&c[i+1]==id&&c[i+2]==member&&c[i+3]==kind) return c[i+4];
	}
	return UINT32_MAX;
}
static VkShaderModule shader(const char *path)
{
	FILE *in=fopen(path,"rb"); if(!in) { perror(path); exit(1); }
	CHECK(!fseek(in,0,SEEK_END)); long bytes=ftell(in); CHECK(bytes>=20&&!(bytes&3)&&!fseek(in,0,SEEK_SET));
	size_t n=(size_t)bytes/4; uint32_t *c=malloc((size_t)bytes); CHECK(c&&fread(c,4,n,in)==n&&!fclose(in)&&c[0]==0x07230203);
	const uint32_t *block=NULL;
	for(size_t i=5;i<n;i+=c[i]>>16) {
		CHECK((c[i]>>16)>0&&i+(c[i]>>16)<=n);
		if((c[i]&65535)==59&&(c[i]>>16)>=4&&decoration(c,n,c[i+2],UINT32_MAX,34)==4&&decoration(c,n,c[i+2],UINT32_MAX,33)==2) {
			const uint32_t *p=instruction(c,n,32,c[i+1]); CHECK(p&&(p[0]>>16)==4); block=instruction(c,n,30,p[3]); break;
		}
	}
	CHECK(block&&(block[0]>>16)==10); const uint32_t offsets[8]={0,128,160,192,208,224,240,3312};
	for(unsigned i=0;i<8;++i) CHECK(decoration(c,n,block[1],i,35)==offsets[i]);
	CHECK(decoration(c,n,block[1],0,7)==16); /* MatrixStride */
	const unsigned members[5]={0,1,2,6,7}, strides[5]={64,16,16,48,131072};
	for(unsigned i=0;i<5;++i) CHECK(decoration(c,n,block[2+members[i]],UINT32_MAX,6)==strides[i]);
	const uint32_t *outer=instruction(c,n,28,block[9]); CHECK(outer&&decoration(c,n,outer[2],UINT32_MAX,6)==8);
	const uint32_t *light_array=instruction(c,n,28,block[8]); CHECK(light_array);
	const uint32_t *light_struct=instruction(c,n,30,light_array[2]); CHECK(light_struct&&(light_struct[0]>>16)==8);
	const uint32_t light_offsets[6]={0,12,16,28,32,44};
	for(unsigned i=0;i<6;++i) CHECK(decoration(c,n,light_struct[1],i,35)==light_offsets[i]);
	VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)bytes,.pCode=c};
	VkShaderModule module; VK(vkCreateShaderModule(f.device,&ci,NULL,&module)); free(c);
	puts("CLUSTER_VULKAN_SPIRV_ABI_PASSED set=4 binding=2 offsets/matrix/array-strides"); return module;
}
static void init_pipeline(const char *path)
{
	VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
	VK(vkCreateDescriptorSetLayout(f.device,&li,NULL,&f.empty_layout));
	VkDescriptorSetLayoutBinding bindings[3]; for(unsigned i=0;i<3;++i) bindings[i]=(VkDescriptorSetLayoutBinding){i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
	li.bindingCount=3; li.pBindings=bindings; VK(vkCreateDescriptorSetLayout(f.device,&li,NULL,&f.set_layout));
	VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,6}; VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=2,.poolSizeCount=1,.pPoolSizes=&ps};
	VK(vkCreateDescriptorPool(f.device,&pi,NULL,&f.descriptor_pool)); VkDescriptorSetLayout slots[2]={f.set_layout,f.set_layout};
	VkDescriptorSetAllocateInfo si={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=f.descriptor_pool,.descriptorSetCount=2,.pSetLayouts=slots};
	VK(vkAllocateDescriptorSets(f.device,&si,f.sets));
	VkDescriptorSetLayout layouts[5]={f.empty_layout,f.empty_layout,f.empty_layout,f.empty_layout,f.set_layout};
	VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=5,.pSetLayouts=layouts};
	VK(vkCreatePipelineLayout(f.device,&pli,NULL,&f.pipeline_layout)); VkShaderModule module=shader(path);
	VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=f.pipeline_layout};
	VK(vkCreateComputePipelines(f.device,VK_NULL_HANDLE,1,&ci,NULL,&f.pipeline)); vkDestroyShaderModule(f.device,module,NULL);
	f.upload=buffer(f.stride*2,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	f.readback=buffer(f.stride*2,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	for(unsigned slot=0;slot<2;++slot) {
		VkDescriptorBufferInfo info[3]={{f.upload.h,0,16},{f.upload.h,0,16},{f.upload.h,slot*f.stride,sizeof(frame_t)}};
		VkWriteDescriptorSet writes[3]; for(unsigned i=0;i<3;++i) writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=f.sets[slot],.dstBinding=i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&info[i]};
		vkUpdateDescriptorSets(f.device,3,writes,0,NULL); /* Immutable for all submissions. */
	}
}
static vec3 add(vec3 a,vec3 b) { return (vec3){a.x+b.x,a.y+b.y,a.z+b.z}; }
static vec3 scale(vec3 a,double b) { return (vec3){a.x*b,a.y*b,a.z*b}; }
static double dot(vec3 a,vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static vec3 read3(const float *p) { return (vec3){p[0],p[1],p[2]}; }
static vec3 unproject(const float *m,double x,double y)
{
	double w=m[3]*x+m[7]*y+m[11]+m[15]; CHECK(isfinite(w)&&fabs(w)>1e-20);
	return (vec3){(m[0]*x+m[4]*y+m[8]+m[12])/w,(m[1]*x+m[5]*y+m[9]+m[13])/w,(m[2]*x+m[6]*y+m[10]+m[14])/w};
}
static vec3 ray(const frame_t *p,unsigned eye,double x,double y)
{
	vec3 v=add(unproject(p->inverse[eye],x,y),scale(read3(p->eye[eye]),-1));
	double depth=dot(v,read3(p->forward[eye])); CHECK(isfinite(depth)&&depth>1e-6); return scale(v,1/depth);
}
static double boundary(const frame_t *p,unsigned z) { return p->params[0]*pow(p->params[1]/p->params[0],z/32.0); }
static bounds_t bounds(const frame_t *p,unsigned eye,unsigned x,unsigned y,unsigned z,int padded)
{
	double px=padded?2.0*p->counts[2]/p->viewport[2]:0, py=padded?2.0*p->counts[3]/p->viewport[3]:0;
	bounds_t b={{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};
	for(unsigned corner=0;corner<8;++corner) {
		double cx=-1+2.0*(x+!!(corner&1))/NX+(corner&1?px:-px), cy=-1+2.0*(y+!!(corner&2))/NY+(corner&2?py:-py);
		vec3 v=add(read3(p->eye[eye]),scale(ray(p,eye,cx,cy),corner&4?boundary(p,z+1):z?boundary(p,z):0));
		CHECK(isfinite(v.x)&&isfinite(v.y)&&isfinite(v.z));
		b.lo=(vec3){fmin(b.lo.x,v.x),fmin(b.lo.y,v.y),fmin(b.lo.z,v.z)};
		b.hi=(vec3){fmax(b.hi.x,v.x),fmax(b.hi.y,v.y),fmax(b.hi.z,v.z)};
	}
	return b;
}
static double distance(bounds_t b,vec3 p)
{
	vec3 excess={fmax(fmax(b.lo.x-p.x,p.x-b.hi.x),0),fmax(fmax(b.lo.y-p.y,p.y-b.hi.y),0),fmax(fmax(b.lo.z-p.z,p.z-b.hi.z),0)};
	return sqrt(dot(excess,excess));
}
static void projection(frame_t *p,unsigned eye,double cant,double sx,double sy)
{
	vec3 origin={eye?17:-11,eye?-9:3,eye?31:-7}, right={cos(cant),0,-sin(cant)}, up={0,1,0}, forward={sin(cant),0,cos(cant)};
	vec3 columns[4]={right,up,scale(origin,1.0/4-1.0/4096),add(add(add(forward,scale(right,sx)),scale(up,sy)),scale(origin,1.0/4096))};
	float *m=p->inverse[eye]; for(unsigned c=0;c<4;++c) { m[4*c]=(float)columns[c].x; m[4*c+1]=(float)columns[c].y; m[4*c+2]=(float)columns[c].z; }
	m[11]=1.0f/4-1.0f/4096; m[15]=1.0f/4096;
	p->eye[eye][0]=(float)origin.x; p->eye[eye][1]=(float)origin.y; p->eye[eye][2]=(float)origin.z; p->eye[eye][3]=1;
	vec3 fwd=add(unproject(m,0,0),scale(origin,-1)); fwd=scale(fwd,1/sqrt(dot(fwd,fwd)));
	p->forward[eye][0]=(float)fwd.x; p->forward[eye][1]=(float)fwd.y; p->forward[eye][2]=(float)fwd.z;
}
static void witness(case_t *c,unsigned light,unsigned eye,unsigned x,unsigned y,unsigned z,double nx,double ny,double depth)
{
	frame_t *p=c->input; vec3 point=add(read3(p->eye[eye]),scale(ray(p,eye,nx,ny),depth));
	p->lights[light]=(light_t){{(float)point.x,(float)point.y,(float)point.z},0.02f,{1,0.5f,0.25f},0,{0,0,0},-1};
	CHECK(c->witnesses_n<70); c->witnesses[c->witnesses_n++]=(witness_t){eye,x,y,z,light};
}
static case_t testcase(const char *name,unsigned eyes,unsigned lights,int canted,int special)
{
	case_t c={.name=name,.input=calloc(1,sizeof(frame_t))}; CHECK(c.input); frame_t *p=c.input;
	p->viewport[0]=37; p->viewport[1]=19; p->viewport[2]=1024; p->viewport[3]=768;
	p->params[0]=4; p->params[1]=4096; p->params[2]=0.75f; p->params[3]=1;
	p->counts[0]=lights; p->counts[1]=eyes; p->counts[2]=16; p->counts[3]=8;
	for(unsigned e=0;e<2;++e) projection(p,e,canted?(e?0.6:-0.6):0,canted?(e?0.3:-0.3):0,canted?-0.15:0);
	for(unsigned i=0;i<lights;++i) {
		unsigned e=eyes==2?i&1:0, x=(i%4==0)?0:(i%4==1)?15:(i%4==2)?16:31;
		unsigned y=(i/4%4==0)?0:(i/4%4==1)?7:(i/4%4==2)?8:15, z=(i/16%4==0)?0:(i/16%4==1)?8:(i/16%4==2)?16:30;
		double tx=i%3==0?0.02:i%3==1?0.5:0.98, ty=i%3==0?0.98:i%3==1?0.5:0.02;
		double depth=z?(boundary(p,z)+boundary(p,z+1))*0.5:0.05;
		if(i==63) { z=31; depth=1e8; }
		witness(&c,i,e,x,y,z,-1+2*(x+tx)/NX,-1+2*(y+ty)/NY,depth);
	}
	if(special==1) {
		/* Tiny light beyond the unpadded extreme tile's AABB but inside padding. */
		c.witnesses_n=0; unsigned z=12; double pad=2.0*p->counts[2]/p->viewport[2];
		witness(&c,0,0,31,8,z,1+0.75*pad,0.0625,boundary(p,z+1)*0.999);
		CHECK(distance(bounds(p,0,31,8,z,0),read3(p->lights[0].origin))>p->lights[0].radius);
	}
	if(special==2) {
		/* Shared central boundary: all four adjacent tiles must retain the same
		 * tiny isolated light at a finite slice's upper plane in each eye. */
		c.witnesses_n=0;
		for(unsigned e=0;e<2;++e) {
			witness(&c,e,e,15,7,8,0,0,boundary(p,9));
			for(unsigned y=7;y<=8;++y) for(unsigned x=15;x<=16;++x)
				c.witnesses[c.witnesses_n++]=(witness_t){e,x,y,8,e};
		}
	}
	return c;
}
static void verify(case_t c,const frame_t *got)
{
	const frame_t *p=c.input; CHECK(!memcmp(got,p,offsetof(frame_t,masks))); size_t comparisons=0,margin_cases=0;
	for(unsigned e=0;e<2;++e) for(unsigned z=0;z<NZ;++z) for(unsigned y=0;y<NY;++y) for(unsigned x=0;x<NX;++x) {
		unsigned index=x+y*NX+z*NX*NY; const uint32_t *mask=got->masks[e][index];
		if(e>=p->counts[1]||!p->counts[0]) { CHECK(mask[0]==0&&mask[1]==0); continue; }
		for(unsigned i=p->counts[0];i<64;++i) CHECK(!(mask[i>>5]&(1u<<(i&31))));
		if(z==31) { for(unsigned i=0;i<p->counts[0];++i) CHECK(mask[i>>5]&(1u<<(i&31))); continue; }
		bounds_t b=bounds(p,e,x,y,z,1);
		for(unsigned i=0;i<p->counts[0];++i) {
			double d=distance(b,read3(p->lights[i].origin)), r=p->lights[i].radius;
			/* GPU pow/FMA vs independent double oracle: ambiguous AABB tangencies
			 * are excluded from exact rejection checks. Explicit witnesses below
			 * are never excluded, so no false-negative test is waived. */
			double epsilon=0.002+boundary(p,z+1)*1e-5; int hit=!!(mask[i>>5]&(1u<<(i&31)));
			if(fabs(d-r)<=epsilon) { ++margin_cases; continue; }
			if(hit!=(d<r)) { fprintf(stderr,"CLUSTER_MASK_MISMATCH case=%s eye=%u xyz=%u,%u,%u light=%u distance=%g radius=%g hit=%d\n",c.name,e,x,y,z,i,d,r,hit); exit(1); }
			++comparisons;
		}
	}
	for(unsigned i=0;i<c.witnesses_n;++i) {
		witness_t w=c.witnesses[i]; unsigned index=w.x+w.y*NX+w.z*NX*NY;
		if(!(got->masks[w.eye][index][w.light>>5]&(1u<<(w.light&31)))) {
			fprintf(stderr,"CLUSTER_FALSE_NEGATIVE case=%s eye=%u xyz=%u,%u,%u light=%u\n",c.name,w.eye,w.x,w.y,w.z,w.light); exit(1);
		}
	}
	printf("CLUSTER_VULKAN_CASE_PASSED %s witnesses=%u comparisons=%zu numerical_margin_cases=%zu\n",c.name,c.witnesses_n,comparisons,margin_cases);
}
static void run_pair(case_t cases[2])
{
	memset(f.upload.map,0xa5,(size_t)f.stride*2);
	for(unsigned slot=0;slot<2;++slot) {
		frame_t *p=(frame_t *)((unsigned char *)f.upload.map+slot*f.stride);
		memcpy(p,cases[slot].input,sizeof(*p)); memset(p->masks,0xcd,sizeof(p->masks));
	}
	cache(f.upload,0); VK(vkResetCommandBuffer(f.cb,0)); VK(vkResetFences(f.device,1,&f.fence));
	VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT}; VK(vkBeginCommandBuffer(f.cb,&bi));
	VkMemoryBarrier mb={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
	vkCmdPipelineBarrier(f.cb,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,NULL,0,NULL);
	vkCmdBindPipeline(f.cb,VK_PIPELINE_BIND_POINT_COMPUTE,f.pipeline);
	for(unsigned slot=0;slot<2;++slot) {
		vkCmdBindDescriptorSets(f.cb,VK_PIPELINE_BIND_POINT_COMPUTE,f.pipeline_layout,4,1,&f.sets[slot],0,NULL);
		vkCmdDispatch(f.cb,4,2,64);
	}
	mb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT; mb.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
	vkCmdPipelineBarrier(f.cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&mb,0,NULL,0,NULL);
	VkBufferCopy copy={.size=f.stride*2}; vkCmdCopyBuffer(f.cb,f.upload.h,f.readback.h,1,&copy);
	mb.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; mb.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(f.cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,NULL,0,NULL);
	VK(vkEndCommandBuffer(f.cb)); VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&f.cb};
	VK(vkQueueSubmit(f.queue,1,&si,f.fence)); VK(vkWaitForFences(f.device,1,&f.fence,VK_TRUE,UINT64_MAX)); cache(f.readback,1);
	for(unsigned slot=0;slot<2;++slot) {
		const unsigned char *out=(const unsigned char *)f.readback.map+slot*f.stride;
		verify(cases[slot],(const frame_t *)out);
		for(VkDeviceSize i=sizeof(frame_t);i<f.stride;++i) CHECK(out[i]==0xa5);
	}
}
int main(int argc,char **argv)
{
	if(argc!=2) { fprintf(stderr,"usage: %s cluster_lights.comp.spv\n",argv[0]); return 2; }
	init_device(); init_pipeline(argv[1]);
	case_t cases[6]={testcase("mono-zero",1,0,0,0),testcase("stereo-asymmetric-canted-64",2,64,1,0),
		testcase("mono-64",1,64,0,0),testcase("stereo-zero",2,0,1,0),
		testcase("mono-padding-isolated",1,1,0,1),testcase("stereo-central-boundary",2,2,1,2)};
	for(unsigned i=0;i<6;i+=2) run_pair(&cases[i]);
	for(unsigned i=0;i<6;++i) free(cases[i].input);
	destroy_buffer(f.readback); destroy_buffer(f.upload); vkDestroyPipeline(f.device,f.pipeline,NULL);
	vkDestroyPipelineLayout(f.device,f.pipeline_layout,NULL); vkDestroyDescriptorPool(f.device,f.descriptor_pool,NULL);
	vkDestroyDescriptorSetLayout(f.device,f.set_layout,NULL); vkDestroyDescriptorSetLayout(f.device,f.empty_layout,NULL);
	vkDestroyFence(f.device,f.fence,NULL); vkDestroyCommandPool(f.device,f.pool,NULL); vkDestroyDevice(f.device,NULL); vkDestroyInstance(f.instance,NULL);
	puts("CLUSTER_LIGHTING_VULKAN_PASSED production-SPIRV ABI two-aligned-slots masks padding finite first-slice unbounded-tail"); return 0;
}
