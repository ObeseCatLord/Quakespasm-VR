#ifdef __cplusplus
extern "C" {
#endif
#include "quakedef.h"
#ifdef __cplusplus
}
#endif
#include "openvr.h"
#include "vr_fbt_visual.h"

#include <math.h>
#include <string.h>

#define VR_FBT_VISUAL_MODEL_NAME_MAX 256
#define VR_FBT_VISUAL_PUCK_SIDES 12

typedef struct {
	char name[VR_FBT_VISUAL_MODEL_NAME_MAX];
	unsigned int device_index;
	vr::RenderModel_t *model;
	vr::RenderModel_TextureMap_t *texture_map;
	GLuint texture_id;
	qboolean model_failed;
	qboolean texture_failed;
} vr_fbt_visual_model_t;

typedef struct {
	GLint matrix_mode;
	GLint depth_func;
	GLint texture_env_mode;
	GLint current_program;
	GLint array_buffer;
	GLint element_array_buffer;
	GLint active_texture;
	GLint texture_binding;
	GLint blend_src;
	GLint blend_dst;
	GLboolean depth_write;
	GLboolean color_mask[4];
	GLfloat color[4];
	GLfloat line_width;
	GLfloat point_size;
	qboolean depth_test;
	qboolean cull_face;
	qboolean blend;
	qboolean alpha_test;
	qboolean texture_2d;
	qboolean fog;
	qboolean lighting;
	qboolean texture0_enabled;
	qboolean has_multitexture;
} vr_fbt_visual_gl_state_t;

static vr_fbt_visual_packet_t vr_fbt_visual_packets[VR_FBT_ROLE_COUNT];
static qboolean vr_fbt_visual_packet_valid[VR_FBT_ROLE_COUNT];
static vr_fbt_visual_model_t vr_fbt_visual_models[VR_FBT_ROLE_COUNT];
static uint64_t vr_fbt_visual_prepared_frame;
static qboolean vr_fbt_visual_have_frame;

static qboolean VR_FBT_VisualFinite3(const float value[3])
{
	return isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]);
}

static qboolean VR_FBT_VisualPacketSane(const vr_fbt_visual_packet_t *packet)
{
	unsigned int axis;

	if (!packet || packet->role < VR_FBT_ROLE_HIP ||
		packet->role >= VR_FBT_ROLE_COUNT ||
		(!packet->draw_tracker && !packet->draw_target))
		return false;
	if (packet->draw_tracker && !VR_FBT_VisualFinite3(packet->tracker_origin))
		return false;
	if (packet->draw_target && !VR_FBT_VisualFinite3(packet->target_origin))
		return false;
	for (axis = 0; axis < 3; ++axis) {
		if (packet->draw_tracker && !VR_FBT_VisualFinite3(packet->tracker_axis[axis]))
			return false;
		if (packet->draw_target && !VR_FBT_VisualFinite3(packet->target_axis[axis]))
			return false;
	}
	return true;
}

static void VR_FBT_VisualFreeModel(vr_fbt_visual_model_t *cache,
	vr::IVRRenderModels *render_models)
{
	if (!cache)
		return;
	if (cache->model && render_models)
		render_models->FreeRenderModel(cache->model);
	if (cache->texture_map && render_models)
		render_models->FreeTexture(cache->texture_map);
	if (cache->texture_id)
		glDeleteTextures(1, &cache->texture_id);
	memset(cache, 0, sizeof(*cache));
	cache->device_index = VR_FBT_VISUAL_INVALID_DEVICE;
}

static void VR_FBT_VisualSelectTexture(GLenum target)
{
	if (GL_SelectTextureFunc)
		GL_SelectTexture(target);
}

