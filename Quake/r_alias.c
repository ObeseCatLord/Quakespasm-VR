/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2016 Axel Gneiting

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// r_alias.c -- alias model rendering

#include "quakedef.h"
#include "vr_input.h"
#include "vr_weapon_calibration.h"
#include "r_vrik.h"
#include "r_vrik_render.h"
#include <float.h>

extern cvar_t r_drawflat, gl_fullbrights, r_lerpmodels, r_lerpmove, r_showtris; // johnfitz
extern cvar_t r_lerpturn;
extern cvar_t cl_gun_fovscale, cl_gun_x, cl_gun_y, cl_gun_z;
extern cvar_t vr_world_scale;
extern qboolean V_UseTrackedView (void);

// up to 16 color translated skins
gltexture_t *playertextures[MAX_SCOREBOARD]; // johnfitz -- changed to an array of pointers

#define NUMVERTEXNORMALS 162

float r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16

typedef struct
{
	float	 model_matrix[16];
	float	 shade_vector[3];
	float	 blend_factor;
	float	 light_color[3];
	float	 entalpha;
	uint32_t flags;
} aliasubo_t;

typedef struct
{
	float	 model_matrix[16];
	float	 shade_vector[3];
	float	 blend_factor;
	float	 light_color[3];
	float	 entalpha;
	uint32_t flags;
	uint32_t joints_offsets[2];
} md5ubo_t;

/*
=============
GLARB_GetXYZOffset

Returns the offset of the first vertex's meshxyz_t.xyz in the vbo for the given
model and pose.
=============
*/
static VkDeviceSize GLARB_GetXYZOffset (entity_t *e, aliashdr_t *hdr, int pose)
{
	const int xyzoffs = offsetof (meshxyz_t, xyz);
	return hdr->numverts_vbo * pose * sizeof (meshxyz_t) + xyzoffs;
}

static qboolean R_IsVRViewmodel (entity_t *e)
{
	return e == &cl.viewent && V_UseTrackedView ();
}

static qboolean R_AliasMatrixIsFinite (const float model_matrix[16])
{
	for (int i = 0; i < 16; ++i)
		if (!isfinite (model_matrix[i]))
			return false;
	return true;
}

