/* Frame-owned Vulkan palette uploads for the inherited Ranger VRIK solve. */
#include "quakedef.h"
#include "r_avatar.h"
#include "custom_avatar.h"
#include "r_vrik.h"
#include "r_vrik_render.h"

#include <float.h>
#include <limits.h>
#include <math.h>

extern cvar_t r_lerpmodels;

#define R_VRIK_RENDER_MAX_JOINTS 256
#define R_VRIK_RENDER_MAX_AVATARS (PLAYER_AVATAR_COUNT + CUSTOM_AVATAR_MAX_PACKAGES)

typedef struct r_vrik_candidate_s
{
	const entity_t *entity;
	const qmodel_t *model;
	const aliashdr_t *geometry;
	md5_skeleton_view_t skeleton;
	lerpdata_t lerpdata;
	vrik_pose_t pose;
	uint32_t joint_count;
	qboolean muzzleflash;
	double tracked_cull_local_bound;
	vec3_t tracked_cull_origin;
	qboolean tracked_cull_valid;
	float target_to_canonical[12];
	qboolean alternate_avatar;
} r_vrik_candidate_t;

typedef struct r_vrik_staged_avatar_s
{
	const entity_t *entity;
	const qmodel_t *original_model;
	qmodel_t *source_model;
	const aliashdr_t *source_geometry;
	qmodel_t *target_model;
	const aliashdr_t *target_geometry;
	int id;
	float target_to_canonical[12];
	qboolean valid;
} r_vrik_staged_avatar_t;

static r_vrik_candidate_t candidates[MAX_SCOREBOARD];
static float candidate_palettes[MAX_SCOREBOARD][R_VRIK_RENDER_MAX_JOINTS][12];
static r_vrik_staged_avatar_t staged[MAX_SCOREBOARD];
static qmodel_t *builtin_models[PLAYER_AVATAR_COUNT];
static qboolean builtin_attempted[PLAYER_AVATAR_COUNT];
static qmodel_t *custom_models[CUSTOM_AVATAR_MAX_PACKAGES];
static qboolean custom_attempted[CUSTOM_AVATAR_MAX_PACKAGES];
static const qmodel_t *floor_source_models[R_VRIK_RENDER_MAX_AVATARS];
static const qmodel_t *floor_target_models[R_VRIK_RENDER_MAX_AVATARS];
static float floor_correction_z[R_VRIK_RENDER_MAX_AVATARS];
static qboolean floor_attempted[R_VRIK_RENDER_MAX_AVATARS];
static qboolean floor_valid[R_VRIK_RENDER_MAX_AVATARS];
static char admission_gamedir[MAX_OSPATH];
static r_vrik_prepared_palette_t prepared[DOUBLE_BUFFERED][MAX_SCOREBOARD];
static size_t prepared_count[DOUBLE_BUFFERED];
static VkDescriptorSet palette_descriptor_sets[DOUBLE_BUFFERED];
static uint32_t active_frame_slot;
static qboolean active_frame_valid;

void R_VRIKRenderResetAdmission (void)
{
	memset (staged, 0, sizeof (staged));
	memset (builtin_models, 0, sizeof (builtin_models));
	memset (builtin_attempted, 0, sizeof (builtin_attempted));
	memset (custom_models, 0, sizeof (custom_models));
	memset (custom_attempted, 0, sizeof (custom_attempted));
	memset (floor_source_models, 0, sizeof (floor_source_models));
	memset (floor_target_models, 0, sizeof (floor_target_models));
	memset (floor_attempted, 0, sizeof (floor_attempted));
	memset (floor_valid, 0, sizeof (floor_valid));
	q_strlcpy (admission_gamedir, com_gamedir, sizeof (admission_gamedir));
}

static qmodel_t *R_VRIKRenderBuiltinModel (int id)
{
	if (strcmp (admission_gamedir, com_gamedir))
		R_VRIKRenderResetAdmission ();
	qmodel_t *model = builtin_models[id];
	if (model && !model->needload && model->avatar_builtin && model->type == mod_alias &&
		model->extradata[PV_MD5] && model->md5_skeleton)
		return model;
	if (!model && builtin_attempted[id])
		return NULL;

	/* Model admission may upload textures and buffers. Only the main-thread
	 * staging caller reaches this path, before this frame's task graph. */
	GL_SynchronizeEndRenderingTask ();
	model = Mod_GetAvatarBuiltinModel (id);
	builtin_models[id] = model;
	builtin_attempted[id] = true;
	return model;
}