static void VR_FBT_VisualUploadTexture(vr_fbt_visual_model_t *cache)
{
	GLint active_texture = GL_TEXTURE0_ARB;
	GLint old_texture;

	if (!cache || !cache->texture_map || cache->texture_id ||
		cache->texture_map->format != vr::VRRenderModelTextureFormat_RGBA8_SRGB)
		return;

	if (GL_SelectTextureFunc && gl_mtexable)
		glGetIntegerv(GL_ACTIVE_TEXTURE_ARB, &active_texture);
	VR_FBT_VisualSelectTexture(GL_TEXTURE0_ARB);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
	glGenTextures(1, &cache->texture_id);
	glBindTexture(GL_TEXTURE_2D, cache->texture_id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cache->texture_map->unWidth,
		cache->texture_map->unHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
		cache->texture_map->rubTextureMapData);
	glBindTexture(GL_TEXTURE_2D, (GLuint)old_texture);
	if (GL_SelectTextureFunc && gl_mtexable)
		VR_FBT_VisualSelectTexture((GLenum)active_texture);
	GL_ClearBindings();
}

static void VR_FBT_VisualPrepareModel(vr_fbt_role_t role,
	const vr_fbt_visual_packet_t *packet, vr::IVRSystem *system,
	vr::IVRRenderModels *render_models)
{
	vr_fbt_visual_model_t *cache;
	vr::ETrackedPropertyError property_error = vr::TrackedProp_Success;
	char name[VR_FBT_VISUAL_MODEL_NAME_MAX];

	if (!packet || !packet->draw_tracker || !system || !render_models ||
		packet->device_index == VR_FBT_VISUAL_INVALID_DEVICE ||
		packet->device_index >= vr::k_unMaxTrackedDeviceCount)
		return;

	name[0] = 0;
	system->GetStringTrackedDeviceProperty(packet->device_index,
		vr::Prop_RenderModelName_String, name, sizeof(name), &property_error);
	if (property_error != vr::TrackedProp_Success || !name[0])
		return;

	cache = &vr_fbt_visual_models[role];
	if (cache->device_index != packet->device_index || strcmp(cache->name, name)) {
		VR_FBT_VisualFreeModel(cache, render_models);
		cache->device_index = packet->device_index;
		q_strlcpy(cache->name, name, sizeof(cache->name));
	}

	if (!cache->model && !cache->model_failed) {
		vr::RenderModel_t *model = NULL;
		vr::EVRRenderModelError error =
			render_models->LoadRenderModel_Async(cache->name, &model);
		if (error == vr::VRRenderModelError_Loading)
			return;
		if (error != vr::VRRenderModelError_None) {
			cache->model_failed = true;
			return;
		}
		cache->model = model;
	}

	if (cache->model && cache->model->diffuseTextureId != vr::INVALID_TEXTURE_ID &&
		!cache->texture_map && !cache->texture_failed) {
		vr::RenderModel_TextureMap_t *texture_map = NULL;
		vr::EVRRenderModelError error = render_models->LoadTexture_Async(
			cache->model->diffuseTextureId, &texture_map);
		if (error == vr::VRRenderModelError_Loading)
			return;
		if (error != vr::VRRenderModelError_None) {
			cache->texture_failed = true;
			return;
		}
		cache->texture_map = texture_map;
	}
	VR_FBT_VisualUploadTexture(cache);
}

void VR_FBT_VisualPrepare(const vr_fbt_visual_prepare_t *prepare)
{
	unsigned int i;
	vr::IVRSystem *system;
	vr::IVRRenderModels *render_models;

	if (!prepare)
		return;
	/* Preparation is deliberately idempotent for a stereo host frame. */
	if (vr_fbt_visual_have_frame &&
		vr_fbt_visual_prepared_frame == prepare->host_frame_id)
		return;
	vr_fbt_visual_have_frame = true;
	vr_fbt_visual_prepared_frame = prepare->host_frame_id;
	memset(vr_fbt_visual_packet_valid, 0, sizeof(vr_fbt_visual_packet_valid));

	for (i = 0; prepare->packets && i < prepare->packet_count; ++i) {
		const vr_fbt_visual_packet_t *packet = &prepare->packets[i];
		if (!VR_FBT_VisualPacketSane(packet))
			continue;
		vr_fbt_visual_packets[packet->role] = *packet;
		vr_fbt_visual_packet_valid[packet->role] = true;
	}

	system = (vr::IVRSystem *)prepare->openvr_system;
	render_models = (vr::IVRRenderModels *)prepare->openvr_render_models;
	for (i = 0; i < VR_FBT_ROLE_COUNT; ++i) {
		if (vr_fbt_visual_packet_valid[i] &&
			vr_fbt_visual_packets[i].draw_tracker) {
			/* Do not draw a prior device's model if property lookup is unavailable. */
			if (vr_fbt_visual_models[i].device_index !=
				vr_fbt_visual_packets[i].device_index)
				VR_FBT_VisualFreeModel(&vr_fbt_visual_models[i], render_models);
			VR_FBT_VisualPrepareModel((vr_fbt_role_t)i,
				&vr_fbt_visual_packets[i], system, render_models);
		} else if (vr_fbt_visual_models[i].device_index !=
			VR_FBT_VISUAL_INVALID_DEVICE) {
			VR_FBT_VisualFreeModel(&vr_fbt_visual_models[i], render_models);
		}
	}
}