/*
=============
GL_DrawAliasFrame -- ericw

Optimized alias model drawing codepath. This makes 1 draw call,
no vertex data is uploaded (it's already in the r_meshvbo and r_meshindexesvbo
static VBOs), and lerping and lighting is done in the vertex shader.

Supports optional fullbright pixels.

Based on code by MH from RMQEngine
=============
*/
static void GL_DrawAliasFrame (
	cb_context_t *cbx, entity_t *e, aliashdr_t *paliashdr, const aliashdr_t *selected_geometry, lerpdata_t lerpdata, gltexture_t *tx, gltexture_t *fb,
	float model_matrix[16], float entity_alpha,
	qboolean alphatest, vec3_t shadevector, vec3_t lightcolor, int showtris, qboolean opposite_front_face, qboolean force_unlit,
	qboolean allow_tracked_palette)
{
	vulkan_pipeline_t pipeline;
	const r_vrik_prepared_palette_t *tracked_palette = NULL;

	// only enable alpha management if entity have alpha or the surface texture has effective
	// non-opaque pixels:
	const bool has_alpha = (entity_alpha < 1.0f) || (tx->flags & TEXPREF_ALPHAPIXELS);

	int pipeline_index;
	if (showtris == 0)
		pipeline_index = (has_alpha ? MODEL_PIPELINE_ALPHA_BLEND_BIT : 0) | (alphatest ? MODEL_PIPELINE_ALPHA_TEST_BIT : 0);
	else
		pipeline_index = (showtris >= 2) ? MODEL_PIPELINE_SHOWTRIS_DEPTH_TEST : MODEL_PIPELINE_SHOWTRIS;

	const qboolean oit_pass = cbx->subpass_type == SUBPASS_WBOIT || cbx->subpass_type == SUBPASS_MBOIT_MOMENTS || cbx->subpass_type == SUBPASS_MBOIT_COMPOSITE;
	if (oit_pass && (showtris != 0 || !has_alpha))
		return;
	const qboolean use_opposite_front_face = opposite_front_face && cbx->subpass_type == SUBPASS_MAIN && pipeline_index < MODEL_PIPELINE_SHOWTRIS;

	if (paliashdr->poseverttype == PV_MD5 || paliashdr->poseverttype == PV_MD5_8)
	{
		vulkan_pipeline_t (*pipelines)[MODEL_PIPELINE_COUNT] =
			(paliashdr->poseverttype == PV_MD5_8) ? vulkan_globals.md5_8_pipelines : vulkan_globals.md5_pipelines;
		vulkan_pipeline_t (*opposite_front_face_pipelines)[MODEL_PIPELINE_SHOWTRIS] =
			(paliashdr->poseverttype == PV_MD5_8) ? vulkan_globals.md5_8_opposite_front_face_pipelines : vulkan_globals.md5_opposite_front_face_pipelines;
		vulkan_pipeline_t *wboit_pipelines = (paliashdr->poseverttype == PV_MD5_8) ? vulkan_globals.md5_8_wboit_pipelines : vulkan_globals.md5_wboit_pipelines;
		vulkan_pipeline_t *mboit_moment_pipelines =
			(paliashdr->poseverttype == PV_MD5_8) ? vulkan_globals.md5_8_mboit_moment_pipelines : vulkan_globals.md5_mboit_moment_pipelines;
		vulkan_pipeline_t *mboit_composite_pipelines =
			(paliashdr->poseverttype == PV_MD5_8) ? vulkan_globals.md5_8_mboit_composite_pipelines : vulkan_globals.md5_mboit_composite_pipelines;

		if (use_opposite_front_face)
			pipeline = opposite_front_face_pipelines[cbx->pipeline_variant][pipeline_index];
		else
			pipeline = R_PipelineForSubpassType (
				cbx->subpass_type, pipelines[cbx->pipeline_variant][pipeline_index], wboit_pipelines[pipeline_index], mboit_moment_pipelines[pipeline_index],
				mboit_composite_pipelines[pipeline_index]);
	}
	else if (use_opposite_front_face)
		pipeline = vulkan_globals.alias_opposite_front_face_pipelines[cbx->pipeline_variant][pipeline_index];
	else
		pipeline = R_PipelineForSubpassType (
			cbx->subpass_type, vulkan_globals.alias_pipelines[cbx->pipeline_variant][pipeline_index], vulkan_globals.alias_wboit_pipelines[pipeline_index],
			vulkan_globals.alias_mboit_moment_pipelines[pipeline_index], vulkan_globals.alias_mboit_composite_pipelines[pipeline_index]);

	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	float blend;

	if (lerpdata.pose1 != lerpdata.pose2)
		blend = lerpdata.blend;
	else // poses the same means either 1. the entity has paused its animation, or 2. r_lerpmodels is disabled
		blend = 0;

	switch (paliashdr->poseverttype)
	{
	case PV_QUAKE1:
	case PV_QUAKE3:
	{
		VkBuffer		uniform_buffer;
		uint32_t		uniform_offset;
		VkDescriptorSet ubo_set;
		aliasubo_t	   *ubo = (aliasubo_t *)R_UniformAllocate (sizeof (aliasubo_t), &uniform_buffer, &uniform_offset, &ubo_set);

		memcpy (ubo->model_matrix, model_matrix, 16 * sizeof (float));
		memcpy (ubo->shade_vector, shadevector, 3 * sizeof (float));
		ubo->blend_factor = blend;
		memcpy (ubo->light_color, lightcolor, 3 * sizeof (float));
		ubo->flags = (fb != NULL) ? 0x1 : 0x0;

		if (force_unlit || r_fullbright_cheatsafe || (r_lightmap_cheatsafe && r_fullbright.value))
			ubo->flags |= 0x2;

		if (paliashdr->poseverttype == PV_QUAKE3)
			ubo->flags |= 0x4;

		ubo->entalpha = entity_alpha;

		VkDescriptorSet descriptor_sets[3] = {tx->descriptor_set, (fb != NULL) ? fb->descriptor_set : tx->descriptor_set, ubo_set};
		vulkan_globals.vk_cmd_bind_descriptor_sets (
			cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 0, 3, descriptor_sets, 1, &uniform_offset);

		VkBuffer	 vertex_buffers[3] = {paliashdr->vertex_buffer, paliashdr->vertex_buffer, paliashdr->vertex_buffer};
		VkDeviceSize vertex_offsets[3] = {
			(unsigned)paliashdr->vbostofs, GLARB_GetXYZOffset (e, paliashdr, lerpdata.pose1), GLARB_GetXYZOffset (e, paliashdr, lerpdata.pose2)};
		vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 3, vertex_buffers, vertex_offsets);
		vulkan_globals.vk_cmd_bind_index_buffer (cbx->cb, paliashdr->index_buffer, 0, VK_INDEX_TYPE_UINT16);

		vulkan_globals.vk_cmd_draw_indexed (cbx->cb, paliashdr->numindexes, 1, 0, 0, 0);
		break;
	}
	case PV_MD5:
	case PV_MD5_8:
	{
		const r_vrik_prepared_palette_t *prepared = allow_tracked_palette ? R_VRIKRenderLookup (e) : NULL;
		if (prepared && prepared->model == e->model && prepared->geometry == selected_geometry &&
			prepared->joint_count == (uint32_t)paliashdr->numjoints &&
			prepared->descriptor_set != VK_NULL_HANDLE)
			tracked_palette = prepared;

		VkBuffer		uniform_buffer;
		uint32_t		uniform_offset;
		VkDescriptorSet ubo_set;
		md5ubo_t	   *ubo = (md5ubo_t *)R_UniformAllocate (sizeof (md5ubo_t), &uniform_buffer, &uniform_offset, &ubo_set);

		memcpy (ubo->model_matrix, model_matrix, 16 * sizeof (float));
		memcpy (ubo->shade_vector, shadevector, 3 * sizeof (float));
		ubo->blend_factor = tracked_palette ? 0.0f : blend;
		memcpy (ubo->light_color, lightcolor, 3 * sizeof (float));
		ubo->flags = (fb != NULL) ? 0x1 : 0x0;
		if (force_unlit || r_fullbright_cheatsafe || (r_lightmap_cheatsafe && r_fullbright.value))
			ubo->flags |= 0x2;
		ubo->entalpha = entity_alpha;
		ubo->joints_offsets[0] = tracked_palette ? tracked_palette->joint_offset : lerpdata.pose1 * paliashdr->numjoints;
		ubo->joints_offsets[1] = tracked_palette ? tracked_palette->joint_offset : lerpdata.pose2 * paliashdr->numjoints;

		VkDescriptorSet descriptor_sets[4] = {
			tx->descriptor_set, (fb != NULL) ? fb->descriptor_set : tx->descriptor_set, ubo_set,
			tracked_palette ? tracked_palette->descriptor_set : paliashdr->joints_set};
		vulkan_globals.vk_cmd_bind_descriptor_sets (
			cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 0, 4, descriptor_sets, 1, &uniform_offset);

		VkBuffer	 vertex_buffers[1] = {paliashdr->vertex_buffer};
		VkDeviceSize vertex_offsets[1] = {0};
		vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, vertex_buffers, vertex_offsets);
		vulkan_globals.vk_cmd_bind_index_buffer (cbx->cb, paliashdr->index_buffer, 0, VK_INDEX_TYPE_UINT16);

		//
		vulkan_globals.vk_cmd_draw_indexed (cbx->cb, paliashdr->numindexes, 1, 0, 0, 0);
		break;
	}
	default:
		assert (false);
	}
}