static const r_avatar_profile_t *R_VRIKRenderProfileForId (int id)
{
	const custom_avatar_t *custom = CustomAvatar_Get (id);
	return custom ? &custom->profile : R_AvatarProfileForId (id);
}

static qmodel_t *R_VRIKRenderCustomModel (int id)
{
	const int index = id - PLAYER_AVATAR_COUNT;
	qmodel_t *model;
	if (index < 0 || index >= CUSTOM_AVATAR_MAX_PACKAGES || !CustomAvatar_Get (id))
		return NULL;
	if (strcmp (admission_gamedir, com_gamedir))
		R_VRIKRenderResetAdmission ();
	model = custom_models[index];
	if (model && Mod_IsAdmittedAvatarModel (model) && model->avatar_custom_id == id)
		return model;
	if (custom_attempted[index] && (!model || CustomAvatar_HasFailed (id)))
		return NULL;
	/* The first admission can upload mesh and textures, so join the previous
	 * render task before touching their shared Vulkan and model tables. */
	GL_SynchronizeEndRenderingTask ();
	model = Mod_GetAvatarCustomModel (id);
	custom_models[index] = model;
	custom_attempted[index] = true;
	return model;
}

/* Admission-time bind contacts use the original MD5 weights, captured before
 * vkQuake releases the CPU mesh. Match the inherited triangle equipment
 * filter, then project only the surviving bind vertices into Ranger space. */
static qboolean R_VRIKRenderMinimumBindZ (const qmodel_t *model,
	const r_avatar_profile_t *profile, qboolean source, qboolean contacts,
	const float target_to_canonical[12], double *minimum_out)
{
	double minimum = DBL_MAX;
	int surface_count = 0;
	if (!model || !model->avatar_bind_surfaces || !minimum_out)
		return false;
	for (const md5_avatar_bind_surface_t *surface = model->avatar_bind_surfaces;
		surface; surface = surface->next)
	{
		if (++surface_count > MAX_SURFACES || !surface->vertices || !surface->indexes ||
			surface->numverts <= 0 || surface->numindexes <= 0 || surface->numindexes % 3)
			return false;
		for (int index = 0; index < surface->numindexes; index += 3)
		{
			const md5_avatar_bind_vertex_t *vertex[3];
			float min_weight = 1.0f, max_weight = 0.0f, sum_weight = 0.0f;
			for (int point = 0; point < 3; ++point)
			{
				const unsigned short vertex_index = surface->indexes[index + point];
				if (vertex_index >= surface->numverts)
					return false;
				vertex[point] = &surface->vertices[vertex_index];
				const float weight = vertex[point]->native_equipment_weight;
				if (!isfinite (weight) || weight < 0.0f || weight > 1.0001f)
					return false;
				min_weight = q_min (min_weight, weight);
				max_weight = q_max (max_weight, weight);
				sum_weight += weight;
			}
			if (!source && profile->equipment_policy == R_AVATAR_EQUIPMENT_ATTACH_HAND &&
				(sum_weight / 3.0f >= 0.5f || (min_weight >= 0.25f && max_weight >= 0.75f)))
				continue;
			for (int point = 0; point < 3; ++point)
			{
				const md5_avatar_bind_vertex_t *v = vertex[point];
				if (!isfinite (v->contact_weight) || v->contact_weight < 0.0f ||
					v->contact_weight > 1.0001f)
					return false;
				if (contacts && v->contact_weight < 0.25f)
					continue;
				if (source && !contacts && (v->ranger_gun_owned || v->ranger_axe_owned))
					continue;
				const double z = source ? v->xyz[2] :
					(double)target_to_canonical[8] * v->xyz[0] +
					(double)target_to_canonical[9] * v->xyz[1] +
					(double)target_to_canonical[10] * v->xyz[2] + target_to_canonical[11];
				if (!isfinite (z))
					return false;
				if (z < minimum)
					minimum = z;
			}
		}
	}
	if (minimum == DBL_MAX || minimum < -FLT_MAX || minimum > FLT_MAX)
		return false;
	*minimum_out = minimum;
	return true;
}