void VR_FBT_VisualClearPackets(void)
{
	memset(vr_fbt_visual_packet_valid, 0, sizeof(vr_fbt_visual_packet_valid));
	/* A later enable in the same host frame must be able to prepare anew. */
	vr_fbt_visual_have_frame = false;
}

static void VR_FBT_VisualPoint(const float origin[3], const float axis[3][3],
	float x, float y, float z, vec3_t out)
{
	out[0] = origin[0] + axis[0][0] * x + axis[1][0] * y + axis[2][0] * z;
	out[1] = origin[1] + axis[0][1] * x + axis[1][1] * y + axis[2][1] * z;
	out[2] = origin[2] + axis[0][2] * x + axis[1][2] * y + axis[2][2] * z;
}

static void VR_FBT_VisualDrawAxes(const float origin[3], const float axis[3][3],
	float length, float alpha)
{
	vec3_t start, end;
	static const float colors[3][3] = {
		{1.0f, 0.2f, 0.2f}, {0.2f, 1.0f, 0.2f}, {0.2f, 0.4f, 1.0f}
	};
	unsigned int i;

	VR_FBT_VisualPoint(origin, axis, 0.0f, 0.0f, 0.0f, start);
	glBegin(GL_LINES);
	for (i = 0; i < 3; ++i) {
		VR_FBT_VisualPoint(origin, axis, i == 0 ? length : 0.0f,
			i == 1 ? length : 0.0f, i == 2 ? length : 0.0f, end);
		glColor4f(colors[i][0], colors[i][1], colors[i][2], alpha);
		glVertex3fv(start);
		glVertex3fv(end);
	}
	glEnd();
}

static void VR_FBT_VisualDrawPuck(const vr_fbt_visual_packet_t *packet)
{
	unsigned int i;
	vec3_t point;
	const float radius = 0.045f;
	const float half_height = 0.012f;

	glColor4f(0.85f, 0.85f, 0.9f, 0.80f);
	glBegin(GL_QUAD_STRIP);
	for (i = 0; i <= VR_FBT_VISUAL_PUCK_SIDES; ++i) {
		const float angle = (float)i * (2.0f * (float)M_PI /
			(float)VR_FBT_VISUAL_PUCK_SIDES);
		const float x = cosf(angle) * radius;
		const float y = sinf(angle) * radius;
		VR_FBT_VisualPoint(packet->tracker_origin, packet->tracker_axis,
			x, y, -half_height, point);
		glVertex3fv(point);
		VR_FBT_VisualPoint(packet->tracker_origin, packet->tracker_axis,
			x, y, half_height, point);
		glVertex3fv(point);
	}
	glEnd();

	glBegin(GL_LINE_LOOP);
	for (i = 0; i < VR_FBT_VISUAL_PUCK_SIDES; ++i) {
		const float angle = (float)i * (2.0f * (float)M_PI /
			(float)VR_FBT_VISUAL_PUCK_SIDES);
		VR_FBT_VisualPoint(packet->tracker_origin, packet->tracker_axis,
			cosf(angle) * radius, sinf(angle) * radius, half_height, point);
		glVertex3fv(point);
	}
	glEnd();
	VR_FBT_VisualDrawAxes(packet->tracker_origin, packet->tracker_axis, 0.12f,
		0.95f);
}

