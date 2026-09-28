/* Capture the production alias draw boundary. Vulkan dispatch and the uniform
 * allocator are test boundaries; this is not a framebuffer or timing test. */
#include "../Quake/r_alias.c"
#include <assert.h>

vulkanglobals_t vulkan_globals;
qboolean r_fullbright_cheatsafe, r_lightmap_cheatsafe;
cvar_t r_fullbright;

#define CAPTURE_COUNT 64
static byte arena[32768];
static int arena_used, allocations, commands, draws, record_count;
static uint32_t draw_sizes[CAPTURE_COUNT];

typedef struct
{
	VkPipeline pipeline;
	VkDescriptorSet sets[4], stereo_set;
	uint32_t set_count, uniform_offset, stereo_offset;
	VkBuffer vertex_buffer, index_buffer;
	VkDeviceSize vertex_offsets[3];
	uint32_t vertex_count;
} bound_state_t;

typedef struct
{
	aliasubo_t instance;
	bound_state_t state;
	uint32_t indices;
} captured_instance_t;

static bound_state_t bound[2];
static captured_instance_t records[CAPTURE_COUNT];

const r_vrik_prepared_palette_t *R_VRIKRenderLookup (const entity_t *e) { return NULL; }
qboolean Mod_IsAdmittedAvatarModel (const qmodel_t *m) { return false; }
VkPipeline R_ResolvePipelineInstance (const cb_context_t *c, vulkan_pipeline_t p) { return p.handle; }

static bound_state_t *state_for (VkCommandBuffer command)
{
	uintptr_t index = (uintptr_t)command - 1;
	assert (index < countof (bound));
	return &bound[index];
}

byte *R_UniformAllocate (int size, VkBuffer *buffer, uint32_t *offset, VkDescriptorSet *set)
{
	assert (size > 0 && size <= MAX_UNIFORM_ALLOC && arena_used + MAX_UNIFORM_ALLOC <= sizeof (arena));
	*buffer = (VkBuffer)1;
	*offset = arena_used;
	*set = (VkDescriptorSet)2;
	byte *data = arena + arena_used;
	arena_used += (size + 255) & ~255;
	allocations++;
	return data;
}

static VKAPI_ATTR void VKAPI_CALL bind_pipeline (VkCommandBuffer c, VkPipelineBindPoint b, VkPipeline p)
{
	state_for (c)->pipeline = p;
	commands++;
}
static VKAPI_ATTR void VKAPI_CALL push (VkCommandBuffer c, VkPipelineLayout l, VkShaderStageFlags s,
	uint32_t o, uint32_t z, const void *p) { commands++; }