static qboolean R_VRIKRenderBindFloorCorrection (const qmodel_t *source,
	const qmodel_t *target, const r_avatar_profile_t *profile,
	const float target_to_canonical[12], float *correction_out)
{
	double source_floor, target_floor, correction;
	const qboolean contacts = profile->contact_root[0] != NULL;
	if (!R_VRIKRenderMinimumBindZ (source, profile, true, contacts,
		target_to_canonical, &source_floor) ||
		!R_VRIKRenderMinimumBindZ (target, profile, false, contacts,
			target_to_canonical, &target_floor))
		return false;
	correction = source_floor - target_floor;
	if (!isfinite (correction) || correction < -FLT_MAX || correction > FLT_MAX)
		return false;
	*correction_out = (float)correction;
	return true;
}

qboolean R_VRIKRenderStageAvatar (const entity_t *entity, int id)
{
	r_vrik_staged_avatar_t selection = {0};
	qmodel_t *source, *target;
	const r_avatar_profile_t *profile;
	md5_skeleton_view_t source_skeleton, target_skeleton;
	r_avatar_rig_t source_rig, target_rig;
	int player;

	if (!entity || !cl.entities)
		return false;
	for (player = 1; player <= cl.maxclients && player < cl.num_entities && player <= MAX_SCOREBOARD; ++player)
		if (entity == &cl.entities[player])
			break;
	if (player > cl.maxclients || player >= cl.num_entities || player > MAX_SCOREBOARD)
		return false;
	/* Replace any prior choice even when admission fails. */
	staged[player - 1] = selection;
	if (id == PLAYER_AVATAR_RANGER)
		return true;
	profile = R_VRIKRenderProfileForId (id);
	if (!profile || !entity->model || entity->model->needload ||
		entity->model->type != mod_alias || strcmp (entity->model->name, "progs/player.mdl"))
		return false;

	source = R_VRIKRenderBuiltinModel (PLAYER_AVATAR_RANGER);
	target = source ? (id < PLAYER_AVATAR_COUNT ? R_VRIKRenderBuiltinModel (id) :
		R_VRIKRenderCustomModel (id)) : NULL;
	if (!source || !target || !Mod_GetMD5Skeleton (source, &source_skeleton) ||
		!source_skeleton.from_rerelease || !Mod_GetMD5Skeleton (target, &target_skeleton) ||
		!R_AvatarResolveRig (R_AvatarProfileForId (PLAYER_AVATAR_RANGER), &source_skeleton, &source_rig) ||
		!R_AvatarResolveRig (profile, &target_skeleton, &target_rig) ||
		!R_AvatarTargetToCanonicalPresentation (&source_rig, &target_rig, selection.target_to_canonical))
		return false;
	if (id < 0 || id >= R_VRIK_RENDER_MAX_AVATARS)
		return false;
	if (!floor_attempted[id] || floor_source_models[id] != source || floor_target_models[id] != target)
	{
		floor_source_models[id] = source;
		floor_target_models[id] = target;
		floor_attempted[id] = true;
		floor_valid[id] = R_VRIKRenderBindFloorCorrection (source, target, profile,
			selection.target_to_canonical, &floor_correction_z[id]);
		if (!floor_valid[id])
			Con_Warning ("Avatar %s has no valid bind-floor contact; using Ranger\n", profile->key);
	}
	if (!floor_valid[id])
		return false;
	selection.target_to_canonical[11] += floor_correction_z[id];
	if (!isfinite (selection.target_to_canonical[11]))
		return false;
	selection.source_geometry = (const aliashdr_t *)source->extradata[PV_MD5];
	selection.target_geometry = (const aliashdr_t *)target->extradata[PV_MD5];
	if (!selection.source_geometry || !selection.target_geometry ||
		selection.source_geometry->numjoints != (int)source_skeleton.joint_count ||
		selection.target_geometry->numjoints != (int)target_skeleton.joint_count ||
		(selection.source_geometry->poseverttype != PV_MD5 && selection.source_geometry->poseverttype != PV_MD5_8) ||
		(selection.target_geometry->poseverttype != PV_MD5 && selection.target_geometry->poseverttype != PV_MD5_8))
		return false;
	int surface_count = 0;
	for (const aliashdr_t *surface = selection.target_geometry; surface; surface = surface->nextsurface)
		if (++surface_count > MAX_SURFACES ||
			(surface->poseverttype != PV_MD5 && surface->poseverttype != PV_MD5_8) ||
			surface->numjoints != (int)target_skeleton.joint_count)
			return false;
	selection.entity = entity;
	selection.original_model = entity->model;
	selection.source_model = source;
	selection.target_model = target;
	selection.id = id;
	selection.valid = true;
	staged[player - 1] = selection;
	return true;
}