static void VR_FBT_VisualDrawModel(const vr_fbt_visual_packet_t *packet,
	const vr_fbt_visual_model_t *cache)
{
	const vr::RenderModel_t *model;
	qboolean textured;
	uint32_t i;

	if (!cache || !cache->model) {
		VR_FBT_VisualDrawPuck(packet);
		return;
	}
	model = cache->model;
	textured = cache->texture_id != 0;
	if (textured) {
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, cache->texture_id);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	} else {
		glDisable(GL_TEXTURE_2D);
	}
	glColor4f(1.0f, 1.0f, 1.0f, 0.85f);
	glBegin(GL_TRIANGLES);
	for (i = 0; i < model->unTriangleCount * 3; ++i) {
		const vr::RenderModel_Vertex_t *vertex =
			&model->rVertexData[model->rIndexData[i]];
		vec3_t point;
		VR_FBT_VisualPoint(packet->tracker_origin, packet->tracker_axis,
			vertex->vPosition.v[0], vertex->vPosition.v[1],
			vertex->vPosition.v[2], point);
		if (textured)
			glTexCoord2f(vertex->rfTextureCoord[0], vertex->rfTextureCoord[1]);
		glVertex3fv(point);
	}
	glEnd();
	VR_FBT_VisualDrawAxes(packet->tracker_origin, packet->tracker_axis, 0.10f,
		0.90f);
}

static void VR_FBT_VisualSaveGLState(vr_fbt_visual_gl_state_t *state)
{
	glGetIntegerv(GL_MATRIX_MODE, &state->matrix_mode);
	glGetIntegerv(GL_DEPTH_FUNC, &state->depth_func);
	state->has_multitexture = GL_SelectTextureFunc && gl_mtexable;
	state->active_texture = GL_TEXTURE0_ARB;
	if (state->has_multitexture)
		glGetIntegerv(GL_ACTIVE_TEXTURE_ARB, &state->active_texture);
	VR_FBT_VisualSelectTexture(GL_TEXTURE0_ARB);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->texture_binding);
	glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &state->texture_env_mode);
	state->texture0_enabled = glIsEnabled(GL_TEXTURE_2D);
	if (state->has_multitexture)
		VR_FBT_VisualSelectTexture((GLenum)state->active_texture);
	glGetIntegerv(GL_BLEND_SRC, &state->blend_src);
	glGetIntegerv(GL_BLEND_DST, &state->blend_dst);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &state->depth_write);
	glGetBooleanv(GL_COLOR_WRITEMASK, state->color_mask);
	glGetFloatv(GL_CURRENT_COLOR, state->color);
	glGetFloatv(GL_LINE_WIDTH, &state->line_width);
	glGetFloatv(GL_POINT_SIZE, &state->point_size);
	state->depth_test = glIsEnabled(GL_DEPTH_TEST);
	state->cull_face = glIsEnabled(GL_CULL_FACE);
	state->blend = glIsEnabled(GL_BLEND);
	state->alpha_test = glIsEnabled(GL_ALPHA_TEST);
	state->texture_2d = glIsEnabled(GL_TEXTURE_2D);
	state->fog = glIsEnabled(GL_FOG);
	state->lighting = glIsEnabled(GL_LIGHTING);
	state->current_program = 0;
	if (GL_UseProgramFunc)
		glGetIntegerv(GL_CURRENT_PROGRAM, &state->current_program);
	state->array_buffer = state->element_array_buffer = 0;
	if (gl_vbo_able) {
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state->array_buffer);
		glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,
			&state->element_array_buffer);
	}
}

static void VR_FBT_VisualRestoreEnabled(GLenum capability, qboolean enabled)
{
	if (enabled)
		glEnable(capability);
	else
		glDisable(capability);
}