/*
=================
R_EntityPoseAt

Pose displayed by the given model frame at the given time (framegroup poses
advance with time).
=================
*/
static int R_EntityPoseAt (aliashdr_t *paliashdr, int frame, double time)
{
	if ((frame >= paliashdr->numframes) || (frame < 0))
		frame = 0;

	int posenum = paliashdr->frames[frame].firstpose;
	int numposes = paliashdr->frames[frame].numposes;
	if (numposes > 1)
		posenum += (int)(time / paliashdr->frames[frame].interval) % numposes;
	return posenum;
}

/*
=================
R_SetupAliasFrame -- johnfitz -- rewritten to support lerping

Computes pose1/pose2/blend from the parse-side interpolation state and
cl.time. Does not modify the entity.
=================
*/
void R_SetupAliasFrame (const entity_t *e, aliashdr_t *paliashdr, lerpdata_t *lerpdata)
{
	int frame = e->frame;
	if ((frame >= paliashdr->numframes) || (frame < 0))
		frame = 0;

	if (r_lerpmodels.value && !(e->model->flags & MOD_NOLERP && r_lerpmodels.value != 2))
	{
		int	   numposes = paliashdr->frames[frame].numposes;
		double change_time = e->lerp.frame_change_time;

		if (numposes > 1)
		{
			// framegroup: poses advance with cl.time; lerp from the previous
			// group pose unless the entity entered this frame more recently
			double interval = paliashdr->frames[frame].interval;
			int	   idx = (int)(cl.time / interval);
			double boundary = idx * interval;

			lerpdata->pose2 = paliashdr->frames[frame].firstpose + idx % numposes;
			if (change_time > boundary)
			{
				lerpdata->pose1 = R_EntityPoseAt (paliashdr, e->lerp.prev_frame, change_time);
				lerpdata->blend = CLAMP (0, (cl.time - change_time) / interval, 1);
			}
			else
			{
				lerpdata->pose1 = paliashdr->frames[frame].firstpose + (idx + numposes - 1) % numposes;
				lerpdata->blend = CLAMP (0, (cl.time - boundary) / interval, 1);
			}
		}
		else if (change_time > 0)
		{
			double duration = (e->lerp.frame_duration > 0) ? e->lerp.frame_duration : 0.1;
			lerpdata->pose2 = paliashdr->frames[frame].firstpose;
			lerpdata->pose1 = R_EntityPoseAt (paliashdr, e->lerp.prev_frame, change_time);
			lerpdata->blend = CLAMP (0, (cl.time - change_time) / duration, 1);
		}
		else
		{
			lerpdata->pose2 = paliashdr->frames[frame].firstpose;
			lerpdata->pose1 = lerpdata->pose2;
			lerpdata->blend = 1;
		}

		// Clamp poses (safety check for Quake1 models)
		if (paliashdr->poseverttype == PV_QUAKE1)
		{
			if (lerpdata->pose2 >= paliashdr->numposes || lerpdata->pose2 < 0)
			{
				Con_DPrintf ("R_AliasSetupFrame: invalid current pose %d (%d total) for '%s'\n", lerpdata->pose2, paliashdr->numposes, e->model->name);
				lerpdata->pose2 = 0;
			}
			if (lerpdata->pose1 >= paliashdr->numposes || lerpdata->pose1 < 0)
			{
				Con_DPrintf ("R_AliasSetupFrame: invalid prev pose %d (%d total) for '%s'\n", lerpdata->pose1, paliashdr->numposes, e->model->name);
				lerpdata->pose1 = lerpdata->pose2;
			}
		}
		else if (paliashdr->poseverttype == PV_MD5 || paliashdr->poseverttype == PV_MD5_8)
		{
			// MD5 uses numframes for joint matrices
			if (lerpdata->pose1 >= paliashdr->numframes || lerpdata->pose1 < 0)
				lerpdata->pose1 = 0;
			if (lerpdata->pose2 >= paliashdr->numframes || lerpdata->pose2 < 0)
				lerpdata->pose2 = 0;
		}
	}
	else
	{
		lerpdata->blend = 1;
		lerpdata->pose1 = R_EntityPoseAt (paliashdr, frame, cl.time);
		lerpdata->pose2 = lerpdata->pose1;
	}
}

/*
=================
R_GetEntityLerpedTransform

Computes lerped origin/angles from the parse-side interpolation state and
cl.time. Does not modify the entity.

Attached entities (tagentity) use their post-attachment origin/angles, which
only exist on the entity itself.
=================
*/
void R_GetEntityLerpedTransform (const entity_t *e, vec3_t out_origin, vec3_t out_angles)
{
	if (r_lerpmove.value && e != &cl.viewent && e->lerp.movestep && !e->netstate.tagentity && e->lerp.move_change_time > 0)
	{
		double change_time = e->lerp.move_change_time;
		double duration = (e->lerp.move_duration > 0) ? e->lerp.move_duration : 0.1;
		float  blend = CLAMP (0, (cl.time - change_time) / duration, 1);

		// translation
		vec3_t d;
		VectorSubtract (e->msg_origins[0], e->lerp.prev_origin, d);
		out_origin[0] = e->lerp.prev_origin[0] + d[0] * blend;
		out_origin[1] = e->lerp.prev_origin[1] + d[1] * blend;
		out_origin[2] = e->lerp.prev_origin[2] + d[2] * blend;

		// rotation (if enabled); EF_ROTATE angles are client-side and only exist on the entity
		if (r_lerpturn.value && !(e->model->flags & EF_ROTATE))
		{
			VectorSubtract (e->msg_angles[0], e->lerp.prev_angles, d);
			for (int i = 0; i < 3; i++)
			{
				if (d[i] > 180)
					d[i] -= 360;
				if (d[i] < -180)
					d[i] += 360;
			}
			out_angles[0] = e->lerp.prev_angles[0] + d[0] * blend;
			out_angles[1] = e->lerp.prev_angles[1] + d[1] * blend;
			out_angles[2] = e->lerp.prev_angles[2] + d[2] * blend;
		}
		else
		{
			VectorCopy (e->angles, out_angles);
		}
	}
	else // don't lerp
	{
		VectorCopy (e->origin, out_origin);
		VectorCopy (e->angles, out_angles);
	}
}