static void R_VRIKRenderReleaseDescriptorSet (uint32_t frame_slot)
{
	if (palette_descriptor_sets[frame_slot] != VK_NULL_HANDLE)
	{
		R_FreeDescriptorSet (palette_descriptor_sets[frame_slot], &vulkan_globals.joints_buffer_set_layout);
		palette_descriptor_sets[frame_slot] = VK_NULL_HANDLE;
	}
}

static void R_VRIKRenderCullCandidate (const entity_t *entity, const aliashdr_t *header,
	uint32_t joint_count, const float (*palette)[12], r_vrik_candidate_t *candidate)
{
	/* Inspect the selected surface chain and the actual solved affine matrices. */
	double max_qmax = 0.0;
	qboolean tracked_cull_valid = true;
	int surface_count = 0;
	for (const aliashdr_t *surface = header; surface; surface = surface->nextsurface)
	{
		if (++surface_count > MAX_SURFACES ||
			(surface->poseverttype != PV_MD5 && surface->poseverttype != PV_MD5_8) ||
			surface->numjoints != (int)joint_count || !surface->tracked_cull_qmax_valid ||
			!isfinite (surface->tracked_cull_qmax) || surface->tracked_cull_qmax < 0.0)
		{
			tracked_cull_valid = false;
			break;
		}
		if (max_qmax < surface->tracked_cull_qmax)
			max_qmax = surface->tracked_cull_qmax;
	}
	if (surface_count > MAX_SURFACES)
		tracked_cull_valid = false;
	double max_rotation_norm = 0.0;
	double max_translation_norm = 0.0;
	for (size_t joint = 0; tracked_cull_valid && joint < joint_count; ++joint)
	{
		const float *matrix = palette[joint];
		double rotation_sum = 0.0;
		double translation_sum = 0.0;
		for (int row = 0; row < 3; ++row)
		{
			for (int column = 0; column < 3; ++column)
			{
				const double value = matrix[row * 4 + column];
				if (!isfinite (value))
				{
					tracked_cull_valid = false;
					break;
				}
				rotation_sum += value * value;
			}
			const double translation = matrix[row * 4 + 3];
			if (!isfinite (translation))
			{
				tracked_cull_valid = false;
				break;
			}
			translation_sum += translation * translation;
		}
		if (!tracked_cull_valid)
			break;
		const double rotation_norm = sqrt (rotation_sum);
		const double translation_norm = sqrt (translation_sum);
		if (!isfinite (rotation_norm) || !isfinite (translation_norm))
		{
			tracked_cull_valid = false;
			break;
		}
		if (max_rotation_norm < rotation_norm)
			max_rotation_norm = rotation_norm;
		if (max_translation_norm < translation_norm)
			max_translation_norm = translation_norm;
	}
	double local_bound = max_qmax * max_rotation_norm + max_translation_norm;
	if (!isfinite (local_bound) || local_bound < 0.0)
		tracked_cull_valid = false;

	vec3_t cull_angles;
	R_GetEntityLerpedTransform (entity, candidate->tracked_cull_origin, cull_angles);
	for (int axis = 0; axis < 3; ++axis)
		if (!isfinite (candidate->tracked_cull_origin[axis]) || !isfinite (cull_angles[axis]))
			tracked_cull_valid = false;
	candidate->tracked_cull_local_bound = local_bound;
	candidate->tracked_cull_valid = tracked_cull_valid;
}