static VKAPI_ATTR void VKAPI_CALL descriptors (VkCommandBuffer c, VkPipelineBindPoint b, VkPipelineLayout l,
	uint32_t first, uint32_t count, const VkDescriptorSet *sets, uint32_t offsets, const uint32_t *offset)
{
	bound_state_t *state = state_for (c);
	assert (offsets == 1);
	if (first == 5)
	{
		assert (count == 1);
		state->stereo_set = *sets;
		state->stereo_offset = *offset;
	}
	else
	{
		assert (first == 0 && (count == 3 || count == 4));
		memcpy (state->sets, sets, count * sizeof (*sets));
		state->set_count = count;
		state->uniform_offset = *offset;
	}
	commands++;
}
static VKAPI_ATTR void VKAPI_CALL vertices (VkCommandBuffer c, uint32_t first, uint32_t count,
	const VkBuffer *buffers, const VkDeviceSize *offsets)
{
	bound_state_t *state = state_for (c);
	assert (first == 0 && (count == 1 || count == 3));
	state->vertex_count = count;
	state->vertex_buffer = buffers[0];
	memcpy (state->vertex_offsets, offsets, count * sizeof (*offsets));
	commands++;
}
static VKAPI_ATTR void VKAPI_CALL indices (VkCommandBuffer c, VkBuffer buffer, VkDeviceSize offset, VkIndexType type)
{
	assert (type == VK_INDEX_TYPE_UINT16 && offset == 0);
	state_for (c)->index_buffer = buffer;
	commands++;
}
static VKAPI_ATTR void VKAPI_CALL draw (VkCommandBuffer c, uint32_t indices, uint32_t instances,
	uint32_t first, int32_t vertex, uint32_t first_instance)
{
	bound_state_t *state = state_for (c);
	assert (instances > 0 && instances <= ALIAS_BATCH_MAX_INSTANCES);
	assert (first == 0 && vertex == 0 && first_instance == 0);
	assert (draws < CAPTURE_COUNT && record_count + instances <= CAPTURE_COUNT);
	draw_sizes[draws++] = instances;
	for (uint32_t i = 0; i < instances; ++i)
	{
		captured_instance_t *record = &records[record_count++];
		memset (record, 0, sizeof (*record));
		if (state->vertex_count == 3)
			memcpy (&record->instance, arena + state->uniform_offset + i * sizeof (aliasubo_t), sizeof (aliasubo_t));
		else
		{
			// MD5 remains a single draw with its original palette UBO layout.
			assert (instances == 1);
			memcpy (&record->instance, arena + state->uniform_offset, offsetof (aliasubo_t, padding));
		}
		record->state = *state;
		record->indices = indices;
	}
	commands++;
}