static float R_VRIKLerpAngle (float from, float to, float blend)
{
	float delta = to - from;

	while (delta > 180.0f) delta -= 360.0f;
	while (delta < -180.0f) delta += 360.0f;
	return from + delta * blend;
}

/* Sample the newest usable head/hand pose without changing the entity cache. */
qboolean R_VRIKSampleEntityPose (const entity_t *entity, vrik_pose_t *out)
{
	const vrik_pose_t *newest, *older;
	double newesttime, oldertime, sampletime;
	float blend;
	int tracker, axis;

	if (!entity || !out || entity->vrik_pose_count < 1)
		return false;
	newest = &entity->vrik_poses[0];
	newesttime = entity->vrik_pose_times[0];
	if (!(newest->flags & VRIK_FLAG_ACTIVE) ||
		!(newest->flags & VRIK_FLAG_HEAD_TRACKED) ||
		realtime - newesttime > VRIK_POSE_STALE_TIME)
		return false;

	*out = *newest;
	if (entity->vrik_pose_count < 2)
		return true;
	older = &entity->vrik_poses[1];
	oldertime = entity->vrik_pose_times[1];
	if (!(older->flags & VRIK_FLAG_ACTIVE) || newesttime <= oldertime)
		return true;

	/* Delay by 75 ms to absorb the natural 20 Hz pose cadence. */
	sampletime = realtime - 0.075;
	blend = (float)((sampletime - oldertime) / (newesttime - oldertime));
	blend = CLAMP (0.0f, blend, 1.0f);
	for (tracker = 0; tracker < VRIK_TRACKER_COUNT; tracker++)
		for (axis = 0; axis < 3; axis++)
		{
			out->position[tracker][axis] = older->position[tracker][axis] +
				(newest->position[tracker][axis] - older->position[tracker][axis]) * blend;
			out->orientation[tracker][axis] = R_VRIKLerpAngle (
				older->orientation[tracker][axis], newest->orientation[tracker][axis], blend);
		}
	{
		unsigned char dominanttracked =
			(newest->flags & VRIK_FLAG_DOMINANT_LEFT) ?
			VRIK_FLAG_LEFT_HAND_TRACKED : VRIK_FLAG_RIGHT_HAND_TRACKED;
		if (!((older->flags ^ newest->flags) & VRIK_FLAG_DOMINANT_LEFT) &&
			(older->flags & dominanttracked) &&
			(newest->flags & dominanttracked))
			for (axis = 0; axis < 3; axis++)
				out->aim_orientation[axis] = R_VRIKLerpAngle (
					older->aim_orientation[axis], newest->aim_orientation[axis], blend);
	}
	out->body_yaw = R_VRIKLerpAngle (older->body_yaw, newest->body_yaw, blend);
	return true;
}

/* Keep v3 lower roles independent from the legacy head/hand compatibility
 * sample.  A present-but-untracked value is a usable sender prediction, while
 * only two consecutive tracked values are interpolated. */
qboolean R_VRIKSampleEntityLowerTargets (const entity_t *entity,
	r_vrik_lowerbody_targets_t *out)
{
	const vrik_codec_pose_t *newest, *older = NULL;
	double newesttime, oldertime, sampletime;
	float blend = 1.0f;
	int role;

	if (!out)
		return false;
	memset (out, 0, sizeof (*out));
	if (!entity || entity->vrik_pose_count < 1)
		return false;
	newest = &entity->vrik_v3_poses[0];
	newesttime = entity->vrik_pose_times[0];
	if (!(newest->flags & VRIK_V3_FLAG_ACTIVE) ||
		realtime - newesttime > VRIK_POSE_STALE_TIME)
		return false;
	if (entity->vrik_pose_count >= 2)
	{
		const vrik_codec_pose_t *candidate = &entity->vrik_v3_poses[1];
		oldertime = entity->vrik_pose_times[1];
		if ((candidate->flags & VRIK_V3_FLAG_ACTIVE) && newesttime > oldertime)
		{
			older = candidate;
			sampletime = realtime - 0.075;
			blend = CLAMP (0.0f,
				(float)((sampletime - oldertime) / (newesttime - oldertime)), 1.0f);
		}
	}
	for (role = 0; role < R_VRIK_LOWER_ROLE_COUNT; role++)
	{
		const int target = VRIK_TARGET_HIP + role;
		const unsigned char sourcebit = VRIK_TARGET_BIT (target);
		const unsigned char destinationbit = R_VRIK_LOWER_BIT (role);
		int axis;

		if (!(newest->present_mask & sourcebit))
			continue;
		out->present_mask |= destinationbit;
		out->confidence[role] = 1.0f;
		VectorCopy (newest->targets[target].position, out->position[role]);
		VectorCopy (newest->targets[target].orientation, out->orientation[role]);
		if (!(newest->tracked_mask & sourcebit))
		{
			out->predicted_mask |= destinationbit;
			continue;
		}
		out->tracked_mask |= destinationbit;
		if (older && (older->present_mask & sourcebit) &&
			(older->tracked_mask & sourcebit))
			for (axis = 0; axis < 3; axis++)
			{
				out->position[role][axis] = older->targets[target].position[axis] +
					(newest->targets[target].position[axis] -
					 older->targets[target].position[axis]) * blend;
				out->orientation[role][axis] = R_VRIKLerpAngle (
					older->targets[target].orientation[axis],
					newest->targets[target].orientation[axis], blend);
			}
	}
	return out->present_mask != 0;
}