static qboolean R_VRIKRenderCandidate (const entity_t *entity, r_vrik_candidate_t *candidate, float (*palette)[12])
{
	aliashdr_t *header;
	r_vrik_palette_output_t output;
	r_vrik_lowerbody_targets_t lower_targets;
	const r_vrik_lowerbody_targets_t *lower_input = NULL;

	if (!entity || !candidate || !palette)
		return false;
	if (!R_VRIKSampleEntityPose (entity, &candidate->pose))
		return false;
	if (!entity->model || entity->model->needload || entity->model->type != mod_alias)
		return false;
	header = (aliashdr_t *)Mod_Extradata_CheckSkin (entity->model, entity->skinnum);
	if (!header || (header->poseverttype != PV_MD5 && header->poseverttype != PV_MD5_8) ||
		header->numjoints <= 0 || header->numjoints > R_VRIK_RENDER_MAX_JOINTS)
		return false;
	if (!Mod_GetMD5Skeleton (entity->model, &candidate->skeleton) ||
		candidate->skeleton.joint_count != (size_t)header->numjoints)
		return false;

	R_SetupAliasFrame (entity, header, &candidate->lerpdata);
	candidate->muzzleflash = (entity->effects & EF_MUZZLEFLASH) != 0;
	if (R_VRIKSampleEntityLowerTargets (entity, &lower_targets))
		lower_input = &lower_targets;
	output.matrices = palette;
	output.capacity = R_VRIK_RENDER_MAX_JOINTS;
	output.joint_count = 0;
	if (R_VRIKBuildRangerPalette (
			&candidate->skeleton, &candidate->lerpdata, &candidate->pose,
			lower_input, candidate->muzzleflash, &output) != R_VRIK_PALETTE_OK ||
		output.joint_count > UINT32_MAX)
		return false;

	candidate->entity = entity;
	candidate->model = entity->model;
	candidate->geometry = header;
	candidate->joint_count = (uint32_t)output.joint_count;
	candidate->alternate_avatar = false;
	R_VRIKRenderCullCandidate (entity, header, candidate->joint_count, (const float (*)[12])palette, candidate);
	return candidate->joint_count != 0;
}

static qboolean R_VRIKRenderFallbackRunning (const entity_t *entity)
{
	const double dx = (double)entity->msg_origins[0][0] - entity->msg_origins[1][0];
	const double dy = (double)entity->msg_origins[0][1] - entity->msg_origins[1][1];
	return isfinite (dx) && isfinite (dy) && dx * dx + dy * dy > 0.25;
}

static int R_VRIKRenderCanonicalFrame (const entity_t *entity, const aliashdr_t *source)
{
	if (entity->frame >= 0 && entity->frame < source->numframes)
		return entity->frame;
	/* The 2.0 parse-side positions are the corresponding two movement samples
	 * used by the source renderer's out-of-range Ranger frame fallback. */
	const double ticks = cl.time * 10.0;
	const int phase = isfinite (ticks) && ticks >= 0.0 ? (int)fmod (ticks, 30.0) : 0;
	const qboolean running = R_VRIKRenderFallbackRunning (entity);
	const int frame = running ? 6 + phase % 6 : 12 + phase % 5;
	return frame >= 0 && frame < source->numframes ? frame : 0;
}