static void VR_FBT_VisualRestoreGLState(const vr_fbt_visual_gl_state_t *state)
{
	glDepthFunc(state->depth_func);
	glDepthMask(state->depth_write);
	glColorMask(state->color_mask[0], state->color_mask[1],
		state->color_mask[2], state->color_mask[3]);
	glBlendFunc(state->blend_src, state->blend_dst);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, state->texture_env_mode);
	glColor4fv(state->color);
	glLineWidth(state->line_width);
	glPointSize(state->point_size);
	VR_FBT_VisualRestoreEnabled(GL_DEPTH_TEST, state->depth_test);
	VR_FBT_VisualRestoreEnabled(GL_CULL_FACE, state->cull_face);
	VR_FBT_VisualRestoreEnabled(GL_BLEND, state->blend);
	VR_FBT_VisualRestoreEnabled(GL_ALPHA_TEST, state->alpha_test);
	VR_FBT_VisualRestoreEnabled(GL_FOG, state->fog);
	VR_FBT_VisualRestoreEnabled(GL_LIGHTING, state->lighting);
	VR_FBT_VisualSelectTexture(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_2D, (GLuint)state->texture_binding);
	VR_FBT_VisualRestoreEnabled(GL_TEXTURE_2D, state->texture0_enabled);
	if (state->has_multitexture)
		VR_FBT_VisualSelectTexture((GLenum)state->active_texture);
	VR_FBT_VisualRestoreEnabled(GL_TEXTURE_2D, state->texture_2d);
	if (gl_vbo_able) {
		GL_BindBuffer(GL_ARRAY_BUFFER, (GLuint)state->array_buffer);
		GL_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)state->element_array_buffer);
	}
	if (GL_UseProgramFunc)
		GL_UseProgram((GLuint)state->current_program);
	glMatrixMode(state->matrix_mode);
	GL_ClearBindings();
}

void VR_FBT_VisualDraw(void)
{
	vr_fbt_visual_gl_state_t state;
	unsigned int i;
	qboolean any = false;

	for (i = 0; i < VR_FBT_ROLE_COUNT; ++i)
		if (vr_fbt_visual_packet_valid[i])
			any = true;
	if (!any)
		return;

	VR_FBT_VisualSaveGLState(&state);
	if (GL_UseProgramFunc)
		GL_UseProgram(0);
	if (gl_vbo_able) {
		GL_BindBuffer(GL_ARRAY_BUFFER, 0);
		GL_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}
	VR_FBT_VisualSelectTexture(GL_TEXTURE0_ARB);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_FOG);
	glDisable(GL_LIGHTING);
	glLineWidth(2.0f);

	for (i = 0; i < VR_FBT_ROLE_COUNT; ++i) {
		const vr_fbt_visual_packet_t *packet;
		if (!vr_fbt_visual_packet_valid[i])
			continue;
		packet = &vr_fbt_visual_packets[i];
		if (packet->draw_tracker)
			VR_FBT_VisualDrawModel(packet, &vr_fbt_visual_models[i]);
		if (packet->draw_target) {
			VR_FBT_VisualDrawAxes(packet->target_origin, packet->target_axis,
				0.14f, 0.98f);
			glPointSize(7.0f);
			glColor4f(1.0f, 0.9f, 0.1f, 0.98f);
			glBegin(GL_POINTS);
			glVertex3fv(packet->target_origin);
			glEnd();
		}
		if (packet->draw_tracker && packet->draw_target &&
			packet->draw_offset_line) {
			glColor4f(1.0f, 0.85f, 0.1f, 0.75f);
			glBegin(GL_LINES);
			glVertex3fv(packet->tracker_origin);
			glVertex3fv(packet->target_origin);
			glEnd();
		}
	}
	VR_FBT_VisualRestoreGLState(&state);
}

void VR_FBT_VisualShutdown(const void *openvr_render_models)
{
	vr::IVRRenderModels *render_models =
		(vr::IVRRenderModels *)openvr_render_models;
	unsigned int i;

	for (i = 0; i < VR_FBT_ROLE_COUNT; ++i)
		VR_FBT_VisualFreeModel(&vr_fbt_visual_models[i], render_models);
	VR_FBT_VisualClearPackets();
	GL_ClearBindings();
}