static cb_context_t context (int index)
{
	cb_context_t cbx = {0};
	cbx.cb = (VkCommandBuffer)(uintptr_t)(index + 1);
	cbx.subpass_type = SUBPASS_MAIN;
	return cbx;
}
static void reset_capture (void)
{
	arena_used = allocations = commands = draws = record_count = 0;
	memset (bound, 0, sizeof (bound));
	r_fullbright_cheatsafe = r_lightmap_cheatsafe = false;
	vulkan_globals.stereo_active = false;
}
static void submit (cb_context_t *cbx, aliashdr_t *hdr, gltexture_t *texture, gltexture_t *fullbright,
	int id, int pose2, float alpha, qboolean holey, int showtris, qboolean opposite, qboolean unlit, int overlay)
{
	lerpdata_t lerp = {0};
	lerp.pose2 = pose2;
	lerp.blend = id * .01f;
	vec3_t shade = {id * .1f, id * .2f, id * .3f};
	vec3_t light = {id * .4f, id * .5f, id * .6f};
	entity_t entity = {0};
	float matrix[16];
	IdentityMatrix (matrix);
	matrix[12] = id;
	GL_DrawAliasFrame (cbx, &entity, hdr, hdr, lerp, texture, fullbright,
		matrix, alpha, holey, shade, light, showtris, opposite, unlit, false, overlay);
}
static void opaque (cb_context_t *cbx, aliashdr_t *hdr, gltexture_t *texture, int id)
{
	submit (cbx, hdr, texture, NULL, id, 1, 1, false, 0, false, false, -1);
}
static void compare_instance (const captured_instance_t *a, const captured_instance_t *b)
{
	assert (!memcmp (&a->instance, &b->instance, sizeof (a->instance)));
	assert (a->state.pipeline == b->state.pipeline && a->indices == b->indices);
	assert (a->state.vertex_buffer == b->state.vertex_buffer && a->state.index_buffer == b->state.index_buffer);
	assert (!memcmp (a->state.vertex_offsets, b->state.vertex_offsets, sizeof (a->state.vertex_offsets)));
	assert (a->state.sets[0] == b->state.sets[0] && a->state.sets[1] == b->state.sets[1]);
}
int main (void)
{
	vulkan_globals.vk_cmd_bind_pipeline = bind_pipeline;
	vulkan_globals.vk_cmd_push_constants = push;
	vulkan_globals.vk_cmd_bind_descriptor_sets = descriptors;
	vulkan_globals.vk_cmd_bind_vertex_buffers = vertices;
	vulkan_globals.vk_cmd_bind_index_buffer = indices;
	vulkan_globals.vk_cmd_draw_indexed = draw;
	for (int i = 0; i < MODEL_PIPELINE_COUNT; ++i)
	{
		vulkan_pipeline_t pipeline = {0};
		pipeline.handle = (VkPipeline)(uintptr_t)(10 + i);
		pipeline.layout.handle = (VkPipelineLayout)3;
		vulkan_globals.alias_pipelines[0][i] = pipeline;
		vulkan_globals.md5_pipelines[0][i] = pipeline;
		if (i < MODEL_PIPELINE_SHOWTRIS)
		{
			pipeline.handle = (VkPipeline)(uintptr_t)(20 + i);
			vulkan_globals.alias_opposite_front_face_pipelines[0][i] = pipeline;
		}
	}
	vulkan_globals.alias_coop_overlay_pipelines[0][0] = vulkan_globals.alias_pipelines[0][0];
	aliashdr_t hdr = {0};
	hdr.poseverttype = PV_QUAKE1;
	hdr.numindexes = 3;
	hdr.numverts_vbo = 3;
	hdr.vertex_buffer = (VkBuffer)4;
	hdr.index_buffer = (VkBuffer)5;
	hdr.joints_set = (VkDescriptorSet)6;
	hdr.numjoints = 1;
	gltexture_t texture = {0}, other = {0};
	texture.descriptor_set = (VkDescriptorSet)7;
	other.descriptor_set = (VkDescriptorSet)8;
	r_alias_batch_t batch;

	reset_capture ();
	cb_context_t cbx = context (0);
	opaque (&cbx, &hdr, &texture, 1);
	opaque (&cbx, &hdr, &texture, 2);
	assert (draws == 2 && allocations == 2);
	captured_instance_t baseline[2] = {records[0], records[1]};
	reset_capture ();
	cbx = context (0);
	R_AliasBatchBegin (&cbx, &batch);
	opaque (&cbx, &hdr, &texture, 1);
	opaque (&cbx, &hdr, &texture, 2);
	assert (!draws && !commands && !allocations && batch.count == 2);
	R_AliasBatchEnd (&cbx);
	assert (draws == 1 && allocations == 1 && draw_sizes[0] == 2 && !cbx.alias_batch);
	for (int i = 0; i < 2; ++i)
		compare_instance (&baseline[i], &records[i]);

	reset_capture ();
	cbx = context (0);
	R_AliasBatchBegin (&cbx, &batch);
	for (int i = 1; i <= ALIAS_BATCH_MAX_INSTANCES + 1; ++i)
		opaque (&cbx, &hdr, &texture, i);
	assert (draws == 1 && draw_sizes[0] == 16 && batch.count == 1);
	R_AliasBatchEnd (&cbx);
	assert (draws == 2 && draw_sizes[1] == 1 && record_count == 17);
	for (int i = 0; i < 17; ++i)
		assert (records[i].instance.model_matrix[12] == i + 1);

	// Every changed bound stream/material/pipeline forces an ordered flush.
	for (int changed = 0; changed < 10; ++changed)
	{
		reset_capture ();
		cbx = context (0);
		R_AliasBatchBegin (&cbx, &batch);
		opaque (&cbx, &hdr, &texture, 1);
		aliashdr_t second = hdr;
		gltexture_t *skin = &texture, *fb = NULL;
		int pose2 = 1;
		qboolean opposite = false, holey = false;
		switch (changed)
		{
		case 0: skin = &other; break; // includes resolved player colors
		case 1: fb = &other; break;
		case 2: pose2 = 2; break;
		case 3: opposite = true; break;
		case 4: holey = true; break;
		case 5: second.vertex_buffer = (VkBuffer)14; break;
		case 6: second.index_buffer = (VkBuffer)15; break;
		case 7: second.numindexes = 6; break;
		case 8: second.vbostofs = 8; break;
		case 9: r_fullbright_cheatsafe = true; break;
		}
		submit (&cbx, &second, skin, fb, 2, pose2, 1, holey, 0, opposite, false, -1);
		assert (draws == 1 && records[0].instance.model_matrix[12] == 1);
		R_AliasBatchEnd (&cbx);
		assert (draws == 2 && records[1].instance.model_matrix[12] == 2);
	}

	// Immediate fallbacks must flush before their binds or push constants.
	for (int excluded = 0; excluded < 6; ++excluded)
	{
		reset_capture ();
		cbx = context (0);
		R_AliasBatchBegin (&cbx, &batch);
		opaque (&cbx, &hdr, &texture, 1);
		aliashdr_t immediate = hdr;
		gltexture_t alpha_texture = texture;
		if (excluded == 0)
			alpha_texture.flags |= TEXPREF_ALPHAPIXELS;
		if (excluded == 5)
			immediate.poseverttype = PV_MD5;
		submit (&cbx, &immediate, &alpha_texture, NULL, 2, 1,
			excluded == 1 ? .5f : 1, false, excluded == 2 ? 2 : 0,
			false, excluded == 3, excluded == 4 ? 0 : -1);
		assert (draws == 2 && !batch.count);
		opaque (&cbx, &hdr, &texture, 3);
		R_AliasBatchEnd (&cbx);
		assert (draws == 3);
		for (int i = 0; i < 3; ++i)
			assert (records[i].instance.model_matrix[12] == i + 1);
	}

	reset_capture ();
	cbx = context (0);
	R_AliasBatchBegin (&cbx, &batch);
	opaque (&cbx, &hdr, &texture, 1);
	R_AliasBatchFlush (&cbx); // brush/sprite boundary in the entity owner
	assert (draws == 1 && !batch.count);
	opaque (&cbx, &hdr, &texture, 2);
	opaque (&cbx, &hdr, &texture, 3);
	R_AliasBatchEnd (&cbx);
	assert (draws == 2 && draw_sizes[0] == 1 && draw_sizes[1] == 2);

	// Distinct task contexts may interleave without sharing a pending batch.
	reset_capture ();
	cbx = context (0);
	cb_context_t other_context = context (1);
	r_alias_batch_t other_batch;
	R_AliasBatchBegin (&cbx, &batch);
	R_AliasBatchBegin (&other_context, &other_batch);
	opaque (&cbx, &hdr, &texture, 1);
	opaque (&other_context, &hdr, &texture, 3);
	opaque (&cbx, &hdr, &texture, 2);
	R_AliasBatchEnd (&other_context);
	assert (draws == 1 && batch.count == 2 && !other_context.alias_batch);
	R_AliasBatchEnd (&cbx);
	assert (draws == 2 && draw_sizes[0] == 1 && draw_sizes[1] == 2);
	assert (records[0].instance.model_matrix[12] == 3);
	compare_instance (&baseline[0], &records[1]);
	compare_instance (&baseline[1], &records[2]);

	reset_capture ();
	vulkan_globals.stereo_active = true;
	vulkan_globals.stereo_descriptor_set = (VkDescriptorSet)30;
	vulkan_globals.stereo_uniform_offset = 256;
	cbx = context (0);
	R_AliasBatchBegin (&cbx, &batch);
	hdr.poseverttype = PV_QUAKE3;
	opaque (&cbx, &hdr, &texture, 1);
	opaque (&cbx, &hdr, &texture, 2);
	R_AliasBatchEnd (&cbx);
	assert (draws == 1 && draw_sizes[0] == 2 && records[1].instance.flags == 4);
	assert (records[1].state.stereo_set == (VkDescriptorSet)30 && records[1].state.stereo_offset == 256);
	puts ("ALIAS_BATCH_COMMAND_CAPTURE_PASSED");
	return 0;
}