static qboolean R_VRIKRenderAlternateCandidate (const entity_t *entity,
	const r_vrik_staged_avatar_t *selection, r_vrik_candidate_t *candidate, float (*palette)[12])
{
	md5_skeleton_view_t source_skeleton, target_skeleton;
	r_avatar_rig_t source_rig, target_rig;
	r_vrik_palette_output_t ranger;
	r_vrik_lowerbody_targets_t lower_targets;
	const r_vrik_lowerbody_targets_t *lower_input = NULL;
	vrik_pose_t pose;
	entity_t canonical_entity;
	float source_palette[R_VRIK_RENDER_MAX_JOINTS][12];
	r_vrik_palette_result_t result;
	qboolean tracked;

	if (!selection || !selection->valid || selection->entity != entity ||
		selection->id <= PLAYER_AVATAR_RANGER || !R_VRIKRenderProfileForId (selection->id) ||
		!entity->model || entity->model != selection->original_model || entity->model->needload ||
		strcmp (entity->model->name, "progs/player.mdl") ||
		!selection->source_model || !selection->target_model ||
		selection->source_model->needload || selection->target_model->needload ||
		!selection->source_model->avatar_builtin ||
		!Mod_IsAdmittedAvatarModel (selection->target_model) ||
		(selection->id < PLAYER_AVATAR_COUNT ? !selection->target_model->avatar_builtin :
		 selection->target_model->avatar_custom_id != selection->id) ||
		selection->source_model->extradata[PV_MD5] != (const byte *)selection->source_geometry ||
		selection->target_model->extradata[PV_MD5] != (const byte *)selection->target_geometry ||
		!Mod_GetMD5Skeleton (selection->source_model, &source_skeleton) ||
		!source_skeleton.from_rerelease || !Mod_GetMD5Skeleton (selection->target_model, &target_skeleton) ||
		source_skeleton.joint_count != (size_t)selection->source_geometry->numjoints ||
		target_skeleton.joint_count != (size_t)selection->target_geometry->numjoints ||
		target_skeleton.joint_count > R_VRIK_RENDER_MAX_JOINTS ||
		!R_AvatarResolveRig (R_AvatarProfileForId (PLAYER_AVATAR_RANGER), &source_skeleton, &source_rig) ||
		!R_AvatarResolveRig (R_VRIKRenderProfileForId (selection->id), &target_skeleton, &target_rig))
		return false;

	canonical_entity = *entity;
	canonical_entity.model = selection->source_model;
	canonical_entity.frame = R_VRIKRenderCanonicalFrame (entity, selection->source_geometry);
	if (entity->frame < 0 || entity->frame >= selection->source_geometry->numframes ||
		canonical_entity.lerp.prev_frame < 0 || canonical_entity.lerp.prev_frame >= selection->source_geometry->numframes)
	{
		canonical_entity.lerp.prev_frame = canonical_entity.frame;
		canonical_entity.lerp.frame_change_time = 0;
	}
	R_SetupAliasFrame (&canonical_entity, (aliashdr_t *)selection->source_geometry, &candidate->lerpdata);
	if ((entity->frame < 0 || entity->frame >= selection->source_geometry->numframes) &&
		selection->source_geometry->numframes > 16 && isfinite (cl.time) && cl.time >= 0.0)
	{
		/* An out-of-range mod frame uses the stock Ranger run/stand cycle.
		 * Interpolate adjacent canonical poses without mutating entity lerp
		 * state; otherwise the fallback advances in visible 100 ms steps. */
		const qboolean running = R_VRIKRenderFallbackRunning (entity);
		const int first = running ? 6 : 12;
		const int count = running ? 6 : 5;
		const double phase = fmod (cl.time * 10.0, (double)count);
		if (isfinite (phase) && phase >= 0.0)
		{
			const int current = (int)phase;
			const int next = (current + 1) % count;
			candidate->lerpdata.pose1 = selection->source_geometry->frames[first + current].firstpose;
			candidate->lerpdata.pose2 = selection->source_geometry->frames[first + next].firstpose;
			candidate->lerpdata.blend = (float)(phase - current);
			if (!r_lerpmodels.value ||
				(selection->source_model->flags & MOD_NOLERP && r_lerpmodels.value != 2))
			{
				candidate->lerpdata.pose2 = candidate->lerpdata.pose1;
				candidate->lerpdata.blend = 1.0f;
			}
		}
	}
	ranger.matrices = source_palette;
	ranger.capacity = R_VRIK_RENDER_MAX_JOINTS;
	ranger.joint_count = 0;
	tracked = R_VRIKSampleEntityPose (entity, &pose);
	if (tracked)
	{
		if (R_VRIKSampleEntityLowerTargets (entity, &lower_targets))
			lower_input = &lower_targets;
		result = R_VRIKBuildRangerPalette (&source_skeleton, &candidate->lerpdata, &pose,
			lower_input, (entity->effects & EF_MUZZLEFLASH) != 0, &ranger);
	}
	else
		result = R_VRIKBuildRangerAnimationPalette (&source_skeleton, &candidate->lerpdata, &ranger);
	if (result != R_VRIK_PALETTE_OK ||
		!R_AvatarRetargetRangerOutput (&source_rig, &target_rig, &ranger,
			palette, R_VRIK_RENDER_MAX_JOINTS))
		return false;
	/* Optional tracked repairs retain successful independent stages. The
	 * canonical palette and supplied lower targets remain authoritative. */
	unsigned char tracked_lower_mask = 0;
	if (lower_input)
	{
		const unsigned char usable = lower_input->present_mask &
			(lower_input->tracked_mask | lower_input->predicted_mask);
		if (usable & R_VRIK_LOWER_BIT (R_VRIK_LOWER_LEFT_FOOT))
			tracked_lower_mask |= R_AVATAR_TRACKED_FOOT_L;
		if (usable & R_VRIK_LOWER_BIT (R_VRIK_LOWER_RIGHT_FOOT))
			tracked_lower_mask |= R_AVATAR_TRACKED_FOOT_R;
		if (usable & R_VRIK_LOWER_BIT (R_VRIK_LOWER_HIP))
			tracked_lower_mask |= R_AVATAR_TRACKED_HIP;
	}
	R_AvatarRefineBuiltinPalette (&source_rig, &target_rig, tracked,
		(const float (*)[12])source_palette, floor_correction_z[selection->id],
		tracked_lower_mask, palette, R_VRIK_RENDER_MAX_JOINTS);

	candidate->entity = entity;
	candidate->model = selection->target_model;
	candidate->geometry = selection->target_geometry;
	candidate->joint_count = (uint32_t)target_skeleton.joint_count;
	candidate->alternate_avatar = true;
	memcpy (candidate->target_to_canonical, selection->target_to_canonical,
		sizeof (candidate->target_to_canonical));
	R_VRIKRenderCullCandidate (entity, selection->target_geometry, candidate->joint_count,
		(const float (*)[12])palette, candidate);
	if (candidate->tracked_cull_valid)
	{
		double linear_squared = 0.0;
		double translation_squared = 0.0;
		for (int row = 0; row < 3; ++row)
		{
			for (int column = 0; column < 3; ++column)
			{
				const double value = candidate->target_to_canonical[row * 4 + column];
				linear_squared += value * value;
			}
			const double translation = candidate->target_to_canonical[row * 4 + 3];
			translation_squared += translation * translation;
		}
		/* ||L||_F bounds the spectral norm, including non-unit display scale. */
		const double bound = sqrt (translation_squared) +
			sqrt (linear_squared) * candidate->tracked_cull_local_bound;
		if (isfinite (bound) && bound >= 0.0)
			candidate->tracked_cull_local_bound = bound;
		else
			candidate->tracked_cull_valid = false;
	}
	return candidate->joint_count != 0;
}