/*
=================
R_SetupAliasLighting -- johnfitz -- broken out from R_DrawAliasModel and rewritten
=================
*/
static void R_SetupAliasLighting (entity_t *e, vec3_t *shadevector, vec3_t *lightcolor)
{
	vec3_t dist;
	float  add;
	int	   i;
	int	   quantizedangle;
	float  radiansangle;

	// if the initial trace is completely black, try again from above
	// this helps with models whose origin is slightly below ground level
	// (e.g. some of the candles in the DOTM start map)
	if (!R_LightPoint (e->origin, 0.f, &e->lightcache, lightcolor))
		R_LightPoint (e->origin, e->model->maxs[2] * 0.5f, &e->lightcache, lightcolor);

	// add dlights
	for (i = 0; i < MAX_DLIGHTS; i++)
	{
		if (cl_dlights[i].die >= cl.time)
		{
			VectorSubtract (e->origin, cl_dlights[i].origin, dist);
			add = cl_dlights[i].radius - VectorLength (dist);
			if (add > 0)
			{
				if (cl_dlights[i].cone_cos > -1.0f)
				{
					vec3_t dir;
					VectorCopy (dist, dir);
					VectorNormalize (dir);
					const float cone_cos = cl_dlights[i].cone_cos;
					const float cone_dot = DotProduct (dir, cl_dlights[i].cone_dir);
					float		cone_scale;
					if (cl_dlights[i].kex_intensity > 0.0f)
					{
						// linear falloff from cone axis to edge, matches update_lightmap.inc
						if (cone_dot < cone_cos)
							continue;
						cone_scale = 1.0f - (1.0f - cone_dot) / (1.0f - cone_cos);
					}
					else
					{
						// soft edged spotlight falloff, matches update_lightmap.inc
						const float cone_soft = cone_cos + ((1.0f - cone_cos) * 0.25f);
						cone_scale = CLAMP (0.0f, (cone_dot - cone_cos) / q_max (cone_soft - cone_cos, 0.0001f), 1.0f);
					}
					add *= cone_scale;
					if (add <= 0.0f)
						continue;
				}
				if (cl_dlights[i].kex_intensity > 0.0f)
				{
					// KEX falloff: range-normalized, scaled by intensity (matches update_lightmap.inc,
					// sans the Lambert term since alias models use their own shading)
					add *= cl_dlights[i].kex_intensity * 0.5f * (256.0f / cl_dlights[i].radius);
				}
				VectorMA (*lightcolor, add, cl_dlights[i].color, *lightcolor);
			}
		}
	}

	// minimum light value on gun (24)
	if (e == &cl.viewent)
	{
		add = 72.0f - ((*lightcolor)[0] + (*lightcolor)[1] + (*lightcolor)[2]);
		if (add > 0.0f)
		{
			(*lightcolor)[0] += add / 3.0f;
			(*lightcolor)[1] += add / 3.0f;
			(*lightcolor)[2] += add / 3.0f;
		}
	}

	// minimum light value on players (8)
	if (e > cl.entities && e <= cl.entities + cl.maxclients)
	{
		add = 24.0f - ((*lightcolor)[0] + (*lightcolor)[1] + (*lightcolor)[2]);
		if (add > 0.0f)
		{
			(*lightcolor)[0] += add / 3.0f;
			(*lightcolor)[1] += add / 3.0f;
			(*lightcolor)[2] += add / 3.0f;
		}
	}

	// clamp lighting so it doesn't overbright as much (96)
	add = 288.0f / ((*lightcolor)[0] + (*lightcolor)[1] + (*lightcolor)[2]);
	if (add < 1.0f)
		VectorScale ((*lightcolor), add, (*lightcolor));

	quantizedangle = ((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1);

	// ericw -- shadevector is passed to the shader to compute shadedots inside the
	// shader, see GLAlias_CreateShaders()
	radiansangle = (quantizedangle / 16.0) * 2.0 * 3.14159;
	(*shadevector)[0] = cos (-radiansangle);
	(*shadevector)[1] = sin (-radiansangle);
	(*shadevector)[2] = 1;
	VectorNormalize (*shadevector);
	// ericw --

	VectorScale ((*lightcolor), 1.0f / 200.0f, (*lightcolor));
}

/*
=================
R_DrawAliasModel -- johnfitz -- almost completely rewritten
=================
*/
/* -1 suppresses an invalid transform; 0/1 select the front-face winding. */
static int R_AliasModelMatrixInternal (
	entity_t *e, const aliashdr_t *paliashdr, lerpdata_t *lerpdata, float model_matrix[16], qboolean apply_viewmodel_transforms)
{
	if (apply_viewmodel_transforms && R_IsVRViewmodel (e))
	{
		vec3_t origin, angles, header_origin, held_offset = {0.0f, 0.0f, 0.0f};
		float header_scale[3], geometry_scale[3];
		float held_scale = 1.0f;
		const qboolean enhanced_format = paliashdr->poseverttype == PV_MD5 || paliashdr->poseverttype == PV_MD5_8;
		const qboolean multiplayer = cl.maxclients > 1;
		const qboolean has_calibration = VR_WeaponCalibrationLookupHeld (e->model->name, enhanced_format, multiplayer, held_offset, &held_scale);

		if (!has_calibration)
		{
			held_offset[0] = held_offset[1] = held_offset[2] = 0.0f;
			held_scale = 1.0f;
		}
		if (!isfinite (held_scale) || held_scale == 0.0f)
			return -1;

		for (int axis = 0; axis < 3; ++axis)
		{
			if (!isfinite (lerpdata->origin[axis]) ||
				!isfinite (lerpdata->angles[axis]) ||
				!isfinite (paliashdr->scale[axis]) ||
				!isfinite (paliashdr->scale_origin[axis]))
				return -1;
			origin[axis] = lerpdata->origin[axis];
			angles[axis] = fmodf (lerpdata->angles[axis], 360.0f);
			header_scale[axis] = paliashdr->scale[axis];
			header_origin[axis] = paliashdr->scale_origin[axis];
		}

		const double c_value = ((double)vr_world_scale.value / 0.75) * (double)vr_gunmodelscale.value;
		if (!isfinite (c_value) || c_value == 0.0 || fabs (c_value) > FLT_MAX ||
			!isfinite (vr_gunmodely.value))
			return -1;
		const float c = (float)c_value;
		const float gunmodel_y = vr_gunmodely.value;
		float local_translation[3];
		for (int axis = 0; axis < 3; ++axis)
		{
			double local_offset = (double)header_origin[axis] + (double)held_offset[axis];
			if (axis == 2)
				local_offset += (double)gunmodel_y;
			double scaled_offset = (double)c * local_offset;
			if (!isfinite (scaled_offset) || fabs (scaled_offset) > FLT_MAX)
				return -1;
			local_translation[axis] = (float)scaled_offset;

			double scale_value = (double)c * (double)held_scale * (double)header_scale[axis];
			if (!isfinite (scale_value) || fabs (scale_value) > FLT_MAX)
				return -1;
			geometry_scale[axis] = (float)scale_value;
		}

		const qboolean mirror_model_y = VR_InputDominantPhysicalHand () == 0;
		if (mirror_model_y)
		{
			/* E * MirrorY * T * S: reflect the held offset with the mesh. */
			local_translation[1] = -local_translation[1];
			geometry_scale[1] = -geometry_scale[1];
		}

		const float entity_scale = ENTSCALE_DECODE (e->netstate.scale);
		if (!isfinite (entity_scale) || entity_scale == 0.0f ||
			geometry_scale[0] == 0.0f || geometry_scale[1] == 0.0f ||
			geometry_scale[2] == 0.0f)
			return -1;
		const qboolean negative_determinant =
			((entity_scale < 0.0f) ^ (geometry_scale[0] < 0.0f) ^ (geometry_scale[1] < 0.0f) ^ (geometry_scale[2] < 0.0f));

		IdentityMatrix (model_matrix);
		R_RotateForEntity (model_matrix, origin, angles, e->netstate.scale);
		float translation_matrix[16];
		TranslationMatrix (translation_matrix, local_translation[0], local_translation[1], local_translation[2]);
		MatrixMultiply (model_matrix, translation_matrix);
		float scale_matrix[16];
		ScaleMatrix (scale_matrix, geometry_scale[0], geometry_scale[1], geometry_scale[2]);
		MatrixMultiply (model_matrix, scale_matrix);

		if (!R_AliasMatrixIsFinite (model_matrix))
			return -1;
		return negative_determinant;
	}

	IdentityMatrix (model_matrix);

	float fovscale = 1.0f;
	if (apply_viewmodel_transforms && e == &cl.viewent && r_refdef.basefov > 90.f && cl_gun_fovscale.value)
	{
		fovscale = tan (r_refdef.basefov * (0.5f * M_PI / 180.f));
		fovscale = 1.f + (fovscale - 1.f) * cl_gun_fovscale.value;
	}

	vec3_t origin;
	VectorCopy (lerpdata->origin, origin);
	if (apply_viewmodel_transforms && e == &cl.viewent)
	{
		VectorMA (origin, cl_gun_x.value * paliashdr->scale[0] * fovscale, vright, origin);
		VectorMA (origin, cl_gun_y.value * paliashdr->scale[1] * fovscale, vup, origin);
		VectorMA (origin, cl_gun_z.value * paliashdr->scale[2], vpn, origin);
	}
	R_RotateForEntity (model_matrix, origin, lerpdata->angles, e->netstate.scale);

	float translation_matrix[16];
	TranslationMatrix (translation_matrix, paliashdr->scale_origin[0], paliashdr->scale_origin[1] * fovscale, paliashdr->scale_origin[2] * fovscale);
	MatrixMultiply (model_matrix, translation_matrix);

	float scale_matrix[16];
	ScaleMatrix (scale_matrix, paliashdr->scale[0], paliashdr->scale[1] * fovscale, paliashdr->scale[2] * fovscale);
	MatrixMultiply (model_matrix, scale_matrix);
	return false;
}

int R_AliasModelMatrix (entity_t *e, const aliashdr_t *paliashdr, lerpdata_t *lerpdata, float model_matrix[16])
{
	return R_AliasModelMatrixInternal (e, paliashdr, lerpdata, model_matrix, true);
}

static void R_DrawAliasSurfaces (
	cb_context_t *cbx, entity_t *e, aliashdr_t *geometry, const aliashdr_t *selected_geometry, lerpdata_t lerpdata,
	float model_matrix[16], float entity_alpha, qboolean alphatest, vec3_t shadevector, vec3_t lightcolor, qboolean force_unlit,
	qboolean allow_tracked_palette, qboolean opposite_front_face, int *aliaspolys)
{
	int skinnum = e->skinnum;
	const int anim = (int)(cl.time * 10) & 3;

	// Draw each surface of the model independently:
	for (aliashdr_t *hdr = geometry; hdr != NULL; hdr = hdr->nextsurface)
	{
		gltexture_t *tx, *fb;

		//
		// set up textures
		//
		if ((skinnum >= hdr->numskins) || (skinnum < 0))
		{
			Con_DPrintf ("R_DrawAliasModel: no such skin # %d for '%s'\n", skinnum, e->model->name);
			// ericw -- display skin 0 for winquake compatibility
			skinnum = 0;
		}
		tx = hdr->gltextures[skinnum][anim];
		fb = hdr->fbtextures[skinnum][anim];

		if (e->colormap != vid.colormap && !gl_nocolors.value)
			if ((uintptr_t)e >= (uintptr_t)&cl.entities[1] && (uintptr_t)e <= (uintptr_t)&cl.entities[cl.maxclients] && playertextures[e - cl.entities - 1])
				tx = playertextures[e - cl.entities - 1];

		// if there are no texture, force the grey one. (a.k.a lightmap).
		if (tx == NULL)
		{
			tx = greytexture;
			fb = NULL;
		}

		if (!gl_fullbrights.value)
			fb = NULL;

		if (!force_unlit && r_fullbright_cheatsafe)
		{
			lightcolor[0] = 0.5f;
			lightcolor[1] = 0.5f;
			lightcolor[2] = 0.5f;
		}
		if (!force_unlit && r_lightmap_cheatsafe)
		{
			tx = greytexture;
			fb = NULL;
			if (r_fullbright.value)
			{
				lightcolor[0] = 1.0f;
				lightcolor[1] = 1.0f;
				lightcolor[2] = 1.0f;
			}
		}

		GL_DrawAliasFrame (
			cbx, e, hdr, selected_geometry, lerpdata, tx, fb, model_matrix, entity_alpha, alphatest, shadevector, lightcolor, false,
			opposite_front_face, force_unlit, allow_tracked_palette);

		if (aliaspolys)
			*aliaspolys += hdr->numtris;
	} // for each surface
}

void R_DrawAliasModel (cb_context_t *cbx, entity_t *e, int *aliaspolys)
{
	aliashdr_t	*paliashdr;
	int			 skinnum = e->skinnum;
	lerpdata_t	 lerpdata;

	// A pending reload can change the model bounds, so refresh it before culling.
	// Loaded models can reject offscreen entities without selecting skin/pose data.
	paliashdr = e->model->needload ? (aliashdr_t *)Mod_Extradata_CheckSkin (e->model, skinnum) : NULL;
	if (!R_IsVRViewmodel (e) && R_CullModelForEntity (e))
		return;

	if (!paliashdr)
		paliashdr = (aliashdr_t *)Mod_Extradata_CheckSkin (e->model, skinnum);

	qboolean alphatest = !!(e->model->flags & MF_HOLEY);

	R_SetupAliasFrame (e, paliashdr, &lerpdata);
	R_GetEntityLerpedTransform (e, lerpdata.origin, lerpdata.angles);

	//
	// transform it
	//
	float model_matrix[16];
	const int matrix_result = R_AliasModelMatrix (e, paliashdr, &lerpdata, model_matrix);
	if (matrix_result < 0)
		return;
	const qboolean opposite_front_face = matrix_result > 0;

	//
	// set up for alpha blending
	//
	float entalpha;
	if (r_lightmap_cheatsafe)
		entalpha = 1;
	else
		entalpha = ENTALPHA_DECODE (e->alpha);
	if (entalpha == 0)
		return;

	//
	// set up lighting
	//
	vec3_t shadevector, lightcolor;
	R_SetupAliasLighting (e, &shadevector, &lightcolor);

	R_DrawAliasSurfaces (
		cbx, e, paliashdr, paliashdr, lerpdata, model_matrix, entalpha, alphatest, shadevector, lightcolor, false, true,
		opposite_front_face, aliaspolys);
}

void R_DrawPreparedWheelAliasModel (
	cb_context_t *cbx, entity_t *e, aliashdr_t *selected_geometry, const vec3_t tint, float mesh_scale, int *aliaspolys)
{
	if (!cbx || !e || !e->model || !selected_geometry || !tint || !isfinite (mesh_scale) || mesh_scale == 0.0f)
		return;

	for (int axis = 0; axis < 3; ++axis)
		if (!isfinite (tint[axis]))
			return;

	lerpdata_t lerpdata;
	R_SetupAliasFrame (e, selected_geometry, &lerpdata);
	R_GetEntityLerpedTransform (e, lerpdata.origin, lerpdata.angles);

	float model_matrix[16];
	const int matrix_result = R_AliasModelMatrixInternal (e, selected_geometry, &lerpdata, model_matrix, false);
	if (matrix_result < 0)
		return;
	qboolean opposite_front_face = matrix_result > 0;
	if (mesh_scale != 1.0f)
	{
		/* Apply the prepared mesh scale in world space while keeping e->origin fixed. */
		float centered_scale[16], scale_matrix[16], translation_matrix[16];
		TranslationMatrix (centered_scale, e->origin[0], e->origin[1], e->origin[2]);
		ScaleMatrix (scale_matrix, mesh_scale, mesh_scale, mesh_scale);
		MatrixMultiply (centered_scale, scale_matrix);
		TranslationMatrix (translation_matrix, -e->origin[0], -e->origin[1], -e->origin[2]);
		MatrixMultiply (centered_scale, translation_matrix);
		MatrixMultiply (centered_scale, model_matrix);
		memcpy (model_matrix, centered_scale, 16 * sizeof (float));
		if (!R_AliasMatrixIsFinite (model_matrix))
			return;
		if (mesh_scale < 0.0f)
			opposite_front_face = !opposite_front_face;
	}
	if (!R_AliasMatrixIsFinite (model_matrix))
		return;

	vec3_t shadevector = {0.0f, 0.0f, 0.0f};
	/* alias_common.inc multiplies the interpolated color by 2.0. */
	vec3_t lightcolor = {tint[0] * 0.5f, tint[1] * 0.5f, tint[2] * 0.5f};
	const qboolean alphatest = !!(e->model->flags & MF_HOLEY);
	R_DrawAliasSurfaces (
		cbx, e, selected_geometry, selected_geometry, lerpdata, model_matrix, 1.0f, alphatest, shadevector, lightcolor, true, false,
		opposite_front_face, aliaspolys);
}

// johnfitz -- values for shadow matrix
#define SHADOW_SKEW_X -0.7 // skew along x axis. -0.7 to mimic glquake shadows
#define SHADOW_SKEW_Y 0	   // skew along y axis. 0 to mimic glquake shadows
#define SHADOW_VSCALE 0	   // 0=completely flat
#define SHADOW_HEIGHT 0.1  // how far above the floor to render the shadow
// johnfitz

/*
=================
R_DrawAliasModel_ShowTris -- johnfitz
=================
*/
void R_DrawAliasModel_ShowTris (cb_context_t *cbx, entity_t *e)
{
	aliashdr_t *paliashdr;
	lerpdata_t	lerpdata;

	//
	// setup pose/lerp data -- do it first so we don't miss updates due to culling
	//
	paliashdr = (aliashdr_t *)Mod_Extradata_CheckSkin (e->model, e->skinnum);

	R_SetupAliasFrame (e, paliashdr, &lerpdata);
	R_GetEntityLerpedTransform (e, lerpdata.origin, lerpdata.angles);

	//
	// cull it
	//
	if (!R_IsVRViewmodel (e) && R_CullModelForEntity (e))
		return;

	//
	// transform it
	//
	float model_matrix[16];
	const int matrix_result = R_AliasModelMatrix (e, paliashdr, &lerpdata, model_matrix);
	if (matrix_result < 0)
		return;
	const qboolean opposite_front_face = matrix_result > 0;

	vec3_t shadevector = {0.0f, 0.0f, 0.0f};
	vec3_t lightcolor = {0.0f, 0.0f, 0.0f};
	// Draw each surface of the model independently:
	for (aliashdr_t *hdr = paliashdr; hdr != NULL; hdr = hdr->nextsurface)
	{
		GL_DrawAliasFrame (
			cbx, e, hdr, paliashdr, lerpdata, nulltexture, nulltexture, model_matrix, 0.0f, false, shadevector, lightcolor, r_showtris.value,
			opposite_front_face, false, true);
	}
}

/*
=================
R_DrawAliasModel_ShowSkel
=================
*/
void R_DrawAliasModel_ShowSkel (cb_context_t *cbx, entity_t *e)
{
	aliashdr_t *paliashdr;
	lerpdata_t	lerpdata;
	const r_vrik_prepared_palette_t *tracked_palette;

	paliashdr = (aliashdr_t *)Mod_Extradata_CheckSkin (e->model, e->skinnum);
	if ((paliashdr->poseverttype != PV_MD5 && paliashdr->poseverttype != PV_MD5_8) || paliashdr->skeleton_index_buffer == VK_NULL_HANDLE ||
		paliashdr->num_skeleton_indexes <= 0 || paliashdr->joints_set == VK_NULL_HANDLE)
		return;

	R_SetupAliasFrame (e, paliashdr, &lerpdata);
	R_GetEntityLerpedTransform (e, lerpdata.origin, lerpdata.angles);
	tracked_palette = R_VRIKRenderLookup (e);
	if (!tracked_palette || tracked_palette->model != e->model || tracked_palette->geometry != paliashdr ||
		tracked_palette->joint_count != (uint32_t)paliashdr->numjoints ||
		tracked_palette->descriptor_set == VK_NULL_HANDLE)
		tracked_palette = NULL;

	if (!R_IsVRViewmodel (e) && R_CullModelForEntity (e))
		return;

	float model_matrix[16];
	if (R_AliasModelMatrix (e, paliashdr, &lerpdata, model_matrix) < 0)
		return;

	float blend = 0.0f;
	if (!tracked_palette && lerpdata.pose1 != lerpdata.pose2)
		blend = lerpdata.blend;

	VkBuffer		uniform_buffer;
	uint32_t		uniform_offset;
	VkDescriptorSet ubo_set;
	md5ubo_t	   *ubo = (md5ubo_t *)R_UniformAllocate (sizeof (md5ubo_t), &uniform_buffer, &uniform_offset, &ubo_set);

	memcpy (ubo->model_matrix, model_matrix, 16 * sizeof (float));
	ubo->shade_vector[0] = 0.0f;
	ubo->shade_vector[1] = 0.0f;
	ubo->shade_vector[2] = 0.0f;
	ubo->blend_factor = blend;
	ubo->light_color[0] = 1.0f;
	ubo->light_color[1] = 1.0f;
	ubo->light_color[2] = 0.0f;
	ubo->entalpha = 1.0f;
	ubo->flags = 0;
	ubo->joints_offsets[0] = tracked_palette ? tracked_palette->joint_offset : lerpdata.pose1 * paliashdr->numjoints;
	ubo->joints_offsets[1] = tracked_palette ? tracked_palette->joint_offset : lerpdata.pose2 * paliashdr->numjoints;

	vulkan_pipeline_t pipeline = vulkan_globals.md5_debug_pipeline[cbx->pipeline_variant];
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	VkDescriptorSet descriptor_sets[2] = {ubo_set, tracked_palette ? tracked_palette->descriptor_set : paliashdr->joints_set};
	vulkan_globals.vk_cmd_bind_descriptor_sets (cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 2, 2, descriptor_sets, 1, &uniform_offset);
	vulkan_globals.vk_cmd_bind_index_buffer (cbx->cb, paliashdr->skeleton_index_buffer, 0, VK_INDEX_TYPE_UINT16);
	vulkan_globals.vk_cmd_draw_indexed (cbx->cb, paliashdr->num_skeleton_indexes, 1, 0, 0, 0);
}