void R_VRIKRenderPrepareFrame (uint32_t frame_slot)
{
	size_t candidate_count = 0;
	size_t total_joints = 0;
	VkDeviceSize max_range = vulkan_globals.device_properties.limits.maxStorageBufferRange;

	if (frame_slot >= DOUBLE_BUFFERED)
	{
		active_frame_valid = false;
		return;
	}
	active_frame_slot = frame_slot;
	active_frame_valid = false;
	prepared_count[frame_slot] = 0;
	if (!cl.entities || cl.num_entities <= 1)
	{
		R_VRIKRenderReleaseDescriptorSet (frame_slot);
		active_frame_valid = true;
		return;
	}
	const int maxclients = q_min (MAX_SCOREBOARD, q_min (q_max (0, cl.maxclients), cl.num_entities - 1));

	for (int player = 1; player <= maxclients && player < cl.num_entities; ++player)
	{
		r_vrik_candidate_t candidate;
		if (!R_VRIKRenderAlternateCandidate (&cl.entities[player], &staged[player - 1],
			&candidate, candidate_palettes[candidate_count]) &&
			!R_VRIKRenderCandidate (&cl.entities[player], &candidate, candidate_palettes[candidate_count]))
			continue;
		const VkDeviceSize palette_bytes = (VkDeviceSize)candidate.joint_count * sizeof (float[12]);
		if (palette_bytes > max_range || total_joints > UINT32_MAX - candidate.joint_count ||
			total_joints > max_range / sizeof (float[12]))
			continue;
		const VkDeviceSize used_bytes = (VkDeviceSize)total_joints * sizeof (float[12]);
		if (palette_bytes > max_range - used_bytes)
			continue;
		candidates[candidate_count] = candidate;
		candidate_count++;
		total_joints += candidate.joint_count;
	}

	if (!candidate_count || total_joints == 0)
	{
		R_VRIKRenderReleaseDescriptorSet (frame_slot);
		active_frame_valid = true;
		return;
	}

	const size_t total_bytes = total_joints * sizeof (float[12]);
	if (total_bytes > INT_MAX || total_bytes > max_range)
	{
		R_VRIKRenderReleaseDescriptorSet (frame_slot);
		active_frame_valid = true;
		return;
	}

	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceSize buffer_offset = 0;
	VkDeviceAddress allocation_address = 0;
	byte *storage = R_StorageAllocate ((int)total_bytes, &buffer, &buffer_offset, &allocation_address);
	if (!storage || buffer == VK_NULL_HANDLE)
	{
		R_VRIKRenderReleaseDescriptorSet (frame_slot);
		active_frame_valid = true;
		return;
	}
	const VkDeviceSize storage_alignment = vulkan_globals.device_properties.limits.minStorageBufferOffsetAlignment;
	if (!storage_alignment || buffer_offset % storage_alignment != 0 ||
		(vulkan_globals.ray_query && (!allocation_address || allocation_address > UINT64_MAX - total_bytes)))
	{
		R_VRIKRenderReleaseDescriptorSet (frame_slot);
		active_frame_valid = true;
		return;
	}

	if (palette_descriptor_sets[frame_slot] == VK_NULL_HANDLE)
		palette_descriptor_sets[frame_slot] = R_AllocateDescriptorSet (&vulkan_globals.joints_buffer_set_layout);
	if (palette_descriptor_sets[frame_slot] == VK_NULL_HANDLE)
	{
		R_VRIKRenderReleaseDescriptorSet (frame_slot);
		active_frame_valid = true;
		return;
	}

	size_t joint_cursor = 0;
	for (size_t i = 0; i < candidate_count; ++i)
	{
		r_vrik_candidate_t *candidate = &candidates[i];
		float (*palette)[12] = (float (*)[12])(storage + joint_cursor * sizeof (float[12]));
		memcpy (palette, candidate_palettes[i], candidate->joint_count * sizeof (float[12]));
		r_vrik_prepared_palette_t *record = &prepared[frame_slot][prepared_count[frame_slot]++];
		record->entity = candidate->entity;
		record->model = candidate->model;
		record->geometry = candidate->geometry;
		record->alternate_avatar = candidate->alternate_avatar;
		if (candidate->alternate_avatar)
			memcpy (record->target_to_canonical, candidate->target_to_canonical,
				sizeof (record->target_to_canonical));
		else
			memset (record->target_to_canonical, 0, sizeof (record->target_to_canonical));
		record->descriptor_set = palette_descriptor_sets[frame_slot];
		record->joint_offset = (uint32_t)joint_cursor;
		record->joint_count = candidate->joint_count;
		record->palette_address = vulkan_globals.ray_query ? allocation_address + joint_cursor * sizeof (float[12]) : 0;
		record->tracked_cull_local_bound = candidate->tracked_cull_local_bound;
		VectorCopy (candidate->tracked_cull_origin, record->tracked_cull_origin);
		record->tracked_cull_valid = candidate->tracked_cull_valid;
		joint_cursor += candidate->joint_count;
	}

	if (!prepared_count[frame_slot])
	{
		R_VRIKRenderReleaseDescriptorSet (frame_slot);
		active_frame_valid = true;
		return;
	}

	ZEROED_STRUCT (VkDescriptorBufferInfo, buffer_info);
	buffer_info.buffer = buffer;
	buffer_info.offset = buffer_offset;
	buffer_info.range = total_bytes;

	ZEROED_STRUCT (VkWriteDescriptorSet, descriptor_write);
	descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptor_write.dstSet = palette_descriptor_sets[frame_slot];
	descriptor_write.dstBinding = 0;
	descriptor_write.descriptorCount = 1;
	descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptor_write.pBufferInfo = &buffer_info;
	vkUpdateDescriptorSets (vulkan_globals.device, 1, &descriptor_write, 0, NULL);
	active_frame_valid = true;
}

const r_vrik_prepared_palette_t *R_VRIKRenderLookup (const entity_t *entity)
{
	if (!active_frame_valid || !entity || active_frame_slot >= DOUBLE_BUFFERED)
		return NULL;
	for (size_t i = 0; i < prepared_count[active_frame_slot]; ++i)
		if (prepared[active_frame_slot][i].entity == entity)
			return &prepared[active_frame_slot][i];
	return NULL;
}

void R_VRIKRenderShutdown (void)
{
	R_VRIKRenderResetAdmission ();
	active_frame_valid = false;
	active_frame_slot = 0;
	for (int slot = 0; slot < DOUBLE_BUFFERED; ++slot)
	{
		prepared_count[slot] = 0;
		R_VRIKRenderReleaseDescriptorSet (slot);
	}
}
